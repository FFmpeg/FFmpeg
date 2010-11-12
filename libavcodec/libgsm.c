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

static av_cold int libgsm_init(AVCodecContext *avctx) {
    if (avctx->channels > 1) {
        av_log(avctx, AV_LOG_ERROR, "Mono required for GSM, got %d channels\n",
               avctx->channels);
        return -1;
    }

    if(avctx->codec->decode){
        if(!avctx->channels)
            avctx->channels= 1;

        if(!avctx->sample_rate)
            avctx->sample_rate= 8000;

        avctx->sample_fmt = AV_SAMPLE_FMT_S16;
    }else{
        if (avctx->sample_rate != 8000) {
            av_log(avctx, AV_LOG_ERROR, "Sample rate 8000Hz required for GSM, got %dHz\n",
                avctx->sample_rate);
            if(avctx->strict_std_compliance > FF_COMPLIANCE_UNOFFICIAL)
                return -1;
        }
        if (avctx->bit_rate != 13000 /* Official */ &&
            avctx->bit_rate != 13200 /* Very common */ &&
            avctx->bit_rate != 0 /* Unknown; a.o. mov does not set bitrate when decoding */ ) {
            av_log(avctx, AV_LOG_ERROR, "Bitrate 13000bps required for GSM, got %dbps\n",
                avctx->bit_rate);
            if(avctx->strict_std_compliance > FF_COMPLIANCE_UNOFFICIAL)
                return -1;
        }
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

static av_cold int libgsm_close(AVCodecContext *avctx) {
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


AVCodec libgsm_encoder = {
    "libgsm",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_GSM,
    0,
    libgsm_init,
    libgsm_encode_frame,
    libgsm_close,
    .sample_fmts = (const enum AVSampleFormat[]){AV_SAMPLE_FMT_S16,AV_SAMPLE_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("libgsm GSM"),
};

AVCodec libgsm_ms_encoder = {
    "libgsm_ms",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_GSM_MS,
    0,
    libgsm_init,
    libgsm_encode_frame,
    libgsm_close,
    .sample_fmts = (const enum AVSampleFormat[]){AV_SAMPLE_FMT_S16,AV_SAMPLE_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("libgsm GSM Microsoft variant"),
};

static int libgsm_decode_frame(AVCodecContext *avctx,
                               void *data, int *data_size,
                               AVPacket *avpkt) {
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    *data_size = 0; /* In case of error */
    if(buf_size < avctx->block_align) return -1;
    switch(avctx->codec_id) {
    case CODEC_ID_GSM:
        if(gsm_decode(avctx->priv_data,buf,data)) return -1;
        *data_size = GSM_FRAME_SIZE*sizeof(int16_t);
        break;
    case CODEC_ID_GSM_MS:
        if(gsm_decode(avctx->priv_data,buf,data) ||
           gsm_decode(avctx->priv_data,buf+33,((int16_t*)data)+GSM_FRAME_SIZE)) return -1;
        *data_size = GSM_FRAME_SIZE*sizeof(int16_t)*2;
    }
    return avctx->block_align;
}

AVCodec libgsm_decoder = {
    "libgsm",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_GSM,
    0,
    libgsm_init,
    NULL,
    libgsm_close,
    libgsm_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("libgsm GSM"),
};

AVCodec libgsm_ms_decoder = {
    "libgsm_ms",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_GSM_MS,
    0,
    libgsm_init,
    NULL,
    libgsm_close,
    libgsm_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("libgsm GSM Microsoft variant"),
};
