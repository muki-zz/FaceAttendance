#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *buf;
    uint16_t w;
    uint16_t h;
    uint32_t len;
    int64_t captured_us;
} camera_frame_t;

void camera_frame_release(camera_frame_t *frame);

#ifdef __cplusplus
}
#endif
