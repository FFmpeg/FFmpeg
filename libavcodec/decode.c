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
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/emms.h"
#include "libavutil/frame.h"
#include "libavutil/hwcontext.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/mem.h"
#include "libavutil/stereo3d.h"

#include "avcodec.h"
#include "avcodec_internal.h"
#include "bytestream.h"
#include "bsf.h"
#include "codec_desc.h"
#include "codec_internal.h"
#include "decode.h"
#include "hwaccel_internal.h"
#include "hwconfig.h"
#include "internal.h"
#include "lcevcdec.h"
#include "packet_internal.h"
#include "progressframe.h"
#include "refstruct.h"
#include "thread.h"
#include "threadprogress.h"

typedef struct DecodeContext {
    AVCodecInternal avci;

    /**
     * This is set to AV_FRAME_FLAG_KEY for decoders of intra-only formats
     * (those whose codec descriptor has AV_CODEC_PROP_INTRA_ONLY set)
     * to set the flag generically.
     */
    int intra_only_flag;

    /**
     * This is set to AV_PICTURE_TYPE_I for intra only video decoders
     * and to AV_PICTURE_TYPE_NONE for other decoders. It is used to set
     * the AVFrame's pict_type before the decoder receives it.
     */
    enum AVPictureType initial_pict_type;

    /* to prevent infinite loop on errors when draining */
    int nb_draining_errors;

    /**
     * The caller has submitted a NULL packet on input.
     */
    int draining_started;

    int64_t pts_correction_num_faulty_pts; /// Number of incorrect PTS values so far
    int64_t pts_correction_num_faulty_dts; /// Number of incorrect DTS values so far
    int64_t pts_correction_last_pts;       /// PTS of the last frame
    int64_t pts_correction_last_dts;       /// DTS of the last frame

    /**
     * Bitmask indicating for which side data types we prefer user-supplied
     * (global or attached to packets) side data over bytestream.
     */
    uint64_t side_data_pref_mask;

    FFLCEVCContext *lcevc;
    int lcevc_frame;
    int width;
    int height;
} DecodeContext;

static DecodeContext *decode_ctx(AVCodecInternal *avci)
{
    return (DecodeContext *)avci;
}

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
#if FF_API_FRAME_PKT
        if (!ret)
            avci->last_pkt_props->stream_index = pkt->size; // Needed for ff_decode_frame_props().
#endif
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

#if !HAVE_THREADS
#define ff_thread_get_packet(avctx, pkt) (AVERROR_BUG)
#define ff_thread_receive_frame(avctx, frame) (AVERROR_BUG)
#endif

static int decode_get_packet(AVCodecContext *avctx, AVPacket *pkt)
{
    AVCodecInternal *avci = avctx->internal;
    int ret;

    ret = av_bsf_receive_packet(avci->bsf, pkt);
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

int ff_decode_get_packet(AVCodecContext *avctx, AVPacket *pkt)
{
    AVCodecInternal *avci = avctx->internal;
    DecodeContext     *dc = decode_ctx(avci);

    if (avci->draining)
        return AVERROR_EOF;

    /* If we are a worker thread, get the next packet from the threading
     * context. Otherwise we are the main (user-facing) context, so we get the
     * next packet from the input filterchain.
     */
    if (avctx->internal->is_frame_mt)
        return ff_thread_get_packet(avctx, pkt);

    while (1) {
        int ret = decode_get_packet(avctx, pkt);
        if (ret == AVERROR(EAGAIN) &&
            (!AVPACKET_IS_EMPTY(avci->buffer_pkt) || dc->draining_started)) {
            ret = av_bsf_send_packet(avci->bsf, avci->buffer_pkt);
            if (ret >= 0)
                continue;

            av_packet_unref(avci->buffer_pkt);
        }

        if (ret == AVERROR_EOF)
            avci->draining = 1;
        return ret;
    }
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
static int64_t guess_correct_pts(DecodeContext *dc,
                                 int64_t reordered_pts, int64_t dts)
{
    int64_t pts = AV_NOPTS_VALUE;

    if (dts != AV_NOPTS_VALUE) {
        dc->pts_correction_num_faulty_dts += dts <= dc->pts_correction_last_dts;
        dc->pts_correction_last_dts = dts;
    } else if (reordered_pts != AV_NOPTS_VALUE)
        dc->pts_correction_last_dts = reordered_pts;

    if (reordered_pts != AV_NOPTS_VALUE) {
        dc->pts_correction_num_faulty_pts += reordered_pts <= dc->pts_correction_last_pts;
        dc->pts_correction_last_pts = reordered_pts;
    } else if(dts != AV_NOPTS_VALUE)
        dc->pts_correction_last_pts = dts;

    if ((dc->pts_correction_num_faulty_pts<=dc->pts_correction_num_faulty_dts || dts == AV_NOPTS_VALUE)
       && reordered_pts != AV_NOPTS_VALUE)
        pts = reordered_pts;
    else
        pts = dts;

    return pts;
}

static int discard_samples(AVCodecContext *avctx, AVFrame *frame, int64_t *discarded_samples)
{
    AVCodecInternal *avci = avctx->internal;
    AVFrameSideData *side;
    uint32_t discard_padding = 0;
    uint8_t skip_reason = 0;
    uint8_t discard_reason = 0;

    side = av_frame_get_side_data(frame, AV_FRAME_DATA_SKIP_SAMPLES);
    if (side && side->size >= 10) {
        avci->skip_samples = AV_RL32(side->data);
        avci->skip_samples = FFMAX(0, avci->skip_samples);
        discard_padding = AV_RL32(side->data + 4);
        av_log(avctx, AV_LOG_DEBUG, "skip %d / discard %d samples due to side data\n",
               avci->skip_samples, (int)discard_padding);
        skip_reason = AV_RL8(side->data + 8);
        discard_reason = AV_RL8(side->data + 9);
    }

    if ((avctx->flags2 & AV_CODEC_FLAG2_SKIP_MANUAL)) {
        if (!side && (avci->skip_samples || discard_padding))
            side = av_frame_new_side_data(frame, AV_FRAME_DATA_SKIP_SAMPLES, 10);
        if (side && (avci->skip_samples || discard_padding)) {
            AV_WL32(side->data, avci->skip_samples);
            AV_WL32(side->data + 4, discard_padding);
            AV_WL8(side->data + 8, skip_reason);
            AV_WL8(side->data + 9, discard_reason);
            avci->skip_samples = 0;
        }
        return 0;
    }
    av_frame_remove_side_data(frame, AV_FRAME_DATA_SKIP_SAMPLES);

    if ((frame->flags & AV_FRAME_FLAG_DISCARD)) {
        avci->skip_samples = FFMAX(0, avci->skip_samples - frame->nb_samples);
        *discarded_samples += frame->nb_samples;
        return AVERROR(EAGAIN);
    }

    if (avci->skip_samples > 0) {
        if (frame->nb_samples <= avci->skip_samples){
            *discarded_samples += frame->nb_samples;
            avci->skip_samples -= frame->nb_samples;
            av_log(avctx, AV_LOG_DEBUG, "skip whole frame, skip left: %d\n",
                   avci->skip_samples);
            return AVERROR(EAGAIN);
        } else {
            av_samples_copy(frame->extended_data, frame->extended_data, 0, avci->skip_samples,
                            frame->nb_samples - avci->skip_samples, avctx->ch_layout.nb_channels, frame->format);
            if (avctx->pkt_timebase.num && avctx->sample_rate) {
                int64_t diff_ts = av_rescale_q(avci->skip_samples,
                                               (AVRational){1, avctx->sample_rate},
                                               avctx->pkt_timebase);
                if (frame->pts != AV_NOPTS_VALUE)
                    frame->pts += diff_ts;
                if (frame->pkt_dts != AV_NOPTS_VALUE)
                    frame->pkt_dts += diff_ts;
                if (frame->duration >= diff_ts)
                    frame->duration -= diff_ts;
            } else
                av_log(avctx, AV_LOG_WARNING, "Could not update timestamps for skipped samples.\n");

            av_log(avctx, AV_LOG_DEBUG, "skip %d/%d samples\n",
                   avci->skip_samples, frame->nb_samples);
            *discarded_samples += avci->skip_samples;
            frame->nb_samples -= avci->skip_samples;
            avci->skip_samples = 0;
        }
    }

    if (discard_padding > 0 && discard_padding <= frame->nb_samples) {
        if (discard_padding == frame->nb_samples) {
            *discarded_samples += frame->nb_samples;
            return AVERROR(EAGAIN);
        } else {
            if (avctx->pkt_timebase.num && avctx->sample_rate) {
                int64_t diff_ts = av_rescale_q(frame->nb_samples - discard_padding,
                                               (AVRational){1, avctx->sample_rate},
                                               avctx->pkt_timebase);
                frame->duration = diff_ts;
            } else
                av_log(avctx, AV_LOG_WARNING, "Could not update timestamps for discarded samples.\n");

            av_log(avctx, AV_LOG_DEBUG, "discard %d/%d samples\n",
                   (int)discard_padding, frame->nb_samples);
            frame->nb_samples -= discard_padding;
        }
    }

    return 0;
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
    DecodeContext     *dc = decode_ctx(avci);
    AVPacket     *const pkt = avci->in_pkt;
    const FFCodec *const codec = ffcodec(avctx->codec);
    int got_frame, consumed;
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
        !(avctx->codec->capabilities & AV_CODEC_CAP_DELAY))
        return AVERROR_EOF;

    got_frame = 0;

    frame->pict_type = dc->initial_pict_type;
    frame->flags    |= dc->intra_only_flag;
    consumed = codec->cb.decode(avctx, frame, &got_frame, pkt);

    if (!(codec->caps_internal & FF_CODEC_CAP_SETS_PKT_DTS))
        frame->pkt_dts = pkt->dts;
    if (avctx->codec->type == AVMEDIA_TYPE_VIDEO) {
#if FF_API_FRAME_PKT
FF_DISABLE_DEPRECATION_WARNINGS
        if(!avctx->has_b_frames)
            frame->pkt_pos = pkt->pos;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    }
    emms_c();

    if (avctx->codec->type == AVMEDIA_TYPE_VIDEO) {
        ret = (!got_frame || frame->flags & AV_FRAME_FLAG_DISCARD)
                          ? AVERROR(EAGAIN)
                          : 0;
    } else if (avctx->codec->type == AVMEDIA_TYPE_AUDIO) {
        ret =  !got_frame ? AVERROR(EAGAIN)
                          : discard_samples(avctx, frame, discarded_samples);
    } else
        av_assert0(0);

    if (ret == AVERROR(EAGAIN))
        av_frame_unref(frame);

    // FF_CODEC_CB_TYPE_DECODE decoders must not return AVERROR EAGAIN
    // code later will add AVERROR(EAGAIN) to a pointer
    av_assert0(consumed != AVERROR(EAGAIN));
    if (consumed < 0)
        ret = consumed;
    if (consumed >= 0 && avctx->codec->type == AVMEDIA_TYPE_VIDEO)
        consumed = pkt->size;

    if (!ret)
        av_assert0(frame->buf[0]);
    if (ret == AVERROR(EAGAIN))
        ret = 0;

    /* do not stop draining when got_frame != 0 or ret < 0 */
    if (avci->draining && !got_frame) {
        if (ret < 0) {
            /* prevent infinite loop if a decoder wrongly always return error on draining */
            /* reasonable nb_errors_max = maximum b frames + thread count */
            int nb_errors_max = 20 + (HAVE_THREADS && avctx->active_thread_type & FF_THREAD_FRAME ?
                                avctx->thread_count : 1);

            if (decode_ctx(avci)->nb_draining_errors++ >= nb_errors_max) {
                av_log(avctx, AV_LOG_ERROR, "Too many errors when draining, this is a bug. "
                       "Stop draining and force EOF.\n");
                avci->draining_done = 1;
                ret = AVERROR_BUG;
            }
        } else {
            avci->draining_done = 1;
        }
    }

    if (consumed >= pkt->size || ret < 0) {
        av_packet_unref(pkt);
    } else {
        pkt->data                += consumed;
        pkt->size                -= consumed;
        pkt->pts                  = AV_NOPTS_VALUE;
        pkt->dts                  = AV_NOPTS_VALUE;
        if (!(codec->caps_internal & FF_CODEC_CAP_SETS_FRAME_PROPS)) {
#if FF_API_FRAME_PKT
            // See extract_packet_props() comment.
            avci->last_pkt_props->stream_index = avci->last_pkt_props->stream_index - consumed;
#endif
            avci->last_pkt_props->pts = AV_NOPTS_VALUE;
            avci->last_pkt_props->dts = AV_NOPTS_VALUE;
        }
    }

    return ret;
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

    ret = ff_icc_profile_sanitize(&avci->icc, profile);
    if (!ret)
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

static int fill_frame_props(const AVCodecContext *avctx, AVFrame *frame)
{
    int ret;

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

    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (!frame->sample_aspect_ratio.num)  frame->sample_aspect_ratio = avctx->sample_aspect_ratio;
            if (frame->format == AV_PIX_FMT_NONE) frame->format              = avctx->pix_fmt;
    } else if (avctx->codec->type == AVMEDIA_TYPE_AUDIO) {
        if (frame->format == AV_SAMPLE_FMT_NONE)
            frame->format = avctx->sample_fmt;
        if (!frame->ch_layout.nb_channels) {
            ret = av_channel_layout_copy(&frame->ch_layout, &avctx->ch_layout);
            if (ret < 0)
                return ret;
        }
        if (!frame->sample_rate)
            frame->sample_rate = avctx->sample_rate;
    }

    return 0;
}

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

int ff_decode_receive_frame_internal(AVCodecContext *avctx, AVFrame *frame)
{
    AVCodecInternal *avci = avctx->internal;
    DecodeContext     *dc = decode_ctx(avci);
    const FFCodec *const codec = ffcodec(avctx->codec);
    int ret;

    av_assert0(!frame->buf[0]);

    if (codec->cb_type == FF_CODEC_CB_TYPE_RECEIVE_FRAME) {
        while (1) {
            frame->pict_type = dc->initial_pict_type;
            frame->flags    |= dc->intra_only_flag;
            ret = codec->cb.receive_frame(avctx, frame);
            emms_c();
            if (!ret) {
                if (avctx->codec->type == AVMEDIA_TYPE_AUDIO) {
                    int64_t discarded_samples = 0;
                    ret = discard_samples(avctx, frame, &discarded_samples);
                }
                if (ret == AVERROR(EAGAIN) || (frame->flags & AV_FRAME_FLAG_DISCARD)) {
                    av_frame_unref(frame);
                    continue;
                }
            }
            break;
        }
    } else
        ret = decode_simple_receive_frame(avctx, frame);

    if (ret == AVERROR_EOF)
        avci->draining_done = 1;

    return ret;
}

static int decode_receive_frame_internal(AVCodecContext *avctx, AVFrame *frame)
{
    AVCodecInternal *avci = avctx->internal;
    DecodeContext     *dc = decode_ctx(avci);
    int ret, ok;

    if (avctx->active_thread_type & FF_THREAD_FRAME)
        ret = ff_thread_receive_frame(avctx, frame);
    else
        ret = ff_decode_receive_frame_internal(avctx, frame);

    /* preserve ret */
    ok = detect_colorspace(avctx, frame);
    if (ok < 0) {
        av_frame_unref(frame);
        return ok;
    }

    if (!ret) {
        if (avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (!frame->width)
                frame->width = avctx->width;
            if (!frame->height)
                frame->height = avctx->height;
        }

        ret = fill_frame_props(avctx, frame);
        if (ret < 0) {
            av_frame_unref(frame);
            return ret;
        }

#if FF_API_FRAME_KEY
FF_DISABLE_DEPRECATION_WARNINGS
        frame->key_frame = !!(frame->flags & AV_FRAME_FLAG_KEY);
FF_ENABLE_DEPRECATION_WARNINGS
#endif
#if FF_API_INTERLACED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
        frame->interlaced_frame = !!(frame->flags & AV_FRAME_FLAG_INTERLACED);
        frame->top_field_first =  !!(frame->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST);
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        frame->best_effort_timestamp = guess_correct_pts(dc,
                                                         frame->pts,
                                                         frame->pkt_dts);

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
    DecodeContext     *dc = decode_ctx(avci);
    int ret;

    if (!avcodec_is_open(avctx) || !av_codec_is_decoder(avctx->codec))
        return AVERROR(EINVAL);

    if (dc->draining_started)
        return AVERROR_EOF;

    if (avpkt && !avpkt->size && avpkt->data)
        return AVERROR(EINVAL);

    if (avpkt && (avpkt->data || avpkt->side_data_elems)) {
        if (!AVPACKET_IS_EMPTY(avci->buffer_pkt))
            return AVERROR(EAGAIN);
        ret = av_packet_ref(avci->buffer_pkt, avpkt);
        if (ret < 0)
            return ret;
    } else
        dc->draining_started = 1;

    if (!avci->buffer_frame->buf[0] && !dc->draining_started) {
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
    int ret;

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

#if FF_API_DROPCHANGED
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
            int changed = avci->initial_format != frame->format;

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
#endif
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
    if (ffcodec(avctx->codec)->cb_type != FF_CODEC_CB_TYPE_DECODE_SUB) {
        av_log(avctx, AV_LOG_ERROR, "Codec not subtitle decoder\n");
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
    const FFHWAccel *hwa;
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

    if (!avctx->internal->hwaccel_priv_data) {
        avctx->internal->hwaccel_priv_data =
            av_mallocz(hwa->priv_data_size);
        if (!avctx->internal->hwaccel_priv_data) {
            av_buffer_unref(&frames_ref);
            return AVERROR(ENOMEM);
        }
    }

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
                        const FFHWAccel *hwaccel)
{
    int err;

    if (hwaccel->p.capabilities & AV_HWACCEL_CODEC_CAP_EXPERIMENTAL &&
        avctx->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL) {
        av_log(avctx, AV_LOG_WARNING, "Ignoring experimental hwaccel: %s\n",
               hwaccel->p.name);
        return AVERROR_PATCHWELCOME;
    }

    if (!avctx->internal->hwaccel_priv_data && hwaccel->priv_data_size) {
        avctx->internal->hwaccel_priv_data =
            av_mallocz(hwaccel->priv_data_size);
        if (!avctx->internal->hwaccel_priv_data)
            return AVERROR(ENOMEM);
    }

    avctx->hwaccel = &hwaccel->p;
    if (hwaccel->init) {
        err = hwaccel->init(avctx);
        if (err < 0) {
            av_log(avctx, AV_LOG_ERROR, "Failed setup for format %s: "
                   "hwaccel initialisation returned error.\n",
                   av_get_pix_fmt_name(hwaccel->p.pix_fmt));
            av_freep(&avctx->internal->hwaccel_priv_data);
            avctx->hwaccel = NULL;
            return err;
        }
    }

    return 0;
}

void ff_hwaccel_uninit(AVCodecContext *avctx)
{
    if (FF_HW_HAS_CB(avctx, uninit))
        FF_HW_SIMPLE_CALL(avctx, uninit);

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
        ff_hwaccel_uninit(avctx);

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
            av_log(avctx, AV_LOG_DEBUG, "Format %s requires hwaccel %s "
                   "initialisation.\n", desc->name, hw_config->hwaccel->p.name);
            err = hwaccel_init(avctx, hw_config->hwaccel);
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

    if (ret < 0)
        ff_hwaccel_uninit(avctx);

    av_freep(&choices);
    return ret;
}

static const AVPacketSideData*
packet_side_data_get(const AVPacketSideData *sd, int nb_sd,
                     enum AVPacketSideDataType type)
{
    for (int i = 0; i < nb_sd; i++)
        if (sd[i].type == type)
            return &sd[i];

    return NULL;
}

const AVPacketSideData *ff_get_coded_side_data(const AVCodecContext *avctx,
                                               enum AVPacketSideDataType type)
{
    return packet_side_data_get(avctx->coded_side_data, avctx->nb_coded_side_data, type);
}

static int side_data_stereo3d_merge(AVFrameSideData *sd_frame,
                                    const AVPacketSideData *sd_pkt)
{
    const AVStereo3D *src;
    AVStereo3D       *dst;
    int ret;

    ret = av_buffer_make_writable(&sd_frame->buf);
    if (ret < 0)
        return ret;
    sd_frame->data = sd_frame->buf->data;

    dst = (      AVStereo3D*)sd_frame->data;
    src = (const AVStereo3D*)sd_pkt->data;

    if (dst->type == AV_STEREO3D_UNSPEC)
        dst->type = src->type;

    if (dst->view == AV_STEREO3D_VIEW_UNSPEC)
        dst->view = src->view;

    if (dst->primary_eye == AV_PRIMARY_EYE_NONE)
        dst->primary_eye = src->primary_eye;

    if (!dst->baseline)
        dst->baseline = src->baseline;

    if (!dst->horizontal_disparity_adjustment.num)
        dst->horizontal_disparity_adjustment = src->horizontal_disparity_adjustment;

    if (!dst->horizontal_field_of_view.num)
        dst->horizontal_field_of_view = src->horizontal_field_of_view;

    return 0;
}

static int side_data_map(AVFrame *dst,
                         const AVPacketSideData *sd_src, int nb_sd_src,
                         const SideDataMap *map)

{
    for (int i = 0; map[i].packet < AV_PKT_DATA_NB; i++) {
        const enum AVPacketSideDataType type_pkt   = map[i].packet;
        const enum AVFrameSideDataType  type_frame = map[i].frame;
        const AVPacketSideData *sd_pkt;
        AVFrameSideData *sd_frame;

        sd_pkt = packet_side_data_get(sd_src, nb_sd_src, type_pkt);
        if (!sd_pkt)
            continue;

        sd_frame = av_frame_get_side_data(dst, type_frame);
        if (sd_frame) {
            if (type_frame == AV_FRAME_DATA_STEREO3D) {
                int ret = side_data_stereo3d_merge(sd_frame, sd_pkt);
                if (ret < 0)
                    return ret;
            }

            continue;
        }

        sd_frame = av_frame_new_side_data(dst, type_frame, sd_pkt->size);
        if (!sd_frame)
            return AVERROR(ENOMEM);

        memcpy(sd_frame->data, sd_pkt->data, sd_pkt->size);
    }

    return 0;
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
    static const SideDataMap sd[] = {
        { AV_PKT_DATA_A53_CC,                     AV_FRAME_DATA_A53_CC },
        { AV_PKT_DATA_AFD,                        AV_FRAME_DATA_AFD },
        { AV_PKT_DATA_DYNAMIC_HDR10_PLUS,         AV_FRAME_DATA_DYNAMIC_HDR_PLUS },
        { AV_PKT_DATA_S12M_TIMECODE,              AV_FRAME_DATA_S12M_TIMECODE },
        { AV_PKT_DATA_SKIP_SAMPLES,               AV_FRAME_DATA_SKIP_SAMPLES },
        { AV_PKT_DATA_LCEVC,                      AV_FRAME_DATA_LCEVC },
        { AV_PKT_DATA_NB }
    };

    int ret = 0;

    frame->pts          = pkt->pts;
    frame->duration     = pkt->duration;
#if FF_API_FRAME_PKT
FF_DISABLE_DEPRECATION_WARNINGS
    frame->pkt_pos      = pkt->pos;
    frame->pkt_size     = pkt->size;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    ret = side_data_map(frame, pkt->side_data, pkt->side_data_elems, ff_sd_global_map);
    if (ret < 0)
        return ret;

    ret = side_data_map(frame, pkt->side_data, pkt->side_data_elems, sd);
    if (ret < 0)
        return ret;

    add_metadata_from_side_data(pkt, frame);

    if (pkt->flags & AV_PKT_FLAG_DISCARD) {
        frame->flags |= AV_FRAME_FLAG_DISCARD;
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
    int ret;

    ret = side_data_map(frame, avctx->coded_side_data, avctx->nb_coded_side_data,
                        ff_sd_global_map);
    if (ret < 0)
        return ret;

    if (!(ffcodec(avctx->codec)->caps_internal & FF_CODEC_CAP_SETS_FRAME_PROPS)) {
        const AVPacket *pkt = avctx->internal->last_pkt_props;

        ret = ff_decode_frame_props_from_pkt(avctx, frame, pkt);
        if (ret < 0)
            return ret;
#if FF_API_FRAME_PKT
FF_DISABLE_DEPRECATION_WARNINGS
        frame->pkt_size = pkt->stream_index;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    }

    ret = fill_frame_props(avctx, frame);
    if (ret < 0)
        return ret;

    switch (avctx->codec->type) {
    case AVMEDIA_TYPE_VIDEO:
        if (frame->width && frame->height &&
            av_image_check_sar(frame->width, frame->height,
                               frame->sample_aspect_ratio) < 0) {
            av_log(avctx, AV_LOG_WARNING, "ignoring invalid SAR: %u/%u\n",
                   frame->sample_aspect_ratio.num,
                   frame->sample_aspect_ratio.den);
            frame->sample_aspect_ratio = (AVRational){ 0, 1 };
        }
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

static void update_frame_props(AVCodecContext *avctx, AVFrame *frame)
{
    AVCodecInternal    *avci = avctx->internal;
    DecodeContext        *dc = decode_ctx(avci);

    dc->lcevc_frame = dc->lcevc && avctx->codec_type == AVMEDIA_TYPE_VIDEO &&
                      av_frame_get_side_data(frame, AV_FRAME_DATA_LCEVC);

    if (dc->lcevc_frame) {
        dc->width     = frame->width;
        dc->height    = frame->height;
        frame->width  = frame->width  * 2 / FFMAX(frame->sample_aspect_ratio.den, 1);
        frame->height = frame->height * 2 / FFMAX(frame->sample_aspect_ratio.num, 1);
    }
}

static void attach_post_process_data(AVCodecContext *avctx, AVFrame *frame)
{
    AVCodecInternal    *avci = avctx->internal;
    DecodeContext        *dc = decode_ctx(avci);

    if (dc->lcevc_frame) {
        FrameDecodeData *fdd = (FrameDecodeData*)frame->private_ref->data;

        fdd->post_process_opaque = ff_refstruct_ref(dc->lcevc);
        fdd->post_process_opaque_free = ff_lcevc_unref;
        fdd->post_process = ff_lcevc_process;

        frame->width  = dc->width;
        frame->height = dc->height;
    }
    dc->lcevc_frame = 0;
}

int ff_get_buffer(AVCodecContext *avctx, AVFrame *frame, int flags)
{
    const FFHWAccel *hwaccel = ffhwaccel(avctx->hwaccel);
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
    } else {
        avctx->sw_pix_fmt = avctx->pix_fmt;
        update_frame_props(avctx, frame);
    }

    ret = avctx->get_buffer2(avctx, frame, flags);
    if (ret < 0)
        goto fail;

    validate_avframe_allocation(avctx, frame);

    ret = ff_attach_decode_data(frame);
    if (ret < 0)
        goto fail;

    attach_post_process_data(avctx, frame);

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

    // make sure the discard flag does not persist
    frame->flags &= ~AV_FRAME_FLAG_DISCARD;

    if (frame->data[0] && (frame->width != avctx->width || frame->height != avctx->height || frame->format != avctx->pix_fmt)) {
        av_log(avctx, AV_LOG_WARNING, "Picture changed from size:%dx%d fmt:%s to size:%dx%d fmt:%s in reget buffer()\n",
               frame->width, frame->height, av_get_pix_fmt_name(frame->format), avctx->width, avctx->height, av_get_pix_fmt_name(avctx->pix_fmt));
        av_frame_unref(frame);
    }

    if (!frame->data[0])
        return ff_get_buffer(avctx, frame, AV_GET_BUFFER_FLAG_REF);

    av_frame_side_data_free(&frame->side_data, &frame->nb_side_data);

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

typedef struct ProgressInternal {
    ThreadProgress progress;
    struct AVFrame *f;
} ProgressInternal;

static void check_progress_consistency(const ProgressFrame *f)
{
    av_assert1(!!f->f == !!f->progress);
    av_assert1(!f->progress || f->progress->f == f->f);
}

int ff_progress_frame_alloc(AVCodecContext *avctx, ProgressFrame *f)
{
    FFRefStructPool *pool = avctx->internal->progress_frame_pool;

    av_assert1(!f->f && !f->progress);

    f->progress = ff_refstruct_pool_get(pool);
    if (!f->progress)
        return AVERROR(ENOMEM);

    f->f = f->progress->f;
    return 0;
}

int ff_progress_frame_get_buffer(AVCodecContext *avctx, ProgressFrame *f, int flags)
{
    int ret;

    check_progress_consistency(f);
    if (!f->f) {
        ret = ff_progress_frame_alloc(avctx, f);
        if (ret < 0)
            return ret;
    }

    ret = ff_thread_get_buffer(avctx, f->progress->f, flags);
    if (ret < 0) {
        f->f = NULL;
        ff_refstruct_unref(&f->progress);
        return ret;
    }
    return 0;
}

void ff_progress_frame_ref(ProgressFrame *dst, const ProgressFrame *src)
{
    av_assert1(src->progress && src->f && src->f == src->progress->f);
    av_assert1(!dst->f && !dst->progress);
    dst->f = src->f;
    dst->progress = ff_refstruct_ref(src->progress);
}

void ff_progress_frame_unref(ProgressFrame *f)
{
    check_progress_consistency(f);
    f->f = NULL;
    ff_refstruct_unref(&f->progress);
}

void ff_progress_frame_replace(ProgressFrame *dst, const ProgressFrame *src)
{
    if (dst == src)
        return;
    ff_progress_frame_unref(dst);
    check_progress_consistency(src);
    if (src->f)
        ff_progress_frame_ref(dst, src);
}

void ff_progress_frame_report(ProgressFrame *f, int n)
{
    ff_thread_progress_report(&f->progress->progress, n);
}

void ff_progress_frame_await(const ProgressFrame *f, int n)
{
    ff_thread_progress_await(&f->progress->progress, n);
}

#if !HAVE_THREADS
enum ThreadingStatus ff_thread_sync_ref(AVCodecContext *avctx, size_t offset)
{
    return FF_THREAD_NO_FRAME_THREADING;
}
#endif /* !HAVE_THREADS */

static av_cold int progress_frame_pool_init_cb(FFRefStructOpaque opaque, void *obj)
{
    const AVCodecContext *avctx = opaque.nc;
    ProgressInternal *progress = obj;
    int ret;

    ret = ff_thread_progress_init(&progress->progress, avctx->active_thread_type & FF_THREAD_FRAME);
    if (ret < 0)
        return ret;

    progress->f = av_frame_alloc();
    if (!progress->f)
        return AVERROR(ENOMEM);

    return 0;
}

static void progress_frame_pool_reset_cb(FFRefStructOpaque unused, void *obj)
{
    ProgressInternal *progress = obj;

    ff_thread_progress_reset(&progress->progress);
    av_frame_unref(progress->f);
}

static av_cold void progress_frame_pool_free_entry_cb(FFRefStructOpaque opaque, void *obj)
{
    ProgressInternal *progress = obj;

    ff_thread_progress_destroy(&progress->progress);
    av_frame_free(&progress->f);
}

int ff_decode_preinit(AVCodecContext *avctx)
{
    AVCodecInternal *avci = avctx->internal;
    DecodeContext     *dc = decode_ctx(avci);
    int ret = 0;

    dc->initial_pict_type = AV_PICTURE_TYPE_NONE;
    if (avctx->codec_descriptor->props & AV_CODEC_PROP_INTRA_ONLY) {
        dc->intra_only_flag = AV_FRAME_FLAG_KEY;
        if (avctx->codec_type == AVMEDIA_TYPE_VIDEO)
            dc->initial_pict_type = AV_PICTURE_TYPE_I;
    }

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

    dc->pts_correction_num_faulty_pts =
    dc->pts_correction_num_faulty_dts = 0;
    dc->pts_correction_last_pts =
    dc->pts_correction_last_dts = INT64_MIN;

    if (   !CONFIG_GRAY && avctx->flags & AV_CODEC_FLAG_GRAY
        && avctx->codec_descriptor->type == AVMEDIA_TYPE_VIDEO)
        av_log(avctx, AV_LOG_WARNING,
               "gray decoding requested but not enabled at configuration time\n");
    if (avctx->flags2 & AV_CODEC_FLAG2_EXPORT_MVS) {
        avctx->export_side_data |= AV_CODEC_EXPORT_DATA_MVS;
    }

    if (avctx->nb_side_data_prefer_packet == 1 &&
        avctx->side_data_prefer_packet[0] == -1)
        dc->side_data_pref_mask = ~0ULL;
    else {
        for (unsigned i = 0; i < avctx->nb_side_data_prefer_packet; i++) {
            int val = avctx->side_data_prefer_packet[i];

            if (val < 0 || val >= AV_PKT_DATA_NB) {
                av_log(avctx, AV_LOG_ERROR, "Invalid side data type: %d\n", val);
                return AVERROR(EINVAL);
            }

            for (unsigned j = 0; ff_sd_global_map[j].packet < AV_PKT_DATA_NB; j++) {
                if (ff_sd_global_map[j].packet == val) {
                    val = ff_sd_global_map[j].frame;

                    // this code will need to be changed when we have more than
                    // 64 frame side data types
                    if (val >= 64) {
                        av_log(avctx, AV_LOG_ERROR, "Side data type too big\n");
                        return AVERROR_BUG;
                    }

                    dc->side_data_pref_mask |= 1ULL << val;
                }
            }
        }
    }

    avci->in_pkt         = av_packet_alloc();
    avci->last_pkt_props = av_packet_alloc();
    if (!avci->in_pkt || !avci->last_pkt_props)
        return AVERROR(ENOMEM);

    if (ffcodec(avctx->codec)->caps_internal & FF_CODEC_CAP_USES_PROGRESSFRAMES) {
        avci->progress_frame_pool =
            ff_refstruct_pool_alloc_ext(sizeof(ProgressInternal),
                                        FF_REFSTRUCT_POOL_FLAG_FREE_ON_INIT_ERROR,
                                        avctx, progress_frame_pool_init_cb,
                                        progress_frame_pool_reset_cb,
                                        progress_frame_pool_free_entry_cb, NULL);
        if (!avci->progress_frame_pool)
            return AVERROR(ENOMEM);
    }
    ret = decode_bsfs_init(avctx);
    if (ret < 0)
        return ret;

    if (!(avctx->export_side_data & AV_CODEC_EXPORT_DATA_ENHANCEMENTS)) {
        ret = ff_lcevc_alloc(&dc->lcevc);
        if (ret < 0 && (avctx->err_recognition & AV_EF_EXPLODE))
            return ret;
    }

#if FF_API_DROPCHANGED
    if (avctx->flags & AV_CODEC_FLAG_DROPCHANGED)
        av_log(avctx, AV_LOG_WARNING, "The dropchanged flag is deprecated.\n");
#endif

    return 0;
}

/**
 * Check side data preference and clear existing side data from frame
 * if needed.
 *
 * @retval 0 side data of this type can be added to frame
 * @retval 1 side data of this type should not be added to frame
 */
static int side_data_pref(const AVCodecContext *avctx, AVFrameSideData ***sd,
                          int *nb_sd, enum AVFrameSideDataType type)
{
    DecodeContext *dc = decode_ctx(avctx->internal);

    // Note: could be skipped for `type` without corresponding packet sd
    if (av_frame_side_data_get(*sd, *nb_sd, type)) {
        if (dc->side_data_pref_mask & (1ULL << type))
            return 1;
        av_frame_side_data_remove(sd, nb_sd, type);
    }

    return 0;
}


int ff_frame_new_side_data(const AVCodecContext *avctx, AVFrame *frame,
                           enum AVFrameSideDataType type, size_t size,
                           AVFrameSideData **psd)
{
    AVFrameSideData *sd;

    if (side_data_pref(avctx, &frame->side_data, &frame->nb_side_data, type)) {
        if (psd)
            *psd = NULL;
        return 0;
    }

    sd = av_frame_new_side_data(frame, type, size);
    if (psd)
        *psd = sd;

    return sd ? 0 : AVERROR(ENOMEM);
}

int ff_frame_new_side_data_from_buf_ext(const AVCodecContext *avctx,
                                        AVFrameSideData ***sd, int *nb_sd,
                                        enum AVFrameSideDataType type,
                                        AVBufferRef **buf)
{
    int ret = 0;

    if (side_data_pref(avctx, sd, nb_sd, type))
        goto finish;

    if (!av_frame_side_data_add(sd, nb_sd, type, buf, 0))
        ret = AVERROR(ENOMEM);

finish:
    av_buffer_unref(buf);

    return ret;
}

int ff_frame_new_side_data_from_buf(const AVCodecContext *avctx,
                                    AVFrame *frame, enum AVFrameSideDataType type,
                                    AVBufferRef **buf)
{
    return ff_frame_new_side_data_from_buf_ext(avctx,
                                               &frame->side_data, &frame->nb_side_data,
                                               type, buf);
}

int ff_decode_mastering_display_new_ext(const AVCodecContext *avctx,
                                        AVFrameSideData ***sd, int *nb_sd,
                                        struct AVMasteringDisplayMetadata **mdm)
{
    AVBufferRef *buf;
    size_t size;

    if (side_data_pref(avctx, sd, nb_sd, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA)) {
        *mdm = NULL;
        return 0;
    }

    *mdm = av_mastering_display_metadata_alloc_size(&size);
    if (!*mdm)
        return AVERROR(ENOMEM);

    buf = av_buffer_create((uint8_t *)*mdm, size, NULL, NULL, 0);
    if (!buf) {
        av_freep(mdm);
        return AVERROR(ENOMEM);
    }

    if (!av_frame_side_data_add(sd, nb_sd, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA,
                                &buf, 0)) {
        *mdm = NULL;
        av_buffer_unref(&buf);
        return AVERROR(ENOMEM);
    }

    return 0;
}

int ff_decode_mastering_display_new(const AVCodecContext *avctx, AVFrame *frame,
                                    AVMasteringDisplayMetadata **mdm)
{
    if (side_data_pref(avctx, &frame->side_data, &frame->nb_side_data,
                       AV_FRAME_DATA_MASTERING_DISPLAY_METADATA)) {
        *mdm = NULL;
        return 0;
    }

    *mdm = av_mastering_display_metadata_create_side_data(frame);
    return *mdm ? 0 : AVERROR(ENOMEM);
}

int ff_decode_content_light_new_ext(const AVCodecContext *avctx,
                                    AVFrameSideData ***sd, int *nb_sd,
                                    AVContentLightMetadata **clm)
{
    AVBufferRef *buf;
    size_t size;

    if (side_data_pref(avctx, sd, nb_sd, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL)) {
        *clm = NULL;
        return 0;
    }

    *clm = av_content_light_metadata_alloc(&size);
    if (!*clm)
        return AVERROR(ENOMEM);

    buf = av_buffer_create((uint8_t *)*clm, size, NULL, NULL, 0);
    if (!buf) {
        av_freep(clm);
        return AVERROR(ENOMEM);
    }

    if (!av_frame_side_data_add(sd, nb_sd, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL,
                                &buf, 0)) {
        *clm = NULL;
        av_buffer_unref(&buf);
        return AVERROR(ENOMEM);
    }

    return 0;
}

int ff_decode_content_light_new(const AVCodecContext *avctx, AVFrame *frame,
                                AVContentLightMetadata **clm)
{
    if (side_data_pref(avctx, &frame->side_data, &frame->nb_side_data,
                       AV_FRAME_DATA_CONTENT_LIGHT_LEVEL)) {
        *clm = NULL;
        return 0;
    }

    *clm = av_content_light_metadata_create_side_data(frame);
    return *clm ? 0 : AVERROR(ENOMEM);
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

int ff_hwaccel_frame_priv_alloc(AVCodecContext *avctx, void **hwaccel_picture_private)
{
    const FFHWAccel *hwaccel = ffhwaccel(avctx->hwaccel);

    if (!hwaccel || !hwaccel->frame_priv_data_size)
        return 0;

    av_assert0(!*hwaccel_picture_private);

    if (hwaccel->free_frame_priv) {
        AVHWFramesContext *frames_ctx;

        if (!avctx->hw_frames_ctx)
            return AVERROR(EINVAL);

        frames_ctx = (AVHWFramesContext *) avctx->hw_frames_ctx->data;
        *hwaccel_picture_private = ff_refstruct_alloc_ext(hwaccel->frame_priv_data_size, 0,
                                                          frames_ctx->device_ctx,
                                                          hwaccel->free_frame_priv);
    } else {
        *hwaccel_picture_private = ff_refstruct_allocz(hwaccel->frame_priv_data_size);
    }

    if (!*hwaccel_picture_private)
        return AVERROR(ENOMEM);

    return 0;
}

void ff_decode_flush_buffers(AVCodecContext *avctx)
{
    AVCodecInternal *avci = avctx->internal;
    DecodeContext     *dc = decode_ctx(avci);

    av_packet_unref(avci->last_pkt_props);
    av_packet_unref(avci->in_pkt);

    dc->pts_correction_last_pts =
    dc->pts_correction_last_dts = INT64_MIN;

    if (avci->bsf)
        av_bsf_flush(avci->bsf);

    dc->nb_draining_errors = 0;
    dc->draining_started   = 0;
}

AVCodecInternal *ff_decode_internal_alloc(void)
{
    return av_mallocz(sizeof(DecodeContext));
}

void ff_decode_internal_sync(AVCodecContext *dst, const AVCodecContext *src)
{
    const DecodeContext *src_dc = decode_ctx(src->internal);
    DecodeContext *dst_dc = decode_ctx(dst->internal);

    ff_refstruct_replace(&dst_dc->lcevc, src_dc->lcevc);
}

void ff_decode_internal_uninit(AVCodecContext *avctx)
{
    AVCodecInternal *avci = avctx->internal;
    DecodeContext *dc = decode_ctx(avci);

    ff_refstruct_unref(&dc->lcevc);
}
