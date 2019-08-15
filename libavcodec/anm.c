/*
 * Deluxe Paint Animation decoder
 * Copyright (c) 2009 Peter Ross
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
 * Deluxe Paint Animation decoder
 */

#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"

typedef struct AnmContext {
    AVFrame *frame;
    int palette[AVPALETTE_COUNT];
    GetByteContext gb;
    int x;  ///< x coordinate position
} AnmContext;

static av_cold int decode_init(AVCodecContext *avctx)
{
    AnmContext *s = avctx->priv_data;
    int i;

    avctx->pix_fmt = AV_PIX_FMT_PAL8;

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);

    bytestream2_init(&s->gb, avctx->extradata, avctx->extradata_size);
    if (bytestream2_get_bytes_left(&s->gb) < 16 * 8 + 4 * 256) {
        av_frame_free(&s->frame);
        return AVERROR_INVALIDDATA;
    }

    bytestream2_skipu(&s->gb, 16 * 8);
    for (i = 0; i < 256; i++)
        s->palette[i] = bytestream2_get_le32u(&s->gb);

    return 0;
}

/**
 * Perform decode operation
 * @param dst     pointer to destination image buffer
 * @param dst_end pointer to end of destination image buffer
 * @param gb GetByteContext (optional, see below)
 * @param pixel Fill color (optional, see below)
 * @param count Pixel count
 * @param x Pointer to x-axis counter
 * @param width Image width
 * @param linesize Destination image buffer linesize
 * @return non-zero if destination buffer is exhausted
 *
 * a copy operation is achieved when 'gb' is set
 * a fill operation is achieved when 'gb' is null and pixel is >= 0
 * a skip operation is achieved when 'gb' is null and pixel is < 0
 */
static inline int op(uint8_t **dst, const uint8_t *dst_end,
                     GetByteContext *gb,
                     int pixel, int count,
                     int *x, int width, int linesize)
{
    int remaining = width - *x;
    while(count > 0) {
        int striplen = FFMIN(count, remaining);
        if (gb) {
            if (bytestream2_get_bytes_left(gb) < striplen)
                goto exhausted;
            bytestream2_get_bufferu(gb, *dst, striplen);
        } else if (pixel >= 0)
            memset(*dst, pixel, striplen);
        *dst      += striplen;
        remaining -= striplen;
        count     -= striplen;
        if (remaining <= 0) {
            *dst      += linesize - width;
            remaining  = width;
        }
        if (linesize > 0) {
            if (*dst >= dst_end) goto exhausted;
        } else {
            if (*dst <= dst_end) goto exhausted;
        }
    }
    *x = width - remaining;
    return 0;

exhausted:
    *x = width - remaining;
    return 1;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *got_frame,
                        AVPacket *avpkt)
{
    AnmContext *s = avctx->priv_data;
    const int buf_size = avpkt->size;
    uint8_t *dst, *dst_end;
    int count, ret;

    if (buf_size < 7)
        return AVERROR_INVALIDDATA;

    if ((ret = ff_reget_buffer(avctx, s->frame)) < 0)
        return ret;
    dst     = s->frame->data[0];
    dst_end = s->frame->data[0] + s->frame->linesize[0]*avctx->height;

    bytestream2_init(&s->gb, avpkt->data, buf_size);

    if (bytestream2_get_byte(&s->gb) != 0x42) {
        avpriv_request_sample(avctx, "Unknown record type");
        return AVERROR_INVALIDDATA;
    }
    if (bytestream2_get_byte(&s->gb)) {
        avpriv_request_sample(avctx, "Padding bytes");
        return AVERROR_PATCHWELCOME;
    }
    bytestream2_skip(&s->gb, 2);

    s->x = 0;
    do {
        /* if statements are ordered by probability */
#define OP(gb, pixel, count) \
    op(&dst, dst_end, (gb), (pixel), (count), &s->x, avctx->width, s->frame->linesize[0])

        int type = bytestream2_get_byte(&s->gb);
        count = type & 0x7F;
        type >>= 7;
        if (count) {
            if (OP(type ? NULL : &s->gb, -1, count)) break;
        } else if (!type) {
            int pixel;
            count = bytestream2_get_byte(&s->gb);  /* count==0 gives nop */
            pixel = bytestream2_get_byte(&s->gb);
            if (OP(NULL, pixel, count)) break;
        } else {
            int pixel;
            type = bytestream2_get_le16(&s->gb);
            count = type & 0x3FFF;
            type >>= 14;
            if (!count) {
                if (type == 0)
                    break; // stop
                if (type == 2) {
                    avpriv_request_sample(avctx, "Unknown opcode");
                    return AVERROR_PATCHWELCOME;
                }
                continue;
            }
            pixel = type == 3 ? bytestream2_get_byte(&s->gb) : -1;
            if (type == 1) count += 0x4000;
            if (OP(type == 2 ? &s->gb : NULL, pixel, count)) break;
        }
    } while (bytestream2_get_bytes_left(&s->gb) > 0);

    memcpy(s->frame->data[1], s->palette, AVPALETTE_SIZE);

    *got_frame = 1;
    if ((ret = av_frame_ref(data, s->frame)) < 0)
        return ret;

    return buf_size;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    AnmContext *s = avctx->priv_data;

    av_frame_free(&s->frame);
    return 0;
}

AVCodec ff_anm_decoder = {
    .name           = "anm",
    .long_name      = NULL_IF_CONFIG_SMALL("Deluxe Paint Animation"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_ANM,
    .priv_data_size = sizeof(AnmContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
