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

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"

#include "libavcodec/lossless_videodsp.h"

#include "checkasm.h"

#define randomize_buffers(buf, size)     \
    do {                                 \
        int j;                           \
        for (j = 0; j < size; j++)       \
            buf[j] = rnd() & 0xFF;       \
    } while (0)

static void check_add_bytes(LLVidDSPContext c, int width)
{
    uint8_t *src0 = av_mallocz(width);
    uint8_t *src1 = av_mallocz(width);
    uint8_t *dst0 = av_mallocz(width);
    uint8_t *dst1 = av_mallocz(width);
    declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t *dst, uint8_t *src, ptrdiff_t w);

    if (!src0 || !src1 || !dst0 || !dst1)
        fail();

    randomize_buffers(src0, width);
    memcpy(src1, src0, width);

    if (check_func(c.add_bytes, "add_bytes")) {
        call_ref(dst0, src0, width);
        call_new(dst1, src1, width);
        if (memcmp(dst0, dst1, width))
            fail();
        bench_new(dst1, src1, width);
    }

    av_free(src0);
    av_free(src1);
    av_free(dst0);
    av_free(dst1);
}

void checkasm_check_llviddsp(void)
{
    LLVidDSPContext c;
    int width = 16 * av_clip(rnd(), 16, 128);

    ff_llviddsp_init(&c);

    check_add_bytes(c, width);

    report("add_bytes");
}
