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

#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "internal.h"

typedef struct VP8DecoderContext {
    struct vpx_codec_ctx decoder;
} VP8Context;

static av_cold int vpx_init(AVCodecContext *avctx,
                            const struct vpx_codec_iface *iface)
{
    VP8Context *ctx = avctx->priv_data;
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

    avctx->pix_fmt = AV_PIX_FMT_YUV420P;
    return 0;
}

static int vp8_decode(AVCodecContext *avctx,
                      void *data, int *got_frame, AVPacket *avpkt)
{
    VP8Context *ctx = avctx->priv_data;
    AVFrame *picture = data;
    const void *iter = NULL;
    struct vpx_image *img;
    int ret;

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
        if ((ret = ff_get_buffer(avctx, picture, 0)) < 0)
            return ret;
        av_image_copy(picture->data, picture->linesize, img->planes,
                      img->stride, avctx->pix_fmt, img->d_w, img->d_h);
        *got_frame           = 1;
    }
    return avpkt->size;
}

static av_cold int vp8_free(AVCodecContext *avctx)
{
    VP8Context *ctx = avctx->priv_data;
    vpx_codec_destroy(&ctx->decoder);
    return 0;
}

#if CONFIG_LIBVPX_VP8_DECODER
static av_cold int vp8_init(AVCodecContext *avctx)
{
    return vpx_init(avctx, &vpx_codec_vp8_dx_algo);
}

AVCodec ff_libvpx_vp8_decoder = {
    .name           = "libvpx",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VP8,
    .priv_data_size = sizeof(VP8Context),
    .init           = vp8_init,
    .close          = vp8_free,
    .decode         = vp8_decode,
    .capabilities   = CODEC_CAP_AUTO_THREADS | CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("libvpx VP8"),
};
#endif /* CONFIG_LIBVPX_VP8_DECODER */

#if CONFIG_LIBVPX_VP9_DECODER
static av_cold int vp9_init(AVCodecContext *avctx)
{
    return vpx_init(avctx, &vpx_codec_vp9_dx_algo);
}

AVCodec ff_libvpx_vp9_decoder = {
    .name           = "libvpx-vp9",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VP9,
    .priv_data_size = sizeof(VP8Context),
    .init           = vp9_init,
    .close          = vp8_free,
    .decode         = vp8_decode,
    .capabilities   = CODEC_CAP_AUTO_THREADS | CODEC_CAP_EXPERIMENTAL,
    .long_name      = NULL_IF_CONFIG_SMALL("libvpx VP9"),
};
#endif /* CONFIG_LIBVPX_VP9_DECODER */
