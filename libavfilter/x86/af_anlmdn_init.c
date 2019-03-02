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

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavfilter/af_anlmdndsp.h"

float ff_compute_distance_ssd_sse(const float *f1, const float *f2,
                                  ptrdiff_t len);

av_cold void ff_anlmdn_init_x86(AudioNLMDNDSPContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE(cpu_flags)) {
        s->compute_distance_ssd = ff_compute_distance_ssd_sse;
    }
}
