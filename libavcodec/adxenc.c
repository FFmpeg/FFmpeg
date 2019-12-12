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

#include "avcodec.h"
#include "adx.h"
#include "bytestream.h"
#include "internal.h"
#include "put_bits.h"

/**
 * @file
 * SEGA CRI adx codecs.
 *
 * Reference documents:
 * http://ku-www.ss.titech.ac.jp/~yatsushi/adx.html
 * adx2wav & wav2adx http://www.geocities.co.jp/Playtown/2004/
 */

static void adx_encode(ADXContext *c, uint8_t *adx, const int16_t *wav,
                       ADXChannelState *prev, int channels)
{
    PutBitContext pb;
    int scale;
    int i, j;
    int s0, s1, s2, d;
    int max = 0;
    int min = 0;

    s1 = prev->s1;
    s2 = prev->s2;
    for (i = 0, j = 0; j < 32; i += channels, j++) {
        s0 = wav[i];
        d = ((s0 << COEFF_BITS) - c->coeff[0] * s1 - c->coeff[1] * s2) >> COEFF_BITS;
        if (max < d)
            max = d;
        if (min > d)
            min = d;
        s2 = s1;
        s1 = s0;
    }

    if (max == 0 && min == 0) {
        prev->s1 = s1;
        prev->s2 = s2;
        memset(adx, 0, BLOCK_SIZE);
        return;
    }

    if (max / 7 > -min / 8)
        scale = max / 7;
    else
        scale = -min / 8;

    if (scale == 0)
        scale = 1;

    AV_WB16(adx, scale);

    init_put_bits(&pb, adx + 2, 16);

    s1 = prev->s1;
    s2 = prev->s2;
    for (i = 0, j = 0; j < 32; i += channels, j++) {
        d = ((wav[i] << COEFF_BITS) - c->coeff[0] * s1 - c->coeff[1] * s2) >> COEFF_BITS;

        d = av_clip_intp2(ROUNDED_DIV(d, scale), 3);

        put_sbits(&pb, 4, d);

        s0 = ((d << COEFF_BITS) * scale + c->coeff[0] * s1 + c->coeff[1] * s2) >> COEFF_BITS;
        s2 = s1;
        s1 = s0;
    }
    prev->s1 = s1;
    prev->s2 = s2;

    flush_put_bits(&pb);
}

#define HEADER_SIZE 36

static int adx_encode_header(AVCodecContext *avctx, uint8_t *buf, int bufsize)
{
    ADXContext *c = avctx->priv_data;

    bytestream_put_be16(&buf, 0x8000);              /* header signature */
    bytestream_put_be16(&buf, HEADER_SIZE - 4);     /* copyright offset */
    bytestream_put_byte(&buf, 3);                   /* encoding */
    bytestream_put_byte(&buf, BLOCK_SIZE);          /* block size */
    bytestream_put_byte(&buf, 4);                   /* sample size */
    bytestream_put_byte(&buf, avctx->channels);     /* channels */
    bytestream_put_be32(&buf, avctx->sample_rate);  /* sample rate */
    bytestream_put_be32(&buf, 0);                   /* total sample count */
    bytestream_put_be16(&buf, c->cutoff);           /* cutoff frequency */
    bytestream_put_byte(&buf, 3);                   /* version */
    bytestream_put_byte(&buf, 0);                   /* flags */
    bytestream_put_be32(&buf, 0);                   /* unknown */
    bytestream_put_be32(&buf, 0);                   /* loop enabled */
    bytestream_put_be16(&buf, 0);                   /* padding */
    bytestream_put_buffer(&buf, "(c)CRI", 6);       /* copyright signature */

    return HEADER_SIZE;
}

static av_cold int adx_encode_init(AVCodecContext *avctx)
{
    ADXContext *c = avctx->priv_data;

    if (avctx->channels > 2) {
        av_log(avctx, AV_LOG_ERROR, "Invalid number of channels\n");
        return AVERROR(EINVAL);
    }
    avctx->frame_size = BLOCK_SAMPLES;

    /* the cutoff can be adjusted, but this seems to work pretty well */
    c->cutoff = 500;
    ff_adx_calculate_coeffs(c->cutoff, avctx->sample_rate, COEFF_BITS, c->coeff);

    return 0;
}

static int adx_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                            const AVFrame *frame, int *got_packet_ptr)
{
    ADXContext *c          = avctx->priv_data;
    const int16_t *samples = frame ? (const int16_t *)frame->data[0] : NULL;
    uint8_t *dst;
    int ch, out_size, ret;

    if (!samples) {
        if (c->eof)
            return 0;
        if ((ret = ff_alloc_packet2(avctx, avpkt, 18, 0)) < 0)
            return ret;
        c->eof = 1;
        dst = avpkt->data;
        bytestream_put_be16(&dst, 0x8001);
        bytestream_put_be16(&dst, 0x000E);
        bytestream_put_be64(&dst, 0x0);
        bytestream_put_be32(&dst, 0x0);
        bytestream_put_be16(&dst, 0x0);
        *got_packet_ptr = 1;
        return 0;
    }

    out_size = BLOCK_SIZE * avctx->channels + !c->header_parsed * HEADER_SIZE;
    if ((ret = ff_alloc_packet2(avctx, avpkt, out_size, 0)) < 0)
        return ret;
    dst = avpkt->data;

    if (!c->header_parsed) {
        int hdrsize;
        if ((hdrsize = adx_encode_header(avctx, dst, avpkt->size)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "output buffer is too small\n");
            return AVERROR(EINVAL);
        }
        dst      += hdrsize;
        c->header_parsed = 1;
    }

    for (ch = 0; ch < avctx->channels; ch++) {
        adx_encode(c, dst, samples + ch, &c->prev[ch], avctx->channels);
        dst += BLOCK_SIZE;
    }

    avpkt->pts = frame->pts;
    avpkt->duration = frame->nb_samples;
    *got_packet_ptr = 1;
    return 0;
}

AVCodec ff_adpcm_adx_encoder = {
    .name           = "adpcm_adx",
    .long_name      = NULL_IF_CONFIG_SMALL("SEGA CRI ADX ADPCM"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_ADPCM_ADX,
    .priv_data_size = sizeof(ADXContext),
    .init           = adx_encode_init,
    .encode2        = adx_encode_frame,
    .capabilities   = AV_CODEC_CAP_DELAY,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16,
                                                      AV_SAMPLE_FMT_NONE },
};
