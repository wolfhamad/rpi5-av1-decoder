#ifndef AV1_DECODER_H
#define AV1_DECODER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int width;
    int height;
    int frame_count;
    int bit_depth;
    uint64_t total_bytes;
    double decode_ms;
} decode_stats_t;

int decode_av1_file(const char *input_path, const char *output_dir, decode_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif
