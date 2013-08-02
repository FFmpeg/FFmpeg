/*
 * Dxtory decoder
 *
 * Copyright (c) 2011 Konstantin Shishkov
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

#define BITSTREAM_READER_LE
#include "avcodec.h"
#include "bytestream.h"
#include "get_bits.h"
#include "internal.h"
#include "unary.h"
#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"

static int dxtory_decode_v1(AVCodecContext *avctx, AVFrame *pic,
                            const uint8_t *src, int src_size)
{
    int h, w;
    uint8_t *Y1, *Y2, *U, *V;
    int ret;

    if (src_size < avctx->width * avctx->height * 3 / 2) {
        av_log(avctx, AV_LOG_ERROR, "packet too small\n");
        return AVERROR_INVALIDDATA;
    }

    avctx->pix_fmt = AV_PIX_FMT_YUV420P;
    if ((ret = ff_get_buffer(avctx, pic, 0)) < 0)
        return ret;

    Y1 = pic->data[0];
    Y2 = pic->data[0] + pic->linesize[0];
    U  = pic->data[1];
    V  = pic->data[2];
    for (h = 0; h < avctx->height; h += 2) {
        for (w = 0; w < avctx->width; w += 2) {
            AV_COPY16(Y1 + w, src);
            AV_COPY16(Y2 + w, src + 2);
            U[w >> 1] = src[4] + 0x80;
            V[w >> 1] = src[5] + 0x80;
            src += 6;
        }
        Y1 += pic->linesize[0] << 1;
        Y2 += pic->linesize[0] << 1;
        U  += pic->linesize[1];
        V  += pic->linesize[2];
    }

    return 0;
}

static const uint8_t def_lru[8] = { 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xFF };

static inline uint8_t decode_sym(GetBitContext *gb, uint8_t lru[8])
{
    uint8_t c, val;

    c = get_unary(gb, 0, 8);
    if (!c) {
        val = get_bits(gb, 8);
        memmove(lru + 1, lru, sizeof(*lru) * (8 - 1));
    } else {
        val = lru[c - 1];
        memmove(lru + 1, lru, sizeof(*lru) * (c - 1));
    }
    lru[0] = val;

    return val;
}

static int dx2_decode_slice(GetBitContext *gb, int width, int height,
                            uint8_t *Y, uint8_t *U, uint8_t *V,
                            int ystride, int ustride, int vstride)
{
    int x, y, i;
    uint8_t lru[3][8];

    for (i = 0; i < 3; i++)
        memcpy(lru[i], def_lru, 8 * sizeof(*def_lru));

    for (y = 0; y < height; y+=2) {
        for (x = 0; x < width; x += 2) {
            Y[x + 0 + 0 * ystride] = decode_sym(gb, lru[0]);
            Y[x + 1 + 0 * ystride] = decode_sym(gb, lru[0]);
            Y[x + 0 + 1 * ystride] = decode_sym(gb, lru[0]);
            Y[x + 1 + 1 * ystride] = decode_sym(gb, lru[0]);
            U[x >> 1] = decode_sym(gb, lru[1]) ^ 0x80;
            V[x >> 1] = decode_sym(gb, lru[2]) ^ 0x80;
        }

        Y += ystride << 1;
        U += ustride;
        V += vstride;
    }

    return 0;
}

static int dxtory_decode_v2(AVCodecContext *avctx, AVFrame *pic,
                            const uint8_t *src, int src_size)
{
    GetByteContext gb;
    GetBitContext  gb2;
    int nslices, slice, slice_height;
    uint32_t off, slice_size;
    uint8_t *Y, *U, *V;
    int ret;

    bytestream2_init(&gb, src, src_size);
    nslices = bytestream2_get_le16(&gb);
    off = FFALIGN(nslices * 4 + 2, 16);
    if (src_size < off) {
        av_log(avctx, AV_LOG_ERROR, "no slice data\n");
        return AVERROR_INVALIDDATA;
    }

    if (!nslices || avctx->height % nslices) {
        avpriv_request_sample(avctx, "%d slices for %dx%d", nslices,
                              avctx->width, avctx->height);
        return AVERROR(ENOSYS);
    }

    slice_height = avctx->height / nslices;
    if ((avctx->width & 1) || (slice_height & 1)) {
        avpriv_request_sample(avctx, "slice dimensions %dx%d",
                              avctx->width, slice_height);
    }

    avctx->pix_fmt = AV_PIX_FMT_YUV420P;
    if ((ret = ff_get_buffer(avctx, pic, 0)) < 0)
        return ret;

    Y = pic->data[0];
    U = pic->data[1];
    V = pic->data[2];

    for (slice = 0; slice < nslices; slice++) {
        slice_size = bytestream2_get_le32(&gb);
        if (slice_size > src_size - off) {
            av_log(avctx, AV_LOG_ERROR,
                   "invalid slice size %d (only %d bytes left)\n",
                   slice_size, src_size - off);
            return AVERROR_INVALIDDATA;
        }
        if (slice_size <= 16) {
            av_log(avctx, AV_LOG_ERROR, "invalid slice size %d\n", slice_size);
            return AVERROR_INVALIDDATA;
        }

        if (AV_RL32(src + off) != slice_size - 16) {
            av_log(avctx, AV_LOG_ERROR,
                   "Slice sizes mismatch: got %d instead of %d\n",
                   AV_RL32(src + off), slice_size - 16);
        }
        init_get_bits(&gb2, src + off + 16, (slice_size - 16) * 8);
        dx2_decode_slice(&gb2, avctx->width, slice_height, Y, U, V,
                         pic->linesize[0], pic->linesize[1], pic->linesize[2]);

        Y += pic->linesize[0] *  slice_height;
        U += pic->linesize[1] * (slice_height >> 1);
        V += pic->linesize[2] * (slice_height >> 1);
        off += slice_size;
    }

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                        AVPacket *avpkt)
{
    AVFrame *pic = data;
    const uint8_t *src = avpkt->data;
    int ret;

    if (avpkt->size < 16) {
        av_log(avctx, AV_LOG_ERROR, "packet too small\n");
        return AVERROR_INVALIDDATA;
    }

    switch (AV_RB32(src)) {
    case 0x02000001:
        ret = dxtory_decode_v1(avctx, pic, src + 16, avpkt->size - 16);
        break;
    case 0x02000009:
        ret = dxtory_decode_v2(avctx, pic, src + 16, avpkt->size - 16);
        break;
    default:
        avpriv_request_sample(avctx, "Frame header %X", AV_RB32(src));
        return AVERROR_PATCHWELCOME;
    }

    if (ret)
        return ret;

    pic->pict_type = AV_PICTURE_TYPE_I;
    pic->key_frame = 1;
    *got_frame = 1;

    return avpkt->size;
}

AVCodec ff_dxtory_decoder = {
    .name           = "dxtory",
    .long_name      = NULL_IF_CONFIG_SMALL("Dxtory"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_DXTORY,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
