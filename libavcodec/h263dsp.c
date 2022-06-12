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

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "config.h"
#include "h263dsp.h"

const uint8_t ff_h263_loop_filter_strength[32] = {
    0, 1, 1, 2, 2, 3, 3,  4,  4,  4,  5,  5,  6,  6,  7, 7,
    7, 8, 8, 8, 9, 9, 9, 10, 10, 10, 11, 11, 11, 12, 12, 12
};

static void h263_h_loop_filter_c(uint8_t *src, int stride, int qscale)
{
    int y;
    const int strength = ff_h263_loop_filter_strength[qscale];

    for (y = 0; y < 8; y++) {
        int d1, d2, ad1;
        int p0 = src[y * stride - 2];
        int p1 = src[y * stride - 1];
        int p2 = src[y * stride + 0];
        int p3 = src[y * stride + 1];
        int d  = (p0 - p3 + 4 * (p2 - p1)) / 8;

        if (d < -2 * strength)
            d1 = 0;
        else if (d < -strength)
            d1 = -2 * strength - d;
        else if (d < strength)
            d1 = d;
        else if (d < 2 * strength)
            d1 = 2 * strength - d;
        else
            d1 = 0;

        p1 += d1;
        p2 -= d1;
        if (p1 & 256)
            p1 = ~(p1 >> 31);
        if (p2 & 256)
            p2 = ~(p2 >> 31);

        src[y * stride - 1] = p1;
        src[y * stride + 0] = p2;

        ad1 = FFABS(d1) >> 1;

        d2 = av_clip((p0 - p3) / 4, -ad1, ad1);

        src[y * stride - 2] = p0 - d2;
        src[y * stride + 1] = p3 + d2;
    }
}

static void h263_v_loop_filter_c(uint8_t *src, int stride, int qscale)
{
    int x;
    const int strength = ff_h263_loop_filter_strength[qscale];

    for (x = 0; x < 8; x++) {
        int d1, d2, ad1;
        int p0 = src[x - 2 * stride];
        int p1 = src[x - 1 * stride];
        int p2 = src[x + 0 * stride];
        int p3 = src[x + 1 * stride];
        int d  = (p0 - p3 + 4 * (p2 - p1)) / 8;

        if (d < -2 * strength)
            d1 = 0;
        else if (d < -strength)
            d1 = -2 * strength - d;
        else if (d < strength)
            d1 = d;
        else if (d < 2 * strength)
            d1 = 2 * strength - d;
        else
            d1 = 0;

        p1 += d1;
        p2 -= d1;
        if (p1 & 256)
            p1 = ~(p1 >> 31);
        if (p2 & 256)
            p2 = ~(p2 >> 31);

        src[x - 1 * stride] = p1;
        src[x + 0 * stride] = p2;

        ad1 = FFABS(d1) >> 1;

        d2 = av_clip((p0 - p3) / 4, -ad1, ad1);

        src[x - 2 * stride] = p0 - d2;
        src[x + stride]     = p3 + d2;
    }
}

av_cold void ff_h263dsp_init(H263DSPContext *ctx)
{
    ctx->h263_h_loop_filter = h263_h_loop_filter_c;
    ctx->h263_v_loop_filter = h263_v_loop_filter_c;

#if ARCH_X86
    ff_h263dsp_init_x86(ctx);
#elif ARCH_MIPS
    ff_h263dsp_init_mips(ctx);
#endif
}
