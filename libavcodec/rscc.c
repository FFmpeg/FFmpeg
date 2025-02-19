/*
 * innoHeim/Rsupport Screen Capture Codec
 * Copyright (C) 2015 Vittorio Giovara <vittorio.giovara@gmail.com>
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
 * innoHeim/Rsupport Screen Capture Codec decoder
 *
 * Fourcc: ISCC, RSCC
 *
 * Lossless codec, data stored in tiles, with optional deflate compression.
 *
 * Header contains the number of tiles in a frame with the tile coordinates,
 * and it can be deflated or not. Similarly, pixel data comes after the header
 * and a variable size value, and it can be deflated or just raw.
 *
 * Supports: PAL8, BGRA, BGR24, RGB555
 */

#include <stdint.h>
#include <string.h>
#include <zlib.h>

#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"

#define TILE_SIZE 8

typedef struct Tile {
    int x, y;
    int w, h;
} Tile;

typedef struct RsccContext {
    GetByteContext gbc;
    AVFrame *reference;
    Tile *tiles;
    unsigned int tiles_size;
    int component_size;

    uint8_t palette[AVPALETTE_SIZE];

    /* zlib interaction */
    uint8_t *inflated_buf;
    uLongf inflated_size;
    int valid_pixels;
} RsccContext;

static av_cold int rscc_init(AVCodecContext *avctx)
{
    RsccContext *ctx = avctx->priv_data;

    /* These needs to be set to estimate uncompressed buffer */
    int ret = av_image_check_size(avctx->width, avctx->height, 0, avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid image size %dx%d.\n",
               avctx->width, avctx->height);
        return ret;
    }

    /* Allocate reference frame */
    ctx->reference = av_frame_alloc();
    if (!ctx->reference)
        return AVERROR(ENOMEM);

    /* Get pixel format and the size of the pixel */
    if (avctx->codec_tag == MKTAG('I', 'S', 'C', 'C')) {
        if (avctx->extradata && avctx->extradata_size == 4) {
            if ((avctx->extradata[0] >> 1) & 1) {
                avctx->pix_fmt = AV_PIX_FMT_BGRA;
                ctx->component_size = 4;
            } else {
                avctx->pix_fmt = AV_PIX_FMT_BGR24;
                ctx->component_size = 3;
            }
        } else {
            avctx->pix_fmt = AV_PIX_FMT_BGRA;
            ctx->component_size = 4;
        }
    } else if (avctx->codec_tag == MKTAG('R', 'S', 'C', 'C')) {
        ctx->component_size = avctx->bits_per_coded_sample / 8;
        switch (avctx->bits_per_coded_sample) {
        case 8:
            avctx->pix_fmt = AV_PIX_FMT_PAL8;
            break;
        case 16:
            avctx->pix_fmt = AV_PIX_FMT_RGB555LE;
            break;
        case 24:
            avctx->pix_fmt = AV_PIX_FMT_BGR24;
            break;
        case 32:
            avctx->pix_fmt = AV_PIX_FMT_BGR0;
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "Invalid bits per pixel value (%d)\n",
                   avctx->bits_per_coded_sample);
            return AVERROR_INVALIDDATA;
        }
    } else {
        avctx->pix_fmt = AV_PIX_FMT_BGR0;
        ctx->component_size = 4;
        av_log(avctx, AV_LOG_WARNING, "Invalid codec tag\n");
    }

    /* Store the value to check for keyframes */
    ctx->inflated_size = avctx->width * avctx->height * ctx->component_size;

    /* Allocate maximum size possible, a full frame */
    ctx->inflated_buf = av_malloc(ctx->inflated_size);
    if (!ctx->inflated_buf)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold int rscc_close(AVCodecContext *avctx)
{
    RsccContext *ctx = avctx->priv_data;

    av_freep(&ctx->tiles);
    av_freep(&ctx->inflated_buf);
    av_frame_free(&ctx->reference);

    return 0;
}

static int rscc_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                             int *got_frame, AVPacket *avpkt)
{
    RsccContext *ctx = avctx->priv_data;
    GetByteContext *gbc = &ctx->gbc;
    GetByteContext tiles_gbc;
    const uint8_t *pixels, *raw;
    uint8_t *inflated_tiles = NULL;
    int tiles_nb, packed_size, pixel_size = 0;
    int i, ret = 0;

    bytestream2_init(gbc, avpkt->data, avpkt->size);

    /* Size check */
    if (bytestream2_get_bytes_left(gbc) < 12) {
        av_log(avctx, AV_LOG_ERROR, "Packet too small (%d)\n", avpkt->size);
        return AVERROR_INVALIDDATA;
    }

    /* Read number of tiles, and allocate the array */
    tiles_nb = bytestream2_get_le16(gbc);

    if (tiles_nb == 0) {
        av_log(avctx, AV_LOG_DEBUG, "no tiles\n");
        return avpkt->size;
    }

    av_fast_malloc(&ctx->tiles, &ctx->tiles_size,
                   tiles_nb * sizeof(*ctx->tiles));
    if (!ctx->tiles) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    av_log(avctx, AV_LOG_DEBUG, "Frame with %d tiles.\n", tiles_nb);

    /* When there are more than 5 tiles, they are packed together with
     * a size header. When that size does not match the number of tiles
     * times the tile size, it means it needs to be inflated as well */
    if (tiles_nb > 5) {
        uLongf packed_tiles_size;

        if (tiles_nb < 32)
            packed_tiles_size = bytestream2_get_byte(gbc);
        else
            packed_tiles_size = bytestream2_get_le16(gbc);

        ff_dlog(avctx, "packed tiles of size %lu.\n", packed_tiles_size);

        /* If necessary, uncompress tiles, and hijack the bytestream reader */
        if (packed_tiles_size != tiles_nb * TILE_SIZE) {
            uLongf length = tiles_nb * TILE_SIZE;

            if (bytestream2_get_bytes_left(gbc) < packed_tiles_size) {
                ret = AVERROR_INVALIDDATA;
                goto end;
            }

            inflated_tiles = av_malloc(length);
            if (!inflated_tiles) {
                ret = AVERROR(ENOMEM);
                goto end;
            }

            ret = uncompress(inflated_tiles, &length,
                             gbc->buffer, packed_tiles_size);
            if (ret) {
                av_log(avctx, AV_LOG_ERROR, "Tile deflate error %d.\n", ret);
                ret = AVERROR_UNKNOWN;
                goto end;
            }

            /* Skip the compressed tile section in the main byte reader,
             * and point it to read the newly uncompressed data */
            bytestream2_skip(gbc, packed_tiles_size);
            bytestream2_init(&tiles_gbc, inflated_tiles, length);
            gbc = &tiles_gbc;
        }
    }

    /* Fill in array of tiles, keeping track of how many pixels are updated */
    for (i = 0; i < tiles_nb; i++) {
        ctx->tiles[i].x = bytestream2_get_le16(gbc);
        ctx->tiles[i].w = bytestream2_get_le16(gbc);
        ctx->tiles[i].y = bytestream2_get_le16(gbc);
        ctx->tiles[i].h = bytestream2_get_le16(gbc);

        if (pixel_size + ctx->tiles[i].w * (int64_t)ctx->tiles[i].h * ctx->component_size > INT_MAX) {
            av_log(avctx, AV_LOG_ERROR, "Invalid tile dimensions\n");
            ret = AVERROR_INVALIDDATA;
            goto end;
        }

        pixel_size += ctx->tiles[i].w * ctx->tiles[i].h * ctx->component_size;

        ff_dlog(avctx, "tile %d orig(%d,%d) %dx%d.\n", i,
                ctx->tiles[i].x, ctx->tiles[i].y,
                ctx->tiles[i].w, ctx->tiles[i].h);

        if (ctx->tiles[i].w == 0 || ctx->tiles[i].h == 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "invalid tile %d at (%d.%d) with size %dx%d.\n", i,
                   ctx->tiles[i].x, ctx->tiles[i].y,
                   ctx->tiles[i].w, ctx->tiles[i].h);
            ret = AVERROR_INVALIDDATA;
            goto end;
        } else if (ctx->tiles[i].x + ctx->tiles[i].w > avctx->width ||
                   ctx->tiles[i].y + ctx->tiles[i].h > avctx->height) {
            av_log(avctx, AV_LOG_ERROR,
                   "out of bounds tile %d at (%d.%d) with size %dx%d.\n", i,
                   ctx->tiles[i].x, ctx->tiles[i].y,
                   ctx->tiles[i].w, ctx->tiles[i].h);
            ret = AVERROR_INVALIDDATA;
            goto end;
        }
    }

    /* Reset the reader in case it had been modified before */
    gbc = &ctx->gbc;

    /* Extract how much pixel data the tiles contain */
    if (pixel_size < 0x100)
        packed_size = bytestream2_get_byte(gbc);
    else if (pixel_size < 0x10000)
        packed_size = bytestream2_get_le16(gbc);
    else if (pixel_size < 0x1000000)
        packed_size = bytestream2_get_le24(gbc);
    else
        packed_size = bytestream2_get_le32(gbc);

    ff_dlog(avctx, "pixel_size %d packed_size %d.\n", pixel_size, packed_size);

    if (packed_size < 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid tile size %d\n", packed_size);
        ret = AVERROR_INVALIDDATA;
        goto end;
    }

    /* Get pixels buffer, it may be deflated or just raw */
    if (pixel_size == packed_size) {
        if (bytestream2_get_bytes_left(gbc) < pixel_size) {
            av_log(avctx, AV_LOG_ERROR, "Insufficient input for %d\n", pixel_size);
            ret = AVERROR_INVALIDDATA;
            goto end;
        }
        pixels = gbc->buffer;
    } else {
        uLongf len = ctx->inflated_size;
        if (bytestream2_get_bytes_left(gbc) < packed_size) {
            av_log(avctx, AV_LOG_ERROR, "Insufficient input for %d\n", packed_size);
            ret = AVERROR_INVALIDDATA;
            goto end;
        }
        if (ctx->inflated_size < pixel_size) {
            ret = AVERROR_INVALIDDATA;
            goto end;
        }
        ret = uncompress(ctx->inflated_buf, &len, gbc->buffer, packed_size);
        if (ret) {
            av_log(avctx, AV_LOG_ERROR, "Pixel deflate error %d.\n", ret);
            ret = AVERROR_UNKNOWN;
            goto end;
        }
        pixels = ctx->inflated_buf;
    }

    /* Allocate when needed */
    ret = ff_reget_buffer(avctx, ctx->reference, 0);
    if (ret < 0)
        goto end;

    /* Pointer to actual pixels, will be updated when data is consumed */
    raw = pixels;
    for (i = 0; i < tiles_nb; i++) {
        uint8_t *dst = ctx->reference->data[0] + ctx->reference->linesize[0] *
                       (avctx->height - ctx->tiles[i].y - 1) +
                       ctx->tiles[i].x * ctx->component_size;
        av_image_copy_plane(dst, -1 * ctx->reference->linesize[0],
                            raw, ctx->tiles[i].w * ctx->component_size,
                            ctx->tiles[i].w * ctx->component_size,
                            ctx->tiles[i].h);
        raw += ctx->tiles[i].w * ctx->component_size * ctx->tiles[i].h;
    }

    /* Frame is ready to be output */
    ret = av_frame_ref(frame, ctx->reference);
    if (ret < 0)
        goto end;

    /* Keyframe when the number of pixels updated matches the whole surface */
    if (pixel_size == ctx->inflated_size) {
        frame->pict_type = AV_PICTURE_TYPE_I;
        frame->flags |= AV_FRAME_FLAG_KEY;
    } else {
        frame->pict_type = AV_PICTURE_TYPE_P;
    }

    /* Palette handling */
    if (avctx->pix_fmt == AV_PIX_FMT_PAL8) {
        ff_copy_palette(ctx->palette, avpkt, avctx);
        memcpy(frame->data[1], ctx->palette, AVPALETTE_SIZE);
    }
    // We only return a picture when enough of it is undamaged, this avoids copying nearly broken frames around
    if (ctx->valid_pixels < ctx->inflated_size)
        ctx->valid_pixels += pixel_size;
    if (ctx->valid_pixels >= ctx->inflated_size * (100 - avctx->discard_damaged_percentage) / 100)
        *got_frame = 1;

    ret = avpkt->size;
end:
    av_free(inflated_tiles);
    return ret;
}

const FFCodec ff_rscc_decoder = {
    .p.name         = "rscc",
    CODEC_LONG_NAME("innoHeim/Rsupport Screen Capture Codec"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_RSCC,
    .init           = rscc_init,
    FF_CODEC_DECODE_CB(rscc_decode_frame),
    .close          = rscc_close,
    .priv_data_size = sizeof(RsccContext),
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
