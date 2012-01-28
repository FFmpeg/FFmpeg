/*
 * Sun Rasterfile (.sun/.ras/im{1,8,24}/.sunras) image decoder
 * Copyright (c) 2007, 2008 Ivo van Poorten
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

#define RAS_MAGIC 0x59a66a95

/* The Old and Standard format types indicate that the image data is
 * uncompressed. There is no difference between the two formats. */
#define RT_OLD          0
#define RT_STANDARD     1

/* The Byte-Encoded format type indicates that the image data is compressed
 * using a run-length encoding scheme. */
#define RT_BYTE_ENCODED 2

/* The RGB format type indicates that the image is uncompressed with reverse
 * component order from Old and Standard (RGB vs BGR). */
#define RT_FORMAT_RGB   3

/* The TIFF and IFF format types indicate that the raster file was originally
 * converted from either of these file formats. We do not have any samples or
 * documentation of the format details. */
#define RT_FORMAT_TIFF  4
#define RT_FORMAT_IFF   5

/* The Experimental format type is implementation-specific and is generally an
 * indication that the image file does not conform to the Sun Raster file
 * format specification. */
#define RT_EXPERIMENTAL 0xffff

typedef struct SUNRASTContext {
    AVFrame picture;
} SUNRASTContext;

static av_cold int sunrast_init(AVCodecContext *avctx) {
    SUNRASTContext *s = avctx->priv_data;

    avcodec_get_frame_defaults(&s->picture);
    avctx->coded_frame = &s->picture;

    return 0;
}

static int sunrast_decode_frame(AVCodecContext *avctx, void *data,
                                int *data_size, AVPacket *avpkt) {
    const uint8_t *buf       = avpkt->data;
    const uint8_t *buf_end   = avpkt->data + avpkt->size;
    SUNRASTContext * const s = avctx->priv_data;
    AVFrame *picture         = data;
    AVFrame * const p        = &s->picture;
    unsigned int w, h, depth, type, maptype, maplength, stride, x, y, len, alen;
    uint8_t *ptr;
    const uint8_t *bufstart = buf;

    if (avpkt->size < 32)
        return AVERROR_INVALIDDATA;

    if (AV_RB32(buf) != RAS_MAGIC) {
        av_log(avctx, AV_LOG_ERROR, "this is not sunras encoded data\n");
        return -1;
    }

    w         = AV_RB32(buf + 4);
    h         = AV_RB32(buf + 8);
    depth     = AV_RB32(buf + 12);
    type      = AV_RB32(buf + 20);
    maptype   = AV_RB32(buf + 24);
    maplength = AV_RB32(buf + 28);
    buf      += 32;

    if (type == RT_FORMAT_TIFF || type == RT_FORMAT_IFF || type == RT_EXPERIMENTAL) {
        av_log_ask_for_sample(avctx, "unsupported (compression) type\n");
        return AVERROR_PATCHWELCOME;
    }
    if (type > RT_FORMAT_IFF) {
        av_log(avctx, AV_LOG_ERROR, "invalid (compression) type\n");
        return -1;
    }
    if (av_image_check_size(w, h, 0, avctx)) {
        av_log(avctx, AV_LOG_ERROR, "invalid image size\n");
        return -1;
    }
    if (maptype & ~1) {
        av_log(avctx, AV_LOG_ERROR, "invalid colormap type\n");
        return -1;
    }


    switch (depth) {
        case 1:
            avctx->pix_fmt = PIX_FMT_MONOWHITE;
            break;
        case 8:
            avctx->pix_fmt = PIX_FMT_PAL8;
            break;
        case 24:
            avctx->pix_fmt = (type == RT_FORMAT_RGB) ? PIX_FMT_RGB24 : PIX_FMT_BGR24;
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "invalid depth\n");
            return -1;
    }

    if (p->data[0])
        avctx->release_buffer(avctx, p);

    if (w != avctx->width || h != avctx->height)
        avcodec_set_dimensions(avctx, w, h);
    if (avctx->get_buffer(avctx, p) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    p->pict_type = AV_PICTURE_TYPE_I;

    if (buf_end - buf < maplength)
        return AVERROR_INVALIDDATA;

    if (depth != 8 && maplength) {
        av_log(avctx, AV_LOG_WARNING, "useless colormap found or file is corrupted, trying to recover\n");

    } else if (depth == 8) {
        unsigned int len = maplength / 3;

        if (!maplength) {
            av_log(avctx, AV_LOG_ERROR, "colormap expected\n");
            return -1;
        }
        if (maplength % 3 || maplength > 768) {
            av_log(avctx, AV_LOG_WARNING, "invalid colormap length\n");
            return -1;
        }

        ptr = p->data[1];
        for (x = 0; x < len; x++, ptr += 4)
            *(uint32_t *)ptr = (buf[x] << 16) + (buf[len + x] << 8) + buf[len + len + x];
    }

    buf += maplength;

    ptr    = p->data[0];
    stride = p->linesize[0];

    /* scanlines are aligned on 16 bit boundaries */
    len  = (depth * w + 7) >> 3;
    alen = len + (len & 1);

    if (type == RT_BYTE_ENCODED) {
        int value, run;
        uint8_t *end = ptr + h * stride;

        x = 0;
        while (ptr != end && buf < buf_end) {
            run = 1;
            if (buf_end - buf < 1)
                return AVERROR_INVALIDDATA;

            if ((value = *buf++) == 0x80) {
                run = *buf++ + 1;
                if (run != 1)
                    value = *buf++;
            }
            while (run--) {
                if (x < len)
                    ptr[x] = value;
                if (++x >= alen) {
                    x = 0;
                    ptr += stride;
                    if (ptr == end)
                        break;
                }
            }
        }
    } else {
        for (y = 0; y < h; y++) {
            if (buf_end - buf < len)
                break;
            memcpy(ptr, buf, len);
            ptr += stride;
            buf += alen;
        }
    }

    *picture   = s->picture;
    *data_size = sizeof(AVFrame);

    return buf - bufstart;
}

static av_cold int sunrast_end(AVCodecContext *avctx) {
    SUNRASTContext *s = avctx->priv_data;

    if(s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);

    return 0;
}

AVCodec ff_sunrast_decoder = {
    .name           = "sunrast",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_SUNRAST,
    .priv_data_size = sizeof(SUNRASTContext),
    .init           = sunrast_init,
    .close          = sunrast_end,
    .decode         = sunrast_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("Sun Rasterfile image"),
};
