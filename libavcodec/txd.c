/*
 * Renderware TeXture Dictionary (.txd) image decoder
 * Copyright (c) 2007 Ivo van Poorten
 *
 * See also: http://wiki.multimedia.cx/index.php?title=TXD
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

#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "bytestream.h"
#include "s3tc.h"

typedef struct TXDContext {
    AVFrame picture;
} TXDContext;

static av_cold int txd_init(AVCodecContext *avctx) {
    TXDContext *s = avctx->priv_data;

    avcodec_get_frame_defaults(&s->picture);
    avctx->coded_frame = &s->picture;

    return 0;
}

static int txd_decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                            AVPacket *avpkt) {
    TXDContext * const s = avctx->priv_data;
    GetByteContext gb;
    AVFrame *picture = data;
    AVFrame * const p = &s->picture;
    unsigned int version, w, h, d3d_format, depth, stride, flags;
    unsigned int y, v;
    uint8_t *ptr;
    uint32_t *pal;

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
        return -1;
    }

    if (depth == 8) {
        avctx->pix_fmt = PIX_FMT_PAL8;
    } else if (depth == 16 || depth == 32) {
        avctx->pix_fmt = PIX_FMT_RGB32;
    } else {
        av_log(avctx, AV_LOG_ERROR, "depth of %i is unsupported\n", depth);
        return -1;
    }

    if (p->data[0])
        avctx->release_buffer(avctx, p);

    if (av_image_check_size(w, h, 0, avctx))
        return -1;
    if (w != avctx->width || h != avctx->height)
        avcodec_set_dimensions(avctx, w, h);
    if (avctx->get_buffer(avctx, p) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    p->pict_type = AV_PICTURE_TYPE_I;

    ptr    = p->data[0];
    stride = p->linesize[0];

    if (depth == 8) {
        pal = (uint32_t *) p->data[1];
        for (y = 0; y < 256; y++) {
            v = bytestream2_get_be32(&gb);
            pal[y] = (v >> 8) + (v << 24);
        }
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
            ff_decode_dxt1(&gb, ptr, w, h, stride);
            break;
        case FF_S3TC_DXT3:
            ff_decode_dxt3(&gb, ptr, w, h, stride);
            break;
        default:
            goto unsupported;
        }
    } else if (depth == 32) {
        switch (d3d_format) {
        case 0x15:
        case 0x16:
            for (y=0; y<h; y++) {
                bytestream2_get_buffer(&gb, ptr, w * 4);
                ptr += stride;
            }
            break;
        default:
            goto unsupported;
        }
    }

    *picture   = s->picture;
    *data_size = sizeof(AVPicture);

    return avpkt->size;

unsupported:
    av_log(avctx, AV_LOG_ERROR, "unsupported d3d format (%08x)\n", d3d_format);
    return -1;
}

static av_cold int txd_end(AVCodecContext *avctx) {
    TXDContext *s = avctx->priv_data;

    if (s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);

    return 0;
}

AVCodec ff_txd_decoder = {
    .name           = "txd",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_TXD,
    .priv_data_size = sizeof(TXDContext),
    .init           = txd_init,
    .close          = txd_end,
    .decode         = txd_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("Renderware TXD (TeXture Dictionary) image"),
};
