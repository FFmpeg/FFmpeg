/*
 * Intel MediaSDK QSV based MJPEG encoder
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
#include <sys/types.h>

#include <mfxvideo.h>

#include "libavutil/common.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "qsv.h"
#include "qsvenc.h"

typedef struct QSVMJPEGEncContext {
    AVClass *class;
    QSVEncContext qsv;
} QSVMJPEGEncContext;

static av_cold int qsv_enc_init(AVCodecContext *avctx)
{
    QSVMJPEGEncContext *q = avctx->priv_data;

    return ff_qsv_enc_init(avctx, &q->qsv);
}

static int qsv_enc_frame(AVCodecContext *avctx, AVPacket *pkt,
                         const AVFrame *frame, int *got_packet)
{
    QSVMJPEGEncContext *q = avctx->priv_data;

    return ff_qsv_encode(avctx, &q->qsv, pkt, frame, got_packet);
}

static av_cold int qsv_enc_close(AVCodecContext *avctx)
{
    QSVMJPEGEncContext *q = avctx->priv_data;

    return ff_qsv_enc_close(avctx, &q->qsv);
}

#define OFFSET(x) offsetof(QSVMJPEGEncContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "async_depth", "Maximum processing parallelism", OFFSET(qsv.async_depth), AV_OPT_TYPE_INT, { .i64 = ASYNC_DEPTH_DEFAULT }, 1, INT_MAX, VE },
    { NULL },
};

static const AVClass class = {
    .class_name = "mjpeg_qsv encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const FFCodecDefault qsv_enc_defaults[] = {
    { "global_quality",  "80" },
    { NULL },
};

const FFCodec ff_mjpeg_qsv_encoder = {
    .p.name         = "mjpeg_qsv",
    CODEC_LONG_NAME("MJPEG (Intel Quick Sync Video acceleration)"),
    .priv_data_size = sizeof(QSVMJPEGEncContext),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_MJPEG,
    .init           = qsv_enc_init,
    FF_CODEC_ENCODE_CB(qsv_enc_frame),
    .close          = qsv_enc_close,
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HYBRID,
    .p.pix_fmts     = (const enum AVPixelFormat[]){ AV_PIX_FMT_NV12,
                                                    AV_PIX_FMT_YUYV422,
                                                    AV_PIX_FMT_BGRA,
                                                    AV_PIX_FMT_QSV,
                                                    AV_PIX_FMT_NONE },
    .color_ranges   = AVCOL_RANGE_MPEG | AVCOL_RANGE_JPEG,
    .p.priv_class   = &class,
    .defaults       = qsv_enc_defaults,
    .p.wrapper_name = "qsv",
    .hw_configs     = ff_qsv_enc_hw_configs,
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE,
};
