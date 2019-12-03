/*
 * TwinVQ decoder
 * Copyright (c) 2009 Vitor Sessak
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

#include <math.h>
#include <stdint.h>

#include "libavutil/channel_layout.h"
#include "avcodec.h"
#include "get_bits.h"
#include "internal.h"
#include "twinvq.h"
#include "twinvq_data.h"

static const TwinVQModeTab mode_08_08 = {
    {
        { 8, bark_tab_s08_64,  10, tab.fcb08s, 1, 5, tab.cb0808s0, tab.cb0808s1, 18 },
        { 2, bark_tab_m08_256, 20, tab.fcb08m, 2, 5, tab.cb0808m0, tab.cb0808m1, 16 },
        { 1, bark_tab_l08_512, 30, tab.fcb08l, 3, 6, tab.cb0808l0, tab.cb0808l1, 17 }
    },
    512, 12, tab.lsp08, 1, 5, 3, 3, tab.shape08, 8, 28, 20, 6, 40
};

static const TwinVQModeTab mode_11_08 = {
    {
        { 8, bark_tab_s11_64,  10, tab.fcb11s, 1, 5, tab.cb1108s0, tab.cb1108s1, 29 },
        { 2, bark_tab_m11_256, 20, tab.fcb11m, 2, 5, tab.cb1108m0, tab.cb1108m1, 24 },
        { 1, bark_tab_l11_512, 30, tab.fcb11l, 3, 6, tab.cb1108l0, tab.cb1108l1, 27 }
    },
    512, 16, tab.lsp11, 1, 6, 4, 3, tab.shape11, 9, 36, 30, 7, 90
};

static const TwinVQModeTab mode_11_10 = {
    {
        { 8, bark_tab_s11_64,  10, tab.fcb11s, 1, 5, tab.cb1110s0, tab.cb1110s1, 21 },
        { 2, bark_tab_m11_256, 20, tab.fcb11m, 2, 5, tab.cb1110m0, tab.cb1110m1, 18 },
        { 1, bark_tab_l11_512, 30, tab.fcb11l, 3, 6, tab.cb1110l0, tab.cb1110l1, 20 }
    },
    512, 16, tab.lsp11, 1, 6, 4, 3, tab.shape11, 9, 36, 30, 7, 90
};

static const TwinVQModeTab mode_16_16 = {
    {
        { 8, bark_tab_s16_128,  10, tab.fcb16s, 1, 5, tab.cb1616s0, tab.cb1616s1, 16 },
        { 2, bark_tab_m16_512,  20, tab.fcb16m, 2, 5, tab.cb1616m0, tab.cb1616m1, 15 },
        { 1, bark_tab_l16_1024, 30, tab.fcb16l, 3, 6, tab.cb1616l0, tab.cb1616l1, 16 }
    },
    1024, 16, tab.lsp16, 1, 6, 4, 3, tab.shape16, 9, 56, 60, 7, 180
};

static const TwinVQModeTab mode_22_20 = {
    {
        { 8, bark_tab_s22_128,  10, tab.fcb22s_1, 1, 6, tab.cb2220s0, tab.cb2220s1, 18 },
        { 2, bark_tab_m22_512,  20, tab.fcb22m_1, 2, 6, tab.cb2220m0, tab.cb2220m1, 17 },
        { 1, bark_tab_l22_1024, 32, tab.fcb22l_1, 4, 6, tab.cb2220l0, tab.cb2220l1, 18 }
    },
    1024, 16, tab.lsp22_1, 1, 6, 4, 3, tab.shape22_1, 9, 56, 36, 7, 144
};

static const TwinVQModeTab mode_22_24 = {
    {
        { 8, bark_tab_s22_128,  10, tab.fcb22s_1, 1, 6, tab.cb2224s0, tab.cb2224s1, 15 },
        { 2, bark_tab_m22_512,  20, tab.fcb22m_1, 2, 6, tab.cb2224m0, tab.cb2224m1, 14 },
        { 1, bark_tab_l22_1024, 32, tab.fcb22l_1, 4, 6, tab.cb2224l0, tab.cb2224l1, 15 }
    },
    1024, 16, tab.lsp22_1, 1, 6, 4, 3, tab.shape22_1, 9, 56, 36, 7, 144
};

static const TwinVQModeTab mode_22_32 = {
    {
        { 4, bark_tab_s22_128, 10, tab.fcb22s_2, 1, 6, tab.cb2232s0, tab.cb2232s1, 11 },
        { 2, bark_tab_m22_256, 20, tab.fcb22m_2, 2, 6, tab.cb2232m0, tab.cb2232m1, 11 },
        { 1, bark_tab_l22_512, 32, tab.fcb22l_2, 4, 6, tab.cb2232l0, tab.cb2232l1, 12 }
    },
    512, 16, tab.lsp22_2, 1, 6, 4, 4, tab.shape22_2, 9, 56, 36, 7, 72
};

static const TwinVQModeTab mode_44_40 = {
    {
        { 16, bark_tab_s44_128,  10, tab.fcb44s, 1, 6, tab.cb4440s0, tab.cb4440s1, 18 },
        { 4,  bark_tab_m44_512,  20, tab.fcb44m, 2, 6, tab.cb4440m0, tab.cb4440m1, 17 },
        { 1,  bark_tab_l44_2048, 40, tab.fcb44l, 4, 6, tab.cb4440l0, tab.cb4440l1, 17 }
    },
    2048, 20, tab.lsp44, 1, 6, 4, 4, tab.shape44, 9, 84, 54, 7, 432
};

static const TwinVQModeTab mode_44_48 = {
    {
        { 16, bark_tab_s44_128,  10, tab.fcb44s, 1, 6, tab.cb4448s0, tab.cb4448s1, 15 },
        { 4,  bark_tab_m44_512,  20, tab.fcb44m, 2, 6, tab.cb4448m0, tab.cb4448m1, 14 },
        { 1,  bark_tab_l44_2048, 40, tab.fcb44l, 4, 6, tab.cb4448l0, tab.cb4448l1, 14 }
    },
    2048, 20, tab.lsp44, 1, 6, 4, 4, tab.shape44, 9, 84, 54, 7, 432
};

/**
 * Evaluate a * b / 400 rounded to the nearest integer. When, for example,
 * a * b == 200 and the nearest integer is ill-defined, use a table to emulate
 * the following broken float-based implementation used by the binary decoder:
 *
 * @code
 * static int very_broken_op(int a, int b)
 * {
 *    static float test; // Ugh, force gcc to do the division first...
 *
 *    test = a / 400.0;
 *    return b * test + 0.5;
 * }
 * @endcode
 *
 * @note if this function is replaced by just ROUNDED_DIV(a * b, 400.0), the
 * stddev between the original file (before encoding with Yamaha encoder) and
 * the decoded output increases, which leads one to believe that the encoder
 * expects exactly this broken calculation.
 */
static int very_broken_op(int a, int b)
{
    int x = a * b + 200;
    int size;
    const uint8_t *rtab;

    if (x % 400 || b % 5)
        return x / 400;

    x /= 400;

    size = tabs[b / 5].size;
    rtab = tabs[b / 5].tab;
    return x - rtab[size * av_log2(2 * (x - 1) / size) + (x - 1) % size];
}

/**
 * Sum to data a periodic peak of a given period, width and shape.
 *
 * @param period the period of the peak divided by 400.0
 */
static void add_peak(int period, int width, const float *shape,
                     float ppc_gain, float *speech, int len)
{
    int i, j;

    const float *shape_end = shape + len;
    int center;

    // First peak centered around zero
    for (i = 0; i < width / 2; i++)
        speech[i] += ppc_gain * *shape++;

    for (i = 1; i < ROUNDED_DIV(len, width); i++) {
        center = very_broken_op(period, i);
        for (j = -width / 2; j < (width + 1) / 2; j++)
            speech[j + center] += ppc_gain * *shape++;
    }

    // For the last block, be careful not to go beyond the end of the buffer
    center = very_broken_op(period, i);
    for (j = -width / 2; j < (width + 1) / 2 && shape < shape_end; j++)
        speech[j + center] += ppc_gain * *shape++;
}

static void decode_ppc(TwinVQContext *tctx, int period_coef, int g_coef,
                       const float *shape, float *speech)
{
    const TwinVQModeTab *mtab = tctx->mtab;
    int isampf = tctx->avctx->sample_rate /  1000;
    int ibps   = tctx->avctx->bit_rate    / (1000 * tctx->avctx->channels);
    int min_period   = ROUNDED_DIV(40 * 2 * mtab->size, isampf);
    int max_period   = ROUNDED_DIV(40 * 2 * mtab->size * 6, isampf);
    int period_range = max_period - min_period;
    float pgain_step = 25000.0 / ((1 << mtab->pgain_bit) - 1);
    float ppc_gain   = 1.0 / 8192 *
                       twinvq_mulawinv(pgain_step * g_coef +
                                           pgain_step / 2,
                                       25000.0, TWINVQ_PGAIN_MU);

    // This is actually the period multiplied by 400. It is just linearly coded
    // between its maximum and minimum value.
    int period = min_period +
                 ROUNDED_DIV(period_coef * period_range,
                             (1 << mtab->ppc_period_bit) - 1);
    int width;

    if (isampf == 22 && ibps == 32) {
        // For some unknown reason, NTT decided to code this case differently...
        width = ROUNDED_DIV((period + 800) * mtab->peak_per2wid,
                            400 * mtab->size);
    } else
        width = period * mtab->peak_per2wid / (400 * mtab->size);

    add_peak(period, width, shape, ppc_gain, speech, mtab->ppc_shape_len);
}

static void dec_bark_env(TwinVQContext *tctx, const uint8_t *in, int use_hist,
                         int ch, float *out, float gain,
                         enum TwinVQFrameType ftype)
{
    const TwinVQModeTab *mtab = tctx->mtab;
    int i, j;
    float *hist     = tctx->bark_hist[ftype][ch];
    float val       = ((const float []) { 0.4, 0.35, 0.28 })[ftype];
    int bark_n_coef = mtab->fmode[ftype].bark_n_coef;
    int fw_cb_len   = mtab->fmode[ftype].bark_env_size / bark_n_coef;
    int idx         = 0;

    for (i = 0; i < fw_cb_len; i++)
        for (j = 0; j < bark_n_coef; j++, idx++) {
            float tmp2 = mtab->fmode[ftype].bark_cb[fw_cb_len * in[j] + i] *
                         (1.0 / 4096);
            float st   = use_hist ? (1.0 - val) * tmp2 + val * hist[idx] + 1.0
                                  : tmp2 + 1.0;

            hist[idx] = tmp2;
            if (st < -1.0)
                st = 1.0;

            twinvq_memset_float(out, st * gain, mtab->fmode[ftype].bark_tab[idx]);
            out += mtab->fmode[ftype].bark_tab[idx];
        }
}

static void read_cb_data(TwinVQContext *tctx, GetBitContext *gb,
                         uint8_t *dst, enum TwinVQFrameType ftype)
{
    int i;

    for (i = 0; i < tctx->n_div[ftype]; i++) {
        int bs_second_part = (i >= tctx->bits_main_spec_change[ftype]);

        *dst++ = get_bits(gb, tctx->bits_main_spec[0][ftype][bs_second_part]);
        *dst++ = get_bits(gb, tctx->bits_main_spec[1][ftype][bs_second_part]);
    }
}

static int twinvq_read_bitstream(AVCodecContext *avctx, TwinVQContext *tctx,
                                 const uint8_t *buf, int buf_size)
{
    TwinVQFrameData     *bits = &tctx->bits[0];
    const TwinVQModeTab *mtab = tctx->mtab;
    int channels              = tctx->avctx->channels;
    int sub;
    GetBitContext gb;
    int i, j, k, ret;

    if ((ret = init_get_bits8(&gb, buf, buf_size)) < 0)
        return ret;
    skip_bits(&gb, get_bits(&gb, 8));

    bits->window_type = get_bits(&gb, TWINVQ_WINDOW_TYPE_BITS);

    if (bits->window_type > 8) {
        av_log(avctx, AV_LOG_ERROR, "Invalid window type, broken sample?\n");
        return AVERROR_INVALIDDATA;
    }

    bits->ftype = ff_twinvq_wtype_to_ftype_table[tctx->bits[0].window_type];

    sub = mtab->fmode[bits->ftype].sub;

    read_cb_data(tctx, &gb, bits->main_coeffs, bits->ftype);

    for (i = 0; i < channels; i++)
        for (j = 0; j < sub; j++)
            for (k = 0; k < mtab->fmode[bits->ftype].bark_n_coef; k++)
                bits->bark1[i][j][k] =
                    get_bits(&gb, mtab->fmode[bits->ftype].bark_n_bit);

    for (i = 0; i < channels; i++)
        for (j = 0; j < sub; j++)
            bits->bark_use_hist[i][j] = get_bits1(&gb);

    if (bits->ftype == TWINVQ_FT_LONG) {
        for (i = 0; i < channels; i++)
            bits->gain_bits[i] = get_bits(&gb, TWINVQ_GAIN_BITS);
    } else {
        for (i = 0; i < channels; i++) {
            bits->gain_bits[i] = get_bits(&gb, TWINVQ_GAIN_BITS);
            for (j = 0; j < sub; j++)
                bits->sub_gain_bits[i * sub + j] = get_bits(&gb,
                                                       TWINVQ_SUB_GAIN_BITS);
        }
    }

    for (i = 0; i < channels; i++) {
        bits->lpc_hist_idx[i] = get_bits(&gb, mtab->lsp_bit0);
        bits->lpc_idx1[i]     = get_bits(&gb, mtab->lsp_bit1);

        for (j = 0; j < mtab->lsp_split; j++)
            bits->lpc_idx2[i][j] = get_bits(&gb, mtab->lsp_bit2);
    }

    if (bits->ftype == TWINVQ_FT_LONG) {
        read_cb_data(tctx, &gb, bits->ppc_coeffs, 3);
        for (i = 0; i < channels; i++) {
            bits->p_coef[i] = get_bits(&gb, mtab->ppc_period_bit);
            bits->g_coef[i] = get_bits(&gb, mtab->pgain_bit);
        }
    }

    return (get_bits_count(&gb) + 7) / 8;
}

static av_cold int twinvq_decode_init(AVCodecContext *avctx)
{
    int isampf, ibps;
    TwinVQContext *tctx = avctx->priv_data;

    if (!avctx->extradata || avctx->extradata_size < 12) {
        av_log(avctx, AV_LOG_ERROR, "Missing or incomplete extradata\n");
        return AVERROR_INVALIDDATA;
    }
    avctx->channels = AV_RB32(avctx->extradata)     + 1;
    avctx->bit_rate = AV_RB32(avctx->extradata + 4) * 1000;
    isampf          = AV_RB32(avctx->extradata + 8);

    if (isampf < 8 || isampf > 44) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported sample rate\n");
        return AVERROR_INVALIDDATA;
    }
    switch (isampf) {
    case 44:
        avctx->sample_rate = 44100;
        break;
    case 22:
        avctx->sample_rate = 22050;
        break;
    case 11:
        avctx->sample_rate = 11025;
        break;
    default:
        avctx->sample_rate = isampf * 1000;
        break;
    }

    if (avctx->channels <= 0 || avctx->channels > TWINVQ_CHANNELS_MAX) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported number of channels: %i\n",
               avctx->channels);
        return -1;
    }
    avctx->channel_layout = avctx->channels == 1 ? AV_CH_LAYOUT_MONO
                                                 : AV_CH_LAYOUT_STEREO;

    ibps = avctx->bit_rate / (1000 * avctx->channels);
    if (ibps < 8 || ibps > 48) {
        av_log(avctx, AV_LOG_ERROR, "Bad bitrate per channel value %d\n", ibps);
        return AVERROR_INVALIDDATA;
    }

    switch ((isampf << 8) + ibps) {
    case (8 << 8) + 8:
        tctx->mtab = &mode_08_08;
        break;
    case (11 << 8) + 8:
        tctx->mtab = &mode_11_08;
        break;
    case (11 << 8) + 10:
        tctx->mtab = &mode_11_10;
        break;
    case (16 << 8) + 16:
        tctx->mtab = &mode_16_16;
        break;
    case (22 << 8) + 20:
        tctx->mtab = &mode_22_20;
        break;
    case (22 << 8) + 24:
        tctx->mtab = &mode_22_24;
        break;
    case (22 << 8) + 32:
        tctx->mtab = &mode_22_32;
        break;
    case (44 << 8) + 40:
        tctx->mtab = &mode_44_40;
        break;
    case (44 << 8) + 48:
        tctx->mtab = &mode_44_48;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR,
               "This version does not support %d kHz - %d kbit/s/ch mode.\n",
               isampf, isampf);
        return -1;
    }

    tctx->codec          = TWINVQ_CODEC_VQF;
    tctx->read_bitstream = twinvq_read_bitstream;
    tctx->dec_bark_env   = dec_bark_env;
    tctx->decode_ppc     = decode_ppc;
    tctx->frame_size     = avctx->bit_rate * tctx->mtab->size
                                           / avctx->sample_rate + 8;
    tctx->is_6kbps       = 0;
    if (avctx->block_align && avctx->block_align * 8LL / tctx->frame_size > 1) {
        av_log(avctx, AV_LOG_ERROR,
               "VQF TwinVQ should have only one frame per packet\n");
        return AVERROR_INVALIDDATA;
    }

    return ff_twinvq_decode_init(avctx);
}

AVCodec ff_twinvq_decoder = {
    .name           = "twinvq",
    .long_name      = NULL_IF_CONFIG_SMALL("VQF TwinVQ"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_TWINVQ,
    .priv_data_size = sizeof(TwinVQContext),
    .init           = twinvq_decode_init,
    .close          = ff_twinvq_decode_close,
    .decode         = ff_twinvq_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                      AV_SAMPLE_FMT_NONE },
};
