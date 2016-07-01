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
#include "internal.h"

#include "nvenc.h"

#define OFFSET(x) offsetof(NvencContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "preset",   "Set the encoding preset",              OFFSET(preset),      AV_OPT_TYPE_INT,    { .i64 = PRESET_MEDIUM }, PRESET_DEFAULT, PRESET_LOSSLESS_HP, VE, "preset" },
    { "default",    "",                                   0,                   AV_OPT_TYPE_CONST,  { .i64 = PRESET_DEFAULT }, 0, 0, VE, "preset" },
    { "slow",       "hq 2 passes",                        0,                   AV_OPT_TYPE_CONST,  { .i64 = PRESET_SLOW }, 0, 0, VE, "preset" },
    { "medium",     "hq 1 pass",                          0,                   AV_OPT_TYPE_CONST,  { .i64 = PRESET_MEDIUM }, 0, 0, VE, "preset" },
    { "fast",       "hp 1 pass",                          0,                   AV_OPT_TYPE_CONST,  { .i64 = PRESET_FAST }, 0, 0, VE, "preset" },
    { "hp",         "",                                   0,                   AV_OPT_TYPE_CONST,  { .i64 = PRESET_HP }, 0, 0, VE, "preset" },
    { "hq",         "",                                   0,                   AV_OPT_TYPE_CONST,  { .i64 = PRESET_HQ }, 0, 0, VE, "preset" },
    { "bd",         "",                                   0,                   AV_OPT_TYPE_CONST,  { .i64 = PRESET_BD }, 0, 0, VE, "preset" },
    { "ll",         "low latency",                        0,                   AV_OPT_TYPE_CONST,  { .i64 = PRESET_LOW_LATENCY_DEFAULT }, 0, 0, VE, "preset" },
    { "llhq",       "low latency hq",                     0,                   AV_OPT_TYPE_CONST,  { .i64 = PRESET_LOW_LATENCY_HQ }, 0, 0, VE, "preset" },
    { "llhp",       "low latency hp",                     0,                   AV_OPT_TYPE_CONST,  { .i64 = PRESET_LOW_LATENCY_HP }, 0, 0, VE, "preset" },
    { "lossless",   NULL,                                 0,                   AV_OPT_TYPE_CONST,  { .i64 = PRESET_LOSSLESS_DEFAULT }, 0, 0, VE, "preset" },
    { "losslesshp", NULL,                                 0,                   AV_OPT_TYPE_CONST,  { .i64 = PRESET_LOSSLESS_HP }, 0, 0, VE, "preset" },
    { "profile",  "Set the encoding profile",             OFFSET(profile),     AV_OPT_TYPE_INT,    { .i64 = NV_ENC_H264_PROFILE_MAIN }, NV_ENC_H264_PROFILE_BASELINE, NV_ENC_H264_PROFILE_HIGH_444P, VE, "profile" },
    { "baseline", "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_H264_PROFILE_BASELINE },            0, 0, VE, "profile" },
    { "main",     "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_H264_PROFILE_MAIN },                0, 0, VE, "profile" },
    { "high",     "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_H264_PROFILE_HIGH },                0, 0, VE, "profile" },
    { "high444p", "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_H264_PROFILE_HIGH_444P },            0, 0, VE, "profile" },
    { "level", "Set the encoding level restriction", OFFSET(level), AV_OPT_TYPE_INT, { .i64 = NV_ENC_LEVEL_AUTOSELECT }, NV_ENC_LEVEL_AUTOSELECT, NV_ENC_LEVEL_H264_51, VE, "level" },
    { "auto",     "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_LEVEL_AUTOSELECT },  0, 0, VE,  "level" },
    { "1",        "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_LEVEL_H264_1 },  0, 0, VE,  "level" },
    { "1.0",      "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_LEVEL_H264_1 },  0, 0, VE,  "level" },
    { "1b",       "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_LEVEL_H264_1b }, 0, 0, VE,  "level" },
    { "1.0b",     "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_LEVEL_H264_1b }, 0, 0, VE,  "level" },
    { "1.1",      "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_LEVEL_H264_11 }, 0, 0, VE,  "level" },
    { "1.2",      "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_LEVEL_H264_12 }, 0, 0, VE,  "level" },
    { "1.3",      "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_LEVEL_H264_13 }, 0, 0, VE,  "level" },
    { "2",        "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_LEVEL_H264_2 },  0, 0, VE,  "level" },
    { "2.0",      "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_LEVEL_H264_2 },  0, 0, VE,  "level" },
    { "2.1",      "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_LEVEL_H264_21 }, 0, 0, VE,  "level" },
    { "2.2",      "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_LEVEL_H264_22 }, 0, 0, VE,  "level" },
    { "3",        "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_LEVEL_H264_3 },  0, 0, VE,  "level" },
    { "3.0",      "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_LEVEL_H264_3 },  0, 0, VE,  "level" },
    { "3.1",      "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_LEVEL_H264_31 }, 0, 0, VE,  "level" },
    { "3.2",      "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_LEVEL_H264_32 }, 0, 0, VE,  "level" },
    { "4",        "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_LEVEL_H264_4 },  0, 0, VE,  "level" },
    { "4.0",      "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_LEVEL_H264_4 },  0, 0, VE,  "level" },
    { "4.1",      "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_LEVEL_H264_41 }, 0, 0, VE,  "level" },
    { "4.2",      "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_LEVEL_H264_42 }, 0, 0, VE,  "level" },
    { "5",        "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_LEVEL_H264_5 },  0, 0, VE,  "level" },
    { "5.0",      "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_LEVEL_H264_5 },  0, 0, VE,  "level" },
    { "5.1",      "",                                     0,                   AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_LEVEL_H264_51 }, 0, 0, VE,  "level" },
    { "rc",       "Override the preset rate-control",     OFFSET(rc),          AV_OPT_TYPE_INT,    { .i64 = -1 },                   -1, INT_MAX, VE, "rc" },
    { "constqp",          "Constant QP mode",                                                            0, AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_PARAMS_RC_CONSTQP },              0, 0, VE, "rc" },
    { "vbr",              "Variable bitrate mode",                                                       0, AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_PARAMS_RC_VBR },                  0, 0, VE, "rc" },
    { "cbr",              "Constant bitrate mode",                                                       0, AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_PARAMS_RC_CBR },                  0, 0, VE, "rc" },
    { "vbr_minqp",        "Variable bitrate mode with MinQP",                                            0, AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_PARAMS_RC_VBR_MINQP },            0, 0, VE, "rc" },
    { "ll_2pass_quality", "Multi-pass optimized for image quality (only for low-latency presets)",       0, AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_PARAMS_RC_2_PASS_QUALITY },       0, 0, VE, "rc" },
    { "ll_2pass_size",    "Multi-pass optimized for constant frame size (only for low-latency presets)", 0, AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_PARAMS_RC_2_PASS_FRAMESIZE_CAP }, 0, 0, VE, "rc" },
    { "vbr_2pass",        "Multi-pass variable bitrate mode",                                            0, AV_OPT_TYPE_CONST,  { .i64 = NV_ENC_PARAMS_RC_2_PASS_VBR },           0, 0, VE, "rc" },
    { "surfaces", "Number of concurrent surfaces",        OFFSET(nb_surfaces), AV_OPT_TYPE_INT,    { .i64 = 32 },                   0, INT_MAX, VE },
    { "cbr", "Use cbr encoding mode", OFFSET(cbr), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    { "2pass", "Use 2pass encoding mode", OFFSET(twopass), AV_OPT_TYPE_BOOL, { .i64 = -1 }, -1, 1, VE },
    { "gpu", "Selects which NVENC capable GPU to use. First GPU is 0, second is 1, and so on.", OFFSET(device), AV_OPT_TYPE_INT, { .i64 = ANY_DEVICE }, -2, INT_MAX, VE, "gpu" },
    { "any",      "Pick the first device available",      0,                   AV_OPT_TYPE_CONST,  { .i64 = ANY_DEVICE },           0, 0, VE, "gpu" },
    { "list",     "List the available devices",           0,                   AV_OPT_TYPE_CONST,  { .i64 = LIST_DEVICES },         0, 0, VE, "gpu" },
    { "delay",    "Delay frame output by the given amount of frames", OFFSET(async_depth), AV_OPT_TYPE_INT, { .i64 = INT_MAX }, 0, INT_MAX, VE },
    { NULL }
};

static const AVCodecDefault defaults[] = {
    { "b", "2M" },
    { "qmin", "-1" },
    { "qmax", "-1" },
    { "qdiff", "-1" },
    { "qblur", "-1" },
    { "qcomp", "-1" },
    { "g", "250" },
    { "bf", "0" },
    { NULL },
};

#if FF_API_NVENC_OLD_NAME

static av_cold int nvenc_old_init(AVCodecContext *avctx)
{
    av_log(avctx, AV_LOG_WARNING, "This encoder is deprecated, use 'h264_nvenc' instead\n");
    return ff_nvenc_encode_init(avctx);
}

#if CONFIG_NVENC_ENCODER
static const AVClass nvenc_class = {
    .class_name = "nvenc",
    .item_name = av_default_item_name,
    .option = options,
    .version = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_nvenc_encoder = {
    .name           = "nvenc",
    .long_name      = NULL_IF_CONFIG_SMALL("NVIDIA NVENC H.264 encoder"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .init           = nvenc_old_init,
    .encode2        = ff_nvenc_encode_frame,
    .close          = ff_nvenc_encode_close,
    .priv_data_size = sizeof(NvencContext),
    .priv_class     = &nvenc_class,
    .defaults       = defaults,
    .capabilities   = AV_CODEC_CAP_DELAY,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .pix_fmts       = ff_nvenc_pix_fmts,
};
#endif

/* Add an alias for nvenc_h264 */
#if CONFIG_NVENC_H264_ENCODER
static const AVClass nvenc_h264_class = {
    .class_name = "nvenc_h264",
    .item_name = av_default_item_name,
    .option = options,
    .version = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_nvenc_h264_encoder = {
    .name           = "nvenc_h264",
    .long_name      = NULL_IF_CONFIG_SMALL("NVIDIA NVENC H.264 encoder"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .init           = nvenc_old_init,
    .encode2        = ff_nvenc_encode_frame,
    .close          = ff_nvenc_encode_close,
    .priv_data_size = sizeof(NvencContext),
    .priv_class     = &nvenc_h264_class,
    .defaults       = defaults,
    .capabilities   = AV_CODEC_CAP_DELAY,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .pix_fmts       = ff_nvenc_pix_fmts,
};
#endif

#endif

static const AVClass h264_nvenc_class = {
    .class_name = "h264_nvenc",
    .item_name = av_default_item_name,
    .option = options,
    .version = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_h264_nvenc_encoder = {
    .name           = "h264_nvenc",
    .long_name      = NULL_IF_CONFIG_SMALL("NVIDIA NVENC H.264 encoder"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .init           = ff_nvenc_encode_init,
    .encode2        = ff_nvenc_encode_frame,
    .close          = ff_nvenc_encode_close,
    .priv_data_size = sizeof(NvencContext),
    .priv_class     = &h264_nvenc_class,
    .defaults       = defaults,
    .capabilities   = AV_CODEC_CAP_DELAY,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .pix_fmts       = ff_nvenc_pix_fmts,
};
