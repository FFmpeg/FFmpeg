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

#include <float.h>
#include <stdint.h>

#include "ffmpeg.h"

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/display.h"
#include "libavutil/error.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/time.h"
#include "libavutil/timestamp.h"
#include "libavutil/thread.h"
#include "libavutil/threadmessage.h"

#include "libavcodec/packet.h"

#include "libavformat/avformat.h"

static const char *const opt_name_discard[]                   = {"discard", NULL};
static const char *const opt_name_reinit_filters[]            = {"reinit_filter", NULL};
static const char *const opt_name_fix_sub_duration[]          = {"fix_sub_duration", NULL};
static const char *const opt_name_canvas_sizes[]              = {"canvas_size", NULL};
static const char *const opt_name_guess_layout_max[]          = {"guess_layout_max", NULL};
static const char *const opt_name_ts_scale[]                  = {"itsscale", NULL};
static const char *const opt_name_hwaccels[]                  = {"hwaccel", NULL};
static const char *const opt_name_hwaccel_devices[]           = {"hwaccel_device", NULL};
static const char *const opt_name_hwaccel_output_formats[]    = {"hwaccel_output_format", NULL};
static const char *const opt_name_autorotate[]                = {"autorotate", NULL};
static const char *const opt_name_display_rotations[]         = {"display_rotation", NULL};
static const char *const opt_name_display_hflips[]            = {"display_hflip", NULL};
static const char *const opt_name_display_vflips[]            = {"display_vflip", NULL};

typedef struct DemuxStream {
    InputStream ist;

    // name used for logging
    char log_name[32];

    double ts_scale;

    int streamcopy_needed;

    int wrap_correction_done;
    int saw_first_ts;
    ///< dts of the first packet read for this stream (in AV_TIME_BASE units)
    int64_t first_dts;

    /* predicted dts of the next packet read for this stream or (when there are
     * several frames in a packet) of the next frame in current packet (in AV_TIME_BASE units) */
    int64_t       next_dts;
    ///< dts of the last packet read for this stream (in AV_TIME_BASE units)
    int64_t       dts;

    int64_t min_pts; /* pts with the smallest value in a current stream */
    int64_t max_pts; /* pts with the higher value in a current stream */

    /* number of packets successfully read for this stream */
    uint64_t nb_packets;
    // combined size of all the packets read
    uint64_t data_size;
} DemuxStream;

typedef struct Demuxer {
    InputFile f;

    // name used for logging
    char log_name[32];

    int64_t wallclock_start;

    /**
     * Extra timestamp offset added by discontinuity handling.
     */
    int64_t ts_offset_discont;
    int64_t last_ts;

    /* number of times input stream should be looped */
    int loop;
    /* actual duration of the longest stream in a file at the moment when
     * looping happens */
    int64_t duration;
    /* time base of the duration */
    AVRational time_base;

    /* number of streams that the user was warned of */
    int nb_streams_warn;

    double readrate_initial_burst;

    AVThreadMessageQueue *in_thread_queue;
    int                   thread_queue_size;
    pthread_t             thread;
    int                   non_blocking;

    int                   read_started;
} Demuxer;

typedef struct DemuxMsg {
    AVPacket *pkt;
    int looping;
} DemuxMsg;

static DemuxStream *ds_from_ist(InputStream *ist)
{
    return (DemuxStream*)ist;
}

static Demuxer *demuxer_from_ifile(InputFile *f)
{
    return (Demuxer*)f;
}

InputStream *ist_find_unused(enum AVMediaType type)
{
    for (InputStream *ist = ist_iter(NULL); ist; ist = ist_iter(ist)) {
        if (ist->par->codec_type == type && ist->discard &&
            ist->user_set_discard != AVDISCARD_ALL)
            return ist;
    }
    return NULL;
}

static void report_new_stream(Demuxer *d, const AVPacket *pkt)
{
    AVStream *st = d->f.ctx->streams[pkt->stream_index];

    if (pkt->stream_index < d->nb_streams_warn)
        return;
    av_log(d, AV_LOG_WARNING,
           "New %s stream with index %d at pos:%"PRId64" and DTS:%ss\n",
           av_get_media_type_string(st->codecpar->codec_type),
           pkt->stream_index, pkt->pos, av_ts2timestr(pkt->dts, &st->time_base));
    d->nb_streams_warn = pkt->stream_index + 1;
}

static void ifile_duration_update(Demuxer *d, DemuxStream *ds,
                                  int64_t last_duration)
{
    /* the total duration of the stream, max_pts - min_pts is
     * the duration of the stream without the last frame */
    if (ds->max_pts > ds->min_pts &&
        ds->max_pts - (uint64_t)ds->min_pts < INT64_MAX - last_duration)
        last_duration += ds->max_pts - ds->min_pts;

    if (!d->duration ||
        av_compare_ts(d->duration, d->time_base,
                      last_duration, ds->ist.st->time_base) < 0) {
        d->duration = last_duration;
        d->time_base = ds->ist.st->time_base;
    }
}

static int seek_to_start(Demuxer *d)
{
    InputFile    *ifile = &d->f;
    AVFormatContext *is = ifile->ctx;
    int ret;

    ret = avformat_seek_file(is, -1, INT64_MIN, is->start_time, is->start_time, 0);
    if (ret < 0)
        return ret;

    if (ifile->audio_duration_queue_size) {
        /* duration is the length of the last frame in a stream
         * when audio stream is present we don't care about
         * last video frame length because it's not defined exactly */
        int got_durations = 0;

        while (got_durations < ifile->audio_duration_queue_size) {
            DemuxStream *ds;
            LastFrameDuration dur;
            ret = av_thread_message_queue_recv(ifile->audio_duration_queue, &dur, 0);
            if (ret < 0)
                return ret;
            got_durations++;

            ds = ds_from_ist(ifile->streams[dur.stream_idx]);
            ifile_duration_update(d, ds, dur.duration);
        }
    } else {
        for (int i = 0; i < ifile->nb_streams; i++) {
            int64_t duration = 0;
            InputStream *ist = ifile->streams[i];
            DemuxStream *ds  = ds_from_ist(ist);

            if (ist->framerate.num) {
                duration = av_rescale_q(1, av_inv_q(ist->framerate), ist->st->time_base);
            } else if (ist->st->avg_frame_rate.num) {
                duration = av_rescale_q(1, av_inv_q(ist->st->avg_frame_rate), ist->st->time_base);
            } else {
                duration = 1;
            }

            ifile_duration_update(d, ds, duration);
        }
    }

    if (d->loop > 0)
        d->loop--;

    return ret;
}

static void ts_discontinuity_detect(Demuxer *d, InputStream *ist,
                                    AVPacket *pkt)
{
    InputFile *ifile = &d->f;
    DemuxStream *ds = ds_from_ist(ist);
    const int fmt_is_discont = ifile->ctx->iformat->flags & AVFMT_TS_DISCONT;
    int disable_discontinuity_correction = copy_ts;
    int64_t pkt_dts = av_rescale_q_rnd(pkt->dts, pkt->time_base, AV_TIME_BASE_Q,
                                       AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);

    if (copy_ts && ds->next_dts != AV_NOPTS_VALUE &&
        fmt_is_discont && ist->st->pts_wrap_bits < 60) {
        int64_t wrap_dts = av_rescale_q_rnd(pkt->dts + (1LL<<ist->st->pts_wrap_bits),
                                            pkt->time_base, AV_TIME_BASE_Q,
                                            AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
        if (FFABS(wrap_dts - ds->next_dts) < FFABS(pkt_dts - ds->next_dts)/10)
            disable_discontinuity_correction = 0;
    }

    if (ds->next_dts != AV_NOPTS_VALUE && !disable_discontinuity_correction) {
        int64_t delta = pkt_dts - ds->next_dts;
        if (fmt_is_discont) {
            if (FFABS(delta) > 1LL * dts_delta_threshold * AV_TIME_BASE ||
                pkt_dts + AV_TIME_BASE/10 < ds->dts) {
                d->ts_offset_discont -= delta;
                av_log(ist, AV_LOG_WARNING,
                       "timestamp discontinuity "
                       "(stream id=%d): %"PRId64", new offset= %"PRId64"\n",
                       ist->st->id, delta, d->ts_offset_discont);
                pkt->dts -= av_rescale_q(delta, AV_TIME_BASE_Q, pkt->time_base);
                if (pkt->pts != AV_NOPTS_VALUE)
                    pkt->pts -= av_rescale_q(delta, AV_TIME_BASE_Q, pkt->time_base);
            }
        } else {
            if (FFABS(delta) > 1LL * dts_error_threshold * AV_TIME_BASE) {
                av_log(NULL, AV_LOG_WARNING,
                       "DTS %"PRId64", next:%"PRId64" st:%d invalid dropping\n",
                       pkt->dts, ds->next_dts, pkt->stream_index);
                pkt->dts = AV_NOPTS_VALUE;
            }
            if (pkt->pts != AV_NOPTS_VALUE){
                int64_t pkt_pts = av_rescale_q(pkt->pts, pkt->time_base, AV_TIME_BASE_Q);
                delta = pkt_pts - ds->next_dts;
                if (FFABS(delta) > 1LL * dts_error_threshold * AV_TIME_BASE) {
                    av_log(NULL, AV_LOG_WARNING,
                           "PTS %"PRId64", next:%"PRId64" invalid dropping st:%d\n",
                           pkt->pts, ds->next_dts, pkt->stream_index);
                    pkt->pts = AV_NOPTS_VALUE;
                }
            }
        }
    } else if (ds->next_dts == AV_NOPTS_VALUE && !copy_ts &&
               fmt_is_discont && d->last_ts != AV_NOPTS_VALUE) {
        int64_t delta = pkt_dts - d->last_ts;
        if (FFABS(delta) > 1LL * dts_delta_threshold * AV_TIME_BASE) {
            d->ts_offset_discont -= delta;
            av_log(NULL, AV_LOG_DEBUG,
                   "Inter stream timestamp discontinuity %"PRId64", new offset= %"PRId64"\n",
                   delta, d->ts_offset_discont);
            pkt->dts -= av_rescale_q(delta, AV_TIME_BASE_Q, pkt->time_base);
            if (pkt->pts != AV_NOPTS_VALUE)
                pkt->pts -= av_rescale_q(delta, AV_TIME_BASE_Q, pkt->time_base);
        }
    }

    d->last_ts = av_rescale_q(pkt->dts, pkt->time_base, AV_TIME_BASE_Q);
}

static void ts_discontinuity_process(Demuxer *d, InputStream *ist,
                                     AVPacket *pkt)
{
    int64_t offset = av_rescale_q(d->ts_offset_discont, AV_TIME_BASE_Q,
                                  pkt->time_base);

    // apply previously-detected timestamp-discontinuity offset
    // (to all streams, not just audio/video)
    if (pkt->dts != AV_NOPTS_VALUE)
        pkt->dts += offset;
    if (pkt->pts != AV_NOPTS_VALUE)
        pkt->pts += offset;

    // detect timestamp discontinuities for audio/video
    if ((ist->par->codec_type == AVMEDIA_TYPE_VIDEO ||
         ist->par->codec_type == AVMEDIA_TYPE_AUDIO) &&
        pkt->dts != AV_NOPTS_VALUE)
        ts_discontinuity_detect(d, ist, pkt);
}

static int ist_dts_update(DemuxStream *ds, AVPacket *pkt)
{
    InputStream *ist = &ds->ist;
    const AVCodecParameters *par = ist->par;

    if (!ds->saw_first_ts) {
        ds->first_dts =
        ds->dts = ist->st->avg_frame_rate.num ? - ist->par->video_delay * AV_TIME_BASE / av_q2d(ist->st->avg_frame_rate) : 0;
        if (pkt->pts != AV_NOPTS_VALUE) {
            ds->first_dts =
            ds->dts += av_rescale_q(pkt->pts, pkt->time_base, AV_TIME_BASE_Q);
        }
        ds->saw_first_ts = 1;
    }

    if (ds->next_dts == AV_NOPTS_VALUE)
        ds->next_dts = ds->dts;

    if (pkt->dts != AV_NOPTS_VALUE)
        ds->next_dts = ds->dts = av_rescale_q(pkt->dts, pkt->time_base, AV_TIME_BASE_Q);

    ds->dts = ds->next_dts;
    switch (par->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        av_assert1(pkt->duration >= 0);
        if (par->sample_rate) {
            ds->next_dts += ((int64_t)AV_TIME_BASE * par->frame_size) /
                              par->sample_rate;
        } else {
            ds->next_dts += av_rescale_q(pkt->duration, pkt->time_base, AV_TIME_BASE_Q);
        }
        break;
    case AVMEDIA_TYPE_VIDEO:
        if (ist->framerate.num) {
            // TODO: Remove work-around for c99-to-c89 issue 7
            AVRational time_base_q = AV_TIME_BASE_Q;
            int64_t next_dts = av_rescale_q(ds->next_dts, time_base_q, av_inv_q(ist->framerate));
            ds->next_dts = av_rescale_q(next_dts + 1, av_inv_q(ist->framerate), time_base_q);
        } else if (pkt->duration) {
            ds->next_dts += av_rescale_q(pkt->duration, pkt->time_base, AV_TIME_BASE_Q);
        } else if (ist->par->framerate.num != 0) {
            AVRational field_rate = av_mul_q(ist->par->framerate,
                                             (AVRational){ 2, 1 });
            int fields = 2;

            if (ist->codec_desc                                 &&
                (ist->codec_desc->props & AV_CODEC_PROP_FIELDS) &&
                av_stream_get_parser(ist->st))
                fields = 1 + av_stream_get_parser(ist->st)->repeat_pict;

            ds->next_dts += av_rescale_q(fields, av_inv_q(field_rate), AV_TIME_BASE_Q);
        }
        break;
    }

    av_assert0(!pkt->opaque_ref);
    if (ds->streamcopy_needed) {
        DemuxPktData *pd;

        pkt->opaque_ref = av_buffer_allocz(sizeof(*pd));
        if (!pkt->opaque_ref)
            return AVERROR(ENOMEM);
        pd = (DemuxPktData*)pkt->opaque_ref->data;

        pd->dts_est = ds->dts;
    }

    return 0;
}

static int ts_fixup(Demuxer *d, AVPacket *pkt)
{
    InputFile *ifile = &d->f;
    InputStream *ist = ifile->streams[pkt->stream_index];
    DemuxStream  *ds = ds_from_ist(ist);
    const int64_t start_time = ifile->start_time_effective;
    int64_t duration;
    int ret;

    pkt->time_base = ist->st->time_base;

#define SHOW_TS_DEBUG(tag_)                                             \
    if (debug_ts) {                                                     \
        av_log(ist, AV_LOG_INFO, "%s -> ist_index:%d:%d type:%s "       \
               "pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s duration:%s duration_time:%s\n", \
               tag_, ifile->index, pkt->stream_index,                   \
               av_get_media_type_string(ist->st->codecpar->codec_type), \
               av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &pkt->time_base), \
               av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &pkt->time_base), \
               av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, &pkt->time_base)); \
    }

    SHOW_TS_DEBUG("demuxer");

    if (!ds->wrap_correction_done && start_time != AV_NOPTS_VALUE &&
        ist->st->pts_wrap_bits < 64) {
        int64_t stime, stime2;

        stime = av_rescale_q(start_time, AV_TIME_BASE_Q, pkt->time_base);
        stime2= stime + (1ULL<<ist->st->pts_wrap_bits);
        ds->wrap_correction_done = 1;

        if(stime2 > stime && pkt->dts != AV_NOPTS_VALUE && pkt->dts > stime + (1LL<<(ist->st->pts_wrap_bits-1))) {
            pkt->dts -= 1ULL<<ist->st->pts_wrap_bits;
            ds->wrap_correction_done = 0;
        }
        if(stime2 > stime && pkt->pts != AV_NOPTS_VALUE && pkt->pts > stime + (1LL<<(ist->st->pts_wrap_bits-1))) {
            pkt->pts -= 1ULL<<ist->st->pts_wrap_bits;
            ds->wrap_correction_done = 0;
        }
    }

    if (pkt->dts != AV_NOPTS_VALUE)
        pkt->dts += av_rescale_q(ifile->ts_offset, AV_TIME_BASE_Q, pkt->time_base);
    if (pkt->pts != AV_NOPTS_VALUE)
        pkt->pts += av_rescale_q(ifile->ts_offset, AV_TIME_BASE_Q, pkt->time_base);

    if (pkt->pts != AV_NOPTS_VALUE)
        pkt->pts *= ds->ts_scale;
    if (pkt->dts != AV_NOPTS_VALUE)
        pkt->dts *= ds->ts_scale;

    duration = av_rescale_q(d->duration, d->time_base, pkt->time_base);
    if (pkt->pts != AV_NOPTS_VALUE) {
        pkt->pts += duration;
        ds->max_pts = FFMAX(pkt->pts, ds->max_pts);
        ds->min_pts = FFMIN(pkt->pts, ds->min_pts);
    }

    if (pkt->dts != AV_NOPTS_VALUE)
        pkt->dts += duration;

    SHOW_TS_DEBUG("demuxer+tsfixup");

    // detect and try to correct for timestamp discontinuities
    ts_discontinuity_process(d, ist, pkt);

    // update estimated/predicted dts
    ret = ist_dts_update(ds, pkt);
    if (ret < 0)
        return ret;

    return 0;
}

// process an input packet into a message to send to the consumer thread
// src is always cleared by this function
static int input_packet_process(Demuxer *d, DemuxMsg *msg, AVPacket *src)
{
    InputFile     *f = &d->f;
    InputStream *ist = f->streams[src->stream_index];
    DemuxStream  *ds = ds_from_ist(ist);
    AVPacket *pkt;
    int ret = 0;

    pkt = av_packet_alloc();
    if (!pkt) {
        av_packet_unref(src);
        return AVERROR(ENOMEM);
    }
    av_packet_move_ref(pkt, src);

    ret = ts_fixup(d, pkt);
    if (ret < 0)
        goto fail;

    ds->data_size += pkt->size;
    ds->nb_packets++;

    /* add the stream-global side data to the first packet */
    if (ds->nb_packets == 1) {
        for (int i = 0; i < ist->st->nb_side_data; i++) {
            AVPacketSideData *src_sd = &ist->st->side_data[i];
            uint8_t *dst_data;

            if (src_sd->type == AV_PKT_DATA_DISPLAYMATRIX)
                continue;

            if (av_packet_get_side_data(pkt, src_sd->type, NULL))
                continue;

            dst_data = av_packet_new_side_data(pkt, src_sd->type, src_sd->size);
            if (!dst_data) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }

            memcpy(dst_data, src_sd->data, src_sd->size);
        }
    }

    if (debug_ts) {
        av_log(NULL, AV_LOG_INFO, "demuxer+ffmpeg -> ist_index:%d:%d type:%s pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s duration:%s duration_time:%s off:%s off_time:%s\n",
               f->index, pkt->stream_index,
               av_get_media_type_string(ist->par->codec_type),
               av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &pkt->time_base),
               av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &pkt->time_base),
               av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, &pkt->time_base),
               av_ts2str(input_files[ist->file_index]->ts_offset),
               av_ts2timestr(input_files[ist->file_index]->ts_offset, &AV_TIME_BASE_Q));
    }

    msg->pkt = pkt;
    pkt      = NULL;

fail:
    av_packet_free(&pkt);

    return ret;
}

static void readrate_sleep(Demuxer *d)
{
    InputFile *f = &d->f;
    int64_t file_start = copy_ts * (
                          (f->start_time_effective != AV_NOPTS_VALUE ? f->start_time_effective * !start_at_zero : 0) +
                          (f->start_time != AV_NOPTS_VALUE ? f->start_time : 0)
                         );
    int64_t burst_until = AV_TIME_BASE * d->readrate_initial_burst;
    for (int i = 0; i < f->nb_streams; i++) {
        InputStream *ist = f->streams[i];
        DemuxStream  *ds = ds_from_ist(ist);
        int64_t stream_ts_offset, pts, now;
        stream_ts_offset = FFMAX(ds->first_dts != AV_NOPTS_VALUE ? ds->first_dts : 0, file_start);
        pts = av_rescale(ds->dts, 1000000, AV_TIME_BASE);
        now = (av_gettime_relative() - d->wallclock_start) * f->readrate + stream_ts_offset;
        if (pts - burst_until > now)
            av_usleep(pts - burst_until - now);
    }
}

static void discard_unused_programs(InputFile *ifile)
{
    for (int j = 0; j < ifile->ctx->nb_programs; j++) {
        AVProgram *p = ifile->ctx->programs[j];
        int discard  = AVDISCARD_ALL;

        for (int k = 0; k < p->nb_stream_indexes; k++)
            if (!ifile->streams[p->stream_index[k]]->discard) {
                discard = AVDISCARD_DEFAULT;
                break;
            }
        p->discard = discard;
    }
}

static void thread_set_name(InputFile *f)
{
    char name[16];
    snprintf(name, sizeof(name), "dmx%d:%s", f->index, f->ctx->iformat->name);
    ff_thread_setname(name);
}

static void *input_thread(void *arg)
{
    Demuxer   *d = arg;
    InputFile *f = &d->f;
    AVPacket *pkt;
    unsigned flags = d->non_blocking ? AV_THREAD_MESSAGE_NONBLOCK : 0;
    int ret = 0;

    pkt = av_packet_alloc();
    if (!pkt) {
        ret = AVERROR(ENOMEM);
        goto finish;
    }

    thread_set_name(f);

    discard_unused_programs(f);

    d->wallclock_start = av_gettime_relative();

    while (1) {
        DemuxMsg msg = { NULL };

        ret = av_read_frame(f->ctx, pkt);

        if (ret == AVERROR(EAGAIN)) {
            av_usleep(10000);
            continue;
        }
        if (ret < 0) {
            if (d->loop) {
                /* signal looping to the consumer thread */
                msg.looping = 1;
                ret = av_thread_message_queue_send(d->in_thread_queue, &msg, 0);
                if (ret >= 0)
                    ret = seek_to_start(d);
                if (ret >= 0)
                    continue;

                /* fallthrough to the error path */
            }

            if (ret == AVERROR_EOF)
                av_log(d, AV_LOG_VERBOSE, "EOF while reading input\n");
            else
                av_log(d, AV_LOG_ERROR, "Error during demuxing: %s\n",
                       av_err2str(ret));

            break;
        }

        if (do_pkt_dump) {
            av_pkt_dump_log2(NULL, AV_LOG_INFO, pkt, do_hex_dump,
                             f->ctx->streams[pkt->stream_index]);
        }

        /* the following test is needed in case new streams appear
           dynamically in stream : we ignore them */
        if (pkt->stream_index >= f->nb_streams ||
            f->streams[pkt->stream_index]->discard) {
            report_new_stream(d, pkt);
            av_packet_unref(pkt);
            continue;
        }

        if (pkt->flags & AV_PKT_FLAG_CORRUPT) {
            av_log(d, exit_on_error ? AV_LOG_FATAL : AV_LOG_WARNING,
                   "corrupt input packet in stream %d\n",
                   pkt->stream_index);
            if (exit_on_error) {
                av_packet_unref(pkt);
                ret = AVERROR_INVALIDDATA;
                break;
            }
        }

        ret = input_packet_process(d, &msg, pkt);
        if (ret < 0)
            break;

        if (f->readrate)
            readrate_sleep(d);

        ret = av_thread_message_queue_send(d->in_thread_queue, &msg, flags);
        if (flags && ret == AVERROR(EAGAIN)) {
            flags = 0;
            ret = av_thread_message_queue_send(d->in_thread_queue, &msg, flags);
            av_log(f, AV_LOG_WARNING,
                   "Thread message queue blocking; consider raising the "
                   "thread_queue_size option (current value: %d)\n",
                   d->thread_queue_size);
        }
        if (ret < 0) {
            if (ret != AVERROR_EOF)
                av_log(f, AV_LOG_ERROR,
                       "Unable to send packet to main thread: %s\n",
                       av_err2str(ret));
            av_packet_free(&msg.pkt);
            break;
        }
    }

finish:
    av_assert0(ret < 0);
    av_thread_message_queue_set_err_recv(d->in_thread_queue, ret);

    av_packet_free(&pkt);

    av_log(d, AV_LOG_VERBOSE, "Terminating demuxer thread\n");

    return NULL;
}

static void thread_stop(Demuxer *d)
{
    InputFile *f = &d->f;
    DemuxMsg msg;

    if (!d->in_thread_queue)
        return;
    av_thread_message_queue_set_err_send(d->in_thread_queue, AVERROR_EOF);
    while (av_thread_message_queue_recv(d->in_thread_queue, &msg, 0) >= 0)
        av_packet_free(&msg.pkt);

    pthread_join(d->thread, NULL);
    av_thread_message_queue_free(&d->in_thread_queue);
    av_thread_message_queue_free(&f->audio_duration_queue);
}

static int thread_start(Demuxer *d)
{
    int ret;
    InputFile *f = &d->f;

    if (d->thread_queue_size <= 0)
        d->thread_queue_size = (nb_input_files > 1 ? 8 : 1);

    if (nb_input_files > 1 &&
        (f->ctx->pb ? !f->ctx->pb->seekable :
         strcmp(f->ctx->iformat->name, "lavfi")))
        d->non_blocking = 1;
    ret = av_thread_message_queue_alloc(&d->in_thread_queue,
                                        d->thread_queue_size, sizeof(DemuxMsg));
    if (ret < 0)
        return ret;

    if (d->loop) {
        int nb_audio_dec = 0;

        for (int i = 0; i < f->nb_streams; i++) {
            InputStream *ist = f->streams[i];
            nb_audio_dec += !!(ist->decoding_needed &&
                               ist->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO);
        }

        if (nb_audio_dec) {
            ret = av_thread_message_queue_alloc(&f->audio_duration_queue,
                                                nb_audio_dec, sizeof(LastFrameDuration));
            if (ret < 0)
                goto fail;
            f->audio_duration_queue_size = nb_audio_dec;
        }
    }

    if ((ret = pthread_create(&d->thread, NULL, input_thread, d))) {
        av_log(d, AV_LOG_ERROR, "pthread_create failed: %s. Try to increase `ulimit -v` or decrease `ulimit -s`.\n", strerror(ret));
        ret = AVERROR(ret);
        goto fail;
    }

    d->read_started = 1;

    return 0;
fail:
    av_thread_message_queue_free(&d->in_thread_queue);
    return ret;
}

int ifile_get_packet(InputFile *f, AVPacket **pkt)
{
    Demuxer *d = demuxer_from_ifile(f);
    DemuxMsg msg;
    int ret;

    if (!d->in_thread_queue) {
        ret = thread_start(d);
        if (ret < 0)
            return ret;
    }

    ret = av_thread_message_queue_recv(d->in_thread_queue, &msg,
                                       d->non_blocking ?
                                       AV_THREAD_MESSAGE_NONBLOCK : 0);
    if (ret < 0)
        return ret;
    if (msg.looping)
        return 1;

    *pkt = msg.pkt;
    return 0;
}

static void demux_final_stats(Demuxer *d)
{
    InputFile *f = &d->f;
    uint64_t total_packets = 0, total_size = 0;

    av_log(f, AV_LOG_VERBOSE, "Input file #%d (%s):\n",
           f->index, f->ctx->url);

    for (int j = 0; j < f->nb_streams; j++) {
        InputStream *ist = f->streams[j];
        DemuxStream  *ds = ds_from_ist(ist);
        enum AVMediaType type = ist->par->codec_type;

        if (ist->discard || type == AVMEDIA_TYPE_ATTACHMENT)
            continue;

        total_size    += ds->data_size;
        total_packets += ds->nb_packets;

        av_log(f, AV_LOG_VERBOSE, "  Input stream #%d:%d (%s): ",
               f->index, j, av_get_media_type_string(type));
        av_log(f, AV_LOG_VERBOSE, "%"PRIu64" packets read (%"PRIu64" bytes); ",
               ds->nb_packets, ds->data_size);

        if (ist->decoding_needed) {
            av_log(f, AV_LOG_VERBOSE,
                   "%"PRIu64" frames decoded; %"PRIu64" decode errors",
                   ist->frames_decoded, ist->decode_errors);
            if (type == AVMEDIA_TYPE_AUDIO)
                av_log(f, AV_LOG_VERBOSE, " (%"PRIu64" samples)", ist->samples_decoded);
            av_log(f, AV_LOG_VERBOSE, "; ");
        }

        av_log(f, AV_LOG_VERBOSE, "\n");
    }

    av_log(f, AV_LOG_VERBOSE, "  Total: %"PRIu64" packets (%"PRIu64" bytes) demuxed\n",
           total_packets, total_size);
}

static void ist_free(InputStream **pist)
{
    InputStream *ist = *pist;

    if (!ist)
        return;

    dec_free(&ist->decoder);

    av_dict_free(&ist->decoder_opts);
    av_freep(&ist->filters);
    av_freep(&ist->outputs);
    av_freep(&ist->hwaccel_device);

    avcodec_free_context(&ist->dec_ctx);
    avcodec_parameters_free(&ist->par);

    av_freep(pist);
}

void ifile_close(InputFile **pf)
{
    InputFile *f = *pf;
    Demuxer   *d = demuxer_from_ifile(f);

    if (!f)
        return;

    thread_stop(d);

    if (d->read_started)
        demux_final_stats(d);

    for (int i = 0; i < f->nb_streams; i++)
        ist_free(&f->streams[i]);
    av_freep(&f->streams);

    avformat_close_input(&f->ctx);

    av_freep(pf);
}

static int ist_use(InputStream *ist, int decoding_needed)
{
    DemuxStream *ds = ds_from_ist(ist);

    if (ist->user_set_discard == AVDISCARD_ALL) {
        av_log(ist, AV_LOG_ERROR, "Cannot %s a disabled input stream\n",
               decoding_needed ? "decode" : "streamcopy");
        return AVERROR(EINVAL);
    }

    ist->discard          = 0;
    ist->st->discard      = ist->user_set_discard;
    ist->decoding_needed |= decoding_needed;
    ds->streamcopy_needed |= !decoding_needed;

    if (decoding_needed && !avcodec_is_open(ist->dec_ctx)) {
        int ret = dec_open(ist);
        if (ret < 0)
            return ret;
    }

    return 0;
}

int ist_output_add(InputStream *ist, OutputStream *ost)
{
    int ret;

    ret = ist_use(ist, ost->enc ? DECODING_FOR_OST : 0);
    if (ret < 0)
        return ret;

    ret = GROW_ARRAY(ist->outputs, ist->nb_outputs);
    if (ret < 0)
        return ret;

    ist->outputs[ist->nb_outputs - 1] = ost;

    return 0;
}

int ist_filter_add(InputStream *ist, InputFilter *ifilter, int is_simple)
{
    int ret;

    ret = ist_use(ist, is_simple ? DECODING_FOR_OST : DECODING_FOR_FILTER);
    if (ret < 0)
        return ret;

    ret = GROW_ARRAY(ist->filters, ist->nb_filters);
    if (ret < 0)
        return ret;

    ist->filters[ist->nb_filters - 1] = ifilter;

    // initialize fallback parameters for filtering
    ret = ifilter_parameters_from_dec(ifilter, ist->dec_ctx);
    if (ret < 0)
        return ret;

    return 0;
}

static int choose_decoder(const OptionsContext *o, AVFormatContext *s, AVStream *st,
                          enum HWAccelID hwaccel_id, enum AVHWDeviceType hwaccel_device_type,
                          const AVCodec **pcodec)

{
    char *codec_name = NULL;

    MATCH_PER_STREAM_OPT(codec_names, str, codec_name, s, st);
    if (codec_name) {
        int ret = find_codec(NULL, codec_name, st->codecpar->codec_type, 0, pcodec);
        if (ret < 0)
            return ret;
        st->codecpar->codec_id = (*pcodec)->id;
        if (recast_media && st->codecpar->codec_type != (*pcodec)->type)
            st->codecpar->codec_type = (*pcodec)->type;
        return 0;
    } else {
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
            hwaccel_id == HWACCEL_GENERIC &&
            hwaccel_device_type != AV_HWDEVICE_TYPE_NONE) {
            const AVCodec *c;
            void *i = NULL;

            while ((c = av_codec_iterate(&i))) {
                const AVCodecHWConfig *config;

                if (c->id != st->codecpar->codec_id ||
                    !av_codec_is_decoder(c))
                    continue;

                for (int j = 0; config = avcodec_get_hw_config(c, j); j++) {
                    if (config->device_type == hwaccel_device_type) {
                        av_log(NULL, AV_LOG_VERBOSE, "Selecting decoder '%s' because of requested hwaccel method %s\n",
                               c->name, av_hwdevice_get_type_name(hwaccel_device_type));
                        *pcodec = c;
                        return 0;
                    }
                }
            }
        }

        *pcodec = avcodec_find_decoder(st->codecpar->codec_id);
        return 0;
    }
}

static int guess_input_channel_layout(InputStream *ist, int guess_layout_max)
{
    AVCodecContext *dec = ist->dec_ctx;

    if (dec->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) {
        char layout_name[256];

        if (dec->ch_layout.nb_channels > guess_layout_max)
            return 0;
        av_channel_layout_default(&dec->ch_layout, dec->ch_layout.nb_channels);
        if (dec->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
            return 0;
        av_channel_layout_describe(&dec->ch_layout, layout_name, sizeof(layout_name));
        av_log(ist, AV_LOG_WARNING, "Guessed Channel Layout: %s\n", layout_name);
    }
    return 1;
}

static int add_display_matrix_to_stream(const OptionsContext *o,
                                        AVFormatContext *ctx, InputStream *ist)
{
    AVStream *st = ist->st;
    double rotation = DBL_MAX;
    int hflip = -1, vflip = -1;
    int hflip_set = 0, vflip_set = 0, rotation_set = 0;
    int32_t *buf;

    MATCH_PER_STREAM_OPT(display_rotations, dbl, rotation, ctx, st);
    MATCH_PER_STREAM_OPT(display_hflips,    i,   hflip,    ctx, st);
    MATCH_PER_STREAM_OPT(display_vflips,    i,   vflip,    ctx, st);

    rotation_set = rotation != DBL_MAX;
    hflip_set    = hflip != -1;
    vflip_set    = vflip != -1;

    if (!rotation_set && !hflip_set && !vflip_set)
        return 0;

    buf = (int32_t *)av_stream_new_side_data(st, AV_PKT_DATA_DISPLAYMATRIX, sizeof(int32_t) * 9);
    if (!buf) {
        av_log(ist, AV_LOG_FATAL, "Failed to generate a display matrix!\n");
        return AVERROR(ENOMEM);
    }

    av_display_rotation_set(buf,
                            rotation_set ? -(rotation) : -0.0f);

    av_display_matrix_flip(buf,
                           hflip_set ? hflip : 0,
                           vflip_set ? vflip : 0);

    return 0;
}

static const char *input_stream_item_name(void *obj)
{
    const DemuxStream *ds = obj;

    return ds->log_name;
}

static const AVClass input_stream_class = {
    .class_name = "InputStream",
    .version    = LIBAVUTIL_VERSION_INT,
    .item_name  = input_stream_item_name,
    .category   = AV_CLASS_CATEGORY_DEMUXER,
};

static DemuxStream *demux_stream_alloc(Demuxer *d, AVStream *st)
{
    const char *type_str = av_get_media_type_string(st->codecpar->codec_type);
    InputFile    *f = &d->f;
    DemuxStream *ds;

    ds = allocate_array_elem(&f->streams, sizeof(*ds), &f->nb_streams);
    if (!ds)
        return NULL;

    ds->ist.st         = st;
    ds->ist.file_index = f->index;
    ds->ist.index      = st->index;
    ds->ist.class      = &input_stream_class;

    snprintf(ds->log_name, sizeof(ds->log_name), "%cist#%d:%d/%s",
             type_str ? *type_str : '?', d->f.index, st->index,
             avcodec_get_name(st->codecpar->codec_id));

    return ds;
}

static int ist_add(const OptionsContext *o, Demuxer *d, AVStream *st)
{
    AVFormatContext *ic = d->f.ctx;
    AVCodecParameters *par = st->codecpar;
    DemuxStream *ds;
    InputStream *ist;
    char *framerate = NULL, *hwaccel_device = NULL;
    const char *hwaccel = NULL;
    char *hwaccel_output_format = NULL;
    char *codec_tag = NULL;
    char *next;
    char *discard_str = NULL;
    const AVClass *cc = avcodec_get_class();
    const AVOption *discard_opt = av_opt_find(&cc, "skip_frame", NULL,
                                              0, AV_OPT_SEARCH_FAKE_OBJ);
    int ret;

    ds  = demux_stream_alloc(d, st);
    if (!ds)
        return AVERROR(ENOMEM);

    ist = &ds->ist;

    ist->discard = 1;
    st->discard  = AVDISCARD_ALL;
    ist->nb_samples = 0;
    ds->first_dts   = AV_NOPTS_VALUE;
    ds->next_dts    = AV_NOPTS_VALUE;

    ds->min_pts = INT64_MAX;
    ds->max_pts = INT64_MIN;

    ds->ts_scale = 1.0;
    MATCH_PER_STREAM_OPT(ts_scale, dbl, ds->ts_scale, ic, st);

    ist->autorotate = 1;
    MATCH_PER_STREAM_OPT(autorotate, i, ist->autorotate, ic, st);

    MATCH_PER_STREAM_OPT(codec_tags, str, codec_tag, ic, st);
    if (codec_tag) {
        uint32_t tag = strtol(codec_tag, &next, 0);
        if (*next) {
            uint8_t buf[4] = { 0 };
            memcpy(buf, codec_tag, FFMIN(sizeof(buf), strlen(codec_tag)));
            tag = AV_RL32(buf);
        }

        st->codecpar->codec_tag = tag;
    }

    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        ret = add_display_matrix_to_stream(o, ic, ist);
        if (ret < 0)
            return ret;

        MATCH_PER_STREAM_OPT(hwaccels, str, hwaccel, ic, st);
        MATCH_PER_STREAM_OPT(hwaccel_output_formats, str,
                             hwaccel_output_format, ic, st);

        if (!hwaccel_output_format && hwaccel && !strcmp(hwaccel, "cuvid")) {
            av_log(ist, AV_LOG_WARNING,
                "WARNING: defaulting hwaccel_output_format to cuda for compatibility "
                "with old commandlines. This behaviour is DEPRECATED and will be removed "
                "in the future. Please explicitly set \"-hwaccel_output_format cuda\".\n");
            ist->hwaccel_output_format = AV_PIX_FMT_CUDA;
        } else if (!hwaccel_output_format && hwaccel && !strcmp(hwaccel, "qsv")) {
            av_log(ist, AV_LOG_WARNING,
                "WARNING: defaulting hwaccel_output_format to qsv for compatibility "
                "with old commandlines. This behaviour is DEPRECATED and will be removed "
                "in the future. Please explicitly set \"-hwaccel_output_format qsv\".\n");
            ist->hwaccel_output_format = AV_PIX_FMT_QSV;
        } else if (!hwaccel_output_format && hwaccel && !strcmp(hwaccel, "mediacodec")) {
            // There is no real AVHWFrameContext implementation. Set
            // hwaccel_output_format to avoid av_hwframe_transfer_data error.
            ist->hwaccel_output_format = AV_PIX_FMT_MEDIACODEC;
        } else if (hwaccel_output_format) {
            ist->hwaccel_output_format = av_get_pix_fmt(hwaccel_output_format);
            if (ist->hwaccel_output_format == AV_PIX_FMT_NONE) {
                av_log(ist, AV_LOG_FATAL, "Unrecognised hwaccel output "
                       "format: %s", hwaccel_output_format);
            }
        } else {
            ist->hwaccel_output_format = AV_PIX_FMT_NONE;
        }

        if (hwaccel) {
            // The NVDEC hwaccels use a CUDA device, so remap the name here.
            if (!strcmp(hwaccel, "nvdec") || !strcmp(hwaccel, "cuvid"))
                hwaccel = "cuda";

            if (!strcmp(hwaccel, "none"))
                ist->hwaccel_id = HWACCEL_NONE;
            else if (!strcmp(hwaccel, "auto"))
                ist->hwaccel_id = HWACCEL_AUTO;
            else {
                enum AVHWDeviceType type = av_hwdevice_find_type_by_name(hwaccel);
                if (type != AV_HWDEVICE_TYPE_NONE) {
                    ist->hwaccel_id = HWACCEL_GENERIC;
                    ist->hwaccel_device_type = type;
                }

                if (!ist->hwaccel_id) {
                    av_log(ist, AV_LOG_FATAL, "Unrecognized hwaccel: %s.\n",
                           hwaccel);
                    av_log(ist, AV_LOG_FATAL, "Supported hwaccels: ");
                    type = AV_HWDEVICE_TYPE_NONE;
                    while ((type = av_hwdevice_iterate_types(type)) !=
                           AV_HWDEVICE_TYPE_NONE)
                        av_log(ist, AV_LOG_FATAL, "%s ",
                               av_hwdevice_get_type_name(type));
                    av_log(ist, AV_LOG_FATAL, "\n");
                    return AVERROR(EINVAL);
                }
            }
        }

        MATCH_PER_STREAM_OPT(hwaccel_devices, str, hwaccel_device, ic, st);
        if (hwaccel_device) {
            ist->hwaccel_device = av_strdup(hwaccel_device);
            if (!ist->hwaccel_device)
                return AVERROR(ENOMEM);
        }
    }

    ret = choose_decoder(o, ic, st, ist->hwaccel_id, ist->hwaccel_device_type,
                         &ist->dec);
    if (ret < 0)
        return ret;

    ret = filter_codec_opts(o->g->codec_opts, ist->st->codecpar->codec_id,
                            ic, st, ist->dec, &ist->decoder_opts);
    if (ret < 0)
        return ret;

    ist->reinit_filters = -1;
    MATCH_PER_STREAM_OPT(reinit_filters, i, ist->reinit_filters, ic, st);

    MATCH_PER_STREAM_OPT(discard, str, discard_str, ic, st);
    ist->user_set_discard = AVDISCARD_NONE;

    if ((o->video_disable && ist->st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) ||
        (o->audio_disable && ist->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) ||
        (o->subtitle_disable && ist->st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) ||
        (o->data_disable && ist->st->codecpar->codec_type == AVMEDIA_TYPE_DATA))
            ist->user_set_discard = AVDISCARD_ALL;

    if (discard_str) {
        ret = av_opt_eval_int(&cc, discard_opt, discard_str, &ist->user_set_discard);
        if (ret  < 0) {
            av_log(ist, AV_LOG_ERROR, "Error parsing discard %s.\n", discard_str);
            return ret;
        }
    }

    ist->dec_ctx = avcodec_alloc_context3(ist->dec);
    if (!ist->dec_ctx)
        return AVERROR(ENOMEM);

    ret = avcodec_parameters_to_context(ist->dec_ctx, par);
    if (ret < 0) {
        av_log(ist, AV_LOG_ERROR, "Error initializing the decoder context.\n");
        return ret;
    }

    if (o->bitexact)
        ist->dec_ctx->flags |= AV_CODEC_FLAG_BITEXACT;

    switch (par->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        MATCH_PER_STREAM_OPT(frame_rates, str, framerate, ic, st);
        if (framerate) {
            ret = av_parse_video_rate(&ist->framerate, framerate);
            if (ret < 0) {
                av_log(ist, AV_LOG_ERROR, "Error parsing framerate %s.\n",
                       framerate);
                return ret;
            }
        }

#if FFMPEG_OPT_TOP
        ist->top_field_first = -1;
        MATCH_PER_STREAM_OPT(top_field_first, i, ist->top_field_first, ic, st);
#endif

        ist->framerate_guessed = av_guess_frame_rate(ic, st, NULL);

        break;
    case AVMEDIA_TYPE_AUDIO: {
        int guess_layout_max = INT_MAX;
        MATCH_PER_STREAM_OPT(guess_layout_max, i, guess_layout_max, ic, st);
        guess_input_channel_layout(ist, guess_layout_max);
        break;
    }
    case AVMEDIA_TYPE_DATA:
    case AVMEDIA_TYPE_SUBTITLE: {
        char *canvas_size = NULL;
        MATCH_PER_STREAM_OPT(fix_sub_duration, i, ist->fix_sub_duration, ic, st);
        MATCH_PER_STREAM_OPT(canvas_sizes, str, canvas_size, ic, st);
        if (canvas_size) {
            ret = av_parse_video_size(&ist->dec_ctx->width, &ist->dec_ctx->height,
                                      canvas_size);
            if (ret < 0) {
                av_log(ist, AV_LOG_FATAL, "Invalid canvas size: %s.\n", canvas_size);
                return ret;
            }
        }

        /* Compute the size of the canvas for the subtitles stream.
           If the subtitles codecpar has set a size, use it. Otherwise use the
           maximum dimensions of the video streams in the same file. */
        ist->sub2video.w = ist->dec_ctx->width;
        ist->sub2video.h = ist->dec_ctx->height;
        if (!(ist->sub2video.w && ist->sub2video.h)) {
            for (int j = 0; j < ic->nb_streams; j++) {
                AVCodecParameters *par1 = ic->streams[j]->codecpar;
                if (par1->codec_type == AVMEDIA_TYPE_VIDEO) {
                    ist->sub2video.w = FFMAX(ist->sub2video.w, par1->width);
                    ist->sub2video.h = FFMAX(ist->sub2video.h, par1->height);
                }
            }
        }

        if (!(ist->sub2video.w && ist->sub2video.h)) {
            ist->sub2video.w = FFMAX(ist->sub2video.w, 720);
            ist->sub2video.h = FFMAX(ist->sub2video.h, 576);
        }

        break;
    }
    case AVMEDIA_TYPE_ATTACHMENT:
    case AVMEDIA_TYPE_UNKNOWN:
        break;
    default:
        abort();
    }

    ist->par = avcodec_parameters_alloc();
    if (!ist->par)
        return AVERROR(ENOMEM);

    ret = avcodec_parameters_from_context(ist->par, ist->dec_ctx);
    if (ret < 0) {
        av_log(ist, AV_LOG_ERROR, "Error initializing the decoder context.\n");
        return ret;
    }

    ist->codec_desc = avcodec_descriptor_get(ist->par->codec_id);

    return 0;
}

static int dump_attachment(InputStream *ist, const char *filename)
{
    AVStream *st = ist->st;
    int ret;
    AVIOContext *out = NULL;
    const AVDictionaryEntry *e;

    if (!st->codecpar->extradata_size) {
        av_log(ist, AV_LOG_WARNING, "No extradata to dump.\n");
        return 0;
    }
    if (!*filename && (e = av_dict_get(st->metadata, "filename", NULL, 0)))
        filename = e->value;
    if (!*filename) {
        av_log(ist, AV_LOG_FATAL, "No filename specified and no 'filename' tag");
        return AVERROR(EINVAL);
    }

    ret = assert_file_overwrite(filename);
    if (ret < 0)
        return ret;

    if ((ret = avio_open2(&out, filename, AVIO_FLAG_WRITE, &int_cb, NULL)) < 0) {
        av_log(ist, AV_LOG_FATAL, "Could not open file %s for writing.\n",
               filename);
        return ret;
    }

    avio_write(out, st->codecpar->extradata, st->codecpar->extradata_size);
    ret = avio_close(out);

    if (ret >= 0)
        av_log(ist, AV_LOG_INFO, "Wrote attachment (%d bytes) to '%s'\n",
               st->codecpar->extradata_size, filename);

    return ret;
}

static const char *input_file_item_name(void *obj)
{
    const Demuxer *d = obj;

    return d->log_name;
}

static const AVClass input_file_class = {
    .class_name = "InputFile",
    .version    = LIBAVUTIL_VERSION_INT,
    .item_name  = input_file_item_name,
    .category   = AV_CLASS_CATEGORY_DEMUXER,
};

static Demuxer *demux_alloc(void)
{
    Demuxer *d = allocate_array_elem(&input_files, sizeof(*d), &nb_input_files);

    if (!d)
        return NULL;

    d->f.class = &input_file_class;
    d->f.index = nb_input_files - 1;

    snprintf(d->log_name, sizeof(d->log_name), "in#%d", d->f.index);

    return d;
}

int ifile_open(const OptionsContext *o, const char *filename)
{
    Demuxer   *d;
    InputFile *f;
    AVFormatContext *ic;
    const AVInputFormat *file_iformat = NULL;
    int err, i, ret = 0;
    int64_t timestamp;
    AVDictionary *unused_opts = NULL;
    const AVDictionaryEntry *e = NULL;
    char *   video_codec_name = NULL;
    char *   audio_codec_name = NULL;
    char *subtitle_codec_name = NULL;
    char *    data_codec_name = NULL;
    int scan_all_pmts_set = 0;

    int64_t start_time     = o->start_time;
    int64_t start_time_eof = o->start_time_eof;
    int64_t stop_time      = o->stop_time;
    int64_t recording_time = o->recording_time;

    d = demux_alloc();
    if (!d)
        return AVERROR(ENOMEM);

    f = &d->f;

    if (stop_time != INT64_MAX && recording_time != INT64_MAX) {
        stop_time = INT64_MAX;
        av_log(d, AV_LOG_WARNING, "-t and -to cannot be used together; using -t.\n");
    }

    if (stop_time != INT64_MAX && recording_time == INT64_MAX) {
        int64_t start = start_time == AV_NOPTS_VALUE ? 0 : start_time;
        if (stop_time <= start) {
            av_log(d, AV_LOG_ERROR, "-to value smaller than -ss; aborting.\n");
            return AVERROR(EINVAL);
        } else {
            recording_time = stop_time - start;
        }
    }

    if (o->format) {
        if (!(file_iformat = av_find_input_format(o->format))) {
            av_log(d, AV_LOG_FATAL, "Unknown input format: '%s'\n", o->format);
            return AVERROR(EINVAL);
        }
    }

    if (!strcmp(filename, "-"))
        filename = "fd:";

    stdin_interaction &= strncmp(filename, "pipe:", 5) &&
                         strcmp(filename, "fd:") &&
                         strcmp(filename, "/dev/stdin");

    /* get default parameters from command line */
    ic = avformat_alloc_context();
    if (!ic)
        return AVERROR(ENOMEM);
    if (o->nb_audio_sample_rate) {
        av_dict_set_int(&o->g->format_opts, "sample_rate", o->audio_sample_rate[o->nb_audio_sample_rate - 1].u.i, 0);
    }
    if (o->nb_audio_channels) {
        const AVClass *priv_class;
        if (file_iformat && (priv_class = file_iformat->priv_class) &&
            av_opt_find(&priv_class, "ch_layout", NULL, 0,
                        AV_OPT_SEARCH_FAKE_OBJ)) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%dC", o->audio_channels[o->nb_audio_channels - 1].u.i);
            av_dict_set(&o->g->format_opts, "ch_layout", buf, 0);
        }
    }
    if (o->nb_audio_ch_layouts) {
        const AVClass *priv_class;
        if (file_iformat && (priv_class = file_iformat->priv_class) &&
            av_opt_find(&priv_class, "ch_layout", NULL, 0,
                        AV_OPT_SEARCH_FAKE_OBJ)) {
            av_dict_set(&o->g->format_opts, "ch_layout", o->audio_ch_layouts[o->nb_audio_ch_layouts - 1].u.str, 0);
        }
    }
    if (o->nb_frame_rates) {
        const AVClass *priv_class;
        /* set the format-level framerate option;
         * this is important for video grabbers, e.g. x11 */
        if (file_iformat && (priv_class = file_iformat->priv_class) &&
            av_opt_find(&priv_class, "framerate", NULL, 0,
                        AV_OPT_SEARCH_FAKE_OBJ)) {
            av_dict_set(&o->g->format_opts, "framerate",
                        o->frame_rates[o->nb_frame_rates - 1].u.str, 0);
        }
    }
    if (o->nb_frame_sizes) {
        av_dict_set(&o->g->format_opts, "video_size", o->frame_sizes[o->nb_frame_sizes - 1].u.str, 0);
    }
    if (o->nb_frame_pix_fmts)
        av_dict_set(&o->g->format_opts, "pixel_format", o->frame_pix_fmts[o->nb_frame_pix_fmts - 1].u.str, 0);

    MATCH_PER_TYPE_OPT(codec_names, str,    video_codec_name, ic, "v");
    MATCH_PER_TYPE_OPT(codec_names, str,    audio_codec_name, ic, "a");
    MATCH_PER_TYPE_OPT(codec_names, str, subtitle_codec_name, ic, "s");
    MATCH_PER_TYPE_OPT(codec_names, str,     data_codec_name, ic, "d");

    if (video_codec_name)
        ret = err_merge(ret, find_codec(NULL, video_codec_name   , AVMEDIA_TYPE_VIDEO   , 0,
                                        &ic->video_codec));
    if (audio_codec_name)
        ret = err_merge(ret, find_codec(NULL, audio_codec_name   , AVMEDIA_TYPE_AUDIO   , 0,
                                        &ic->audio_codec));
    if (subtitle_codec_name)
        ret = err_merge(ret, find_codec(NULL, subtitle_codec_name, AVMEDIA_TYPE_SUBTITLE, 0,
                                        &ic->subtitle_codec));
    if (data_codec_name)
        ret = err_merge(ret, find_codec(NULL, data_codec_name    , AVMEDIA_TYPE_DATA,     0,
                                        &ic->data_codec));
    if (ret < 0)
        return ret;

    ic->video_codec_id     = video_codec_name    ? ic->video_codec->id    : AV_CODEC_ID_NONE;
    ic->audio_codec_id     = audio_codec_name    ? ic->audio_codec->id    : AV_CODEC_ID_NONE;
    ic->subtitle_codec_id  = subtitle_codec_name ? ic->subtitle_codec->id : AV_CODEC_ID_NONE;
    ic->data_codec_id      = data_codec_name     ? ic->data_codec->id     : AV_CODEC_ID_NONE;

    ic->flags |= AVFMT_FLAG_NONBLOCK;
    if (o->bitexact)
        ic->flags |= AVFMT_FLAG_BITEXACT;
    ic->interrupt_callback = int_cb;

    if (!av_dict_get(o->g->format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {
        av_dict_set(&o->g->format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
        scan_all_pmts_set = 1;
    }
    /* open the input file with generic avformat function */
    err = avformat_open_input(&ic, filename, file_iformat, &o->g->format_opts);
    if (err < 0) {
        av_log(d, AV_LOG_ERROR,
               "Error opening input: %s\n", av_err2str(err));
        if (err == AVERROR_PROTOCOL_NOT_FOUND)
            av_log(d, AV_LOG_ERROR, "Did you mean file:%s?\n", filename);
        return err;
    }

    av_strlcat(d->log_name, "/",               sizeof(d->log_name));
    av_strlcat(d->log_name, ic->iformat->name, sizeof(d->log_name));

    if (scan_all_pmts_set)
        av_dict_set(&o->g->format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);
    remove_avoptions(&o->g->format_opts, o->g->codec_opts);

    ret = check_avoptions(o->g->format_opts);
    if (ret < 0)
        return ret;

    /* apply forced codec ids */
    for (i = 0; i < ic->nb_streams; i++) {
        const AVCodec *dummy;
        ret = choose_decoder(o, ic, ic->streams[i], HWACCEL_NONE, AV_HWDEVICE_TYPE_NONE,
                             &dummy);
        if (ret < 0)
            return ret;
    }

    if (o->find_stream_info) {
        AVDictionary **opts;
        int orig_nb_streams = ic->nb_streams;

        ret = setup_find_stream_info_opts(ic, o->g->codec_opts, &opts);
        if (ret < 0)
            return ret;

        /* If not enough info to get the stream parameters, we decode the
           first frames to get it. (used in mpeg case for example) */
        ret = avformat_find_stream_info(ic, opts);

        for (i = 0; i < orig_nb_streams; i++)
            av_dict_free(&opts[i]);
        av_freep(&opts);

        if (ret < 0) {
            av_log(d, AV_LOG_FATAL, "could not find codec parameters\n");
            if (ic->nb_streams == 0) {
                avformat_close_input(&ic);
                return ret;
            }
        }
    }

    if (start_time != AV_NOPTS_VALUE && start_time_eof != AV_NOPTS_VALUE) {
        av_log(d, AV_LOG_WARNING, "Cannot use -ss and -sseof both, using -ss\n");
        start_time_eof = AV_NOPTS_VALUE;
    }

    if (start_time_eof != AV_NOPTS_VALUE) {
        if (start_time_eof >= 0) {
            av_log(d, AV_LOG_ERROR, "-sseof value must be negative; aborting\n");
            return AVERROR(EINVAL);
        }
        if (ic->duration > 0) {
            start_time = start_time_eof + ic->duration;
            if (start_time < 0) {
                av_log(d, AV_LOG_WARNING, "-sseof value seeks to before start of file; ignored\n");
                start_time = AV_NOPTS_VALUE;
            }
        } else
            av_log(d, AV_LOG_WARNING, "Cannot use -sseof, file duration not known\n");
    }
    timestamp = (start_time == AV_NOPTS_VALUE) ? 0 : start_time;
    /* add the stream start time */
    if (!o->seek_timestamp && ic->start_time != AV_NOPTS_VALUE)
        timestamp += ic->start_time;

    /* if seeking requested, we execute it */
    if (start_time != AV_NOPTS_VALUE) {
        int64_t seek_timestamp = timestamp;

        if (!(ic->iformat->flags & AVFMT_SEEK_TO_PTS)) {
            int dts_heuristic = 0;
            for (i=0; i<ic->nb_streams; i++) {
                const AVCodecParameters *par = ic->streams[i]->codecpar;
                if (par->video_delay) {
                    dts_heuristic = 1;
                    break;
                }
            }
            if (dts_heuristic) {
                seek_timestamp -= 3*AV_TIME_BASE / 23;
            }
        }
        ret = avformat_seek_file(ic, -1, INT64_MIN, seek_timestamp, seek_timestamp, 0);
        if (ret < 0) {
            av_log(d, AV_LOG_WARNING, "could not seek to position %0.3f\n",
                   (double)timestamp / AV_TIME_BASE);
        }
    }

    f->ctx        = ic;
    f->start_time = start_time;
    f->recording_time = recording_time;
    f->input_sync_ref = o->input_sync_ref;
    f->input_ts_offset = o->input_ts_offset;
    f->ts_offset  = o->input_ts_offset - (copy_ts ? (start_at_zero && ic->start_time != AV_NOPTS_VALUE ? ic->start_time : 0) : timestamp);
    f->accurate_seek = o->accurate_seek;
    d->loop = o->loop;
    d->duration = 0;
    d->time_base = (AVRational){ 1, 1 };
    d->nb_streams_warn = ic->nb_streams;

    f->format_nots = !!(ic->iformat->flags & AVFMT_NOTIMESTAMPS);

    f->readrate = o->readrate ? o->readrate : 0.0;
    if (f->readrate < 0.0f) {
        av_log(d, AV_LOG_ERROR, "Option -readrate is %0.3f; it must be non-negative.\n", f->readrate);
        return AVERROR(EINVAL);
    }
    if (o->rate_emu) {
        if (f->readrate) {
            av_log(d, AV_LOG_WARNING, "Both -readrate and -re set. Using -readrate %0.3f.\n", f->readrate);
        } else
            f->readrate = 1.0f;
    }

    if (f->readrate) {
        d->readrate_initial_burst = o->readrate_initial_burst ? o->readrate_initial_burst : 0.5;
        if (d->readrate_initial_burst < 0.0) {
            av_log(d, AV_LOG_ERROR,
                   "Option -readrate_initial_burst is %0.3f; it must be non-negative.\n",
                   d->readrate_initial_burst);
            return AVERROR(EINVAL);
        }
    } else if (o->readrate_initial_burst) {
        av_log(d, AV_LOG_WARNING, "Option -readrate_initial_burst ignored "
               "since neither -readrate nor -re were given\n");
    }

    d->thread_queue_size = o->thread_queue_size;

    /* Add all the streams from the given input file to the demuxer */
    for (int i = 0; i < ic->nb_streams; i++) {
        ret = ist_add(o, d, ic->streams[i]);
        if (ret < 0)
            return ret;
    }

    /* dump the file content */
    av_dump_format(ic, f->index, filename, 0);

    /* check if all codec options have been used */
    unused_opts = strip_specifiers(o->g->codec_opts);
    for (i = 0; i < f->nb_streams; i++) {
        e = NULL;
        while ((e = av_dict_iterate(f->streams[i]->decoder_opts, e)))
            av_dict_set(&unused_opts, e->key, NULL, 0);
    }

    e = NULL;
    while ((e = av_dict_iterate(unused_opts, e))) {
        const AVClass *class = avcodec_get_class();
        const AVOption *option = av_opt_find(&class, e->key, NULL, 0,
                                             AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ);
        const AVClass *fclass = avformat_get_class();
        const AVOption *foption = av_opt_find(&fclass, e->key, NULL, 0,
                                             AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ);
        if (!option || foption)
            continue;


        if (!(option->flags & AV_OPT_FLAG_DECODING_PARAM)) {
            av_log(d, AV_LOG_ERROR, "Codec AVOption %s (%s) is not a decoding "
                   "option.\n", e->key, option->help ? option->help : "");
            return AVERROR(EINVAL);
        }

        av_log(d, AV_LOG_WARNING, "Codec AVOption %s (%s) has not been used "
               "for any stream. The most likely reason is either wrong type "
               "(e.g. a video option with no video streams) or that it is a "
               "private option of some decoder which was not actually used "
               "for any stream.\n", e->key, option->help ? option->help : "");
    }
    av_dict_free(&unused_opts);

    for (i = 0; i < o->nb_dump_attachment; i++) {
        int j;

        for (j = 0; j < f->nb_streams; j++) {
            InputStream *ist = f->streams[j];

            if (check_stream_specifier(ic, ist->st, o->dump_attachment[i].specifier) == 1) {
                ret = dump_attachment(ist, o->dump_attachment[i].u.str);
                if (ret < 0)
                    return ret;
            }
        }
    }

    return 0;
}
