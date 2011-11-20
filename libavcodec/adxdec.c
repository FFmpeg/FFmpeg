/*
 * ADX ADPCM codecs
 * Copyright (c) 2001,2003 BERO
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "adx.h"
#include "get_bits.h"

/**
 * @file
 * SEGA CRI adx codecs.
 *
 * Reference documents:
 * http://ku-www.ss.titech.ac.jp/~yatsushi/adx.html
 * adx2wav & wav2adx http://www.geocities.co.jp/Playtown/2004/
 */

#define BLOCK_SIZE      18
#define BLOCK_SAMPLES   32

static av_cold int adx_decode_init(AVCodecContext *avctx)
{
    avctx->sample_fmt = AV_SAMPLE_FMT_S16;
    return 0;
}

/**
 * Decode 32 samples from 18 bytes.
 *
 * A 16-bit scalar value is applied to 32 residuals, which then have a
 * 2nd-order LPC filter applied to it to form the output signal for a single
 * channel.
 */
static void adx_decode(ADXContext *c, int16_t *out, const uint8_t *in, int ch)
{
    ADXChannelState *prev = &c->prev[ch];
    GetBitContext gb;
    int scale = AV_RB16(in);
    int i;
    int s0, s1, s2, d;

    init_get_bits(&gb, in + 2, (BLOCK_SIZE - 2) * 8);
    s1 = prev->s1;
    s2 = prev->s2;
    for (i = 0; i < BLOCK_SAMPLES; i++) {
        d  = get_sbits(&gb, 4);
        s0 = ((d << COEFF_BITS) * scale + c->coeff[0] * s1 + c->coeff[1] * s2) >> COEFF_BITS;
        s2 = s1;
        s1 = av_clip_int16(s0);
        *out = s1;
        out += c->channels;
    }
    prev->s1 = s1;
    prev->s2 = s2;
}

/**
 * Decode stream header.
 *
 * @param avctx   codec context
 * @param buf     packet data
 * @param bufsize packet size
 * @return data offset or negative error code if header is invalid
 */
static int adx_decode_header(AVCodecContext *avctx, const uint8_t *buf,
                             int bufsize)
{
    ADXContext *c = avctx->priv_data;
    int offset, cutoff;

    if (AV_RB16(buf) != 0x8000)
        return AVERROR_INVALIDDATA;
    offset = AV_RB16(buf + 2) + 4;
    if (bufsize < offset || memcmp(buf + offset - 6, "(c)CRI", 6))
        return AVERROR_INVALIDDATA;

    /* check for encoding=3 block_size=18, sample_size=4 */
    if (buf[4] != 3 || buf[5] != 18 || buf[6] != 4) {
        av_log_ask_for_sample(avctx, "unsupported ADX format\n");
        return AVERROR_PATCHWELCOME;
    }

    c->channels = avctx->channels = buf[7];
    if (avctx->channels > 2)
        return AVERROR_INVALIDDATA;
    avctx->sample_rate = AV_RB32(buf + 8);
    if (avctx->sample_rate < 1 ||
        avctx->sample_rate > INT_MAX / (avctx->channels * BLOCK_SIZE * 8))
        return AVERROR_INVALIDDATA;
    avctx->bit_rate = avctx->sample_rate * avctx->channels * BLOCK_SIZE * 8 / BLOCK_SAMPLES;

    cutoff = AV_RB16(buf + 16);
    ff_adx_calculate_coeffs(cutoff, avctx->sample_rate, COEFF_BITS, c->coeff);

    return offset;
}

static int adx_decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                            AVPacket *avpkt)
{
    const uint8_t *buf0 = avpkt->data;
    int buf_size        = avpkt->size;
    ADXContext *c       = avctx->priv_data;
    int16_t *samples    = data;
    const uint8_t *buf  = buf0;
    int rest            = buf_size;
    int num_blocks;

    if (!c->header_parsed) {
        int hdrsize = adx_decode_header(avctx, buf, rest);
        if (hdrsize < 0) {
            av_log(avctx, AV_LOG_ERROR, "invalid stream header\n");
            return hdrsize;
        }
        c->header_parsed = 1;
        buf  += hdrsize;
        rest -= hdrsize;
    }

    /* 18 bytes of data are expanded into 32*2 bytes of audio,
       so guard against buffer overflows */
    num_blocks = (rest + c->in_temp) / (BLOCK_SIZE * c->channels);
    if (num_blocks > *data_size / (BLOCK_SAMPLES * c->channels)) {
        rest = (*data_size / (BLOCK_SAMPLES * c->channels)) * BLOCK_SIZE;
        num_blocks = (rest + c->in_temp) / (BLOCK_SIZE * c->channels);
    }
    if (!num_blocks) {
        av_log(avctx, AV_LOG_ERROR, "packet is too small\n");
        return AVERROR_INVALIDDATA;
    }

    if (c->in_temp) {
        int copysize = BLOCK_SIZE * avctx->channels - c->in_temp;
        memcpy(c->dec_temp + c->in_temp, buf, copysize);
        rest -= copysize;
        buf  += copysize;
        adx_decode(c, samples, c->dec_temp, 0);
        if (avctx->channels == 2)
            adx_decode(c, samples + 1, c->dec_temp + BLOCK_SIZE, 1);
        samples += BLOCK_SAMPLES * c->channels;
        num_blocks--;
    }

    while (num_blocks--) {
        adx_decode(c, samples, buf, 0);
        if (c->channels == 2)
            adx_decode(c, samples + 1, buf + BLOCK_SIZE, 1);
        rest    -= BLOCK_SIZE * c->channels;
        buf     += BLOCK_SIZE * c->channels;
        samples += BLOCK_SAMPLES * c->channels;
    }

    c->in_temp = rest;
    if (rest) {
        memcpy(c->dec_temp, buf, rest);
        buf += rest;
    }
    *data_size = (uint8_t*)samples - (uint8_t*)data;
    return buf - buf0;
}

AVCodec ff_adpcm_adx_decoder = {
    .name           = "adpcm_adx",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_ADPCM_ADX,
    .priv_data_size = sizeof(ADXContext),
    .init           = adx_decode_init,
    .decode         = adx_decode_frame,
    .long_name      = NULL_IF_CONFIG_SMALL("SEGA CRI ADX ADPCM"),
};
