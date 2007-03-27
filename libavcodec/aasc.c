/*
 * Autodesc RLE Decoder
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
 * Autodesc RLE Video Decoder by Konstantin Shishkov
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "avcodec.h"
#include "dsputil.h"

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

static int aasc_decode_init(AVCodecContext *avctx)
{
    AascContext *s = (AascContext *)avctx->priv_data;

    s->avctx = avctx;

    avctx->pix_fmt = PIX_FMT_BGR24;
    s->frame.data[0] = NULL;

    return 0;
}

static int aasc_decode_frame(AVCodecContext *avctx,
                              void *data, int *data_size,
                              uint8_t *buf, int buf_size)
{
    AascContext *s = (AascContext *)avctx->priv_data;
    int stream_ptr = 4;
    unsigned char rle_code;
    unsigned char stream_byte;
    int pixel_ptr = 0;
    int row_dec, row_ptr;
    int frame_size;
    int i;

    s->frame.reference = 1;
    s->frame.buffer_hints = FF_BUFFER_HINTS_VALID | FF_BUFFER_HINTS_PRESERVE | FF_BUFFER_HINTS_REUSABLE;
    if (avctx->reget_buffer(avctx, &s->frame)) {
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return -1;
    }

    row_dec = s->frame.linesize[0];
    row_ptr = (s->avctx->height - 1) * row_dec;
    frame_size = row_dec * s->avctx->height;

    while (row_ptr >= 0) {
        FETCH_NEXT_STREAM_BYTE();
        rle_code = stream_byte;
        if (rle_code == 0) {
            /* fetch the next byte to see how to handle escape code */
            FETCH_NEXT_STREAM_BYTE();
            if (stream_byte == 0) {
                /* line is done, goto the next one */
                row_ptr -= row_dec;
                pixel_ptr = 0;
            } else if (stream_byte == 1) {
                /* decode is done */
                break;
            } else if (stream_byte == 2) {
                /* reposition frame decode coordinates */
                FETCH_NEXT_STREAM_BYTE();
                pixel_ptr += stream_byte;
                FETCH_NEXT_STREAM_BYTE();
                row_ptr -= stream_byte * row_dec;
            } else {
                /* copy pixels from encoded stream */
                if ((pixel_ptr + stream_byte > avctx->width * 3) ||
                    (row_ptr < 0)) {
                    av_log(s->avctx, AV_LOG_ERROR, " AASC: frame ptr just went out of bounds (copy1)\n");
                    break;
                }

                rle_code = stream_byte;
                if (stream_ptr + rle_code > buf_size) {
                    av_log(s->avctx, AV_LOG_ERROR, " AASC: stream ptr just went out of bounds (copy2)\n");
                    break;
                }

                for (i = 0; i < rle_code; i++) {
                    FETCH_NEXT_STREAM_BYTE();
                    s->frame.data[0][row_ptr + pixel_ptr] = stream_byte;
                    pixel_ptr++;
                }
                if (rle_code & 1)
                    stream_ptr++;
            }
        } else {
            /* decode a run of data */
            if ((pixel_ptr + rle_code > avctx->width * 3) ||
                (row_ptr < 0)) {
                av_log(s->avctx, AV_LOG_ERROR, " AASC: frame ptr just went out of bounds (run1)\n");
                break;
            }

            FETCH_NEXT_STREAM_BYTE();

            while(rle_code--) {
                s->frame.data[0][row_ptr + pixel_ptr] = stream_byte;
                pixel_ptr++;
            }
        }
    }

    /* one last sanity check on the way out */
    if (stream_ptr < buf_size)
        av_log(s->avctx, AV_LOG_ERROR, " AASC: ended frame decode with bytes left over (%d < %d)\n",
            stream_ptr, buf_size);

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;

    /* report that the buffer was completely consumed */
    return buf_size;
}

static int aasc_decode_end(AVCodecContext *avctx)
{
    AascContext *s = (AascContext *)avctx->priv_data;

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
};
