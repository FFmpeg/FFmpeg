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
#include "internal.h"

static av_cold int encode_init(AVCodecContext *avctx)
{
    if (avctx->width & 1) {
        av_log(avctx, AV_LOG_ERROR, "v210 needs even width\n");
        return AVERROR(EINVAL);
    }

    if (avctx->bits_per_raw_sample != 10)
        av_log(avctx, AV_LOG_WARNING, "bits per raw sample: %d != 10-bit\n",
               avctx->bits_per_raw_sample);

    avctx->coded_frame = avcodec_alloc_frame();
    if (!avctx->coded_frame)
        return AVERROR(ENOMEM);

    avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;

    return 0;
}

static int encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                        const AVFrame *pic, int *got_packet)
{
    int aligned_width = ((avctx->width + 47) / 48) * 48;
    int stride = aligned_width * 8 / 3;
    int line_padding = stride - ((avctx->width * 8 + 11) / 12) * 4;
    int h, w, ret;
    const uint16_t *y = (const uint16_t*)pic->data[0];
    const uint16_t *u = (const uint16_t*)pic->data[1];
    const uint16_t *v = (const uint16_t*)pic->data[2];
    PutByteContext p;

    if ((ret = ff_alloc_packet2(avctx, pkt, avctx->height * stride)) < 0)
        return ret;

    bytestream2_init_writer(&p, pkt->data, pkt->size);

#define CLIP(v) av_clip(v, 4, 1019)

#define WRITE_PIXELS(a, b, c)           \
    do {                                \
        val =   CLIP(*a++);             \
        val |= (CLIP(*b++) << 10) |     \
               (CLIP(*c++) << 20);      \
        bytestream2_put_le32u(&p, val); \
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
                bytestream2_put_le32u(&p, val);
            if (w < avctx->width - 3) {
                val |= (CLIP(*u++) << 10) | (CLIP(*y++) << 20);
                bytestream2_put_le32u(&p, val);

                val = CLIP(*v++) | (CLIP(*y++) << 10);
                bytestream2_put_le32u(&p, val);
            }
        }

        bytestream2_set_buffer(&p, 0, line_padding);

        y += pic->linesize[0] / 2 - avctx->width;
        u += pic->linesize[1] / 2 - avctx->width / 2;
        v += pic->linesize[2] / 2 - avctx->width / 2;
    }

    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;
    return 0;
}

static av_cold int encode_close(AVCodecContext *avctx)
{
    av_freep(&avctx->coded_frame);

    return 0;
}

AVCodec ff_v210_encoder = {
    .name           = "v210",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_V210,
    .init           = encode_init,
    .encode2        = encode_frame,
    .close          = encode_close,
    .pix_fmts       = (const enum PixelFormat[]){ PIX_FMT_YUV422P10, PIX_FMT_NONE },
    .long_name      = NULL_IF_CONFIG_SMALL("Uncompressed 4:2:2 10-bit"),
};
