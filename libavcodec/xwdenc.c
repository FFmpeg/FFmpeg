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

#include "libavutil/pixdesc.h"
#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "encode.h"
#include "xwd.h"

#define WINDOW_NAME         "lavcxwdenc"
#define WINDOW_NAME_SIZE    11

static int xwd_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                            const AVFrame *pict, int *got_packet)
{
    enum AVPixelFormat pix_fmt = avctx->pix_fmt;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    uint32_t pixdepth, bpp, bpad, ncolors = 0, lsize, vclass, be = 0;
    uint32_t rgb[3] = { 0 }, bitorder = 0;
    uint32_t header_size;
    int i, out_size, ret;
    uint8_t *ptr, *buf;
    AVFrame * const p = (AVFrame *)pict;
    uint32_t pal[256];

    pixdepth = av_get_bits_per_pixel(desc);
    if (desc->flags & AV_PIX_FMT_FLAG_BE)
        be = 1;
    switch (pix_fmt) {
    case AV_PIX_FMT_ARGB:
    case AV_PIX_FMT_BGRA:
    case AV_PIX_FMT_RGBA:
    case AV_PIX_FMT_ABGR:
        if (pix_fmt == AV_PIX_FMT_ARGB ||
            pix_fmt == AV_PIX_FMT_ABGR)
            be = 1;
        if (pix_fmt == AV_PIX_FMT_ABGR ||
            pix_fmt == AV_PIX_FMT_RGBA) {
            rgb[0] = 0xFF;
            rgb[1] = 0xFF00;
            rgb[2] = 0xFF0000;
        } else {
            rgb[0] = 0xFF0000;
            rgb[1] = 0xFF00;
            rgb[2] = 0xFF;
        }
        bpp      = 32;
        pixdepth = 24;
        vclass   = XWD_TRUE_COLOR;
        bpad     = 32;
        break;
    case AV_PIX_FMT_BGR24:
    case AV_PIX_FMT_RGB24:
        if (pix_fmt == AV_PIX_FMT_RGB24)
            be = 1;
        bpp      = 24;
        vclass   = XWD_TRUE_COLOR;
        bpad     = 32;
        rgb[0]   = 0xFF0000;
        rgb[1]   = 0xFF00;
        rgb[2]   = 0xFF;
        break;
    case AV_PIX_FMT_RGB565LE:
    case AV_PIX_FMT_RGB565BE:
    case AV_PIX_FMT_BGR565LE:
    case AV_PIX_FMT_BGR565BE:
        if (pix_fmt == AV_PIX_FMT_BGR565LE ||
            pix_fmt == AV_PIX_FMT_BGR565BE) {
            rgb[0] = 0x1F;
            rgb[1] = 0x7E0;
            rgb[2] = 0xF800;
        } else {
            rgb[0] = 0xF800;
            rgb[1] = 0x7E0;
            rgb[2] = 0x1F;
        }
        bpp      = 16;
        vclass   = XWD_TRUE_COLOR;
        bpad     = 16;
        break;
    case AV_PIX_FMT_RGB555LE:
    case AV_PIX_FMT_RGB555BE:
    case AV_PIX_FMT_BGR555LE:
    case AV_PIX_FMT_BGR555BE:
        if (pix_fmt == AV_PIX_FMT_BGR555LE ||
            pix_fmt == AV_PIX_FMT_BGR555BE) {
            rgb[0] = 0x1F;
            rgb[1] = 0x3E0;
            rgb[2] = 0x7C00;
        } else {
            rgb[0] = 0x7C00;
            rgb[1] = 0x3E0;
            rgb[2] = 0x1F;
        }
        bpp      = 16;
        vclass   = XWD_TRUE_COLOR;
        bpad     = 16;
        break;
    case AV_PIX_FMT_RGB8:
    case AV_PIX_FMT_BGR8:
    case AV_PIX_FMT_RGB4_BYTE:
    case AV_PIX_FMT_BGR4_BYTE:
    case AV_PIX_FMT_PAL8:
        bpp      = 8;
        vclass   = XWD_PSEUDO_COLOR;
        bpad     = 8;
        ncolors  = 256;
        break;
    case AV_PIX_FMT_GRAY8:
        bpp      = 8;
        bpad     = 8;
        vclass   = XWD_STATIC_GRAY;
        break;
    case AV_PIX_FMT_MONOWHITE:
        be       = 1;
        bitorder = 1;
        bpp      = 1;
        bpad     = 8;
        vclass   = XWD_STATIC_GRAY;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "unsupported pixel format\n");
        return AVERROR(EINVAL);
    }

    lsize       = FFALIGN(bpp * avctx->width, bpad) / 8;
    header_size = XWD_HEADER_SIZE + WINDOW_NAME_SIZE;
    out_size    = header_size + ncolors * XWD_CMAP_SIZE + avctx->height * lsize;

    if ((ret = ff_get_encode_buffer(avctx, pkt, out_size, 0)) < 0)
        return ret;
    buf = pkt->data;

    p->key_frame = 1;
    p->pict_type = AV_PICTURE_TYPE_I;

    bytestream_put_be32(&buf, header_size);
    bytestream_put_be32(&buf, XWD_VERSION);   // file version
    bytestream_put_be32(&buf, XWD_Z_PIXMAP);  // pixmap format
    bytestream_put_be32(&buf, pixdepth);      // pixmap depth in pixels
    bytestream_put_be32(&buf, avctx->width);  // pixmap width in pixels
    bytestream_put_be32(&buf, avctx->height); // pixmap height in pixels
    bytestream_put_be32(&buf, 0);             // bitmap x offset
    bytestream_put_be32(&buf, be);            // byte order
    bytestream_put_be32(&buf, 32);            // bitmap unit
    bytestream_put_be32(&buf, bitorder);      // bit-order of image data
    bytestream_put_be32(&buf, bpad);          // bitmap scan-line pad in bits
    bytestream_put_be32(&buf, bpp);           // bits per pixel
    bytestream_put_be32(&buf, lsize);         // bytes per scan-line
    bytestream_put_be32(&buf, vclass);        // visual class
    bytestream_put_be32(&buf, rgb[0]);        // red mask
    bytestream_put_be32(&buf, rgb[1]);        // green mask
    bytestream_put_be32(&buf, rgb[2]);        // blue mask
    bytestream_put_be32(&buf, 8);             // size of each bitmask in bits
    bytestream_put_be32(&buf, ncolors);       // number of colors
    bytestream_put_be32(&buf, ncolors);       // number of entries in color map
    bytestream_put_be32(&buf, avctx->width);  // window width
    bytestream_put_be32(&buf, avctx->height); // window height
    bytestream_put_be32(&buf, 0);             // window upper left X coordinate
    bytestream_put_be32(&buf, 0);             // window upper left Y coordinate
    bytestream_put_be32(&buf, 0);             // window border width
    bytestream_put_buffer(&buf, WINDOW_NAME, WINDOW_NAME_SIZE);

    if (pix_fmt == AV_PIX_FMT_PAL8) {
        memcpy(pal, p->data[1], sizeof(pal));
    } else {
        avpriv_set_systematic_pal2(pal, pix_fmt);
    }

    for (i = 0; i < ncolors; i++) {
        uint32_t val;
        uint8_t red, green, blue;

        val   = pal[i];
        red   = (val >> 16) & 0xFF;
        green = (val >>  8) & 0xFF;
        blue  =  val        & 0xFF;

        bytestream_put_be32(&buf, i);         // colormap entry number
        bytestream_put_be16(&buf, red   << 8);
        bytestream_put_be16(&buf, green << 8);
        bytestream_put_be16(&buf, blue  << 8);
        bytestream_put_byte(&buf, 0x7);       // bitmask flag
        bytestream_put_byte(&buf, 0);         // padding
    }

    ptr = p->data[0];
    for (i = 0; i < avctx->height; i++) {
        bytestream_put_buffer(&buf, ptr, lsize);
        ptr += p->linesize[0];
    }

    *got_packet = 1;
    return 0;
}

const FFCodec ff_xwd_encoder = {
    .p.name         = "xwd",
    .p.long_name    = NULL_IF_CONFIG_SMALL("XWD (X Window Dump) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_XWD,
    .p.capabilities = AV_CODEC_CAP_DR1,
    FF_CODEC_ENCODE_CB(xwd_encode_frame),
    .p.pix_fmts     = (const enum AVPixelFormat[]) { AV_PIX_FMT_BGRA,
                                                 AV_PIX_FMT_RGBA,
                                                 AV_PIX_FMT_ARGB,
                                                 AV_PIX_FMT_ABGR,
                                                 AV_PIX_FMT_RGB24,
                                                 AV_PIX_FMT_BGR24,
                                                 AV_PIX_FMT_RGB565BE,
                                                 AV_PIX_FMT_RGB565LE,
                                                 AV_PIX_FMT_BGR565BE,
                                                 AV_PIX_FMT_BGR565LE,
                                                 AV_PIX_FMT_RGB555BE,
                                                 AV_PIX_FMT_RGB555LE,
                                                 AV_PIX_FMT_BGR555BE,
                                                 AV_PIX_FMT_BGR555LE,
                                                 AV_PIX_FMT_RGB8,
                                                 AV_PIX_FMT_BGR8,
                                                 AV_PIX_FMT_RGB4_BYTE,
                                                 AV_PIX_FMT_BGR4_BYTE,
                                                 AV_PIX_FMT_PAL8,
                                                 AV_PIX_FMT_GRAY8,
                                                 AV_PIX_FMT_MONOWHITE,
                                                 AV_PIX_FMT_NONE },
};
