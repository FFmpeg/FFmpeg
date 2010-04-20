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
 * @file
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

    return 0;
}

static int aasc_decode_frame(AVCodecContext *avctx,
                              void *data, int *data_size,
                              AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    AascContext *s = avctx->priv_data;
    int compr, i, stride;

    s->frame.reference = 1;
    s->frame.buffer_hints = FF_BUFFER_HINTS_VALID | FF_BUFFER_HINTS_PRESERVE | FF_BUFFER_HINTS_REUSABLE;
    if (avctx->reget_buffer(avctx, &s->frame)) {
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return -1;
    }

    compr = AV_RL32(buf);
    buf += 4;
    buf_size -= 4;
    switch(compr){
    case 0:
        stride = (avctx->width * 3 + 3) & ~3;
        for(i = avctx->height - 1; i >= 0; i--){
            memcpy(s->frame.data[0] + i*s->frame.linesize[0], buf, avctx->width*3);
            buf += stride;
        }
        break;
    case 1:
        ff_msrle_decode(avctx, (AVPicture*)&s->frame, 8, buf - 4, buf_size + 4);
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown compression type %d\n", compr);
        return -1;
    }

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
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_AASC,
    sizeof(AascContext),
    aasc_decode_init,
    NULL,
    aasc_decode_end,
    aasc_decode_frame,
    CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("Autodesk RLE"),
};
