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

#include "libavutil/imgutils.h"

#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"

static int pam_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                            const AVFrame *pict, int *got_packet)
{
    uint8_t *bytestream_start, *bytestream, *bytestream_end;
    const AVFrame * const p = pict;
    int i, h, w, n, linesize, depth, maxval, ret;
    const char *tuple_type;
    uint8_t *ptr;
    int size = av_image_get_buffer_size(avctx->pix_fmt,
                                        avctx->width, avctx->height, 1);

    if ((ret = ff_alloc_packet(pkt, size + 200)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "encoded frame too large\n");
        return ret;
    }

    bytestream_start =
    bytestream       = pkt->data;
    bytestream_end   = pkt->data + pkt->size;

    h = avctx->height;
    w = avctx->width;
    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_MONOWHITE:
        n          = (w + 7) >> 3;
        depth      = 1;
        maxval     = 1;
        tuple_type = "BLACKANDWHITE";
        break;
    case AV_PIX_FMT_GRAY8:
        n          = w;
        depth      = 1;
        maxval     = 255;
        tuple_type = "GRAYSCALE";
        break;
    case AV_PIX_FMT_RGB24:
        n          = w * 3;
        depth      = 3;
        maxval     = 255;
        tuple_type = "RGB";
        break;
    case AV_PIX_FMT_RGB32:
        n          = w * 4;
        depth      = 4;
        maxval     = 255;
        tuple_type = "RGB_ALPHA";
        break;
    default:
        return -1;
    }
    snprintf(bytestream, bytestream_end - bytestream,
             "P7\nWIDTH %d\nHEIGHT %d\nDEPTH %d\nMAXVAL %d\nTUPLTYPE %s\nENDHDR\n",
             w, h, depth, maxval, tuple_type);
    bytestream += strlen(bytestream);

    ptr      = p->data[0];
    linesize = p->linesize[0];

    if (avctx->pix_fmt == AV_PIX_FMT_RGB32) {
        int j;
        unsigned int v;

        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                v = ((uint32_t *)ptr)[j];
                bytestream_put_be24(&bytestream, v);
                *bytestream++ = v >> 24;
            }
            ptr += linesize;
        }
    } else {
        for (i = 0; i < h; i++) {
            memcpy(bytestream, ptr, n);
            bytestream += n;
            ptr        += linesize;
        }
    }

    pkt->size   = bytestream - bytestream_start;
    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;
    return 0;
}

static av_cold int pam_encode_init(AVCodecContext *avctx)
{
#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;
    avctx->coded_frame->key_frame = 1;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    return 0;
}

AVCodec ff_pam_encoder = {
    .name           = "pam",
    .long_name      = NULL_IF_CONFIG_SMALL("PAM (Portable AnyMap) image"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_PAM,
    .init           = pam_encode_init,
    .encode2        = pam_encode_frame,
    .pix_fmts       = (const enum AVPixelFormat[]){
        AV_PIX_FMT_RGB24, AV_PIX_FMT_RGB32, AV_PIX_FMT_GRAY8, AV_PIX_FMT_MONOWHITE,
        AV_PIX_FMT_NONE
    },
};
