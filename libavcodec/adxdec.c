/*
 * ADX ADPCM codecs
 * Copyright (c) 2001,2003 BERO
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

static av_cold int adx_decode_init(AVCodecContext *avctx)
{
    ADXContext *c = avctx->priv_data;
    int ret, header_size;

    if (avctx->extradata_size < 24)
        return AVERROR_INVALIDDATA;

    if ((ret = avpriv_adx_decode_header(avctx, avctx->extradata,
                                        avctx->extradata_size, &header_size,
                                        c->coeff)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "error parsing ADX header\n");
        return AVERROR_INVALIDDATA;
    }
    c->channels = avctx->channels;

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
static int adx_decode(ADXContext *c, int16_t *out, const uint8_t *in, int ch)
{
    ADXChannelState *prev = &c->prev[ch];
    GetBitContext gb;
    int scale = AV_RB16(in);
    int i;
    int s0, s1, s2, d;

    /* check if this is an EOF packet */
    if (scale & 0x8000)
        return -1;

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

    return 0;
}

static int adx_decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                            AVPacket *avpkt)
{
    int buf_size        = avpkt->size;
    ADXContext *c       = avctx->priv_data;
    int16_t *samples    = data;
    const uint8_t *buf  = avpkt->data;
    int num_blocks, ch;

    if (c->eof) {
        *data_size = 0;
        return buf_size;
    }

    /* 18 bytes of data are expanded into 32*2 bytes of audio,
       so guard against buffer overflows */
    num_blocks = buf_size / (BLOCK_SIZE * c->channels);
    if (num_blocks > *data_size / (BLOCK_SAMPLES * c->channels)) {
        buf_size   = (*data_size / (BLOCK_SAMPLES * c->channels)) * BLOCK_SIZE;
        num_blocks = buf_size / (BLOCK_SIZE * c->channels);
    }
    if (!buf_size || buf_size % (BLOCK_SIZE * avctx->channels)) {
        if (buf_size >= 4 && (AV_RB16(buf) & 0x8000)) {
            c->eof = 1;
            *data_size = 0;
            return avpkt->size;
        }
        return AVERROR_INVALIDDATA;
    }

    while (num_blocks--) {
        for (ch = 0; ch < c->channels; ch++) {
            if (adx_decode(c, samples + ch, buf, ch)) {
                c->eof = 1;
                buf = avpkt->data + avpkt->size;
                break;
            }
            buf_size -= BLOCK_SIZE;
            buf      += BLOCK_SIZE;
        }
        samples += BLOCK_SAMPLES * c->channels;
    }

    *data_size = (uint8_t*)samples - (uint8_t*)data;
    return buf - avpkt->data;
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
