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
#include "put_bits.h"
#include "pnm.h"


static int pnm_decode_frame(AVCodecContext *avctx, void *data,
                            int *data_size, AVPacket *avpkt)
{
    const uint8_t *buf   = avpkt->data;
    int buf_size         = avpkt->size;
    PNMContext * const s = avctx->priv_data;
    AVFrame *picture     = data;
    AVFrame * const p    = &s->picture;
    int i, j, n, linesize, h, upgrade = 0, is_mono = 0;
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
    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;

    switch (avctx->pix_fmt) {
    default:
        return -1;
    case PIX_FMT_RGBA64BE:
        n = avctx->width * 8;
        components=4;
        sample_len=16;
        goto do_read;
    case PIX_FMT_RGB48BE:
        n = avctx->width * 6;
        components=3;
        sample_len=16;
        goto do_read;
    case PIX_FMT_RGBA:
        n = avctx->width * 4;
        components=4;
        sample_len=8;
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
    case PIX_FMT_GRAY8A:
        n = avctx->width * 2;
        components=2;
        sample_len=8;
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
        is_mono = 1;
    do_read:
        ptr      = p->data[0];
        linesize = p->linesize[0];
        if (s->bytestream + n * avctx->height > s->bytestream_end)
            return -1;
        if(s->type < 4 || (is_mono && s->type==7)){
            for (i=0; i<avctx->height; i++) {
                PutBitContext pb;
                init_put_bits(&pb, ptr, linesize);
                for(j=0; j<avctx->width * components; j++){
                    unsigned int c=0;
                    int v=0;
                    if(s->type < 4)
                    while(s->bytestream < s->bytestream_end && (*s->bytestream < '0' || *s->bytestream > '9' ))
                        s->bytestream++;
                    if(s->bytestream >= s->bytestream_end)
                        return -1;
                    if (is_mono) {
                        /* read a single digit */
                        v = (*s->bytestream++)&1;
                    } else {
                        /* read a sequence of digits */
                        do {
                            v = 10*v + c;
                            c = (*s->bytestream++) - '0';
                        } while (c <= 9);
                    }
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
                    v = av_be2ne16(((uint16_t *)s->bytestream)[j]);
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
    }
    *picture   = s->picture;
    *data_size = sizeof(AVPicture);

    return s->bytestream - s->bytestream_start;
}


#if CONFIG_PGM_DECODER
AVCodec ff_pgm_decoder = {
    .name           = "pgm",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_PGM,
    .priv_data_size = sizeof(PNMContext),
    .init           = ff_pnm_init,
    .close          = ff_pnm_end,
    .decode         = pnm_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("PGM (Portable GrayMap) image"),
};
#endif

#if CONFIG_PGMYUV_DECODER
AVCodec ff_pgmyuv_decoder = {
    .name           = "pgmyuv",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_PGMYUV,
    .priv_data_size = sizeof(PNMContext),
    .init           = ff_pnm_init,
    .close          = ff_pnm_end,
    .decode         = pnm_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("PGMYUV (Portable GrayMap YUV) image"),
};
#endif

#if CONFIG_PPM_DECODER
AVCodec ff_ppm_decoder = {
    .name           = "ppm",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_PPM,
    .priv_data_size = sizeof(PNMContext),
    .init           = ff_pnm_init,
    .close          = ff_pnm_end,
    .decode         = pnm_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("PPM (Portable PixelMap) image"),
};
#endif

#if CONFIG_PBM_DECODER
AVCodec ff_pbm_decoder = {
    .name           = "pbm",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_PBM,
    .priv_data_size = sizeof(PNMContext),
    .init           = ff_pnm_init,
    .close          = ff_pnm_end,
    .decode         = pnm_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("PBM (Portable BitMap) image"),
};
#endif

#if CONFIG_PAM_DECODER
AVCodec ff_pam_decoder = {
    .name           = "pam",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_PAM,
    .priv_data_size = sizeof(PNMContext),
    .init           = ff_pnm_init,
    .close          = ff_pnm_end,
    .decode         = pnm_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("PAM (Portable AnyMap) image"),
};
#endif
