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
 * Initalize the decoder on a per-frame basis. All of these need to be set
 * once each time the decoder is reset, which it must be each frame to make
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

    /* This needs to be set each time the decoder is reset */
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

    /* check for negative zero, our default */
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
    if (JxlEncoderOptionsSetDistance(ctx->options, ctx->distance) != JXL_ENC_SUCCESS) {
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
    JxlEncoderStatus jret;
    int ret;
    size_t available = ctx->buffer_size;
    size_t bytes_written = 0;
    uint8_t *next_out = ctx->buffer;

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
    info.bits_per_sample = av_get_bits_per_pixel(pix_desc) / jxl_fmt.num_channels;
    info.alpha_bits = (info.num_extra_channels > 0) * info.bits_per_sample;
    if (pix_desc->flags & AV_PIX_FMT_FLAG_FLOAT) {
        info.exponent_bits_per_sample = info.bits_per_sample > 16 ? 8 : 5;
        info.alpha_exponent_bits = info.alpha_bits ? info.exponent_bits_per_sample : 0;
        jxl_fmt.data_type = info.bits_per_sample > 16 ? JXL_TYPE_FLOAT : JXL_TYPE_FLOAT16;
        JxlColorEncodingSetToLinearSRGB(&jxl_color, info.num_color_channels == 1);
    } else {
        info.exponent_bits_per_sample = 0;
        info.alpha_exponent_bits = 0;
        jxl_fmt.data_type = info.bits_per_sample <= 8 ? JXL_TYPE_UINT8 : JXL_TYPE_UINT16;
        JxlColorEncodingSetToSRGB(&jxl_color, info.num_color_channels == 1);
    }

    if (info.bits_per_sample > 16
        || info.xsize > (1 << 18) || info.ysize > (1 << 18)
        || (info.xsize << 4) * (info.ysize << 4) > (1 << 20)) {
        /*
         * must upgrade codestream to level 10, from level 5
         * the encoder will not do this automatically
         */
        if (JxlEncoderSetCodestreamLevel(ctx->encoder, 10) != JXL_ENC_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Could not upgrade JXL Codestream level.\n");
            return AVERROR_EXTERNAL;
        }
    }

    /* bitexact lossless requires there to be no XYB transform */
    info.uses_original_profile = ctx->distance == 0.0;

    sd = av_frame_get_side_data(frame, AV_FRAME_DATA_ICC_PROFILE);
    if (sd && sd->size && JxlEncoderSetICCProfile(ctx->encoder, sd->data, sd->size) != JXL_ENC_SUCCESS) {
        av_log(avctx, AV_LOG_WARNING, "Could not set ICC Profile\n");
    } else if (info.uses_original_profile) {
        /*
        * the color encoding is not used if uses_original_profile is false
        * this just works around a bug in libjxl 0.7.0 and lower
        */
        if (JxlEncoderSetColorEncoding(ctx->encoder, &jxl_color) != JXL_ENC_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to set JxlColorEncoding\n");
            return AVERROR_EXTERNAL;
        }
    }

    if (JxlEncoderSetBasicInfo(ctx->encoder, &info) != JXL_ENC_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to set JxlBasicInfo\n");
        return AVERROR_EXTERNAL;
    }

    jxl_fmt.endianness = JXL_NATIVE_ENDIAN;
    jxl_fmt.align = frame->linesize[0];

    if (JxlEncoderAddImageFrame(ctx->options, &jxl_fmt, frame->data[0], jxl_fmt.align * info.ysize) != JXL_ENC_SUCCESS) {
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
     * destroying the decoder also frees
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
    .p.long_name      = NULL_IF_CONFIG_SMALL("libjxl JPEG XL"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_JPEGXL,
    .priv_data_size   = sizeof(LibJxlEncodeContext),
    .init             = libjxl_encode_init,
    FF_CODEC_ENCODE_CB(libjxl_encode_frame),
    .close            = libjxl_encode_close,
    .p.capabilities   = AV_CODEC_CAP_OTHER_THREADS,
    .caps_internal    = FF_CODEC_CAP_AUTO_THREADS | FF_CODEC_CAP_INIT_CLEANUP,
    .p.pix_fmts       = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_RGB24, AV_PIX_FMT_RGBA,
        AV_PIX_FMT_RGB48, AV_PIX_FMT_RGBA64,
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_YA8,
        AV_PIX_FMT_GRAY16, AV_PIX_FMT_YA16,
        AV_PIX_FMT_GRAYF32,
        AV_PIX_FMT_NONE
    },
    .p.priv_class     = &libjxl_encode_class,
    .p.wrapper_name   = "libjxl",
};
