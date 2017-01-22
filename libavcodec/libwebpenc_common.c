/*
 * WebP encoding support via libwebp
 * Copyright (c) 2013 Justin Ruggles <justin.ruggles@gmail.com>
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
 * WebP encoder using libwebp: common structs and methods.
 */

#include "libwebpenc_common.h"

int ff_libwebp_error_to_averror(int err)
{
    switch (err) {
    case VP8_ENC_ERROR_OUT_OF_MEMORY:
    case VP8_ENC_ERROR_BITSTREAM_OUT_OF_MEMORY:
        return AVERROR(ENOMEM);
    case VP8_ENC_ERROR_NULL_PARAMETER:
    case VP8_ENC_ERROR_INVALID_CONFIGURATION:
    case VP8_ENC_ERROR_BAD_DIMENSION:
        return AVERROR(EINVAL);
    }
    return AVERROR_UNKNOWN;
}

av_cold int ff_libwebp_encode_init_common(AVCodecContext *avctx)
{
    LibWebPContextCommon *s = avctx->priv_data;
    int ret;

    if (avctx->global_quality >= 0)
        s->quality = av_clipf(avctx->global_quality / (float)FF_QP2LAMBDA,
                              0.0f, 100.0f);

    if (avctx->compression_level < 0 || avctx->compression_level > 6) {
        av_log(avctx, AV_LOG_WARNING, "invalid compression level: %d\n",
               avctx->compression_level);
        avctx->compression_level = av_clip(avctx->compression_level, 0, 6);
    }

    if (s->preset >= WEBP_PRESET_DEFAULT) {
        ret = WebPConfigPreset(&s->config, s->preset, s->quality);
        if (!ret)
            return AVERROR_UNKNOWN;
        s->lossless              = s->config.lossless;
        s->quality               = s->config.quality;
        avctx->compression_level = s->config.method;
    } else {
        ret = WebPConfigInit(&s->config);
        if (!ret)
            return AVERROR_UNKNOWN;

        s->config.lossless = s->lossless;
        s->config.quality  = s->quality;
        s->config.method   = avctx->compression_level;

        ret = WebPValidateConfig(&s->config);
        if (!ret)
            return AVERROR(EINVAL);
    }

    av_log(avctx, AV_LOG_DEBUG, "%s - quality=%.1f method=%d\n",
           s->lossless ? "Lossless" : "Lossy", s->quality,
           avctx->compression_level);

    return 0;
}

int ff_libwebp_get_frame(AVCodecContext *avctx, LibWebPContextCommon *s,
                         const AVFrame *frame, AVFrame **alt_frame_ptr,
                         WebPPicture **pic_ptr) {
    int ret;
    WebPPicture *pic = NULL;
    AVFrame *alt_frame = NULL;

    if (avctx->width > WEBP_MAX_DIMENSION || avctx->height > WEBP_MAX_DIMENSION) {
        av_log(avctx, AV_LOG_ERROR, "Picture size is too large. Max is %dx%d.\n",
               WEBP_MAX_DIMENSION, WEBP_MAX_DIMENSION);
        return AVERROR(EINVAL);
    }

    *pic_ptr = av_malloc(sizeof(*pic));
    pic = *pic_ptr;
    if (!pic)
        return AVERROR(ENOMEM);

    ret = WebPPictureInit(pic);
    if (!ret) {
        ret = AVERROR_UNKNOWN;
        goto end;
    }
    pic->width  = avctx->width;
    pic->height = avctx->height;

    if (avctx->pix_fmt == AV_PIX_FMT_RGB32) {
        if (!s->lossless) {
            /* libwebp will automatically convert RGB input to YUV when
               encoding lossy. */
            if (!s->conversion_warning) {
                av_log(avctx, AV_LOG_WARNING,
                       "Using libwebp for RGB-to-YUV conversion. You may want "
                       "to consider passing in YUV instead for lossy "
                       "encoding.\n");
                s->conversion_warning = 1;
            }
        }
        pic->use_argb    = 1;
        pic->argb        = (uint32_t *)frame->data[0];
        pic->argb_stride = frame->linesize[0] / 4;
    } else {
        if (frame->linesize[1] != frame->linesize[2] || s->cr_threshold) {
            if (!s->chroma_warning && !s->cr_threshold) {
                av_log(avctx, AV_LOG_WARNING,
                       "Copying frame due to differing chroma linesizes.\n");
                s->chroma_warning = 1;
            }
            *alt_frame_ptr = av_frame_alloc();
            alt_frame = *alt_frame_ptr;
            if (!alt_frame) {
                ret = AVERROR(ENOMEM);
                goto end;
            }
            alt_frame->width  = frame->width;
            alt_frame->height = frame->height;
            alt_frame->format = frame->format;
            if (s->cr_threshold)
                alt_frame->format = AV_PIX_FMT_YUVA420P;
            ret = av_frame_get_buffer(alt_frame, 32);
            if (ret < 0)
                goto end;
            alt_frame->format = frame->format;
            av_frame_copy(alt_frame, frame);
            frame = alt_frame;
            if (s->cr_threshold) {
                int x,y, x2, y2, p;
                int bs = s->cr_size;

                if (!s->ref) {
                    s->ref = av_frame_clone(frame);
                    if (!s->ref) {
                        ret = AVERROR(ENOMEM);
                        goto end;
                    }
                }

                alt_frame->format = AV_PIX_FMT_YUVA420P;
                for (y = 0; y < frame->height; y+= bs) {
                    for (x = 0; x < frame->width; x+= bs) {
                        int skip;
                        int sse = 0;
                        for (p = 0; p < 3; p++) {
                            int bs2 = bs >> !!p;
                            int w = AV_CEIL_RSHIFT(frame->width , !!p);
                            int h = AV_CEIL_RSHIFT(frame->height, !!p);
                            int xs = x >> !!p;
                            int ys = y >> !!p;
                            for (y2 = ys; y2 < FFMIN(ys + bs2, h); y2++) {
                                for (x2 = xs; x2 < FFMIN(xs + bs2, w); x2++) {
                                    int diff =  frame->data[p][frame->linesize[p] * y2 + x2]
                                              -s->ref->data[p][frame->linesize[p] * y2 + x2];
                                    sse += diff*diff;
                                }
                            }
                        }
                        skip = sse < s->cr_threshold && frame->data[3] != s->ref->data[3];
                        if (!skip)
                            for (p = 0; p < 3; p++) {
                                int bs2 = bs >> !!p;
                                int w = AV_CEIL_RSHIFT(frame->width , !!p);
                                int h = AV_CEIL_RSHIFT(frame->height, !!p);
                                int xs = x >> !!p;
                                int ys = y >> !!p;
                                for (y2 = ys; y2 < FFMIN(ys + bs2, h); y2++) {
                                    memcpy(&s->ref->data[p][frame->linesize[p] * y2 + xs],
                                            & frame->data[p][frame->linesize[p] * y2 + xs], FFMIN(bs2, w-xs));
                                }
                            }
                        for (y2 = y; y2 < FFMIN(y+bs, frame->height); y2++) {
                            memset(&frame->data[3][frame->linesize[3] * y2 + x],
                                    skip ? 0 : 255,
                                    FFMIN(bs, frame->width-x));
                        }
                    }
                }
            }
        }

        pic->use_argb  = 0;
        pic->y         = frame->data[0];
        pic->u         = frame->data[1];
        pic->v         = frame->data[2];
        pic->y_stride  = frame->linesize[0];
        pic->uv_stride = frame->linesize[1];
        if (frame->format == AV_PIX_FMT_YUVA420P) {
            pic->colorspace = WEBP_YUV420A;
            pic->a          = frame->data[3];
            pic->a_stride   = frame->linesize[3];
            if (alt_frame)
                WebPCleanupTransparentArea(pic);
        } else {
            pic->colorspace = WEBP_YUV420;
        }

        if (s->lossless) {
            /* We do not have a way to automatically prioritize RGB over YUV
               in automatic pixel format conversion based on whether we're
               encoding lossless or lossy, so we do conversion with libwebp as
               a convenience. */
            if (!s->conversion_warning) {
                av_log(avctx, AV_LOG_WARNING,
                       "Using libwebp for YUV-to-RGB conversion. You may want "
                       "to consider passing in RGB instead for lossless "
                       "encoding.\n");
                s->conversion_warning = 1;
            }

#if (WEBP_ENCODER_ABI_VERSION <= 0x201)
            /* libwebp should do the conversion automatically, but there is a
               bug that causes it to return an error instead, so a work-around
               is required.
               See https://code.google.com/p/webp/issues/detail?id=178 */
            pic->memory_ = (void*)1;  /* something non-null */
            ret = WebPPictureYUVAToARGB(pic);
            if (!ret) {
                av_log(avctx, AV_LOG_ERROR,
                       "WebPPictureYUVAToARGB() failed with error: %d\n",
                       pic->error_code);
                ret = libwebp_error_to_averror(pic->error_code);
                goto end;
            }
            pic->memory_ = NULL;  /* restore pointer */
#endif
        }
    }
end:
    return ret;
}
