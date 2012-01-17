/*
 * PNM image format
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


static int pnm_encode_frame(AVCodecContext *avctx, unsigned char *outbuf,
                            int buf_size, void *data)
{
    PNMContext *s     = avctx->priv_data;
    AVFrame *pict     = data;
    AVFrame * const p = (AVFrame*)&s->picture;
    int i, h, h1, c, n, linesize;
    uint8_t *ptr, *ptr1, *ptr2;

    if (buf_size < avpicture_get_size(avctx->pix_fmt, avctx->width, avctx->height) + 200) {
        av_log(avctx, AV_LOG_ERROR, "encoded frame too large\n");
        return -1;
    }

    *p           = *pict;
    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;

    s->bytestream_start =
    s->bytestream       = outbuf;
    s->bytestream_end   = outbuf + buf_size;

    h  = avctx->height;
    h1 = h;
    switch (avctx->pix_fmt) {
    case PIX_FMT_MONOWHITE:
        c  = '4';
        n  = (avctx->width + 7) >> 3;
        break;
    case PIX_FMT_GRAY8:
        c  = '5';
        n  = avctx->width;
        break;
    case PIX_FMT_GRAY16BE:
        c  = '5';
        n  = avctx->width * 2;
        break;
    case PIX_FMT_RGB24:
        c  = '6';
        n  = avctx->width * 3;
        break;
    case PIX_FMT_RGB48BE:
        c  = '6';
        n  = avctx->width * 6;
        break;
    case PIX_FMT_YUV420P:
        c  = '5';
        n  = avctx->width;
        h1 = (h * 3) / 2;
        break;
    default:
        return -1;
    }
    snprintf(s->bytestream, s->bytestream_end - s->bytestream,
             "P%c\n%d %d\n", c, avctx->width, h1);
    s->bytestream += strlen(s->bytestream);
    if (avctx->pix_fmt != PIX_FMT_MONOWHITE) {
        snprintf(s->bytestream, s->bytestream_end - s->bytestream,
                 "%d\n", (avctx->pix_fmt != PIX_FMT_GRAY16BE && avctx->pix_fmt != PIX_FMT_RGB48BE) ? 255 : 65535);
        s->bytestream += strlen(s->bytestream);
    }

    ptr      = p->data[0];
    linesize = p->linesize[0];
    for (i = 0; i < h; i++) {
        memcpy(s->bytestream, ptr, n);
        s->bytestream += n;
        ptr           += linesize;
    }

    if (avctx->pix_fmt == PIX_FMT_YUV420P) {
        h >>= 1;
        n >>= 1;
        ptr1 = p->data[1];
        ptr2 = p->data[2];
        for (i = 0; i < h; i++) {
            memcpy(s->bytestream, ptr1, n);
            s->bytestream += n;
            memcpy(s->bytestream, ptr2, n);
            s->bytestream += n;
                ptr1 += p->linesize[1];
                ptr2 += p->linesize[2];
        }
    }
    return s->bytestream - s->bytestream_start;
}


#if CONFIG_PGM_ENCODER
AVCodec ff_pgm_encoder = {
    .name           = "pgm",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_PGM,
    .priv_data_size = sizeof(PNMContext),
    .init           = ff_pnm_init,
    .encode         = pnm_encode_frame,
    .pix_fmts  = (const enum PixelFormat[]){PIX_FMT_GRAY8, PIX_FMT_GRAY16BE, PIX_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("PGM (Portable GrayMap) image"),
};
#endif

#if CONFIG_PGMYUV_ENCODER
AVCodec ff_pgmyuv_encoder = {
    .name           = "pgmyuv",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_PGMYUV,
    .priv_data_size = sizeof(PNMContext),
    .init           = ff_pnm_init,
    .encode         = pnm_encode_frame,
    .pix_fmts  = (const enum PixelFormat[]){PIX_FMT_YUV420P, PIX_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("PGMYUV (Portable GrayMap YUV) image"),
};
#endif

#if CONFIG_PPM_ENCODER
AVCodec ff_ppm_encoder = {
    .name           = "ppm",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_PPM,
    .priv_data_size = sizeof(PNMContext),
    .init           = ff_pnm_init,
    .encode         = pnm_encode_frame,
    .pix_fmts  = (const enum PixelFormat[]){PIX_FMT_RGB24, PIX_FMT_RGB48BE, PIX_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("PPM (Portable PixelMap) image"),
};
#endif

#if CONFIG_PBM_ENCODER
AVCodec ff_pbm_encoder = {
    .name           = "pbm",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_PBM,
    .priv_data_size = sizeof(PNMContext),
    .init           = ff_pnm_init,
    .encode         = pnm_encode_frame,
    .pix_fmts  = (const enum PixelFormat[]){PIX_FMT_MONOWHITE, PIX_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("PBM (Portable BitMap) image"),
};
#endif
