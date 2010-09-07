/*
 * Copyright (c) 2010, Google, Inc.
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
 * VP8 decoder support via libvpx
 */

#define VPX_CODEC_DISABLE_COMPAT 1
#include <vpx/vpx_decoder.h>
#include <vpx/vp8dx.h>

#include "libavcore/imgutils.h"
#include "avcodec.h"

typedef struct VP8DecoderContext {
    struct vpx_codec_ctx decoder;
} VP8Context;

static av_cold int vp8_init(AVCodecContext *avctx)
{
    VP8Context *ctx = avctx->priv_data;
    const struct vpx_codec_iface *iface = &vpx_codec_vp8_dx_algo;
    struct vpx_codec_dec_cfg deccfg = {
        /* token partitions+1 would be a decent choice */
        .threads = FFMIN(avctx->thread_count, 16)
    };

    av_log(avctx, AV_LOG_INFO, "%s\n", vpx_codec_version_str());
    av_log(avctx, AV_LOG_VERBOSE, "%s\n", vpx_codec_build_config());

    if (vpx_codec_dec_init(&ctx->decoder, iface, &deccfg, 0) != VPX_CODEC_OK) {
        const char *error = vpx_codec_error(&ctx->decoder);
        av_log(avctx, AV_LOG_ERROR, "Failed to initialize decoder: %s\n",
               error);
        return AVERROR(EINVAL);
    }

    avctx->pix_fmt = PIX_FMT_YUV420P;
    return 0;
}

static int vp8_decode(AVCodecContext *avctx,
                      void *data, int *data_size, AVPacket *avpkt)
{
    VP8Context *ctx = avctx->priv_data;
    AVFrame *picture = data;
    const void *iter = NULL;
    struct vpx_image *img;

    if (vpx_codec_decode(&ctx->decoder, avpkt->data, avpkt->size, NULL, 0) !=
        VPX_CODEC_OK) {
        const char *error  = vpx_codec_error(&ctx->decoder);
        const char *detail = vpx_codec_error_detail(&ctx->decoder);

        av_log(avctx, AV_LOG_ERROR, "Failed to decode frame: %s\n", error);
        if (detail)
            av_log(avctx, AV_LOG_ERROR, "  Additional information: %s\n",
                   detail);
        return AVERROR_INVALIDDATA;
    }

    if ((img = vpx_codec_get_frame(&ctx->decoder, &iter))) {
        if (img->fmt != VPX_IMG_FMT_I420) {
            av_log(avctx, AV_LOG_ERROR, "Unsupported output colorspace (%d)\n",
                   img->fmt);
            return AVERROR_INVALIDDATA;
        }

        if ((int) img->d_w != avctx->width || (int) img->d_h != avctx->height) {
            av_log(avctx, AV_LOG_INFO, "dimension change! %dx%d -> %dx%d\n",
                   avctx->width, avctx->height, img->d_w, img->d_h);
            if (av_image_check_size(img->d_w, img->d_h, 0, avctx))
                return AVERROR_INVALIDDATA;
            avcodec_set_dimensions(avctx, img->d_w, img->d_h);
        }
        picture->data[0]     = img->planes[0];
        picture->data[1]     = img->planes[1];
        picture->data[2]     = img->planes[2];
        picture->data[3]     = NULL;
        picture->linesize[0] = img->stride[0];
        picture->linesize[1] = img->stride[1];
        picture->linesize[2] = img->stride[2];
        picture->linesize[3] = 0;
        *data_size           = sizeof(AVPicture);
    }
    return avpkt->size;
}

static av_cold int vp8_free(AVCodecContext *avctx)
{
    VP8Context *ctx = avctx->priv_data;
    vpx_codec_destroy(&ctx->decoder);
    return 0;
}

AVCodec libvpx_decoder = {
    "libvpx",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_VP8,
    sizeof(VP8Context),
    vp8_init,
    NULL, /* encode */
    vp8_free,
    vp8_decode,
    0, /* capabilities */
    .long_name = NULL_IF_CONFIG_SMALL("libvpx VP8"),
};
