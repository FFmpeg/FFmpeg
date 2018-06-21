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
 * VP8/9 decoder support via libvpx
 */

#define VPX_CODEC_DISABLE_COMPAT 1
#include <vpx/vpx_decoder.h>
#include <vpx/vp8dx.h>

#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "internal.h"
#include "libvpx.h"
#include "profiles.h"

typedef struct VPxDecoderContext {
    struct vpx_codec_ctx decoder;
    struct vpx_codec_ctx decoder_alpha;
    int has_alpha_channel;
} VPxContext;

static av_cold int vpx_init(AVCodecContext *avctx,
                            const struct vpx_codec_iface *iface,
                            int is_alpha_decoder)
{
    VPxContext *ctx = avctx->priv_data;
    struct vpx_codec_dec_cfg deccfg = {
        /* token partitions+1 would be a decent choice */
        .threads = FFMIN(avctx->thread_count, 16)
    };

    av_log(avctx, AV_LOG_INFO, "%s\n", vpx_codec_version_str());
    av_log(avctx, AV_LOG_VERBOSE, "%s\n", vpx_codec_build_config());

    if (vpx_codec_dec_init(
            is_alpha_decoder ? &ctx->decoder_alpha : &ctx->decoder,
            iface, &deccfg, 0) != VPX_CODEC_OK) {
        const char *error = vpx_codec_error(&ctx->decoder);
        av_log(avctx, AV_LOG_ERROR, "Failed to initialize decoder: %s\n",
               error);
        return AVERROR(EINVAL);
    }

    return 0;
}

// returns 0 on success, AVERROR_INVALIDDATA otherwise
static int set_pix_fmt(AVCodecContext *avctx, struct vpx_image *img,
                       int has_alpha_channel)
{
    static const enum AVColorSpace colorspaces[8] = {
        AVCOL_SPC_UNSPECIFIED, AVCOL_SPC_BT470BG, AVCOL_SPC_BT709, AVCOL_SPC_SMPTE170M,
        AVCOL_SPC_SMPTE240M, AVCOL_SPC_BT2020_NCL, AVCOL_SPC_RESERVED, AVCOL_SPC_RGB,
    };
#if VPX_IMAGE_ABI_VERSION >= 4
    static const enum AVColorRange color_ranges[] = {
        AVCOL_RANGE_MPEG, AVCOL_RANGE_JPEG
    };
    avctx->color_range = color_ranges[img->range];
#endif
    avctx->colorspace = colorspaces[img->cs];
    if (avctx->codec_id == AV_CODEC_ID_VP8 && img->fmt != VPX_IMG_FMT_I420)
        return AVERROR_INVALIDDATA;
    switch (img->fmt) {
    case VPX_IMG_FMT_I420:
        if (avctx->codec_id == AV_CODEC_ID_VP9)
            avctx->profile = FF_PROFILE_VP9_0;
        avctx->pix_fmt =
            has_alpha_channel ? AV_PIX_FMT_YUVA420P : AV_PIX_FMT_YUV420P;
        return 0;
#if CONFIG_LIBVPX_VP9_DECODER
    case VPX_IMG_FMT_I422:
        avctx->profile = FF_PROFILE_VP9_1;
        avctx->pix_fmt = AV_PIX_FMT_YUV422P;
        return 0;
    case VPX_IMG_FMT_I440:
        avctx->profile = FF_PROFILE_VP9_1;
        avctx->pix_fmt = AV_PIX_FMT_YUV440P;
        return 0;
    case VPX_IMG_FMT_I444:
        avctx->profile = FF_PROFILE_VP9_1;
        avctx->pix_fmt = avctx->colorspace == AVCOL_SPC_RGB ?
                         AV_PIX_FMT_GBRP : AV_PIX_FMT_YUV444P;
        return 0;
    case VPX_IMG_FMT_I42016:
        avctx->profile = FF_PROFILE_VP9_2;
        if (img->bit_depth == 10) {
            avctx->pix_fmt = AV_PIX_FMT_YUV420P10;
            return 0;
        } else if (img->bit_depth == 12) {
            avctx->pix_fmt = AV_PIX_FMT_YUV420P12;
            return 0;
        } else {
            return AVERROR_INVALIDDATA;
        }
    case VPX_IMG_FMT_I42216:
        avctx->profile = FF_PROFILE_VP9_3;
        if (img->bit_depth == 10) {
            avctx->pix_fmt = AV_PIX_FMT_YUV422P10;
            return 0;
        } else if (img->bit_depth == 12) {
            avctx->pix_fmt = AV_PIX_FMT_YUV422P12;
            return 0;
        } else {
            return AVERROR_INVALIDDATA;
        }
    case VPX_IMG_FMT_I44016:
        avctx->profile = FF_PROFILE_VP9_3;
        if (img->bit_depth == 10) {
            avctx->pix_fmt = AV_PIX_FMT_YUV440P10;
            return 0;
        } else if (img->bit_depth == 12) {
            avctx->pix_fmt = AV_PIX_FMT_YUV440P12;
            return 0;
        } else {
            return AVERROR_INVALIDDATA;
        }
    case VPX_IMG_FMT_I44416:
        avctx->profile = FF_PROFILE_VP9_3;
        if (img->bit_depth == 10) {
            avctx->pix_fmt = avctx->colorspace == AVCOL_SPC_RGB ?
                             AV_PIX_FMT_GBRP10 : AV_PIX_FMT_YUV444P10;
            return 0;
        } else if (img->bit_depth == 12) {
            avctx->pix_fmt = avctx->colorspace == AVCOL_SPC_RGB ?
                             AV_PIX_FMT_GBRP12 : AV_PIX_FMT_YUV444P12;
            return 0;
        } else {
            return AVERROR_INVALIDDATA;
        }
#endif
    default:
        return AVERROR_INVALIDDATA;
    }
}

static int decode_frame(AVCodecContext *avctx, vpx_codec_ctx_t *decoder,
                        uint8_t *data, uint32_t data_sz)
{
    if (vpx_codec_decode(decoder, data, data_sz, NULL, 0) != VPX_CODEC_OK) {
        const char *error  = vpx_codec_error(decoder);
        const char *detail = vpx_codec_error_detail(decoder);

        av_log(avctx, AV_LOG_ERROR, "Failed to decode frame: %s\n", error);
        if (detail) {
            av_log(avctx, AV_LOG_ERROR, "  Additional information: %s\n",
                   detail);
        }
        return AVERROR_INVALIDDATA;
    }
    return 0;
}

static int vpx_decode(AVCodecContext *avctx,
                      void *data, int *got_frame, AVPacket *avpkt)
{
    VPxContext *ctx = avctx->priv_data;
    AVFrame *picture = data;
    const void *iter = NULL;
    const void *iter_alpha = NULL;
    struct vpx_image *img, *img_alpha;
    int ret;
    uint8_t *side_data = NULL;
    int side_data_size = 0;

    ret = decode_frame(avctx, &ctx->decoder, avpkt->data, avpkt->size);
    if (ret)
        return ret;

    side_data = av_packet_get_side_data(avpkt,
                                        AV_PKT_DATA_MATROSKA_BLOCKADDITIONAL,
                                        &side_data_size);
    if (side_data_size > 1) {
        const uint64_t additional_id = AV_RB64(side_data);
        side_data += 8;
        side_data_size -= 8;
        if (additional_id == 1) {  // 1 stands for alpha channel data.
            if (!ctx->has_alpha_channel) {
                ctx->has_alpha_channel = 1;
                ret = vpx_init(avctx,
#if CONFIG_LIBVPX_VP8_DECODER && CONFIG_LIBVPX_VP9_DECODER
                               (avctx->codec_id == AV_CODEC_ID_VP8) ?
                               &vpx_codec_vp8_dx_algo : &vpx_codec_vp9_dx_algo,
#elif CONFIG_LIBVPX_VP8_DECODER
                               &vpx_codec_vp8_dx_algo,
#else
                               &vpx_codec_vp9_dx_algo,
#endif
                               1);
                if (ret)
                    return ret;
            }
            ret = decode_frame(avctx, &ctx->decoder_alpha, side_data,
                               side_data_size);
            if (ret)
                return ret;
        }
    }

    if ((img = vpx_codec_get_frame(&ctx->decoder, &iter)) &&
        (!ctx->has_alpha_channel ||
         (img_alpha = vpx_codec_get_frame(&ctx->decoder_alpha, &iter_alpha)))) {
        uint8_t *planes[4];
        int linesizes[4];

        if (img->d_w > img->w || img->d_h > img->h) {
            av_log(avctx, AV_LOG_ERROR, "Display dimensions %dx%d exceed storage %dx%d\n",
                   img->d_w, img->d_h, img->w, img->h);
            return AVERROR_EXTERNAL;
        }

        if ((ret = set_pix_fmt(avctx, img, ctx->has_alpha_channel)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "Unsupported output colorspace (%d) / bit_depth (%d)\n",
                   img->fmt, img->bit_depth);
            return ret;
        }

        if ((int) img->d_w != avctx->width || (int) img->d_h != avctx->height) {
            av_log(avctx, AV_LOG_INFO, "dimension change! %dx%d -> %dx%d\n",
                   avctx->width, avctx->height, img->d_w, img->d_h);
            ret = ff_set_dimensions(avctx, img->d_w, img->d_h);
            if (ret < 0)
                return ret;
        }
        if ((ret = ff_get_buffer(avctx, picture, 0)) < 0)
            return ret;

        planes[0] = img->planes[VPX_PLANE_Y];
        planes[1] = img->planes[VPX_PLANE_U];
        planes[2] = img->planes[VPX_PLANE_V];
        planes[3] =
            ctx->has_alpha_channel ? img_alpha->planes[VPX_PLANE_Y] : NULL;
        linesizes[0] = img->stride[VPX_PLANE_Y];
        linesizes[1] = img->stride[VPX_PLANE_U];
        linesizes[2] = img->stride[VPX_PLANE_V];
        linesizes[3] =
            ctx->has_alpha_channel ? img_alpha->stride[VPX_PLANE_Y] : 0;
        av_image_copy(picture->data, picture->linesize, (const uint8_t**)planes,
                      linesizes, avctx->pix_fmt, img->d_w, img->d_h);
        *got_frame           = 1;
    }
    return avpkt->size;
}

static av_cold int vpx_free(AVCodecContext *avctx)
{
    VPxContext *ctx = avctx->priv_data;
    vpx_codec_destroy(&ctx->decoder);
    if (ctx->has_alpha_channel)
        vpx_codec_destroy(&ctx->decoder_alpha);
    return 0;
}

#if CONFIG_LIBVPX_VP8_DECODER
static av_cold int vp8_init(AVCodecContext *avctx)
{
    return vpx_init(avctx, &vpx_codec_vp8_dx_algo, 0);
}

AVCodec ff_libvpx_vp8_decoder = {
    .name           = "libvpx",
    .long_name      = NULL_IF_CONFIG_SMALL("libvpx VP8"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VP8,
    .priv_data_size = sizeof(VPxContext),
    .init           = vp8_init,
    .close          = vpx_free,
    .decode         = vpx_decode,
    .capabilities   = AV_CODEC_CAP_AUTO_THREADS | AV_CODEC_CAP_DR1,
    .wrapper_name   = "libvpx",
};
#endif /* CONFIG_LIBVPX_VP8_DECODER */

#if CONFIG_LIBVPX_VP9_DECODER
static av_cold int vp9_init(AVCodecContext *avctx)
{
    return vpx_init(avctx, &vpx_codec_vp9_dx_algo, 0);
}

AVCodec ff_libvpx_vp9_decoder = {
    .name           = "libvpx-vp9",
    .long_name      = NULL_IF_CONFIG_SMALL("libvpx VP9"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VP9,
    .priv_data_size = sizeof(VPxContext),
    .init           = vp9_init,
    .close          = vpx_free,
    .decode         = vpx_decode,
    .capabilities   = AV_CODEC_CAP_AUTO_THREADS | AV_CODEC_CAP_DR1,
    .init_static_data = ff_vp9_init_static,
    .profiles       = NULL_IF_CONFIG_SMALL(ff_vp9_profiles),
    .wrapper_name   = "libvpx",
};
#endif /* CONFIG_LIBVPX_VP9_DECODER */
