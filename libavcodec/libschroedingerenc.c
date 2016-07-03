/*
 * Dirac encoder support via Schroedinger libraries
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
* Dirac encoder support via libschroedinger-1.0 libraries. More details about
* the Schroedinger project can be found at http://www.diracvideo.org/.
* The library implements Dirac Specification Version 2.2
* (http://dirac.sourceforge.net/specification.html).
*/

#include <schroedinger/schro.h>
#include <schroedinger/schrodebug.h>
#include <schroedinger/schrovideoformat.h>

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "internal.h"
#include "libschroedinger.h"
#include "bytestream.h"


/** libschroedinger encoder private data */
typedef struct SchroEncoderParams {
    AVClass        *class;

    /** Schroedinger video format */
    SchroVideoFormat *format;

    /** Schroedinger frame format */
    SchroFrameFormat frame_format;

    /** frame size */
    int frame_size;

    /** Schroedinger encoder handle*/
    SchroEncoder* encoder;

    /** buffer to store encoder output before writing it to the frame queue*/
    unsigned char *enc_buf;

    /** Size of encoder buffer*/
    int enc_buf_size;

    /** queue storing encoded frames */
    FFSchroQueue enc_frame_queue;

    /** end of sequence signalled */
    int eos_signalled;

    /** end of sequence pulled */
    int eos_pulled;

    /* counter for frames submitted to encoder, used as dts */
    int64_t dts;

    /** enable noarith */
    int noarith;
} SchroEncoderParams;

/**
* Works out Schro-compatible chroma format.
*/
static int set_chroma_format(AVCodecContext *avctx)
{
    int num_formats = sizeof(schro_pixel_format_map) /
                      sizeof(schro_pixel_format_map[0]);
    int idx;

    SchroEncoderParams *p_schro_params = avctx->priv_data;

    for (idx = 0; idx < num_formats; ++idx) {
        if (schro_pixel_format_map[idx].ff_pix_fmt == avctx->pix_fmt) {
            p_schro_params->format->chroma_format =
                            schro_pixel_format_map[idx].schro_pix_fmt;
            return 0;
        }
    }

    av_log(avctx, AV_LOG_ERROR,
           "This codec currently only supports planar YUV 4:2:0, 4:2:2"
           " and 4:4:4 formats.\n");

    return -1;
}

static av_cold int libschroedinger_encode_init(AVCodecContext *avctx)
{
    SchroEncoderParams *p_schro_params = avctx->priv_data;
    SchroVideoFormatEnum preset;

    /* Initialize the libraries that libschroedinger depends on. */
    schro_init();

    /* Create an encoder object. */
    p_schro_params->encoder = schro_encoder_new();

    if (!p_schro_params->encoder) {
        av_log(avctx, AV_LOG_ERROR,
               "Unrecoverable Error: schro_encoder_new failed. ");
        return -1;
    }

    /* Initialize the format. */
    preset = ff_get_schro_video_format_preset(avctx);
    p_schro_params->format =
                    schro_encoder_get_video_format(p_schro_params->encoder);
    schro_video_format_set_std_video_format(p_schro_params->format, preset);
    p_schro_params->format->width  = avctx->width;
    p_schro_params->format->height = avctx->height;

    if (set_chroma_format(avctx) == -1)
        return -1;

    if (avctx->color_primaries == AVCOL_PRI_BT709) {
        p_schro_params->format->colour_primaries = SCHRO_COLOUR_PRIMARY_HDTV;
    } else if (avctx->color_primaries == AVCOL_PRI_BT470BG) {
        p_schro_params->format->colour_primaries = SCHRO_COLOUR_PRIMARY_SDTV_625;
    } else if (avctx->color_primaries == AVCOL_PRI_SMPTE170M) {
        p_schro_params->format->colour_primaries = SCHRO_COLOUR_PRIMARY_SDTV_525;
    }

    if (avctx->colorspace == AVCOL_SPC_BT709) {
        p_schro_params->format->colour_matrix = SCHRO_COLOUR_MATRIX_HDTV;
    } else if (avctx->colorspace == AVCOL_SPC_BT470BG) {
        p_schro_params->format->colour_matrix = SCHRO_COLOUR_MATRIX_SDTV;
    }

    if (avctx->color_trc == AVCOL_TRC_BT709) {
        p_schro_params->format->transfer_function = SCHRO_TRANSFER_CHAR_TV_GAMMA;
    }

    if (ff_get_schro_frame_format(p_schro_params->format->chroma_format,
                                  &p_schro_params->frame_format) == -1) {
        av_log(avctx, AV_LOG_ERROR,
               "This codec currently supports only planar YUV 4:2:0, 4:2:2"
               " and 4:4:4 formats.\n");
        return -1;
    }

    p_schro_params->format->frame_rate_numerator   = avctx->time_base.den;
    p_schro_params->format->frame_rate_denominator = avctx->time_base.num;

    p_schro_params->frame_size = av_image_get_buffer_size(avctx->pix_fmt,
                                                          avctx->width,
                                                          avctx->height, 1);

    if (!avctx->gop_size) {
        schro_encoder_setting_set_double(p_schro_params->encoder,
                                         "gop_structure",
                                         SCHRO_ENCODER_GOP_INTRA_ONLY);

#if FF_API_CODER_TYPE
FF_DISABLE_DEPRECATION_WARNINGS
        if (avctx->coder_type != FF_CODER_TYPE_VLC)
            p_schro_params->noarith = 0;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        schro_encoder_setting_set_double(p_schro_params->encoder,
                                         "enable_noarith",
                                         p_schro_params->noarith);
    } else {
        schro_encoder_setting_set_double(p_schro_params->encoder,
                                         "au_distance", avctx->gop_size);
        avctx->has_b_frames = 1;
        p_schro_params->dts = -1;
    }

    /* FIXME - Need to handle SCHRO_ENCODER_RATE_CONTROL_LOW_DELAY. */
    if (avctx->flags & AV_CODEC_FLAG_QSCALE) {
        if (!avctx->global_quality) {
            /* lossless coding */
            schro_encoder_setting_set_double(p_schro_params->encoder,
                                             "rate_control",
                                             SCHRO_ENCODER_RATE_CONTROL_LOSSLESS);
        } else {
            int quality;
            schro_encoder_setting_set_double(p_schro_params->encoder,
                                             "rate_control",
                                             SCHRO_ENCODER_RATE_CONTROL_CONSTANT_QUALITY);

            quality = avctx->global_quality / FF_QP2LAMBDA;
            if (quality > 10)
                quality = 10;
            schro_encoder_setting_set_double(p_schro_params->encoder,
                                             "quality", quality);
        }
    } else {
        schro_encoder_setting_set_double(p_schro_params->encoder,
                                         "rate_control",
                                         SCHRO_ENCODER_RATE_CONTROL_CONSTANT_BITRATE);

        schro_encoder_setting_set_double(p_schro_params->encoder,
                                         "bitrate", avctx->bit_rate);
    }

    if (avctx->flags & AV_CODEC_FLAG_INTERLACED_ME)
        /* All material can be coded as interlaced or progressive
           irrespective of the type of source material. */
        schro_encoder_setting_set_double(p_schro_params->encoder,
                                         "interlaced_coding", 1);

    schro_encoder_setting_set_double(p_schro_params->encoder, "open_gop",
                                     !(avctx->flags & AV_CODEC_FLAG_CLOSED_GOP));

    /* FIXME: Signal range hardcoded to 8-bit data until both libschroedinger
     * and libdirac support other bit-depth data. */
    schro_video_format_set_std_signal_range(p_schro_params->format,
                                            SCHRO_SIGNAL_RANGE_8BIT_VIDEO);

    /* Set the encoder format. */
    schro_encoder_set_video_format(p_schro_params->encoder,
                                   p_schro_params->format);

    /* Set the debug level. */
    schro_debug_set_level(avctx->debug);

    schro_encoder_start(p_schro_params->encoder);

    /* Initialize the encoded frame queue. */
    ff_schro_queue_init(&p_schro_params->enc_frame_queue);
    return 0;
}

static SchroFrame *libschroedinger_frame_from_data(AVCodecContext *avctx,
                                                   const AVFrame *frame)
{
    SchroEncoderParams *p_schro_params = avctx->priv_data;
    SchroFrame *in_frame = ff_create_schro_frame(avctx,
                                                 p_schro_params->frame_format);

    if (in_frame) {
        /* Copy input data to SchroFrame buffers (they match the ones
         * referenced by the AVFrame stored in priv) */
        if (av_frame_copy(in_frame->priv, frame) < 0) {
            av_log(avctx, AV_LOG_ERROR, "Failed to copy input data\n");
            return NULL;
        }
    }

    return in_frame;
}

static void libschroedinger_free_frame(void *data)
{
    FFSchroEncodedFrame *enc_frame = data;

    av_freep(&enc_frame->p_encbuf);
    av_free(enc_frame);
}

static int libschroedinger_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                                        const AVFrame *frame, int *got_packet)
{
    int enc_size = 0;
    SchroEncoderParams *p_schro_params = avctx->priv_data;
    SchroEncoder *encoder = p_schro_params->encoder;
    struct FFSchroEncodedFrame *p_frame_output = NULL;
    int go = 1;
    SchroBuffer *enc_buf;
    int presentation_frame;
    int parse_code;
    int last_frame_in_sequence = 0;
    int pkt_size, ret;

    if (!frame) {
        /* Push end of sequence if not already signalled. */
        if (!p_schro_params->eos_signalled) {
            schro_encoder_end_of_stream(encoder);
            p_schro_params->eos_signalled = 1;
        }
    } else {
        /* Allocate frame data to schro input buffer. */
        SchroFrame *in_frame = libschroedinger_frame_from_data(avctx, frame);
        if (!in_frame)
            return AVERROR(ENOMEM);
        /* Load next frame. */
        schro_encoder_push_frame(encoder, in_frame);
    }

    if (p_schro_params->eos_pulled)
        go = 0;

    /* Now check to see if we have any output from the encoder. */
    while (go) {
        int err;
        SchroStateEnum state;
        state = schro_encoder_wait(encoder);
        switch (state) {
        case SCHRO_STATE_HAVE_BUFFER:
        case SCHRO_STATE_END_OF_STREAM:
            enc_buf = schro_encoder_pull(encoder, &presentation_frame);
            if (enc_buf->length <= 0)
                return AVERROR_BUG;
            parse_code = enc_buf->data[4];

            /* All non-frame data is prepended to actual frame data to
             * be able to set the pts correctly. So we don't write data
             * to the frame output queue until we actually have a frame
             */
            if ((err = av_reallocp(&p_schro_params->enc_buf,
                                   p_schro_params->enc_buf_size +
                                   enc_buf->length)) < 0) {
                p_schro_params->enc_buf_size = 0;
                return err;
            }

            memcpy(p_schro_params->enc_buf + p_schro_params->enc_buf_size,
                   enc_buf->data, enc_buf->length);
            p_schro_params->enc_buf_size += enc_buf->length;


            if (state == SCHRO_STATE_END_OF_STREAM) {
                p_schro_params->eos_pulled = 1;
                go = 0;
            }

            if (!SCHRO_PARSE_CODE_IS_PICTURE(parse_code)) {
                schro_buffer_unref(enc_buf);
                break;
            }

            /* Create output frame. */
            p_frame_output = av_mallocz(sizeof(FFSchroEncodedFrame));
            if (!p_frame_output)
                return AVERROR(ENOMEM);
            /* Set output data. */
            p_frame_output->size     = p_schro_params->enc_buf_size;
            p_frame_output->p_encbuf = p_schro_params->enc_buf;
            if (SCHRO_PARSE_CODE_IS_INTRA(parse_code) &&
                SCHRO_PARSE_CODE_IS_REFERENCE(parse_code))
                p_frame_output->key_frame = 1;

            /* Parse the coded frame number from the bitstream. Bytes 14
             * through 17 represent the frame number. */
            p_frame_output->frame_num = AV_RB32(enc_buf->data + 13);

            ff_schro_queue_push_back(&p_schro_params->enc_frame_queue,
                                     p_frame_output);
            p_schro_params->enc_buf_size = 0;
            p_schro_params->enc_buf      = NULL;

            schro_buffer_unref(enc_buf);

            break;

        case SCHRO_STATE_NEED_FRAME:
            go = 0;
            break;

        case SCHRO_STATE_AGAIN:
            break;

        default:
            av_log(avctx, AV_LOG_ERROR, "Unknown Schro Encoder state\n");
            return -1;
        }
    }

    /* Copy 'next' frame in queue. */

    if (p_schro_params->enc_frame_queue.size == 1 &&
        p_schro_params->eos_pulled)
        last_frame_in_sequence = 1;

    p_frame_output = ff_schro_queue_pop(&p_schro_params->enc_frame_queue);

    if (!p_frame_output)
        return 0;

    pkt_size = p_frame_output->size;
    if (last_frame_in_sequence && p_schro_params->enc_buf_size > 0)
        pkt_size += p_schro_params->enc_buf_size;
    if ((ret = ff_alloc_packet2(avctx, pkt, pkt_size, 0)) < 0)
        goto error;

    memcpy(pkt->data, p_frame_output->p_encbuf, p_frame_output->size);
#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    avctx->coded_frame->key_frame = p_frame_output->key_frame;
    avctx->coded_frame->pts = p_frame_output->frame_num;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    /* Use the frame number of the encoded frame as the pts. It is OK to
     * do so since Dirac is a constant frame rate codec. It expects input
     * to be of constant frame rate. */
    pkt->pts = p_frame_output->frame_num;
    pkt->dts = p_schro_params->dts++;
    enc_size = p_frame_output->size;

    /* Append the end of sequence information to the last frame in the
     * sequence. */
    if (last_frame_in_sequence && p_schro_params->enc_buf_size > 0) {
        memcpy(pkt->data + enc_size, p_schro_params->enc_buf,
               p_schro_params->enc_buf_size);
        enc_size += p_schro_params->enc_buf_size;
        av_freep(&p_schro_params->enc_buf);
        p_schro_params->enc_buf_size = 0;
    }

    if (p_frame_output->key_frame)
        pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

error:
    /* free frame */
    libschroedinger_free_frame(p_frame_output);
    return ret;
}


static int libschroedinger_encode_close(AVCodecContext *avctx)
{
    SchroEncoderParams *p_schro_params = avctx->priv_data;

    /* Close the encoder. */
    schro_encoder_free(p_schro_params->encoder);

    /* Free data in the output frame queue. */
    ff_schro_queue_free(&p_schro_params->enc_frame_queue,
                        libschroedinger_free_frame);


    /* Free the encoder buffer. */
    if (p_schro_params->enc_buf_size)
        av_freep(&p_schro_params->enc_buf);

    /* Free the video format structure. */
    av_freep(&p_schro_params->format);

    return 0;
}

#define OFFSET(x) offsetof(SchroEncoderParams, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "noarith", "Enable noarith", OFFSET(noarith), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 1, VE },

    { NULL },
};

static const AVClass libschroedinger_class = {
    .class_name = "libschroedinger",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_libschroedinger_encoder = {
    .name           = "libschroedinger",
    .long_name      = NULL_IF_CONFIG_SMALL("libschroedinger Dirac 2.2"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_DIRAC,
    .priv_data_size = sizeof(SchroEncoderParams),
    .priv_class     = &libschroedinger_class,
    .init           = libschroedinger_encode_init,
    .encode2        = libschroedinger_encode_frame,
    .close          = libschroedinger_encode_close,
    .capabilities   = AV_CODEC_CAP_DELAY,
    .pix_fmts       = (const enum AVPixelFormat[]){
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_NONE
    },
};
