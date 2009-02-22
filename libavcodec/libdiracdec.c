/*
 * Dirac decoder support via libdirac library
 * Copyright (c) 2005 BBC, Andrew Kennedy <dirac at rd dot bbc dot co dot uk>
 * Copyright (c) 2006-2008 BBC, Anuradha Suraparaju <asuraparaju at gmail dot com >
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
* @file libavcodec/libdiracdec.c
* Dirac decoder support via libdirac library; more details about the Dirac
* project can be found at http://dirac.sourceforge.net/.
* The libdirac_decoder library implements Dirac specification version 2.2
* (http://dirac.sourceforge.net/specification.html).
*/

#include "libdirac.h"

#undef NDEBUG
#include <assert.h>

#include <libdirac_decoder/dirac_parser.h>

/** contains a single frame returned from Dirac */
typedef struct FfmpegDiracDecoderParams
{
    /** decoder handle */
    dirac_decoder_t* p_decoder;

    /** buffer to hold decoded frame */
    unsigned char* p_out_frame_buf;
} FfmpegDiracDecoderParams;


/**
* returns FFmpeg chroma format
*/
static enum PixelFormat GetFfmpegChromaFormat(dirac_chroma_t dirac_pix_fmt)
{
    int num_formats = sizeof(ffmpeg_dirac_pixel_format_map) /
                      sizeof(ffmpeg_dirac_pixel_format_map[0]);
    int idx;

    for (idx = 0; idx < num_formats; ++idx) {
        if (ffmpeg_dirac_pixel_format_map[idx].dirac_pix_fmt == dirac_pix_fmt) {
            return ffmpeg_dirac_pixel_format_map[idx].ff_pix_fmt;
        }
    }
    return PIX_FMT_NONE;
}

static av_cold int libdirac_decode_init(AVCodecContext *avccontext)
{

    FfmpegDiracDecoderParams *p_dirac_params = avccontext->priv_data ;
    p_dirac_params->p_decoder =  dirac_decoder_init(avccontext->debug);

    if (!p_dirac_params->p_decoder)
        return -1;

    return 0 ;
}

static int libdirac_decode_frame(AVCodecContext *avccontext,
                                 void *data, int *data_size,
                                 const uint8_t *buf, int buf_size)
{

    FfmpegDiracDecoderParams *p_dirac_params = avccontext->priv_data;
    AVPicture *picture = data;
    AVPicture pic;
    int pict_size;
    unsigned char *buffer[3];

    *data_size = 0;

    if (buf_size>0) {
        /* set data to decode into buffer */
        dirac_buffer (p_dirac_params->p_decoder, buf, buf+buf_size);
        if ((buf[4] &0x08) == 0x08 && (buf[4] & 0x03))
            avccontext->has_b_frames = 1;
    }
    while (1) {
         /* parse data and process result */
        DecoderState state = dirac_parse (p_dirac_params->p_decoder);
        switch (state)
        {
        case STATE_BUFFER:
            return buf_size;

        case STATE_SEQUENCE:
        {
            /* tell FFmpeg about sequence details */
            dirac_sourceparams_t *src_params =
                                  &p_dirac_params->p_decoder->src_params;

            if (avcodec_check_dimensions(avccontext, src_params->width,
                                         src_params->height) < 0) {
                av_log(avccontext, AV_LOG_ERROR, "Invalid dimensions (%dx%d)\n",
                       src_params->width, src_params->height);
                avccontext->height = avccontext->width = 0;
                return -1;
            }

            avccontext->height = src_params->height;
            avccontext->width  = src_params->width;

            avccontext->pix_fmt = GetFfmpegChromaFormat(src_params->chroma);
            if (avccontext->pix_fmt == PIX_FMT_NONE) {
                av_log (avccontext, AV_LOG_ERROR,
                        "Dirac chroma format %d not supported currently\n",
                        src_params->chroma);
                return -1;
            }

            avccontext->time_base.den = src_params->frame_rate.numerator;
            avccontext->time_base.num = src_params->frame_rate.denominator;

            /* calculate output dimensions */
            avpicture_fill(&pic, NULL, avccontext->pix_fmt,
                           avccontext->width, avccontext->height);

            pict_size = avpicture_get_size(avccontext->pix_fmt,
                                           avccontext->width,
                                           avccontext->height);

            /* allocate output buffer */
            if (p_dirac_params->p_out_frame_buf == NULL)
                p_dirac_params->p_out_frame_buf = av_malloc (pict_size);
            buffer[0] = p_dirac_params->p_out_frame_buf;
            buffer[1] = p_dirac_params->p_out_frame_buf +
                        pic.linesize[0] * avccontext->height;
            buffer[2] = buffer[1] +
                        pic.linesize[1] * src_params->chroma_height;

            /* tell Dirac about output destination */
            dirac_set_buf(p_dirac_params->p_decoder, buffer, NULL);
            break;
        }
        case STATE_SEQUENCE_END:
            break;

        case STATE_PICTURE_AVAIL:
            /* fill picture with current buffer data from Dirac */
            avpicture_fill(picture, p_dirac_params->p_out_frame_buf,
                           avccontext->pix_fmt,
                           avccontext->width, avccontext->height);
            *data_size = sizeof(AVPicture);
            return buf_size;

        case STATE_INVALID:
            return -1;

        default:
            break;
        }
    }

    return buf_size;
}


static av_cold int libdirac_decode_close(AVCodecContext *avccontext)
{
    FfmpegDiracDecoderParams *p_dirac_params = avccontext->priv_data;
    dirac_decoder_close (p_dirac_params->p_decoder);

    av_freep(&p_dirac_params->p_out_frame_buf);

    return 0 ;
}

static void libdirac_flush (AVCodecContext *avccontext)
{
    /* Got a seek request. We will need free memory held in the private
     * context and free the current Dirac decoder handle and then open
     * a new decoder handle. */
    libdirac_decode_close (avccontext);
    libdirac_decode_init (avccontext);
    return;
}



AVCodec libdirac_decoder = {
    "libdirac",
    CODEC_TYPE_VIDEO,
    CODEC_ID_DIRAC,
    sizeof(FfmpegDiracDecoderParams),
    libdirac_decode_init,
    NULL,
    libdirac_decode_close,
    libdirac_decode_frame,
    CODEC_CAP_DELAY,
    .flush = libdirac_flush,
    .long_name = NULL_IF_CONFIG_SMALL("libdirac Dirac 2.2"),
} ;
