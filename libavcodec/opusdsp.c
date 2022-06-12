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

#include "config.h"
#include "libavutil/attributes.h"
#include "opusdsp.h"

static void postfilter_c(float *data, int period, float *gains, int len)
{
    const float g0 = gains[0];
    const float g1 = gains[1];
    const float g2 = gains[2];

    float x4 = data[-period - 2];
    float x3 = data[-period - 1];
    float x2 = data[-period + 0];
    float x1 = data[-period + 1];

    for (int i = 0; i < len; i++) {
        float x0 = data[i - period + 2];
        data[i] += g0 * x2        +
                   g1 * (x1 + x3) +
                   g2 * (x0 + x4);
        x4 = x3;
        x3 = x2;
        x2 = x1;
        x1 = x0;
    }
}

static float deemphasis_c(float *y, float *x, float coeff, int len)
{
    for (int i = 0; i < len; i++)
        coeff = y[i] = x[i] + coeff*CELT_EMPH_COEFF;

    return coeff;
}

av_cold void ff_opus_dsp_init(OpusDSP *ctx)
{
    ctx->postfilter = postfilter_c;
    ctx->deemphasis = deemphasis_c;

#if ARCH_AARCH64
    ff_opus_dsp_init_aarch64(ctx);
#elif ARCH_X86
    ff_opus_dsp_init_x86(ctx);
#endif
}
