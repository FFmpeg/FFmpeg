/*
 * RealAudio Lossless decoder
 *
 * Copyright (c) 2012 Konstantin Shishkov
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
 * This is a decoder for Real Audio Lossless format.
 * Dedicated to the mastermind behind it, Ralph Wiggum.
 */

#include "libavutil/attributes.h"
#include "libavutil/channel_layout.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"
#include "golomb.h"
#include "unary.h"
#include "ralfdata.h"

#define FILTER_NONE 0
#define FILTER_RAW  642

typedef struct VLCSet {
    VLC filter_params;
    VLC bias;
    VLC coding_mode;
    VLC filter_coeffs[10][11];
    VLC short_codes[15];
    VLC long_codes[125];
} VLCSet;

#define RALF_MAX_PKT_SIZE 8192

typedef struct RALFContext {
    int version;
    int max_frame_size;
    VLCSet sets[3];
    int32_t channel_data[2][4096];

    int     filter_params;   ///< combined filter parameters for the current channel data
    int     filter_length;   ///< length of the filter for the current channel data
    int     filter_bits;     ///< filter precision for the current channel data
    int32_t filter[64];

    unsigned bias[2];        ///< a constant value added to channel data after filtering

    int sample_offset;
    int block_size[1 << 12]; ///< size of the blocks
    int block_pts[1 << 12];  ///< block start time (in milliseconds)

    uint8_t pkt[16384];
    int     has_pkt;
} RALFContext;

#define MAX_ELEMS 644 // no RALF table uses more than that

static av_cold int init_ralf_vlc(VLC *vlc, const uint8_t *data, int elems)
{
    uint8_t  lens[MAX_ELEMS];
    uint16_t codes[MAX_ELEMS];
    int counts[17], prefixes[18];
    int i, cur_len;
    int max_bits = 0;
    int nb = 0;

    for (i = 0; i <= 16; i++)
        counts[i] = 0;
    for (i = 0; i < elems; i++) {
        cur_len  = (nb ? *data & 0xF : *data >> 4) + 1;
        counts[cur_len]++;
        max_bits = FFMAX(max_bits, cur_len);
        lens[i]  = cur_len;
        data    += nb;
        nb      ^= 1;
    }
    prefixes[1] = 0;
    for (i = 1; i <= 16; i++)
        prefixes[i + 1] = (prefixes[i] + counts[i]) << 1;

    for (i = 0; i < elems; i++)
        codes[i] = prefixes[lens[i]]++;

    return ff_vlc_init_sparse(vlc, FFMIN(max_bits, 9), elems,
                              lens, 1, 1, codes, 2, 2, NULL, 0, 0, 0);
}

static av_cold int decode_close(AVCodecContext *avctx)
{
    RALFContext *ctx = avctx->priv_data;
    int i, j, k;

    for (i = 0; i < 3; i++) {
        ff_vlc_free(&ctx->sets[i].filter_params);
        ff_vlc_free(&ctx->sets[i].bias);
        ff_vlc_free(&ctx->sets[i].coding_mode);
        for (j = 0; j < 10; j++)
            for (k = 0; k < 11; k++)
                ff_vlc_free(&ctx->sets[i].filter_coeffs[j][k]);
        for (j = 0; j < 15; j++)
            ff_vlc_free(&ctx->sets[i].short_codes[j]);
        for (j = 0; j < 125; j++)
            ff_vlc_free(&ctx->sets[i].long_codes[j]);
    }

    return 0;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    RALFContext *ctx = avctx->priv_data;
    int i, j, k;
    int ret, channels;

    if (avctx->extradata_size < 24 || memcmp(avctx->extradata, "LSD:", 4)) {
        av_log(avctx, AV_LOG_ERROR, "Extradata is not groovy, dude\n");
        return AVERROR_INVALIDDATA;
    }

    ctx->version = AV_RB16(avctx->extradata + 4);
    if (ctx->version != 0x103) {
        avpriv_request_sample(avctx, "Unknown version %X", ctx->version);
        return AVERROR_PATCHWELCOME;
    }

    channels           = AV_RB16(avctx->extradata + 8);
    avctx->sample_rate = AV_RB32(avctx->extradata + 12);
    if (channels < 1 || channels > 2
        || avctx->sample_rate < 8000 || avctx->sample_rate > 96000) {
        av_log(avctx, AV_LOG_ERROR, "Invalid coding parameters %d Hz %d ch\n",
               avctx->sample_rate, channels);
        return AVERROR_INVALIDDATA;
    }
    avctx->sample_fmt     = AV_SAMPLE_FMT_S16P;
    av_channel_layout_uninit(&avctx->ch_layout);
    av_channel_layout_default(&avctx->ch_layout, channels);

    ctx->max_frame_size = AV_RB32(avctx->extradata + 16);
    if (ctx->max_frame_size > (1 << 20) || !ctx->max_frame_size) {
        av_log(avctx, AV_LOG_ERROR, "invalid frame size %d\n",
               ctx->max_frame_size);
    }
    ctx->max_frame_size = FFMAX(ctx->max_frame_size, avctx->sample_rate);

    for (i = 0; i < 3; i++) {
        ret = init_ralf_vlc(&ctx->sets[i].filter_params, filter_param_def[i],
                            FILTERPARAM_ELEMENTS);
        if (ret < 0)
            return ret;
        ret = init_ralf_vlc(&ctx->sets[i].bias, bias_def[i], BIAS_ELEMENTS);
        if (ret < 0)
            return ret;
        ret = init_ralf_vlc(&ctx->sets[i].coding_mode, coding_mode_def[i],
                            CODING_MODE_ELEMENTS);
        if (ret < 0)
            return ret;
        for (j = 0; j < 10; j++) {
            for (k = 0; k < 11; k++) {
                ret = init_ralf_vlc(&ctx->sets[i].filter_coeffs[j][k],
                                    filter_coeffs_def[i][j][k],
                                    FILTER_COEFFS_ELEMENTS);
                if (ret < 0)
                    return ret;
            }
        }
        for (j = 0; j < 15; j++) {
            ret = init_ralf_vlc(&ctx->sets[i].short_codes[j],
                                short_codes_def[i][j], SHORT_CODES_ELEMENTS);
            if (ret < 0)
                return ret;
        }
        for (j = 0; j < 125; j++) {
            ret = init_ralf_vlc(&ctx->sets[i].long_codes[j],
                                long_codes_def[i][j], LONG_CODES_ELEMENTS);
            if (ret < 0)
                return ret;
        }
    }

    return 0;
}

static inline int extend_code(GetBitContext *gb, int val, int range, int bits)
{
    if (val == 0) {
        val = -range - get_ue_golomb(gb);
    } else if (val == range * 2) {
        val =  range + get_ue_golomb(gb);
    } else {
        val -= range;
    }
    if (bits)
        val = ((unsigned)val << bits) | get_bits(gb, bits);
    return val;
}

static int decode_channel(RALFContext *ctx, GetBitContext *gb, int ch,
                          int length, int mode, int bits)
{
    int i, t;
    int code_params;
    VLCSet *set = ctx->sets + mode;
    VLC *code_vlc; int range, range2, add_bits;
    int *dst = ctx->channel_data[ch];

    ctx->filter_params = get_vlc2(gb, set->filter_params.table, 9, 2);
    if (ctx->filter_params > 1) {
        ctx->filter_bits   = (ctx->filter_params - 2) >> 6;
        ctx->filter_length = ctx->filter_params - (ctx->filter_bits << 6) - 1;
    }

    if (ctx->filter_params == FILTER_RAW) {
        for (i = 0; i < length; i++)
            dst[i] = get_bits(gb, bits);
        ctx->bias[ch] = 0;
        return 0;
    }

    ctx->bias[ch] = get_vlc2(gb, set->bias.table, 9, 2);
    ctx->bias[ch] = extend_code(gb, ctx->bias[ch], 127, 4);

    if (ctx->filter_params == FILTER_NONE) {
        memset(dst, 0, sizeof(*dst) * length);
        return 0;
    }

    if (ctx->filter_params > 1) {
        int cmode = 0, coeff = 0;
        VLC *vlc = set->filter_coeffs[ctx->filter_bits] + 5;

        add_bits = ctx->filter_bits;

        for (i = 0; i < ctx->filter_length; i++) {
            t = get_vlc2(gb, vlc[cmode].table, vlc[cmode].bits, 2);
            t = extend_code(gb, t, 21, add_bits);
            if (!cmode)
                coeff -= 12U << add_bits;
            coeff = (unsigned)t - coeff;
            ctx->filter[i] = coeff;

            cmode = coeff >> add_bits;
            if (cmode < 0) {
                cmode = -1 - av_log2(-cmode);
                if (cmode < -5)
                    cmode = -5;
            } else if (cmode > 0) {
                cmode =  1 + av_log2(cmode);
                if (cmode > 5)
                    cmode = 5;
            }
        }
    }

    code_params = get_vlc2(gb, set->coding_mode.table, set->coding_mode.bits, 2);
    if (code_params >= 15) {
        add_bits = av_clip((code_params / 5 - 3) / 2, 0, 10);
        if (add_bits > 9 && (code_params % 5) != 2)
            add_bits--;
        range    = 10;
        range2   = 21;
        code_vlc = set->long_codes + (code_params - 15);
    } else {
        add_bits = 0;
        range    = 6;
        range2   = 13;
        code_vlc = set->short_codes + code_params;
    }

    for (i = 0; i < length; i += 2) {
        int code1, code2;

        t = get_vlc2(gb, code_vlc->table, code_vlc->bits, 2);
        code1 = t / range2;
        code2 = t % range2;
        dst[i]     = extend_code(gb, code1, range, 0) * (1U << add_bits);
        dst[i + 1] = extend_code(gb, code2, range, 0) * (1U << add_bits);
        if (add_bits) {
            dst[i]     |= get_bits(gb, add_bits);
            dst[i + 1] |= get_bits(gb, add_bits);
        }
    }

    return 0;
}

static void apply_lpc(RALFContext *ctx, int ch, int length, int bits)
{
    int i, j, acc;
    int *audio = ctx->channel_data[ch];
    int bias = 1 << (ctx->filter_bits - 1);
    int max_clip = (1 << bits) - 1, min_clip = -max_clip - 1;

    for (i = 1; i < length; i++) {
        int flen = FFMIN(ctx->filter_length, i);

        acc = 0;
        for (j = 0; j < flen; j++)
            acc += (unsigned)ctx->filter[j] * audio[i - j - 1];
        if (acc < 0) {
            acc = (acc + bias - 1) >> ctx->filter_bits;
            acc = FFMAX(acc, min_clip);
        } else {
            acc = ((unsigned)acc + bias) >> ctx->filter_bits;
            acc = FFMIN(acc, max_clip);
        }
        audio[i] += acc;
    }
}

static int decode_block(AVCodecContext *avctx, GetBitContext *gb,
                        int16_t *dst0, int16_t *dst1)
{
    RALFContext *ctx = avctx->priv_data;
    int len, ch, ret;
    int dmode, mode[2], bits[2];
    int *ch0, *ch1;
    int i;
    unsigned int t, t2;

    len = 12 - get_unary(gb, 0, 6);

    if (len <= 7) len ^= 1; // codes for length = 6 and 7 are swapped
    len = 1 << len;

    if (ctx->sample_offset + len > ctx->max_frame_size) {
        av_log(avctx, AV_LOG_ERROR,
               "Decoder's stomach is crying, it ate too many samples\n");
        return AVERROR_INVALIDDATA;
    }

    if (avctx->ch_layout.nb_channels > 1)
        dmode = get_bits(gb, 2) + 1;
    else
        dmode = 0;

    mode[0] = (dmode == 4) ? 1 : 0;
    mode[1] = (dmode >= 2) ? 2 : 0;
    bits[0] = 16;
    bits[1] = (mode[1] == 2) ? 17 : 16;

    for (ch = 0; ch < avctx->ch_layout.nb_channels; ch++) {
        if ((ret = decode_channel(ctx, gb, ch, len, mode[ch], bits[ch])) < 0)
            return ret;
        if (ctx->filter_params > 1 && ctx->filter_params != FILTER_RAW) {
            ctx->filter_bits += 3;
            apply_lpc(ctx, ch, len, bits[ch]);
        }
        if (get_bits_left(gb) < 0)
            return AVERROR_INVALIDDATA;
    }
    ch0 = ctx->channel_data[0];
    ch1 = ctx->channel_data[1];
    switch (dmode) {
    case 0:
        for (i = 0; i < len; i++)
            dst0[i] = ch0[i] + ctx->bias[0];
        break;
    case 1:
        for (i = 0; i < len; i++) {
            dst0[i] = ch0[i] + ctx->bias[0];
            dst1[i] = ch1[i] + ctx->bias[1];
        }
        break;
    case 2:
        for (i = 0; i < len; i++) {
            ch0[i] += ctx->bias[0];
            dst0[i] = ch0[i];
            dst1[i] = ch0[i] - (ch1[i] + ctx->bias[1]);
        }
        break;
    case 3:
        for (i = 0; i < len; i++) {
            t  = ch0[i] + ctx->bias[0];
            t2 = ch1[i] + ctx->bias[1];
            dst0[i] = t + t2;
            dst1[i] = t;
        }
        break;
    case 4:
        for (i = 0; i < len; i++) {
            t  =   ch1[i] + ctx->bias[1];
            t2 = ((ch0[i] + ctx->bias[0]) * 2) | (t & 1);
            dst0[i] = (int)(t2 + t) / 2;
            dst1[i] = (int)(t2 - t) / 2;
        }
        break;
    }

    ctx->sample_offset += len;

    return 0;
}

static int decode_frame(AVCodecContext *avctx, AVFrame *frame,
                        int *got_frame_ptr, AVPacket *avpkt)
{
    RALFContext *ctx = avctx->priv_data;
    int16_t *samples0;
    int16_t *samples1;
    int ret;
    GetBitContext gb;
    int table_size, table_bytes, num_blocks;
    const uint8_t *src, *block_pointer;
    int src_size;
    int bytes_left;

    if (ctx->has_pkt) {
        ctx->has_pkt = 0;
        table_bytes = (AV_RB16(avpkt->data) + 7) >> 3;
        if (table_bytes + 3 > avpkt->size || avpkt->size > RALF_MAX_PKT_SIZE) {
            av_log(avctx, AV_LOG_ERROR, "Wrong packet's breath smells of wrong data!\n");
            return AVERROR_INVALIDDATA;
        }
        if (memcmp(ctx->pkt, avpkt->data, 2 + table_bytes)) {
            av_log(avctx, AV_LOG_ERROR, "Wrong packet tails are wrong!\n");
            return AVERROR_INVALIDDATA;
        }

        src      = ctx->pkt;
        src_size = RALF_MAX_PKT_SIZE + avpkt->size;
        memcpy(ctx->pkt + RALF_MAX_PKT_SIZE, avpkt->data + 2 + table_bytes,
               avpkt->size - 2 - table_bytes);
    } else {
        if (avpkt->size == RALF_MAX_PKT_SIZE) {
            memcpy(ctx->pkt, avpkt->data, avpkt->size);
            ctx->has_pkt   = 1;
            *got_frame_ptr = 0;

            return avpkt->size;
        }
        src      = avpkt->data;
        src_size = avpkt->size;
    }

    if (src_size < 5) {
        av_log(avctx, AV_LOG_ERROR, "too short packets are too short!\n");
        return AVERROR_INVALIDDATA;
    }
    table_size  = AV_RB16(src);
    table_bytes = (table_size + 7) >> 3;
    if (src_size < table_bytes + 3) {
        av_log(avctx, AV_LOG_ERROR, "short packets are short!\n");
        return AVERROR_INVALIDDATA;
    }
    init_get_bits(&gb, src + 2, table_size);
    num_blocks = 0;
    while (get_bits_left(&gb) > 0) {
        if (num_blocks >= FF_ARRAY_ELEMS(ctx->block_size))
            return AVERROR_INVALIDDATA;
        ctx->block_size[num_blocks] = get_bits(&gb, 13 + avctx->ch_layout.nb_channels);
        if (get_bits1(&gb)) {
            ctx->block_pts[num_blocks] = get_bits(&gb, 9);
        } else {
            ctx->block_pts[num_blocks] = 0;
        }
        num_blocks++;
    }

    frame->nb_samples = ctx->max_frame_size;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;
    samples0 = (int16_t *)frame->data[0];
    samples1 = (int16_t *)frame->data[1];
    block_pointer = src      + table_bytes + 2;
    bytes_left    = src_size - table_bytes - 2;
    ctx->sample_offset = 0;
    for (int i = 0; i < num_blocks; i++) {
        if (bytes_left < ctx->block_size[i]) {
            av_log(avctx, AV_LOG_ERROR, "I'm pedaling backwards\n");
            break;
        }
        init_get_bits(&gb, block_pointer, ctx->block_size[i] * 8);
        if (decode_block(avctx, &gb, samples0 + ctx->sample_offset,
                                     samples1 + ctx->sample_offset) < 0) {
            av_log(avctx, AV_LOG_ERROR, "Sir, I got carsick in your office. Not decoding the rest of packet.\n");
            break;
        }
        block_pointer += ctx->block_size[i];
        bytes_left    -= ctx->block_size[i];
    }

    frame->nb_samples = ctx->sample_offset;
    *got_frame_ptr    = ctx->sample_offset > 0;

    return avpkt->size;
}

static void decode_flush(AVCodecContext *avctx)
{
    RALFContext *ctx = avctx->priv_data;

    ctx->has_pkt = 0;
}


const FFCodec ff_ralf_decoder = {
    .p.name         = "ralf",
    CODEC_LONG_NAME("RealAudio Lossless"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_RALF,
    .priv_data_size = sizeof(RALFContext),
    .init           = decode_init,
    .close          = decode_close,
    FF_CODEC_DECODE_CB(decode_frame),
    .flush          = decode_flush,
    .p.capabilities = AV_CODEC_CAP_CHANNEL_CONF |
                      AV_CODEC_CAP_DR1,
    .p.sample_fmts  = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16P,
                                                      AV_SAMPLE_FMT_NONE },
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
