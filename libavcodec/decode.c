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
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/fifo.h"
#include "libavutil/frame.h"
#include "libavutil/hwcontext.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/intmath.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "bytestream.h"
#include "bsf.h"
#include "codec_internal.h"
#include "decode.h"
#include "hwconfig.h"
#include "internal.h"
#include "thread.h"

static int apply_param_change(AVCodecContext *avctx, const AVPacket *avpkt)
{
    int ret;
    size_t size;
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

#if FF_API_OLD_CHANNEL_LAYOUT
FF_DISABLE_DEPRECATION_WARNINGS
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
FF_ENABLE_DEPRECATION_WARNINGS
#endif
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
            avci->last_pkt_props->opaque = (void *)(intptr_t)pkt->size; // Needed for ff_decode_frame_props().
    }
    return ret;
}

static int decode_bsfs_init(AVCodecContext *avctx)
{
    AVCodecInternal *avci = avctx->internal;
    const FFCodec *const codec = ffcodec(avctx->codec);
    int ret;

    if (avci->bsf)
        return 0;

    ret = av_bsf_list_parse_str(codec->bsfs, &avci->bsf);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error parsing decoder bitstream filters '%s': %s\n", codec->bsfs, av_err2str(ret));
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

    if (!(ffcodec(avctx->codec)->caps_internal & FF_CODEC_CAP_SETS_FRAME_PROPS)) {
        ret = extract_packet_props(avctx->internal, pkt);
        if (ret < 0)
            goto finish;
    }

    ret = apply_param_change(avctx, pkt);
    if (ret < 0)
        goto finish;

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
static inline int decode_simple_internal(AVCodecContext *avctx, AVFrame *frame, int64_t *discarded_samples)
{
    AVCodecInternal   *avci = avctx->internal;
    AVPacket     *const pkt = avci->in_pkt;
    const FFCodec *const codec = ffcodec(avctx->codec);
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
        ret = codec->cb.decode(avctx, frame, &got_frame, pkt);

        if (!(codec->caps_internal & FF_CODEC_CAP_SETS_PKT_DTS))
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
    } else if (avctx->codec->type == AVMEDIA_TYPE_AUDIO) {
        uint8_t *side;
        size_t side_size;
        uint32_t discard_padding = 0;
        uint8_t skip_reason = 0;
        uint8_t discard_reason = 0;

        if (ret >= 0 && got_frame) {
            if (frame->format == AV_SAMPLE_FMT_NONE)
                frame->format = avctx->sample_fmt;
            if (!frame->ch_layout.nb_channels) {
                int ret2 = av_channel_layout_copy(&frame->ch_layout, &avctx->ch_layout);
                if (ret2 < 0) {
                    ret = ret2;
                    got_frame = 0;
                }
            }
#if FF_API_OLD_CHANNEL_LAYOUT
FF_DISABLE_DEPRECATION_WARNINGS
            if (!frame->channel_layout)
                frame->channel_layout = avctx->ch_layout.order == AV_CHANNEL_ORDER_NATIVE ?
                                        avctx->ch_layout.u.mask : 0;
            if (!frame->channels)
                frame->channels = avctx->ch_layout.nb_channels;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
            if (!frame->sample_rate)
                frame->sample_rate = avctx->sample_rate;
        }

        side= av_packet_get_side_data(avci->last_pkt_props, AV_PKT_DATA_SKIP_SAMPLES, &side_size);
        if(side && side_size>=10) {
            avci->skip_samples = AV_RL32(side);
            avci->skip_samples = FFMAX(0, avci->skip_samples);
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
            *discarded_samples += frame->nb_samples;
        }

        if (avci->skip_samples > 0 && got_frame &&
            !(avctx->flags2 & AV_CODEC_FLAG2_SKIP_MANUAL)) {
            if(frame->nb_samples <= avci->skip_samples){
                got_frame = 0;
                *discarded_samples += frame->nb_samples;
                avci->skip_samples -= frame->nb_samples;
                av_log(avctx, AV_LOG_DEBUG, "skip whole frame, skip left: %d\n",
                       avci->skip_samples);
            } else {
                av_samples_copy(frame->extended_data, frame->extended_data, 0, avci->skip_samples,
                                frame->nb_samples - avci->skip_samples, avctx->ch_layout.nb_channels, frame->format);
                if(avctx->pkt_timebase.num && avctx->sample_rate) {
                    int64_t diff_ts = av_rescale_q(avci->skip_samples,
                                                   (AVRational){1, avctx->sample_rate},
                                                   avctx->pkt_timebase);
                    if(frame->pts!=AV_NOPTS_VALUE)
                        frame->pts += diff_ts;
                    if(frame->pkt_dts!=AV_NOPTS_VALUE)
                        frame->pkt_dts += diff_ts;
                    if (frame->duration >= diff_ts)
                        frame->duration -= diff_ts;
                } else {
                    av_log(avctx, AV_LOG_WARNING, "Could not update timestamps for skipped samples.\n");
                }
                av_log(avctx, AV_LOG_DEBUG, "skip %d/%d samples\n",
                       avci->skip_samples, frame->nb_samples);
                *discarded_samples += avci->skip_samples;
                frame->nb_samples -= avci->skip_samples;
                avci->skip_samples = 0;
            }
        }

        if (discard_padding > 0 && discard_padding <= frame->nb_samples && got_frame &&
            !(avctx->flags2 & AV_CODEC_FLAG2_SKIP_MANUAL)) {
            if (discard_padding == frame->nb_samples) {
                *discarded_samples += frame->nb_samples;
                got_frame = 0;
            } else {
                if(avctx->pkt_timebase.num && avctx->sample_rate) {
                    int64_t diff_ts = av_rescale_q(frame->nb_samples - discard_padding,
                                                   (AVRational){1, avctx->sample_rate},
                                                   avctx->pkt_timebase);
                    frame->duration = diff_ts;
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

    if (ret >= 0 && avctx->codec->type == AVMEDIA_TYPE_VIDEO)
        ret = pkt->size;

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

    if (ret >= pkt->size || ret < 0) {
        av_packet_unref(pkt);
    } else {
        int consumed = ret;

        pkt->data                += consumed;
        pkt->size                -= consumed;
        pkt->pts                  = AV_NOPTS_VALUE;
        pkt->dts                  = AV_NOPTS_VALUE;
        if (!(codec->caps_internal & FF_CODEC_CAP_SETS_FRAME_PROPS)) {
            // See extract_packet_props() comment.
            avci->last_pkt_props->opaque = (void *)((intptr_t)avci->last_pkt_props->opaque - consumed);
            avci->last_pkt_props->pts = AV_NOPTS_VALUE;
            avci->last_pkt_props->dts = AV_NOPTS_VALUE;
        }
    }

    if (got_frame)
        av_assert0(frame->buf[0]);

    return ret < 0 ? ret : 0;
}

#if CONFIG_LCMS2
static int detect_colorspace(AVCodecContext *avctx, AVFrame *frame)
{
    AVCodecInternal *avci = avctx->internal;
    enum AVColorTransferCharacteristic trc;
    AVColorPrimariesDesc coeffs;
    enum AVColorPrimaries prim;
    cmsHPROFILE profile;
    AVFrameSideData *sd;
    int ret;
    if (!(avctx->flags2 & AV_CODEC_FLAG2_ICC_PROFILES))
        return 0;

    sd = av_frame_get_side_data(frame, AV_FRAME_DATA_ICC_PROFILE);
    if (!sd || !sd->size)
        return 0;

    if (!avci->icc.avctx) {
        ret = ff_icc_context_init(&avci->icc, avctx);
        if (ret < 0)
            return ret;
    }

    profile = cmsOpenProfileFromMemTHR(avci->icc.ctx, sd->data, sd->size);
    if (!profile)
        return AVERROR_INVALIDDATA;

    ret = ff_icc_profile_read_primaries(&avci->icc, profile, &coeffs);
    if (!ret)
        ret = ff_icc_profile_detect_transfer(&avci->icc, profile, &trc);
    cmsCloseProfile(profile);
    if (ret < 0)
        return ret;

    prim = av_csp_primaries_id_from_desc(&coeffs);
    if (prim != AVCOL_PRI_UNSPECIFIED)
        frame->color_primaries = prim;
    if (trc != AVCOL_TRC_UNSPECIFIED)
        frame->color_trc = trc;
    return 0;
}
#else /* !CONFIG_LCMS2 */
static int detect_colorspace(av_unused AVCodecContext *c, av_unused AVFrame *f)
{
    return 0;
}
#endif

static int decode_simple_receive_frame(AVCodecContext *avctx, AVFrame *frame)
{
    int ret;
    int64_t discarded_samples = 0;

    while (!frame->buf[0]) {
        if (discarded_samples > avctx->max_samples)
            return AVERROR(EAGAIN);
        ret = decode_simple_internal(avctx, frame, &discarded_samples);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int decode_receive_frame_internal(AVCodecContext *avctx, AVFrame *frame)
{
    AVCodecInternal *avci = avctx->internal;
    const FFCodec *const codec = ffcodec(avctx->codec);
    int ret, ok;

    av_assert0(!frame->buf[0]);

    if (codec->cb_type == FF_CODEC_CB_TYPE_RECEIVE_FRAME) {
        ret = codec->cb.receive_frame(avctx, frame);
    } else
        ret = decode_simple_receive_frame(avctx, frame);

    if (ret == AVERROR_EOF)
        avci->draining_done = 1;

    /* preserve ret */
    ok = detect_colorspace(avctx, frame);
    if (ok < 0) {
        av_frame_unref(frame);
        return ok;
    }

    if (!ret) {
        frame->best_effort_timestamp = guess_correct_pts(avctx,
                                                         frame->pts,
                                                         frame->pkt_dts);

#if FF_API_PKT_DURATION
FF_DISABLE_DEPRECATION_WARNINGS
        frame->pkt_duration = frame->duration;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

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

// make sure frames returned to the caller are valid
static int frame_validate(AVCodecContext *avctx, AVFrame *frame)
{
    if (!frame->buf[0] || frame->format < 0)
        goto fail;

    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        if (frame->width <= 0 || frame->height <= 0)
            goto fail;
        break;
    case AVMEDIA_TYPE_AUDIO:
        if (!av_channel_layout_check(&frame->ch_layout) ||
            frame->sample_rate <= 0)
            goto fail;

        break;
    default: av_assert0(0);
    }

    return 0;
fail:
    av_log(avctx, AV_LOG_ERROR, "An invalid frame was output by a decoder. "
           "This is a bug, please report it.\n");
    return AVERROR_BUG;
}

int ff_decode_receive_frame(AVCodecContext *avctx, AVFrame *frame)
{
    AVCodecInternal *avci = avctx->internal;
    int ret, changed;

    if (!avcodec_is_open(avctx) || !av_codec_is_decoder(avctx->codec))
        return AVERROR(EINVAL);

    if (avci->buffer_frame->buf[0]) {
        av_frame_move_ref(frame, avci->buffer_frame);
    } else {
        ret = decode_receive_frame_internal(avctx, frame);
        if (ret < 0)
            return ret;
    }

    ret = frame_validate(avctx, frame);
    if (ret < 0)
        goto fail;

    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        ret = apply_cropping(avctx, frame);
        if (ret < 0)
            goto fail;
    }

    avctx->frame_num++;
#if FF_API_AVCTX_FRAME_NUMBER
FF_DISABLE_DEPRECATION_WARNINGS
    avctx->frame_number = avctx->frame_num;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    if (avctx->flags & AV_CODEC_FLAG_DROPCHANGED) {

        if (avctx->frame_num == 1) {
            avci->initial_format = frame->format;
            switch(avctx->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                avci->initial_width  = frame->width;
                avci->initial_height = frame->height;
                break;
            case AVMEDIA_TYPE_AUDIO:
                avci->initial_sample_rate = frame->sample_rate ? frame->sample_rate :
                                                                 avctx->sample_rate;
                ret = av_channel_layout_copy(&avci->initial_ch_layout, &frame->ch_layout);
                if (ret < 0)
                    goto fail;
                break;
            }
        }

        if (avctx->frame_num > 1) {
            changed = avci->initial_format != frame->format;

            switch(avctx->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                changed |= avci->initial_width  != frame->width ||
                           avci->initial_height != frame->height;
                break;
            case AVMEDIA_TYPE_AUDIO:
                changed |= avci->initial_sample_rate    != frame->sample_rate ||
                           avci->initial_sample_rate    != avctx->sample_rate ||
                           av_channel_layout_compare(&avci->initial_ch_layout, &frame->ch_layout);
                break;
            }

            if (changed) {
                avci->changed_frames_dropped++;
                av_log(avctx, AV_LOG_INFO, "dropped changed frame #%"PRId64" pts %"PRId64
                                            " drop count: %d \n",
                                            avctx->frame_num, frame->pts,
                                            avci->changed_frames_dropped);
                ret = AVERROR_INPUT_CHANGED;
                goto fail;
            }
        }
    }
    return 0;
fail:
    av_frame_unref(frame);
    return ret;
}

static void get_subtitle_defaults(AVSubtitle *sub)
{
    memset(sub, 0, sizeof(*sub));
    sub->pts = AV_NOPTS_VALUE;
}

#define UTF8_MAX_BYTES 4 /* 5 and 6 bytes sequences should not be used */
static int recode_subtitle(AVCodecContext *avctx, const AVPacket **outpkt,
                           const AVPacket *inpkt, AVPacket *buf_pkt)
{
#if CONFIG_ICONV
    iconv_t cd = (iconv_t)-1;
    int ret = 0;
    char *inb, *outb;
    size_t inl, outl;
#endif

    if (avctx->sub_charenc_mode != FF_SUB_CHARENC_MODE_PRE_DECODER || inpkt->size == 0) {
        *outpkt = inpkt;
        return 0;
    }

#if CONFIG_ICONV
    inb = inpkt->data;
    inl = inpkt->size;

    if (inl >= INT_MAX / UTF8_MAX_BYTES - AV_INPUT_BUFFER_PADDING_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "Subtitles packet is too big for recoding\n");
        return AVERROR(ERANGE);
    }

    cd = iconv_open("UTF-8", avctx->sub_charenc);
    av_assert0(cd != (iconv_t)-1);

    ret = av_new_packet(buf_pkt, inl * UTF8_MAX_BYTES);
    if (ret < 0)
        goto end;
    ret = av_packet_copy_props(buf_pkt, inpkt);
    if (ret < 0)
        goto end;
    outb = buf_pkt->data;
    outl = buf_pkt->size;

    if (iconv(cd, &inb, &inl, &outb, &outl) == (size_t)-1 ||
        iconv(cd, NULL, NULL, &outb, &outl) == (size_t)-1 ||
        outl >= buf_pkt->size || inl != 0) {
        ret = FFMIN(AVERROR(errno), -1);
        av_log(avctx, AV_LOG_ERROR, "Unable to recode subtitle event \"%s\" "
               "from %s to UTF-8\n", inpkt->data, avctx->sub_charenc);
        goto end;
    }
    buf_pkt->size -= outl;
    memset(buf_pkt->data + buf_pkt->size, 0, outl);
    *outpkt = buf_pkt;

    ret = 0;
end:
    if (ret < 0)
        av_packet_unref(buf_pkt);
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

int avcodec_decode_subtitle2(AVCodecContext *avctx, AVSubtitle *sub,
                             int *got_sub_ptr, const AVPacket *avpkt)
{
    int ret = 0;

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
        AVCodecInternal *avci = avctx->internal;
        const AVPacket *pkt;

        ret = recode_subtitle(avctx, &pkt, avpkt, avci->buffer_pkt);
        if (ret < 0)
            return ret;

        if (avctx->pkt_timebase.num && avpkt->pts != AV_NOPTS_VALUE)
            sub->pts = av_rescale_q(avpkt->pts,
                                    avctx->pkt_timebase, AV_TIME_BASE_Q);
        ret = ffcodec(avctx->codec)->cb.decode_sub(avctx, sub, got_sub_ptr, pkt);
        if (pkt == avci->buffer_pkt) // did we recode?
            av_packet_unref(avci->buffer_pkt);
        if (ret < 0) {
            *got_sub_ptr = 0;
            avsubtitle_free(sub);
            return ret;
        }
        av_assert1(!sub->num_rects || *got_sub_ptr);

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

        for (unsigned i = 0; i < sub->num_rects; i++) {
            if (avctx->sub_charenc_mode != FF_SUB_CHARENC_MODE_IGNORE &&
                sub->rects[i]->ass && !utf8_check(sub->rects[i]->ass)) {
                av_log(avctx, AV_LOG_ERROR,
                       "Invalid UTF-8 in decoded subtitles text; "
                       "maybe missing -sub_charenc option\n");
                avsubtitle_free(sub);
                *got_sub_ptr = 0;
                return AVERROR_INVALIDDATA;
            }
        }

        if (*got_sub_ptr)
            avctx->frame_num++;
#if FF_API_AVCTX_FRAME_NUMBER
FF_DISABLE_DEPRECATION_WARNINGS
        avctx->frame_number = avctx->frame_num;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
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
    if (avctx->hw_device_ctx && ffcodec(avctx->codec)->hw_configs) {
        AVHWDeviceContext *device_ctx =
            (AVHWDeviceContext*)avctx->hw_device_ctx->data;
        for (i = 0;; i++) {
            config = &ffcodec(avctx->codec)->hw_configs[i]->public;
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
        hw_config = ffcodec(avctx->codec)->hw_configs[i];
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

    choices = av_memdup(fmt, (n + 1) * sizeof(*choices));
    if (!choices)
        return AV_PIX_FMT_NONE;

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

        if (ffcodec(avctx->codec)->hw_configs) {
            for (i = 0;; i++) {
                hw_config = ffcodec(avctx->codec)->hw_configs[i];
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

static int add_metadata_from_side_data(const AVPacket *avpkt, AVFrame *frame)
{
    size_t size;
    const uint8_t *side_metadata;

    AVDictionary **frame_md = &frame->metadata;

    side_metadata = av_packet_get_side_data(avpkt,
                                            AV_PKT_DATA_STRINGS_METADATA, &size);
    return av_packet_unpack_dictionary(side_metadata, size, frame_md);
}

int ff_decode_frame_props_from_pkt(const AVCodecContext *avctx,
                                   AVFrame *frame, const AVPacket *pkt)
{
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
        { AV_PKT_DATA_DYNAMIC_HDR10_PLUS,         AV_FRAME_DATA_DYNAMIC_HDR_PLUS },
    };

    frame->pts          = pkt->pts;
    frame->pkt_pos      = pkt->pos;
    frame->duration     = pkt->duration;
    frame->pkt_size     = pkt->size;

    for (int i = 0; i < FF_ARRAY_ELEMS(sd); i++) {
        size_t size;
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

    if (avctx->flags & AV_CODEC_FLAG_COPY_OPAQUE) {
        int ret = av_buffer_replace(&frame->opaque_ref, pkt->opaque_ref);
        if (ret < 0)
            return ret;
        frame->opaque = pkt->opaque;
    }

    return 0;
}

int ff_decode_frame_props(AVCodecContext *avctx, AVFrame *frame)
{
    const AVPacket *pkt = avctx->internal->last_pkt_props;

    if (!(ffcodec(avctx->codec)->caps_internal & FF_CODEC_CAP_SETS_FRAME_PROPS)) {
        int ret = ff_decode_frame_props_from_pkt(avctx, frame, pkt);
        if (ret < 0)
            return ret;
        frame->pkt_size     = (int)(intptr_t)pkt->opaque;
    }
#if FF_API_REORDERED_OPAQUE
FF_DISABLE_DEPRECATION_WARNINGS
    frame->reordered_opaque = avctx->reordered_opaque;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

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
        if (!frame->ch_layout.nb_channels) {
            int ret = av_channel_layout_copy(&frame->ch_layout, &avctx->ch_layout);
            if (ret < 0)
                return ret;
        }
#if FF_API_OLD_CHANNEL_LAYOUT
FF_DISABLE_DEPRECATION_WARNINGS
        frame->channels = frame->ch_layout.nb_channels;
        frame->channel_layout = frame->ch_layout.order == AV_CHANNEL_ORDER_NATIVE ?
                                frame->ch_layout.u.mask : 0;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
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

    av_assert0(av_codec_is_decoder(avctx->codec));

    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        if ((unsigned)avctx->width > INT_MAX - STRIDE_ALIGN ||
            (ret = av_image_check_size2(FFALIGN(avctx->width, STRIDE_ALIGN), avctx->height, avctx->max_pixels, AV_PIX_FMT_NONE, 0, avctx)) < 0 || avctx->pix_fmt<0) {
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
#if FF_API_OLD_CHANNEL_LAYOUT
FF_DISABLE_DEPRECATION_WARNINGS
        /* compat layer for old-style get_buffer() implementations */
        avctx->channels = avctx->ch_layout.nb_channels;
        avctx->channel_layout = (avctx->ch_layout.order == AV_CHANNEL_ORDER_NATIVE) ?
                                avctx->ch_layout.u.mask : 0;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

        if (frame->nb_samples * (int64_t)avctx->ch_layout.nb_channels > avctx->max_samples) {
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
        !(ffcodec(avctx->codec)->caps_internal & FF_CODEC_CAP_EXPORTS_CROPPING)) {
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

int ff_decode_preinit(AVCodecContext *avctx)
{
    AVCodecInternal *avci = avctx->internal;
    int ret = 0;

    /* if the decoder init function was already called previously,
     * free the already allocated subtitle_header before overwriting it */
    av_freep(&avctx->subtitle_header);

    if (avctx->codec->max_lowres < avctx->lowres || avctx->lowres < 0) {
        av_log(avctx, AV_LOG_WARNING, "The maximum value for lowres supported by the decoder is %d\n",
               avctx->codec->max_lowres);
        avctx->lowres = avctx->codec->max_lowres;
    }
    if (avctx->sub_charenc) {
        if (avctx->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            av_log(avctx, AV_LOG_ERROR, "Character encoding is only "
                   "supported with subtitles codecs\n");
            return AVERROR(EINVAL);
        } else if (avctx->codec_descriptor->props & AV_CODEC_PROP_BITMAP_SUB) {
            av_log(avctx, AV_LOG_WARNING, "Codec '%s' is bitmap-based, "
                   "subtitles character encoding will be ignored\n",
                   avctx->codec_descriptor->name);
            avctx->sub_charenc_mode = FF_SUB_CHARENC_MODE_DO_NOTHING;
        } else {
            /* input character encoding is set for a text based subtitle
             * codec at this point */
            if (avctx->sub_charenc_mode == FF_SUB_CHARENC_MODE_AUTOMATIC)
                avctx->sub_charenc_mode = FF_SUB_CHARENC_MODE_PRE_DECODER;

            if (avctx->sub_charenc_mode == FF_SUB_CHARENC_MODE_PRE_DECODER) {
#if CONFIG_ICONV
                iconv_t cd = iconv_open("UTF-8", avctx->sub_charenc);
                if (cd == (iconv_t)-1) {
                    ret = AVERROR(errno);
                    av_log(avctx, AV_LOG_ERROR, "Unable to open iconv context "
                           "with input character encoding \"%s\"\n", avctx->sub_charenc);
                    return ret;
                }
                iconv_close(cd);
#else
                av_log(avctx, AV_LOG_ERROR, "Character encoding subtitles "
                       "conversion needs a libavcodec built with iconv support "
                       "for this codec\n");
                return AVERROR(ENOSYS);
#endif
            }
        }
    }

    avctx->pts_correction_num_faulty_pts =
    avctx->pts_correction_num_faulty_dts = 0;
    avctx->pts_correction_last_pts =
    avctx->pts_correction_last_dts = INT64_MIN;

    if (   !CONFIG_GRAY && avctx->flags & AV_CODEC_FLAG_GRAY
        && avctx->codec_descriptor->type == AVMEDIA_TYPE_VIDEO)
        av_log(avctx, AV_LOG_WARNING,
               "gray decoding requested but not enabled at configuration time\n");
    if (avctx->flags2 & AV_CODEC_FLAG2_EXPORT_MVS) {
        avctx->export_side_data |= AV_CODEC_EXPORT_DATA_MVS;
    }

    avci->in_pkt         = av_packet_alloc();
    avci->last_pkt_props = av_packet_alloc();
    if (!avci->in_pkt || !avci->last_pkt_props)
        return AVERROR(ENOMEM);

    ret = decode_bsfs_init(avctx);
    if (ret < 0)
        return ret;

    return 0;
}

int ff_copy_palette(void *dst, const AVPacket *src, void *logctx)
{
    size_t size;
    const void *pal = av_packet_get_side_data(src, AV_PKT_DATA_PALETTE, &size);

    if (pal && size == AVPALETTE_SIZE) {
        memcpy(dst, pal, AVPALETTE_SIZE);
        return 1;
    } else if (pal) {
        av_log(logctx, AV_LOG_ERROR,
               "Palette size %"SIZE_SPECIFIER" is wrong\n", size);
    }
    return 0;
}
