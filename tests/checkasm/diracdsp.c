/*
 * Copyright (c) 2024 Kyosuke Kawakami
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

#include "checkasm.h"

#include "libavcodec/diracdsp.h"

#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#define RANDOMIZE_DESTS(name, size)             \
    do {                                        \
        int i;                                  \
        for (i = 0; i < size; ++i) {            \
            uint16_t r = rnd();                 \
            AV_WN16A(name##0 + i, r);           \
            AV_WN16A(name##1 + i, r);           \
        }                                       \
    } while (0)

#define RANDOMIZE_BUFFER8(name, size)         \
    do {                                      \
        int i;                                \
        for (i = 0; i < size; ++i) {          \
            uint8_t r = rnd();                \
            name[i] = r;                      \
        }                                     \
    } while (0)

#define OBMC_STRIDE 32
#define XBLEN_MAX 32
#define YBLEN_MAX 64

static void check_add_obmc(size_t func_index, int xblen)
{
    LOCAL_ALIGNED_16(uint8_t, src, [XBLEN_MAX * YBLEN_MAX]);
    LOCAL_ALIGNED_16(uint16_t, _dst0, [XBLEN_MAX * YBLEN_MAX + 4]);
    LOCAL_ALIGNED_16(uint16_t, _dst1, [XBLEN_MAX * YBLEN_MAX + 4]);
    LOCAL_ALIGNED_16(uint8_t, obmc_weight, [XBLEN_MAX * YBLEN_MAX]);

    // Ensure that they accept unaligned buffer.
    // Not using LOCAL_ALIGNED_8 because it might make 16 byte aligned buffer.
    uint16_t *dst0 = _dst0 + 4;
    uint16_t *dst1 = _dst1 + 4;

    int yblen;
    DiracDSPContext h;

    ff_diracdsp_init(&h);

    if (check_func(h.add_dirac_obmc[func_index], "diracdsp.add_dirac_obmc_%d", xblen)) {
        declare_func(void, uint16_t*, const uint8_t*, int, const uint8_t *, int);

        RANDOMIZE_BUFFER8(src, YBLEN_MAX * xblen);
        RANDOMIZE_DESTS(dst, YBLEN_MAX * xblen);
        RANDOMIZE_BUFFER8(obmc_weight, YBLEN_MAX * OBMC_STRIDE);

        yblen = 1 + (rnd() % YBLEN_MAX);
        call_ref(dst0, src, xblen, obmc_weight, yblen);
        call_new(dst1, src, xblen, obmc_weight, yblen);
        if (memcmp(dst0, dst1, yblen * xblen))
            fail();

        bench_new(dst1, src, xblen, obmc_weight, YBLEN_MAX);
    }
}

void checkasm_check_diracdsp(void)
{
    check_add_obmc(0, 8);
    check_add_obmc(1, 16);
    check_add_obmc(2, 32);
    report("diracdsp");
}
