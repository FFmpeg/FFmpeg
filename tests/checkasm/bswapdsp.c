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
#include "libavcodec/bswapdsp.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#define BUF_SIZE 512

#define randomize_buffers()                 \
    do {                                    \
        int i;                              \
        for (i = 0; i < BUF_SIZE; i += 4) { \
            uint32_t r = rnd();             \
            AV_WN32A(src0 + i, r);          \
            AV_WN32A(src1 + i, r);          \
            r = rnd();                      \
            AV_WN32A(dst0 + i, r);          \
            AV_WN32A(dst1 + i, r);          \
        }                                   \
    } while (0)

#define check_bswap(type)                                                                  \
    do {                                                                                   \
        int w;                                                                             \
        declare_func(void, type *dst, const type *src, int w);                             \
                                                                                           \
        for (w = 0; w < BUF_SIZE / sizeof(type); w++) {                                    \
            int offset = (BUF_SIZE / sizeof(type) - w) & 15; /* Test various alignments */ \
            randomize_buffers();                                                           \
            call_ref((type *)dst0 + offset, (type *)src0 + offset, w);                     \
            call_new((type *)dst1 + offset, (type *)src1 + offset, w);                     \
            if (memcmp(src0, src1, BUF_SIZE) || memcmp(dst0, dst1, BUF_SIZE))              \
                fail();                                                                    \
            bench_new((type *)dst1 + offset, (type *)src1 + offset, w);                    \
        }                                                                                  \
    } while (0)

void checkasm_check_bswapdsp(void)
{
    LOCAL_ALIGNED_16(uint8_t, src0, [BUF_SIZE]);
    LOCAL_ALIGNED_16(uint8_t, src1, [BUF_SIZE]);
    LOCAL_ALIGNED_16(uint8_t, dst0, [BUF_SIZE]);
    LOCAL_ALIGNED_16(uint8_t, dst1, [BUF_SIZE]);
    BswapDSPContext h;

    ff_bswapdsp_init(&h);

    if (check_func(h.bswap_buf, "bswap_buf"))
        check_bswap(uint32_t);

    if (check_func(h.bswap16_buf, "bswap16_buf"))
        check_bswap(uint16_t);

    report("bswap");
}
