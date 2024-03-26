/*
 * WavArc audio decoder
 * Copyright (c) 2023 Paul B Mahol
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

#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"
#include "bytestream.h"
#include "mathops.h"
#include "unary.h"

typedef struct WavArcContext {
    AVClass *av_class;

    GetBitContext gb;

    int shift;
    int nb_samples;
    int offset;
    int align;

    int eof;
    int skip;
    uint8_t *bitstream;
    int64_t max_framesize;
    int bitstream_size;
    int bitstream_index;

    int pred[2][70];
    int filter[2][70];
    int samples[2][640];
    uint8_t model[256];
    uint16_t freqs[257];
    uint16_t ac_value;
    uint16_t ac_low;
    uint16_t ac_high;
    uint16_t range_high;
    uint16_t range_low;
    uint16_t freq_range;
    int ac_pred[70];
    int ac_out[570];
} WavArcContext;

static av_cold int wavarc_init(AVCodecContext *avctx)
{
    WavArcContext *s = avctx->priv_data;

    if (avctx->extradata_size < 52)
        return AVERROR_INVALIDDATA;
    if (AV_RL32(avctx->extradata + 16) != MKTAG('R','I','F','F'))
        return AVERROR_INVALIDDATA;
    if (AV_RL32(avctx->extradata + 24) != MKTAG('W','A','V','E'))
        return AVERROR_INVALIDDATA;
    if (AV_RL32(avctx->extradata + 28) != MKTAG('f','m','t',' '))
        return AVERROR_INVALIDDATA;
    if (AV_RL16(avctx->extradata + 38) != 1 &&
        AV_RL16(avctx->extradata + 38) != 2)
        return AVERROR_INVALIDDATA;

    av_channel_layout_uninit(&avctx->ch_layout);
    av_channel_layout_default(&avctx->ch_layout, AV_RL16(avctx->extradata + 38));
    avctx->sample_rate = AV_RL32(avctx->extradata + 40);

    s->align = avctx->ch_layout.nb_channels;

    switch (AV_RL16(avctx->extradata + 50)) {
    case  8: avctx->sample_fmt = AV_SAMPLE_FMT_U8P;  break;
    case 16: s->align *= 2;
             avctx->sample_fmt = AV_SAMPLE_FMT_S16P; break;
    }

    s->shift = 0;
    switch (avctx->codec_tag) {
    case MKTAG('0','C','P','Y'):
        s->nb_samples = 640;
        s->offset = 0;
        break;
    case MKTAG('1','D','I','F'):
        s->nb_samples = 256;
        s->offset = 4;
        break;
    case MKTAG('2','S','L','P'):
    case MKTAG('3','N','L','P'):
    case MKTAG('4','A','L','P'):
    case MKTAG('5','E','L','P'):
        s->nb_samples = 570;
        s->offset = 70;
        break;
    default:
        return AVERROR_INVALIDDATA;
    }

    s->max_framesize = s->nb_samples * 16;
    s->bitstream = av_calloc(s->max_framesize + AV_INPUT_BUFFER_PADDING_SIZE, sizeof(*s->bitstream));
    if (!s->bitstream)
        return AVERROR(ENOMEM);

    return 0;
}

static unsigned get_urice(GetBitContext *gb, int k)
{
    unsigned x = get_unary(gb, 1, get_bits_left(gb));
    unsigned y = get_bits_long(gb, k);
    unsigned z = (x << k) | y;

    return z;
}

static int get_srice(GetBitContext *gb, int k)
{
    unsigned z = get_urice(gb, k);

    return (z & 1) ? ~((int)(z >> 1)) : z >> 1;
}

static void do_stereo(WavArcContext *s, int ch, int correlated, int len)
{
    const int nb_samples = s->nb_samples;
    const int shift = s->shift;

    if (ch == 0) {
        if (correlated) {
            for (int n = 0; n < len; n++) {
                s->samples[0][n] = s->samples[0][nb_samples + n] >> shift;
                s->samples[1][n] = s->pred[1][n] >> shift;
            }
        } else {
            for (int n = 0; n < len; n++) {
                s->samples[0][n] = s->samples[0][nb_samples + n] >> shift;
                s->samples[1][n] = s->pred[0][n] >> shift;
            }
        }
    } else {
        if (correlated) {
            for (int n = 0; n < nb_samples; n++)
                s->samples[1][n + len] += (unsigned)s->samples[0][n + len];
        }
        for (int n = 0; n < len; n++) {
            s->pred[0][n] = s->samples[1][nb_samples + n];
            s->pred[1][n] = s->pred[0][n] - (unsigned)s->samples[0][nb_samples + n];
        }
    }
}

static int decode_0cpy(AVCodecContext *avctx,
                       WavArcContext *s, GetBitContext *gb)
{
    const int bits = s->align * 8;

    s->nb_samples = FFMIN(640, get_bits_left(gb) / bits);

    switch (avctx->sample_fmt) {
    case AV_SAMPLE_FMT_U8P:
        for (int n = 0; n < s->nb_samples; n++) {
            for (int ch = 0; ch < avctx->ch_layout.nb_channels; ch++)
                s->samples[ch][n] = get_bits(gb, 8) - 0x80;
        }
        break;
    case AV_SAMPLE_FMT_S16P:
        for (int n = 0; n < s->nb_samples; n++) {
            for (int ch = 0; ch < avctx->ch_layout.nb_channels; ch++)
                s->samples[ch][n] = sign_extend(av_bswap16(get_bits(gb, 16)), 16);
        }
        break;
    }
    return 0;
}

static int decode_1dif(AVCodecContext *avctx,
                       WavArcContext *s, GetBitContext *gb)
{
    int ch, finished, fill, correlated;

    ch = 0;
    finished = 0;
    while (!finished) {
        int *samples = s->samples[ch];
        int k, block_type;

        if (get_bits_left(gb) <= 0)
            return AVERROR_INVALIDDATA;

        block_type = get_urice(gb, 1);
        if (block_type < 4 && block_type >= 0) {
            k = 1 + (avctx->sample_fmt == AV_SAMPLE_FMT_S16P);
            k = get_urice(gb, k) + 1;
            if (k >= 32)
                return AVERROR_INVALIDDATA;
        }

        switch (block_type) {
        case 8:
            s->eof = 1;
            return AVERROR_EOF;
        case 7:
            s->nb_samples = get_bits(gb, 8);
            continue;
        case 6:
            s->shift = get_urice(gb, 2);
            if ((unsigned)s->shift > 31) {
                s->shift = 0;
                return AVERROR_INVALIDDATA;
            }
            continue;
        case 5:
            if (avctx->sample_fmt == AV_SAMPLE_FMT_U8P) {
                fill = (int8_t)get_bits(gb, 8);
                fill -= 0x80;
            } else {
                fill = (int16_t)get_bits(gb, 16);
                fill -= 0x8000;
            }

            for (int n = 0; n < s->nb_samples; n++)
                samples[n + 4] = fill;
            finished = 1;
            break;
        case 4:
            for (int n = 0; n < s->nb_samples; n++)
                samples[n + 4] = 0;
            finished = 1;
            break;
        case 3:
            for (int n = 0; n < s->nb_samples; n++)
                samples[n + 4] = get_srice(gb, k) + (samples[n + 3] - (unsigned)samples[n + 2]) * 3 +
                                          samples[n + 1];
            finished = 1;
            break;
        case 2:
            for (int n = 0; n < s->nb_samples; n++)
                samples[n + 4] = get_srice(gb, k) + (samples[n + 3] * 2U - samples[n + 2]);
            finished = 1;
            break;
        case 1:
            for (int n = 0; n < s->nb_samples; n++)
                samples[n + 4] = get_srice(gb, k) + (unsigned)samples[n + 3];
            finished = 1;
            break;
        case 0:
            for (int n = 0; n < s->nb_samples; n++)
                samples[n + 4] = get_srice(gb, k);
            finished = 1;
            break;
        default:
            return AVERROR_INVALIDDATA;
        }

        if (finished == 1 && avctx->ch_layout.nb_channels == 2) {
            if (ch == 0)
                correlated = get_bits1(gb);
            finished = ch != 0;
            do_stereo(s, ch, correlated, 4);
            ch = 1;
        }
    }

    if (avctx->ch_layout.nb_channels == 1) {
        for (int n = 0; n < 4; n++)
            s->samples[0][n] = s->samples[0][s->nb_samples + n];
    }

    return 0;
}

static int decode_2slp(AVCodecContext *avctx,
                       WavArcContext *s, GetBitContext *gb)
{
    int ch, finished, fill, correlated, order;

    ch = 0;
    finished = 0;
    while (!finished) {
        int *samples = s->samples[ch];
        int k, block_type;

        if (get_bits_left(gb) <= 0)
            return AVERROR_INVALIDDATA;

        block_type = get_urice(gb, 1);
        if (block_type < 5 && block_type >= 0) {
            k = 1 + (avctx->sample_fmt == AV_SAMPLE_FMT_S16P);
            k = get_urice(gb, k) + 1;
            if (k >= 32)
                return AVERROR_INVALIDDATA;
        }

        switch (block_type) {
        case 9:
            s->eof = 1;
            return AVERROR_EOF;
        case 8:
            s->nb_samples = get_urice(gb, 8);
            if (s->nb_samples > 570U) {
                s->nb_samples = 570;
                return AVERROR_INVALIDDATA;
            }
            continue;
        case 7:
            s->shift = get_urice(gb, 2);
            if ((unsigned)s->shift > 31) {
                s->shift = 0;
                return AVERROR_INVALIDDATA;
            }
            continue;
        case 6:
            if (avctx->sample_fmt == AV_SAMPLE_FMT_U8P) {
                fill = (int8_t)get_bits(gb, 8);
                fill -= 0x80;
            } else {
                fill = (int16_t)get_bits(gb, 16);
                fill -= 0x8000;
            }

            for (int n = 0; n < s->nb_samples; n++)
                samples[n + 70] = fill;
            finished = 1;
            break;
        case 5:
            for (int n = 0; n < s->nb_samples; n++)
                samples[n + 70] = 0;
            finished = 1;
            break;
        case 4:
            for (int n = 0; n < s->nb_samples; n++)
                samples[n + 70] = get_srice(gb, k) + (samples[n + 69] - (unsigned)samples[n + 68]) * 3 +
                                           samples[n + 67];
            finished = 1;
            break;
        case 3:
            for (int n = 0; n < s->nb_samples; n++)
                samples[n + 70] = get_srice(gb, k) + (samples[n + 69] * 2U - samples[n + 68]);
            finished = 1;
            break;
        case 2:
            for (int n = 0; n < s->nb_samples; n++)
                samples[n + 70] = get_srice(gb, k);
            finished = 1;
            break;
        case 1:
            for (int n = 0; n < s->nb_samples; n++)
                samples[n + 70] = get_srice(gb, k) + (unsigned)samples[n + 69];
            finished = 1;
            break;
        case 0:
            order = get_urice(gb, 2);
            if ((unsigned)order > FF_ARRAY_ELEMS(s->filter[ch]))
                return AVERROR_INVALIDDATA;
            for (int o = 0; o < order; o++)
                s->filter[ch][o] = get_srice(gb, 2);
            for (int n = 0; n < s->nb_samples; n++) {
                int sum = 15;

                for (int o = 0; o < order; o++)
                    sum += s->filter[ch][o] * (unsigned)samples[n + 70 - o - 1];

                samples[n + 70] = get_srice(gb, k) + (unsigned)(sum >> 4);
            }
            finished = 1;
            break;
        default:
            return AVERROR_INVALIDDATA;
        }

        if (finished == 1 && avctx->ch_layout.nb_channels == 2) {
            if (ch == 0)
                correlated = get_bits1(gb);
            finished = ch != 0;
            do_stereo(s, ch, correlated, 70);
            ch = 1;
        }
    }

    if (avctx->ch_layout.nb_channels == 1) {
        for (int n = 0; n < 70; n++)
            s->samples[0][n] = s->samples[0][s->nb_samples + n];
    }

    return 0;
}

static int ac_init(AVCodecContext *avctx,
                   WavArcContext *s, GetBitContext *gb)
{
    s->ac_low   = 0;
    s->ac_high  = 0xffffu;
    s->ac_value = get_bits(gb, 16);

    s->freq_range = s->freqs[256];
    if (!s->freq_range)
        return AVERROR_INVALIDDATA;
    return 0;
}

static uint16_t ac_get_prob(WavArcContext *s)
{
    return ((s->freq_range - 1) + (s->ac_value - s->ac_low) * (unsigned)s->freq_range) /
           ((s->ac_high - s->ac_low) + 1U);
}

static uint8_t ac_map_symbol(WavArcContext *s, uint16_t prob)
{
    int idx = 255;

    while (prob < s->freqs[idx])
        idx--;

    s->range_high = s->freqs[idx + 1];
    s->range_low  = s->freqs[idx];

    return idx;
}

static int ac_normalize(AVCodecContext *avctx, WavArcContext *s, GetBitContext *gb)
{
    int range;

    if (s->ac_high < s->ac_low)
        goto fail;

    range = (s->ac_high - s->ac_low) + 1;
    s->ac_high = (range * (unsigned)s->range_high) / s->freq_range + s->ac_low - 1;
    s->ac_low += (range * (unsigned)s->range_low)  / s->freq_range;

    if (s->ac_high < s->ac_low)
        goto fail;

    for (;;) {
        if ((s->ac_high & 0x8000) != (s->ac_low & 0x8000)) {
            if (((s->ac_low & 0x4000) == 0) || ((s->ac_high & 0x4000) != 0))
                return 0;
            s->ac_value ^= 0x4000;
            s->ac_low   &= 0x3fff;
            s->ac_high  |= 0x4000;
        }

        s->ac_low = s->ac_low * 2;
        s->ac_high = s->ac_high * 2 | 1;
        if (s->ac_high < s->ac_low)
            goto fail;

        if (get_bits_left(gb) <= 0) {
            av_log(avctx, AV_LOG_ERROR, "overread in arithmetic coder\n");
            goto fail;
        }

        s->ac_value = s->ac_value * 2 + get_bits1(gb);
        if (s->ac_low > s->ac_value || s->ac_high < s->ac_value)
            goto fail;
    }

fail:
    av_log(avctx, AV_LOG_ERROR, "invalid state\n");
    return AVERROR_INVALIDDATA;
}

static void ac_init_model(WavArcContext *s)
{
    memset(s->freqs, 0, sizeof(s->freqs));

    for (int n = 0; n < 256; n++)
        s->freqs[n+1] = s->model[n] + s->freqs[n];
}

static int ac_read_model(AVCodecContext *avctx,
                         WavArcContext *s,
                         GetBitContext *gb)
{
    unsigned start, end;

    memset(s->model, 0, sizeof(s->model));

    start = get_bits(gb, 8);
    end = get_bits(gb, 8);

    for (;;) {
        while (start <= end) {
            if (get_bits_left(gb) < 8)
                return AVERROR_INVALIDDATA;
            s->model[start++] = get_bits(gb, 8);
        }

        if (get_bits_left(gb) < 8)
            return AVERROR_INVALIDDATA;

        start = get_bits(gb, 8);
        if (!start)
            break;

        end = get_bits(gb, 8);
    }

    ac_init_model(s);

    return 0;
}

static int decode_5elp(AVCodecContext *avctx,
                       WavArcContext *s, GetBitContext *gb)
{
    int ch, finished, fill, correlated, order = 0;

    ch = 0;
    finished = 0;
    while (!finished) {
        int *samples = s->samples[ch];
        int *ac_pred = s->ac_pred;
        int *ac_out = s->ac_out;
        int k, block_type;

        if (get_bits_left(gb) <= 0)
            return AVERROR_INVALIDDATA;

        memset(s->ac_out, 0, sizeof(s->ac_out));

        block_type = get_urice(gb, 1);
        av_log(avctx, AV_LOG_DEBUG, "block_type : %d\n", block_type);

        if (block_type >= 0 && block_type <= 7) {
            k = 1 + (avctx->sample_fmt == AV_SAMPLE_FMT_S16P);
            k = get_urice(gb, k) + 1;
            if (k >= 32)
                return AVERROR_INVALIDDATA;
        }

        if (block_type <=  2 || block_type ==  6 || block_type == 13 ||
            block_type == 14 || block_type == 15 || block_type == 19) {
            order = get_urice(gb, 2);
            if ((unsigned)order > FF_ARRAY_ELEMS(s->filter[ch]))
                return AVERROR_INVALIDDATA;
            for (int o = 0; o < order; o++)
                s->filter[ch][o] = get_srice(gb, 2);
        }

        if (block_type >= 0 && block_type <= 7) {
            for (int n = 0; n < s->nb_samples; n++)
                samples[n + 70] = get_srice(gb, k);
        } else {
            for (int n = 0; n < s->nb_samples; n++)
                samples[n + 70] = 0;
        }

        if (block_type >= 13 && block_type <= 20) {
            const int ac_size = get_bits(gb, 12);
            const int ac_pos = get_bits_count(gb);
            GetBitContext ac_gb = *gb;
            int ret;

            skip_bits_long(gb, ac_size);
            ret = ac_read_model(avctx, s, &ac_gb);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "bad arithmetic model\n");
                return ret;
            }

            ret = ac_init(avctx, s, &ac_gb);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "cannot init arithmetic decoder\n");
                return ret;
            }

            for (int n = 0; n < s->nb_samples; n++) {
                uint16_t prob = ac_get_prob(s);
                int ac = ac_map_symbol(s, prob);
                ac_out[n] = ac - 0x80;
                if ((ret = ac_normalize(avctx, s, &ac_gb)) < 0)
                    return ret;
            }

            if (get_bits_count(&ac_gb) != ac_pos + ac_size) {
                av_log(avctx, AV_LOG_DEBUG, "over/under-read in arithmetic coder: %d\n",
                       ac_pos + ac_size - get_bits_count(&ac_gb));
            }
        }

        switch (block_type) {
        case 12:
            s->eof = 1;
            return AVERROR_EOF;
        case 11:
            s->nb_samples = get_urice(gb, 8);
            if (s->nb_samples > 570U) {
                s->nb_samples = 570;
                return AVERROR_INVALIDDATA;
            }
            continue;
        case 10:
            s->shift = get_urice(gb, 2);
            if ((unsigned)s->shift > 31) {
                s->shift = 0;
                return AVERROR_INVALIDDATA;
            }
            continue;
        case 9:
            if (avctx->sample_fmt == AV_SAMPLE_FMT_U8P) {
                fill = (int8_t)get_bits(gb, 8);
                fill -= 0x80;
            } else {
                fill = (int16_t)get_bits(gb, 16);
                fill -= 0x8000;
            }

            for (int n = 0; n < s->nb_samples; n++)
                samples[n + 70] = fill;
            finished = 1;
            break;
        case 8:
            for (int n = 0; n < s->nb_samples; n++)
                samples[n + 70] = 0;
            finished = 1;
            break;
        case 20:
        case 7:
            for (int n = 0; n < s->nb_samples; n++)
                samples[n + 70] += ac_out[n] + samples[n + 69] * 3U - samples[n + 68] * 3U + samples[n + 67];
            finished = 1;
            break;
        case 19:
        case 6:
            for (int n = 0; n < 70; n++) {
                ac_pred[n] = samples[n];
                samples[n] = 0;
            }

            for (int n = 0; n < s->nb_samples; n++) {
                int sum = 15;

                for (int o = 0; o < order; o++)
                    sum += s->filter[ch][o] * (unsigned)samples[n + 70 - o - 1];

                samples[n + 70] += ac_out[n] + (sum >> 4);
            }

            for (int n = 0; n < 70; n++)
                samples[n] = ac_pred[n];

            for (int n = 0; n < s->nb_samples; n++)
                samples[n + 70] += ac_out[n] + samples[n + 69] * 3U - samples[n + 68] * 3U + samples[n + 67];

            finished = 1;
            break;
        case 18:
        case 5:
            for (int n = 0; n < s->nb_samples; n++)
                samples[n + 70] += ac_out[n] + samples[n + 69] * 2U - samples[n + 68];
            finished = 1;
            break;
        case 17:
        case 4:
            for (int n = 0; n < s->nb_samples; n++)
                samples[n + 70] += ac_out[n];
            finished = 1;
            break;
        case 16:
        case 3:
            for (int n = 0; n < s->nb_samples; n++)
                samples[n + 70] += ac_out[n] + (unsigned)samples[n + 69];
            finished = 1;
            break;
        case 15:
        case 2:
            for (int n = 0; n < 70; n++) {
                ac_pred[n] = samples[n];
                samples[n] = 0;
            }

            for (int n = 0; n < s->nb_samples; n++) {
                int sum = 15;

                for (int o = 0; o < order; o++)
                    sum += s->filter[ch][o] * (unsigned)samples[n + 70 - o - 1];

                samples[n + 70] += ac_out[n] + (sum >> 4);
            }

            for (int n = 0; n < 70; n++)
                samples[n] = ac_pred[n];

            for (int n = 0; n < s->nb_samples; n++)
                samples[n + 70] += samples[n + 69] * 2U - samples[n + 68];

            finished = 1;
            break;
        case 14:
        case 1:
            for (int n = 0; n < 70; n++) {
                ac_pred[n] = samples[n];
                samples[n] = 0;
            }

            for (int n = 0; n < s->nb_samples; n++) {
                int sum = 15;

                for (int o = 0; o < order; o++)
                    sum += s->filter[ch][o] * (unsigned)samples[n + 70 - o - 1];

                samples[n + 70] += (unsigned)ac_out[n] + (sum >> 4);
            }

            for (int n = 0; n < 70; n++)
                samples[n] = ac_pred[n];

            for (int n = 0; n < s->nb_samples; n++)
                samples[n + 70] += (unsigned)samples[n + 69];

            finished = 1;
            break;
        case 13:
        case 0:
            for (int n = 0; n < s->nb_samples; n++) {
                int sum = 15;

                for (int o = 0; o < order; o++)
                    sum += s->filter[ch][o] * (unsigned)samples[n + 70 - o - 1];

                samples[n + 70] += (unsigned)ac_out[n] + (sum >> 4);
            }
            finished = 1;
            break;
        default:
            return AVERROR_INVALIDDATA;
        }

        if (finished == 1 && avctx->ch_layout.nb_channels == 2) {
            if (ch == 0)
                correlated = get_bits1(gb);
            finished = ch != 0;
            do_stereo(s, ch, correlated, 70);
            ch = 1;
        }
    }

    if (avctx->ch_layout.nb_channels == 1) {
        for (int n = 0; n < 70; n++)
            s->samples[0][n] = s->samples[0][s->nb_samples + n];
    }

    return 0;
}

static int wavarc_decode(AVCodecContext *avctx, AVFrame *frame,
                         int *got_frame_ptr, AVPacket *pkt)
{
    WavArcContext *s = avctx->priv_data;
    GetBitContext *gb = &s->gb;
    int buf_size, input_buf_size;
    const uint8_t *buf;
    int ret, n;

    if ((!pkt->size && !s->bitstream_size) || s->nb_samples == 0 || s->eof) {
        *got_frame_ptr = 0;
        return pkt->size;
    }

    buf_size = FFMIN(pkt->size, s->max_framesize - s->bitstream_size);
    input_buf_size = buf_size;
    if (s->bitstream_index + s->bitstream_size + buf_size + AV_INPUT_BUFFER_PADDING_SIZE > s->max_framesize) {
        memmove(s->bitstream, &s->bitstream[s->bitstream_index], s->bitstream_size);
        s->bitstream_index = 0;
    }
    if (pkt->data)
        memcpy(&s->bitstream[s->bitstream_index + s->bitstream_size], pkt->data, buf_size);
    buf                = &s->bitstream[s->bitstream_index];
    buf_size          += s->bitstream_size;
    s->bitstream_size  = buf_size;
    if (buf_size < s->max_framesize && pkt->data) {
        *got_frame_ptr = 0;
        return input_buf_size;
    }

    if ((ret = init_get_bits8(gb, buf, buf_size)) < 0)
        goto fail;
    skip_bits(gb, s->skip);

    switch (avctx->codec_tag) {
    case MKTAG('0','C','P','Y'):
        ret = decode_0cpy(avctx, s, gb);
        break;
    case MKTAG('1','D','I','F'):
        ret = decode_1dif(avctx, s, gb);
        break;
    case MKTAG('2','S','L','P'):
    case MKTAG('3','N','L','P'):
    case MKTAG('4','A','L','P'):
        ret = decode_2slp(avctx, s, gb);
        break;
    case MKTAG('5','E','L','P'):
        ret = decode_5elp(avctx, s, gb);
        break;
    default:
        ret = AVERROR_INVALIDDATA;
    }

    if (ret < 0)
        goto fail;

    s->skip = get_bits_count(gb) - 8 * (get_bits_count(gb) / 8);
    n = get_bits_count(gb) / 8;

    if (n > buf_size) {
fail:
        s->bitstream_size = 0;
        s->bitstream_index = 0;
        if (ret == AVERROR_EOF)
            return 0;
        return AVERROR_INVALIDDATA;
    }

    frame->nb_samples = s->nb_samples;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        goto fail;

    switch (avctx->sample_fmt) {
    case AV_SAMPLE_FMT_U8P:
        for (int ch = 0; ch < avctx->ch_layout.nb_channels; ch++) {
            uint8_t *dst = (uint8_t *)frame->extended_data[ch];
            const int *src = s->samples[ch] + s->offset;

            for (int n = 0; n < frame->nb_samples; n++)
                dst[n] = src[n] * (1U << s->shift) + 0x80U;
        }
        break;
    case AV_SAMPLE_FMT_S16P:
        for (int ch = 0; ch < avctx->ch_layout.nb_channels; ch++) {
            int16_t *dst = (int16_t *)frame->extended_data[ch];
            const int *src = s->samples[ch] + s->offset;

            for (int n = 0; n < frame->nb_samples; n++)
                dst[n] = src[n] * (1U << s->shift);
        }
        break;
    }

    *got_frame_ptr = 1;

    if (s->bitstream_size) {
        s->bitstream_index += n;
        s->bitstream_size  -= n;
        return input_buf_size;
    }

    return n;
}

static av_cold int wavarc_close(AVCodecContext *avctx)
{
    WavArcContext *s = avctx->priv_data;

    av_freep(&s->bitstream);
    s->bitstream_size = 0;

    return 0;
}

const FFCodec ff_wavarc_decoder = {
    .p.name           = "wavarc",
    CODEC_LONG_NAME("Waveform Archiver"),
    .p.type           = AVMEDIA_TYPE_AUDIO,
    .p.id             = AV_CODEC_ID_WAVARC,
    .priv_data_size   = sizeof(WavArcContext),
    .init             = wavarc_init,
    FF_CODEC_DECODE_CB(wavarc_decode),
    .close            = wavarc_close,
    .p.capabilities   = AV_CODEC_CAP_DR1 |
#if FF_API_SUBFRAMES
                        AV_CODEC_CAP_SUBFRAMES |
#endif
                        AV_CODEC_CAP_DELAY,
    .p.sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_U8P,
                                                        AV_SAMPLE_FMT_S16P,
                                                        AV_SAMPLE_FMT_NONE },
};
