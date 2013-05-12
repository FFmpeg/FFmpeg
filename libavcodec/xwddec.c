/*
 * XWD image format
 *
 * Copyright (c) 2012 Paul B Mahol
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

#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "xwd.h"

static int xwd_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_frame, AVPacket *avpkt)
{
    AVFrame *p = data;
    const uint8_t *buf = avpkt->data;
    int i, ret, buf_size = avpkt->size;
    uint32_t version, header_size, vclass, ncolors;
    uint32_t xoffset, be, bpp, lsize, rsize;
    uint32_t pixformat, pixdepth, bunit, bitorder, bpad;
    uint32_t rgb[3];
    uint8_t *ptr;
    GetByteContext gb;

    if (buf_size < XWD_HEADER_SIZE)
        return AVERROR_INVALIDDATA;

    bytestream2_init(&gb, buf, buf_size);
    header_size = bytestream2_get_be32u(&gb);

    version = bytestream2_get_be32u(&gb);
    if (version != XWD_VERSION) {
        av_log(avctx, AV_LOG_ERROR, "unsupported version\n");
        return AVERROR_INVALIDDATA;
    }

    if (buf_size < header_size || header_size < XWD_HEADER_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "invalid header size\n");
        return AVERROR_INVALIDDATA;
    }

    pixformat     = bytestream2_get_be32u(&gb);
    pixdepth      = bytestream2_get_be32u(&gb);
    avctx->width  = bytestream2_get_be32u(&gb);
    avctx->height = bytestream2_get_be32u(&gb);
    xoffset       = bytestream2_get_be32u(&gb);
    be            = bytestream2_get_be32u(&gb);
    bunit         = bytestream2_get_be32u(&gb);
    bitorder      = bytestream2_get_be32u(&gb);
    bpad          = bytestream2_get_be32u(&gb);
    bpp           = bytestream2_get_be32u(&gb);
    lsize         = bytestream2_get_be32u(&gb);
    vclass        = bytestream2_get_be32u(&gb);
    rgb[0]        = bytestream2_get_be32u(&gb);
    rgb[1]        = bytestream2_get_be32u(&gb);
    rgb[2]        = bytestream2_get_be32u(&gb);
    bytestream2_skipu(&gb, 8);
    ncolors       = bytestream2_get_be32u(&gb);
    bytestream2_skipu(&gb, header_size - (XWD_HEADER_SIZE - 20));

    av_log(avctx, AV_LOG_DEBUG, "pixformat %d, pixdepth %d, bunit %d, bitorder %d, bpad %d\n",
           pixformat, pixdepth, bunit, bitorder, bpad);
    av_log(avctx, AV_LOG_DEBUG, "vclass %d, ncolors %d, bpp %d, be %d, lsize %d, xoffset %d\n",
           vclass, ncolors, bpp, be, lsize, xoffset);
    av_log(avctx, AV_LOG_DEBUG, "red %0x, green %0x, blue %0x\n", rgb[0], rgb[1], rgb[2]);

    if (pixformat > XWD_Z_PIXMAP) {
        av_log(avctx, AV_LOG_ERROR, "invalid pixmap format\n");
        return AVERROR_INVALIDDATA;
    }

    if (pixdepth == 0 || pixdepth > 32) {
        av_log(avctx, AV_LOG_ERROR, "invalid pixmap depth\n");
        return AVERROR_INVALIDDATA;
    }

    if (xoffset) {
        avpriv_request_sample(avctx, "xoffset %d", xoffset);
        return AVERROR_PATCHWELCOME;
    }

    if (be > 1) {
        av_log(avctx, AV_LOG_ERROR, "invalid byte order\n");
        return AVERROR_INVALIDDATA;
    }

    if (bitorder > 1) {
        av_log(avctx, AV_LOG_ERROR, "invalid bitmap bit order\n");
        return AVERROR_INVALIDDATA;
    }

    if (bunit != 8 && bunit != 16 && bunit != 32) {
        av_log(avctx, AV_LOG_ERROR, "invalid bitmap unit\n");
        return AVERROR_INVALIDDATA;
    }

    if (bpad != 8 && bpad != 16 && bpad != 32) {
        av_log(avctx, AV_LOG_ERROR, "invalid bitmap scan-line pad\n");
        return AVERROR_INVALIDDATA;
    }

    if (bpp == 0 || bpp > 32) {
        av_log(avctx, AV_LOG_ERROR, "invalid bits per pixel\n");
        return AVERROR_INVALIDDATA;
    }

    if (ncolors > 256) {
        av_log(avctx, AV_LOG_ERROR, "invalid number of entries in colormap\n");
        return AVERROR_INVALIDDATA;
    }

    if ((ret = av_image_check_size(avctx->width, avctx->height, 0, NULL)) < 0)
        return ret;

    rsize = FFALIGN(avctx->width * bpp, bpad) / 8;
    if (lsize < rsize) {
        av_log(avctx, AV_LOG_ERROR, "invalid bytes per scan-line\n");
        return AVERROR_INVALIDDATA;
    }

    if (bytestream2_get_bytes_left(&gb) < ncolors * XWD_CMAP_SIZE + avctx->height * lsize) {
        av_log(avctx, AV_LOG_ERROR, "input buffer too small\n");
        return AVERROR_INVALIDDATA;
    }

    if (pixformat != XWD_Z_PIXMAP) {
        av_log(avctx, AV_LOG_ERROR, "pixmap format %d unsupported\n", pixformat);
        return AVERROR_PATCHWELCOME;
    }

    avctx->pix_fmt = AV_PIX_FMT_NONE;
    switch (vclass) {
    case XWD_STATIC_GRAY:
    case XWD_GRAY_SCALE:
        if (bpp != 1 && bpp != 8)
            return AVERROR_INVALIDDATA;
        if (pixdepth == 1) {
            avctx->pix_fmt = AV_PIX_FMT_MONOWHITE;
        } else if (pixdepth == 8) {
            avctx->pix_fmt = AV_PIX_FMT_GRAY8;
        }
        break;
    case XWD_STATIC_COLOR:
    case XWD_PSEUDO_COLOR:
        if (bpp == 8)
            avctx->pix_fmt = AV_PIX_FMT_PAL8;
        break;
    case XWD_TRUE_COLOR:
    case XWD_DIRECT_COLOR:
        if (bpp != 16 && bpp != 24 && bpp != 32)
            return AVERROR_INVALIDDATA;
        if (bpp == 16 && pixdepth == 15) {
            if (rgb[0] == 0x7C00 && rgb[1] == 0x3E0 && rgb[2] == 0x1F)
                avctx->pix_fmt = be ? AV_PIX_FMT_RGB555BE : AV_PIX_FMT_RGB555LE;
            else if (rgb[0] == 0x1F && rgb[1] == 0x3E0 && rgb[2] == 0x7C00)
                avctx->pix_fmt = be ? AV_PIX_FMT_BGR555BE : AV_PIX_FMT_BGR555LE;
        } else if (bpp == 16 && pixdepth == 16) {
            if (rgb[0] == 0xF800 && rgb[1] == 0x7E0 && rgb[2] == 0x1F)
                avctx->pix_fmt = be ? AV_PIX_FMT_RGB565BE : AV_PIX_FMT_RGB565LE;
            else if (rgb[0] == 0x1F && rgb[1] == 0x7E0 && rgb[2] == 0xF800)
                avctx->pix_fmt = be ? AV_PIX_FMT_BGR565BE : AV_PIX_FMT_BGR565LE;
        } else if (bpp == 24) {
            if (rgb[0] == 0xFF0000 && rgb[1] == 0xFF00 && rgb[2] == 0xFF)
                avctx->pix_fmt = be ? AV_PIX_FMT_RGB24 : AV_PIX_FMT_BGR24;
            else if (rgb[0] == 0xFF && rgb[1] == 0xFF00 && rgb[2] == 0xFF0000)
                avctx->pix_fmt = be ? AV_PIX_FMT_BGR24 : AV_PIX_FMT_RGB24;
        } else if (bpp == 32) {
            if (rgb[0] == 0xFF0000 && rgb[1] == 0xFF00 && rgb[2] == 0xFF)
                avctx->pix_fmt = be ? AV_PIX_FMT_ARGB : AV_PIX_FMT_BGRA;
            else if (rgb[0] == 0xFF && rgb[1] == 0xFF00 && rgb[2] == 0xFF0000)
                avctx->pix_fmt = be ? AV_PIX_FMT_ABGR : AV_PIX_FMT_RGBA;
        }
        bytestream2_skipu(&gb, ncolors * XWD_CMAP_SIZE);
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "invalid visual class\n");
        return AVERROR_INVALIDDATA;
    }

    if (avctx->pix_fmt == AV_PIX_FMT_NONE) {
        avpriv_request_sample(avctx,
                              "Unknown file: bpp %d, pixdepth %d, vclass %d",
                              bpp, pixdepth, vclass);
        return AVERROR_PATCHWELCOME;
    }

    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;

    p->key_frame = 1;
    p->pict_type = AV_PICTURE_TYPE_I;

    if (avctx->pix_fmt == AV_PIX_FMT_PAL8) {
        uint32_t *dst = (uint32_t *)p->data[1];
        uint8_t red, green, blue;

        for (i = 0; i < ncolors; i++) {

            bytestream2_skipu(&gb, 4); // skip colormap entry number
            red    = bytestream2_get_byteu(&gb);
            bytestream2_skipu(&gb, 1);
            green  = bytestream2_get_byteu(&gb);
            bytestream2_skipu(&gb, 1);
            blue   = bytestream2_get_byteu(&gb);
            bytestream2_skipu(&gb, 3); // skip bitmask flag and padding

            dst[i] = red << 16 | green << 8 | blue;
        }
    }

    ptr = p->data[0];
    for (i = 0; i < avctx->height; i++) {
        bytestream2_get_bufferu(&gb, ptr, rsize);
        bytestream2_skipu(&gb, lsize - rsize);
        ptr += p->linesize[0];
    }

    *got_frame       = 1;

    return buf_size;
}

AVCodec ff_xwd_decoder = {
    .name           = "xwd",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_XWD,
    .decode         = xwd_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("XWD (X Window Dump) image"),
};
