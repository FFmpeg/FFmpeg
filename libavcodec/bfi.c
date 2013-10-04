/*
 * Brute Force & Ignorance (BFI) video decoder
 * Copyright (c) 2008 Sisir Koppaka
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
 * @brief Brute Force & Ignorance (.bfi) video decoder
 * @author Sisir Koppaka ( sisir.koppaka at gmail dot com )
 * @see http://wiki.multimedia.cx/index.php?title=BFI
 */

#include "libavutil/common.h"
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"

typedef struct BFIContext {
    AVCodecContext *avctx;
    uint8_t *dst;
    uint32_t pal[256];
} BFIContext;

static av_cold int bfi_decode_init(AVCodecContext *avctx)
{
    BFIContext *bfi = avctx->priv_data;
    avctx->pix_fmt  = AV_PIX_FMT_PAL8;
    bfi->dst        = av_mallocz(avctx->width * avctx->height);
    if (!bfi->dst)
        return AVERROR(ENOMEM);
    return 0;
}

static int bfi_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_frame, AVPacket *avpkt)
{
    AVFrame *frame = data;
    GetByteContext g;
    int buf_size    = avpkt->size;
    BFIContext *bfi = avctx->priv_data;
    uint8_t *dst    = bfi->dst;
    uint8_t *src, *dst_offset, colour1, colour2;
    uint8_t *frame_end = bfi->dst + avctx->width * avctx->height;
    uint32_t *pal;
    int i, j, ret, height = avctx->height;

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    bytestream2_init(&g, avpkt->data, buf_size);

    /* Set frame parameters and palette, if necessary */
    if (!avctx->frame_number) {
        frame->pict_type = AV_PICTURE_TYPE_I;
        frame->key_frame = 1;
        /* Setting the palette */
        if (avctx->extradata_size > 768) {
            av_log(NULL, AV_LOG_ERROR, "Palette is too large.\n");
            return AVERROR_INVALIDDATA;
        }
        pal = (uint32_t *)frame->data[1];
        for (i = 0; i < avctx->extradata_size / 3; i++) {
            int shift = 16;
            *pal = 0xFFU << 24;
            for (j = 0; j < 3; j++, shift -= 8)
                *pal += ((avctx->extradata[i * 3 + j] << 2) |
                         (avctx->extradata[i * 3 + j] >> 4)) << shift;
            pal++;
        }
        memcpy(bfi->pal, frame->data[1], sizeof(bfi->pal));
        frame->palette_has_changed = 1;
    } else {
        frame->pict_type = AV_PICTURE_TYPE_P;
        frame->key_frame = 0;
        frame->palette_has_changed = 0;
        memcpy(frame->data[1], bfi->pal, sizeof(bfi->pal));
    }

    bytestream2_skip(&g, 4); // Unpacked size, not required.

    while (dst != frame_end) {
        static const uint8_t lentab[4] = { 0, 2, 0, 1 };
        unsigned int byte   = bytestream2_get_byte(&g), av_uninit(offset);
        unsigned int code   = byte >> 6;
        unsigned int length = byte & ~0xC0;

        if (!bytestream2_get_bytes_left(&g)) {
            av_log(avctx, AV_LOG_ERROR,
                   "Input resolution larger than actual frame.\n");
            return AVERROR_INVALIDDATA;
        }

        /* Get length and offset (if required) */
        if (length == 0) {
            if (code == 1) {
                length = bytestream2_get_byte(&g);
                offset = bytestream2_get_le16(&g);
            } else {
                length = bytestream2_get_le16(&g);
                if (code == 2 && length == 0)
                    break;
            }
        } else {
            if (code == 1)
                offset = bytestream2_get_byte(&g);
        }

        /* Do boundary check */
        if (dst + (length << lentab[code]) > frame_end)
            break;

        switch (code) {
        case 0:                // normal chain
            if (length >= bytestream2_get_bytes_left(&g)) {
                av_log(avctx, AV_LOG_ERROR, "Frame larger than buffer.\n");
                return AVERROR_INVALIDDATA;
            }
            bytestream2_get_buffer(&g, dst, length);
            dst += length;
            break;
        case 1:                // back chain
            dst_offset = dst - offset;
            length    *= 4;     // Convert dwords to bytes.
            if (dst_offset < bfi->dst)
                break;
            while (length--)
                *dst++ = *dst_offset++;
            break;
        case 2:                // skip chain
            dst += length;
            break;
        case 3:                // fill chain
            colour1 = bytestream2_get_byte(&g);
            colour2 = bytestream2_get_byte(&g);
            while (length--) {
                *dst++ = colour1;
                *dst++ = colour2;
            }
            break;
        }
    }

    src = bfi->dst;
    dst = frame->data[0];
    while (height--) {
        memcpy(dst, src, avctx->width);
        src += avctx->width;
        dst += frame->linesize[0];
    }
    *got_frame = 1;

    return buf_size;
}

static av_cold int bfi_decode_close(AVCodecContext *avctx)
{
    BFIContext *bfi = avctx->priv_data;
    av_free(bfi->dst);
    return 0;
}

AVCodec ff_bfi_decoder = {
    .name           = "bfi",
    .long_name      = NULL_IF_CONFIG_SMALL("Brute Force & Ignorance"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_BFI,
    .priv_data_size = sizeof(BFIContext),
    .init           = bfi_decode_init,
    .close          = bfi_decode_close,
    .decode         = bfi_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
