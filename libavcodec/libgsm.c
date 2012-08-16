/*
 * Interface to libgsm for gsm encoding/decoding
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
 * Interface to libgsm for gsm encoding/decoding
 */

// The idiosyncrasies of GSM-in-WAV are explained at http://kbs.cs.tu-berlin.de/~jutta/toast.html

#include <gsm/gsm.h>

#include "avcodec.h"
#include "internal.h"
#include "gsm.h"
#include "libavutil/common.h"

static av_cold int libgsm_encode_close(AVCodecContext *avctx) {
#if FF_API_OLD_ENCODE_AUDIO
    av_freep(&avctx->coded_frame);
#endif
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
        av_log(avctx, AV_LOG_ERROR, "Bitrate 13000bps required for GSM, got %dbps\n",
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

#if FF_API_OLD_ENCODE_AUDIO
    avctx->coded_frame= avcodec_alloc_frame();
    if (!avctx->coded_frame)
        goto error;
#endif

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

    if ((ret = ff_alloc_packet2(avctx, avpkt, avctx->block_align)))
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
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_GSM,
    .init           = libgsm_encode_init,
    .encode2        = libgsm_encode_frame,
    .close          = libgsm_encode_close,
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                     AV_SAMPLE_FMT_NONE },
    .long_name      = NULL_IF_CONFIG_SMALL("libgsm GSM"),
};
#endif
#if CONFIG_LIBGSM_MS_ENCODER
AVCodec ff_libgsm_ms_encoder = {
    .name           = "libgsm_ms",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_GSM_MS,
    .init           = libgsm_encode_init,
    .encode2        = libgsm_encode_frame,
    .close          = libgsm_encode_close,
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                     AV_SAMPLE_FMT_NONE },
    .long_name      = NULL_IF_CONFIG_SMALL("libgsm GSM Microsoft variant"),
};
#endif

typedef struct LibGSMDecodeContext {
    AVFrame frame;
    struct gsm_state *state;
} LibGSMDecodeContext;

static av_cold int libgsm_decode_init(AVCodecContext *avctx) {
    LibGSMDecodeContext *s = avctx->priv_data;

    if (avctx->channels > 1) {
        av_log(avctx, AV_LOG_ERROR, "Mono required for GSM, got %d channels\n",
               avctx->channels);
        return -1;
    }

    if (!avctx->channels)
        avctx->channels = 1;

    if (!avctx->sample_rate)
        avctx->sample_rate = 8000;

    avctx->sample_fmt = AV_SAMPLE_FMT_S16;

    s->state = gsm_create();

    switch(avctx->codec_id) {
    case AV_CODEC_ID_GSM:
        avctx->frame_size  = GSM_FRAME_SIZE;
        avctx->block_align = GSM_BLOCK_SIZE;
        break;
    case AV_CODEC_ID_GSM_MS: {
        int one = 1;
        gsm_option(s->state, GSM_OPT_WAV49, &one);
        avctx->frame_size  = 2 * GSM_FRAME_SIZE;
        avctx->block_align = GSM_MS_BLOCK_SIZE;
        }
    }

    avcodec_get_frame_defaults(&s->frame);
    avctx->coded_frame = &s->frame;

    return 0;
}

static av_cold int libgsm_decode_close(AVCodecContext *avctx) {
    LibGSMDecodeContext *s = avctx->priv_data;

    gsm_destroy(s->state);
    s->state = NULL;
    return 0;
}

static int libgsm_decode_frame(AVCodecContext *avctx, void *data,
                               int *got_frame_ptr, AVPacket *avpkt)
{
    int i, ret;
    LibGSMDecodeContext *s = avctx->priv_data;
    uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    int16_t *samples;

    if (buf_size < avctx->block_align) {
        av_log(avctx, AV_LOG_ERROR, "Packet is too small\n");
        return AVERROR_INVALIDDATA;
    }

    /* get output buffer */
    s->frame.nb_samples = avctx->frame_size;
    if ((ret = avctx->get_buffer(avctx, &s->frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }
    samples = (int16_t *)s->frame.data[0];

    for (i = 0; i < avctx->frame_size / GSM_FRAME_SIZE; i++) {
        if ((ret = gsm_decode(s->state, buf, samples)) < 0)
            return -1;
        buf     += GSM_BLOCK_SIZE;
        samples += GSM_FRAME_SIZE;
    }

    *got_frame_ptr   = 1;
    *(AVFrame *)data = s->frame;

    return avctx->block_align;
}

static void libgsm_flush(AVCodecContext *avctx) {
    LibGSMDecodeContext *s = avctx->priv_data;
    int one = 1;

    gsm_destroy(s->state);
    s->state = gsm_create();
    if (avctx->codec_id == AV_CODEC_ID_GSM_MS)
        gsm_option(s->state, GSM_OPT_WAV49, &one);
}

#if CONFIG_LIBGSM_DECODER
AVCodec ff_libgsm_decoder = {
    .name           = "libgsm",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_GSM,
    .priv_data_size = sizeof(LibGSMDecodeContext),
    .init           = libgsm_decode_init,
    .close          = libgsm_decode_close,
    .decode         = libgsm_decode_frame,
    .flush          = libgsm_flush,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("libgsm GSM"),
};
#endif
#if CONFIG_LIBGSM_MS_DECODER
AVCodec ff_libgsm_ms_decoder = {
    .name           = "libgsm_ms",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_GSM_MS,
    .priv_data_size = sizeof(LibGSMDecodeContext),
    .init           = libgsm_decode_init,
    .close          = libgsm_decode_close,
    .decode         = libgsm_decode_frame,
    .flush          = libgsm_flush,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("libgsm GSM Microsoft variant"),
};
#endif
