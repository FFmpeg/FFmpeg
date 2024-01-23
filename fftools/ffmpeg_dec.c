/*
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

#include "libavutil/avassert.h"
#include "libavutil/dict.h"
#include "libavutil/error.h"
#include "libavutil/log.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"
#include "libavutil/time.h"
#include "libavutil/timestamp.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/codec.h"

#include "libavfilter/buffersrc.h"

#include "ffmpeg.h"
#include "ffmpeg_utils.h"
#include "thread_queue.h"

typedef struct DecoderPriv {
    Decoder          dec;

    AVCodecContext  *dec_ctx;

    AVFrame         *frame;
    AVPacket        *pkt;

    // override output video sample aspect ratio with this value
    AVRational       sar_override;

    AVRational       framerate_in;

    // a combination of DECODER_FLAG_*, provided to dec_open()
    int              flags;

    enum AVPixelFormat  hwaccel_pix_fmt;
    enum HWAccelID      hwaccel_id;
    enum AVHWDeviceType hwaccel_device_type;
    enum AVPixelFormat  hwaccel_output_format;

    // pts/estimated duration of the last decoded frame
    // * in decoder timebase for video,
    // * in last_frame_tb (may change during decoding) for audio
    int64_t         last_frame_pts;
    int64_t         last_frame_duration_est;
    AVRational      last_frame_tb;
    int64_t         last_filter_in_rescale_delta;
    int             last_frame_sample_rate;

    /* previous decoded subtitles */
    AVFrame *sub_prev[2];
    AVFrame *sub_heartbeat;

    Scheduler      *sch;
    unsigned        sch_idx;

    void           *log_parent;
    char            log_name[32];
    char           *parent_name;
} DecoderPriv;

static DecoderPriv *dp_from_dec(Decoder *d)
{
    return (DecoderPriv*)d;
}

// data that is local to the decoder thread and not visible outside of it
typedef struct DecThreadContext {
    AVFrame         *frame;
    AVPacket        *pkt;
} DecThreadContext;

void dec_free(Decoder **pdec)
{
    Decoder *dec = *pdec;
    DecoderPriv *dp;

    if (!dec)
        return;
    dp = dp_from_dec(dec);

    avcodec_free_context(&dp->dec_ctx);

    av_frame_free(&dp->frame);
    av_packet_free(&dp->pkt);

    for (int i = 0; i < FF_ARRAY_ELEMS(dp->sub_prev); i++)
        av_frame_free(&dp->sub_prev[i]);
    av_frame_free(&dp->sub_heartbeat);

    av_freep(&dp->parent_name);

    av_freep(pdec);
}

static int dec_alloc(DecoderPriv **pdec)
{
    DecoderPriv *dp;

    *pdec = NULL;

    dp = av_mallocz(sizeof(*dp));
    if (!dp)
        return AVERROR(ENOMEM);

    dp->frame = av_frame_alloc();
    if (!dp->frame)
        goto fail;

    dp->pkt = av_packet_alloc();
    if (!dp->pkt)
        goto fail;

    dp->last_filter_in_rescale_delta = AV_NOPTS_VALUE;
    dp->last_frame_pts               = AV_NOPTS_VALUE;
    dp->last_frame_tb                = (AVRational){ 1, 1 };
    dp->hwaccel_pix_fmt              = AV_PIX_FMT_NONE;

    *pdec = dp;

    return 0;
fail:
    dec_free((Decoder**)&dp);
    return AVERROR(ENOMEM);
}

static AVRational audio_samplerate_update(DecoderPriv *dp,
                                          const AVFrame *frame)
{
    const int prev = dp->last_frame_tb.den;
    const int sr   = frame->sample_rate;

    AVRational tb_new;
    int64_t gcd;

    if (frame->sample_rate == dp->last_frame_sample_rate)
        goto finish;

    gcd  = av_gcd(prev, sr);

    if (prev / gcd >= INT_MAX / sr) {
        av_log(dp, AV_LOG_WARNING,
               "Audio timestamps cannot be represented exactly after "
               "sample rate change: %d -> %d\n", prev, sr);

        // LCM of 192000, 44100, allows to represent all common samplerates
        tb_new = (AVRational){ 1, 28224000 };
    } else
        tb_new = (AVRational){ 1, prev / gcd * sr };

    // keep the frame timebase if it is strictly better than
    // the samplerate-defined one
    if (frame->time_base.num == 1 && frame->time_base.den > tb_new.den &&
        !(frame->time_base.den % tb_new.den))
        tb_new = frame->time_base;

    if (dp->last_frame_pts != AV_NOPTS_VALUE)
        dp->last_frame_pts = av_rescale_q(dp->last_frame_pts,
                                          dp->last_frame_tb, tb_new);
    dp->last_frame_duration_est = av_rescale_q(dp->last_frame_duration_est,
                                               dp->last_frame_tb, tb_new);

    dp->last_frame_tb          = tb_new;
    dp->last_frame_sample_rate = frame->sample_rate;

finish:
    return dp->last_frame_tb;
}

static void audio_ts_process(DecoderPriv *dp, AVFrame *frame)
{
    AVRational tb_filter = (AVRational){1, frame->sample_rate};
    AVRational tb;
    int64_t pts_pred;

    // on samplerate change, choose a new internal timebase for timestamp
    // generation that can represent timestamps from all the samplerates
    // seen so far
    tb = audio_samplerate_update(dp, frame);
    pts_pred = dp->last_frame_pts == AV_NOPTS_VALUE ? 0 :
               dp->last_frame_pts + dp->last_frame_duration_est;

    if (frame->pts == AV_NOPTS_VALUE) {
        frame->pts = pts_pred;
        frame->time_base = tb;
    } else if (dp->last_frame_pts != AV_NOPTS_VALUE &&
               frame->pts > av_rescale_q_rnd(pts_pred, tb, frame->time_base,
                                             AV_ROUND_UP)) {
        // there was a gap in timestamps, reset conversion state
        dp->last_filter_in_rescale_delta = AV_NOPTS_VALUE;
    }

    frame->pts = av_rescale_delta(frame->time_base, frame->pts,
                                  tb, frame->nb_samples,
                                  &dp->last_filter_in_rescale_delta, tb);

    dp->last_frame_pts          = frame->pts;
    dp->last_frame_duration_est = av_rescale_q(frame->nb_samples,
                                               tb_filter, tb);

    // finally convert to filtering timebase
    frame->pts       = av_rescale_q(frame->pts, tb, tb_filter);
    frame->duration  = frame->nb_samples;
    frame->time_base = tb_filter;
}

static int64_t video_duration_estimate(const DecoderPriv *dp, const AVFrame *frame)
{
    const int  ts_unreliable = dp->flags & DECODER_FLAG_TS_UNRELIABLE;
    const int      fr_forced = dp->flags & DECODER_FLAG_FRAMERATE_FORCED;
    int64_t codec_duration = 0;

    // XXX lavf currently makes up frame durations when they are not provided by
    // the container. As there is no way to reliably distinguish real container
    // durations from the fake made-up ones, we use heuristics based on whether
    // the container has timestamps. Eventually lavf should stop making up
    // durations, then this should be simplified.

    // prefer frame duration for containers with timestamps
    if (frame->duration > 0 && (!ts_unreliable || fr_forced))
        return frame->duration;

    if (dp->dec_ctx->framerate.den && dp->dec_ctx->framerate.num) {
        int fields = frame->repeat_pict + 2;
        AVRational field_rate = av_mul_q(dp->dec_ctx->framerate,
                                         (AVRational){ 2, 1 });
        codec_duration = av_rescale_q(fields, av_inv_q(field_rate),
                                      frame->time_base);
    }

    // prefer codec-layer duration for containers without timestamps
    if (codec_duration > 0 && ts_unreliable)
        return codec_duration;

    // when timestamps are available, repeat last frame's actual duration
    // (i.e. pts difference between this and last frame)
    if (frame->pts != AV_NOPTS_VALUE && dp->last_frame_pts != AV_NOPTS_VALUE &&
        frame->pts > dp->last_frame_pts)
        return frame->pts - dp->last_frame_pts;

    // try frame/codec duration
    if (frame->duration > 0)
        return frame->duration;
    if (codec_duration > 0)
        return codec_duration;

    // try average framerate
    if (dp->framerate_in.num && dp->framerate_in.den) {
        int64_t d = av_rescale_q(1, av_inv_q(dp->framerate_in),
                                 frame->time_base);
        if (d > 0)
            return d;
    }

    // last resort is last frame's estimated duration, and 1
    return FFMAX(dp->last_frame_duration_est, 1);
}

static int hwaccel_retrieve_data(AVCodecContext *avctx, AVFrame *input)
{
    DecoderPriv *dp = avctx->opaque;
    AVFrame *output = NULL;
    enum AVPixelFormat output_format = dp->hwaccel_output_format;
    int err;

    if (input->format == output_format) {
        // Nothing to do.
        return 0;
    }

    output = av_frame_alloc();
    if (!output)
        return AVERROR(ENOMEM);

    output->format = output_format;

    err = av_hwframe_transfer_data(output, input, 0);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to transfer data to "
               "output frame: %d.\n", err);
        goto fail;
    }

    err = av_frame_copy_props(output, input);
    if (err < 0) {
        av_frame_unref(output);
        goto fail;
    }

    av_frame_unref(input);
    av_frame_move_ref(input, output);
    av_frame_free(&output);

    return 0;

fail:
    av_frame_free(&output);
    return err;
}

static int video_frame_process(DecoderPriv *dp, AVFrame *frame)
{
#if FFMPEG_OPT_TOP
    if (dp->flags & DECODER_FLAG_TOP_FIELD_FIRST) {
        av_log(dp, AV_LOG_WARNING, "-top is deprecated, use the setfield filter instead\n");
        frame->flags |= AV_FRAME_FLAG_TOP_FIELD_FIRST;
    }
#endif

    if (frame->format == dp->hwaccel_pix_fmt) {
        int err = hwaccel_retrieve_data(dp->dec_ctx, frame);
        if (err < 0)
            return err;
    }

    frame->pts = frame->best_effort_timestamp;

    // forced fixed framerate
    if (dp->flags & DECODER_FLAG_FRAMERATE_FORCED) {
        frame->pts       = AV_NOPTS_VALUE;
        frame->duration  = 1;
        frame->time_base = av_inv_q(dp->framerate_in);
    }

    // no timestamp available - extrapolate from previous frame duration
    if (frame->pts == AV_NOPTS_VALUE)
        frame->pts = dp->last_frame_pts == AV_NOPTS_VALUE ? 0 :
                     dp->last_frame_pts + dp->last_frame_duration_est;

    // update timestamp history
    dp->last_frame_duration_est = video_duration_estimate(dp, frame);
    dp->last_frame_pts          = frame->pts;
    dp->last_frame_tb           = frame->time_base;

    if (debug_ts) {
        av_log(dp, AV_LOG_INFO,
               "decoder -> pts:%s pts_time:%s "
               "pkt_dts:%s pkt_dts_time:%s "
               "duration:%s duration_time:%s "
               "keyframe:%d frame_type:%d time_base:%d/%d\n",
               av_ts2str(frame->pts),
               av_ts2timestr(frame->pts, &frame->time_base),
               av_ts2str(frame->pkt_dts),
               av_ts2timestr(frame->pkt_dts, &frame->time_base),
               av_ts2str(frame->duration),
               av_ts2timestr(frame->duration, &frame->time_base),
               !!(frame->flags & AV_FRAME_FLAG_KEY), frame->pict_type,
               frame->time_base.num, frame->time_base.den);
    }

    if (dp->sar_override.num)
        frame->sample_aspect_ratio = dp->sar_override;

    return 0;
}

static int process_subtitle(DecoderPriv *dp, AVFrame *frame)
{
    const AVSubtitle *subtitle = (AVSubtitle*)frame->buf[0]->data;
    int ret = 0;

    if (dp->flags & DECODER_FLAG_FIX_SUB_DURATION) {
        AVSubtitle *sub_prev = dp->sub_prev[0]->buf[0] ?
                               (AVSubtitle*)dp->sub_prev[0]->buf[0]->data : NULL;
        int end = 1;
        if (sub_prev) {
            end = av_rescale(subtitle->pts - sub_prev->pts,
                             1000, AV_TIME_BASE);
            if (end < sub_prev->end_display_time) {
                av_log(dp, AV_LOG_DEBUG,
                       "Subtitle duration reduced from %"PRId32" to %d%s\n",
                       sub_prev->end_display_time, end,
                       end <= 0 ? ", dropping it" : "");
                sub_prev->end_display_time = end;
            }
        }

        av_frame_unref(dp->sub_prev[1]);
        av_frame_move_ref(dp->sub_prev[1], frame);

        frame    = dp->sub_prev[0];
        subtitle = frame->buf[0] ? (AVSubtitle*)frame->buf[0]->data : NULL;

        FFSWAP(AVFrame*, dp->sub_prev[0], dp->sub_prev[1]);

        if (end <= 0)
            return 0;
    }

    if (!subtitle)
        return 0;

    ret = sch_dec_send(dp->sch, dp->sch_idx, frame);
    if (ret < 0)
        av_frame_unref(frame);

    return ret == AVERROR_EOF ? AVERROR_EXIT : ret;
}

static int fix_sub_duration_heartbeat(DecoderPriv *dp, int64_t signal_pts)
{
    int ret = AVERROR_BUG;
    AVSubtitle *prev_subtitle = dp->sub_prev[0]->buf[0] ?
        (AVSubtitle*)dp->sub_prev[0]->buf[0]->data : NULL;
    AVSubtitle *subtitle;

    if (!(dp->flags & DECODER_FLAG_FIX_SUB_DURATION) || !prev_subtitle ||
        !prev_subtitle->num_rects || signal_pts <= prev_subtitle->pts)
        return 0;

    av_frame_unref(dp->sub_heartbeat);
    ret = subtitle_wrap_frame(dp->sub_heartbeat, prev_subtitle, 1);
    if (ret < 0)
        return ret;

    subtitle = (AVSubtitle*)dp->sub_heartbeat->buf[0]->data;
    subtitle->pts = signal_pts;

    return process_subtitle(dp, dp->sub_heartbeat);
}

static int transcode_subtitles(DecoderPriv *dp, const AVPacket *pkt,
                               AVFrame *frame)
{
    AVPacket *flush_pkt = NULL;
    AVSubtitle subtitle;
    int got_output;
    int ret;

    if (pkt && (intptr_t)pkt->opaque == PKT_OPAQUE_SUB_HEARTBEAT) {
        frame->pts       = pkt->pts;
        frame->time_base = pkt->time_base;
        frame->opaque    = (void*)(intptr_t)FRAME_OPAQUE_SUB_HEARTBEAT;

        ret = sch_dec_send(dp->sch, dp->sch_idx, frame);
        return ret == AVERROR_EOF ? AVERROR_EXIT : ret;
    } else if (pkt && (intptr_t)pkt->opaque == PKT_OPAQUE_FIX_SUB_DURATION) {
        return fix_sub_duration_heartbeat(dp, av_rescale_q(pkt->pts, pkt->time_base,
                                                           AV_TIME_BASE_Q));
    }

    if (!pkt) {
        flush_pkt = av_packet_alloc();
        if (!flush_pkt)
            return AVERROR(ENOMEM);
    }

    ret = avcodec_decode_subtitle2(dp->dec_ctx, &subtitle, &got_output,
                                   pkt ? pkt : flush_pkt);
    av_packet_free(&flush_pkt);

    if (ret < 0) {
        av_log(dp, AV_LOG_ERROR, "Error decoding subtitles: %s\n",
               av_err2str(ret));
        dp->dec.decode_errors++;
        return exit_on_error ? ret : 0;
    }

    if (!got_output)
        return pkt ? 0 : AVERROR_EOF;

    dp->dec.frames_decoded++;

    // XXX the queue for transferring data to consumers runs
    // on AVFrames, so we wrap AVSubtitle in an AVBufferRef and put that
    // inside the frame
    // eventually, subtitles should be switched to use AVFrames natively
    ret = subtitle_wrap_frame(frame, &subtitle, 0);
    if (ret < 0) {
        avsubtitle_free(&subtitle);
        return ret;
    }

    frame->width  = dp->dec_ctx->width;
    frame->height = dp->dec_ctx->height;

    return process_subtitle(dp, frame);
}

static int packet_decode(DecoderPriv *dp, AVPacket *pkt, AVFrame *frame)
{
    AVCodecContext *dec = dp->dec_ctx;
    const char *type_desc = av_get_media_type_string(dec->codec_type);
    int ret;

    if (dec->codec_type == AVMEDIA_TYPE_SUBTITLE)
        return transcode_subtitles(dp, pkt, frame);

    // With fate-indeo3-2, we're getting 0-sized packets before EOF for some
    // reason. This seems like a semi-critical bug. Don't trigger EOF, and
    // skip the packet.
    if (pkt && pkt->size == 0)
        return 0;

    if (pkt && (dp->flags & DECODER_FLAG_TS_UNRELIABLE)) {
        pkt->pts = AV_NOPTS_VALUE;
        pkt->dts = AV_NOPTS_VALUE;
    }

    if (pkt) {
        FrameData *fd = packet_data(pkt);
        if (!fd)
            return AVERROR(ENOMEM);
        fd->wallclock[LATENCY_PROBE_DEC_PRE] = av_gettime_relative();
    }

    ret = avcodec_send_packet(dec, pkt);
    if (ret < 0 && !(ret == AVERROR_EOF && !pkt)) {
        // In particular, we don't expect AVERROR(EAGAIN), because we read all
        // decoded frames with avcodec_receive_frame() until done.
        if (ret == AVERROR(EAGAIN)) {
            av_log(dp, AV_LOG_FATAL, "A decoder returned an unexpected error code. "
                                     "This is a bug, please report it.\n");
            return AVERROR_BUG;
        }
        av_log(dp, AV_LOG_ERROR, "Error submitting %s to decoder: %s\n",
               pkt ? "packet" : "EOF", av_err2str(ret));

        if (ret != AVERROR_EOF) {
            dp->dec.decode_errors++;
            if (!exit_on_error)
                ret = 0;
        }

        return ret;
    }

    while (1) {
        FrameData *fd;

        av_frame_unref(frame);

        update_benchmark(NULL);
        ret = avcodec_receive_frame(dec, frame);
        update_benchmark("decode_%s %s", type_desc, dp->parent_name);

        if (ret == AVERROR(EAGAIN)) {
            av_assert0(pkt); // should never happen during flushing
            return 0;
        } else if (ret == AVERROR_EOF) {
            return ret;
        } else if (ret < 0) {
            av_log(dp, AV_LOG_ERROR, "Decoding error: %s\n", av_err2str(ret));
            dp->dec.decode_errors++;

            if (exit_on_error)
                return ret;

            continue;
        }

        if (frame->decode_error_flags || (frame->flags & AV_FRAME_FLAG_CORRUPT)) {
            av_log(dp, exit_on_error ? AV_LOG_FATAL : AV_LOG_WARNING,
                   "corrupt decoded frame\n");
            if (exit_on_error)
                return AVERROR_INVALIDDATA;
        }

        fd      = frame_data(frame);
        if (!fd) {
            av_frame_unref(frame);
            return AVERROR(ENOMEM);
        }
        fd->dec.pts                 = frame->pts;
        fd->dec.tb                  = dec->pkt_timebase;
        fd->dec.frame_num           = dec->frame_num - 1;
        fd->bits_per_raw_sample     = dec->bits_per_raw_sample;

        fd->wallclock[LATENCY_PROBE_DEC_POST] = av_gettime_relative();

        frame->time_base = dec->pkt_timebase;

        if (dec->codec_type == AVMEDIA_TYPE_AUDIO) {
            dp->dec.samples_decoded += frame->nb_samples;

            audio_ts_process(dp, frame);
        } else {
            ret = video_frame_process(dp, frame);
            if (ret < 0) {
                av_log(dp, AV_LOG_FATAL,
                       "Error while processing the decoded data\n");
                return ret;
            }
        }

        dp->dec.frames_decoded++;

        ret = sch_dec_send(dp->sch, dp->sch_idx, frame);
        if (ret < 0) {
            av_frame_unref(frame);
            return ret == AVERROR_EOF ? AVERROR_EXIT : ret;
        }
    }
}

static void dec_thread_set_name(const DecoderPriv *dp)
{
    char name[16];
    snprintf(name, sizeof(name), "dec%s:%s", dp->parent_name,
             dp->dec_ctx->codec->name);
    ff_thread_setname(name);
}

static void dec_thread_uninit(DecThreadContext *dt)
{
    av_packet_free(&dt->pkt);
    av_frame_free(&dt->frame);

    memset(dt, 0, sizeof(*dt));
}

static int dec_thread_init(DecThreadContext *dt)
{
    memset(dt, 0, sizeof(*dt));

    dt->frame = av_frame_alloc();
    if (!dt->frame)
        goto fail;

    dt->pkt = av_packet_alloc();
    if (!dt->pkt)
        goto fail;

    return 0;

fail:
    dec_thread_uninit(dt);
    return AVERROR(ENOMEM);
}

static void *decoder_thread(void *arg)
{
    DecoderPriv  *dp = arg;
    DecThreadContext dt;
    int ret = 0, input_status = 0;

    ret = dec_thread_init(&dt);
    if (ret < 0)
        goto finish;

    dec_thread_set_name(dp);

    while (!input_status) {
        int flush_buffers, have_data;

        input_status  = sch_dec_receive(dp->sch, dp->sch_idx, dt.pkt);
        have_data     = input_status >= 0 &&
            (dt.pkt->buf || dt.pkt->side_data_elems ||
             (intptr_t)dt.pkt->opaque == PKT_OPAQUE_SUB_HEARTBEAT ||
             (intptr_t)dt.pkt->opaque == PKT_OPAQUE_FIX_SUB_DURATION);
        flush_buffers = input_status >= 0 && !have_data;
        if (!have_data)
            av_log(dp, AV_LOG_VERBOSE, "Decoder thread received %s packet\n",
                   flush_buffers ? "flush" : "EOF");

        ret = packet_decode(dp, have_data ? dt.pkt : NULL, dt.frame);

        av_packet_unref(dt.pkt);
        av_frame_unref(dt.frame);

        // AVERROR_EOF  - EOF from the decoder
        // AVERROR_EXIT - EOF from the scheduler
        // we treat them differently when flushing
        if (ret == AVERROR_EXIT) {
            ret = AVERROR_EOF;
            flush_buffers = 0;
        }

        if (ret == AVERROR_EOF) {
            av_log(dp, AV_LOG_VERBOSE, "Decoder returned EOF, %s\n",
                   flush_buffers ? "resetting" : "finishing");

            if (!flush_buffers)
                break;

            /* report last frame duration to the scheduler */
            if (dp->dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
                dt.pkt->pts       = dp->last_frame_pts + dp->last_frame_duration_est;
                dt.pkt->time_base = dp->last_frame_tb;
            }

            avcodec_flush_buffers(dp->dec_ctx);
        } else if (ret < 0) {
            av_log(dp, AV_LOG_ERROR, "Error processing packet in decoder: %s\n",
                   av_err2str(ret));
            break;
        }
    }

    // EOF is normal thread termination
    if (ret == AVERROR_EOF)
        ret = 0;

    // on success send EOF timestamp to our downstreams
    if (ret >= 0) {
        float err_rate;

        av_frame_unref(dt.frame);

        dt.frame->opaque    = (void*)(intptr_t)FRAME_OPAQUE_EOF;
        dt.frame->pts       = dp->last_frame_pts == AV_NOPTS_VALUE ? AV_NOPTS_VALUE :
                              dp->last_frame_pts + dp->last_frame_duration_est;
        dt.frame->time_base = dp->last_frame_tb;

        ret = sch_dec_send(dp->sch, dp->sch_idx, dt.frame);
        if (ret < 0 && ret != AVERROR_EOF) {
            av_log(dp, AV_LOG_FATAL,
                   "Error signalling EOF timestamp: %s\n", av_err2str(ret));
            goto finish;
        }
        ret = 0;

        err_rate = (dp->dec.frames_decoded || dp->dec.decode_errors) ?
                   dp->dec.decode_errors / (dp->dec.frames_decoded + dp->dec.decode_errors) : 0.f;
        if (err_rate > max_error_rate) {
            av_log(dp, AV_LOG_FATAL, "Decode error rate %g exceeds maximum %g\n",
                   err_rate, max_error_rate);
            ret = FFMPEG_ERROR_RATE_EXCEEDED;
        } else if (err_rate)
            av_log(dp, AV_LOG_VERBOSE, "Decode error rate %g\n", err_rate);
    }

finish:
    dec_thread_uninit(&dt);

    return (void*)(intptr_t)ret;
}

static enum AVPixelFormat get_format(AVCodecContext *s, const enum AVPixelFormat *pix_fmts)
{
    DecoderPriv  *dp = s->opaque;
    const enum AVPixelFormat *p;

    for (p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(*p);
        const AVCodecHWConfig  *config = NULL;
        int i;

        if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
            break;

        if (dp->hwaccel_id == HWACCEL_GENERIC ||
            dp->hwaccel_id == HWACCEL_AUTO) {
            for (i = 0;; i++) {
                config = avcodec_get_hw_config(s->codec, i);
                if (!config)
                    break;
                if (!(config->methods &
                      AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX))
                    continue;
                if (config->pix_fmt == *p)
                    break;
            }
        }
        if (config && config->device_type == dp->hwaccel_device_type) {
            dp->hwaccel_pix_fmt = *p;
            break;
        }
    }

    return *p;
}

static HWDevice *hw_device_match_by_codec(const AVCodec *codec)
{
    const AVCodecHWConfig *config;
    HWDevice *dev;
    int i;
    for (i = 0;; i++) {
        config = avcodec_get_hw_config(codec, i);
        if (!config)
            return NULL;
        if (!(config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX))
            continue;
        dev = hw_device_get_by_type(config->device_type);
        if (dev)
            return dev;
    }
}

static int hw_device_setup_for_decode(DecoderPriv *dp,
                                      const AVCodec *codec,
                                      const char *hwaccel_device)
{
    const AVCodecHWConfig *config;
    enum AVHWDeviceType type;
    HWDevice *dev = NULL;
    int err, auto_device = 0;

    if (hwaccel_device) {
        dev = hw_device_get_by_name(hwaccel_device);
        if (!dev) {
            if (dp->hwaccel_id == HWACCEL_AUTO) {
                auto_device = 1;
            } else if (dp->hwaccel_id == HWACCEL_GENERIC) {
                type = dp->hwaccel_device_type;
                err = hw_device_init_from_type(type, hwaccel_device,
                                               &dev);
            } else {
                // This will be dealt with by API-specific initialisation
                // (using hwaccel_device), so nothing further needed here.
                return 0;
            }
        } else {
            if (dp->hwaccel_id == HWACCEL_AUTO) {
                dp->hwaccel_device_type = dev->type;
            } else if (dp->hwaccel_device_type != dev->type) {
                av_log(dp, AV_LOG_ERROR, "Invalid hwaccel device "
                       "specified for decoder: device %s of type %s is not "
                       "usable with hwaccel %s.\n", dev->name,
                       av_hwdevice_get_type_name(dev->type),
                       av_hwdevice_get_type_name(dp->hwaccel_device_type));
                return AVERROR(EINVAL);
            }
        }
    } else {
        if (dp->hwaccel_id == HWACCEL_AUTO) {
            auto_device = 1;
        } else if (dp->hwaccel_id == HWACCEL_GENERIC) {
            type = dp->hwaccel_device_type;
            dev = hw_device_get_by_type(type);

            // When "-qsv_device device" is used, an internal QSV device named
            // as "__qsv_device" is created. Another QSV device is created too
            // if "-init_hw_device qsv=name:device" is used. There are 2 QSV devices
            // if both "-qsv_device device" and "-init_hw_device qsv=name:device"
            // are used, hw_device_get_by_type(AV_HWDEVICE_TYPE_QSV) returns NULL.
            // To keep back-compatibility with the removed ad-hoc libmfx setup code,
            // call hw_device_get_by_name("__qsv_device") to select the internal QSV
            // device.
            if (!dev && type == AV_HWDEVICE_TYPE_QSV)
                dev = hw_device_get_by_name("__qsv_device");

            if (!dev)
                err = hw_device_init_from_type(type, NULL, &dev);
        } else {
            dev = hw_device_match_by_codec(codec);
            if (!dev) {
                // No device for this codec, but not using generic hwaccel
                // and therefore may well not need one - ignore.
                return 0;
            }
        }
    }

    if (auto_device) {
        int i;
        if (!avcodec_get_hw_config(codec, 0)) {
            // Decoder does not support any hardware devices.
            return 0;
        }
        for (i = 0; !dev; i++) {
            config = avcodec_get_hw_config(codec, i);
            if (!config)
                break;
            type = config->device_type;
            dev = hw_device_get_by_type(type);
            if (dev) {
                av_log(dp, AV_LOG_INFO, "Using auto "
                       "hwaccel type %s with existing device %s.\n",
                       av_hwdevice_get_type_name(type), dev->name);
            }
        }
        for (i = 0; !dev; i++) {
            config = avcodec_get_hw_config(codec, i);
            if (!config)
                break;
            type = config->device_type;
            // Try to make a new device of this type.
            err = hw_device_init_from_type(type, hwaccel_device,
                                           &dev);
            if (err < 0) {
                // Can't make a device of this type.
                continue;
            }
            if (hwaccel_device) {
                av_log(dp, AV_LOG_INFO, "Using auto "
                       "hwaccel type %s with new device created "
                       "from %s.\n", av_hwdevice_get_type_name(type),
                       hwaccel_device);
            } else {
                av_log(dp, AV_LOG_INFO, "Using auto "
                       "hwaccel type %s with new default device.\n",
                       av_hwdevice_get_type_name(type));
            }
        }
        if (dev) {
            dp->hwaccel_device_type = type;
        } else {
            av_log(dp, AV_LOG_INFO, "Auto hwaccel "
                   "disabled: no device found.\n");
            dp->hwaccel_id = HWACCEL_NONE;
            return 0;
        }
    }

    if (!dev) {
        av_log(dp, AV_LOG_ERROR, "No device available "
               "for decoder: device type %s needed for codec %s.\n",
               av_hwdevice_get_type_name(type), codec->name);
        return err;
    }

    dp->dec_ctx->hw_device_ctx = av_buffer_ref(dev->device_ref);
    if (!dp->dec_ctx->hw_device_ctx)
        return AVERROR(ENOMEM);

    return 0;
}

static const char *dec_item_name(void *obj)
{
    const DecoderPriv *dp = obj;

    return dp->log_name;
}

static const AVClass dec_class = {
    .class_name                = "Decoder",
    .version                   = LIBAVUTIL_VERSION_INT,
    .parent_log_context_offset = offsetof(DecoderPriv, log_parent),
    .item_name                 = dec_item_name,
};

int dec_open(Decoder **pdec, Scheduler *sch,
             AVDictionary **dec_opts, const DecoderOpts *o)
{
    DecoderPriv *dp;
    const AVCodec *codec = o->codec;
    int ret;

    *pdec = NULL;

    ret = dec_alloc(&dp);
    if (ret < 0)
        return ret;

    ret = sch_add_dec(sch, decoder_thread, dp, o->flags & DECODER_FLAG_SEND_END_TS);
    if (ret < 0)
        return ret;
    dp->sch     = sch;
    dp->sch_idx = ret;

    dp->flags      = o->flags;
    dp->dec.class  = &dec_class;
    dp->log_parent = o->log_parent;

    dp->framerate_in            = o->framerate;

    dp->hwaccel_id              = o->hwaccel_id;
    dp->hwaccel_device_type     = o->hwaccel_device_type;
    dp->hwaccel_output_format   = o->hwaccel_output_format;

    snprintf(dp->log_name, sizeof(dp->log_name), "dec:%s", codec->name);

    dp->parent_name = av_strdup(o->name ? o->name : "");
    if (!dp->parent_name) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if (codec->type == AVMEDIA_TYPE_SUBTITLE &&
        (dp->flags & DECODER_FLAG_FIX_SUB_DURATION)) {
        for (int i = 0; i < FF_ARRAY_ELEMS(dp->sub_prev); i++) {
            dp->sub_prev[i] = av_frame_alloc();
            if (!dp->sub_prev[i]) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
        }
        dp->sub_heartbeat = av_frame_alloc();
        if (!dp->sub_heartbeat) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    dp->sar_override = o->par->sample_aspect_ratio;

    dp->dec_ctx = avcodec_alloc_context3(codec);
    if (!dp->dec_ctx) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = avcodec_parameters_to_context(dp->dec_ctx, o->par);
    if (ret < 0) {
        av_log(dp, AV_LOG_ERROR, "Error initializing the decoder context.\n");
        goto fail;
    }

    dp->dec_ctx->opaque                = dp;
    dp->dec_ctx->get_format            = get_format;
    dp->dec_ctx->pkt_timebase          = o->time_base;

    if (!av_dict_get(*dec_opts, "threads", NULL, 0))
        av_dict_set(dec_opts, "threads", "auto", 0);

    av_dict_set(dec_opts, "flags", "+copy_opaque", AV_DICT_MULTIKEY);

    ret = hw_device_setup_for_decode(dp, codec, o->hwaccel_device);
    if (ret < 0) {
        av_log(dp, AV_LOG_ERROR,
               "Hardware device setup failed for decoder: %s\n",
               av_err2str(ret));
        goto fail;
    }

    if ((ret = avcodec_open2(dp->dec_ctx, codec, dec_opts)) < 0) {
        av_log(dp, AV_LOG_ERROR, "Error while opening decoder: %s\n",
               av_err2str(ret));
        goto fail;
    }

    ret = check_avoptions(*dec_opts);
    if (ret < 0)
        goto fail;

    dp->dec.subtitle_header      = dp->dec_ctx->subtitle_header;
    dp->dec.subtitle_header_size = dp->dec_ctx->subtitle_header_size;

    *pdec = &dp->dec;

    return dp->sch_idx;
fail:
    dec_free((Decoder**)&dp);
    return ret;
}

int dec_add_filter(Decoder *dec, InputFilter *ifilter)
{
    DecoderPriv *dp = dp_from_dec(dec);

    // initialize fallback parameters for filtering
    return ifilter_parameters_from_dec(ifilter, dp->dec_ctx);
}
