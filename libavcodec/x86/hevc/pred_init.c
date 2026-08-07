/*
 * SIMD-optimized HEVC intra prediction
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

#include "config.h"

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/hevc/pred.h"

#define PRED_PLANAR_FUNC(idx, opt)                                            \
void ff_hevc_pred_planar_ ## idx ## _8_ ## opt(uint8_t *src,                  \
                                               const uint8_t *top,            \
                                               const uint8_t *left,           \
                                               ptrdiff_t stride);

PRED_PLANAR_FUNC(0, ssse3)
PRED_PLANAR_FUNC(1, ssse3)
PRED_PLANAR_FUNC(2, ssse3)
PRED_PLANAR_FUNC(3, ssse3)

av_cold void ff_hevc_pred_init_x86(HEVCPredContext *hpc, int bit_depth)
{
    int cpu_flags = av_get_cpu_flags();

    if (bit_depth == 8) {
        if (EXTERNAL_SSSE3(cpu_flags)) {
            hpc->pred_planar[0] = ff_hevc_pred_planar_0_8_ssse3;
            hpc->pred_planar[1] = ff_hevc_pred_planar_1_8_ssse3;
            hpc->pred_planar[2] = ff_hevc_pred_planar_2_8_ssse3;
            hpc->pred_planar[3] = ff_hevc_pred_planar_3_8_ssse3;
        }
    }
}
