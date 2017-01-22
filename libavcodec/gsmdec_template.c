/*
 * gsm 06.10 decoder
 * Copyright (c) 2010 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
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
 * GSM decoder
 */

#include "get_bits.h"
#include "gsm.h"
#include "gsmdec_data.h"

static void apcm_dequant_add(GetBitContext *gb, int16_t *dst, const int *frame_bits)
{
    int i, val;
    int maxidx = get_bits(gb, 6);
    const int16_t *tab = ff_gsm_dequant_tab[maxidx];
    for (i = 0; i < 13; i++) {
        val = get_bits(gb, frame_bits[i]);
        dst[3 * i] += tab[ff_gsm_requant_tab[frame_bits[i]][val]];
    }
}

static inline int gsm_mult(int a, int b)
{
    return (a * b + (1 << 14)) >> 15;
}

static void long_term_synth(int16_t *dst, int lag, int gain_idx)
{
    int i;
    const int16_t *src = dst - lag;
    uint16_t gain = ff_gsm_long_term_gain_tab[gain_idx];
    for (i = 0; i < 40; i++)
        dst[i] = gsm_mult(gain, src[i]);
}

static inline int decode_log_area(int coded, int factor, int offset)
{
    coded <<= 10;
    coded -= offset;
    return gsm_mult(coded, factor) * 2;
}

static av_noinline int get_rrp(int filtered)
{
    int abs = FFABS(filtered);
    if      (abs < 11059) abs <<= 1;
    else if (abs < 20070) abs += 11059;
    else                  abs = (abs >> 2) + 26112;
    return filtered < 0 ? -abs : abs;
}

static int filter_value(int in, int rrp[8], int v[9])
{
    int i;
    for (i = 7; i >= 0; i--) {
        in -= gsm_mult(rrp[i], v[i]);
        v[i + 1] = v[i] + gsm_mult(rrp[i], in);
    }
    v[0] = in;
    return in;
}

static void short_term_synth(GSMContext *ctx, int16_t *dst, const int16_t *src)
{
    int i;
    int rrp[8];
    int *lar = ctx->lar[ctx->lar_idx];
    int *lar_prev = ctx->lar[ctx->lar_idx ^ 1];
    for (i = 0; i < 8; i++)
        rrp[i] = get_rrp((lar_prev[i] >> 2) + (lar_prev[i] >> 1) + (lar[i] >> 2));
    for (i = 0; i < 13; i++)
        dst[i] = filter_value(src[i], rrp, ctx->v);

    for (i = 0; i < 8; i++)
        rrp[i] = get_rrp((lar_prev[i] >> 1) + (lar     [i] >> 1));
    for (i = 13; i < 27; i++)
        dst[i] = filter_value(src[i], rrp, ctx->v);

    for (i = 0; i < 8; i++)
        rrp[i] = get_rrp((lar_prev[i] >> 2) + (lar     [i] >> 1) + (lar[i] >> 2));
    for (i = 27; i < 40; i++)
        dst[i] = filter_value(src[i], rrp, ctx->v);

    for (i = 0; i < 8; i++)
        rrp[i] = get_rrp(lar[i]);
    for (i = 40; i < 160; i++)
        dst[i] = filter_value(src[i], rrp, ctx->v);

    ctx->lar_idx ^= 1;
}

static int postprocess(int16_t *data, int msr)
{
    int i;
    for (i = 0; i < 160; i++) {
        msr = av_clip_int16(data[i] + gsm_mult(msr, 28180));
        data[i] = av_clip_int16(msr * 2) & ~7;
    }
    return msr;
}

static int gsm_decode_block(AVCodecContext *avctx, int16_t *samples,
                            GetBitContext *gb, int mode)
{
    GSMContext *ctx = avctx->priv_data;
    int i;
    int16_t *ref_dst = ctx->ref_buf + 120;
    int *lar = ctx->lar[ctx->lar_idx];
    lar[0] = decode_log_area(get_bits(gb, 6), 13107,  1 << 15);
    lar[1] = decode_log_area(get_bits(gb, 6), 13107,  1 << 15);
    lar[2] = decode_log_area(get_bits(gb, 5), 13107, (1 << 14) + 2048*2);
    lar[3] = decode_log_area(get_bits(gb, 5), 13107, (1 << 14) - 2560*2);
    lar[4] = decode_log_area(get_bits(gb, 4), 19223, (1 << 13) +   94*2);
    lar[5] = decode_log_area(get_bits(gb, 4), 17476, (1 << 13) - 1792*2);
    lar[6] = decode_log_area(get_bits(gb, 3), 31454, (1 << 12) -  341*2);
    lar[7] = decode_log_area(get_bits(gb, 3), 29708, (1 << 12) - 1144*2);

    for (i = 0; i < 4; i++) {
        int lag      = get_bits(gb, 7);
        int gain_idx = get_bits(gb, 2);
        int offset   = get_bits(gb, 2);
        lag = av_clip(lag, 40, 120);
        long_term_synth(ref_dst, lag, gain_idx);
        apcm_dequant_add(gb, ref_dst + offset, ff_gsm_apcm_bits[mode][i]);
        ref_dst += 40;
    }
    memcpy(ctx->ref_buf, ctx->ref_buf + 160, 120 * sizeof(*ctx->ref_buf));
    short_term_synth(ctx, samples, ctx->ref_buf + 120);
    // for optimal speed this could be merged with short_term_synth,
    // not done yet because it is a bit ugly
    ctx->msr = postprocess(samples, ctx->msr);
    return 0;
}
