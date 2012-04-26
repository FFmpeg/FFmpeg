/*
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

#include <stdint.h>

#include "libavutil/arm/cpu.h"
#include "libavcodec/vp8dsp.h"
#include "vp8dsp.h"

av_cold void ff_vp8dsp_init_arm(VP8DSPContext *dsp)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_armv6(cpu_flags))
        ff_vp8dsp_init_armv6(dsp);
    if (have_neon(cpu_flags))
        ff_vp8dsp_init_neon(dsp);
}
