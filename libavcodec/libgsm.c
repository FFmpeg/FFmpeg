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

#include "avcodec.h"
#include <gsm/gsm.h>

// gsm.h misses some essential constants
#define GSM_BLOCK_SIZE 33
#define GSM_MS_BLOCK_SIZE 65
#define GSM_FRAME_SIZE 160

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

    switch(avctx->codec_id) {
    case CODEC_ID_GSM:
        avctx->frame_size = GSM_FRAME_SIZE;
        avctx->block_align = GSM_BLOCK_SIZE;
        break;
    case CODEC_ID_GSM_MS: {
        int one = 1;
        gsm_option(avctx->priv_data, GSM_OPT_WAV49, &one);
        avctx->frame_size = 2*GSM_FRAME_SIZE;
        avctx->block_align = GSM_MS_BLOCK_SIZE;
        }
    }

    avctx->coded_frame= avcodec_alloc_frame();
    avctx->coded_frame->key_frame= 1;

    return 0;
}

static av_cold int libgsm_encode_close(AVCodecContext *avctx) {
    av_freep(&avctx->coded_frame);
    gsm_destroy(avctx->priv_data);
    avctx->priv_data = NULL;
    return 0;
}

static int libgsm_encode_frame(AVCodecContext *avctx,
                               unsigned char *frame, int buf_size, void *data) {
    // we need a full block
    if(buf_size < avctx->block_align) return 0;

    switch(avctx->codec_id) {
    case CODEC_ID_GSM:
        gsm_encode(avctx->priv_data,data,frame);
        break;
    case CODEC_ID_GSM_MS:
        gsm_encode(avctx->priv_data,data,frame);
        gsm_encode(avctx->priv_data,((short*)data)+GSM_FRAME_SIZE,frame+32);
    }
    return avctx->block_align;
}


AVCodec ff_libgsm_encoder = {
    .name           = "libgsm",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_GSM,
    .init           = libgsm_encode_init,
    .encode         = libgsm_encode_frame,
    .close          = libgsm_encode_close,
    .sample_fmts = (const enum AVSampleFormat[]){AV_SAMPLE_FMT_S16,AV_SAMPLE_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("libgsm GSM"),
};

AVCodec ff_libgsm_ms_encoder = {
    .name           = "libgsm_ms",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_GSM_MS,
    .init           = libgsm_encode_init,
    .encode         = libgsm_encode_frame,
    .close          = libgsm_encode_close,
    .sample_fmts = (const enum AVSampleFormat[]){AV_SAMPLE_FMT_S16,AV_SAMPLE_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("libgsm GSM Microsoft variant"),
};

static av_cold int libgsm_decode_init(AVCodecContext *avctx) {
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

    avctx->priv_data = gsm_create();

    switch(avctx->codec_id) {
    case CODEC_ID_GSM:
        avctx->frame_size  = GSM_FRAME_SIZE;
        avctx->block_align = GSM_BLOCK_SIZE;
        break;
    case CODEC_ID_GSM_MS: {
        int one = 1;
        gsm_option(avctx->priv_data, GSM_OPT_WAV49, &one);
        avctx->frame_size  = 2 * GSM_FRAME_SIZE;
        avctx->block_align = GSM_MS_BLOCK_SIZE;
        }
    }

    return 0;
}

static av_cold int libgsm_decode_close(AVCodecContext *avctx) {
    gsm_destroy(avctx->priv_data);
    avctx->priv_data = NULL;
    return 0;
}

static int libgsm_decode_frame(AVCodecContext *avctx,
                               void *data, int *data_size,
                               AVPacket *avpkt) {
    int i, ret;
    struct gsm_state *s = avctx->priv_data;
    uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    int16_t *samples = data;
    int out_size = avctx->frame_size * av_get_bytes_per_sample(avctx->sample_fmt);

    if (*data_size < out_size) {
        av_log(avctx, AV_LOG_ERROR, "Output buffer is too small\n");
        return AVERROR(EINVAL);
    }

    if (buf_size < avctx->block_align) {
        av_log(avctx, AV_LOG_ERROR, "Packet is too small\n");
        return AVERROR_INVALIDDATA;
    }

    for (i = 0; i < avctx->frame_size / GSM_FRAME_SIZE; i++) {
        if ((ret = gsm_decode(s, buf, samples)) < 0)
            return -1;
        buf     += GSM_BLOCK_SIZE;
        samples += GSM_FRAME_SIZE;
    }

    *data_size = out_size;
    return avctx->block_align;
}

static void libgsm_flush(AVCodecContext *avctx) {
    gsm_destroy(avctx->priv_data);
    avctx->priv_data = gsm_create();
}

AVCodec ff_libgsm_decoder = {
    .name           = "libgsm",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_GSM,
    .init           = libgsm_decode_init,
    .close          = libgsm_decode_close,
    .decode         = libgsm_decode_frame,
    .flush          = libgsm_flush,
    .long_name = NULL_IF_CONFIG_SMALL("libgsm GSM"),
};

AVCodec ff_libgsm_ms_decoder = {
    .name           = "libgsm_ms",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_GSM_MS,
    .init           = libgsm_decode_init,
    .close          = libgsm_decode_close,
    .decode         = libgsm_decode_frame,
    .flush          = libgsm_flush,
    .long_name = NULL_IF_CONFIG_SMALL("libgsm GSM Microsoft variant"),
};
