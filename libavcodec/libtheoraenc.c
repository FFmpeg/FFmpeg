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

/**
 * @file
 * @brief Theora encoder using libtheora.
 * @author Paul Richards <paul.richards@gmail.com>
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
#include "libavutil/base64.h"
#include "avcodec.h"

/* libtheora includes */
#include <theora/theoraenc.h>

typedef struct TheoraContext {
    th_enc_ctx *t_state;
    uint8_t    *stats;
    int         stats_size;
    int         stats_offset;
    int         uv_hshift;
    int         uv_vshift;
    int         keyframe_mask;
} TheoraContext;

/** Concatenate an ogg_packet into the extradata. */
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

static int get_stats(AVCodecContext *avctx, int eos)
{
#ifdef TH_ENCCTL_2PASS_OUT
    TheoraContext *h = avctx->priv_data;
    uint8_t *buf;
    int bytes;

    bytes = th_encode_ctl(h->t_state, TH_ENCCTL_2PASS_OUT, &buf, sizeof(buf));
    if (bytes < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error getting first pass stats\n");
        return -1;
    }
    if (!eos) {
        h->stats = av_fast_realloc(h->stats, &h->stats_size,
                                   h->stats_offset + bytes);
        memcpy(h->stats + h->stats_offset, buf, bytes);
        h->stats_offset += bytes;
    } else {
        int b64_size = AV_BASE64_SIZE(h->stats_offset);
        // libtheora generates a summary header at the end
        memcpy(h->stats, buf, bytes);
        avctx->stats_out = av_malloc(b64_size);
        av_base64_encode(avctx->stats_out, b64_size, h->stats, h->stats_offset);
    }
    return 0;
#else
    av_log(avctx, AV_LOG_ERROR, "libtheora too old to support 2pass\n");
    return -1;
#endif
}

// libtheora won't read the entire buffer we give it at once, so we have to
// repeatedly submit it...
static int submit_stats(AVCodecContext *avctx)
{
#ifdef TH_ENCCTL_2PASS_IN
    TheoraContext *h = avctx->priv_data;
    int bytes;
    if (!h->stats) {
        if (!avctx->stats_in) {
            av_log(avctx, AV_LOG_ERROR, "No statsfile for second pass\n");
            return -1;
        }
        h->stats_size = strlen(avctx->stats_in) * 3/4;
        h->stats      = av_malloc(h->stats_size);
        h->stats_size = av_base64_decode(h->stats, avctx->stats_in, h->stats_size);
    }
    while (h->stats_size - h->stats_offset > 0) {
        bytes = th_encode_ctl(h->t_state, TH_ENCCTL_2PASS_IN,
                              h->stats + h->stats_offset,
                              h->stats_size - h->stats_offset);
        if (bytes < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error submitting stats\n");
            return -1;
        }
        if (!bytes)
            return 0;
        h->stats_offset += bytes;
    }
    return 0;
#else
    av_log(avctx, AV_LOG_ERROR, "libtheora too old to support 2pass\n");
    return -1;
#endif
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

    if (avc_context->color_primaries == AVCOL_PRI_BT470M)
        t_info.colorspace = TH_CS_ITU_REC_470M;
    else if (avc_context->color_primaries == AVCOL_PRI_BT470BG)
        t_info.colorspace = TH_CS_ITU_REC_470BG;
    else
        t_info.colorspace = TH_CS_UNSPECIFIED;

    if (avc_context->pix_fmt == PIX_FMT_YUV420P)
        t_info.pixel_fmt = TH_PF_420;
    else if (avc_context->pix_fmt == PIX_FMT_YUV422P)
        t_info.pixel_fmt = TH_PF_422;
    else if (avc_context->pix_fmt == PIX_FMT_YUV444P)
        t_info.pixel_fmt = TH_PF_444;
    else {
        av_log(avc_context, AV_LOG_ERROR, "Unsupported pix_fmt\n");
        return -1;
    }
    avcodec_get_chroma_sub_sample(avc_context->pix_fmt, &h->uv_hshift, &h->uv_vshift);

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

    h->keyframe_mask = (1 << t_info.keyframe_granule_shift) - 1;
    /* Clear up theora_info struct */
    th_info_clear(&t_info);

    if (th_encode_ctl(h->t_state, TH_ENCCTL_SET_KEYFRAME_FREQUENCY_FORCE,
                      &gop_size, sizeof(gop_size))) {
        av_log(avc_context, AV_LOG_ERROR, "Error setting GOP size\n");
        return -1;
    }

    // need to enable 2 pass (via TH_ENCCTL_2PASS_) before encoding headers
    if (avc_context->flags & CODEC_FLAG_PASS1) {
        if (get_stats(avc_context, 0))
            return -1;
    } else if (avc_context->flags & CODEC_FLAG_PASS2) {
        if (submit_stats(avc_context))
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

    // EOS, finish and get 1st pass stats if applicable
    if (!frame) {
        th_encode_packetout(h->t_state, 1, &o_packet);
        if (avc_context->flags & CODEC_FLAG_PASS1)
            if (get_stats(avc_context, 1))
                return -1;
        return 0;
    }

    /* Copy planes to the theora yuv_buffer */
    for (i = 0; i < 3; i++) {
        t_yuv_buffer[i].width  = FFALIGN(avc_context->width,  16) >> (i && h->uv_hshift);
        t_yuv_buffer[i].height = FFALIGN(avc_context->height, 16) >> (i && h->uv_vshift);
        t_yuv_buffer[i].stride = frame->linesize[i];
        t_yuv_buffer[i].data   = frame->data[i];
    }

    if (avc_context->flags & CODEC_FLAG_PASS2)
        if (submit_stats(avc_context))
            return -1;

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

    if (avc_context->flags & CODEC_FLAG_PASS1)
        if (get_stats(avc_context, 0))
            return -1;

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

    // HACK: assumes no encoder delay, this is true until libtheora becomes
    // multithreaded (which will be disabled unless explictly requested)
    avc_context->coded_frame->pts = frame->pts;
    avc_context->coded_frame->key_frame = !(o_packet.granulepos & h->keyframe_mask);

    return o_packet.bytes;
}

static av_cold int encode_close(AVCodecContext* avc_context)
{
    TheoraContext *h = avc_context->priv_data;

    th_encode_free(h->t_state);
    av_freep(&h->stats);
    av_freep(&avc_context->coded_frame);
    av_freep(&avc_context->stats_out);
    av_freep(&avc_context->extradata);
    avc_context->extradata_size = 0;

    return 0;
}

/** AVCodec struct exposed to libavcodec */
AVCodec ff_libtheora_encoder = {
    .name = "libtheora",
    .type = AVMEDIA_TYPE_VIDEO,
    .id = CODEC_ID_THEORA,
    .priv_data_size = sizeof(TheoraContext),
    .init = encode_init,
    .close = encode_close,
    .encode = encode_frame,
    .capabilities = CODEC_CAP_DELAY, // needed to get the statsfile summary
    .pix_fmts= (const enum PixelFormat[]){PIX_FMT_YUV420P, PIX_FMT_YUV422P, PIX_FMT_YUV444P, PIX_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("libtheora Theora"),
};
