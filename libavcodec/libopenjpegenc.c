/*
 * JPEG 2000 encoding support via OpenJPEG
 * Copyright (c) 2011 Michael Bradshaw <mbradshaw@sorensonmedia.com>
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
* JPEG 2000 encoder using libopenjpeg
*/

#include "libavutil/imgutils.h"
#include "libavutil/avassert.h"
#include "avcodec.h"
#include "libavutil/intreadwrite.h"
#define  OPJ_STATIC
#include <openjpeg.h>

typedef struct {
    opj_image_t *image;
    opj_cparameters_t enc_params;
    opj_cinfo_t *compress;
    opj_event_mgr_t event_mgr;
} LibOpenJPEGContext;

static void error_callback(const char *msg, void *data)
{
    av_log((AVCodecContext*)data, AV_LOG_ERROR, "libopenjpeg: %s\n", msg);
}

static void warning_callback(const char *msg, void *data)
{
    av_log((AVCodecContext*)data, AV_LOG_WARNING, "libopenjpeg: %s\n", msg);
}

static opj_image_t *mj2_create_image(AVCodecContext *avctx, opj_cparameters_t *parameters)
{
    opj_image_cmptparm_t *cmptparm;
    opj_image_t *img;
    int i;
    int bpp;
    int sub_dx[4];
    int sub_dy[4];
    int numcomps = 0;
    OPJ_COLOR_SPACE color_space = CLRSPC_UNKNOWN;

    sub_dx[0] = sub_dx[3] = 1;
    sub_dy[0] = sub_dy[3] = 1;
    sub_dx[1] = sub_dx[2] = 1<<av_pix_fmt_descriptors[avctx->pix_fmt].log2_chroma_w;
    sub_dy[1] = sub_dy[2] = 1<<av_pix_fmt_descriptors[avctx->pix_fmt].log2_chroma_h;

    switch (avctx->pix_fmt) {
    case PIX_FMT_GRAY8:
        color_space = CLRSPC_GRAY;
        numcomps = 1;
        bpp = 8;
        break;
    case PIX_FMT_RGB24:
        color_space = CLRSPC_SRGB;
        numcomps = 3;
        bpp = 24;
        break;
    case PIX_FMT_RGBA:
        color_space = CLRSPC_SRGB;
        numcomps = 4;
        bpp = 32;
        break;
    case PIX_FMT_YUV420P:
        color_space = CLRSPC_SYCC;
        numcomps = 3;
        bpp = 12;
        break;
    case PIX_FMT_YUV422P:
        color_space = CLRSPC_SYCC;
        numcomps = 3;
        bpp = 16;
        break;
    case PIX_FMT_YUV440P:
        color_space = CLRSPC_SYCC;
        numcomps = 3;
        bpp = 16;
        break;
    case PIX_FMT_YUV444P:
        color_space = CLRSPC_SYCC;
        numcomps = 3;
        bpp = 24;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "The requested pixel format '%s' is not supported\n", av_get_pix_fmt_name(avctx->pix_fmt));
        return NULL;
    }

    cmptparm = av_mallocz(numcomps * sizeof(opj_image_cmptparm_t));
    if (!cmptparm) {
        av_log(avctx, AV_LOG_ERROR, "Not enough memory");
        return NULL;
    }
    for (i = 0; i < numcomps; i++) {
        cmptparm[i].prec = 8;
        cmptparm[i].bpp = bpp;
        cmptparm[i].sgnd = 0;
        cmptparm[i].dx = sub_dx[i];
        cmptparm[i].dy = sub_dy[i];
        cmptparm[i].w = avctx->width / sub_dx[i];
        cmptparm[i].h = avctx->height / sub_dy[i];
    }

    img = opj_image_create(numcomps, cmptparm, color_space);
    av_freep(&cmptparm);
    return img;
}

static av_cold int libopenjpeg_encode_init(AVCodecContext *avctx)
{
    LibOpenJPEGContext *ctx = avctx->priv_data;

    opj_set_default_encoder_parameters(&ctx->enc_params);
    ctx->enc_params.tcp_numlayers = 1;
    ctx->enc_params.tcp_rates[0] = FFMAX(avctx->compression_level, 0);
    ctx->enc_params.cp_disto_alloc = 1;

    ctx->compress = opj_create_compress(CODEC_J2K);
    if (!ctx->compress) {
        av_log(avctx, AV_LOG_ERROR, "Error creating the compressor\n");
        return AVERROR(ENOMEM);
    }

    avctx->coded_frame = avcodec_alloc_frame();
    if (!avctx->coded_frame) {
        av_freep(&ctx->compress);
        av_log(avctx, AV_LOG_ERROR, "Error allocating coded frame\n");
        return AVERROR(ENOMEM);
    }

    ctx->image = mj2_create_image(avctx, &ctx->enc_params);
    if (!ctx->image) {
        av_freep(&ctx->compress);
        av_freep(&avctx->coded_frame);
        av_log(avctx, AV_LOG_ERROR, "Error creating the mj2 image\n");
        return AVERROR(EINVAL);
    }

    memset(&ctx->event_mgr, 0, sizeof(opj_event_mgr_t));
    ctx->event_mgr.error_handler = error_callback;
    ctx->event_mgr.warning_handler = warning_callback;
    ctx->event_mgr.info_handler = NULL;
    opj_set_event_mgr((opj_common_ptr)ctx->compress, &ctx->event_mgr, avctx);

    return 0;
}

static int libopenjpeg_copy_rgba(AVCodecContext *avctx, AVFrame *frame, opj_image_t *image, int numcomps)
{
    int compno;
    int x;
    int y;

    av_assert0(numcomps == 1 || numcomps == 3 || numcomps == 4);

    for (compno = 0; compno < numcomps; ++compno) {
        if (image->comps[compno].w > frame->linesize[0] / numcomps) {
            return 0;
        }
    }

    for (compno = 0; compno < numcomps; ++compno) {
        for (y = 0; y < avctx->height; ++y) {
            for (x = 0; x < avctx->width; ++x) {
                image->comps[compno].data[y * avctx->width + x] = frame->data[0][y * frame->linesize[0] + x * numcomps + compno];
            }
        }
    }

    return 1;
}

static int libopenjpeg_copy_yuv(AVCodecContext *avctx, AVFrame *frame, opj_image_t *image)
{
    int compno;
    int x;
    int y;
    int width;
    int height;
    const int numcomps = 3;

    for (compno = 0; compno < numcomps; ++compno) {
        if (image->comps[compno].w > frame->linesize[compno]) {
            return 0;
        }
    }

    for (compno = 0; compno < numcomps; ++compno) {
        width = avctx->width / image->comps[compno].dx;
        height = avctx->height / image->comps[compno].dy;
        for (y = 0; y < height; ++y) {
            for (x = 0; x < width; ++x) {
                image->comps[compno].data[y * width + x] = frame->data[compno][y * frame->linesize[compno] + x];
            }
        }
    }

    return 1;
}

static int libopenjpeg_encode_frame(AVCodecContext *avctx, uint8_t *buf, int buf_size, void *data)
{
    AVFrame *frame = data;
    LibOpenJPEGContext *ctx = avctx->priv_data;
    opj_cinfo_t *compress = ctx->compress;
    opj_image_t *image = ctx->image;
    opj_cio_t *stream;
    int cpyresult = 0;
    int len = 0;

    // x0, y0 is the top left corner of the image
    // x1, y1 is the width, height of the reference grid
    image->x0 = 0;
    image->y0 = 0;
    image->x1 = (avctx->width - 1) * ctx->enc_params.subsampling_dx + 1;
    image->y1 = (avctx->height - 1) * ctx->enc_params.subsampling_dy + 1;

    switch (avctx->pix_fmt) {
    case PIX_FMT_GRAY8:
        cpyresult = libopenjpeg_copy_rgba(avctx, frame, image, 1);
        break;
    case PIX_FMT_RGB24:
        cpyresult = libopenjpeg_copy_rgba(avctx, frame, image, 3);
        break;
    case PIX_FMT_RGBA:
        cpyresult = libopenjpeg_copy_rgba(avctx, frame, image, 4);
        break;
    case PIX_FMT_YUV420P:
    case PIX_FMT_YUV422P:
    case PIX_FMT_YUV440P:
    case PIX_FMT_YUV444P:
        cpyresult = libopenjpeg_copy_yuv(avctx, frame, image);
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "The frame's pixel format '%s' is not supported\n", av_get_pix_fmt_name(avctx->pix_fmt));
        return AVERROR(EINVAL);
        break;
    }

    if (!cpyresult) {
        av_log(avctx, AV_LOG_ERROR, "Could not copy the frame data to the internal image buffer\n");
        return -1;
    }

    opj_setup_encoder(compress, &ctx->enc_params, image);
    stream = opj_cio_open((opj_common_ptr)compress, NULL, 0);
    if (!stream) {
        av_log(avctx, AV_LOG_ERROR, "Error creating the cio stream\n");
        return AVERROR(ENOMEM);
    }

    if (!opj_encode(compress, stream, image, NULL)) {
        opj_cio_close(stream);
        av_log(avctx, AV_LOG_ERROR, "Error during the opj encode\n");
        return -1;
    }

    len = cio_tell(stream);
    if (len > buf_size) {
        opj_cio_close(stream);
        av_log(avctx, AV_LOG_ERROR, "Error with buf_size, not large enough to hold the frame\n");
        return -1;
    }

    memcpy(buf, stream->buffer, len);
    opj_cio_close(stream);
    return len;
}

static av_cold int libopenjpeg_encode_close(AVCodecContext *avctx)
{
    LibOpenJPEGContext *ctx = avctx->priv_data;

    opj_destroy_compress(ctx->compress);
    opj_image_destroy(ctx->image);
    av_freep(&avctx->coded_frame);
    return 0 ;
}


AVCodec ff_libopenjpeg_encoder = {
    .name = "libopenjpeg",
    .type = AVMEDIA_TYPE_VIDEO,
    .id = CODEC_ID_JPEG2000,
    .priv_data_size = sizeof(LibOpenJPEGContext),
    .init = libopenjpeg_encode_init,
    .encode = libopenjpeg_encode_frame,
    .close = libopenjpeg_encode_close,
    .decode = NULL,
    .capabilities = 0,
    .pix_fmts = (const enum PixelFormat[]){PIX_FMT_GRAY8,PIX_FMT_RGB24,PIX_FMT_RGBA,PIX_FMT_YUV420P,PIX_FMT_YUV422P,PIX_FMT_YUV440P,PIX_FMT_YUV444P},
    .long_name = NULL_IF_CONFIG_SMALL("OpenJPEG based JPEG 2000 encoder"),
} ;
