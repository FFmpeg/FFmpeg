/*
 * Copyright (C) 2017 foo86
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

#include "libavutil/float_dsp.h"
#include "libavutil/thread.h"
#include "libavutil/mem.h"

#include "internal.h"
#include "get_bits.h"
#include "put_bits.h"
#include "dolby_e.h"
#include "fft.h"

static int skip_input(DBEContext *s, int nb_words)
{
    if (nb_words > s->input_size) {
        av_log(s->avctx, AV_LOG_ERROR, "Packet too short\n");
        return AVERROR_INVALIDDATA;
    }

    s->input      += nb_words * s->word_bytes;
    s->input_size -= nb_words;
    return 0;
}

static int parse_key(DBEContext *s)
{
    if (s->key_present) {
        uint8_t *key = s->input;
        int      ret = skip_input(s, 1);
        if (ret < 0)
            return ret;
        return AV_RB24(key) >> 24 - s->word_bits;
    }
    return 0;
}

static int convert_input(DBEContext *s, int nb_words, int key)
{
    uint8_t *src = s->input;
    uint8_t *dst = s->buffer;
    PutBitContext pb;
    int i;

    av_assert0(nb_words <= 1024u);

    if (nb_words > s->input_size) {
        av_log(s->avctx, AV_LOG_ERROR, "Packet too short\n");
        return AVERROR_INVALIDDATA;
    }

    switch (s->word_bits) {
    case 16:
        for (i = 0; i < nb_words; i++, src += 2, dst += 2)
            AV_WB16(dst, AV_RB16(src) ^ key);
        break;
    case 20:
        init_put_bits(&pb, s->buffer, sizeof(s->buffer));
        for (i = 0; i < nb_words; i++, src += 3)
            put_bits(&pb, 20, AV_RB24(src) >> 4 ^ key);
        flush_put_bits(&pb);
        break;
    case 24:
        for (i = 0; i < nb_words; i++, src += 3, dst += 3)
            AV_WB24(dst, AV_RB24(src) ^ key);
        break;
    default:
        av_assert0(0);
    }

    return init_get_bits(&s->gb, s->buffer, nb_words * s->word_bits);
}

static int parse_metadata(DBEContext *s)
{
    int i, ret, key, mtd_size;

    if ((key = parse_key(s)) < 0)
        return key;
    if ((ret = convert_input(s, 1, key)) < 0)
        return ret;

    skip_bits(&s->gb, 4);
    mtd_size = get_bits(&s->gb, 10);
    if (!mtd_size) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid metadata size\n");
        return AVERROR_INVALIDDATA;
    }

    if ((ret = convert_input(s, mtd_size, key)) < 0)
        return ret;

    skip_bits(&s->gb, 14);
    s->prog_conf = get_bits(&s->gb, 6);
    if (s->prog_conf > MAX_PROG_CONF) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid program configuration\n");
        return AVERROR_INVALIDDATA;
    }

    s->nb_channels = nb_channels_tab[s->prog_conf];
    s->nb_programs = nb_programs_tab[s->prog_conf];

    s->fr_code      = get_bits(&s->gb, 4);
    s->fr_code_orig = get_bits(&s->gb, 4);
    if (!sample_rate_tab[s->fr_code] ||
        !sample_rate_tab[s->fr_code_orig]) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid frame rate code\n");
        return AVERROR_INVALIDDATA;
    }

    skip_bits_long(&s->gb, 88);
    for (i = 0; i < s->nb_channels; i++)
        s->ch_size[i] = get_bits(&s->gb, 10);
    s->mtd_ext_size = get_bits(&s->gb, 8);
    s->meter_size   = get_bits(&s->gb, 8);

    skip_bits_long(&s->gb, 10 * s->nb_programs);
    for (i = 0; i < s->nb_channels; i++) {
        s->rev_id[i]     = get_bits(&s->gb,  4);
        skip_bits1(&s->gb);
        s->begin_gain[i] = get_bits(&s->gb, 10);
        s->end_gain[i]   = get_bits(&s->gb, 10);
    }

    if (get_bits_left(&s->gb) < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Read past end of metadata\n");
        return AVERROR_INVALIDDATA;
    }

    return skip_input(s, mtd_size + 1);
}

static int parse_metadata_ext(DBEContext *s)
{
    if (s->mtd_ext_size)
        return skip_input(s, s->key_present + s->mtd_ext_size + 1);
    return 0;
}

static void unbias_exponents(DBEContext *s, DBEChannel *c, DBEGroup *g)
{
    int mstr_exp[MAX_MSTR_EXP];
    int bias_exp[MAX_BIAS_EXP];
    int i, j, k;

    for (i = 0; i < c->nb_mstr_exp; i++)
        mstr_exp[i] = get_bits(&s->gb, 2) * 6;

    for (i = 0; i < g->nb_exponent; i++)
        bias_exp[i] = get_bits(&s->gb, 5);

    for (i = k = 0; i < c->nb_mstr_exp; i++)
        for (j = 0; j < g->nb_bias_exp[i]; j++, k++)
            c->exponents[g->exp_ofs + k] = mstr_exp[i] + bias_exp[k];
}

static int parse_exponents(DBEContext *s, DBEChannel *c)
{
    DBEGroup *p, *g;
    int i;

    for (i = 0, p = NULL, g = c->groups; i < c->nb_groups; i++, p = g, g++) {
        c->exp_strategy[i] = !i || g->nb_exponent != p->nb_exponent || get_bits1(&s->gb);
        if (c->exp_strategy[i]) {
            unbias_exponents(s, c, g);
        } else {
            memcpy(c->exponents + g->exp_ofs,
                   c->exponents + p->exp_ofs,
                   g->nb_exponent * sizeof(c->exponents[0]));
        }
    }

    return 0;
}

static inline int log_add(int a, int b)
{
    int c = FFABS(a - b) >> 1;
    return FFMAX(a, b) + log_add_tab[FFMIN(c, 211)];
}

static void calc_lowcomp(int *msk_val)
{
    int lwc_val[17] = { 0 };
    int i, j, k;

    for (i = 0; i < 11; i++) {
        int max_j = 0;
        int max_v = INT_MIN;
        int thr   = 0;

        for (j = FFMAX(i - 3, 0), k = 0; j <= i + 3; j++, k++) {
            int v = msk_val[j] + lwc_gain_tab[i][k];
            if (v > max_v) {
                max_j = j;
                max_v = v;
            }
            thr = log_add(thr, v);
        }

        if (msk_val[i] < thr) {
            for (j = FFMAX(max_j - 3, 0),
                 k = FFMAX(3 - max_j, 0);
                 j <= max_j + 3; j++, k++)
                lwc_val[j] += lwc_adj_tab[k];
        }
    }

    for (i = 0; i < 16; i++) {
        int v = FFMAX(lwc_val[i], -512);
        msk_val[i] = FFMAX(msk_val[i] + v, 0);
    }
}

static void bit_allocate(int nb_exponent, int nb_code, int fr_code,
                         int *exp, int *bap,
                         int fg_spc, int fg_ofs, int msk_mod, int snr_ofs)
{
    int msk_val[MAX_BIAS_EXP];
    int psd_val[MAX_BIAS_EXP];
    int fast_leak  = 0;
    int slow_leak  = 0;
    int dc_code    = dc_code_tab[fr_code - 1];
    int ht_code    = ht_code_tab[fr_code - 1];
    int fast_gain  = fast_gain_tab[fg_ofs];
    int slow_decay = slow_decay_tab[dc_code][msk_mod];
    int misc_decay = misc_decay_tab[nb_code][dc_code][msk_mod];
    const uint16_t *slow_gain      = slow_gain_tab[nb_code][msk_mod];
    const uint16_t *fast_decay     = fast_decay_tab[nb_code][dc_code][msk_mod];
    const uint16_t *fast_gain_adj  = fast_gain_adj_tab[nb_code][dc_code];
    const uint16_t *hearing_thresh = hearing_thresh_tab[nb_code][ht_code];
    int i;

    for (i = 0; i < nb_exponent; i++)
        psd_val[i] = (48 - exp[i]) * 64;

    fast_gain_adj += band_ofs_tab[nb_code][fg_spc];
    for (i = 0; i < nb_exponent; i++) {
        fast_leak = log_add(fast_leak  - fast_decay[i],
                            psd_val[i] - fast_gain + fast_gain_adj[i]);
        slow_leak = log_add(slow_leak  - slow_decay,
                            psd_val[i] - slow_gain[i]);
        msk_val[i] = FFMAX(fast_leak, slow_leak);
    }

    fast_leak = 0;
    for (i = nb_exponent - 1; i > band_low_tab[nb_code]; i--) {
        fast_leak = log_add(fast_leak - misc_decay, psd_val[i] - fast_gain);
        msk_val[i] = FFMAX(msk_val[i], fast_leak);
    }

    for (i = 0; i < nb_exponent; i++)
        msk_val[i] = FFMAX(msk_val[i], hearing_thresh[i]);

    if (!nb_code)
        calc_lowcomp(msk_val);

    for (i = 0; i < nb_exponent; i++) {
        int v = 16 * (snr_ofs - 64) + psd_val[i] - msk_val[i] >> 5;
        bap[i] = bap_tab[av_clip_uintp2(v, 6)];
    }
}

static int parse_bit_alloc(DBEContext *s, DBEChannel *c)
{
    DBEGroup *p, *g;
    int bap_strategy[MAX_GROUPS], fg_spc[MAX_GROUPS];
    int fg_ofs[MAX_GROUPS], msk_mod[MAX_GROUPS];
    int i, snr_ofs;

    for (i = 0; i < c->nb_groups; i++) {
        bap_strategy[i] = !i || get_bits1(&s->gb);
        if (bap_strategy[i]) {
             fg_spc[i] = get_bits(&s->gb, 2);
             fg_ofs[i] = get_bits(&s->gb, 3);
            msk_mod[i] = get_bits1(&s->gb);
        } else {
             fg_spc[i] =  fg_spc[i - 1];
             fg_ofs[i] =  fg_ofs[i - 1];
            msk_mod[i] = msk_mod[i - 1];
        }
    }

    if (get_bits1(&s->gb)) {
        avpriv_report_missing_feature(s->avctx, "Delta bit allocation");
        return AVERROR_PATCHWELCOME;
    }

    snr_ofs = get_bits(&s->gb, 8);
    if (!snr_ofs) {
        memset(c->bap, 0, sizeof(c->bap));
        return 0;
    }

    for (i = 0, p = NULL, g = c->groups; i < c->nb_groups; i++, p = g, g++) {
        if (c->exp_strategy[i] || bap_strategy[i]) {
            bit_allocate(g->nb_exponent, g->imdct_idx, s->fr_code,
                         c->exponents + g->exp_ofs, c->bap + g->exp_ofs,
                         fg_spc[i], fg_ofs[i], msk_mod[i], snr_ofs);
        } else {
            memcpy(c->bap + g->exp_ofs,
                   c->bap + p->exp_ofs,
                   g->nb_exponent * sizeof(c->bap[0]));
        }
    }

    return 0;
}

static int parse_indices(DBEContext *s, DBEChannel *c)
{
    DBEGroup *p, *g;
    int i, j;

    for (i = 0, p = NULL, g = c->groups; i < c->nb_groups; i++, p = g, g++) {
        if (get_bits1(&s->gb)) {
            int start = get_bits(&s->gb, 6);

            if (start > g->nb_exponent) {
                av_log(s->avctx, AV_LOG_ERROR, "Invalid start index\n");
                return AVERROR_INVALIDDATA;
            }

            for (j = 0; j < start; j++)
                c->idx[g->exp_ofs + j] = 0;

            for (; j < g->nb_exponent; j++)
                c->idx[g->exp_ofs + j] = get_bits(&s->gb, 2);
        } else if (i && g->nb_exponent == p->nb_exponent) {
            memcpy(c->idx + g->exp_ofs,
                   c->idx + p->exp_ofs,
                   g->nb_exponent * sizeof(c->idx[0]));
        } else {
            memset(c->idx + g->exp_ofs, 0, g->nb_exponent * sizeof(c->idx[0]));
        }
    }

    return 0;
}

static int parse_mantissas(DBEContext *s, DBEChannel *c)
{
    DBEGroup *g;
    int i, j, k;

    for (i = 0, g = c->groups; i < c->nb_groups; i++, g++) {
        float *mnt = c->mantissas + g->mnt_ofs;

        for (j = 0; j < g->nb_exponent; j++) {
            int bap     = c->bap[g->exp_ofs + j];
            int idx     = c->idx[g->exp_ofs + j];
            int size1   = mantissa_size1[bap][idx];
            int count   = g->nb_mantissa[j];
            float exp   = exponent_tab[c->exponents[g->exp_ofs + j]];
            float scale = mantissa_tab1[size1][idx] * exp;

            if (!size1) {
                memset(mnt, 0, count * sizeof(*mnt));
            } else if (idx) {
                int values[100];
                int escape = -(1 << size1 - 1);

                for (k = 0; k < count; k++)
                    values[k] = get_sbits(&s->gb, size1);

                for (k = 0; k < count; k++) {
                    if (values[k] != escape) {
                        mnt[k] = values[k] * scale;
                    } else {
                        int size2 = mantissa_size2[bap][idx];
                        int value = get_sbits(&s->gb, size2);
                        float a = mantissa_tab2[size2][idx];
                        float b = mantissa_tab3[size2][idx];
                        if (value < 0)
                            mnt[k] = ((value + 1) * a - b) * exp;
                        else
                            mnt[k] = (value * a + b) * exp;
                    }
                }
            } else {
                for (k = 0; k < count; k++)
                    mnt[k] = get_sbits(&s->gb, size1) * scale;
            }

            mnt += count;
        }

        for (; j < g->nb_exponent + c->bw_code; j++) {
            memset(mnt, 0, g->nb_mantissa[j] * sizeof(*mnt));
            mnt += g->nb_mantissa[j];
        }
    }

    return 0;
}

static int parse_channel(DBEContext *s, int ch, int seg_id)
{
    DBEChannel *c = &s->channels[seg_id][ch];
    int i, ret;

    if (s->rev_id[ch] > 1) {
        avpriv_report_missing_feature(s->avctx, "Encoder revision %d", s->rev_id[ch]);
        return AVERROR_PATCHWELCOME;
    }

    if (ch == lfe_channel_tab[s->prog_conf]) {
        c->gr_code = 3;
        c->bw_code = 29;
    } else {
        c->gr_code = get_bits(&s->gb, 2);
        c->bw_code = get_bits(&s->gb, 3);
        if (c->gr_code == 3) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid group type code\n");
            return AVERROR_INVALIDDATA;
        }
    }

    c->nb_groups   = nb_groups_tab[c->gr_code];
    c->nb_mstr_exp = nb_mstr_exp_tab[c->gr_code];

    for (i = 0; i < c->nb_groups; i++) {
        c->groups[i] = frm_ofs_tab[seg_id][c->gr_code][i];
        if (c->nb_mstr_exp == 2) {
            c->groups[i].nb_exponent    -= c->bw_code;
            c->groups[i].nb_bias_exp[1] -= c->bw_code;
        }
    }

    if ((ret = parse_exponents(s, c)) < 0)
        return ret;
    if ((ret = parse_bit_alloc(s, c)) < 0)
        return ret;
    if ((ret = parse_indices(s, c)) < 0)
        return ret;
    if ((ret = parse_mantissas(s, c)) < 0)
        return ret;

    if (get_bits_left(&s->gb) < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Read past end of channel %d\n", ch);
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int parse_audio(DBEContext *s, int start, int end, int seg_id)
{
    int ch, ret, key;

    if ((key = parse_key(s)) < 0)
        return key;

    for (ch = start; ch < end; ch++) {
        if (!s->ch_size[ch]) {
            s->channels[seg_id][ch].nb_groups = 0;
            continue;
        }
        if ((ret = convert_input(s, s->ch_size[ch], key)) < 0)
            return ret;
        if ((ret = parse_channel(s, ch, seg_id)) < 0) {
            if (s->avctx->err_recognition & AV_EF_EXPLODE)
                return ret;
            s->channels[seg_id][ch].nb_groups = 0;
        }
        if ((ret = skip_input(s, s->ch_size[ch])) < 0)
            return ret;
    }

    return skip_input(s, 1);
}

static int parse_meter(DBEContext *s)
{
    if (s->meter_size)
        return skip_input(s, s->key_present + s->meter_size + 1);
    return 0;
}

static void imdct_calc(DBEContext *s, DBEGroup *g, float *result, float *values)
{
    FFTContext *imdct = &s->imdct[g->imdct_idx];
    int n   = 1 << imdct_bits_tab[g->imdct_idx];
    int n2  = n >> 1;
    int i;

    switch (g->imdct_phs) {
    case 0:
        imdct->imdct_half(imdct, result, values);
        for (i = 0; i < n2; i++)
            result[n2 + i] = result[n2 - i - 1];
        break;
    case 1:
        imdct->imdct_calc(imdct, result, values);
        break;
    case 2:
        imdct->imdct_half(imdct, result + n2, values);
        for (i = 0; i < n2; i++)
            result[i] = -result[n - i - 1];
        break;
    default:
        av_assert0(0);
    }
}

static void transform(DBEContext *s, DBEChannel *c, float *history, float *output)
{
    LOCAL_ALIGNED_32(float, buffer, [2048]);
    LOCAL_ALIGNED_32(float, result, [1152]);
    DBEGroup *g;
    int i;

    memset(result, 0, 1152 * sizeof(float));
    for (i = 0, g = c->groups; i < c->nb_groups; i++, g++) {
        float *src = buffer + g->src_ofs;
        float *dst = result + g->dst_ofs;
        float *win = window + g->win_ofs;

        imdct_calc(s, g, buffer, c->mantissas + g->mnt_ofs);
        s->fdsp->vector_fmul_add(dst, src, win, dst, g->win_len);
    }

    for (i = 0; i < 256; i++)
        output[i] = history[i] + result[i];
    for (i = 256; i < 896; i++)
        output[i] = result[i];
    for (i = 0; i < 256; i++)
        history[i] = result[896 + i];
}

static void apply_gain(DBEContext *s, int begin, int end, float *output)
{
    if (begin == 960 && end == 960)
        return;

    if (begin == end) {
        s->fdsp->vector_fmul_scalar(output, output, gain_tab[end], FRAME_SAMPLES);
    } else {
        float a = gain_tab[begin] * (1.0f / (FRAME_SAMPLES - 1));
        float b = gain_tab[end  ] * (1.0f / (FRAME_SAMPLES - 1));
        int i;

        for (i = 0; i < FRAME_SAMPLES; i++)
            output[i] *= a * (FRAME_SAMPLES - i - 1) + b * i;
    }
}

static int filter_frame(DBEContext *s, AVFrame *frame)
{
    const uint8_t *reorder;
    int ch, ret;

    if (s->nb_channels == 4)
        reorder = ch_reorder_4;
    else if (s->nb_channels == 6)
        reorder = ch_reorder_6;
    else if (s->nb_programs == 1 && !(s->avctx->request_channel_layout & AV_CH_LAYOUT_NATIVE))
        reorder = ch_reorder_8;
    else
        reorder = ch_reorder_n;

    frame->nb_samples = FRAME_SAMPLES;
    if ((ret = ff_get_buffer(s->avctx, frame, 0)) < 0)
        return ret;

    for (ch = 0; ch < s->nb_channels; ch++) {
        float *output = (float *)frame->extended_data[reorder[ch]];
        transform(s, &s->channels[0][ch], s->history[ch], output);
        transform(s, &s->channels[1][ch], s->history[ch], output + FRAME_SAMPLES / 2);
        apply_gain(s, s->begin_gain[ch], s->end_gain[ch], output);
    }

    return 0;
}

static int dolby_e_decode_frame(AVCodecContext *avctx, void *data,
                                int *got_frame_ptr, AVPacket *avpkt)
{
    DBEContext *s = avctx->priv_data;
    int i, j, hdr, ret;

    if (avpkt->size < 3)
        return AVERROR_INVALIDDATA;

    hdr = AV_RB24(avpkt->data);
    if ((hdr & 0xfffffe) == 0x7888e) {
        s->word_bits = 24;
    } else if ((hdr & 0xffffe0) == 0x788e0) {
        s->word_bits = 20;
    } else if ((hdr & 0xfffe00) == 0x78e00) {
        s->word_bits = 16;
    } else {
        av_log(avctx, AV_LOG_ERROR, "Invalid frame header\n");
        return AVERROR_INVALIDDATA;
    }

    s->word_bytes  = s->word_bits + 7 >> 3;
    s->input       = avpkt->data + s->word_bytes;
    s->input_size  = avpkt->size / s->word_bytes - 1;
    s->key_present = hdr >> 24 - s->word_bits & 1;

    if ((ret = parse_metadata(s)) < 0)
        return ret;

    if (s->nb_programs > 1 && !s->multi_prog_warned) {
        av_log(avctx, AV_LOG_WARNING, "Stream has %d programs (configuration %d), "
               "channels will be output in native order.\n", s->nb_programs, s->prog_conf);
        s->multi_prog_warned = 1;
    }

    switch (s->nb_channels) {
    case 4:
        avctx->channel_layout = AV_CH_LAYOUT_4POINT0;
        break;
    case 6:
        avctx->channel_layout = AV_CH_LAYOUT_5POINT1;
        break;
    case 8:
        avctx->channel_layout = AV_CH_LAYOUT_7POINT1;
        break;
    }

    avctx->channels    = s->nb_channels;
    avctx->sample_rate = sample_rate_tab[s->fr_code];
    avctx->sample_fmt  = AV_SAMPLE_FMT_FLTP;

    i = s->nb_channels / 2;
    j = s->nb_channels;
    if ((ret = parse_audio(s, 0, i, 0)) < 0)
        return ret;
    if ((ret = parse_audio(s, i, j, 0)) < 0)
        return ret;
    if ((ret = parse_metadata_ext(s)) < 0)
        return ret;
    if ((ret = parse_audio(s, 0, i, 1)) < 0)
        return ret;
    if ((ret = parse_audio(s, i, j, 1)) < 0)
        return ret;
    if ((ret = parse_meter(s)) < 0)
        return ret;
    if ((ret = filter_frame(s, data)) < 0)
        return ret;

    *got_frame_ptr = 1;
    return avpkt->size;
}

static av_cold void dolby_e_flush(AVCodecContext *avctx)
{
    DBEContext *s = avctx->priv_data;

    memset(s->history, 0, sizeof(s->history));
}

static av_cold int dolby_e_close(AVCodecContext *avctx)
{
    DBEContext *s = avctx->priv_data;
    int i;

    for (i = 0; i < 3; i++)
        ff_mdct_end(&s->imdct[i]);

    av_freep(&s->fdsp);
    return 0;
}


static av_cold void init_tables(void)
{
    int i, j;

    for (i = 1; i < 17; i++)
        mantissa_tab1[i][0] = 1.0f / (1 << i - 1);

    for (i = 2; i < 16; i++) {
        mantissa_tab1[i][1] = 1.0f  / ((1 << i) - 1);
        mantissa_tab1[i][2] = 0.5f  / ((1 << i) - 1);
        mantissa_tab1[i][3] = 0.25f / ((1 << i) - 1);
    }

    mantissa_tab1[i][1] = 0.5f   / (1 << 15);
    mantissa_tab1[i][2] = 0.75f  / (1 << 15);
    mantissa_tab1[i][3] = 0.875f / (1 << 15);

    for (i = 1; i < 17; i++) {
        mantissa_tab2[i][1] = mantissa_tab1[i][0] * 0.5f;
        mantissa_tab2[i][2] = mantissa_tab1[i][0] * 0.75f;
        mantissa_tab2[i][3] = mantissa_tab1[i][0] * 0.875f;
        for (j = 1; j < 4; j++)
            mantissa_tab3[i][j] = 1.0f / (1 << i) + 1.0f / (1 << j) - 1.0f / (1 << i + j);
    }

    mantissa_tab3[1][3] = 0.6875f;

    for (i = 0; i < 25; i++) {
        exponent_tab[i * 2    ] = 1.0f      / (1 << i);
        exponent_tab[i * 2 + 1] = M_SQRT1_2 / (1 << i);
    }

    for (i = 1; i < 1024; i++)
        gain_tab[i] = exp2f((i - 960) / 64.0f);

    // short 1
    ff_kbd_window_init(window, 3.0f, 128);
    for (i = 0; i < 128; i++)
        window[128 + i] = window[127 - i];

    // start
    for (i = 0; i < 192; i++)
        window[256 + i] = start_window[i];

    // short 2
    for (i = 0; i < 192; i++)
        window[448 + i] = short_window2[i];
    for (i = 0; i < 64; i++)
        window[640 + i] = window[63 - i];

    // short 3
    for (i = 0; i < 64; i++)
        window[704 + i] = short_window3[i];
    for (i = 0; i < 192; i++)
        window[768 + i] = window[64 + i];

    // bridge
    for (i = 0; i < 128; i++)
        window[960 + i] = window[i];
    for (i = 0; i < 64; i++)
        window[1088 + i] = 1.0f;

    // long
    ff_kbd_window_init(window + 1408, 3.0f, 256);
    for (i = 0; i < 640; i++)
        window[1664 + i] = 1.0f;
    for (i = 0; i < 256; i++)
        window[2304 + i] = window[1152 + i] = window[1663 - i];

    // reverse start
    for (i = 0; i < 192; i++)
        window[2560 + i] = window[447 - i];

    // reverse short 2
    for (i = 0; i < 256; i++)
        window[2752 + i] = window[703 - i];

    // reverse short 3
    for (i = 0; i < 256; i++)
        window[3008 + i] = window[959 - i];

    // reverse bridge
    for (i = 0; i < 448; i++)
        window[3264 + i] = window[1407 - i];
}

static av_cold int dolby_e_init(AVCodecContext *avctx)
{
    static AVOnce init_once = AV_ONCE_INIT;
    DBEContext *s = avctx->priv_data;
    int i;

    if (ff_thread_once(&init_once, init_tables))
        return AVERROR_UNKNOWN;

    for (i = 0; i < 3; i++)
        if (ff_mdct_init(&s->imdct[i], imdct_bits_tab[i], 1, 2.0) < 0)
            return AVERROR(ENOMEM);

    if (!(s->fdsp = avpriv_float_dsp_alloc(0)))
        return AVERROR(ENOMEM);

    s->multi_prog_warned = !!(avctx->request_channel_layout & AV_CH_LAYOUT_NATIVE);
    s->avctx = avctx;
    return 0;
}

AVCodec ff_dolby_e_decoder = {
    .name           = "dolby_e",
    .long_name      = NULL_IF_CONFIG_SMALL("Dolby E"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_DOLBY_E,
    .priv_data_size = sizeof(DBEContext),
    .init           = dolby_e_init,
    .decode         = dolby_e_decode_frame,
    .close          = dolby_e_close,
    .flush          = dolby_e_flush,
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_NONE },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
};
