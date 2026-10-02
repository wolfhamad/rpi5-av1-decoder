#include "av1_decoder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void print_usage(const char *argv0) {
    fprintf(stderr,
            "Usage: %s <input.av1> [output_dir]\n"
            "\n"
            "CPU-only AV1 decode benchmarking for Raspberry Pi 5 (BCM2712)\n"
            "This project is intentionally focused on software decode because the\n"
            "VideoCore VII path has no public AV1 hardware decode support.\n"
            "\n",
            argv0);
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        print_usage(argv[0]);
        return 1;
    }

    const char *input_path = argv[1];
    const char *output_dir = (argc == 3) ? argv[2] : "./output";

    decode_stats_t stats = {0};

    printf("Raspberry Pi 5 AV1 decoder (CPU path)\n");
    printf("Input: %s\n", input_path);
    printf("Output: %s\n", output_dir);

    int ret = decode_av1_file(input_path, output_dir, &stats);
    if (ret < 0) {
        fprintf(stderr, "Decode failed\n");
        return 2;
    }

    printf("Decode finished successfully\n");
    printf("Frames decoded: %d\n", stats.frame_count);
    printf("Resolution: %dx%d\n", stats.width, stats.height);
    printf("Bit depth: %d\n", stats.bit_depth);
    printf("Decode time: %.2f ms\n", stats.decode_ms);
    printf("Bytes emitted (decoded image payload): %