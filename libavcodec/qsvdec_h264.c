/*
 * Intel MediaSDK QSV based H.264 decoder
 *
 * copyright (c) 2013 Luca Barbato
 * copyright (c) 2015 Anton Khirnov
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


#include <stdint.h>
#include <string.h>

#include <mfx/mfxvideo.h>

#include "libavutil/common.h"
#include "libavutil/fifo.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "internal.h"
#include "qsv_internal.h"
#include "qsvdec.h"
#include "qsv.h"

typedef struct QSVH264Context {
    AVClass *class;
    QSVContext qsv;

    // the internal parser and codec context for parsing the data
    AVCodecParserContext *parser;
    AVCodecContext *avctx_internal;
    enum AVPixelFormat orig_pix_fmt;

    // the filter for converting to Annex B
    AVBitStreamFilterContext *bsf;

    AVFifoBuffer *packet_fifo;

    AVPacket input_ref;
    AVPacket pkt_filtered;
    uint8_t *filtered_data;
} QSVH264Context;

static void qsv_clear_buffers(QSVH264Context *s)
{
    AVPacket pkt;
    while (av_fifo_size(s->packet_fifo) >= sizeof(pkt)) {
        av_fifo_generic_read(s->packet_fifo, &pkt, sizeof(pkt), NULL);
        av_packet_unref(&pkt);
    }

    if (s->filtered_data != s->input_ref.data)
        av_freep(&s->filtered_data);
    s->filtered_data = NULL;
    av_packet_unref(&s->input_ref);
}

static av_cold int qsv_decode_close(AVCodecContext *avctx)
{
    QSVH264Context *s = avctx->priv_data;

    ff_qsv_decode_close(&s->qsv);

    qsv_clear_buffers(s);

    av_fifo_free(s->packet_fifo);

    av_bitstream_filter_close(s->bsf);
    av_parser_close(s->parser);
    avcodec_free_context(&s->avctx_internal);

    return 0;
}

static av_cold int qsv_decode_init(AVCodecContext *avctx)
{
    QSVH264Context *s = avctx->priv_data;
    int ret;

    s->orig_pix_fmt = AV_PIX_FMT_NONE;

    s->packet_fifo = av_fifo_alloc(sizeof(AVPacket));
    if (!s->packet_fifo) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    s->bsf = av_bitstream_filter_init("h264_mp4toannexb");
    if (!s->bsf) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    s->avctx_internal = avcodec_alloc_context3(NULL);
    if (!s->avctx_internal) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if (avctx->extradata) {
        s->avctx_internal->extradata = av_mallocz(avctx->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
        if (!s->avctx_internal->extradata) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        memcpy(s->avctx_internal->extradata, avctx->extradata,
               avctx->extradata_size);
        s->avctx_internal->extradata_size = avctx->extradata_size;
    }

    s->parser = av_parser_init(AV_CODEC_ID_H264);
    if (!s->parser) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    s->parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;

    s->qsv.iopattern = MFX_IOPATTERN_OUT_SYSTEM_MEMORY;

    return 0;
fail:
    qsv_decode_close(avctx);
    return ret;
}

static int qsv_process_data(AVCodecContext *avctx, AVFrame *frame,
                            int *got_frame, AVPacket *pkt)
{
    QSVH264Context *s = avctx->priv_data;
    uint8_t *dummy_data;
    int dummy_size;
    int ret;

    /* we assume the packets are already split properly and want
     * just the codec parameters here */
    av_parser_parse2(s->parser, s->avctx_internal,
                     &dummy_data, &dummy_size,
                     pkt->data, pkt->size, pkt->pts, pkt->dts,
                     pkt->pos);

    /* TODO: flush delayed frames on reinit */
    if (s->parser->format       != s->orig_pix_fmt    ||
        s->parser->coded_width  != avctx->coded_width ||
        s->parser->coded_height != avctx->coded_height) {
        mfxSession session = NULL;

        enum AVPixelFormat pix_fmts[3] = { AV_PIX_FMT_QSV,
                                           AV_PIX_FMT_NONE,
                                           AV_PIX_FMT_NONE };
        enum AVPixelFormat qsv_format;

        qsv_format = ff_qsv_map_pixfmt(s->parser->format);
        if (qsv_format < 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "Only 8-bit YUV420 streams are supported.\n");
            ret = AVERROR(ENOSYS);
            goto reinit_fail;
        }

        s->orig_pix_fmt     = s->parser->format;
        avctx->pix_fmt      = pix_fmts[1] = qsv_format;
        avctx->width        = s->parser->width;
        avctx->height       = s->parser->height;
        avctx->coded_width  = s->parser->coded_width;
        avctx->coded_height = s->parser->coded_height;
        avctx->level        = s->avctx_internal->level;
        avctx->profile      = s->avctx_internal->profile;

        ret = ff_get_format(avctx, pix_fmts);
        if (ret < 0)
            goto reinit_fail;

        avctx->pix_fmt = ret;

        if (avctx->hwaccel_context) {
            AVQSVContext *user_ctx = avctx->hwaccel_context;
            session               = user_ctx->session;
            s->qsv.iopattern      = user_ctx->iopattern;
            s->qsv.ext_buffers    = user_ctx->ext_buffers;
            s->qsv.nb_ext_buffers = user_ctx->nb_ext_buffers;
        }

        ret = ff_qsv_decode_init(avctx, &s->qsv, session);
        if (ret < 0)
            goto reinit_fail;
    }

    return ff_qsv_decode(avctx, &s->qsv, frame, got_frame, &s->pkt_filtered);

reinit_fail:
    s->orig_pix_fmt = s->parser->format = avctx->pix_fmt = AV_PIX_FMT_NONE;
    return ret;
}

static int qsv_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_frame, AVPacket *avpkt)
{
    QSVH264Context *s = avctx->priv_data;
    AVFrame *frame    = data;
    int ret;

    /* buffer the input packet */
    if (avpkt->size) {
        AVPacket input_ref = { 0 };

        if (av_fifo_space(s->packet_fifo) < sizeof(input_ref)) {
            ret = av_fifo_realloc2(s->packet_fifo,
                                   av_fifo_size(s->packet_fifo) + sizeof(input_ref));
            if (ret < 0)
                return ret;
        }

        ret = av_packet_ref(&input_ref, avpkt);
        if (ret < 0)
            return ret;
        av_fifo_generic_write(s->packet_fifo, &input_ref, sizeof(input_ref), NULL);
    }

    /* process buffered data */
    while (!*got_frame) {
        /* prepare the input data -- convert to Annex B if needed */
        if (s->pkt_filtered.size <= 0) {
            int size;

            /* no more data */
            if (av_fifo_size(s->packet_fifo) < sizeof(AVPacket))
                return avpkt->size ? avpkt->size : ff_qsv_decode(avctx, &s->qsv, frame, got_frame, avpkt);

            if (s->filtered_data != s->input_ref.data)
                av_freep(&s->filtered_data);
            s->filtered_data = NULL;
            av_packet_unref(&s->input_ref);

            av_fifo_generic_read(s->packet_fifo, &s->input_ref, sizeof(s->input_ref), NULL);
            ret = av_bitstream_filter_filter(s->bsf, avctx, NULL,
                                             &s->filtered_data, &size,
                                             s->input_ref.data, s->input_ref.size, 0);
            if (ret < 0) {
                s->filtered_data = s->input_ref.data;
                size             = s->input_ref.size;
            }
            s->pkt_filtered      = s->input_ref;
            s->pkt_filtered.data = s->filtered_data;
            s->pkt_filtered.size = size;
        }

        ret = qsv_process_data(avctx, frame, got_frame, &s->pkt_filtered);
        if (ret < 0)
            return ret;

        s->pkt_filtered.size -= ret;
        s->pkt_filtered.data += ret;
    }

    return avpkt->size;
}

static void qsv_decode_flush(AVCodecContext *avctx)
{
    QSVH264Context *s = avctx->priv_data;

    qsv_clear_buffers(s);
    s->orig_pix_fmt = AV_PIX_FMT_NONE;
}

AVHWAccel ff_h264_qsv_hwaccel = {
    .name           = "h264_qsv",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .pix_fmt        = AV_PIX_FMT_QSV,
};

#define OFFSET(x) offsetof(QSVH264Context, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "async_depth", "Internal parallelization depth, the higher the value the higher the latency.", OFFSET(qsv.async_depth), AV_OPT_TYPE_INT, { .i64 = ASYNC_DEPTH_DEFAULT }, 0, INT_MAX, VD },
    { NULL },
};

static const AVClass class = {
    .class_name = "h264_qsv",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_h264_qsv_decoder = {
    .name           = "h264_qsv",
    .long_name      = NULL_IF_CONFIG_SMALL("H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10 (Intel Quick Sync Video acceleration)"),
    .priv_data_size = sizeof(QSVH264Context),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .init           = qsv_decode_init,
    .decode         = qsv_decode_frame,
    .flush          = qsv_decode_flush,
    .close          = qsv_decode_close,
    .capabilities   = CODEC_CAP_DELAY,
    .priv_class     = &class,
};
