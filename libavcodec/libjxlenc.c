/*
 * JPEG XL encoding support via libjxl
 * Copyright (c) 2021 Leo Izen <leo.izen@gmail.com>
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

/**
 * @file
 * JPEG XL encoder using libjxl
 */

#include <string.h>

#include "libavutil/avutil.h"
#include "libavutil/csp.h"
#include "libavutil/error.h"
#include "libavutil/frame.h"
#include "libavutil/libm.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"
#include "libavutil/version.h"

#include "avcodec.h"
#include "encode.h"
#include "codec_internal.h"

#include <jxl/encode.h>
#include <jxl/thread_parallel_runner.h>
#include "libjxl.h"

typedef struct LibJxlEncodeContext {
    AVClass *class;
    void *runner;
    JxlEncoder *encoder;
    JxlEncoderFrameSettings *options;
    int effort;
    float distance;
    int modular;
    int xyb;
    uint8_t *buffer;
    size_t buffer_size;
} LibJxlEncodeContext;

/**
 * Map a quality setting for -qscale roughly from libjpeg
 * quality numbers to libjxl's butteraugli distance for
 * photographic content.
 *
 * Setting distance explicitly is preferred, but this will
 * allow qscale to be used as a fallback.
 *
 * This function is continuous and injective on [0, 100] which
 * makes it monotonic.
 *
 * @param  quality 0.0 to 100.0 quality setting, libjpeg quality
 * @return         Butteraugli distance between 0.0 and 15.0
 */
static float quality_to_distance(float quality)
{
    if (quality >= 100.0)
        return 0.0;
    else if (quality >= 90.0)
        return (100.0 - quality) * 0.10;
    else if (quality >= 30.0)
        return 0.1 + (100.0 - quality) * 0.09;
    else if (quality > 0.0)
        return 15.0 + (59.0 * quality - 4350.0) * quality / 9000.0;
    else
        return 15.0;
}

/**
 * Initalize the encoder on a per-frame basis. All of these need to be set
 * once each time the encoder is reset, which it must be each frame to make
 * the image2 muxer work.
 *
 * @return       0 upon success, negative on failure.
 */
static int libjxl_init_jxl_encoder(AVCodecContext *avctx)
{
    LibJxlEncodeContext *ctx = avctx->priv_data;

    /* reset the encoder every frame for image2 muxer */
    JxlEncoderReset(ctx->encoder);

    ctx->options = JxlEncoderFrameSettingsCreate(ctx->encoder, NULL);
    if (!ctx->options) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create JxlEncoderOptions\n");
        return AVERROR_EXTERNAL;
    }

    /* This needs to be set each time the encoder is reset */
    if (JxlEncoderSetParallelRunner(ctx->encoder, JxlThreadParallelRunner, ctx->runner)
            != JXL_ENC_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to set JxlThreadParallelRunner\n");
        return AVERROR_EXTERNAL;
    }

    /* these shouldn't fail, libjxl bug notwithstanding */
    if (JxlEncoderFrameSettingsSetOption(ctx->options, JXL_ENC_FRAME_SETTING_EFFORT, ctx->effort)
            != JXL_ENC_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to set effort to: %d\n", ctx->effort);
        return AVERROR_EXTERNAL;
    }

    /* check for negative, our default */
    if (ctx->distance < 0.0) {
        /* use ffmpeg.c -q option if passed */
        if (avctx->flags & AV_CODEC_FLAG_QSCALE)
            ctx->distance = quality_to_distance((float)avctx->global_quality / FF_QP2LAMBDA);
        else
            /* default 1.0 matches cjxl */
            ctx->distance = 1.0;
    }

    /*
     * 0.01 is the minimum distance accepted for lossy
     * interpreting any positive value less than this as minimum
     */
    if (ctx->distance > 0.0 && ctx->distance < 0.01)
        ctx->distance = 0.01;
    if (JxlEncoderSetFrameDistance(ctx->options, ctx->distance) != JXL_ENC_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to set distance: %f\n", ctx->distance);
        return AVERROR_EXTERNAL;
    }

    /*
     * In theory the library should automatically enable modular if necessary,
     * but it appears it won't at the moment due to a bug. This will still
     * work even if that is patched.
     */
    if (JxlEncoderFrameSettingsSetOption(ctx->options, JXL_ENC_FRAME_SETTING_MODULAR,
            ctx->modular || ctx->distance <= 0.0 ? 1 : -1) != JXL_ENC_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to set modular\n");
        return AVERROR_EXTERNAL;
    }

    return 0;
}

/**
 * Global encoder initialization. This only needs to be run once,
 * not every frame.
 */
static av_cold int libjxl_encode_init(AVCodecContext *avctx)
{
    LibJxlEncodeContext *ctx = avctx->priv_data;
    JxlMemoryManager manager;

    ff_libjxl_init_memory_manager(&manager);
    ctx->encoder = JxlEncoderCreate(&manager);
    if (!ctx->encoder) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create JxlEncoder\n");
        return AVERROR_EXTERNAL;
    }

    ctx->runner = JxlThreadParallelRunnerCreate(&manager, ff_libjxl_get_threadcount(avctx->thread_count));
    if (!ctx->runner) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create JxlThreadParallelRunner\n");
        return AVERROR_EXTERNAL;
    }

    ctx->buffer_size = 4096;
    ctx->buffer = av_realloc(NULL, ctx->buffer_size);

    if (!ctx->buffer) {
        av_log(avctx, AV_LOG_ERROR, "Could not allocate encoding buffer\n");
        return AVERROR(ENOMEM);
    }

    return 0;
}

/**
 * Populate a JxlColorEncoding with the given enum AVColorPrimaries.
 * @return < 0 upon failure, >= 0 upon success
 */
static int libjxl_populate_primaries(void *avctx, JxlColorEncoding *jxl_color, enum AVColorPrimaries prm)
{
    const AVColorPrimariesDesc *desc;

    switch (prm) {
    case AVCOL_PRI_BT709:
        jxl_color->primaries = JXL_PRIMARIES_SRGB;
        jxl_color->white_point = JXL_WHITE_POINT_D65;
        return 0;
    case AVCOL_PRI_BT2020:
        jxl_color->primaries = JXL_PRIMARIES_2100;
        jxl_color->white_point = JXL_WHITE_POINT_D65;
        return 0;
    case AVCOL_PRI_SMPTE431:
        jxl_color->primaries = JXL_PRIMARIES_P3;
        jxl_color->white_point = JXL_WHITE_POINT_DCI;
        return 0;
    case AVCOL_PRI_SMPTE432:
        jxl_color->primaries = JXL_PRIMARIES_P3;
        jxl_color->white_point = JXL_WHITE_POINT_D65;
        return 0;
    case AVCOL_PRI_UNSPECIFIED:
        av_log(avctx, AV_LOG_WARNING, "Unknown primaries, assuming BT.709/sRGB. Colors may be wrong.\n");
        jxl_color->primaries = JXL_PRIMARIES_SRGB;
        jxl_color->white_point = JXL_WHITE_POINT_D65;
        return 0;
    }

    desc = av_csp_primaries_desc_from_id(prm);
    if (!desc)
        return AVERROR(EINVAL);

    jxl_color->primaries = JXL_PRIMARIES_CUSTOM;
    jxl_color->white_point = JXL_WHITE_POINT_CUSTOM;

    jxl_color->primaries_red_xy[0] = av_q2d(desc->prim.r.x);
    jxl_color->primaries_red_xy[1] = av_q2d(desc->prim.r.y);
    jxl_color->primaries_green_xy[0] = av_q2d(desc->prim.g.x);
    jxl_color->primaries_green_xy[1] = av_q2d(desc->prim.g.y);
    jxl_color->primaries_blue_xy[0] = av_q2d(desc->prim.b.x);
    jxl_color->primaries_blue_xy[1] = av_q2d(desc->prim.b.y);
    jxl_color->white_point_xy[0] = av_q2d(desc->wp.x);
    jxl_color->white_point_xy[1] = av_q2d(desc->wp.y);

    return 0;
}

/**
 * Encode an entire frame. Currently animation, is not supported by
 * this encoder, so this will always reinitialize a new still image
 * and encode a one-frame image (for image2 and image2pipe).
 */
static int libjxl_encode_frame(AVCodecContext *avctx, AVPacket *pkt, const AVFrame *frame, int *got_packet)
{
    LibJxlEncodeContext *ctx = avctx->priv_data;
    AVFrameSideData *sd;
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(frame->format);
    JxlBasicInfo info;
    JxlColorEncoding jxl_color;
    JxlPixelFormat jxl_fmt;
    int bits_per_sample;
#if JPEGXL_NUMERIC_VERSION >= JPEGXL_COMPUTE_NUMERIC_VERSION(0, 8, 0)
    JxlBitDepth jxl_bit_depth;
#endif
    JxlEncoderStatus jret;
    int ret;
    size_t available = ctx->buffer_size;
    size_t bytes_written = 0;
    uint8_t *next_out = ctx->buffer;
    const uint8_t *data;

    ret = libjxl_init_jxl_encoder(avctx);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR, "Error frame-initializing JxlEncoder\n");
        return ret;
    }

    /* populate the basic info settings */
    JxlEncoderInitBasicInfo(&info);
    jxl_fmt.num_channels = pix_desc->nb_components;
    info.xsize = frame->width;
    info.ysize = frame->height;
    info.num_extra_channels = (jxl_fmt.num_channels + 1) % 2;
    info.num_color_channels = jxl_fmt.num_channels - info.num_extra_channels;
    bits_per_sample = av_get_bits_per_pixel(pix_desc) / jxl_fmt.num_channels;
    info.bits_per_sample = avctx->bits_per_raw_sample > 0 && !(pix_desc->flags & AV_PIX_FMT_FLAG_FLOAT)
                           ? avctx->bits_per_raw_sample : bits_per_sample;
    info.alpha_bits = (info.num_extra_channels > 0) * info.bits_per_sample;
    if (pix_desc->flags & AV_PIX_FMT_FLAG_FLOAT) {
        info.exponent_bits_per_sample = info.bits_per_sample > 16 ? 8 : 5;
        info.alpha_exponent_bits = info.alpha_bits ? info.exponent_bits_per_sample : 0;
        jxl_fmt.data_type = info.bits_per_sample > 16 ? JXL_TYPE_FLOAT : JXL_TYPE_FLOAT16;
    } else {
        info.exponent_bits_per_sample = 0;
        info.alpha_exponent_bits = 0;
        jxl_fmt.data_type = info.bits_per_sample <= 8 ? JXL_TYPE_UINT8 : JXL_TYPE_UINT16;
    }

#if JPEGXL_NUMERIC_VERSION >= JPEGXL_COMPUTE_NUMERIC_VERSION(0, 8, 0)
    jxl_bit_depth.bits_per_sample = bits_per_sample;
    jxl_bit_depth.type = JXL_BIT_DEPTH_FROM_PIXEL_FORMAT;
    jxl_bit_depth.exponent_bits_per_sample = pix_desc->flags & AV_PIX_FMT_FLAG_FLOAT ?
                                             info.exponent_bits_per_sample : 0;
#endif

    /* JPEG XL format itself does not support limited range */
    if (avctx->color_range == AVCOL_RANGE_MPEG ||
        avctx->color_range == AVCOL_RANGE_UNSPECIFIED && frame->color_range == AVCOL_RANGE_MPEG)
        av_log(avctx, AV_LOG_WARNING, "This encoder does not support limited (tv) range, colors will be wrong!\n");
    else if (avctx->color_range != AVCOL_RANGE_JPEG && frame->color_range != AVCOL_RANGE_JPEG)
        av_log(avctx, AV_LOG_WARNING, "Unknown color range, assuming full (pc)\n");

    /* bitexact lossless requires there to be no XYB transform */
    info.uses_original_profile = ctx->distance == 0.0 || !ctx->xyb;
    info.orientation = frame->linesize[0] >= 0 ? JXL_ORIENT_IDENTITY : JXL_ORIENT_FLIP_VERTICAL;

    if (JxlEncoderSetBasicInfo(ctx->encoder, &info) != JXL_ENC_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to set JxlBasicInfo\n");
        return AVERROR_EXTERNAL;
    }

    /* rendering intent doesn't matter here
     * but libjxl will whine if we don't set it */
    jxl_color.rendering_intent = JXL_RENDERING_INTENT_RELATIVE;

    switch (frame->color_trc && frame->color_trc != AVCOL_TRC_UNSPECIFIED
            ? frame->color_trc : avctx->color_trc) {
    case AVCOL_TRC_BT709:
        jxl_color.transfer_function = JXL_TRANSFER_FUNCTION_709;
        break;
    case AVCOL_TRC_LINEAR:
        jxl_color.transfer_function = JXL_TRANSFER_FUNCTION_LINEAR;
        break;
    case AVCOL_TRC_IEC61966_2_1:
        jxl_color.transfer_function = JXL_TRANSFER_FUNCTION_SRGB;
        break;
    case AVCOL_TRC_SMPTE428:
        jxl_color.transfer_function = JXL_TRANSFER_FUNCTION_DCI;
        break;
    case AVCOL_TRC_SMPTE2084:
        jxl_color.transfer_function = JXL_TRANSFER_FUNCTION_PQ;
        break;
    case AVCOL_TRC_ARIB_STD_B67:
        jxl_color.transfer_function = JXL_TRANSFER_FUNCTION_HLG;
        break;
    case AVCOL_TRC_GAMMA22:
        jxl_color.transfer_function = JXL_TRANSFER_FUNCTION_GAMMA;
        jxl_color.gamma = 1/2.2f;
        break;
    case AVCOL_TRC_GAMMA28:
        jxl_color.transfer_function = JXL_TRANSFER_FUNCTION_GAMMA;
        jxl_color.gamma = 1/2.8f;
        break;
    default:
        if (pix_desc->flags & AV_PIX_FMT_FLAG_FLOAT) {
            av_log(avctx, AV_LOG_WARNING, "Unknown transfer function, assuming Linear Light. Colors may be wrong.\n");
            jxl_color.transfer_function = JXL_TRANSFER_FUNCTION_LINEAR;
        } else {
            av_log(avctx, AV_LOG_WARNING, "Unknown transfer function, assuming IEC61966-2-1/sRGB. Colors may be wrong.\n");
            jxl_color.transfer_function = JXL_TRANSFER_FUNCTION_SRGB;
        }
    }

    /* This should be implied to be honest
     * but a libjxl bug makes it fail otherwise */
    if (info.num_color_channels == 1)
        jxl_color.color_space = JXL_COLOR_SPACE_GRAY;
    else
        jxl_color.color_space = JXL_COLOR_SPACE_RGB;

    ret = libjxl_populate_primaries(avctx, &jxl_color,
            frame->color_primaries && frame->color_primaries != AVCOL_PRI_UNSPECIFIED
            ? frame->color_primaries : avctx->color_primaries);
    if (ret < 0)
        return ret;

    sd = av_frame_get_side_data(frame, AV_FRAME_DATA_ICC_PROFILE);
    if (sd && sd->size && JxlEncoderSetICCProfile(ctx->encoder, sd->data, sd->size) != JXL_ENC_SUCCESS)
        av_log(avctx, AV_LOG_WARNING, "Could not set ICC Profile\n");
    if (JxlEncoderSetColorEncoding(ctx->encoder, &jxl_color) != JXL_ENC_SUCCESS)
        av_log(avctx, AV_LOG_WARNING, "Failed to set JxlColorEncoding\n");

#if JPEGXL_NUMERIC_VERSION >= JPEGXL_COMPUTE_NUMERIC_VERSION(0, 8, 0)
    if (JxlEncoderSetFrameBitDepth(ctx->options, &jxl_bit_depth) != JXL_ENC_SUCCESS)
        av_log(avctx, AV_LOG_WARNING, "Failed to set JxlBitDepth\n");
#endif

    /* depending on basic info, level 10 might
     * be required instead of level 5 */
    if (JxlEncoderGetRequiredCodestreamLevel(ctx->encoder) > 5) {
        if (JxlEncoderSetCodestreamLevel(ctx->encoder, 10) != JXL_ENC_SUCCESS)
            av_log(avctx, AV_LOG_WARNING, "Could not increase codestream level\n");
    }

    jxl_fmt.endianness = JXL_NATIVE_ENDIAN;
    if (frame->linesize[0] >= 0) {
        jxl_fmt.align = frame->linesize[0];
        data = frame->data[0];
    } else {
        jxl_fmt.align = -frame->linesize[0];
        data = frame->data[0] + frame->linesize[0] * (info.ysize - 1);
    }

    if (JxlEncoderAddImageFrame(ctx->options, &jxl_fmt, data, jxl_fmt.align * info.ysize) != JXL_ENC_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to add Image Frame\n");
        return AVERROR_EXTERNAL;
    }

    /*
     * Run this after the last frame in the image has been passed.
     * TODO support animation
     */
    JxlEncoderCloseInput(ctx->encoder);

    while (1) {
        jret = JxlEncoderProcessOutput(ctx->encoder, &next_out, &available);
        if (jret == JXL_ENC_ERROR) {
            av_log(avctx, AV_LOG_ERROR, "Unspecified libjxl error occurred\n");
            return AVERROR_EXTERNAL;
        }
        bytes_written = ctx->buffer_size - available;
        /* all data passed has been encoded */
        if (jret == JXL_ENC_SUCCESS)
            break;
        if (jret == JXL_ENC_NEED_MORE_OUTPUT) {
            /*
             * at the moment, libjxl has no way to
             * tell us how much space it actually needs
             * so we need to malloc loop
             */
            uint8_t *temp;
            size_t new_size = ctx->buffer_size * 2;
            temp = av_realloc(ctx->buffer, new_size);
            if (!temp)
                return AVERROR(ENOMEM);
            ctx->buffer = temp;
            ctx->buffer_size = new_size;
            next_out = ctx->buffer + bytes_written;
            available = new_size - bytes_written;
            continue;
        }
        av_log(avctx, AV_LOG_ERROR, "Bad libjxl event: %d\n", jret);
        return AVERROR_EXTERNAL;
    }

    ret = ff_get_encode_buffer(avctx, pkt, bytes_written, 0);
    if (ret < 0)
        return ret;

    memcpy(pkt->data, ctx->buffer, bytes_written);
    *got_packet = 1;

    return 0;
}

static av_cold int libjxl_encode_close(AVCodecContext *avctx)
{
    LibJxlEncodeContext *ctx = avctx->priv_data;

    if (ctx->runner)
        JxlThreadParallelRunnerDestroy(ctx->runner);
    ctx->runner = NULL;

    /*
     * destroying the encoder also frees
     * ctx->options so we don't need to
     */
    if (ctx->encoder)
        JxlEncoderDestroy(ctx->encoder);
    ctx->encoder = NULL;

    av_freep(&ctx->buffer);

    return 0;
}

#define OFFSET(x) offsetof(LibJxlEncodeContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM

static const AVOption libjxl_encode_options[] = {
    { "effort",        "Encoding effort",                                  OFFSET(effort),     AV_OPT_TYPE_INT,    { .i64 =    7 },    1,     9, VE },
    { "distance",      "Maximum Butteraugli distance (quality setting, "
                        "lower = better, zero = lossless, default 1.0)",   OFFSET(distance),   AV_OPT_TYPE_FLOAT,  { .dbl = -1.0 }, -1.0,  15.0, VE },
    { "modular",       "Force modular mode",                               OFFSET(modular),    AV_OPT_TYPE_INT,    { .i64 =    0 },    0,     1, VE },
    { "xyb",           "Use XYB-encoding for lossy images",                OFFSET(xyb),
        AV_OPT_TYPE_INT,   { .i64 =    1 },    0,     1, VE },
    { NULL },
};

static const AVClass libjxl_encode_class = {
    .class_name = "libjxl",
    .item_name  = av_default_item_name,
    .option     = libjxl_encode_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_libjxl_encoder = {
    .p.name           = "libjxl",
    CODEC_LONG_NAME("libjxl JPEG XL"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_JPEGXL,
    .priv_data_size   = sizeof(LibJxlEncodeContext),
    .init             = libjxl_encode_init,
    FF_CODEC_ENCODE_CB(libjxl_encode_frame),
    .close            = libjxl_encode_close,
    .p.capabilities   = AV_CODEC_CAP_OTHER_THREADS |
                        AV_CODEC_CAP_DR1 |
                        AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .caps_internal    = FF_CODEC_CAP_NOT_INIT_THREADSAFE |
                        FF_CODEC_CAP_AUTO_THREADS | FF_CODEC_CAP_INIT_CLEANUP |
                        FF_CODEC_CAP_ICC_PROFILES,
    .p.pix_fmts       = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_RGB24, AV_PIX_FMT_RGBA,
        AV_PIX_FMT_RGB48, AV_PIX_FMT_RGBA64,
        AV_PIX_FMT_RGBF32, AV_PIX_FMT_RGBAF32,
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_YA8,
        AV_PIX_FMT_GRAY16, AV_PIX_FMT_YA16,
        AV_PIX_FMT_GRAYF32,
        AV_PIX_FMT_NONE
    },
    .p.priv_class     = &libjxl_encode_class,
    .p.wrapper_name   = "libjxl",
};
