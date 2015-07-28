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
* @file
* Dirac decoder support via libschroedinger-1.0 libraries. More details about
* the Schroedinger project can be found at http://www.diracvideo.org/.
* The library implements Dirac Specification Version 2.2.
* (http://dirac.sourceforge.net/specification.html).
*/

#include <string.h>

#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "avcodec.h"
#include "internal.h"
#include "libschroedinger.h"

#include <schroedinger/schro.h>
#include <schroedinger/schrodebug.h>
#include <schroedinger/schrovideoformat.h>

/** SchroFrame and Pts relation */
typedef struct LibSchroFrameContext {
     SchroFrame *frame;
     int64_t pts;
} LibSchroFrameContext;

/** libschroedinger decoder private data */
typedef struct SchroDecoderParams {
    /** Schroedinger video format */
    SchroVideoFormat *format;

    /** Schroedinger frame format */
    SchroFrameFormat frame_format;

    /** decoder handle */
    SchroDecoder* decoder;

    /** queue storing decoded frames */
    FFSchroQueue dec_frame_queue;

    /** end of sequence signalled */
    int eos_signalled;

    /** end of sequence pulled */
    int eos_pulled;
} SchroDecoderParams;

typedef struct SchroParseUnitContext {
    const uint8_t *buf;
    int           buf_size;
} SchroParseUnitContext;


static void libschroedinger_decode_buffer_free(SchroBuffer *schro_buf,
                                               void *priv)
{
    av_freep(&priv);
}

static void parse_context_init(SchroParseUnitContext *parse_ctx,
                               const uint8_t *buf, int buf_size)
{
    parse_ctx->buf           = buf;
    parse_ctx->buf_size      = buf_size;
}

static SchroBuffer *find_next_parse_unit(SchroParseUnitContext *parse_ctx)
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
    if (!in_buf) {
        av_log(parse_ctx, AV_LOG_ERROR, "Unable to allocate input buffer\n");
        return NULL;
    }

    memcpy(in_buf, parse_ctx->buf, next_pu_offset);
    enc_buf       = schro_buffer_new_with_data(in_buf, next_pu_offset);
    enc_buf->free = libschroedinger_decode_buffer_free;
    enc_buf->priv = in_buf;

    parse_ctx->buf      += next_pu_offset;
    parse_ctx->buf_size -= next_pu_offset;

    return enc_buf;
}

/**
* Returns FFmpeg chroma format.
*/
static enum AVPixelFormat get_chroma_format(SchroChromaFormat schro_pix_fmt)
{
    int num_formats = sizeof(schro_pixel_format_map) /
                      sizeof(schro_pixel_format_map[0]);
    int idx;

    for (idx = 0; idx < num_formats; ++idx)
        if (schro_pixel_format_map[idx].schro_pix_fmt == schro_pix_fmt)
            return schro_pixel_format_map[idx].ff_pix_fmt;
    return AV_PIX_FMT_NONE;
}

static av_cold int libschroedinger_decode_init(AVCodecContext *avctx)
{

    SchroDecoderParams *p_schro_params = avctx->priv_data;
    /* First of all, initialize our supporting libraries. */
    schro_init();

    schro_debug_set_level(avctx->debug);
    p_schro_params->decoder = schro_decoder_new();
    schro_decoder_set_skip_ratio(p_schro_params->decoder, 1);

    if (!p_schro_params->decoder)
        return -1;

    /* Initialize the decoded frame queue. */
    ff_schro_queue_init(&p_schro_params->dec_frame_queue);
    return 0;
}

static void libschroedinger_decode_frame_free(void *frame)
{
    schro_frame_unref(frame);
}

static void libschroedinger_handle_first_access_unit(AVCodecContext *avctx)
{
    SchroDecoderParams *p_schro_params = avctx->priv_data;
    SchroDecoder *decoder = p_schro_params->decoder;

    p_schro_params->format = schro_decoder_get_video_format(decoder);

    /* Tell FFmpeg about sequence details. */
    if (av_image_check_size(p_schro_params->format->width,
                            p_schro_params->format->height, 0, avctx) < 0) {
        av_log(avctx, AV_LOG_ERROR, "invalid dimensions (%dx%d)\n",
               p_schro_params->format->width, p_schro_params->format->height);
        avctx->height = avctx->width = 0;
        return;
    }
    avctx->height  = p_schro_params->format->height;
    avctx->width   = p_schro_params->format->width;
    avctx->pix_fmt = get_chroma_format(p_schro_params->format->chroma_format);

    if (ff_get_schro_frame_format(p_schro_params->format->chroma_format,
                                  &p_schro_params->frame_format) == -1) {
        av_log(avctx, AV_LOG_ERROR,
               "This codec currently only supports planar YUV 4:2:0, 4:2:2 "
               "and 4:4:4 formats.\n");
        return;
    }

    avctx->framerate.num = p_schro_params->format->frame_rate_numerator;
    avctx->framerate.den = p_schro_params->format->frame_rate_denominator;
}

static int libschroedinger_decode_frame(AVCodecContext *avctx,
                                        void *data, int *got_frame,
                                        AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    int64_t pts  = avpkt->pts;
    SchroTag *tag;

    SchroDecoderParams *p_schro_params = avctx->priv_data;
    SchroDecoder *decoder = p_schro_params->decoder;
    SchroBuffer *enc_buf;
    SchroFrame* frame;
    AVFrame *avframe = data;
    int state;
    int go = 1;
    int outer = 1;
    SchroParseUnitContext parse_ctx;
    LibSchroFrameContext *framewithpts = NULL;

    *got_frame = 0;

    parse_context_init(&parse_ctx, buf, buf_size);
    if (!buf_size) {
        if (!p_schro_params->eos_signalled) {
            state = schro_decoder_push_end_of_stream(decoder);
            p_schro_params->eos_signalled = 1;
        }
    }

    /* Loop through all the individual parse units in the input buffer */
    do {
        if ((enc_buf = find_next_parse_unit(&parse_ctx))) {
            /* Set Schrotag with the pts to be recovered after decoding*/
            enc_buf->tag = schro_tag_new(av_malloc(sizeof(int64_t)), av_free);
            if (!enc_buf->tag->value) {
                av_log(avctx, AV_LOG_ERROR, "Unable to allocate SchroTag\n");
                return AVERROR(ENOMEM);
            }
            AV_WN(64, enc_buf->tag->value, pts);
            /* Push buffer into decoder. */
            if (SCHRO_PARSE_CODE_IS_PICTURE(enc_buf->data[4]) &&
                SCHRO_PARSE_CODE_NUM_REFS(enc_buf->data[4]) > 0)
                avctx->has_b_frames = 1;
            state = schro_decoder_push(decoder, enc_buf);
            if (state == SCHRO_DECODER_FIRST_ACCESS_UNIT)
                libschroedinger_handle_first_access_unit(avctx);
            go = 1;
        } else
            outer = 0;

        while (go) {
            /* Parse data and process result. */
            state = schro_decoder_wait(decoder);
            switch (state) {
            case SCHRO_DECODER_FIRST_ACCESS_UNIT:
                libschroedinger_handle_first_access_unit(avctx);
                break;

            case SCHRO_DECODER_NEED_BITS:
                /* Need more input data - stop iterating over what we have. */
                go = 0;
                break;

            case SCHRO_DECODER_NEED_FRAME:
                /* Decoder needs a frame - create one and push it in. */
                frame = ff_create_schro_frame(avctx,
                                              p_schro_params->frame_format);
                if (!frame)
                    return AVERROR(ENOMEM);
                schro_decoder_add_output_picture(decoder, frame);
                break;

            case SCHRO_DECODER_OK:
                /* Pull a frame out of the decoder. */
                tag   = schro_decoder_get_picture_tag(decoder);
                frame = schro_decoder_pull(decoder);

                if (frame) {
                    /* Add relation between schroframe and pts. */
                    framewithpts = av_malloc(sizeof(LibSchroFrameContext));
                    if (!framewithpts) {
                        av_log(avctx, AV_LOG_ERROR, "Unable to allocate FrameWithPts\n");
                        return AVERROR(ENOMEM);
                    }
                    framewithpts->frame = frame;
                    framewithpts->pts   = AV_RN64(tag->value);
                    ff_schro_queue_push_back(&p_schro_params->dec_frame_queue,
                                             framewithpts);
                }
                break;
            case SCHRO_DECODER_EOS:
                go = 0;
                p_schro_params->eos_pulled = 1;
                schro_decoder_reset(decoder);
                outer = 0;
                break;

            case SCHRO_DECODER_ERROR:
                return -1;
                break;
            }
        }
    } while (outer);

    /* Grab next frame to be returned from the top of the queue. */
    framewithpts = ff_schro_queue_pop(&p_schro_params->dec_frame_queue);

    if (framewithpts && framewithpts->frame) {
        int ret;

        if ((ret = ff_get_buffer(avctx, avframe, 0)) < 0)
            return ret;

        memcpy(avframe->data[0],
               framewithpts->frame->components[0].data,
               framewithpts->frame->components[0].length);

        memcpy(avframe->data[1],
               framewithpts->frame->components[1].data,
               framewithpts->frame->components[1].length);

        memcpy(avframe->data[2],
               framewithpts->frame->components[2].data,
               framewithpts->frame->components[2].length);

        /* Fill frame with current buffer data from Schroedinger. */
        avframe->pkt_pts = framewithpts->pts;
        avframe->linesize[0] = framewithpts->frame->components[0].stride;
        avframe->linesize[1] = framewithpts->frame->components[1].stride;
        avframe->linesize[2] = framewithpts->frame->components[2].stride;

        *got_frame      = 1;

        /* Now free the frame resources. */
        libschroedinger_decode_frame_free(framewithpts->frame);
        av_free(framewithpts);
    } else {
        data       = NULL;
        *got_frame = 0;
    }
    return buf_size;
}


static av_cold int libschroedinger_decode_close(AVCodecContext *avctx)
{
    SchroDecoderParams *p_schro_params = avctx->priv_data;
    /* Free the decoder. */
    schro_decoder_free(p_schro_params->decoder);
    av_freep(&p_schro_params->format);

    /* Free data in the output frame queue. */
    ff_schro_queue_free(&p_schro_params->dec_frame_queue,
                        libschroedinger_decode_frame_free);

    return 0;
}

static void libschroedinger_flush(AVCodecContext *avctx)
{
    /* Got a seek request. Free the decoded frames queue and then reset
     * the decoder */
    SchroDecoderParams *p_schro_params = avctx->priv_data;

    /* Free data in the output frame queue. */
    ff_schro_queue_free(&p_schro_params->dec_frame_queue,
                        libschroedinger_decode_frame_free);

    ff_schro_queue_init(&p_schro_params->dec_frame_queue);
    schro_decoder_reset(p_schro_params->decoder);
    p_schro_params->eos_pulled = 0;
    p_schro_params->eos_signalled = 0;
}

AVCodec ff_libschroedinger_decoder = {
    .name           = "libschroedinger",
    .long_name      = NULL_IF_CONFIG_SMALL("libschroedinger Dirac 2.2"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_DIRAC,
    .priv_data_size = sizeof(SchroDecoderParams),
    .init           = libschroedinger_decode_init,
    .close          = libschroedinger_decode_close,
    .decode         = libschroedinger_decode_frame,
    .capabilities   = AV_CODEC_CAP_DELAY,
    .flush          = libschroedinger_flush,
};
