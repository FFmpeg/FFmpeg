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
#include "ffmpeg_sched.h"
#include "ffmpeg_utils.h"

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/display.h"
#include "libavutil/error.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/time.h"
#include "libavutil/timestamp.h"

#include "libavcodec/bsf.h"
#include "libavcodec/packet.h"

#include "libavformat/avformat.h"

typedef struct DemuxStream {
    InputStream              ist;

    // name used for logging
    char                     log_name[32];

    int                      sch_idx_stream;
    int                      sch_idx_dec;

    double                   ts_scale;

    /* non zero if the packets must be decoded in 'raw_fifo', see DECODING_FOR_* */
    int                      decoding_needed;
#define DECODING_FOR_OST    1
#define DECODING_FOR_FILTER 2

    /* true if stream data should be discarded */
    int                      discard;

    // scheduler returned EOF for this stream
    int                      finished;

    int                      streamcopy_needed;
    int                      have_sub2video;
    int                      reinit_filters;
    int                      autorotate;
    int                      apply_cropping;


    int                      wrap_correction_done;
    int                      saw_first_ts;
    ///< dts of the first packet read for this stream (in AV_TIME_BASE units)
    int64_t                  first_dts;

    /* predicted dts of the next packet read for this stream or (when there are
     * several frames in a packet) of the next frame in current packet (in AV_TIME_BASE units) */
    int64_t                  next_dts;
    ///< dts of the last packet read for this stream (in AV_TIME_BASE units)
    int64_t                  dts;

    const AVCodecDescriptor *codec_desc;

    AVDictionary            *decoder_opts;
    DecoderOpts              dec_opts;
    char                     dec_name[16];
    // decoded media properties, as estimated by opening the decoder
    AVFrame                 *decoded_params;

    AVBSFContext            *bsf;

    /* number of packets successfully read for this stream */
    uint64_t                 nb_packets;
    // combined size of all the packets read
    uint64_t                 data_size;
} DemuxStream;

typedef struct Demuxer {
    InputFile             f;

    // name used for logging
    char                  log_name[32];

    int64_t               wallclock_start;

    /**
     * Extra timestamp offset added by discontinuity handling.
     */
    int64_t               ts_offset_discont;
    int64_t               last_ts;

    int64_t               recording_time;
    int                   accurate_seek;

    /* number of times input stream should be looped */
    int                   loop;
    int                   have_audio_dec;
    /* duration of the looped segment of the input file */
    Timestamp             duration;
    /* pts with the smallest/largest values ever seen */
    Timestamp             min_pts;
    Timestamp             max_pts;

    /* number of streams that the user was warned of */
    int                   nb_streams_warn;

    float                 readrate;
    double                readrate_initial_burst;

    Scheduler            *sch;

    AVPacket             *pkt_heartbeat;

    int                   read_started;
    int                   nb_streams_used;
    int                   nb_streams_finished;
} Demuxer;

typedef struct DemuxThreadContext {
    // packet used for reading from the demuxer
    AVPacket *pkt_demux;
    // packet for reading from BSFs
    AVPacket *pkt_bsf;
} DemuxThreadContext;

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
        DemuxStream *ds = ds_from_ist(ist);
        if (ist->par->codec_type == type && ds->discard &&
            ist->user_set_discard != AVDISCARD_ALL)
            return ist;
    }
    return NULL;
}

static void report_new_stream(Demuxer *d, const AVPacket *pkt)
{
    const AVStream *st = d->f.ctx->streams[pkt->stream_index];

    if (pkt->stream_index < d->nb_streams_warn)
        return;
    av_log(d, AV_LOG_WARNING,
           "New %s stream with index %d at pos:%"PRId64" and DTS:%ss\n",
           av_get_media_type_string(st->codecpar->codec_type),
           pkt->stream_index, pkt->pos, av_ts2timestr(pkt->dts, &st->time_base));
    d->nb_streams_warn = pkt->stream_index + 1;
}

static int seek_to_start(Demuxer *d, Timestamp end_pts)
{
    InputFile    *ifile = &d->f;
    AVFormatContext *is = ifile->ctx;
    int ret;

    ret = avformat_seek_file(is, -1, INT64_MIN, is->start_time, is->start_time, 0);
    if (ret < 0)
        return ret;

    if (end_pts.ts != AV_NOPTS_VALUE &&
        (d->max_pts.ts == AV_NOPTS_VALUE ||
         av_compare_ts(d->max_pts.ts, d->max_pts.tb, end_pts.ts, end_pts.tb) < 0))
        d->max_pts = end_pts;

    if (d->max_pts.ts != AV_NOPTS_VALUE) {
        int64_t min_pts = d->min_pts.ts == AV_NOPTS_VALUE ? 0 : d->min_pts.ts;
        d->duration.ts = d->max_pts.ts - av_rescale_q(min_pts, d->min_pts.tb, d->max_pts.tb);
    }
    d->duration.tb = d->max_pts.tb;

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
                av_log(ist, AV_LOG_WARNING,
                       "DTS %"PRId64", next:%"PRId64" st:%d invalid dropping\n",
                       pkt->dts, ds->next_dts, pkt->stream_index);
                pkt->dts = AV_NOPTS_VALUE;
            }
            if (pkt->pts != AV_NOPTS_VALUE){
                int64_t pkt_pts = av_rescale_q(pkt->pts, pkt->time_base, AV_TIME_BASE_Q);
                delta = pkt_pts - ds->next_dts;
                if (FFABS(delta) > 1LL * dts_error_threshold * AV_TIME_BASE) {
                    av_log(ist, AV_LOG_WARNING,
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
            av_log(ist, AV_LOG_DEBUG,
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

static int ist_dts_update(DemuxStream *ds, AVPacket *pkt, FrameData *fd)
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

            if (ds->codec_desc                                 &&
                (ds->codec_desc->props & AV_CODEC_PROP_FIELDS) &&
                av_stream_get_parser(ist->st))
                fields = 1 + av_stream_get_parser(ist->st)->repeat_pict;

            ds->next_dts += av_rescale_q(fields, av_inv_q(field_rate), AV_TIME_BASE_Q);
        }
        break;
    }

    fd->dts_est = ds->dts;

    return 0;
}

static int ts_fixup(Demuxer *d, AVPacket *pkt, FrameData *fd)
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

    duration = av_rescale_q(d->duration.ts, d->duration.tb, pkt->time_base);
    if (pkt->pts != AV_NOPTS_VALUE) {
        // audio decoders take precedence for estimating total file duration
        int64_t pkt_duration = d->have_audio_dec ? 0 : pkt->duration;

        pkt->pts += duration;

        // update max/min pts that will be used to compute total file duration
        // when using -stream_loop
        if (d->max_pts.ts == AV_NOPTS_VALUE ||
            av_compare_ts(d->max_pts.ts, d->max_pts.tb,
                          pkt->pts + pkt_duration, pkt->time_base) < 0) {
            d->max_pts = (Timestamp){ .ts = pkt->pts + pkt_duration,
                                      .tb = pkt->time_base };
        }
        if (d->min_pts.ts == AV_NOPTS_VALUE ||
            av_compare_ts(d->min_pts.ts, d->min_pts.tb,
                          pkt->pts, pkt->time_base) > 0) {
            d->min_pts = (Timestamp){ .ts = pkt->pts,
                                      .tb = pkt->time_base };
        }
    }

    if (pkt->dts != AV_NOPTS_VALUE)
        pkt->dts += duration;

    SHOW_TS_DEBUG("demuxer+tsfixup");

    // detect and try to correct for timestamp discontinuities
    ts_discontinuity_process(d, ist, pkt);

    // update estimated/predicted dts
    ret = ist_dts_update(ds, pkt, fd);
    if (ret < 0)
        return ret;

    return 0;
}

static int input_packet_process(Demuxer *d, AVPacket *pkt, unsigned *send_flags)
{
    InputFile     *f = &d->f;
    InputStream *ist = f->streams[pkt->stream_index];
    DemuxStream  *ds = ds_from_ist(ist);
    FrameData *fd;
    int ret = 0;

    fd = packet_data(pkt);
    if (!fd)
        return AVERROR(ENOMEM);

    ret = ts_fixup(d, pkt, fd);
    if (ret < 0)
        return ret;

    if (d->recording_time != INT64_MAX) {
        int64_t start_time = 0;
        if (copy_ts) {
            start_time += f->start_time != AV_NOPTS_VALUE ? f->start_time : 0;
            start_time += start_at_zero ? 0 : f->start_time_effective;
        }
        if (ds->dts >= d->recording_time + start_time)
            *send_flags |= DEMUX_SEND_STREAMCOPY_EOF;
    }

    ds->data_size += pkt->size;
    ds->nb_packets++;

    fd->wallclock[LATENCY_PROBE_DEMUX] = av_gettime_relative();

    if (debug_ts) {
        av_log(ist, AV_LOG_INFO, "demuxer+ffmpeg -> ist_index:%d:%d type:%s pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s duration:%s duration_time:%s off:%s off_time:%s\n",
               f->index, pkt->stream_index,
               av_get_media_type_string(ist->par->codec_type),
               av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &pkt->time_base),
               av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &pkt->time_base),
               av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, &pkt->time_base),
               av_ts2str(f->ts_offset),  av_ts2timestr(f->ts_offset, &AV_TIME_BASE_Q));
    }

    return 0;
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
        now = (av_gettime_relative() - d->wallclock_start) * d->readrate + stream_ts_offset;
        if (pts - burst_until > now)
            av_usleep(pts - burst_until - now);
    }
}

static int do_send(Demuxer *d, DemuxStream *ds, AVPacket *pkt, unsigned flags,
                   const char *pkt_desc)
{
    int ret;

    pkt->stream_index = ds->sch_idx_stream;

    ret = sch_demux_send(d->sch, d->f.index, pkt, flags);
    if (ret == AVERROR_EOF) {
        av_packet_unref(pkt);

        av_log(ds, AV_LOG_VERBOSE, "All consumers of this stream are done\n");
        ds->finished = 1;

        if (++d->nb_streams_finished == d->nb_streams_used) {
            av_log(d, AV_LOG_VERBOSE, "All consumers are done\n");
            return AVERROR_EOF;
        }
    } else if (ret < 0) {
        if (ret != AVERROR_EXIT)
            av_log(d, AV_LOG_ERROR,
                   "Unable to send %s packet to consumers: %s\n",
                   pkt_desc, av_err2str(ret));
        return ret;
    }

    return 0;
}

static int demux_send(Demuxer *d, DemuxThreadContext *dt, DemuxStream *ds,
                      AVPacket *pkt, unsigned flags)
{
    InputFile  *f = &d->f;
    int ret;

    // pkt can be NULL only when flushing BSFs
    av_assert0(ds->bsf || pkt);

    // send heartbeat for sub2video streams
    if (d->pkt_heartbeat && pkt && pkt->pts != AV_NOPTS_VALUE) {
        for (int i = 0; i < f->nb_streams; i++) {
            DemuxStream *ds1 = ds_from_ist(f->streams[i]);

            if (ds1->finished || !ds1->have_sub2video)
                continue;

            d->pkt_heartbeat->pts          = pkt->pts;
            d->pkt_heartbeat->time_base    = pkt->time_base;
            d->pkt_heartbeat->opaque       = (void*)(intptr_t)PKT_OPAQUE_SUB_HEARTBEAT;

            ret = do_send(d, ds1, d->pkt_heartbeat, 0, "heartbeat");
            if (ret < 0)
                return ret;
        }
    }

    if (ds->bsf) {
        if (pkt)
            av_packet_rescale_ts(pkt, pkt->time_base, ds->bsf->time_base_in);

        ret = av_bsf_send_packet(ds->bsf, pkt);
        if (ret < 0) {
            if (pkt)
                av_packet_unref(pkt);
            av_log(ds, AV_LOG_ERROR, "Error submitting a packet for filtering: %s\n",
                   av_err2str(ret));
            return ret;
        }

        while (1) {
            ret = av_bsf_receive_packet(ds->bsf, dt->pkt_bsf);
            if (ret == AVERROR(EAGAIN))
                return 0;
            else if (ret < 0) {
                if (ret != AVERROR_EOF)
                    av_log(ds, AV_LOG_ERROR,
                           "Error applying bitstream filters to a packet: %s\n",
                           av_err2str(ret));
                return ret;
            }

            dt->pkt_bsf->time_base = ds->bsf->time_base_out;

            ret = do_send(d, ds, dt->pkt_bsf, 0, "filtered");
            if (ret < 0) {
                av_packet_unref(dt->pkt_bsf);
                return ret;
            }
        }
    } else {
        ret = do_send(d, ds, pkt, flags, "demuxed");
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int demux_bsf_flush(Demuxer *d, DemuxThreadContext *dt)
{
    InputFile *f = &d->f;
    int ret;

    for (unsigned i = 0; i < f->nb_streams; i++) {
        DemuxStream *ds = ds_from_ist(f->streams[i]);

        if (!ds->bsf)
            continue;

        ret = demux_send(d, dt, ds, NULL, 0);
        ret = (ret == AVERROR_EOF) ? 0 : (ret < 0) ? ret : AVERROR_BUG;
        if (ret < 0) {
            av_log(ds, AV_LOG_ERROR, "Error flushing BSFs: %s\n",
                   av_err2str(ret));
            return ret;
        }

        av_bsf_flush(ds->bsf);
    }

    return 0;
}

static void discard_unused_programs(InputFile *ifile)
{
    for (int j = 0; j < ifile->ctx->nb_programs; j++) {
        AVProgram *p = ifile->ctx->programs[j];
        int discard  = AVDISCARD_ALL;

        for (int k = 0; k < p->nb_stream_indexes; k++) {
            DemuxStream *ds = ds_from_ist(ifile->streams[p->stream_index[k]]);

            if (!ds->discard) {
                discard = AVDISCARD_DEFAULT;
                break;
            }
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

static void demux_thread_uninit(DemuxThreadContext *dt)
{
    av_packet_free(&dt->pkt_demux);
    av_packet_free(&dt->pkt_bsf);

    memset(dt, 0, sizeof(*dt));
}

static int demux_thread_init(DemuxThreadContext *dt)
{
    memset(dt, 0, sizeof(*dt));

    dt->pkt_demux = av_packet_alloc();
    if (!dt->pkt_demux)
        return AVERROR(ENOMEM);

    dt->pkt_bsf = av_packet_alloc();
    if (!dt->pkt_bsf)
        return AVERROR(ENOMEM);

    return 0;
}

static int input_thread(void *arg)
{
    Demuxer   *d = arg;
    InputFile *f = &d->f;

    DemuxThreadContext dt;

    int ret = 0;

    ret = demux_thread_init(&dt);
    if (ret < 0)
        goto finish;

    thread_set_name(f);

    discard_unused_programs(f);

    d->read_started    = 1;
    d->wallclock_start = av_gettime_relative();

    while (1) {
        DemuxStream *ds;
        unsigned send_flags = 0;

        ret = av_read_frame(f->ctx, dt.pkt_demux);

        if (ret == AVERROR(EAGAIN)) {
            av_usleep(10000);
            continue;
        }
        if (ret < 0) {
            int ret_bsf;

            if (ret == AVERROR_EOF)
                av_log(d, AV_LOG_VERBOSE, "EOF while reading input\n");
            else {
                av_log(d, AV_LOG_ERROR, "Error during demuxing: %s\n",
                       av_err2str(ret));
                ret = exit_on_error ? ret : 0;
            }

            ret_bsf = demux_bsf_flush(d, &dt);
            ret = err_merge(ret == AVERROR_EOF ? 0 : ret, ret_bsf);

            if (d->loop) {
                /* signal looping to our consumers */
                dt.pkt_demux->stream_index = -1;
                ret = sch_demux_send(d->sch, f->index, dt.pkt_demux, 0);
                if (ret >= 0)
                    ret = seek_to_start(d, (Timestamp){ .ts = dt.pkt_demux->pts,
                                                        .tb = dt.pkt_demux->time_base });
                if (ret >= 0)
                    continue;

                /* fallthrough to the error path */
            }

            break;
        }

        if (do_pkt_dump) {
            av_pkt_dump_log2(NULL, AV_LOG_INFO, dt.pkt_demux, do_hex_dump,
                             f->ctx->streams[dt.pkt_demux->stream_index]);
        }

        /* the following test is needed in case new streams appear
           dynamically in stream : we ignore them */
        ds = dt.pkt_demux->stream_index < f->nb_streams ?
             ds_from_ist(f->streams[dt.pkt_demux->stream_index]) : NULL;
        if (!ds || ds->discard || ds->finished) {
            report_new_stream(d, dt.pkt_demux);
            av_packet_unref(dt.pkt_demux);
            continue;
        }

        if (dt.pkt_demux->flags & AV_PKT_FLAG_CORRUPT) {
            av_log(d, exit_on_error ? AV_LOG_FATAL : AV_LOG_WARNING,
                   "corrupt input packet in stream %d\n",
                   dt.pkt_demux->stream_index);
            if (exit_on_error) {
                av_packet_unref(dt.pkt_demux);
                ret = AVERROR_INVALIDDATA;
                break;
            }
        }

        ret = input_packet_process(d, dt.pkt_demux, &send_flags);
        if (ret < 0)
            break;

        if (d->readrate)
            readrate_sleep(d);

        ret = demux_send(d, &dt, ds, dt.pkt_demux, send_flags);
        if (ret < 0)
            break;
    }

    // EOF/EXIT is normal termination
    if (ret == AVERROR_EOF || ret == AVERROR_EXIT)
        ret = 0;

finish:
    demux_thread_uninit(&dt);

    return ret;
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

        if (ds->discard || type == AVMEDIA_TYPE_ATTACHMENT)
            continue;

        total_size    += ds->data_size;
        total_packets += ds->nb_packets;

        av_log(f, AV_LOG_VERBOSE, "  Input stream #%d:%d (%s): ",
               f->index, j, av_get_media_type_string(type));
        av_log(f, AV_LOG_VERBOSE, "%"PRIu64" packets read (%"PRIu64" bytes); ",
               ds->nb_packets, ds->data_size);

        if (ds->decoding_needed) {
            av_log(f, AV_LOG_VERBOSE,
                   "%"PRIu64" frames decoded; %"PRIu64" decode errors",
                   ist->decoder->frames_decoded, ist->decoder->decode_errors);
            if (type == AVMEDIA_TYPE_AUDIO)
                av_log(f, AV_LOG_VERBOSE, " (%"PRIu64" samples)", ist->decoder->samples_decoded);
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
    DemuxStream *ds;

    if (!ist)
        return;
    ds = ds_from_ist(ist);

    dec_free(&ist->decoder);

    av_dict_free(&ds->decoder_opts);
    av_freep(&ist->filters);
    av_freep(&ds->dec_opts.hwaccel_device);

    avcodec_parameters_free(&ist->par);

    av_frame_free(&ds->decoded_params);

    av_bsf_free(&ds->bsf);

    av_freep(pist);
}

void ifile_close(InputFile **pf)
{
    InputFile *f = *pf;
    Demuxer   *d = demuxer_from_ifile(f);

    if (!f)
        return;

    if (d->read_started)
        demux_final_stats(d);

    for (int i = 0; i < f->nb_streams; i++)
        ist_free(&f->streams[i]);
    av_freep(&f->streams);

    avformat_close_input(&f->ctx);

    av_packet_free(&d->pkt_heartbeat);

    av_freep(pf);
}

int ist_use(InputStream *ist, int decoding_needed,
            const ViewSpecifier *vs, SchedulerNode *src)
{
    Demuxer      *d = demuxer_from_ifile(ist->file);
    DemuxStream *ds = ds_from_ist(ist);
    int ret;

    if (ist->user_set_discard == AVDISCARD_ALL) {
        av_log(ist, AV_LOG_ERROR, "Cannot %s a disabled input stream\n",
               decoding_needed ? "decode" : "streamcopy");
        return AVERROR(EINVAL);
    }

    if (decoding_needed && !ist->dec) {
        av_log(ist, AV_LOG_ERROR,
               "Decoding requested, but no decoder found for: %s\n",
                avcodec_get_name(ist->par->codec_id));
        return AVERROR(EINVAL);
    }

    if (ds->sch_idx_stream < 0) {
        ret = sch_add_demux_stream(d->sch, d->f.index);
        if (ret < 0)
            return ret;
        ds->sch_idx_stream = ret;
    }

    if (ds->discard) {
        ds->discard = 0;
        d->nb_streams_used++;
    }

    ist->st->discard      = ist->user_set_discard;
    ds->decoding_needed   |= decoding_needed;
    ds->streamcopy_needed |= !decoding_needed;

    if (decoding_needed && ds->sch_idx_dec < 0) {
        int is_audio = ist->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO;

        ds->dec_opts.flags |= (!!ist->fix_sub_duration * DECODER_FLAG_FIX_SUB_DURATION) |
                              (!!(d->f.ctx->iformat->flags & AVFMT_NOTIMESTAMPS) * DECODER_FLAG_TS_UNRELIABLE) |
                              (!!(d->loop && is_audio) * DECODER_FLAG_SEND_END_TS)
#if FFMPEG_OPT_TOP
                              | ((ist->top_field_first >= 0) * DECODER_FLAG_TOP_FIELD_FIRST)
#endif
                             ;

        if (ist->framerate.num) {
            ds->dec_opts.flags     |= DECODER_FLAG_FRAMERATE_FORCED;
            ds->dec_opts.framerate  = ist->framerate;
        } else
            ds->dec_opts.framerate  = ist->st->avg_frame_rate;

        if (ist->dec->id == AV_CODEC_ID_DVB_SUBTITLE &&
           (ds->decoding_needed & DECODING_FOR_OST)) {
            av_dict_set(&ds->decoder_opts, "compute_edt", "1", AV_DICT_DONT_OVERWRITE);
            if (ds->decoding_needed & DECODING_FOR_FILTER)
                av_log(ist, AV_LOG_WARNING,
                       "Warning using DVB subtitles for filtering and output at the "
                       "same time is not fully supported, also see -compute_edt [0|1]\n");
        }

        snprintf(ds->dec_name, sizeof(ds->dec_name), "%d:%d", ist->file->index, ist->index);
        ds->dec_opts.name = ds->dec_name;

        ds->dec_opts.codec = ist->dec;
        ds->dec_opts.par   = ist->par;

        ds->dec_opts.log_parent = ist;

        ds->decoded_params = av_frame_alloc();
        if (!ds->decoded_params)
            return AVERROR(ENOMEM);

        ret = dec_init(&ist->decoder, d->sch,
                       &ds->decoder_opts, &ds->dec_opts, ds->decoded_params);
        if (ret < 0)
            return ret;
        ds->sch_idx_dec = ret;

        ret = sch_connect(d->sch, SCH_DSTREAM(d->f.index, ds->sch_idx_stream),
                                  SCH_DEC_IN(ds->sch_idx_dec));
        if (ret < 0)
            return ret;

        d->have_audio_dec |= is_audio;
    }

    if (decoding_needed && ist->par->codec_type == AVMEDIA_TYPE_VIDEO) {
        ret = dec_request_view(ist->decoder, vs, src);
        if (ret < 0)
            return ret;
    } else {
        *src = decoding_needed                             ?
               SCH_DEC_OUT(ds->sch_idx_dec, 0)             :
               SCH_DSTREAM(d->f.index, ds->sch_idx_stream);
    }

    return 0;
}

int ist_filter_add(InputStream *ist, InputFilter *ifilter, int is_simple,
                   const ViewSpecifier *vs, InputFilterOptions *opts,
                   SchedulerNode *src)
{
    Demuxer      *d = demuxer_from_ifile(ist->file);
    DemuxStream *ds = ds_from_ist(ist);
    int64_t tsoffset = 0;
    int ret;

    ret = ist_use(ist, is_simple ? DECODING_FOR_OST : DECODING_FOR_FILTER,
                  vs, src);
    if (ret < 0)
        return ret;

    ret = GROW_ARRAY(ist->filters, ist->nb_filters);
    if (ret < 0)
        return ret;

    ist->filters[ist->nb_filters - 1] = ifilter;

    if (ist->par->codec_type == AVMEDIA_TYPE_VIDEO) {
        const AVPacketSideData *sd = av_packet_side_data_get(ist->par->coded_side_data,
                                                             ist->par->nb_coded_side_data,
                                                             AV_PKT_DATA_FRAME_CROPPING);
        if (ist->framerate.num > 0 && ist->framerate.den > 0) {
            opts->framerate = ist->framerate;
            opts->flags |= IFILTER_FLAG_CFR;
        } else
            opts->framerate = av_guess_frame_rate(d->f.ctx, ist->st, NULL);
        if (sd && sd->size >= sizeof(uint32_t) * 4) {
            opts->crop_top    = AV_RL32(sd->data +  0);
            opts->crop_bottom = AV_RL32(sd->data +  4);
            opts->crop_left   = AV_RL32(sd->data +  8);
            opts->crop_right  = AV_RL32(sd->data + 12);
            if (ds->apply_cropping && ds->apply_cropping != CROP_CODEC &&
                (opts->crop_top | opts->crop_bottom | opts->crop_left | opts->crop_right))
                opts->flags |= IFILTER_FLAG_CROP;
        }
    } else if (ist->par->codec_type == AVMEDIA_TYPE_SUBTITLE) {
        /* Compute the size of the canvas for the subtitles stream.
           If the subtitles codecpar has set a size, use it. Otherwise use the
           maximum dimensions of the video streams in the same file. */
        opts->sub2video_width  = ist->par->width;
        opts->sub2video_height = ist->par->height;
        if (!(opts->sub2video_width && opts->sub2video_height)) {
            for (int j = 0; j < d->f.nb_streams; j++) {
                AVCodecParameters *par1 = d->f.streams[j]->par;
                if (par1->codec_type == AVMEDIA_TYPE_VIDEO) {
                    opts->sub2video_width  = FFMAX(opts->sub2video_width,  par1->width);
                    opts->sub2video_height = FFMAX(opts->sub2video_height, par1->height);
                }
            }
        }

        if (!(opts->sub2video_width && opts->sub2video_height)) {
            opts->sub2video_width  = FFMAX(opts->sub2video_width,  720);
            opts->sub2video_height = FFMAX(opts->sub2video_height, 576);
        }

        if (!d->pkt_heartbeat) {
            d->pkt_heartbeat = av_packet_alloc();
            if (!d->pkt_heartbeat)
                return AVERROR(ENOMEM);
        }
        ds->have_sub2video = 1;
    }

    ret = av_frame_copy_props(opts->fallback, ds->decoded_params);
    if (ret < 0)
        return ret;
    opts->fallback->format = ds->decoded_params->format;
    opts->fallback->width  = ds->decoded_params->width;
    opts->fallback->height = ds->decoded_params->height;

    ret = av_channel_layout_copy(&opts->fallback->ch_layout, &ds->decoded_params->ch_layout);
    if (ret < 0)
        return ret;

    if (copy_ts) {
        tsoffset = d->f.start_time == AV_NOPTS_VALUE ? 0 : d->f.start_time;
        if (!start_at_zero && d->f.ctx->start_time != AV_NOPTS_VALUE)
            tsoffset += d->f.ctx->start_time;
    }
    opts->trim_start_us = ((d->f.start_time == AV_NOPTS_VALUE) || !d->accurate_seek) ?
                          AV_NOPTS_VALUE : tsoffset;
    opts->trim_end_us   = d->recording_time;

    opts->name = av_strdup(ds->dec_name);
    if (!opts->name)
        return AVERROR(ENOMEM);

    opts->flags |= IFILTER_FLAG_AUTOROTATE * !!(ds->autorotate) |
                   IFILTER_FLAG_REINIT     * !!(ds->reinit_filters);

    return 0;
}

static int choose_decoder(const OptionsContext *o, void *logctx,
                          AVFormatContext *s, AVStream *st,
                          enum HWAccelID hwaccel_id, enum AVHWDeviceType hwaccel_device_type,
                          const AVCodec **pcodec)

{
    const char *codec_name = NULL;

    opt_match_per_stream_str(logctx, &o->codec_names, s, st, &codec_name);
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
                        av_log(logctx, AV_LOG_VERBOSE, "Selecting decoder '%s' because of requested hwaccel method %s\n",
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

static int guess_input_channel_layout(InputStream *ist, AVCodecParameters *par,
                                      int guess_layout_max)
{
    if (par->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) {
        char layout_name[256];

        if (par->ch_layout.nb_channels > guess_layout_max)
            return 0;
        av_channel_layout_default(&par->ch_layout, par->ch_layout.nb_channels);
        if (par->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
            return 0;
        av_channel_layout_describe(&par->ch_layout, layout_name, sizeof(layout_name));
        av_log(ist, AV_LOG_WARNING, "Guessed Channel Layout: %s\n", layout_name);
    }
    return 1;
}

static int add_display_matrix_to_stream(const OptionsContext *o,
                                        AVFormatContext *ctx, InputStream *ist)
{
    AVStream *st = ist->st;
    AVPacketSideData *sd;
    double rotation = DBL_MAX;
    int hflip = -1, vflip = -1;
    int hflip_set = 0, vflip_set = 0, rotation_set = 0;
    int32_t *buf;

    opt_match_per_stream_dbl(ist, &o->display_rotations, ctx, st, &rotation);
    opt_match_per_stream_int(ist, &o->display_hflips, ctx, st, &hflip);
    opt_match_per_stream_int(ist, &o->display_vflips, ctx, st, &vflip);

    rotation_set = rotation != DBL_MAX;
    hflip_set    = hflip != -1;
    vflip_set    = vflip != -1;

    if (!rotation_set && !hflip_set && !vflip_set)
        return 0;

    sd = av_packet_side_data_new(&st->codecpar->coded_side_data,
                                 &st->codecpar->nb_coded_side_data,
                                 AV_PKT_DATA_DISPLAYMATRIX,
                                 sizeof(int32_t) * 9, 0);
    if (!sd) {
        av_log(ist, AV_LOG_FATAL, "Failed to generate a display matrix!\n");
        return AVERROR(ENOMEM);
    }

    buf = (int32_t *)sd->data;
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

    ds->sch_idx_stream = -1;
    ds->sch_idx_dec    = -1;

    ds->ist.st         = st;
    ds->ist.file       = f;
    ds->ist.index      = st->index;
    ds->ist.class      = &input_stream_class;

    snprintf(ds->log_name, sizeof(ds->log_name), "%cist#%d:%d/%s",
             type_str ? *type_str : '?', d->f.index, st->index,
             avcodec_get_name(st->codecpar->codec_id));

    return ds;
}

static int ist_add(const OptionsContext *o, Demuxer *d, AVStream *st, AVDictionary **opts_used)
{
    AVFormatContext *ic = d->f.ctx;
    AVCodecParameters *par = st->codecpar;
    DemuxStream *ds;
    InputStream *ist;
    const char *framerate = NULL, *hwaccel_device = NULL;
    const char *hwaccel = NULL;
    const char *apply_cropping = NULL;
    const char *hwaccel_output_format = NULL;
    const char *codec_tag = NULL;
    const char *bsfs = NULL;
    char *next;
    const char *discard_str = NULL;
    int ret;

    ds  = demux_stream_alloc(d, st);
    if (!ds)
        return AVERROR(ENOMEM);

    ist = &ds->ist;

    ds->discard     = 1;
    st->discard  = AVDISCARD_ALL;
    ds->first_dts   = AV_NOPTS_VALUE;
    ds->next_dts    = AV_NOPTS_VALUE;

    ds->dec_opts.time_base = st->time_base;

    ds->ts_scale = 1.0;
    opt_match_per_stream_dbl(ist, &o->ts_scale, ic, st, &ds->ts_scale);

    ds->autorotate = 1;
    opt_match_per_stream_int(ist, &o->autorotate, ic, st, &ds->autorotate);

    ds->apply_cropping = CROP_ALL;
    opt_match_per_stream_str(ist, &o->apply_cropping, ic, st, &apply_cropping);
    if (apply_cropping) {
        const AVOption opts[] = {
            { "apply_cropping", NULL, 0, AV_OPT_TYPE_INT,
                    { .i64 = CROP_ALL }, CROP_DISABLED, CROP_CONTAINER, AV_OPT_FLAG_DECODING_PARAM, .unit = "apply_cropping" },
                { "none",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = CROP_DISABLED  }, .unit = "apply_cropping" },
                { "all",       NULL, 0, AV_OPT_TYPE_CONST, { .i64 = CROP_ALL       }, .unit = "apply_cropping" },
                { "codec",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = CROP_CODEC     }, .unit = "apply_cropping" },
                { "container", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = CROP_CONTAINER }, .unit = "apply_cropping" },
            { NULL },
        };
        const AVClass class = {
            .class_name = "apply_cropping",
            .item_name  = av_default_item_name,
            .option     = opts,
            .version    = LIBAVUTIL_VERSION_INT,
        };
        const AVClass *pclass = &class;

        ret = av_opt_eval_int(&pclass, opts, apply_cropping, &ds->apply_cropping);
        if (ret < 0) {
            av_log(ist, AV_LOG_ERROR, "Invalid apply_cropping value '%s'.\n", apply_cropping);
            return ret;
        }
    }

    opt_match_per_stream_str(ist, &o->codec_tags, ic, st, &codec_tag);
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

        opt_match_per_stream_str(ist, &o->hwaccels, ic, st, &hwaccel);
        opt_match_per_stream_str(ist, &o->hwaccel_output_formats, ic, st,
                                       &hwaccel_output_format);
        if (!hwaccel_output_format && hwaccel && !strcmp(hwaccel, "cuvid")) {
            av_log(ist, AV_LOG_WARNING,
                "WARNING: defaulting hwaccel_output_format to cuda for compatibility "
                "with old commandlines. This behaviour is DEPRECATED and will be removed "
                "in the future. Please explicitly set \"-hwaccel_output_format cuda\".\n");
            ds->dec_opts.hwaccel_output_format = AV_PIX_FMT_CUDA;
        } else if (!hwaccel_output_format && hwaccel && !strcmp(hwaccel, "qsv")) {
            av_log(ist, AV_LOG_WARNING,
                "WARNING: defaulting hwaccel_output_format to qsv for compatibility "
                "with old commandlines. This behaviour is DEPRECATED and will be removed "
                "in the future. Please explicitly set \"-hwaccel_output_format qsv\".\n");
            ds->dec_opts.hwaccel_output_format = AV_PIX_FMT_QSV;
        } else if (!hwaccel_output_format && hwaccel && !strcmp(hwaccel, "mediacodec")) {
            // There is no real AVHWFrameContext implementation. Set
            // hwaccel_output_format to avoid av_hwframe_transfer_data error.
            ds->dec_opts.hwaccel_output_format = AV_PIX_FMT_MEDIACODEC;
        } else if (hwaccel_output_format) {
            ds->dec_opts.hwaccel_output_format = av_get_pix_fmt(hwaccel_output_format);
            if (ds->dec_opts.hwaccel_output_format == AV_PIX_FMT_NONE) {
                av_log(ist, AV_LOG_FATAL, "Unrecognised hwaccel output "
                       "format: %s", hwaccel_output_format);
            }
        } else {
            ds->dec_opts.hwaccel_output_format = AV_PIX_FMT_NONE;
        }

        if (hwaccel) {
            // The NVDEC hwaccels use a CUDA device, so remap the name here.
            if (!strcmp(hwaccel, "nvdec") || !strcmp(hwaccel, "cuvid"))
                hwaccel = "cuda";

            if (!strcmp(hwaccel, "none"))
                ds->dec_opts.hwaccel_id = HWACCEL_NONE;
            else if (!strcmp(hwaccel, "auto"))
                ds->dec_opts.hwaccel_id = HWACCEL_AUTO;
            else {
                enum AVHWDeviceType type = av_hwdevice_find_type_by_name(hwaccel);
                if (type != AV_HWDEVICE_TYPE_NONE) {
                    ds->dec_opts.hwaccel_id = HWACCEL_GENERIC;
                    ds->dec_opts.hwaccel_device_type = type;
                }

                if (!ds->dec_opts.hwaccel_id) {
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

        opt_match_per_stream_str(ist, &o->hwaccel_devices, ic, st, &hwaccel_device);
        if (hwaccel_device) {
            ds->dec_opts.hwaccel_device = av_strdup(hwaccel_device);
            if (!ds->dec_opts.hwaccel_device)
                return AVERROR(ENOMEM);
        }
    }

    ret = choose_decoder(o, ist, ic, st, ds->dec_opts.hwaccel_id,
                         ds->dec_opts.hwaccel_device_type, &ist->dec);
    if (ret < 0)
        return ret;

    if (ist->dec) {
        ret = filter_codec_opts(o->g->codec_opts, ist->st->codecpar->codec_id,
                                ic, st, ist->dec, &ds->decoder_opts, opts_used);
        if (ret < 0)
            return ret;
    }

    ds->reinit_filters = -1;
    opt_match_per_stream_int(ist, &o->reinit_filters, ic, st, &ds->reinit_filters);

    ist->user_set_discard = AVDISCARD_NONE;

    if ((o->video_disable && ist->st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) ||
        (o->audio_disable && ist->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) ||
        (o->subtitle_disable && ist->st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) ||
        (o->data_disable && ist->st->codecpar->codec_type == AVMEDIA_TYPE_DATA))
            ist->user_set_discard = AVDISCARD_ALL;

    opt_match_per_stream_str(ist, &o->discard, ic, st, &discard_str);
    if (discard_str) {
        ret = av_opt_set(ist->st, "discard", discard_str, 0);
        if (ret  < 0) {
            av_log(ist, AV_LOG_ERROR, "Error parsing discard %s.\n", discard_str);
            return ret;
        }
        ist->user_set_discard = ist->st->discard;
    }

    ds->dec_opts.flags |= DECODER_FLAG_BITEXACT * !!o->bitexact;

    av_dict_set_int(&ds->decoder_opts, "apply_cropping",
                    ds->apply_cropping && ds->apply_cropping != CROP_CONTAINER, 0);

    /* Attached pics are sparse, therefore we would not want to delay their decoding
     * till EOF. */
    if (ist->st->disposition & AV_DISPOSITION_ATTACHED_PIC)
        av_dict_set(&ds->decoder_opts, "thread_type", "-frame", 0);

    switch (par->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        opt_match_per_stream_str(ist, &o->frame_rates, ic, st, &framerate);
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
        opt_match_per_stream_int(ist, &o->top_field_first, ic, st, &ist->top_field_first);
#endif

        break;
    case AVMEDIA_TYPE_AUDIO: {
        const char *ch_layout_str = NULL;

        opt_match_per_stream_str(ist, &o->audio_ch_layouts, ic, st, &ch_layout_str);
        if (ch_layout_str) {
            AVChannelLayout ch_layout;
            ret = av_channel_layout_from_string(&ch_layout, ch_layout_str);
            if (ret < 0) {
                av_log(ist, AV_LOG_ERROR, "Error parsing channel layout %s.\n", ch_layout_str);
                return ret;
            }
            if (par->ch_layout.nb_channels <= 0 || par->ch_layout.nb_channels == ch_layout.nb_channels) {
                av_channel_layout_uninit(&par->ch_layout);
                par->ch_layout = ch_layout;
            } else {
                av_log(ist, AV_LOG_ERROR,
                    "Specified channel layout '%s' has %d channels, but input has %d channels.\n",
                    ch_layout_str, ch_layout.nb_channels, par->ch_layout.nb_channels);
                av_channel_layout_uninit(&ch_layout);
                return AVERROR(EINVAL);
            }
        } else {
            int guess_layout_max = INT_MAX;
            opt_match_per_stream_int(ist, &o->guess_layout_max, ic, st, &guess_layout_max);
            guess_input_channel_layout(ist, par, guess_layout_max);
        }
        break;
    }
    case AVMEDIA_TYPE_DATA:
    case AVMEDIA_TYPE_SUBTITLE: {
        const char *canvas_size = NULL;

        opt_match_per_stream_int(ist, &o->fix_sub_duration, ic, st, &ist->fix_sub_duration);
        opt_match_per_stream_str(ist, &o->canvas_sizes, ic, st, &canvas_size);
        if (canvas_size) {
            ret = av_parse_video_size(&par->width, &par->height,
                                      canvas_size);
            if (ret < 0) {
                av_log(ist, AV_LOG_FATAL, "Invalid canvas size: %s.\n", canvas_size);
                return ret;
            }
        }
        break;
    }
    case AVMEDIA_TYPE_ATTACHMENT:
    case AVMEDIA_TYPE_UNKNOWN:
        break;
    default: av_assert0(0);
    }

    ist->par = avcodec_parameters_alloc();
    if (!ist->par)
        return AVERROR(ENOMEM);

    ret = avcodec_parameters_copy(ist->par, par);
    if (ret < 0) {
        av_log(ist, AV_LOG_ERROR, "Error exporting stream parameters.\n");
        return ret;
    }

    if (ist->st->sample_aspect_ratio.num)
        ist->par->sample_aspect_ratio = ist->st->sample_aspect_ratio;

    opt_match_per_stream_str(ist, &o->bitstream_filters, ic, st, &bsfs);
    if (bsfs) {
        ret = av_bsf_list_parse_str(bsfs, &ds->bsf);
        if (ret < 0) {
            av_log(ist, AV_LOG_ERROR,
                   "Error parsing bitstream filter sequence '%s': %s\n",
                   bsfs, av_err2str(ret));
            return ret;
        }

        ret = avcodec_parameters_copy(ds->bsf->par_in, ist->par);
        if (ret < 0)
            return ret;
        ds->bsf->time_base_in = ist->st->time_base;

        ret = av_bsf_init(ds->bsf);
        if (ret < 0) {
            av_log(ist, AV_LOG_ERROR, "Error initializing bitstream filters: %s\n",
                   av_err2str(ret));
            return ret;
        }

        ret = avcodec_parameters_copy(ist->par, ds->bsf->par_out);
        if (ret < 0)
            return ret;
    }

    ds->codec_desc = avcodec_descriptor_get(ist->par->codec_id);

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

int ifile_open(const OptionsContext *o, const char *filename, Scheduler *sch)
{
    Demuxer   *d;
    InputFile *f;
    AVFormatContext *ic;
    const AVInputFormat *file_iformat = NULL;
    int err, ret = 0;
    int64_t timestamp;
    AVDictionary *opts_used = NULL;
    const char*    video_codec_name = NULL;
    const char*    audio_codec_name = NULL;
    const char* subtitle_codec_name = NULL;
    const char*     data_codec_name = NULL;
    int scan_all_pmts_set = 0;

    int64_t start_time     = o->start_time;
    int64_t start_time_eof = o->start_time_eof;
    int64_t stop_time      = o->stop_time;
    int64_t recording_time = o->recording_time;

    d = demux_alloc();
    if (!d)
        return AVERROR(ENOMEM);

    f = &d->f;

    ret = sch_add_demux(sch, input_thread, d);
    if (ret < 0)
        return ret;
    d->sch = sch;

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
    if (o->audio_sample_rate.nb_opt) {
        av_dict_set_int(&o->g->format_opts, "sample_rate", o->audio_sample_rate.opt[o->audio_sample_rate.nb_opt - 1].u.i, 0);
    }
    if (o->audio_channels.nb_opt) {
        const AVClass *priv_class;
        if (file_iformat && (priv_class = file_iformat->priv_class) &&
            av_opt_find(&priv_class, "ch_layout", NULL, 0,
                        AV_OPT_SEARCH_FAKE_OBJ)) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%dC", o->audio_channels.opt[o->audio_channels.nb_opt - 1].u.i);
            av_dict_set(&o->g->format_opts, "ch_layout", buf, 0);
        }
    }
    if (o->audio_ch_layouts.nb_opt) {
        const AVClass *priv_class;
        if (file_iformat && (priv_class = file_iformat->priv_class) &&
            av_opt_find(&priv_class, "ch_layout", NULL, 0,
                        AV_OPT_SEARCH_FAKE_OBJ)) {
            av_dict_set(&o->g->format_opts, "ch_layout", o->audio_ch_layouts.opt[o->audio_ch_layouts.nb_opt - 1].u.str, 0);
        }
    }
    if (o->frame_rates.nb_opt) {
        const AVClass *priv_class;
        /* set the format-level framerate option;
         * this is important for video grabbers, e.g. x11 */
        if (file_iformat && (priv_class = file_iformat->priv_class) &&
            av_opt_find(&priv_class, "framerate", NULL, 0,
                        AV_OPT_SEARCH_FAKE_OBJ)) {
            av_dict_set(&o->g->format_opts, "framerate",
                        o->frame_rates.opt[o->frame_rates.nb_opt - 1].u.str, 0);
        }
    }
    if (o->frame_sizes.nb_opt) {
        av_dict_set(&o->g->format_opts, "video_size", o->frame_sizes.opt[o->frame_sizes.nb_opt - 1].u.str, 0);
    }
    if (o->frame_pix_fmts.nb_opt)
        av_dict_set(&o->g->format_opts, "pixel_format", o->frame_pix_fmts.opt[o->frame_pix_fmts.nb_opt - 1].u.str, 0);

    video_codec_name    = opt_match_per_type_str(&o->codec_names, 'v');
    audio_codec_name    = opt_match_per_type_str(&o->codec_names, 'a');
    subtitle_codec_name = opt_match_per_type_str(&o->codec_names, 's');
    data_codec_name     = opt_match_per_type_str(&o->codec_names, 'd');

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
    if (ret < 0) {
        avformat_free_context(ic);
        return ret;
    }

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
    f->ctx = ic;

    av_strlcat(d->log_name, "/",               sizeof(d->log_name));
    av_strlcat(d->log_name, ic->iformat->name, sizeof(d->log_name));

    if (scan_all_pmts_set)
        av_dict_set(&o->g->format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);
    remove_avoptions(&o->g->format_opts, o->g->codec_opts);

    ret = check_avoptions(o->g->format_opts);
    if (ret < 0)
        return ret;

    /* apply forced codec ids */
    for (int i = 0; i < ic->nb_streams; i++) {
        const AVCodec *dummy;
        ret = choose_decoder(o, f, ic, ic->streams[i], HWACCEL_NONE, AV_HWDEVICE_TYPE_NONE,
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

        for (int i = 0; i < orig_nb_streams; i++)
            av_dict_free(&opts[i]);
        av_freep(&opts);

        if (ret < 0) {
            av_log(d, AV_LOG_FATAL, "could not find codec parameters\n");
            if (ic->nb_streams == 0)
                return ret;
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
            for (int i = 0; i < ic->nb_streams; i++) {
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

    f->start_time = start_time;
    d->recording_time = recording_time;
    f->input_sync_ref = o->input_sync_ref;
    f->input_ts_offset = o->input_ts_offset;
    f->ts_offset  = o->input_ts_offset - (copy_ts ? (start_at_zero && ic->start_time != AV_NOPTS_VALUE ? ic->start_time : 0) : timestamp);
    d->accurate_seek   = o->accurate_seek;
    d->loop = o->loop;
    d->nb_streams_warn = ic->nb_streams;

    d->duration        = (Timestamp){ .ts = 0,              .tb = (AVRational){ 1, 1 } };
    d->min_pts         = (Timestamp){ .ts = AV_NOPTS_VALUE, .tb = (AVRational){ 1, 1 } };
    d->max_pts         = (Timestamp){ .ts = AV_NOPTS_VALUE, .tb = (AVRational){ 1, 1 } };

    d->readrate = o->readrate ? o->readrate : 0.0;
    if (d->readrate < 0.0f) {
        av_log(d, AV_LOG_ERROR, "Option -readrate is %0.3f; it must be non-negative.\n", d->readrate);
        return AVERROR(EINVAL);
    }
    if (o->rate_emu) {
        if (d->readrate) {
            av_log(d, AV_LOG_WARNING, "Both -readrate and -re set. Using -readrate %0.3f.\n", d->readrate);
        } else
            d->readrate = 1.0f;
    }

    if (d->readrate) {
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

    /* Add all the streams from the given input file to the demuxer */
    for (int i = 0; i < ic->nb_streams; i++) {
        ret = ist_add(o, d, ic->streams[i], &opts_used);
        if (ret < 0) {
            av_dict_free(&opts_used);
            return ret;
        }
    }

    /* dump the file content */
    av_dump_format(ic, f->index, filename, 0);

    /* check if all codec options have been used */
    ret = check_avoptions_used(o->g->codec_opts, opts_used, d, 1);
    av_dict_free(&opts_used);
    if (ret < 0)
        return ret;

    for (int i = 0; i < o->dump_attachment.nb_opt; i++) {
        for (int j = 0; j < f->nb_streams; j++) {
            InputStream *ist = f->streams[j];

            if (check_stream_specifier(ic, ist->st, o->dump_attachment.opt[i].specifier) == 1) {
                ret = dump_attachment(ist, o->dump_attachment.opt[i].u.str);
                if (ret < 0)
                    return ret;
            }
        }
    }

    return 0;
}
