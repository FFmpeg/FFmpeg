/*
 * V210 encoder
 *
 * Copyright (C) 2009 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2009 Baptiste Coudurier <baptiste dot coudurier at gmail dot com>
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

static av_cold int encode_init(AVCodecContext *avctx)
{
    if (avctx->width & 1) {
        av_log(avctx, AV_LOG_ERROR, "v210 needs even width\n");
        return -1;
    }

    if (avctx->pix_fmt != PIX_FMT_YUV422P10) {
        av_log(avctx, AV_LOG_ERROR, "v210 needs YUV422P10\n");
        return -1;
    }

    if (avctx->bits_per_raw_sample != 10)
        av_log(avctx, AV_LOG_WARNING, "bits per raw sample: %d != 10-bit\n",
               avctx->bits_per_raw_sample);

    avctx->coded_frame = avcodec_alloc_frame();

    avctx->coded_frame->key_frame = 1;
    avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;

    return 0;
}

static int encode_frame(AVCodecContext *avctx, unsigned char *buf,
                        int buf_size, void *data)
{
    const AVFrame *pic = data;
    int aligned_width = ((avctx->width + 47) / 48) * 48;
    int stride = aligned_width * 8 / 3;
    int h, w;
    const uint16_t *y = (const uint16_t*)pic->data[0];
    const uint16_t *u = (const uint16_t*)pic->data[1];
    const uint16_t *v = (const uint16_t*)pic->data[2];
    uint8_t *p = buf;
    uint8_t *pdst = buf;

    if (buf_size < aligned_width * avctx->height * 8 / 3) {
        av_log(avctx, AV_LOG_ERROR, "output buffer too small\n");
        return -1;
    }

#define CLIP(v) av_clip(v, 4, 1019)

#define WRITE_PIXELS(a, b, c)           \
    do {                                \
        val =   CLIP(*a++);             \
        val |= (CLIP(*b++) << 10) |     \
               (CLIP(*c++) << 20);      \
        bytestream_put_le32(&p, val);   \
    } while (0)

    for (h = 0; h < avctx->height; h++) {
        uint32_t val;
        for (w = 0; w < avctx->width - 5; w += 6) {
            WRITE_PIXELS(u, y, v);
            WRITE_PIXELS(y, u, y);
            WRITE_PIXELS(v, y, u);
            WRITE_PIXELS(y, v, y);
        }
        if (w < avctx->width - 1) {
            WRITE_PIXELS(u, y, v);

            val = CLIP(*y++);
            if (w == avctx->width - 2)
                bytestream_put_le32(&p, val);
        }
        if (w < avctx->width - 3) {
            val |= (CLIP(*u++) << 10) | (CLIP(*y++) << 20);
            bytestream_put_le32(&p, val);

            val = CLIP(*v++) | (CLIP(*y++) << 10);
            bytestream_put_le32(&p, val);
        }

        pdst += stride;
        memset(p, 0, pdst - p);
        p = pdst;
        y += pic->linesize[0] / 2 - avctx->width;
        u += pic->linesize[1] / 2 - avctx->width / 2;
        v += pic->linesize[2] / 2 - avctx->width / 2;
    }

    return p - buf;
}

static av_cold int encode_close(AVCodecContext *avctx)
{
    av_freep(&avctx->coded_frame);

    return 0;
}

AVCodec ff_v210_encoder = {
    "v210",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_V210,
    0,
    encode_init,
    encode_frame,
    encode_close,
    .pix_fmts = (const enum PixelFormat[]){PIX_FMT_YUV422P10, PIX_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("Uncompressed 4:2:2 10-bit"),
};
