/*
 * Voxware MetaSound decoder
 * Copyright (c) 2013 Konstantin Shishkov
 * based on TwinVQ decoder
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

#include <inttypes.h>
#include <math.h>
#include <stdint.h>

#include "libavutil/channel_layout.h"
#include "libavutil/float_dsp.h"

#define BITSTREAM_READER_LE
#include "avcodec.h"
#include "fft.h"
#include "get_bits.h"
#include "internal.h"
#include "lsp.h"
#include "sinewin.h"

#include "twinvq.h"
#include "metasound_data.h"

static void add_peak(float period, int width, const float *shape,
                     float ppc_gain, float *speech, int len)
{
    int i, j, center;
    const float *shape_end = shape + len;

    // First peak centered around zero
    for (i = 0; i < width / 2; i++)
        speech[i] += ppc_gain * *shape++;

    for (i = 1; i < ROUNDED_DIV(len, width); i++) {
        center = (int)(i * period + 0.5);
        for (j = -width / 2; j < (width + 1) / 2; j++)
            speech[j + center] += ppc_gain * *shape++;
    }

    // For the last block, be careful not to go beyond the end of the buffer
    center = (int)(i * period + 0.5);
    for (j = -width / 2; j < (width + 1) / 2 && shape < shape_end; j++)
        speech[j + center] += ppc_gain * *shape++;
}

static void decode_ppc(TwinVQContext *tctx, int period_coef, int g_coef,
                       const float *shape, float *speech)
{
    const TwinVQModeTab *mtab = tctx->mtab;
    int isampf       = tctx->avctx->sample_rate / 1000;
    int ibps         = tctx->avctx->bit_rate / (1000 * tctx->avctx->channels);
    int width;

    float ratio = (float)mtab->size / isampf;
    float min_period, max_period, period_range, period;
    float some_mult;

    float pgain_base, pgain_step, ppc_gain;

    if (tctx->avctx->channels == 1) {
        min_period = log2(ratio * 0.2);
        max_period = min_period + log2(6);
    } else {
        min_period = (int)(ratio * 0.2 * 400     + 0.5) / 400.0;
        max_period = (int)(ratio * 0.2 * 400 * 6 + 0.5) / 400.0;
    }
    period_range = max_period - min_period;
    period       = min_period + period_coef * period_range /
                   ((1 << mtab->ppc_period_bit) - 1);
    if (tctx->avctx->channels == 1)
        period = powf(2.0, period);
    else
        period = (int)(period * 400 + 0.5) / 400.0;

    switch (isampf) {
    case  8: some_mult = 2.0; break;
    case 11: some_mult = 3.0; break;
    case 16: some_mult = 3.0; break;
    case 22: some_mult = ibps == 32 ? 2.0 : 4.0; break;
    case 44: some_mult = 8.0; break;
    default: some_mult = 4.0;
    }

    width = (int)(some_mult / (mtab->size / period) * mtab->ppc_shape_len);
    if (isampf == 22 && ibps == 32)
        width = (int)((2.0 / period + 1) * width + 0.5);

    pgain_base = tctx->avctx->channels == 2 ? 25000.0 : 20000.0;
    pgain_step = pgain_base / ((1 << mtab->pgain_bit) - 1);
    ppc_gain   = 1.0 / 8192 *
                 twinvq_mulawinv(pgain_step * g_coef + pgain_step / 2,
                                 pgain_base, TWINVQ_PGAIN_MU);

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

    if (tctx->avctx->channels == 1)
        val = 0.5;
    for (i = 0; i < fw_cb_len; i++)
        for (j = 0; j < bark_n_coef; j++, idx++) {
            float tmp2 = mtab->fmode[ftype].bark_cb[fw_cb_len * in[j] + i] *
                         (1.0 / 2048);
            float st;

            if (tctx->avctx->channels == 1)
                st = use_hist ?
                    tmp2 + val * hist[idx] + 1.0 : tmp2 + 1.0;
            else
                st = use_hist ? (1.0 - val) * tmp2 + val * hist[idx] + 1.0
                              : tmp2 + 1.0;

            hist[idx] = tmp2;
            if (st < 0.1)
                st = 0.1;

            twinvq_memset_float(out, st * gain,
                                mtab->fmode[ftype].bark_tab[idx]);
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

static int metasound_read_bitstream(AVCodecContext *avctx, TwinVQContext *tctx,
                                    const uint8_t *buf, int buf_size)
{
    TwinVQFrameData     *bits;
    const TwinVQModeTab *mtab = tctx->mtab;
    int channels              = tctx->avctx->channels;
    int sub;
    GetBitContext gb;
    int i, j, k, ret;

    if ((ret = init_get_bits8(&gb, buf, buf_size)) < 0)
        return ret;

    for (tctx->cur_frame = 0; tctx->cur_frame < tctx->frames_per_packet;
         tctx->cur_frame++) {
        bits = tctx->bits + tctx->cur_frame;

        bits->window_type = get_bits(&gb, TWINVQ_WINDOW_TYPE_BITS);

        if (bits->window_type > 8) {
            av_log(avctx, AV_LOG_ERROR, "Invalid window type, broken sample?\n");
            return AVERROR_INVALIDDATA;
        }

        bits->ftype = ff_twinvq_wtype_to_ftype_table[tctx->bits[tctx->cur_frame].window_type];

        sub = mtab->fmode[bits->ftype].sub;

        if (bits->ftype != TWINVQ_FT_SHORT && !tctx->is_6kbps)
            get_bits(&gb, 2);

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
                    bits->sub_gain_bits[i * sub + j] =
                        get_bits(&gb, TWINVQ_SUB_GAIN_BITS);
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

        // subframes are aligned to nibbles
        if (get_bits_count(&gb) & 3)
            skip_bits(&gb, 4 - (get_bits_count(&gb) & 3));
    }

    return (get_bits_count(&gb) + 7) / 8;
}

typedef struct MetasoundProps {
    uint32_t tag;
    int      bit_rate;
    int      channels;
    int      sample_rate;
} MetasoundProps;

static const MetasoundProps codec_props[] = {
    { MKTAG('V','X','0','3'),  6, 1,  8000 },
    { MKTAG('V','X','0','4'), 12, 2,  8000 },

    { MKTAG('V','O','X','i'),  8, 1,  8000 },
    { MKTAG('V','O','X','j'), 10, 1, 11025 },
    { MKTAG('V','O','X','k'), 16, 1, 16000 },
    { MKTAG('V','O','X','L'), 24, 1, 22050 },
    { MKTAG('V','O','X','q'), 32, 1, 44100 },
    { MKTAG('V','O','X','r'), 40, 1, 44100 },
    { MKTAG('V','O','X','s'), 48, 1, 44100 },
    { MKTAG('V','O','X','t'), 16, 2,  8000 },
    { MKTAG('V','O','X','u'), 20, 2, 11025 },
    { MKTAG('V','O','X','v'), 32, 2, 16000 },
    { MKTAG('V','O','X','w'), 48, 2, 22050 },
    { MKTAG('V','O','X','x'), 64, 2, 44100 },
    { MKTAG('V','O','X','y'), 80, 2, 44100 },
    { MKTAG('V','O','X','z'), 96, 2, 44100 },

    { 0, 0, 0, 0 }
};

static av_cold int metasound_decode_init(AVCodecContext *avctx)
{
    int isampf, ibps;
    TwinVQContext *tctx = avctx->priv_data;
    uint32_t tag;
    const MetasoundProps *props = codec_props;

    if (!avctx->extradata || avctx->extradata_size < 16) {
        av_log(avctx, AV_LOG_ERROR, "Missing or incomplete extradata\n");
        return AVERROR_INVALIDDATA;
    }

    tag = AV_RL32(avctx->extradata + 12);

    for (;;) {
        if (!props->tag) {
            av_log(avctx, AV_LOG_ERROR, "Could not find tag %08"PRIX32"\n", tag);
            return AVERROR_INVALIDDATA;
        }
        if (props->tag == tag) {
            avctx->sample_rate = props->sample_rate;
            avctx->channels    = props->channels;
            avctx->bit_rate    = props->bit_rate * 1000;
            isampf             = avctx->sample_rate / 1000;
            break;
        }
        props++;
    }

    if (avctx->channels <= 0 || avctx->channels > TWINVQ_CHANNELS_MAX) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported number of channels: %i\n",
               avctx->channels);
        return AVERROR_INVALIDDATA;
    }
    avctx->channel_layout = avctx->channels == 1 ? AV_CH_LAYOUT_MONO
                                                 : AV_CH_LAYOUT_STEREO;

    ibps = avctx->bit_rate / (1000 * avctx->channels);

    switch ((avctx->channels << 16) + (isampf << 8) + ibps) {
    case (1 << 16) + ( 8 << 8) +  6:
        tctx->mtab = &ff_metasound_mode0806;
        break;
    case (2 << 16) + ( 8 << 8) +  6:
        tctx->mtab = &ff_metasound_mode0806s;
        break;
    case (1 << 16) + ( 8 << 8) +  8:
        tctx->mtab = &ff_metasound_mode0808;
        break;
    case (2 << 16) + ( 8 << 8) +  8:
        tctx->mtab = &ff_metasound_mode0808s;
        break;
    case (1 << 16) + (11 << 8) + 10:
        tctx->mtab = &ff_metasound_mode1110;
        break;
    case (2 << 16) + (11 << 8) + 10:
        tctx->mtab = &ff_metasound_mode1110s;
        break;
    case (1 << 16) + (16 << 8) + 16:
        tctx->mtab = &ff_metasound_mode1616;
        break;
    case (2 << 16) + (16 << 8) + 16:
        tctx->mtab = &ff_metasound_mode1616s;
        break;
    case (1 << 16) + (22 << 8) + 24:
        tctx->mtab = &ff_metasound_mode2224;
        break;
    case (2 << 16) + (22 << 8) + 24:
        tctx->mtab = &ff_metasound_mode2224s;
        break;
    case (1 << 16) + (44 << 8) + 32:
        tctx->mtab = &ff_metasound_mode4432;
        break;
    case (2 << 16) + (44 << 8) + 32:
        tctx->mtab = &ff_metasound_mode4432s;
        break;
    case (1 << 16) + (44 << 8) + 40:
        tctx->mtab = &ff_metasound_mode4440;
        break;
    case (2 << 16) + (44 << 8) + 40:
        tctx->mtab = &ff_metasound_mode4440s;
        break;
    case (1 << 16) + (44 << 8) + 48:
        tctx->mtab = &ff_metasound_mode4448;
        break;
    case (2 << 16) + (44 << 8) + 48:
        tctx->mtab = &ff_metasound_mode4448s;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR,
               "This version does not support %d kHz - %d kbit/s/ch mode.\n",
               isampf, ibps);
        return AVERROR(ENOSYS);
    }

    tctx->codec          = TWINVQ_CODEC_METASOUND;
    tctx->read_bitstream = metasound_read_bitstream;
    tctx->dec_bark_env   = dec_bark_env;
    tctx->decode_ppc     = decode_ppc;
    tctx->frame_size     = avctx->bit_rate * tctx->mtab->size
                                           / avctx->sample_rate;
    tctx->is_6kbps       = ibps == 6;

    return ff_twinvq_decode_init(avctx);
}

AVCodec ff_metasound_decoder = {
    .name           = "metasound",
    .long_name      = NULL_IF_CONFIG_SMALL("Voxware MetaSound"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_METASOUND,
    .priv_data_size = sizeof(TwinVQContext),
    .init           = metasound_decode_init,
    .close          = ff_twinvq_decode_close,
    .decode         = ff_twinvq_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                      AV_SAMPLE_FMT_NONE },
};
