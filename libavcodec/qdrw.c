/*
 * QuickDraw (qdrw) codec
 * Copyright (c) 2004 Konstantin Shishkov
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
 * Apple QuickDraw codec.
 */

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *got_frame,
                        AVPacket *avpkt)
{
    AVFrame * const p      = data;
    GetByteContext gbc;
    uint8_t* outdata;
    int colors;
    int i, ret;
    uint32_t *pal;

    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;
    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;

    outdata = p->data[0];

    bytestream2_init(&gbc, avpkt->data, avpkt->size);

    if (bytestream2_get_bytes_left(&gbc) < 0x68 + 4) {
        av_log(avctx, AV_LOG_ERROR, "Frame is too small %d\n",
               bytestream2_get_bytes_left(&gbc));
        return AVERROR_INVALIDDATA;
    }

    /* jump to palette */
    bytestream2_skip(&gbc, 0x68);
    colors = bytestream2_get_be32(&gbc);

    if (colors < 0 || colors > 256) {
        av_log(avctx, AV_LOG_ERROR, "Error color count - %i(0x%X)\n", colors, colors);
        return AVERROR_INVALIDDATA;
    }
    if (bytestream2_get_bytes_left(&gbc) < (colors + 1) * 8) {
        av_log(avctx, AV_LOG_ERROR, "Palette is too small %d\n",
               bytestream2_get_bytes_left(&gbc));
        return AVERROR_INVALIDDATA;
    }

    pal = (uint32_t*)p->data[1];
    for (i = 0; i <= colors; i++) {
        uint8_t r, g, b;
        unsigned int idx = bytestream2_get_be16(&gbc); /* color index */
        if (idx > 255) {
            av_log(avctx, AV_LOG_ERROR, "Palette index out of range: %u\n", idx);
            bytestream2_skip(&gbc, 6);
            continue;
        }
        r = bytestream2_get_byte(&gbc);
        bytestream2_skip(&gbc, 1);
        g = bytestream2_get_byte(&gbc);
        bytestream2_skip(&gbc, 1);
        b = bytestream2_get_byte(&gbc);
        bytestream2_skip(&gbc, 1);
        pal[idx] = 0xFFU << 24 | r << 16 | g << 8 | b;
    }
    p->palette_has_changed = 1;

    /* skip unneeded data */
    bytestream2_skip(&gbc, 18);

    for (i = 0; i < avctx->height; i++) {
        int size, left, code, pix;
        uint8_t *out = outdata;

        /* size of packed line */
        size = left = bytestream2_get_be16(&gbc);
        if (bytestream2_get_bytes_left(&gbc) < size)
            return AVERROR_INVALIDDATA;

        /* decode line */
        while (left > 0) {
            code = bytestream2_get_byte(&gbc);
            if (code & 0x80 ) { /* run */
                pix = bytestream2_get_byte(&gbc);
                memset(out, pix, 257 - code);
                out   += 257 - code;
                left  -= 2;
            } else { /* copy */
                bytestream2_get_buffer(&gbc, out, code + 1);
                out   += code + 1;
                left  -= 2 + code;
            }
        }
        outdata += p->linesize[0];
    }

    *got_frame      = 1;

    return avpkt->size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    avctx->pix_fmt= AV_PIX_FMT_PAL8;

    return 0;
}

AVCodec ff_qdraw_decoder = {
    .name           = "qdraw",
    .long_name      = NULL_IF_CONFIG_SMALL("Apple QuickDraw"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_QDRAW,
    .init           = decode_init,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
