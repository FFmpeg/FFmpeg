/*
 * Infinity IMM6 video decoder
 *
 * Copyright (c) 2026 Tom Needham
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

#include "libavutil/attributes.h"
#include "libavutil/attributes_internal.h"
#include "libavutil/intreadwrite.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"

typedef struct IMM6Context {
    AVCodecContext *h264_avctx;
    int draining;
} IMM6Context;

static av_cold int imm6_init(AVCodecContext *avctx)
{
    IMM6Context *s = avctx->priv_data;
    int ret;

    EXTERN const FFCodec ff_h264_decoder;

    s->h264_avctx = avcodec_alloc_context3(&ff_h264_decoder.p);
    if (!s->h264_avctx)
        return AVERROR(ENOMEM);

    /* The wrapper delegates threading to the nested H.264 decoder. */
    s->h264_avctx->thread_count     = avctx->thread_count;
    s->h264_avctx->thread_type      = avctx->thread_type;
    s->h264_avctx->flags            = avctx->flags;
    s->h264_avctx->flags2           = avctx->flags2;
    s->h264_avctx->max_pixels       = avctx->max_pixels;
    s->h264_avctx->pkt_timebase     = avctx->pkt_timebase;
    s->h264_avctx->skip_frame       = avctx->skip_frame;
    s->h264_avctx->skip_loop_filter = avctx->skip_loop_filter;
    s->h264_avctx->apply_cropping   = 0;

    ret = avcodec_open2(s->h264_avctx, NULL, NULL);
    if (ret < 0)
        return ret;

    return 0;
}

static int imm6_send_packet(AVCodecContext *avctx, AVPacket *pkt)
{
    IMM6Context *s = avctx->priv_data;
    AVPacket h264_pkt;
    uint32_t payload_size;
    int ret;

    if (pkt->size < 24) {
        av_log(avctx, AV_LOG_ERROR,
               "Packet too small for IMM6 header: %d bytes\n", pkt->size);
        return AVERROR_INVALIDDATA;
    }

    if (pkt->data[8] > 1) {
        av_log(avctx, AV_LOG_ERROR,
               "Invalid IMM6 mode %u\n", pkt->data[8]);
        return AVERROR_INVALIDDATA;
    }

    payload_size = AV_RL32(pkt->data + 4);
    if (payload_size > (unsigned)pkt->size - 24) {
        av_log(avctx, AV_LOG_ERROR,
               "Invalid IMM6 payload size %u in %d-byte packet\n",
               payload_size, pkt->size);
        return AVERROR_INVALIDDATA;
    }

    h264_pkt       = *pkt;
    h264_pkt.data += 24;
    h264_pkt.size  = payload_size;

    s->h264_avctx->skip_frame       = avctx->skip_frame;
    s->h264_avctx->skip_loop_filter = avctx->skip_loop_filter;

    ret = avcodec_send_packet(s->h264_avctx, &h264_pkt);
    if (ret < 0 && ret != AVERROR(EAGAIN))
        av_log(avctx, AV_LOG_ERROR,
               "Error submitting IMM6 H.264 payload for decoding\n");

    return ret;
}

static void imm6_update_context(AVCodecContext *avctx)
{
    IMM6Context *s = avctx->priv_data;
    AVCodecContext *h264 = s->h264_avctx;

    avctx->pix_fmt                = h264->pix_fmt;
    avctx->coded_width            = h264->coded_width;
    avctx->coded_height           = h264->coded_height;
    avctx->width                  = h264->width;
    avctx->height                 = h264->height;
    avctx->delay                  = h264->delay;
    avctx->colorspace             = h264->colorspace;
    avctx->color_range            = h264->color_range;
    avctx->color_trc              = h264->color_trc;
    avctx->color_primaries        = h264->color_primaries;
    avctx->chroma_sample_location = h264->chroma_sample_location;
    avctx->sample_aspect_ratio    = h264->sample_aspect_ratio;
    avctx->has_b_frames           = h264->has_b_frames;
}

static int imm6_receive_frame(AVCodecContext *avctx, AVFrame *frame)
{
    IMM6Context *s = avctx->priv_data;
    AVPacket pkt = { 0 };
    int ret;

    for (;;) {
        ret = avcodec_receive_frame(s->h264_avctx, frame);
        if (ret >= 0) {
            imm6_update_context(avctx);
            return 0;
        }
        if (ret == AVERROR_EOF)
            return ret;
        if (ret != AVERROR(EAGAIN))
            return ret;

        if (s->draining)
            return AVERROR(EAGAIN);

        ret = ff_decode_get_packet(avctx, &pkt);
        if (ret == AVERROR(EAGAIN))
            return ret;
        if (ret == AVERROR_EOF) {
            s->draining = 1;
            ret = avcodec_send_packet(s->h264_avctx, NULL);
            if (ret < 0 && ret != AVERROR_EOF && ret != AVERROR(EAGAIN))
                return ret;
            continue;
        }
        if (ret < 0)
            return ret;

        ret = imm6_send_packet(avctx, &pkt);
        av_packet_unref(&pkt);
        if (ret < 0)
            return ret;
    }
}

static av_cold void imm6_flush(AVCodecContext *avctx)
{
    IMM6Context *s = avctx->priv_data;

    avcodec_flush_buffers(s->h264_avctx);
    s->draining = 0;
}

static av_cold int imm6_close(AVCodecContext *avctx)
{
    IMM6Context *s = avctx->priv_data;

    avcodec_free_context(&s->h264_avctx);

    return 0;
}

const FFCodec ff_imm6_decoder = {
    .p.name         = "imm6",
    CODEC_LONG_NAME("Infinity IMM6"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_IMM6,
    .init           = imm6_init,
    FF_CODEC_RECEIVE_FRAME_CB(imm6_receive_frame),
    .close          = imm6_close,
    .flush          = imm6_flush,
    .priv_data_size = sizeof(IMM6Context),
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_OTHER_THREADS,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_AUTO_THREADS,
};
