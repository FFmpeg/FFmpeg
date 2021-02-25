/*
 * Copyright (c) 2019 Paul B Mahol
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

#include "libavutil/intreadwrite.h"

#include "avcodec.h"
#include "internal.h"

typedef struct IMM5Context {
    AVCodecContext *h264_avctx;   // wrapper context for H264
    AVCodecContext *hevc_avctx;   // wrapper context for HEVC
} IMM5Context;

static const struct IMM5_unit {
    uint8_t bits[14];
    uint8_t len;
} IMM5_units[14] = {
    { { 0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x80, 0x1E, 0xF4, 0x0B, 0x0F, 0x88 }, 12 },
    { { 0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x80, 0x1E, 0xF4, 0x05, 0x83, 0xE2 }, 12 },
    { { 0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x80, 0x1E, 0xF4, 0x05, 0x81, 0xE8, 0x80 }, 13 },
    { { 0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x80, 0x1E, 0xF4, 0x0B, 0x04, 0xA2 }, 12 },
    { { 0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x80, 0x1E, 0xF4, 0x05, 0x81, 0x28, 0x80 }, 13 },
    { { 0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x80, 0x1E, 0xF4, 0x05, 0x80, 0x92, 0x20 }, 13 },
    { { 0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1E, 0x9A, 0x74, 0x0B, 0x0F, 0xC8 }, 13 },
    { { 0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1E, 0x9A, 0x74, 0x05, 0x83, 0xF2 }, 13 },
    { { 0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1E, 0x9A, 0x74, 0x05, 0x81, 0xEC, 0x80 }, 14 },
    { { 0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1E, 0x9A, 0x74, 0x0B, 0x04, 0xB2 }, 13 },
    { { 0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1E, 0x9A, 0x74, 0x05, 0x81, 0x2C, 0x80 }, 14 },
    { { 0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1E, 0x9A, 0x74, 0x05, 0x80, 0x93, 0x20 }, 14 },
    { { 0x00, 0x00, 0x00, 0x01, 0x68, 0xDE, 0x3C, 0x80 }, 8 },
    { { 0x00, 0x00, 0x00, 0x01, 0x68, 0xCE, 0x32, 0x28 }, 8 },
};

static av_cold int imm5_init(AVCodecContext *avctx)
{
    IMM5Context *ctx = avctx->priv_data;
    const AVCodec *codec;
    int ret;

    codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec)
        return AVERROR_BUG;
    ctx->h264_avctx = avcodec_alloc_context3(codec);
    if (!ctx->h264_avctx)
        return AVERROR(ENOMEM);
    ctx->h264_avctx->thread_count = 1;
    ctx->h264_avctx->flags        = avctx->flags;
    ctx->h264_avctx->flags2       = avctx->flags2;
    ret = avcodec_open2(ctx->h264_avctx, codec, NULL);
    if (ret < 0)
        return ret;

    codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
    if (!codec)
        return AVERROR_BUG;
    ctx->hevc_avctx = avcodec_alloc_context3(codec);
    if (!ctx->hevc_avctx)
        return AVERROR(ENOMEM);
    ctx->hevc_avctx->thread_count = 1;
    ctx->hevc_avctx->flags        = avctx->flags;
    ctx->hevc_avctx->flags2       = avctx->flags2;
    ret = avcodec_open2(ctx->hevc_avctx, codec, NULL);
    if (ret < 0)
        return ret;

    return 0;
}

static int imm5_decode_frame(AVCodecContext *avctx, void *data,
                             int *got_frame, AVPacket *avpkt)
{
    IMM5Context *ctx = avctx->priv_data;
    AVFrame *frame = data;
    AVCodecContext *codec_avctx = ctx->h264_avctx;
    int ret;

    if (avpkt->size > 24 && avpkt->data[8] <= 1 && AV_RL32(avpkt->data + 4) + 24ULL <= avpkt->size) {
        int codec_type = avpkt->data[1];
        int index = avpkt->data[10];
        int new_size = AV_RL32(avpkt->data + 4);
        int offset, off;

        if (codec_type == 0xA) {
            codec_avctx = ctx->hevc_avctx;
        } else if (index == 17) {
            index = 4;
        } else if (index == 18) {
            index = 5;
        }

        if (index >= 1 && index <= 12) {
            ret = av_packet_make_writable(avpkt);
            if (ret < 0)
                return ret;

            index -= 1;
            off = offset = IMM5_units[index].len;
            if (codec_type == 2) {
                offset += IMM5_units[12].len;
            } else {
                offset += IMM5_units[13].len;
            }

            avpkt->data += 24 - offset;
            avpkt->size = new_size + offset;

            memcpy(avpkt->data, IMM5_units[index].bits, IMM5_units[index].len);
            if (codec_type == 2) {
                memcpy(avpkt->data + off, IMM5_units[12].bits, IMM5_units[12].len);
            } else {
                memcpy(avpkt->data + off, IMM5_units[13].bits, IMM5_units[13].len);
            }
        } else {
            avpkt->data += 24;
            avpkt->size -= 24;
        }
    }

    ret = avcodec_send_packet(codec_avctx, avpkt);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error submitting a packet for decoding\n");
        return ret;
    }

    ret = avcodec_receive_frame(codec_avctx, frame);
    if (ret < 0)
        return ret;

    avctx->pix_fmt      = codec_avctx->pix_fmt;
    avctx->coded_width  = codec_avctx->coded_width;
    avctx->coded_height = codec_avctx->coded_height;
    avctx->width        = codec_avctx->width;
    avctx->height       = codec_avctx->height;
    avctx->bit_rate     = codec_avctx->bit_rate;
    avctx->colorspace   = codec_avctx->colorspace;
    avctx->color_range  = codec_avctx->color_range;
    avctx->color_trc    = codec_avctx->color_trc;
    avctx->color_primaries = codec_avctx->color_primaries;
    avctx->chroma_sample_location = codec_avctx->chroma_sample_location;

    *got_frame = 1;

    return avpkt->size;
}

static void imm5_flush(AVCodecContext *avctx)
{
    IMM5Context *ctx = avctx->priv_data;

    avcodec_flush_buffers(ctx->h264_avctx);
    avcodec_flush_buffers(ctx->hevc_avctx);
}

static av_cold int imm5_close(AVCodecContext *avctx)
{
    IMM5Context *ctx = avctx->priv_data;

    avcodec_free_context(&ctx->h264_avctx);
    avcodec_free_context(&ctx->hevc_avctx);

    return 0;
}

const AVCodec ff_imm5_decoder = {
    .name           = "imm5",
    .long_name      = NULL_IF_CONFIG_SMALL("Infinity IMM5"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_IMM5,
    .init           = imm5_init,
    .decode         = imm5_decode_frame,
    .close          = imm5_close,
    .flush          = imm5_flush,
    .priv_data_size = sizeof(IMM5Context),
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
};
