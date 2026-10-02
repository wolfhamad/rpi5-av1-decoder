#include "av1_decoder.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>

static int ensure_directory(const char *path) {
    struct stat st = {0};
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0;
        }
        fprintf(stderr, "Output path exists and is not a directory: %s\n", path);
        return -1;
    }

    if (mkdir(path, 0755) != 0) {
        fprintf(stderr, "Failed to create output directory '%s': %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

static void print_stream_info(AVCodecParameters *par, int index) {
    const char *codec_name = avcodec_get_name(par->codec_id);
    printf("stream[%d] codec=%s width=%d height=%d pix_fmt=%d bit_rate=%" PRId64 "\n",
           index,
           codec_name ? codec_name : "unknown",
           par->width,
           par->height,
           par->format,
           par->bit_rate);
}

int decode_av1_file(const char *input_path, const char *output_dir, decode_stats_t *stats) {
    if (!input_path || !stats) {
        return -1;
    }

    memset(stats, 0, sizeof(*stats));

    av_log_set_level(AV_LOG_ERROR);

    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;
    const AVCodec *codec = NULL;
    AVFrame *frame = NULL;
    AVPacket *pkt = NULL;
    int video_stream_index = -1;
    int ret = 0;
    int64_t start_time_ns = 0;
    int64_t end_time_ns = 0;

    if (output_dir && ensure_directory(output_dir) != 0) {
        return -1;
    }

    ret = avformat_open_input(&fmt_ctx, input_path, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "Failed to open input '%s'\n", input_path);
        return -1;
    }

    ret = avformat_find_stream_info(fmt_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Failed to find stream info for '%s'\n", input_path);
        goto cleanup;
    }

    for (unsigned int i = 0; i < fmt_ctx->nb_streams; ++i) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = (int)i;
            print_stream_info(fmt_ctx->streams[i]->codecpar, i);
            break;
        }
    }

    if (video_stream_index < 0) {
        fprintf(stderr, "No video stream found in '%s'\n", input_path);
        ret = -1;
        goto cleanup;
    }

    AVCodecParameters *par = fmt_ctx->streams[video_stream_index]->codecpar;
    if (par->codec_id != AV_CODEC_ID_AV1) {
        fprintf(stderr, "Input codec is not AV1: %s\n", avcodec_get_name(par->codec_id));
        ret = -1;
        goto cleanup;
    }

    codec = avcodec_find_decoder(par->codec_id);
    if (!codec) {
        fprintf(stderr, "No AV1 decoder available in this FFmpeg build. Install libdav1d / FFmpeg AV1 support.\n");
        ret = -1;
        goto cleanup;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        fprintf(stderr, "Failed to allocate codec context\n");
        ret = -1;
        goto cleanup;
    }

    ret = avcodec_parameters_to_context(codec_ctx, par);
    if (ret < 0) {
        fprintf(stderr, "Failed to copy codec parameters\n");
        goto cleanup;
    }

    codec_ctx->thread_count = 0;
    ret = avcodec_open2(codec_ctx, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Failed to open AV1 decoder\n");
        goto cleanup;
    }

    frame = av_frame_alloc();
    pkt = av_packet_alloc();
    if (!frame || !pkt) {
        fprintf(stderr, "Failed to allocate frame / packet\n");
        ret = -1;
        goto cleanup;
    }

    start_time_ns = av_gettime_relative();

    while ((ret = av_read_frame(fmt_ctx, pkt)) >= 0) {
        if (pkt->stream_index != video_stream_index) {
            av_packet_unref(pkt);
            continue;
        }

        ret = avcodec_send_packet(codec_ctx, pkt);
        av_packet_unref(pkt);
        if (ret < 0) {
            fprintf(stderr, "Error sending packet to decoder\n");
            goto cleanup;
        }

        while (1) {
            ret = avcodec_receive_frame(codec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                fprintf(stderr, "Error receiving frame from decoder\n");
                goto cleanup;
            }

            stats->frame_count += 1;
            stats->width = frame->width;
            stats->height = frame->height;
            stats->bit_depth = av_pix_fmt_desc_get(frame->format)->comp[0].depth;

            if (output_dir) {
                char frame_path[512];
                snprintf(frame_path, sizeof(frame_path), "%s/frame_%05d.yuv", output_dir, stats->frame_count);

                int size = av_image_get_buffer_size(frame->format, frame->width, frame->height, 1);
                if (size < 0) {
                    fprintf(stderr, "Could not determine image buffer size\n");
                    continue;
                }

                stats->total_bytes += (uint64_t)size;

                FILE *fp = fopen(frame_path, "wb");
                if (!fp) {
                    fprintf(stderr, "Failed to open output file '%s'\n", frame_path);
                    continue;
                }

                uint8_t *buf = av_malloc(size);
                if (!buf) {
                    fclose(fp);
                    continue;
                }

                ret = av_image_copy_to_buffer(buf, size, (const uint8_t * const *)frame->data, frame->linesize,
                                              frame->format, frame->width, frame->height, 1);
                if (ret >= 0) {
                    fwrite(buf, 1, (size_t)ret, fp);
                }

                av_free(buf);
                fclose(fp);
            }

            av_frame_unref(frame);
        }
    }

    if (ret == AVERROR_EOF) {
        ret = 0;
    }

    end_time_ns = av_gettime_relative();
    stats->decode_ms = (double)(end_time_ns - start_time_ns) / 1000000.0;

cleanup:
    if (frame) {
        av_frame_free(&frame);
    }
    if (pkt) {
        av_packet_free(&pkt);
    }
    if (codec_ctx) {
        avcodec_free_context(&codec_ctx);
    }
    if (fmt_ctx) {
        avformat_close_input(&fmt_ctx);
    }

    return ret;
}
