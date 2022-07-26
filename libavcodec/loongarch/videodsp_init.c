/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 * Contributed by Xiwei Gu <guxiwei-hf@loongson.cn>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavcodec/videodsp.h"
#include "libavutil/attributes.h"

static void prefetch_loongarch(const uint8_t *mem, ptrdiff_t stride, int h)
{
    register const uint8_t *p = mem;

    __asm__ volatile (
        "1:                                     \n\t"
        "preld      0,     %[p],     0          \n\t"
        "preld      0,     %[p],     32         \n\t"
        "addi.d     %[h],  %[h],     -1         \n\t"
        "add.d      %[p],  %[p],     %[stride]  \n\t"

        "blt        $r0,   %[h],     1b         \n\t"
        : [p] "+r" (p), [h] "+r" (h)
        : [stride] "r" (stride)
    );
}

av_cold void ff_videodsp_init_loongarch(VideoDSPContext *ctx, int bpc)
{
    ctx->prefetch = prefetch_loongarch;
}
