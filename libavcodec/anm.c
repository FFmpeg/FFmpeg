/*
 * Deluxe Paint Animation decoder
 * Copyright (c) 2009 Peter Ross
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
 * Deluxe Paint Animation decoder
 */

#include "avcodec.h"
#include "bytestream.h"

typedef struct AnmContext {
    AVFrame frame;
    int x;  ///< x coordinate position
} AnmContext;

static av_cold int decode_init(AVCodecContext *avctx)
{
    AnmContext *s = avctx->priv_data;
    const uint8_t *buf;
    int i;

    avctx->pix_fmt = PIX_FMT_PAL8;

    if (avctx->extradata_size != 16*8 + 4*256)
        return -1;

    s->frame.reference = 1;
    if (avctx->get_buffer(avctx, &s->frame) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    buf = avctx->extradata + 16*8;
    for (i = 0; i < 256; i++)
        ((uint32_t*)s->frame.data[1])[i] = bytestream_get_le32(&buf);

    return 0;
}

/**
 * Perform decode operation
 * @param dst, dst_end Destination image buffer
 * @param buf, buf_end Source buffer (optional, see below)
 * @param pixel Fill color (optional, see below)
 * @param count Pixel count
 * @param x Pointer to x-axis counter
 * @param width Image width
 * @param linesize Destination image buffer linesize
 * @return non-zero if destination buffer is exhausted
 *
 * a copy operation is achieved when 'buf' is set
 * a fill operation is acheived when 'buf' is null and pixel is >= 0
 * a skip operation is acheived when 'buf' is null and pixel is < 0
 */
static inline int op(uint8_t **dst, const uint8_t *dst_end,
                     const uint8_t **buf, const uint8_t *buf_end,
                     int pixel, int count,
                     int *x, int width, int linesize)
{
    int remaining = width - *x;
    while(count > 0) {
        int striplen = FFMIN(count, remaining);
        if (buf) {
            striplen = FFMIN(striplen, buf_end - *buf);
            if (*buf >= buf_end)
                goto exhausted;
            memcpy(*dst, *buf, striplen);
            *buf += striplen;
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
                        void *data, int *data_size,
                        AVPacket *avpkt)
{
    AnmContext *s = avctx->priv_data;
    const uint8_t *buf = avpkt->data;
    const int buf_size = avpkt->size;
    const uint8_t *buf_end = buf + buf_size;
    uint8_t *dst, *dst_end;
    int count;

    if(avctx->reget_buffer(avctx, &s->frame) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    dst     = s->frame.data[0];
    dst_end = s->frame.data[0] + s->frame.linesize[0]*avctx->height;

    if (buf[0] != 0x42) {
        av_log_ask_for_sample(avctx, "unknown record type\n");
        return buf_size;
    }
    if (buf[1]) {
        av_log_ask_for_sample(avctx, "padding bytes not supported\n");
        return buf_size;
    }
    buf += 4;

    s->x = 0;
    do {
        /* if statements are ordered by probability */
#define OP(buf, pixel, count) \
    op(&dst, dst_end, (buf), buf_end, (pixel), (count), &s->x, avctx->width, s->frame.linesize[0])

        int type = bytestream_get_byte(&buf);
        count = type & 0x7F;
        type >>= 7;
        if (count) {
            if (OP(type ? NULL : &buf, -1, count)) break;
        } else if (!type) {
            int pixel;
            count = bytestream_get_byte(&buf);  /* count==0 gives nop */
            pixel = bytestream_get_byte(&buf);
            if (OP(NULL, pixel, count)) break;
        } else {
            int pixel;
            type = bytestream_get_le16(&buf);
            count = type & 0x3FFF;
            type >>= 14;
            if (!count) {
                if (type == 0)
                    break; // stop
                if (type == 2) {
                    av_log_ask_for_sample(avctx, "unknown opcode");
                    return AVERROR_INVALIDDATA;
                }
                continue;
            }
            pixel = type == 3 ? bytestream_get_byte(&buf) : -1;
            if (type == 1) count += 0x4000;
            if (OP(type == 2 ? &buf : NULL, pixel, count)) break;
        }
    } while (buf + 1 < buf_end);

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;
    return buf_size;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    AnmContext *s = avctx->priv_data;
    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);
    return 0;
}

AVCodec ff_anm_decoder = {
    .name           = "anm",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_ANM,
    .priv_data_size = sizeof(AnmContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("Deluxe Paint Animation"),
};
