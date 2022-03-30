/*
 * BMP image format encoder
 * Copyright (c) 2006, 2007 Michel Bardiaux
 * Copyright (c) 2009 Daniel Verkamp <daniel at drv.nu>
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

#include "config.h"

#include "libavutil/imgutils.h"
#include "libavutil/avassert.h"
#include "avcodec.h"
#include "bytestream.h"
#include "bmp.h"
#include "codec_internal.h"
#include "encode.h"

static const uint32_t monoblack_pal[] = { 0x000000, 0xFFFFFF };
static const uint32_t rgb565_masks[]  = { 0xF800, 0x07E0, 0x001F };
static const uint32_t rgb444_masks[]  = { 0x0F00, 0x00F0, 0x000F };

static av_cold int bmp_encode_init(AVCodecContext *avctx){
    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_BGRA:
        avctx->bits_per_coded_sample = 32;
        break;
    case AV_PIX_FMT_BGR24:
        avctx->bits_per_coded_sample = 24;
        break;
    case AV_PIX_FMT_RGB555:
    case AV_PIX_FMT_RGB565:
    case AV_PIX_FMT_RGB444:
        avctx->bits_per_coded_sample = 16;
        break;
    case AV_PIX_FMT_RGB8:
    case AV_PIX_FMT_BGR8:
    case AV_PIX_FMT_RGB4_BYTE:
    case AV_PIX_FMT_BGR4_BYTE:
    case AV_PIX_FMT_GRAY8:
    case AV_PIX_FMT_PAL8:
        avctx->bits_per_coded_sample = 8;
        break;
    case AV_PIX_FMT_MONOBLACK:
        avctx->bits_per_coded_sample = 1;
        break;
    }

    return 0;
}

static int bmp_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                            const AVFrame *pict, int *got_packet)
{
    const AVFrame * const p = pict;
    int n_bytes_image, n_bytes_per_row, n_bytes, i, n, hsize, ret;
    const uint32_t *pal = NULL;
    uint32_t palette256[256];
    int pad_bytes_per_row, pal_entries = 0, compression = BMP_RGB;
    int bit_count = avctx->bits_per_coded_sample;
    uint8_t *ptr, *buf;

    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_RGB444:
        compression = BMP_BITFIELDS;
        pal = rgb444_masks; // abuse pal to hold color masks
        pal_entries = 3;
        break;
    case AV_PIX_FMT_RGB565:
        compression = BMP_BITFIELDS;
        pal = rgb565_masks; // abuse pal to hold color masks
        pal_entries = 3;
        break;
    case AV_PIX_FMT_RGB8:
    case AV_PIX_FMT_BGR8:
    case AV_PIX_FMT_RGB4_BYTE:
    case AV_PIX_FMT_BGR4_BYTE:
    case AV_PIX_FMT_GRAY8:
        av_assert1(bit_count == 8);
        avpriv_set_systematic_pal2(palette256, avctx->pix_fmt);
        pal = palette256;
        break;
    case AV_PIX_FMT_PAL8:
        pal = (uint32_t *)p->data[1];
        break;
    case AV_PIX_FMT_MONOBLACK:
        pal = monoblack_pal;
        break;
    }
    if (pal && !pal_entries) pal_entries = 1 << bit_count;
    n_bytes_per_row = ((int64_t)avctx->width * (int64_t)bit_count + 7LL) >> 3LL;
    pad_bytes_per_row = (4 - n_bytes_per_row) & 3;
    n_bytes_image = avctx->height * (n_bytes_per_row + pad_bytes_per_row);

    // STRUCTURE.field refer to the MSVC documentation for BITMAPFILEHEADER
    // and related pages.
#define SIZE_BITMAPFILEHEADER 14
#define SIZE_BITMAPINFOHEADER 40
    hsize = SIZE_BITMAPFILEHEADER + SIZE_BITMAPINFOHEADER + (pal_entries << 2);
    n_bytes = n_bytes_image + hsize;
    if ((ret = ff_get_encode_buffer(avctx, pkt, n_bytes, 0)) < 0)
        return ret;
    buf = pkt->data;
    bytestream_put_byte(&buf, 'B');                   // BITMAPFILEHEADER.bfType
    bytestream_put_byte(&buf, 'M');                   // do.
    bytestream_put_le32(&buf, n_bytes);               // BITMAPFILEHEADER.bfSize
    bytestream_put_le16(&buf, 0);                     // BITMAPFILEHEADER.bfReserved1
    bytestream_put_le16(&buf, 0);                     // BITMAPFILEHEADER.bfReserved2
    bytestream_put_le32(&buf, hsize);                 // BITMAPFILEHEADER.bfOffBits
    bytestream_put_le32(&buf, SIZE_BITMAPINFOHEADER); // BITMAPINFOHEADER.biSize
    bytestream_put_le32(&buf, avctx->width);          // BITMAPINFOHEADER.biWidth
    bytestream_put_le32(&buf, avctx->height);         // BITMAPINFOHEADER.biHeight
    bytestream_put_le16(&buf, 1);                     // BITMAPINFOHEADER.biPlanes
    bytestream_put_le16(&buf, bit_count);             // BITMAPINFOHEADER.biBitCount
    bytestream_put_le32(&buf, compression);           // BITMAPINFOHEADER.biCompression
    bytestream_put_le32(&buf, n_bytes_image);         // BITMAPINFOHEADER.biSizeImage
    bytestream_put_le32(&buf, 0);                     // BITMAPINFOHEADER.biXPelsPerMeter
    bytestream_put_le32(&buf, 0);                     // BITMAPINFOHEADER.biYPelsPerMeter
    bytestream_put_le32(&buf, 0);                     // BITMAPINFOHEADER.biClrUsed
    bytestream_put_le32(&buf, 0);                     // BITMAPINFOHEADER.biClrImportant
    for (i = 0; i < pal_entries; i++)
        bytestream_put_le32(&buf, pal[i] & 0xFFFFFF);
    // BMP files are bottom-to-top so we start from the end...
    ptr = p->data[0] + (avctx->height - 1) * p->linesize[0];
    buf = pkt->data + hsize;
    for(i = 0; i < avctx->height; i++) {
        if (HAVE_BIGENDIAN && bit_count == 16) {
            const uint16_t *src = (const uint16_t *) ptr;
            for(n = 0; n < avctx->width; n++)
                AV_WL16(buf + 2 * n, src[n]);
        } else {
            memcpy(buf, ptr, n_bytes_per_row);
        }
        buf += n_bytes_per_row;
        memset(buf, 0, pad_bytes_per_row);
        buf += pad_bytes_per_row;
        ptr -= p->linesize[0]; // ... and go back
    }

    *got_packet = 1;
    return 0;
}

const FFCodec ff_bmp_encoder = {
    .p.name         = "bmp",
    .p.long_name    = NULL_IF_CONFIG_SMALL("BMP (Windows and OS/2 bitmap)"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_BMP,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .init           = bmp_encode_init,
    FF_CODEC_ENCODE_CB(bmp_encode_frame),
    .p.pix_fmts     = (const enum AVPixelFormat[]){
        AV_PIX_FMT_BGRA, AV_PIX_FMT_BGR24,
        AV_PIX_FMT_RGB565, AV_PIX_FMT_RGB555, AV_PIX_FMT_RGB444,
        AV_PIX_FMT_RGB8, AV_PIX_FMT_BGR8, AV_PIX_FMT_RGB4_BYTE, AV_PIX_FMT_BGR4_BYTE, AV_PIX_FMT_GRAY8, AV_PIX_FMT_PAL8,
        AV_PIX_FMT_MONOBLACK,
        AV_PIX_FMT_NONE
    },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
