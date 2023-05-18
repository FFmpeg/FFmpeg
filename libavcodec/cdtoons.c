/*
 * CDToons video decoder
 * Copyright (C) 2020 Alyssa Milburn <amilburn@zall.org>
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
 * CDToons video decoder
 * @author Alyssa Milburn <amilburn@zall.org>
 */

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/internal.h"
#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"

#define CDTOONS_HEADER_SIZE   44
#define CDTOONS_MAX_SPRITES 1200

typedef struct CDToonsSprite {
    uint16_t flags;
    uint16_t owner_frame;
    uint16_t start_frame;
    uint16_t end_frame;
    unsigned int alloc_size;
    uint32_t size;
    uint8_t *data;
    int      active;
} CDToonsSprite;

typedef struct CDToonsContext {
    AVFrame *frame;

    uint16_t last_pal_id;   ///< The index of the active palette sprite.
    uint32_t pal[256];      ///< The currently-used palette data.
    CDToonsSprite sprites[CDTOONS_MAX_SPRITES];
} CDToonsContext;

static int cdtoons_render_sprite(AVCodecContext *avctx, const uint8_t *data,
                                 uint32_t data_size,
                                 int dst_x, int dst_y, int width, int height)
{
    CDToonsContext *c = avctx->priv_data;
    const uint8_t *next_line = data;
    const uint8_t *end = data + data_size;
    uint16_t line_size;
    uint8_t *dest;
    int skip = 0, to_skip, x;

    if (dst_x + width > avctx->width)
        width = avctx->width - dst_x;
    if (dst_y + height > avctx->height)
        height = avctx->height - dst_y;

    if (dst_x < 0) {
        /* we need to skip the start of the scanlines */
        skip = -dst_x;
        if (width <= skip)
            return 0;
        dst_x = 0;
    }

    for (int y = 0; y < height; y++) {
        /* one scanline at a time, size is provided */
        data      = next_line;
        if (end - data < 2)
            return 1;
        line_size = bytestream_get_be16(&data);
        if (end - data < line_size)
            return 1;
        next_line = data + line_size;
        if (dst_y + y < 0)
            continue;

        dest = c->frame->data[0] + (dst_y + y) * c->frame->linesize[0] + dst_x;

        to_skip = skip;
        x       = 0;
        while (x < width - skip) {
            int raw, size, step;
            uint8_t val;

            if (data >= end)
                return 1;

            val  = bytestream_get_byte(&data);
            raw  = !(val & 0x80);
            size = (int)(val & 0x7F) + 1;

            /* skip the start of a scanline if it is off-screen */
            if (to_skip >= size) {
                to_skip -= size;
                if (raw) {
                    step = size;
                } else {
                    step = 1;
                }
                if (next_line - data < step)
                    return 1;
                data += step;
                continue;
            } else if (to_skip) {
                size -= to_skip;
                if (raw) {
                    if (next_line - data < to_skip)
                        return 1;
                    data += to_skip;
                }
                to_skip = 0;
            }

            if (x + size >= width - skip)
                size = width - skip - x;

            /* either raw data, or a run of a single color */
            if (raw) {
                if (next_line - data < size)
                    return 1;
                memcpy(dest + x, data, size);
                data += size;
            } else {
                uint8_t color = bytestream_get_byte(&data);
                /* ignore transparent runs */
                if (color)
                    memset(dest + x, color, size);
            }
            x += size;
        }
    }

    return 0;
}

static int cdtoons_decode_frame(AVCodecContext *avctx, AVFrame *rframe,
                                int *got_frame, AVPacket *avpkt)
{
    CDToonsContext *c = avctx->priv_data;
    const uint8_t *buf = avpkt->data;
    const uint8_t *eod = avpkt->data + avpkt->size;
    const int buf_size = avpkt->size;
    uint16_t frame_id;
    uint8_t background_color;
    uint16_t sprite_count, sprite_offset;
    uint8_t referenced_count;
    uint16_t palette_id;
    uint8_t palette_set;
    int ret;
    int saw_embedded_sprites = 0;

    if (buf_size < CDTOONS_HEADER_SIZE)
        return AVERROR_INVALIDDATA;

    if ((ret = ff_reget_buffer(avctx, c->frame, 0)) < 0)
        return ret;

    /* a lot of the header is useless junk in the absence of
     * dirty rectangling etc */
    buf               += 2; /* version? (always 9?) */
    frame_id           = bytestream_get_be16(&buf);
    buf               += 2; /* blocks_valid_until */
    buf               += 1;
    background_color   = bytestream_get_byte(&buf);
    buf               += 16; /* clip rect, dirty rect */
    buf               += 4; /* flags */
    sprite_count       = bytestream_get_be16(&buf);
    sprite_offset      = bytestream_get_be16(&buf);
    buf               += 2; /* max block id? */
    referenced_count   = bytestream_get_byte(&buf);
    buf               += 1;
    palette_id         = bytestream_get_be16(&buf);
    palette_set        = bytestream_get_byte(&buf);
    buf               += 5;

    if (sprite_offset > buf_size)
        return AVERROR_INVALIDDATA;

    /* read new sprites introduced in this frame */
    buf = avpkt->data + sprite_offset;
    while (sprite_count--) {
        uint32_t size;
        uint16_t sprite_id;

        if (buf + 14 > eod)
            return AVERROR_INVALIDDATA;

        sprite_id = bytestream_get_be16(&buf);
        if (sprite_id >= CDTOONS_MAX_SPRITES) {
            av_log(avctx, AV_LOG_ERROR,
                   "Sprite ID %d is too high.\n", sprite_id);
            return AVERROR_INVALIDDATA;
        }
        if (c->sprites[sprite_id].active) {
            av_log(avctx, AV_LOG_ERROR,
                   "Sprite ID %d is a duplicate.\n", sprite_id);
            return AVERROR_INVALIDDATA;
        }

        c->sprites[sprite_id].flags = bytestream_get_be16(&buf);
        size                        = bytestream_get_be32(&buf);
        if (size < 14) {
            av_log(avctx, AV_LOG_ERROR,
                   "Sprite only has %d bytes of data.\n", size);
            return AVERROR_INVALIDDATA;
        }
        size -= 14;
        c->sprites[sprite_id].size        = size;
        c->sprites[sprite_id].owner_frame = frame_id;
        c->sprites[sprite_id].start_frame = bytestream_get_be16(&buf);
        c->sprites[sprite_id].end_frame   = bytestream_get_be16(&buf);
        buf += 2;

        if (size > buf_size || buf + size > eod)
            return AVERROR_INVALIDDATA;

        av_fast_padded_malloc(&c->sprites[sprite_id].data, &c->sprites[sprite_id].alloc_size, size);
        if (!c->sprites[sprite_id].data)
            return AVERROR(ENOMEM);

        c->sprites[sprite_id].active = 1;

        bytestream_get_buffer(&buf, c->sprites[sprite_id].data, size);
    }

    /* render any embedded sprites */
    while (buf < eod) {
        uint32_t tag, size;
        if (buf + 8 > eod) {
            av_log(avctx, AV_LOG_WARNING, "Ran (seriously) out of data for embedded sprites.\n");
            return AVERROR_INVALIDDATA;
        }
        tag  = bytestream_get_be32(&buf);
        size = bytestream_get_be32(&buf);
        if (tag == MKBETAG('D', 'i', 'f', 'f')) {
            uint16_t diff_count;
            if (buf + 10 > eod) {
                av_log(avctx, AV_LOG_WARNING, "Ran (seriously) out of data for Diff frame.\n");
                return AVERROR_INVALIDDATA;
            }
            diff_count = bytestream_get_be16(&buf);
            buf       += 8; /* clip rect? */
            for (int i = 0; i < diff_count; i++) {
                int16_t top, left;
                uint16_t diff_size, width, height;

                if (buf + 16 > eod) {
                    av_log(avctx, AV_LOG_WARNING, "Ran (seriously) out of data for Diff frame header.\n");
                    return AVERROR_INVALIDDATA;
                }

                top        = bytestream_get_be16(&buf);
                left       = bytestream_get_be16(&buf);
                buf       += 4; /* bottom, right */
                diff_size  = bytestream_get_be32(&buf);
                width      = bytestream_get_be16(&buf);
                height     = bytestream_get_be16(&buf);
                if (diff_size < 8 || diff_size - 4 > eod - buf) {
                    av_log(avctx, AV_LOG_WARNING, "Ran (seriously) out of data for Diff frame data.\n");
                    return AVERROR_INVALIDDATA;
                }
                if (cdtoons_render_sprite(avctx, buf + 4, diff_size - 8,
                                      left, top, width, height)) {
                    av_log(avctx, AV_LOG_WARNING, "Ran beyond end of sprite while rendering.\n");
                }
                buf += diff_size - 4;
            }
            saw_embedded_sprites = 1;
        } else {
            /* we don't care about any other entries */
            if (size < 8 || size - 8 > eod - buf) {
                av_log(avctx, AV_LOG_WARNING, "Ran out of data for ignored entry (size %X, %d left).\n", size, (int)(eod - buf));
                return AVERROR_INVALIDDATA;
            }
            buf += (size - 8);
        }
    }

    /* was an intra frame? */
    if (saw_embedded_sprites)
        goto done;

    /* render any referenced sprites */
    buf = avpkt->data + CDTOONS_HEADER_SIZE;
    eod = avpkt->data + sprite_offset;
    for (int i = 0; i < referenced_count; i++) {
        const uint8_t *block_data;
        uint16_t sprite_id, width, height;
        int16_t top, left, right;

        if (buf + 10 > eod) {
            av_log(avctx, AV_LOG_WARNING, "Ran (seriously) out of data when rendering.\n");
            return AVERROR_INVALIDDATA;
        }

        sprite_id = bytestream_get_be16(&buf);
        top       = bytestream_get_be16(&buf);
        left      = bytestream_get_be16(&buf);
        buf      += 2; /* bottom */
        right     = bytestream_get_be16(&buf);

        if ((i == 0) && (sprite_id == 0)) {
            /* clear background */
            memset(c->frame->data[0], background_color,
                   c->frame->linesize[0] * avctx->height);
        }

        if (!right)
            continue;
        if (sprite_id >= CDTOONS_MAX_SPRITES) {
            av_log(avctx, AV_LOG_ERROR,
                   "Sprite ID %d is too high.\n", sprite_id);
            return AVERROR_INVALIDDATA;
        }

        block_data = c->sprites[sprite_id].data;
        if (!c->sprites[sprite_id].active) {
            /* this can happen when seeking around */
            av_log(avctx, AV_LOG_WARNING, "Sprite %d is missing.\n", sprite_id);
            continue;
        }
        if (c->sprites[sprite_id].size < 14) {
            av_log(avctx, AV_LOG_ERROR, "Sprite %d is too small.\n", sprite_id);
            continue;
        }

        height      = bytestream_get_be16(&block_data);
        width       = bytestream_get_be16(&block_data);
        block_data += 10;
        if (cdtoons_render_sprite(avctx, block_data,
                              c->sprites[sprite_id].size - 14,
                              left, top, width, height)) {
            av_log(avctx, AV_LOG_WARNING, "Ran beyond end of sprite while rendering.\n");
        }
    }

    if (palette_id && (palette_id != c->last_pal_id)) {
        if (palette_id >= CDTOONS_MAX_SPRITES) {
            av_log(avctx, AV_LOG_ERROR,
                   "Palette ID %d is too high.\n", palette_id);
            return AVERROR_INVALIDDATA;
        }
        if (!c->sprites[palette_id].active) {
            /* this can happen when seeking around */
            av_log(avctx, AV_LOG_WARNING,
                   "Palette ID %d is missing.\n", palette_id);
            goto done;
        }
        if (c->sprites[palette_id].size != 256 * 2 * 3) {
            av_log(avctx, AV_LOG_ERROR,
                   "Palette ID %d is wrong size (%d).\n",
                   palette_id, c->sprites[palette_id].size);
            return AVERROR_INVALIDDATA;
        }
        c->last_pal_id = palette_id;
        if (!palette_set) {
            uint8_t *palette_data = c->sprites[palette_id].data;
            for (int i = 0; i < 256; i++) {
                /* QuickTime-ish palette: 16-bit RGB components */
                unsigned r, g, b;
                r             = *palette_data;
                g             = *(palette_data + 2);
                b             = *(palette_data + 4);
                c->pal[i]     = (0xFFU << 24) | (r << 16) | (g << 8) | b;
                palette_data += 6;
            }
            /* first palette entry indicates transparency */
            c->pal[0]                     = 0;
#if FF_API_PALETTE_HAS_CHANGED
FF_DISABLE_DEPRECATION_WARNINGS
            c->frame->palette_has_changed = 1;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        }
    }

done:
    /* discard outdated blocks */
    for (int i = 0; i < CDTOONS_MAX_SPRITES; i++) {
        if (c->sprites[i].end_frame > frame_id)
            continue;
        c->sprites[i].active = 0;
    }

    memcpy(c->frame->data[1], c->pal, AVPALETTE_SIZE);

    if ((ret = av_frame_ref(rframe, c->frame)) < 0)
        return ret;

    *got_frame = 1;

    /* always report that the buffer was completely consumed */
    return buf_size;
}

static av_cold int cdtoons_decode_init(AVCodecContext *avctx)
{
    CDToonsContext *c = avctx->priv_data;

    avctx->pix_fmt = AV_PIX_FMT_PAL8;
    c->last_pal_id = 0;
    c->frame       = av_frame_alloc();
    if (!c->frame)
        return AVERROR(ENOMEM);

    return 0;
}

static void cdtoons_flush(AVCodecContext *avctx)
{
    CDToonsContext *c = avctx->priv_data;

    c->last_pal_id = 0;
    for (int i = 0; i < CDTOONS_MAX_SPRITES; i++)
        c->sprites[i].active = 0;
}

static av_cold int cdtoons_decode_end(AVCodecContext *avctx)
{
    CDToonsContext *c = avctx->priv_data;

    for (int i = 0; i < CDTOONS_MAX_SPRITES; i++) {
        av_freep(&c->sprites[i].data);
        c->sprites[i].active = 0;
    }

    av_frame_free(&c->frame);

    return 0;
}

const FFCodec ff_cdtoons_decoder = {
    .p.name         = "cdtoons",
    CODEC_LONG_NAME("CDToons video"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_CDTOONS,
    .priv_data_size = sizeof(CDToonsContext),
    .init           = cdtoons_decode_init,
    .close          = cdtoons_decode_end,
    FF_CODEC_DECODE_CB(cdtoons_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
    .flush          = cdtoons_flush,
};
