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
 * \file libtheoraenc.c
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
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "avcodec.h"

/* libtheora includes */
#include <theora/theoraenc.h>

typedef struct TheoraContext {
    th_enc_ctx *t_state;
} TheoraContext;

/*!
    Concatenates an ogg_packet into the extradata.
*/
static int concatenate_packet(unsigned int* offset,
                              AVCodecContext* avc_context,
                              const ogg_packet* packet)
{
    const char* message = NULL;
    uint8_t* newdata    = NULL;
    int newsize = avc_context->extradata_size + 2 + packet->bytes;

    if (packet->bytes < 0) {
        message = "ogg_packet has negative size";
    } else if (packet->bytes > 0xffff) {
        message = "ogg_packet is larger than 65535 bytes";
    } else if (newsize < avc_context->extradata_size) {
        message = "extradata_size would overflow";
    } else {
        newdata = av_realloc(avc_context->extradata, newsize);
        if (!newdata)
            message = "av_realloc failed";
    }
    if (message) {
        av_log(avc_context, AV_LOG_ERROR, "concatenate_packet failed: %s\n", message);
        return -1;
    }

    avc_context->extradata      = newdata;
    avc_context->extradata_size = newsize;
    AV_WB16(avc_context->extradata + (*offset), packet->bytes);
    *offset += 2;
    memcpy(avc_context->extradata + (*offset), packet->packet, packet->bytes);
    (*offset) += packet->bytes;
    return 0;
}

static av_cold int encode_init(AVCodecContext* avc_context)
{
    th_info t_info;
    th_comment t_comment;
    ogg_packet o_packet;
    unsigned int offset;
    TheoraContext *h = avc_context->priv_data;
    uint32_t gop_size = avc_context->gop_size;

    /* Set up the theora_info struct */
    th_info_init(&t_info);
    t_info.frame_width  = FFALIGN(avc_context->width,  16);
    t_info.frame_height = FFALIGN(avc_context->height, 16);
    t_info.pic_width    = avc_context->width;
    t_info.pic_height   = avc_context->height;
    t_info.pic_x        = 0;
    t_info.pic_y        = 0;
    /* Swap numerator and denominator as time_base in AVCodecContext gives the
     * time period between frames, but theora_info needs the framerate.  */
    t_info.fps_numerator   = avc_context->time_base.den;
    t_info.fps_denominator = avc_context->time_base.num;
    if (avc_context->sample_aspect_ratio.num) {
        t_info.aspect_numerator   = avc_context->sample_aspect_ratio.num;
        t_info.aspect_denominator = avc_context->sample_aspect_ratio.den;
    } else {
        t_info.aspect_numerator   = 1;
        t_info.aspect_denominator = 1;
    }
    t_info.colorspace = TH_CS_UNSPECIFIED;
    t_info.pixel_fmt  = TH_PF_420;

    if (avc_context->flags & CODEC_FLAG_QSCALE) {
        /* to be constant with the libvorbis implementation, clip global_quality to 0 - 10
           Theora accepts a quality parameter p, which is:
                * 0 <= p <=63
                * an int value
         */
        t_info.quality        = av_clip(avc_context->global_quality / (float)FF_QP2LAMBDA, 0, 10) * 6.3;
        t_info.target_bitrate = 0;
    } else {
        t_info.target_bitrate = avc_context->bit_rate;
        t_info.quality        = 0;
    }

    /* Now initialise libtheora */
    h->t_state = th_encode_alloc(&t_info);
    if (!h->t_state) {
        av_log(avc_context, AV_LOG_ERROR, "theora_encode_init failed\n");
        return -1;
    }

    /* Clear up theora_info struct */
    th_info_clear(&t_info);

    if (th_encode_ctl(h->t_state, TH_ENCCTL_SET_KEYFRAME_FREQUENCY_FORCE,
                      &gop_size, sizeof(gop_size))) {
        av_log(avc_context, AV_LOG_ERROR, "Error setting GOP size\n");
        return -1;
    }

    /*
        Output first header packet consisting of theora
        header, comment, and tables.

        Each one is prefixed with a 16bit size, then they
        are concatenated together into ffmpeg's extradata.
    */
    offset = 0;

    /* Headers */
    th_comment_init(&t_comment);

    while (th_encode_flushheader(h->t_state, &t_comment, &o_packet))
        if (concatenate_packet(&offset, avc_context, &o_packet))
            return -1;

    th_comment_clear(&t_comment);

    /* Set up the output AVFrame */
    avc_context->coded_frame= avcodec_alloc_frame();

    return 0;
}

static int encode_frame(AVCodecContext* avc_context, uint8_t *outbuf,
                        int buf_size, void *data)
{
    th_ycbcr_buffer t_yuv_buffer;
    TheoraContext *h = avc_context->priv_data;
    AVFrame *frame = data;
    ogg_packet o_packet;
    int result, i;

    assert(avc_context->pix_fmt == PIX_FMT_YUV420P);

    /* Copy planes to the theora yuv_buffer */
    for (i = 0; i < 3; i++) {
        t_yuv_buffer[i].width  = FFALIGN(avc_context->width,  16) >> !!i;
        t_yuv_buffer[i].height = FFALIGN(avc_context->height, 16) >> !!i;
        t_yuv_buffer[i].stride = frame->linesize[i];
        t_yuv_buffer[i].data   = frame->data[i];
    }

    /* Now call into theora_encode_YUVin */
    result = th_encode_ycbcr_in(h->t_state, t_yuv_buffer);
    if (result) {
        const char* message;
        switch (result) {
        case -1:
            message = "differing frame sizes";
            break;
        case TH_EINVAL:
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
    result = th_encode_packetout(h->t_state, 0, &o_packet);
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

    // HACK: does not take codec delay into account (neither does the decoder though)
    avc_context->coded_frame->pts = frame->pts;

    return o_packet.bytes;
}

static av_cold int encode_close(AVCodecContext* avc_context)
{
    ogg_packet o_packet;
    TheoraContext *h = avc_context->priv_data;
    int result;
    const char* message;

    result = th_encode_packetout(h->t_state, 1, &o_packet);
    th_encode_free(h->t_state);
    av_freep(&avc_context->coded_frame);
    av_freep(&avc_context->extradata);
    avc_context->extradata_size = 0;

    switch (result) {
    case 0:  /* No packet is ready */
    case -1: /* Encoding finished */
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

static const enum PixelFormat supported_pixel_formats[] = { PIX_FMT_YUV420P, PIX_FMT_NONE };

/*! AVCodec struct exposed to libavcodec */
AVCodec libtheora_encoder = {
    .name = "libtheora",
    .type = CODEC_TYPE_VIDEO,
    .id = CODEC_ID_THEORA,
    .priv_data_size = sizeof(TheoraContext),
    .init = encode_init,
    .close = encode_close,
    .encode = encode_frame,
    .pix_fmts = supported_pixel_formats,
    .long_name = NULL_IF_CONFIG_SMALL("libtheora Theora"),
};
