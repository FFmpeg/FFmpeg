/*
 * JPEG 2000 encoding support via OpenJPEG
 * Copyright (c) 2011 Michael Bradshaw <mbradshaw@sorensonmedia.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * JPEG 2000 encoder using libopenjpeg
 */

#define  OPJ_STATIC
#include <openjpeg.h>

#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "internal.h"

typedef struct LibOpenJPEGContext {
    AVClass *avclass;
    opj_image_t *image;
    opj_cparameters_t enc_params;
    opj_cinfo_t *compress;
    opj_event_mgr_t event_mgr;
    int format;
    int profile;
    int prog_order;
    int cinema_mode;
    int numresolution;
    int numlayers;
    int disto_alloc;
    int fixed_alloc;
    int fixed_quality;
} LibOpenJPEGContext;

static void error_callback(const char *msg, void *data)
{
    av_log(data, AV_LOG_ERROR, "%s\n", msg);
}

static void warning_callback(const char *msg, void *data)
{
    av_log(data, AV_LOG_WARNING, "%s\n", msg);
}

static void info_callback(const char *msg, void *data)
{
    av_log(data, AV_LOG_DEBUG, "%s\n", msg);
}

static opj_image_t *libopenjpeg_create_image(AVCodecContext *avctx,
                                             opj_cparameters_t *parameters)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(avctx->pix_fmt);
    opj_image_cmptparm_t *cmptparm;
    OPJ_COLOR_SPACE color_space;
    opj_image_t *img;
    int i;
    int sub_dx[4];
    int sub_dy[4];
    int numcomps = desc->nb_components;

    sub_dx[0] =
    sub_dx[3] = 1;
    sub_dy[0] =
    sub_dy[3] = 1;
    sub_dx[1] =
    sub_dx[2] = 1 << desc->log2_chroma_w;
    sub_dy[1] =
    sub_dy[2] = 1 << desc->log2_chroma_h;

    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_GRAY8:
    case AV_PIX_FMT_GRAY16:
    case AV_PIX_FMT_YA8:
        color_space = CLRSPC_GRAY;
        break;
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_RGBA:
    case AV_PIX_FMT_RGB48:
    case AV_PIX_FMT_RGBA64:
        color_space = CLRSPC_SRGB;
        break;
    case AV_PIX_FMT_YUV410P:
    case AV_PIX_FMT_YUV411P:
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUV440P:
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUVA420P:
    case AV_PIX_FMT_YUV420P9:
    case AV_PIX_FMT_YUV422P9:
    case AV_PIX_FMT_YUV444P9:
    case AV_PIX_FMT_YUV420P10:
    case AV_PIX_FMT_YUV422P10:
    case AV_PIX_FMT_YUV444P10:
    case AV_PIX_FMT_YUV420P16:
    case AV_PIX_FMT_YUV422P16:
    case AV_PIX_FMT_YUV444P16:
        color_space = CLRSPC_SYCC;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR,
               "The requested pixel format '%s' is not supported\n",
               av_get_pix_fmt_name(avctx->pix_fmt));
        return NULL;
    }

    cmptparm = av_mallocz(numcomps * sizeof(*cmptparm));
    if (!cmptparm) {
        av_log(avctx, AV_LOG_ERROR, "Not enough memory");
        return NULL;
    }

    for (i = 0; i < numcomps; i++) {
        cmptparm[i].prec = desc->comp[i].depth;
        cmptparm[i].bpp  = desc->comp[i].depth;
        cmptparm[i].sgnd = 0;
        cmptparm[i].dx   = sub_dx[i];
        cmptparm[i].dy   = sub_dy[i];
        cmptparm[i].w    = avctx->width / sub_dx[i];
        cmptparm[i].h    = avctx->height / sub_dy[i];
    }

    img = opj_image_create(numcomps, cmptparm, color_space);
    av_freep(&cmptparm);
    return img;
}

static av_cold int libopenjpeg_encode_init(AVCodecContext *avctx)
{
    LibOpenJPEGContext *ctx = avctx->priv_data;
    int err = AVERROR(ENOMEM);

    opj_set_default_encoder_parameters(&ctx->enc_params);

    ctx->enc_params.cp_rsiz          = ctx->profile;
    ctx->enc_params.mode             = !!avctx->global_quality;
    ctx->enc_params.cp_cinema        = ctx->cinema_mode;
    ctx->enc_params.prog_order       = ctx->prog_order;
    ctx->enc_params.numresolution    = ctx->numresolution;
    ctx->enc_params.cp_disto_alloc   = ctx->disto_alloc;
    ctx->enc_params.cp_fixed_alloc   = ctx->fixed_alloc;
    ctx->enc_params.cp_fixed_quality = ctx->fixed_quality;
    ctx->enc_params.tcp_numlayers    = ctx->numlayers;
    ctx->enc_params.tcp_rates[0]     = FFMAX(avctx->compression_level, 0) * 2;

    ctx->compress = opj_create_compress(ctx->format);
    if (!ctx->compress) {
        av_log(avctx, AV_LOG_ERROR, "Error creating the compressor\n");
        return AVERROR(ENOMEM);
    }

    ctx->image = libopenjpeg_create_image(avctx, &ctx->enc_params);
    if (!ctx->image) {
        av_log(avctx, AV_LOG_ERROR, "Error creating the mj2 image\n");
        err = AVERROR(EINVAL);
        goto fail;
    }

    ctx->event_mgr.info_handler    = info_callback;
    ctx->event_mgr.error_handler   = error_callback;
    ctx->event_mgr.warning_handler = warning_callback;
    opj_set_event_mgr((opj_common_ptr) ctx->compress, &ctx->event_mgr, avctx);

    return 0;

fail:
    av_freep(&ctx->compress);
    return err;
}

static void libopenjpeg_copy_packed8(AVCodecContext *avctx,
                                     const AVFrame *frame, opj_image_t *image)
{
    int compno;
    int x, y;
    int image_index, frame_index;
    const int numcomps = image->numcomps;

    for (compno = 0; compno < numcomps; ++compno)
        for (y = 0; y < avctx->height; ++y) {
            image_index = y * avctx->width;
            frame_index = y * frame->linesize[0] + compno;
            for (x = 0; x < avctx->width; ++x) {
                image->comps[compno].data[image_index++] =
                    frame->data[0][frame_index];
                frame_index += numcomps;
            }
        }
}

static void libopenjpeg_copy_packed16(AVCodecContext *avctx,
                                      const AVFrame *frame, opj_image_t *image)
{
    int compno;
    int x, y;
    int image_index, frame_index;
    const int numcomps  = image->numcomps;
    uint16_t *frame_ptr = (uint16_t *)frame->data[0];

    for (compno = 0; compno < numcomps; ++compno)
        for (y = 0; y < avctx->height; ++y) {
            image_index = y * avctx->width;
            frame_index = y * (frame->linesize[0] / 2) + compno;
            for (x = 0; x < avctx->width; ++x) {
                image->comps[compno].data[image_index++] =
                    frame_ptr[frame_index];
                frame_index += numcomps;
            }
        }
}

static void libopenjpeg_copy_unpacked8(AVCodecContext *avctx,
                                       const AVFrame *frame, opj_image_t *image)
{
    int compno;
    int x, y;
    int width, height;
    int image_index, frame_index;
    const int numcomps = image->numcomps;

    for (compno = 0; compno < numcomps; ++compno) {
        width  = avctx->width / image->comps[compno].dx;
        height = avctx->height / image->comps[compno].dy;
        for (y = 0; y < height; ++y) {
            image_index = y * width;
            frame_index = y * frame->linesize[compno];
            for (x = 0; x < width; ++x)
                image->comps[compno].data[image_index++] =
                    frame->data[compno][frame_index++];
        }
    }
}

static void libopenjpeg_copy_unpacked16(AVCodecContext *avctx,
                                        const AVFrame *frame,
                                        opj_image_t *image)
{
    int compno;
    int x, y;
    int width, height;
    int image_index, frame_index;
    const int numcomps = image->numcomps;
    uint16_t *frame_ptr;

    for (compno = 0; compno < numcomps; ++compno) {
        width     = avctx->width / image->comps[compno].dx;
        height    = avctx->height / image->comps[compno].dy;
        frame_ptr = (uint16_t *)frame->data[compno];
        for (y = 0; y < height; ++y) {
            image_index = y * width;
            frame_index = y * (frame->linesize[compno] / 2);
            for (x = 0; x < width; ++x)
                image->comps[compno].data[image_index++] =
                    frame_ptr[frame_index++];
        }
    }
}

static int libopenjpeg_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                                    const AVFrame *frame, int *got_packet)
{
    LibOpenJPEGContext *ctx = avctx->priv_data;
    opj_cinfo_t *compress   = ctx->compress;
    opj_image_t *image      = ctx->image;
    opj_cio_t *stream;
    int ret, len;

    // x0, y0 is the top left corner of the image
    // x1, y1 is the width, height of the reference grid
    image->x0 = 0;
    image->y0 = 0;
    image->x1 = (avctx->width - 1) * ctx->enc_params.subsampling_dx + 1;
    image->y1 = (avctx->height - 1) * ctx->enc_params.subsampling_dy + 1;

    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_RGBA:
    case AV_PIX_FMT_YA8:
        libopenjpeg_copy_packed8(avctx, frame, image);
        break;
    case AV_PIX_FMT_RGB48:
    case AV_PIX_FMT_RGBA64:
        libopenjpeg_copy_packed16(avctx, frame, image);
        break;
    case AV_PIX_FMT_GRAY8:
    case AV_PIX_FMT_YUV410P:
    case AV_PIX_FMT_YUV411P:
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUV440P:
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUVA420P:
        libopenjpeg_copy_unpacked8(avctx, frame, image);
        break;
    case AV_PIX_FMT_GRAY16:
    case AV_PIX_FMT_YUV420P9:
    case AV_PIX_FMT_YUV422P9:
    case AV_PIX_FMT_YUV444P9:
    case AV_PIX_FMT_YUV444P10:
    case AV_PIX_FMT_YUV422P10:
    case AV_PIX_FMT_YUV420P10:
    case AV_PIX_FMT_YUV444P16:
    case AV_PIX_FMT_YUV422P16:
    case AV_PIX_FMT_YUV420P16:
        libopenjpeg_copy_unpacked16(avctx, frame, image);
        break;
    default:
        av_log(avctx, AV_LOG_ERROR,
               "The frame's pixel format '%s' is not supported\n",
               av_get_pix_fmt_name(avctx->pix_fmt));
        return AVERROR(EINVAL);
        break;
    }

    opj_setup_encoder(compress, &ctx->enc_params, image);
    stream = opj_cio_open((opj_common_ptr) compress, NULL, 0);
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
    if ((ret = ff_alloc_packet(pkt, len)) < 0) {
        opj_cio_close(stream);
        return ret;
    }

    memcpy(pkt->data, stream->buffer, len);
    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;
    opj_cio_close(stream);
    return 0;
}

static av_cold int libopenjpeg_encode_close(AVCodecContext *avctx)
{
    LibOpenJPEGContext *ctx = avctx->priv_data;

    opj_destroy_compress(ctx->compress);
    opj_image_destroy(ctx->image);
    return 0;
}

#define OFFSET(x) offsetof(LibOpenJPEGContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "format",        "Codec Format",      OFFSET(format),        AV_OPT_TYPE_INT,   { .i64 = CODEC_JP2   }, CODEC_J2K, CODEC_JP2,   VE, "format"      },
    { "j2k",           NULL,                0,                     AV_OPT_TYPE_CONST, { .i64 = CODEC_J2K   }, 0,         0,           VE, "format"      },
    { "jp2",           NULL,                0,                     AV_OPT_TYPE_CONST, { .i64 = CODEC_JP2   }, 0,         0,           VE, "format"      },
    { "profile",       NULL,                OFFSET(profile),       AV_OPT_TYPE_INT,   { .i64 = STD_RSIZ    }, STD_RSIZ,  CINEMA4K,    VE, "profile"     },
    { "jpeg2000",      NULL,                0,                     AV_OPT_TYPE_CONST, { .i64 = STD_RSIZ    }, 0,         0,           VE, "profile"     },
    { "cinema2k",      NULL,                0,                     AV_OPT_TYPE_CONST, { .i64 = CINEMA2K    }, 0,         0,           VE, "profile"     },
    { "cinema4k",      NULL,                0,                     AV_OPT_TYPE_CONST, { .i64 = CINEMA4K    }, 0,         0,           VE, "profile"     },
    { "cinema_mode",   "Digital Cinema",    OFFSET(cinema_mode),   AV_OPT_TYPE_INT,   { .i64 = OFF         }, OFF,       CINEMA4K_24, VE, "cinema_mode" },
    { "off",           NULL,                0,                     AV_OPT_TYPE_CONST, { .i64 = OFF         }, 0,         0,           VE, "cinema_mode" },
    { "2k_24",         NULL,                0,                     AV_OPT_TYPE_CONST, { .i64 = CINEMA2K_24 }, 0,         0,           VE, "cinema_mode" },
    { "2k_48",         NULL,                0,                     AV_OPT_TYPE_CONST, { .i64 = CINEMA2K_48 }, 0,         0,           VE, "cinema_mode" },
    { "4k_24",         NULL,                0,                     AV_OPT_TYPE_CONST, { .i64 = CINEMA4K_24 }, 0,         0,           VE, "cinema_mode" },
    { "prog_order",    "Progression Order", OFFSET(prog_order),    AV_OPT_TYPE_INT,   { .i64 = LRCP        }, LRCP,      CPRL,        VE, "prog_order"  },
    { "lrcp",          NULL,                0,                     AV_OPT_TYPE_CONST, { .i64 = LRCP        }, 0,         0,           VE, "prog_order"  },
    { "rlcp",          NULL,                0,                     AV_OPT_TYPE_CONST, { .i64 = RLCP        }, 0,         0,           VE, "prog_order"  },
    { "rpcl",          NULL,                0,                     AV_OPT_TYPE_CONST, { .i64 = RPCL        }, 0,         0,           VE, "prog_order"  },
    { "pcrl",          NULL,                0,                     AV_OPT_TYPE_CONST, { .i64 = PCRL        }, 0,         0,           VE, "prog_order"  },
    { "cprl",          NULL,                0,                     AV_OPT_TYPE_CONST, { .i64 = CPRL        }, 0,         0,           VE, "prog_order"  },
    { "numresolution", NULL,                OFFSET(numresolution), AV_OPT_TYPE_INT,   { .i64 = 6           }, 1,         10,          VE },
    { "numlayers",     NULL,                OFFSET(numlayers),     AV_OPT_TYPE_INT,   { .i64 = 1           }, 1,         10,          VE },
    { "disto_alloc",   NULL,                OFFSET(disto_alloc),   AV_OPT_TYPE_INT,   { .i64 = 1           }, 0,         1,           VE },
    { "fixed_alloc",   NULL,                OFFSET(fixed_alloc),   AV_OPT_TYPE_INT,   { .i64 = 0           }, 0,         1,           VE },
    { "fixed_quality", NULL,                OFFSET(fixed_quality), AV_OPT_TYPE_INT,   { .i64 = 0           }, 0,         1,           VE },
    { NULL },
};

static const AVClass class = {
    .class_name = "libopenjpeg",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_libopenjpeg_encoder = {
    .name           = "libopenjpeg",
    .long_name      = NULL_IF_CONFIG_SMALL("OpenJPEG JPEG 2000"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_JPEG2000,
    .priv_data_size = sizeof(LibOpenJPEGContext),
    .init           = libopenjpeg_encode_init,
    .encode2        = libopenjpeg_encode_frame,
    .close          = libopenjpeg_encode_close,
    .capabilities   = 0,
    .pix_fmts       = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_RGB24, AV_PIX_FMT_RGBA, AV_PIX_FMT_RGB48,
        AV_PIX_FMT_RGBA64,
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY16, AV_PIX_FMT_YA8,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
        AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
        AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
        AV_PIX_FMT_NONE
    },
    .priv_class     = &class,
};
