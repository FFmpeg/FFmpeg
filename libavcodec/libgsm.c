/*
 * Interface to libgsm for gsm encoding/decoding
 * Copyright (c) 2005 Alban Bedel <albeu@free.fr>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file libgsm.c
 * Interface to libgsm for gsm encoding/decoding
 */

#include "avcodec.h"
#include <gsm.h>

// gsm.h miss some essential constants
#define GSM_BLOCK_SIZE 33
#define GSM_FRAME_SIZE 160

static int libgsm_init(AVCodecContext *avctx) {
    if (avctx->channels > 1 || avctx->sample_rate != 8000)
        return -1;

    avctx->frame_size = GSM_FRAME_SIZE;
    avctx->block_align = GSM_BLOCK_SIZE;

    avctx->priv_data = gsm_create();

    avctx->coded_frame= avcodec_alloc_frame();
    avctx->coded_frame->key_frame= 1;

    return 0;
}

static int libgsm_close(AVCodecContext *avctx) {
    gsm_destroy(avctx->priv_data);
    avctx->priv_data = NULL;
    return 0;
}

static int libgsm_encode_frame(AVCodecContext *avctx,
                               unsigned char *frame, int buf_size, void *data) {
    // we need a full block
    if(buf_size < GSM_BLOCK_SIZE) return 0;

    gsm_encode(avctx->priv_data,data,frame);

    return GSM_BLOCK_SIZE;
}


AVCodec libgsm_encoder = {
    "gsm",
    CODEC_TYPE_AUDIO,
    CODEC_ID_GSM,
    0,
    libgsm_init,
    libgsm_encode_frame,
    libgsm_close,
};

static int libgsm_decode_frame(AVCodecContext *avctx,
                               void *data, int *data_size,
                               uint8_t *buf, int buf_size) {

    if(buf_size < GSM_BLOCK_SIZE) return 0;

    if(gsm_decode(avctx->priv_data,buf,data)) return -1;

    *data_size = GSM_FRAME_SIZE*2;
    return GSM_BLOCK_SIZE;
}

AVCodec libgsm_decoder = {
    "gsm",
    CODEC_TYPE_AUDIO,
    CODEC_ID_GSM,
    0,
    libgsm_init,
    NULL,
    libgsm_close,
    libgsm_decode_frame,
};
