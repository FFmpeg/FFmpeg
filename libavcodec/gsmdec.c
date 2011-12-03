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

#include "avcodec.h"
#include "get_bits.h"
#include "msgsmdec.h"

#include "gsmdec_template.c"

static av_cold int gsm_init(AVCodecContext *avctx)
{
    GSMContext *s = avctx->priv_data;

    avctx->channels = 1;
    if (!avctx->sample_rate)
        avctx->sample_rate = 8000;
    avctx->sample_fmt = AV_SAMPLE_FMT_S16;

    switch (avctx->codec_id) {
    case CODEC_ID_GSM:
        avctx->frame_size  = GSM_FRAME_SIZE;
        avctx->block_align = GSM_BLOCK_SIZE;
        break;
    case CODEC_ID_GSM_MS:
        avctx->frame_size  = 2 * GSM_FRAME_SIZE;
        avctx->block_align = GSM_MS_BLOCK_SIZE;
    }

    avcodec_get_frame_defaults(&s->frame);
    avctx->coded_frame = &s->frame;

    return 0;
}

static int gsm_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_frame_ptr, AVPacket *avpkt)
{
    GSMContext *s = avctx->priv_data;
    int res;
    GetBitContext gb;
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    int16_t *samples;

    if (buf_size < avctx->block_align) {
        av_log(avctx, AV_LOG_ERROR, "Packet is too small\n");
        return AVERROR_INVALIDDATA;
    }

    /* get output buffer */
    s->frame.nb_samples = avctx->frame_size;
    if ((res = avctx->get_buffer(avctx, &s->frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return res;
    }
    samples = (int16_t *)s->frame.data[0];

    switch (avctx->codec_id) {
    case CODEC_ID_GSM:
        init_get_bits(&gb, buf, buf_size * 8);
        if (get_bits(&gb, 4) != 0xd)
            av_log(avctx, AV_LOG_WARNING, "Missing GSM magic!\n");
        res = gsm_decode_block(avctx, samples, &gb);
        if (res < 0)
            return res;
        break;
    case CODEC_ID_GSM_MS:
        res = ff_msgsm_decode_block(avctx, samples, buf);
        if (res < 0)
            return res;
    }

    *got_frame_ptr   = 1;
    *(AVFrame *)data = s->frame;

    return avctx->block_align;
}

static void gsm_flush(AVCodecContext *avctx)
{
    GSMContext *s = avctx->priv_data;
    memset(s, 0, sizeof(*s));
}

AVCodec ff_gsm_decoder = {
    .name           = "gsm",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_GSM,
    .priv_data_size = sizeof(GSMContext),
    .init           = gsm_init,
    .decode         = gsm_decode_frame,
    .flush          = gsm_flush,
    .capabilities   = CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("GSM"),
};

AVCodec ff_gsm_ms_decoder = {
    .name           = "gsm_ms",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_GSM_MS,
    .priv_data_size = sizeof(GSMContext),
    .init           = gsm_init,
    .decode         = gsm_decode_frame,
    .flush          = gsm_flush,
    .capabilities   = CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("GSM Microsoft variant"),
};
