/*
 * Copyright (c) 2010 Mans Rullgard
 * Copyright (c) 2014 James Yu <james.yu@linaro.org>
 *
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

#include <arm_neon.h>

#include "config.h"

#include "libavutil/cpu.h"
#if   ARCH_AARCH64
#   include "libavutil/aarch64/cpu.h"
#elif ARCH_ARM
#   include "libavutil/arm/cpu.h"
#endif

#include "libavcodec/mpegvideo.h"

static void inline ff_dct_unquantize_h263_neon(int qscale, int qadd, int nCoeffs,
                                               int16_t *block)
{
    int16x8_t q0s16, q2s16, q3s16, q8s16, q10s16, q11s16, q13s16;
    int16x8_t q14s16, q15s16, qzs16;
    int16x4_t d0s16, d2s16, d3s16, dzs16;
    uint16x8_t q1u16, q9u16;
    uint16x4_t d1u16;

    dzs16 = vdup_n_s16(0);
    qzs16 = vdupq_n_s16(0);

    q15s16 = vdupq_n_s16(qscale << 1);
    q14s16 = vdupq_n_s16(qadd);
    q13s16 = vnegq_s16(q14s16);

    if (nCoeffs > 4) {
        for (; nCoeffs > 8; nCoeffs -= 16, block += 16) {
            q0s16 = vld1q_s16(block);
            q3s16 = vreinterpretq_s16_u16(vcltq_s16(q0s16, qzs16));
            q8s16 = vld1q_s16(block + 8);
            q1u16 = vceqq_s16(q0s16, qzs16);
            q2s16 = vmulq_s16(q0s16, q15s16);
            q11s16 = vreinterpretq_s16_u16(vcltq_s16(q8s16, qzs16));
            q10s16 = vmulq_s16(q8s16, q15s16);
            q3s16 = vbslq_s16(vreinterpretq_u16_s16(q3s16), q13s16, q14s16);
            q11s16 = vbslq_s16(vreinterpretq_u16_s16(q11s16), q13s16, q14s16);
            q2s16 = vaddq_s16(q2s16, q3s16);
            q9u16 = vceqq_s16(q8s16, qzs16);
            q10s16 = vaddq_s16(q10s16, q11s16);
            q0s16 = vbslq_s16(q1u16, q0s16, q2s16);
            q8s16 = vbslq_s16(q9u16, q8s16, q10s16);
            vst1q_s16(block, q0s16);
            vst1q_s16(block + 8, q8s16);
        }
    }
    if (nCoeffs <= 0)
        return;

    d0s16 = vld1_s16(block);
    d3s16 = vreinterpret_s16_u16(vclt_s16(d0s16, dzs16));
    d1u16 = vceq_s16(d0s16, dzs16);
    d2s16 = vmul_s16(d0s16, vget_high_s16(q15s16));
    d3s16 = vbsl_s16(vreinterpret_u16_s16(d3s16),
                     vget_high_s16(q13s16), vget_high_s16(q14s16));
    d2s16 = vadd_s16(d2s16, d3s16);
    d0s16 = vbsl_s16(d1u16, d0s16, d2s16);
    vst1_s16(block, d0s16);
}

static void dct_unquantize_h263_inter_neon(MpegEncContext *s, int16_t *block,
                                           int n, int qscale)
{
    int nCoeffs = s->inter_scantable.raster_end[s->block_last_index[n]];
    int qadd    = (qscale - 1) | 1;

    ff_dct_unquantize_h263_neon(qscale, qadd, nCoeffs + 1, block);
}

static void dct_unquantize_h263_intra_neon(MpegEncContext *s, int16_t *block,
                                           int n, int qscale)
{
    int qadd;
    int nCoeffs, blk0;

    if (!s->h263_aic) {
        if (n < 4)
            block[0] *= s->y_dc_scale;
        else
            block[0] *= s->c_dc_scale;
        qadd = (qscale - 1) | 1;
    } else {
        qadd = 0;
    }

    if (s->ac_pred) {
        nCoeffs = 63;
    } else {
        nCoeffs = s->inter_scantable.raster_end[s->block_last_index[n]];
        if (nCoeffs <= 0)
            return;
    }

    blk0 = block[0];

    ff_dct_unquantize_h263_neon(qscale, qadd, nCoeffs + 1, block);

    block[0] = blk0;
}


av_cold void ff_mpv_common_init_neon(MpegEncContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        s->dct_unquantize_h263_intra = dct_unquantize_h263_intra_neon;
        s->dct_unquantize_h263_inter = dct_unquantize_h263_inter_neon;
    }
}
