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

#include "libavutil/aarch64/cpu.h"
#include "libavfilter/vf_nlmeans.h"

void ff_compute_safe_ssd_integral_image_neon(uint32_t *dst, ptrdiff_t dst_linesize_32,
                                             const uint8_t *s1, ptrdiff_t linesize1,
                                             const uint8_t *s2, ptrdiff_t linesize2,
                                             int w, int h);

av_cold void ff_nlmeans_init_aarch64(NLMeansDSPContext *dsp)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags))
        dsp->compute_safe_ssd_integral_image = ff_compute_safe_ssd_integral_image_neon;
}
