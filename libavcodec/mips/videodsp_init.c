/*
 * Copyright (c) 2017 Kaustubh Raste (kaustubh.raste@imgtec.com)
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

#include "libavutil/mips/cpu.h"
#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/mips/asmdefs.h"
#include "libavcodec/videodsp.h"

static void prefetch_mips(uint8_t *mem, ptrdiff_t stride, int h)
{
    register const uint8_t *p = mem;

    __asm__ volatile (
        "1:                                     \n\t"
        "pref          4,  0(%[p])              \n\t"
        "pref          4,  32(%[p])             \n\t"
        PTR_ADDIU"  %[h],  %[h],     -1         \n\t"
        PTR_ADDU "  %[p],  %[p],     %[stride]  \n\t"

        "bnez       %[h],  1b                   \n\t"

        : [p] "+r" (p), [h] "+r" (h)
        : [stride] "r" (stride)
    );
}

av_cold void ff_videodsp_init_mips(VideoDSPContext *ctx, int bpc)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_msa(cpu_flags))
        ctx->prefetch = prefetch_mips;
}
