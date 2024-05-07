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

#include "libavcodec/blockdsp.h"

#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#define randomize_buffers(size)             \
    do {                                    \
        int i;                              \
        for (i = 0; i < size; i++) {        \
            uint16_t r = rnd();             \
            AV_WN16A(buf0 + i, r);          \
            AV_WN16A(buf1 + i, r);          \
        }                                   \
    } while (0)

#define check_clear(func, size)                                     \
do {                                                                \
    if (check_func(h.func, "blockdsp." #func)) {                    \
        declare_func(void, int16_t *block);                         \
        randomize_buffers(size);                                    \
        call_ref(buf0);                                             \
        call_new(buf1);                                             \
        if (memcmp(buf0, buf1, sizeof(*buf0) * size))               \
            fail();                                                 \
        bench_new(buf0);                                            \
    }                                                               \
} while (0)

static void check_fill(BlockDSPContext *h){
    LOCAL_ALIGNED_16(uint8_t, buf0_16, [16 * 16]);
    LOCAL_ALIGNED_16(uint8_t, buf1_16, [16 * 16]);

    for (int t = 0; t < 2; ++t) {
        uint8_t *buf0 = buf0_16 + t * /* force 8 byte alignment */ 8;
        uint8_t *buf1 = buf1_16 + t * /* force 8 byte alignment */ 8;
        int n = 16 - 8 * t;
        declare_func(void, uint8_t *block, uint8_t value,
                     ptrdiff_t line_size, int h);
        if (check_func(h->fill_block_tab[t], "blockdsp.fill_block_tab[%d]", t)) {
            uint8_t value = rnd();
            memset(buf0, 0, sizeof(*buf0) * n * n);
            memset(buf1, 0, sizeof(*buf1) * n * n);
            call_ref(buf0, value, n, n);
            call_new(buf1, value, n, n);
            if (memcmp(buf0, buf1, sizeof(*buf0) * n * n))
                fail();
            bench_new(buf0, value, n, n);
        }
    }
}

void checkasm_check_blockdsp(void)
{
    LOCAL_ALIGNED_32(uint16_t, buf0, [6 * 8 * 8]);
    LOCAL_ALIGNED_32(uint16_t, buf1, [6 * 8 * 8]);

    BlockDSPContext h;

    ff_blockdsp_init(&h);

    check_clear(clear_block,  8 * 8);
    check_clear(clear_blocks, 8 * 8 * 6);

    check_fill(&h);

    report("blockdsp");
}
