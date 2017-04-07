/*
 * generic decoding-related code
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

#include <stdint.h>
#include <string.h>

#include "config.h"

#if CONFIG_ICONV
# include <iconv.h>
#endif

#include "libavutil/avassert.h"
#include "libavutil/bprint.h"
#include "libavutil/common.h"
#include "libavutil/frame.h"
#include "libavutil/hwcontext.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"

#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "thread.h"

static int apply_param_change(AVCodecContext *avctx, const AVPacket *avpkt)
{
    int size = 0, ret;
    const uint8_t *data;
    uint32_t flags;
    int64_t val;

    data = av_packet_get_side_data(avpkt, AV_PKT_DATA_PARAM_CHANGE, &size);
    if (!data)
        return 0;

    if (!(avctx->codec->capabilities & AV_CODEC_CAP_PARAM_CHANGE)) {
        av_log(avctx, AV_LOG_ERROR, "This decoder does not support parameter "
               "changes, but PARAM_CHANGE side data was sent to it.\n");
        ret = AVERROR(EINVAL);
        goto fail2;
    }

    if (size < 4)
        goto fail;

    flags = bytestream_get_le32(&data);
    size -= 4;

    if (flags & AV_SIDE_DATA_PARAM_CHANGE_CHANNEL_COUNT) {
        if (size < 4)
            goto fail;
        val = bytestream_get_le32(&data);
        if (val <= 0 || val > INT_MAX) {
            av_log(avctx, AV_LOG_ERROR, "Invalid channel count");
            ret = AVERROR_INVALIDDATA;
            goto fail2;
        }
        avctx->channels = val;
        size -= 4;
    }
    if (flags & AV_SIDE_DATA_PARAM_CHANGE_CHANNEL_LAYOUT) {
        if (size < 8)
            goto fail;
        avctx->channel_layout = bytestream_get_le64(&data);
        size -= 8;
    }
    if (flags & AV_SIDE_DATA_PARAM_CHANGE_SAMPLE_RATE) {
        if (size < 4)
            goto fail;
        val = bytestream_get_le32(&data);
        if (val <= 0 || val > INT_MAX) {
            av_log(avctx, AV_LOG_ERROR, "Invalid sample rate");
            ret = AVERROR_INVALIDDATA;
            goto fail2;
        }
        avctx->sample_rate = val;
        size -= 4;
    }
    if (flags & AV_SIDE_DATA_PARAM_CHANGE_DIMENSIONS) {
        if (size < 8)
            goto fail;
        avctx->width  = bytestream_get_le32(&data);
        avctx->height = bytestream_get_le32(&data);
        size -= 8;
        ret = ff_set_dimensions(avctx, avctx->width, avctx->height);
        if (ret < 0)
            goto fail2;
    }

    return 0;
fail:
    av_log(avctx, AV_LOG_ERROR, "PARAM_CHANGE side data too small.\n");
    ret = AVERROR_INVALIDDATA;
fail2:
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error applying parameter changes.\n");
        if (avctx->err_recognition & AV_EF_EXPLODE)
            return ret;
    }
    return 0;
}

static int unrefcount_frame(AVCodecInternal *avci, AVFrame *frame)
{
    int ret;

    /* move the original frame to our backup */
    av_frame_unref(avci->to_free);
    av_frame_move_ref(avci->to_free, frame);

    /* now copy everything except the AVBufferRefs back
     * note that we make a COPY of the side data, so calling av_frame_free() on
     * the caller's frame will work properly */
    ret = av_frame_copy_props(frame, avci->to_free);
    if (ret < 0)
        return ret;

    memcpy(frame->data,     avci->to_free->data,     sizeof(frame->data));
    memcpy(frame->linesize, avci->to_free->linesize, sizeof(frame->linesize));
    if (avci->to_free->extended_data != avci->to_free->data) {
        int planes = av_frame_get_channels(avci->to_free);
        int size   = planes * sizeof(*frame->extended_data);

        if (!size) {
            av_frame_unref(frame);
            return AVERROR_BUG;
        }

        frame->extended_data = av_malloc(size);
        if (!frame->extended_data) {
            av_frame_unref(frame);
            return AVERROR(ENOMEM);
        }
        memcpy(frame->extended_data, avci->to_free->extended_data,
               size);
    } else
        frame->extended_data = frame->data;

    frame->format         = avci->to_free->format;
    frame->width          = avci->to_free->width;
    frame->height         = avci->to_free->height;
    frame->channel_layout = avci->to_free->channel_layout;
    frame->nb_samples     = avci->to_free->nb_samples;
    av_frame_set_channels(frame, av_frame_get_channels(avci->to_free));

    return 0;
}

/**
 * Attempt to guess proper monotonic timestamps for decoded video frames
 * which might have incorrect times. Input timestamps may wrap around, in
 * which case the output will as well.
 *
 * @param pts the pts field of the decoded AVPacket, as passed through
 * AVFrame.pts
 * @param dts the dts field of the decoded AVPacket
 * @return one of the input values, may be AV_NOPTS_VALUE
 */
static int64_t guess_correct_pts(AVCodecContext *ctx,
                                 int64_t reordered_pts, int64_t dts)
{
    int64_t pts = AV_NOPTS_VALUE;

    if (dts != AV_NOPTS_VALUE) {
        ctx->pts_correction_num_faulty_dts += dts <= ctx->pts_correction_last_dts;
        ctx->pts_correction_last_dts = dts;
    } else if (reordered_pts != AV_NOPTS_VALUE)
        ctx->pts_correction_last_dts = reordered_pts;

    if (reordered_pts != AV_NOPTS_VALUE) {
        ctx->pts_correction_num_faulty_pts += reordered_pts <= ctx->pts_correction_last_pts;
        ctx->pts_correction_last_pts = reordered_pts;
    } else if(dts != AV_NOPTS_VALUE)
        ctx->pts_correction_last_pts = dts;

    if ((ctx->pts_correction_num_faulty_pts<=ctx->pts_correction_num_faulty_dts || dts == AV_NOPTS_VALUE)
       && reordered_pts != AV_NOPTS_VALUE)
        pts = reordered_pts;
    else
        pts = dts;

    return pts;
}

static int do_decode(AVCodecContext *avctx, AVPacket *pkt)
{
    int got_frame = 0;
    int ret;

    av_assert0(!avctx->internal->buffer_frame->buf[0]);

    if (!pkt)
        pkt = avctx->internal->buffer_pkt;

    // This is the lesser evil. The field is for compatibility with legacy users
    // of the legacy API, and users using the new API should not be forced to
    // even know about this field.
    avctx->refcounted_frames = 1;

    // Some codecs (at least wma lossless) will crash when feeding drain packets
    // after EOF was signaled.
    if (avctx->internal->draining_done)
        return AVERROR_EOF;

    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        ret = avcodec_decode_video2(avctx, avctx->internal->buffer_frame,
                                    &got_frame, pkt);
        if (ret >= 0 && !(avctx->flags & AV_CODEC_FLAG_TRUNCATED))
            ret = pkt->size;
    } else if (avctx->codec_type == AVMEDIA_TYPE_AUDIO) {
        ret = avcodec_decode_audio4(avctx, avctx->internal->buffer_frame,
                                    &got_frame, pkt);
    } else {
        ret = AVERROR(EINVAL);
    }

    if (ret == AVERROR(EAGAIN))
        ret = pkt->size;

    if (avctx->internal->draining && !got_frame)
        avctx->internal->draining_done = 1;

    if (ret < 0)
        return ret;

    if (ret >= pkt->size) {
        av_packet_unref(avctx->internal->buffer_pkt);
    } else {
        int consumed = ret;

        if (pkt != avctx->internal->buffer_pkt) {
            av_packet_unref(avctx->internal->buffer_pkt);
            if ((ret = av_packet_ref(avctx->internal->buffer_pkt, pkt)) < 0)
                return ret;
        }

        avctx->internal->buffer_pkt->data += consumed;
        avctx->internal->buffer_pkt->size -= consumed;
        avctx->internal->buffer_pkt->pts   = AV_NOPTS_VALUE;
        avctx->internal->buffer_pkt->dts   = AV_NOPTS_VALUE;
    }

    if (got_frame)
        av_assert0(avctx->internal->buffer_frame->buf[0]);

    return 0;
}

int attribute_align_arg avcodec_send_packet(AVCodecContext *avctx, const AVPacket *avpkt)
{
    int ret;

    if (!avcodec_is_open(avctx) || !av_codec_is_decoder(avctx->codec))
        return AVERROR(EINVAL);

    if (avctx->internal->draining)
        return AVERROR_EOF;

    if (avpkt && !avpkt->size && avpkt->data)
        return AVERROR(EINVAL);

    if (!avpkt || !avpkt->size) {
        avctx->internal->draining = 1;
        avpkt = NULL;

        if (!(avctx->codec->capabilities & AV_CODEC_CAP_DELAY))
            return 0;
    }

    if (avctx->codec->send_packet) {
        if (avpkt) {
            AVPacket tmp = *avpkt;
#if FF_API_MERGE_SD
FF_DISABLE_DEPRECATION_WARNINGS
            int did_split = av_packet_split_side_data(&tmp);
FF_ENABLE_DEPRECATION_WARNINGS
#endif
            ret = apply_param_change(avctx, &tmp);
            if (ret >= 0)
                ret = avctx->codec->send_packet(avctx, &tmp);
#if FF_API_MERGE_SD
            if (did_split)
                av_packet_free_side_data(&tmp);
#endif
            return ret;
        } else {
            return avctx->codec->send_packet(avctx, NULL);
        }
    }

    // Emulation via old API. Assume avpkt is likely not refcounted, while
    // decoder output is always refcounted, and avoid copying.

    if (avctx->internal->buffer_pkt->size || avctx->internal->buffer_frame->buf[0])
        return AVERROR(EAGAIN);

    // The goal is decoding the first frame of the packet without using memcpy,
    // because the common case is having only 1 frame per packet (especially
    // with video, but audio too). In other cases, it can't be avoided, unless
    // the user is feeding refcounted packets.
    return do_decode(avctx, (AVPacket *)avpkt);
}

int attribute_align_arg avcodec_receive_frame(AVCodecContext *avctx, AVFrame *frame)
{
    int ret;

    av_frame_unref(frame);

    if (!avcodec_is_open(avctx) || !av_codec_is_decoder(avctx->codec))
        return AVERROR(EINVAL);

    if (avctx->codec->receive_frame) {
        if (avctx->internal->draining && !(avctx->codec->capabilities & AV_CODEC_CAP_DELAY))
            return AVERROR_EOF;
        ret = avctx->codec->receive_frame(avctx, frame);
        if (ret >= 0) {
            if (av_frame_get_best_effort_timestamp(frame) == AV_NOPTS_VALUE) {
                av_frame_set_best_effort_timestamp(frame,
                    guess_correct_pts(avctx, frame->pts, frame->pkt_dts));
            }
        }
        return ret;
    }

    // Emulation via old API.

    if (!avctx->internal->buffer_frame->buf[0]) {
        if (!avctx->internal->buffer_pkt->size && !avctx->internal->draining)
            return AVERROR(EAGAIN);

        while (1) {
            if ((ret = do_decode(avctx, avctx->internal->buffer_pkt)) < 0) {
                av_packet_unref(avctx->internal->buffer_pkt);
                return ret;
            }
            // Some audio decoders may consume partial data without returning
            // a frame (fate-wmapro-2ch). There is no way to make the caller
            // call avcodec_receive_frame() again without returning a frame,
            // so try to decode more in these cases.
            if (avctx->internal->buffer_frame->buf[0] ||
                !avctx->internal->buffer_pkt->size)
                break;
        }
    }

    if (!avctx->internal->buffer_frame->buf[0])
        return avctx->internal->draining ? AVERROR_EOF : AVERROR(EAGAIN);

    av_frame_move_ref(frame, avctx->internal->buffer_frame);
    return 0;
}

int attribute_align_arg avcodec_decode_video2(AVCodecContext *avctx, AVFrame *picture,
                                              int *got_picture_ptr,
                                              const AVPacket *avpkt)
{
    AVCodecInternal *avci = avctx->internal;
    int ret;
    // copy to ensure we do not change avpkt
    AVPacket tmp = *avpkt;

    if (!avctx->codec)
        return AVERROR(EINVAL);
    if (avctx->codec->type != AVMEDIA_TYPE_VIDEO) {
        av_log(avctx, AV_LOG_ERROR, "Invalid media type for video\n");
        return AVERROR(EINVAL);
    }

    if (!avctx->codec->decode) {
        av_log(avctx, AV_LOG_ERROR, "This decoder requires using the avcodec_send_packet() API.\n");
        return AVERROR(ENOSYS);
    }

    *got_picture_ptr = 0;
    if ((avctx->coded_width || avctx->coded_height) && av_image_check_size2(avctx->coded_width, avctx->coded_height, avctx->max_pixels, AV_PIX_FMT_NONE, 0, avctx))
        return AVERROR(EINVAL);

    avctx->internal->pkt = avpkt;
    ret = apply_param_change(avctx, avpkt);
    if (ret < 0)
        return ret;

    av_frame_unref(picture);

    if ((avctx->codec->capabilities & AV_CODEC_CAP_DELAY) || avpkt->size ||
        (avctx->active_thread_type & FF_THREAD_FRAME)) {
#if FF_API_MERGE_SD
FF_DISABLE_DEPRECATION_WARNINGS
        int did_split = av_packet_split_side_data(&tmp);
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        ret = apply_param_change(avctx, &tmp);
        if (ret < 0)
            goto fail;

        avctx->internal->pkt = &tmp;
        if (HAVE_THREADS && avctx->active_thread_type & FF_THREAD_FRAME)
            ret = ff_thread_decode_frame(avctx, picture, got_picture_ptr,
                                         &tmp);
        else {
            ret = avctx->codec->decode(avctx, picture, got_picture_ptr,
                                       &tmp);
            if (!(avctx->codec->caps_internal & FF_CODEC_CAP_SETS_PKT_DTS))
                picture->pkt_dts = avpkt->dts;

            if(!avctx->has_b_frames){
                av_frame_set_pkt_pos(picture, avpkt->pos);
            }
            //FIXME these should be under if(!avctx->has_b_frames)
            /* get_buffer is supposed to set frame parameters */
            if (!(avctx->codec->capabilities & AV_CODEC_CAP_DR1)) {
                if (!picture->sample_aspect_ratio.num)    picture->sample_aspect_ratio = avctx->sample_aspect_ratio;
                if (!picture->width)                      picture->width               = avctx->width;
                if (!picture->height)                     picture->height              = avctx->height;
                if (picture->format == AV_PIX_FMT_NONE)   picture->format              = avctx->pix_fmt;
            }
        }

fail:
        emms_c(); //needed to avoid an emms_c() call before every return;

        avctx->internal->pkt = NULL;
#if FF_API_MERGE_SD
        if (did_split) {
            av_packet_free_side_data(&tmp);
            if(ret == tmp.size)
                ret = avpkt->size;
        }
#endif
        if (picture->flags & AV_FRAME_FLAG_DISCARD) {
            *got_picture_ptr = 0;
        }
        if (*got_picture_ptr) {
            if (!avctx->refcounted_frames) {
                int err = unrefcount_frame(avci, picture);
                if (err < 0)
                    return err;
            }

            avctx->frame_number++;
            av_frame_set_best_effort_timestamp(picture,
                                               guess_correct_pts(avctx,
                                                                 picture->pts,
                                                                 picture->pkt_dts));
        } else
            av_frame_unref(picture);
    } else
        ret = 0;

    /* many decoders assign whole AVFrames, thus overwriting extended_data;
     * make sure it's set correctly */
    av_assert0(!picture->extended_data || picture->extended_data == picture->data);

#if FF_API_AVCTX_TIMEBASE
    if (avctx->framerate.num > 0 && avctx->framerate.den > 0)
        avctx->time_base = av_inv_q(av_mul_q(avctx->framerate, (AVRational){avctx->ticks_per_frame, 1}));
#endif

    return ret;
}

int attribute_align_arg avcodec_decode_audio4(AVCodecContext *avctx,
                                              AVFrame *frame,
                                              int *got_frame_ptr,
                                              const AVPacket *avpkt)
{
    AVCodecInternal *avci = avctx->internal;
    int ret = 0;

    *got_frame_ptr = 0;

    if (!avctx->codec)
        return AVERROR(EINVAL);

    if (!avctx->codec->decode) {
        av_log(avctx, AV_LOG_ERROR, "This decoder requires using the avcodec_send_packet() API.\n");
        return AVERROR(ENOSYS);
    }

    if (!avpkt->data && avpkt->size) {
        av_log(avctx, AV_LOG_ERROR, "invalid packet: NULL data, size != 0\n");
        return AVERROR(EINVAL);
    }
    if (avctx->codec->type != AVMEDIA_TYPE_AUDIO) {
        av_log(avctx, AV_LOG_ERROR, "Invalid media type for audio\n");
        return AVERROR(EINVAL);
    }

    av_frame_unref(frame);

    if ((avctx->codec->capabilities & AV_CODEC_CAP_DELAY) || avpkt->size || (avctx->active_thread_type & FF_THREAD_FRAME)) {
        uint8_t *side;
        int side_size;
        uint32_t discard_padding = 0;
        uint8_t skip_reason = 0;
        uint8_t discard_reason = 0;
        // copy to ensure we do not change avpkt
        AVPacket tmp = *avpkt;
#if FF_API_MERGE_SD
FF_DISABLE_DEPRECATION_WARNINGS
        int did_split = av_packet_split_side_data(&tmp);
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        ret = apply_param_change(avctx, &tmp);
        if (ret < 0)
            goto fail;

        avctx->internal->pkt = &tmp;
        if (HAVE_THREADS && avctx->active_thread_type & FF_THREAD_FRAME)
            ret = ff_thread_decode_frame(avctx, frame, got_frame_ptr, &tmp);
        else {
            ret = avctx->codec->decode(avctx, frame, got_frame_ptr, &tmp);
            av_assert0(ret <= tmp.size);
            frame->pkt_dts = avpkt->dts;
        }
        if (ret >= 0 && *got_frame_ptr) {
            avctx->frame_number++;
            av_frame_set_best_effort_timestamp(frame,
                                               guess_correct_pts(avctx,
                                                                 frame->pts,
                                                                 frame->pkt_dts));
            if (frame->format == AV_SAMPLE_FMT_NONE)
                frame->format = avctx->sample_fmt;
            if (!frame->channel_layout)
                frame->channel_layout = avctx->channel_layout;
            if (!av_frame_get_channels(frame))
                av_frame_set_channels(frame, avctx->channels);
            if (!frame->sample_rate)
                frame->sample_rate = avctx->sample_rate;
        }

        side= av_packet_get_side_data(avctx->internal->pkt, AV_PKT_DATA_SKIP_SAMPLES, &side_size);
        if(side && side_size>=10) {
            avctx->internal->skip_samples = AV_RL32(side) * avctx->internal->skip_samples_multiplier;
            discard_padding = AV_RL32(side + 4);
            av_log(avctx, AV_LOG_DEBUG, "skip %d / discard %d samples due to side data\n",
                   avctx->internal->skip_samples, (int)discard_padding);
            skip_reason = AV_RL8(side + 8);
            discard_reason = AV_RL8(side + 9);
        }

        if ((frame->flags & AV_FRAME_FLAG_DISCARD) && *got_frame_ptr &&
            !(avctx->flags2 & AV_CODEC_FLAG2_SKIP_MANUAL)) {
            avctx->internal->skip_samples = FFMAX(0, avctx->internal->skip_samples - frame->nb_samples);
            *got_frame_ptr = 0;
        }

        if (avctx->internal->skip_samples > 0 && *got_frame_ptr &&
            !(avctx->flags2 & AV_CODEC_FLAG2_SKIP_MANUAL)) {
            if(frame->nb_samples <= avctx->internal->skip_samples){
                *got_frame_ptr = 0;
                avctx->internal->skip_samples -= frame->nb_samples;
                av_log(avctx, AV_LOG_DEBUG, "skip whole frame, skip left: %d\n",
                       avctx->internal->skip_samples);
            } else {
                av_samples_copy(frame->extended_data, frame->extended_data, 0, avctx->internal->skip_samples,
                                frame->nb_samples - avctx->internal->skip_samples, avctx->channels, frame->format);
                if(avctx->pkt_timebase.num && avctx->sample_rate) {
                    int64_t diff_ts = av_rescale_q(avctx->internal->skip_samples,
                                                   (AVRational){1, avctx->sample_rate},
                                                   avctx->pkt_timebase);
                    if(frame->pts!=AV_NOPTS_VALUE)
                        frame->pts += diff_ts;
#if FF_API_PKT_PTS
FF_DISABLE_DEPRECATION_WARNINGS
                    if(frame->pkt_pts!=AV_NOPTS_VALUE)
                        frame->pkt_pts += diff_ts;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
                    if(frame->pkt_dts!=AV_NOPTS_VALUE)
                        frame->pkt_dts += diff_ts;
                    if (av_frame_get_pkt_duration(frame) >= diff_ts)
                        av_frame_set_pkt_duration(frame, av_frame_get_pkt_duration(frame) - diff_ts);
                } else {
                    av_log(avctx, AV_LOG_WARNING, "Could not update timestamps for skipped samples.\n");
                }
                av_log(avctx, AV_LOG_DEBUG, "skip %d/%d samples\n",
                       avctx->internal->skip_samples, frame->nb_samples);
                frame->nb_samples -= avctx->internal->skip_samples;
                avctx->internal->skip_samples = 0;
            }
        }

        if (discard_padding > 0 && discard_padding <= frame->nb_samples && *got_frame_ptr &&
            !(avctx->flags2 & AV_CODEC_FLAG2_SKIP_MANUAL)) {
            if (discard_padding == frame->nb_samples) {
                *got_frame_ptr = 0;
            } else {
                if(avctx->pkt_timebase.num && avctx->sample_rate) {
                    int64_t diff_ts = av_rescale_q(frame->nb_samples - discard_padding,
                                                   (AVRational){1, avctx->sample_rate},
                                                   avctx->pkt_timebase);
                    av_frame_set_pkt_duration(frame, diff_ts);
                } else {
                    av_log(avctx, AV_LOG_WARNING, "Could not update timestamps for discarded samples.\n");
                }
                av_log(avctx, AV_LOG_DEBUG, "discard %d/%d samples\n",
                       (int)discard_padding, frame->nb_samples);
                frame->nb_samples -= discard_padding;
            }
        }

        if ((avctx->flags2 & AV_CODEC_FLAG2_SKIP_MANUAL) && *got_frame_ptr) {
            AVFrameSideData *fside = av_frame_new_side_data(frame, AV_FRAME_DATA_SKIP_SAMPLES, 10);
            if (fside) {
                AV_WL32(fside->data, avctx->internal->skip_samples);
                AV_WL32(fside->data + 4, discard_padding);
                AV_WL8(fside->data + 8, skip_reason);
                AV_WL8(fside->data + 9, discard_reason);
                avctx->internal->skip_samples = 0;
            }
        }
fail:
        avctx->internal->pkt = NULL;
#if FF_API_MERGE_SD
        if (did_split) {
            av_packet_free_side_data(&tmp);
            if(ret == tmp.size)
                ret = avpkt->size;
        }
#endif

        if (ret >= 0 && *got_frame_ptr) {
            if (!avctx->refcounted_frames) {
                int err = unrefcount_frame(avci, frame);
                if (err < 0)
                    return err;
            }
        } else
            av_frame_unref(frame);
    }

    av_assert0(ret <= avpkt->size);

    if (!avci->showed_multi_packet_warning &&
        ret >= 0 && ret != avpkt->size && !(avctx->codec->capabilities & AV_CODEC_CAP_SUBFRAMES)) {
            av_log(avctx, AV_LOG_WARNING, "Multiple frames in a packet.\n");
        avci->showed_multi_packet_warning = 1;
    }

    return ret;
}

static void get_subtitle_defaults(AVSubtitle *sub)
{
    memset(sub, 0, sizeof(*sub));
    sub->pts = AV_NOPTS_VALUE;
}

#define UTF8_MAX_BYTES 4 /* 5 and 6 bytes sequences should not be used */
static int recode_subtitle(AVCodecContext *avctx,
                           AVPacket *outpkt, const AVPacket *inpkt)
{
#if CONFIG_ICONV
    iconv_t cd = (iconv_t)-1;
    int ret = 0;
    char *inb, *outb;
    size_t inl, outl;
    AVPacket tmp;
#endif

    if (avctx->sub_charenc_mode != FF_SUB_CHARENC_MODE_PRE_DECODER || inpkt->size == 0)
        return 0;

#if CONFIG_ICONV
    cd = iconv_open("UTF-8", avctx->sub_charenc);
    av_assert0(cd != (iconv_t)-1);

    inb = inpkt->data;
    inl = inpkt->size;

    if (inl >= INT_MAX / UTF8_MAX_BYTES - AV_INPUT_BUFFER_PADDING_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "Subtitles packet is too big for recoding\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    ret = av_new_packet(&tmp, inl * UTF8_MAX_BYTES);
    if (ret < 0)
        goto end;
    outpkt->buf  = tmp.buf;
    outpkt->data = tmp.data;
    outpkt->size = tmp.size;
    outb = outpkt->data;
    outl = outpkt->size;

    if (iconv(cd, &inb, &inl, &outb, &outl) == (size_t)-1 ||
        iconv(cd, NULL, NULL, &outb, &outl) == (size_t)-1 ||
        outl >= outpkt->size || inl != 0) {
        ret = FFMIN(AVERROR(errno), -1);
        av_log(avctx, AV_LOG_ERROR, "Unable to recode subtitle event \"%s\" "
               "from %s to UTF-8\n", inpkt->data, avctx->sub_charenc);
        av_packet_unref(&tmp);
        goto end;
    }
    outpkt->size -= outl;
    memset(outpkt->data + outpkt->size, 0, outl);

end:
    if (cd != (iconv_t)-1)
        iconv_close(cd);
    return ret;
#else
    av_log(avctx, AV_LOG_ERROR, "requesting subtitles recoding without iconv");
    return AVERROR(EINVAL);
#endif
}

static int utf8_check(const uint8_t *str)
{
    const uint8_t *byte;
    uint32_t codepoint, min;

    while (*str) {
        byte = str;
        GET_UTF8(codepoint, *(byte++), return 0;);
        min = byte - str == 1 ? 0 : byte - str == 2 ? 0x80 :
              1 << (5 * (byte - str) - 4);
        if (codepoint < min || codepoint >= 0x110000 ||
            codepoint == 0xFFFE /* BOM */ ||
            codepoint >= 0xD800 && codepoint <= 0xDFFF /* surrogates */)
            return 0;
        str = byte;
    }
    return 1;
}

#if FF_API_ASS_TIMING
static void insert_ts(AVBPrint *buf, int ts)
{
    if (ts == -1) {
        av_bprintf(buf, "9:59:59.99,");
    } else {
        int h, m, s;

        h = ts/360000;  ts -= 360000*h;
        m = ts/  6000;  ts -=   6000*m;
        s = ts/   100;  ts -=    100*s;
        av_bprintf(buf, "%d:%02d:%02d.%02d,", h, m, s, ts);
    }
}

static int convert_sub_to_old_ass_form(AVSubtitle *sub, const AVPacket *pkt, AVRational tb)
{
    int i;
    AVBPrint buf;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);

    for (i = 0; i < sub->num_rects; i++) {
        char *final_dialog;
        const char *dialog;
        AVSubtitleRect *rect = sub->rects[i];
        int ts_start, ts_duration = -1;
        long int layer;

        if (rect->type != SUBTITLE_ASS || !strncmp(rect->ass, "Dialogue: ", 10))
            continue;

        av_bprint_clear(&buf);

        /* skip ReadOrder */
        dialog = strchr(rect->ass, ',');
        if (!dialog)
            continue;
        dialog++;

        /* extract Layer or Marked */
        layer = strtol(dialog, (char**)&dialog, 10);
        if (*dialog != ',')
            continue;
        dialog++;

        /* rescale timing to ASS time base (ms) */
        ts_start = av_rescale_q(pkt->pts, tb, av_make_q(1, 100));
        if (pkt->duration != -1)
            ts_duration = av_rescale_q(pkt->duration, tb, av_make_q(1, 100));
        sub->end_display_time = FFMAX(sub->end_display_time, 10 * ts_duration);

        /* construct ASS (standalone file form with timestamps) string */
        av_bprintf(&buf, "Dialogue: %ld,", layer);
        insert_ts(&buf, ts_start);
        insert_ts(&buf, ts_duration == -1 ? -1 : ts_start + ts_duration);
        av_bprintf(&buf, "%s\r\n", dialog);

        final_dialog = av_strdup(buf.str);
        if (!av_bprint_is_complete(&buf) || !final_dialog) {
            av_freep(&final_dialog);
            av_bprint_finalize(&buf, NULL);
            return AVERROR(ENOMEM);
        }
        av_freep(&rect->ass);
        rect->ass = final_dialog;
    }

    av_bprint_finalize(&buf, NULL);
    return 0;
}
#endif

int avcodec_decode_subtitle2(AVCodecContext *avctx, AVSubtitle *sub,
                             int *got_sub_ptr,
                             AVPacket *avpkt)
{
    int i, ret = 0;

    if (!avpkt->data && avpkt->size) {
        av_log(avctx, AV_LOG_ERROR, "invalid packet: NULL data, size != 0\n");
        return AVERROR(EINVAL);
    }
    if (!avctx->codec)
        return AVERROR(EINVAL);
    if (avctx->codec->type != AVMEDIA_TYPE_SUBTITLE) {
        av_log(avctx, AV_LOG_ERROR, "Invalid media type for subtitles\n");
        return AVERROR(EINVAL);
    }

    *got_sub_ptr = 0;
    get_subtitle_defaults(sub);

    if ((avctx->codec->capabilities & AV_CODEC_CAP_DELAY) || avpkt->size) {
        AVPacket pkt_recoded;
        AVPacket tmp = *avpkt;
#if FF_API_MERGE_SD
FF_DISABLE_DEPRECATION_WARNINGS
        int did_split = av_packet_split_side_data(&tmp);
        //apply_param_change(avctx, &tmp);

        if (did_split) {
            /* FFMIN() prevents overflow in case the packet wasn't allocated with
             * proper padding.
             * If the side data is smaller than the buffer padding size, the
             * remaining bytes should have already been filled with zeros by the
             * original packet allocation anyway. */
            memset(tmp.data + tmp.size, 0,
                   FFMIN(avpkt->size - tmp.size, AV_INPUT_BUFFER_PADDING_SIZE));
        }
FF_ENABLE_DEPRECATION_WARNINGS
#endif

        pkt_recoded = tmp;
        ret = recode_subtitle(avctx, &pkt_recoded, &tmp);
        if (ret < 0) {
            *got_sub_ptr = 0;
        } else {
            avctx->internal->pkt = &pkt_recoded;

            if (avctx->pkt_timebase.num && avpkt->pts != AV_NOPTS_VALUE)
                sub->pts = av_rescale_q(avpkt->pts,
                                        avctx->pkt_timebase, AV_TIME_BASE_Q);
            ret = avctx->codec->decode(avctx, sub, got_sub_ptr, &pkt_recoded);
            av_assert1((ret >= 0) >= !!*got_sub_ptr &&
                       !!*got_sub_ptr >= !!sub->num_rects);

#if FF_API_ASS_TIMING
            if (avctx->sub_text_format == FF_SUB_TEXT_FMT_ASS_WITH_TIMINGS
                && *got_sub_ptr && sub->num_rects) {
                const AVRational tb = avctx->pkt_timebase.num ? avctx->pkt_timebase
                                                              : avctx->time_base;
                int err = convert_sub_to_old_ass_form(sub, avpkt, tb);
                if (err < 0)
                    ret = err;
            }
#endif

            if (sub->num_rects && !sub->end_display_time && avpkt->duration &&
                avctx->pkt_timebase.num) {
                AVRational ms = { 1, 1000 };
                sub->end_display_time = av_rescale_q(avpkt->duration,
                                                     avctx->pkt_timebase, ms);
            }

            if (avctx->codec_descriptor->props & AV_CODEC_PROP_BITMAP_SUB)
                sub->format = 0;
            else if (avctx->codec_descriptor->props & AV_CODEC_PROP_TEXT_SUB)
                sub->format = 1;

            for (i = 0; i < sub->num_rects; i++) {
                if (sub->rects[i]->ass && !utf8_check(sub->rects[i]->ass)) {
                    av_log(avctx, AV_LOG_ERROR,
                           "Invalid UTF-8 in decoded subtitles text; "
                           "maybe missing -sub_charenc option\n");
                    avsubtitle_free(sub);
                    ret = AVERROR_INVALIDDATA;
                    break;
                }
            }

            if (tmp.data != pkt_recoded.data) { // did we recode?
                /* prevent from destroying side data from original packet */
                pkt_recoded.side_data = NULL;
                pkt_recoded.side_data_elems = 0;

                av_packet_unref(&pkt_recoded);
            }
            avctx->internal->pkt = NULL;
        }

#if FF_API_MERGE_SD
        if (did_split) {
            av_packet_free_side_data(&tmp);
            if(ret == tmp.size)
                ret = avpkt->size;
        }
#endif

        if (*got_sub_ptr)
            avctx->frame_number++;
    }

    return ret;
}

static int is_hwaccel_pix_fmt(enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    return desc->flags & AV_PIX_FMT_FLAG_HWACCEL;
}

enum AVPixelFormat avcodec_default_get_format(struct AVCodecContext *s, const enum AVPixelFormat *fmt)
{
    while (*fmt != AV_PIX_FMT_NONE && is_hwaccel_pix_fmt(*fmt))
        ++fmt;
    return fmt[0];
}

static AVHWAccel *find_hwaccel(enum AVCodecID codec_id,
                               enum AVPixelFormat pix_fmt)
{
    AVHWAccel *hwaccel = NULL;

    while ((hwaccel = av_hwaccel_next(hwaccel)))
        if (hwaccel->id == codec_id
            && hwaccel->pix_fmt == pix_fmt)
            return hwaccel;
    return NULL;
}

static int setup_hwaccel(AVCodecContext *avctx,
                         const enum AVPixelFormat fmt,
                         const char *name)
{
    AVHWAccel *hwa = find_hwaccel(avctx->codec_id, fmt);
    int ret        = 0;

    if (!hwa) {
        av_log(avctx, AV_LOG_ERROR,
               "Could not find an AVHWAccel for the pixel format: %s",
               name);
        return AVERROR(ENOENT);
    }

    if (hwa->capabilities & HWACCEL_CODEC_CAP_EXPERIMENTAL &&
        avctx->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL) {
        av_log(avctx, AV_LOG_WARNING, "Ignoring experimental hwaccel: %s\n",
               hwa->name);
        return AVERROR_PATCHWELCOME;
    }

    if (hwa->priv_data_size) {
        avctx->internal->hwaccel_priv_data = av_mallocz(hwa->priv_data_size);
        if (!avctx->internal->hwaccel_priv_data)
            return AVERROR(ENOMEM);
    }

    if (hwa->init) {
        ret = hwa->init(avctx);
        if (ret < 0) {
            av_freep(&avctx->internal->hwaccel_priv_data);
            return ret;
        }
    }

    avctx->hwaccel = hwa;

    return 0;
}

int ff_get_format(AVCodecContext *avctx, const enum AVPixelFormat *fmt)
{
    const AVPixFmtDescriptor *desc;
    enum AVPixelFormat *choices;
    enum AVPixelFormat ret;
    unsigned n = 0;

    while (fmt[n] != AV_PIX_FMT_NONE)
        ++n;

    av_assert0(n >= 1);
    avctx->sw_pix_fmt = fmt[n - 1];
    av_assert2(!is_hwaccel_pix_fmt(avctx->sw_pix_fmt));

    choices = av_malloc_array(n + 1, sizeof(*choices));
    if (!choices)
        return AV_PIX_FMT_NONE;

    memcpy(choices, fmt, (n + 1) * sizeof(*choices));

    for (;;) {
        if (avctx->hwaccel && avctx->hwaccel->uninit)
            avctx->hwaccel->uninit(avctx);
        av_freep(&avctx->internal->hwaccel_priv_data);
        avctx->hwaccel = NULL;

        av_buffer_unref(&avctx->hw_frames_ctx);

        ret = avctx->get_format(avctx, choices);

        desc = av_pix_fmt_desc_get(ret);
        if (!desc) {
            ret = AV_PIX_FMT_NONE;
            break;
        }

        if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
            break;
#if FF_API_CAP_VDPAU
        if (avctx->codec->capabilities&AV_CODEC_CAP_HWACCEL_VDPAU)
            break;
#endif

        if (avctx->hw_frames_ctx) {
            AVHWFramesContext *hw_frames_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
            if (hw_frames_ctx->format != ret) {
                av_log(avctx, AV_LOG_ERROR, "Format returned from get_buffer() "
                       "does not match the format of provided AVHWFramesContext\n");
                ret = AV_PIX_FMT_NONE;
                break;
            }
        }

        if (!setup_hwaccel(avctx, ret, desc->name))
            break;

        /* Remove failed hwaccel from choices */
        for (n = 0; choices[n] != ret; n++)
            av_assert0(choices[n] != AV_PIX_FMT_NONE);

        do
            choices[n] = choices[n + 1];
        while (choices[n++] != AV_PIX_FMT_NONE);
    }

    av_freep(&choices);
    return ret;
}

static int update_frame_pool(AVCodecContext *avctx, AVFrame *frame)
{
    FramePool *pool = avctx->internal->pool;
    int i, ret;

    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_VIDEO: {
        uint8_t *data[4];
        int linesize[4];
        int size[4] = { 0 };
        int w = frame->width;
        int h = frame->height;
        int tmpsize, unaligned;

        if (pool->format == frame->format &&
            pool->width == frame->width && pool->height == frame->height)
            return 0;

        avcodec_align_dimensions2(avctx, &w, &h, pool->stride_align);

        do {
            // NOTE: do not align linesizes individually, this breaks e.g. assumptions
            // that linesize[0] == 2*linesize[1] in the MPEG-encoder for 4:2:2
            ret = av_image_fill_linesizes(linesize, avctx->pix_fmt, w);
            if (ret < 0)
                return ret;
            // increase alignment of w for next try (rhs gives the lowest bit set in w)
            w += w & ~(w - 1);

            unaligned = 0;
            for (i = 0; i < 4; i++)
                unaligned |= linesize[i] % pool->stride_align[i];
        } while (unaligned);

        tmpsize = av_image_fill_pointers(data, avctx->pix_fmt, h,
                                         NULL, linesize);
        if (tmpsize < 0)
            return -1;

        for (i = 0; i < 3 && data[i + 1]; i++)
            size[i] = data[i + 1] - data[i];
        size[i] = tmpsize - (data[i] - data[0]);

        for (i = 0; i < 4; i++) {
            av_buffer_pool_uninit(&pool->pools[i]);
            pool->linesize[i] = linesize[i];
            if (size[i]) {
                pool->pools[i] = av_buffer_pool_init(size[i] + 16 + STRIDE_ALIGN - 1,
                                                     CONFIG_MEMORY_POISONING ?
                                                        NULL :
                                                        av_buffer_allocz);
                if (!pool->pools[i]) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
            }
        }
        pool->format = frame->format;
        pool->width  = frame->width;
        pool->height = frame->height;

        break;
        }
    case AVMEDIA_TYPE_AUDIO: {
        int ch     = av_frame_get_channels(frame); //av_get_channel_layout_nb_channels(frame->channel_layout);
        int planar = av_sample_fmt_is_planar(frame->format);
        int planes = planar ? ch : 1;

        if (pool->format == frame->format && pool->planes == planes &&
            pool->channels == ch && frame->nb_samples == pool->samples)
            return 0;

        av_buffer_pool_uninit(&pool->pools[0]);
        ret = av_samples_get_buffer_size(&pool->linesize[0], ch,
                                         frame->nb_samples, frame->format, 0);
        if (ret < 0)
            goto fail;

        pool->pools[0] = av_buffer_pool_init(pool->linesize[0], NULL);
        if (!pool->pools[0]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        pool->format     = frame->format;
        pool->planes     = planes;
        pool->channels   = ch;
        pool->samples = frame->nb_samples;
        break;
        }
    default: av_assert0(0);
    }
    return 0;
fail:
    for (i = 0; i < 4; i++)
        av_buffer_pool_uninit(&pool->pools[i]);
    pool->format = -1;
    pool->planes = pool->channels = pool->samples = 0;
    pool->width  = pool->height = 0;
    return ret;
}

static int audio_get_buffer(AVCodecContext *avctx, AVFrame *frame)
{
    FramePool *pool = avctx->internal->pool;
    int planes = pool->planes;
    int i;

    frame->linesize[0] = pool->linesize[0];

    if (planes > AV_NUM_DATA_POINTERS) {
        frame->extended_data = av_mallocz_array(planes, sizeof(*frame->extended_data));
        frame->nb_extended_buf = planes - AV_NUM_DATA_POINTERS;
        frame->extended_buf  = av_mallocz_array(frame->nb_extended_buf,
                                          sizeof(*frame->extended_buf));
        if (!frame->extended_data || !frame->extended_buf) {
            av_freep(&frame->extended_data);
            av_freep(&frame->extended_buf);
            return AVERROR(ENOMEM);
        }
    } else {
        frame->extended_data = frame->data;
        av_assert0(frame->nb_extended_buf == 0);
    }

    for (i = 0; i < FFMIN(planes, AV_NUM_DATA_POINTERS); i++) {
        frame->buf[i] = av_buffer_pool_get(pool->pools[0]);
        if (!frame->buf[i])
            goto fail;
        frame->extended_data[i] = frame->data[i] = frame->buf[i]->data;
    }
    for (i = 0; i < frame->nb_extended_buf; i++) {
        frame->extended_buf[i] = av_buffer_pool_get(pool->pools[0]);
        if (!frame->extended_buf[i])
            goto fail;
        frame->extended_data[i + AV_NUM_DATA_POINTERS] = frame->extended_buf[i]->data;
    }

    if (avctx->debug & FF_DEBUG_BUFFERS)
        av_log(avctx, AV_LOG_DEBUG, "default_get_buffer called on frame %p", frame);

    return 0;
fail:
    av_frame_unref(frame);
    return AVERROR(ENOMEM);
}

static int video_get_buffer(AVCodecContext *s, AVFrame *pic)
{
    FramePool *pool = s->internal->pool;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pic->format);
    int i;

    if (pic->data[0] || pic->data[1] || pic->data[2] || pic->data[3]) {
        av_log(s, AV_LOG_ERROR, "pic->data[*]!=NULL in avcodec_default_get_buffer\n");
        return -1;
    }

    if (!desc) {
        av_log(s, AV_LOG_ERROR,
            "Unable to get pixel format descriptor for format %s\n",
            av_get_pix_fmt_name(pic->format));
        return AVERROR(EINVAL);
    }

    memset(pic->data, 0, sizeof(pic->data));
    pic->extended_data = pic->data;

    for (i = 0; i < 4 && pool->pools[i]; i++) {
        pic->linesize[i] = pool->linesize[i];

        pic->buf[i] = av_buffer_pool_get(pool->pools[i]);
        if (!pic->buf[i])
            goto fail;

        pic->data[i] = pic->buf[i]->data;
    }
    for (; i < AV_NUM_DATA_POINTERS; i++) {
        pic->data[i] = NULL;
        pic->linesize[i] = 0;
    }
    if (desc->flags & AV_PIX_FMT_FLAG_PAL ||
        desc->flags & AV_PIX_FMT_FLAG_PSEUDOPAL)
        avpriv_set_systematic_pal2((uint32_t *)pic->data[1], pic->format);

    if (s->debug & FF_DEBUG_BUFFERS)
        av_log(s, AV_LOG_DEBUG, "default_get_buffer called on pic %p\n", pic);

    return 0;
fail:
    av_frame_unref(pic);
    return AVERROR(ENOMEM);
}

int avcodec_default_get_buffer2(AVCodecContext *avctx, AVFrame *frame, int flags)
{
    int ret;

    if (avctx->hw_frames_ctx)
        return av_hwframe_get_buffer(avctx->hw_frames_ctx, frame, 0);

    if ((ret = update_frame_pool(avctx, frame)) < 0)
        return ret;

    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        return video_get_buffer(avctx, frame);
    case AVMEDIA_TYPE_AUDIO:
        return audio_get_buffer(avctx, frame);
    default:
        return -1;
    }
}

static int add_metadata_from_side_data(const AVPacket *avpkt, AVFrame *frame)
{
    int size;
    const uint8_t *side_metadata;

    AVDictionary **frame_md = avpriv_frame_get_metadatap(frame);

    side_metadata = av_packet_get_side_data(avpkt,
                                            AV_PKT_DATA_STRINGS_METADATA, &size);
    return av_packet_unpack_dictionary(side_metadata, size, frame_md);
}

int ff_init_buffer_info(AVCodecContext *avctx, AVFrame *frame)
{
    const AVPacket *pkt = avctx->internal->pkt;
    int i;
    static const struct {
        enum AVPacketSideDataType packet;
        enum AVFrameSideDataType frame;
    } sd[] = {
        { AV_PKT_DATA_REPLAYGAIN ,                AV_FRAME_DATA_REPLAYGAIN },
        { AV_PKT_DATA_DISPLAYMATRIX,              AV_FRAME_DATA_DISPLAYMATRIX },
        { AV_PKT_DATA_SPHERICAL,                  AV_FRAME_DATA_SPHERICAL },
        { AV_PKT_DATA_STEREO3D,                   AV_FRAME_DATA_STEREO3D },
        { AV_PKT_DATA_AUDIO_SERVICE_TYPE,         AV_FRAME_DATA_AUDIO_SERVICE_TYPE },
        { AV_PKT_DATA_MASTERING_DISPLAY_METADATA, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA },
        { AV_PKT_DATA_CONTENT_LIGHT_LEVEL,        AV_FRAME_DATA_CONTENT_LIGHT_LEVEL },
    };

    if (pkt) {
        frame->pts = pkt->pts;
#if FF_API_PKT_PTS
FF_DISABLE_DEPRECATION_WARNINGS
        frame->pkt_pts = pkt->pts;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        av_frame_set_pkt_pos     (frame, pkt->pos);
        av_frame_set_pkt_duration(frame, pkt->duration);
        av_frame_set_pkt_size    (frame, pkt->size);

        for (i = 0; i < FF_ARRAY_ELEMS(sd); i++) {
            int size;
            uint8_t *packet_sd = av_packet_get_side_data(pkt, sd[i].packet, &size);
            if (packet_sd) {
                AVFrameSideData *frame_sd = av_frame_new_side_data(frame,
                                                                   sd[i].frame,
                                                                   size);
                if (!frame_sd)
                    return AVERROR(ENOMEM);

                memcpy(frame_sd->data, packet_sd, size);
            }
        }
        add_metadata_from_side_data(pkt, frame);

        if (pkt->flags & AV_PKT_FLAG_DISCARD) {
            frame->flags |= AV_FRAME_FLAG_DISCARD;
        } else {
            frame->flags = (frame->flags & ~AV_FRAME_FLAG_DISCARD);
        }
    } else {
        frame->pts = AV_NOPTS_VALUE;
#if FF_API_PKT_PTS
FF_DISABLE_DEPRECATION_WARNINGS
        frame->pkt_pts = AV_NOPTS_VALUE;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        av_frame_set_pkt_pos     (frame, -1);
        av_frame_set_pkt_duration(frame, 0);
        av_frame_set_pkt_size    (frame, -1);
    }
    frame->reordered_opaque = avctx->reordered_opaque;

    if (frame->color_primaries == AVCOL_PRI_UNSPECIFIED)
        frame->color_primaries = avctx->color_primaries;
    if (frame->color_trc == AVCOL_TRC_UNSPECIFIED)
        frame->color_trc = avctx->color_trc;
    if (av_frame_get_colorspace(frame) == AVCOL_SPC_UNSPECIFIED)
        av_frame_set_colorspace(frame, avctx->colorspace);
    if (av_frame_get_color_range(frame) == AVCOL_RANGE_UNSPECIFIED)
        av_frame_set_color_range(frame, avctx->color_range);
    if (frame->chroma_location == AVCHROMA_LOC_UNSPECIFIED)
        frame->chroma_location = avctx->chroma_sample_location;

    switch (avctx->codec->type) {
    case AVMEDIA_TYPE_VIDEO:
        frame->format              = avctx->pix_fmt;
        if (!frame->sample_aspect_ratio.num)
            frame->sample_aspect_ratio = avctx->sample_aspect_ratio;

        if (frame->width && frame->height &&
            av_image_check_sar(frame->width, frame->height,
                               frame->sample_aspect_ratio) < 0) {
            av_log(avctx, AV_LOG_WARNING, "ignoring invalid SAR: %u/%u\n",
                   frame->sample_aspect_ratio.num,
                   frame->sample_aspect_ratio.den);
            frame->sample_aspect_ratio = (AVRational){ 0, 1 };
        }

        break;
    case AVMEDIA_TYPE_AUDIO:
        if (!frame->sample_rate)
            frame->sample_rate    = avctx->sample_rate;
        if (frame->format < 0)
            frame->format         = avctx->sample_fmt;
        if (!frame->channel_layout) {
            if (avctx->channel_layout) {
                 if (av_get_channel_layout_nb_channels(avctx->channel_layout) !=
                     avctx->channels) {
                     av_log(avctx, AV_LOG_ERROR, "Inconsistent channel "
                            "configuration.\n");
                     return AVERROR(EINVAL);
                 }

                frame->channel_layout = avctx->channel_layout;
            } else {
                if (avctx->channels > FF_SANE_NB_CHANNELS) {
                    av_log(avctx, AV_LOG_ERROR, "Too many channels: %d.\n",
                           avctx->channels);
                    return AVERROR(ENOSYS);
                }
            }
        }
        av_frame_set_channels(frame, avctx->channels);
        break;
    }
    return 0;
}

int ff_decode_frame_props(AVCodecContext *avctx, AVFrame *frame)
{
    return ff_init_buffer_info(avctx, frame);
}

static void validate_avframe_allocation(AVCodecContext *avctx, AVFrame *frame)
{
    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        int i;
        int num_planes = av_pix_fmt_count_planes(frame->format);
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(frame->format);
        int flags = desc ? desc->flags : 0;
        if (num_planes == 1 && (flags & AV_PIX_FMT_FLAG_PAL))
            num_planes = 2;
        for (i = 0; i < num_planes; i++) {
            av_assert0(frame->data[i]);
        }
        // For now do not enforce anything for palette of pseudopal formats
        if (num_planes == 1 && (flags & AV_PIX_FMT_FLAG_PSEUDOPAL))
            num_planes = 2;
        // For formats without data like hwaccel allow unused pointers to be non-NULL.
        for (i = num_planes; num_planes > 0 && i < FF_ARRAY_ELEMS(frame->data); i++) {
            if (frame->data[i])
                av_log(avctx, AV_LOG_ERROR, "Buffer returned by get_buffer2() did not zero unused plane pointers\n");
            frame->data[i] = NULL;
        }
    }
}

static int get_buffer_internal(AVCodecContext *avctx, AVFrame *frame, int flags)
{
    const AVHWAccel *hwaccel = avctx->hwaccel;
    int override_dimensions = 1;
    int ret;

    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        if ((ret = av_image_check_size2(avctx->width, avctx->height, avctx->max_pixels, AV_PIX_FMT_NONE, 0, avctx)) < 0 || avctx->pix_fmt<0) {
            av_log(avctx, AV_LOG_ERROR, "video_get_buffer: image parameters invalid\n");
            return AVERROR(EINVAL);
        }

        if (frame->width <= 0 || frame->height <= 0) {
            frame->width  = FFMAX(avctx->width,  AV_CEIL_RSHIFT(avctx->coded_width,  avctx->lowres));
            frame->height = FFMAX(avctx->height, AV_CEIL_RSHIFT(avctx->coded_height, avctx->lowres));
            override_dimensions = 0;
        }

        if (frame->data[0] || frame->data[1] || frame->data[2] || frame->data[3]) {
            av_log(avctx, AV_LOG_ERROR, "pic->data[*]!=NULL in get_buffer_internal\n");
            return AVERROR(EINVAL);
        }
    }
    ret = ff_decode_frame_props(avctx, frame);
    if (ret < 0)
        return ret;

    if (hwaccel) {
        if (hwaccel->alloc_frame) {
            ret = hwaccel->alloc_frame(avctx, frame);
            goto end;
        }
    } else
        avctx->sw_pix_fmt = avctx->pix_fmt;

    ret = avctx->get_buffer2(avctx, frame, flags);
    if (ret >= 0)
        validate_avframe_allocation(avctx, frame);

end:
    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO && !override_dimensions) {
        frame->width  = avctx->width;
        frame->height = avctx->height;
    }

    return ret;
}

int ff_get_buffer(AVCodecContext *avctx, AVFrame *frame, int flags)
{
    int ret = get_buffer_internal(avctx, frame, flags);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        frame->width = frame->height = 0;
    }
    return ret;
}

static int reget_buffer_internal(AVCodecContext *avctx, AVFrame *frame)
{
    AVFrame *tmp;
    int ret;

    av_assert0(avctx->codec_type == AVMEDIA_TYPE_VIDEO);

    if (frame->data[0] && (frame->width != avctx->width || frame->height != avctx->height || frame->format != avctx->pix_fmt)) {
        av_log(avctx, AV_LOG_WARNING, "Picture changed from size:%dx%d fmt:%s to size:%dx%d fmt:%s in reget buffer()\n",
               frame->width, frame->height, av_get_pix_fmt_name(frame->format), avctx->width, avctx->height, av_get_pix_fmt_name(avctx->pix_fmt));
        av_frame_unref(frame);
    }

    ff_init_buffer_info(avctx, frame);

    if (!frame->data[0])
        return ff_get_buffer(avctx, frame, AV_GET_BUFFER_FLAG_REF);

    if (av_frame_is_writable(frame))
        return ff_decode_frame_props(avctx, frame);

    tmp = av_frame_alloc();
    if (!tmp)
        return AVERROR(ENOMEM);

    av_frame_move_ref(tmp, frame);

    ret = ff_get_buffer(avctx, frame, AV_GET_BUFFER_FLAG_REF);
    if (ret < 0) {
        av_frame_free(&tmp);
        return ret;
    }

    av_frame_copy(frame, tmp);
    av_frame_free(&tmp);

    return 0;
}

int ff_reget_buffer(AVCodecContext *avctx, AVFrame *frame)
{
    int ret = reget_buffer_internal(avctx, frame);
    if (ret < 0)
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
    return ret;
}

void avcodec_flush_buffers(AVCodecContext *avctx)
{
    avctx->internal->draining      = 0;
    avctx->internal->draining_done = 0;
    av_frame_unref(avctx->internal->buffer_frame);
    av_packet_unref(avctx->internal->buffer_pkt);
    avctx->internal->buffer_pkt_valid = 0;

    if (HAVE_THREADS && avctx->active_thread_type & FF_THREAD_FRAME)
        ff_thread_flush(avctx);
    else if (avctx->codec->flush)
        avctx->codec->flush(avctx);

    avctx->pts_correction_last_pts =
    avctx->pts_correction_last_dts = INT64_MIN;

    if (!avctx->refcounted_frames)
        av_frame_unref(avctx->internal->to_free);
}
