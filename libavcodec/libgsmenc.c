/*
 * Interface to libgsm for GSM encoding
 * Copyright (c) 2005 Alban Bedel <albeu@free.fr>
 * Copyright (c) 2006, 2007 Michel Bardiaux <mbardiaux@mediaxim.be>
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
 * Interface to libgsm for GSM encoding
 */

// The idiosyncrasies of GSM-in-WAV are explained at http://kbs.cs.tu-berlin.de/~jutta/toast.html

#include "config.h"
#if HAVE_GSM_H
#include <gsm.h>
#else
#include <gsm/gsm.h>
#endif

#include "libavutil/common.h"

#include "avcodec.h"
#include "internal.h"
#include "gsm.h"

static av_cold int libgsm_encode_close(AVCodecContext *avctx) {
    gsm_destroy(avctx->priv_data);
    avctx->priv_data = NULL;
    return 0;
}

static av_cold int libgsm_encode_init(AVCodecContext *avctx) {
    if (avctx->channels > 1) {
        av_log(avctx, AV_LOG_ERROR, "Mono required for GSM, got %d channels\n",
               avctx->channels);
        return -1;
    }

    if (avctx->sample_rate != 8000) {
        av_log(avctx, AV_LOG_ERROR, "Sample rate 8000Hz required for GSM, got %dHz\n",
               avctx->sample_rate);
        if (avctx->strict_std_compliance > FF_COMPLIANCE_UNOFFICIAL)
            return -1;
    }
    if (avctx->bit_rate != 13000 /* Official */ &&
        avctx->bit_rate != 13200 /* Very common */ &&
        avctx->bit_rate != 0 /* Unknown; a.o. mov does not set bitrate when decoding */ ) {
        av_log(avctx, AV_LOG_ERROR, "Bitrate 13000bps required for GSM, got %"PRId64"bps\n",
               avctx->bit_rate);
        if (avctx->strict_std_compliance > FF_COMPLIANCE_UNOFFICIAL)
            return -1;
    }

    avctx->priv_data = gsm_create();
    if (!avctx->priv_data)
        goto error;

    switch(avctx->codec_id) {
    case AV_CODEC_ID_GSM:
        avctx->frame_size = GSM_FRAME_SIZE;
        avctx->block_align = GSM_BLOCK_SIZE;
        break;
    case AV_CODEC_ID_GSM_MS: {
        int one = 1;
        gsm_option(avctx->priv_data, GSM_OPT_WAV49, &one);
        avctx->frame_size = 2*GSM_FRAME_SIZE;
        avctx->block_align = GSM_MS_BLOCK_SIZE;
        }
    }

    return 0;
error:
    libgsm_encode_close(avctx);
    return -1;
}

static int libgsm_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                               const AVFrame *frame, int *got_packet_ptr)
{
    int ret;
    gsm_signal *samples = (gsm_signal *)frame->data[0];
    struct gsm_state *state = avctx->priv_data;

    if ((ret = ff_alloc_packet2(avctx, avpkt, avctx->block_align, 0)) < 0)
        return ret;

    switch(avctx->codec_id) {
    case AV_CODEC_ID_GSM:
        gsm_encode(state, samples, avpkt->data);
        break;
    case AV_CODEC_ID_GSM_MS:
        gsm_encode(state, samples,                  avpkt->data);
        gsm_encode(state, samples + GSM_FRAME_SIZE, avpkt->data + 32);
    }

    *got_packet_ptr = 1;
    return 0;
}


#if CONFIG_LIBGSM_ENCODER
AVCodec ff_libgsm_encoder = {
    .name           = "libgsm",
    .long_name      = NULL_IF_CONFIG_SMALL("libgsm GSM"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_GSM,
    .init           = libgsm_encode_init,
    .encode2        = libgsm_encode_frame,
    .close          = libgsm_encode_close,
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                     AV_SAMPLE_FMT_NONE },
};
#endif
#if CONFIG_LIBGSM_MS_ENCODER
AVCodec ff_libgsm_ms_encoder = {
    .name           = "libgsm_ms",
    .long_name      = NULL_IF_CONFIG_SMALL("libgsm GSM Microsoft variant"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_GSM_MS,
    .init           = libgsm_encode_init,
    .encode2        = libgsm_encode_frame,
    .close          = libgsm_encode_close,
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                     AV_SAMPLE_FMT_NONE },
};
#endif
