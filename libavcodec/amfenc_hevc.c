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
    { "usage",          "Set the encoding usage",             OFFSET(usage),          AV_OPT_TYPE_INT,   { .i64 = AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCONDING }, AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCONDING, AMF_VIDEO_ENCODER_HEVC_USAGE_WEBCAM, VE, "usage" },
    { "transcoding",    "", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCONDING },         0, 0, VE, "usage" },
    { "ultralowlatency","", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_HEVC_USAGE_ULTRA_LOW_LATENCY },    0, 0, VE, "usage" },
    { "lowlatency",     "", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_HEVC_USAGE_LOW_LATENCY },          0, 0, VE, "usage" },
    { "webcam",         "", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_HEVC_USAGE_WEBCAM },               0, 0, VE, "usage" },

    { "profile",        "Set the profile (default main)",           OFFSET(profile),   AV_OPT_TYPE_INT,{ .i64 = AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN }, AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN, AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN, VE, "profile" },
    { "main",           "", 0,                      AV_OPT_TYPE_CONST,{ .i64 = AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN }, 0, 0, VE, "profile" },

    { "profile_tier",   "Set the profile tier (default main)",      OFFSET(tier), AV_OPT_TYPE_INT,{ .i64 = AMF_VIDEO_ENCODER_HEVC_TIER_MAIN }, AMF_VIDEO_ENCODER_HEVC_TIER_MAIN, AMF_VIDEO_ENCODER_HEVC_TIER_HIGH, VE, "tier" },
    { "main",           "", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_HEVC_TIER_MAIN }, 0, 0, VE, "tier" },
    { "high",           "", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_HEVC_TIER_HIGH }, 0, 0, VE, "tier" },

    { "level",          "Set the encoding level (default auto)",    OFFSET(level), AV_OPT_TYPE_INT,{ .i64 = 0 }, 0, AMF_LEVEL_6_2, VE, "level" },
    { "auto",           "", 0, AV_OPT_TYPE_CONST, { .i64 = 0             }, 0, 0, VE, "level" },
    { "1.0",            "", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_LEVEL_1   }, 0, 0, VE, "level" },
    { "2.0",            "", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_LEVEL_2   }, 0, 0, VE, "level" },
    { "2.1",            "", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_LEVEL_2_1 }, 0, 0, VE, "level" },
    { "3.0",            "", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_LEVEL_3   }, 0, 0, VE, "level" },
    { "3.1",            "", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_LEVEL_3_1 }, 0, 0, VE, "level" },
    { "4.0",            "", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_LEVEL_4   }, 0, 0, VE, "level" },
    { "4.1",            "", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_LEVEL_4_1 }, 0, 0, VE, "level" },
    { "5.0",            "", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_LEVEL_5   }, 0, 0, VE, "level" },
    { "5.1",            "", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_LEVEL_5_1 }, 0, 0, VE, "level" },
    { "5.2",            "", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_LEVEL_5_2 }, 0, 0, VE, "level" },
    { "6.0",            "", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_LEVEL_6   }, 0, 0, VE, "level" },
    { "6.1",            "", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_LEVEL_6_1 }, 0, 0, VE, "level" },
    { "6.2",            "", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_LEVEL_6_2 }, 0, 0, VE, "level" },

    { "quality",        "Set the encoding quality",                 OFFSET(quality),      AV_OPT_TYPE_INT,   { .i64 = AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED }, AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_QUALITY, AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED, VE, "quality" },
    { "balanced",       "", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_BALANCED }, 0, 0, VE, "quality" },
    { "speed",          "", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED    }, 0, 0, VE, "quality" },
    { "quality",        "", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_QUALITY  }, 0, 0, VE, "quality" },

    { "rc",             "Set the rate control mode",            OFFSET(rate_control_mode), AV_OPT_TYPE_INT, { .i64 = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_UNKNOWN }, AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_UNKNOWN, AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR, VE, "rc" },
    { "cqp",            "Constant Quantization Parameter",      0, AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CONSTANT_QP             }, 0, 0, VE, "rc" },
    { "cbr",            "Constant Bitrate",                     0, AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR                     }, 0, 0, VE, "rc" },
    { "vbr_peak",       "Peak Contrained Variable Bitrate",     0, AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR    }, 0, 0, VE, "rc" },
    { "vbr_latency",    "Latency Constrained Variable Bitrate", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR }, 0, 0, VE, "rc" },

    { "header_insertion_mode",        "Set header insertion mode",  OFFSET(header_insertion_mode),      AV_OPT_TYPE_INT,{ .i64 = AMF_VIDEO_ENCODER_HEVC_HEADER_INSERTION_MODE_NONE }, AMF_VIDEO_ENCODER_HEVC_HEADER_INSERTION_MODE_NONE, AMF_VIDEO_ENCODER_HEVC_HEADER_INSERTION_MODE_IDR_ALIGNED, VE, "hdrmode" },
    { "none",           "", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_HEVC_HEADER_INSERTION_MODE_NONE        }, 0, 0, VE, "hdrmode" },
    { "gop",            "", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_HEVC_HEADER_INSERTION_MODE_GOP_ALIGNED }, 0, 0, VE, "hdrmode" },
    { "idr",            "", 0, AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_HEVC_HEADER_INSERTION_MODE_IDR_ALIGNED }, 0, 0, VE, "hdrmode" },

    { "gops_per_idr",    "GOPs per IDR 0-no IDR will be inserted",  OFFSET(gops_per_idr),  AV_OPT_TYPE_INT,  { .i64 = 60 },  0, INT_MAX, VE },
    { "preanalysis",    "Enable preanalysis",                       OFFSET(preanalysis),   AV_OPT_TYPE_BOOL, { .i64 = 0  },  0, 1, VE},
    { "vbaq",           "Enable VBAQ",                              OFFSET(enable_vbaq),   AV_OPT_TYPE_BOOL, { .i64 = 0  },  0, 1, VE},
    { "enforce_hrd",    "Enforce HRD",                              OFFSET(enforce_hrd),   AV_OPT_TYPE_BOOL, { .i64 = 0  },  0, 1, VE},
    { "filler_data",    "Filler Data Enable",                       OFFSET(filler_data),   AV_OPT_TYPE_BOOL, { .i64 = 0  },  0, 1, VE},
    { "max_au_size",    "Maximum Access Unit Size for rate control (in bits)", OFFSET(max_au_size),   AV_OPT_TYPE_INT,{ .i64 = 0 }, 0, INT_MAX, VE},
    { "min_qp_i",       "min quantization parameter for I-frame",   OFFSET(min_qp_i),      AV_OPT_TYPE_INT, { .i64 = -1  }, -1, 51, VE },
    { "max_qp_i",       "max quantization parameter for I-frame",   OFFSET(max_qp_i),      AV_OPT_TYPE_INT, { .i64 = -1  }, -1, 51, VE },
    { "min_qp_p",       "min quantization parameter for P-frame",   OFFSET(min_qp_p),      AV_OPT_TYPE_INT, { .i64 = -1  }, -1, 51, VE },
    { "max_qp_p",       "max quantization parameter for P-frame",   OFFSET(max_qp_p),      AV_OPT_TYPE_INT, { .i64 = -1  }, -1, 51, VE },
    { "qp_p",           "quantization parameter for P-frame",       OFFSET(qp_p),          AV_OPT_TYPE_INT, { .i64 = -1  }, -1, 51, VE },
    { "qp_i",           "quantization parameter for I-frame",       OFFSET(qp_i),          AV_OPT_TYPE_INT, { .i64 = -1  }, -1, 51, VE },
    { "skip_frame",     "Rate Control Based Frame Skip",            OFFSET(skip_frame),    AV_OPT_TYPE_BOOL,{ .i64 = 0   },  0, 1, VE },
    { "me_half_pel",    "Enable ME Half Pixel",                     OFFSET(me_half_pel),   AV_OPT_TYPE_BOOL,{ .i64 = 1   },  0, 1, VE },
    { "me_quarter_pel", "Enable ME Quarter Pixel ",                 OFFSET(me_quarter_pel),AV_OPT_TYPE_BOOL,{ .i64 = 1   },  0, 1, VE },

    { "aud",            "Inserts AU Delimiter NAL unit",            OFFSET(aud)           ,AV_OPT_TYPE_BOOL,{ .i64 = 0 }, 0, 1, VE },

    { "log_to_dbg",     "Enable AMF logging to debug output",   OFFSET(log_to_dbg), AV_OPT_TYPE_BOOL,{ .i64 = 0 }, 0, 1, VE },
    { NULL }
};

static av_cold int amf_encode_init_hevc(AVCodecContext *avctx)
{
    int                 ret = 0;
    AMF_RESULT          res = AMF_OK;
    AmfContext         *ctx = avctx->priv_data;
    AMFVariantStruct    var = {0};
    amf_int64           profile = 0;
    amf_int64           profile_level = 0;
    AMFBuffer          *buffer;
    AMFGuid             guid;
    AMFRate             framerate;
    AMFSize             framesize = AMFConstructSize(avctx->width, avctx->height);
    int                 deblocking_filter = (avctx->flags & AV_CODEC_FLAG_LOOP_FILTER) ? 1 : 0;

    if (avctx->framerate.num > 0 && avctx->framerate.den > 0) {
        framerate = AMFConstructRate(avctx->framerate.num, avctx->framerate.den);
    } else {
        framerate = AMFConstructRate(avctx->time_base.den, avctx->time_base.num * avctx->ticks_per_frame);
    }

    if ((ret = ff_amf_encode_init(avctx)) < 0)
        return ret;

    // init static parameters
    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_USAGE, ctx->usage);

    AMF_ASSIGN_PROPERTY_SIZE(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, framesize);

    AMF_ASSIGN_PROPERTY_RATE(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_FRAMERATE, framerate);

    switch (avctx->profile) {
    case FF_PROFILE_HEVC_MAIN:
        profile = AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN;
        break;
    default:
        break;
    }
    if (profile == 0) {
        profile = ctx->profile;
    }
    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_PROFILE, profile);

    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_TIER, ctx->tier);

    profile_level = avctx->level;
    if (profile_level == FF_LEVEL_UNKNOWN) {
        profile_level = ctx->level;
    }
    if (profile_level != 0) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_PROFILE_LEVEL, profile_level);
    }
    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET, ctx->quality);
    // Maximum Reference Frames
    if (avctx->refs != -1) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_MAX_NUM_REFRAMES, avctx->refs);
    }
    // Aspect Ratio
    if (avctx->sample_aspect_ratio.den && avctx->sample_aspect_ratio.num) {
        AMFRatio ratio = AMFConstructRatio(avctx->sample_aspect_ratio.num, avctx->sample_aspect_ratio.den);
        AMF_ASSIGN_PROPERTY_RATIO(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_ASPECT_RATIO, ratio);
    }

    // Picture control properties
    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_NUM_GOPS_PER_IDR, ctx->gops_per_idr);
    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_GOP_SIZE, avctx->gop_size);
    if (avctx->slices > 1) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_SLICES_PER_FRAME, avctx->slices);
    }
    AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_DE_BLOCKING_FILTER_DISABLE, deblocking_filter);
    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_HEADER_INSERTION_MODE, ctx->header_insertion_mode);

    // Rate control
    // autodetect rate control method
    if (ctx->rate_control_mode == AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_UNKNOWN) {
        if (ctx->min_qp_i != -1 || ctx->max_qp_i != -1 ||
            ctx->min_qp_p != -1 || ctx->max_qp_p != -1 ||
            ctx->qp_i !=-1 || ctx->qp_p != -1) {
            ctx->rate_control_mode = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CONSTANT_QP;
            av_log(ctx, AV_LOG_DEBUG, "Rate control turned to CQP\n");
        } else if (avctx->rc_max_rate > 0) {
            ctx->rate_control_mode = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR;
            av_log(ctx, AV_LOG_DEBUG, "Rate control turned to Peak VBR\n");
        } else {
            ctx->rate_control_mode = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR;
            av_log(ctx, AV_LOG_DEBUG, "Rate control turned to CBR\n");
        }
    }


    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD, ctx->rate_control_mode);
    if (avctx->rc_buffer_size) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_VBV_BUFFER_SIZE, avctx->rc_buffer_size);

        if (avctx->rc_initial_buffer_occupancy != 0) {
            int amf_buffer_fullness = avctx->rc_initial_buffer_occupancy * 64 / avctx->rc_buffer_size;
            if (amf_buffer_fullness > 64)
                amf_buffer_fullness = 64;
            AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_INITIAL_VBV_BUFFER_FULLNESS, amf_buffer_fullness);
        }
    }
    // Pre-Pass, Pre-Analysis, Two-Pass
    AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_PREANALYSIS_ENABLE, ctx->preanalysis);

    if (ctx->rate_control_mode == AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CONSTANT_QP) {
        AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_ENABLE_VBAQ, false);
        if (ctx->enable_vbaq)
            av_log(ctx, AV_LOG_WARNING, "VBAQ is not supported by cqp Rate Control Method, automatically disabled\n");
    } else {
        AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_ENABLE_VBAQ, !!ctx->enable_vbaq);
    }
    AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_MOTION_HALF_PIXEL, ctx->me_half_pel);
    AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_MOTION_QUARTERPIXEL, ctx->me_quarter_pel);

    // init dynamic rate control params
    if (ctx->max_au_size)
        ctx->enforce_hrd = 1;
    AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_ENFORCE_HRD, ctx->enforce_hrd);
    AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_FILLER_DATA_ENABLE, ctx->filler_data);

    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, avctx->bit_rate);

    if (ctx->rate_control_mode == AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_PEAK_BITRATE, avctx->bit_rate);
    }
    if (avctx->rc_max_rate) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_PEAK_BITRATE, avctx->rc_max_rate);
    } else if (ctx->rate_control_mode == AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR) {
        av_log(ctx, AV_LOG_WARNING, "rate control mode is PEAK_CONSTRAINED_VBR but rc_max_rate is not set\n");
    }

    // init encoder
    res = ctx->encoder->pVtbl->Init(ctx->encoder, ctx->format, avctx->width, avctx->height);
    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_BUG, "encoder->Init() failed with error %d\n", res);

    // init dynamic picture control params
    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_MAX_AU_SIZE, ctx->max_au_size);

    if (ctx->min_qp_i != -1) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_MIN_QP_I, ctx->min_qp_i);
    } else if (avctx->qmin != -1) {
        int qval = avctx->qmin > 51 ? 51 : avctx->qmin;
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_MIN_QP_I, qval);
    }
    if (ctx->max_qp_i != -1) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_MAX_QP_I, ctx->max_qp_i);
    } else if (avctx->qmax != -1) {
        int qval = avctx->qmax > 51 ? 51 : avctx->qmax;
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_MAX_QP_I, qval);
    }
    if (ctx->min_qp_p != -1) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_MIN_QP_P, ctx->min_qp_p);
    } else if (avctx->qmin != -1) {
        int qval = avctx->qmin > 51 ? 51 : avctx->qmin;
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_MIN_QP_P, qval);
    }
    if (ctx->max_qp_p != -1) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_MAX_QP_P, ctx->max_qp_p);
    } else if (avctx->qmax != -1) {
        int qval = avctx->qmax > 51 ? 51 : avctx->qmax;
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_MAX_QP_P, qval);
    }

    if (ctx->qp_p != -1) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_QP_P, ctx->qp_p);
    }
    if (ctx->qp_i != -1) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_QP_I, ctx->qp_i);
    }
    AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_SKIP_FRAME_ENABLE, ctx->skip_frame);


    // fill extradata
    res = AMFVariantInit(&var);
    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_BUG, "AMFVariantInit() failed with error %d\n", res);

    res = ctx->encoder->pVtbl->GetProperty(ctx->encoder, AMF_VIDEO_ENCODER_HEVC_EXTRADATA, &var);
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
    { "b",          "2M"  },
    { "g",          "250" },
    { "slices",     "1"   },
    { NULL                },
};
static const AVClass hevc_amf_class = {
    .class_name = "hevc_amf",
    .item_name = av_default_item_name,
    .option = options,
    .version = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_hevc_amf_encoder = {
    .name           = "hevc_amf",
    .long_name      = NULL_IF_CONFIG_SMALL("AMD AMF HEVC encoder"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_HEVC,
    .init           = amf_encode_init_hevc,
    .send_frame     = ff_amf_send_frame,
    .receive_packet = ff_amf_receive_packet,
    .close          = ff_amf_encode_close,
    .priv_data_size = sizeof(AmfContext),
    .priv_class     = &hevc_amf_class,
    .defaults       = defaults,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .pix_fmts       = ff_amf_pix_fmts,
    .wrapper_name   = "amf",
};
