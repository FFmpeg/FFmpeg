/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_DCADSP_H
#define AVCODEC_DCADSP_H

#include "avfft.h"
#include "synth_filter.h"

typedef struct DCADSPContext {
    void (*lfe_fir[2])(float *out, const float *in, const float *coefs,
                       float scale);
    void (*qmf_32_subbands)(float samples_in[32][8], int sb_act,
                            SynthFilterContext *synth, FFTContext *imdct,
                            float synth_buf_ptr[512],
                            int *synth_buf_offset, float synth_buf2[32],
                            const float window[512], float *samples_out,
                            float raXin[32], float scale);
    void (*int8x8_fmul_int32)(float *dst, const int8_t *src, int scale);
} DCADSPContext;

void ff_dcadsp_init(DCADSPContext *s);
void ff_dcadsp_init_arm(DCADSPContext *s);
void ff_dcadsp_init_x86(DCADSPContext *s);

#endif /* AVCODEC_DCADSP_H */
