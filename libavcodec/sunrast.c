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

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "internal.h"
#include "sunrast.h"

static int sunrast_decode_frame(AVCodecContext *avctx, void *data,
                                int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf       = avpkt->data;
    const uint8_t *buf_end   = avpkt->data + avpkt->size;
    AVFrame * const p        = data;
    unsigned int w, h, depth, type, maptype, maplength, stride, x, y, len, alen;
    uint8_t *ptr, *ptr2 = NULL;
    const uint8_t *bufstart = buf;
    int ret;

    if (avpkt->size < 32)
        return AVERROR_INVALIDDATA;

    if (AV_RB32(buf) != RAS_MAGIC) {
        av_log(avctx, AV_LOG_ERROR, "this is not sunras encoded data\n");
        return AVERROR_INVALIDDATA;
    }

    w         = AV_RB32(buf + 4);
    h         = AV_RB32(buf + 8);
    depth     = AV_RB32(buf + 12);
    type      = AV_RB32(buf + 20);
    maptype   = AV_RB32(buf + 24);
    maplength = AV_RB32(buf + 28);
    buf      += 32;

    if (type == RT_EXPERIMENTAL) {
        avpriv_request_sample(avctx, "TIFF/IFF/EXPERIMENTAL (compression) type");
        return AVERROR_PATCHWELCOME;
    }
    if (type > RT_FORMAT_IFF) {
        av_log(avctx, AV_LOG_ERROR, "invalid (compression) type\n");
        return AVERROR_INVALIDDATA;
    }
    if (av_image_check_size(w, h, 0, avctx)) {
        av_log(avctx, AV_LOG_ERROR, "invalid image size\n");
        return AVERROR_INVALIDDATA;
    }
    if (maptype == RMT_RAW) {
        avpriv_request_sample(avctx, "Unknown colormap type");
        return AVERROR_PATCHWELCOME;
    }
    if (maptype > RMT_RAW) {
        av_log(avctx, AV_LOG_ERROR, "invalid colormap type\n");
        return AVERROR_INVALIDDATA;
    }

    if (type == RT_FORMAT_TIFF || type == RT_FORMAT_IFF) {
        av_log(avctx, AV_LOG_ERROR, "unsupported (compression) type\n");
        return -1;
    }

    switch (depth) {
        case 1:
            avctx->pix_fmt = maplength ? AV_PIX_FMT_PAL8 : AV_PIX_FMT_MONOWHITE;
            break;
        case 4:
            avctx->pix_fmt = maplength ? AV_PIX_FMT_PAL8 : AV_PIX_FMT_NONE;
            break;
        case 8:
            avctx->pix_fmt = maplength ? AV_PIX_FMT_PAL8 : AV_PIX_FMT_GRAY8;
            break;
        case 24:
            avctx->pix_fmt = (type == RT_FORMAT_RGB) ? AV_PIX_FMT_RGB24 : AV_PIX_FMT_BGR24;
            break;
        case 32:
            avctx->pix_fmt = (type == RT_FORMAT_RGB) ? AV_PIX_FMT_0RGB : AV_PIX_FMT_0BGR;
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "invalid depth\n");
            return AVERROR_INVALIDDATA;
    }

    if (w != avctx->width || h != avctx->height)
        avcodec_set_dimensions(avctx, w, h);
    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;

    p->pict_type = AV_PICTURE_TYPE_I;

    if (buf_end - buf < maplength)
        return AVERROR_INVALIDDATA;

    if (depth > 8 && maplength) {
        av_log(avctx, AV_LOG_WARNING, "useless colormap found or file is corrupted, trying to recover\n");

    } else if (maplength) {
        unsigned int len = maplength / 3;

        if (maplength % 3 || maplength > 768) {
            av_log(avctx, AV_LOG_WARNING, "invalid colormap length\n");
            return AVERROR_INVALIDDATA;
        }

        ptr = p->data[1];
        for (x = 0; x < len; x++, ptr += 4)
            *(uint32_t *)ptr = (0xFFU<<24) + (buf[x]<<16) + (buf[len+x]<<8) + buf[len+len+x];
    }

    buf += maplength;

    if (maplength && depth < 8) {
        ptr = ptr2 = av_malloc((w + 15) * h);
        if (!ptr)
            return AVERROR(ENOMEM);
        stride = (w + 15 >> 3) * depth;
    } else {
    ptr    = p->data[0];
    stride = p->linesize[0];
    }

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

            if ((value = *buf++) == RLE_TRIGGER) {
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
    if (avctx->pix_fmt == AV_PIX_FMT_PAL8 && depth < 8) {
        uint8_t *ptr_free = ptr2;
        ptr = p->data[0];
        for (y=0; y<h; y++) {
            for (x = 0; x < (w + 7 >> 3) * depth; x++) {
                if (depth == 1) {
                    ptr[8*x]   = ptr2[x] >> 7;
                    ptr[8*x+1] = ptr2[x] >> 6 & 1;
                    ptr[8*x+2] = ptr2[x] >> 5 & 1;
                    ptr[8*x+3] = ptr2[x] >> 4 & 1;
                    ptr[8*x+4] = ptr2[x] >> 3 & 1;
                    ptr[8*x+5] = ptr2[x] >> 2 & 1;
                    ptr[8*x+6] = ptr2[x] >> 1 & 1;
                    ptr[8*x+7] = ptr2[x]      & 1;
                } else {
                    ptr[2*x]   = ptr2[x] >> 4;
                    ptr[2*x+1] = ptr2[x] & 0xF;
                }
            }
            ptr  += p->linesize[0];
            ptr2 += (w + 15 >> 3) * depth;
        }
        av_freep(&ptr_free);
    }

    *got_frame = 1;

    return buf - bufstart;
}

AVCodec ff_sunrast_decoder = {
    .name           = "sunrast",
    .long_name      = NULL_IF_CONFIG_SMALL("Sun Rasterfile image"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_SUNRAST,
    .decode         = sunrast_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
