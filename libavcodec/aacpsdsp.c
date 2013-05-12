/*
 * Copyright (c) 2010 Alex Converse <alex.converse@gmail.com>
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

#include "config.h"
#include "libavutil/attributes.h"
#include "aacpsdsp.h"

static void ps_add_squares_c(float *dst, const float (*src)[2], int n)
{
    int i;
    for (i = 0; i < n; i++)
        dst[i] += src[i][0] * src[i][0] + src[i][1] * src[i][1];
}

static void ps_mul_pair_single_c(float (*dst)[2], float (*src0)[2], float *src1,
                                 int n)
{
    int i;
    for (i = 0; i < n; i++) {
        dst[i][0] = src0[i][0] * src1[i];
        dst[i][1] = src0[i][1] * src1[i];
    }
}

static void ps_hybrid_analysis_c(float (*out)[2], float (*in)[2],
                                 const float (*filter)[8][2],
                                 int stride, int n)
{
    int i, j;

    for (i = 0; i < n; i++) {
        float sum_re = filter[i][6][0] * in[6][0];
        float sum_im = filter[i][6][0] * in[6][1];

        for (j = 0; j < 6; j++) {
            float in0_re = in[j][0];
            float in0_im = in[j][1];
            float in1_re = in[12-j][0];
            float in1_im = in[12-j][1];
            sum_re += filter[i][j][0] * (in0_re + in1_re) -
                      filter[i][j][1] * (in0_im - in1_im);
            sum_im += filter[i][j][0] * (in0_im + in1_im) +
                      filter[i][j][1] * (in0_re - in1_re);
        }
        out[i * stride][0] = sum_re;
        out[i * stride][1] = sum_im;
    }
}

static void ps_hybrid_analysis_ileave_c(float (*out)[32][2], float L[2][38][64],
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

static void ps_hybrid_synthesis_deint_c(float out[2][38][64],
                                        float (*in)[32][2],
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

static void ps_decorrelate_c(float (*out)[2], float (*delay)[2],
                             float (*ap_delay)[PS_QMF_TIME_SLOTS + PS_MAX_AP_DELAY][2],
                             const float phi_fract[2], float (*Q_fract)[2],
                             const float *transient_gain,
                             float g_decay_slope,
                             int len)
{
    static const float a[] = { 0.65143905753106f,
                               0.56471812200776f,
                               0.48954165955695f };
    float ag[PS_AP_LINKS];
    int m, n;

    for (m = 0; m < PS_AP_LINKS; m++)
        ag[m] = a[m] * g_decay_slope;

    for (n = 0; n < len; n++) {
        float in_re = delay[n][0] * phi_fract[0] - delay[n][1] * phi_fract[1];
        float in_im = delay[n][0] * phi_fract[1] + delay[n][1] * phi_fract[0];
        for (m = 0; m < PS_AP_LINKS; m++) {
            float a_re                = ag[m] * in_re;
            float a_im                = ag[m] * in_im;
            float link_delay_re       = ap_delay[m][n+2-m][0];
            float link_delay_im       = ap_delay[m][n+2-m][1];
            float fractional_delay_re = Q_fract[m][0];
            float fractional_delay_im = Q_fract[m][1];
            float apd_re = in_re;
            float apd_im = in_im;
            in_re = link_delay_re * fractional_delay_re -
                    link_delay_im * fractional_delay_im - a_re;
            in_im = link_delay_re * fractional_delay_im +
                    link_delay_im * fractional_delay_re - a_im;
            ap_delay[m][n+5][0] = apd_re + ag[m] * in_re;
            ap_delay[m][n+5][1] = apd_im + ag[m] * in_im;
        }
        out[n][0] = transient_gain[n] * in_re;
        out[n][1] = transient_gain[n] * in_im;
    }
}

static void ps_stereo_interpolate_c(float (*l)[2], float (*r)[2],
                                    float h[2][4], float h_step[2][4],
                                    int len)
{
    float h0 = h[0][0];
    float h1 = h[0][1];
    float h2 = h[0][2];
    float h3 = h[0][3];
    float hs0 = h_step[0][0];
    float hs1 = h_step[0][1];
    float hs2 = h_step[0][2];
    float hs3 = h_step[0][3];
    int n;

    for (n = 0; n < len; n++) {
        //l is s, r is d
        float l_re = l[n][0];
        float l_im = l[n][1];
        float r_re = r[n][0];
        float r_im = r[n][1];
        h0 += hs0;
        h1 += hs1;
        h2 += hs2;
        h3 += hs3;
        l[n][0] = h0 * l_re + h2 * r_re;
        l[n][1] = h0 * l_im + h2 * r_im;
        r[n][0] = h1 * l_re + h3 * r_re;
        r[n][1] = h1 * l_im + h3 * r_im;
    }
}

static void ps_stereo_interpolate_ipdopd_c(float (*l)[2], float (*r)[2],
                                           float h[2][4], float h_step[2][4],
                                           int len)
{
    float h00  = h[0][0],      h10  = h[1][0];
    float h01  = h[0][1],      h11  = h[1][1];
    float h02  = h[0][2],      h12  = h[1][2];
    float h03  = h[0][3],      h13  = h[1][3];
    float hs00 = h_step[0][0], hs10 = h_step[1][0];
    float hs01 = h_step[0][1], hs11 = h_step[1][1];
    float hs02 = h_step[0][2], hs12 = h_step[1][2];
    float hs03 = h_step[0][3], hs13 = h_step[1][3];
    int n;

    for (n = 0; n < len; n++) {
        //l is s, r is d
        float l_re = l[n][0];
        float l_im = l[n][1];
        float r_re = r[n][0];
        float r_im = r[n][1];
        h00 += hs00;
        h01 += hs01;
        h02 += hs02;
        h03 += hs03;
        h10 += hs10;
        h11 += hs11;
        h12 += hs12;
        h13 += hs13;

        l[n][0] = h00 * l_re + h02 * r_re - h10 * l_im - h12 * r_im;
        l[n][1] = h00 * l_im + h02 * r_im + h10 * l_re + h12 * r_re;
        r[n][0] = h01 * l_re + h03 * r_re - h11 * l_im - h13 * r_im;
        r[n][1] = h01 * l_im + h03 * r_im + h11 * l_re + h13 * r_re;
    }
}

av_cold void ff_psdsp_init(PSDSPContext *s)
{
    s->add_squares            = ps_add_squares_c;
    s->mul_pair_single        = ps_mul_pair_single_c;
    s->hybrid_analysis        = ps_hybrid_analysis_c;
    s->hybrid_analysis_ileave = ps_hybrid_analysis_ileave_c;
    s->hybrid_synthesis_deint = ps_hybrid_synthesis_deint_c;
    s->decorrelate            = ps_decorrelate_c;
    s->stereo_interpolate[0]  = ps_stereo_interpolate_c;
    s->stereo_interpolate[1]  = ps_stereo_interpolate_ipdopd_c;

    if (ARCH_ARM)
        ff_psdsp_init_arm(s);
    if (ARCH_MIPS)
        ff_psdsp_init_mips(s);
}
