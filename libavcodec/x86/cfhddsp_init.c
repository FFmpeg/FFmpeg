/*
 * Copyright (c) 2020 Paul B Mahol
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
#include "libavcodec/cfhddsp.h"

void ff_cfhd_horiz_filter_sse2(int16_t *output, ptrdiff_t out_stride,
                               const int16_t *low, ptrdiff_t low_stride,
                               const int16_t *high, ptrdiff_t high_stride,
                               int width, int height);
void ff_cfhd_vert_filter_sse2(int16_t *output, ptrdiff_t out_stride,
                              const int16_t *low, ptrdiff_t low_stride,
                              const int16_t *high, ptrdiff_t high_stride,
                              int width, int height);
void ff_cfhd_horiz_filter_clip10_sse2(int16_t *output, const int16_t *low, const int16_t *high, int width, int bpc);
void ff_cfhd_horiz_filter_clip12_sse2(int16_t *output, const int16_t *low, const int16_t *high, int width, int bpc);

av_cold void ff_cfhddsp_init_x86(CFHDDSPContext *c, int depth, int bayer)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE2(cpu_flags)) {
        c->horiz_filter = ff_cfhd_horiz_filter_sse2;
        c->vert_filter = ff_cfhd_vert_filter_sse2;
        if (depth == 10 && !bayer)
            c->horiz_filter_clip = ff_cfhd_horiz_filter_clip10_sse2;
        if (depth == 12 && !bayer)
            c->horiz_filter_clip = ff_cfhd_horiz_filter_clip12_sse2;
    }
}
