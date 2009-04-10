/*
 * Sun Rasterfile (.sun/.ras/im{1,8,24}/.sunras) image decoder
 * Copyright (c) 2007, 2008 Ivo van Poorten
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
#include "avcodec.h"

#define RT_OLD          0
#define RT_STANDARD     1
#define RT_BYTE_ENCODED 2
#define RT_FORMAT_RGB   3
#define RT_FORMAT_TIFF  4
#define RT_FORMAT_IFF   5

typedef struct SUNRASTContext {
    AVFrame picture;
} SUNRASTContext;

static av_cold int sunrast_init(AVCodecContext *avctx) {
    SUNRASTContext *s = avctx->priv_data;

    avcodec_get_frame_defaults(&s->picture);
    avctx->coded_frame= &s->picture;

    return 0;
}

static int sunrast_decode_frame(AVCodecContext *avctx, void *data,
                                int *data_size, AVPacket *avpkt) {
    const uint8_t *buf = avpkt->data;
    SUNRASTContext * const s = avctx->priv_data;
    AVFrame *picture = data;
    AVFrame * const p = &s->picture;
    unsigned int w, h, depth, type, maptype, maplength, stride, x, y, len, alen;
    uint8_t *ptr;
    const uint8_t *bufstart = buf;

    if (AV_RB32(buf) != 0x59a66a95) {
        av_log(avctx, AV_LOG_ERROR, "this is not sunras encoded data\n");
        return -1;
    }

    w         = AV_RB32(buf+4);
    h         = AV_RB32(buf+8);
    depth     = AV_RB32(buf+12);
    type      = AV_RB32(buf+20);
    maptype   = AV_RB32(buf+24);
    maplength = AV_RB32(buf+28);

    if (type > RT_BYTE_ENCODED && type <= RT_FORMAT_IFF) {
        av_log(avctx, AV_LOG_ERROR, "unsupported (compression) type\n");
        return -1;
    }
    if (type > RT_FORMAT_IFF) {
        av_log(avctx, AV_LOG_ERROR, "invalid (compression) type\n");
        return -1;
    }
    if (maptype & ~1) {
        av_log(avctx, AV_LOG_ERROR, "invalid colormap type\n");
        return -1;
    }

    buf += 32;

    switch (depth) {
        case 1:
            avctx->pix_fmt = PIX_FMT_MONOWHITE;
            break;
        case 8:
            avctx->pix_fmt = PIX_FMT_PAL8;
            break;
        case 24:
            avctx->pix_fmt = PIX_FMT_BGR24;
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "invalid depth\n");
            return -1;
    }

    if (p->data[0])
        avctx->release_buffer(avctx, p);

    if (avcodec_check_dimensions(avctx, w, h))
        return -1;
    if (w != avctx->width || h != avctx->height)
        avcodec_set_dimensions(avctx, w, h);
    if (avctx->get_buffer(avctx, p) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    p->pict_type = FF_I_TYPE;

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
        for (x=0; x<len; x++, ptr+=4)
            *(uint32_t *)ptr = (buf[x]<<16) + (buf[len+x]<<8) + buf[len+len+x];
    }

    buf += maplength;

    ptr    = p->data[0];
    stride = p->linesize[0];

    /* scanlines are aligned on 16 bit boundaries */
    len  = (depth * w + 7) >> 3;
    alen = len + (len&1);

    if (type == RT_BYTE_ENCODED) {
        int value, run;
        uint8_t *end = ptr + h*stride;

        x = 0;
        while (ptr != end) {
            run = 1;
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
        for (y=0; y<h; y++) {
            memcpy(ptr, buf, len);
            ptr += stride;
            buf += alen;
        }
    }

    *picture = s->picture;
    *data_size = sizeof(AVFrame);

    return buf - bufstart;
}

static av_cold int sunrast_end(AVCodecContext *avctx) {
    SUNRASTContext *s = avctx->priv_data;

    if(s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);

    return 0;
}

AVCodec sunrast_decoder = {
    "sunrast",
    CODEC_TYPE_VIDEO,
    CODEC_ID_SUNRAST,
    sizeof(SUNRASTContext),
    sunrast_init,
    NULL,
    sunrast_end,
    sunrast_decode_frame,
    0,
    NULL,
    .long_name = NULL_IF_CONFIG_SMALL("Sun Rasterfile image"),
};
