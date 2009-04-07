/*
 * Dirac decoder support via Schroedinger libraries
 * Copyright (c) 2008 BBC, Anuradha Suraparaju <asuraparaju at gmail dot com >
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
* @file libavcodec/libschroedingerdec.c
* Dirac decoder support via libschroedinger-1.0 libraries. More details about
* the Schroedinger project can be found at http://www.diracvideo.org/.
* The library implements Dirac Specification Version 2.2.
* (http://dirac.sourceforge.net/specification.html).
*/

#include "avcodec.h"
#include "libdirac_libschro.h"
#include "libschroedinger.h"

#undef NDEBUG
#include <assert.h>


#include <schroedinger/schro.h>
#include <schroedinger/schrodebug.h>
#include <schroedinger/schrovideoformat.h>

/** libschroedinger decoder private data */
typedef struct FfmpegSchroDecoderParams
{
    /** Schroedinger video format */
    SchroVideoFormat *format;

    /** Schroedinger frame format */
    SchroFrameFormat frame_format;

    /** decoder handle */
    SchroDecoder* decoder;

    /** queue storing decoded frames */
    FfmpegDiracSchroQueue dec_frame_queue;

    /** end of sequence signalled */
    int eos_signalled;

    /** end of sequence pulled */
    int eos_pulled;

    /** decoded picture */
    AVPicture dec_pic;
} FfmpegSchroDecoderParams;

typedef struct FfmpegSchroParseUnitContext
{
    const uint8_t *buf;
    int           buf_size;
} FfmpegSchroParseUnitContext;


static void libschroedinger_decode_buffer_free (SchroBuffer *schro_buf,
                                                void *priv);

static void FfmpegSchroParseContextInit (FfmpegSchroParseUnitContext *parse_ctx,
                                         const uint8_t *buf, int buf_size)
{
    parse_ctx->buf           = buf;
    parse_ctx->buf_size      = buf_size;
}

static SchroBuffer* FfmpegFindNextSchroParseUnit (FfmpegSchroParseUnitContext *parse_ctx)
{
    SchroBuffer *enc_buf = NULL;
    int next_pu_offset = 0;
    unsigned char *in_buf;

    if (parse_ctx->buf_size < 13 ||
        parse_ctx->buf[0] != 'B' ||
        parse_ctx->buf[1] != 'B' ||
        parse_ctx->buf[2] != 'C' ||
        parse_ctx->buf[3] != 'D')
        return NULL;

    next_pu_offset = (parse_ctx->buf[5] << 24) +
                     (parse_ctx->buf[6] << 16) +
                     (parse_ctx->buf[7] <<  8) +
                      parse_ctx->buf[8];

    if (next_pu_offset == 0 &&
        SCHRO_PARSE_CODE_IS_END_OF_SEQUENCE(parse_ctx->buf[4]))
        next_pu_offset = 13;

    if (next_pu_offset <= 0 || parse_ctx->buf_size < next_pu_offset)
        return NULL;

    in_buf = av_malloc(next_pu_offset);
    memcpy (in_buf, parse_ctx->buf, next_pu_offset);
    enc_buf = schro_buffer_new_with_data (in_buf, next_pu_offset);
    enc_buf->free = libschroedinger_decode_buffer_free;
    enc_buf->priv = in_buf;

    parse_ctx->buf += next_pu_offset;
    parse_ctx->buf_size -= next_pu_offset;

    return enc_buf;
}

/**
* Returns FFmpeg chroma format.
*/
static enum PixelFormat GetFfmpegChromaFormat(SchroChromaFormat schro_pix_fmt)
{
    int num_formats = sizeof(ffmpeg_schro_pixel_format_map) /
                      sizeof(ffmpeg_schro_pixel_format_map[0]);
    int idx;

    for (idx = 0; idx < num_formats; ++idx) {
        if (ffmpeg_schro_pixel_format_map[idx].schro_pix_fmt == schro_pix_fmt) {
            return ffmpeg_schro_pixel_format_map[idx].ff_pix_fmt;
        }
    }
    return PIX_FMT_NONE;
}

static av_cold int libschroedinger_decode_init(AVCodecContext *avccontext)
{

    FfmpegSchroDecoderParams *p_schro_params = avccontext->priv_data ;
    /* First of all, initialize our supporting libraries. */
    schro_init();

    schro_debug_set_level(avccontext->debug);
    p_schro_params->decoder =  schro_decoder_new();
    schro_decoder_set_skip_ratio(p_schro_params->decoder, 1);

    if (!p_schro_params->decoder)
        return -1;

    /* Initialize the decoded frame queue. */
    ff_dirac_schro_queue_init (&p_schro_params->dec_frame_queue);
    return 0 ;
}

static void libschroedinger_decode_buffer_free (SchroBuffer *schro_buf,
                                                void *priv)
{
    av_freep(&priv);
}

static void libschroedinger_decode_frame_free (void *frame)
{
    schro_frame_unref(frame);
}

static void libschroedinger_handle_first_access_unit(AVCodecContext *avccontext)
{
    FfmpegSchroDecoderParams *p_schro_params = avccontext->priv_data;
    SchroDecoder *decoder = p_schro_params->decoder;

    p_schro_params->format = schro_decoder_get_video_format (decoder);

    /* Tell FFmpeg about sequence details. */
    if(avcodec_check_dimensions(avccontext, p_schro_params->format->width,
                                p_schro_params->format->height) < 0) {
        av_log(avccontext, AV_LOG_ERROR, "invalid dimensions (%dx%d)\n",
               p_schro_params->format->width, p_schro_params->format->height);
        avccontext->height = avccontext->width = 0;
        return;
    }
    avccontext->height  = p_schro_params->format->height;
    avccontext->width   = p_schro_params->format->width;
    avccontext->pix_fmt =
                   GetFfmpegChromaFormat(p_schro_params->format->chroma_format);

    if (ff_get_schro_frame_format( p_schro_params->format->chroma_format,
                                   &p_schro_params->frame_format) == -1) {
        av_log (avccontext, AV_LOG_ERROR,
                "This codec currently only supports planar YUV 4:2:0, 4:2:2 "
                "and 4:4:4 formats.\n");
        return;
    }

    avccontext->time_base.den = p_schro_params->format->frame_rate_numerator;
    avccontext->time_base.num = p_schro_params->format->frame_rate_denominator;

    if (p_schro_params->dec_pic.data[0] == NULL)
    {
        avpicture_alloc(&p_schro_params->dec_pic,
                        avccontext->pix_fmt,
                        avccontext->width,
                        avccontext->height);
    }
}

static int libschroedinger_decode_frame(AVCodecContext *avccontext,
                                        void *data, int *data_size,
                                        AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;

    FfmpegSchroDecoderParams *p_schro_params = avccontext->priv_data;
    SchroDecoder *decoder = p_schro_params->decoder;
    SchroVideoFormat *format;
    AVPicture *picture = data;
    SchroBuffer *enc_buf;
    SchroFrame* frame;
    int state;
    int go = 1;
    int outer = 1;
    FfmpegSchroParseUnitContext parse_ctx;

    *data_size = 0;

    FfmpegSchroParseContextInit (&parse_ctx, buf, buf_size);
    if (buf_size == 0) {
        if (!p_schro_params->eos_signalled) {
            state = schro_decoder_push_end_of_stream(decoder);
            p_schro_params->eos_signalled = 1;
        }
    }

    /* Loop through all the individual parse units in the input buffer */
    do {
        if ((enc_buf = FfmpegFindNextSchroParseUnit(&parse_ctx))) {
            /* Push buffer into decoder. */
            if (SCHRO_PARSE_CODE_IS_PICTURE(enc_buf->data[4]) &&
                SCHRO_PARSE_CODE_NUM_REFS(enc_buf->data[4]) > 0)
                avccontext->has_b_frames = 1;
            state = schro_decoder_push (decoder, enc_buf);
            if (state == SCHRO_DECODER_FIRST_ACCESS_UNIT)
                  libschroedinger_handle_first_access_unit(avccontext);
            go = 1;
        }
        else
            outer = 0;
    format = p_schro_params->format;

    while (go) {
        /* Parse data and process result. */
        state = schro_decoder_wait (decoder);
        switch (state)
        {
        case SCHRO_DECODER_FIRST_ACCESS_UNIT:
            libschroedinger_handle_first_access_unit (avccontext);
            break;

        case SCHRO_DECODER_NEED_BITS:
            /* Need more input data - stop iterating over what we have. */
            go = 0;
            break;

        case SCHRO_DECODER_NEED_FRAME:
            /* Decoder needs a frame - create one and push it in. */

            frame = schro_frame_new_and_alloc(NULL,
                                              p_schro_params->frame_format,
                                              format->width,
                                              format->height);
            schro_decoder_add_output_picture (decoder, frame);
            break;

        case SCHRO_DECODER_OK:
            /* Pull a frame out of the decoder. */
            frame = schro_decoder_pull (decoder);

            if (frame) {
                ff_dirac_schro_queue_push_back(
                                             &p_schro_params->dec_frame_queue,
                                             frame);
            }
            break;
        case SCHRO_DECODER_EOS:
            go = 0;
            p_schro_params->eos_pulled = 1;
            schro_decoder_reset (decoder);
            outer = 0;
            break;

        case SCHRO_DECODER_ERROR:
            return -1;
            break;
        }
    }
    } while(outer);

    /* Grab next frame to be returned from the top of the queue. */
    frame = ff_dirac_schro_queue_pop(&p_schro_params->dec_frame_queue);

    if (frame != NULL) {
        memcpy (p_schro_params->dec_pic.data[0],
                frame->components[0].data,
                frame->components[0].length);

        memcpy (p_schro_params->dec_pic.data[1],
                frame->components[1].data,
                frame->components[1].length);

        memcpy (p_schro_params->dec_pic.data[2],
                frame->components[2].data,
                frame->components[2].length);

        /* Fill picture with current buffer data from Schroedinger. */
        avpicture_fill(picture, p_schro_params->dec_pic.data[0],
                       avccontext->pix_fmt,
                       avccontext->width, avccontext->height);

        *data_size = sizeof(AVPicture);

        /* Now free the frame resources. */
        libschroedinger_decode_frame_free (frame);
    }
    return buf_size;
}


static av_cold int libschroedinger_decode_close(AVCodecContext *avccontext)
{
    FfmpegSchroDecoderParams *p_schro_params = avccontext->priv_data;
    /* Free the decoder. */
    schro_decoder_free (p_schro_params->decoder);
    av_freep(&p_schro_params->format);

    avpicture_free (&p_schro_params->dec_pic);

    /* Free data in the output frame queue. */
    ff_dirac_schro_queue_free (&p_schro_params->dec_frame_queue,
                               libschroedinger_decode_frame_free);

    return 0 ;
}

static void libschroedinger_flush (AVCodecContext *avccontext)
{
    /* Got a seek request. Free the decoded frames queue and then reset
     * the decoder */
    FfmpegSchroDecoderParams *p_schro_params = avccontext->priv_data;

    /* Free data in the output frame queue. */
    ff_dirac_schro_queue_free (&p_schro_params->dec_frame_queue,
                               libschroedinger_decode_frame_free);

    ff_dirac_schro_queue_init (&p_schro_params->dec_frame_queue);
    schro_decoder_reset(p_schro_params->decoder);
    p_schro_params->eos_pulled = 0;
    p_schro_params->eos_signalled = 0;
}

AVCodec libschroedinger_decoder = {
     "libschroedinger",
    CODEC_TYPE_VIDEO,
    CODEC_ID_DIRAC,
    sizeof(FfmpegSchroDecoderParams),
    libschroedinger_decode_init,
    NULL,
    libschroedinger_decode_close,
    libschroedinger_decode_frame,
    CODEC_CAP_DELAY,
    .flush = libschroedinger_flush,
    .long_name = NULL_IF_CONFIG_SMALL("libschroedinger Dirac 2.2"),
};
