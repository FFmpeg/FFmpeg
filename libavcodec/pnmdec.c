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
#include "bytestream.h"
#include "put_bits.h"
#include "pnm.h"


static int pnm_decode_frame(AVCodecContext *avctx, void *data,
                            int *data_size, AVPacket *avpkt)
{
    const uint8_t *buf   = avpkt->data;
    int buf_size         = avpkt->size;
    PNMContext * const s = avctx->priv_data;
    AVFrame *picture     = data;
    AVFrame * const p    = (AVFrame*)&s->picture;
    int i, j, n, linesize, h, upgrade = 0;
    unsigned char *ptr;
    int components, sample_len;

    s->bytestream_start =
    s->bytestream       = buf;
    s->bytestream_end   = buf + buf_size;

    if (ff_pnm_decode_header(avctx, s) < 0)
        return -1;

    if (p->data[0])
        avctx->release_buffer(avctx, p);

    p->reference = 0;
    if (avctx->get_buffer(avctx, p) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    p->pict_type = FF_I_TYPE;
    p->key_frame = 1;

    switch (avctx->pix_fmt) {
    default:
        return -1;
    case PIX_FMT_RGB48BE:
        n = avctx->width * 6;
        components=3;
        sample_len=16;
        goto do_read;
    case PIX_FMT_RGB24:
        n = avctx->width * 3;
        components=3;
        sample_len=8;
        goto do_read;
    case PIX_FMT_GRAY8:
        n = avctx->width;
        components=1;
        sample_len=8;
        if (s->maxval < 255)
            upgrade = 1;
        goto do_read;
    case PIX_FMT_GRAY16BE:
    case PIX_FMT_GRAY16LE:
        n = avctx->width * 2;
        components=1;
        sample_len=16;
        if (s->maxval < 65535)
            upgrade = 2;
        goto do_read;
    case PIX_FMT_MONOWHITE:
    case PIX_FMT_MONOBLACK:
        n = (avctx->width + 7) >> 3;
        components=1;
        sample_len=1;
    do_read:
        ptr      = p->data[0];
        linesize = p->linesize[0];
        if (s->bytestream + n * avctx->height > s->bytestream_end)
            return -1;
        if(s->type < 4){
            for (i=0; i<avctx->height; i++) {
                PutBitContext pb;
                init_put_bits(&pb, ptr, linesize);
                for(j=0; j<avctx->width * components; j++){
                    unsigned int c=0;
                    int v=0;
                    while(s->bytestream < s->bytestream_end && (*s->bytestream < '0' || *s->bytestream > '9' ))
                        s->bytestream++;
                    if(s->bytestream >= s->bytestream_end)
                        return -1;
                    do{
                        v= 10*v + c;
                        c= (*s->bytestream++) - '0';
                    }while(c <= 9);
                    put_bits(&pb, sample_len, (((1<<sample_len)-1)*v + (s->maxval>>1))/s->maxval);
                }
                flush_put_bits(&pb);
                ptr+= linesize;
            }
        }else{
        for (i = 0; i < avctx->height; i++) {
            if (!upgrade)
                memcpy(ptr, s->bytestream, n);
            else if (upgrade == 1) {
                unsigned int j, f = (255 * 128 + s->maxval / 2) / s->maxval;
                for (j = 0; j < n; j++)
                    ptr[j] = (s->bytestream[j] * f + 64) >> 7;
            } else if (upgrade == 2) {
                unsigned int j, v, f = (65535 * 32768 + s->maxval / 2) / s->maxval;
                for (j = 0; j < n / 2; j++) {
                    v = be2me_16(((uint16_t *)s->bytestream)[j]);
                    ((uint16_t *)ptr)[j] = (v * f + 16384) >> 15;
                }
            }
            s->bytestream += n;
            ptr           += linesize;
        }
        }
        break;
    case PIX_FMT_YUV420P:
        {
            unsigned char *ptr1, *ptr2;

            n        = avctx->width;
            ptr      = p->data[0];
            linesize = p->linesize[0];
            if (s->bytestream + n * avctx->height * 3 / 2 > s->bytestream_end)
                return -1;
            for (i = 0; i < avctx->height; i++) {
                memcpy(ptr, s->bytestream, n);
                s->bytestream += n;
                ptr           += linesize;
            }
            ptr1 = p->data[1];
            ptr2 = p->data[2];
            n >>= 1;
            h = avctx->height >> 1;
            for (i = 0; i < h; i++) {
                memcpy(ptr1, s->bytestream, n);
                s->bytestream += n;
                memcpy(ptr2, s->bytestream, n);
                s->bytestream += n;
                ptr1 += p->linesize[1];
                ptr2 += p->linesize[2];
            }
        }
        break;
    case PIX_FMT_RGB32:
        ptr      = p->data[0];
        linesize = p->linesize[0];
        if (s->bytestream + avctx->width * avctx->height * 4 > s->bytestream_end)
            return -1;
        for (i = 0; i < avctx->height; i++) {
            int j, r, g, b, a;

            for (j = 0; j < avctx->width; j++) {
                r = *s->bytestream++;
                g = *s->bytestream++;
                b = *s->bytestream++;
                a = *s->bytestream++;
                ((uint32_t *)ptr)[j] = (a << 24) | (r << 16) | (g << 8) | b;
            }
            ptr += linesize;
        }
        break;
    }
    *picture   = *(AVFrame*)&s->picture;
    *data_size = sizeof(AVPicture);

    return s->bytestream - s->bytestream_start;
}


#if CONFIG_PGM_DECODER
AVCodec pgm_decoder = {
    "pgm",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_PGM,
    sizeof(PNMContext),
    ff_pnm_init,
    NULL,
    ff_pnm_end,
    pnm_decode_frame,
    CODEC_CAP_DR1,
    .pix_fmts  = (const enum PixelFormat[]){PIX_FMT_GRAY8, PIX_FMT_GRAY16BE, PIX_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("PGM (Portable GrayMap) image"),
};
#endif

#if CONFIG_PGMYUV_DECODER
AVCodec pgmyuv_decoder = {
    "pgmyuv",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_PGMYUV,
    sizeof(PNMContext),
    ff_pnm_init,
    NULL,
    ff_pnm_end,
    pnm_decode_frame,
    CODEC_CAP_DR1,
    .pix_fmts  = (const enum PixelFormat[]){PIX_FMT_YUV420P, PIX_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("PGMYUV (Portable GrayMap YUV) image"),
};
#endif

#if CONFIG_PPM_DECODER
AVCodec ppm_decoder = {
    "ppm",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_PPM,
    sizeof(PNMContext),
    ff_pnm_init,
    NULL,
    ff_pnm_end,
    pnm_decode_frame,
    CODEC_CAP_DR1,
    .pix_fmts  = (const enum PixelFormat[]){PIX_FMT_RGB24, PIX_FMT_RGB48BE, PIX_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("PPM (Portable PixelMap) image"),
};
#endif

#if CONFIG_PBM_DECODER
AVCodec pbm_decoder = {
    "pbm",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_PBM,
    sizeof(PNMContext),
    ff_pnm_init,
    NULL,
    ff_pnm_end,
    pnm_decode_frame,
    CODEC_CAP_DR1,
    .pix_fmts  = (const enum PixelFormat[]){PIX_FMT_MONOWHITE, PIX_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("PBM (Portable BitMap) image"),
};
#endif

#if CONFIG_PAM_DECODER
AVCodec pam_decoder = {
    "pam",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_PAM,
    sizeof(PNMContext),
    ff_pnm_init,
    NULL,
    ff_pnm_end,
    pnm_decode_frame,
    CODEC_CAP_DR1,
    .pix_fmts  = (const enum PixelFormat[]){PIX_FMT_RGB24, PIX_FMT_RGB32, PIX_FMT_GRAY8, PIX_FMT_MONOWHITE, PIX_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("PAM (Portable AnyMap) image"),
};
#endif
