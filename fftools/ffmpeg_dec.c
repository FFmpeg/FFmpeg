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
#include "libavutil/timestamp.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/codec.h"

#include "libavfilter/buffersrc.h"

#include "ffmpeg.h"
#include "thread_queue.h"

struct Decoder {
    AVFrame         *frame;
    AVPacket        *pkt;

    enum AVPixelFormat hwaccel_pix_fmt;

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

    pthread_t       thread;
    /**
     * Queue for sending coded packets from the main thread to
     * the decoder thread.
     *
     * An empty packet is sent to flush the decoder without terminating
     * decoding.
     */
    ThreadQueue    *queue_in;
    /**
     * Queue for sending decoded frames from the decoder thread
     * to the main thread.
     *
     * An empty frame is sent to signal that a single packet has been fully
     * processed.
     */
    ThreadQueue    *queue_out;
};

// data that is local to the decoder thread and not visible outside of it
typedef struct DecThreadContext {
    AVFrame         *frame;
    AVPacket        *pkt;
} DecThreadContext;

static int dec_thread_stop(Decoder *d)
{
    void *ret;

    if (!d->queue_in)
        return 0;

    tq_send_finish(d->queue_in, 0);
    tq_receive_finish(d->queue_out, 0);

    pthread_join(d->thread, &ret);

    tq_free(&d->queue_in);
    tq_free(&d->queue_out);

    return (intptr_t)ret;
}

void dec_free(Decoder **pdec)
{
    Decoder *dec = *pdec;

    if (!dec)
        return;

    dec_thread_stop(dec);

    av_frame_free(&dec->frame);
    av_packet_free(&dec->pkt);

    for (int i = 0; i < FF_ARRAY_ELEMS(dec->sub_prev); i++)
        av_frame_free(&dec->sub_prev[i]);
    av_frame_free(&dec->sub_heartbeat);

    av_freep(pdec);
}

static int dec_alloc(Decoder **pdec)
{
    Decoder *dec;

    *pdec = NULL;

    dec = av_mallocz(sizeof(*dec));
    if (!dec)
        return AVERROR(ENOMEM);

    dec->frame = av_frame_alloc();
    if (!dec->frame)
        goto fail;

    dec->pkt = av_packet_alloc();
    if (!dec->pkt)
        goto fail;

    dec->last_filter_in_rescale_delta = AV_NOPTS_VALUE;
    dec->last_frame_pts               = AV_NOPTS_VALUE;
    dec->last_frame_tb                = (AVRational){ 1, 1 };
    dec->hwaccel_pix_fmt              = AV_PIX_FMT_NONE;

    *pdec = dec;

    return 0;
fail:
    dec_free(&dec);
    return AVERROR(ENOMEM);
}

static int send_frame_to_filters(InputStream *ist, AVFrame *decoded_frame)
{
    int i, ret;

    av_assert1(ist->nb_filters > 0); /* ensure ret is initialized */
    for (i = 0; i < ist->nb_filters; i++) {
        ret = ifilter_send_frame(ist->filters[i], decoded_frame, i < ist->nb_filters - 1);
        if (ret == AVERROR_EOF)
            ret = 0; /* ignore */
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "Failed to inject frame into filter network: %s\n", av_err2str(ret));
            break;
        }
    }
    return ret;
}

static AVRational audio_samplerate_update(void *logctx, Decoder *d,
                                          const AVFrame *frame)
{
    const int prev = d->last_frame_tb.den;
    const int sr   = frame->sample_rate;

    AVRational tb_new;
    int64_t gcd;

    if (frame->sample_rate == d->last_frame_sample_rate)
        goto finish;

    gcd  = av_gcd(prev, sr);

    if (prev / gcd >= INT_MAX / sr) {
        av_log(logctx, AV_LOG_WARNING,
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

    if (d->last_frame_pts != AV_NOPTS_VALUE)
        d->last_frame_pts = av_rescale_q(d->last_frame_pts,
                                         d->last_frame_tb, tb_new);
    d->last_frame_duration_est = av_rescale_q(d->last_frame_duration_est,
                                              d->last_frame_tb, tb_new);

    d->last_frame_tb          = tb_new;
    d->last_frame_sample_rate = frame->sample_rate;

finish:
    return d->last_frame_tb;
}

static void audio_ts_process(void *logctx, Decoder *d, AVFrame *frame)
{
    AVRational tb_filter = (AVRational){1, frame->sample_rate};
    AVRational tb;
    int64_t pts_pred;

    // on samplerate change, choose a new internal timebase for timestamp
    // generation that can represent timestamps from all the samplerates
    // seen so far
    tb = audio_samplerate_update(logctx, d, frame);
    pts_pred = d->last_frame_pts == AV_NOPTS_VALUE ? 0 :
               d->last_frame_pts + d->last_frame_duration_est;

    if (frame->pts == AV_NOPTS_VALUE) {
        frame->pts = pts_pred;
        frame->time_base = tb;
    } else if (d->last_frame_pts != AV_NOPTS_VALUE &&
               frame->pts > av_rescale_q_rnd(pts_pred, tb, frame->time_base,
                                             AV_ROUND_UP)) {
        // there was a gap in timestamps, reset conversion state
        d->last_filter_in_rescale_delta = AV_NOPTS_VALUE;
    }

    frame->pts = av_rescale_delta(frame->time_base, frame->pts,
                                  tb, frame->nb_samples,
                                  &d->last_filter_in_rescale_delta, tb);

    d->last_frame_pts          = frame->pts;
    d->last_frame_duration_est = av_rescale_q(frame->nb_samples,
                                              tb_filter, tb);

    // finally convert to filtering timebase
    frame->pts       = av_rescale_q(frame->pts, tb, tb_filter);
    frame->duration  = frame->nb_samples;
    frame->time_base = tb_filter;
}

static int64_t video_duration_estimate(const InputStream *ist, const AVFrame *frame)
{
    const Decoder         *d = ist->decoder;
    const InputFile   *ifile = input_files[ist->file_index];
    int64_t codec_duration = 0;

    // XXX lavf currently makes up frame durations when they are not provided by
    // the container. As there is no way to reliably distinguish real container
    // durations from the fake made-up ones, we use heuristics based on whether
    // the container has timestamps. Eventually lavf should stop making up
    // durations, then this should be simplified.

    // prefer frame duration for containers with timestamps
    if (frame->duration > 0 && (!ifile->format_nots || ist->framerate.num))
        return frame->duration;

    if (ist->dec_ctx->framerate.den && ist->dec_ctx->framerate.num) {
        int fields = frame->repeat_pict + 2;
        AVRational field_rate = av_mul_q(ist->dec_ctx->framerate,
                                         (AVRational){ 2, 1 });
        codec_duration = av_rescale_q(fields, av_inv_q(field_rate),
                                      frame->time_base);
    }

    // prefer codec-layer duration for containers without timestamps
    if (codec_duration > 0 && ifile->format_nots)
        return codec_duration;

    // when timestamps are available, repeat last frame's actual duration
    // (i.e. pts difference between this and last frame)
    if (frame->pts != AV_NOPTS_VALUE && d->last_frame_pts != AV_NOPTS_VALUE &&
        frame->pts > d->last_frame_pts)
        return frame->pts - d->last_frame_pts;

    // try frame/codec duration
    if (frame->duration > 0)
        return frame->duration;
    if (codec_duration > 0)
        return codec_duration;

    // try average framerate
    if (ist->st->avg_frame_rate.num && ist->st->avg_frame_rate.den) {
        int64_t d = av_rescale_q(1, av_inv_q(ist->st->avg_frame_rate),
                                 frame->time_base);
        if (d > 0)
            return d;
    }

    // last resort is last frame's estimated duration, and 1
    return FFMAX(d->last_frame_duration_est, 1);
}

static int video_frame_process(InputStream *ist, AVFrame *frame)
{
    Decoder *d = ist->decoder;

    // The following line may be required in some cases where there is no parser
    // or the parser does not has_b_frames correctly
    if (ist->par->video_delay < ist->dec_ctx->has_b_frames) {
        if (ist->dec_ctx->codec_id == AV_CODEC_ID_H264) {
            ist->par->video_delay = ist->dec_ctx->has_b_frames;
        } else
            av_log(ist->dec_ctx, AV_LOG_WARNING,
                   "video_delay is larger in decoder than demuxer %d > %d.\n"
                   "If you want to help, upload a sample "
                   "of this file to https://streams.videolan.org/upload/ "
                   "and contact the ffmpeg-devel mailing list. (ffmpeg-devel@ffmpeg.org)\n",
                   ist->dec_ctx->has_b_frames,
                   ist->par->video_delay);
    }

    if (ist->dec_ctx->width  != frame->width ||
        ist->dec_ctx->height != frame->height ||
        ist->dec_ctx->pix_fmt != frame->format) {
        av_log(NULL, AV_LOG_DEBUG, "Frame parameters mismatch context %d,%d,%d != %d,%d,%d\n",
            frame->width,
            frame->height,
            frame->format,
            ist->dec_ctx->width,
            ist->dec_ctx->height,
            ist->dec_ctx->pix_fmt);
    }

#if FFMPEG_OPT_TOP
    if(ist->top_field_first>=0) {
        av_log(ist, AV_LOG_WARNING, "-top is deprecated, use the setfield filter instead\n");
        frame->flags |= AV_FRAME_FLAG_TOP_FIELD_FIRST;
    }
#endif

    if (frame->format == d->hwaccel_pix_fmt) {
        int err = hwaccel_retrieve_data(ist->dec_ctx, frame);
        if (err < 0)
            return err;
    }

    frame->pts = frame->best_effort_timestamp;

    // forced fixed framerate
    if (ist->framerate.num) {
        frame->pts       = AV_NOPTS_VALUE;
        frame->duration  = 1;
        frame->time_base = av_inv_q(ist->framerate);
    }

    // no timestamp available - extrapolate from previous frame duration
    if (frame->pts == AV_NOPTS_VALUE)
        frame->pts = d->last_frame_pts == AV_NOPTS_VALUE ? 0 :
                     d->last_frame_pts + d->last_frame_duration_est;

    // update timestamp history
    d->last_frame_duration_est = video_duration_estimate(ist, frame);
    d->last_frame_pts          = frame->pts;
    d->last_frame_tb           = frame->time_base;

    if (debug_ts) {
        av_log(ist, AV_LOG_INFO,
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

    if (ist->st->sample_aspect_ratio.num)
        frame->sample_aspect_ratio = ist->st->sample_aspect_ratio;

    return 0;
}

static void sub2video_flush(InputStream *ist)
{
    for (int i = 0; i < ist->nb_filters; i++) {
        int ret = ifilter_sub2video(ist->filters[i], NULL);
        if (ret != AVERROR_EOF && ret < 0)
            av_log(NULL, AV_LOG_WARNING, "Flush the frame error.\n");
    }
}

static int process_subtitle(InputStream *ist, AVFrame *frame)
{
    Decoder *d = ist->decoder;
    const AVSubtitle *subtitle = (AVSubtitle*)frame->buf[0]->data;
    int ret = 0;

    if (ist->fix_sub_duration) {
        AVSubtitle *sub_prev = d->sub_prev[0]->buf[0] ?
                               (AVSubtitle*)d->sub_prev[0]->buf[0]->data : NULL;
        int end = 1;
        if (sub_prev) {
            end = av_rescale(subtitle->pts - sub_prev->pts,
                             1000, AV_TIME_BASE);
            if (end < sub_prev->end_display_time) {
                av_log(NULL, AV_LOG_DEBUG,
                       "Subtitle duration reduced from %"PRId32" to %d%s\n",
                       sub_prev->end_display_time, end,
                       end <= 0 ? ", dropping it" : "");
                sub_prev->end_display_time = end;
            }
        }

        av_frame_unref(d->sub_prev[1]);
        av_frame_move_ref(d->sub_prev[1], frame);

        frame    = d->sub_prev[0];
        subtitle = frame->buf[0] ? (AVSubtitle*)frame->buf[0]->data : NULL;

        FFSWAP(AVFrame*, d->sub_prev[0], d->sub_prev[1]);

        if (end <= 0)
            return 0;
    }

    if (!subtitle)
        return 0;

    for (int i = 0; i < ist->nb_filters; i++) {
        ret = ifilter_sub2video(ist->filters[i], frame);
        if (ret < 0) {
            av_log(ist, AV_LOG_ERROR, "Error sending a subtitle for filtering: %s\n",
                   av_err2str(ret));
            return ret;
        }
    }

    subtitle = (AVSubtitle*)frame->buf[0]->data;
    if (!subtitle->num_rects)
        return 0;

    for (int oidx = 0; oidx < ist->nb_outputs; oidx++) {
        OutputStream *ost = ist->outputs[oidx];
        if (!ost->enc || ost->type != AVMEDIA_TYPE_SUBTITLE)
            continue;

        ret = enc_subtitle(output_files[ost->file_index], ost, subtitle);
        if (ret < 0)
            return ret;
    }

    return 0;
}

int fix_sub_duration_heartbeat(InputStream *ist, int64_t signal_pts)
{
    Decoder *d = ist->decoder;
    int ret = AVERROR_BUG;
    AVSubtitle *prev_subtitle = d->sub_prev[0]->buf[0] ?
        (AVSubtitle*)d->sub_prev[0]->buf[0]->data : NULL;
    AVSubtitle *subtitle;

    if (!ist->fix_sub_duration || !prev_subtitle ||
        !prev_subtitle->num_rects || signal_pts <= prev_subtitle->pts)
        return 0;

    av_frame_unref(d->sub_heartbeat);
    ret = subtitle_wrap_frame(d->sub_heartbeat, prev_subtitle, 1);
    if (ret < 0)
        return ret;

    subtitle = (AVSubtitle*)d->sub_heartbeat->buf[0]->data;
    subtitle->pts = signal_pts;

    return process_subtitle(ist, d->sub_heartbeat);
}

static int transcode_subtitles(InputStream *ist, const AVPacket *pkt,
                               AVFrame *frame)
{
    Decoder          *d = ist->decoder;
    AVPacket *flush_pkt = NULL;
    AVSubtitle subtitle;
    int got_output;
    int ret;

    if (!pkt) {
        flush_pkt = av_packet_alloc();
        if (!flush_pkt)
            return AVERROR(ENOMEM);
    }

    ret = avcodec_decode_subtitle2(ist->dec_ctx, &subtitle, &got_output,
                                   pkt ? pkt : flush_pkt);
    av_packet_free(&flush_pkt);

    if (ret < 0) {
        av_log(ist, AV_LOG_ERROR, "Error decoding subtitles: %s\n",
                av_err2str(ret));
        ist->decode_errors++;
        return exit_on_error ? ret : 0;
    }

    if (!got_output)
        return pkt ? 0 : AVERROR_EOF;

    ist->frames_decoded++;

    // XXX the queue for transferring data back to the main thread runs
    // on AVFrames, so we wrap AVSubtitle in an AVBufferRef and put that
    // inside the frame
    // eventually, subtitles should be switched to use AVFrames natively
    ret = subtitle_wrap_frame(frame, &subtitle, 0);
    if (ret < 0) {
        avsubtitle_free(&subtitle);
        return ret;
    }

    frame->width  = ist->dec_ctx->width;
    frame->height = ist->dec_ctx->height;

    ret = tq_send(d->queue_out, 0, frame);
    if (ret < 0)
        av_frame_unref(frame);

    return ret;
}

static int send_filter_eof(InputStream *ist)
{
    Decoder *d = ist->decoder;
    int i, ret;

    for (i = 0; i < ist->nb_filters; i++) {
        int64_t end_pts = d->last_frame_pts == AV_NOPTS_VALUE ? AV_NOPTS_VALUE :
                          d->last_frame_pts + d->last_frame_duration_est;
        ret = ifilter_send_eof(ist->filters[i], end_pts, d->last_frame_tb);
        if (ret < 0)
            return ret;
    }
    return 0;
}

static int packet_decode(InputStream *ist, AVPacket *pkt, AVFrame *frame)
{
    const InputFile *ifile = input_files[ist->file_index];
    Decoder *d = ist->decoder;
    AVCodecContext *dec = ist->dec_ctx;
    const char *type_desc = av_get_media_type_string(dec->codec_type);
    int ret;

    if (dec->codec_type == AVMEDIA_TYPE_SUBTITLE)
        return transcode_subtitles(ist, pkt, frame);

    // With fate-indeo3-2, we're getting 0-sized packets before EOF for some
    // reason. This seems like a semi-critical bug. Don't trigger EOF, and
    // skip the packet.
    if (pkt && pkt->size == 0)
        return 0;

    if (pkt && ifile->format_nots) {
        pkt->pts = AV_NOPTS_VALUE;
        pkt->dts = AV_NOPTS_VALUE;
    }

    ret = avcodec_send_packet(dec, pkt);
    if (ret < 0 && !(ret == AVERROR_EOF && !pkt)) {
        // In particular, we don't expect AVERROR(EAGAIN), because we read all
        // decoded frames with avcodec_receive_frame() until done.
        if (ret == AVERROR(EAGAIN)) {
            av_log(ist, AV_LOG_FATAL, "A decoder returned an unexpected error code. "
                                      "This is a bug, please report it.\n");
            return AVERROR_BUG;
        }
        av_log(ist, AV_LOG_ERROR, "Error submitting %s to decoder: %s\n",
               pkt ? "packet" : "EOF", av_err2str(ret));

        if (ret != AVERROR_EOF) {
            ist->decode_errors++;
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
        update_benchmark("decode_%s %d.%d", type_desc,
                         ist->file_index, ist->index);

        if (ret == AVERROR(EAGAIN)) {
            av_assert0(pkt); // should never happen during flushing
            return 0;
        } else if (ret == AVERROR_EOF) {
            return ret;
        } else if (ret < 0) {
            av_log(ist, AV_LOG_ERROR, "Decoding error: %s\n", av_err2str(ret));
            ist->decode_errors++;

            if (exit_on_error)
                return ret;

            continue;
        }

        if (frame->decode_error_flags || (frame->flags & AV_FRAME_FLAG_CORRUPT)) {
            av_log(ist, exit_on_error ? AV_LOG_FATAL : AV_LOG_WARNING,
                   "corrupt decoded frame\n");
            if (exit_on_error)
                return AVERROR_INVALIDDATA;
        }


        av_assert0(!frame->opaque_ref);
        fd      = frame_data(frame);
        if (!fd) {
            av_frame_unref(frame);
            return AVERROR(ENOMEM);
        }
        fd->dec.pts                 = frame->pts;
        fd->dec.tb                  = dec->pkt_timebase;
        fd->dec.frame_num           = dec->frame_num - 1;
        fd->bits_per_raw_sample     = dec->bits_per_raw_sample;

        frame->time_base = dec->pkt_timebase;

        if (dec->codec_type == AVMEDIA_TYPE_AUDIO) {
            ist->samples_decoded += frame->nb_samples;
            ist->nb_samples       = frame->nb_samples;

            audio_ts_process(ist, ist->decoder, frame);
        } else {
            ret = video_frame_process(ist, frame);
            if (ret < 0) {
                av_log(NULL, AV_LOG_FATAL, "Error while processing the decoded "
                       "data for stream #%d:%d\n", ist->file_index, ist->index);
                return ret;
            }
        }

        ist->frames_decoded++;

        ret = tq_send(d->queue_out, 0, frame);
        if (ret < 0)
            return ret;
    }
}

static void dec_thread_set_name(const InputStream *ist)
{
    char name[16];
    snprintf(name, sizeof(name), "dec%d:%d:%s", ist->file_index, ist->index,
             ist->dec_ctx->codec->name);
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
    InputStream *ist = arg;
    InputFile *ifile = input_files[ist->file_index];
    Decoder       *d = ist->decoder;
    DecThreadContext dt;
    int ret = 0, input_status = 0;

    ret = dec_thread_init(&dt);
    if (ret < 0)
        goto finish;

    dec_thread_set_name(ist);

    while (!input_status) {
        int dummy, flush_buffers;

        input_status = tq_receive(d->queue_in, &dummy, dt.pkt);
        flush_buffers = input_status >= 0 && !dt.pkt->buf;
        if (!dt.pkt->buf)
            av_log(ist, AV_LOG_VERBOSE, "Decoder thread received %s packet\n",
                   flush_buffers ? "flush" : "EOF");

        ret = packet_decode(ist, dt.pkt->buf ? dt.pkt : NULL, dt.frame);

        av_packet_unref(dt.pkt);
        av_frame_unref(dt.frame);

        if (ret == AVERROR_EOF) {
            av_log(ist, AV_LOG_VERBOSE, "Decoder returned EOF, %s\n",
                   flush_buffers ? "resetting" : "finishing");

            if (!flush_buffers)
                break;

            /* report last frame duration to the demuxer thread */
            if (ist->dec->type == AVMEDIA_TYPE_AUDIO) {
                LastFrameDuration dur;

                dur.stream_idx = ist->index;
                dur.duration   = av_rescale_q(ist->nb_samples,
                                              (AVRational){ 1, ist->dec_ctx->sample_rate},
                                              ist->st->time_base);

                av_thread_message_queue_send(ifile->audio_duration_queue, &dur, 0);
            }

            avcodec_flush_buffers(ist->dec_ctx);
        } else if (ret < 0) {
            av_log(ist, AV_LOG_ERROR, "Error processing packet in decoder: %s\n",
                   av_err2str(ret));
            break;
        }

        // signal to the consumer thread that the entire packet was processed
        ret = tq_send(d->queue_out, 0, dt.frame);
        if (ret < 0) {
            if (ret != AVERROR_EOF)
                av_log(ist, AV_LOG_ERROR, "Error communicating with the main thread\n");
            break;
        }
    }

    // EOF is normal thread termination
    if (ret == AVERROR_EOF)
        ret = 0;

finish:
    tq_receive_finish(d->queue_in,  0);
    tq_send_finish   (d->queue_out, 0);

    // make sure the demuxer does not get stuck waiting for audio durations
    // that will never arrive
    if (ifile->audio_duration_queue && ist->dec->type == AVMEDIA_TYPE_AUDIO)
        av_thread_message_queue_set_err_recv(ifile->audio_duration_queue, AVERROR_EOF);

    dec_thread_uninit(&dt);

    av_log(ist, AV_LOG_VERBOSE, "Terminating decoder thread\n");

    return (void*)(intptr_t)ret;
}

int dec_packet(InputStream *ist, const AVPacket *pkt, int no_eof)
{
    Decoder *d = ist->decoder;
    int ret = 0, thread_ret;

    // thread already joined
    if (!d->queue_in)
        return AVERROR_EOF;

    // send the packet/flush request/EOF to the decoder thread
    if (pkt || no_eof) {
        av_packet_unref(d->pkt);

        if (pkt) {
            ret = av_packet_ref(d->pkt, pkt);
            if (ret < 0)
                goto finish;
        }

        ret = tq_send(d->queue_in, 0, d->pkt);
        if (ret < 0)
            goto finish;
    } else
        tq_send_finish(d->queue_in, 0);

    // retrieve all decoded data for the packet
    while (1) {
        int dummy;

        ret = tq_receive(d->queue_out, &dummy, d->frame);
        if (ret < 0)
            goto finish;

        // packet fully processed
        if (!d->frame->buf[0])
            return 0;

        // process the decoded frame
        if (ist->dec->type == AVMEDIA_TYPE_SUBTITLE) {
            ret = process_subtitle(ist, d->frame);
        } else {
            ret = send_frame_to_filters(ist, d->frame);
        }
        av_frame_unref(d->frame);
        if (ret < 0)
            goto finish;
    }

finish:
    thread_ret = dec_thread_stop(d);
    if (thread_ret < 0) {
        av_log(ist, AV_LOG_ERROR, "Decoder thread returned error: %s\n",
               av_err2str(thread_ret));
        ret = err_merge(ret, thread_ret);
    }
    // non-EOF errors here are all fatal
    if (ret < 0 && ret != AVERROR_EOF)
        return ret;

    // signal EOF to our downstreams
    if (ist->dec->type == AVMEDIA_TYPE_SUBTITLE)
        sub2video_flush(ist);
    else {
        ret = send_filter_eof(ist);
        if (ret < 0) {
            av_log(NULL, AV_LOG_FATAL, "Error marking filters as finished\n");
            return ret;
        }
    }

    return AVERROR_EOF;
}

static int dec_thread_start(InputStream *ist)
{
    Decoder *d = ist->decoder;
    ObjPool *op;
    int ret = 0;

    op = objpool_alloc_packets();
    if (!op)
        return AVERROR(ENOMEM);

    d->queue_in = tq_alloc(1, 1, op, pkt_move);
    if (!d->queue_in) {
        objpool_free(&op);
        return AVERROR(ENOMEM);
    }

    op = objpool_alloc_frames();
    if (!op)
        goto fail;

    d->queue_out = tq_alloc(1, 4, op, frame_move);
    if (!d->queue_out) {
        objpool_free(&op);
        goto fail;
    }

    ret = pthread_create(&d->thread, NULL, decoder_thread, ist);
    if (ret) {
        ret = AVERROR(ret);
        av_log(ist, AV_LOG_ERROR, "pthread_create() failed: %s\n",
               av_err2str(ret));
        goto fail;
    }

    return 0;
fail:
    if (ret >= 0)
        ret = AVERROR(ENOMEM);

    tq_free(&d->queue_in);
    tq_free(&d->queue_out);
    return ret;
}

static enum AVPixelFormat get_format(AVCodecContext *s, const enum AVPixelFormat *pix_fmts)
{
    InputStream *ist = s->opaque;
    Decoder       *d = ist->decoder;
    const enum AVPixelFormat *p;

    for (p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(*p);
        const AVCodecHWConfig  *config = NULL;
        int i;

        if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
            break;

        if (ist->hwaccel_id == HWACCEL_GENERIC ||
            ist->hwaccel_id == HWACCEL_AUTO) {
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
        if (config && config->device_type == ist->hwaccel_device_type) {
            d->hwaccel_pix_fmt = *p;
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

static int hw_device_setup_for_decode(InputStream *ist)
{
    const AVCodecHWConfig *config;
    enum AVHWDeviceType type;
    HWDevice *dev = NULL;
    int err, auto_device = 0;

    if (ist->hwaccel_device) {
        dev = hw_device_get_by_name(ist->hwaccel_device);
        if (!dev) {
            if (ist->hwaccel_id == HWACCEL_AUTO) {
                auto_device = 1;
            } else if (ist->hwaccel_id == HWACCEL_GENERIC) {
                type = ist->hwaccel_device_type;
                err = hw_device_init_from_type(type, ist->hwaccel_device,
                                               &dev);
            } else {
                // This will be dealt with by API-specific initialisation
                // (using hwaccel_device), so nothing further needed here.
                return 0;
            }
        } else {
            if (ist->hwaccel_id == HWACCEL_AUTO) {
                ist->hwaccel_device_type = dev->type;
            } else if (ist->hwaccel_device_type != dev->type) {
                av_log(NULL, AV_LOG_ERROR, "Invalid hwaccel device "
                       "specified for decoder: device %s of type %s is not "
                       "usable with hwaccel %s.\n", dev->name,
                       av_hwdevice_get_type_name(dev->type),
                       av_hwdevice_get_type_name(ist->hwaccel_device_type));
                return AVERROR(EINVAL);
            }
        }
    } else {
        if (ist->hwaccel_id == HWACCEL_AUTO) {
            auto_device = 1;
        } else if (ist->hwaccel_id == HWACCEL_GENERIC) {
            type = ist->hwaccel_device_type;
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
            dev = hw_device_match_by_codec(ist->dec);
            if (!dev) {
                // No device for this codec, but not using generic hwaccel
                // and therefore may well not need one - ignore.
                return 0;
            }
        }
    }

    if (auto_device) {
        int i;
        if (!avcodec_get_hw_config(ist->dec, 0)) {
            // Decoder does not support any hardware devices.
            return 0;
        }
        for (i = 0; !dev; i++) {
            config = avcodec_get_hw_config(ist->dec, i);
            if (!config)
                break;
            type = config->device_type;
            dev = hw_device_get_by_type(type);
            if (dev) {
                av_log(NULL, AV_LOG_INFO, "Using auto "
                       "hwaccel type %s with existing device %s.\n",
                       av_hwdevice_get_type_name(type), dev->name);
            }
        }
        for (i = 0; !dev; i++) {
            config = avcodec_get_hw_config(ist->dec, i);
            if (!config)
                break;
            type = config->device_type;
            // Try to make a new device of this type.
            err = hw_device_init_from_type(type, ist->hwaccel_device,
                                           &dev);
            if (err < 0) {
                // Can't make a device of this type.
                continue;
            }
            if (ist->hwaccel_device) {
                av_log(NULL, AV_LOG_INFO, "Using auto "
                       "hwaccel type %s with new device created "
                       "from %s.\n", av_hwdevice_get_type_name(type),
                       ist->hwaccel_device);
            } else {
                av_log(NULL, AV_LOG_INFO, "Using auto "
                       "hwaccel type %s with new default device.\n",
                       av_hwdevice_get_type_name(type));
            }
        }
        if (dev) {
            ist->hwaccel_device_type = type;
        } else {
            av_log(NULL, AV_LOG_INFO, "Auto hwaccel "
                   "disabled: no device found.\n");
            ist->hwaccel_id = HWACCEL_NONE;
            return 0;
        }
    }

    if (!dev) {
        av_log(NULL, AV_LOG_ERROR, "No device available "
               "for decoder: device type %s needed for codec %s.\n",
               av_hwdevice_get_type_name(type), ist->dec->name);
        return err;
    }

    ist->dec_ctx->hw_device_ctx = av_buffer_ref(dev->device_ref);
    if (!ist->dec_ctx->hw_device_ctx)
        return AVERROR(ENOMEM);

    return 0;
}

int dec_open(InputStream *ist)
{
    Decoder *d;
    const AVCodec *codec = ist->dec;
    int ret;

    if (!codec) {
        av_log(ist, AV_LOG_ERROR,
               "Decoding requested, but no decoder found for: %s\n",
                avcodec_get_name(ist->dec_ctx->codec_id));
        return AVERROR(EINVAL);
    }

    ret = dec_alloc(&ist->decoder);
    if (ret < 0)
        return ret;
    d = ist->decoder;

    if (codec->type == AVMEDIA_TYPE_SUBTITLE && ist->fix_sub_duration) {
        for (int i = 0; i < FF_ARRAY_ELEMS(d->sub_prev); i++) {
            d->sub_prev[i] = av_frame_alloc();
            if (!d->sub_prev[i])
                return AVERROR(ENOMEM);
        }
        d->sub_heartbeat = av_frame_alloc();
        if (!d->sub_heartbeat)
            return AVERROR(ENOMEM);
    }

    ist->dec_ctx->opaque                = ist;
    ist->dec_ctx->get_format            = get_format;

    if (ist->dec_ctx->codec_id == AV_CODEC_ID_DVB_SUBTITLE &&
       (ist->decoding_needed & DECODING_FOR_OST)) {
        av_dict_set(&ist->decoder_opts, "compute_edt", "1", AV_DICT_DONT_OVERWRITE);
        if (ist->decoding_needed & DECODING_FOR_FILTER)
            av_log(NULL, AV_LOG_WARNING, "Warning using DVB subtitles for filtering and output at the same time is not fully supported, also see -compute_edt [0|1]\n");
    }

    /* Useful for subtitles retiming by lavf (FIXME), skipping samples in
     * audio, and video decoders such as cuvid or mediacodec */
    ist->dec_ctx->pkt_timebase = ist->st->time_base;

    if (!av_dict_get(ist->decoder_opts, "threads", NULL, 0))
        av_dict_set(&ist->decoder_opts, "threads", "auto", 0);
    /* Attached pics are sparse, therefore we would not want to delay their decoding till EOF. */
    if (ist->st->disposition & AV_DISPOSITION_ATTACHED_PIC)
        av_dict_set(&ist->decoder_opts, "threads", "1", 0);

    ret = hw_device_setup_for_decode(ist);
    if (ret < 0) {
        av_log(ist, AV_LOG_ERROR,
               "Hardware device setup failed for decoder: %s\n",
               av_err2str(ret));
        return ret;
    }

    if ((ret = avcodec_open2(ist->dec_ctx, codec, &ist->decoder_opts)) < 0) {
        av_log(ist, AV_LOG_ERROR, "Error while opening decoder: %s\n",
               av_err2str(ret));
        return ret;
    }

    ret = check_avoptions(ist->decoder_opts);
    if (ret < 0)
        return ret;

    ret = dec_thread_start(ist);
    if (ret < 0) {
        av_log(ist, AV_LOG_ERROR, "Error starting decoder thread: %s\n",
               av_err2str(ret));
        return ret;
    }

    return 0;
}
