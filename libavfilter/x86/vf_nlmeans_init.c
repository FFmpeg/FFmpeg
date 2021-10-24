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

#include "libavutil/attributes.h"
#include "libavutil/x86/cpu.h"
#include "libavfilter/vf_nlmeans.h"

void ff_compute_weights_line_avx2(const uint32_t *const iia,
                                  const uint32_t *const iib,
                                  const uint32_t *const iid,
                                  const uint32_t *const iie,
                                  const uint8_t *const src,
                                  float *total_weight,
                                  float *sum,
                                  const float *const weight_lut,
                                  int max_meaningful_diff,
                                  int startx, int endx);

av_cold void ff_nlmeans_init_x86(NLMeansDSPContext *dsp)
{
    int cpu_flags = av_get_cpu_flags();

    if (ARCH_X86_64 && EXTERNAL_AVX2_FAST(cpu_flags))
        dsp->compute_weights_line = ff_compute_weights_line_avx2;
}
