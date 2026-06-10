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

#ifndef AVCODEC_AACENCDSP_H
#define AVCODEC_AACENCDSP_H

#include <stdint.h>

typedef struct AACEncDSPContext {
    void (*abs_pow34)(float *out, const float *in, const int size);
    void (*quant_bands)(int *out, const float *in, const float *scaled,
                        int size, int is_signed, int maxval, const float Q34,
                        const float rounding);

    void (*nmr_trellis_step)(float *dp, uint8_t *bp, const float *dpp,
                             const float *node, const float *lamsf,
                             int n_cur, int n_prev, int base, int step, int mdiff);
} AACEncDSPContext;

void ff_aacenc_dsp_init(AACEncDSPContext *s);
void ff_aacenc_dsp_init_riscv(AACEncDSPContext *s);
void ff_aacenc_dsp_init_x86(AACEncDSPContext *s);
void ff_aacenc_dsp_init_aarch64(AACEncDSPContext *s);

#endif
