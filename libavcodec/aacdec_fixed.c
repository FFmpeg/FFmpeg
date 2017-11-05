/*
 * Copyright (c) 2013
 *      MIPS Technologies, Inc., California.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the MIPS Technologies, Inc., nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE MIPS TECHNOLOGIES, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE MIPS TECHNOLOGIES, INC. BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * AAC decoder fixed-point implementation
 *
 * Copyright (c) 2005-2006 Oded Shimon ( ods15 ods15 dyndns org )
 * Copyright (c) 2006-2007 Maxim Gavrilov ( maxim.gavrilov gmail com )
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

/**
 * @file
 * AAC decoder
 * @author Oded Shimon  ( ods15 ods15 dyndns org )
 * @author Maxim Gavrilov ( maxim.gavrilov gmail com )
 *
 * Fixed point implementation
 * @author Stanislav Ocovaj ( stanislav.ocovaj imgtec com )
 */

#define FFT_FLOAT 0
#define FFT_FIXED_32 1
#define USE_FIXED 1

#include "libavutil/fixed_dsp.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "internal.h"
#include "get_bits.h"
#include "fft.h"
#include "lpc.h"
#include "kbdwin.h"
#include "sinewin.h"

#include "aac.h"
#include "aactab.h"
#include "aacdectab.h"
#include "cbrt_data.h"
#include "sbr.h"
#include "aacsbr.h"
#include "mpeg4audio.h"
#include "aacadtsdec.h"
#include "profiles.h"
#include "libavutil/intfloat.h"

#include <math.h>
#include <string.h>

static av_always_inline void reset_predict_state(PredictorState *ps)
{
    ps->r0.mant   = 0;
    ps->r0.exp   = 0;
    ps->r1.mant   = 0;
    ps->r1.exp   = 0;
    ps->cor0.mant = 0;
    ps->cor0.exp = 0;
    ps->cor1.mant = 0;
    ps->cor1.exp = 0;
    ps->var0.mant = 0x20000000;
    ps->var0.exp = 1;
    ps->var1.mant = 0x20000000;
    ps->var1.exp = 1;
}

static const int exp2tab[4] = { Q31(1.0000000000/2), Q31(1.1892071150/2), Q31(1.4142135624/2), Q31(1.6817928305/2) };  // 2^0, 2^0.25, 2^0.5, 2^0.75

static inline int *DEC_SPAIR(int *dst, unsigned idx)
{
    dst[0] = (idx & 15) - 4;
    dst[1] = (idx >> 4 & 15) - 4;

    return dst + 2;
}

static inline int *DEC_SQUAD(int *dst, unsigned idx)
{
    dst[0] = (idx & 3) - 1;
    dst[1] = (idx >> 2 & 3) - 1;
    dst[2] = (idx >> 4 & 3) - 1;
    dst[3] = (idx >> 6 & 3) - 1;

    return dst + 4;
}

static inline int *DEC_UPAIR(int *dst, unsigned idx, unsigned sign)
{
    dst[0] = (idx & 15) * (1 - (sign & 0xFFFFFFFE));
    dst[1] = (idx >> 4 & 15) * (1 - ((sign & 1) * 2));

    return dst + 2;
}

static inline int *DEC_UQUAD(int *dst, unsigned idx, unsigned sign)
{
    unsigned nz = idx >> 12;

    dst[0] = (idx & 3) * (1 + (((int)sign >> 31) * 2));
    sign <<= nz & 1;
    nz >>= 1;
    dst[1] = (idx >> 2 & 3) * (1 + (((int)sign >> 31) * 2));
    sign <<= nz & 1;
    nz >>= 1;
    dst[2] = (idx >> 4 & 3) * (1 + (((int)sign >> 31) * 2));
    sign <<= nz & 1;
    nz >>= 1;
    dst[3] = (idx >> 6 & 3) * (1 + (((int)sign >> 31) * 2));

    return dst + 4;
}

static void vector_pow43(int *coefs, int len)
{
    int i, coef;

    for (i=0; i<len; i++) {
        coef = coefs[i];
        if (coef < 0)
            coef = -(int)ff_cbrt_tab_fixed[-coef];
        else
            coef = (int)ff_cbrt_tab_fixed[coef];
        coefs[i] = coef;
    }
}

static void subband_scale(int *dst, int *src, int scale, int offset, int len)
{
    int ssign = scale < 0 ? -1 : 1;
    int s = FFABS(scale);
    unsigned int round;
    int i, out, c = exp2tab[s & 3];

    s = offset - (s >> 2);

    if (s > 31) {
        for (i=0; i<len; i++) {
            dst[i] = 0;
        }
    } else if (s > 0) {
        round = 1 << (s-1);
        for (i=0; i<len; i++) {
            out = (int)(((int64_t)src[i] * c) >> 32);
            dst[i] = ((int)(out+round) >> s) * ssign;
        }
    } else if (s > -32) {
        s = s + 32;
        round = 1U << (s-1);
        for (i=0; i<len; i++) {
            out = (int)((int64_t)((int64_t)src[i] * c + round) >> s);
            dst[i] = out * (unsigned)ssign;
        }
    } else {
        av_log(NULL, AV_LOG_ERROR, "Overflow in subband_scale()\n");
    }
}

static void noise_scale(int *coefs, int scale, int band_energy, int len)
{
    int ssign = scale < 0 ? -1 : 1;
    int s = FFABS(scale);
    unsigned int round;
    int i, out, c = exp2tab[s & 3];
    int nlz = 0;

    while (band_energy > 0x7fff) {
        band_energy >>= 1;
        nlz++;
    }
    c /= band_energy;
    s = 21 + nlz - (s >> 2);

    if (s > 31) {
        for (i=0; i<len; i++) {
            coefs[i] = 0;
        }
    } else if (s >= 0) {
        round = s ? 1 << (s-1) : 0;
        for (i=0; i<len; i++) {
            out = (int)(((int64_t)coefs[i] * c) >> 32);
            coefs[i] = ((int)(out+round) >> s) * ssign;
        }
    }
    else {
        s = s + 32;
        round = 1 << (s-1);
        for (i=0; i<len; i++) {
            out = (int)((int64_t)((int64_t)coefs[i] * c + round) >> s);
            coefs[i] = out * ssign;
        }
    }
}

static av_always_inline SoftFloat flt16_round(SoftFloat pf)
{
    SoftFloat tmp;
    int s;

    tmp.exp = pf.exp;
    s = pf.mant >> 31;
    tmp.mant = (pf.mant ^ s) - s;
    tmp.mant = (tmp.mant + 0x00200000U) & 0xFFC00000U;
    tmp.mant = (tmp.mant ^ s) - s;

    return tmp;
}

static av_always_inline SoftFloat flt16_even(SoftFloat pf)
{
    SoftFloat tmp;
    int s;

    tmp.exp = pf.exp;
    s = pf.mant >> 31;
    tmp.mant = (pf.mant ^ s) - s;
    tmp.mant = (tmp.mant + 0x001FFFFFU + (tmp.mant & 0x00400000U >> 16)) & 0xFFC00000U;
    tmp.mant = (tmp.mant ^ s) - s;

    return tmp;
}

static av_always_inline SoftFloat flt16_trunc(SoftFloat pf)
{
    SoftFloat pun;
    int s;

    pun.exp = pf.exp;
    s = pf.mant >> 31;
    pun.mant = (pf.mant ^ s) - s;
    pun.mant = pun.mant & 0xFFC00000U;
    pun.mant = (pun.mant ^ s) - s;

    return pun;
}

static av_always_inline void predict(PredictorState *ps, int *coef,
                                     int output_enable)
{
    const SoftFloat a     = { 1023410176, 0 };  // 61.0 / 64
    const SoftFloat alpha = {  973078528, 0 };  // 29.0 / 32
    SoftFloat e0, e1;
    SoftFloat pv;
    SoftFloat k1, k2;
    SoftFloat   r0 = ps->r0,     r1 = ps->r1;
    SoftFloat cor0 = ps->cor0, cor1 = ps->cor1;
    SoftFloat var0 = ps->var0, var1 = ps->var1;
    SoftFloat tmp;

    if (var0.exp > 1 || (var0.exp == 1 && var0.mant > 0x20000000)) {
        k1 = av_mul_sf(cor0, flt16_even(av_div_sf(a, var0)));
    }
    else {
        k1.mant = 0;
        k1.exp = 0;
    }

    if (var1.exp > 1 || (var1.exp == 1 && var1.mant > 0x20000000)) {
        k2 = av_mul_sf(cor1, flt16_even(av_div_sf(a, var1)));
    }
    else {
        k2.mant = 0;
        k2.exp = 0;
    }

    tmp = av_mul_sf(k1, r0);
    pv = flt16_round(av_add_sf(tmp, av_mul_sf(k2, r1)));
    if (output_enable) {
        int shift = 28 - pv.exp;

        if (shift < 31) {
            if (shift > 0) {
                *coef += (unsigned)((pv.mant + (1 << (shift - 1))) >> shift);
            } else
                *coef += (unsigned)pv.mant << -shift;
        }
    }

    e0 = av_int2sf(*coef, 2);
    e1 = av_sub_sf(e0, tmp);

    ps->cor1 = flt16_trunc(av_add_sf(av_mul_sf(alpha, cor1), av_mul_sf(r1, e1)));
    tmp = av_add_sf(av_mul_sf(r1, r1), av_mul_sf(e1, e1));
    tmp.exp--;
    ps->var1 = flt16_trunc(av_add_sf(av_mul_sf(alpha, var1), tmp));
    ps->cor0 = flt16_trunc(av_add_sf(av_mul_sf(alpha, cor0), av_mul_sf(r0, e0)));
    tmp = av_add_sf(av_mul_sf(r0, r0), av_mul_sf(e0, e0));
    tmp.exp--;
    ps->var0 = flt16_trunc(av_add_sf(av_mul_sf(alpha, var0), tmp));

    ps->r1 = flt16_trunc(av_mul_sf(a, av_sub_sf(r0, av_mul_sf(k1, e0))));
    ps->r0 = flt16_trunc(av_mul_sf(a, e0));
}


static const int cce_scale_fixed[8] = {
    Q30(1.0),          //2^(0/8)
    Q30(1.0905077327), //2^(1/8)
    Q30(1.1892071150), //2^(2/8)
    Q30(1.2968395547), //2^(3/8)
    Q30(1.4142135624), //2^(4/8)
    Q30(1.5422108254), //2^(5/8)
    Q30(1.6817928305), //2^(6/8)
    Q30(1.8340080864), //2^(7/8)
};

/**
 * Apply dependent channel coupling (applied before IMDCT).
 *
 * @param   index   index into coupling gain array
 */
static void apply_dependent_coupling_fixed(AACContext *ac,
                                     SingleChannelElement *target,
                                     ChannelElement *cce, int index)
{
    IndividualChannelStream *ics = &cce->ch[0].ics;
    const uint16_t *offsets = ics->swb_offset;
    int *dest = target->coeffs;
    const int *src = cce->ch[0].coeffs;
    int g, i, group, k, idx = 0;
    if (ac->oc[1].m4ac.object_type == AOT_AAC_LTP) {
        av_log(ac->avctx, AV_LOG_ERROR,
               "Dependent coupling is not supported together with LTP\n");
        return;
    }
    for (g = 0; g < ics->num_window_groups; g++) {
        for (i = 0; i < ics->max_sfb; i++, idx++) {
            if (cce->ch[0].band_type[idx] != ZERO_BT) {
                const int gain = cce->coup.gain[index][idx];
                int shift, round, c, tmp;

                if (gain < 0) {
                    c = -cce_scale_fixed[-gain & 7];
                    shift = (-gain-1024) >> 3;
                }
                else {
                    c = cce_scale_fixed[gain & 7];
                    shift = (gain-1024) >> 3;
                }

                if (shift < -31) {
                    // Nothing to do
                } else if (shift < 0) {
                    shift = -shift;
                    round = 1 << (shift - 1);

                    for (group = 0; group < ics->group_len[g]; group++) {
                        for (k = offsets[i]; k < offsets[i + 1]; k++) {
                            tmp = (int)(((int64_t)src[group * 128 + k] * c + \
                                       (int64_t)0x1000000000) >> 37);
                            dest[group * 128 + k] += (tmp + round) >> shift;
                        }
                    }
                }
                else {
                    for (group = 0; group < ics->group_len[g]; group++) {
                        for (k = offsets[i]; k < offsets[i + 1]; k++) {
                            tmp = (int)(((int64_t)src[group * 128 + k] * c + \
                                        (int64_t)0x1000000000) >> 37);
                            dest[group * 128 + k] += tmp * (1U << shift);
                        }
                    }
                }
            }
        }
        dest += ics->group_len[g] * 128;
        src  += ics->group_len[g] * 128;
    }
}

/**
 * Apply independent channel coupling (applied after IMDCT).
 *
 * @param   index   index into coupling gain array
 */
static void apply_independent_coupling_fixed(AACContext *ac,
                                       SingleChannelElement *target,
                                       ChannelElement *cce, int index)
{
    int i, c, shift, round, tmp;
    const int gain = cce->coup.gain[index][0];
    const int *src = cce->ch[0].ret;
    int *dest = target->ret;
    const int len = 1024 << (ac->oc[1].m4ac.sbr == 1);

    c = cce_scale_fixed[gain & 7];
    shift = (gain-1024) >> 3;
    if (shift < -31) {
        return;
    } else if (shift < 0) {
        shift = -shift;
        round = 1 << (shift - 1);

        for (i = 0; i < len; i++) {
            tmp = (int)(((int64_t)src[i] * c + (int64_t)0x1000000000) >> 37);
            dest[i] += (tmp + round) >> shift;
        }
    }
    else {
      for (i = 0; i < len; i++) {
          tmp = (int)(((int64_t)src[i] * c + (int64_t)0x1000000000) >> 37);
          dest[i] += tmp * (1 << shift);
      }
    }
}

#include "aacdec_template.c"

AVCodec ff_aac_fixed_decoder = {
    .name            = "aac_fixed",
    .long_name       = NULL_IF_CONFIG_SMALL("AAC (Advanced Audio Coding)"),
    .type            = AVMEDIA_TYPE_AUDIO,
    .id              = AV_CODEC_ID_AAC,
    .priv_data_size  = sizeof(AACContext),
    .init            = aac_decode_init,
    .close           = aac_decode_close,
    .decode          = aac_decode_frame,
    .sample_fmts     = (const enum AVSampleFormat[]) {
        AV_SAMPLE_FMT_S32P, AV_SAMPLE_FMT_NONE
    },
    .capabilities    = AV_CODEC_CAP_CHANNEL_CONF | AV_CODEC_CAP_DR1,
    .caps_internal   = FF_CODEC_CAP_INIT_THREADSAFE,
    .channel_layouts = aac_channel_layout,
    .profiles        = NULL_IF_CONFIG_SMALL(ff_aac_profiles),
    .flush = flush,
};
