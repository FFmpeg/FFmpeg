/*
 * Microsoft Video-1 Decoder
 * Copyright (C) 2003 the ffmpeg project
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
 * Microsoft Video-1 Decoder by Mike Melanson (melanson@pcisys.net)
 * For more information about the MS Video-1 format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * This decoder outputs either PAL8 or RGB555 data, depending on the
 * whether a RGB palette was passed through palctrl;
 * if it's present, then the data is PAL8; RGB555 otherwise.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavutil/intreadwrite.h"
#include "avcodec.h"

#define PALETTE_COUNT 256
#define CHECK_STREAM_PTR(n) \
  if ((stream_ptr + n) > s->size ) { \
    av_log(s->avctx, AV_LOG_ERROR, " MS Video-1 warning: stream_ptr out of bounds (%d >= %d)\n", \
      stream_ptr + n, s->size); \
    return; \
  }

typedef struct Msvideo1Context {

    AVCodecContext *avctx;
    AVFrame frame;

    const unsigned char *buf;
    int size;

    int mode_8bit;  /* if it's not 8-bit, it's 16-bit */

} Msvideo1Context;

static av_cold int msvideo1_decode_init(AVCodecContext *avctx)
{
    Msvideo1Context *s = avctx->priv_data;

    s->avctx = avctx;

    /* figure out the colorspace based on the presence of a palette */
    if (s->avctx->palctrl) {
        s->mode_8bit = 1;
        avctx->pix_fmt = PIX_FMT_PAL8;
    } else {
        s->mode_8bit = 0;
        avctx->pix_fmt = PIX_FMT_RGB555;
    }

    s->frame.data[0] = NULL;

    return 0;
}

static void msvideo1_decode_8bit(Msvideo1Context *s)
{
    int block_ptr, pixel_ptr;
    int total_blocks;
    int pixel_x, pixel_y;  /* pixel width and height iterators */
    int block_x, block_y;  /* block width and height iterators */
    int blocks_wide, blocks_high;  /* width and height in 4x4 blocks */
    int block_inc;
    int row_dec;

    /* decoding parameters */
    int stream_ptr;
    unsigned char byte_a, byte_b;
    unsigned short flags;
    int skip_blocks;
    unsigned char colors[8];
    unsigned char *pixels = s->frame.data[0];
    int stride = s->frame.linesize[0];

    stream_ptr = 0;
    skip_blocks = 0;
    blocks_wide = s->avctx->width / 4;
    blocks_high = s->avctx->height / 4;
    total_blocks = blocks_wide * blocks_high;
    block_inc = 4;
    row_dec = stride + 4;

    for (block_y = blocks_high; block_y > 0; block_y--) {
        block_ptr = ((block_y * 4) - 1) * stride;
        for (block_x = blocks_wide; block_x > 0; block_x--) {
            /* check if this block should be skipped */
            if (skip_blocks) {
                block_ptr += block_inc;
                skip_blocks--;
                total_blocks--;
                continue;
            }

            pixel_ptr = block_ptr;

            /* get the next two bytes in the encoded data stream */
            CHECK_STREAM_PTR(2);
            byte_a = s->buf[stream_ptr++];
            byte_b = s->buf[stream_ptr++];

            /* check if the decode is finished */
            if ((byte_a == 0) && (byte_b == 0) && (total_blocks == 0))
                return;
            else if ((byte_b & 0xFC) == 0x84) {
                /* skip code, but don't count the current block */
                skip_blocks = ((byte_b - 0x84) << 8) + byte_a - 1;
            } else if (byte_b < 0x80) {
                /* 2-color encoding */
                flags = (byte_b << 8) | byte_a;

                CHECK_STREAM_PTR(2);
                colors[0] = s->buf[stream_ptr++];
                colors[1] = s->buf[stream_ptr++];

                for (pixel_y = 0; pixel_y < 4; pixel_y++) {
                    for (pixel_x = 0; pixel_x < 4; pixel_x++, flags >>= 1)
                        pixels[pixel_ptr++] = colors[(flags & 0x1) ^ 1];
                    pixel_ptr -= row_dec;
                }
            } else if (byte_b >= 0x90) {
                /* 8-color encoding */
                flags = (byte_b << 8) | byte_a;

                CHECK_STREAM_PTR(8);
                memcpy(colors, &s->buf[stream_ptr], 8);
                stream_ptr += 8;

                for (pixel_y = 0; pixel_y < 4; pixel_y++) {
                    for (pixel_x = 0; pixel_x < 4; pixel_x++, flags >>= 1)
                        pixels[pixel_ptr++] =
                            colors[((pixel_y & 0x2) << 1) +
                                (pixel_x & 0x2) + ((flags & 0x1) ^ 1)];
                    pixel_ptr -= row_dec;
                }
            } else {
                /* 1-color encoding */
                colors[0] = byte_a;

                for (pixel_y = 0; pixel_y < 4; pixel_y++) {
                    for (pixel_x = 0; pixel_x < 4; pixel_x++)
                        pixels[pixel_ptr++] = colors[0];
                    pixel_ptr -= row_dec;
                }
            }

            block_ptr += block_inc;
            total_blocks--;
        }
    }

    /* make the palette available on the way out */
    if (s->avctx->pix_fmt == PIX_FMT_PAL8) {
        memcpy(s->frame.data[1], s->avctx->palctrl->palette, AVPALETTE_SIZE);
        if (s->avctx->palctrl->palette_changed) {
            s->frame.palette_has_changed = 1;
            s->avctx->palctrl->palette_changed = 0;
        }
    }
}

static void msvideo1_decode_16bit(Msvideo1Context *s)
{
    int block_ptr, pixel_ptr;
    int total_blocks;
    int pixel_x, pixel_y;  /* pixel width and height iterators */
    int block_x, block_y;  /* block width and height iterators */
    int blocks_wide, blocks_high;  /* width and height in 4x4 blocks */
    int block_inc;
    int row_dec;

    /* decoding parameters */
    int stream_ptr;
    unsigned char byte_a, byte_b;
    unsigned short flags;
    int skip_blocks;
    unsigned short colors[8];
    unsigned short *pixels = (unsigned short *)s->frame.data[0];
    int stride = s->frame.linesize[0] / 2;

    stream_ptr = 0;
    skip_blocks = 0;
    blocks_wide = s->avctx->width / 4;
    blocks_high = s->avctx->height / 4;
    total_blocks = blocks_wide * blocks_high;
    block_inc = 4;
    row_dec = stride + 4;

    for (block_y = blocks_high; block_y > 0; block_y--) {
        block_ptr = ((block_y * 4) - 1) * stride;
        for (block_x = blocks_wide; block_x > 0; block_x--) {
            /* check if this block should be skipped */
            if (skip_blocks) {
                block_ptr += block_inc;
                skip_blocks--;
                total_blocks--;
                continue;
            }

            pixel_ptr = block_ptr;

            /* get the next two bytes in the encoded data stream */
            CHECK_STREAM_PTR(2);
            byte_a = s->buf[stream_ptr++];
            byte_b = s->buf[stream_ptr++];

            /* check if the decode is finished */
            if ((byte_a == 0) && (byte_b == 0) && (total_blocks == 0)) {
                return;
            } else if ((byte_b & 0xFC) == 0x84) {
                /* skip code, but don't count the current block */
                skip_blocks = ((byte_b - 0x84) << 8) + byte_a - 1;
            } else if (byte_b < 0x80) {
                /* 2- or 8-color encoding modes */
                flags = (byte_b << 8) | byte_a;

                CHECK_STREAM_PTR(4);
                colors[0] = AV_RL16(&s->buf[stream_ptr]);
                stream_ptr += 2;
                colors[1] = AV_RL16(&s->buf[stream_ptr]);
                stream_ptr += 2;

                if (colors[0] & 0x8000) {
                    /* 8-color encoding */
                    CHECK_STREAM_PTR(12);
                    colors[2] = AV_RL16(&s->buf[stream_ptr]);
                    stream_ptr += 2;
                    colors[3] = AV_RL16(&s->buf[stream_ptr]);
                    stream_ptr += 2;
                    colors[4] = AV_RL16(&s->buf[stream_ptr]);
                    stream_ptr += 2;
                    colors[5] = AV_RL16(&s->buf[stream_ptr]);
                    stream_ptr += 2;
                    colors[6] = AV_RL16(&s->buf[stream_ptr]);
                    stream_ptr += 2;
                    colors[7] = AV_RL16(&s->buf[stream_ptr]);
                    stream_ptr += 2;

                    for (pixel_y = 0; pixel_y < 4; pixel_y++) {
                        for (pixel_x = 0; pixel_x < 4; pixel_x++, flags >>= 1)
                            pixels[pixel_ptr++] =
                                colors[((pixel_y & 0x2) << 1) +
                                    (pixel_x & 0x2) + ((flags & 0x1) ^ 1)];
                        pixel_ptr -= row_dec;
                    }
                } else {
                    /* 2-color encoding */
                    for (pixel_y = 0; pixel_y < 4; pixel_y++) {
                        for (pixel_x = 0; pixel_x < 4; pixel_x++, flags >>= 1)
                            pixels[pixel_ptr++] = colors[(flags & 0x1) ^ 1];
                        pixel_ptr -= row_dec;
                    }
                }
            } else {
                /* otherwise, it's a 1-color block */
                colors[0] = (byte_b << 8) | byte_a;

                for (pixel_y = 0; pixel_y < 4; pixel_y++) {
                    for (pixel_x = 0; pixel_x < 4; pixel_x++)
                        pixels[pixel_ptr++] = colors[0];
                    pixel_ptr -= row_dec;
                }
            }

            block_ptr += block_inc;
            total_blocks--;
        }
    }
}

static int msvideo1_decode_frame(AVCodecContext *avctx,
                                void *data, int *data_size,
                                AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    Msvideo1Context *s = avctx->priv_data;

    s->buf = buf;
    s->size = buf_size;

    s->frame.reference = 1;
    s->frame.buffer_hints = FF_BUFFER_HINTS_VALID | FF_BUFFER_HINTS_PRESERVE | FF_BUFFER_HINTS_REUSABLE;
    if (avctx->reget_buffer(avctx, &s->frame)) {
        av_log(s->avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return -1;
    }

    if (s->mode_8bit)
        msvideo1_decode_8bit(s);
    else
        msvideo1_decode_16bit(s);

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;

    /* report that the buffer was completely consumed */
    return buf_size;
}

static av_cold int msvideo1_decode_end(AVCodecContext *avctx)
{
    Msvideo1Context *s = avctx->priv_data;

    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    return 0;
}

AVCodec msvideo1_decoder = {
    "msvideo1",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_MSVIDEO1,
    sizeof(Msvideo1Context),
    msvideo1_decode_init,
    NULL,
    msvideo1_decode_end,
    msvideo1_decode_frame,
    CODEC_CAP_DR1,
    .long_name= NULL_IF_CONFIG_SMALL("Microsoft Video 1"),
};
