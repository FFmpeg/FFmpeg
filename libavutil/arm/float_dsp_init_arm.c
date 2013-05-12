/*
 * ARM optimized DSP utils
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
#include "libavutil/float_dsp.h"
#include "cpu.h"
#include "float_dsp_arm.h"

av_cold void ff_float_dsp_init_arm(AVFloatDSPContext *fdsp)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_vfp(cpu_flags))
        ff_float_dsp_init_vfp(fdsp);
    if (have_neon(cpu_flags))
        ff_float_dsp_init_neon(fdsp);
}
