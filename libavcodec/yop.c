/*
 * Psygnosis YOP decoder
 *
 * Copyright (C) 2010 Mohamed Naufal Basheer <naufal11@gmail.com>
 * derived from the code by
 * Copyright (C) 2009 Thomas P. Higdon <thomas.p.higdon@gmail.com>
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

#include <string.h>

#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "internal.h"

typedef struct YopDecContext {
    AVCodecContext *avctx;
    AVFrame *frame;

    int num_pal_colors;
    int first_color[2];
    int frame_data_length;

    uint8_t *low_nibble;
    uint8_t *srcptr;
    uint8_t *src_end;
    uint8_t *dstptr;
    uint8_t *dstbuf;
} YopDecContext;

// These tables are taken directly from:
// http://wiki.multimedia.cx/index.php?title=Psygnosis_YOP

/**
 * Lookup table for painting macroblocks. Bytes 0-2 of each entry contain
 * the macroblock positions to be painted (taken as (0, B0, B1, B2)).
 * Byte 3 contains the number of bytes consumed on the input,
 * equal to max(bytes 0-2) + 1.
 */
static const uint8_t paint_lut[15][4] =
    {{1, 2, 3, 4}, {1, 2, 0, 3},
     {1, 2, 1, 3}, {1, 2, 2, 3},
     {1, 0, 2, 3}, {1, 0, 0, 2},
     {1, 0, 1, 2}, {1, 1, 2, 3},
     {0, 1, 2, 3}, {0, 1, 0, 2},
     {1, 1, 0, 2}, {0, 1, 1, 2},
     {0, 0, 1, 2}, {0, 0, 0, 1},
     {1, 1, 1, 2},
    };

/**
 * Lookup table for copying macroblocks. Each entry contains the respective
 * x and y pixel offset for the copy source.
 */
static const int8_t motion_vector[16][2] =
    {{-4, -4}, {-2, -4},
     { 0, -4}, { 2, -4},
     {-4, -2}, {-4,  0},
     {-3, -3}, {-1, -3},
     { 1, -3}, { 3, -3},
     {-3, -1}, {-2, -2},
     { 0, -2}, { 2, -2},
     { 4, -2}, {-2,  0},
    };

static av_cold int yop_decode_close(AVCodecContext *avctx)
{
    YopDecContext *s = avctx->priv_data;

    av_frame_free(&s->frame);

    return 0;
}

static av_cold int yop_decode_init(AVCodecContext *avctx)
{
    YopDecContext *s = avctx->priv_data;
    s->avctx = avctx;

    if (avctx->width & 1 || avctx->height & 1 ||
        av_image_check_size(avctx->width, avctx->height, 0, avctx) < 0) {
        av_log(avctx, AV_LOG_ERROR, "YOP has invalid dimensions\n");
        return AVERROR_INVALIDDATA;
    }

    if (avctx->extradata_size < 3) {
        av_log(avctx, AV_LOG_ERROR, "Missing or incomplete extradata.\n");
        return AVERROR_INVALIDDATA;
    }

    avctx->pix_fmt = AV_PIX_FMT_PAL8;

    s->num_pal_colors = avctx->extradata[0];
    s->first_color[0] = avctx->extradata[1];
    s->first_color[1] = avctx->extradata[2];

    if (s->num_pal_colors + s->first_color[0] > 256 ||
        s->num_pal_colors + s->first_color[1] > 256) {
        av_log(avctx, AV_LOG_ERROR,
               "Palette parameters invalid, header probably corrupt\n");
        return AVERROR_INVALIDDATA;
    }

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);

    return 0;
}

/**
 * Paint a macroblock using the pattern in paint_lut.
 * @param s codec context
 * @param tag the tag that was in the nibble
 */
static int yop_paint_block(YopDecContext *s, int linesize, int tag)
{
    if (s->src_end - s->srcptr < paint_lut[tag][3]) {
        av_log(s->avctx, AV_LOG_ERROR, "Packet too small.\n");
        return AVERROR_INVALIDDATA;
    }

    s->dstptr[0]            = s->srcptr[0];
    s->dstptr[1]            = s->srcptr[paint_lut[tag][0]];
    s->dstptr[linesize]     = s->srcptr[paint_lut[tag][1]];
    s->dstptr[linesize + 1] = s->srcptr[paint_lut[tag][2]];

    // The number of src bytes consumed is in the last part of the lut entry.
    s->srcptr += paint_lut[tag][3];
    return 0;
}

/**
 * Copy a previously painted macroblock to the current_block.
 * @param copy_tag the tag that was in the nibble
 */
static int yop_copy_previous_block(YopDecContext *s, int linesize, int copy_tag)
{
    uint8_t *bufptr;

    // Calculate position for the copy source
    bufptr = s->dstptr + motion_vector[copy_tag][0] +
             linesize * motion_vector[copy_tag][1];
    if (bufptr < s->dstbuf) {
        av_log(s->avctx, AV_LOG_ERROR, "File probably corrupt\n");
        return AVERROR_INVALIDDATA;
    }

    s->dstptr[0]            = bufptr[0];
    s->dstptr[1]            = bufptr[1];
    s->dstptr[linesize]     = bufptr[linesize];
    s->dstptr[linesize + 1] = bufptr[linesize + 1];

    return 0;
}

/**
 * Return the next nibble in sequence, consuming a new byte on the input
 * only if necessary.
 */
static uint8_t yop_get_next_nibble(YopDecContext *s)
{
    int ret;

    if (s->low_nibble) {
        ret           = *s->low_nibble & 0xf;
        s->low_nibble = NULL;
    }else {
        s->low_nibble = s->srcptr++;
        ret           = *s->low_nibble >> 4;
    }
    return ret;
}

static int yop_decode_frame(AVCodecContext *avctx, AVFrame *rframe,
                            int *got_frame, AVPacket *avpkt)
{
    YopDecContext *s = avctx->priv_data;
    AVFrame *frame = s->frame;
    int tag, firstcolor, is_odd_frame;
    int ret, i, x, y;
    uint32_t *palette;

    if (avpkt->size < 4 + 3 * s->num_pal_colors) {
        av_log(avctx, AV_LOG_ERROR, "Packet too small.\n");
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_reget_buffer(avctx, frame, 0)) < 0)
        return ret;

    if (!avctx->frame_number)
        memset(frame->data[1], 0, AVPALETTE_SIZE);

    s->dstbuf     = frame->data[0];
    s->dstptr     = frame->data[0];
    s->srcptr     = avpkt->data + 4;
    s->src_end    = avpkt->data + avpkt->size;
    s->low_nibble = NULL;

    is_odd_frame = avpkt->data[0];
    if(is_odd_frame>1){
        av_log(avctx, AV_LOG_ERROR, "frame is too odd %d\n", is_odd_frame);
        return AVERROR_INVALIDDATA;
    }
    firstcolor   = s->first_color[is_odd_frame];
    palette      = (uint32_t *)frame->data[1];

    for (i = 0; i < s->num_pal_colors; i++, s->srcptr += 3) {
        palette[i + firstcolor] = (s->srcptr[0] << 18) |
                                  (s->srcptr[1] << 10) |
                                  (s->srcptr[2] << 2);
        palette[i + firstcolor] |= 0xFFU << 24 |
                                   (palette[i + firstcolor] >> 6) & 0x30303;
    }

    frame->palette_has_changed = 1;

    for (y = 0; y < avctx->height; y += 2) {
        for (x = 0; x < avctx->width; x += 2) {
            if (s->srcptr - avpkt->data >= avpkt->size) {
                av_log(avctx, AV_LOG_ERROR, "Packet too small.\n");
                return AVERROR_INVALIDDATA;
            }

            tag = yop_get_next_nibble(s);

            if (tag != 0xf) {
                ret = yop_paint_block(s, frame->linesize[0], tag);
                if (ret < 0)
                    return ret;
            } else {
                tag = yop_get_next_nibble(s);
                ret = yop_copy_previous_block(s, frame->linesize[0], tag);
                if (ret < 0)
                    return ret;
            }
            s->dstptr += 2;
        }
        s->dstptr += 2*frame->linesize[0] - x;
    }

    if ((ret = av_frame_ref(rframe, s->frame)) < 0)
        return ret;

    *got_frame = 1;
    return avpkt->size;
}

const FFCodec ff_yop_decoder = {
    .p.name         = "yop",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Psygnosis YOP Video"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_YOP,
    .priv_data_size = sizeof(YopDecContext),
    .init           = yop_decode_init,
    .close          = yop_decode_close,
    FF_CODEC_DECODE_CB(yop_decode_frame),
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
