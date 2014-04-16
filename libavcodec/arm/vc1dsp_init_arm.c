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
#include "libavcodec/vc1dsp.h"
#include "vc1dsp.h"

int ff_startcode_find_candidate_armv6(const uint8_t *buf, int size);

av_cold void ff_vc1dsp_init_arm(VC1DSPContext *dsp)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_armv6(cpu_flags))
        dsp->vc1_find_start_code_candidate = ff_startcode_find_candidate_armv6;
    if (have_neon(cpu_flags))
        ff_vc1dsp_init_neon(dsp);
}
