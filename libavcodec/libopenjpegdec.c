/*
 * JPEG 2000 decoding support via OpenJPEG
 * Copyright (c) 2009 Jaikrishnan Menon <realityman@gmx.net>
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
 * JPEG 2000 decoder using libopenjpeg
 */

#define  OPJ_STATIC
#include <openjpeg.h>

#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/pixfmt.h"

#include "avcodec.h"
#include "internal.h"
#include "thread.h"

#define JP2_SIG_TYPE    0x6A502020
#define JP2_SIG_VALUE   0x0D0A870A

// pix_fmts with lower bpp have to be listed before
// similar pix_fmts with higher bpp.
#define RGB_PIXEL_FORMATS  AV_PIX_FMT_RGB24, AV_PIX_FMT_RGBA,                 \
                           AV_PIX_FMT_RGB48, AV_PIX_FMT_RGBA64

#define GRAY_PIXEL_FORMATS AV_PIX_FMT_GRAY8, AV_PIX_FMT_YA8,                  \
                           AV_PIX_FMT_GRAY16

#define YUV_PIXEL_FORMATS  AV_PIX_FMT_YUV410P,   AV_PIX_FMT_YUV411P,          \
                           AV_PIX_FMT_YUVA420P,                               \
                           AV_PIX_FMT_YUV420P,   AV_PIX_FMT_YUV422P,          \
                           AV_PIX_FMT_YUV440P,   AV_PIX_FMT_YUV444P,          \
                           AV_PIX_FMT_YUV420P9,  AV_PIX_FMT_YUV422P9,         \
                           AV_PIX_FMT_YUV444P9,                               \
                           AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10,        \
                           AV_PIX_FMT_YUV444P10,                              \
                           AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16,        \
                           AV_PIX_FMT_YUV444P16

#define XYZ_PIXEL_FORMATS  AV_PIX_FMT_XYZ12

static const enum AVPixelFormat rgb_pix_fmts[] = {
    RGB_PIXEL_FORMATS
};
static const enum AVPixelFormat gray_pix_fmts[] = {
    GRAY_PIXEL_FORMATS
};
static const enum AVPixelFormat yuv_pix_fmts[] = {
    YUV_PIXEL_FORMATS
};
static const enum AVPixelFormat any_pix_fmts[] = {
    RGB_PIXEL_FORMATS, GRAY_PIXEL_FORMATS, YUV_PIXEL_FORMATS, XYZ_PIXEL_FORMATS
};

typedef struct LibOpenJPEGContext {
    AVClass *class;
    opj_dparameters_t dec_params;
    int lowres;
    int lowqual;
} LibOpenJPEGContext;

static int libopenjpeg_matches_pix_fmt(const opj_image_t *img,
                                       enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    int match = 1;

    if (desc->nb_components != img->numcomps) {
        return 0;
    }

    switch (desc->nb_components) {
    case 4:
        match = match &&
                desc->comp[3].depth >= img->comps[3].prec &&
                1 == img->comps[3].dx &&
                1 == img->comps[3].dy;
    case 3:
        match = match &&
                desc->comp[2].depth >= img->comps[2].prec &&
                1 << desc->log2_chroma_w == img->comps[2].dx &&
                1 << desc->log2_chroma_h == img->comps[2].dy;
    case 2:
        match = match &&
                desc->comp[1].depth >= img->comps[1].prec &&
                1 << desc->log2_chroma_w == img->comps[1].dx &&
                1 << desc->log2_chroma_h == img->comps[1].dy;
    case 1:
        match = match &&
                desc->comp[0].depth >= img->comps[0].prec &&
                1 == img->comps[0].dx &&
                1 == img->comps[0].dy;
    default:
        break;
    }

    return match;
}

static enum AVPixelFormat libopenjpeg_guess_pix_fmt(const opj_image_t *image)
{
    int index;
    const enum AVPixelFormat *possible_fmts = NULL;
    int possible_fmts_nb = 0;

    switch (image->color_space) {
    case CLRSPC_SRGB:
        possible_fmts    = rgb_pix_fmts;
        possible_fmts_nb = FF_ARRAY_ELEMS(rgb_pix_fmts);
        break;
    case CLRSPC_GRAY:
        possible_fmts    = gray_pix_fmts;
        possible_fmts_nb = FF_ARRAY_ELEMS(gray_pix_fmts);
        break;
    case CLRSPC_SYCC:
        possible_fmts    = yuv_pix_fmts;
        possible_fmts_nb = FF_ARRAY_ELEMS(yuv_pix_fmts);
        break;
    default:
        possible_fmts    = any_pix_fmts;
        possible_fmts_nb = FF_ARRAY_ELEMS(any_pix_fmts);
        break;
    }

    for (index = 0; index < possible_fmts_nb; ++index)
        if (libopenjpeg_matches_pix_fmt(image, possible_fmts[index])) {
            return possible_fmts[index];
        }

    return AV_PIX_FMT_NONE;
}

static inline int libopenjpeg_ispacked(enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    int i, component_plane;

    if (pix_fmt == AV_PIX_FMT_GRAY16)
        return 0;

    component_plane = desc->comp[0].plane;
    for (i = 1; i < desc->nb_components; i++)
        if (component_plane != desc->comp[i].plane)
            return 0;
    return 1;
}

static void libopenjpeg_copy_to_packed8(AVFrame *picture, opj_image_t *image)
{
    uint8_t *img_ptr;
    int index, x, y, c;

    for (y = 0; y < picture->height; y++) {
        index   = y * picture->width;
        img_ptr = picture->data[0] + y * picture->linesize[0];
        for (x = 0; x < picture->width; x++, index++)
            for (c = 0; c < image->numcomps; c++)
                *img_ptr++ = image->comps[c].data[index];
    }
}

static void libopenjpeg_copy_to_packed16(AVFrame *picture, opj_image_t *image)
{
    uint16_t *img_ptr;
    int index, x, y, c;
    int adjust[4];

    for (x = 0; x < image->numcomps; x++)
        adjust[x] = FFMAX(FFMIN(16 - image->comps[x].prec, 8), 0);

    for (y = 0; y < picture->height; y++) {
        index   = y * picture->width;
        img_ptr = (uint16_t *) (picture->data[0] + y * picture->linesize[0]);
        for (x = 0; x < picture->width; x++, index++)
            for (c = 0; c < image->numcomps; c++)
                *img_ptr++ = image->comps[c].data[index] << adjust[c];
    }
}

static void libopenjpeg_copyto8(AVFrame *picture, opj_image_t *image)
{
    int *comp_data;
    uint8_t *img_ptr;
    int index, x, y;

    for (index = 0; index < image->numcomps; index++) {
        comp_data = image->comps[index].data;
        for (y = 0; y < image->comps[index].h; y++) {
            img_ptr = picture->data[index] + y * picture->linesize[index];
            for (x = 0; x < image->comps[index].w; x++) {
                *img_ptr = (uint8_t) *comp_data;
                img_ptr++;
                comp_data++;
            }
        }
    }
}

static void libopenjpeg_copyto16(AVFrame *p, opj_image_t *image)
{
    int *comp_data;
    uint16_t *img_ptr;
    int index, x, y;

    for (index = 0; index < image->numcomps; index++) {
        comp_data = image->comps[index].data;
        for (y = 0; y < image->comps[index].h; y++) {
            img_ptr = (uint16_t *)(p->data[index] + y * p->linesize[index]);
            for (x = 0; x < image->comps[index].w; x++) {
                *img_ptr = *comp_data;
                img_ptr++;
                comp_data++;
            }
        }
    }
}

static av_cold int libopenjpeg_decode_init(AVCodecContext *avctx)
{
    LibOpenJPEGContext *ctx = avctx->priv_data;

    opj_set_default_decoder_parameters(&ctx->dec_params);
    return 0;
}

static int libopenjpeg_decode_frame(AVCodecContext *avctx,
                                    void *data, int *got_frame,
                                    AVPacket *avpkt)
{
    uint8_t *buf            = avpkt->data;
    int buf_size            = avpkt->size;
    LibOpenJPEGContext *ctx = avctx->priv_data;
    ThreadFrame frame       = { .f = data };
    AVFrame *picture        = data;
    const AVPixFmtDescriptor *desc;
    opj_dinfo_t *dec;
    opj_cio_t *stream;
    opj_image_t *image;
    int width, height, ret;
    int pixel_size = 0;
    int ispacked   = 0;
    int i;

    *got_frame = 0;

    // Check if input is a raw jpeg2k codestream or in jp2 wrapping
    if ((AV_RB32(buf) == 12) &&
        (AV_RB32(buf + 4) == JP2_SIG_TYPE) &&
        (AV_RB32(buf + 8) == JP2_SIG_VALUE)) {
        dec = opj_create_decompress(CODEC_JP2);
    } else {
        /* If the AVPacket contains a jp2c box, then skip to
         * the starting byte of the codestream. */
        if (AV_RB32(buf + 4) == AV_RB32("jp2c"))
            buf += 8;
        dec = opj_create_decompress(CODEC_J2K);
    }

    if (!dec) {
        av_log(avctx, AV_LOG_ERROR, "Error initializing decoder.\n");
        return AVERROR_UNKNOWN;
    }
    opj_set_event_mgr((opj_common_ptr) dec, NULL, NULL);

    ctx->dec_params.cp_limit_decoding = LIMIT_TO_MAIN_HEADER;
    ctx->dec_params.cp_reduce         = ctx->lowres;
    ctx->dec_params.cp_layer          = ctx->lowqual;
    // Tie decoder with decoding parameters
    opj_setup_decoder(dec, &ctx->dec_params);
    stream = opj_cio_open((opj_common_ptr) dec, buf, buf_size);

    if (!stream) {
        av_log(avctx, AV_LOG_ERROR,
               "Codestream could not be opened for reading.\n");
        opj_destroy_decompress(dec);
        return AVERROR_UNKNOWN;
    }

    // Decode the header only.
    image = opj_decode_with_info(dec, stream, NULL);
    opj_cio_close(stream);

    if (!image) {
        av_log(avctx, AV_LOG_ERROR, "Error decoding codestream.\n");
        opj_destroy_decompress(dec);
        return AVERROR_UNKNOWN;
    }

    width  = image->x1 - image->x0;
    height = image->y1 - image->y0;

    if (ctx->lowres) {
        width  = (width + (1 << ctx->lowres) - 1) >> ctx->lowres;
        height = (height + (1 << ctx->lowres) - 1) >> ctx->lowres;
    }

    ret = ff_set_dimensions(avctx, width, height);
    if (ret < 0)
        goto done;

    if (avctx->pix_fmt != AV_PIX_FMT_NONE)
        if (!libopenjpeg_matches_pix_fmt(image, avctx->pix_fmt))
            avctx->pix_fmt = AV_PIX_FMT_NONE;

    if (avctx->pix_fmt == AV_PIX_FMT_NONE)
        avctx->pix_fmt = libopenjpeg_guess_pix_fmt(image);

    if (avctx->pix_fmt == AV_PIX_FMT_NONE) {
        av_log(avctx, AV_LOG_ERROR, "Unable to determine pixel format\n");
        ret = AVERROR_INVALIDDATA;
        goto done;
    }

    for (i = 0; i < image->numcomps; i++)
        if (image->comps[i].prec > avctx->bits_per_raw_sample)
            avctx->bits_per_raw_sample = image->comps[i].prec;

    if ((ret = ff_thread_get_buffer(avctx, &frame, 0)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "ff_thread_get_buffer() failed\n");
        goto done;
    }

    ctx->dec_params.cp_limit_decoding = NO_LIMITATION;
    // Tie decoder with decoding parameters.
    opj_setup_decoder(dec, &ctx->dec_params);
    stream = opj_cio_open((opj_common_ptr) dec, buf, buf_size);
    if (!stream) {
        av_log(avctx, AV_LOG_ERROR,
               "Codestream could not be opened for reading.\n");
        ret = AVERROR_UNKNOWN;
        goto done;
    }

    opj_image_destroy(image);
    // Decode the codestream
    image = opj_decode_with_info(dec, stream, NULL);
    opj_cio_close(stream);

    if (!image) {
        av_log(avctx, AV_LOG_ERROR, "Error decoding codestream.\n");
        ret = AVERROR_UNKNOWN;
        goto done;
    }

    desc       = av_pix_fmt_desc_get(avctx->pix_fmt);
    pixel_size = desc->comp[0].step;
    ispacked   = libopenjpeg_ispacked(avctx->pix_fmt);

    switch (pixel_size) {
    case 1:
        if (ispacked) {
            libopenjpeg_copy_to_packed8(picture, image);
        } else {
            libopenjpeg_copyto8(picture, image);
        }
        break;
    case 2:
        if (ispacked) {
            libopenjpeg_copy_to_packed8(picture, image);
        } else {
            libopenjpeg_copyto16(picture, image);
        }
        break;
    case 3:
    case 4:
        if (ispacked) {
            libopenjpeg_copy_to_packed8(picture, image);
        }
        break;
    case 6:
    case 8:
        if (ispacked) {
            libopenjpeg_copy_to_packed16(picture, image);
        }
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "unsupported pixel size %d\n", pixel_size);
        ret = AVERROR_PATCHWELCOME;
        goto done;
    }

    *got_frame = 1;
    ret        = buf_size;

done:
    opj_image_destroy(image);
    opj_destroy_decompress(dec);
    return ret;
}

#define OFFSET(x) offsetof(LibOpenJPEGContext, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { "lowqual", "Limit the number of layers used for decoding",
        OFFSET(lowqual), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, VD },
    { "lowres",  "Lower the decoding resolution by a power of two",
        OFFSET(lowres),  AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, VD },
    { NULL },
};

static const AVClass class = {
    .class_name = "libopenjpeg",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_libopenjpeg_decoder = {
    .name           = "libopenjpeg",
    .long_name      = NULL_IF_CONFIG_SMALL("OpenJPEG JPEG 2000"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_JPEG2000,
    .priv_data_size = sizeof(LibOpenJPEGContext),
    .init           = libopenjpeg_decode_init,
    .decode         = libopenjpeg_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
    .priv_class     = &class,
};
