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

#include <mfx/mfxvideo.h>

#include "libavutil/common.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "internal.h"
#include "h264.h"
#include "qsv.h"
#include "qsv_internal.h"
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
    { NULL },
};

static const AVClass class = {
    .class_name = "mjpeg_qsv encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_mjpeg_qsv_encoder = {
    .name           = "mjpeg_qsv",
    .long_name      = NULL_IF_CONFIG_SMALL("MJPEG (Intel Quick Sync Video acceleration)"),
    .priv_data_size = sizeof(QSVMJPEGEncContext),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MJPEG,
    .init           = qsv_enc_init,
    .encode2        = qsv_enc_frame,
    .close          = qsv_enc_close,
    .capabilities   = AV_CODEC_CAP_DELAY,
    .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_NV12,
                                                    AV_PIX_FMT_QSV,
                                                    AV_PIX_FMT_NONE },
    .priv_class     = &class,
};
