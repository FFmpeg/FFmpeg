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

#include <mfxvideo.h>

#include "libavutil/common.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "qsv.h"
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
    QSV_OPTION_RDO

    { "profile", NULL, OFFSET(qsv.profile), AV_OPT_TYPE_INT, { .i64 = MFX_PROFILE_UNKNOWN }, 0, INT_MAX, VE, .unit = "profile" },
    { "unknown", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_UNKNOWN        }, INT_MIN, INT_MAX,     VE, .unit = "profile" },
    { "simple",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_MPEG2_SIMPLE   }, INT_MIN, INT_MAX,     VE, .unit = "profile" },
    { "main",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_MPEG2_MAIN     }, INT_MIN, INT_MAX,     VE, .unit = "profile" },
    { "high",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_MPEG2_HIGH     }, INT_MIN, INT_MAX,     VE, .unit = "profile" },

    { NULL },
};

static const AVClass class = {
    .class_name = "mpeg2_qsv encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const FFCodecDefault qsv_enc_defaults[] = {
    { "b",         "0"     },
    { "refs",      "0"     },
    // same as the x264 default
    { "g",         "250"   },
    { "bf",        "3"     },
    { "trellis",   "-1"    },
    { "flags",     "+cgop" },
    { NULL },
};

const FFCodec ff_mpeg2_qsv_encoder = {
    .p.name         = "mpeg2_qsv",
    CODEC_LONG_NAME("MPEG-2 video (Intel Quick Sync Video acceleration)"),
    .priv_data_size = sizeof(QSVMpeg2EncContext),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_MPEG2VIDEO,
    .init           = qsv_enc_init,
    FF_CODEC_ENCODE_CB(qsv_enc_frame),
    .close          = qsv_enc_close,
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HYBRID,
    .p.pix_fmts     = (const enum AVPixelFormat[]){ AV_PIX_FMT_NV12,
                                                    AV_PIX_FMT_QSV,
                                                    AV_PIX_FMT_NONE },
    .color_ranges   = AVCOL_RANGE_MPEG,
    .p.priv_class   = &class,
    .defaults       = qsv_enc_defaults,
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
    .p.wrapper_name = "qsv",
    .hw_configs     = ff_qsv_enc_hw_configs,
};
