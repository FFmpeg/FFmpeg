/*
 * RTV1 decoder
 * Copyright (c) 2023 Paul B Mahol
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
#include <string.h>

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"
#include "texturedsp.h"
#include "thread.h"

static av_cold int decode_init(AVCodecContext *avctx)
{
    TextureDSPContext *dsp = avctx->priv_data;
    avctx->pix_fmt = AV_PIX_FMT_BGR0;
    ff_texturedsp_init(dsp);
    return 0;
}

static int decode_rtv1(GetByteContext *gb, uint8_t *dst, ptrdiff_t linesize,
                       int width, int height, int flag,
                       int (*dxt1_block)(uint8_t *dst, ptrdiff_t stride, const uint8_t *block))
{
    uint8_t block[8] = { 0 };
    int run = 0;

    for (int y = 0; y < height; y += 4) {
        for (int x = 0; x < width * 4; x += 16) {
            int mode = 0;

            if (run && --run > 0) {
                dxt1_block(dst + x, linesize, block);
            } else {
                int a, b;

                if (bytestream2_get_bytes_left(gb) < 4)
                    return AVERROR_INVALIDDATA;

                a = bytestream2_get_le16u(gb);
                b = bytestream2_get_le16u(gb);
                if (a == b && flag) {
                    AV_WL32(block + 4, 0);
                } else if (a == 1 && b == 0xffff) {
                    mode = 1;
                } else if (b && a == 0) {
                    run = b;
                } else {
                    AV_WL16(block,     a);
                    AV_WL16(block + 2, b);
                    AV_WL32(block + 4, bytestream2_get_le32(gb));
                }
                if (run && !mode) {
                    dxt1_block(dst + x, linesize, block);
                } else if (!mode) {
                    AV_WL16(block,     a);
                    AV_WL16(block + 2, b);
                    dxt1_block(dst + x, linesize, block);
                } else {
                    if (bytestream2_get_bytes_left(gb) < 12 * 4)
                        return AVERROR_INVALIDDATA;

                    for (int by = 0; by < 4; by++) {
                        for (int bx = 0; bx < 4; bx++)
                            AV_WL32(dst + x + bx * 4 + by * linesize, bytestream2_get_le24u(gb));
                    }
                }
            }
        }

        dst += linesize * 4;
    }

    return 0;
}

static int decode_frame(AVCodecContext *avctx, AVFrame *p,
                        int *got_frame, AVPacket *avpkt)
{
    int ret, width, height, flags;
    TextureDSPContext *dsp = avctx->priv_data;
    GetByteContext gb;
    ptrdiff_t linesize;
    uint8_t *dst;

    if (avpkt->size < 22)
        return AVERROR_INVALIDDATA;

    bytestream2_init(&gb, avpkt->data, avpkt->size);

    if (bytestream2_get_le32(&gb) != MKTAG('D','X','T','1'))
        return AVERROR_INVALIDDATA;
    flags = bytestream2_get_le32(&gb);

    width = bytestream2_get_le32(&gb);
    height = bytestream2_get_le32(&gb);
    if (width > INT_MAX-4U || height > INT_MAX-4U)
        return AVERROR_INVALIDDATA;
    ret = ff_set_dimensions(avctx, FFALIGN(width, 4), FFALIGN(height, 4));
    if (ret < 0)
        return ret;

    avctx->width  = width;
    avctx->height = height;

    if ((ret = ff_thread_get_buffer(avctx, p, 0)) < 0)
        return ret;

    dst = p->data[0] + p->linesize[0] * (avctx->coded_height - 1);
    linesize = -p->linesize[0];

    ret = decode_rtv1(&gb, dst, linesize, width, height, flags, dsp->dxt1_block);
    if (ret < 0)
        return ret;

    p->pict_type = AV_PICTURE_TYPE_I;
    p->flags |= AV_FRAME_FLAG_KEY;

    *got_frame = 1;

    return avpkt->size;
}

const FFCodec ff_rtv1_decoder = {
    .p.name           = "rtv1",
    CODEC_LONG_NAME("RTV1 (RivaTuner Video)"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_RTV1,
    .priv_data_size   = sizeof(TextureDSPContext),
    .init             = decode_init,
    FF_CODEC_DECODE_CB(decode_frame),
    .p.capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
};
