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

#if CONFIG_ZLIB
#include <zlib.h>
#endif
#include "libavutil/lzo.h"

typedef struct {
    AVFrame pic;
    int linelen, height, bpp;
    unsigned int decomp_size;
    unsigned char* decomp_buf;
} CamStudioContext;

static void copy_frame_default(AVFrame *f, const uint8_t *src, int src_stride,
                               int linelen, int height) {
    int i;
    uint8_t *dst = f->data[0];
    dst += (height - 1) * f->linesize[0];
    for (i = height; i; i--) {
        memcpy(dst, src, linelen);
        src += src_stride;
        dst -= f->linesize[0];
    }
}

static void add_frame_default(AVFrame *f, const uint8_t *src, int src_stride,
                              int linelen, int height) {
    int i, j;
    uint8_t *dst = f->data[0];
    dst += (height - 1) * f->linesize[0];
    for (i = height; i; i--) {
        for (j = linelen; j; j--)
            *dst++ += *src++;
        src += src_stride - linelen;
        dst -= f->linesize[0] + linelen;
    }
}

#if !HAVE_BIGENDIAN
#define copy_frame_16(f, s, l, h) copy_frame_default(f, s, l, l, h)
#define copy_frame_32(f, s, l, h) copy_frame_default(f, s, l, l, h)
#define add_frame_16(f, s, l, h) add_frame_default(f, s, l, l, h)
#define add_frame_32(f, s, l, h) add_frame_default(f, s, l, l, h)
#else
static void copy_frame_16(AVFrame *f, const uint8_t *src,
                          int linelen, int height) {
    int i, j;
    uint8_t *dst = f->data[0];
    dst += (height - 1) * f->linesize[0];
    for (i = height; i; i--) {
        for (j = linelen / 2; j; j--) {
          dst[0] = src[1];
          dst[1] = src[0];
          src += 2;
          dst += 2;
        }
        dst -= f->linesize[0] + linelen;
    }
}

static void copy_frame_32(AVFrame *f, const uint8_t *src,
                          int linelen, int height) {
    int i, j;
    uint8_t *dst = f->data[0];
    dst += (height - 1) * f->linesize[0];
    for (i = height; i; i--) {
        for (j = linelen / 4; j; j--) {
          dst[0] = src[3];
          dst[1] = src[2];
          dst[2] = src[1];
          dst[3] = src[0];
          src += 4;
          dst += 4;
        }
        dst -= f->linesize[0] + linelen;
    }
}

static void add_frame_16(AVFrame *f, const uint8_t *src,
                         int linelen, int height) {
    int i, j;
    uint8_t *dst = f->data[0];
    dst += (height - 1) * f->linesize[0];
    for (i = height; i; i--) {
        for (j = linelen / 2; j; j--) {
          dst[0] += src[1];
          dst[1] += src[0];
          src += 2;
          dst += 2;
        }
        dst -= f->linesize[0] + linelen;
    }
}

static void add_frame_32(AVFrame *f, const uint8_t *src,
                         int linelen, int height) {
    int i, j;
    uint8_t *dst = f->data[0];
    dst += (height - 1) * f->linesize[0];
    for (i = height; i; i--) {
        for (j = linelen / 4; j; j--) {
          dst[0] += src[3];
          dst[1] += src[2];
          dst[2] += src[1];
          dst[3] += src[0];
          src += 4;
          dst += 4;
        }
        dst -= f->linesize[0] + linelen;
    }
}
#endif

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                        AVPacket *avpkt) {
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    CamStudioContext *c = avctx->priv_data;
    AVFrame *picture = data;

    if (buf_size < 2) {
        av_log(avctx, AV_LOG_ERROR, "coded frame too small\n");
        return -1;
    }

    if (c->pic.data[0])
        avctx->release_buffer(avctx, &c->pic);
    c->pic.reference = 3;
    c->pic.buffer_hints = FF_BUFFER_HINTS_VALID | FF_BUFFER_HINTS_READABLE |
                          FF_BUFFER_HINTS_PRESERVE | FF_BUFFER_HINTS_REUSABLE;
    if (avctx->get_buffer(avctx, &c->pic) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

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
            return -1;
#endif
        }
        default:
            av_log(avctx, AV_LOG_ERROR, "unknown compression\n");
            return -1;
    }

    // flip upside down, add difference frame
    if (buf[0] & 1) { // keyframe
        c->pic.pict_type = AV_PICTURE_TYPE_I;
        c->pic.key_frame = 1;
        switch (c->bpp) {
          case 16:
              copy_frame_16(&c->pic, c->decomp_buf, c->linelen, c->height);
              break;
          case 32:
              copy_frame_32(&c->pic, c->decomp_buf, c->linelen, c->height);
              break;
          default:
              copy_frame_default(&c->pic, c->decomp_buf, FFALIGN(c->linelen, 4),
                                 c->linelen, c->height);
        }
    } else {
        c->pic.pict_type = AV_PICTURE_TYPE_P;
        c->pic.key_frame = 0;
        switch (c->bpp) {
          case 16:
              add_frame_16(&c->pic, c->decomp_buf, c->linelen, c->height);
              break;
          case 32:
              add_frame_32(&c->pic, c->decomp_buf, c->linelen, c->height);
              break;
          default:
              add_frame_default(&c->pic, c->decomp_buf, FFALIGN(c->linelen, 4),
                                c->linelen, c->height);
        }
    }

    *picture = c->pic;
    *data_size = sizeof(AVFrame);
    return buf_size;
}

static av_cold int decode_init(AVCodecContext *avctx) {
    CamStudioContext *c = avctx->priv_data;
    int stride;
    switch (avctx->bits_per_coded_sample) {
        case 16: avctx->pix_fmt = PIX_FMT_RGB555; break;
        case 24: avctx->pix_fmt = PIX_FMT_BGR24; break;
        case 32: avctx->pix_fmt = PIX_FMT_RGB32; break;
        default:
            av_log(avctx, AV_LOG_ERROR,
                   "CamStudio codec error: invalid depth %i bpp\n",
                   avctx->bits_per_coded_sample);
            return 1;
    }
    c->bpp = avctx->bits_per_coded_sample;
    avcodec_get_frame_defaults(&c->pic);
    c->pic.data[0] = NULL;
    c->linelen = avctx->width * avctx->bits_per_coded_sample / 8;
    c->height = avctx->height;
    stride = c->linelen;
    if (avctx->bits_per_coded_sample == 24)
        stride = FFALIGN(stride, 4);
    c->decomp_size = c->height * stride;
    c->decomp_buf = av_malloc(c->decomp_size + AV_LZO_OUTPUT_PADDING);
    if (!c->decomp_buf) {
        av_log(avctx, AV_LOG_ERROR, "Can't allocate decompression buffer.\n");
        return 1;
    }
    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx) {
    CamStudioContext *c = avctx->priv_data;
    av_freep(&c->decomp_buf);
    if (c->pic.data[0])
        avctx->release_buffer(avctx, &c->pic);
    return 0;
}

AVCodec ff_cscd_decoder = {
    .name           = "camstudio",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_CSCD,
    .priv_data_size = sizeof(CamStudioContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("CamStudio"),
};

