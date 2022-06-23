/*
 * Copyright (c) 2016 Alexandra Hájková
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

#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#include "libavcodec/hevcdsp.h"

#include "checkasm.h"

#define randomize_buffers(buf, size)            \
    do {                                        \
        int j;                                  \
        for (j = 0; j < size; j++) {            \
            int16_t r = rnd();                  \
            AV_WN16A(buf + j, r >> 3);          \
        }                                       \
    } while (0)

#define randomize_buffers2(buf, size, mask)       \
    do {                                          \
        int j;                                    \
        for (j = 0; j < size; j++)                \
            AV_WN16A(buf + j * 2, rnd() & mask); \
    } while (0)

static void compare_add_res(int size, ptrdiff_t stride, int overflow_test, int mask)
{
    LOCAL_ALIGNED_32(int16_t, res0, [32 * 32]);
    LOCAL_ALIGNED_32(int16_t, res1, [32 * 32]);
    LOCAL_ALIGNED_32(uint8_t, dst0, [32 * 32 * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst1, [32 * 32 * 2]);

    declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t *dst, int16_t *res, ptrdiff_t stride);

    randomize_buffers(res0, size);
    randomize_buffers2(dst0, size, mask);
    if (overflow_test)
        res0[0] = 0x8000;
    memcpy(res1, res0, sizeof(*res0) * size);
    memcpy(dst1, dst0, sizeof(int16_t) * size);

    call_ref(dst0, res0, stride);
    call_new(dst1, res1, stride);
    if (memcmp(dst0, dst1, size))
        fail();
    bench_new(dst1, res1, stride);
}

static void check_add_res(HEVCDSPContext h, int bit_depth)
{
    int i;
    int mask = bit_depth == 8 ? 0xFFFF : bit_depth == 10 ? 0x03FF : 0x07FF;

    for (i = 2; i <= 5; i++) {
        int block_size = 1 << i;
        int size = block_size * block_size;
        ptrdiff_t stride = block_size << (bit_depth > 8);

        if (check_func(h.add_residual[i - 2], "hevc_add_res_%dx%d_%d", block_size, block_size, bit_depth)) {
            compare_add_res(size, stride, 0, mask);
            // overflow test for res = -32768
            compare_add_res(size, stride, 1, mask);
        }
    }
}

void checkasm_check_hevc_add_res(void)
{
    int bit_depth;

    for (bit_depth = 8; bit_depth <= 12; bit_depth++) {
        HEVCDSPContext h;

        ff_hevc_dsp_init(&h, bit_depth);
        check_add_res(h, bit_depth);
    }
    report("add_residual");
}
