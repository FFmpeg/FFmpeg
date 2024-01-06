
/*
 * HEVC/VVC deblocking dsp template
 *
 * Copyright (C) 2024 Nuo Mi
 * Copyright (C) 2012 - 2013 Guillaume Martres
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

static void FUNC(loop_filter_luma_strong)(pixel *pix, const ptrdiff_t xstride, const ptrdiff_t ystride,
    const int32_t tc, const int32_t tc2, const int tc3,
    const uint8_t no_p, const uint8_t no_q)
{
    for (int d = 0; d < 4; d++) {
        const int p3 = P3;
        const int p2 = P2;
        const int p1 = P1;
        const int p0 = P0;
        const int q0 = Q0;
        const int q1 = Q1;
        const int q2 = Q2;
        const int q3 = Q3;
        if (!no_p) {
            P0 = p0 + av_clip(((p2 + 2 * p1 + 2 * p0 + 2 * q0 + q1 + 4) >> 3) - p0, -tc3, tc3);
            P1 = p1 + av_clip(((p2 + p1 + p0 + q0 + 2) >> 2) - p1, -tc2, tc2);
            P2 = p2 + av_clip(((2 * p3 + 3 * p2 + p1 + p0 + q0 + 4) >> 3) - p2, -tc, tc);
        }
        if (!no_q) {
            Q0 = q0 + av_clip(((p1 + 2 * p0 + 2 * q0 + 2 * q1 + q2 + 4) >> 3) - q0, -tc3, tc3);
            Q1 = q1 + av_clip(((p0 + q0 + q1 + q2 + 2) >> 2) - q1, -tc2, tc2);
            Q2 = q2 + av_clip(((2 * q3 + 3 * q2 + q1 + q0 + p0 + 4) >> 3) - q2, -tc, tc);
        }
        pix += ystride;
    }
}

static void FUNC(loop_filter_luma_weak)(pixel *pix, const ptrdiff_t xstride, const ptrdiff_t ystride,
    const int32_t tc, const int32_t beta, const uint8_t no_p, const uint8_t no_q, const int nd_p, const int nd_q)
{
    const int tc_2 = tc >> 1;
    for (int d = 0; d < 4; d++) {
        const int p2 = P2;
        const int p1 = P1;
        const int p0 = P0;
        const int q0 = Q0;
        const int q1 = Q1;
        const int q2 = Q2;
        int delta0 = (9 * (q0 - p0) - 3 * (q1 - p1) + 8) >> 4;
        if (abs(delta0) < 10 * tc) {
            delta0 = av_clip(delta0, -tc, tc);
            if (!no_p)
                P0 = av_clip_pixel(p0 + delta0);
            if (!no_q)
                Q0 = av_clip_pixel(q0 - delta0);
            if (!no_p && nd_p > 1) {
                const int deltap1 = av_clip((((p2 + p0 + 1) >> 1) - p1 + delta0) >> 1, -tc_2, tc_2);
                P1 = av_clip_pixel(p1 + deltap1);
            }
            if (!no_q && nd_q > 1) {
                const int deltaq1 = av_clip((((q2 + q0 + 1) >> 1) - q1 - delta0) >> 1, -tc_2, tc_2);
                Q1 = av_clip_pixel(q1 + deltaq1);
            }
        }
        pix += ystride;
    }
}

static void FUNC(loop_filter_chroma_weak)(pixel *pix, const ptrdiff_t xstride, const ptrdiff_t ystride,
    const int size, const int32_t tc, const uint8_t no_p, const uint8_t no_q)
{
    for (int d = 0; d < size; d++) {
        int delta0;
        const int p1 = P1;
        const int p0 = P0;
        const int q0 = Q0;
        const int q1 = Q1;
        delta0 = av_clip((((q0 - p0) * 4) + p1 - q1 + 4) >> 3, -tc, tc);
        if (!no_p)
            P0 = av_clip_pixel(p0 + delta0);
        if (!no_q)
            Q0 = av_clip_pixel(q0 - delta0);
        pix += ystride;
    }
}
