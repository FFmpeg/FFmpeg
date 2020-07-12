/*
 * Intel MediaSDK QSV based MPEG-2 encoder
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
#include "qsv.h"
#include "qsv_internal.h"
#include "qsvenc.h"

typedef struct QSVMpeg2EncContext {
    AVClass *class;
    QSVEncContext qsv;
} QSVMpeg2EncContext;

static av_cold int qsv_enc_init(AVCodecContext *avctx)
{
    QSVMpeg2EncContext *q = avctx->priv_data;

    return ff_qsv_enc_init(avctx, &q->qsv);
}

static int qsv_enc_frame(AVCodecContext *avctx, AVPacket *pkt,
                         const AVFrame *frame, int *got_packet)
{
    QSVMpeg2EncContext *q = avctx->priv_data;

    return ff_qsv_encode(avctx, &q->qsv, pkt, frame, got_packet);
}

static av_cold int qsv_enc_close(AVCodecContext *avctx)
{
    QSVMpeg2EncContext *q = avctx->priv_data;

    return ff_qsv_enc_close(avctx, &q->qsv);
}

#define OFFSET(x) offsetof(QSVMpeg2EncContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    QSV_COMMON_OPTS

    { "profile", NULL, OFFSET(qsv.profile), AV_OPT_TYPE_INT, { .i64 = MFX_PROFILE_UNKNOWN }, 0, INT_MAX, VE, "profile" },
    { "unknown", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_UNKNOWN        }, INT_MIN, INT_MAX,     VE, "profile" },
    { "simple",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_MPEG2_SIMPLE   }, INT_MIN, INT_MAX,     VE, "profile" },
    { "main",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_MPEG2_MAIN     }, INT_MIN, INT_MAX,     VE, "profile" },
    { "high",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_MPEG2_HIGH     }, INT_MIN, INT_MAX,     VE, "profile" },

    { NULL },
};

static const AVClass class = {
    .class_name = "mpeg2_qsv encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVCodecDefault qsv_enc_defaults[] = {
    { "b",         "1M"    },
    { "refs",      "0"     },
    // same as the x264 default
    { "g",         "250"   },
    { "bf",        "3"     },
    { "trellis",   "-1"    },
    { "flags",     "+cgop" },
#if FF_API_PRIVATE_OPT
    { "b_strategy", "-1"   },
#endif
    { NULL },
};

AVCodec ff_mpeg2_qsv_encoder = {
    .name           = "mpeg2_qsv",
    .long_name      = NULL_IF_CONFIG_SMALL("MPEG-2 video (Intel Quick Sync Video acceleration)"),
    .priv_data_size = sizeof(QSVMpeg2EncContext),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MPEG2VIDEO,
    .init           = qsv_enc_init,
    .encode2        = qsv_enc_frame,
    .close          = qsv_enc_close,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HYBRID,
    .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_NV12,
                                                    AV_PIX_FMT_QSV,
                                                    AV_PIX_FMT_NONE },
    .priv_class     = &class,
    .defaults       = qsv_enc_defaults,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .wrapper_name   = "qsv",
    .hw_configs     = ff_qsv_enc_hw_configs,
};
