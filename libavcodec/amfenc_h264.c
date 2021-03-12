/*
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


#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "amfenc.h"
#include "internal.h"

#define OFFSET(x) offsetof(AmfContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM

static const AVOption options[] = {
    // Static
    /// Usage
    { "usage",          "Encoder Usage",        OFFSET(usage),  AV_OPT_TYPE_INT,   { .i64 = AMF_VIDEO_ENCODER_USAGE_TRANSCONDING      }, AMF_VIDEO_ENCODER_USAGE_TRANSCONDING, AMF_VIDEO_ENCODER_USAGE_WEBCAM, VE, "usage" },
    { "transcoding",    "Generic Transcoding",  0,              AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_USAGE_TRANSCONDING      }, 0, 0, VE, "usage" },
    { "ultralowlatency","",                     0,              AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_USAGE_ULTRA_LOW_LATENCY }, 0, 0, VE, "usage" },
    { "lowlatency",     "",                     0,              AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_USAGE_LOW_LATENCY       }, 0, 0, VE, "usage" },
    { "webcam",         "Webcam",               0,              AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_USAGE_WEBCAM            }, 0, 0, VE, "usage" },

    /// Profile,
    { "profile",        "Profile",              OFFSET(profile),AV_OPT_TYPE_INT,   { .i64 = AMF_VIDEO_ENCODER_PROFILE_MAIN                 }, AMF_VIDEO_ENCODER_PROFILE_BASELINE, AMF_VIDEO_ENCODER_PROFILE_CONSTRAINED_HIGH, VE, "profile" },
    { "main",           "",                     0,              AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_PROFILE_MAIN                 }, 0, 0, VE, "profile" },
    { "high",           "",                     0,              AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_PROFILE_HIGH                 }, 0, 0, VE, "profile" },
    { "constrained_baseline", "",               0,              AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_PROFILE_CONSTRAINED_BASELINE }, 0, 0, VE, "profile" },
    { "constrained_high",     "",               0,              AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_PROFILE_CONSTRAINED_HIGH     }, 0, 0, VE, "profile" },

    /// Profile Level
    { "level",          "Profile Level",        OFFSET(level),  AV_OPT_TYPE_INT,   { .i64 = 0  }, 0, 62, VE, "level" },
    { "auto",           "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 0  }, 0, 0,  VE, "level" },
    { "1.0",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 10 }, 0, 0,  VE, "level" },
    { "1.1",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 11 }, 0, 0,  VE, "level" },
    { "1.2",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 12 }, 0, 0,  VE, "level" },
    { "1.3",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 13 }, 0, 0,  VE, "level" },
    { "2.0",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 20 }, 0, 0,  VE, "level" },
    { "2.1",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 21 }, 0, 0,  VE, "level" },
    { "2.2",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 22 }, 0, 0,  VE, "level" },
    { "3.0",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 30 }, 0, 0,  VE, "level" },
    { "3.1",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 31 }, 0, 0,  VE, "level" },
    { "3.2",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 32 }, 0, 0,  VE, "level" },
    { "4.0",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 40 }, 0, 0,  VE, "level" },
    { "4.1",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 41 }, 0, 0,  VE, "level" },
    { "4.2",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 42 }, 0, 0,  VE, "level" },
    { "5.0",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 50 }, 0, 0,  VE, "level" },
    { "5.1",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 51 }, 0, 0,  VE, "level" },
    { "5.2",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 52 }, 0, 0,  VE, "level" },
    { "6.0",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 60 }, 0, 0,  VE, "level" },
    { "6.1",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 61 }, 0, 0,  VE, "level" },
    { "6.2",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 62 }, 0, 0,  VE, "level" },


    /// Quality Preset
    { "quality",        "Quality Preference",                   OFFSET(quality),    AV_OPT_TYPE_INT,   { .i64 = AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED    }, AMF_VIDEO_ENCODER_QUALITY_PRESET_BALANCED, AMF_VIDEO_ENCODER_QUALITY_PRESET_QUALITY, VE, "quality" },
    { "speed",          "Prefer Speed",                         0,                  AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED    },       0, 0, VE, "quality" },
    { "balanced",       "Balanced",                             0,                  AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_QUALITY_PRESET_BALANCED },    0, 0, VE, "quality" },
    { "quality",        "Prefer Quality",                       0,                  AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_QUALITY_PRESET_QUALITY  },     0, 0, VE, "quality" },

    // Dynamic
    /// Rate Control Method
    { "rc",             "Rate Control Method",                  OFFSET(rate_control_mode), AV_OPT_TYPE_INT,   { .i64 = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_UNKNOWN }, AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_UNKNOWN, AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR, VE, "rc" },
    { "cqp",            "Constant Quantization Parameter",      0,                         AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP             }, 0, 0, VE, "rc" },
    { "cbr",            "Constant Bitrate",                     0,                         AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR                     }, 0, 0, VE, "rc" },
    { "vbr_peak",       "Peak Contrained Variable Bitrate",     0,                         AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR    }, 0, 0, VE, "rc" },
    { "vbr_latency",    "Latency Constrained Variable Bitrate", 0,                         AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR }, 0, 0, VE, "rc" },

    /// Enforce HRD, Filler Data, VBAQ, Frame Skipping
    { "enforce_hrd",    "Enforce HRD",                          OFFSET(enforce_hrd),        AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    { "filler_data",    "Filler Data Enable",                   OFFSET(filler_data),        AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    { "vbaq",           "Enable VBAQ",                          OFFSET(enable_vbaq),        AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    { "frame_skipping", "Rate Control Based Frame Skip",        OFFSET(skip_frame),         AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },

    /// QP Values
    { "qp_i",           "Quantization Parameter for I-Frame",   OFFSET(qp_i),               AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 51, VE },
    { "qp_p",           "Quantization Parameter for P-Frame",   OFFSET(qp_p),               AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 51, VE },
    { "qp_b",           "Quantization Parameter for B-Frame",   OFFSET(qp_b),               AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 51, VE },

    /// Pre-Pass, Pre-Analysis, Two-Pass
    { "preanalysis",    "Pre-Analysis Mode",                    OFFSET(preanalysis),        AV_OPT_TYPE_BOOL,{ .i64 = 0 }, 0, 1, VE, NULL },

    /// Maximum Access Unit Size
    { "max_au_size",    "Maximum Access Unit Size for rate control (in bits)",   OFFSET(max_au_size),        AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, VE },

    /// Header Insertion Spacing
    { "header_spacing", "Header Insertion Spacing",             OFFSET(header_spacing),     AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 1000, VE },

    /// B-Frames
    // BPicturesPattern=bf
    { "bf_delta_qp",    "B-Picture Delta QP",                   OFFSET(b_frame_delta_qp),   AV_OPT_TYPE_INT,  { .i64 = 4 }, -10, 10, VE },
    { "bf_ref",         "Enable Reference to B-Frames",         OFFSET(b_frame_ref),        AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, VE },
    { "bf_ref_delta_qp","Reference B-Picture Delta QP",         OFFSET(ref_b_frame_delta_qp), AV_OPT_TYPE_INT,  { .i64 = 4 }, -10, 10, VE },

    /// Intra-Refresh
    { "intra_refresh_mb","Intra Refresh MBs Number Per Slot in Macroblocks",       OFFSET(intra_refresh_mb),    AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, VE },

    /// coder
    { "coder",          "Coding Type",                          OFFSET(coding_mode),   AV_OPT_TYPE_INT,   { .i64 = AMF_VIDEO_ENCODER_UNDEFINED }, AMF_VIDEO_ENCODER_UNDEFINED, AMF_VIDEO_ENCODER_CALV, VE, "coder" },
    { "auto",           "Automatic",                            0,                     AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_UNDEFINED }, 0, 0, VE, "coder" },
    { "cavlc",          "Context Adaptive Variable-Length Coding", 0,                  AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_CALV },      0, 0, VE, "coder" },
    { "cabac",          "Context Adaptive Binary Arithmetic Coding", 0,                AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_CABAC },     0, 0, VE, "coder" },

    { "me_half_pel",    "Enable ME Half Pixel",                 OFFSET(me_half_pel),   AV_OPT_TYPE_BOOL,  { .i64 = 1 }, 0, 1, VE },
    { "me_quarter_pel", "Enable ME Quarter Pixel",              OFFSET(me_quarter_pel),AV_OPT_TYPE_BOOL,  { .i64 = 1 }, 0, 1, VE },

    { "aud",            "Inserts AU Delimiter NAL unit",        OFFSET(aud)          ,AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0, 1, VE },

    { "log_to_dbg",     "Enable AMF logging to debug output",   OFFSET(log_to_dbg)    , AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },

    { NULL }
};

static av_cold int amf_encode_init_h264(AVCodecContext *avctx)
{
    int                              ret = 0;
    AMF_RESULT                       res = AMF_OK;
    AmfContext                      *ctx = avctx->priv_data;
    AMFVariantStruct                 var = { 0 };
    amf_int64                        profile = 0;
    amf_int64                        profile_level = 0;
    AMFBuffer                       *buffer;
    AMFGuid                          guid;
    AMFRate                          framerate;
    AMFSize                          framesize = AMFConstructSize(avctx->width, avctx->height);
    int                              deblocking_filter = (avctx->flags & AV_CODEC_FLAG_LOOP_FILTER) ? 1 : 0;

    if (avctx->framerate.num > 0 && avctx->framerate.den > 0) {
        framerate = AMFConstructRate(avctx->framerate.num, avctx->framerate.den);
    } else {
        framerate = AMFConstructRate(avctx->time_base.den, avctx->time_base.num * avctx->ticks_per_frame);
    }

    if ((ret = ff_amf_encode_init(avctx)) != 0)
        return ret;

    // Static parameters
    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_USAGE, ctx->usage);

    AMF_ASSIGN_PROPERTY_SIZE(res, ctx->encoder, AMF_VIDEO_ENCODER_FRAMESIZE, framesize);

    AMF_ASSIGN_PROPERTY_RATE(res, ctx->encoder, AMF_VIDEO_ENCODER_FRAMERATE, framerate);

    switch (avctx->profile) {
    case FF_PROFILE_H264_BASELINE:
        profile = AMF_VIDEO_ENCODER_PROFILE_BASELINE;
        break;
    case FF_PROFILE_H264_MAIN:
        profile = AMF_VIDEO_ENCODER_PROFILE_MAIN;
        break;
    case FF_PROFILE_H264_HIGH:
        profile = AMF_VIDEO_ENCODER_PROFILE_HIGH;
        break;
    case FF_PROFILE_H264_CONSTRAINED_BASELINE:
        profile = AMF_VIDEO_ENCODER_PROFILE_CONSTRAINED_BASELINE;
        break;
    case (FF_PROFILE_H264_HIGH | FF_PROFILE_H264_CONSTRAINED):
        profile = AMF_VIDEO_ENCODER_PROFILE_CONSTRAINED_HIGH;
        break;
    }
    if (profile == 0) {
        profile = ctx->profile;
    }

    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_PROFILE, profile);

    profile_level = avctx->level;
    if (profile_level == FF_LEVEL_UNKNOWN) {
        profile_level = ctx->level;
    }
    if (profile_level != 0) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_PROFILE_LEVEL, profile_level);
    }

    // Maximum Reference Frames
    if (avctx->refs != -1) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_MAX_NUM_REFRAMES, avctx->refs);
    }
    if (avctx->sample_aspect_ratio.den && avctx->sample_aspect_ratio.num) {
        AMFRatio ratio = AMFConstructRatio(avctx->sample_aspect_ratio.num, avctx->sample_aspect_ratio.den);
        AMF_ASSIGN_PROPERTY_RATIO(res, ctx->encoder, AMF_VIDEO_ENCODER_ASPECT_RATIO, ratio);
    }

    /// Color Range (Partial/TV/MPEG or Full/PC/JPEG)
    if (avctx->color_range == AVCOL_RANGE_JPEG) {
        AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_FULL_RANGE_COLOR, 1);
    }

    // autodetect rate control method
    if (ctx->rate_control_mode == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_UNKNOWN) {
        if (ctx->qp_i != -1 || ctx->qp_p != -1 || ctx->qp_b != -1) {
            ctx->rate_control_mode = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP;
            av_log(ctx, AV_LOG_DEBUG, "Rate control turned to CQP\n");
        } else if (avctx->rc_max_rate > 0 ) {
            ctx->rate_control_mode = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR;
            av_log(ctx, AV_LOG_DEBUG, "Rate control turned to Peak VBR\n");
        } else {
            ctx->rate_control_mode = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR;
            av_log(ctx, AV_LOG_DEBUG, "Rate control turned to CBR\n");
        }
    }

    if (ctx->rate_control_mode == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_RATE_CONTROL_PREANALYSIS_ENABLE, AMF_VIDEO_ENCODER_PREENCODE_DISABLED);
        if (ctx->preanalysis)
            av_log(ctx, AV_LOG_WARNING, "Pre-Analysis is not supported by cqp Rate Control Method, automatically disabled\n");
    } else {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_RATE_CONTROL_PREANALYSIS_ENABLE, ctx->preanalysis);
    }

    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_QUALITY_PRESET, ctx->quality);

    // Dynamic parmaters
    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD, ctx->rate_control_mode);

    /// VBV Buffer
    if (avctx->rc_buffer_size != 0) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_VBV_BUFFER_SIZE, avctx->rc_buffer_size);
        if (avctx->rc_initial_buffer_occupancy != 0) {
            int amf_buffer_fullness = avctx->rc_initial_buffer_occupancy * 64 / avctx->rc_buffer_size;
            if (amf_buffer_fullness > 64)
                amf_buffer_fullness = 64;
            AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_INITIAL_VBV_BUFFER_FULLNESS, amf_buffer_fullness);
        }
    }
    /// Maximum Access Unit Size
    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_MAX_AU_SIZE, ctx->max_au_size);

    if (ctx->max_au_size)
        ctx->enforce_hrd = 1;

    // QP Minimum / Maximum
    if (ctx->rate_control_mode == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_MIN_QP, 0);
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_MAX_QP, 51);
    } else {
        if (avctx->qmin != -1) {
            int qval = avctx->qmin > 51 ? 51 : avctx->qmin;
            AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_MIN_QP, qval);
        }
        if (avctx->qmax != -1) {
            int qval = avctx->qmax > 51 ? 51 : avctx->qmax;
            AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_MAX_QP, qval);
        }
    }
    // QP Values
    if (ctx->qp_i != -1)
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_QP_I, ctx->qp_i);
    if (ctx->qp_p != -1)
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_QP_P, ctx->qp_p);
    if (ctx->qp_b != -1)
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_QP_B, ctx->qp_b);

    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_TARGET_BITRATE, avctx->bit_rate);

    if (ctx->rate_control_mode == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_PEAK_BITRATE, avctx->bit_rate);
    }
    if (avctx->rc_max_rate) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_PEAK_BITRATE, avctx->rc_max_rate);
    } else if (ctx->rate_control_mode == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR) {
        av_log(ctx, AV_LOG_WARNING, "rate control mode is PEAK_CONSTRAINED_VBR but rc_max_rate is not set\n");
    }

    // Initialize Encoder
    res = ctx->encoder->pVtbl->Init(ctx->encoder, ctx->format, avctx->width, avctx->height);
    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_BUG, "encoder->Init() failed with error %d\n", res);

    // Enforce HRD, Filler Data, VBAQ, Frame Skipping, Deblocking Filter
    AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_ENFORCE_HRD, !!ctx->enforce_hrd);
    AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_FILLER_DATA_ENABLE, !!ctx->filler_data);
    AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_RATE_CONTROL_SKIP_FRAME_ENABLE, !!ctx->skip_frame);
    if (ctx->rate_control_mode == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP) {
        AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_ENABLE_VBAQ, 0);
        if (ctx->enable_vbaq)
            av_log(ctx, AV_LOG_WARNING, "VBAQ is not supported by cqp Rate Control Method, automatically disabled\n");
    } else {
        AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_ENABLE_VBAQ, !!ctx->enable_vbaq);
    }
    AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_DE_BLOCKING_FILTER, !!deblocking_filter);

    // B-Frames
    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_B_PIC_PATTERN, avctx->max_b_frames);
    if (res != AMF_OK) {
        res = ctx->encoder->pVtbl->GetProperty(ctx->encoder, AMF_VIDEO_ENCODER_B_PIC_PATTERN, &var);
        av_log(ctx, AV_LOG_WARNING, "B-frames=%d is not supported by this GPU, switched to %d\n",
            avctx->max_b_frames, (int)var.int64Value);
        avctx->max_b_frames = (int)var.int64Value;
    }
    if (avctx->max_b_frames) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_B_PIC_DELTA_QP, ctx->b_frame_delta_qp);
        AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_B_REFERENCE_ENABLE, !!ctx->b_frame_ref);
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_REF_B_PIC_DELTA_QP, ctx->ref_b_frame_delta_qp);
    }

    // Keyframe Interval
    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_IDR_PERIOD, avctx->gop_size);

    // Header Insertion Spacing
    if (ctx->header_spacing >= 0)
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEADER_INSERTION_SPACING, ctx->header_spacing);

    // Intra-Refresh, Slicing
    if (ctx->intra_refresh_mb > 0)
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_INTRA_REFRESH_NUM_MBS_PER_SLOT, ctx->intra_refresh_mb);
    if (avctx->slices > 1)
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_SLICES_PER_FRAME, avctx->slices);

    // Coding
    if (ctx->coding_mode != 0)
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_CABAC_ENABLE, ctx->coding_mode);

    // Motion Estimation
    AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_MOTION_HALF_PIXEL, !!ctx->me_half_pel);
    AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_MOTION_QUARTERPIXEL, !!ctx->me_quarter_pel);

    // fill extradata
    res = AMFVariantInit(&var);
    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_BUG, "AMFVariantInit() failed with error %d\n", res);

    res = ctx->encoder->pVtbl->GetProperty(ctx->encoder, AMF_VIDEO_ENCODER_EXTRADATA, &var);
    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_BUG, "GetProperty(AMF_VIDEO_ENCODER_EXTRADATA) failed with error %d\n", res);
    AMF_RETURN_IF_FALSE(ctx, var.pInterface != NULL, AVERROR_BUG, "GetProperty(AMF_VIDEO_ENCODER_EXTRADATA) returned NULL\n");

    guid = IID_AMFBuffer();

    res = var.pInterface->pVtbl->QueryInterface(var.pInterface, &guid, (void**)&buffer); // query for buffer interface
    if (res != AMF_OK) {
        var.pInterface->pVtbl->Release(var.pInterface);
    }
    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_BUG, "QueryInterface(IID_AMFBuffer) failed with error %d\n", res);

    avctx->extradata_size = (int)buffer->pVtbl->GetSize(buffer);
    avctx->extradata = av_mallocz(avctx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!avctx->extradata) {
        buffer->pVtbl->Release(buffer);
        var.pInterface->pVtbl->Release(var.pInterface);
        return AVERROR(ENOMEM);
    }
    memcpy(avctx->extradata, buffer->pVtbl->GetNative(buffer), avctx->extradata_size);

    buffer->pVtbl->Release(buffer);
    var.pInterface->pVtbl->Release(var.pInterface);

    return 0;
}

static const AVCodecDefault defaults[] = {
    { "refs",       "-1"  },
    { "aspect",     "0"   },
    { "qmin",       "-1"  },
    { "qmax",       "-1"  },
    { "b",          "2M"  },
    { "g",          "250" },
    { "slices",     "1"   },
    { "flags",      "+loop"},
    { NULL                },
};

static const AVClass h264_amf_class = {
    .class_name = "h264_amf",
    .item_name = av_default_item_name,
    .option = options,
    .version = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_h264_amf_encoder = {
    .name           = "h264_amf",
    .long_name      = NULL_IF_CONFIG_SMALL("AMD AMF H.264 Encoder"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .init           = amf_encode_init_h264,
    .receive_packet = ff_amf_receive_packet,
    .close          = ff_amf_encode_close,
    .priv_data_size = sizeof(AmfContext),
    .priv_class     = &h264_amf_class,
    .defaults       = defaults,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE |
                      AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .pix_fmts       = ff_amf_pix_fmts,
    .wrapper_name   = "amf",
    .hw_configs     = ff_amfenc_hw_configs,
};
