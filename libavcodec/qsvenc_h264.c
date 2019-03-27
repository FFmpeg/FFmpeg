/*
 * Intel MediaSDK QSV based H.264 encoder
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

#include "libavutil/common.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "internal.h"
#include "qsv.h"
#include "qsv_internal.h"
#include "qsvenc.h"

typedef struct QSVH264EncContext {
    AVClass *class;
    QSVEncContext qsv;
} QSVH264EncContext;

static int qsv_h264_set_encode_ctrl(AVCodecContext *avctx,
                                    const AVFrame *frame, mfxEncodeCtrl* enc_ctrl)
{
    QSVH264EncContext *qh264 = avctx->priv_data;
    QSVEncContext *q = &qh264->qsv;

    if (q->a53_cc && frame) {
        mfxPayload* payload;
        mfxU8* sei_data;
        size_t sei_size;
        int res;

        res = ff_alloc_a53_sei(frame, sizeof(mfxPayload) + 2, (void**)&payload, &sei_size);
        if (res < 0 || !payload)
            return res;

        sei_data = (mfxU8*)(payload + 1);
        // SEI header
        sei_data[0] = 4;
        sei_data[1] = (mfxU8)sei_size; // size of SEI data
        // SEI data filled in by ff_alloc_a53_sei

        payload->BufSize = sei_size + 2;
        payload->NumBit = payload->BufSize * 8;
        payload->Type = 4;
        payload->Data = sei_data;

        enc_ctrl->NumExtParam = 0;
        enc_ctrl->NumPayload = 1;
        enc_ctrl->Payload[0] = payload;
    }
    return 0;
}

static av_cold int qsv_enc_init(AVCodecContext *avctx)
{
    QSVH264EncContext *q = avctx->priv_data;

    q->qsv.set_encode_ctrl_cb = qsv_h264_set_encode_ctrl;
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
    QSV_COMMON_OPTS

    { "cavlc",          "Enable CAVLC",                           OFFSET(qsv.cavlc),          AV_OPT_TYPE_INT, { .i64 = 0 },   0,          1, VE },
#if QSV_HAVE_VCM
    { "vcm",      "Use the video conferencing mode ratecontrol",  OFFSET(qsv.vcm),      AV_OPT_TYPE_INT, { .i64 = 0  },  0, 1,         VE },
#endif
    { "idr_interval", "Distance (in I-frames) between IDR frames", OFFSET(qsv.idr_interval), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, VE },
    { "pic_timing_sei",    "Insert picture timing SEI with pic_struct_syntax element", OFFSET(qsv.pic_timing_sei), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 1, VE },
    { "single_sei_nal_unit",    "Put all the SEI messages into one NALU",        OFFSET(qsv.single_sei_nal_unit),     AV_OPT_TYPE_INT, { .i64 = -1 }, -1,          1, VE },
    { "max_dec_frame_buffering", "Maximum number of frames buffered in the DPB", OFFSET(qsv.max_dec_frame_buffering), AV_OPT_TYPE_INT, { .i64 = 0 },   0, UINT16_MAX, VE },

#if QSV_HAVE_LA
    { "look_ahead",       "Use VBR algorithm with look ahead",    OFFSET(qsv.look_ahead),       AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, VE },
    { "look_ahead_depth", "Depth of look ahead in number frames", OFFSET(qsv.look_ahead_depth), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 100, VE },
#endif
#if QSV_HAVE_LA_DS
    { "look_ahead_downsampling", "Downscaling factor for the frames saved for the lookahead analysis", OFFSET(qsv.look_ahead_downsampling),
                                          AV_OPT_TYPE_INT,   { .i64 = MFX_LOOKAHEAD_DS_UNKNOWN }, MFX_LOOKAHEAD_DS_UNKNOWN, MFX_LOOKAHEAD_DS_4x, VE, "look_ahead_downsampling" },
    { "unknown"                , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_LOOKAHEAD_DS_UNKNOWN }, INT_MIN, INT_MAX,     VE, "look_ahead_downsampling" },
    { "auto"                   , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_LOOKAHEAD_DS_UNKNOWN }, INT_MIN, INT_MAX,     VE, "look_ahead_downsampling" },
    { "off"                    , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_LOOKAHEAD_DS_OFF     }, INT_MIN, INT_MAX,     VE, "look_ahead_downsampling" },
    { "2x"                     , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_LOOKAHEAD_DS_2x      }, INT_MIN, INT_MAX,     VE, "look_ahead_downsampling" },
    { "4x"                     , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_LOOKAHEAD_DS_4x      }, INT_MIN, INT_MAX,     VE, "look_ahead_downsampling" },
#endif

    { "int_ref_type", "Intra refresh type",                                      OFFSET(qsv.int_ref_type),            AV_OPT_TYPE_INT, { .i64 = -1 }, -1, UINT16_MAX, VE, "int_ref_type" },
        { "none",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 0 }, .flags = VE, "int_ref_type" },
        { "vertical", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 1 }, .flags = VE, "int_ref_type" },
    { "int_ref_cycle_size", "Number of frames in the intra refresh cycle",       OFFSET(qsv.int_ref_cycle_size),      AV_OPT_TYPE_INT, { .i64 = -1 },               -1, UINT16_MAX, VE },
    { "int_ref_qp_delta",   "QP difference for the refresh MBs",                 OFFSET(qsv.int_ref_qp_delta),        AV_OPT_TYPE_INT, { .i64 = INT16_MIN }, INT16_MIN,  INT16_MAX, VE },
    { "recovery_point_sei", "Insert recovery point SEI messages",                OFFSET(qsv.recovery_point_sei),      AV_OPT_TYPE_INT, { .i64 = -1 },               -1,          1, VE },

    { "profile", NULL, OFFSET(qsv.profile), AV_OPT_TYPE_INT, { .i64 = MFX_PROFILE_UNKNOWN }, 0, INT_MAX, VE, "profile" },
    { "unknown" , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_UNKNOWN      }, INT_MIN, INT_MAX,     VE, "profile" },
    { "baseline", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_AVC_BASELINE }, INT_MIN, INT_MAX,     VE, "profile" },
    { "main"    , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_AVC_MAIN     }, INT_MIN, INT_MAX,     VE, "profile" },
    { "high"    , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_AVC_HIGH     }, INT_MIN, INT_MAX,     VE, "profile" },

    { "a53cc" , "Use A53 Closed Captions (if available)", OFFSET(qsv.a53_cc), AV_OPT_TYPE_INT, {.i64 = 1}, 0, 1, VE},

    { "aud", "Insert the Access Unit Delimiter NAL", OFFSET(qsv.aud), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, VE},

#if QSV_HAVE_MF
    { "mfmode", "Multi-Frame Mode", OFFSET(qsv.mfmode), AV_OPT_TYPE_INT, { .i64 = MFX_MF_AUTO }, MFX_MF_DEFAULT, MFX_MF_AUTO, VE, "mfmode"},
    { "off"    , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_MF_DISABLED }, INT_MIN, INT_MAX,     VE, "mfmode" },
    { "auto"   , NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_MF_AUTO     }, INT_MIN, INT_MAX,     VE, "mfmode" },
#endif

#if QSV_HAVE_VDENC
    { "low_power", "enable low power mode(experimental: many limitations by mfx version, BRC modes, etc.)", OFFSET(qsv.low_power), AV_OPT_TYPE_BOOL, { .i64 =  0 }, 0, 1, VE},
#endif

    { "repeat_pps", "repeat pps for every frame", OFFSET(qsv.repeat_pps), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },

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
    { "qmin",      "-1"    },
    { "qmax",      "-1"    },
#if FF_API_CODER_TYPE
    { "coder",     "-1"    },
#endif
    { "trellis",   "-1"    },
    { "flags",     "+cgop" },
#if FF_API_PRIVATE_OPT
    { "b_strategy", "-1"   },
#endif
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
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HYBRID,
    .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_NV12,
                                                    AV_PIX_FMT_P010,
                                                    AV_PIX_FMT_QSV,
                                                    AV_PIX_FMT_NONE },
    .priv_class     = &class,
    .defaults       = qsv_enc_defaults,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .wrapper_name   = "qsv",
};
