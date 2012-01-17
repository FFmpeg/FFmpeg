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

#include "avcodec.h"
#include "pnm.h"


static int pam_encode_frame(AVCodecContext *avctx, unsigned char *outbuf,
                            int buf_size, void *data)
{
    PNMContext *s     = avctx->priv_data;
    AVFrame *pict     = data;
    AVFrame * const p = (AVFrame*)&s->picture;
    int i, h, w, n, linesize, depth, maxval;
    const char *tuple_type;
    uint8_t *ptr;

    if (buf_size < avpicture_get_size(avctx->pix_fmt, avctx->width, avctx->height) + 200) {
        av_log(avctx, AV_LOG_ERROR, "encoded frame too large\n");
        return -1;
    }

    *p           = *pict;
    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;

    s->bytestream_start =
    s->bytestream       = outbuf;
    s->bytestream_end   = outbuf+buf_size;

    h = avctx->height;
    w = avctx->width;
    switch (avctx->pix_fmt) {
    case PIX_FMT_MONOBLACK:
        n          = (w + 7) >> 3;
        depth      = 1;
        maxval     = 1;
        tuple_type = "BLACKANDWHITE";
        break;
    case PIX_FMT_GRAY8:
        n          = w;
        depth      = 1;
        maxval     = 255;
        tuple_type = "GRAYSCALE";
        break;
    case PIX_FMT_GRAY16BE:
        n          = w * 2;
        depth      = 1;
        maxval     = 0xFFFF;
        tuple_type = "GRAYSCALE";
        break;
    case PIX_FMT_GRAY8A:
        n          = w * 2;
        depth      = 2;
        maxval     = 255;
        tuple_type = "GRAYSCALE_ALPHA";
        break;
    case PIX_FMT_RGB24:
        n          = w * 3;
        depth      = 3;
        maxval     = 255;
        tuple_type = "RGB";
        break;
    case PIX_FMT_RGBA:
        n          = w * 4;
        depth      = 4;
        maxval     = 255;
        tuple_type = "RGB_ALPHA";
        break;
    case PIX_FMT_RGB48BE:
        n          = w * 6;
        depth      = 3;
        maxval     = 0xFFFF;
        tuple_type = "RGB";
        break;
    case PIX_FMT_RGBA64BE:
        n          = w * 8;
        depth      = 4;
        maxval     = 0xFFFF;
        tuple_type = "RGB_ALPHA";
        break;
    default:
        return -1;
    }
    snprintf(s->bytestream, s->bytestream_end - s->bytestream,
             "P7\nWIDTH %d\nHEIGHT %d\nDEPTH %d\nMAXVAL %d\nTUPLTYPE %s\nENDHDR\n",
             w, h, depth, maxval, tuple_type);
    s->bytestream += strlen(s->bytestream);

    ptr      = p->data[0];
    linesize = p->linesize[0];

    if (avctx->pix_fmt == PIX_FMT_MONOBLACK){
        int j;
        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++)
                *s->bytestream++ = ptr[j >> 3] >> (7 - j & 7) & 1;
            ptr += linesize;
        }
    } else {
        for (i = 0; i < h; i++) {
            memcpy(s->bytestream, ptr, n);
            s->bytestream += n;
            ptr           += linesize;
        }
    }
    return s->bytestream - s->bytestream_start;
}


AVCodec ff_pam_encoder = {
    .name           = "pam",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_PAM,
    .priv_data_size = sizeof(PNMContext),
    .init           = ff_pnm_init,
    .encode         = pam_encode_frame,
    .pix_fmts  = (const enum PixelFormat[]){PIX_FMT_RGB24, PIX_FMT_RGBA, PIX_FMT_RGB48BE, PIX_FMT_RGBA64BE, PIX_FMT_GRAY8, PIX_FMT_GRAY8A, PIX_FMT_GRAY16BE, PIX_FMT_MONOBLACK, PIX_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("PAM (Portable AnyMap) image"),
};
