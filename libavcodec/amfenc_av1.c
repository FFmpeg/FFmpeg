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
#include "codec_internal.h"
#include "internal.h"

#define OFFSET(x) offsetof(AmfContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "usage",                  "Set the encoding usage",                   OFFSET(usage),                          AV_OPT_TYPE_INT,   {.i64 = AMF_VIDEO_ENCODER_AV1_USAGE_TRANSCODING }, AMF_VIDEO_ENCODER_AV1_USAGE_TRANSCODING, AMF_VIDEO_ENCODER_AV1_USAGE_LOW_LATENCY, VE, .unit = "usage" },
    { "transcoding",            "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_USAGE_TRANSCODING }, 0, 0, VE, .unit = "usage" },
    { "lowlatency",             "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_USAGE_LOW_LATENCY }, 0, 0, VE, .unit = "usage" },

    { "profile",                "Set the profile (default main)",           OFFSET(profile),                        AV_OPT_TYPE_INT,{.i64 = AMF_VIDEO_ENCODER_AV1_PROFILE_MAIN }, AMF_VIDEO_ENCODER_AV1_PROFILE_MAIN, AMF_VIDEO_ENCODER_AV1_PROFILE_MAIN, VE, .unit = "profile" },
    { "main",                   "", 0, AV_OPT_TYPE_CONST,{.i64 = AMF_VIDEO_ENCODER_AV1_PROFILE_MAIN }, 0, 0, VE, .unit = "profile" },

    { "level",                  "Set the encoding level (default auto)",    OFFSET(level),                          AV_OPT_TYPE_INT,{.i64 = 0 }, 0, AMF_VIDEO_ENCODER_AV1_LEVEL_7_3, VE, .unit = "level" },
    { "auto",                   "", 0, AV_OPT_TYPE_CONST, {.i64 = 0                               }, 0, 0, VE, .unit = "level" },
    { "2.0",                    "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_LEVEL_2_0 }, 0, 0, VE, .unit = "level" },
    { "2.1",                    "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_LEVEL_2_1 }, 0, 0, VE, .unit = "level" },
    { "2.2",                    "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_LEVEL_2_2 }, 0, 0, VE, .unit = "level" },
    { "2.3",                    "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_LEVEL_2_3 }, 0, 0, VE, .unit = "level" },
    { "3.0",                    "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_LEVEL_3_0 }, 0, 0, VE, .unit = "level" },
    { "3.1",                    "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_LEVEL_3_1 }, 0, 0, VE, .unit = "level" },
    { "3.2",                    "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_LEVEL_3_2 }, 0, 0, VE, .unit = "level" },
    { "3.3",                    "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_LEVEL_3_3 }, 0, 0, VE, .unit = "level" },
    { "4.0",                    "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_LEVEL_4_0 }, 0, 0, VE, .unit = "level" },
    { "4.1",                    "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_LEVEL_4_1 }, 0, 0, VE, .unit = "level" },
    { "4.2",                    "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_LEVEL_4_2 }, 0, 0, VE, .unit = "level" },
    { "4.3",                    "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_LEVEL_4_3 }, 0, 0, VE, .unit = "level" },
    { "5.0",                    "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_LEVEL_5_0 }, 0, 0, VE, .unit = "level" },
    { "5.1",                    "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_LEVEL_5_1 }, 0, 0, VE, .unit = "level" },
    { "5.2",                    "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_LEVEL_5_2 }, 0, 0, VE, .unit = "level" },
    { "5.3",                    "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_LEVEL_5_3 }, 0, 0, VE, .unit = "level" },
    { "6.0",                    "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_LEVEL_6_0 }, 0, 0, VE, .unit = "level" },
    { "6.1",                    "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_LEVEL_6_1 }, 0, 0, VE, .unit = "level" },
    { "6.2",                    "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_LEVEL_6_2 }, 0, 0, VE, .unit = "level" },
    { "6.3",                    "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_LEVEL_6_3 }, 0, 0, VE, .unit = "level" },
    { "7.0",                    "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_LEVEL_7_0 }, 0, 0, VE, .unit = "level" },
    { "7.1",                    "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_LEVEL_7_1 }, 0, 0, VE, .unit = "level" },
    { "7.2",                    "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_LEVEL_7_2 }, 0, 0, VE, .unit = "level" },
    { "7.3",                    "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_LEVEL_7_3 }, 0, 0, VE, .unit = "level" },

    { "quality",                "Set the encoding quality",                 OFFSET(quality),                        AV_OPT_TYPE_INT,   {.i64 = AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_SPEED }, AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_HIGH_QUALITY, AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_SPEED, VE, .unit = "quality" },
    { "balanced",               "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_BALANCED      }, 0, 0, VE, .unit = "quality" },
    { "speed",                  "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_SPEED         }, 0, 0, VE, .unit = "quality" },
    { "quality",                "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_QUALITY       }, 0, 0, VE, .unit = "quality" },
    { "high_quality",           "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_HIGH_QUALITY  }, 0, 0, VE, .unit = "quality" },

    { "rc",                     "Set the rate control mode",                OFFSET(rate_control_mode),              AV_OPT_TYPE_INT, {.i64 = AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_UNKNOWN }, AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_UNKNOWN, AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_HIGH_QUALITY_CBR, VE, .unit = "rc" },
    { "cqp",                    "Constant Quantization Parameter",      0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CONSTANT_QP             }, 0, 0, VE, .unit = "rc" },
    { "vbr_latency",            "Latency Constrained Variable Bitrate", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR }, 0, 0, VE, .unit = "rc" },
    { "vbr_peak",               "Peak Contrained Variable Bitrate",     0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR    }, 0, 0, VE, .unit = "rc" },
    { "cbr",                    "Constant Bitrate",                     0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CBR                     }, 0, 0, VE, .unit = "rc" },
    { "qvbr",                   "Quality Variable Bitrate",             0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_QUALITY_VBR             }, 0, 0, VE, .unit = "rc" },
    { "hqvbr",                  "High Quality Variable Bitrate",        0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_HIGH_QUALITY_VBR        }, 0, 0, VE, .unit = "rc" },
    { "hqcbr",                  "High Quality Constant Bitrate",        0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_HIGH_QUALITY_CBR        }, 0, 0, VE, .unit = "rc" },

    { "qvbr_quality_level",     "Sets the QVBR quality level",              OFFSET(qvbr_quality_level),             AV_OPT_TYPE_INT,   {.i64 = -1 }, -1, 51, VE },


    { "header_insertion_mode",  "Set header insertion mode",                OFFSET(header_insertion_mode),          AV_OPT_TYPE_INT,{.i64 = -1 }, -1, AMF_VIDEO_ENCODER_AV1_HEADER_INSERTION_MODE_KEY_FRAME_ALIGNED, VE, .unit = "hdrmode" },
    { "none",                   "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_HEADER_INSERTION_MODE_NONE              }, 0, 0, VE, .unit = "hdrmode" },
    { "gop",                    "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_HEADER_INSERTION_MODE_GOP_ALIGNED       }, 0, 0, VE, .unit = "hdrmode" },
    { "frame",                  "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_HEADER_INSERTION_MODE_KEY_FRAME_ALIGNED }, 0, 0, VE, .unit = "hdrmode" },

    { "preencode",              "Enable preencode",                       OFFSET(preencode),                        AV_OPT_TYPE_BOOL, {.i64 = 0  }, 0, 1, VE},
    { "enforce_hrd",            "Enforce HRD",                              OFFSET(enforce_hrd),                    AV_OPT_TYPE_BOOL, {.i64 = 0  }, 0, 1, VE},
    { "filler_data",            "Filler Data Enable",                       OFFSET(filler_data),                    AV_OPT_TYPE_BOOL, {.i64 = 0  }, 0, 1, VE},

    { "high_motion_quality_boost_enable",   "Enable High motion quality boost mode",  OFFSET(hw_high_motion_quality_boost), AV_OPT_TYPE_BOOL,   {.i64 = -1 }, -1, 1, VE },

    // min_qp_i -> min_qp_intra, min_qp_p -> min_qp_inter
    { "min_qp_i",               "min quantization parameter for I-frame",   OFFSET(min_qp_i),                       AV_OPT_TYPE_INT, {.i64 = -1  }, -1, 255, VE },
    { "max_qp_i",               "max quantization parameter for I-frame",   OFFSET(max_qp_i),                       AV_OPT_TYPE_INT, {.i64 = -1  }, -1, 255, VE },
    { "min_qp_p",               "min quantization parameter for P-frame",   OFFSET(min_qp_p),                       AV_OPT_TYPE_INT, {.i64 = -1  }, -1, 255, VE },
    { "max_qp_p",               "max quantization parameter for P-frame",   OFFSET(max_qp_p),                       AV_OPT_TYPE_INT, {.i64 = -1  }, -1, 255, VE },
    { "qp_p",                   "quantization parameter for P-frame",       OFFSET(qp_p),                           AV_OPT_TYPE_INT, {.i64 = -1  }, -1, 255, VE },
    { "qp_i",                   "quantization parameter for I-frame",       OFFSET(qp_i),                           AV_OPT_TYPE_INT, {.i64 = -1  }, -1, 255, VE },
    { "skip_frame",             "Rate Control Based Frame Skip",            OFFSET(skip_frame),                     AV_OPT_TYPE_BOOL,{.i64 = 0   },  0, 1, VE },

    { "align",                  "alignment mode",                           OFFSET(align),                          AV_OPT_TYPE_INT,     {.i64 = AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE_NO_RESTRICTIONS },         AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE_64X16_ONLY, AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE_NO_RESTRICTIONS, VE, .unit = "align" },
    { "64x16",                  "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE_64X16_ONLY               }, 0, 0, VE, .unit = "align" },
    { "1080p",                  "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE_64X16_1080P_CODED_1082   }, 0, 0, VE, .unit = "align" },
    { "none",                   "", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE_NO_RESTRICTIONS          }, 0, 0, VE, .unit = "align" },

    { "log_to_dbg",     "Enable AMF logging to debug output",   OFFSET(log_to_dbg), AV_OPT_TYPE_BOOL,{.i64 = 0 }, 0, 1, VE },

    //Pre Analysis options
    { "preanalysis",                            "Enable preanalysis",                                           OFFSET(preanalysis),                            AV_OPT_TYPE_BOOL,   {.i64 = -1 }, -1, 1, VE },

    { "pa_activity_type",                       "Set the type of activity analysis",                            OFFSET(pa_activity_type),                       AV_OPT_TYPE_INT,    {.i64 = -1 }, -1, AMF_PA_ACTIVITY_YUV, VE, .unit = "activity_type" },
    { "y",                                      "activity y",   0, AV_OPT_TYPE_CONST, {.i64 = AMF_PA_ACTIVITY_Y     }, 0, 0, VE, .unit = "activity_type" },
    { "yuv",                                    "activity yuv", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_PA_ACTIVITY_YUV   }, 0, 0, VE, .unit = "activity_type" },

    { "pa_scene_change_detection_enable",       "Enable scene change detection",                                OFFSET(pa_scene_change_detection),              AV_OPT_TYPE_BOOL,   {.i64 = -1 }, -1, 1, VE },

    { "pa_scene_change_detection_sensitivity",  "Set the sensitivity of scene change detection",                OFFSET(pa_scene_change_detection_sensitivity),  AV_OPT_TYPE_INT,    {.i64 = -1 }, -1, AMF_PA_SCENE_CHANGE_DETECTION_SENSITIVITY_HIGH, VE, .unit = "scene_change_sensitivity" },
    { "low",                                    "low scene change dectection sensitivity",      0, AV_OPT_TYPE_CONST, {.i64 = AMF_PA_SCENE_CHANGE_DETECTION_SENSITIVITY_LOW     }, 0, 0, VE, .unit = "scene_change_sensitivity" },
    { "medium",                                 "medium scene change dectection sensitivity",   0, AV_OPT_TYPE_CONST, {.i64 = AMF_PA_SCENE_CHANGE_DETECTION_SENSITIVITY_MEDIUM  }, 0, 0, VE, .unit = "scene_change_sensitivity" },
    { "high",                                   "high scene change dectection sensitivity",     0, AV_OPT_TYPE_CONST, {.i64 = AMF_PA_SCENE_CHANGE_DETECTION_SENSITIVITY_HIGH    }, 0, 0, VE, .unit = "scene_change_sensitivity" },

    { "pa_static_scene_detection_enable",       "Enable static scene detection",                                OFFSET(pa_static_scene_detection),              AV_OPT_TYPE_BOOL,   {.i64 = -1 }, -1, 1, VE },

    { "pa_static_scene_detection_sensitivity",  "Set the sensitivity of static scene detection",                OFFSET(pa_static_scene_detection_sensitivity),  AV_OPT_TYPE_INT,    {.i64 = -1 }, -1, AMF_PA_STATIC_SCENE_DETECTION_SENSITIVITY_HIGH, VE , .unit = "static_scene_sensitivity" },
    { "low",                                    "low static scene dectection sensitivity",      0, AV_OPT_TYPE_CONST, {.i64 = AMF_PA_STATIC_SCENE_DETECTION_SENSITIVITY_LOW    }, 0, 0, VE, .unit = "static_scene_sensitivity" },
    { "medium",                                 "medium static scene dectection sensitivity",   0, AV_OPT_TYPE_CONST, {.i64 = AMF_PA_STATIC_SCENE_DETECTION_SENSITIVITY_MEDIUM }, 0, 0, VE, .unit = "static_scene_sensitivity" },
    { "high",                                   "high static scene dectection sensitivity",     0, AV_OPT_TYPE_CONST, {.i64 = AMF_PA_STATIC_SCENE_DETECTION_SENSITIVITY_HIGH   }, 0, 0, VE, .unit = "static_scene_sensitivity" },

    { "pa_initial_qp_after_scene_change",       "The QP value that is used immediately after a scene change",   OFFSET(pa_initial_qp),                          AV_OPT_TYPE_INT,    {.i64 = -1 }, -1, 51, VE },
    { "pa_max_qp_before_force_skip",            "The QP threshold to allow a skip frame",                       OFFSET(pa_max_qp),                              AV_OPT_TYPE_INT,    {.i64 = -1 }, -1, 51, VE },

    { "pa_caq_strength",                        "Content Adaptive Quantization strength",                       OFFSET(pa_caq_strength),                        AV_OPT_TYPE_INT,    {.i64 = -1 }, -1, AMF_PA_CAQ_STRENGTH_HIGH, VE , .unit = "caq_strength" },
    { "low",                                    "low Content Adaptive Quantization strength",       0, AV_OPT_TYPE_CONST, {.i64 = AMF_PA_CAQ_STRENGTH_LOW      }, 0, 0, VE, .unit = "caq_strength" },
    { "medium",                                 "medium Content Adaptive Quantization strength",    0, AV_OPT_TYPE_CONST, {.i64 = AMF_PA_CAQ_STRENGTH_MEDIUM   }, 0, 0, VE, .unit = "caq_strength" },
    { "high",                                   "high Content Adaptive Quantization strength",      0, AV_OPT_TYPE_CONST, {.i64 = AMF_PA_CAQ_STRENGTH_HIGH     }, 0, 0, VE, .unit = "caq_strength" },

    { "pa_frame_sad_enable",                    "Enable Frame SAD algorithm",                                   OFFSET(pa_frame_sad),                           AV_OPT_TYPE_BOOL,   {.i64 = -1 }, -1, 1, VE },
    { "pa_ltr_enable",                          "Enable long term reference frame management",                  OFFSET(pa_ltr),                                 AV_OPT_TYPE_BOOL,   {.i64 = -1 }, -1, 1, VE },
    { "pa_lookahead_buffer_depth",              "Sets the PA lookahead buffer size",                            OFFSET(pa_lookahead_buffer_depth),              AV_OPT_TYPE_INT,    {.i64 = -1 }, -1, MAX_LOOKAHEAD_DEPTH, VE },

    { "pa_paq_mode",                            "Sets the perceptual adaptive quantization mode",               OFFSET(pa_paq_mode),                            AV_OPT_TYPE_INT,    {.i64 = -1 }, -1, AMF_PA_PAQ_MODE_CAQ, VE , .unit = "paq_mode" },
    { "none",                                   "no perceptual adaptive quantization",  0, AV_OPT_TYPE_CONST, {.i64 = AMF_PA_PAQ_MODE_NONE }, 0, 0, VE, .unit = "paq_mode" },
    { "caq",                                    "caq perceptual adaptive quantization", 0, AV_OPT_TYPE_CONST, {.i64 = AMF_PA_PAQ_MODE_CAQ  }, 0, 0, VE, .unit = "paq_mode" },

    { "pa_taq_mode",                            "Sets the temporal adaptive quantization mode",                 OFFSET(pa_taq_mode),                            AV_OPT_TYPE_INT,    {.i64 = -1 }, -1, AMF_PA_TAQ_MODE_2, VE , .unit = "taq_mode" },
    { "none",                                   "no temporal adaptive quantization",        0, AV_OPT_TYPE_CONST, {.i64 = AMF_PA_TAQ_MODE_NONE }, 0, 0, VE, .unit = "taq_mode" },
    { "1",                                      "temporal adaptive quantization mode 1",    0, AV_OPT_TYPE_CONST, {.i64 = AMF_PA_TAQ_MODE_1    }, 0, 0, VE, .unit = "taq_mode" },
    { "2",                                      "temporal adaptive quantization mode 2",    0, AV_OPT_TYPE_CONST, {.i64 = AMF_PA_TAQ_MODE_2    }, 0, 0, VE, .unit = "taq_mode" },

    { "pa_high_motion_quality_boost_mode",      "Sets the PA high motion quality boost mode",                   OFFSET(pa_high_motion_quality_boost_mode),      AV_OPT_TYPE_INT,    {.i64 = -1 }, -1, AMF_PA_HIGH_MOTION_QUALITY_BOOST_MODE_AUTO, VE , .unit = "high_motion_quality_boost_mode" },
    { "none",                                   "no high motion quality boost",     0, AV_OPT_TYPE_CONST, {.i64 = AMF_PA_HIGH_MOTION_QUALITY_BOOST_MODE_NONE   }, 0, 0, VE, .unit = "high_motion_quality_boost_mode" },
    { "auto",                                   "auto high motion quality boost",   0, AV_OPT_TYPE_CONST, {.i64 = AMF_PA_HIGH_MOTION_QUALITY_BOOST_MODE_AUTO   }, 0, 0, VE, .unit = "high_motion_quality_boost_mode" },
    { NULL }

};

static av_cold int amf_encode_init_av1(AVCodecContext* avctx)
{
    int                 ret = 0;
    AMF_RESULT          res = AMF_OK;
    AmfContext* ctx = avctx->priv_data;
    AMFVariantStruct    var = { 0 };
    amf_int64           profile = 0;
    amf_int64           profile_level = 0;
    AMFBuffer* buffer;
    AMFGuid             guid;
    AMFRate             framerate;
    AMFSize             framesize = AMFConstructSize(avctx->width, avctx->height);



    if (avctx->framerate.num > 0 && avctx->framerate.den > 0) {
        framerate = AMFConstructRate(avctx->framerate.num, avctx->framerate.den);
    }
    else {
FF_DISABLE_DEPRECATION_WARNINGS
        framerate = AMFConstructRate(avctx->time_base.den, avctx->time_base.num
#if FF_API_TICKS_PER_FRAME
                                     * avctx->ticks_per_frame
#endif
                                     );
FF_ENABLE_DEPRECATION_WARNINGS
    }

    if ((ret = ff_amf_encode_init(avctx)) < 0)
        return ret;

    // init static parameters
    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_USAGE, ctx->usage);

    AMF_ASSIGN_PROPERTY_SIZE(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_FRAMESIZE, framesize);

    AMF_ASSIGN_PROPERTY_RATE(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_FRAMERATE, framerate);

    switch (avctx->profile) {
    case AV_PROFILE_AV1_MAIN:
        profile = AMF_VIDEO_ENCODER_AV1_PROFILE_MAIN;
        break;
    default:
        break;
    }
    if (profile == 0) {
        profile = ctx->profile;
    }
    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_PROFILE, profile);

    profile_level = avctx->level;
    if (profile_level == AV_LEVEL_UNKNOWN) {
        profile_level = ctx->level;
    }
    if (profile_level != 0) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_LEVEL, profile_level);
    }
    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET, ctx->quality);

    // Maximum Reference Frames
    if (avctx->refs != -1) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_MAX_NUM_REFRAMES, avctx->refs);
    }

    // Picture control properties
    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_GOP_SIZE, avctx->gop_size);

    // Setup header insertion mode only if this option was defined explicitly
    if (ctx->header_insertion_mode != -1)
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_HEADER_INSERTION_MODE, ctx->header_insertion_mode);

    // Rate control
    // autodetect rate control method
    if (ctx->rate_control_mode == AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_UNKNOWN) {
        if (ctx->min_qp_i != -1 || ctx->max_qp_i != -1 ||
            ctx->min_qp_p != -1 || ctx->max_qp_p != -1 ||
            ctx->qp_i != -1 || ctx->qp_p != -1) {
            ctx->rate_control_mode = AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CONSTANT_QP;
            av_log(ctx, AV_LOG_DEBUG, "Rate control turned to CQP\n");
        }
        else if (avctx->rc_max_rate > 0) {
            ctx->rate_control_mode = AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR;
            av_log(ctx, AV_LOG_DEBUG, "Rate control turned to Peak VBR\n");
        }
        else {
            ctx->rate_control_mode = AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CBR;
            av_log(ctx, AV_LOG_DEBUG, "Rate control turned to CBR\n");
        }
    }

    // Pre-Pass, Pre-Analysis, Two-Pass
    if (ctx->rate_control_mode == AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CONSTANT_QP) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_PREENCODE, 0);
        if (ctx->preencode)
            av_log(ctx, AV_LOG_WARNING, "Preencode is not supported by cqp Rate Control Method, automatically disabled\n");
    }
    else {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_PREENCODE, ctx->preencode);
    }

    if (ctx->rate_control_mode == AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_QUALITY_VBR) {
        if (ctx->qvbr_quality_level != -1) {
            AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_QVBR_QUALITY_LEVEL, ctx->qvbr_quality_level);
        }
    }

    if (ctx->hw_high_motion_quality_boost != -1) {
        AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_HIGH_MOTION_QUALITY_BOOST, ((ctx->hw_high_motion_quality_boost == 0) ? false : true));
    }

    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD, ctx->rate_control_mode);
    if (avctx->rc_buffer_size) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_VBV_BUFFER_SIZE, avctx->rc_buffer_size);

        if (avctx->rc_initial_buffer_occupancy != 0) {
            int amf_buffer_fullness = avctx->rc_initial_buffer_occupancy * 64 / avctx->rc_buffer_size;
            if (amf_buffer_fullness > 64)
                amf_buffer_fullness = 64;
            AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_INITIAL_VBV_BUFFER_FULLNESS, amf_buffer_fullness);
        }
    }

    // init dynamic rate control params
    if (ctx->max_au_size)
        ctx->enforce_hrd = 1;
    AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_ENFORCE_HRD, ctx->enforce_hrd);
    AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_FILLER_DATA, ctx->filler_data);

    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_TARGET_BITRATE, avctx->bit_rate);

    if (ctx->rate_control_mode == AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CBR) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_PEAK_BITRATE, avctx->bit_rate);
    }
    if (avctx->rc_max_rate) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_PEAK_BITRATE, avctx->rc_max_rate);
    }
    else if (ctx->rate_control_mode == AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR) {
        av_log(ctx, AV_LOG_WARNING, "rate control mode is PEAK_CONSTRAINED_VBR but rc_max_rate is not set\n");
    }
    if (avctx->bit_rate > 0) {
        ctx->rate_control_mode = AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CBR;
        av_log(ctx, AV_LOG_DEBUG, "Rate control turned to CBR\n");
    }

    switch (ctx->align)
    {
    case AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE_64X16_ONLY:
        if (avctx->width / 64 * 64 != avctx->width || avctx->height / 16 * 16 != avctx->height)
        {
            res = AMF_NOT_SUPPORTED;
            av_log(ctx, AV_LOG_ERROR, "Resolution incorrect for alignment mode\n");
            return AVERROR_EXIT;
        }
        break;
    case AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE_64X16_1080P_CODED_1082:
        if ((avctx->width / 64 * 64 == avctx->width && avctx->height / 16 * 16 == avctx->height) || (avctx->width == 1920 && avctx->height == 1080))
        {
            res = AMF_OK;
        }
        else
        {
            res = AMF_NOT_SUPPORTED;
            av_log(ctx, AV_LOG_ERROR, "Resolution incorrect for alignment mode\n");
            return AVERROR_EXIT;
        }
        break;
    case AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE_NO_RESTRICTIONS:
        res = AMF_OK;
        break;
    default:
        res = AMF_NOT_SUPPORTED;
        av_log(ctx, AV_LOG_ERROR, "Invalid alignment mode\n");
        return AVERROR_EXIT;
    }
    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE, ctx->align);

    if (ctx->preanalysis != -1) {
        AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_PRE_ANALYSIS_ENABLE, !!((ctx->preanalysis == 0) ? false : true));
    }

    res = ctx->encoder->pVtbl->GetProperty(ctx->encoder, AMF_VIDEO_ENCODER_AV1_PRE_ANALYSIS_ENABLE, &var);
    if ((int)var.int64Value)
    {
        if (ctx->pa_activity_type != -1) {
            AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_PA_ACTIVITY_TYPE, ctx->pa_activity_type);
        }
        if (ctx->pa_scene_change_detection != -1) {
            AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_PA_SCENE_CHANGE_DETECTION_ENABLE, ((ctx->pa_scene_change_detection == 0) ? false : true));
        }
        if (ctx->pa_scene_change_detection_sensitivity != -1) {
            AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_PA_SCENE_CHANGE_DETECTION_SENSITIVITY, ctx->pa_scene_change_detection_sensitivity);
        }
        if (ctx->pa_static_scene_detection != -1) {
            AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_PA_STATIC_SCENE_DETECTION_ENABLE, ((ctx->pa_static_scene_detection == 0) ? false : true));
        }
        if (ctx->pa_static_scene_detection_sensitivity != -1) {
            AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_PA_STATIC_SCENE_DETECTION_SENSITIVITY, ctx->pa_static_scene_detection_sensitivity);
        }
        if (ctx->pa_initial_qp != -1) {
            AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_PA_INITIAL_QP_AFTER_SCENE_CHANGE, ctx->pa_initial_qp);
        }
        if (ctx->pa_max_qp != -1) {
            AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_PA_MAX_QP_BEFORE_FORCE_SKIP, ctx->pa_max_qp);
        }
        if (ctx->pa_caq_strength != -1) {
            AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_PA_CAQ_STRENGTH, ctx->pa_caq_strength);
        }
        if (ctx->pa_frame_sad != -1) {
            AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_PA_FRAME_SAD_ENABLE, ((ctx->pa_frame_sad == 0) ? false : true));
        }
        if (ctx->pa_paq_mode != -1) {
            AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_PA_PAQ_MODE, ctx->pa_paq_mode);
        }
        if (ctx->pa_taq_mode != -1) {
            AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_PA_TAQ_MODE, ctx->pa_taq_mode);
        }
        if (ctx->pa_ltr != -1) {
            AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_PA_LTR_ENABLE, ((ctx->pa_ltr == 0) ? false : true));
        }
        if (ctx->pa_lookahead_buffer_depth != -1) {
            AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_PA_LOOKAHEAD_BUFFER_DEPTH, ctx->pa_lookahead_buffer_depth);
        }
        if (ctx->pa_high_motion_quality_boost_mode != -1) {
            AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_PA_HIGH_MOTION_QUALITY_BOOST_MODE, ctx->pa_high_motion_quality_boost_mode);
        }
    }

    // init encoder
    res = ctx->encoder->pVtbl->Init(ctx->encoder, ctx->format, avctx->width, avctx->height);
    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_BUG, "encoder->Init() failed with error %d\n", res);

    // init dynamic picture control params
    if (ctx->min_qp_i != -1) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_MIN_Q_INDEX_INTRA, ctx->min_qp_i);
    }
    else if (avctx->qmin != -1) {
        int qval = avctx->qmin > 255 ? 255 : avctx->qmin;
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_MIN_Q_INDEX_INTRA, qval);
    }
    if (ctx->max_qp_i != -1) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_MAX_Q_INDEX_INTRA, ctx->max_qp_i);
    }
    else if (avctx->qmax != -1) {
        int qval = avctx->qmax > 255 ? 255 : avctx->qmax;
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_MAX_Q_INDEX_INTRA, qval);
    }
    if (ctx->min_qp_p != -1) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_MIN_Q_INDEX_INTER, ctx->min_qp_p);
    }
    else if (avctx->qmin != -1) {
        int qval = avctx->qmin > 255 ? 255 : avctx->qmin;
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_MIN_Q_INDEX_INTER, qval);
    }
    if (ctx->max_qp_p != -1) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_MAX_Q_INDEX_INTER, ctx->max_qp_p);
    }
    else if (avctx->qmax != -1) {
        int qval = avctx->qmax > 255 ? 255 : avctx->qmax;
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_MAX_Q_INDEX_INTER, qval);
    }

    if (ctx->qp_p != -1) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_Q_INDEX_INTER, ctx->qp_p);
    }
    if (ctx->qp_i != -1) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_Q_INDEX_INTRA, ctx->qp_i);
    }
    AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_SKIP_FRAME, ctx->skip_frame);

    // fill extradata
    res = AMFVariantInit(&var);
    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_BUG, "AMFVariantInit() failed with error %d\n", res);

    res = ctx->encoder->pVtbl->GetProperty(ctx->encoder, AMF_VIDEO_ENCODER_AV1_EXTRA_DATA, &var);
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

static const FFCodecDefault defaults[] = {
    { "refs",       "-1"  },
    { "aspect",     "0"   },
    { "b",          "2M"  },
    { "g",          "250" },
    { "qmin",       "-1"  },
    { "qmax",       "-1"  },
    { NULL                },
};

static const AVClass av1_amf_class = {
    .class_name = "av1_amf",
    .item_name = av_default_item_name,
    .option = options,
    .version = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_av1_amf_encoder = {
    .p.name           = "av1_amf",
    CODEC_LONG_NAME("AMD AMF AV1 encoder"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_AV1,
    .init           = amf_encode_init_av1,
    FF_CODEC_RECEIVE_PACKET_CB(ff_amf_receive_packet),
    .close          = ff_amf_encode_close,
    .priv_data_size = sizeof(AmfContext),
    .p.priv_class     = &av1_amf_class,
    .defaults       = defaults,
    .p.capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE |
                      AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .p.pix_fmts       = ff_amf_pix_fmts,
    .p.wrapper_name   = "amf",
    .hw_configs     = ff_amfenc_hw_configs,
};
