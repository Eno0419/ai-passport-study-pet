#ifndef PASSPORT_DISPLAY_ROUNDING_H_
#define PASSPORT_DISPLAY_ROUNDING_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Computes the inclusive horizontal visible pixel range [x1, x2] for scanline y
// inside a rounded rectangle of size (width x height) and corner radius.
// Matches FoloToy/ai-passport bsp_display_rounding.c.
bool passport_display_rounded_row_span(int32_t y, int32_t width, int32_t height,
                                       int32_t radius, int32_t* x1, int32_t* x2);

bool passport_display_pixel_outside_rounded_rect(int32_t x, int32_t y,
                                                 int32_t width, int32_t height,
                                                 int32_t radius);

#ifdef __cplusplus
}
#endif

#endif  // PASSPORT_DISPLAY_ROUNDING_H_
