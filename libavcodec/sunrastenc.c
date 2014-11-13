/*
 * Sun Rasterfile (.sun/.ras/im{1,8,24}/.sunras) image encoder
 * Copyright (c) 2012 Aneesh Dogra (lionaneesh) <lionaneesh@gmail.com>
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

#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "sunrast.h"

typedef struct SUNRASTContext {
    PutByteContext p;
    int depth;      ///< depth of pixel
    int length;     ///< length (bytes) of image
    int type;       ///< type of file
    int maptype;    ///< type of colormap
    int maplength;  ///< length (bytes) of colormap
    int size;
} SUNRASTContext;

static void sunrast_image_write_header(AVCodecContext *avctx)
{
    SUNRASTContext *s = avctx->priv_data;

    bytestream2_put_be32u(&s->p, RAS_MAGIC);
    bytestream2_put_be32u(&s->p, avctx->width);
    bytestream2_put_be32u(&s->p, avctx->height);
    bytestream2_put_be32u(&s->p, s->depth);
    bytestream2_put_be32u(&s->p, s->length);
    bytestream2_put_be32u(&s->p, s->type);
    bytestream2_put_be32u(&s->p, s->maptype);
    bytestream2_put_be32u(&s->p, s->maplength);
}

static void sunrast_image_write_image(AVCodecContext *avctx,
                                      const uint8_t *pixels,
                                      const uint32_t *palette_data,
                                      int linesize)
{
    SUNRASTContext *s = avctx->priv_data;
    const uint8_t *ptr;
    int len, alen, x, y;

    if (s->maplength) {     // palettized
        PutByteContext pb_r, pb_g;
        int len = s->maplength / 3;

        pb_r = s->p;
        bytestream2_skip_p(&s->p, len);
        pb_g = s->p;
        bytestream2_skip_p(&s->p, len);

        for (x = 0; x < len; x++) {
            uint32_t pixel = palette_data[x];

            bytestream2_put_byteu(&pb_r, (pixel >> 16) & 0xFF);
            bytestream2_put_byteu(&pb_g, (pixel >> 8)  & 0xFF);
            bytestream2_put_byteu(&s->p,  pixel        & 0xFF);
        }
    }

    len  = (s->depth * avctx->width + 7) >> 3;
    alen = len + (len & 1);
    ptr  = pixels;

     if (s->type == RT_BYTE_ENCODED) {
        uint8_t value, value2;
        int run;

        ptr = pixels;

#define GET_VALUE y >= avctx->height ? 0 : x >= len ? ptr[len-1] : ptr[x]

        x = 0, y = 0;
        value2 = GET_VALUE;
        while (y < avctx->height) {
            run = 1;
            value = value2;
            x++;
            if (x >= alen) {
                x = 0;
                ptr += linesize, y++;
            }

            value2 = GET_VALUE;
            while (value2 == value && run < 256 && y < avctx->height) {
                x++;
                run++;
                if (x >= alen) {
                    x = 0;
                    ptr += linesize, y++;
                }
                value2 = GET_VALUE;
            }

            if (run > 2 || value == RLE_TRIGGER) {
                bytestream2_put_byteu(&s->p, RLE_TRIGGER);
                bytestream2_put_byteu(&s->p, run - 1);
                if (run > 1)
                    bytestream2_put_byteu(&s->p, value);
            } else if (run == 1) {
                bytestream2_put_byteu(&s->p, value);
            } else
                bytestream2_put_be16u(&s->p, (value << 8) | value);
        }

        // update data length for header
        s->length = bytestream2_tell_p(&s->p) - 32 - s->maplength;
    } else {
        for (y = 0; y < avctx->height; y++) {
            bytestream2_put_buffer(&s->p, ptr, len);
            if (len < alen)
                bytestream2_put_byteu(&s->p, 0);
            ptr += linesize;
        }
    }
}

static av_cold int sunrast_encode_init(AVCodecContext *avctx)
{
    SUNRASTContext *s = avctx->priv_data;

    switch (avctx->coder_type) {
    case FF_CODER_TYPE_RLE:
        s->type = RT_BYTE_ENCODED;
        break;
    case FF_CODER_TYPE_RAW:
        s->type = RT_STANDARD;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "invalid coder_type\n");
        return AVERROR(EINVAL);
    }

    s->maptype                    = RMT_NONE;
    s->maplength                  = 0;

    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_MONOWHITE:
        s->depth = 1;
        break;
    case AV_PIX_FMT_PAL8 :
        s->maptype   = RMT_EQUAL_RGB;
        s->maplength = 3 * 256;
        /* fall-through */
    case AV_PIX_FMT_GRAY8:
        s->depth = 8;
        break;
    case AV_PIX_FMT_BGR24:
        s->depth = 24;
        break;
    default:
        return AVERROR_BUG;
    }
    s->length = avctx->height * (FFALIGN(avctx->width * s->depth, 16) >> 3);
    s->size   = 32 + s->maplength +
                s->length * (s->type == RT_BYTE_ENCODED ? 2 : 1);

    return 0;
}

static int sunrast_encode_frame(AVCodecContext *avctx,  AVPacket *avpkt,
                                const AVFrame *frame, int *got_packet_ptr)
{
    SUNRASTContext *s = avctx->priv_data;
    int ret;

    if ((ret = ff_alloc_packet2(avctx, avpkt, s->size)) < 0)
        return ret;

    bytestream2_init_writer(&s->p, avpkt->data, avpkt->size);
    sunrast_image_write_header(avctx);
    sunrast_image_write_image(avctx, frame->data[0],
                              (const uint32_t *)frame->data[1],
                              frame->linesize[0]);
    // update data length in header after RLE
    if (s->type == RT_BYTE_ENCODED)
        AV_WB32(&avpkt->data[16], s->length);

    *got_packet_ptr = 1;
    avpkt->flags |= AV_PKT_FLAG_KEY;
    avpkt->size = bytestream2_tell_p(&s->p);
    return 0;
}

static av_cold int sunrast_encode_close(AVCodecContext *avctx)
{
    av_frame_free(&avctx->coded_frame);
    return 0;
}

static const AVCodecDefault sunrast_defaults[] = {
     { "coder", "rle" },
     { NULL },
};

AVCodec ff_sunrast_encoder = {
    .name           = "sunrast",
    .long_name      = NULL_IF_CONFIG_SMALL("Sun Rasterfile image"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_SUNRAST,
    .priv_data_size = sizeof(SUNRASTContext),
    .init           = sunrast_encode_init,
    .close          = sunrast_encode_close,
    .encode2        = sunrast_encode_frame,
    .defaults       = sunrast_defaults,
    .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_BGR24,
                                                  AV_PIX_FMT_PAL8,
                                                  AV_PIX_FMT_GRAY8,
                                                  AV_PIX_FMT_MONOWHITE,
                                                  AV_PIX_FMT_NONE },
};
