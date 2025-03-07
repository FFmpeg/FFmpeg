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
#include "config_components.h"
#if HAVE_GSM_H
#include <gsm.h>
#else
#include <gsm/gsm.h>
#endif

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"
#include "gsm.h"

static av_cold int libgsm_encode_close(AVCodecContext *avctx) {
    gsm_destroy(avctx->priv_data);
    avctx->priv_data = NULL;
    return 0;
}

static av_cold int libgsm_encode_init(AVCodecContext *avctx) {
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

    if ((ret = ff_get_encode_buffer(avctx, avpkt, avctx->block_align, 0)) < 0)
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

static const FFCodecDefault libgsm_defaults[] = {
    { "b",                "13000" },
    { NULL },
};

#if CONFIG_LIBGSM_ENCODER
const FFCodec ff_libgsm_encoder = {
    .p.name         = "libgsm",
    CODEC_LONG_NAME("libgsm GSM"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_GSM,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .init           = libgsm_encode_init,
    FF_CODEC_ENCODE_CB(libgsm_encode_frame),
    .close          = libgsm_encode_close,
    .defaults       = libgsm_defaults,
    CODEC_CH_LAYOUTS(AV_CHANNEL_LAYOUT_MONO),
    CODEC_SAMPLEFMTS(AV_SAMPLE_FMT_S16),
    .p.wrapper_name = "libgsm",
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE,
};
#endif
#if CONFIG_LIBGSM_MS_ENCODER
const FFCodec ff_libgsm_ms_encoder = {
    .p.name         = "libgsm_ms",
    CODEC_LONG_NAME("libgsm GSM Microsoft variant"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_GSM_MS,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .init           = libgsm_encode_init,
    FF_CODEC_ENCODE_CB(libgsm_encode_frame),
    .close          = libgsm_encode_close,
    .defaults       = libgsm_defaults,
    CODEC_CH_LAYOUTS(AV_CHANNEL_LAYOUT_MONO),
    CODEC_SAMPLEFMTS(AV_SAMPLE_FMT_S16),
    .p.wrapper_name = "libgsm",
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE,
};
#endif
