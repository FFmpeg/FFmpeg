/*
 * Copyright (c) 2006 Paul Richards <paul.richards@gmail.com>
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

/*!
 * \file theoraenc.c
 * \brief Theora encoder using libtheora.
 * \author Paul Richards <paul.richards@gmail.com>
 *
 * A lot of this is copy / paste from other output codecs in
 * libavcodec or pure guesswork (or both).
 *
 * I have used t_ prefixes on variables which are libtheora types
 * and o_ prefixes on variables which are libogg types.
 */

/* FFmpeg includes */
#include "avcodec.h"
#include "log.h"

/* libtheora includes */
#include <theora/theora.h>

typedef struct TheoraContext{
    theora_state t_state;
} TheoraContext;

/*!
    Concatenates an ogg_packet into the extradata.
*/
static int concatenate_packet(unsigned int* offset, AVCodecContext* avc_context, const ogg_packet* packet)
{
    char* message = NULL;
    uint8_t* newdata = NULL;
    int newsize = avc_context->extradata_size + 2 + packet->bytes;

    if (packet->bytes < 0) {
        message = "ogg_packet has negative size";
    } else if (packet->bytes > 0xffff) {
        message = "ogg_packet is larger than 65535 bytes";
    } else if (newsize < avc_context->extradata_size) {
        message = "extradata_size would overflow";
    } else {
        newdata = av_realloc(avc_context->extradata, newsize);
        if (newdata == NULL) {
            message = "av_realloc failed";
        }
    }
    if (message != NULL) {
        av_log(avc_context, AV_LOG_ERROR, "concatenate_packet failed: %s\n", message);
        return -1;
    }

    avc_context->extradata = newdata;
    avc_context->extradata_size = newsize;
    AV_WB16(avc_context->extradata + (*offset), packet->bytes);
    *offset += 2;
    memcpy( avc_context->extradata + (*offset), packet->packet, packet->bytes );
    (*offset) += packet->bytes;
    return 0;
}

static int encode_init(AVCodecContext* avc_context)
{
    theora_info t_info;
    theora_comment t_comment;
    ogg_packet o_packet;
    unsigned int offset;
    TheoraContext *h = avc_context->priv_data;

    /* Set up the theora_info struct */
    theora_info_init( &t_info );
    t_info.width = avc_context->width;
    t_info.height = avc_context->height;
    t_info.frame_width = avc_context->width;
    t_info.frame_height = avc_context->height;
    t_info.offset_x = 0;
    t_info.offset_y = 0;
    /* Swap numerator and denominator as time_base in AVCodecContext gives the
     * time period between frames, but theora_info needs the framerate.  */
    t_info.fps_numerator = avc_context->time_base.den;
    t_info.fps_denominator = avc_context->time_base.num;
    if (avc_context->sample_aspect_ratio.num != 0) {
        t_info.aspect_numerator = avc_context->sample_aspect_ratio.num;
        t_info.aspect_denominator = avc_context->sample_aspect_ratio.den;
    } else {
        t_info.aspect_numerator = 1;
        t_info.aspect_denominator = 1;
    }
    t_info.colorspace = OC_CS_UNSPECIFIED;
    t_info.pixelformat = OC_PF_420;
    t_info.target_bitrate = avc_context->bit_rate;
    t_info.keyframe_frequency = avc_context->gop_size;
    t_info.keyframe_frequency_force = avc_context->gop_size;
    t_info.keyframe_mindistance = avc_context->keyint_min;
    t_info.quality = 0;

    t_info.quick_p = 1;
    t_info.dropframes_p = 0;
    t_info.keyframe_auto_p = 1;
    t_info.keyframe_data_target_bitrate = t_info.target_bitrate * 1.5;
    t_info.keyframe_auto_threshold = 80;
    t_info.noise_sensitivity = 1;
    t_info.sharpness = 0;

    /* Now initialise libtheora */
    if (theora_encode_init( &(h->t_state), &t_info ) != 0) {
        av_log(avc_context, AV_LOG_ERROR, "theora_encode_init failed\n");
        return -1;
    }

    /* Clear up theora_info struct */
    theora_info_clear( &t_info );

    /*
        Output first header packet consisting of theora
        header, comment, and tables.

        Each one is prefixed with a 16bit size, then they
        are concatenated together into ffmpeg's extradata.
    */
    offset = 0;

    /* Header */
    theora_encode_header( &(h->t_state), &o_packet );
    if (concatenate_packet( &offset, avc_context, &o_packet ) != 0) {
        return -1;
    }

    /* Comment */
    theora_comment_init( &t_comment );
    theora_encode_comment( &t_comment, &o_packet );
    if (concatenate_packet( &offset, avc_context, &o_packet ) != 0) {
        return -1;
    }

    /* Tables */
    theora_encode_tables( &(h->t_state), &o_packet );
    if (concatenate_packet( &offset, avc_context, &o_packet ) != 0) {
        return -1;
    }

    /* Clear up theora_comment struct */
    theora_comment_clear( &t_comment );

    /* Set up the output AVFrame */
    avc_context->coded_frame= avcodec_alloc_frame();

    return 0;
}

static int encode_frame(
    AVCodecContext* avc_context,
    uint8_t *outbuf,
    int buf_size,
    void *data)
{
    yuv_buffer t_yuv_buffer;
    TheoraContext *h = avc_context->priv_data;
    AVFrame *frame = data;
    ogg_packet o_packet;
    int result;

    assert(avc_context->pix_fmt == PIX_FMT_YUV420P);

    /* Copy planes to the theora yuv_buffer */
    if (frame->linesize[1] != frame->linesize[2]) {
        av_log(avc_context, AV_LOG_ERROR, "U and V stride differ\n");
        return -1;
    }

    t_yuv_buffer.y_width = avc_context->width;
    t_yuv_buffer.y_height = avc_context->height;
    t_yuv_buffer.y_stride = frame->linesize[0];
    t_yuv_buffer.uv_width = t_yuv_buffer.y_width / 2;
    t_yuv_buffer.uv_height = t_yuv_buffer.y_height / 2;
    t_yuv_buffer.uv_stride = frame->linesize[1];

    t_yuv_buffer.y = frame->data[0];
    t_yuv_buffer.u = frame->data[1];
    t_yuv_buffer.v = frame->data[2];

    /* Now call into theora_encode_YUVin */
    result = theora_encode_YUVin( &(h->t_state), &t_yuv_buffer );
    if (result != 0) {
        const char* message;
        switch (result) {
            case -1:
                message = "differing frame sizes";
                break;
            case OC_EINVAL:
                message = "encoder is not ready or is finished";
                break;
            default:
                message = "unknown reason";
                break;
        }
        av_log(avc_context, AV_LOG_ERROR, "theora_encode_YUVin failed (%s) [%d]\n", message, result);
        return -1;
    }

    /* Pick up returned ogg_packet */
    result = theora_encode_packetout( &(h->t_state), 0, &o_packet );
    switch (result) {
        case 0:
            /* No packet is ready */
            return 0;
        case 1:
            /* Success, we have a packet */
            break;
        default:
            av_log(avc_context, AV_LOG_ERROR, "theora_encode_packetout failed [%d]\n", result);
            return -1;
    }

    /* Copy ogg_packet content out to buffer */
    if (buf_size < o_packet.bytes) {
        av_log(avc_context, AV_LOG_ERROR, "encoded frame too large\n");
        return -1;
    }
    memcpy(outbuf, o_packet.packet, o_packet.bytes);

    return o_packet.bytes;
}

static int encode_close(AVCodecContext* avc_context)
{
    ogg_packet o_packet;
    TheoraContext *h = avc_context->priv_data;
    int result;
    const char* message;

    result = theora_encode_packetout( &(h->t_state), 1, &o_packet );
    theora_clear( &(h->t_state) );
    switch (result) {
        case 0:/* No packet is ready */
        case -1:/* Encoding finished */
            return 0;
        case 1:
            /* We have a packet */
            message = "gave us a packet";
            break;
        default:
            message = "unknown reason";
            break;
    }
    av_log(avc_context, AV_LOG_ERROR, "theora_encode_packetout failed (%s) [%d]\n", message, result);
    return -1;
}

static const enum PixelFormat supported_pixel_formats[] = { PIX_FMT_YUV420P, -1 };

/*! AVCodec struct exposed to libavcodec */
AVCodec libtheora_encoder =
{
    .name = "libtheora",
    .type = CODEC_TYPE_VIDEO,
    .id = CODEC_ID_THEORA,
    .priv_data_size = sizeof(TheoraContext),
    .init = encode_init,
    .close = encode_close,
    .encode = encode_frame,
    .pix_fmts = supported_pixel_formats,
};
