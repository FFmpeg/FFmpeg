/*
 * Micrsoft RLE Video Decoder
 * Copyright (C) 2003 the ffmpeg project
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * @file msrle.c
 * MS RLE Video Decoder by Mike Melanson (melanson@pcisys.net)
 * For more information about the MS RLE format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * The MS RLE decoder outputs PAL8 colorspace data.
 *
 * Note that this decoder expects the palette colors from the end of the
 * BITMAPINFO header passed through palctrl.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "avcodec.h"
#include "dsputil.h"

typedef struct MsrleContext {
    AVCodecContext *avctx;
    AVFrame frame;

    unsigned char *buf;
    int size;

} MsrleContext;

#define FETCH_NEXT_STREAM_BYTE() \
    if (stream_ptr >= s->size) \
    { \
      av_log(s->avctx, AV_LOG_ERROR, " MS RLE: stream ptr just went out of bounds (1)\n"); \
      return; \
    } \
    stream_byte = s->buf[stream_ptr++];

static void msrle_decode_pal4(MsrleContext *s)
{
    int stream_ptr = 0;
    unsigned char rle_code;
    unsigned char extra_byte, odd_pixel;
    unsigned char stream_byte;
    int pixel_ptr = 0;
    int row_dec = s->frame.linesize[0];
    int row_ptr = (s->avctx->height - 1) * row_dec;
    int frame_size = row_dec * s->avctx->height;
    int i;

    /* make the palette available */
    memcpy(s->frame.data[1], s->avctx->palctrl->palette, AVPALETTE_SIZE);
    if (s->avctx->palctrl->palette_changed) {
        s->frame.palette_has_changed = 1;
        s->avctx->palctrl->palette_changed = 0;
    }

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
                return;
            } else if (stream_byte == 2) {
                /* reposition frame decode coordinates */
                FETCH_NEXT_STREAM_BYTE();
                pixel_ptr += stream_byte;
                FETCH_NEXT_STREAM_BYTE();
                row_ptr -= stream_byte * row_dec;
        } else {
            // copy pixels from encoded stream
            odd_pixel =  stream_byte & 1;
            rle_code = (stream_byte + 1) / 2;
            extra_byte = rle_code & 0x01;
            if ((row_ptr + pixel_ptr + stream_byte > frame_size) ||
                (row_ptr < 0)) {
                av_log(s->avctx, AV_LOG_ERROR, " MS RLE: frame ptr just went out of bounds (1)\n");
                return;
            }

            for (i = 0; i < rle_code; i++) {
                if (pixel_ptr >= s->avctx->width)
                    break;
                FETCH_NEXT_STREAM_BYTE();
                s->frame.data[0][row_ptr + pixel_ptr] = stream_byte >> 4;
                pixel_ptr++;
                if (i + 1 == rle_code && odd_pixel)
                    break;
                if (pixel_ptr >= s->avctx->width)
                    break;
                s->frame.data[0][row_ptr + pixel_ptr] = stream_byte & 0x0F;
                pixel_ptr++;
            }

            // if the RLE code is odd, skip a byte in the stream
            if (extra_byte)
              stream_ptr++;
            }
        } else {
            // decode a run of data
            if ((row_ptr + pixel_ptr + stream_byte > frame_size) ||
                (row_ptr < 0)) {
                av_log(s->avctx, AV_LOG_ERROR, " MS RLE: frame ptr just went out of bounds (1)\n");
                return;
            }
            FETCH_NEXT_STREAM_BYTE();
            for (i = 0; i < rle_code; i++) {
                if (pixel_ptr >= s->avctx->width)
                    break;
                if ((i & 1) == 0)
                    s->frame.data[0][row_ptr + pixel_ptr] = stream_byte >> 4;
                else
                    s->frame.data[0][row_ptr + pixel_ptr] = stream_byte & 0x0F;
                pixel_ptr++;
            }
        }
    }

    /* one last sanity check on the way out */
    if (stream_ptr < s->size)
        av_log(s->avctx, AV_LOG_ERROR, " MS RLE: ended frame decode with bytes left over (%d < %d)\n",
            stream_ptr, s->size);
}



static void msrle_decode_pal8(MsrleContext *s)
{
    int stream_ptr = 0;
    unsigned char rle_code;
    unsigned char extra_byte;
    unsigned char stream_byte;
    int pixel_ptr = 0;
    int row_dec = s->frame.linesize[0];
    int row_ptr = (s->avctx->height - 1) * row_dec;
    int frame_size = row_dec * s->avctx->height;

    /* make the palette available */
    memcpy(s->frame.data[1], s->avctx->palctrl->palette, AVPALETTE_SIZE);
    if (s->avctx->palctrl->palette_changed) {
        s->frame.palette_has_changed = 1;
        s->avctx->palctrl->palette_changed = 0;
    }

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
                return;
            } else if (stream_byte == 2) {
                /* reposition frame decode coordinates */
                FETCH_NEXT_STREAM_BYTE();
                pixel_ptr += stream_byte;
                FETCH_NEXT_STREAM_BYTE();
                row_ptr -= stream_byte * row_dec;
            } else {
                /* copy pixels from encoded stream */
                if ((row_ptr + pixel_ptr + stream_byte > frame_size) ||
                    (row_ptr < 0)) {
                    av_log(s->avctx, AV_LOG_ERROR, " MS RLE: frame ptr just went out of bounds (1)\n");
                    return;
                }

                rle_code = stream_byte;
                extra_byte = stream_byte & 0x01;
                if (stream_ptr + rle_code + extra_byte > s->size) {
                    av_log(s->avctx, AV_LOG_ERROR, " MS RLE: stream ptr just went out of bounds (2)\n");
                    return;
                }

                while (rle_code--) {
                    FETCH_NEXT_STREAM_BYTE();
                    s->frame.data[0][row_ptr + pixel_ptr] = stream_byte;
                    pixel_ptr++;
                }

                /* if the RLE code is odd, skip a byte in the stream */
                if (extra_byte)
                    stream_ptr++;
            }
        } else {
            /* decode a run of data */
            if ((row_ptr + pixel_ptr + stream_byte > frame_size) ||
                (row_ptr < 0)) {
                av_log(s->avctx, AV_LOG_ERROR, " MS RLE: frame ptr just went out of bounds (2)\n");
                return;
            }

            FETCH_NEXT_STREAM_BYTE();

            while(rle_code--) {
                s->frame.data[0][row_ptr + pixel_ptr] = stream_byte;
                pixel_ptr++;
            }
        }
    }

    /* one last sanity check on the way out */
    if (stream_ptr < s->size)
        av_log(s->avctx, AV_LOG_ERROR, " MS RLE: ended frame decode with bytes left over (%d < %d)\n",
            stream_ptr, s->size);
}

static int msrle_decode_init(AVCodecContext *avctx)
{
    MsrleContext *s = (MsrleContext *)avctx->priv_data;

    s->avctx = avctx;

    avctx->pix_fmt = PIX_FMT_PAL8;
    avctx->has_b_frames = 0;
    s->frame.data[0] = NULL;

    return 0;
}

static int msrle_decode_frame(AVCodecContext *avctx,
                              void *data, int *data_size,
                              uint8_t *buf, int buf_size)
{
    MsrleContext *s = (MsrleContext *)avctx->priv_data;

    s->buf = buf;
    s->size = buf_size;

    s->frame.reference = 1;
    s->frame.buffer_hints = FF_BUFFER_HINTS_VALID | FF_BUFFER_HINTS_PRESERVE | FF_BUFFER_HINTS_REUSABLE;
    if (avctx->reget_buffer(avctx, &s->frame)) {
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return -1;
    }

    switch (avctx->bits_per_sample) {
        case 8:
            msrle_decode_pal8(s);
            break;
        case 4:
            msrle_decode_pal4(s);
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "Don't know how to decode depth %u.\n",
                   avctx->bits_per_sample);
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;

    /* report that the buffer was completely consumed */
    return buf_size;
}

static int msrle_decode_end(AVCodecContext *avctx)
{
    MsrleContext *s = (MsrleContext *)avctx->priv_data;

    /* release the last frame */
    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    return 0;
}

AVCodec msrle_decoder = {
    "msrle",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MSRLE,
    sizeof(MsrleContext),
    msrle_decode_init,
    NULL,
    msrle_decode_end,
    msrle_decode_frame,
    CODEC_CAP_DR1,
};
