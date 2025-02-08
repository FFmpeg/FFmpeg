/*
 * Copyright (c) 2015 Henrik Gramner
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>
#include "checkasm.h"
#include "libavcodec/v210enc_init.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#define BUF_SIZE 512

#define randomize_buffers(mask)                        \
    do {                                               \
        int i, size = sizeof(*y0);                     \
        for (i = 0; i < BUF_SIZE; i += 4 / size) {     \
            uint32_t r = rnd() & mask;                 \
            AV_WN32A(y0 + i, r);                       \
            AV_WN32A(y1 + i, r);                       \
        }                                              \
        for (i = 0; i < BUF_SIZE / 2; i += 4 / size) { \
            uint32_t r = rnd() & mask;                 \
            AV_WN32A(u0 + i, r);                       \
            AV_WN32A(u1 + i, r);                       \
            r = rnd() & mask;                          \
            AV_WN32A(v0 + i, r);                       \
            AV_WN32A(v1 + i, r);                       \
        }                                              \
        for (i = 0; i < width * 8 / 3; i += 4) {       \
            uint32_t r = rnd();                        \
            AV_WN32A(dst0 + i, r);                     \
            AV_WN32A(dst1 + i, r);                     \
        }                                              \
    } while (0)

#define check_pack_line(type, mask)                                                \
    do {                                                                           \
        LOCAL_ALIGNED_16(type, y0, [BUF_SIZE]);                                    \
        LOCAL_ALIGNED_16(type, y1, [BUF_SIZE]);                                    \
        LOCAL_ALIGNED_16(type, u0, [BUF_SIZE / 2]);                                \
        LOCAL_ALIGNED_16(type, u1, [BUF_SIZE / 2]);                                \
        LOCAL_ALIGNED_16(type, v0, [BUF_SIZE / 2]);                                \
        LOCAL_ALIGNED_16(type, v1, [BUF_SIZE / 2]);                                \
        LOCAL_ALIGNED_16(uint8_t, dst0, [BUF_SIZE * 8 / 3]);                       \
        LOCAL_ALIGNED_16(uint8_t, dst1, [BUF_SIZE * 8 / 3]);                       \
                                                                                   \
        declare_func(void, const type * y, const type * u, const type * v,         \
                     uint8_t * dst, ptrdiff_t width);                              \
        ptrdiff_t width, step = 12 / sizeof(type);                                 \
                                                                                   \
        for (width = step; width < BUF_SIZE - 15; width += step) {                 \
            int y_offset  = rnd() & 15;                                            \
            int uv_offset = y_offset / 2;                                          \
            randomize_buffers(mask);                                               \
            call_ref(y0 + y_offset, u0 + uv_offset, v0 + uv_offset, dst0, width);  \
            call_new(y1 + y_offset, u1 + uv_offset, v1 + uv_offset, dst1, width);  \
            checkasm_check(type,    y0,   0, y1,   0, BUF_SIZE,      1, "y");      \
            checkasm_check(type,    u0,   0, u1,   0, BUF_SIZE / 2,  1, "u");      \
            checkasm_check(type,    v0,   0, v1,   0, BUF_SIZE / 2,  1, "v");      \
            checkasm_check(uint8_t, dst0, 0, dst1, 0, width * 8 / 3, 1, "dst");    \
            bench_new(y1 + y_offset, u1 + uv_offset, v1 + uv_offset, dst1, width); \
        }                                                                          \
    } while (0)

void checkasm_check_v210enc(void)
{
    V210EncContext h;

    ff_v210enc_init(&h);

    if (check_func(h.pack_line_8, "v210_planar_pack_8"))
        check_pack_line(uint8_t, 0xffffffff);

    if (check_func(h.pack_line_10, "v210_planar_pack_10"))
        check_pack_line(uint16_t, 0x03ff03ff);

    report("planar_pack");
}
