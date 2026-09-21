/*
 * Unpack bit-packed streams to formats supported by FFmpeg
 * Copyright (c) 2017 Savoir-faire Linux, Inc
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

/* Development sponsored by CBC/Radio-Canada */

/**
 * @file
 * Bitpacked
 */

#include "avcodec.h"
#include "bitpacked_dec.h"
#include "bitpacked_dec_init.h"
#include "codec_internal.h"
#include "libavutil/imgutils.h"
#include "thread.h"

/* For this format, it's a simple passthrough */
static int bitpacked_decode_uyvy422(AVCodecContext *avctx, AVFrame *frame,
                                    const AVPacket *avpkt)
{
    int ret;

    /* there is no need to copy as the data already match
     * a known pixel format */
    frame->buf[0] = av_buffer_ref(avpkt->buf);
    if (!frame->buf[0]) {
        return AVERROR(ENOMEM);
    }

    ret = av_image_fill_arrays(frame->data, frame->linesize, avpkt->data,
                               avctx->pix_fmt, avctx->width, avctx->height, 1);
    if (ret < 0) {
        av_buffer_unref(&frame->buf[0]);
        return ret;
    }

    return 0;
}

static int bitpacked_decode_yuv422p10(AVCodecContext *avctx, AVFrame *frame,
                                      const AVPacket *avpkt)
{
    struct BitpackedContext *bc = avctx->priv_data;
    uint64_t frame_size = (uint64_t)avctx->width * (uint64_t)avctx->height * 20;
    uint64_t packet_size = (uint64_t)avpkt->size * 8;
    const uint8_t *src;
    uint16_t *y, *u, *v;
    const int bulk = avctx->width & ~7;
    int ret, i;

    ret = ff_thread_get_buffer(avctx, frame, 0);
    if (ret < 0)
        return ret;

    if (frame_size > packet_size)
        return AVERROR_INVALIDDATA;

    if (avctx->width % 2)
        return AVERROR_PATCHWELCOME;

    src = avpkt->data;
    for (i = 0; i < avctx->height; i++) {
        y = (uint16_t*)(frame->data[0] + i * frame->linesize[0]);
        u = (uint16_t*)(frame->data[1] + i * frame->linesize[1]);
        v = (uint16_t*)(frame->data[2] + i * frame->linesize[2]);

        if (bulk) {
            bc->unpack_yuv422p10(src, y, u, v, bulk);
            src += bulk * 5 / 2;
            y   += bulk;
            u   += bulk / 2;
            v   += bulk / 2;
        }

        bitpacked_unpack_yuv422p10_c(src, y, u, v, avctx->width - bulk);
        src += (avctx->width - bulk) * 5 / 2;
    }

    return 0;
}

static av_cold int bitpacked_init_decoder(AVCodecContext *avctx)
{
    struct BitpackedContext *bc = avctx->priv_data;

    if (!avctx->codec_tag || !avctx->width || !avctx->height)
        return AVERROR_INVALIDDATA;

    ff_bitpackeddec_init(bc);

    if (avctx->codec_tag == MKTAG('U', 'Y', 'V', 'Y')) {
        if (avctx->bits_per_coded_sample == 16 &&
            avctx->pix_fmt == AV_PIX_FMT_UYVY422)
            bc->decode = bitpacked_decode_uyvy422;
        else if (avctx->bits_per_coded_sample == 20 &&
                 avctx->pix_fmt == AV_PIX_FMT_YUV422P10)
            bc->decode = bitpacked_decode_yuv422p10;
        else
            return AVERROR_INVALIDDATA;
    } else {
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int bitpacked_decode(AVCodecContext *avctx, AVFrame *frame,
                            int *got_frame, AVPacket *avpkt)
{
    struct BitpackedContext *bc = avctx->priv_data;
    int buf_size = avpkt->size;
    int res;

    res = bc->decode(avctx, frame, avpkt);
    if (res)
        return res;

    *got_frame = 1;
    return buf_size;

}

const FFCodec ff_bitpacked_decoder = {
    .p.name          = "bitpacked",
    CODEC_LONG_NAME("Bitpacked"),
    .p.type          = AVMEDIA_TYPE_VIDEO,
    .p.id            = AV_CODEC_ID_BITPACKED,
    .p.capabilities  = AV_CODEC_CAP_FRAME_THREADS,
    .priv_data_size        = sizeof(struct BitpackedContext),
    .init = bitpacked_init_decoder,
    FF_CODEC_DECODE_CB(bitpacked_decode),
    .codec_tags     = (const uint32_t []){
        MKTAG('U', 'Y', 'V', 'Y'),
        FF_CODEC_TAGS_END,
    },
};
