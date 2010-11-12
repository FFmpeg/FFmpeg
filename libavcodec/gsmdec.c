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

    return 0;
}

static int gsm_decode_frame(AVCodecContext *avctx, void *data,
                            int *data_size, AVPacket *avpkt)
{
    int res;
    GetBitContext gb;
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    int16_t *samples = data;
    int frame_bytes = 2 * avctx->frame_size;

    if (*data_size < frame_bytes)
        return -1;
    *data_size = 0;
    if(buf_size < avctx->block_align)
        return AVERROR_INVALIDDATA;

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
    *data_size = frame_bytes;
    return avctx->block_align;
}

AVCodec gsm_decoder = {
    "gsm",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_GSM,
    sizeof(GSMContext),
    gsm_init,
    NULL,
    NULL,
    gsm_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("GSM"),
};

AVCodec gsm_ms_decoder = {
    "gsm_ms",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_GSM_MS,
    sizeof(GSMContext),
    gsm_init,
    NULL,
    NULL,
    gsm_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("GSM Microsoft variant"),
};
