/*
 * JPEG 2000 decoding support via OpenJPEG
 * Copyright (c) 2009 Jaikrishnan Menon <realityman@gmx.net>
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
 * JPEG 2000 decoder using libopenjpeg
 */

#define  OPJ_STATIC

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixfmt.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "thread.h"

#if HAVE_OPENJPEG_1_5_OPENJPEG_H
# include <openjpeg-1.5/openjpeg.h>
#else
# include <openjpeg.h>
#endif

#define JP2_SIG_TYPE    0x6A502020
#define JP2_SIG_VALUE   0x0D0A870A

// pix_fmts with lower bpp have to be listed before
// similar pix_fmts with higher bpp.
#define RGB_PIXEL_FORMATS   AV_PIX_FMT_RGB24,AV_PIX_FMT_RGBA,AV_PIX_FMT_RGB48,AV_PIX_FMT_RGBA64
#define GRAY_PIXEL_FORMATS  AV_PIX_FMT_GRAY8,AV_PIX_FMT_GRAY8A,AV_PIX_FMT_GRAY16
#define YUV_PIXEL_FORMATS   AV_PIX_FMT_YUV410P,AV_PIX_FMT_YUV411P,AV_PIX_FMT_YUVA420P, \
                            AV_PIX_FMT_YUV420P,AV_PIX_FMT_YUV422P,AV_PIX_FMT_YUVA422P, \
                            AV_PIX_FMT_YUV440P,AV_PIX_FMT_YUV444P,AV_PIX_FMT_YUVA444P, \
                            AV_PIX_FMT_YUV420P9,AV_PIX_FMT_YUV422P9,AV_PIX_FMT_YUV444P9, \
                            AV_PIX_FMT_YUVA420P9,AV_PIX_FMT_YUVA422P9,AV_PIX_FMT_YUVA444P9, \
                            AV_PIX_FMT_YUV420P10,AV_PIX_FMT_YUV422P10,AV_PIX_FMT_YUV444P10, \
                            AV_PIX_FMT_YUVA420P10,AV_PIX_FMT_YUVA422P10,AV_PIX_FMT_YUVA444P10, \
                            AV_PIX_FMT_YUV420P12,AV_PIX_FMT_YUV422P12,AV_PIX_FMT_YUV444P12, \
                            AV_PIX_FMT_YUV420P14,AV_PIX_FMT_YUV422P14,AV_PIX_FMT_YUV444P14, \
                            AV_PIX_FMT_YUV420P16,AV_PIX_FMT_YUV422P16,AV_PIX_FMT_YUV444P16, \
                            AV_PIX_FMT_YUVA420P16,AV_PIX_FMT_YUVA422P16,AV_PIX_FMT_YUVA444P16

static const enum AVPixelFormat libopenjpeg_rgb_pix_fmts[]  = {RGB_PIXEL_FORMATS};
static const enum AVPixelFormat libopenjpeg_gray_pix_fmts[] = {GRAY_PIXEL_FORMATS};
static const enum AVPixelFormat libopenjpeg_yuv_pix_fmts[]  = {YUV_PIXEL_FORMATS};
static const enum AVPixelFormat libopenjpeg_all_pix_fmts[]  = {RGB_PIXEL_FORMATS,GRAY_PIXEL_FORMATS,YUV_PIXEL_FORMATS};

typedef struct {
    AVClass *class;
    opj_dparameters_t dec_params;
    AVFrame image;
    int lowqual;
} LibOpenJPEGContext;

static inline int libopenjpeg_matches_pix_fmt(const opj_image_t *image, enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    int match = 1;

    if (desc->nb_components != image->numcomps) {
        return 0;
    }

    switch (desc->nb_components) {
    case 4: match = match && desc->comp[3].depth_minus1 + 1 >= image->comps[3].prec &&
                             1 == image->comps[3].dx &&
                             1 == image->comps[3].dy;
    case 3: match = match && desc->comp[2].depth_minus1 + 1 >= image->comps[2].prec &&
                             1 << desc->log2_chroma_w == image->comps[2].dx &&
                             1 << desc->log2_chroma_h == image->comps[2].dy;
    case 2: match = match && desc->comp[1].depth_minus1 + 1 >= image->comps[1].prec &&
                             1 << desc->log2_chroma_w == image->comps[1].dx &&
                             1 << desc->log2_chroma_h == image->comps[1].dy;
    case 1: match = match && desc->comp[0].depth_minus1 + 1 >= image->comps[0].prec &&
                             1 == image->comps[0].dx &&
                             1 == image->comps[0].dy;
    default:
        break;
    }

    return match;
}

static inline enum AVPixelFormat libopenjpeg_guess_pix_fmt(const opj_image_t *image) {
    int index;
    const enum AVPixelFormat *possible_fmts = NULL;
    int possible_fmts_nb = 0;

    switch (image->color_space) {
    case CLRSPC_SRGB:
        possible_fmts = libopenjpeg_rgb_pix_fmts;
        possible_fmts_nb = FF_ARRAY_ELEMS(libopenjpeg_rgb_pix_fmts);
        break;
    case CLRSPC_GRAY:
        possible_fmts = libopenjpeg_gray_pix_fmts;
        possible_fmts_nb = FF_ARRAY_ELEMS(libopenjpeg_gray_pix_fmts);
        break;
    case CLRSPC_SYCC:
        possible_fmts = libopenjpeg_yuv_pix_fmts;
        possible_fmts_nb = FF_ARRAY_ELEMS(libopenjpeg_yuv_pix_fmts);
        break;
    default:
        possible_fmts = libopenjpeg_all_pix_fmts;
        possible_fmts_nb = FF_ARRAY_ELEMS(libopenjpeg_all_pix_fmts);
        break;
    }

    for (index = 0; index < possible_fmts_nb; ++index) {
        if (libopenjpeg_matches_pix_fmt(image, possible_fmts[index])) {
            return possible_fmts[index];
        }
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
    for (i = 1; i < desc->nb_components; i++) {
        if (component_plane != desc->comp[i].plane)
            return 0;
    }
    return 1;
}

static inline void libopenjpeg_copy_to_packed8(AVFrame *picture, opj_image_t *image) {
    uint8_t *img_ptr;
    int index, x, y, c;
    for (y = 0; y < picture->height; y++) {
        index = y*picture->width;
        img_ptr = picture->data[0] + y*picture->linesize[0];
        for (x = 0; x < picture->width; x++, index++) {
            for (c = 0; c < image->numcomps; c++) {
                *img_ptr++ = image->comps[c].data[index];
            }
        }
    }
}

static inline void libopenjpeg_copy_to_packed16(AVFrame *picture, opj_image_t *image) {
    uint16_t *img_ptr;
    int index, x, y, c;
    int adjust[4];
    for (x = 0; x < image->numcomps; x++)
        adjust[x] = FFMAX(FFMIN(16 - image->comps[x].prec, 8), 0);

    for (y = 0; y < picture->height; y++) {
        index = y*picture->width;
        img_ptr = (uint16_t*) (picture->data[0] + y*picture->linesize[0]);
        for (x = 0; x < picture->width; x++, index++) {
            for (c = 0; c < image->numcomps; c++) {
                *img_ptr++ = image->comps[c].data[index] << adjust[c];
            }
        }
    }
}

static inline void libopenjpeg_copyto8(AVFrame *picture, opj_image_t *image) {
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

static inline void libopenjpeg_copyto16(AVFrame *picture, opj_image_t *image) {
    int *comp_data;
    uint16_t *img_ptr;
    int index, x, y;
    for (index = 0; index < image->numcomps; index++) {
        comp_data = image->comps[index].data;
        for (y = 0; y < image->comps[index].h; y++) {
            img_ptr = (uint16_t*) (picture->data[index] + y * picture->linesize[index]);
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
    avcodec_get_frame_defaults(&ctx->image);
    avctx->coded_frame = &ctx->image;
    return 0;
}

static av_cold int libopenjpeg_decode_init_thread_copy(AVCodecContext *avctx)
{
    LibOpenJPEGContext *ctx = avctx->priv_data;

    avctx->coded_frame = &ctx->image;
    return 0;
}

static int libopenjpeg_decode_frame(AVCodecContext *avctx,
                                    void *data, int *got_frame,
                                    AVPacket *avpkt)
{
    uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    LibOpenJPEGContext *ctx = avctx->priv_data;
    AVFrame *picture = &ctx->image, *output = data;
    const AVPixFmtDescriptor *desc;
    opj_dinfo_t *dec;
    opj_cio_t *stream;
    opj_image_t *image;
    int width, height, ret = -1;
    int pixel_size = 0;
    int ispacked = 0;
    int i;

    *got_frame = 0;

    // Check if input is a raw jpeg2k codestream or in jp2 wrapping
    if ((AV_RB32(buf)     == 12)           &&
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
        return -1;
    }
    opj_set_event_mgr((opj_common_ptr)dec, NULL, NULL);
    ctx->dec_params.cp_limit_decoding = LIMIT_TO_MAIN_HEADER;
    ctx->dec_params.cp_layer          = ctx->lowqual;
    // Tie decoder with decoding parameters
    opj_setup_decoder(dec, &ctx->dec_params);
    stream = opj_cio_open((opj_common_ptr)dec, buf, buf_size);

    if (!stream) {
        av_log(avctx, AV_LOG_ERROR,
               "Codestream could not be opened for reading.\n");
        opj_destroy_decompress(dec);
        return -1;
    }

    // Decode the header only.
    image = opj_decode_with_info(dec, stream, NULL);
    opj_cio_close(stream);

    if (!image) {
        av_log(avctx, AV_LOG_ERROR, "Error decoding codestream.\n");
        opj_destroy_decompress(dec);
        return -1;
    }

    width  = image->x1 - image->x0;
    height = image->y1 - image->y0;

    if (av_image_check_size(width, height, 0, avctx) < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "%dx%d dimension invalid.\n", width, height);
        goto done;
    }

    avcodec_set_dimensions(avctx, width, height);

    if (avctx->pix_fmt != AV_PIX_FMT_NONE)
        if (!libopenjpeg_matches_pix_fmt(image, avctx->pix_fmt))
            avctx->pix_fmt = AV_PIX_FMT_NONE;

    if (avctx->pix_fmt == AV_PIX_FMT_NONE)
        avctx->pix_fmt = libopenjpeg_guess_pix_fmt(image);

    if (avctx->pix_fmt == AV_PIX_FMT_NONE) {
        av_log(avctx, AV_LOG_ERROR, "Unable to determine pixel format\n");
        goto done;
    }
    for (i = 0; i < image->numcomps; i++)
        if (image->comps[i].prec > avctx->bits_per_raw_sample)
            avctx->bits_per_raw_sample = image->comps[i].prec;

    if (picture->data[0])
        ff_thread_release_buffer(avctx, picture);

    if (ff_thread_get_buffer(avctx, picture) < 0) {
        av_log(avctx, AV_LOG_ERROR, "ff_thread_get_buffer() failed\n");
        goto done;
    }

    ctx->dec_params.cp_limit_decoding = NO_LIMITATION;
    ctx->dec_params.cp_reduce = avctx->lowres;
    // Tie decoder with decoding parameters.
    opj_setup_decoder(dec, &ctx->dec_params);
    stream = opj_cio_open((opj_common_ptr)dec, buf, buf_size);
    if (!stream) {
        av_log(avctx, AV_LOG_ERROR,
               "Codestream could not be opened for reading.\n");
        goto done;
    }

    opj_image_destroy(image);
    // Decode the codestream
    image = opj_decode_with_info(dec, stream, NULL);
    opj_cio_close(stream);

    if (!image) {
        av_log(avctx, AV_LOG_ERROR, "Error decoding codestream.\n");
        goto done;
    }

    desc = av_pix_fmt_desc_get(avctx->pix_fmt);
    pixel_size = desc->comp[0].step_minus1 + 1;
    ispacked = libopenjpeg_ispacked(avctx->pix_fmt);

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
        goto done;
    }

    *output    = ctx->image;
    *got_frame = 1;
    ret        = buf_size;

done:
    opj_image_destroy(image);
    opj_destroy_decompress(dec);
    return ret;
}

static av_cold int libopenjpeg_decode_close(AVCodecContext *avctx)
{
    LibOpenJPEGContext *ctx = avctx->priv_data;

    if (ctx->image.data[0])
        ff_thread_release_buffer(avctx, &ctx->image);
    return 0;
}

#define OFFSET(x) offsetof(LibOpenJPEGContext, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { "lowqual", "Limit the number of layers used for decoding",    OFFSET(lowqual), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, VD },
    { NULL },
};

static const AVClass class = {
    .class_name = "libopenjpeg",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_libopenjpeg_decoder = {
    .name             = "libopenjpeg",
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_JPEG2000,
    .priv_data_size   = sizeof(LibOpenJPEGContext),
    .init             = libopenjpeg_decode_init,
    .close            = libopenjpeg_decode_close,
    .decode           = libopenjpeg_decode_frame,
    .capabilities     = CODEC_CAP_DR1 | CODEC_CAP_FRAME_THREADS,
    .max_lowres       = 31,
    .long_name        = NULL_IF_CONFIG_SMALL("OpenJPEG JPEG 2000"),
    .priv_class       = &class,
    .init_thread_copy = ONLY_IF_THREADS_ENABLED(libopenjpeg_decode_init_thread_copy),
};
