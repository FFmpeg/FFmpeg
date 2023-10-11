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
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "amfenc.h"
#include "codec_internal.h"
#include <AMF/components/PreAnalysis.h>

#define OFFSET(x) offsetof(AmfContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM

static const AVOption options[] = {
    // Static
    /// Usage
    { "usage",          "Encoder Usage",        OFFSET(usage),  AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, AMF_VIDEO_ENCODER_USAGE_LOW_LATENCY_HIGH_QUALITY, VE, .unit = "usage" },
    { "transcoding",    "Generic Transcoding",          0,      AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_USAGE_TRANSCODING       }, 0, 0, VE, .unit = "usage" },
    { "ultralowlatency","Ultra low latency usecase",    0,      AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_USAGE_ULTRA_LOW_LATENCY }, 0, 0, VE, .unit = "usage" },
    { "lowlatency",     "Low latency usecase",          0,      AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_USAGE_LOW_LATENCY       }, 0, 0, VE, .unit = "usage" },
    { "webcam",         "Webcam",                       0,      AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_USAGE_WEBCAM            }, 0, 0, VE, .unit = "usage" },
    { "high_quality",   "High quality usecase",         0,      AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_USAGE_HIGH_QUALITY      }, 0, 0, VE, .unit = "usage" },
    { "lowlatency_high_quality", "Low latency yet high quality usecase",  0, AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_USAGE_LOW_LATENCY_HIGH_QUALITY }, 0, 0, VE, .unit = "usage" },

    /// Profile,
    { "profile",        "Profile",              OFFSET(profile),AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, AMF_VIDEO_ENCODER_PROFILE_CONSTRAINED_HIGH, VE, .unit = "profile" },
    { "main",           "",                     0,              AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_PROFILE_MAIN                 }, 0, 0, VE, .unit = "profile" },
    { "high",           "",                     0,              AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_PROFILE_HIGH                 }, 0, 0, VE, .unit = "profile" },
    { "constrained_baseline", "",               0,              AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_PROFILE_CONSTRAINED_BASELINE }, 0, 0, VE, .unit = "profile" },
    { "constrained_high",     "",               0,              AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_PROFILE_CONSTRAINED_HIGH     }, 0, 0, VE, .unit = "profile" },

    /// Profile Level
    { "level",          "Profile Level",        OFFSET(level),  AV_OPT_TYPE_INT,   { .i64 = 0  }, 0, 62, VE, .unit = "level" },
    { "auto",           "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 0  }, 0, 0,  VE, .unit = "level" },
    { "1.0",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 10 }, 0, 0,  VE, .unit = "level" },
    { "1.1",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 11 }, 0, 0,  VE, .unit = "level" },
    { "1.2",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 12 }, 0, 0,  VE, .unit = "level" },
    { "1.3",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 13 }, 0, 0,  VE, .unit = "level" },
    { "2.0",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 20 }, 0, 0,  VE, .unit = "level" },
    { "2.1",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 21 }, 0, 0,  VE, .unit = "level" },
    { "2.2",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 22 }, 0, 0,  VE, .unit = "level" },
    { "3.0",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 30 }, 0, 0,  VE, .unit = "level" },
    { "3.1",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 31 }, 0, 0,  VE, .unit = "level" },
    { "3.2",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 32 }, 0, 0,  VE, .unit = "level" },
    { "4.0",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 40 }, 0, 0,  VE, .unit = "level" },
    { "4.1",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 41 }, 0, 0,  VE, .unit = "level" },
    { "4.2",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 42 }, 0, 0,  VE, .unit = "level" },
    { "5.0",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 50 }, 0, 0,  VE, .unit = "level" },
    { "5.1",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 51 }, 0, 0,  VE, .unit = "level" },
    { "5.2",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 52 }, 0, 0,  VE, .unit = "level" },
    { "6.0",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 60 }, 0, 0,  VE, .unit = "level" },
    { "6.1",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 61 }, 0, 0,  VE, .unit = "level" },
    { "6.2",            "",                     0,              AV_OPT_TYPE_CONST, { .i64 = 62 }, 0, 0,  VE, .unit = "level" },

    { "latency",        "enables low latency mode", OFFSET(latency), AV_OPT_TYPE_BOOL, {.i64 = -1 },  -1, 1, VE },

    /// Quality Preset
    { "quality",        "Set the encoding quality preset",  OFFSET(quality),    AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, AMF_VIDEO_ENCODER_QUALITY_PRESET_QUALITY, VE, .unit = "quality" },
    { "preset",         "Set the encoding quality preset",  OFFSET(quality),    AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, AMF_VIDEO_ENCODER_QUALITY_PRESET_QUALITY, VE, .unit = "quality" },
    { "balanced",       "Balanced",                         0,                  AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_QUALITY_PRESET_BALANCED },    0, 0, VE, .unit = "quality" },
    { "speed",          "Prefer Speed",                     0,                  AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED    },    0, 0, VE, .unit = "quality" },
    { "quality",        "Prefer Quality",                   0,                  AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_QUALITY_PRESET_QUALITY  },    0, 0, VE, .unit = "quality" },

    // Dynamic
    /// Rate Control Method
    { "rc",             "Rate Control Method",                  OFFSET(rate_control_mode), AV_OPT_TYPE_INT,   { .i64 = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_UNKNOWN }, AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_UNKNOWN, AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_HIGH_QUALITY_CBR, VE, .unit = "rc" },
    { "cqp",            "Constant Quantization Parameter",      0,                         AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP             }, 0, 0, VE, .unit = "rc" },
    { "cbr",            "Constant Bitrate",                     0,                         AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR                     }, 0, 0, VE, .unit = "rc" },
    { "vbr_peak",       "Peak Contrained Variable Bitrate",     0,                         AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR    }, 0, 0, VE, .unit = "rc" },
    { "vbr_latency",    "Latency Constrained Variable Bitrate", 0,                         AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR }, 0, 0, VE, .unit = "rc" },
    { "qvbr",           "Quality Variable Bitrate",             0,                         AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_QUALITY_VBR             }, 0, 0, VE, .unit = "rc" },
    { "hqvbr",          "High Quality Variable Bitrate",        0,                         AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_HIGH_QUALITY_VBR        }, 0, 0, VE, .unit = "rc" },
    { "hqcbr",          "High Quality Constant Bitrate",        0,                         AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_HIGH_QUALITY_CBR        }, 0, 0, VE, .unit = "rc" },

    { "qvbr_quality_level",     "Sets the QVBR quality level",  OFFSET(qvbr_quality_level),AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, 51, VE },

    /// Enforce HRD, Filler Data, VBAQ, Frame Skipping
    { "enforce_hrd",    "Enforce HRD",                          OFFSET(enforce_hrd),        AV_OPT_TYPE_BOOL, { .i64 = -1 }, -1, 1, VE },
    { "filler_data",    "Filler Data Enable",                   OFFSET(filler_data),        AV_OPT_TYPE_BOOL, { .i64 = -1 }, -1, 1, VE },
    { "vbaq",           "Enable VBAQ",                          OFFSET(enable_vbaq),        AV_OPT_TYPE_BOOL, { .i64 = -1 }, -1, 1, VE },
    { "frame_skipping", "Rate Control Based Frame Skip",        OFFSET(skip_frame),         AV_OPT_TYPE_BOOL, { .i64 = -1 }, -1, 1, VE },

    /// QP Values
    { "qp_i",           "Quantization Parameter for I-Frame",   OFFSET(qp_i),               AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 51, VE },
    { "qp_p",           "Quantization Parameter for P-Frame",   OFFSET(qp_p),               AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 51, VE },
    { "qp_b",           "Quantization Parameter for B-Frame",   OFFSET(qp_b),               AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 51, VE },

    /// Pre-Pass, Pre-Analysis, Two-Pass
    { "preencode",      "Pre-encode assisted rate control",     OFFSET(preencode),          AV_OPT_TYPE_BOOL,{ .i64 = -1 }, -1, 1, VE, NULL },

    /// Maximum Access Unit Size
    { "max_au_size",    "Maximum Access Unit Size for rate control (in bits)",   OFFSET(max_au_size),        AV_OPT_TYPE_INT, { .i64 = -1 }, -1, INT_MAX, VE },

    /// Header Insertion Spacing
    { "header_spacing", "Header Insertion Spacing",             OFFSET(header_spacing),     AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 1000, VE },

    /// B-Frames
    // BPicturesPattern=bf
    { "bf_delta_qp",    "B-Picture Delta QP",                   OFFSET(b_frame_delta_qp),   AV_OPT_TYPE_INT,  { .i64 = 4 }, -10, 10, VE },
    { "bf_ref",         "Enable Reference to B-Frames",         OFFSET(b_frame_ref),        AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, VE },
    { "bf_ref_delta_qp","Reference B-Picture Delta QP",         OFFSET(ref_b_frame_delta_qp), AV_OPT_TYPE_INT,  { .i64 = 4 }, -10, 10, VE },

    { "max_b_frames",   "Maximum number of consecutive B Pictures", OFFSET(max_consecutive_b_frames), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 3, VE },
    { "bf",             "B Picture Pattern",                        OFFSET(max_b_frames),             AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 3, VE },

    /// Intra-Refresh
    { "intra_refresh_mb","Intra Refresh MBs Number Per Slot in Macroblocks",       OFFSET(intra_refresh_mb),    AV_OPT_TYPE_INT, { .i64 = -1 }, -1, INT_MAX, VE },

    /// coder
    { "coder",          "Coding Type",                          OFFSET(coding_mode),   AV_OPT_TYPE_INT,   { .i64 = AMF_VIDEO_ENCODER_UNDEFINED }, AMF_VIDEO_ENCODER_UNDEFINED, AMF_VIDEO_ENCODER_CALV, VE, .unit = "coder" },
    { "auto",           "Automatic",                            0,                     AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_UNDEFINED }, 0, 0, VE, .unit = "coder" },
    { "cavlc",          "Context Adaptive Variable-Length Coding", 0,                  AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_CALV },      0, 0, VE, .unit = "coder" },
    { "cabac",          "Context Adaptive Binary Arithmetic Coding", 0,                AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_ENCODER_CABAC },     0, 0, VE, .unit = "coder" },

    { "high_motion_quality_boost_enable",   "Enable High motion quality boost mode",  OFFSET(hw_high_motion_quality_boost), AV_OPT_TYPE_BOOL,   {.i64 = -1 }, -1, 1, VE },

    { "me_half_pel",    "Enable ME Half Pixel",                 OFFSET(me_half_pel),   AV_OPT_TYPE_BOOL,  { .i64 = -1 }, -1, 1, VE },
    { "me_quarter_pel", "Enable ME Quarter Pixel",              OFFSET(me_quarter_pel),AV_OPT_TYPE_BOOL,  { .i64 = -1 }, -1, 1, VE },

    { "aud",            "Inserts AU Delimiter NAL unit",        OFFSET(aud)          , AV_OPT_TYPE_BOOL,  { .i64 = -1 }, -1, 1, VE },


    { "log_to_dbg",     "Enable AMF logging to debug output",   OFFSET(log_to_dbg)    , AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },

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

    { "pa_adaptive_mini_gop",                  "Enable Adaptive MiniGOP",                                      OFFSET(pa_adaptive_mini_gop),                      AV_OPT_TYPE_BOOL,   { .i64 = -1 }, -1, 1, VE },
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
    amf_int64                        color_profile;
    enum                             AVPixelFormat pix_fmt;

    if (avctx->framerate.num > 0 && avctx->framerate.den > 0) {
        framerate = AMFConstructRate(avctx->framerate.num, avctx->framerate.den);
    } else {
FF_DISABLE_DEPRECATION_WARNINGS
        framerate = AMFConstructRate(avctx->time_base.den, avctx->time_base.num
#if FF_API_TICKS_PER_FRAME
                                     * avctx->ticks_per_frame
#endif
                                     );
FF_ENABLE_DEPRECATION_WARNINGS
    }

    if ((ret = ff_amf_encode_init(avctx)) != 0)
        return ret;

    // init static parameters
    if (ctx->usage != -1) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_USAGE, ctx->usage);
    }

    AMF_ASSIGN_PROPERTY_SIZE(res, ctx->encoder, AMF_VIDEO_ENCODER_FRAMESIZE, framesize);

    AMF_ASSIGN_PROPERTY_RATE(res, ctx->encoder, AMF_VIDEO_ENCODER_FRAMERATE, framerate);

    switch (avctx->profile) {
    case AV_PROFILE_H264_BASELINE:
        profile = AMF_VIDEO_ENCODER_PROFILE_BASELINE;
        break;
    case AV_PROFILE_H264_MAIN:
        profile = AMF_VIDEO_ENCODER_PROFILE_MAIN;
        break;
    case AV_PROFILE_H264_HIGH:
        profile = AMF_VIDEO_ENCODER_PROFILE_HIGH;
        break;
    case AV_PROFILE_H264_CONSTRAINED_BASELINE:
        profile = AMF_VIDEO_ENCODER_PROFILE_CONSTRAINED_BASELINE;
        break;
    case (AV_PROFILE_H264_HIGH | AV_PROFILE_H264_CONSTRAINED):
        profile = AMF_VIDEO_ENCODER_PROFILE_CONSTRAINED_HIGH;
        break;
    }
    if (profile == 0) {
        if (ctx->profile != -1) {
            profile = ctx->profile;
        }
    }

    if (profile != 0) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_PROFILE, profile);
    }

    profile_level = avctx->level;
    if (profile_level == AV_LEVEL_UNKNOWN) {
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

    color_profile = ff_amf_get_color_profile(avctx);
    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_OUTPUT_COLOR_PROFILE, color_profile);

    /// Color Range (Support for older Drivers)
    AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_FULL_RANGE_COLOR, !!(avctx->color_range == AVCOL_RANGE_JPEG));

    /// Color Depth
    pix_fmt = avctx->hw_frames_ctx ? ((AVHWFramesContext*)avctx->hw_frames_ctx->data)->sw_format
                                : avctx->pix_fmt;

    // 10 bit input video is not supported by AMF H264 encoder
    AMF_RETURN_IF_FALSE(ctx, pix_fmt != AV_PIX_FMT_P010, AVERROR_INVALIDDATA, "10-bit input video is not supported by AMF H264 encoder\n");

    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_COLOR_BIT_DEPTH, AMF_COLOR_BIT_DEPTH_8);
    /// Color Transfer Characteristics (AMF matches ISO/IEC)
    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_OUTPUT_TRANSFER_CHARACTERISTIC, (amf_int64)avctx->color_trc);
    /// Color Primaries (AMF matches ISO/IEC)
    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_OUTPUT_COLOR_PRIMARIES, (amf_int64)avctx->color_primaries);

    // autodetect rate control method
    if (ctx->rate_control_mode == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_UNKNOWN) {
        if (ctx->qp_i != -1 || ctx->qp_p != -1 || ctx->qp_b != -1) {
            ctx->rate_control_mode = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP;
            av_log(ctx, AV_LOG_DEBUG, "Rate control turned to CQP\n");
        } else if (avctx->bit_rate > 0 && avctx->rc_max_rate == avctx->bit_rate) {
            ctx->rate_control_mode = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR;
            av_log(ctx, AV_LOG_DEBUG, "Rate control turned to CBR\n");
        } else {
            ctx->rate_control_mode = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR;
            av_log(ctx, AV_LOG_DEBUG, "Rate control turned to Peak VBR\n");
        }
    }

    if (ctx->rate_control_mode == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_PREENCODE_ENABLE, AMF_VIDEO_ENCODER_PREENCODE_DISABLED);
        if (ctx->preencode != -1) {
            if (ctx->preencode) {
                av_log(ctx, AV_LOG_WARNING, "Preencode is not supported by cqp Rate Control Method, automatically disabled\n");
            }
        }
    }
    else {
        if (ctx->preencode != -1) {
            AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_PREENCODE_ENABLE, ctx->preencode);
        }
    }

    if (ctx->rate_control_mode == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_QUALITY_VBR) {
        if (ctx->qvbr_quality_level != -1) {
            AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_QVBR_QUALITY_LEVEL, ctx->qvbr_quality_level);
        }
    }

    if (ctx->hw_high_motion_quality_boost != -1) {
        AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_HIGH_MOTION_QUALITY_BOOST_ENABLE, ((ctx->hw_high_motion_quality_boost == 0) ? false : true));
    }

    if (ctx->quality != -1) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_QUALITY_PRESET, ctx->quality);
    }

    // Dynamic parameters
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
    /// Maximum Access Unit Size and AUD
    if (ctx->max_au_size != -1) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_MAX_AU_SIZE, ctx->max_au_size);
    }

    if (ctx->aud != -1) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_INSERT_AUD, ctx->aud);
    }

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

    if (avctx->bit_rate) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_TARGET_BITRATE, avctx->bit_rate);
    }

    if (ctx->rate_control_mode == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR) {
        if (avctx->bit_rate) {
            AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_PEAK_BITRATE, avctx->bit_rate);
        }
    }

    if (avctx->rc_max_rate) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_PEAK_BITRATE, avctx->rc_max_rate);
    } else if (ctx->rate_control_mode == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR) {
        av_log(ctx, AV_LOG_WARNING, "rate control mode is PEAK_CONSTRAINED_VBR but rc_max_rate is not set\n");
    }

    if (ctx->latency != -1) {
        AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_LOWLATENCY_MODE, ((ctx->latency == 0) ? false : true));
    }

    if (ctx->preanalysis != -1) {
        AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_PRE_ANALYSIS_ENABLE, !!((ctx->preanalysis == 0) ? false : true));
    }

    res = ctx->encoder->pVtbl->GetProperty(ctx->encoder, AMF_VIDEO_ENCODER_PRE_ANALYSIS_ENABLE, &var);
    if ((int)var.int64Value)
    {
        AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_PRE_ANALYSIS_ENABLE, true);

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
        if (ctx->pa_adaptive_mini_gop != -1) {
            AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_ADAPTIVE_MINIGOP, ((ctx->pa_adaptive_mini_gop == 0) ? false : true));
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

    // B-Frames
    if (ctx->max_consecutive_b_frames != -1) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_MAX_CONSECUTIVE_BPICTURES, ctx->max_consecutive_b_frames);
        if (ctx->max_b_frames != -1) {
            AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_B_PIC_PATTERN, ctx->max_b_frames);
            if (res != AMF_OK) {
                res = ctx->encoder->pVtbl->GetProperty(ctx->encoder, AMF_VIDEO_ENCODER_B_PIC_PATTERN, &var);
                av_log(ctx, AV_LOG_WARNING, "B-frames=%d is not supported by this GPU, switched to %d\n",
                    ctx->max_b_frames, (int)var.int64Value);
                ctx->max_b_frames = (int)var.int64Value;
            }
            if (ctx->max_consecutive_b_frames < ctx->max_b_frames) {
                av_log(ctx, AVERROR_BUG, "Maxium B frames needs to be greater than the specified B frame count.\n");
            }
        }
    }
    else {
        if (ctx->max_b_frames != -1) {
            av_log(ctx, AVERROR_BUG, "Maxium number of B frames needs to be specified.\n");
        }
    }
    res = ctx->encoder->pVtbl->GetProperty(ctx->encoder, AMF_VIDEO_ENCODER_B_PIC_PATTERN, &var);
    if ((int)var.int64Value) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_B_PIC_DELTA_QP, ctx->b_frame_delta_qp);
        AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_B_REFERENCE_ENABLE, !!ctx->b_frame_ref);
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_REF_B_PIC_DELTA_QP, ctx->ref_b_frame_delta_qp);
    }

    // Initialize Encoder
    res = ctx->encoder->pVtbl->Init(ctx->encoder, ctx->format, avctx->width, avctx->height);
    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_BUG, "encoder->Init() failed with error %d\n", res);

    // Enforce HRD, Filler Data, VBAQ, Frame Skipping, Deblocking Filter
    if (ctx->enforce_hrd != -1) {
        AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_ENFORCE_HRD, ((ctx->enforce_hrd == 0) ? false : true));
    }

    if (ctx->filler_data != -1) {
        AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_FILLER_DATA_ENABLE, ((ctx->filler_data == 0) ? false : true));
    }

    if (ctx->skip_frame != -1) {
        AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_RATE_CONTROL_SKIP_FRAME_ENABLE, ((ctx->skip_frame == 0) ? false : true));
    }

    if (ctx->rate_control_mode == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP) {
        AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_ENABLE_VBAQ, 0);
        if (ctx->enable_vbaq)
            av_log(ctx, AV_LOG_WARNING, "VBAQ is not supported by cqp Rate Control Method, automatically disabled\n");
    } else {
        if (ctx->enable_vbaq != -1) {
            AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_ENABLE_VBAQ, !!ctx->enable_vbaq);
        }
    }
    AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_DE_BLOCKING_FILTER, !!deblocking_filter);

    // Keyframe Interval
    AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_IDR_PERIOD, avctx->gop_size);

    // Header Insertion Spacing
    if (ctx->header_spacing >= 0)
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_HEADER_INSERTION_SPACING, ctx->header_spacing);

    // Intra-Refresh, Slicing
    if (ctx->intra_refresh_mb != -1)
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_INTRA_REFRESH_NUM_MBS_PER_SLOT, ctx->intra_refresh_mb);
    if (avctx->slices > 1)
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_SLICES_PER_FRAME, avctx->slices);

    // Coding
    if (ctx->coding_mode != 0)
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->encoder, AMF_VIDEO_ENCODER_CABAC_ENABLE, ctx->coding_mode);

    // Motion Estimation
    if (ctx->me_half_pel != -1) {
        AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_MOTION_HALF_PIXEL, !!ctx->me_half_pel);
    }

    if (ctx->me_quarter_pel != -1) {
        AMF_ASSIGN_PROPERTY_BOOL(res, ctx->encoder, AMF_VIDEO_ENCODER_MOTION_QUARTERPIXEL, !!ctx->me_quarter_pel);
    }

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

static const FFCodecDefault defaults[] = {
    { "refs",       "-1"  },
    { "aspect",     "0"   },
    { "qmin",       "-1"  },
    { "qmax",       "-1"  },
    { "b",          "0"   },
    { "g",          "-1"  },
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

const FFCodec ff_h264_amf_encoder = {
    .p.name         = "h264_amf",
    CODEC_LONG_NAME("AMD AMF H.264 Encoder"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_H264,
    .init           = amf_encode_init_h264,
    FF_CODEC_RECEIVE_PACKET_CB(ff_amf_receive_packet),
    .close          = ff_amf_encode_close,
    .priv_data_size = sizeof(AmfContext),
    .p.priv_class   = &h264_amf_class,
    .defaults       = defaults,
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE |
                      AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
    .p.pix_fmts     = ff_amf_pix_fmts,
    .color_ranges   = AVCOL_RANGE_MPEG | AVCOL_RANGE_JPEG,
    .p.wrapper_name = "amf",
    .hw_configs     = ff_amfenc_hw_configs,
};
