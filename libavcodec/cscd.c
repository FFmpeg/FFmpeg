/*
 * CamStudio decoder
 * Copyright (c) 2006 Reimar Doeffinger
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
#include <stdio.h>
#include <stdlib.h>

#include "avcodec.h"
#include "internal.h"
#include "libavutil/common.h"

#if CONFIG_ZLIB
#include <zlib.h>
#endif
#include "libavutil/lzo.h"

typedef struct CamStudioContext {
    AVFrame *pic;
    int linelen, height, bpp;
    unsigned int decomp_size;
    unsigned char* decomp_buf;
} CamStudioContext;

static void copy_frame_default(AVFrame *f, const uint8_t *src,
                               int linelen, int height) {
    int i, src_stride = FFALIGN(linelen, 4);
    uint8_t *dst = f->data[0];
    dst += (height - 1) * f->linesize[0];
    for (i = height; i; i--) {
        memcpy(dst, src, linelen);
        src += src_stride;
        dst -= f->linesize[0];
    }
}

static void add_frame_default(AVFrame *f, const uint8_t *src,
                              int linelen, int height) {
    int i, j, src_stride = FFALIGN(linelen, 4);
    uint8_t *dst = f->data[0];
    dst += (height - 1) * f->linesize[0];
    for (i = height; i; i--) {
        for (j = linelen; j; j--)
            *dst++ += *src++;
        src += src_stride - linelen;
        dst -= f->linesize[0] + linelen;
    }
}

static int decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                        AVPacket *avpkt) {
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    CamStudioContext *c = avctx->priv_data;
    int ret;

    if (buf_size < 2) {
        av_log(avctx, AV_LOG_ERROR, "coded frame too small\n");
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_reget_buffer(avctx, c->pic)) < 0)
        return ret;

    // decompress data
    switch ((buf[0] >> 1) & 7) {
        case 0: { // lzo compression
            int outlen = c->decomp_size, inlen = buf_size - 2;
            if (av_lzo1x_decode(c->decomp_buf, &outlen, &buf[2], &inlen))
                av_log(avctx, AV_LOG_ERROR, "error during lzo decompression\n");
            break;
        }
        case 1: { // zlib compression
#if CONFIG_ZLIB
            unsigned long dlen = c->decomp_size;
            if (uncompress(c->decomp_buf, &dlen, &buf[2], buf_size - 2) != Z_OK)
                av_log(avctx, AV_LOG_ERROR, "error during zlib decompression\n");
            break;
#else
            av_log(avctx, AV_LOG_ERROR, "compiled without zlib support\n");
            return AVERROR(ENOSYS);
#endif
        }
        default:
            av_log(avctx, AV_LOG_ERROR, "unknown compression\n");
            return AVERROR_INVALIDDATA;
    }

    // flip upside down, add difference frame
    if (buf[0] & 1) { // keyframe
        c->pic->pict_type = AV_PICTURE_TYPE_I;
        c->pic->key_frame = 1;
              copy_frame_default(c->pic, c->decomp_buf,
                                 c->linelen, c->height);
    } else {
        c->pic->pict_type = AV_PICTURE_TYPE_P;
        c->pic->key_frame = 0;
              add_frame_default(c->pic, c->decomp_buf,
                                c->linelen, c->height);
    }

    *got_frame = 1;
    if ((ret = av_frame_ref(data, c->pic)) < 0)
        return ret;

    return buf_size;
}

static av_cold int decode_init(AVCodecContext *avctx) {
    CamStudioContext *c = avctx->priv_data;
    int stride;
    switch (avctx->bits_per_coded_sample) {
        case 16: avctx->pix_fmt = AV_PIX_FMT_RGB555LE; break;
        case 24: avctx->pix_fmt = AV_PIX_FMT_BGR24; break;
        case 32: avctx->pix_fmt = AV_PIX_FMT_BGRA; break;
        default:
            av_log(avctx, AV_LOG_ERROR,
                   "CamStudio codec error: invalid depth %i bpp\n",
                   avctx->bits_per_coded_sample);
            return AVERROR_INVALIDDATA;
    }
    c->bpp = avctx->bits_per_coded_sample;
    c->linelen = avctx->width * avctx->bits_per_coded_sample / 8;
    c->height = avctx->height;
    stride = FFALIGN(c->linelen, 4);
    c->decomp_size = c->height * stride;
    c->decomp_buf = av_malloc(c->decomp_size + AV_LZO_OUTPUT_PADDING);
    if (!c->decomp_buf) {
        av_log(avctx, AV_LOG_ERROR, "Can't allocate decompression buffer.\n");
        return AVERROR(ENOMEM);
    }
    c->pic = av_frame_alloc();
    if (!c->pic)
        return AVERROR(ENOMEM);
    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx) {
    CamStudioContext *c = avctx->priv_data;
    av_freep(&c->decomp_buf);
    av_frame_free(&c->pic);
    return 0;
}

AVCodec ff_cscd_decoder = {
    .name           = "camstudio",
    .long_name      = NULL_IF_CONFIG_SMALL("CamStudio"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_CSCD,
    .priv_data_size = sizeof(CamStudioContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
