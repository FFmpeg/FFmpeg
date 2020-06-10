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
#include "libavcodec/h264qpel.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

static const uint32_t pixel_mask[3] = { 0xffffffff, 0x01ff01ff, 0x03ff03ff };

#define SIZEOF_PIXEL ((bit_depth + 7) / 8)
#define BUF_SIZE (2 * 16 * (16 + 3 + 4))

#define randomize_buffers()                        \
    do {                                           \
        uint32_t mask = pixel_mask[bit_depth - 8]; \
        int k;                                     \
        for (k = 0; k < BUF_SIZE; k += 4) {        \
            uint32_t r = rnd() & mask;             \
            AV_WN32A(buf0 + k, r);                 \
            AV_WN32A(buf1 + k, r);                 \
            r = rnd();                             \
            AV_WN32A(dst0 + k, r);                 \
            AV_WN32A(dst1 + k, r);                 \
        }                                          \
    } while (0)

#define src0 (buf0 + 3 * 2 * 16) /* h264qpel functions read data from negative src pointer offsets */
#define src1 (buf1 + 3 * 2 * 16)

void checkasm_check_h264qpel(void)
{
    LOCAL_ALIGNED_16(uint8_t, buf0, [BUF_SIZE]);
    LOCAL_ALIGNED_16(uint8_t, buf1, [BUF_SIZE]);
    LOCAL_ALIGNED_16(uint8_t, dst0, [BUF_SIZE]);
    LOCAL_ALIGNED_16(uint8_t, dst1, [BUF_SIZE]);
    H264QpelContext h;
    int op, bit_depth, i, j;
    declare_func_emms(AV_CPU_FLAG_MMX | AV_CPU_FLAG_MMXEXT, void, uint8_t *dst, const uint8_t *src, ptrdiff_t stride);

    for (op = 0; op < 2; op++) {
        qpel_mc_func (*tab)[16] = op ? h.avg_h264_qpel_pixels_tab : h.put_h264_qpel_pixels_tab;
        const char *op_name = op ? "avg" : "put";

        for (bit_depth = 8; bit_depth <= 10; bit_depth++) {
            ff_h264qpel_init(&h, bit_depth);
            for (i = 0; i < (op ? 3 : 4); i++) {
                int size = 16 >> i;
                for (j = 0; j < 16; j++)
                    if (check_func(tab[i][j], "%s_h264_qpel_%d_mc%d%d_%d", op_name, size, j & 3, j >> 2, bit_depth)) {
                        randomize_buffers();
                        call_ref(dst0, src0, size * SIZEOF_PIXEL);
                        call_new(dst1, src1, size * SIZEOF_PIXEL);
                        if (memcmp(buf0, buf1, BUF_SIZE) || memcmp(dst0, dst1, BUF_SIZE))
                            fail();
                        bench_new(dst1, src1, size * SIZEOF_PIXEL);
                    }
            }
        }
        report("%s", op_name);
    }
}
