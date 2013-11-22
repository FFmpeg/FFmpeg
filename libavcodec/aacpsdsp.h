/*
 * Copyright (c) 2012 Mans Rullgard
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

#ifndef LIBAVCODEC_AACPSDSP_H
#define LIBAVCODEC_AACPSDSP_H

#define PS_QMF_TIME_SLOTS 32
#define PS_AP_LINKS 3
#define PS_MAX_AP_DELAY 5

typedef struct PSDSPContext {
    void (*add_squares)(float *dst, const float (*src)[2], int n);
    void (*mul_pair_single)(float (*dst)[2], float (*src0)[2], float *src1,
                            int n);
    void (*hybrid_analysis)(float (*out)[2], float (*in)[2],
                            const float (*filter)[8][2],
                            int stride, int n);
    void (*hybrid_analysis_ileave)(float (*out)[32][2], float L[2][38][64],
                                   int i, int len);
    void (*hybrid_synthesis_deint)(float out[2][38][64], float (*in)[32][2],
                                   int i, int len);
    void (*decorrelate)(float (*out)[2], float (*delay)[2],
                        float (*ap_delay)[PS_QMF_TIME_SLOTS+PS_MAX_AP_DELAY][2],
                        const float phi_fract[2], const float (*Q_fract)[2],
                        const float *transient_gain,
                        float g_decay_slope,
                        int len);
    void (*stereo_interpolate[2])(float (*l)[2], float (*r)[2],
                                  float h[2][4], float h_step[2][4],
                                  int len);
} PSDSPContext;

void ff_psdsp_init(PSDSPContext *s);
void ff_psdsp_init_arm(PSDSPContext *s);
void ff_psdsp_init_mips(PSDSPContext *s);

#endif /* LIBAVCODEC_AACPSDSP_H */
