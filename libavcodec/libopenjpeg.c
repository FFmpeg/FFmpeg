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
* @file libavcodec/libopenjpeg.c
* JPEG 2000 decoder using libopenjpeg
*/

#include "avcodec.h"
#include "libavutil/intreadwrite.h"
#define  OPJ_STATIC
#include <openjpeg.h>

#define JP2_SIG_TYPE    0x6A502020
#define JP2_SIG_VALUE   0x0D0A870A

typedef struct {
    opj_dparameters_t dec_params;
    AVFrame image;
} LibOpenJPEGContext;

static int check_image_attributes(opj_image_t *image)
{
    return(image->comps[0].dx == image->comps[1].dx &&
           image->comps[1].dx == image->comps[2].dx &&
           image->comps[0].dy == image->comps[1].dy &&
           image->comps[1].dy == image->comps[2].dy &&
           image->comps[0].prec == image->comps[1].prec &&
           image->comps[1].prec == image->comps[2].prec);
}

static av_cold int libopenjpeg_decode_init(AVCodecContext *avctx)
{
    LibOpenJPEGContext *ctx = avctx->priv_data;

    opj_set_default_decoder_parameters(&ctx->dec_params);
    avctx->coded_frame = &ctx->image;
    return 0;
}

static int libopenjpeg_decode_frame(AVCodecContext *avctx,
                                    void *data, int *data_size,
                                    const uint8_t *buf, int buf_size)
{
    LibOpenJPEGContext *ctx = avctx->priv_data;
    AVFrame *picture = &ctx->image, *output = data;
    opj_dinfo_t *dec;
    opj_cio_t *stream;
    opj_image_t *image;
    int width, height, has_alpha = 0, ret = -1;
    int x, y, index;
    uint8_t *img_ptr;
    int adjust[4];

    *data_size = 0;

    // Check if input is a raw jpeg2k codestream or in jp2 wrapping
    if((AV_RB32(buf) == 12) &&
       (AV_RB32(buf + 4) == JP2_SIG_TYPE) &&
       (AV_RB32(buf + 8) == JP2_SIG_VALUE)) {
         dec = opj_create_decompress(CODEC_JP2);
    } else {
         dec = opj_create_decompress(CODEC_J2K);
    }

    if(!dec) {
        av_log(avctx, AV_LOG_ERROR, "Error initializing decoder.\n");
        return -1;
    }
    opj_set_event_mgr((opj_common_ptr)dec, NULL, NULL);

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
    if(!image) {
        av_log(avctx, AV_LOG_ERROR, "Error decoding codestream.\n");
        opj_destroy_decompress(dec);
        return -1;
    }
    width  = image->comps[0].w;
    height = image->comps[0].h;
    if(avcodec_check_dimensions(avctx, width, height) < 0) {
        av_log(avctx, AV_LOG_ERROR, "%dx%d dimension invalid.\n", width, height);
        goto done;
    }
    avcodec_set_dimensions(avctx, width, height);

    switch(image->numcomps)
    {
        case 1:  avctx->pix_fmt = PIX_FMT_GRAY8;
                 break;
        case 3:  if(check_image_attributes(image)) {
                     avctx->pix_fmt = PIX_FMT_RGB24;
                 } else {
                     avctx->pix_fmt = PIX_FMT_GRAY8;
                     av_log(avctx, AV_LOG_ERROR, "Only first component will be used.\n");
                 }
                 break;
        case 4:  has_alpha = 1;
                 avctx->pix_fmt = PIX_FMT_RGB32;
                 break;
        default: av_log(avctx, AV_LOG_ERROR, "%d components unsupported.\n", image->numcomps);
                 goto done;
    }

    if(picture->data[0])
        avctx->release_buffer(avctx, picture);

    if(avctx->get_buffer(avctx, picture) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Couldn't allocate image buffer.\n");
        return -1;
    }

    for(x = 0; x < image->numcomps; x++) {
        adjust[x] = FFMAX(image->comps[x].prec - 8, 0);
    }

    for(y = 0; y < height; y++) {
        index = y*width;
        img_ptr = picture->data[0] + y*picture->linesize[0];
        for(x = 0; x < width; x++, index++) {
            *img_ptr++ = image->comps[0].data[index] >> adjust[0];
            if(image->numcomps > 2 && check_image_attributes(image)) {
                *img_ptr++ = image->comps[1].data[index] >> adjust[1];
                *img_ptr++ = image->comps[2].data[index] >> adjust[2];
                if(has_alpha)
                    *img_ptr++ = image->comps[3].data[index] >> adjust[3];
            }
        }
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
        avctx->release_buffer(avctx, &ctx->image);
    return 0 ;
}


AVCodec libopenjpeg_decoder = {
    "libopenjpeg",
    CODEC_TYPE_VIDEO,
    CODEC_ID_JPEG2000,
    sizeof(LibOpenJPEGContext),
    libopenjpeg_decode_init,
    NULL,
    libopenjpeg_decode_close,
    libopenjpeg_decode_frame,
    NULL,
    .long_name = NULL_IF_CONFIG_SMALL("OpenJPEG based JPEG 2000 decoder"),
} ;
