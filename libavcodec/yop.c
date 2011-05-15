/**
 * @file
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

#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"

#include "avcodec.h"
#include "get_bits.h"

typedef struct YopDecContext {
    AVFrame frame;
    AVCodecContext *avctx;

    int num_pal_colors;
    int first_color[2];
    int frame_data_length;
    int row_pos;

    uint8_t *low_nibble;
    uint8_t *srcptr;
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

static av_cold int yop_decode_init(AVCodecContext *avctx)
{
    YopDecContext *s = avctx->priv_data;
    s->avctx = avctx;

    if (avctx->width & 1 || avctx->height & 1 ||
        av_image_check_size(avctx->width, avctx->height, 0, avctx) < 0) {
        av_log(avctx, AV_LOG_ERROR, "YOP has invalid dimensions\n");
        return -1;
    }

    avctx->pix_fmt = PIX_FMT_PAL8;

    avcodec_get_frame_defaults(&s->frame);
    s->num_pal_colors = avctx->extradata[0];
    s->first_color[0] = avctx->extradata[1];
    s->first_color[1] = avctx->extradata[2];

    if (s->num_pal_colors + s->first_color[0] > 256 ||
        s->num_pal_colors + s->first_color[1] > 256) {
        av_log(avctx, AV_LOG_ERROR,
               "YOP: palette parameters invalid, header probably corrupt\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static av_cold int yop_decode_close(AVCodecContext *avctx)
{
    YopDecContext *s = avctx->priv_data;
    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);
    return 0;
}

/**
 * Paint a macroblock using the pattern in paint_lut.
 * @param s codec context
 * @param tag the tag that was in the nibble
 */
static void yop_paint_block(YopDecContext *s, int tag)
{
    s->dstptr[0]                        = s->srcptr[0];
    s->dstptr[1]                        = s->srcptr[paint_lut[tag][0]];
    s->dstptr[s->frame.linesize[0]]     = s->srcptr[paint_lut[tag][1]];
    s->dstptr[s->frame.linesize[0] + 1] = s->srcptr[paint_lut[tag][2]];

    // The number of src bytes consumed is in the last part of the lut entry.
    s->srcptr += paint_lut[tag][3];
}

/**
 * Copy a previously painted macroblock to the current_block.
 * @param copy_tag the tag that was in the nibble
 */
static int yop_copy_previous_block(YopDecContext *s, int copy_tag)
{
    uint8_t *bufptr;

    // Calculate position for the copy source
    bufptr = s->dstptr + motion_vector[copy_tag][0] +
             s->frame.linesize[0] * motion_vector[copy_tag][1];
    if (bufptr < s->dstbuf) {
        av_log(s->avctx, AV_LOG_ERROR,
               "YOP: cannot decode, file probably corrupt\n");
        return AVERROR_INVALIDDATA;
    }

    s->dstptr[0]                        = bufptr[0];
    s->dstptr[1]                        = bufptr[1];
    s->dstptr[s->frame.linesize[0]]     = bufptr[s->frame.linesize[0]];
    s->dstptr[s->frame.linesize[0] + 1] = bufptr[s->frame.linesize[0] + 1];

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

/**
 * Take s->dstptr to the next macroblock in sequence.
 */
static void yop_next_macroblock(YopDecContext *s)
{
    // If we are advancing to the next row of macroblocks
    if (s->row_pos == s->frame.linesize[0] - 2) {
        s->dstptr  += s->frame.linesize[0];
        s->row_pos =  0;
    }else {
        s->row_pos += 2;
    }
    s->dstptr += 2;
}

static int yop_decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                            AVPacket *avpkt)
{
    YopDecContext *s = avctx->priv_data;
    int tag, firstcolor, is_odd_frame;
    int ret, i;
    uint32_t *palette;

    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    ret = avctx->get_buffer(avctx, &s->frame);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }

    s->frame.linesize[0] = avctx->width;

    s->dstbuf     = s->frame.data[0];
    s->dstptr     = s->frame.data[0];
    s->srcptr     = avpkt->data + 4;
    s->row_pos    = 0;
    s->low_nibble = NULL;

    is_odd_frame = avpkt->data[0];
    firstcolor   = s->first_color[is_odd_frame];
    palette      = (uint32_t *)s->frame.data[1];

    for (i = 0; i < s->num_pal_colors; i++, s->srcptr += 3)
        palette[i + firstcolor] = (s->srcptr[0] << 18) |
                                  (s->srcptr[1] << 10) |
                                  (s->srcptr[2] << 2);

    s->frame.palette_has_changed = 1;

    while (s->dstptr - s->dstbuf <
           avctx->width * avctx->height &&
           s->srcptr - avpkt->data < avpkt->size) {

        tag = yop_get_next_nibble(s);

        if (tag != 0xf) {
            yop_paint_block(s, tag);
        }else {
            tag = yop_get_next_nibble(s);
            ret = yop_copy_previous_block(s, tag);
            if (ret < 0) {
                avctx->release_buffer(avctx, &s->frame);
                return ret;
            }
        }
        yop_next_macroblock(s);
    }

    *data_size        = sizeof(AVFrame);
    *(AVFrame *) data = s->frame;
    return avpkt->size;
}

AVCodec ff_yop_decoder = {
    "yop",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_YOP,
    sizeof(YopDecContext),
    yop_decode_init,
    NULL,
    yop_decode_close,
    yop_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("Psygnosis YOP Video"),
};
