/*
 * AArch64 NEON optimised DCA DSP functions
 * Copyright (c) 2026 Jeongkeun Kim <variety0724@gmail.com>
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
#include "libavutil/aarch64/cpu.h"
#include "libavcodec/dcadsp.h"

void ff_lfe_fir0_float_neon(float *pcm_samples, const int32_t *lfe_samples,
                            const float *filter_coeff, ptrdiff_t npcmblocks);
void ff_lfe_fir1_float_neon(float *pcm_samples, const int32_t *lfe_samples,
                            const float *filter_coeff, ptrdiff_t npcmblocks);

av_cold void ff_dcadsp_init_aarch64(DCADSPContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        s->lfe_fir_float[0] = ff_lfe_fir0_float_neon;
        s->lfe_fir_float[1] = ff_lfe_fir1_float_neon;
    }
}
