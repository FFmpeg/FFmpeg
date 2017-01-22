/*
 * Copyright (c) 2012, Xidorn Quan
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
 * H.264 decoder via VDA
 * @author Xidorn Quan <quanxunzhen@gmail.com>
 */

#include <string.h>
#include <CoreFoundation/CoreFoundation.h>

#include "vda.h"
#include "h264dec.h"
#include "avcodec.h"

#ifndef kCFCoreFoundationVersionNumber10_7
#define kCFCoreFoundationVersionNumber10_7      635.00
#endif

extern AVCodec ff_h264_decoder, ff_h264_vda_decoder;

static const enum AVPixelFormat vda_pixfmts_prior_10_7[] = {
    AV_PIX_FMT_UYVY422,
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat vda_pixfmts[] = {
    AV_PIX_FMT_UYVY422,
    AV_PIX_FMT_YUYV422,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_NONE
};

typedef struct {
    H264Context h264ctx;
    int h264_initialized;
    struct vda_context vda_ctx;
    enum AVPixelFormat pix_fmt;

    /* for backing-up fields set by user.
     * we have to gain full control of such fields here */
    void *hwaccel_context;
    enum AVPixelFormat (*get_format)(struct AVCodecContext *s, const enum AVPixelFormat * fmt);
    int (*get_buffer2)(struct AVCodecContext *s, AVFrame *frame, int flags);
} VDADecoderContext;

static enum AVPixelFormat get_format(struct AVCodecContext *avctx,
        const enum AVPixelFormat *fmt)
{
    return AV_PIX_FMT_VDA_VLD;
}

typedef struct {
    CVPixelBufferRef cv_buffer;
} VDABufferContext;

static void release_buffer(void *opaque, uint8_t *data)
{
    VDABufferContext *context = opaque;
    CVPixelBufferUnlockBaseAddress(context->cv_buffer, 0);
    CVPixelBufferRelease(context->cv_buffer);
    av_free(context);
}

static int get_buffer2(AVCodecContext *avctx, AVFrame *pic, int flag)
{
    VDABufferContext *context = av_mallocz(sizeof(VDABufferContext));
    AVBufferRef *buffer = av_buffer_create(NULL, 0, release_buffer, context, 0);
    if (!context || !buffer) {
        av_free(context);
        return AVERROR(ENOMEM);
    }

    pic->buf[0] = buffer;
    pic->data[0] = (void *)1;
    return 0;
}

static inline void set_context(AVCodecContext *avctx)
{
    VDADecoderContext *ctx = avctx->priv_data;
    ctx->hwaccel_context = avctx->hwaccel_context;
    avctx->hwaccel_context = &ctx->vda_ctx;
    ctx->get_format = avctx->get_format;
    avctx->get_format = get_format;
    ctx->get_buffer2 = avctx->get_buffer2;
    avctx->get_buffer2 = get_buffer2;
}

static inline void restore_context(AVCodecContext *avctx)
{
    VDADecoderContext *ctx = avctx->priv_data;
    avctx->hwaccel_context = ctx->hwaccel_context;
    avctx->get_format = ctx->get_format;
    avctx->get_buffer2 = ctx->get_buffer2;
}

static int vdadec_decode(AVCodecContext *avctx,
        void *data, int *got_frame, AVPacket *avpkt)
{
    VDADecoderContext *ctx = avctx->priv_data;
    AVFrame *pic = data;
    int ret;

    set_context(avctx);
    ret = ff_h264_decoder.decode(avctx, data, got_frame, avpkt);
    restore_context(avctx);
    if (*got_frame) {
        AVBufferRef *buffer = pic->buf[0];
        VDABufferContext *context = av_buffer_get_opaque(buffer);
        CVPixelBufferRef cv_buffer = (CVPixelBufferRef)pic->data[3];

        CVPixelBufferRetain(cv_buffer);
        CVPixelBufferLockBaseAddress(cv_buffer, 0);
        context->cv_buffer = cv_buffer;
        pic->format = ctx->pix_fmt;
        if (CVPixelBufferIsPlanar(cv_buffer)) {
            int i, count = CVPixelBufferGetPlaneCount(cv_buffer);
            av_assert0(count < 4);
            for (i = 0; i < count; i++) {
                pic->data[i] = CVPixelBufferGetBaseAddressOfPlane(cv_buffer, i);
                pic->linesize[i] = CVPixelBufferGetBytesPerRowOfPlane(cv_buffer, i);
            }
        } else {
            pic->data[0] = CVPixelBufferGetBaseAddress(cv_buffer);
            pic->linesize[0] = CVPixelBufferGetBytesPerRow(cv_buffer);
        }
    }
    avctx->pix_fmt = ctx->pix_fmt;

    return ret;
}

static av_cold int vdadec_close(AVCodecContext *avctx)
{
    VDADecoderContext *ctx = avctx->priv_data;
    /* release buffers and decoder */
    ff_vda_destroy_decoder(&ctx->vda_ctx);
    /* close H.264 decoder */
    if (ctx->h264_initialized) {
        set_context(avctx);
        ff_h264_decoder.close(avctx);
        restore_context(avctx);
    }
    return 0;
}

static av_cold int vdadec_init(AVCodecContext *avctx)
{
    VDADecoderContext *ctx = avctx->priv_data;
    struct vda_context *vda_ctx = &ctx->vda_ctx;
    OSStatus status;
    int ret, i;

    ctx->h264_initialized = 0;

    /* init pix_fmts of codec */
    if (!ff_h264_vda_decoder.pix_fmts) {
        if (kCFCoreFoundationVersionNumber < kCFCoreFoundationVersionNumber10_7)
            ff_h264_vda_decoder.pix_fmts = vda_pixfmts_prior_10_7;
        else
            ff_h264_vda_decoder.pix_fmts = vda_pixfmts;
    }

    /* init vda */
    memset(vda_ctx, 0, sizeof(struct vda_context));
    vda_ctx->width = avctx->width;
    vda_ctx->height = avctx->height;
    vda_ctx->format = 'avc1';
    vda_ctx->use_sync_decoding = 1;
    vda_ctx->use_ref_buffer = 1;
    ctx->pix_fmt = avctx->get_format(avctx, avctx->codec->pix_fmts);
    switch (ctx->pix_fmt) {
    case AV_PIX_FMT_UYVY422:
        vda_ctx->cv_pix_fmt_type = '2vuy';
        break;
    case AV_PIX_FMT_YUYV422:
        vda_ctx->cv_pix_fmt_type = 'yuvs';
        break;
    case AV_PIX_FMT_NV12:
        vda_ctx->cv_pix_fmt_type = '420v';
        break;
    case AV_PIX_FMT_YUV420P:
        vda_ctx->cv_pix_fmt_type = 'y420';
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported pixel format: %d\n", avctx->pix_fmt);
        goto failed;
    }
    status = ff_vda_create_decoder(vda_ctx,
                                   avctx->extradata, avctx->extradata_size);
    if (status != kVDADecoderNoErr) {
        av_log(avctx, AV_LOG_ERROR,
                "Failed to init VDA decoder: %d.\n", status);
        goto failed;
    }

    /* init H.264 decoder */
    set_context(avctx);
    ret = ff_h264_decoder.init(avctx);
    restore_context(avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to open H.264 decoder.\n");
        goto failed;
    }
    ctx->h264_initialized = 1;

    for (i = 0; i < MAX_SPS_COUNT; i++) {
        const SPS *sps = (const SPS*)ctx->h264ctx.ps.sps_list[i]->data;
        if (sps && (sps->bit_depth_luma != 8 ||
                sps->chroma_format_idc == 2 ||
                sps->chroma_format_idc == 3)) {
            av_log(avctx, AV_LOG_ERROR, "Format is not supported.\n");
            goto failed;
        }
    }

    return 0;

failed:
    vdadec_close(avctx);
    return -1;
}

static void vdadec_flush(AVCodecContext *avctx)
{
    set_context(avctx);
    ff_h264_decoder.flush(avctx);
    restore_context(avctx);
}

AVCodec ff_h264_vda_decoder = {
    .name           = "h264_vda",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .priv_data_size = sizeof(VDADecoderContext),
    .init           = vdadec_init,
    .close          = vdadec_close,
    .decode         = vdadec_decode,
    .capabilities   = AV_CODEC_CAP_DELAY,
    .flush          = vdadec_flush,
    .long_name      = NULL_IF_CONFIG_SMALL("H.264 (VDA acceleration)"),
};
