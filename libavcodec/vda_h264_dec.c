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
#include "h264.h"
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
} VDADecoderContext;

static enum AVPixelFormat get_format(struct AVCodecContext *avctx,
        const enum AVPixelFormat *fmt)
{
    return AV_PIX_FMT_VDA_VLD;
}

static int get_buffer(AVCodecContext *avctx, AVFrame *pic)
{
    pic->type = FF_BUFFER_TYPE_USER;
    pic->data[0] = (void *)1;
    return 0;
}

static void release_buffer(AVCodecContext *avctx, AVFrame *pic)
{
    int i;

    CVPixelBufferRef cv_buffer = (CVPixelBufferRef)pic->data[3];
    CVPixelBufferUnlockBaseAddress(cv_buffer, 0);
    CVPixelBufferRelease(cv_buffer);

    for (i = 0; i < 4; i++)
        pic->data[i] = NULL;
}

static int vdadec_decode(AVCodecContext *avctx,
        void *data, int *got_frame, AVPacket *avpkt)
{
    VDADecoderContext *ctx = avctx->priv_data;
    AVFrame *pic = data;
    int ret;

    ret = ff_h264_decoder.decode(avctx, data, got_frame, avpkt);
    if (*got_frame) {
        CVPixelBufferRef cv_buffer = (CVPixelBufferRef)pic->data[3];
        CVPixelBufferLockBaseAddress(cv_buffer, 0);
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
    if (ctx->h264_initialized)
        ff_h264_decoder.close(avctx);
    return 0;
}

static av_cold int check_format(AVCodecContext *avctx)
{
    AVCodecParserContext *parser;
    uint8_t *pout;
    int psize;
    int index;
    H264Context *h;
    int ret = -1;

    /* init parser & parse file */
    parser = av_parser_init(avctx->codec->id);
    if (!parser) {
        av_log(avctx, AV_LOG_ERROR, "Failed to open H.264 parser.\n");
        goto final;
    }
    parser->flags = PARSER_FLAG_COMPLETE_FRAMES;
    index = av_parser_parse2(parser, avctx, &pout, &psize, NULL, 0, 0, 0, 0);
    if (index < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to parse this file.\n");
        goto release_parser;
    }

    /* check if support */
    h = parser->priv_data;
    switch (h->sps.bit_depth_luma) {
    case 8:
        if (!CHROMA444 && !CHROMA422) {
            // only this will H.264 decoder switch to hwaccel
            ret = 0;
            break;
        }
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported file.\n");
    }

release_parser:
    av_parser_close(parser);

final:
    return ret;
}

static av_cold int vdadec_init(AVCodecContext *avctx)
{
    VDADecoderContext *ctx = avctx->priv_data;
    struct vda_context *vda_ctx = &ctx->vda_ctx;
    OSStatus status;
    int ret;

    ctx->h264_initialized = 0;

    /* init pix_fmts of codec */
    if (!ff_h264_vda_decoder.pix_fmts) {
        if (kCFCoreFoundationVersionNumber < kCFCoreFoundationVersionNumber10_7)
            ff_h264_vda_decoder.pix_fmts = vda_pixfmts_prior_10_7;
        else
            ff_h264_vda_decoder.pix_fmts = vda_pixfmts;
    }

    /* check if VDA supports this file */
    if (check_format(avctx) < 0)
        goto failed;

    /* init vda */
    memset(vda_ctx, 0, sizeof(struct vda_context));
    vda_ctx->width = avctx->width;
    vda_ctx->height = avctx->height;
    vda_ctx->format = 'avc1';
    vda_ctx->use_sync_decoding = 1;
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
    avctx->hwaccel_context = vda_ctx;

    /* changes callback functions */
    avctx->get_format = get_format;
    avctx->get_buffer = get_buffer;
    avctx->release_buffer = release_buffer;

    /* init H.264 decoder */
    ret = ff_h264_decoder.init(avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to open H.264 decoder.\n");
        goto failed;
    }
    ctx->h264_initialized = 1;

    return 0;

failed:
    vdadec_close(avctx);
    return -1;
}

static void vdadec_flush(AVCodecContext *avctx)
{
    return ff_h264_decoder.flush(avctx);
}

AVCodec ff_h264_vda_decoder = {
    .name           = "h264_vda",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .priv_data_size = sizeof(VDADecoderContext),
    .init           = vdadec_init,
    .close          = vdadec_close,
    .decode         = vdadec_decode,
    .capabilities   = CODEC_CAP_DELAY,
    .flush          = vdadec_flush,
    .long_name      = NULL_IF_CONFIG_SMALL("H.264 (VDA acceleration)"),
};
