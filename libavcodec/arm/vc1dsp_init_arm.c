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

#include "libavutil/attributes.h"
#include "libavutil/arm/cpu.h"
#include "libavcodec/arm/startcode.h"
#include "libavcodec/vc1dsp.h"
#include "vc1dsp.h"

av_cold void ff_vc1dsp_init_arm(VC1DSPContext *dsp)
{
    int cpu_flags = av_get_cpu_flags();

#if HAVE_ARMV6
    if (have_setend(cpu_flags))
        dsp->startcode_find_candidate = ff_startcode_find_candidate_armv6;
#endif
    if (have_neon(cpu_flags))
        ff_vc1dsp_init_neon(dsp);
}
