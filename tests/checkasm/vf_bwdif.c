/*
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
#include "libavcodec/internal.h"
#include "libavfilter/bwdif.h"

#define WIDTH 256

#define randomize_buffers(buf0, buf1, mask, count) \
    for (size_t i = 0; i < count; i++) \
        buf0[i] = buf1[i] = rnd() & mask

#define BODY(type, depth)                                                      \
    do {                                                                       \
        type prev0[9*WIDTH], prev1[9*WIDTH];                                   \
        type next0[9*WIDTH], next1[9*WIDTH];                                   \
        type cur0[9*WIDTH], cur1[9*WIDTH];                                     \
        type dst0[WIDTH], dst1[WIDTH];                                         \
        const int stride = WIDTH;                                              \
        const int mask = (1<<depth)-1;                                         \
                                                                               \
        declare_func(void, void *dst, void *prev, void *cur, void *next,       \
                        int w, int prefs, int mrefs, int prefs2, int mrefs2,   \
                        int prefs3, int mrefs3, int prefs4, int mrefs4,        \
                        int parity, int clip_max);                             \
                                                                               \
        randomize_buffers(prev0, prev1, mask, 9*WIDTH);                        \
        randomize_buffers(next0, next1, mask, 9*WIDTH);                        \
        randomize_buffers( cur0,  cur1, mask, 9*WIDTH);                        \
                                                                               \
        call_ref(dst0, prev0 + 4*WIDTH, cur0 + 4*WIDTH, next0 + 4*WIDTH,       \
                WIDTH, stride, -stride, 2*stride, -2*stride,                   \
                3*stride, -3*stride, 4*stride, -4*stride,                      \
                0, mask);                                                      \
        call_new(dst1, prev1 + 4*WIDTH, cur1 + 4*WIDTH, next1 + 4*WIDTH,       \
                WIDTH, stride, -stride, 2*stride, -2*stride,                   \
                3*stride, -3*stride, 4*stride, -4*stride,                      \
                0, mask);                                                      \
                                                                               \
        if (memcmp(dst0, dst1, sizeof dst0)                                    \
                || memcmp(prev0, prev1, sizeof prev0)                          \
                || memcmp(next0, next1, sizeof next0)                          \
                || memcmp( cur0,  cur1, sizeof cur0))                          \
            fail();                                                            \
        bench_new(dst1, prev1 + 4*WIDTH, cur1 + 4*WIDTH, next1 + 4*WIDTH,      \
                WIDTH, stride, -stride, 2*stride, -2*stride,                   \
                3*stride, -3*stride, 4*stride, -4*stride,                      \
                0, mask);                                                      \
    } while (0)

void checkasm_check_vf_bwdif(void)
{
    BWDIFContext ctx_8, ctx_10;

    ff_bwdif_init_filter_line(&ctx_8, 8);
    ff_bwdif_init_filter_line(&ctx_10, 10);

    if (check_func(ctx_8.filter_line, "bwdif8")) {
        BODY(uint8_t, 8);
        report("bwdif8");
    }

    if (check_func(ctx_10.filter_line, "bwdif10")) {
        BODY(uint16_t, 10);
        report("bwdif10");
    }
}
