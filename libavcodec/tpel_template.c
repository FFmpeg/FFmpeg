/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stddef.h>
#include <stdint.h>

#include "libavutil/intreadwrite.h"
#include "pixels.h"
#include "rnd_avg.h"

#include "bit_depth_template.c"

#define DEF_TPEL(OPNAME, OP)                                            \
static inline void FUNCC(OPNAME ## _pixels2)(uint8_t *block,            \
                                             const uint8_t *pixels,     \
                                             ptrdiff_t line_size,       \
                                             int h)                     \
{                                                                       \
    int i;                                                              \
    for (i = 0; i < h; i++) {                                           \
        OP(*((pixel2 *) block), AV_RN2P(pixels));                       \
        pixels += line_size;                                            \
        block  += line_size;                                            \
    }                                                                   \
}                                                                       \
                                                                        \
static inline void FUNCC(OPNAME ## _pixels4)(uint8_t *block,            \
                                             const uint8_t *pixels,     \
                                             ptrdiff_t line_size,       \
                                             int h)                     \
{                                                                       \
    int i;                                                              \
    for (i = 0; i < h; i++) {                                           \
        OP(*((pixel4 *) block), AV_RN4P(pixels));                       \
        pixels += line_size;                                            \
        block  += line_size;                                            \
    }                                                                   \
}                                                                       \
                                                                        \
static inline void FUNCC(OPNAME ## _pixels8)(uint8_t *block,            \
                                             const uint8_t *pixels,     \
                                             ptrdiff_t line_size,       \
                                             int h)                     \
{                                                                       \
    int i;                                                              \
    for (i = 0; i < h; i++) {                                           \
        OP(*((pixel4 *) block), AV_RN4P(pixels));                       \
        OP(*((pixel4 *) (block + 4 * sizeof(pixel))),                   \
           AV_RN4P(pixels + 4 * sizeof(pixel)));                        \
        pixels += line_size;                                            \
        block  += line_size;                                            \
    }                                                                   \
}                                                                       \
                                                                        \
CALL_2X_PIXELS(FUNCC(OPNAME ## _pixels16),                              \
               FUNCC(OPNAME ## _pixels8),                               \
               8 * sizeof(pixel))

#define op_avg(a, b) a = rnd_avg_pixel4(a, b)
#define op_put(a, b) a = b

DEF_TPEL(avg, op_avg)
DEF_TPEL(put, op_put)
#undef op_avg
#undef op_put
