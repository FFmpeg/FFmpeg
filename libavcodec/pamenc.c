/*
 * PAM image format
 * Copyright (c) 2002, 2003 Fabrice Bellard
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

#include "libavutil/avassert.h"
#include "avcodec.h"
#include "encode.h"
#include "internal.h"

static int pam_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                            const AVFrame *p, int *got_packet)
{
    int i, h, w, n, linesize, depth, maxval, ret, header_size;
    uint8_t *bytestream, *ptr;
    const char *tuple_type;
    char header[100];

    h = avctx->height;
    w = avctx->width;
    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_MONOBLACK:
        n          = w;
        depth      = 1;
        maxval     = 1;
        tuple_type = "BLACKANDWHITE";
        break;
    case AV_PIX_FMT_GRAY8:
        n          = w;
        depth      = 1;
        maxval     = 255;
        tuple_type = "GRAYSCALE";
        break;
    case AV_PIX_FMT_GRAY16BE:
        n          = w * 2;
        depth      = 1;
        maxval     = 0xFFFF;
        tuple_type = "GRAYSCALE";
        break;
    case AV_PIX_FMT_GRAY8A:
        n          = w * 2;
        depth      = 2;
        maxval     = 255;
        tuple_type = "GRAYSCALE_ALPHA";
        break;
    case AV_PIX_FMT_YA16BE:
        n          = w * 4;
        depth      = 2;
        maxval     = 0xFFFF;
        tuple_type = "GRAYSCALE_ALPHA";
        break;
    case AV_PIX_FMT_RGB24:
        n          = w * 3;
        depth      = 3;
        maxval     = 255;
        tuple_type = "RGB";
        break;
    case AV_PIX_FMT_RGBA:
        n          = w * 4;
        depth      = 4;
        maxval     = 255;
        tuple_type = "RGB_ALPHA";
        break;
    case AV_PIX_FMT_RGB48BE:
        n          = w * 6;
        depth      = 3;
        maxval     = 0xFFFF;
        tuple_type = "RGB";
        break;
    case AV_PIX_FMT_RGBA64BE:
        n          = w * 8;
        depth      = 4;
        maxval     = 0xFFFF;
        tuple_type = "RGB_ALPHA";
        break;
    default:
        return -1;
    }

    header_size = snprintf(header, sizeof(header),
             "P7\nWIDTH %d\nHEIGHT %d\nDEPTH %d\nMAXVAL %d\nTUPLTYPE %s\nENDHDR\n",
             w, h, depth, maxval, tuple_type);
    av_assert1(header_size < sizeof(header));

    if ((ret = ff_get_encode_buffer(avctx, pkt, n*h + header_size, 0)) < 0)
        return ret;

    bytestream       = pkt->data;
    memcpy(bytestream, header, header_size);
    bytestream += header_size;

    ptr      = p->data[0];
    linesize = p->linesize[0];

    if (avctx->pix_fmt == AV_PIX_FMT_MONOBLACK){
        int j;
        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++)
                *bytestream++ = ptr[j >> 3] >> (7 - j & 7) & 1;
            ptr += linesize;
        }
    } else {
        for (i = 0; i < h; i++) {
            memcpy(bytestream, ptr, n);
            bytestream += n;
            ptr        += linesize;
        }
    }

    *got_packet = 1;
    return 0;
}

const AVCodec ff_pam_encoder = {
    .name           = "pam",
    .long_name      = NULL_IF_CONFIG_SMALL("PAM (Portable AnyMap) image"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_PAM,
    .capabilities   = AV_CODEC_CAP_DR1,
    .encode2        = pam_encode_frame,
    .pix_fmts       = (const enum AVPixelFormat[]){
        AV_PIX_FMT_RGB24, AV_PIX_FMT_RGBA,
        AV_PIX_FMT_RGB48BE, AV_PIX_FMT_RGBA64BE,
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY8A,
        AV_PIX_FMT_GRAY16BE, AV_PIX_FMT_YA16BE,
        AV_PIX_FMT_MONOBLACK, AV_PIX_FMT_NONE
    },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
