/*
 * generic encoding-related code
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

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/frame.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/samplefmt.h"

#include "avcodec.h"
#include "encode.h"
#include "frame_thread_encoder.h"
#include "internal.h"

int ff_alloc_packet2(AVCodecContext *avctx, AVPacket *avpkt, int64_t size, int64_t min_size)
{
    if (size < 0 || size > INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "Invalid minimum required packet size %"PRId64" (max allowed is %d)\n",
               size, INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE);
        return AVERROR(EINVAL);
    }

    av_assert0(!avpkt->data);

    if (avctx && 2*min_size < size) { // FIXME The factor needs to be finetuned
        av_fast_padded_malloc(&avctx->internal->byte_buffer, &avctx->internal->byte_buffer_size, size);
        avpkt->data = avctx->internal->byte_buffer;
        avpkt->size = size;
    }

    if (!avpkt->data) {
        int ret = av_new_packet(avpkt, size);
        if (ret < 0)
            av_log(avctx, AV_LOG_ERROR, "Failed to allocate packet of size %"PRId64"\n", size);
        return ret;
    }

    return 0;
}

/**
 * Pad last frame with silence.
 */
static int pad_last_frame(AVCodecContext *s, AVFrame *frame, const AVFrame *src)
{
    int ret;

    frame->format         = src->format;
    frame->channel_layout = src->channel_layout;
    frame->channels       = src->channels;
    frame->nb_samples     = s->frame_size;
    ret = av_frame_get_buffer(frame, 0);
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

    return 0;

fail:
    av_frame_unref(frame);
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

    ret = avctx->codec->encode_sub(avctx, buf, buf_size, sub);
    avctx->frame_number++;
    return ret;
}

int ff_encode_get_frame(AVCodecContext *avctx, AVFrame *frame)
{
    AVCodecInternal *avci = avctx->internal;

    if (avci->draining)
        return AVERROR_EOF;

    if (!avci->buffer_frame->buf[0])
        return AVERROR(EAGAIN);

    av_frame_move_ref(frame, avci->buffer_frame);

    return 0;
}

static int encode_simple_internal(AVCodecContext *avctx, AVPacket *avpkt)
{
    AVCodecInternal   *avci = avctx->internal;
    EncodeSimpleContext *es = &avci->es;
    AVFrame          *frame = es->in_frame;
    int got_packet;
    int ret;

    if (avci->draining_done)
        return AVERROR_EOF;

    if (!frame->buf[0] && !avci->draining) {
        av_frame_unref(frame);
        ret = ff_encode_get_frame(avctx, frame);
        if (ret < 0 && ret != AVERROR_EOF)
            return ret;
    }

    if (!frame->buf[0]) {
        if (!(avctx->codec->capabilities & AV_CODEC_CAP_DELAY ||
              (avci->frame_thread_encoder && avctx->active_thread_type & FF_THREAD_FRAME)))
            return AVERROR_EOF;

        // Flushing is signaled with a NULL frame
        frame = NULL;
    }

    got_packet = 0;

    av_assert0(avctx->codec->encode2);

    if (CONFIG_FRAME_THREAD_ENCODER &&
        avci->frame_thread_encoder && (avctx->active_thread_type & FF_THREAD_FRAME))
        ret = ff_thread_video_encode_frame(avctx, avpkt, frame, &got_packet);
    else {
        ret = avctx->codec->encode2(avctx, avpkt, frame, &got_packet);
        if (avctx->codec->type == AVMEDIA_TYPE_VIDEO && !ret && got_packet &&
            !(avctx->codec->capabilities & AV_CODEC_CAP_DELAY))
            avpkt->pts = avpkt->dts = frame->pts;
    }

    av_assert0(ret <= 0);

    emms_c();

    if (!ret && got_packet) {
        if (avpkt->data) {
            ret = av_packet_make_refcounted(avpkt);
            if (ret < 0)
                goto end;
        }

        if (frame && !(avctx->codec->capabilities & AV_CODEC_CAP_DELAY)) {
            if (avctx->codec->type == AVMEDIA_TYPE_AUDIO) {
                if (avpkt->pts == AV_NOPTS_VALUE)
                    avpkt->pts = frame->pts;
                if (!avpkt->duration)
                    avpkt->duration = ff_samples_to_time_base(avctx,
                                                              frame->nb_samples);
            }
        }
        if (avctx->codec->type == AVMEDIA_TYPE_AUDIO) {
            /* NOTE: if we add any audio encoders which output non-keyframe packets,
             *       this needs to be moved to the encoders, but for now we can do it
             *       here to simplify things */
            avpkt->flags |= AV_PKT_FLAG_KEY;
            avpkt->dts = avpkt->pts;
        }
    }

    if (avci->draining && !got_packet)
        avci->draining_done = 1;

end:
    if (ret < 0 || !got_packet)
        av_packet_unref(avpkt);

    if (frame) {
        if (!ret)
            avctx->frame_number++;
        av_frame_unref(frame);
    }

    if (got_packet)
        // Encoders must always return ref-counted buffers.
        // Side-data only packets have no data and can be not ref-counted.
        av_assert0(!avpkt->data || avpkt->buf);

    return ret;
}

static int encode_simple_receive_packet(AVCodecContext *avctx, AVPacket *avpkt)
{
    int ret;

    while (!avpkt->data && !avpkt->side_data) {
        ret = encode_simple_internal(avctx, avpkt);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int encode_receive_packet_internal(AVCodecContext *avctx, AVPacket *avpkt)
{
    AVCodecInternal *avci = avctx->internal;
    int ret;

    if (avci->draining_done)
        return AVERROR_EOF;

    av_assert0(!avpkt->data && !avpkt->side_data);

    if (avctx->codec->type == AVMEDIA_TYPE_VIDEO) {
        if ((avctx->flags & AV_CODEC_FLAG_PASS1) && avctx->stats_out)
            avctx->stats_out[0] = '\0';
        if (av_image_check_size2(avctx->width, avctx->height, avctx->max_pixels, AV_PIX_FMT_NONE, 0, avctx))
            return AVERROR(EINVAL);
    }

    if (avctx->codec->receive_packet) {
        ret = avctx->codec->receive_packet(avctx, avpkt);
        if (ret < 0)
            av_packet_unref(avpkt);
        else
            // Encoders must always return ref-counted buffers.
            // Side-data only packets have no data and can be not ref-counted.
            av_assert0(!avpkt->data || avpkt->buf);
    } else
        ret = encode_simple_receive_packet(avctx, avpkt);

    if (ret == AVERROR_EOF)
        avci->draining_done = 1;

    return ret;
}

static int encode_send_frame_internal(AVCodecContext *avctx, const AVFrame *src)
{
    AVCodecInternal *avci = avctx->internal;
    AVFrame *dst = avci->buffer_frame;
    int ret;

    if (avctx->codec->type == AVMEDIA_TYPE_AUDIO) {
        /* extract audio service type metadata */
        AVFrameSideData *sd = av_frame_get_side_data(src, AV_FRAME_DATA_AUDIO_SERVICE_TYPE);
        if (sd && sd->size >= sizeof(enum AVAudioServiceType))
            avctx->audio_service_type = *(enum AVAudioServiceType*)sd->data;

        /* check for valid frame size */
        if (avctx->codec->capabilities & AV_CODEC_CAP_SMALL_LAST_FRAME) {
            if (src->nb_samples > avctx->frame_size) {
                av_log(avctx, AV_LOG_ERROR, "more samples than frame size\n");
                return AVERROR(EINVAL);
            }
        } else if (!(avctx->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)) {
            /* if we already got an undersized frame, that must have been the last */
            if (avctx->internal->last_audio_frame) {
                av_log(avctx, AV_LOG_ERROR, "frame_size (%d) was not respected for a non-last frame\n", avctx->frame_size);
                return AVERROR(EINVAL);
            }

            if (src->nb_samples < avctx->frame_size) {
                ret = pad_last_frame(avctx, dst, src);
                if (ret < 0)
                    return ret;

                avctx->internal->last_audio_frame = 1;
            } else if (src->nb_samples > avctx->frame_size) {
                av_log(avctx, AV_LOG_ERROR, "nb_samples (%d) != frame_size (%d)\n", src->nb_samples, avctx->frame_size);
                return AVERROR(EINVAL);
            }
        }
    }

    if (!dst->data[0]) {
        ret = av_frame_ref(dst, src);
        if (ret < 0)
             return ret;
    }

    return 0;
}

int attribute_align_arg avcodec_send_frame(AVCodecContext *avctx, const AVFrame *frame)
{
    AVCodecInternal *avci = avctx->internal;
    int ret;

    if (!avcodec_is_open(avctx) || !av_codec_is_encoder(avctx->codec))
        return AVERROR(EINVAL);

    if (avci->draining)
        return AVERROR_EOF;

    if (avci->buffer_frame->data[0])
        return AVERROR(EAGAIN);

    if (!frame) {
        avci->draining = 1;
    } else {
        ret = encode_send_frame_internal(avctx, frame);
        if (ret < 0)
            return ret;
    }

    if (!avci->buffer_pkt->data && !avci->buffer_pkt->side_data) {
        ret = encode_receive_packet_internal(avctx, avci->buffer_pkt);
        if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
            return ret;
    }

    return 0;
}

int attribute_align_arg avcodec_receive_packet(AVCodecContext *avctx, AVPacket *avpkt)
{
    AVCodecInternal *avci = avctx->internal;
    int ret;

    av_packet_unref(avpkt);

    if (!avcodec_is_open(avctx) || !av_codec_is_encoder(avctx->codec))
        return AVERROR(EINVAL);

    if (avci->buffer_pkt->data || avci->buffer_pkt->side_data) {
        av_packet_move_ref(avpkt, avci->buffer_pkt);
    } else {
        ret = encode_receive_packet_internal(avctx, avpkt);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int compat_encode(AVCodecContext *avctx, AVPacket *avpkt,
                         int *got_packet, const AVFrame *frame)
{
    AVCodecInternal *avci = avctx->internal;
    AVPacket user_pkt;
    int ret;

    *got_packet = 0;

    if (frame && avctx->codec->type == AVMEDIA_TYPE_VIDEO) {
        if (frame->format == AV_PIX_FMT_NONE)
            av_log(avctx, AV_LOG_WARNING, "AVFrame.format is not set\n");
        if (frame->width == 0 || frame->height == 0)
            av_log(avctx, AV_LOG_WARNING, "AVFrame.width or height is not set\n");
    }

    ret = avcodec_send_frame(avctx, frame);
    if (ret == AVERROR_EOF)
        ret = 0;
    else if (ret == AVERROR(EAGAIN)) {
        /* we fully drain all the output in each encode call, so this should not
         * ever happen */
        return AVERROR_BUG;
    } else if (ret < 0)
        return ret;

    av_packet_move_ref(&user_pkt, avpkt);
    while (ret >= 0) {
        ret = avcodec_receive_packet(avctx, avpkt);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                ret = 0;
            goto finish;
        }

        if (avpkt != avci->compat_encode_packet) {
            if (avpkt->data && user_pkt.data) {
                if (user_pkt.size >= avpkt->size) {
                    memcpy(user_pkt.data, avpkt->data, avpkt->size);
                    av_buffer_unref(&avpkt->buf);
                    avpkt->buf  = user_pkt.buf;
                    avpkt->data = user_pkt.data;
                    av_init_packet(&user_pkt);
                } else {
                    av_log(avctx, AV_LOG_ERROR, "Provided packet is too small, needs to be %d\n", avpkt->size);
                    av_packet_unref(avpkt);
                    ret = AVERROR(EINVAL);
                    goto finish;
                }
            }

            *got_packet = 1;
            avpkt = avci->compat_encode_packet;
        } else {
            if (!avci->compat_decode_warned) {
                av_log(avctx, AV_LOG_WARNING, "The deprecated avcodec_encode_* "
                       "API cannot return all the packets for this encoder. "
                       "Some packets will be dropped. Update your code to the "
                       "new encoding API to fix this.\n");
                avci->compat_decode_warned = 1;
                av_packet_unref(avpkt);
            }
        }

        if (avci->draining)
            break;
    }

finish:
    if (ret < 0)
        av_packet_unref(&user_pkt);

    return ret;
}

int attribute_align_arg avcodec_encode_audio2(AVCodecContext *avctx,
                                              AVPacket *avpkt,
                                              const AVFrame *frame,
                                              int *got_packet_ptr)
{
    int ret = compat_encode(avctx, avpkt, got_packet_ptr, frame);

    if (ret < 0)
        av_packet_unref(avpkt);

    return ret;
}

int attribute_align_arg avcodec_encode_video2(AVCodecContext *avctx,
                                              AVPacket *avpkt,
                                              const AVFrame *frame,
                                              int *got_packet_ptr)
{
    int ret = compat_encode(avctx, avpkt, got_packet_ptr, frame);

    if (ret < 0)
        av_packet_unref(avpkt);

    return ret;
}
