/*
 * Copyright (c) 2018 Paul B Mahol
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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavfilter/f_ebur128.h"

void ff_ebur128_filter_channels_avx(const EBUR128DSPContext *, const double *,
                                    double *, double *, double *, double *, int);

double ff_ebur128_find_peak_2ch_avx(double *, int, const double *, int);

av_cold void ff_ebur128_init_x86(EBUR128DSPContext *dsp, int nb_channels)
{
    int cpu_flags = av_get_cpu_flags();

    if (ARCH_X86_64 && EXTERNAL_AVX(cpu_flags)) {
        dsp->filter_channels = ff_ebur128_filter_channels_avx;
        if (nb_channels == 2)
            dsp->find_peak = ff_ebur128_find_peak_2ch_avx;
    }
}
