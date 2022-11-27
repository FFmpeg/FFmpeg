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
#include "libavutil/float2half.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"

typedef struct PHMEncContext {
    Float2HalfTables f2h_tables;
} PHMEncContext;

static int pnm_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                            const AVFrame *p, int *got_packet)
{
    PHMEncContext *s = avctx->priv_data;
    uint8_t *bytestream, *bytestream_start, *bytestream_end;
    int i, h, h1, c, n, linesize, ret;
    int size = av_image_get_buffer_size(avctx->pix_fmt,
                                        avctx->width, avctx->height, 1);

    if (size < 0)
        return size;

    if ((ret = ff_get_encode_buffer(avctx, pkt, size + 200U, 0)) < 0)
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
    case AV_PIX_FMT_GBRPF32BE:
    case AV_PIX_FMT_GBRPF32LE:
        if (avctx->codec_id == AV_CODEC_ID_PFM) {
        c  = 'F';
        n  = avctx->width * 4;
        } else {
            c  = 'H';
            n  = avctx->width * 2;
        }
        break;
    case AV_PIX_FMT_GRAYF32BE:
    case AV_PIX_FMT_GRAYF32LE:
        if (avctx->codec_id == AV_CODEC_ID_PFM) {
        c  = 'f';
        n  = avctx->width * 4;
        } else {
            c  = 'h';
            n  = avctx->width * 2;
        }
        break;
    default:
        return -1;
    }
    snprintf(bytestream, bytestream_end - bytestream,
             "P%c\n%d %d\n", c, avctx->width, h1);
    bytestream += strlen(bytestream);
    if (avctx->pix_fmt == AV_PIX_FMT_GBRPF32LE ||
        avctx->pix_fmt == AV_PIX_FMT_GRAYF32LE ||
        avctx->pix_fmt == AV_PIX_FMT_GBRPF32BE ||
        avctx->pix_fmt == AV_PIX_FMT_GRAYF32BE)
        snprintf(bytestream, bytestream_end - bytestream,
                 "%f\n", (avctx->pix_fmt == AV_PIX_FMT_GBRPF32BE ||
                          avctx->pix_fmt == AV_PIX_FMT_GRAYF32BE) ? 1.f: -1.f);
    bytestream += strlen(bytestream);
    if (avctx->pix_fmt != AV_PIX_FMT_MONOWHITE &&
        avctx->pix_fmt != AV_PIX_FMT_GBRPF32LE &&
        avctx->pix_fmt != AV_PIX_FMT_GRAYF32LE &&
        avctx->pix_fmt != AV_PIX_FMT_GBRPF32BE &&
        avctx->pix_fmt != AV_PIX_FMT_GRAYF32BE) {
        int maxdepth = (1 << av_pix_fmt_desc_get(avctx->pix_fmt)->comp[0].depth) - 1;
        snprintf(bytestream, bytestream_end - bytestream,
                 "%d\n", maxdepth);
        bytestream += strlen(bytestream);
    }

    if ((avctx->pix_fmt == AV_PIX_FMT_GBRPF32LE ||
         avctx->pix_fmt == AV_PIX_FMT_GBRPF32BE) && c == 'F') {
        /* PFM is encoded from bottom to top */
        const float *r = (const float *)(p->data[2] + p->linesize[2] * (avctx->height - 1));
        const float *g = (const float *)(p->data[0] + p->linesize[0] * (avctx->height - 1));
        const float *b = (const float *)(p->data[1] + p->linesize[1] * (avctx->height - 1));

        for (int i = 0; i < avctx->height; i++) {
            for (int j = 0; j < avctx->width; j++) {
                AV_WN32(bytestream + 0, av_float2int(r[j]));
                AV_WN32(bytestream + 4, av_float2int(g[j]));
                AV_WN32(bytestream + 8, av_float2int(b[j]));
                bytestream += 12;
            }

            r -= p->linesize[2] / 4;
            g -= p->linesize[0] / 4;
            b -= p->linesize[1] / 4;
        }
    } else if ((avctx->pix_fmt == AV_PIX_FMT_GRAYF32LE ||
                avctx->pix_fmt == AV_PIX_FMT_GRAYF32BE) && c == 'f') {
        /* PFM is encoded from bottom to top */
        const float *g = (const float *)(p->data[0] + p->linesize[0] * (avctx->height - 1));

        for (int i = 0; i < avctx->height; i++) {
            for (int j = 0; j < avctx->width; j++) {
                AV_WN32(bytestream, av_float2int(g[j]));
                bytestream += 4;
            }

            g -= p->linesize[0] / 4;
        }
    } else if (avctx->pix_fmt == AV_PIX_FMT_GBRPF32 && c == 'H') {
        const float *r = (const float *)p->data[2];
        const float *g = (const float *)p->data[0];
        const float *b = (const float *)p->data[1];

        for (int i = 0; i < avctx->height; i++) {
            for (int j = 0; j < avctx->width; j++) {
                AV_WN16(bytestream + 0, float2half(av_float2int(r[j]), &s->f2h_tables));
                AV_WN16(bytestream + 2, float2half(av_float2int(g[j]), &s->f2h_tables));
                AV_WN16(bytestream + 4, float2half(av_float2int(b[j]), &s->f2h_tables));
                bytestream += 6;
            }

            r += p->linesize[2] / 4;
            g += p->linesize[0] / 4;
            b += p->linesize[1] / 4;
        }
    } else if (avctx->pix_fmt == AV_PIX_FMT_GRAYF32 && c == 'h') {
        const float *g = (const float *)p->data[0];

        for (int i = 0; i < avctx->height; i++) {
            for (int j = 0; j < avctx->width; j++) {
                AV_WN16(bytestream, float2half(av_float2int(g[j]), &s->f2h_tables));
                bytestream += 2;
            }

            g += p->linesize[0] / 4;
        }
    } else {
        const uint8_t *ptr = p->data[0];
        linesize = p->linesize[0];
        for (i = 0; i < h; i++) {
            memcpy(bytestream, ptr, n);
            bytestream += n;
            ptr        += linesize;
        }
    }

    if (avctx->pix_fmt == AV_PIX_FMT_YUV420P || avctx->pix_fmt == AV_PIX_FMT_YUV420P16BE) {
        const uint8_t *ptr1 = p->data[1], *ptr2 = p->data[2];
        h >>= 1;
        n >>= 1;
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
    CODEC_LONG_NAME("PGM (Portable GrayMap) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PGM,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    FF_CODEC_ENCODE_CB(pnm_encode_frame),
    .p.pix_fmts     = (const enum AVPixelFormat[]){
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY16BE, AV_PIX_FMT_NONE
    },
};
#endif

#if CONFIG_PGMYUV_ENCODER
const FFCodec ff_pgmyuv_encoder = {
    .p.name         = "pgmyuv",
    CODEC_LONG_NAME("PGMYUV (Portable GrayMap YUV) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PGMYUV,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    FF_CODEC_ENCODE_CB(pnm_encode_frame),
    .p.pix_fmts     = (const enum AVPixelFormat[]){
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P16BE, AV_PIX_FMT_NONE
    },
};
#endif

#if CONFIG_PPM_ENCODER
const FFCodec ff_ppm_encoder = {
    .p.name         = "ppm",
    CODEC_LONG_NAME("PPM (Portable PixelMap) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PPM,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    FF_CODEC_ENCODE_CB(pnm_encode_frame),
    .p.pix_fmts     = (const enum AVPixelFormat[]){
        AV_PIX_FMT_RGB24, AV_PIX_FMT_RGB48BE, AV_PIX_FMT_NONE
    },
};
#endif

#if CONFIG_PBM_ENCODER
const FFCodec ff_pbm_encoder = {
    .p.name         = "pbm",
    CODEC_LONG_NAME("PBM (Portable BitMap) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PBM,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    FF_CODEC_ENCODE_CB(pnm_encode_frame),
    .p.pix_fmts     = (const enum AVPixelFormat[]){ AV_PIX_FMT_MONOWHITE,
                                                  AV_PIX_FMT_NONE },
};
#endif

#if CONFIG_PFM_ENCODER
const FFCodec ff_pfm_encoder = {
    .p.name         = "pfm",
    CODEC_LONG_NAME("PFM (Portable FloatMap) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PFM,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    FF_CODEC_ENCODE_CB(pnm_encode_frame),
    .p.pix_fmts     = (const enum AVPixelFormat[]){ AV_PIX_FMT_GBRPF32LE,
                                                    AV_PIX_FMT_GRAYF32LE,
                                                    AV_PIX_FMT_GBRPF32BE,
                                                    AV_PIX_FMT_GRAYF32BE,
                                                    AV_PIX_FMT_NONE },
};
#endif

#if CONFIG_PHM_ENCODER
static av_cold int phm_enc_init(AVCodecContext *avctx)
{
    PHMEncContext *s = avctx->priv_data;

    ff_init_float2half_tables(&s->f2h_tables);

    return 0;
}

const FFCodec ff_phm_encoder = {
    .p.name         = "phm",
    CODEC_LONG_NAME("PHM (Portable HalfFloatMap) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PHM,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .priv_data_size = sizeof(PHMEncContext),
    .init           = phm_enc_init,
    FF_CODEC_ENCODE_CB(pnm_encode_frame),
    .p.pix_fmts     = (const enum AVPixelFormat[]){ AV_PIX_FMT_GBRPF32,
                                                    AV_PIX_FMT_GRAYF32,
                                                    AV_PIX_FMT_NONE },
};
#endif
