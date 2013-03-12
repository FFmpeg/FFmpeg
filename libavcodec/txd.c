/*
 * Renderware TeXture Dictionary (.txd) image decoder
 * Copyright (c) 2007 Ivo van Poorten
 *
 * See also: http://wiki.multimedia.cx/index.php?title=TXD
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
#include "bytestream.h"
#include "avcodec.h"
#include "internal.h"
#include "s3tc.h"

static int txd_decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                            AVPacket *avpkt) {
    GetByteContext gb;
    AVFrame * const p = data;
    unsigned int version, w, h, d3d_format, depth, stride, flags;
    unsigned int y, v;
    uint8_t *ptr;
    uint32_t *pal;
    int ret;

    bytestream2_init(&gb, avpkt->data, avpkt->size);
    version         = bytestream2_get_le32(&gb);
    bytestream2_skip(&gb, 72);
    d3d_format      = bytestream2_get_le32(&gb);
    w               = bytestream2_get_le16(&gb);
    h               = bytestream2_get_le16(&gb);
    depth           = bytestream2_get_byte(&gb);
    bytestream2_skip(&gb, 2);
    flags           = bytestream2_get_byte(&gb);

    if (version < 8 || version > 9) {
        av_log(avctx, AV_LOG_ERROR, "texture data version %i is unsupported\n",
                                                                    version);
        return AVERROR_PATCHWELCOME;
    }

    if (depth == 8) {
        avctx->pix_fmt = AV_PIX_FMT_PAL8;
    } else if (depth == 16 || depth == 32) {
        avctx->pix_fmt = AV_PIX_FMT_RGB32;
    } else {
        av_log(avctx, AV_LOG_ERROR, "depth of %i is unsupported\n", depth);
        return AVERROR_PATCHWELCOME;
    }

    if ((ret = av_image_check_size(w, h, 0, avctx)) < 0)
        return ret;
    if (w != avctx->width || h != avctx->height)
        avcodec_set_dimensions(avctx, w, h);
    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;

    p->pict_type = AV_PICTURE_TYPE_I;

    ptr    = p->data[0];
    stride = p->linesize[0];

    if (depth == 8) {
        pal = (uint32_t *) p->data[1];
        for (y = 0; y < 256; y++) {
            v = bytestream2_get_be32(&gb);
            pal[y] = (v >> 8) + (v << 24);
        }
        if (bytestream2_get_bytes_left(&gb) < w * h)
            return AVERROR_INVALIDDATA;
        bytestream2_skip(&gb, 4);
        for (y=0; y<h; y++) {
            bytestream2_get_buffer(&gb, ptr, w);
            ptr += stride;
        }
    } else if (depth == 16) {
        bytestream2_skip(&gb, 4);
        switch (d3d_format) {
        case 0:
            if (!(flags & 1))
                goto unsupported;
        case FF_S3TC_DXT1:
            if (bytestream2_get_bytes_left(&gb) < (w/4) * (h/4) * 8)
                return AVERROR_INVALIDDATA;
            ff_decode_dxt1(&gb, ptr, w, h, stride);
            break;
        case FF_S3TC_DXT3:
            if (bytestream2_get_bytes_left(&gb) < (w/4) * (h/4) * 16)
                return AVERROR_INVALIDDATA;
            ff_decode_dxt3(&gb, ptr, w, h, stride);
            break;
        default:
            goto unsupported;
        }
    } else if (depth == 32) {
        switch (d3d_format) {
        case 0x15:
        case 0x16:
            if (bytestream2_get_bytes_left(&gb) < h * w * 4)
                return AVERROR_INVALIDDATA;
            for (y=0; y<h; y++) {
                bytestream2_get_buffer(&gb, ptr, w * 4);
                ptr += stride;
            }
            break;
        default:
            goto unsupported;
        }
    }

    *got_frame = 1;

    return avpkt->size;

unsupported:
    av_log(avctx, AV_LOG_ERROR, "unsupported d3d format (%08x)\n", d3d_format);
    return AVERROR_PATCHWELCOME;
}

AVCodec ff_txd_decoder = {
    .name           = "txd",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_TXD,
    .decode         = txd_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("Renderware TXD (TeXture Dictionary) image"),
};
