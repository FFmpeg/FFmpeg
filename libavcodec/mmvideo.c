/*
 * American Laser Games MM Video Decoder
 * Copyright (c) 2006,2008 Peter Ross
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * American Laser Games MM Video Decoder
 * by Peter Ross (pross@xvid.org)
 *
 * The MM format was used by IBM-PC ports of ALG's "arcade shooter" games,
 * including Mad Dog McCree and Crime Patrol.
 *
 * Technical details here:
 *  http://wiki.multimedia.cx/index.php?title=American_Laser_Games_MM
 */

#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"

#define MM_PREAMBLE_SIZE    6

#define MM_TYPE_INTER       0x5
#define MM_TYPE_INTRA       0x8
#define MM_TYPE_INTRA_HH    0xc
#define MM_TYPE_INTER_HH    0xd
#define MM_TYPE_INTRA_HHV   0xe
#define MM_TYPE_INTER_HHV   0xf
#define MM_TYPE_PALETTE     0x31

typedef struct MmContext {
    AVCodecContext *avctx;
    AVFrame *frame;
    int palette[AVPALETTE_COUNT];
    GetByteContext gb;
} MmContext;

static av_cold int mm_decode_init(AVCodecContext *avctx)
{
    MmContext *s = avctx->priv_data;

    s->avctx = avctx;

    avctx->pix_fmt = AV_PIX_FMT_PAL8;

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);

    return 0;
}

static int mm_decode_pal(MmContext *s)
{
    int i;

    bytestream2_skip(&s->gb, 4);
    for (i = 0; i < 128; i++) {
        s->palette[i] = bytestream2_get_be24(&s->gb);
        s->palette[i+128] = s->palette[i]<<2;
    }

    return 0;
}

/**
 * @param half_horiz Half horizontal resolution (0 or 1)
 * @param half_vert Half vertical resolution (0 or 1)
 */
static int mm_decode_intra(MmContext * s, int half_horiz, int half_vert)
{
    int x = 0, y = 0;

    while (bytestream2_get_bytes_left(&s->gb) > 0) {
        int run_length, color;

        if (y >= s->avctx->height)
            return 0;

        color = bytestream2_get_byte(&s->gb);
        if (color & 0x80) {
            run_length = 1;
        }else{
            run_length = (color & 0x7f) + 2;
            color = bytestream2_get_byte(&s->gb);
        }

        if (half_horiz)
            run_length *=2;

        if (color) {
            memset(s->frame->data[0] + y*s->frame->linesize[0] + x, color, run_length);
            if (half_vert)
                memset(s->frame->data[0] + (y+1)*s->frame->linesize[0] + x, color, run_length);
        }
        x+= run_length;

        if (x >= s->avctx->width) {
            x=0;
            y += 1 + half_vert;
        }
    }

    return 0;
}

/*
 * @param half_horiz Half horizontal resolution (0 or 1)
 * @param half_vert Half vertical resolution (0 or 1)
 */
static int mm_decode_inter(MmContext * s, int half_horiz, int half_vert)
{
    int data_off = bytestream2_get_le16(&s->gb);
    int y = 0;
    GetByteContext data_ptr;

    if (bytestream2_get_bytes_left(&s->gb) < data_off)
        return AVERROR_INVALIDDATA;

    bytestream2_init(&data_ptr, s->gb.buffer + data_off, bytestream2_get_bytes_left(&s->gb) - data_off);
    while (s->gb.buffer < data_ptr.buffer_start) {
        int i, j;
        int length = bytestream2_get_byte(&s->gb);
        int x = bytestream2_get_byte(&s->gb) + ((length & 0x80) << 1);
        length &= 0x7F;

        if (length==0) {
            y += x;
            continue;
        }

        if (y + half_vert >= s->avctx->height)
            return 0;

        for(i=0; i<length; i++) {
            int replace_array = bytestream2_get_byte(&s->gb);
            for(j=0; j<8; j++) {
                int replace = (replace_array >> (7-j)) & 1;
                if (x + half_horiz >= s->avctx->width)
                    return AVERROR_INVALIDDATA;
                if (replace) {
                    int color = bytestream2_get_byte(&data_ptr);
                    s->frame->data[0][y*s->frame->linesize[0] + x] = color;
                    if (half_horiz)
                        s->frame->data[0][y*s->frame->linesize[0] + x + 1] = color;
                    if (half_vert) {
                        s->frame->data[0][(y+1)*s->frame->linesize[0] + x] = color;
                        if (half_horiz)
                            s->frame->data[0][(y+1)*s->frame->linesize[0] + x + 1] = color;
                    }
                }
                x += 1 + half_horiz;
            }
        }

        y += 1 + half_vert;
    }

    return 0;
}

static int mm_decode_frame(AVCodecContext *avctx,
                            void *data, int *got_frame,
                            AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    MmContext *s = avctx->priv_data;
    int type, res;

    if (buf_size < MM_PREAMBLE_SIZE)
        return AVERROR_INVALIDDATA;
    type = AV_RL16(&buf[0]);
    buf += MM_PREAMBLE_SIZE;
    buf_size -= MM_PREAMBLE_SIZE;
    bytestream2_init(&s->gb, buf, buf_size);

    if ((res = ff_reget_buffer(avctx, s->frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return res;
    }

    switch(type) {
    case MM_TYPE_PALETTE   : res = mm_decode_pal(s); return buf_size;
    case MM_TYPE_INTRA     : res = mm_decode_intra(s, 0, 0); break;
    case MM_TYPE_INTRA_HH  : res = mm_decode_intra(s, 1, 0); break;
    case MM_TYPE_INTRA_HHV : res = mm_decode_intra(s, 1, 1); break;
    case MM_TYPE_INTER     : res = mm_decode_inter(s, 0, 0); break;
    case MM_TYPE_INTER_HH  : res = mm_decode_inter(s, 1, 0); break;
    case MM_TYPE_INTER_HHV : res = mm_decode_inter(s, 1, 1); break;
    default:
        res = AVERROR_INVALIDDATA;
        break;
    }
    if (res < 0)
        return res;

    memcpy(s->frame->data[1], s->palette, AVPALETTE_SIZE);

    if ((res = av_frame_ref(data, s->frame)) < 0)
        return res;

    *got_frame      = 1;

    return buf_size;
}

static av_cold int mm_decode_end(AVCodecContext *avctx)
{
    MmContext *s = avctx->priv_data;

    av_frame_free(&s->frame);

    return 0;
}

AVCodec ff_mmvideo_decoder = {
    .name           = "mmvideo",
    .long_name      = NULL_IF_CONFIG_SMALL("American Laser Games MM Video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MMVIDEO,
    .priv_data_size = sizeof(MmContext),
    .init           = mm_decode_init,
    .close          = mm_decode_end,
    .decode         = mm_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
