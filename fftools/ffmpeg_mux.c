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

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "ffmpeg.h"
#include "ffmpeg_mux.h"
#include "ffmpeg_utils.h"
#include "sync_queue.h"

#include "libavutil/avstring.h"
#include "libavutil/fifo.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/time.h"
#include "libavutil/timestamp.h"

#include "libavcodec/packet.h"

#include "libavformat/avformat.h"
#include "libavformat/avio.h"

typedef struct MuxThreadContext {
    AVPacket *pkt;
    AVPacket *fix_sub_duration_pkt;
} MuxThreadContext;

static Muxer *mux_from_of(OutputFile *of)
{
    return (Muxer*)of;
}

static int64_t filesize(AVIOContext *pb)
{
    int64_t ret = -1;

    if (pb) {
        ret = avio_size(pb);
        if (ret <= 0) // FIXME improve avio_size() so it works with non seekable output too
            ret = avio_tell(pb);
    }

    return ret;
}

static void mux_log_debug_ts(OutputStream *ost, const AVPacket *pkt)
{
    static const char *desc[] = {
        [LATENCY_PROBE_DEMUX]       = "demux",
        [LATENCY_PROBE_DEC_PRE]     = "decode",
        [LATENCY_PROBE_DEC_POST]    = "decode",
        [LATENCY_PROBE_FILTER_PRE]  = "filter",
        [LATENCY_PROBE_FILTER_POST] = "filter",
        [LATENCY_PROBE_ENC_PRE]     = "encode",
        [LATENCY_PROBE_ENC_POST]    = "encode",
        [LATENCY_PROBE_NB]          = "mux",
    };

    char latency[512];

    *latency = 0;
    if (pkt->opaque_ref) {
        const FrameData *fd = (FrameData*)pkt->opaque_ref->data;
        int64_t         now = av_gettime_relative();
        int64_t       total = INT64_MIN;

        int next;

        for (unsigned i = 0; i < FF_ARRAY_ELEMS(fd->wallclock); i = next) {
            int64_t val = fd->wallclock[i];

            next = i + 1;

            if (val == INT64_MIN)
                continue;

            if (total == INT64_MIN) {
                total = now - val;
                snprintf(latency, sizeof(latency), "total:%gms", total / 1e3);
            }

            // find the next valid entry
            for (; next <= FF_ARRAY_ELEMS(fd->wallclock); next++) {
                int64_t val_next = (next == FF_ARRAY_ELEMS(fd->wallclock)) ?
                                   now : fd->wallclock[next];
                int64_t diff;

                if (val_next == INT64_MIN)
                    continue;
                diff = val_next - val;

                // print those stages that take at least 5% of total
                if (100. * diff > 5. * total) {
                    av_strlcat(latency, ", ", sizeof(latency));

                    if (!strcmp(desc[i], desc[next]))
                        av_strlcat(latency, desc[i], sizeof(latency));
                    else
                        av_strlcatf(latency, sizeof(latency), "%s-%s:",
                                    desc[i], desc[next]);

                    av_strlcatf(latency, sizeof(latency), " %gms/%d%%",
                                diff / 1e3, (int)(100. * diff / total));
                }

                break;
            }

        }
    }

    av_log(ost, AV_LOG_INFO, "muxer <- pts:%s pts_time:%s dts:%s dts_time:%s "
           "duration:%s duration_time:%s size:%d latency(%s)\n",
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &ost->st->time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &ost->st->time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, &ost->st->time_base),
           pkt->size, *latency ? latency : "N/A");
}

static int mux_fixup_ts(Muxer *mux, MuxStream *ms, AVPacket *pkt)
{
    OutputStream *ost = &ms->ost;

#if FFMPEG_OPT_VSYNC_DROP
    if (ost->type == AVMEDIA_TYPE_VIDEO && ms->ts_drop)
        pkt->pts = pkt->dts = AV_NOPTS_VALUE;
#endif

    // rescale timestamps to the stream timebase
    if (ost->type == AVMEDIA_TYPE_AUDIO && !ost->enc) {
        // use av_rescale_delta() for streamcopying audio, to preserve
        // accuracy with coarse input timebases
        int duration = av_get_audio_frame_duration2(ost->st->codecpar, pkt->size);

        if (!duration)
            duration = ost->st->codecpar->frame_size;

        pkt->dts = av_rescale_delta(pkt->time_base, pkt->dts,
                                    (AVRational){1, ost->st->codecpar->sample_rate}, duration,
                                    &ms->ts_rescale_delta_last, ost->st->time_base);
        pkt->pts = pkt->dts;

        pkt->duration = av_rescale_q(pkt->duration, pkt->time_base, ost->st->time_base);
    } else
        av_packet_rescale_ts(pkt, pkt->time_base, ost->st->time_base);
    pkt->time_base = ost->st->time_base;

    if (!(mux->fc->oformat->flags & AVFMT_NOTIMESTAMPS)) {
        if (pkt->dts != AV_NOPTS_VALUE &&
            pkt->pts != AV_NOPTS_VALUE &&
            pkt->dts > pkt->pts) {
            av_log(ost, AV_LOG_WARNING, "Invalid DTS: %"PRId64" PTS: %"PRId64", replacing by guess\n",
                   pkt->dts, pkt->pts);
            pkt->pts =
            pkt->dts = pkt->pts + pkt->dts + ms->last_mux_dts + 1
                     - FFMIN3(pkt->pts, pkt->dts, ms->last_mux_dts + 1)
                     - FFMAX3(pkt->pts, pkt->dts, ms->last_mux_dts + 1);
        }
        if ((ost->type == AVMEDIA_TYPE_AUDIO || ost->type == AVMEDIA_TYPE_VIDEO || ost->type == AVMEDIA_TYPE_SUBTITLE) &&
            pkt->dts != AV_NOPTS_VALUE &&
            ms->last_mux_dts != AV_NOPTS_VALUE) {
            int64_t max = ms->last_mux_dts + !(mux->fc->oformat->flags & AVFMT_TS_NONSTRICT);
            if (pkt->dts < max) {
                int loglevel = max - pkt->dts > 2 || ost->type == AVMEDIA_TYPE_VIDEO ? AV_LOG_WARNING : AV_LOG_DEBUG;
                if (exit_on_error)
                    loglevel = AV_LOG_ERROR;
                av_log(ost, loglevel, "Non-monotonic DTS; "
                       "previous: %"PRId64", current: %"PRId64"; ",
                       ms->last_mux_dts, pkt->dts);
                if (exit_on_error) {
                    return AVERROR(EINVAL);
                }

                av_log(ost, loglevel, "changing to %"PRId64". This may result "
                       "in incorrect timestamps in the output file.\n",
                       max);
                if (pkt->pts >= pkt->dts)
                    pkt->pts = FFMAX(pkt->pts, max);
                pkt->dts = max;
            }
        }
    }
    ms->last_mux_dts = pkt->dts;

    if (debug_ts)
        mux_log_debug_ts(ost, pkt);

    return 0;
}

static int write_packet(Muxer *mux, OutputStream *ost, AVPacket *pkt)
{
    MuxStream *ms = ms_from_ost(ost);
    AVFormatContext *s = mux->fc;
    int64_t fs;
    uint64_t frame_num;
    int ret;

    fs = filesize(s->pb);
    atomic_store(&mux->last_filesize, fs);
    if (fs >= mux->limit_filesize) {
        ret = AVERROR_EOF;
        goto fail;
    }

    ret = mux_fixup_ts(mux, ms, pkt);
    if (ret < 0)
        goto fail;

    ms->data_size_mux += pkt->size;
    frame_num = atomic_fetch_add(&ost->packets_written, 1);

    pkt->stream_index = ost->index;

    if (ms->stats.io)
        enc_stats_write(ost, &ms->stats, NULL, pkt, frame_num);

    ret = av_interleaved_write_frame(s, pkt);
    if (ret < 0) {
        av_log(ost, AV_LOG_ERROR,
               "Error submitting a packet to the muxer: %s\n",
               av_err2str(ret));
        goto fail;
    }

    return 0;
fail:
    av_packet_unref(pkt);
    return ret;
}

static int sync_queue_process(Muxer *mux, MuxStream *ms, AVPacket *pkt, int *stream_eof)
{
    OutputFile *of = &mux->of;

    if (ms->sq_idx_mux >= 0) {
        int ret = sq_send(mux->sq_mux, ms->sq_idx_mux, SQPKT(pkt));
        if (ret < 0) {
            if (ret == AVERROR_EOF)
                *stream_eof = 1;

            return ret;
        }

        while (1) {
            ret = sq_receive(mux->sq_mux, -1, SQPKT(mux->sq_pkt));
            if (ret < 0) {
                /* n.b.: We forward EOF from the sync queue, terminating muxing.
                 * This assumes that if a muxing sync queue is present, then all
                 * the streams use it. That is true currently, but may change in
                 * the future, then this code needs to be revisited.
                 */
                return ret == AVERROR(EAGAIN) ? 0 : ret;
            }

            ret = write_packet(mux, of->streams[ret],
                               mux->sq_pkt);
            if (ret < 0)
                return ret;
        }
    } else if (pkt)
        return write_packet(mux, &ms->ost, pkt);

    return 0;
}

static int of_streamcopy(OutputFile *of, OutputStream *ost, AVPacket *pkt);

/* apply the output bitstream filters */
static int mux_packet_filter(Muxer *mux, MuxThreadContext *mt,
                             OutputStream *ost, AVPacket *pkt, int *stream_eof)
{
    MuxStream *ms = ms_from_ost(ost);
    const char *err_msg;
    int ret;

    if (pkt && !ost->enc) {
        ret = of_streamcopy(&mux->of, ost, pkt);
        if (ret == AVERROR(EAGAIN))
            return 0;
        else if (ret == AVERROR_EOF) {
            av_packet_unref(pkt);
            pkt = NULL;
            *stream_eof = 1;
        } else if (ret < 0)
            goto fail;
    }

    // emit heartbeat for -fix_sub_duration;
    // we are only interested in heartbeats on on random access points.
    if (pkt && (pkt->flags & AV_PKT_FLAG_KEY)) {
        mt->fix_sub_duration_pkt->opaque    = (void*)(intptr_t)PKT_OPAQUE_FIX_SUB_DURATION;
        mt->fix_sub_duration_pkt->pts       = pkt->pts;
        mt->fix_sub_duration_pkt->time_base = pkt->time_base;

        ret = sch_mux_sub_heartbeat(mux->sch, mux->sch_idx, ms->sch_idx,
                                    mt->fix_sub_duration_pkt);
        if (ret < 0)
            goto fail;
    }

    if (ms->bsf_ctx) {
        int bsf_eof = 0;

        if (pkt)
            av_packet_rescale_ts(pkt, pkt->time_base, ms->bsf_ctx->time_base_in);

        ret = av_bsf_send_packet(ms->bsf_ctx, pkt);
        if (ret < 0) {
            err_msg = "submitting a packet for bitstream filtering";
            goto fail;
        }

        while (!bsf_eof) {
            ret = av_bsf_receive_packet(ms->bsf_ctx, ms->bsf_pkt);
            if (ret == AVERROR(EAGAIN))
                return 0;
            else if (ret == AVERROR_EOF)
                bsf_eof = 1;
            else if (ret < 0) {
                av_log(ost, AV_LOG_ERROR,
                       "Error applying bitstream filters to a packet: %s",
                       av_err2str(ret));
                if (exit_on_error)
                    return ret;
                continue;
            }

            if (!bsf_eof)
                ms->bsf_pkt->time_base = ms->bsf_ctx->time_base_out;

            ret = sync_queue_process(mux, ms, bsf_eof ? NULL : ms->bsf_pkt, stream_eof);
            if (ret < 0)
                goto mux_fail;
        }
        *stream_eof = 1;
    } else {
        ret = sync_queue_process(mux, ms, pkt, stream_eof);
        if (ret < 0)
            goto mux_fail;
    }

    return *stream_eof ? AVERROR_EOF : 0;

mux_fail:
    err_msg = "submitting a packet to the muxer";

fail:
    if (ret != AVERROR_EOF)
        av_log(ost, AV_LOG_ERROR, "Error %s: %s\n", err_msg, av_err2str(ret));
    return ret;
}

static void thread_set_name(Muxer *mux)
{
    char name[16];
    snprintf(name, sizeof(name), "mux%d:%s",
             mux->of.index, mux->fc->oformat->name);
    ff_thread_setname(name);
}

static void mux_thread_uninit(MuxThreadContext *mt)
{
    av_packet_free(&mt->pkt);
    av_packet_free(&mt->fix_sub_duration_pkt);

    memset(mt, 0, sizeof(*mt));
}

static int mux_thread_init(MuxThreadContext *mt)
{
    memset(mt, 0, sizeof(*mt));

    mt->pkt = av_packet_alloc();
    if (!mt->pkt)
        goto fail;

    mt->fix_sub_duration_pkt = av_packet_alloc();
    if (!mt->fix_sub_duration_pkt)
        goto fail;

    return 0;

fail:
    mux_thread_uninit(mt);
    return AVERROR(ENOMEM);
}

int muxer_thread(void *arg)
{
    Muxer     *mux = arg;
    OutputFile *of = &mux->of;

    MuxThreadContext mt;

    int        ret = 0;

    ret = mux_thread_init(&mt);
    if (ret < 0)
        goto finish;

    thread_set_name(mux);

    while (1) {
        OutputStream *ost;
        int stream_idx, stream_eof = 0;

        ret = sch_mux_receive(mux->sch, of->index, mt.pkt);
        stream_idx = mt.pkt->stream_index;
        if (stream_idx < 0) {
            av_log(mux, AV_LOG_VERBOSE, "All streams finished\n");
            ret = 0;
            break;
        }

        ost = of->streams[mux->sch_stream_idx[stream_idx]];
        mt.pkt->stream_index = ost->index;
        mt.pkt->flags       &= ~AV_PKT_FLAG_TRUSTED;

        ret = mux_packet_filter(mux, &mt, ost, ret < 0 ? NULL : mt.pkt, &stream_eof);
        av_packet_unref(mt.pkt);
        if (ret == AVERROR_EOF) {
            if (stream_eof) {
                sch_mux_receive_finish(mux->sch, of->index, stream_idx);
            } else {
                av_log(mux, AV_LOG_VERBOSE, "Muxer returned EOF\n");
                ret = 0;
                break;
            }
        } else if (ret < 0) {
            av_log(mux, AV_LOG_ERROR, "Error muxing a packet\n");
            break;
        }
    }

finish:
    mux_thread_uninit(&mt);

    return ret;
}

static int of_streamcopy(OutputFile *of, OutputStream *ost, AVPacket *pkt)
{
    MuxStream  *ms = ms_from_ost(ost);
    FrameData  *fd = pkt->opaque_ref ? (FrameData*)pkt->opaque_ref->data : NULL;
    int64_t      dts = fd ? fd->dts_est : AV_NOPTS_VALUE;
    int64_t start_time = (of->start_time == AV_NOPTS_VALUE) ? 0 : of->start_time;
    int64_t ts_offset;

    if (of->recording_time != INT64_MAX &&
        dts >= of->recording_time + start_time)
        return AVERROR_EOF;

    if (!ms->streamcopy_started && !(pkt->flags & AV_PKT_FLAG_KEY) &&
        !ms->copy_initial_nonkeyframes)
        return AVERROR(EAGAIN);

    if (!ms->streamcopy_started) {
        if (!ms->copy_prior_start &&
            (pkt->pts == AV_NOPTS_VALUE ?
             dts < ms->ts_copy_start :
             pkt->pts < av_rescale_q(ms->ts_copy_start, AV_TIME_BASE_Q, pkt->time_base)))
            return AVERROR(EAGAIN);

        if (of->start_time != AV_NOPTS_VALUE && dts < of->start_time)
            return AVERROR(EAGAIN);
    }

    ts_offset = av_rescale_q(start_time, AV_TIME_BASE_Q, pkt->time_base);

    if (pkt->pts != AV_NOPTS_VALUE)
        pkt->pts -= ts_offset;

    if (pkt->dts == AV_NOPTS_VALUE) {
        pkt->dts = av_rescale_q(dts, AV_TIME_BASE_Q, pkt->time_base);
    } else if (ost->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        pkt->pts = pkt->dts - ts_offset;
    }

    pkt->dts -= ts_offset;

    ms->streamcopy_started = 1;

    return 0;
}

int print_sdp(const char *filename);

int print_sdp(const char *filename)
{
    char sdp[16384];
    int j = 0, ret;
    AVIOContext *sdp_pb;
    AVFormatContext **avc;

    avc = av_malloc_array(nb_output_files, sizeof(*avc));
    if (!avc)
        return AVERROR(ENOMEM);
    for (int i = 0; i < nb_output_files; i++) {
        Muxer *mux = mux_from_of(output_files[i]);

        if (!strcmp(mux->fc->oformat->name, "rtp")) {
            avc[j] = mux->fc;
            j++;
        }
    }

    if (!j) {
        av_log(NULL, AV_LOG_ERROR, "No output streams in the SDP.\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }

    ret = av_sdp_create(avc, j, sdp, sizeof(sdp));
    if (ret < 0)
        goto fail;

    if (!filename) {
        printf("SDP:\n%s\n", sdp);
        fflush(stdout);
    } else {
        ret = avio_open2(&sdp_pb, filename, AVIO_FLAG_WRITE, &int_cb, NULL);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Failed to open sdp file '%s'\n", filename);
            goto fail;
        }

        avio_print(sdp_pb, sdp);
        avio_closep(&sdp_pb);
    }

fail:
    av_freep(&avc);
    return ret;
}

int mux_check_init(void *arg)
{
    Muxer     *mux = arg;
    OutputFile *of = &mux->of;
    AVFormatContext *fc = mux->fc;
    int ret;

    ret = avformat_write_header(fc, &mux->opts);
    if (ret < 0) {
        av_log(mux, AV_LOG_ERROR, "Could not write header (incorrect codec "
               "parameters ?): %s\n", av_err2str(ret));
        return ret;
    }
    //assert_avoptions(of->opts);
    mux->header_written = 1;

    av_dump_format(fc, of->index, fc->url, 1);
    atomic_fetch_add(&nb_output_dumped, 1);

    return 0;
}

static int bsf_init(MuxStream *ms)
{
    OutputStream *ost = &ms->ost;
    AVBSFContext *ctx = ms->bsf_ctx;
    int ret;

    if (!ctx)
        return avcodec_parameters_copy(ost->st->codecpar, ost->par_in);

    ret = avcodec_parameters_copy(ctx->par_in, ost->par_in);
    if (ret < 0)
        return ret;

    ctx->time_base_in = ost->st->time_base;

    ret = av_bsf_init(ctx);
    if (ret < 0) {
        av_log(ms, AV_LOG_ERROR, "Error initializing bitstream filter: %s\n",
               ctx->filter->name);
        return ret;
    }

    ret = avcodec_parameters_copy(ost->st->codecpar, ctx->par_out);
    if (ret < 0)
        return ret;
    ost->st->time_base = ctx->time_base_out;

    ms->bsf_pkt = av_packet_alloc();
    if (!ms->bsf_pkt)
        return AVERROR(ENOMEM);

    return 0;
}

int of_stream_init(OutputFile *of, OutputStream *ost)
{
    Muxer *mux = mux_from_of(of);
    MuxStream *ms = ms_from_ost(ost);
    int ret;

    /* initialize bitstream filters for the output stream
     * needs to be done here, because the codec id for streamcopy is not
     * known until now */
    ret = bsf_init(ms);
    if (ret < 0)
        return ret;

    if (ms->stream_duration) {
        ost->st->duration = av_rescale_q(ms->stream_duration, ms->stream_duration_tb,
                                         ost->st->time_base);
    }

    if (ms->sch_idx >= 0)
        return sch_mux_stream_ready(mux->sch, of->index, ms->sch_idx);

    return 0;
}

static int check_written(OutputFile *of)
{
    int64_t total_packets_written = 0;
    int pass1_used = 1;
    int ret = 0;

    for (int i = 0; i < of->nb_streams; i++) {
        OutputStream *ost = of->streams[i];
        uint64_t packets_written = atomic_load(&ost->packets_written);

        total_packets_written += packets_written;

        if (ost->enc_ctx &&
            (ost->enc_ctx->flags & (AV_CODEC_FLAG_PASS1 | AV_CODEC_FLAG_PASS2))
             != AV_CODEC_FLAG_PASS1)
            pass1_used = 0;

        if (!packets_written &&
            (abort_on_flags & ABORT_ON_FLAG_EMPTY_OUTPUT_STREAM)) {
            av_log(ost, AV_LOG_FATAL, "Empty output stream\n");
            ret = err_merge(ret, AVERROR(EINVAL));
        }
    }

    if (!total_packets_written) {
        int level = AV_LOG_WARNING;

        if (abort_on_flags & ABORT_ON_FLAG_EMPTY_OUTPUT) {
            ret = err_merge(ret, AVERROR(EINVAL));
            level = AV_LOG_FATAL;
        }

        av_log(of, level, "Output file is empty, nothing was encoded%s\n",
               pass1_used ? "" : "(check -ss / -t / -frames parameters if used)");
    }

    return ret;
}

static void mux_final_stats(Muxer *mux)
{
    OutputFile *of = &mux->of;
    uint64_t total_packets = 0, total_size = 0;
    uint64_t video_size = 0, audio_size = 0, subtitle_size = 0,
             extra_size = 0, other_size = 0;

    uint8_t overhead[16] = "unknown";
    int64_t file_size = of_filesize(of);

    av_log(of, AV_LOG_VERBOSE, "Output file #%d (%s):\n",
           of->index, of->url);

    for (int j = 0; j < of->nb_streams; j++) {
        OutputStream *ost = of->streams[j];
        MuxStream     *ms = ms_from_ost(ost);
        const AVCodecParameters *par = ost->st->codecpar;
        const  enum AVMediaType type = par->codec_type;
        const uint64_t s = ms->data_size_mux;

        switch (type) {
        case AVMEDIA_TYPE_VIDEO:    video_size    += s; break;
        case AVMEDIA_TYPE_AUDIO:    audio_size    += s; break;
        case AVMEDIA_TYPE_SUBTITLE: subtitle_size += s; break;
        default:                    other_size    += s; break;
        }

        extra_size    += par->extradata_size;
        total_size    += s;
        total_packets += atomic_load(&ost->packets_written);

        av_log(of, AV_LOG_VERBOSE, "  Output stream #%d:%d (%s): ",
               of->index, j, av_get_media_type_string(type));
        if (ost->enc) {
            av_log(of, AV_LOG_VERBOSE, "%"PRIu64" frames encoded",
                   ost->frames_encoded);
            if (type == AVMEDIA_TYPE_AUDIO)
                av_log(of, AV_LOG_VERBOSE, " (%"PRIu64" samples)", ost->samples_encoded);
            av_log(of, AV_LOG_VERBOSE, "; ");
        }

        av_log(of, AV_LOG_VERBOSE, "%"PRIu64" packets muxed (%"PRIu64" bytes); ",
               atomic_load(&ost->packets_written), s);

        av_log(of, AV_LOG_VERBOSE, "\n");
    }

    av_log(of, AV_LOG_VERBOSE, "  Total: %"PRIu64" packets (%"PRIu64" bytes) muxed\n",
           total_packets, total_size);

    if (total_size && file_size > 0 && file_size >= total_size) {
        snprintf(overhead, sizeof(overhead), "%f%%",
                 100.0 * (file_size - total_size) / total_size);
    }

    av_log(of, AV_LOG_INFO,
           "video:%1.0fKiB audio:%1.0fKiB subtitle:%1.0fKiB other streams:%1.0fKiB "
           "global headers:%1.0fKiB muxing overhead: %s\n",
           video_size    / 1024.0,
           audio_size    / 1024.0,
           subtitle_size / 1024.0,
           other_size    / 1024.0,
           extra_size    / 1024.0,
           overhead);
}

int of_write_trailer(OutputFile *of)
{
    Muxer *mux = mux_from_of(of);
    AVFormatContext *fc = mux->fc;
    int ret, mux_result = 0;

    if (!mux->header_written) {
        av_log(mux, AV_LOG_ERROR,
               "Nothing was written into output file, because "
               "at least one of its streams received no packets.\n");
        return AVERROR(EINVAL);
    }

    ret = av_write_trailer(fc);
    if (ret < 0) {
        av_log(mux, AV_LOG_ERROR, "Error writing trailer: %s\n", av_err2str(ret));
        mux_result = err_merge(mux_result, ret);
    }

    mux->last_filesize = filesize(fc->pb);

    if (!(fc->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_closep(&fc->pb);
        if (ret < 0) {
            av_log(mux, AV_LOG_ERROR, "Error closing file: %s\n", av_err2str(ret));
            mux_result = err_merge(mux_result, ret);
        }
    }

    mux_final_stats(mux);

    // check whether anything was actually written
    ret = check_written(of);
    mux_result = err_merge(mux_result, ret);

    return mux_result;
}

static void enc_stats_uninit(EncStats *es)
{
    for (int i = 0; i < es->nb_components; i++)
        av_freep(&es->components[i].str);
    av_freep(&es->components);

    if (es->lock_initialized)
        pthread_mutex_destroy(&es->lock);
    es->lock_initialized = 0;
}

static void ost_free(OutputStream **post)
{
    OutputStream *ost = *post;
    MuxStream *ms;

    if (!ost)
        return;
    ms = ms_from_ost(ost);

    enc_free(&ost->enc);
    fg_free(&ost->fg_simple);

    if (ost->logfile) {
        if (fclose(ost->logfile))
            av_log(ms, AV_LOG_ERROR,
                   "Error closing logfile, loss of information possible: %s\n",
                   av_err2str(AVERROR(errno)));
        ost->logfile = NULL;
    }

    avcodec_parameters_free(&ost->par_in);

    av_bsf_free(&ms->bsf_ctx);
    av_packet_free(&ms->bsf_pkt);

    av_packet_free(&ms->pkt);

    av_freep(&ost->kf.pts);
    av_expr_free(ost->kf.pexpr);

    av_freep(&ost->logfile_prefix);

    av_freep(&ost->attachment_filename);

    if (ost->enc_ctx)
        av_freep(&ost->enc_ctx->stats_in);
    avcodec_free_context(&ost->enc_ctx);

    enc_stats_uninit(&ost->enc_stats_pre);
    enc_stats_uninit(&ost->enc_stats_post);
    enc_stats_uninit(&ms->stats);

    av_freep(post);
}

static void fc_close(AVFormatContext **pfc)
{
    AVFormatContext *fc = *pfc;

    if (!fc)
        return;

    if (!(fc->oformat->flags & AVFMT_NOFILE))
        avio_closep(&fc->pb);
    avformat_free_context(fc);

    *pfc = NULL;
}

void of_free(OutputFile **pof)
{
    OutputFile *of = *pof;
    Muxer *mux;

    if (!of)
        return;
    mux = mux_from_of(of);

    sq_free(&mux->sq_mux);

    for (int i = 0; i < of->nb_streams; i++)
        ost_free(&of->streams[i]);
    av_freep(&of->streams);

    av_freep(&mux->sch_stream_idx);

    av_dict_free(&mux->opts);
    av_dict_free(&mux->enc_opts_used);

    av_packet_free(&mux->sq_pkt);

    fc_close(&mux->fc);

    av_freep(pof);
}

int64_t of_filesize(OutputFile *of)
{
    Muxer *mux = mux_from_of(of);
    return atomic_load(&mux->last_filesize);
}
