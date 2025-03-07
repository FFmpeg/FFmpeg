/*
 * bitpacked encoder
 *
 * Copyright (c) 2021 Limin Wang
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
#include "codec_internal.h"
#include "encode.h"
#include "internal.h"
#include "put_bits.h"
#include "libavutil/pixdesc.h"

struct BitpackedContext {
    int (*encode)(AVCodecContext *avctx, AVPacket *pkt, const AVFrame *frame);
};

static int encode_yuv422p10(AVCodecContext *avctx, AVPacket *pkt, const AVFrame *frame)
{
    const int buf_size = avctx->height * avctx->width * avctx->bits_per_coded_sample / 8;
    int ret;
    uint8_t *dst;
    const uint16_t *y;
    const uint16_t *u;
    const uint16_t *v;
    PutBitContext pb;

    ret = ff_get_encode_buffer(avctx, pkt,  buf_size, 0);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error getting output packet.\n");
        return ret;
    }
    dst = pkt->data;

    init_put_bits(&pb, dst, buf_size);

    for (int i = 0; i < avctx->height; i++) {
        y = (uint16_t*)(frame->data[0] + i * frame->linesize[0]);
        u = (uint16_t*)(frame->data[1] + i * frame->linesize[1]);
        v = (uint16_t*)(frame->data[2] + i * frame->linesize[2]);

        for (int j = 0; j < avctx->width; j += 2) {
            /* u, y0, v, y1 */
            put_bits(&pb, 10, av_clip_uintp2(*u++, 10));
            put_bits(&pb, 10, av_clip_uintp2(*y++, 10));
            put_bits(&pb, 10, av_clip_uintp2(*v++, 10));
            put_bits(&pb, 10, av_clip_uintp2(*y++, 10));
        }
    }
    flush_put_bits(&pb);

    return 0;
}


static av_cold int encode_init(AVCodecContext *avctx)
{
    struct BitpackedContext *s = avctx->priv_data;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(avctx->pix_fmt);

    if (avctx->width & 1) {
        av_log(avctx, AV_LOG_ERROR, "bitpacked needs even width\n");
        return AVERROR(EINVAL);
    }

    avctx->bits_per_coded_sample = av_get_bits_per_pixel(desc);
    avctx->bit_rate = ff_guess_coded_bitrate(avctx);

    if (avctx->pix_fmt == AV_PIX_FMT_YUV422P10)
        s->encode = encode_yuv422p10;
    else
        return AVERROR(EINVAL);

    return 0;
}

static int encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                        const AVFrame *frame, int *got_packet)
{
    struct BitpackedContext *s = avctx->priv_data;
    int ret;

    ret = s->encode(avctx, pkt, frame);
    if (ret)
        return ret;

    *got_packet = 1;
    return 0;
}

const FFCodec ff_bitpacked_encoder = {
    .p.name         = "bitpacked",
    CODEC_LONG_NAME("Bitpacked"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_BITPACKED,
    .priv_data_size = sizeof(struct BitpackedContext),
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS |
                      AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .init           = encode_init,
    FF_CODEC_ENCODE_CB(encode_frame),
    CODEC_PIXFMTS(AV_PIX_FMT_YUV422P10),
};
