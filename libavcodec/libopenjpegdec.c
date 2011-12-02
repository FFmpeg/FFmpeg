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

#include "libavutil/imgutils.h"
#include "libavutil/pixfmt.h"
#include "avcodec.h"
#include "libavutil/intreadwrite.h"
#include "thread.h"
#define  OPJ_STATIC
#include <openjpeg.h>

#define JP2_SIG_TYPE    0x6A502020
#define JP2_SIG_VALUE   0x0D0A870A

typedef struct {
    opj_dparameters_t dec_params;
    AVFrame image;
} LibOpenJPEGContext;

static enum PixelFormat check_image_attributes(AVCodecContext *avctx, opj_image_t *image)
{
    opj_image_comp_t c0 = image->comps[0];
    opj_image_comp_t c1 = image->comps[1];
    opj_image_comp_t c2 = image->comps[2];
    int compRatio = 0;
    compRatio |= c0.dx << 15 | c0.dy << 12;
    compRatio |= c1.dx << 9  | c1.dy << 6;
    compRatio |= c2.dx << 3  | c2.dy;

    switch (compRatio) {
    case 0111111: goto libopenjpeg_yuv444_rgb;
    case 0111212: return PIX_FMT_YUV440P;
    case 0112121: goto libopenjpeg_yuv422;
    case 0112222: goto libopenjpeg_yuv420;
    default: goto libopenjpeg_rgb;
    }

libopenjpeg_yuv420:
    switch (c0.prec) {
    case 8:  return PIX_FMT_YUV420P;
    case 9:  return PIX_FMT_YUV420P9;
    case 10: return PIX_FMT_YUV420P10;
    case 16: return PIX_FMT_YUV420P16;
    }

libopenjpeg_yuv422:
    switch (c0.prec) {
    case 8:  return PIX_FMT_YUV422P;
    case 9:  return PIX_FMT_YUV422P9;
    case 10: return PIX_FMT_YUV422P10;
    case 16: return PIX_FMT_YUV422P16;
    }

libopenjpeg_yuv444_rgb:
    switch (c0.prec) {
    case 8:  return PIX_FMT_RGB24;
    case 9:  return PIX_FMT_YUV444P9;
    case 10: return PIX_FMT_YUV444P10;
    case 16: return PIX_FMT_YUV444P16;
    }

libopenjpeg_rgb:
    switch (c0.prec) {
    case 8: return PIX_FMT_RGB24;
    default: return PIX_FMT_RGB48;
    }

    return PIX_FMT_RGB24;
}

static int is_yuva420(opj_image_t *image)
{
    return image->numcomps == 4 &&
           image->comps[0].dx == 1 && image->comps[0].dy == 1 &&
           image->comps[1].dx == 2 && image->comps[1].dy == 2 &&
           image->comps[2].dx == 2 && image->comps[2].dy == 2 &&
           image->comps[3].dx == 1 && image->comps[3].dy == 1;
}

static inline int libopenjpeg_ispacked(enum PixelFormat pix_fmt) {
    int i, component_plane;
    component_plane = av_pix_fmt_descriptors[pix_fmt].comp[0].plane;
    for(i = 1; i < av_pix_fmt_descriptors[pix_fmt].nb_components; i++) {
        if (component_plane != av_pix_fmt_descriptors[pix_fmt].comp[i].plane)
            return 0;
    }
    return 1;
}

static inline void libopenjpeg_copy_to_packed8(AVFrame *picture, opj_image_t *image) {
    uint8_t *img_ptr;
    int index, x, y, c;
    for(y = 0; y < picture->height; y++) {
        index = y*picture->width;
        img_ptr = picture->data[0] + y*picture->linesize[0];
        for(x = 0; x < picture->width; x++, index++) {
            for(c = 0; c < image->numcomps; c++) {
                *img_ptr++ = image->comps[c].data[index];
            }
        }
    }
}

static inline void libopenjpeg_copy_to_packed16(AVFrame *picture, opj_image_t *image) {
    uint16_t *img_ptr;
    int index, x, y, c;
    int adjust[4];
    for (x = 0; x < image->numcomps; x++) {
        adjust[x] = FFMAX(FFMIN(16 - image->comps[x].prec, 8), 0);
    }
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

    for(index = 0; index < image->numcomps; index++) {
        comp_data = image->comps[index].data;
        for(y = 0; y < image->comps[index].h; y++) {
            img_ptr = picture->data[index] + y * picture->linesize[index];
            for(x = 0; x < image->comps[index].w; x++) {
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
    for(index = 0; index < image->numcomps; index++) {
        comp_data = image->comps[index].data;
        for(y = 0; y < image->comps[index].h; y++) {
            img_ptr = (uint16_t*) (picture->data[index] + y * picture->linesize[index]);
            for(x = 0; x < image->comps[index].w; x++) {
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
                                    void *data, int *data_size,
                                    AVPacket *avpkt)
{
    uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    LibOpenJPEGContext *ctx = avctx->priv_data;
    AVFrame *picture = &ctx->image, *output = data;
    opj_dinfo_t *dec;
    opj_cio_t *stream;
    opj_image_t *image;
    int width, height, ret = -1;
    int pixel_size = 0;
    int ispacked = 0;

    *data_size = 0;

    // Check if input is a raw jpeg2k codestream or in jp2 wrapping
    if((AV_RB32(buf) == 12) &&
       (AV_RB32(buf + 4) == JP2_SIG_TYPE) &&
       (AV_RB32(buf + 8) == JP2_SIG_VALUE)) {
        dec = opj_create_decompress(CODEC_JP2);
    } else {
        // If the AVPacket contains a jp2c box, then skip to
        // the starting byte of the codestream.
        if (AV_RB32(buf + 4) == AV_RB32("jp2c"))
            buf += 8;
        dec = opj_create_decompress(CODEC_J2K);
    }

    if(!dec) {
        av_log(avctx, AV_LOG_ERROR, "Error initializing decoder.\n");
        return -1;
    }
    opj_set_event_mgr((opj_common_ptr)dec, NULL, NULL);

    ctx->dec_params.cp_limit_decoding = LIMIT_TO_MAIN_HEADER;
    // Tie decoder with decoding parameters
    opj_setup_decoder(dec, &ctx->dec_params);
    stream = opj_cio_open((opj_common_ptr)dec, buf, buf_size);
    if(!stream) {
        av_log(avctx, AV_LOG_ERROR, "Codestream could not be opened for reading.\n");
        opj_destroy_decompress(dec);
        return -1;
    }

    // Decode the header only
    image = opj_decode_with_info(dec, stream, NULL);
    opj_cio_close(stream);
    if(!image) {
        av_log(avctx, AV_LOG_ERROR, "Error decoding codestream.\n");
        opj_destroy_decompress(dec);
        return -1;
    }
    width  = image->x1 - image->x0;
    height = image->y1 - image->y0;
    if(av_image_check_size(width, height, 0, avctx) < 0) {
        av_log(avctx, AV_LOG_ERROR, "%dx%d dimension invalid.\n", width, height);
        goto done;
    }
    avcodec_set_dimensions(avctx, width, height);

    switch (image->numcomps) {
    case 1:  avctx->pix_fmt = PIX_FMT_GRAY8;
             break;
    case 3:  avctx->pix_fmt = check_image_attributes(avctx, image);
             break;
    case 4:  avctx->pix_fmt = is_yuva420(image) ? PIX_FMT_YUVA420P : PIX_FMT_RGBA;
             break;
    default: av_log(avctx, AV_LOG_ERROR, "%d components unsupported.\n", image->numcomps);
             goto done;
    }

    if(picture->data[0])
        ff_thread_release_buffer(avctx, picture);

    if(ff_thread_get_buffer(avctx, picture) < 0){
        av_log(avctx, AV_LOG_ERROR, "ff_thread_get_buffer() failed\n");
        return -1;
    }

    ctx->dec_params.cp_limit_decoding = NO_LIMITATION;
    ctx->dec_params.cp_reduce = avctx->lowres;
    // Tie decoder with decoding parameters
    opj_setup_decoder(dec, &ctx->dec_params);
    stream = opj_cio_open((opj_common_ptr)dec, buf, buf_size);
    if(!stream) {
        av_log(avctx, AV_LOG_ERROR, "Codestream could not be opened for reading.\n");
        opj_destroy_decompress(dec);
        return -1;
    }

    // Decode the codestream
    image = opj_decode_with_info(dec, stream, NULL);
    opj_cio_close(stream);

    pixel_size = av_pix_fmt_descriptors[avctx->pix_fmt].comp[0].step_minus1 + 1;
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
        libopenjpeg_copyto16(picture, image);
        break;
    case 3:
    case 4:
        if (ispacked) {
            libopenjpeg_copy_to_packed8(picture, image);
        }
        break;
    case 6:
        if (ispacked) {
            libopenjpeg_copy_to_packed16(picture, image);
        }
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "unsupported pixel size %d\n", pixel_size);
        goto done;
    }

    *output    = ctx->image;
    *data_size = sizeof(AVPicture);
    ret = buf_size;

done:
    opj_image_destroy(image);
    opj_destroy_decompress(dec);
    return ret;
}

static av_cold int libopenjpeg_decode_close(AVCodecContext *avctx)
{
    LibOpenJPEGContext *ctx = avctx->priv_data;

    if(ctx->image.data[0])
        ff_thread_release_buffer(avctx, &ctx->image);
    return 0 ;
}


AVCodec ff_libopenjpeg_decoder = {
    .name           = "libopenjpeg",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_JPEG2000,
    .priv_data_size = sizeof(LibOpenJPEGContext),
    .init           = libopenjpeg_decode_init,
    .close          = libopenjpeg_decode_close,
    .decode         = libopenjpeg_decode_frame,
    .capabilities   = CODEC_CAP_DR1 | CODEC_CAP_FRAME_THREADS,
    .max_lowres     = 5,
    .long_name      = NULL_IF_CONFIG_SMALL("OpenJPEG based JPEG 2000 decoder"),
    .init_thread_copy = ONLY_IF_THREADS_ENABLED(libopenjpeg_decode_init_thread_copy)
};
