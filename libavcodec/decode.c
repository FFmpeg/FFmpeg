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
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/common.h"
#include "libavutil/frame.h"
#include "libavutil/hwcontext.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/intmath.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "bytestream.h"
#include "decode.h"
#include "hwconfig.h"
#include "internal.h"
#include "thread.h"

typedef struct FramePool {
    /**
     * Pools for each data plane. For audio all the planes have the same size,
     * so only pools[0] is used.
     */
    AVBufferPool *pools[4];

    /*
     * Pool parameters
     */
    int format;
    int width, height;
    int stride_align[AV_NUM_DATA_POINTERS];
    int linesize[4];
    int planes;
    int channels;
    int samples;
} FramePool;

static int apply_param_change(AVCodecContext *avctx, const AVPacket *avpkt)
{
    int size, ret;
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

static int extract_packet_props(AVCodecInternal *avci, const AVPacket *pkt)
{
    int ret = 0;

    av_packet_unref(avci->last_pkt_props);
    if (pkt) {
        ret = av_packet_copy_props(avci->last_pkt_props, pkt);
        if (!ret)
            avci->last_pkt_props->size = pkt->size; // HACK: Needed for ff_decode_frame_props().
    }
    return ret;
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
        int planes = avci->to_free->channels;
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
    frame->channels       = avci->to_free->channels;

    return 0;
}

int ff_decode_bsfs_init(AVCodecContext *avctx)
{
    AVCodecInternal *avci = avctx->internal;
    int ret;

    if (avci->bsf)
        return 0;

    ret = av_bsf_list_parse_str(avctx->codec->bsfs, &avci->bsf);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error parsing decoder bitstream filters '%s': %s\n", avctx->codec->bsfs, av_err2str(ret));
        if (ret != AVERROR(ENOMEM))
            ret = AVERROR_BUG;
        goto fail;
    }

    /* We do not currently have an API for passing the input timebase into decoders,
     * but no filters used here should actually need it.
     * So we make up some plausible-looking number (the MPEG 90kHz timebase) */
    avci->bsf->time_base_in = (AVRational){ 1, 90000 };
    ret = avcodec_parameters_from_context(avci->bsf->par_in, avctx);
    if (ret < 0)
        goto fail;

    ret = av_bsf_init(avci->bsf);
    if (ret < 0)
        goto fail;

    return 0;
fail:
    av_bsf_free(&avci->bsf);
    return ret;
}

int ff_decode_get_packet(AVCodecContext *avctx, AVPacket *pkt)
{
    AVCodecInternal *avci = avctx->internal;
    int ret;

    if (avci->draining)
        return AVERROR_EOF;

    ret = av_bsf_receive_packet(avci->bsf, pkt);
    if (ret == AVERROR_EOF)
        avci->draining = 1;
    if (ret < 0)
        return ret;

    ret = extract_packet_props(avctx->internal, pkt);
    if (ret < 0)
        goto finish;

    ret = apply_param_change(avctx, pkt);
    if (ret < 0)
        goto finish;

    if (avctx->codec->receive_frame)
        avci->compat_decode_consumed += pkt->size;

    return 0;
finish:
    av_packet_unref(pkt);
    return ret;
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

/*
 * The core of the receive_frame_wrapper for the decoders implementing
 * the simple API. Certain decoders might consume partial packets without
 * returning any output, so this function needs to be called in a loop until it
 * returns EAGAIN.
 **/
static inline int decode_simple_internal(AVCodecContext *avctx, AVFrame *frame)
{
    AVCodecInternal   *avci = avctx->internal;
    DecodeSimpleContext *ds = &avci->ds;
    AVPacket           *pkt = ds->in_pkt;
    // copy to ensure we do not change pkt
    int got_frame, actual_got_frame;
    int ret;

    if (!pkt->data && !avci->draining) {
        av_packet_unref(pkt);
        ret = ff_decode_get_packet(avctx, pkt);
        if (ret < 0 && ret != AVERROR_EOF)
            return ret;
    }

    // Some codecs (at least wma lossless) will crash when feeding drain packets
    // after EOF was signaled.
    if (avci->draining_done)
        return AVERROR_EOF;

    if (!pkt->data &&
        !(avctx->codec->capabilities & AV_CODEC_CAP_DELAY ||
          avctx->active_thread_type & FF_THREAD_FRAME))
        return AVERROR_EOF;

    got_frame = 0;

    if (HAVE_THREADS && avctx->active_thread_type & FF_THREAD_FRAME) {
        ret = ff_thread_decode_frame(avctx, frame, &got_frame, pkt);
    } else {
        ret = avctx->codec->decode(avctx, frame, &got_frame, pkt);

        if (!(avctx->codec->caps_internal & FF_CODEC_CAP_SETS_PKT_DTS))
            frame->pkt_dts = pkt->dts;
        if (avctx->codec->type == AVMEDIA_TYPE_VIDEO) {
            if(!avctx->has_b_frames)
                frame->pkt_pos = pkt->pos;
            //FIXME these should be under if(!avctx->has_b_frames)
            /* get_buffer is supposed to set frame parameters */
            if (!(avctx->codec->capabilities & AV_CODEC_CAP_DR1)) {
                if (!frame->sample_aspect_ratio.num)  frame->sample_aspect_ratio = avctx->sample_aspect_ratio;
                if (!frame->width)                    frame->width               = avctx->width;
                if (!frame->height)                   frame->height              = avctx->height;
                if (frame->format == AV_PIX_FMT_NONE) frame->format              = avctx->pix_fmt;
            }
        }
    }
    emms_c();
    actual_got_frame = got_frame;

    if (avctx->codec->type == AVMEDIA_TYPE_VIDEO) {
        if (frame->flags & AV_FRAME_FLAG_DISCARD)
            got_frame = 0;
        if (got_frame)
            frame->best_effort_timestamp = guess_correct_pts(avctx,
                                                             frame->pts,
                                                             frame->pkt_dts);
    } else if (avctx->codec->type == AVMEDIA_TYPE_AUDIO) {
        uint8_t *side;
        int side_size;
        uint32_t discard_padding = 0;
        uint8_t skip_reason = 0;
        uint8_t discard_reason = 0;

        if (ret >= 0 && got_frame) {
            frame->best_effort_timestamp = guess_correct_pts(avctx,
                                                             frame->pts,
                                                             frame->pkt_dts);
            if (frame->format == AV_SAMPLE_FMT_NONE)
                frame->format = avctx->sample_fmt;
            if (!frame->channel_layout)
                frame->channel_layout = avctx->channel_layout;
            if (!frame->channels)
                frame->channels = avctx->channels;
            if (!frame->sample_rate)
                frame->sample_rate = avctx->sample_rate;
        }

        side= av_packet_get_side_data(avci->last_pkt_props, AV_PKT_DATA_SKIP_SAMPLES, &side_size);
        if(side && side_size>=10) {
            avci->skip_samples = AV_RL32(side) * avci->skip_samples_multiplier;
            discard_padding = AV_RL32(side + 4);
            av_log(avctx, AV_LOG_DEBUG, "skip %d / discard %d samples due to side data\n",
                   avci->skip_samples, (int)discard_padding);
            skip_reason = AV_RL8(side + 8);
            discard_reason = AV_RL8(side + 9);
        }

        if ((frame->flags & AV_FRAME_FLAG_DISCARD) && got_frame &&
            !(avctx->flags2 & AV_CODEC_FLAG2_SKIP_MANUAL)) {
            avci->skip_samples = FFMAX(0, avci->skip_samples - frame->nb_samples);
            got_frame = 0;
        }

        if (avci->skip_samples > 0 && got_frame &&
            !(avctx->flags2 & AV_CODEC_FLAG2_SKIP_MANUAL)) {
            if(frame->nb_samples <= avci->skip_samples){
                got_frame = 0;
                avci->skip_samples -= frame->nb_samples;
                av_log(avctx, AV_LOG_DEBUG, "skip whole frame, skip left: %d\n",
                       avci->skip_samples);
            } else {
                av_samples_copy(frame->extended_data, frame->extended_data, 0, avci->skip_samples,
                                frame->nb_samples - avci->skip_samples, avctx->channels, frame->format);
                if(avctx->pkt_timebase.num && avctx->sample_rate) {
                    int64_t diff_ts = av_rescale_q(avci->skip_samples,
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
                    if (frame->pkt_duration >= diff_ts)
                        frame->pkt_duration -= diff_ts;
                } else {
                    av_log(avctx, AV_LOG_WARNING, "Could not update timestamps for skipped samples.\n");
                }
                av_log(avctx, AV_LOG_DEBUG, "skip %d/%d samples\n",
                       avci->skip_samples, frame->nb_samples);
                frame->nb_samples -= avci->skip_samples;
                avci->skip_samples = 0;
            }
        }

        if (discard_padding > 0 && discard_padding <= frame->nb_samples && got_frame &&
            !(avctx->flags2 & AV_CODEC_FLAG2_SKIP_MANUAL)) {
            if (discard_padding == frame->nb_samples) {
                got_frame = 0;
            } else {
                if(avctx->pkt_timebase.num && avctx->sample_rate) {
                    int64_t diff_ts = av_rescale_q(frame->nb_samples - discard_padding,
                                                   (AVRational){1, avctx->sample_rate},
                                                   avctx->pkt_timebase);
                    frame->pkt_duration = diff_ts;
                } else {
                    av_log(avctx, AV_LOG_WARNING, "Could not update timestamps for discarded samples.\n");
                }
                av_log(avctx, AV_LOG_DEBUG, "discard %d/%d samples\n",
                       (int)discard_padding, frame->nb_samples);
                frame->nb_samples -= discard_padding;
            }
        }

        if ((avctx->flags2 & AV_CODEC_FLAG2_SKIP_MANUAL) && got_frame) {
            AVFrameSideData *fside = av_frame_new_side_data(frame, AV_FRAME_DATA_SKIP_SAMPLES, 10);
            if (fside) {
                AV_WL32(fside->data, avci->skip_samples);
                AV_WL32(fside->data + 4, discard_padding);
                AV_WL8(fside->data + 8, skip_reason);
                AV_WL8(fside->data + 9, discard_reason);
                avci->skip_samples = 0;
            }
        }
    }

    if (avctx->codec->type == AVMEDIA_TYPE_AUDIO &&
        !avci->showed_multi_packet_warning &&
        ret >= 0 && ret != pkt->size && !(avctx->codec->capabilities & AV_CODEC_CAP_SUBFRAMES)) {
        av_log(avctx, AV_LOG_WARNING, "Multiple frames in a packet.\n");
        avci->showed_multi_packet_warning = 1;
    }

    if (!got_frame)
        av_frame_unref(frame);

    if (ret >= 0 && avctx->codec->type == AVMEDIA_TYPE_VIDEO && !(avctx->flags & AV_CODEC_FLAG_TRUNCATED))
        ret = pkt->size;

#if FF_API_AVCTX_TIMEBASE
    if (avctx->framerate.num > 0 && avctx->framerate.den > 0)
        avctx->time_base = av_inv_q(av_mul_q(avctx->framerate, (AVRational){avctx->ticks_per_frame, 1}));
#endif

    /* do not stop draining when actual_got_frame != 0 or ret < 0 */
    /* got_frame == 0 but actual_got_frame != 0 when frame is discarded */
    if (avci->draining && !actual_got_frame) {
        if (ret < 0) {
            /* prevent infinite loop if a decoder wrongly always return error on draining */
            /* reasonable nb_errors_max = maximum b frames + thread count */
            int nb_errors_max = 20 + (HAVE_THREADS && avctx->active_thread_type & FF_THREAD_FRAME ?
                                avctx->thread_count : 1);

            if (avci->nb_draining_errors++ >= nb_errors_max) {
                av_log(avctx, AV_LOG_ERROR, "Too many errors when draining, this is a bug. "
                       "Stop draining and force EOF.\n");
                avci->draining_done = 1;
                ret = AVERROR_BUG;
            }
        } else {
            avci->draining_done = 1;
        }
    }

    avci->compat_decode_consumed += ret;

    if (ret >= pkt->size || ret < 0) {
        av_packet_unref(pkt);
    } else {
        int consumed = ret;

        pkt->data                += consumed;
        pkt->size                -= consumed;
        avci->last_pkt_props->size -= consumed; // See extract_packet_props() comment.
        pkt->pts                  = AV_NOPTS_VALUE;
        pkt->dts                  = AV_NOPTS_VALUE;
        avci->last_pkt_props->pts = AV_NOPTS_VALUE;
        avci->last_pkt_props->dts = AV_NOPTS_VALUE;
    }

    if (got_frame)
        av_assert0(frame->buf[0]);

    return ret < 0 ? ret : 0;
}

static int decode_simple_receive_frame(AVCodecContext *avctx, AVFrame *frame)
{
    int ret;

    while (!frame->buf[0]) {
        ret = decode_simple_internal(avctx, frame);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int decode_receive_frame_internal(AVCodecContext *avctx, AVFrame *frame)
{
    AVCodecInternal *avci = avctx->internal;
    int ret;

    av_assert0(!frame->buf[0]);

    if (avctx->codec->receive_frame)
        ret = avctx->codec->receive_frame(avctx, frame);
    else
        ret = decode_simple_receive_frame(avctx, frame);

    if (ret == AVERROR_EOF)
        avci->draining_done = 1;

    if (!ret) {
        /* the only case where decode data is not set should be decoders
         * that do not call ff_get_buffer() */
        av_assert0((frame->private_ref && frame->private_ref->size == sizeof(FrameDecodeData)) ||
                   !(avctx->codec->capabilities & AV_CODEC_CAP_DR1));

        if (frame->private_ref) {
            FrameDecodeData *fdd = (FrameDecodeData*)frame->private_ref->data;

            if (fdd->post_process) {
                ret = fdd->post_process(avctx, frame);
                if (ret < 0) {
                    av_frame_unref(frame);
                    return ret;
                }
            }
        }
    }

    /* free the per-frame decode data */
    av_buffer_unref(&frame->private_ref);

    return ret;
}

int attribute_align_arg avcodec_send_packet(AVCodecContext *avctx, const AVPacket *avpkt)
{
    AVCodecInternal *avci = avctx->internal;
    int ret;

    if (!avcodec_is_open(avctx) || !av_codec_is_decoder(avctx->codec))
        return AVERROR(EINVAL);

    if (avctx->internal->draining)
        return AVERROR_EOF;

    if (avpkt && !avpkt->size && avpkt->data)
        return AVERROR(EINVAL);

    av_packet_unref(avci->buffer_pkt);
    if (avpkt && (avpkt->data || avpkt->side_data_elems)) {
        ret = av_packet_ref(avci->buffer_pkt, avpkt);
        if (ret < 0)
            return ret;
    }

    ret = av_bsf_send_packet(avci->bsf, avci->buffer_pkt);
    if (ret < 0) {
        av_packet_unref(avci->buffer_pkt);
        return ret;
    }

    if (!avci->buffer_frame->buf[0]) {
        ret = decode_receive_frame_internal(avctx, avci->buffer_frame);
        if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
            return ret;
    }

    return 0;
}

static int apply_cropping(AVCodecContext *avctx, AVFrame *frame)
{
    /* make sure we are noisy about decoders returning invalid cropping data */
    if (frame->crop_left >= INT_MAX - frame->crop_right        ||
        frame->crop_top  >= INT_MAX - frame->crop_bottom       ||
        (frame->crop_left + frame->crop_right) >= frame->width ||
        (frame->crop_top + frame->crop_bottom) >= frame->height) {
        av_log(avctx, AV_LOG_WARNING,
               "Invalid cropping information set by a decoder: "
               "%"SIZE_SPECIFIER"/%"SIZE_SPECIFIER"/%"SIZE_SPECIFIER"/%"SIZE_SPECIFIER" "
               "(frame size %dx%d). This is a bug, please report it\n",
               frame->crop_left, frame->crop_right, frame->crop_top, frame->crop_bottom,
               frame->width, frame->height);
        frame->crop_left   = 0;
        frame->crop_right  = 0;
        frame->crop_top    = 0;
        frame->crop_bottom = 0;
        return 0;
    }

    if (!avctx->apply_cropping)
        return 0;

    return av_frame_apply_cropping(frame, avctx->flags & AV_CODEC_FLAG_UNALIGNED ?
                                          AV_FRAME_CROP_UNALIGNED : 0);
}

int attribute_align_arg avcodec_receive_frame(AVCodecContext *avctx, AVFrame *frame)
{
    AVCodecInternal *avci = avctx->internal;
    int ret, changed;

    av_frame_unref(frame);

    if (!avcodec_is_open(avctx) || !av_codec_is_decoder(avctx->codec))
        return AVERROR(EINVAL);

    if (avci->buffer_frame->buf[0]) {
        av_frame_move_ref(frame, avci->buffer_frame);
    } else {
        ret = decode_receive_frame_internal(avctx, frame);
        if (ret < 0)
            return ret;
    }

    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        ret = apply_cropping(avctx, frame);
        if (ret < 0) {
            av_frame_unref(frame);
            return ret;
        }
    }

    avctx->frame_number++;

    if (avctx->flags & AV_CODEC_FLAG_DROPCHANGED) {

        if (avctx->frame_number == 1) {
            avci->initial_format = frame->format;
            switch(avctx->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                avci->initial_width  = frame->width;
                avci->initial_height = frame->height;
                break;
            case AVMEDIA_TYPE_AUDIO:
                avci->initial_sample_rate = frame->sample_rate ? frame->sample_rate :
                                                                 avctx->sample_rate;
                avci->initial_channels       = frame->channels;
                avci->initial_channel_layout = frame->channel_layout;
                break;
            }
        }

        if (avctx->frame_number > 1) {
            changed = avci->initial_format != frame->format;

            switch(avctx->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                changed |= avci->initial_width  != frame->width ||
                           avci->initial_height != frame->height;
                break;
            case AVMEDIA_TYPE_AUDIO:
                changed |= avci->initial_sample_rate    != frame->sample_rate ||
                           avci->initial_sample_rate    != avctx->sample_rate ||
                           avci->initial_channels       != frame->channels ||
                           avci->initial_channel_layout != frame->channel_layout;
                break;
            }

            if (changed) {
                avci->changed_frames_dropped++;
                av_log(avctx, AV_LOG_INFO, "dropped changed frame #%d pts %"PRId64
                                            " drop count: %d \n",
                                            avctx->frame_number, frame->pts,
                                            avci->changed_frames_dropped);
                av_frame_unref(frame);
                return AVERROR_INPUT_CHANGED;
            }
        }
    }
    return 0;
}

static int compat_decode(AVCodecContext *avctx, AVFrame *frame,
                         int *got_frame, const AVPacket *pkt)
{
    AVCodecInternal *avci = avctx->internal;
    int ret = 0;

    av_assert0(avci->compat_decode_consumed == 0);

    if (avci->draining_done && pkt && pkt->size != 0) {
        av_log(avctx, AV_LOG_WARNING, "Got unexpected packet after EOF\n");
        avcodec_flush_buffers(avctx);
    }

    *got_frame = 0;

    if (avci->compat_decode_partial_size > 0 &&
        avci->compat_decode_partial_size != pkt->size) {
        av_log(avctx, AV_LOG_ERROR,
               "Got unexpected packet size after a partial decode\n");
        ret = AVERROR(EINVAL);
        goto finish;
    }

    if (!avci->compat_decode_partial_size) {
        ret = avcodec_send_packet(avctx, pkt);
        if (ret == AVERROR_EOF)
            ret = 0;
        else if (ret == AVERROR(EAGAIN)) {
            /* we fully drain all the output in each decode call, so this should not
             * ever happen */
            ret = AVERROR_BUG;
            goto finish;
        } else if (ret < 0)
            goto finish;
    }

    while (ret >= 0) {
        ret = avcodec_receive_frame(avctx, frame);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                ret = 0;
            goto finish;
        }

        if (frame != avci->compat_decode_frame) {
            if (!avctx->refcounted_frames) {
                ret = unrefcount_frame(avci, frame);
                if (ret < 0)
                    goto finish;
            }

            *got_frame = 1;
            frame = avci->compat_decode_frame;
        } else {
            if (!avci->compat_decode_warned) {
                av_log(avctx, AV_LOG_WARNING, "The deprecated avcodec_decode_* "
                       "API cannot return all the frames for this decoder. "
                       "Some frames will be dropped. Update your code to the "
                       "new decoding API to fix this.\n");
                avci->compat_decode_warned = 1;
            }
        }

        if (avci->draining || (!avctx->codec->bsfs && avci->compat_decode_consumed < pkt->size))
            break;
    }

finish:
    if (ret == 0) {
        /* if there are any bsfs then assume full packet is always consumed */
        if (avctx->codec->bsfs)
            ret = pkt->size;
        else
            ret = FFMIN(avci->compat_decode_consumed, pkt->size);
    }
    avci->compat_decode_consumed = 0;
    avci->compat_decode_partial_size = (ret >= 0) ? pkt->size - ret : 0;

    return ret;
}

int attribute_align_arg avcodec_decode_video2(AVCodecContext *avctx, AVFrame *picture,
                                              int *got_picture_ptr,
                                              const AVPacket *avpkt)
{
    return compat_decode(avctx, picture, got_picture_ptr, avpkt);
}

int attribute_align_arg avcodec_decode_audio4(AVCodecContext *avctx,
                                              AVFrame *frame,
                                              int *got_frame_ptr,
                                              const AVPacket *avpkt)
{
    return compat_decode(avctx, frame, got_frame_ptr, avpkt);
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
        AVPacket pkt_recoded = *avpkt;

        ret = recode_subtitle(avctx, &pkt_recoded, avpkt);
        if (ret < 0) {
            *got_sub_ptr = 0;
        } else {
             ret = extract_packet_props(avctx->internal, &pkt_recoded);
             if (ret < 0)
                return ret;

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
                if (avctx->sub_charenc_mode != FF_SUB_CHARENC_MODE_IGNORE &&
                    sub->rects[i]->ass && !utf8_check(sub->rects[i]->ass)) {
                    av_log(avctx, AV_LOG_ERROR,
                           "Invalid UTF-8 in decoded subtitles text; "
                           "maybe missing -sub_charenc option\n");
                    avsubtitle_free(sub);
                    ret = AVERROR_INVALIDDATA;
                    break;
                }
            }

            if (avpkt->data != pkt_recoded.data) { // did we recode?
                /* prevent from destroying side data from original packet */
                pkt_recoded.side_data = NULL;
                pkt_recoded.side_data_elems = 0;

                av_packet_unref(&pkt_recoded);
            }
        }

        if (*got_sub_ptr)
            avctx->frame_number++;
    }

    return ret;
}

enum AVPixelFormat avcodec_default_get_format(struct AVCodecContext *avctx,
                                              const enum AVPixelFormat *fmt)
{
    const AVPixFmtDescriptor *desc;
    const AVCodecHWConfig *config;
    int i, n;

    // If a device was supplied when the codec was opened, assume that the
    // user wants to use it.
    if (avctx->hw_device_ctx && avctx->codec->hw_configs) {
        AVHWDeviceContext *device_ctx =
            (AVHWDeviceContext*)avctx->hw_device_ctx->data;
        for (i = 0;; i++) {
            config = &avctx->codec->hw_configs[i]->public;
            if (!config)
                break;
            if (!(config->methods &
                  AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX))
                continue;
            if (device_ctx->type != config->device_type)
                continue;
            for (n = 0; fmt[n] != AV_PIX_FMT_NONE; n++) {
                if (config->pix_fmt == fmt[n])
                    return fmt[n];
            }
        }
    }
    // No device or other setup, so we have to choose from things which
    // don't any other external information.

    // If the last element of the list is a software format, choose it
    // (this should be best software format if any exist).
    for (n = 0; fmt[n] != AV_PIX_FMT_NONE; n++);
    desc = av_pix_fmt_desc_get(fmt[n - 1]);
    if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
        return fmt[n - 1];

    // Finally, traverse the list in order and choose the first entry
    // with no external dependencies (if there is no hardware configuration
    // information available then this just picks the first entry).
    for (n = 0; fmt[n] != AV_PIX_FMT_NONE; n++) {
        for (i = 0;; i++) {
            config = avcodec_get_hw_config(avctx->codec, i);
            if (!config)
                break;
            if (config->pix_fmt == fmt[n])
                break;
        }
        if (!config) {
            // No specific config available, so the decoder must be able
            // to handle this format without any additional setup.
            return fmt[n];
        }
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_INTERNAL) {
            // Usable with only internal setup.
            return fmt[n];
        }
    }

    // Nothing is usable, give up.
    return AV_PIX_FMT_NONE;
}

int ff_decode_get_hw_frames_ctx(AVCodecContext *avctx,
                                enum AVHWDeviceType dev_type)
{
    AVHWDeviceContext *device_ctx;
    AVHWFramesContext *frames_ctx;
    int ret;

    if (!avctx->hwaccel)
        return AVERROR(ENOSYS);

    if (avctx->hw_frames_ctx)
        return 0;
    if (!avctx->hw_device_ctx) {
        av_log(avctx, AV_LOG_ERROR, "A hardware frames or device context is "
                "required for hardware accelerated decoding.\n");
        return AVERROR(EINVAL);
    }

    device_ctx = (AVHWDeviceContext *)avctx->hw_device_ctx->data;
    if (device_ctx->type != dev_type) {
        av_log(avctx, AV_LOG_ERROR, "Device type %s expected for hardware "
               "decoding, but got %s.\n", av_hwdevice_get_type_name(dev_type),
               av_hwdevice_get_type_name(device_ctx->type));
        return AVERROR(EINVAL);
    }

    ret = avcodec_get_hw_frames_parameters(avctx,
                                           avctx->hw_device_ctx,
                                           avctx->hwaccel->pix_fmt,
                                           &avctx->hw_frames_ctx);
    if (ret < 0)
        return ret;

    frames_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;


    if (frames_ctx->initial_pool_size) {
        // We guarantee 4 base work surfaces. The function above guarantees 1
        // (the absolute minimum), so add the missing count.
        frames_ctx->initial_pool_size += 3;
    }

    ret = av_hwframe_ctx_init(avctx->hw_frames_ctx);
    if (ret < 0) {
        av_buffer_unref(&avctx->hw_frames_ctx);
        return ret;
    }

    return 0;
}

int avcodec_get_hw_frames_parameters(AVCodecContext *avctx,
                                     AVBufferRef *device_ref,
                                     enum AVPixelFormat hw_pix_fmt,
                                     AVBufferRef **out_frames_ref)
{
    AVBufferRef *frames_ref = NULL;
    const AVCodecHWConfigInternal *hw_config;
    const AVHWAccel *hwa;
    int i, ret;

    for (i = 0;; i++) {
        hw_config = avctx->codec->hw_configs[i];
        if (!hw_config)
            return AVERROR(ENOENT);
        if (hw_config->public.pix_fmt == hw_pix_fmt)
            break;
    }

    hwa = hw_config->hwaccel;
    if (!hwa || !hwa->frame_params)
        return AVERROR(ENOENT);

    frames_ref = av_hwframe_ctx_alloc(device_ref);
    if (!frames_ref)
        return AVERROR(ENOMEM);

    ret = hwa->frame_params(avctx, frames_ref);
    if (ret >= 0) {
        AVHWFramesContext *frames_ctx = (AVHWFramesContext*)frames_ref->data;

        if (frames_ctx->initial_pool_size) {
            // If the user has requested that extra output surfaces be
            // available then add them here.
            if (avctx->extra_hw_frames > 0)
                frames_ctx->initial_pool_size += avctx->extra_hw_frames;

            // If frame threading is enabled then an extra surface per thread
            // is also required.
            if (avctx->active_thread_type & FF_THREAD_FRAME)
                frames_ctx->initial_pool_size += avctx->thread_count;
        }

        *out_frames_ref = frames_ref;
    } else {
        av_buffer_unref(&frames_ref);
    }
    return ret;
}

static int hwaccel_init(AVCodecContext *avctx,
                        const AVCodecHWConfigInternal *hw_config)
{
    const AVHWAccel *hwaccel;
    int err;

    hwaccel = hw_config->hwaccel;
    if (hwaccel->capabilities & AV_HWACCEL_CODEC_CAP_EXPERIMENTAL &&
        avctx->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL) {
        av_log(avctx, AV_LOG_WARNING, "Ignoring experimental hwaccel: %s\n",
               hwaccel->name);
        return AVERROR_PATCHWELCOME;
    }

    if (hwaccel->priv_data_size) {
        avctx->internal->hwaccel_priv_data =
            av_mallocz(hwaccel->priv_data_size);
        if (!avctx->internal->hwaccel_priv_data)
            return AVERROR(ENOMEM);
    }

    avctx->hwaccel = hwaccel;
    if (hwaccel->init) {
        err = hwaccel->init(avctx);
        if (err < 0) {
            av_log(avctx, AV_LOG_ERROR, "Failed setup for format %s: "
                   "hwaccel initialisation returned error.\n",
                   av_get_pix_fmt_name(hw_config->public.pix_fmt));
            av_freep(&avctx->internal->hwaccel_priv_data);
            avctx->hwaccel = NULL;
            return err;
        }
    }

    return 0;
}

static void hwaccel_uninit(AVCodecContext *avctx)
{
    if (avctx->hwaccel && avctx->hwaccel->uninit)
        avctx->hwaccel->uninit(avctx);

    av_freep(&avctx->internal->hwaccel_priv_data);

    avctx->hwaccel = NULL;

    av_buffer_unref(&avctx->hw_frames_ctx);
}

int ff_get_format(AVCodecContext *avctx, const enum AVPixelFormat *fmt)
{
    const AVPixFmtDescriptor *desc;
    enum AVPixelFormat *choices;
    enum AVPixelFormat ret, user_choice;
    const AVCodecHWConfigInternal *hw_config;
    const AVCodecHWConfig *config;
    int i, n, err;

    // Find end of list.
    for (n = 0; fmt[n] != AV_PIX_FMT_NONE; n++);
    // Must contain at least one entry.
    av_assert0(n >= 1);
    // If a software format is available, it must be the last entry.
    desc = av_pix_fmt_desc_get(fmt[n - 1]);
    if (desc->flags & AV_PIX_FMT_FLAG_HWACCEL) {
        // No software format is available.
    } else {
        avctx->sw_pix_fmt = fmt[n - 1];
    }

    choices = av_malloc_array(n + 1, sizeof(*choices));
    if (!choices)
        return AV_PIX_FMT_NONE;

    memcpy(choices, fmt, (n + 1) * sizeof(*choices));

    for (;;) {
        // Remove the previous hwaccel, if there was one.
        hwaccel_uninit(avctx);

        user_choice = avctx->get_format(avctx, choices);
        if (user_choice == AV_PIX_FMT_NONE) {
            // Explicitly chose nothing, give up.
            ret = AV_PIX_FMT_NONE;
            break;
        }

        desc = av_pix_fmt_desc_get(user_choice);
        if (!desc) {
            av_log(avctx, AV_LOG_ERROR, "Invalid format returned by "
                   "get_format() callback.\n");
            ret = AV_PIX_FMT_NONE;
            break;
        }
        av_log(avctx, AV_LOG_DEBUG, "Format %s chosen by get_format().\n",
               desc->name);

        for (i = 0; i < n; i++) {
            if (choices[i] == user_choice)
                break;
        }
        if (i == n) {
            av_log(avctx, AV_LOG_ERROR, "Invalid return from get_format(): "
                   "%s not in possible list.\n", desc->name);
            ret = AV_PIX_FMT_NONE;
            break;
        }

        if (avctx->codec->hw_configs) {
            for (i = 0;; i++) {
                hw_config = avctx->codec->hw_configs[i];
                if (!hw_config)
                    break;
                if (hw_config->public.pix_fmt == user_choice)
                    break;
            }
        } else {
            hw_config = NULL;
        }

        if (!hw_config) {
            // No config available, so no extra setup required.
            ret = user_choice;
            break;
        }
        config = &hw_config->public;

        if (config->methods &
            AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX &&
            avctx->hw_frames_ctx) {
            const AVHWFramesContext *frames_ctx =
                (AVHWFramesContext*)avctx->hw_frames_ctx->data;
            if (frames_ctx->format != user_choice) {
                av_log(avctx, AV_LOG_ERROR, "Invalid setup for format %s: "
                       "does not match the format of the provided frames "
                       "context.\n", desc->name);
                goto try_again;
            }
        } else if (config->methods &
                   AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                   avctx->hw_device_ctx) {
            const AVHWDeviceContext *device_ctx =
                (AVHWDeviceContext*)avctx->hw_device_ctx->data;
            if (device_ctx->type != config->device_type) {
                av_log(avctx, AV_LOG_ERROR, "Invalid setup for format %s: "
                       "does not match the type of the provided device "
                       "context.\n", desc->name);
                goto try_again;
            }
        } else if (config->methods &
                   AV_CODEC_HW_CONFIG_METHOD_INTERNAL) {
            // Internal-only setup, no additional configuration.
        } else if (config->methods &
                   AV_CODEC_HW_CONFIG_METHOD_AD_HOC) {
            // Some ad-hoc configuration we can't see and can't check.
        } else {
            av_log(avctx, AV_LOG_ERROR, "Invalid setup for format %s: "
                   "missing configuration.\n", desc->name);
            goto try_again;
        }
        if (hw_config->hwaccel) {
            av_log(avctx, AV_LOG_DEBUG, "Format %s requires hwaccel "
                   "initialisation.\n", desc->name);
            err = hwaccel_init(avctx, hw_config);
            if (err < 0)
                goto try_again;
        }
        ret = user_choice;
        break;

    try_again:
        av_log(avctx, AV_LOG_DEBUG, "Format %s not usable, retrying "
               "get_format() without it.\n", desc->name);
        for (i = 0; i < n; i++) {
            if (choices[i] == user_choice)
                break;
        }
        for (; i + 1 < n; i++)
            choices[i] = choices[i + 1];
        --n;
    }

    av_freep(&choices);
    return ret;
}

static void frame_pool_free(void *opaque, uint8_t *data)
{
    FramePool *pool = (FramePool*)data;
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(pool->pools); i++)
        av_buffer_pool_uninit(&pool->pools[i]);

    av_freep(&data);
}

static AVBufferRef *frame_pool_alloc(void)
{
    FramePool *pool = av_mallocz(sizeof(*pool));
    AVBufferRef *buf;

    if (!pool)
        return NULL;

    buf = av_buffer_create((uint8_t*)pool, sizeof(*pool),
                           frame_pool_free, NULL, 0);
    if (!buf) {
        av_freep(&pool);
        return NULL;
    }

    return buf;
}

static int update_frame_pool(AVCodecContext *avctx, AVFrame *frame)
{
    FramePool *pool = avctx->internal->pool ?
                      (FramePool*)avctx->internal->pool->data : NULL;
    AVBufferRef *pool_buf;
    int i, ret, ch, planes;

    if (avctx->codec_type == AVMEDIA_TYPE_AUDIO) {
        int planar = av_sample_fmt_is_planar(frame->format);
        ch     = frame->channels;
        planes = planar ? ch : 1;
    }

    if (pool && pool->format == frame->format) {
        if (avctx->codec_type == AVMEDIA_TYPE_VIDEO &&
            pool->width == frame->width && pool->height == frame->height)
            return 0;
        if (avctx->codec_type == AVMEDIA_TYPE_AUDIO && pool->planes == planes &&
            pool->channels == ch && frame->nb_samples == pool->samples)
            return 0;
    }

    pool_buf = frame_pool_alloc();
    if (!pool_buf)
        return AVERROR(ENOMEM);
    pool = (FramePool*)pool_buf->data;

    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_VIDEO: {
        uint8_t *data[4];
        int linesize[4];
        int size[4] = { 0 };
        int w = frame->width;
        int h = frame->height;
        int tmpsize, unaligned;

        avcodec_align_dimensions2(avctx, &w, &h, pool->stride_align);

        do {
            // NOTE: do not align linesizes individually, this breaks e.g. assumptions
            // that linesize[0] == 2*linesize[1] in the MPEG-encoder for 4:2:2
            ret = av_image_fill_linesizes(linesize, avctx->pix_fmt, w);
            if (ret < 0)
                goto fail;
            // increase alignment of w for next try (rhs gives the lowest bit set in w)
            w += w & ~(w - 1);

            unaligned = 0;
            for (i = 0; i < 4; i++)
                unaligned |= linesize[i] % pool->stride_align[i];
        } while (unaligned);

        tmpsize = av_image_fill_pointers(data, avctx->pix_fmt, h,
                                         NULL, linesize);
        if (tmpsize < 0) {
            ret = tmpsize;
            goto fail;
        }

        for (i = 0; i < 3 && data[i + 1]; i++)
            size[i] = data[i + 1] - data[i];
        size[i] = tmpsize - (data[i] - data[0]);

        for (i = 0; i < 4; i++) {
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

    av_buffer_unref(&avctx->internal->pool);
    avctx->internal->pool = pool_buf;

    return 0;
fail:
    av_buffer_unref(&pool_buf);
    return ret;
}

static int audio_get_buffer(AVCodecContext *avctx, AVFrame *frame)
{
    FramePool *pool = (FramePool*)avctx->internal->pool->data;
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
    FramePool *pool = (FramePool*)s->internal->pool->data;
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
        ((desc->flags & FF_PSEUDOPAL) && pic->data[1]))
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

    if (avctx->hw_frames_ctx) {
        ret = av_hwframe_get_buffer(avctx->hw_frames_ctx, frame, 0);
        frame->width  = avctx->coded_width;
        frame->height = avctx->coded_height;
        return ret;
    }

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

    AVDictionary **frame_md = &frame->metadata;

    side_metadata = av_packet_get_side_data(avpkt,
                                            AV_PKT_DATA_STRINGS_METADATA, &size);
    return av_packet_unpack_dictionary(side_metadata, size, frame_md);
}

int ff_decode_frame_props(AVCodecContext *avctx, AVFrame *frame)
{
    const AVPacket *pkt = avctx->internal->last_pkt_props;
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
        { AV_PKT_DATA_A53_CC,                     AV_FRAME_DATA_A53_CC },
        { AV_PKT_DATA_ICC_PROFILE,                AV_FRAME_DATA_ICC_PROFILE },
        { AV_PKT_DATA_S12M_TIMECODE,              AV_FRAME_DATA_S12M_TIMECODE },
    };

    if (pkt) {
        frame->pts = pkt->pts;
#if FF_API_PKT_PTS
FF_DISABLE_DEPRECATION_WARNINGS
        frame->pkt_pts = pkt->pts;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        frame->pkt_pos      = pkt->pos;
        frame->pkt_duration = pkt->duration;
        frame->pkt_size     = pkt->size;

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
    }
    frame->reordered_opaque = avctx->reordered_opaque;

    if (frame->color_primaries == AVCOL_PRI_UNSPECIFIED)
        frame->color_primaries = avctx->color_primaries;
    if (frame->color_trc == AVCOL_TRC_UNSPECIFIED)
        frame->color_trc = avctx->color_trc;
    if (frame->colorspace == AVCOL_SPC_UNSPECIFIED)
        frame->colorspace = avctx->colorspace;
    if (frame->color_range == AVCOL_RANGE_UNSPECIFIED)
        frame->color_range = avctx->color_range;
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
        frame->channels = avctx->channels;
        break;
    }
    return 0;
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
        if ((flags & FF_PSEUDOPAL) && frame->data[1])
            num_planes = 2;
        for (i = 0; i < num_planes; i++) {
            av_assert0(frame->data[i]);
        }
        // For formats without data like hwaccel allow unused pointers to be non-NULL.
        for (i = num_planes; num_planes > 0 && i < FF_ARRAY_ELEMS(frame->data); i++) {
            if (frame->data[i])
                av_log(avctx, AV_LOG_ERROR, "Buffer returned by get_buffer2() did not zero unused plane pointers\n");
            frame->data[i] = NULL;
        }
    }
}

static void decode_data_free(void *opaque, uint8_t *data)
{
    FrameDecodeData *fdd = (FrameDecodeData*)data;

    if (fdd->post_process_opaque_free)
        fdd->post_process_opaque_free(fdd->post_process_opaque);

    if (fdd->hwaccel_priv_free)
        fdd->hwaccel_priv_free(fdd->hwaccel_priv);

    av_freep(&fdd);
}

int ff_attach_decode_data(AVFrame *frame)
{
    AVBufferRef *fdd_buf;
    FrameDecodeData *fdd;

    av_assert1(!frame->private_ref);
    av_buffer_unref(&frame->private_ref);

    fdd = av_mallocz(sizeof(*fdd));
    if (!fdd)
        return AVERROR(ENOMEM);

    fdd_buf = av_buffer_create((uint8_t*)fdd, sizeof(*fdd), decode_data_free,
                               NULL, AV_BUFFER_FLAG_READONLY);
    if (!fdd_buf) {
        av_freep(&fdd);
        return AVERROR(ENOMEM);
    }

    frame->private_ref = fdd_buf;

    return 0;
}

int ff_get_buffer(AVCodecContext *avctx, AVFrame *frame, int flags)
{
    const AVHWAccel *hwaccel = avctx->hwaccel;
    int override_dimensions = 1;
    int ret;

    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        if ((ret = av_image_check_size2(FFALIGN(avctx->width, STRIDE_ALIGN), avctx->height, avctx->max_pixels, AV_PIX_FMT_NONE, 0, avctx)) < 0 || avctx->pix_fmt<0) {
            av_log(avctx, AV_LOG_ERROR, "video_get_buffer: image parameters invalid\n");
            ret = AVERROR(EINVAL);
            goto fail;
        }

        if (frame->width <= 0 || frame->height <= 0) {
            frame->width  = FFMAX(avctx->width,  AV_CEIL_RSHIFT(avctx->coded_width,  avctx->lowres));
            frame->height = FFMAX(avctx->height, AV_CEIL_RSHIFT(avctx->coded_height, avctx->lowres));
            override_dimensions = 0;
        }

        if (frame->data[0] || frame->data[1] || frame->data[2] || frame->data[3]) {
            av_log(avctx, AV_LOG_ERROR, "pic->data[*]!=NULL in get_buffer_internal\n");
            ret = AVERROR(EINVAL);
            goto fail;
        }
    } else if (avctx->codec_type == AVMEDIA_TYPE_AUDIO) {
        if (frame->nb_samples * (int64_t)avctx->channels > avctx->max_samples) {
            av_log(avctx, AV_LOG_ERROR, "samples per frame %d, exceeds max_samples %"PRId64"\n", frame->nb_samples, avctx->max_samples);
            ret = AVERROR(EINVAL);
            goto fail;
        }
    }
    ret = ff_decode_frame_props(avctx, frame);
    if (ret < 0)
        goto fail;

    if (hwaccel) {
        if (hwaccel->alloc_frame) {
            ret = hwaccel->alloc_frame(avctx, frame);
            goto end;
        }
    } else
        avctx->sw_pix_fmt = avctx->pix_fmt;

    ret = avctx->get_buffer2(avctx, frame, flags);
    if (ret < 0)
        goto fail;

    validate_avframe_allocation(avctx, frame);

    ret = ff_attach_decode_data(frame);
    if (ret < 0)
        goto fail;

end:
    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO && !override_dimensions &&
        !(avctx->codec->caps_internal & FF_CODEC_CAP_EXPORTS_CROPPING)) {
        frame->width  = avctx->width;
        frame->height = avctx->height;
    }

fail:
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        av_frame_unref(frame);
    }

    return ret;
}

static int reget_buffer_internal(AVCodecContext *avctx, AVFrame *frame, int flags)
{
    AVFrame *tmp;
    int ret;

    av_assert0(avctx->codec_type == AVMEDIA_TYPE_VIDEO);

    if (frame->data[0] && (frame->width != avctx->width || frame->height != avctx->height || frame->format != avctx->pix_fmt)) {
        av_log(avctx, AV_LOG_WARNING, "Picture changed from size:%dx%d fmt:%s to size:%dx%d fmt:%s in reget buffer()\n",
               frame->width, frame->height, av_get_pix_fmt_name(frame->format), avctx->width, avctx->height, av_get_pix_fmt_name(avctx->pix_fmt));
        av_frame_unref(frame);
    }

    if (!frame->data[0])
        return ff_get_buffer(avctx, frame, AV_GET_BUFFER_FLAG_REF);

    if ((flags & FF_REGET_BUFFER_FLAG_READONLY) || av_frame_is_writable(frame))
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

int ff_reget_buffer(AVCodecContext *avctx, AVFrame *frame, int flags)
{
    int ret = reget_buffer_internal(avctx, frame, flags);
    if (ret < 0)
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
    return ret;
}
