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
#include "libavutil/error.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"
#include "libavutil/frame.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "internal.h"

#include <jxl/decode.h>
#include <jxl/thread_parallel_runner.h>
#include "libjxl.h"

typedef struct LibJxlDecodeContext {
    void *runner;
    JxlDecoder *decoder;
    JxlBasicInfo basic_info;
    JxlPixelFormat jxl_pixfmt;
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

static enum AVPixelFormat libjxl_get_pix_fmt(AVCodecContext *avctx, JxlBasicInfo *basic_info, JxlPixelFormat *format)
{
    format->endianness = JXL_NATIVE_ENDIAN;
    format->num_channels = basic_info->num_color_channels + (basic_info->alpha_bits > 0);
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
        if (basic_info->bits_per_sample > 16)
            av_log(avctx, AV_LOG_WARNING, "Downsampling larger integer to 16-bit via libjxl\n");
        if (basic_info->exponent_bits_per_sample)
            av_log(avctx, AV_LOG_WARNING, "Downsampling float to 16-bit integer via libjxl\n");
        format->data_type = JXL_TYPE_UINT16;
        return basic_info->alpha_bits ? AV_PIX_FMT_RGBA64 : AV_PIX_FMT_RGB48;
    }

    return AV_PIX_FMT_NONE;
}

static int libjxl_decode_frame(AVCodecContext *avctx, AVFrame *frame, int *got_frame, AVPacket *avpkt)
{
    LibJxlDecodeContext *ctx = avctx->priv_data;
    uint8_t *buf = avpkt->data;
    size_t remaining = avpkt->size, iccp_len;
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
            avctx->pix_fmt = libjxl_get_pix_fmt(avctx, &ctx->basic_info, &ctx->jxl_pixfmt);
            if (avctx->pix_fmt == AV_PIX_FMT_NONE) {
                av_log(avctx, AV_LOG_ERROR, "Bad libjxl pixel format\n");
                return AVERROR_EXTERNAL;
            }
            ret = ff_set_dimensions(avctx, ctx->basic_info.xsize, ctx->basic_info.ysize);
            if (ret < 0)
                return ret;
            continue;
        case JXL_DEC_COLOR_ENCODING:
            av_log(avctx, AV_LOG_DEBUG, "COLOR_ENCODING event emitted\n");
            jret = JxlDecoderGetICCProfileSize(ctx->decoder, &ctx->jxl_pixfmt, JXL_COLOR_PROFILE_TARGET_ORIGINAL, &iccp_len);
            if (jret == JXL_DEC_SUCCESS && iccp_len > 0) {
                av_buffer_unref(&ctx->iccp);
                ctx->iccp = av_buffer_alloc(iccp_len);
                if (!ctx->iccp)
                    return AVERROR(ENOMEM);
                jret = JxlDecoderGetColorAsICCProfile(ctx->decoder, &ctx->jxl_pixfmt, JXL_COLOR_PROFILE_TARGET_ORIGINAL, ctx->iccp->data, iccp_len);
                if (jret != JXL_DEC_SUCCESS)
                    av_buffer_unref(&ctx->iccp);
            }
            continue;
        case JXL_DEC_NEED_IMAGE_OUT_BUFFER:
            av_log(avctx, AV_LOG_DEBUG, "NEED_IMAGE_OUT_BUFFER event emitted\n");
            ret = ff_get_buffer(avctx, frame, 0);
            if (ret < 0)
                return ret;
            ctx->jxl_pixfmt.align = frame->linesize[0];
            if (JxlDecoderSetImageOutBuffer(ctx->decoder, &ctx->jxl_pixfmt, frame->data[0], frame->buf[0]->size) != JXL_DEC_SUCCESS) {
                av_log(avctx, AV_LOG_ERROR, "Bad libjxl dec need image out buffer event\n");
                return AVERROR_EXTERNAL;
            }
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
    .p.long_name      = NULL_IF_CONFIG_SMALL("libjxl JPEG XL"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_JPEGXL,
    .priv_data_size   = sizeof(LibJxlDecodeContext),
    .init             = libjxl_decode_init,
    FF_CODEC_DECODE_CB(libjxl_decode_frame),
    .close            = libjxl_decode_close,
    .p.capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_OTHER_THREADS,
    .caps_internal    = FF_CODEC_CAP_AUTO_THREADS | FF_CODEC_CAP_INIT_CLEANUP,
    .p.wrapper_name   = "libjxl",
};
