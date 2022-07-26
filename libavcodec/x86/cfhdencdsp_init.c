/*
 * Copyright (c) 2021 Paul B Mahol
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

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/cfhdencdsp.h"

void ff_cfhdenc_horiz_filter_sse2(const int16_t *input, int16_t *low, int16_t *high,
                                  ptrdiff_t in_stride, ptrdiff_t low_stride,
                                  ptrdiff_t high_stride,
                                  int width, int height);
void ff_cfhdenc_vert_filter_sse2(const int16_t *input, int16_t *low, int16_t *high,
                                 ptrdiff_t in_stride, ptrdiff_t low_stride,
                                 ptrdiff_t high_stride,
                                 int width, int height);

av_cold void ff_cfhdencdsp_init_x86(CFHDEncDSPContext *c)
{
    int cpu_flags = av_get_cpu_flags();

#if ARCH_X86_64
    if (EXTERNAL_SSE2(cpu_flags)) {
        c->horiz_filter = ff_cfhdenc_horiz_filter_sse2;
        c->vert_filter = ff_cfhdenc_vert_filter_sse2;
    }
#endif
}
