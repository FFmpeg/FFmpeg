/*
 * Intel MediaSDK QSV based VC-1 video decoder
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

#include "libavutil/common.h"
#include "libavutil/fifo.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "qsvdec.h"

typedef struct QSVVC1Context {
    AVClass *class;
    QSVContext qsv;
} QSVVC1Context;


static av_cold int qsv_decode_close(AVCodecContext *avctx)
{
    QSVVC1Context *s = avctx->priv_data;

    ff_qsv_decode_close(&s->qsv);

    return 0;
}

static int qsv_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_frame, AVPacket *avpkt)
{
    QSVVC1Context *s = avctx->priv_data;
    AVFrame *frame    = data;

    return ff_qsv_decode(avctx, &s->qsv, frame, got_frame, avpkt);
}

static void qsv_decode_flush(AVCodecContext *avctx)
{
    QSVVC1Context *s = avctx->priv_data;
    ff_qsv_decode_reset(avctx, &s->qsv);
}

AVHWAccel ff_vc1_qsv_hwaccel = {
    .name           = "vc1_qsv",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VC1,
    .pix_fmt        = AV_PIX_FMT_QSV,
};

#define OFFSET(x) offsetof(QSVVC1Context, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "async_depth", "Internal parallelization depth, the higher the value the higher the latency.", OFFSET(qsv.async_depth), AV_OPT_TYPE_INT, { .i64 = ASYNC_DEPTH_DEFAULT }, 0, INT_MAX, VD },
    { NULL },
};

static const AVClass class = {
    .class_name = "vc1_qsv",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_vc1_qsv_decoder = {
    .name           = "vc1_qsv",
    .long_name      = NULL_IF_CONFIG_SMALL("VC-1 video (Intel Quick Sync Video acceleration)"),
    .priv_data_size = sizeof(QSVVC1Context),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VC1,
    .init           = NULL,
    .decode         = qsv_decode_frame,
    .flush          = qsv_decode_flush,
    .close          = qsv_decode_close,
    .capabilities   = AV_CODEC_CAP_DELAY,
    .priv_class     = &class,
    .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_NV12,
                                                    AV_PIX_FMT_QSV,
                                                    AV_PIX_FMT_NONE },
};
