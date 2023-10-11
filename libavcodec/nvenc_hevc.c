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

#include "avcodec.h"
#include "codec_internal.h"

#include "nvenc.h"

#define OFFSET(x) offsetof(NvencContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
#ifdef NVENC_HAVE_NEW_PRESETS
    { "preset",       "Set the encoding preset",            OFFSET(preset),       AV_OPT_TYPE_INT,   { .i64 = PRESET_P4 },     PRESET_DEFAULT, PRESET_P7,          VE, .unit = "preset" },
#else
    { "preset",       "Set the encoding preset",            OFFSET(preset),       AV_OPT_TYPE_INT,   { .i64 = PRESET_MEDIUM }, PRESET_DEFAULT, PRESET_LOSSLESS_HP, VE, .unit = "preset" },
#endif
    { "default",      "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_DEFAULT },             0, 0, VE, .unit = "preset" },
    { "slow",         "hq 2 passes",                        0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_SLOW },                0, 0, VE, .unit = "preset" },
    { "medium",       "hq 1 pass",                          0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_MEDIUM },              0, 0, VE, .unit = "preset" },
    { "fast",         "hp 1 pass",                          0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_FAST },                0, 0, VE, .unit = "preset" },
    { "hp",           "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_HP },                  0, 0, VE, .unit = "preset" },
    { "hq",           "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_HQ },                  0, 0, VE, .unit = "preset" },
    { "bd",           "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_BD },                  0, 0, VE, .unit = "preset" },
    { "ll",           "low latency",                        0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_LOW_LATENCY_DEFAULT }, 0, 0, VE, .unit = "preset" },
    { "llhq",         "low latency hq",                     0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_LOW_LATENCY_HQ },      0, 0, VE, .unit = "preset" },
    { "llhp",         "low latency hp",                     0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_LOW_LATENCY_HP },      0, 0, VE, .unit = "preset" },
    { "lossless",     "lossless",                           0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_LOSSLESS_DEFAULT },    0, 0, VE, .unit = "preset" },
    { "losslesshp",   "lossless hp",                        0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_LOSSLESS_HP },         0, 0, VE, .unit = "preset" },
#ifdef NVENC_HAVE_NEW_PRESETS
    { "p1",          "fastest (lowest quality)",            0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_P1 },                  0, 0, VE, .unit = "preset" },
    { "p2",          "faster (lower quality)",              0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_P2 },                  0, 0, VE, .unit = "preset" },
    { "p3",          "fast (low quality)",                  0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_P3 },                  0, 0, VE, .unit = "preset" },
    { "p4",          "medium (default)",                    0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_P4 },                  0, 0, VE, .unit = "preset" },
    { "p5",          "slow (good quality)",                 0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_P5 },                  0, 0, VE, .unit = "preset" },
    { "p6",          "slower (better quality)",             0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_P6 },                  0, 0, VE, .unit = "preset" },
    { "p7",          "slowest (best quality)",              0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_P7 },                  0, 0, VE, .unit = "preset" },
    { "tune",        "Set the encoding tuning info",        OFFSET(tuning_info),  AV_OPT_TYPE_INT,   { .i64 = NV_ENC_TUNING_INFO_HIGH_QUALITY }, NV_ENC_TUNING_INFO_HIGH_QUALITY, NV_ENC_TUNING_INFO_COUNT - 1, VE, .unit = "tune" },
    { "hq",          "High quality",                        0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_TUNING_INFO_HIGH_QUALITY },             0, 0, VE, .unit = "tune" },
#ifdef NVENC_HAVE_UHQ_TUNING
    { "uhq",         "Ultra high quality",                  0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_TUNING_INFO_ULTRA_HIGH_QUALITY },       0, 0, VE, .unit = "tune" },
#endif
    { "ll",          "Low latency",                         0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_TUNING_INFO_LOW_LATENCY },              0, 0, VE, .unit = "tune" },
    { "ull",         "Ultra low latency",                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY },        0, 0, VE, .unit = "tune" },
    { "lossless",    "Lossless",                            0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_TUNING_INFO_LOSSLESS },                 0, 0, VE, .unit = "tune" },
#endif
    { "profile",      "Set the encoding profile",           OFFSET(profile),      AV_OPT_TYPE_INT,   { .i64 = NV_ENC_HEVC_PROFILE_MAIN }, NV_ENC_HEVC_PROFILE_MAIN, AV_PROFILE_HEVC_REXT, VE, .unit = "profile" },
    { "main",         "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_HEVC_PROFILE_MAIN },    0, 0, VE, .unit = "profile" },
    { "main10",       "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_HEVC_PROFILE_MAIN_10 }, 0, 0, VE, .unit = "profile" },
    { "rext",         "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_HEVC_PROFILE_REXT },    0, 0, VE, .unit = "profile" },
    { "level",        "Set the encoding level restriction", OFFSET(level),        AV_OPT_TYPE_INT,   { .i64 = NV_ENC_LEVEL_AUTOSELECT }, NV_ENC_LEVEL_AUTOSELECT, NV_ENC_LEVEL_HEVC_62, VE, .unit = "level" },
    { "auto",         "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_AUTOSELECT },  0, 0, VE,  .unit = "level" },
    { "1",            "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_HEVC_1 },      0, 0, VE,  .unit = "level" },
    { "1.0",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_HEVC_1 },      0, 0, VE,  .unit = "level" },
    { "2",            "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_HEVC_2 },      0, 0, VE,  .unit = "level" },
    { "2.0",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_HEVC_2 },      0, 0, VE,  .unit = "level" },
    { "2.1",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_HEVC_21 },     0, 0, VE,  .unit = "level" },
    { "3",            "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_HEVC_3 },      0, 0, VE,  .unit = "level" },
    { "3.0",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_HEVC_3 },      0, 0, VE,  .unit = "level" },
    { "3.1",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_HEVC_31 },     0, 0, VE,  .unit = "level" },
    { "4",            "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_HEVC_4 },      0, 0, VE,  .unit = "level" },
    { "4.0",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_HEVC_4 },      0, 0, VE,  .unit = "level" },
    { "4.1",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_HEVC_41 },     0, 0, VE,  .unit = "level" },
    { "5",            "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_HEVC_5 },      0, 0, VE,  .unit = "level" },
    { "5.0",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_HEVC_5 },      0, 0, VE,  .unit = "level" },
    { "5.1",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_HEVC_51 },     0, 0, VE,  .unit = "level" },
    { "5.2",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_HEVC_52 },     0, 0, VE,  .unit = "level" },
    { "6",            "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_HEVC_6 },      0, 0, VE,  .unit = "level" },
    { "6.0",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_HEVC_6 },      0, 0, VE,  .unit = "level" },
    { "6.1",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_HEVC_61 },     0, 0, VE,  .unit = "level" },
    { "6.2",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_HEVC_62 },     0, 0, VE,  .unit = "level" },
    { "tier",         "Set the encoding tier",              OFFSET(tier),         AV_OPT_TYPE_INT,   { .i64 = NV_ENC_TIER_HEVC_MAIN }, NV_ENC_TIER_HEVC_MAIN, NV_ENC_TIER_HEVC_HIGH, VE, .unit = "tier"},
    { "main",         "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_TIER_HEVC_MAIN },    0, 0, VE,   .unit = "tier" },
    { "high",         "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_TIER_HEVC_HIGH },    0, 0, VE,   .unit = "tier" },
    { "rc",           "Override the preset rate-control",   OFFSET(rc),           AV_OPT_TYPE_INT,   { .i64 = -1 },                                  -1, INT_MAX, VE, .unit = "rc" },
    { "constqp",      "Constant QP mode",                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_PARAMS_RC_CONSTQP },                   0, 0, VE, .unit = "rc" },
    { "vbr",          "Variable bitrate mode",              0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_PARAMS_RC_VBR },                       0, 0, VE, .unit = "rc" },
    { "cbr",          "Constant bitrate mode",              0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_PARAMS_RC_CBR },                       0, 0, VE, .unit = "rc" },
#ifndef NVENC_NO_DEPRECATED_RC
    { "vbr_minqp",    "Variable bitrate mode with MinQP (deprecated)", 0,         AV_OPT_TYPE_CONST, { .i64 = RCD(NV_ENC_PARAMS_RC_VBR_MINQP) },            0, 0, VE, .unit = "rc" },
    { "ll_2pass_quality", "Multi-pass optimized for image quality (deprecated)",
                                                            0,                    AV_OPT_TYPE_CONST, { .i64 = RCD(NV_ENC_PARAMS_RC_2_PASS_QUALITY) },       0, 0, VE, .unit = "rc" },
    { "ll_2pass_size", "Multi-pass optimized for constant frame size (deprecated)",
                                                            0,                    AV_OPT_TYPE_CONST, { .i64 = RCD(NV_ENC_PARAMS_RC_2_PASS_FRAMESIZE_CAP) }, 0, 0, VE, .unit = "rc" },
    { "vbr_2pass",    "Multi-pass variable bitrate mode (deprecated)", 0,         AV_OPT_TYPE_CONST, { .i64 = RCD(NV_ENC_PARAMS_RC_2_PASS_VBR) },           0, 0, VE, .unit = "rc" },
    { "cbr_ld_hq",    "Constant bitrate low delay high quality mode", 0,          AV_OPT_TYPE_CONST, { .i64 = RCD(NV_ENC_PARAMS_RC_CBR_LOWDELAY_HQ) },      0, 0, VE, .unit = "rc" },
    { "cbr_hq",       "Constant bitrate high quality mode", 0,                    AV_OPT_TYPE_CONST, { .i64 = RCD(NV_ENC_PARAMS_RC_CBR_HQ) },               0, 0, VE, .unit = "rc" },
    { "vbr_hq",       "Variable bitrate high quality mode", 0,                    AV_OPT_TYPE_CONST, { .i64 = RCD(NV_ENC_PARAMS_RC_VBR_HQ) },               0, 0, VE, .unit = "rc" },
#else
    { "vbr_minqp",    "Variable bitrate mode with MinQP (deprecated)", 0,         AV_OPT_TYPE_CONST, { .i64 = RCD(NV_ENC_PARAMS_RC_VBR) },                  0, 0, VE, .unit = "rc" },
    { "ll_2pass_quality", "Multi-pass optimized for image quality (deprecated)",
                                                            0,                    AV_OPT_TYPE_CONST, { .i64 = RCD(NV_ENC_PARAMS_RC_VBR) },                  0, 0, VE, .unit = "rc" },
    { "ll_2pass_size", "Multi-pass optimized for constant frame size (deprecated)",
                                                            0,                    AV_OPT_TYPE_CONST, { .i64 = RCD(NV_ENC_PARAMS_RC_CBR) },                  0, 0, VE, .unit = "rc" },
    { "vbr_2pass",    "Multi-pass variable bitrate mode (deprecated)", 0,         AV_OPT_TYPE_CONST, { .i64 = RCD(NV_ENC_PARAMS_RC_VBR) },                  0, 0, VE, .unit = "rc" },
    { "cbr_ld_hq",    "Constant bitrate low delay high quality mode", 0,          AV_OPT_TYPE_CONST, { .i64 = RCD(NV_ENC_PARAMS_RC_CBR) },                  0, 0, VE, .unit = "rc" },
    { "cbr_hq",       "Constant bitrate high quality mode", 0,                    AV_OPT_TYPE_CONST, { .i64 = RCD(NV_ENC_PARAMS_RC_CBR) },                  0, 0, VE, .unit = "rc" },
    { "vbr_hq",       "Variable bitrate high quality mode", 0,                    AV_OPT_TYPE_CONST, { .i64 = RCD(NV_ENC_PARAMS_RC_VBR) },                  0, 0, VE, .unit = "rc" },
#endif
    { "rc-lookahead", "Number of frames to look ahead for rate-control",
                                                            OFFSET(rc_lookahead), AV_OPT_TYPE_INT,   { .i64 = 0 }, 0, INT_MAX, VE },
    { "surfaces",     "Number of concurrent surfaces",      OFFSET(nb_surfaces),  AV_OPT_TYPE_INT,   { .i64 = 0 }, 0, MAX_REGISTERED_FRAMES, VE },
    { "cbr",          "Use cbr encoding mode",              OFFSET(cbr),          AV_OPT_TYPE_BOOL,  { .i64 = 0 },   0, 1, VE },
    { "2pass",        "Use 2pass encoding mode",            OFFSET(twopass),      AV_OPT_TYPE_BOOL,  { .i64 = -1 }, -1, 1, VE },
    { "gpu",          "Selects which NVENC capable GPU to use. First GPU is 0, second is 1, and so on.",
                                                            OFFSET(device),       AV_OPT_TYPE_INT,   { .i64 = ANY_DEVICE }, -2, INT_MAX, VE, .unit = "gpu" },
    { "any",          "Pick the first device available",    0,                    AV_OPT_TYPE_CONST, { .i64 = ANY_DEVICE },        0, 0, VE, .unit = "gpu" },
    { "list",         "List the available devices",         0,                    AV_OPT_TYPE_CONST, { .i64 = LIST_DEVICES },      0, 0, VE, .unit = "gpu" },
    { "rgb_mode",     "Configure how nvenc handles packed RGB input.",
                                                            OFFSET(rgb_mode),     AV_OPT_TYPE_INT,   { .i64 = NVENC_RGB_MODE_420 }, 0, INT_MAX, VE, .unit = "rgb_mode" },
    { "yuv420",       "Convert to yuv420",                  0,                    AV_OPT_TYPE_CONST, { .i64 = NVENC_RGB_MODE_420 },       0, 0, VE, .unit = "rgb_mode" },
    { "yuv444",       "Convert to yuv444",                  0,                    AV_OPT_TYPE_CONST, { .i64 = NVENC_RGB_MODE_444 },       0, 0, VE, .unit = "rgb_mode" },
    { "disabled",     "Disables support, throws an error.", 0,                    AV_OPT_TYPE_CONST, { .i64 = NVENC_RGB_MODE_DISABLED },  0, 0, VE, .unit = "rgb_mode" },
    { "delay",        "Delay frame output by the given amount of frames",
                                                            OFFSET(async_depth),  AV_OPT_TYPE_INT,   { .i64 = INT_MAX }, 0, INT_MAX, VE },
    { "no-scenecut",  "When lookahead is enabled, set this to 1 to disable adaptive I-frame insertion at scene cuts",
                                                            OFFSET(no_scenecut),  AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0, 1, VE },
    { "forced-idr",   "If forcing keyframes, force them as IDR frames.",
                                                            OFFSET(forced_idr),   AV_OPT_TYPE_BOOL,  { .i64 = 0 }, -1, 1, VE },
    { "spatial_aq",   "set to 1 to enable Spatial AQ",      OFFSET(aq),           AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0, 1, VE },
    { "spatial-aq",   "set to 1 to enable Spatial AQ",      OFFSET(aq),           AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0, 1, VE },
    { "temporal_aq",  "set to 1 to enable Temporal AQ",     OFFSET(temporal_aq),  AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0, 1, VE },
    { "temporal-aq",  "set to 1 to enable Temporal AQ",     OFFSET(temporal_aq),  AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0, 1, VE },
    { "zerolatency",  "Set 1 to indicate zero latency operation (no reordering delay)",
                                                            OFFSET(zerolatency),  AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0, 1, VE },
    { "nonref_p",     "Set this to 1 to enable automatic insertion of non-reference P-frames",
                                                            OFFSET(nonref_p),     AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0, 1, VE },
    { "strict_gop",   "Set 1 to minimize GOP-to-GOP rate fluctuations",
                                                            OFFSET(strict_gop),   AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0, 1, VE },
    { "aq-strength",  "When Spatial AQ is enabled, this field is used to specify AQ strength. AQ strength scale is from 1 (low) - 15 (aggressive)",
                                                            OFFSET(aq_strength),  AV_OPT_TYPE_INT,   { .i64 = 8 }, 1, 15, VE },
    { "cq",           "Set target quality level (0 to 51, 0 means automatic) for constant quality mode in VBR rate control",
                                                            OFFSET(quality),      AV_OPT_TYPE_FLOAT, { .dbl = 0.}, 0., 51., VE },
    { "aud",          "Use access unit delimiters",         OFFSET(aud),          AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0, 1, VE },
    { "bluray-compat", "Bluray compatibility workarounds",  OFFSET(bluray_compat),AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0, 1, VE },
    { "init_qpP",     "Initial QP value for P frame",       OFFSET(init_qp_p),    AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, 51, VE },
    { "init_qpB",     "Initial QP value for B frame",       OFFSET(init_qp_b),    AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, 51, VE },
    { "init_qpI",     "Initial QP value for I frame",       OFFSET(init_qp_i),    AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, 51, VE },
    { "qp",           "Constant quantization parameter rate control method",
                                                            OFFSET(cqp),          AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, 51, VE },
    { "qp_cb_offset", "Quantization parameter offset for cb channel",
                                                            OFFSET(qp_cb_offset), AV_OPT_TYPE_INT,   { .i64 = 0 }, -12, 12, VE },
    { "qp_cr_offset", "Quantization parameter offset for cr channel",
                                                            OFFSET(qp_cr_offset), AV_OPT_TYPE_INT,   { .i64 = 0 }, -12, 12, VE },
    { "weighted_pred","Set 1 to enable weighted prediction",
                                                            OFFSET(weighted_pred),AV_OPT_TYPE_INT,   { .i64 = 0 }, 0, 1, VE },
#ifdef NVENC_HAVE_HEVC_BFRAME_REF_MODE
    { "b_ref_mode",   "Use B frames as references",         OFFSET(b_ref_mode),   AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, NV_ENC_BFRAME_REF_MODE_MIDDLE, VE, .unit = "b_ref_mode" },
    { "disabled",     "B frames will not be used for reference", 0,               AV_OPT_TYPE_CONST, { .i64 = NV_ENC_BFRAME_REF_MODE_DISABLED }, 0, 0, VE, .unit = "b_ref_mode" },
    { "each",         "Each B frame will be used for reference", 0,               AV_OPT_TYPE_CONST, { .i64 = NV_ENC_BFRAME_REF_MODE_EACH }, 0, 0, VE, .unit = "b_ref_mode" },
    { "middle",       "Only (number of B frames)/2 will be used for reference", 0,AV_OPT_TYPE_CONST, { .i64 = NV_ENC_BFRAME_REF_MODE_MIDDLE }, 0, 0, VE, .unit = "b_ref_mode" },
#else
    { "b_ref_mode",   "(not supported)",                    OFFSET(b_ref_mode),   AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, INT_MAX, VE, .unit = "b_ref_mode" },
    { "disabled",     "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = 0 }, 0, 0,         VE, .unit = "b_ref_mode" },
    { "each",         "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = 1 }, 0, 0,         VE, .unit = "b_ref_mode" },
    { "middle",       "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = 2 }, 0, 0,         VE, .unit = "b_ref_mode" },
#endif
    { "a53cc",        "Use A53 Closed Captions (if available)", OFFSET(a53_cc),   AV_OPT_TYPE_BOOL,  { .i64 = 1 }, 0, 1,       VE },
    { "s12m_tc",      "Use timecode (if available)",        OFFSET(s12m_tc),      AV_OPT_TYPE_BOOL,  { .i64 = 1 }, 0, 1,       VE },
    { "dpb_size",     "Specifies the DPB size used for encoding (0 means automatic)",
                                                            OFFSET(dpb_size),     AV_OPT_TYPE_INT,   { .i64 = 0 }, 0, INT_MAX, VE },
#ifdef NVENC_HAVE_MULTIPASS
    { "multipass",    "Set the multipass encoding",         OFFSET(multipass),    AV_OPT_TYPE_INT,   { .i64 = NV_ENC_MULTI_PASS_DISABLED },         NV_ENC_MULTI_PASS_DISABLED, NV_ENC_TWO_PASS_FULL_RESOLUTION, VE, .unit = "multipass" },
    { "disabled",     "Single Pass",                        0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_MULTI_PASS_DISABLED },         0,                          0,                               VE, .unit = "multipass" },
    { "qres",         "Two Pass encoding is enabled where first Pass is quarter resolution",
                                                            0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_TWO_PASS_QUARTER_RESOLUTION }, 0,                          0,                               VE, .unit = "multipass" },
    { "fullres",      "Two Pass encoding is enabled where first Pass is full resolution",
                                                            0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_TWO_PASS_FULL_RESOLUTION },    0,                          0,                               VE, .unit = "multipass" },
#endif
#ifdef NVENC_HAVE_NEW_BIT_DEPTH_API
    { "highbitdepth", "Enable 10 bit encode for 8 bit input",OFFSET(highbitdepth),AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0, 1, VE },
#endif
#ifdef NVENC_HAVE_LDKFS
    { "ldkfs",        "Low delay key frame scale; Specifies the Scene Change frame size increase allowed in case of single frame VBV and CBR",
                                                            OFFSET(ldkfs),        AV_OPT_TYPE_INT,   { .i64 = 0 }, 0, UCHAR_MAX, VE },
#endif
    { "extra_sei",    "Pass on extra SEI data (e.g. a53 cc) to be included in the bitstream",
                                                            OFFSET(extra_sei),    AV_OPT_TYPE_BOOL,  { .i64 = 1 }, 0, 1, VE },
    { "udu_sei",      "Pass on user data unregistered SEI if available",
                                                            OFFSET(udu_sei),      AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0, 1, VE },
    { "intra-refresh","Use Periodic Intra Refresh instead of IDR frames",
                                                            OFFSET(intra_refresh),AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0, 1, VE },
    { "single-slice-intra-refresh", "Use single slice intra refresh",
                                                            OFFSET(single_slice_intra_refresh), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    { "max_slice_size", "Maximum encoded slice size in bytes",
                                                            OFFSET(max_slice_size), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, VE },
    { "constrained-encoding", "Enable constrainedFrame encoding where each slice in the constrained picture is independent of other slices",
                                                            OFFSET(constrained_encoding), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
#ifdef NVENC_HAVE_TEMPORAL_FILTER
    { "tf_level",     "Specifies the strength of the temporal filtering",
                                                            OFFSET(tf_level),     AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, INT_MAX, VE, .unit = "tf_level" },
    { "0",            "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_TEMPORAL_FILTER_LEVEL_0 }, 0, 0, VE, .unit = "tf_level" },
    { "4",            "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_TEMPORAL_FILTER_LEVEL_4 }, 0, 0, VE, .unit = "tf_level" },
#endif
#ifdef NVENC_HAVE_LOOKAHEAD_LEVEL
    { "lookahead_level", "Specifies the lookahead level. Higher level may improve quality at the expense of performance.",
                                                            OFFSET(lookahead_level), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, NV_ENC_LOOKAHEAD_LEVEL_AUTOSELECT, VE, .unit = "lookahead_level" },
    { "auto",         "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LOOKAHEAD_LEVEL_AUTOSELECT }, 0, 0, VE, .unit = "lookahead_level" },
    { "0",            "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LOOKAHEAD_LEVEL_0 }, 0, 0, VE, .unit = "lookahead_level" },
    { "1",            "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LOOKAHEAD_LEVEL_1 }, 0, 0, VE, .unit = "lookahead_level" },
    { "2",            "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LOOKAHEAD_LEVEL_2 }, 0, 0, VE, .unit = "lookahead_level" },
    { "3",            "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LOOKAHEAD_LEVEL_3 }, 0, 0, VE, .unit = "lookahead_level" },
#endif
#ifdef NVENC_HAVE_UNIDIR_B
    { "unidir_b",     "Enable use of unidirectional B-Frames.", OFFSET(unidir_b), AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0, 1, VE },
#endif
#ifdef NVENC_HAVE_SPLIT_FRAME_ENCODING
    { "split_encode_mode", "Specifies the split encoding mode", OFFSET(split_encode_mode), AV_OPT_TYPE_INT, { .i64 = NV_ENC_SPLIT_AUTO_MODE }, 0, NV_ENC_SPLIT_DISABLE_MODE, VE, .unit = "split_encode_mode" },
    { "disabled",          "Disabled for all configurations",                                                0, AV_OPT_TYPE_CONST, { .i64 = NV_ENC_SPLIT_DISABLE_MODE },      0, 0, VE, .unit = "split_encode_mode" },
    { "auto",              "Enabled or disabled depending on the preset and tuning info",                    0, AV_OPT_TYPE_CONST, { .i64 = NV_ENC_SPLIT_AUTO_MODE },         0, 0, VE, .unit = "split_encode_mode" },
    { "forced",            "Enabled with number of horizontal strips selected by the driver",                0, AV_OPT_TYPE_CONST, { .i64 = NV_ENC_SPLIT_AUTO_FORCED_MODE },  0, 0, VE, .unit = "split_encode_mode" },
    { "2",                 "Enabled with number of horizontal strips forced to 2 when number of NVENCs > 1", 0, AV_OPT_TYPE_CONST, { .i64 = NV_ENC_SPLIT_TWO_FORCED_MODE },   0, 0, VE, .unit = "split_encode_mode" },
    { "3",                 "Enabled with number of horizontal strips forced to 3 when number of NVENCs > 2", 0, AV_OPT_TYPE_CONST, { .i64 = NV_ENC_SPLIT_THREE_FORCED_MODE }, 0, 0, VE, .unit = "split_encode_mode" },
#endif
    { NULL }
};

static const FFCodecDefault defaults[] = {
    { "b", "2M" },
    { "qmin", "-1" },
    { "qmax", "-1" },
    { "qdiff", "-1" },
    { "qblur", "-1" },
    { "qcomp", "-1" },
    { "g", "-1" },
    { "bf", "-1" },
    { "refs", "0" },
    { NULL },
};

static const AVClass hevc_nvenc_class = {
    .class_name = "hevc_nvenc",
    .item_name = av_default_item_name,
    .option = options,
    .version = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_hevc_nvenc_encoder = {
    .p.name         = "hevc_nvenc",
    CODEC_LONG_NAME("NVIDIA NVENC hevc encoder"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_HEVC,
    .init           = ff_nvenc_encode_init,
    FF_CODEC_RECEIVE_PACKET_CB(ff_nvenc_receive_packet),
    .close          = ff_nvenc_encode_close,
    .flush          = ff_nvenc_encode_flush,
    .priv_data_size = sizeof(NvencContext),
    .p.priv_class   = &hevc_nvenc_class,
    .defaults       = defaults,
    .p.pix_fmts     = ff_nvenc_pix_fmts,
    .color_ranges   = AVCOL_RANGE_MPEG | AVCOL_RANGE_JPEG,
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE |
                      AV_CODEC_CAP_ENCODER_FLUSH | AV_CODEC_CAP_DR1 |
                      AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
    .p.wrapper_name = "nvenc",
    .hw_configs     = ff_nvenc_hw_configs,
};
