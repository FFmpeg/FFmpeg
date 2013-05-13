/*
 * Quicktime Video (RPZA) Video Decoder
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
 * QT RPZA Video Decoder by Roberto Togni
 * For more information about the RPZA format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * The RPZA decoder outputs RGB555 colorspace data.
 *
 * Note that this decoder reads big endian RGB555 pixel values from the
 * bytestream, arranges them in the host's endian order, and outputs
 * them to the final rendered map in the same host endian order. This is
 * intended behavior as the libavcodec documentation states that RGB555
 * pixels shall be stored in native CPU endianness.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "internal.h"

typedef struct RpzaContext {

    AVCodecContext *avctx;
    AVFrame frame;

    const unsigned char *buf;
    int size;

} RpzaContext;

#define ADVANCE_BLOCK() \
{ \
    pixel_ptr += 4; \
    if (pixel_ptr >= width) \
    { \
        pixel_ptr = 0; \
        row_ptr += stride * 4; \
    } \
    total_blocks--; \
    if (total_blocks < 0) \
    { \
        av_log(s->avctx, AV_LOG_ERROR, "warning: block counter just went negative (this should not happen)\n"); \
        return; \
    } \
}

static void rpza_decode_stream(RpzaContext *s)
{
    int width = s->avctx->width;
    int stride = s->frame.linesize[0] / 2;
    int row_inc = stride - 4;
    int stream_ptr = 0;
    int chunk_size;
    unsigned char opcode;
    int n_blocks;
    unsigned short colorA = 0, colorB;
    unsigned short color4[4];
    unsigned char index, idx;
    unsigned short ta, tb;
    unsigned short *pixels = (unsigned short *)s->frame.data[0];

    int row_ptr = 0;
    int pixel_ptr = 0;
    int block_ptr;
    int pixel_x, pixel_y;
    int total_blocks;

    /* First byte is always 0xe1. Warn if it's different */
    if (s->buf[stream_ptr] != 0xe1)
        av_log(s->avctx, AV_LOG_ERROR, "First chunk byte is 0x%02x instead of 0xe1\n",
            s->buf[stream_ptr]);

    /* Get chunk size, ingnoring first byte */
    chunk_size = AV_RB32(&s->buf[stream_ptr]) & 0x00FFFFFF;
    stream_ptr += 4;

    /* If length mismatch use size from MOV file and try to decode anyway */
    if (chunk_size != s->size)
        av_log(s->avctx, AV_LOG_ERROR, "MOV chunk size != encoded chunk size; using MOV chunk size\n");

    chunk_size = s->size;

    /* Number of 4x4 blocks in frame. */
    total_blocks = ((s->avctx->width + 3) / 4) * ((s->avctx->height + 3) / 4);

    /* Process chunk data */
    while (stream_ptr < chunk_size) {
        opcode = s->buf[stream_ptr++]; /* Get opcode */

        n_blocks = (opcode & 0x1f) + 1; /* Extract block counter from opcode */

        /* If opcode MSbit is 0, we need more data to decide what to do */
        if ((opcode & 0x80) == 0) {
            colorA = (opcode << 8) | (s->buf[stream_ptr++]);
            opcode = 0;
            if ((s->buf[stream_ptr] & 0x80) != 0) {
                /* Must behave as opcode 110xxxxx, using colorA computed
                 * above. Use fake opcode 0x20 to enter switch block at
                 * the right place */
                opcode = 0x20;
                n_blocks = 1;
            }
        }

        switch (opcode & 0xe0) {

        /* Skip blocks */
        case 0x80:
            while (n_blocks--) {
              ADVANCE_BLOCK();
            }
            break;

        /* Fill blocks with one color */
        case 0xa0:
            colorA = AV_RB16 (&s->buf[stream_ptr]);
            stream_ptr += 2;
            while (n_blocks--) {
                block_ptr = row_ptr + pixel_ptr;
                for (pixel_y = 0; pixel_y < 4; pixel_y++) {
                    for (pixel_x = 0; pixel_x < 4; pixel_x++){
                        pixels[block_ptr] = colorA;
                        block_ptr++;
                    }
                    block_ptr += row_inc;
                }
                ADVANCE_BLOCK();
            }
            break;

        /* Fill blocks with 4 colors */
        case 0xc0:
            colorA = AV_RB16 (&s->buf[stream_ptr]);
            stream_ptr += 2;
        case 0x20:
            colorB = AV_RB16 (&s->buf[stream_ptr]);
            stream_ptr += 2;

            /* sort out the colors */
            color4[0] = colorB;
            color4[1] = 0;
            color4[2] = 0;
            color4[3] = colorA;

            /* red components */
            ta = (colorA >> 10) & 0x1F;
            tb = (colorB >> 10) & 0x1F;
            color4[1] |= ((11 * ta + 21 * tb) >> 5) << 10;
            color4[2] |= ((21 * ta + 11 * tb) >> 5) << 10;

            /* green components */
            ta = (colorA >> 5) & 0x1F;
            tb = (colorB >> 5) & 0x1F;
            color4[1] |= ((11 * ta + 21 * tb) >> 5) << 5;
            color4[2] |= ((21 * ta + 11 * tb) >> 5) << 5;

            /* blue components */
            ta = colorA & 0x1F;
            tb = colorB & 0x1F;
            color4[1] |= ((11 * ta + 21 * tb) >> 5);
            color4[2] |= ((21 * ta + 11 * tb) >> 5);

            if (s->size - stream_ptr < n_blocks * 4)
                return;
            while (n_blocks--) {
                block_ptr = row_ptr + pixel_ptr;
                for (pixel_y = 0; pixel_y < 4; pixel_y++) {
                    index = s->buf[stream_ptr++];
                    for (pixel_x = 0; pixel_x < 4; pixel_x++){
                        idx = (index >> (2 * (3 - pixel_x))) & 0x03;
                        pixels[block_ptr] = color4[idx];
                        block_ptr++;
                    }
                    block_ptr += row_inc;
                }
                ADVANCE_BLOCK();
            }
            break;

        /* Fill block with 16 colors */
        case 0x00:
            if (s->size - stream_ptr < 16)
                return;
            block_ptr = row_ptr + pixel_ptr;
            for (pixel_y = 0; pixel_y < 4; pixel_y++) {
                for (pixel_x = 0; pixel_x < 4; pixel_x++){
                    /* We already have color of upper left pixel */
                    if ((pixel_y != 0) || (pixel_x !=0)) {
                        colorA = AV_RB16 (&s->buf[stream_ptr]);
                        stream_ptr += 2;
                    }
                    pixels[block_ptr] = colorA;
                    block_ptr++;
                }
                block_ptr += row_inc;
            }
            ADVANCE_BLOCK();
            break;

        /* Unknown opcode */
        default:
            av_log(s->avctx, AV_LOG_ERROR, "Unknown opcode %d in rpza chunk."
                 " Skip remaining %d bytes of chunk data.\n", opcode,
                 chunk_size - stream_ptr);
            return;
        } /* Opcode switch */
    }
}

static av_cold int rpza_decode_init(AVCodecContext *avctx)
{
    RpzaContext *s = avctx->priv_data;

    s->avctx = avctx;
    avctx->pix_fmt = AV_PIX_FMT_RGB555;

    avcodec_get_frame_defaults(&s->frame);

    return 0;
}

static int rpza_decode_frame(AVCodecContext *avctx,
                             void *data, int *got_frame,
                             AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    RpzaContext *s = avctx->priv_data;
    int ret;

    s->buf = buf;
    s->size = buf_size;

    if ((ret = ff_reget_buffer(avctx, &s->frame)) < 0)
        return ret;

    rpza_decode_stream(s);

    if ((ret = av_frame_ref(data, &s->frame)) < 0)
        return ret;

    *got_frame      = 1;

    /* always report that the buffer was completely consumed */
    return buf_size;
}

static av_cold int rpza_decode_end(AVCodecContext *avctx)
{
    RpzaContext *s = avctx->priv_data;

    av_frame_unref(&s->frame);

    return 0;
}

AVCodec ff_rpza_decoder = {
    .name           = "rpza",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_RPZA,
    .priv_data_size = sizeof(RpzaContext),
    .init           = rpza_decode_init,
    .close          = rpza_decode_end,
    .decode         = rpza_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("QuickTime video (RPZA)"),
};
