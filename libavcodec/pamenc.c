/*
 * PAM image format
 * Copyright (c) 2002, 2003 Fabrice Bellard
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "pnm.h"


static int pam_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                            const AVFrame *pict, int *got_packet)
{
    PNMContext *s     = avctx->priv_data;
    AVFrame * const p = &s->picture;
    int i, h, w, n, linesize, depth, maxval, ret;
    const char *tuple_type;
    uint8_t *ptr;

    if ((ret = ff_alloc_packet(pkt, avpicture_get_size(avctx->pix_fmt,
                                                       avctx->width,
                                                       avctx->height) + 200)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "encoded frame too large\n");
        return ret;
    }

    *p           = *pict;
    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;

    s->bytestream_start =
    s->bytestream       = pkt->data;
    s->bytestream_end   = pkt->data + pkt->size;

    h = avctx->height;
    w = avctx->width;
    switch (avctx->pix_fmt) {
    case PIX_FMT_MONOWHITE:
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
    case PIX_FMT_RGB24:
        n          = w * 3;
        depth      = 3;
        maxval     = 255;
        tuple_type = "RGB";
        break;
    case PIX_FMT_RGB32:
        n          = w * 4;
        depth      = 4;
        maxval     = 255;
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

    if (avctx->pix_fmt == PIX_FMT_RGB32) {
        int j;
        unsigned int v;

        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                v = ((uint32_t *)ptr)[j];
                bytestream_put_be24(&s->bytestream, v);
                *s->bytestream++ = v >> 24;
            }
            ptr += linesize;
        }
    } else {
        for (i = 0; i < h; i++) {
            memcpy(s->bytestream, ptr, n);
            s->bytestream += n;
            ptr           += linesize;
        }
    }

    pkt->size   = s->bytestream - s->bytestream_start;
    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;
    return 0;
}


AVCodec ff_pam_encoder = {
    .name           = "pam",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_PAM,
    .priv_data_size = sizeof(PNMContext),
    .init           = ff_pnm_init,
    .encode2        = pam_encode_frame,
    .pix_fmts       = (const enum PixelFormat[]){
        PIX_FMT_RGB24, PIX_FMT_RGB32, PIX_FMT_GRAY8, PIX_FMT_MONOWHITE,
        PIX_FMT_NONE
    },
    .long_name      = NULL_IF_CONFIG_SMALL("PAM (Portable AnyMap) image"),
};
