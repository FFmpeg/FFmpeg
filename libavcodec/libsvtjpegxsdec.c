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
    svt_jpeg_xs_image_config_t config;
    svt_jpeg_xs_decoder_api_t decoder;
    svt_jpeg_xs_frame_t input;
    svt_jpeg_xs_frame_t output;
    uint32_t decoder_initialized;

    int proxy_mode;
} SvtJpegXsDecodeContext;

static int set_pix_fmt(void *logctx, const svt_jpeg_xs_image_config_t *config)
{
    int ret = AVERROR_BUG;

    switch (config->format) {
    case COLOUR_FORMAT_PLANAR_YUV420:
        if (config->bit_depth == 8)
            return AV_PIX_FMT_YUV420P;
        else if (config->bit_depth == 10)
            return AV_PIX_FMT_YUV420P10LE;
        else if (config->bit_depth == 12)
            return AV_PIX_FMT_YUV420P12LE;
        else
            return AV_PIX_FMT_YUV420P14LE;
        break;
    case COLOUR_FORMAT_PLANAR_YUV422:
        if (config->bit_depth == 8)
            return AV_PIX_FMT_YUV422P;
        else if (config->bit_depth == 10)
            return AV_PIX_FMT_YUV422P10LE;
        else if (config->bit_depth == 12)
            return AV_PIX_FMT_YUV422P12LE;
        else
            return AV_PIX_FMT_YUV422P14LE;
        break;
    case COLOUR_FORMAT_PLANAR_YUV444_OR_RGB:
        if (config->bit_depth == 8)
            return AV_PIX_FMT_YUV444P;
        else if (config->bit_depth == 10)
            return AV_PIX_FMT_YUV444P10LE;
        else if (config->bit_depth == 12)
            return AV_PIX_FMT_YUV444P12LE;
        else
            return AV_PIX_FMT_YUV444P14LE;
        break;
    default:
        av_log(logctx, AV_LOG_ERROR, "Unsupported pixel format.\n");
        ret = AVERROR_INVALIDDATA;
        break;
    }

    return ret;
}

static int svt_jpegxs_dec_decode(AVCodecContext* avctx, AVFrame* picture, int* got_frame, AVPacket* avpkt)
{
    SvtJpegXsDecodeContext* svt_dec = avctx->priv_data;
    SvtJxsErrorType_t err = SvtJxsErrorNone;
    uint32_t frame_size;
    int ret;

    err = svt_jpeg_xs_decoder_get_single_frame_size_with_proxy(
        avpkt->data, avpkt->size, &svt_dec->config, &frame_size, 1 /*quick search*/, svt_dec->decoder.proxy_mode);
    if (err) {
        av_log(avctx, AV_LOG_ERROR, "svt_jpeg_xs_decoder_get_single_frame_size_with_proxy failed, err=%d\n", err);
        return AVERROR_EXTERNAL;
    }
    if (avpkt->size < frame_size) {
        av_log(avctx, AV_LOG_ERROR, "Not enough data in a packet.\n");
        return AVERROR(EINVAL);
    }
    if (avpkt->size > frame_size) {
        av_log(avctx, AV_LOG_ERROR, "Single packet have data for more than one frame.\n");
        return AVERROR(EINVAL);
    }

    ret = set_pix_fmt(avctx, &svt_dec->config);
    if (ret < 0)
        return ret;

    if (!svt_dec->decoder_initialized || ret != avctx->pix_fmt ||
        avctx->width != svt_dec->config.width || avctx->height != svt_dec->config.height) {
        if (svt_dec->decoder_initialized)
            svt_jpeg_xs_decoder_close(&svt_dec->decoder);
        err = svt_jpeg_xs_decoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR,
                                       &svt_dec->decoder, avpkt->data, avpkt->size, &svt_dec->config);
        if (err) {
            av_log(avctx, AV_LOG_ERROR, "svt_jpeg_xs_decoder_init failed, err=%d\n", err);
            return AVERROR_EXTERNAL;
        }

        avctx->pix_fmt = ret;

        ret = ff_set_dimensions(avctx, svt_dec->config.width, svt_dec->config.height);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "ff_set_dimensions failed, err=%d\n", ret);
            return ret;
        }

        svt_dec->decoder_initialized = 1;
    }

    if (avctx->skip_frame == AVDISCARD_ALL)
        return 0;

    svt_dec->input.bitstream.buffer = avpkt->data;
    svt_dec->input.bitstream.allocation_size = avpkt->size;
    svt_dec->input.bitstream.used_size = avpkt->size;
    svt_dec->input.user_prv_ctx_ptr = avpkt;

    ret = ff_get_buffer(avctx, picture, 0);
    if (ret < 0)
        return ret;

    unsigned pixel_shift = svt_dec->config.bit_depth <= 8 ? 0 : 1;
    for (int comp = 0; comp < svt_dec->config.components_num; comp++) {
        svt_dec->input.image.data_yuv[comp] = picture->data[comp];
        svt_dec->input.image.stride[comp] = picture->linesize[comp] >> pixel_shift;
        svt_dec->input.image.alloc_size[comp] = picture->linesize[comp] * svt_dec->config.components[comp].height;
    }

    err = svt_jpeg_xs_decoder_send_frame(&svt_dec->decoder, &svt_dec->input, 1 /*blocking*/);
    if (err) {
        av_log(avctx, AV_LOG_ERROR, "svt_jpeg_xs_decoder_send_frame failed, err=%d\n", err);
        return AVERROR_EXTERNAL;
    }

    err = svt_jpeg_xs_decoder_get_frame(&svt_dec->decoder, &svt_dec->output, 1 /*blocking*/);
    if (err) {
        av_log(avctx, AV_LOG_ERROR, "svt_jpeg_xs_decoder_get_frame failed, err=%d\n", err);
        return AVERROR_EXTERNAL;
    }

    if (svt_dec->output.user_prv_ctx_ptr != avpkt) {
        av_log(avctx, AV_LOG_ERROR, "Returned different user_prv_ctx_ptr than expected\n");
        return AVERROR_EXTERNAL;
    }

    *got_frame = 1;

    return avpkt->size;
}

static av_cold int svt_jpegxs_dec_free(AVCodecContext* avctx)
{
    SvtJpegXsDecodeContext* svt_dec = avctx->priv_data;

    svt_jpeg_xs_decoder_close(&svt_dec->decoder);

    return 0;
}

static av_cold int svt_jpegxs_dec_init(AVCodecContext* avctx)
{
    SvtJpegXsDecodeContext* svt_dec = avctx->priv_data;

    int log_level = av_log_get_level();
    svt_dec->decoder.verbose = log_level < AV_LOG_DEBUG ? VERBOSE_ERRORS :
                                  log_level == AV_LOG_DEBUG ? VERBOSE_SYSTEM_INFO : VERBOSE_WARNINGS;

    if (avctx->lowres == 1)
        svt_dec->decoder.proxy_mode = proxy_mode_half;
    else if (avctx->lowres == 2)
        svt_dec->decoder.proxy_mode = proxy_mode_quarter;
    else
        svt_dec->decoder.proxy_mode = proxy_mode_full;

    int thread_count = avctx->thread_count ? avctx->thread_count : av_cpu_count();
    svt_dec->decoder.threads_num = FFMIN(thread_count, 64);
    svt_dec->decoder.use_cpu_flags = CPU_FLAGS_ALL;

    return 0;
}

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
    .p.max_lowres   = 2,
    .p.wrapper_name = "libsvtjpegxs",
};
