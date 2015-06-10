/*
 * Copyright (c) 2004 Gildas Bazin
 * Copyright (c) 2010 Mans Rullgard <mans@mansr.com>
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
#include "libavutil/intreadwrite.h"

#include "dcadsp.h"

static void decode_hf_c(float dst[DCA_SUBBANDS][8],
                        const int32_t vq_num[DCA_SUBBANDS],
                        const int8_t hf_vq[1024][32], intptr_t vq_offset,
                        int32_t scale[DCA_SUBBANDS][2],
                        intptr_t start, intptr_t end)
{
    int i, l;

    for (l = start; l < end; l++) {
        /* 1 vector -> 32 samples but we only need the 8 samples
         * for this subsubframe. */
        const int8_t *ptr = &hf_vq[vq_num[l]][vq_offset];
        float fscale = scale[l][0] * (1 / 16.0);
        for (i = 0; i < 8; i++)
            dst[l][i] = ptr[i] * fscale;
    }
}

static inline void dca_lfe_fir(float *out, const float *in, const float *coefs,
                               int decifactor)
{
    float *out2    = out + 2 * decifactor - 1;
    int num_coeffs = 256 / decifactor;
    int j, k;

    /* One decimated sample generates 2*decifactor interpolated ones */
    for (k = 0; k < decifactor; k++) {
        float v0 = 0.0;
        float v1 = 0.0;
        for (j = 0; j < num_coeffs; j++, coefs++) {
            v0 += in[-j]                 * *coefs;
            v1 += in[j + 1 - num_coeffs] * *coefs;
        }
        *out++  = v0;
        *out2-- = v1;
    }
}

static void dca_qmf_32_subbands(float samples_in[32][8], int sb_act,
                                SynthFilterContext *synth, FFTContext *imdct,
                                float synth_buf_ptr[512],
                                int *synth_buf_offset, float synth_buf2[32],
                                const float window[512], float *samples_out,
                                float raXin[32], float scale)
{
    int i;
    int subindex;

    for (i = sb_act; i < 32; i++)
        raXin[i] = 0.0;

    /* Reconstructed channel sample index */
    for (subindex = 0; subindex < 8; subindex++) {
        /* Load in one sample from each subband and clear inactive subbands */
        for (i = 0; i < sb_act; i++) {
            unsigned sign = (i - 1) & 2;
            uint32_t v    = AV_RN32A(&samples_in[i][subindex]) ^ sign << 30;
            AV_WN32A(&raXin[i], v);
        }

        synth->synth_filter_float(imdct, synth_buf_ptr, synth_buf_offset,
                                  synth_buf2, window, samples_out, raXin,
                                  scale);
        samples_out += 32;
    }
}

static void dca_lfe_fir0_c(float *out, const float *in, const float *coefs)
{
    dca_lfe_fir(out, in, coefs, 32);
}

static void dca_lfe_fir1_c(float *out, const float *in, const float *coefs)
{
    dca_lfe_fir(out, in, coefs, 64);
}

av_cold void ff_dcadsp_init(DCADSPContext *s)
{
    s->lfe_fir[0]      = dca_lfe_fir0_c;
    s->lfe_fir[1]      = dca_lfe_fir1_c;
    s->qmf_32_subbands = dca_qmf_32_subbands;
    s->decode_hf       = decode_hf_c;

    if (ARCH_ARM)
        ff_dcadsp_init_arm(s);
    if (ARCH_X86)
        ff_dcadsp_init_x86(s);
}
