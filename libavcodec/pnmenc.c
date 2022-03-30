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

#include "config_components.h"

#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"

static int pnm_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                            const AVFrame *p, int *got_packet)
{
    uint8_t *bytestream, *bytestream_start, *bytestream_end;
    int i, h, h1, c, n, linesize, ret;
    uint8_t *ptr, *ptr1, *ptr2;
    int size = av_image_get_buffer_size(avctx->pix_fmt,
                                        avctx->width, avctx->height, 1);

    if ((ret = ff_get_encode_buffer(avctx, pkt, size + 200, 0)) < 0)
        return ret;

    bytestream_start =
    bytestream       = pkt->data;
    bytestream_end   = pkt->data + pkt->size;

    h  = avctx->height;
    h1 = h;
    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_MONOWHITE:
        c  = '4';
        n  = (avctx->width + 7) >> 3;
        break;
    case AV_PIX_FMT_GRAY8:
        c  = '5';
        n  = avctx->width;
        break;
    case AV_PIX_FMT_GRAY16BE:
        c  = '5';
        n  = avctx->width * 2;
        break;
    case AV_PIX_FMT_RGB24:
        c  = '6';
        n  = avctx->width * 3;
        break;
    case AV_PIX_FMT_RGB48BE:
        c  = '6';
        n  = avctx->width * 6;
        break;
    case AV_PIX_FMT_YUV420P:
        if (avctx->width & 1 || avctx->height & 1) {
            av_log(avctx, AV_LOG_ERROR, "pgmyuv needs even width and height\n");
            return AVERROR(EINVAL);
        }
        c  = '5';
        n  = avctx->width;
        h1 = (h * 3) / 2;
        break;
    case AV_PIX_FMT_YUV420P16BE:
        c  = '5';
        n  = avctx->width * 2;
        h1 = (h * 3) / 2;
        break;
    case AV_PIX_FMT_GBRPF32:
        c  = 'F';
        n  = avctx->width * 4;
        break;
    default:
        return -1;
    }
    snprintf(bytestream, bytestream_end - bytestream,
             "P%c\n%d %d\n", c, avctx->width, h1);
    bytestream += strlen(bytestream);
    if (avctx->pix_fmt == AV_PIX_FMT_GBRPF32)
        snprintf(bytestream, bytestream_end - bytestream,
                 "%f\n", avctx->pix_fmt == AV_PIX_FMT_GBRPF32BE ? 1.f: -1.f);
    bytestream += strlen(bytestream);
    if (avctx->pix_fmt != AV_PIX_FMT_MONOWHITE &&
        avctx->pix_fmt != AV_PIX_FMT_GBRPF32) {
        int maxdepth = (1 << av_pix_fmt_desc_get(avctx->pix_fmt)->comp[0].depth) - 1;
        snprintf(bytestream, bytestream_end - bytestream,
                 "%d\n", maxdepth);
        bytestream += strlen(bytestream);
    }

    if (avctx->pix_fmt == AV_PIX_FMT_GBRPF32) {
        float *r = (float *)p->data[2];
        float *g = (float *)p->data[0];
        float *b = (float *)p->data[1];

        for (int i = 0; i < avctx->height; i++) {
            for (int j = 0; j < avctx->width; j++) {
                AV_WN32(bytestream + 0, av_float2int(r[j]));
                AV_WN32(bytestream + 4, av_float2int(g[j]));
                AV_WN32(bytestream + 8, av_float2int(b[j]));
                bytestream += 12;
            }

            r += p->linesize[2] / 4;
            g += p->linesize[0] / 4;
            b += p->linesize[1] / 4;
        }
    } else {
    ptr      = p->data[0];
    linesize = p->linesize[0];
    for (i = 0; i < h; i++) {
        memcpy(bytestream, ptr, n);
        bytestream += n;
        ptr        += linesize;
    }
    }

    if (avctx->pix_fmt == AV_PIX_FMT_YUV420P || avctx->pix_fmt == AV_PIX_FMT_YUV420P16BE) {
        h >>= 1;
        n >>= 1;
        ptr1 = p->data[1];
        ptr2 = p->data[2];
        for (i = 0; i < h; i++) {
            memcpy(bytestream, ptr1, n);
            bytestream += n;
            memcpy(bytestream, ptr2, n);
            bytestream += n;
                ptr1 += p->linesize[1];
                ptr2 += p->linesize[2];
        }
    }
    av_shrink_packet(pkt, bytestream - bytestream_start);
    *got_packet = 1;

    return 0;
}

#if CONFIG_PGM_ENCODER
const FFCodec ff_pgm_encoder = {
    .p.name         = "pgm",
    .p.long_name    = NULL_IF_CONFIG_SMALL("PGM (Portable GrayMap) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PGM,
    .p.capabilities = AV_CODEC_CAP_DR1,
    FF_CODEC_ENCODE_CB(pnm_encode_frame),
    .p.pix_fmts     = (const enum AVPixelFormat[]){
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY16BE, AV_PIX_FMT_NONE
    },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
#endif

#if CONFIG_PGMYUV_ENCODER
const FFCodec ff_pgmyuv_encoder = {
    .p.name         = "pgmyuv",
    .p.long_name    = NULL_IF_CONFIG_SMALL("PGMYUV (Portable GrayMap YUV) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PGMYUV,
    .p.capabilities = AV_CODEC_CAP_DR1,
    FF_CODEC_ENCODE_CB(pnm_encode_frame),
    .p.pix_fmts     = (const enum AVPixelFormat[]){
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P16BE, AV_PIX_FMT_NONE
    },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
#endif

#if CONFIG_PPM_ENCODER
const FFCodec ff_ppm_encoder = {
    .p.name         = "ppm",
    .p.long_name    = NULL_IF_CONFIG_SMALL("PPM (Portable PixelMap) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PPM,
    .p.capabilities = AV_CODEC_CAP_DR1,
    FF_CODEC_ENCODE_CB(pnm_encode_frame),
    .p.pix_fmts     = (const enum AVPixelFormat[]){
        AV_PIX_FMT_RGB24, AV_PIX_FMT_RGB48BE, AV_PIX_FMT_NONE
    },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
#endif

#if CONFIG_PBM_ENCODER
const FFCodec ff_pbm_encoder = {
    .p.name         = "pbm",
    .p.long_name    = NULL_IF_CONFIG_SMALL("PBM (Portable BitMap) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PBM,
    .p.capabilities = AV_CODEC_CAP_DR1,
    FF_CODEC_ENCODE_CB(pnm_encode_frame),
    .p.pix_fmts     = (const enum AVPixelFormat[]){ AV_PIX_FMT_MONOWHITE,
                                                  AV_PIX_FMT_NONE },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
#endif

#if CONFIG_PFM_ENCODER
const FFCodec ff_pfm_encoder = {
    .p.name         = "pfm",
    .p.long_name    = NULL_IF_CONFIG_SMALL("PFM (Portable FloatMap) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PFM,
    .p.capabilities = AV_CODEC_CAP_DR1,
    FF_CODEC_ENCODE_CB(pnm_encode_frame),
    .p.pix_fmts     = (const enum AVPixelFormat[]){ AV_PIX_FMT_GBRPF32,
                                                    AV_PIX_FMT_NONE },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
#endif
