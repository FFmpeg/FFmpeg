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
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"

/**
 * @file
 * SEGA CRI adx codecs.
 *
 * Reference documents:
 * http://ku-www.ss.titech.ac.jp/~yatsushi/adx.html
 * adx2wav & wav2adx http://www.geocities.co.jp/Playtown/2004/
 */

/**
 * Decode ADX stream header.
 * Sets avctx->channels and avctx->sample_rate.
 *
 * @param      avctx        codec context
 * @param      buf          header data
 * @param      bufsize      data size, should be at least 24 bytes
 * @param[out] header_size  size of ADX header
 * @param[out] coeff        2 LPC coefficients, can be NULL
 * @return data offset or negative error code if header is invalid
 */
static int adx_decode_header(AVCodecContext *avctx, const uint8_t *buf,
                             int bufsize, int *header_size, int *coeff)
{
    int offset, cutoff, channels;

    if (bufsize < 24)
        return AVERROR_INVALIDDATA;

    if (AV_RB16(buf) != 0x8000)
        return AVERROR_INVALIDDATA;
    offset = AV_RB16(buf + 2) + 4;

    /* if copyright string is within the provided data, validate it */
    if (bufsize >= offset && offset >= 6 && memcmp(buf + offset - 6, "(c)CRI", 6))
        return AVERROR_INVALIDDATA;

    /* check for encoding=3 block_size=18, sample_size=4 */
    if (buf[4] != 3 || buf[5] != 18 || buf[6] != 4) {
        avpriv_request_sample(avctx, "Support for this ADX format");
        return AVERROR_PATCHWELCOME;
    }

    /* channels */
    channels = buf[7];
    if (channels <= 0 || channels > 2)
        return AVERROR_INVALIDDATA;

    if (avctx->ch_layout.nb_channels != channels) {
        av_channel_layout_uninit(&avctx->ch_layout);
        avctx->ch_layout.order = AV_CHANNEL_ORDER_UNSPEC;
        avctx->ch_layout.nb_channels = channels;
    }

    /* sample rate */
    avctx->sample_rate = AV_RB32(buf + 8);
    if (avctx->sample_rate < 1 ||
        avctx->sample_rate > INT_MAX / (channels * BLOCK_SIZE * 8))
        return AVERROR_INVALIDDATA;

    /* bit rate */
    avctx->bit_rate = avctx->sample_rate * channels * BLOCK_SIZE * 8 / BLOCK_SAMPLES;

    /* LPC coefficients */
    if (coeff) {
        cutoff = AV_RB16(buf + 16);
        ff_adx_calculate_coeffs(cutoff, avctx->sample_rate, COEFF_BITS, coeff);
    }

    *header_size = offset;
    return 0;
}

static av_cold int adx_decode_init(AVCodecContext *avctx)
{
    ADXContext *c = avctx->priv_data;
    int ret, header_size;

    if (avctx->extradata_size >= 24) {
        if ((ret = adx_decode_header(avctx, avctx->extradata,
                                     avctx->extradata_size, &header_size,
                                     c->coeff)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "error parsing ADX header\n");
            return AVERROR_INVALIDDATA;
        }
        c->channels      = avctx->ch_layout.nb_channels;
        c->header_parsed = 1;
    }

    avctx->sample_fmt = AV_SAMPLE_FMT_S16P;

    return 0;
}

/**
 * Decode 32 samples from 18 bytes.
 *
 * A 16-bit scalar value is applied to 32 residuals, which then have a
 * 2nd-order LPC filter applied to it to form the output signal for a single
 * channel.
 */
static int adx_decode(ADXContext *c, int16_t *out, int offset,
                      const uint8_t *in, int ch)
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
    out += offset;
    s1 = prev->s1;
    s2 = prev->s2;
    for (i = 0; i < BLOCK_SAMPLES; i++) {
        d  = get_sbits(&gb, 4);
        s0 = d * scale + ((c->coeff[0] * s1 + c->coeff[1] * s2) >> COEFF_BITS);
        s2 = s1;
        s1 = av_clip_int16(s0);
        *out++ = s1;
    }
    prev->s1 = s1;
    prev->s2 = s2;

    return 0;
}

static int adx_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                            int *got_frame_ptr, AVPacket *avpkt)
{
    int buf_size        = avpkt->size;
    ADXContext *c       = avctx->priv_data;
    int16_t **samples;
    int samples_offset;
    const uint8_t *buf  = avpkt->data;
    const uint8_t *buf_end = buf + avpkt->size;
    int num_blocks, ch, ret;
    size_t new_extradata_size;
    uint8_t *new_extradata;

    new_extradata = av_packet_get_side_data(avpkt, AV_PKT_DATA_NEW_EXTRADATA,
                                            &new_extradata_size);
    if (new_extradata && new_extradata_size > 0) {
        int header_size;
        if ((ret = adx_decode_header(avctx, new_extradata,
                                     new_extradata_size, &header_size,
                                     c->coeff)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "error parsing new ADX extradata\n");
            return AVERROR_INVALIDDATA;
        }

        c->eof = 0;
    }

    if (c->eof) {
        *got_frame_ptr = 0;
        return buf_size;
    }

    if (!c->header_parsed && buf_size >= 2 && AV_RB16(buf) == 0x8000) {
        int header_size;
        if ((ret = adx_decode_header(avctx, buf, buf_size, &header_size,
                                     c->coeff)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "error parsing ADX header\n");
            return AVERROR_INVALIDDATA;
        }
        c->channels      = avctx->ch_layout.nb_channels;
        c->header_parsed = 1;
        if (buf_size < header_size)
            return AVERROR_INVALIDDATA;
        buf      += header_size;
        buf_size -= header_size;
    }
    if (!c->header_parsed)
        return AVERROR_INVALIDDATA;

    /* calculate number of blocks in the packet */
    num_blocks = buf_size / (BLOCK_SIZE * c->channels);

    /* if the packet is not an even multiple of BLOCK_SIZE, check for an EOF
       packet */
    if (!num_blocks || buf_size % (BLOCK_SIZE * c->channels)) {
        if (buf_size >= 4 && (AV_RB16(buf) & 0x8000)) {
            c->eof = 1;
            *got_frame_ptr = 0;
            return avpkt->size;
        }
        return AVERROR_INVALIDDATA;
    }

    /* get output buffer */
    frame->nb_samples = num_blocks * BLOCK_SAMPLES;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;
    samples = (int16_t **)frame->extended_data;
    samples_offset = 0;

    while (num_blocks--) {
        for (ch = 0; ch < c->channels; ch++) {
            if (buf_end - buf < BLOCK_SIZE || adx_decode(c, samples[ch], samples_offset, buf, ch)) {
                c->eof = 1;
                buf = avpkt->data + avpkt->size;
                break;
            }
            buf_size -= BLOCK_SIZE;
            buf      += BLOCK_SIZE;
        }
        if (!c->eof)
            samples_offset += BLOCK_SAMPLES;
    }

    frame->nb_samples = samples_offset;
    *got_frame_ptr = 1;

    return buf - avpkt->data;
}

static void adx_decode_flush(AVCodecContext *avctx)
{
    ADXContext *c = avctx->priv_data;
    memset(c->prev, 0, sizeof(c->prev));
    c->eof = 0;
}

const FFCodec ff_adpcm_adx_decoder = {
    .p.name         = "adpcm_adx",
    CODEC_LONG_NAME("SEGA CRI ADX ADPCM"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_ADPCM_ADX,
    .priv_data_size = sizeof(ADXContext),
    .init           = adx_decode_init,
    FF_CODEC_DECODE_CB(adx_decode_frame),
    .flush          = adx_decode_flush,
    .p.capabilities = AV_CODEC_CAP_CHANNEL_CONF |
                      AV_CODEC_CAP_DR1,
    .p.sample_fmts  = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16P,
                                                      AV_SAMPLE_FMT_NONE },
};
