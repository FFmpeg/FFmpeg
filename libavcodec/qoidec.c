/*
 * QOI image format
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
#include "codec_internal.h"
#include "decode.h"
#include "thread.h"
#include "qoi.h"

static int qoi_decode_frame(AVCodecContext *avctx, AVFrame *p,
                            int *got_frame, AVPacket *avpkt)
{
    int width, height, channels, space, run = 0;
    uint8_t index[64][4] = { 0 };
    uint8_t px[4] = { 0, 0, 0, 255 };
    GetByteContext gb;
    uint8_t *dst;
    uint64_t len;
    int ret;

    if (avpkt->size < 20)
        return AVERROR_INVALIDDATA;

    bytestream2_init(&gb, avpkt->data, avpkt->size);
    bytestream2_skip(&gb, 4);
    width  = bytestream2_get_be32(&gb);
    height = bytestream2_get_be32(&gb);
    channels = bytestream2_get_byte(&gb);
    space = bytestream2_get_byte(&gb);
    switch (space) {
    case 0: break;
    case 1: avctx->color_trc = AVCOL_TRC_LINEAR; break;
    default: return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_set_dimensions(avctx, width, height)) < 0)
        return ret;

    switch (channels) {
    case 3: avctx->pix_fmt = AV_PIX_FMT_RGB24; break;
    case 4: avctx->pix_fmt = AV_PIX_FMT_RGBA;  break;
    default: return AVERROR_INVALIDDATA;
    }

    if (avctx->skip_frame >= AVDISCARD_ALL)
        return avpkt->size;

    if ((ret = ff_thread_get_buffer(avctx, p, 0)) < 0)
        return ret;

    dst = p->data[0];
    len = width * height * channels;
    for (int n = 0, off_x = 0; n < len; n += channels, off_x++) {
        if (off_x >= width) {
            off_x = 0;
            dst += p->linesize[0];
        }
        if (run > 0) {
            run--;
        } else if (bytestream2_get_bytes_left(&gb) > 4) {
            int chunk = bytestream2_get_byteu(&gb);

            if (chunk == QOI_OP_RGB) {
                bytestream2_get_bufferu(&gb, px, 3);
            } else if (chunk == QOI_OP_RGBA) {
                bytestream2_get_bufferu(&gb, px, 4);
            } else if ((chunk & QOI_MASK_2) == QOI_OP_INDEX) {
                memcpy(px, index[chunk], 4);
            } else if ((chunk & QOI_MASK_2) == QOI_OP_DIFF) {
                px[0] += ((chunk >> 4) & 0x03) - 2;
                px[1] += ((chunk >> 2) & 0x03) - 2;
                px[2] += ( chunk       & 0x03) - 2;
            } else if ((chunk & QOI_MASK_2) == QOI_OP_LUMA) {
                int b2 = bytestream2_get_byteu(&gb);
                int vg = (chunk & 0x3f) - 32;
                px[0] += vg - 8 + ((b2 >> 4) & 0x0f);
                px[1] += vg;
                px[2] += vg - 8 +  (b2       & 0x0f);
            } else if ((chunk & QOI_MASK_2) == QOI_OP_RUN) {
                run = chunk & 0x3f;
            }

            memcpy(index[QOI_COLOR_HASH(px) & 63], px, 4);
        } else {
            break;
        }

        memcpy(&dst[off_x * channels], px, channels);
    }

    p->flags |= AV_FRAME_FLAG_KEY;
    p->pict_type = AV_PICTURE_TYPE_I;

    *got_frame   = 1;

    return avpkt->size;
}

const FFCodec ff_qoi_decoder = {
    .p.name         = "qoi",
    CODEC_LONG_NAME("QOI (Quite OK Image format) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_QOI,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
    .caps_internal  = FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    FF_CODEC_DECODE_CB(qoi_decode_frame),
};
