/*
 * generic encoding-related code
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/frame.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/samplefmt.h"

#include "avcodec.h"
#include "internal.h"

int ff_alloc_packet(AVPacket *avpkt, int size)
{
    if (size > INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE)
        return AVERROR(EINVAL);

    if (avpkt->data) {
        AVBufferRef *buf = avpkt->buf;

        if (avpkt->size < size)
            return AVERROR(EINVAL);

        av_init_packet(avpkt);
        avpkt->buf      = buf;
        avpkt->size     = size;
        return 0;
    } else {
        return av_new_packet(avpkt, size);
    }
}

/**
 * Pad last frame with silence.
 */
static int pad_last_frame(AVCodecContext *s, AVFrame **dst, const AVFrame *src)
{
    AVFrame *frame = NULL;
    int ret;

    if (!(frame = av_frame_alloc()))
        return AVERROR(ENOMEM);

    frame->format         = src->format;
    frame->channel_layout = src->channel_layout;
    frame->nb_samples     = s->frame_size;
    ret = av_frame_get_buffer(frame, 32);
    if (ret < 0)
        goto fail;

    ret = av_frame_copy_props(frame, src);
    if (ret < 0)
        goto fail;

    if ((ret = av_samples_copy(frame->extended_data, src->extended_data, 0, 0,
                               src->nb_samples, s->channels, s->sample_fmt)) < 0)
        goto fail;
    if ((ret = av_samples_set_silence(frame->extended_data, src->nb_samples,
                                      frame->nb_samples - src->nb_samples,
                                      s->channels, s->sample_fmt)) < 0)
        goto fail;

    *dst = frame;

    return 0;

fail:
    av_frame_free(&frame);
    return ret;
}

int attribute_align_arg avcodec_encode_audio2(AVCodecContext *avctx,
                                              AVPacket *avpkt,
                                              const AVFrame *frame,
                                              int *got_packet_ptr)
{
    AVFrame tmp;
    AVFrame *padded_frame = NULL;
    int ret;
    int user_packet = !!avpkt->data;

    *got_packet_ptr = 0;

    if (!avctx->codec->encode2) {
        av_log(avctx, AV_LOG_ERROR, "This encoder requires using the avcodec_send_frame() API.\n");
        return AVERROR(ENOSYS);
    }

    if (!(avctx->codec->capabilities & AV_CODEC_CAP_DELAY) && !frame) {
        av_packet_unref(avpkt);
        av_init_packet(avpkt);
        return 0;
    }

    /* ensure that extended_data is properly set */
    if (frame && !frame->extended_data) {
        if (av_sample_fmt_is_planar(avctx->sample_fmt) &&
            avctx->channels > AV_NUM_DATA_POINTERS) {
            av_log(avctx, AV_LOG_ERROR, "Encoding to a planar sample format, "
                                        "with more than %d channels, but extended_data is not set.\n",
                   AV_NUM_DATA_POINTERS);
            return AVERROR(EINVAL);
        }
        av_log(avctx, AV_LOG_WARNING, "extended_data is not set.\n");

        tmp = *frame;
        tmp.extended_data = tmp.data;
        frame = &tmp;
    }

    /* extract audio service type metadata */
    if (frame) {
        AVFrameSideData *sd = av_frame_get_side_data(frame, AV_FRAME_DATA_AUDIO_SERVICE_TYPE);
        if (sd && sd->size >= sizeof(enum AVAudioServiceType))
            avctx->audio_service_type = *(enum AVAudioServiceType*)sd->data;
    }

    /* check for valid frame size */
    if (frame) {
        if (avctx->codec->capabilities & AV_CODEC_CAP_SMALL_LAST_FRAME) {
            if (frame->nb_samples > avctx->frame_size)
                return AVERROR(EINVAL);
        } else if (!(avctx->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)) {
            if (frame->nb_samples < avctx->frame_size &&
                !avctx->internal->last_audio_frame) {
                ret = pad_last_frame(avctx, &padded_frame, frame);
                if (ret < 0)
                    return ret;

                frame = padded_frame;
                avctx->internal->last_audio_frame = 1;
            }

            if (frame->nb_samples != avctx->frame_size) {
                ret = AVERROR(EINVAL);
                goto end;
            }
        }
    }

    ret = avctx->codec->encode2(avctx, avpkt, frame, got_packet_ptr);
    if (!ret) {
        if (*got_packet_ptr) {
            if (!(avctx->codec->capabilities & AV_CODEC_CAP_DELAY)) {
                if (avpkt->pts == AV_NOPTS_VALUE)
                    avpkt->pts = frame->pts;
                if (!avpkt->duration)
                    avpkt->duration = ff_samples_to_time_base(avctx,
                                                              frame->nb_samples);
            }
            avpkt->dts = avpkt->pts;
        } else {
            avpkt->size = 0;
        }

        if (!user_packet && avpkt->size) {
            ret = av_buffer_realloc(&avpkt->buf, avpkt->size);
            if (ret >= 0)
                avpkt->data = avpkt->buf->data;
        }

        avctx->frame_number++;
    }

    if (ret < 0 || !*got_packet_ptr) {
        av_packet_unref(avpkt);
        av_init_packet(avpkt);
        goto end;
    }

    /* NOTE: if we add any audio encoders which output non-keyframe packets,
     *       this needs to be moved to the encoders, but for now we can do it
     *       here to simplify things */
    avpkt->flags |= AV_PKT_FLAG_KEY;

end:
    av_frame_free(&padded_frame);

    return ret;
}

int attribute_align_arg avcodec_encode_video2(AVCodecContext *avctx,
                                              AVPacket *avpkt,
                                              const AVFrame *frame,
                                              int *got_packet_ptr)
{
    int ret;
    int user_packet = !!avpkt->data;

    *got_packet_ptr = 0;

    if (!avctx->codec->encode2) {
        av_log(avctx, AV_LOG_ERROR, "This encoder requires using the avcodec_send_frame() API.\n");
        return AVERROR(ENOSYS);
    }

    if (!(avctx->codec->capabilities & AV_CODEC_CAP_DELAY) && !frame) {
        av_packet_unref(avpkt);
        av_init_packet(avpkt);
        avpkt->size = 0;
        return 0;
    }

    if (av_image_check_size(avctx->width, avctx->height, 0, avctx))
        return AVERROR(EINVAL);

    av_assert0(avctx->codec->encode2);

    ret = avctx->codec->encode2(avctx, avpkt, frame, got_packet_ptr);
    if (!ret) {
        if (!*got_packet_ptr)
            avpkt->size = 0;
        else if (!(avctx->codec->capabilities & AV_CODEC_CAP_DELAY))
            avpkt->pts = avpkt->dts = frame->pts;

        if (!user_packet && avpkt->size) {
            ret = av_buffer_realloc(&avpkt->buf, avpkt->size);
            if (ret >= 0)
                avpkt->data = avpkt->buf->data;
        }

        avctx->frame_number++;
    }

    if (ret < 0 || !*got_packet_ptr)
        av_packet_unref(avpkt);

    emms_c();
    return ret;
}

int avcodec_encode_subtitle(AVCodecContext *avctx, uint8_t *buf, int buf_size,
                            const AVSubtitle *sub)
{
    int ret;
    if (sub->start_display_time) {
        av_log(avctx, AV_LOG_ERROR, "start_display_time must be 0.\n");
        return -1;
    }
    if (sub->num_rects == 0 || !sub->rects)
        return -1;
    ret = avctx->codec->encode_sub(avctx, buf, buf_size, sub);
    avctx->frame_number++;
    return ret;
}

static int do_encode(AVCodecContext *avctx, const AVFrame *frame, int *got_packet)
{
    int ret;
    *got_packet = 0;

    av_packet_unref(avctx->internal->buffer_pkt);
    avctx->internal->buffer_pkt_valid = 0;

    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        ret = avcodec_encode_video2(avctx, avctx->internal->buffer_pkt,
                                    frame, got_packet);
    } else if (avctx->codec_type == AVMEDIA_TYPE_AUDIO) {
        ret = avcodec_encode_audio2(avctx, avctx->internal->buffer_pkt,
                                    frame, got_packet);
    } else {
        ret = AVERROR(EINVAL);
    }

    if (ret >= 0 && *got_packet) {
        // Encoders must always return ref-counted buffers.
        // Side-data only packets have no data and can be not ref-counted.
        av_assert0(!avctx->internal->buffer_pkt->data || avctx->internal->buffer_pkt->buf);
        avctx->internal->buffer_pkt_valid = 1;
        ret = 0;
    } else {
        av_packet_unref(avctx->internal->buffer_pkt);
    }

    return ret;
}

int attribute_align_arg avcodec_send_frame(AVCodecContext *avctx, const AVFrame *frame)
{
    if (!avcodec_is_open(avctx) || !av_codec_is_encoder(avctx->codec))
        return AVERROR(EINVAL);

    if (avctx->internal->draining)
        return AVERROR_EOF;

    if (!frame) {
        avctx->internal->draining = 1;

        if (!(avctx->codec->capabilities & AV_CODEC_CAP_DELAY))
            return 0;
    }

    if (avctx->codec->send_frame)
        return avctx->codec->send_frame(avctx, frame);

    // Emulation via old API. Do it here instead of avcodec_receive_packet, because:
    // 1. if the AVFrame is not refcounted, the copying will be much more
    //    expensive than copying the packet data
    // 2. assume few users use non-refcounted AVPackets, so usually no copy is
    //    needed

    if (avctx->internal->buffer_pkt_valid)
        return AVERROR(EAGAIN);

    return do_encode(avctx, frame, &(int){0});
}

int attribute_align_arg avcodec_receive_packet(AVCodecContext *avctx, AVPacket *avpkt)
{
    av_packet_unref(avpkt);

    if (!avcodec_is_open(avctx) || !av_codec_is_encoder(avctx->codec))
        return AVERROR(EINVAL);

    if (avctx->codec->receive_packet) {
        if (avctx->internal->draining && !(avctx->codec->capabilities & AV_CODEC_CAP_DELAY))
            return AVERROR_EOF;
        return avctx->codec->receive_packet(avctx, avpkt);
    }

    // Emulation via old API.

    if (!avctx->internal->buffer_pkt_valid) {
        int got_packet;
        int ret;
        if (!avctx->internal->draining)
            return AVERROR(EAGAIN);
        ret = do_encode(avctx, NULL, &got_packet);
        if (ret < 0)
            return ret;
        if (ret >= 0 && !got_packet)
            return AVERROR_EOF;
    }

    av_packet_move_ref(avpkt, avctx->internal->buffer_pkt);
    avctx->internal->buffer_pkt_valid = 0;
    return 0;
}
