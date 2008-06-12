/*
 * BMP image format encoder
 * Copyright (c) 2006, 2007 Michel Bardiaux
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
#include "bmp.h"

static av_cold int bmp_encode_init(AVCodecContext *avctx){
    BMPContext *s = avctx->priv_data;

    avcodec_get_frame_defaults((AVFrame*)&s->picture);
    avctx->coded_frame = (AVFrame*)&s->picture;

    return 0;
}

static int bmp_encode_frame(AVCodecContext *avctx, unsigned char *buf, int buf_size, void *data){
    BMPContext *s = avctx->priv_data;
    AVFrame *pict = data;
    AVFrame * const p= (AVFrame*)&s->picture;
    int n_bytes_image, n_bytes_per_row, n_bytes, i, n, hsize;
    uint8_t *ptr;
    unsigned char* buf0 = buf;
    *p = *pict;
    p->pict_type= FF_I_TYPE;
    p->key_frame= 1;
    n_bytes_per_row = (avctx->width*3 + 3) & ~3;
    n_bytes_image = avctx->height*n_bytes_per_row;

    // STRUCTURE.field refer to the MSVC documentation for BITMAPFILEHEADER
    // and related pages.
#define SIZE_BITMAPFILEHEADER 14
#define SIZE_BITMAPINFOHEADER 40
    hsize = SIZE_BITMAPFILEHEADER + SIZE_BITMAPINFOHEADER;
    n_bytes = n_bytes_image + hsize;
    if(n_bytes>buf_size) {
        av_log(avctx, AV_LOG_ERROR, "buf size too small (need %d, got %d)\n", n_bytes, buf_size);
        return -1;
    }
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
    bytestream_put_le16(&buf, 24);                    // BITMAPINFOHEADER.biBitCount
    bytestream_put_le32(&buf, BMP_RGB);               // BITMAPINFOHEADER.biCompression
    bytestream_put_le32(&buf, n_bytes_image);         // BITMAPINFOHEADER.biSizeImage
    bytestream_put_le32(&buf, 0);                     // BITMAPINFOHEADER.biXPelsPerMeter
    bytestream_put_le32(&buf, 0);                     // BITMAPINFOHEADER.biYPelsPerMeter
    bytestream_put_le32(&buf, 0);                     // BITMAPINFOHEADER.biClrUsed
    bytestream_put_le32(&buf, 0);                     // BITMAPINFOHEADER.biClrImportant
    // BMP files are bottom-to-top so we start from the end...
    ptr = p->data[0] + (avctx->height - 1) * p->linesize[0];
    buf = buf0 + hsize;
    for(i = 0; i < avctx->height; i++) {
        n = 3*avctx->width;
        memcpy(buf, ptr, n);
        buf += n;
        memset(buf, 0, n_bytes_per_row-n);
        buf += n_bytes_per_row-n;
        ptr -= p->linesize[0]; // ... and go back
    }
    return n_bytes;
}

AVCodec bmp_encoder = {
    "bmp",
    CODEC_TYPE_VIDEO,
    CODEC_ID_BMP,
    sizeof(BMPContext),
    bmp_encode_init,
    bmp_encode_frame,
    NULL, //encode_end,
    .pix_fmts= (enum PixelFormat[]){PIX_FMT_BGR24, PIX_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("BMP image"),
};
