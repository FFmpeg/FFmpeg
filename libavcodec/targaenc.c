/*
 * Targa (.tga) image encoder
 * Copyright (c) 2007 Bobby Bingham
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
 *
 */
#include "avcodec.h"

static int targa_encode_frame(AVCodecContext *avctx,
                              unsigned char *outbuf,
                              int buf_size, void *data){
    AVFrame *p = data;
    int i, n, linesize;
    uint8_t *ptr, *out;

    if(avctx->width > 0xffff || avctx->height > 0xffff) {
        av_log(avctx, AV_LOG_ERROR, "image dimensions too large\n");
        return -1;
    }
    if(buf_size < avpicture_get_size(avctx->pix_fmt, avctx->width, avctx->height) + 45) {
        av_log(avctx, AV_LOG_ERROR, "encoded frame too large\n");
        return -1;
    }

    p->pict_type= FF_I_TYPE;
    p->key_frame= 1;

    /* zero out the header and only set applicable fields */
    memset(outbuf, 0, 11);
    AV_WL16(outbuf+12, avctx->width);
    AV_WL16(outbuf+14, avctx->height);
    outbuf[17] = 0x20;           /* origin is top-left. no alpha */

    /* TODO: support alpha channel and RLE */
    switch(avctx->pix_fmt) {
    case PIX_FMT_GRAY8:
        outbuf[2] = 3;           /* uncompressed grayscale image */
        outbuf[16] = 8;          /* bpp */
        n = avctx->width;
        break;
    case PIX_FMT_RGB555:
        outbuf[2] = 2;           /* uncompresses true-color image */
        outbuf[16] = 16;         /* bpp */
        n = 2 * avctx->width;
        break;
    case PIX_FMT_BGR24:
        outbuf[2] = 2;           /* uncompressed true-color image */
        outbuf[16] = 24;         /* bpp */
        n = 3 * avctx->width;
        break;
    default:
        return -1;
    }

    out = outbuf + 18;  /* skip past the header we just output */
    ptr = p->data[0];
    linesize = p->linesize[0];

    for(i=0; i < avctx->height; i++) {
        memcpy(out, ptr, n);
        out += n;
        ptr += linesize;
    }

    /* The standard recommends including this section, even if we don't use
     * any of the features it affords. TODO: take advantage of the pixel
     * aspect ratio and encoder ID fields available? */
    memcpy(out, "\0\0\0\0\0\0\0\0TRUEVISION-XFILE.", 26);

    return out + 26 - outbuf;
}

static int targa_encode_init(AVCodecContext *avctx)
{
    return 0;
}

AVCodec targa_encoder = {
    .name = "targa",
    .type = CODEC_TYPE_VIDEO,
    .id = CODEC_ID_TARGA,
    .priv_data_size = 0,
    .init = targa_encode_init,
    .encode = targa_encode_frame,
    .pix_fmts= (enum PixelFormat[]){PIX_FMT_BGR24, PIX_FMT_RGB555, PIX_FMT_GRAY8, -1},
};
