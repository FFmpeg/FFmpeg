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
    { "preset", "Set the encoding preset (one of slow = hq 2pass, medium = hq, fast = hp, hq, hp, bd, ll, llhq, llhp, lossless, losslesshp, default)", OFFSET(preset), AV_OPT_TYPE_STRING, { .str = "medium" }, 0, 0, VE },
    { "profile", "Set the encoding profile (high, main, baseline or high444p)", OFFSET(profile), AV_OPT_TYPE_STRING, { .str = "main" }, 0, 0, VE },
    { "level", "Set the encoding level restriction (auto, 1.0, 1.0b, 1.1, 1.2, ..., 4.2, 5.0, 5.1)", OFFSET(level), AV_OPT_TYPE_STRING, { .str = "auto" }, 0, 0, VE },
    { "cbr", "Use cbr encoding mode", OFFSET(cbr), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    { "2pass", "Use 2pass encoding mode", OFFSET(twopass), AV_OPT_TYPE_BOOL, { .i64 = -1 }, -1, 1, VE },
    { "gpu", "Selects which NVENC capable GPU to use. First GPU is 0, second is 1, and so on.", OFFSET(gpu), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, VE },
    { "delay", "Delays frame output by the given amount of frames.", OFFSET(buffer_delay), AV_OPT_TYPE_INT, { .i64 = INT_MAX }, 0, INT_MAX, VE },
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

#if CONFIG_NVENC_ENCODER
static const AVClass nvenc_class = {
    .class_name = "nvenc",
    .item_name = av_default_item_name,
    .option = options,
    .version = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_nvenc_encoder = {
    .name           = "nvenc",
    .long_name      = NULL_IF_CONFIG_SMALL("NVIDIA NVENC h264 encoder"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .init           = ff_nvenc_encode_init,
    .encode2        = ff_nvenc_encode_frame,
    .close          = ff_nvenc_encode_close,
    .priv_data_size = sizeof(NvencContext),
    .priv_class     = &nvenc_class,
    .defaults       = defaults,
    .capabilities   = AV_CODEC_CAP_DELAY,
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
    .long_name      = NULL_IF_CONFIG_SMALL("NVIDIA NVENC h264 encoder"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .init           = ff_nvenc_encode_init,
    .encode2        = ff_nvenc_encode_frame,
    .close          = ff_nvenc_encode_close,
    .priv_data_size = sizeof(NvencContext),
    .priv_class     = &nvenc_h264_class,
    .defaults       = defaults,
    .capabilities   = AV_CODEC_CAP_DELAY,
    .pix_fmts       = ff_nvenc_pix_fmts,
};
#endif
