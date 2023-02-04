/*
 * JPEG XL decoding support via libjxl
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
 * JPEG XL decoder using libjxl
 */

#include "libavutil/avassert.h"
#include "libavutil/buffer.h"
#include "libavutil/common.h"
#include "libavutil/csp.h"
#include "libavutil/error.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"
#include "libavutil/frame.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"

#include <jxl/decode.h>
#include <jxl/thread_parallel_runner.h>
#include "libjxl.h"

typedef struct LibJxlDecodeContext {
    void *runner;
    JxlDecoder *decoder;
    JxlBasicInfo basic_info;
    JxlPixelFormat jxl_pixfmt;
#if JPEGXL_NUMERIC_VERSION >= JPEGXL_COMPUTE_NUMERIC_VERSION(0, 8, 0)
    JxlBitDepth jxl_bit_depth;
#endif
    JxlDecoderStatus events;
    AVBufferRef *iccp;
} LibJxlDecodeContext;

static int libjxl_init_jxl_decoder(AVCodecContext *avctx)
{
    LibJxlDecodeContext *ctx = avctx->priv_data;

    ctx->events = JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE | JXL_DEC_COLOR_ENCODING;
    if (JxlDecoderSubscribeEvents(ctx->decoder, ctx->events) != JXL_DEC_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Error subscribing to JXL events\n");
        return AVERROR_EXTERNAL;
    }

    if (JxlDecoderSetParallelRunner(ctx->decoder, JxlThreadParallelRunner, ctx->runner) != JXL_DEC_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to set JxlThreadParallelRunner\n");
        return AVERROR_EXTERNAL;
    }

    memset(&ctx->basic_info, 0, sizeof(JxlBasicInfo));
    memset(&ctx->jxl_pixfmt, 0, sizeof(JxlPixelFormat));

    return 0;
}

static av_cold int libjxl_decode_init(AVCodecContext *avctx)
{
    LibJxlDecodeContext *ctx = avctx->priv_data;
    JxlMemoryManager manager;

    ff_libjxl_init_memory_manager(&manager);
    ctx->decoder = JxlDecoderCreate(&manager);
    if (!ctx->decoder) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create JxlDecoder\n");
        return AVERROR_EXTERNAL;
    }

    ctx->runner = JxlThreadParallelRunnerCreate(&manager, ff_libjxl_get_threadcount(avctx->thread_count));
    if (!ctx->runner) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create JxlThreadParallelRunner\n");
        return AVERROR_EXTERNAL;
    }

    return libjxl_init_jxl_decoder(avctx);
}

static enum AVPixelFormat libjxl_get_pix_fmt(AVCodecContext *avctx, LibJxlDecodeContext *ctx)
{
    const JxlBasicInfo *basic_info = &ctx->basic_info;
    JxlPixelFormat *format = &ctx->jxl_pixfmt;
    format->endianness = JXL_NATIVE_ENDIAN;
    format->num_channels = basic_info->num_color_channels + (basic_info->alpha_bits > 0);
#if JPEGXL_NUMERIC_VERSION >= JPEGXL_COMPUTE_NUMERIC_VERSION(0, 8, 0)
    ctx->jxl_bit_depth.bits_per_sample = avctx->bits_per_raw_sample = basic_info->bits_per_sample;
    ctx->jxl_bit_depth.type = JXL_BIT_DEPTH_FROM_PIXEL_FORMAT;
    ctx->jxl_bit_depth.exponent_bits_per_sample = basic_info->exponent_bits_per_sample;
#endif
    /* Gray */
    if (basic_info->num_color_channels == 1) {
        if (basic_info->bits_per_sample <= 8) {
            format->data_type = JXL_TYPE_UINT8;
            return basic_info->alpha_bits ? AV_PIX_FMT_YA8 : AV_PIX_FMT_GRAY8;
        }
        if (basic_info->exponent_bits_per_sample || basic_info->bits_per_sample > 16) {
            if (basic_info->alpha_bits)
                return AV_PIX_FMT_NONE;
            format->data_type = JXL_TYPE_FLOAT;
            return AV_PIX_FMT_GRAYF32;
        }
        format->data_type = JXL_TYPE_UINT16;
        return basic_info->alpha_bits ? AV_PIX_FMT_YA16 : AV_PIX_FMT_GRAY16;
    }
    /* rgb only */
    /* libjxl only supports packed RGB and gray output at the moment */
    if (basic_info->num_color_channels == 3) {
        if (basic_info->bits_per_sample <= 8) {
            format->data_type = JXL_TYPE_UINT8;
            return basic_info->alpha_bits ? AV_PIX_FMT_RGBA : AV_PIX_FMT_RGB24;
        }
        if (basic_info->exponent_bits_per_sample)
            av_log(avctx, AV_LOG_WARNING, "Downsampling float to 16-bit integer via libjxl\n");
        else if (basic_info->bits_per_sample > 16)
            av_log(avctx, AV_LOG_WARNING, "Downsampling larger integer to 16-bit via libjxl\n");
        format->data_type = JXL_TYPE_UINT16;
        return basic_info->alpha_bits ? AV_PIX_FMT_RGBA64 : AV_PIX_FMT_RGB48;
    }

    return AV_PIX_FMT_NONE;
}

static enum AVColorPrimaries libjxl_get_primaries(void *avctx, const JxlColorEncoding *jxl_color)
{
    AVColorPrimariesDesc desc;
    enum AVColorPrimaries prim;

    /* libjxl populates these double values even if it uses an enum space */
    desc.prim.r.x = av_d2q(jxl_color->primaries_red_xy[0], 300000);
    desc.prim.r.y = av_d2q(jxl_color->primaries_red_xy[1], 300000);
    desc.prim.g.x = av_d2q(jxl_color->primaries_green_xy[0], 300000);
    desc.prim.g.y = av_d2q(jxl_color->primaries_green_xy[1], 300000);
    desc.prim.b.x = av_d2q(jxl_color->primaries_blue_xy[0], 300000);
    desc.prim.b.y = av_d2q(jxl_color->primaries_blue_xy[1], 300000);
    desc.wp.x = av_d2q(jxl_color->white_point_xy[0], 300000);
    desc.wp.y = av_d2q(jxl_color->white_point_xy[1], 300000);

    prim = av_csp_primaries_id_from_desc(&desc);
    if (prim == AVCOL_PRI_UNSPECIFIED) {
        /* try D65 with the same primaries */
        /* BT.709 uses D65 white point */
        desc.wp = av_csp_primaries_desc_from_id(AVCOL_PRI_BT709)->wp;
        av_log(avctx, AV_LOG_WARNING, "Changing unknown white point to D65\n");
        prim = av_csp_primaries_id_from_desc(&desc);
    }

    return prim;
}

static enum AVColorTransferCharacteristic libjxl_get_trc(void *avctx, const JxlColorEncoding *jxl_color)
{
    switch (jxl_color->transfer_function) {
    case JXL_TRANSFER_FUNCTION_709: return AVCOL_TRC_BT709;
    case JXL_TRANSFER_FUNCTION_LINEAR: return AVCOL_TRC_LINEAR;
    case JXL_TRANSFER_FUNCTION_SRGB: return AVCOL_TRC_IEC61966_2_1;
    case JXL_TRANSFER_FUNCTION_PQ: return AVCOL_TRC_SMPTE2084;
    case JXL_TRANSFER_FUNCTION_DCI: return AVCOL_TRC_SMPTE428;
    case JXL_TRANSFER_FUNCTION_HLG: return AVCOL_TRC_ARIB_STD_B67;
    case JXL_TRANSFER_FUNCTION_GAMMA:
        if (jxl_color->gamma > 0.45355 && jxl_color->gamma < 0.45555)
            return AVCOL_TRC_GAMMA22;
        else if (jxl_color->gamma > 0.35614 && jxl_color->gamma < 0.35814)
            return AVCOL_TRC_GAMMA28;
        else
            av_log(avctx, AV_LOG_WARNING, "Unsupported gamma transfer: %f\n", jxl_color->gamma);
        break;
    default:
        av_log(avctx, AV_LOG_WARNING, "Unknown transfer function: %d\n", jxl_color->transfer_function);
    }

    return AVCOL_TRC_UNSPECIFIED;
}

static int libjxl_get_icc(AVCodecContext *avctx)
{
    LibJxlDecodeContext *ctx = avctx->priv_data;
    size_t icc_len;
    JxlDecoderStatus jret;
    /* an ICC profile is present, and we can meaningfully get it,
     * because the pixel data is not XYB-encoded */
    jret = JxlDecoderGetICCProfileSize(ctx->decoder, &ctx->jxl_pixfmt, JXL_COLOR_PROFILE_TARGET_DATA, &icc_len);
    if (jret == JXL_DEC_SUCCESS && icc_len > 0) {
        av_buffer_unref(&ctx->iccp);
        ctx->iccp = av_buffer_alloc(icc_len);
        if (!ctx->iccp)
            return AVERROR(ENOMEM);
        jret = JxlDecoderGetColorAsICCProfile(ctx->decoder, &ctx->jxl_pixfmt, JXL_COLOR_PROFILE_TARGET_DATA,
                                                ctx->iccp->data, icc_len);
        if (jret != JXL_DEC_SUCCESS) {
            av_log(avctx, AV_LOG_WARNING, "Unable to obtain ICC Profile\n");
            av_buffer_unref(&ctx->iccp);
        }
    }

    return 0;
}

/*
 * There's generally four cases when it comes to decoding a libjxl image
 * with regard to color encoding:
 * (a) There is an embedded ICC profile in the image, and the image is XYB-encoded.
 * (b) There is an embedded ICC profile in the image, and the image is not XYB-encoded.
 * (c) There is no embedded ICC profile, and FFmpeg supports the tagged colorspace.
 * (d) There is no embedded ICC profile, and FFmpeg does not support the tagged colorspace.
 *
 * In case (b), we forward the pixel data as is and forward the ICC Profile as-is.
 * In case (c), we request the pixel data in the space it's tagged as,
 *     and tag the space accordingly.
 * In case (a), libjxl does not support getting the pixel data in the space described by the ICC
 *     profile, so instead we request the pixel data in BT.2020/PQ as it is the widest
 *     space that FFmpeg supports.
 * In case (d), we also request wide-gamut pixel data as a fallback since FFmpeg doesn't support
 *     the custom primaries tagged in the space.
 */
static int libjxl_color_encoding_event(AVCodecContext *avctx, AVFrame *frame)
{
    LibJxlDecodeContext *ctx = avctx->priv_data;
    JxlDecoderStatus jret;
    int ret;
    JxlColorEncoding jxl_color;
    /* set this flag if we need to fall back on wide gamut */
    int fallback = 0;

    jret = JxlDecoderGetColorAsEncodedProfile(ctx->decoder, NULL, JXL_COLOR_PROFILE_TARGET_ORIGINAL, &jxl_color);
    if (jret == JXL_DEC_SUCCESS) {
        /* enum values describe the colors of this image */
        jret = JxlDecoderSetPreferredColorProfile(ctx->decoder, &jxl_color);
        if (jret == JXL_DEC_SUCCESS)
            jret = JxlDecoderGetColorAsEncodedProfile(ctx->decoder, &ctx->jxl_pixfmt, JXL_COLOR_PROFILE_TARGET_DATA, &jxl_color);
        /* if we couldn't successfully request the pixel data space, we fall back on wide gamut */
        /* this code path is very unlikely to happen in practice */
        if (jret != JXL_DEC_SUCCESS)
            fallback = 1;
    } else {
        /* an ICC Profile is present in the stream */
        if (ctx->basic_info.uses_original_profile) {
            /* uses_original_profile is the same as !xyb_encoded */
            av_log(avctx, AV_LOG_VERBOSE, "Using embedded ICC Profile\n");
            if ((ret = libjxl_get_icc(avctx)) < 0)
                return ret;
        } else {
            /*
             * an XYB-encoded image with an embedded ICC profile can't always have the
             * pixel data requested in the original space, so libjxl has no feature
             * to allow this to happen, so we fall back on wide gamut
             */
            fallback = 1;
        }
    }

    avctx->color_range = frame->color_range = AVCOL_RANGE_JPEG;
    if (ctx->jxl_pixfmt.num_channels >= 3)
        avctx->colorspace = AVCOL_SPC_RGB;
    avctx->color_primaries = AVCOL_PRI_UNSPECIFIED;
    avctx->color_trc = AVCOL_TRC_UNSPECIFIED;

    if (!ctx->iccp) {
        /* checking enum values */
        if (!fallback) {
            if (avctx->colorspace == AVCOL_SPC_RGB)
                avctx->color_primaries = libjxl_get_primaries(avctx, &jxl_color);
            avctx->color_trc = libjxl_get_trc(avctx, &jxl_color);
        }
        /* fall back on wide gamut if enum values fail */
        if (avctx->color_primaries == AVCOL_PRI_UNSPECIFIED) {
            if (avctx->colorspace == AVCOL_SPC_RGB) {
                av_log(avctx, AV_LOG_WARNING, "Falling back on wide gamut output\n");
                jxl_color.primaries = JXL_PRIMARIES_2100;
                avctx->color_primaries = AVCOL_PRI_BT2020;
            }
            /* libjxl requires this set even for grayscale */
            jxl_color.white_point = JXL_WHITE_POINT_D65;
        }
        if (avctx->color_trc == AVCOL_TRC_UNSPECIFIED) {
            if (ctx->jxl_pixfmt.data_type == JXL_TYPE_FLOAT
                || ctx->jxl_pixfmt.data_type == JXL_TYPE_FLOAT16) {
                av_log(avctx, AV_LOG_WARNING, "Falling back on Linear Light transfer\n");
                jxl_color.transfer_function = JXL_TRANSFER_FUNCTION_LINEAR;
                avctx->color_trc = AVCOL_TRC_LINEAR;
            } else {
                av_log(avctx, AV_LOG_WARNING, "Falling back on iec61966-2-1/sRGB transfer\n");
                jxl_color.transfer_function = JXL_TRANSFER_FUNCTION_SRGB;
                avctx->color_trc = AVCOL_TRC_IEC61966_2_1;
            }
        }
        /* all colors will be in-gamut so we want accurate colors */
        jxl_color.rendering_intent = JXL_RENDERING_INTENT_RELATIVE;
        jxl_color.color_space = avctx->colorspace == AVCOL_SPC_RGB ? JXL_COLOR_SPACE_RGB : JXL_COLOR_SPACE_GRAY;
        jret = JxlDecoderSetPreferredColorProfile(ctx->decoder, &jxl_color);
        if (jret != JXL_DEC_SUCCESS) {
            av_log(avctx, AV_LOG_WARNING, "Unable to set fallback color encoding\n");
            /*
             * This should only happen if there's a non-XYB encoded image with custom primaries
             * embedded as enums and no embedded ICC Profile.
             * In this case, libjxl will synthesize an ICC Profile for us.
             */
            avctx->color_trc = AVCOL_TRC_UNSPECIFIED;
            avctx->color_primaries = AVCOL_PRI_UNSPECIFIED;
            if ((ret = libjxl_get_icc(avctx)) < 0)
                return ret;
        }
    }

    frame->color_trc = avctx->color_trc;
    frame->color_primaries = avctx->color_primaries;
    frame->colorspace = avctx->colorspace;

    return 0;
}

static int libjxl_decode_frame(AVCodecContext *avctx, AVFrame *frame, int *got_frame, AVPacket *avpkt)
{
    LibJxlDecodeContext *ctx = avctx->priv_data;
    const uint8_t *buf = avpkt->data;
    size_t remaining = avpkt->size;
    JxlDecoderStatus jret;
    int ret;
    *got_frame = 0;

    while (1) {

        jret = JxlDecoderSetInput(ctx->decoder, buf, remaining);

        if (jret == JXL_DEC_ERROR) {
            /* this should never happen here unless there's a bug in libjxl */
            av_log(avctx, AV_LOG_ERROR, "Unknown libjxl decode error\n");
            return AVERROR_EXTERNAL;
        }

        jret = JxlDecoderProcessInput(ctx->decoder);
        /*
         * JxlDecoderReleaseInput returns the number
         * of bytes remaining to be read, rather than
         * the number of bytes that it did read
         */
        remaining = JxlDecoderReleaseInput(ctx->decoder);
        buf = avpkt->data + avpkt->size - remaining;

        switch(jret) {
        case JXL_DEC_ERROR:
            av_log(avctx, AV_LOG_ERROR, "Unknown libjxl decode error\n");
            return AVERROR_INVALIDDATA;
        case JXL_DEC_NEED_MORE_INPUT:
            if (remaining == 0) {
                av_log(avctx, AV_LOG_ERROR, "Unexpected end of JXL codestream\n");
                return AVERROR_INVALIDDATA;
            }
            av_log(avctx, AV_LOG_DEBUG, "NEED_MORE_INPUT event emitted\n");
            continue;
        case JXL_DEC_BASIC_INFO:
            av_log(avctx, AV_LOG_DEBUG, "BASIC_INFO event emitted\n");
            if (JxlDecoderGetBasicInfo(ctx->decoder, &ctx->basic_info) != JXL_DEC_SUCCESS) {
                /*
                 * this should never happen
                 * if it does it is likely a libjxl decoder bug
                 */
                av_log(avctx, AV_LOG_ERROR, "Bad libjxl basic info event\n");
                return AVERROR_EXTERNAL;
            }
            avctx->pix_fmt = libjxl_get_pix_fmt(avctx, ctx);
            if (avctx->pix_fmt == AV_PIX_FMT_NONE) {
                av_log(avctx, AV_LOG_ERROR, "Bad libjxl pixel format\n");
                return AVERROR_EXTERNAL;
            }
            if ((ret = ff_set_dimensions(avctx, ctx->basic_info.xsize, ctx->basic_info.ysize)) < 0)
                return ret;
            continue;
        case JXL_DEC_COLOR_ENCODING:
            av_log(avctx, AV_LOG_DEBUG, "COLOR_ENCODING event emitted\n");
            if ((ret = libjxl_color_encoding_event(avctx, frame)) < 0)
                return ret;
            continue;
        case JXL_DEC_NEED_IMAGE_OUT_BUFFER:
            av_log(avctx, AV_LOG_DEBUG, "NEED_IMAGE_OUT_BUFFER event emitted\n");
            if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
                return ret;
            ctx->jxl_pixfmt.align = frame->linesize[0];
            if (JxlDecoderSetImageOutBuffer(ctx->decoder, &ctx->jxl_pixfmt, frame->data[0], frame->buf[0]->size)
                != JXL_DEC_SUCCESS) {
                av_log(avctx, AV_LOG_ERROR, "Bad libjxl dec need image out buffer event\n");
                return AVERROR_EXTERNAL;
            }
#if JPEGXL_NUMERIC_VERSION >= JPEGXL_COMPUTE_NUMERIC_VERSION(0, 8, 0)
            if (JxlDecoderSetImageOutBitDepth(ctx->decoder, &ctx->jxl_bit_depth) != JXL_DEC_SUCCESS) {
                av_log(avctx, AV_LOG_ERROR, "Error setting output bit depth\n");
                return AVERROR_EXTERNAL;
            }
#endif
            continue;
        case JXL_DEC_FULL_IMAGE:
            /* full image is one frame, even if animated */
            av_log(avctx, AV_LOG_DEBUG, "FULL_IMAGE event emitted\n");
            frame->pict_type = AV_PICTURE_TYPE_I;
            frame->key_frame = 1;
            if (ctx->iccp) {
                AVFrameSideData *sd = av_frame_new_side_data_from_buf(frame, AV_FRAME_DATA_ICC_PROFILE, ctx->iccp);
                if (!sd)
                    return AVERROR(ENOMEM);
                /* ownership is transfered, and it is not ref-ed */
                ctx->iccp = NULL;
            }
            *got_frame = 1;
            return avpkt->size - remaining;
        case JXL_DEC_SUCCESS:
            av_log(avctx, AV_LOG_DEBUG, "SUCCESS event emitted\n");
            /*
             * The SUCCESS event isn't fired until after JXL_DEC_FULL_IMAGE. If this
             * stream only contains one JXL image then JXL_DEC_SUCCESS will never fire.
             * If the image2 sequence being decoded contains several JXL files, then
             * libjxl will fire this event after the next AVPacket has been passed,
             * which means the current packet is actually the next image in the sequence.
             * This is why we reset the decoder and populate the packet data now, since
             * this is the next packet and it has not been decoded yet. The decoder does
             * have to be reset to allow us to use it for the next image, or libjxl
             * will become very confused if the header information is not identical.
             */
            JxlDecoderReset(ctx->decoder);
            libjxl_init_jxl_decoder(avctx);
            buf = avpkt->data;
            remaining = avpkt->size;
            continue;
        default:
             av_log(avctx, AV_LOG_ERROR, "Bad libjxl event: %d\n", jret);
             return AVERROR_EXTERNAL;
        }
    }
}

static av_cold int libjxl_decode_close(AVCodecContext *avctx)
{
    LibJxlDecodeContext *ctx = avctx->priv_data;

    if (ctx->runner)
        JxlThreadParallelRunnerDestroy(ctx->runner);
    ctx->runner = NULL;
    if (ctx->decoder)
        JxlDecoderDestroy(ctx->decoder);
    ctx->decoder = NULL;
    av_buffer_unref(&ctx->iccp);

    return 0;
}

const FFCodec ff_libjxl_decoder = {
    .p.name           = "libjxl",
    CODEC_LONG_NAME("libjxl JPEG XL"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_JPEGXL,
    .priv_data_size   = sizeof(LibJxlDecodeContext),
    .init             = libjxl_decode_init,
    FF_CODEC_DECODE_CB(libjxl_decode_frame),
    .close            = libjxl_decode_close,
    .p.capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_OTHER_THREADS,
    .caps_internal    = FF_CODEC_CAP_NOT_INIT_THREADSAFE |
                        FF_CODEC_CAP_AUTO_THREADS | FF_CODEC_CAP_INIT_CLEANUP |
                        FF_CODEC_CAP_ICC_PROFILES,
    .p.wrapper_name   = "libjxl",
};
