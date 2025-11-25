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

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "checkasm.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/macros.h"
#include "libavutil/mem_internal.h"
#include "libavcodec/vp6data.h"
#include "libavcodec/vp56dsp.h"

#define randomize_buffer(buf)                                   \
    do {                                                        \
        for (size_t k = 0; k < (sizeof(buf) & ~3); k += 4)      \
            AV_WN32A(buf + k, rnd());                           \
        for (size_t k = sizeof(buf) & ~3; k < sizeof(buf); ++k) \
            buf[k] = rnd();                                     \
    } while (0)


void checkasm_check_vp6dsp(void)
{
    enum {
        BLOCK_SIZE_1D  = 8,
        SRC_ROWS_ABOVE = 1,
        SRC_ROWS_BELOW = 2,
        SRC_COLS_LEFT  = 1,
        SRC_COLS_RIGHT = 2,
        SRC_ROWS       = SRC_ROWS_ABOVE + BLOCK_SIZE_1D + SRC_ROWS_BELOW,
        SRC_ROW_SIZE   = SRC_COLS_LEFT  + BLOCK_SIZE_1D + SRC_COLS_RIGHT,
        MAX_STRIDE     = 64,    ///< arbitrary
        SRC_BUF_SIZE   = (SRC_ROWS - 1) * MAX_STRIDE + SRC_ROW_SIZE + 7 /* to vary misalignment */,
        DST_BUF_SIZE   = (BLOCK_SIZE_1D - 1) * MAX_STRIDE + BLOCK_SIZE_1D,
    };
    VP6DSPContext vp6dsp;

    ff_vp6dsp_init(&vp6dsp);

    declare_func(void, uint8_t *dst, const uint8_t *src, ptrdiff_t stride,
                       const int16_t *h_weights, const int16_t *v_weights);

    if (check_func(vp6dsp.vp6_filter_diag4, "filter_diag4")) {
        DECLARE_ALIGNED(8, uint8_t, dstbuf_ref)[DST_BUF_SIZE];
        DECLARE_ALIGNED(8, uint8_t, dstbuf_new)[DST_BUF_SIZE];
        DECLARE_ALIGNED(8, uint8_t, srcbuf)[SRC_BUF_SIZE];

        randomize_buffer(dstbuf_ref);
        randomize_buffer(srcbuf);
        memcpy(dstbuf_new, dstbuf_ref, sizeof(dstbuf_new));

        ptrdiff_t  stride = (rnd() % (MAX_STRIDE / 16) + 1) * 16;
        const uint8_t *src = srcbuf + SRC_COLS_LEFT + rnd() % 8U;
        uint8_t *dst_new = dstbuf_new, *dst_ref = dstbuf_ref;

        if (rnd() & 1) {
            dst_new += (BLOCK_SIZE_1D - 1) * stride;
            dst_ref += (BLOCK_SIZE_1D - 1) * stride;
            src     += (SRC_ROWS - 1) * stride;
            stride  *= -1;
        }
        src += SRC_ROWS_ABOVE * stride;

        unsigned select = rnd() % FF_ARRAY_ELEMS(vp6_block_copy_filter);
        unsigned x8 = 1 + rnd() % (FF_ARRAY_ELEMS(vp6_block_copy_filter[0]) - 1);
        unsigned y8 = 1 + rnd() % (FF_ARRAY_ELEMS(vp6_block_copy_filter[0]) - 1);
        const int16_t *h_weights = vp6_block_copy_filter[select][x8];
        const int16_t *v_weights = vp6_block_copy_filter[select][y8];

        call_ref(dst_ref, src, stride, h_weights, v_weights);
        call_new(dst_new, src, stride, h_weights, v_weights);
        if (memcmp(dstbuf_new, dstbuf_ref, sizeof(dstbuf_new)))
            fail();
        bench_new(dst_new, src, stride, h_weights, v_weights);
    }
}
