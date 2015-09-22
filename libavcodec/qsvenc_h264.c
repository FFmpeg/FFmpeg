/*
 * Intel MediaSDK QSV based H.264 enccoder
 *
 * copyright (c) 2013 Yukinori Yamazoe
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

#include "libavutil/opt.h"

#include "avcodec.h"
#include "internal.h"
#include "h264.h"
#include "qsv.h"
#include "qsv_internal.h"
#include "qsvenc.h"

typedef struct QSVH264EncContext {
    AVClass *class;
    QSVEncContext qsv;
} QSVH264EncContext;

static av_cold int qsv_enc_init(AVCodecContext *avctx)
{
    QSVH264EncContext *q = avctx->priv_data;

    return ff_qsv_enc_init(avctx, &q->qsv);
}

static int qsv_enc_frame(AVCodecContext *avctx, AVPacket *pkt,
                         const AVFrame *frame, int *got_packet)
{
    QSVH264EncContext *q = avctx->priv_data;

    return ff_qsv_encode(avctx, &q->qsv, pkt, frame, got_packet);
}

static av_cold int qsv_enc_close(AVCodecContext *avctx)
{
    QSVH264EncContext *q = avctx->priv_data;

    return ff_qsv_enc_close(avctx, &q->qsv);
}

#define OFFSET(x) offsetof(QSVH264EncContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "async_depth", "Maximum processing parallelism", OFFSET(qsv.async_depth), AV_OPT_TYPE_INT, { .i64 = ASYNC_DEPTH_DEFAULT }, 0, INT_MAX, VE },
    { "idr_interval", "Distance (in I-frames) between IDR frames", OFFSET(qsv.idr_interval), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, VE },
    { "avbr_accuracy",    "Accuracy of the AVBR ratecontrol",    OFFSET(qsv.avbr_accuracy),    AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, VE },
    { "avbr_convergence", "Convergence of the AVBR ratecontrol", OFFSET(qsv.avbr_convergence), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, VE },
    { "pic_timing_sei",    "Insert picture timing SEI with pic_struct_syntax element", OFFSET(qsv.pic_timing_sei), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 1, VE },

#if QSV_VERSION_ATLEAST(1,7)
    { "look_ahead",       "Use VBR algorithm with look ahead",    OFFSET(qsv.look_ahead),       AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 1, VE },
    { "look_ahead_depth", "Depth of look ahead in number frames", OFFSET(qsv.look_ahead_depth), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 100, VE },
#endif

#if QSV_VERSION_ATLEAST(1,8)
    { "look_ahead_downsampling", NULL, OFFSET(qsv.look_ahead_downsampling), AV_OPT_TYPE_INT, { .i64 = MFX_LOOKAHEAD_DS_UNKNOWN }, MFX_LOOKAHEAD_DS_UNKNOWN, MFX_LOOKAHEAD_DS_2x, VE, "look_ahead_downsampling" },
    { "unknown"                , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_LOOKAHEAD_DS_UNKNOWN }, INT_MIN, INT_MAX,     VE, "look_ahead_downsampling" },
    { "off"                    , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_LOOKAHEAD_DS_OFF     }, INT_MIN, INT_MAX,     VE, "look_ahead_downsampling" },
    { "2x"                     , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_LOOKAHEAD_DS_2x      }, INT_MIN, INT_MAX,     VE, "look_ahead_downsampling" },
#endif

    { "profile", NULL, OFFSET(qsv.profile), AV_OPT_TYPE_INT, { .i64 = MFX_PROFILE_UNKNOWN }, 0, INT_MAX, VE, "profile" },
    { "unknown" , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_UNKNOWN      }, INT_MIN, INT_MAX,     VE, "profile" },
    { "baseline", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_AVC_BASELINE }, INT_MIN, INT_MAX,     VE, "profile" },
    { "main"    , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_AVC_MAIN     }, INT_MIN, INT_MAX,     VE, "profile" },
    { "high"    , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_AVC_HIGH     }, INT_MIN, INT_MAX,     VE, "profile" },

    { "preset", NULL, OFFSET(qsv.preset), AV_OPT_TYPE_INT, { .i64 = MFX_TARGETUSAGE_BALANCED }, MFX_TARGETUSAGE_BEST_QUALITY, MFX_TARGETUSAGE_BEST_SPEED,   VE, "preset" },
    { "veryfast",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_TARGETUSAGE_BEST_SPEED  },   INT_MIN, INT_MAX, VE, "preset" },
    { "faster",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_TARGETUSAGE_6  },            INT_MIN, INT_MAX, VE, "preset" },
    { "fast",        NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_TARGETUSAGE_5  },            INT_MIN, INT_MAX, VE, "preset" },
    { "medium",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_TARGETUSAGE_BALANCED  },     INT_MIN, INT_MAX, VE, "preset" },
    { "slow",        NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_TARGETUSAGE_3  },            INT_MIN, INT_MAX, VE, "preset" },
    { "slower",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_TARGETUSAGE_2  },            INT_MIN, INT_MAX, VE, "preset" },
    { "veryslow",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_TARGETUSAGE_BEST_QUALITY  }, INT_MIN, INT_MAX, VE, "preset" },

    { NULL },
};

static const AVClass class = {
    .class_name = "h264_qsv encoder",
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
    { "coder",     "ac"    },

    { "flags",     "+cgop" },
    { NULL },
};

AVCodec ff_h264_qsv_encoder = {
    .name           = "h264_qsv",
    .long_name      = NULL_IF_CONFIG_SMALL("H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10 (Intel Quick Sync Video acceleration)"),
    .priv_data_size = sizeof(QSVH264EncContext),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .init           = qsv_enc_init,
    .encode2        = qsv_enc_frame,
    .close          = qsv_enc_close,
    .capabilities   = AV_CODEC_CAP_DELAY,
    .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_NV12,
                                                    AV_PIX_FMT_QSV,
                                                    AV_PIX_FMT_NONE },
    .priv_class     = &class,
    .defaults       = qsv_enc_defaults,
};
