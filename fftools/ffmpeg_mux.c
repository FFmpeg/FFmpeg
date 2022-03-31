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
#include "objpool.h"
#include "sync_queue.h"
#include "thread_queue.h"

#include "libavutil/fifo.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/timestamp.h"
#include "libavutil/thread.h"

#include "libavcodec/packet.h"

#include "libavformat/avformat.h"
#include "libavformat/avio.h"

typedef struct MuxStream {
    /* the packets are buffered here until the muxer is ready to be initialized */
    AVFifo *muxing_queue;

    /*
     * The size of the AVPackets' buffers in queue.
     * Updated when a packet is either pushed or pulled from the queue.
     */
    size_t muxing_queue_data_size;

    /* dts of the last packet sent to the muxer, in the stream timebase
     * used for making up missing dts values */
    int64_t last_mux_dts;
} MuxStream;

struct Muxer {
    AVFormatContext *fc;

    pthread_t    thread;
    ThreadQueue *tq;

    MuxStream *streams;

    AVDictionary *opts;

    int thread_queue_size;

    /* filesize limit expressed in bytes */
    int64_t limit_filesize;
    atomic_int_least64_t last_filesize;
    int header_written;

    AVPacket *sq_pkt;
};

static int want_sdp = 1;

static int queue_packet(OutputFile *of, OutputStream *ost, AVPacket *pkt)
{
    MuxStream *ms = &of->mux->streams[ost->index];
    AVPacket *tmp_pkt = NULL;
    int ret;

    if (!av_fifo_can_write(ms->muxing_queue)) {
        size_t cur_size = av_fifo_can_read(ms->muxing_queue);
        size_t pkt_size = pkt ? pkt->size : 0;
        unsigned int are_we_over_size =
            (ms->muxing_queue_data_size + pkt_size) > ost->muxing_queue_data_threshold;
        size_t limit    = are_we_over_size ? ost->max_muxing_queue_size : SIZE_MAX;
        size_t new_size = FFMIN(2 * cur_size, limit);

        if (new_size <= cur_size) {
            av_log(NULL, AV_LOG_ERROR,
                   "Too many packets buffered for output stream %d:%d.\n",
                   ost->file_index, ost->st->index);
            return AVERROR(ENOSPC);
        }
        ret = av_fifo_grow2(ms->muxing_queue, new_size - cur_size);
        if (ret < 0)
            return ret;
    }

    if (pkt) {
        ret = av_packet_make_refcounted(pkt);
        if (ret < 0)
            return ret;

        tmp_pkt = av_packet_alloc();
        if (!tmp_pkt)
            return AVERROR(ENOMEM);

        av_packet_move_ref(tmp_pkt, pkt);
        ms->muxing_queue_data_size += tmp_pkt->size;
    }
    av_fifo_write(ms->muxing_queue, &tmp_pkt, 1);

    return 0;
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

static int write_packet(OutputFile *of, OutputStream *ost, AVPacket *pkt)
{
    MuxStream *ms = &of->mux->streams[ost->index];
    AVFormatContext *s = of->mux->fc;
    AVStream *st = ost->st;
    int64_t fs;
    int ret;

    fs = filesize(s->pb);
    atomic_store(&of->mux->last_filesize, fs);
    if (fs >= of->mux->limit_filesize)
        return AVERROR_EOF;

    if ((st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && ost->vsync_method == VSYNC_DROP) ||
        (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_sync_method < 0))
        pkt->pts = pkt->dts = AV_NOPTS_VALUE;

    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (ost->frame_rate.num && ost->is_cfr) {
            if (pkt->duration > 0)
                av_log(NULL, AV_LOG_WARNING, "Overriding packet duration by frame rate, this should not happen\n");
            pkt->duration = av_rescale_q(1, av_inv_q(ost->frame_rate),
                                         ost->mux_timebase);
        }
    }

    av_packet_rescale_ts(pkt, ost->mux_timebase, ost->st->time_base);

    if (!(s->oformat->flags & AVFMT_NOTIMESTAMPS)) {
        if (pkt->dts != AV_NOPTS_VALUE &&
            pkt->pts != AV_NOPTS_VALUE &&
            pkt->dts > pkt->pts) {
            av_log(s, AV_LOG_WARNING, "Invalid DTS: %"PRId64" PTS: %"PRId64" in output stream %d:%d, replacing by guess\n",
                   pkt->dts, pkt->pts,
                   ost->file_index, ost->st->index);
            pkt->pts =
            pkt->dts = pkt->pts + pkt->dts + ms->last_mux_dts + 1
                     - FFMIN3(pkt->pts, pkt->dts, ms->last_mux_dts + 1)
                     - FFMAX3(pkt->pts, pkt->dts, ms->last_mux_dts + 1);
        }
        if ((st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO || st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO || st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) &&
            pkt->dts != AV_NOPTS_VALUE &&
            ms->last_mux_dts != AV_NOPTS_VALUE) {
            int64_t max = ms->last_mux_dts + !(s->oformat->flags & AVFMT_TS_NONSTRICT);
            if (pkt->dts < max) {
                int loglevel = max - pkt->dts > 2 || st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ? AV_LOG_WARNING : AV_LOG_DEBUG;
                if (exit_on_error)
                    loglevel = AV_LOG_ERROR;
                av_log(s, loglevel, "Non-monotonous DTS in output stream "
                       "%d:%d; previous: %"PRId64", current: %"PRId64"; ",
                       ost->file_index, ost->st->index, ms->last_mux_dts, pkt->dts);
                if (exit_on_error)
                    return AVERROR(EINVAL);
                av_log(s, loglevel, "changing to %"PRId64". This may result "
                       "in incorrect timestamps in the output file.\n",
                       max);
                if (pkt->pts >= pkt->dts)
                    pkt->pts = FFMAX(pkt->pts, max);
                pkt->dts = max;
            }
        }
    }
    ms->last_mux_dts = pkt->dts;

    ost->data_size += pkt->size;
    atomic_fetch_add(&ost->packets_written, 1);

    pkt->stream_index = ost->index;

    if (debug_ts) {
        av_log(NULL, AV_LOG_INFO, "muxer <- type:%s "
                "pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s duration:%s duration_time:%s size:%d\n",
                av_get_media_type_string(ost->enc_ctx->codec_type),
                av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &ost->st->time_base),
                av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &ost->st->time_base),
                av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, &ost->st->time_base),
                pkt->size
              );
    }

    ret = av_interleaved_write_frame(s, pkt);
    if (ret < 0) {
        print_error("av_interleaved_write_frame()", ret);
        return ret;
    }

    return 0;
}

static int sync_queue_process(OutputFile *of, OutputStream *ost, AVPacket *pkt)
{
    if (ost->sq_idx_mux >= 0) {
        int ret = sq_send(of->sq_mux, ost->sq_idx_mux, SQPKT(pkt));
        if (ret < 0)
            return ret;

        while (1) {
            ret = sq_receive(of->sq_mux, -1, SQPKT(of->mux->sq_pkt));
            if (ret < 0)
                return (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) ? 0 : ret;

            ret = write_packet(of, output_streams[of->ost_index + ret],
                               of->mux->sq_pkt);
            if (ret < 0)
                return ret;
        }
    } else if (pkt)
        return write_packet(of, ost, pkt);

    return 0;
}

static void *muxer_thread(void *arg)
{
    OutputFile *of = arg;
    Muxer     *mux = of->mux;
    AVPacket  *pkt = NULL;
    int        ret = 0;

    pkt = av_packet_alloc();
    if (!pkt) {
        ret = AVERROR(ENOMEM);
        goto finish;
    }

    while (1) {
        OutputStream *ost;
        int stream_idx;

        ret = tq_receive(mux->tq, &stream_idx, pkt);
        if (stream_idx < 0) {
            av_log(NULL, AV_LOG_VERBOSE,
                   "All streams finished for output file #%d\n", of->index);
            ret = 0;
            break;
        }

        ost = output_streams[of->ost_index + stream_idx];
        ret = sync_queue_process(of, ost, ret < 0 ? NULL : pkt);
        av_packet_unref(pkt);
        if (ret == AVERROR_EOF)
            tq_receive_finish(mux->tq, stream_idx);
        else if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "Error muxing a packet for output file #%d\n", of->index);
            break;
        }
    }

finish:
    av_packet_free(&pkt);

    for (unsigned int i = 0; i < mux->fc->nb_streams; i++)
        tq_receive_finish(mux->tq, i);

    av_log(NULL, AV_LOG_VERBOSE, "Terminating muxer thread %d\n", of->index);

    return (void*)(intptr_t)ret;
}

static int print_sdp(void)
{
    char sdp[16384];
    int i;
    int j, ret;
    AVIOContext *sdp_pb;
    AVFormatContext **avc;

    for (i = 0; i < nb_output_files; i++) {
        if (!output_files[i]->mux->header_written)
            return 0;
    }

    avc = av_malloc_array(nb_output_files, sizeof(*avc));
    if (!avc)
        return AVERROR(ENOMEM);
    for (i = 0, j = 0; i < nb_output_files; i++) {
        if (!strcmp(output_files[i]->format->name, "rtp")) {
            avc[j] = output_files[i]->mux->fc;
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

    if (!sdp_filename) {
        printf("SDP:\n%s\n", sdp);
        fflush(stdout);
    } else {
        ret = avio_open2(&sdp_pb, sdp_filename, AVIO_FLAG_WRITE, &int_cb, NULL);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Failed to open sdp file '%s'\n", sdp_filename);
            goto fail;
        }

        avio_print(sdp_pb, sdp);
        avio_closep(&sdp_pb);
        av_freep(&sdp_filename);
    }

    // SDP successfully written, allow muxer threads to start
    ret = 1;

fail:
    av_freep(&avc);
    return ret;
}

static int submit_packet(OutputFile *of, OutputStream *ost, AVPacket *pkt)
{
    Muxer *mux = of->mux;
    int ret = 0;

    if (!pkt || ost->finished & MUXER_FINISHED)
        goto finish;

    ret = tq_send(mux->tq, ost->index, pkt);
    if (ret < 0)
        goto finish;

    return 0;

finish:
    if (pkt)
        av_packet_unref(pkt);

    ost->finished |= MUXER_FINISHED;
    tq_send_finish(mux->tq, ost->index);
    return ret == AVERROR_EOF ? 0 : ret;
}

int of_submit_packet(OutputFile *of, AVPacket *pkt, OutputStream *ost)
{
    int ret;

    if (of->mux->tq) {
        return submit_packet(of, ost, pkt);
    } else {
        /* the muxer is not initialized yet, buffer the packet */
        ret = queue_packet(of, ost, pkt);
        if (ret < 0) {
            av_packet_unref(pkt);
            return ret;
        }
    }

    return 0;
}

static int thread_stop(OutputFile *of)
{
    Muxer *mux = of->mux;
    void *ret;

    if (!mux || !mux->tq)
        return 0;

    for (unsigned int i = 0; i < mux->fc->nb_streams; i++)
        tq_send_finish(mux->tq, i);

    pthread_join(mux->thread, &ret);

    tq_free(&mux->tq);

    return (int)(intptr_t)ret;
}

static void pkt_move(void *dst, void *src)
{
    av_packet_move_ref(dst, src);
}

static int thread_start(OutputFile *of)
{
    Muxer          *mux = of->mux;
    AVFormatContext *fc = mux->fc;
    ObjPool *op;
    int ret;

    op = objpool_alloc_packets();
    if (!op)
        return AVERROR(ENOMEM);

    mux->tq = tq_alloc(fc->nb_streams, mux->thread_queue_size, op, pkt_move);
    if (!mux->tq) {
        objpool_free(&op);
        return AVERROR(ENOMEM);
    }

    ret = pthread_create(&mux->thread, NULL, muxer_thread, (void*)of);
    if (ret) {
        tq_free(&mux->tq);
        return AVERROR(ret);
    }

    /* flush the muxing queues */
    for (int i = 0; i < fc->nb_streams; i++) {
        MuxStream     *ms = &of->mux->streams[i];
        OutputStream *ost = output_streams[of->ost_index + i];
        AVPacket *pkt;

        /* try to improve muxing time_base (only possible if nothing has been written yet) */
        if (!av_fifo_can_read(ms->muxing_queue))
            ost->mux_timebase = ost->st->time_base;

        while (av_fifo_read(ms->muxing_queue, &pkt, 1) >= 0) {
            ret = submit_packet(of, ost, pkt);
            if (pkt) {
                ms->muxing_queue_data_size -= pkt->size;
                av_packet_free(&pkt);
            }
            if (ret < 0)
                return ret;
        }
    }

    return 0;
}

/* open the muxer when all the streams are initialized */
int of_check_init(OutputFile *of)
{
    AVFormatContext *fc = of->mux->fc;
    int ret, i;

    for (i = 0; i < fc->nb_streams; i++) {
        OutputStream *ost = output_streams[of->ost_index + i];
        if (!ost->initialized)
            return 0;
    }

    ret = avformat_write_header(fc, &of->mux->opts);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR,
               "Could not write header for output file #%d "
               "(incorrect codec parameters ?): %s\n",
               of->index, av_err2str(ret));
        return ret;
    }
    //assert_avoptions(of->opts);
    of->mux->header_written = 1;

    av_dump_format(fc, of->index, fc->url, 1);
    nb_output_dumped++;

    if (sdp_filename || want_sdp) {
        ret = print_sdp();
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error writing the SDP.\n");
            return ret;
        } else if (ret == 1) {
            /* SDP is written only after all the muxers are ready, so now we
             * start ALL the threads */
            for (i = 0; i < nb_output_files; i++) {
                ret = thread_start(output_files[i]);
                if (ret < 0)
                    return ret;
            }
        }
    } else {
        ret = thread_start(of);
        if (ret < 0)
            return ret;
    }

    return 0;
}

int of_write_trailer(OutputFile *of)
{
    AVFormatContext *fc = of->mux->fc;
    int ret;

    if (!of->mux->tq) {
        av_log(NULL, AV_LOG_ERROR,
               "Nothing was written into output file %d (%s), because "
               "at least one of its streams received no packets.\n",
               of->index, fc->url);
        return AVERROR(EINVAL);
    }

    ret = thread_stop(of);
    if (ret < 0)
        main_return_code = ret;

    ret = av_write_trailer(fc);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error writing trailer of %s: %s\n", fc->url, av_err2str(ret));
        return ret;
    }

    of->mux->last_filesize = filesize(fc->pb);

    if (!(of->format->flags & AVFMT_NOFILE)) {
        ret = avio_closep(&fc->pb);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error closing file %s: %s\n",
                   fc->url, av_err2str(ret));
            return ret;
        }
    }

    return 0;
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

static void mux_free(Muxer **pmux)
{
    Muxer *mux = *pmux;

    if (!mux)
        return;

    for (int i = 0; i < mux->fc->nb_streams; i++) {
        MuxStream *ms = &mux->streams[i];
        AVPacket *pkt;

        if (!ms->muxing_queue)
            continue;

        while (av_fifo_read(ms->muxing_queue, &pkt, 1) >= 0)
            av_packet_free(&pkt);
        av_fifo_freep2(&ms->muxing_queue);
    }
    av_freep(&mux->streams);
    av_dict_free(&mux->opts);

    av_packet_free(&mux->sq_pkt);

    fc_close(&mux->fc);

    av_freep(pmux);
}

void of_close(OutputFile **pof)
{
    OutputFile *of = *pof;

    if (!of)
        return;

    thread_stop(of);

    sq_free(&of->sq_encode);
    sq_free(&of->sq_mux);

    mux_free(&of->mux);

    av_freep(pof);
}

int of_muxer_init(OutputFile *of, AVFormatContext *fc,
                  AVDictionary *opts, int64_t limit_filesize,
                  int thread_queue_size)
{
    Muxer *mux = av_mallocz(sizeof(*mux));
    int ret = 0;

    if (!mux) {
        fc_close(&fc);
        return AVERROR(ENOMEM);
    }

    mux->streams = av_calloc(fc->nb_streams, sizeof(*mux->streams));
    if (!mux->streams) {
        fc_close(&fc);
        av_freep(&mux);
        return AVERROR(ENOMEM);
    }

    of->mux  = mux;
    mux->fc  = fc;

    for (int i = 0; i < fc->nb_streams; i++) {
        MuxStream *ms = &mux->streams[i];
        ms->muxing_queue = av_fifo_alloc2(8, sizeof(AVPacket*), 0);
        if (!ms->muxing_queue) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        ms->last_mux_dts = AV_NOPTS_VALUE;
    }

    mux->thread_queue_size = thread_queue_size > 0 ? thread_queue_size : 8;
    mux->limit_filesize = limit_filesize;
    mux->opts           = opts;

    if (strcmp(of->format->name, "rtp"))
        want_sdp = 0;

    if (of->sq_mux) {
        mux->sq_pkt = av_packet_alloc();
        if (!mux->sq_pkt) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    /* write the header for files with no streams */
    if (of->format->flags & AVFMT_NOSTREAMS && fc->nb_streams == 0) {
        ret = of_check_init(of);
        if (ret < 0)
            goto fail;
    }

fail:
    if (ret < 0)
        mux_free(&of->mux);

    return ret;
}

int64_t of_filesize(OutputFile *of)
{
    return atomic_load(&of->mux->last_filesize);
}

AVChapter * const *
of_get_chapters(OutputFile *of, unsigned int *nb_chapters)
{
    *nb_chapters = of->mux->fc->nb_chapters;
    return of->mux->fc->chapters;
}
