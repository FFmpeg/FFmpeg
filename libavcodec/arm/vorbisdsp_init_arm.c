/*
 * ARM NEON optimised DSP functions
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
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
#include "libavutil/arm/cpu.h"
#include "libavcodec/vorbisdsp.h"

void ff_vorbis_inverse_coupling_neon(float *mag, float *ang,
                                     ptrdiff_t blocksize);

av_cold void ff_vorbisdsp_init_arm(VorbisDSPContext *c)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        c->vorbis_inverse_coupling = ff_vorbis_inverse_coupling_neon;
    }
}
