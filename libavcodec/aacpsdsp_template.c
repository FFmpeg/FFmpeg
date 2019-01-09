/*
 * Copyright (c) 2010 Alex Converse <alex.converse@gmail.com>
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
 *
 * Note: Rounding-to-nearest used unless otherwise stated
 *
 */
#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "aacpsdsp.h"

static void ps_add_squares_c(INTFLOAT *dst, const INTFLOAT (*src)[2], int n)
{
    int i;
    for (i = 0; i < n; i++)
        dst[i] += (UINTFLOAT)AAC_MADD28(src[i][0], src[i][0], src[i][1], src[i][1]);
}

static void ps_mul_pair_single_c(INTFLOAT (*dst)[2], INTFLOAT (*src0)[2], INTFLOAT *src1,
                                 int n)
{
    int i;
    for (i = 0; i < n; i++) {
        dst[i][0] = AAC_MUL16(src0[i][0], src1[i]);
        dst[i][1] = AAC_MUL16(src0[i][1], src1[i]);
    }
}

static void ps_hybrid_analysis_c(INTFLOAT (*out)[2], INTFLOAT (*in)[2],
                                 const INTFLOAT (*filter)[8][2],
                                 ptrdiff_t stride, int n)
{
    int i, j;

    for (i = 0; i < n; i++) {
        INT64FLOAT sum_re = (INT64FLOAT)filter[i][6][0] * in[6][0];
        INT64FLOAT sum_im = (INT64FLOAT)filter[i][6][0] * in[6][1];

        for (j = 0; j < 6; j++) {
            INTFLOAT in0_re = in[j][0];
            INTFLOAT in0_im = in[j][1];
            INTFLOAT in1_re = in[12-j][0];
            INTFLOAT in1_im = in[12-j][1];
            sum_re += (INT64FLOAT)filter[i][j][0] * (in0_re + in1_re) -
                      (INT64FLOAT)filter[i][j][1] * (in0_im - in1_im);
            sum_im += (INT64FLOAT)filter[i][j][0] * (in0_im + in1_im) +
                      (INT64FLOAT)filter[i][j][1] * (in0_re - in1_re);
        }
#if USE_FIXED
        out[i * stride][0] = (int)((sum_re + 0x40000000) >> 31);
        out[i * stride][1] = (int)((sum_im + 0x40000000) >> 31);
#else
        out[i * stride][0] = sum_re;
        out[i * stride][1] = sum_im;
#endif /* USE_FIXED */
    }
}

static void ps_hybrid_analysis_ileave_c(INTFLOAT (*out)[32][2], INTFLOAT L[2][38][64],
                                      int i, int len)
{
    int j;

    for (; i < 64; i++) {
        for (j = 0; j < len; j++) {
            out[i][j][0] = L[0][j][i];
            out[i][j][1] = L[1][j][i];
        }
    }
}

static void ps_hybrid_synthesis_deint_c(INTFLOAT out[2][38][64],
                                      INTFLOAT (*in)[32][2],
                                      int i, int len)
{
    int n;

    for (; i < 64; i++) {
        for (n = 0; n < len; n++) {
            out[0][n][i] = in[i][n][0];
            out[1][n][i] = in[i][n][1];
        }
    }
}

static void ps_decorrelate_c(INTFLOAT (*out)[2], INTFLOAT (*delay)[2],
                             INTFLOAT (*ap_delay)[PS_QMF_TIME_SLOTS + PS_MAX_AP_DELAY][2],
                             const INTFLOAT phi_fract[2], const INTFLOAT (*Q_fract)[2],
                             const INTFLOAT *transient_gain,
                             INTFLOAT g_decay_slope,
                             int len)
{
    static const INTFLOAT a[] = { Q31(0.65143905753106f),
                               Q31(0.56471812200776f),
                               Q31(0.48954165955695f) };
    INTFLOAT ag[PS_AP_LINKS];
    int m, n;

    for (m = 0; m < PS_AP_LINKS; m++)
        ag[m] = AAC_MUL30(a[m], g_decay_slope);

    for (n = 0; n < len; n++) {
        INTFLOAT in_re = AAC_MSUB30(delay[n][0], phi_fract[0], delay[n][1], phi_fract[1]);
        INTFLOAT in_im = AAC_MADD30(delay[n][0], phi_fract[1], delay[n][1], phi_fract[0]);
        for (m = 0; m < PS_AP_LINKS; m++) {
            INTFLOAT a_re                = AAC_MUL31(ag[m], in_re);
            INTFLOAT a_im                = AAC_MUL31(ag[m], in_im);
            INTFLOAT link_delay_re       = ap_delay[m][n+2-m][0];
            INTFLOAT link_delay_im       = ap_delay[m][n+2-m][1];
            INTFLOAT fractional_delay_re = Q_fract[m][0];
            INTFLOAT fractional_delay_im = Q_fract[m][1];
            INTFLOAT apd_re = in_re;
            INTFLOAT apd_im = in_im;
            in_re = AAC_MSUB30(link_delay_re, fractional_delay_re,
                    link_delay_im, fractional_delay_im);
            in_re -= (UINTFLOAT)a_re;
            in_im = AAC_MADD30(link_delay_re, fractional_delay_im,
                    link_delay_im, fractional_delay_re);
            in_im -= (UINTFLOAT)a_im;
            ap_delay[m][n+5][0] = apd_re + (UINTFLOAT)AAC_MUL31(ag[m], in_re);
            ap_delay[m][n+5][1] = apd_im + (UINTFLOAT)AAC_MUL31(ag[m], in_im);
        }
        out[n][0] = AAC_MUL16(transient_gain[n], in_re);
        out[n][1] = AAC_MUL16(transient_gain[n], in_im);
    }
}

static void ps_stereo_interpolate_c(INTFLOAT (*l)[2], INTFLOAT (*r)[2],
                                    INTFLOAT h[2][4], INTFLOAT h_step[2][4],
                                    int len)
{
    INTFLOAT h0 = h[0][0];
    INTFLOAT h1 = h[0][1];
    INTFLOAT h2 = h[0][2];
    INTFLOAT h3 = h[0][3];
    UINTFLOAT hs0 = h_step[0][0];
    UINTFLOAT hs1 = h_step[0][1];
    UINTFLOAT hs2 = h_step[0][2];
    UINTFLOAT hs3 = h_step[0][3];
    int n;

    for (n = 0; n < len; n++) {
        //l is s, r is d
        INTFLOAT l_re = l[n][0];
        INTFLOAT l_im = l[n][1];
        INTFLOAT r_re = r[n][0];
        INTFLOAT r_im = r[n][1];
        h0 += hs0;
        h1 += hs1;
        h2 += hs2;
        h3 += hs3;
        l[n][0] = AAC_MADD30(h0, l_re, h2, r_re);
        l[n][1] = AAC_MADD30(h0, l_im, h2, r_im);
        r[n][0] = AAC_MADD30(h1, l_re, h3, r_re);
        r[n][1] = AAC_MADD30(h1, l_im, h3, r_im);
    }
}

static void ps_stereo_interpolate_ipdopd_c(INTFLOAT (*l)[2], INTFLOAT (*r)[2],
                                           INTFLOAT h[2][4], INTFLOAT h_step[2][4],
                                           int len)
{
    INTFLOAT h00  = h[0][0],      h10  = h[1][0];
    INTFLOAT h01  = h[0][1],      h11  = h[1][1];
    INTFLOAT h02  = h[0][2],      h12  = h[1][2];
    INTFLOAT h03  = h[0][3],      h13  = h[1][3];
    UINTFLOAT hs00 = h_step[0][0], hs10 = h_step[1][0];
    UINTFLOAT hs01 = h_step[0][1], hs11 = h_step[1][1];
    UINTFLOAT hs02 = h_step[0][2], hs12 = h_step[1][2];
    UINTFLOAT hs03 = h_step[0][3], hs13 = h_step[1][3];
    int n;

    for (n = 0; n < len; n++) {
        //l is s, r is d
        INTFLOAT l_re = l[n][0];
        INTFLOAT l_im = l[n][1];
        INTFLOAT r_re = r[n][0];
        INTFLOAT r_im = r[n][1];
        h00 += hs00;
        h01 += hs01;
        h02 += hs02;
        h03 += hs03;
        h10 += hs10;
        h11 += hs11;
        h12 += hs12;
        h13 += hs13;

        l[n][0] = AAC_MSUB30_V8(h00, l_re, h02, r_re, h10, l_im, h12, r_im);
        l[n][1] = AAC_MADD30_V8(h00, l_im, h02, r_im, h10, l_re, h12, r_re);
        r[n][0] = AAC_MSUB30_V8(h01, l_re, h03, r_re, h11, l_im, h13, r_im);
        r[n][1] = AAC_MADD30_V8(h01, l_im, h03, r_im, h11, l_re, h13, r_re);
    }
}

av_cold void AAC_RENAME(ff_psdsp_init)(PSDSPContext *s)
{
    s->add_squares            = ps_add_squares_c;
    s->mul_pair_single        = ps_mul_pair_single_c;
    s->hybrid_analysis        = ps_hybrid_analysis_c;
    s->hybrid_analysis_ileave = ps_hybrid_analysis_ileave_c;
    s->hybrid_synthesis_deint = ps_hybrid_synthesis_deint_c;
    s->decorrelate            = ps_decorrelate_c;
    s->stereo_interpolate[0]  = ps_stereo_interpolate_c;
    s->stereo_interpolate[1]  = ps_stereo_interpolate_ipdopd_c;

#if !USE_FIXED
    if (ARCH_ARM)
        ff_psdsp_init_arm(s);
    if (ARCH_AARCH64)
        ff_psdsp_init_aarch64(s);
    if (ARCH_MIPS)
        ff_psdsp_init_mips(s);
    if (ARCH_X86)
        ff_psdsp_init_x86(s);
#endif /* !USE_FIXED */
}
