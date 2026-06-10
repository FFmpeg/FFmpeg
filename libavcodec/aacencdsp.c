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

#include <float.h>
#include <math.h>

#include "config.h"

#include "libavutil/macros.h"
#include "aacencdsp.h"

static void abs_pow34_v(float *out, const float *in, const int size)
{
    for (int i = 0; i < size; i++) {
        float a = fabsf(in[i]);
        out[i] = sqrtf(a * sqrtf(a));
    }
}

static void quantize_bands(int *out, const float *in, const float *scaled,
                                  int size, int is_signed, int maxval, const float Q34,
                                  const float rounding)
{
    for (int i = 0; i < size; i++) {
        float qc = scaled[i] * Q34;
        int tmp = (int)FFMIN((float)(qc + rounding), (float)maxval);
        if (is_signed && in[i] < 0.0f) {
            tmp = -tmp;
        }
        out[i] = tmp;
    }
}

/* One NMR scalefactor-trellis Viterbi step, for each current-band candidate, find the
 * previous-band candidate minimising dpp[op] + lamsf[d] then set
 * dp[o] = node[o] + that cost and record the back-pointer bp[o] */
static void nmr_trellis_step_c(float *dp, uint8_t *bp, const float *dpp,
                               const float *node, const float *lamsf,
                               int n_cur, int n_prev, int base, int step, int mdiff)
{
    for (int o = 0; o < n_cur; o++) {
        int best = -1;
        float bestc = FLT_MAX;
        for (int op = 0; op < n_prev; op++) {
            int d = base + (o - op) * step;
            float c;
            if (d < -mdiff || d > mdiff)
                continue;
            c = dpp[op] + lamsf[d + mdiff];
            if (c < bestc) {
                bestc = c;
                best  = op;
            }
        }
        bp[o] = best < 0 ? 0 : best;
        dp[o] = best < 0 ? FLT_MAX : node[o] + bestc;
    }
}

void ff_aacenc_dsp_init(AACEncDSPContext *s)
{
    s->abs_pow34        = abs_pow34_v;
    s->quant_bands      = quantize_bands;
    s->nmr_trellis_step = nmr_trellis_step_c;

#if ARCH_RISCV
    ff_aacenc_dsp_init_riscv(s);
#elif ARCH_X86 && HAVE_X86ASM
    ff_aacenc_dsp_init_x86(s);
#elif ARCH_AARCH64
    ff_aacenc_dsp_init_aarch64(s);
#endif
}
