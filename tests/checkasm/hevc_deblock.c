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

#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/hevcdsp.h"

#include "checkasm.h"

static const uint32_t pixel_mask[3] = { 0xffffffff, 0x03ff03ff, 0x0fff0fff };

#define SIZEOF_PIXEL ((bit_depth + 7) / 8)
#define BUF_STRIDE (8 * 2)
#define BUF_LINES (8)
#define BUF_OFFSET (BUF_STRIDE * BUF_LINES)
#define BUF_SIZE (BUF_STRIDE * BUF_LINES + BUF_OFFSET * 2)

#define randomize_buffers(buf0, buf1, size)                 \
    do {                                                    \
        uint32_t mask = pixel_mask[(bit_depth - 8) >> 1];   \
        int k;                                              \
        for (k = 0; k < size; k += 4) {                     \
            uint32_t r = rnd() & mask;                      \
            AV_WN32A(buf0 + k, r);                          \
            AV_WN32A(buf1 + k, r);                          \
        }                                                   \
    } while (0)

static void check_deblock_chroma(HEVCDSPContext *h, int bit_depth)
{
    int32_t tc[2] = { 0, 0 };
    // no_p, no_q can only be { 0,0 } for the simpler assembly (non *_c
    // variant) functions, see deblocking_filter_CTB() in hevc_filter.c
    uint8_t no_p[2] = { 0, 0 };
    uint8_t no_q[2] = { 0, 0 };
    LOCAL_ALIGNED_32(uint8_t, buf0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, buf1, [BUF_SIZE]);

    declare_func(void, uint8_t *pix, ptrdiff_t stride, int32_t *tc, uint8_t *no_p, uint8_t *no_q);

    if (check_func(h->hevc_h_loop_filter_chroma, "hevc_h_loop_filter_chroma%d", bit_depth)) {
        for (int i = 0; i < 4; i++) {
            randomize_buffers(buf0, buf1, BUF_SIZE);
            // see betatable[] in hevc_filter.c
            tc[0] = (rnd() & 63) + (rnd() & 1);
            tc[1] = (rnd() & 63) + (rnd() & 1);

            call_ref(buf0 + BUF_OFFSET, BUF_STRIDE, tc, no_p, no_q);
            call_new(buf1 + BUF_OFFSET, BUF_STRIDE, tc, no_p, no_q);
            if (memcmp(buf0, buf1, BUF_SIZE))
                fail();
        }
        bench_new(buf1 + BUF_OFFSET, BUF_STRIDE, tc, no_p, no_q);
    }

    if (check_func(h->hevc_v_loop_filter_chroma, "hevc_v_loop_filter_chroma%d", bit_depth)) {
        for (int i = 0; i < 4; i++) {
            randomize_buffers(buf0, buf1, BUF_SIZE);
            // see betatable[] in hevc_filter.c
            tc[0] = (rnd() & 63) + (rnd() & 1);
            tc[1] = (rnd() & 63) + (rnd() & 1);

            call_ref(buf0 + BUF_OFFSET, BUF_STRIDE, tc, no_p, no_q);
            call_new(buf1 + BUF_OFFSET, BUF_STRIDE, tc, no_p, no_q);
            if (memcmp(buf0, buf1, BUF_SIZE))
                fail();
        }
        bench_new(buf1 + BUF_OFFSET, BUF_STRIDE, tc, no_p, no_q);
    }
}

void checkasm_check_hevc_deblock(void)
{
    int bit_depth;

    for (bit_depth = 8; bit_depth <= 12; bit_depth += 2) {
        HEVCDSPContext h;
        ff_hevc_dsp_init(&h, bit_depth);
        check_deblock_chroma(&h, bit_depth);
    }
    report("chroma");
}
