/*
 * Copyright(c) 2024 Intel Corporation
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

/*
* Copyright(c) 2024 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

#include <SvtJpegxsDec.h>

#include "libavutil/mem.h"
#include "libavutil/common.h"
#include "libavutil/cpu.h"
#include "libavutil/imgutils.h"
#include "libavutil/avassert.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "profiles.h"

typedef struct SvtJpegXsDecodeContext {
    AVClass* class;
    svt_jpeg_xs_image_config_t config;
    svt_jpeg_xs_decoder_api_t decoder;
    uint32_t decoder_initialized;

    /*0- AVPacket* avpkt have full frame*/
    /*1- AVPacket* avpkt have chunk of frame, need another buffer to merge packets*/
    uint32_t chunk_decoding;
    uint32_t frame_size;
    uint32_t buffer_filled_len;
    uint8_t* bitstream_buffer;
    int proxy_mode;
} SvtJpegXsDecodeContext;

static int set_pix_fmt(AVCodecContext* avctx, const svt_jpeg_xs_image_config_t *config)
{
    int ret = 0;

    switch (config->format) {
    case COLOUR_FORMAT_PLANAR_YUV420:
        if (config->bit_depth == 8)
            avctx->pix_fmt = AV_PIX_FMT_YUV420P;
        else if (config->bit_depth == 10)
            avctx->pix_fmt = AV_PIX_FMT_YUV420P10LE;
        else if (config->bit_depth == 12)
            avctx->pix_fmt = AV_PIX_FMT_YUV420P12LE;
        else
            avctx->pix_fmt = AV_PIX_FMT_YUV420P14LE;
        break;
    case COLOUR_FORMAT_PLANAR_YUV422:
        if (config->bit_depth == 8)
            avctx->pix_fmt = AV_PIX_FMT_YUV422P;
        else if (config->bit_depth == 10)
            avctx->pix_fmt = AV_PIX_FMT_YUV422P10LE;
        else if (config->bit_depth == 12)
            avctx->pix_fmt = AV_PIX_FMT_YUV422P12LE;
        else
            avctx->pix_fmt = AV_PIX_FMT_YUV422P14LE;
        break;
    case COLOUR_FORMAT_PLANAR_YUV444_OR_RGB:
        if (config->bit_depth == 8)
            avctx->pix_fmt = AV_PIX_FMT_YUV444P;
        else if (config->bit_depth == 10)
            avctx->pix_fmt = AV_PIX_FMT_YUV444P10LE;
        else if (config->bit_depth == 12)
            avctx->pix_fmt = AV_PIX_FMT_YUV444P12LE;
        else
            avctx->pix_fmt = AV_PIX_FMT_YUV444P14LE;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported pixel format.\n");
        ret = AVERROR_INVALIDDATA;
        break;
    }

    return ret;
}

static int svt_jpegxs_dec_decode(AVCodecContext* avctx, AVFrame* picture, int* got_frame, AVPacket* avpkt)
{
    SvtJpegXsDecodeContext* svt_dec = avctx->priv_data;
    SvtJxsErrorType_t err = SvtJxsErrorNone;
    int ret;
    svt_jpeg_xs_frame_t dec_input;
    svt_jpeg_xs_frame_t dec_output;
    uint32_t pixel_size;

    if (!svt_dec->decoder_initialized) {
        err = svt_jpeg_xs_decoder_get_single_frame_size_with_proxy(
            avpkt->data, avpkt->size, NULL, &svt_dec->frame_size, 1 /*quick search*/, svt_dec->decoder.proxy_mode);
        if (err) {
            av_log(avctx, AV_LOG_ERROR, "svt_jpeg_xs_decoder_get_single_frame_size_with_proxy failed, err=%d\n", err);
            return err;
        }
        if (avpkt->size < svt_dec->frame_size) {
            svt_dec->chunk_decoding = 1;
            svt_dec->bitstream_buffer = av_malloc(svt_dec->frame_size);
            if (!svt_dec->bitstream_buffer) {
                av_log(avctx, AV_LOG_ERROR, "Failed to allocate svt_dec->bitstream_buffer.\n");
                return AVERROR(ENOMEM);
            }
            av_log(avctx, AV_LOG_DEBUG, "svt_jpegxs_dec_decode, bitstream_size=%d, chunk = %d\n", svt_dec->frame_size, avpkt->size);
        }
        if (avpkt->size > svt_dec->frame_size) {
            av_log(avctx, AV_LOG_ERROR, "Single packet have data for more than one frame.\n");
            return AVERROR_EXTERNAL;
        }

        err = svt_jpeg_xs_decoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR,
                                       &svt_dec->decoder, avpkt->data, avpkt->size, &svt_dec->config);
        if (err) {
            av_log(avctx, AV_LOG_ERROR, "svt_jpeg_xs_decoder_init failed, err=%d\n", err);
            return err;
        }

        ret = set_pix_fmt(avctx, &svt_dec->config);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "set_pix_fmt failed, err=%d\n", ret);
            return ret;
        }

        ret = ff_set_dimensions(avctx, svt_dec->config.width, svt_dec->config.height);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "ff_set_dimensions failed, err=%d\n", ret);
            return ret;
        }

        svt_dec->decoder_initialized = 1;
    }

    if (avctx->skip_frame == AVDISCARD_ALL)
        return 0;

    if (svt_dec->chunk_decoding) {
        uint8_t* bitstrream_addr = svt_dec->bitstream_buffer + svt_dec->buffer_filled_len;
        int bytes_to_copy = avpkt->size;
        //Do not copy more data than allocation
        if ((bytes_to_copy + svt_dec->buffer_filled_len) > svt_dec->frame_size) {
            bytes_to_copy = svt_dec->frame_size - svt_dec->buffer_filled_len;
        }

        memcpy(bitstrream_addr, avpkt->data, bytes_to_copy);
        svt_dec->buffer_filled_len += avpkt->size;
        if (svt_dec->buffer_filled_len >= svt_dec->frame_size) {
            dec_input.bitstream.buffer = svt_dec->bitstream_buffer;
            dec_input.bitstream.allocation_size = svt_dec->frame_size;
            dec_input.bitstream.used_size = svt_dec->frame_size;
        } else {
            *got_frame = 0;
            return avpkt->size;
        }
    } else {
        dec_input.bitstream.buffer = avpkt->data;
        dec_input.bitstream.allocation_size = avpkt->size;
        dec_input.bitstream.used_size = avpkt->size;
    }
    dec_input.user_prv_ctx_ptr = avpkt;

    ret = ff_get_buffer(avctx, picture, 0);
    if (ret < 0)
        return ret;

    pixel_size = svt_dec->config.bit_depth <= 8 ? 1 : 2;

    for (int comp = 0; comp < svt_dec->config.components_num; comp++) {
        dec_input.image.data_yuv[comp] = picture->data[comp];
        dec_input.image.stride[comp] = picture->linesize[comp]/pixel_size;
        dec_input.image.alloc_size[comp] = picture->linesize[comp] * svt_dec->config.components[comp].height;
    }

    err = svt_jpeg_xs_decoder_send_frame(&svt_dec->decoder, &dec_input, 1 /*blocking*/);
    if (err) {
        av_log(avctx, AV_LOG_ERROR, "svt_jpeg_xs_decoder_send_frame failed, err=%d\n", err);
        return err;
    }

    err = svt_jpeg_xs_decoder_get_frame(&svt_dec->decoder, &dec_output, 1 /*blocking*/);
    if (err == SvtJxsErrorDecoderConfigChange) {
        av_log(avctx, AV_LOG_ERROR, "svt_jpeg_xs_decoder_get_frame return SvtJxsErrorDecoderConfigChange\n");
        return AVERROR_INPUT_CHANGED;
    }
    if (err) {
        av_log(avctx, AV_LOG_ERROR, "svt_jpeg_xs_decoder_get_frame failed, err=%d\n", err);
        return err;
    }

    if (dec_output.user_prv_ctx_ptr != avpkt) {
        av_log(avctx, AV_LOG_ERROR, "Returned different user_prv_ctx_ptr than expected\n");
        return AVERROR_EXTERNAL;
    }

    //Copy leftover from AVPacket if it contain data from two frames
    if (svt_dec->chunk_decoding) {
        int bytes_to_copy = svt_dec->buffer_filled_len % svt_dec->frame_size;
        int packet_offset = avpkt->size - bytes_to_copy;
        uint8_t* packet_addr = avpkt->data + packet_offset;

        memcpy(svt_dec->bitstream_buffer, packet_addr, bytes_to_copy);
        svt_dec->buffer_filled_len = bytes_to_copy;
    }

    *got_frame = 1;

    return avpkt->size;
}

static av_cold int svt_jpegxs_dec_free(AVCodecContext* avctx)
{
    SvtJpegXsDecodeContext* svt_dec = avctx->priv_data;

    svt_jpeg_xs_decoder_close(&svt_dec->decoder);
    av_freep(&svt_dec->bitstream_buffer);

    return 0;
}

static av_cold int svt_jpegxs_dec_init(AVCodecContext* avctx)
{
    SvtJpegXsDecodeContext* svt_dec = avctx->priv_data;

    if (av_log_get_level() < AV_LOG_DEBUG)
        svt_dec->decoder.verbose = VERBOSE_ERRORS;
    else if (av_log_get_level() == AV_LOG_DEBUG)
        svt_dec->decoder.verbose = VERBOSE_SYSTEM_INFO;
    else
        svt_dec->decoder.verbose = VERBOSE_WARNINGS;

    if (svt_dec->proxy_mode == 1)
        svt_dec->decoder.proxy_mode = proxy_mode_half;
    else if (svt_dec->proxy_mode == 2)
        svt_dec->decoder.proxy_mode = proxy_mode_quarter;
    else
        svt_dec->decoder.proxy_mode = proxy_mode_full;

    svt_dec->decoder.threads_num = FFMIN(avctx->thread_count ? avctx->thread_count : av_cpu_count(), 64);
    svt_dec->decoder.use_cpu_flags = CPU_FLAGS_ALL;

    return 0;
}

#define OFFSET(x) offsetof(SvtJpegXsDecodeContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption svtjpegxs_dec_options[] = {
    { "proxy_mode", "Resolution scaling mode", OFFSET(proxy_mode), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 2, VE, .unit = "proxy_mode" },
      { "full",     NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0}, INT_MIN, INT_MAX, VE, .unit = "proxy_mode" },
      { "half",     NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 1}, INT_MIN, INT_MAX, VE, .unit = "proxy_mode" },
      { "quarter",  NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 2}, INT_MIN, INT_MAX, VE, .unit = "proxy_mode" },
    {NULL},
};

static const AVClass svtjpegxs_dec_class = {
    .class_name = "libsvtjpegxsdec",
    .item_name = av_default_item_name,
    .option = svtjpegxs_dec_options,
    .version = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_libsvtjpegxs_decoder = {
    .p.name         = "libsvtjpegxs",
    CODEC_LONG_NAME("SVT JPEG XS(Scalable Video Technology for JPEG XS) decoder"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_JPEGXS,
    .priv_data_size = sizeof(SvtJpegXsDecodeContext),
    .init           = svt_jpegxs_dec_init,
    .close          = svt_jpegxs_dec_free,
    FF_CODEC_DECODE_CB(svt_jpegxs_dec_decode),
    .p.capabilities = AV_CODEC_CAP_OTHER_THREADS | AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE |
                      FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM |
                      FF_CODEC_CAP_AUTO_THREADS,
    .p.wrapper_name = "libsvtjpegxs",
    .p.priv_class = &svtjpegxs_dec_class,
};
