/*
 * Autodesk RLE Decoder
 * Copyright (C) 2005 the ffmpeg project
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
 * @file aasc.c
 * Autodesk RLE Video Decoder by Konstantin Shishkov
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avcodec.h"
#include "dsputil.h"
#include "msrledec.h"

typedef struct AascContext {
    AVCodecContext *avctx;
    AVFrame frame;
} AascContext;

#define FETCH_NEXT_STREAM_BYTE() \
    if (stream_ptr >= buf_size) \
    { \
      av_log(s->avctx, AV_LOG_ERROR, " AASC: stream ptr just went out of bounds (fetch)\n"); \
      break; \
    } \
    stream_byte = buf[stream_ptr++];

static av_cold int aasc_decode_init(AVCodecContext *avctx)
{
    AascContext *s = avctx->priv_data;

    s->avctx = avctx;

    avctx->pix_fmt = PIX_FMT_BGR24;
    s->frame.data[0] = NULL;

    return 0;
}

static int aasc_decode_frame(AVCodecContext *avctx,
                              void *data, int *data_size,
                              const uint8_t *buf, int buf_size)
{
    AascContext *s = avctx->priv_data;

    s->frame.reference = 1;
    s->frame.buffer_hints = FF_BUFFER_HINTS_VALID | FF_BUFFER_HINTS_PRESERVE | FF_BUFFER_HINTS_REUSABLE;
    if (avctx->reget_buffer(avctx, &s->frame)) {
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return -1;
    }

    ff_msrle_decode(avctx, &s->frame, 8, buf, buf_size);

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;

    /* report that the buffer was completely consumed */
    return buf_size;
}

static av_cold int aasc_decode_end(AVCodecContext *avctx)
{
    AascContext *s = avctx->priv_data;

    /* release the last frame */
    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    return 0;
}

AVCodec aasc_decoder = {
    "aasc",
    CODEC_TYPE_VIDEO,
    CODEC_ID_AASC,
    sizeof(AascContext),
    aasc_decode_init,
    NULL,
    aasc_decode_end,
    aasc_decode_frame,
    CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("Autodesk RLE"),
};
