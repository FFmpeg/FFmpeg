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

int want_sdp = 1;

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

static int write_packet(Muxer *mux, OutputStream *ost, AVPacket *pkt)
{
    MuxStream *ms = ms_from_ost(ost);
    AVFormatContext *s = mux->fc;
    AVStream *st = ost->st;
    int64_t fs;
    int ret;

    fs = filesize(s->pb);
    atomic_store(&mux->last_filesize, fs);
    if (fs >= mux->limit_filesize) {
        ret = AVERROR_EOF;
        goto fail;
    }

    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && ost->vsync_method == VSYNC_DROP)
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
                if (exit_on_error) {
                    ret = AVERROR(EINVAL);
                    goto fail;
                }

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

    ost->data_size_mux += pkt->size;
    atomic_fetch_add(&ost->packets_written, 1);

    pkt->stream_index = ost->index;

    if (debug_ts) {
        av_log(NULL, AV_LOG_INFO, "muxer <- type:%s "
                "pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s duration:%s duration_time:%s size:%d\n",
                av_get_media_type_string(st->codecpar->codec_type),
                av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &ost->st->time_base),
                av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &ost->st->time_base),
                av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, &ost->st->time_base),
                pkt->size
              );
    }

    ret = av_interleaved_write_frame(s, pkt);
    if (ret < 0) {
        print_error("av_interleaved_write_frame()", ret);
        goto fail;
    }

    return 0;
fail:
    av_packet_unref(pkt);
    return ret;
}

static int sync_queue_process(Muxer *mux, OutputStream *ost, AVPacket *pkt)
{
    OutputFile *of = &mux->of;

    if (ost->sq_idx_mux >= 0) {
        int ret = sq_send(mux->sq_mux, ost->sq_idx_mux, SQPKT(pkt));
        if (ret < 0)
            return ret;

        while (1) {
            ret = sq_receive(mux->sq_mux, -1, SQPKT(mux->sq_pkt));
            if (ret < 0)
                return (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) ? 0 : ret;

            ret = write_packet(mux, of->streams[ret],
                               mux->sq_pkt);
            if (ret < 0)
                return ret;
        }
    } else if (pkt)
        return write_packet(mux, ost, pkt);

    return 0;
}

static void thread_set_name(OutputFile *of)
{
    char name[16];
    snprintf(name, sizeof(name), "mux%d:%s", of->index, of->format->name);
    ff_thread_setname(name);
}

static void *muxer_thread(void *arg)
{
    Muxer     *mux = arg;
    OutputFile *of = &mux->of;
    AVPacket  *pkt = NULL;
    int        ret = 0;

    pkt = av_packet_alloc();
    if (!pkt) {
        ret = AVERROR(ENOMEM);
        goto finish;
    }

    thread_set_name(of);

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

        ost = of->streams[stream_idx];
        ret = sync_queue_process(mux, ost, ret < 0 ? NULL : pkt);
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

static int thread_submit_packet(Muxer *mux, OutputStream *ost, AVPacket *pkt)
{
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

static int queue_packet(Muxer *mux, OutputStream *ost, AVPacket *pkt)
{
    MuxStream *ms = ms_from_ost(ost);
    AVPacket *tmp_pkt = NULL;
    int ret;

    if (!av_fifo_can_write(ms->muxing_queue)) {
        size_t cur_size = av_fifo_can_read(ms->muxing_queue);
        size_t pkt_size = pkt ? pkt->size : 0;
        unsigned int are_we_over_size =
            (ms->muxing_queue_data_size + pkt_size) > ms->muxing_queue_data_threshold;
        size_t limit    = are_we_over_size ? ms->max_muxing_queue_size : SIZE_MAX;
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

static int submit_packet(Muxer *mux, AVPacket *pkt, OutputStream *ost)
{
    int ret;

    if (mux->tq) {
        return thread_submit_packet(mux, ost, pkt);
    } else {
        /* the muxer is not initialized yet, buffer the packet */
        ret = queue_packet(mux, ost, pkt);
        if (ret < 0) {
            if (pkt)
                av_packet_unref(pkt);
            return ret;
        }
    }

    return 0;
}

void of_output_packet(OutputFile *of, AVPacket *pkt, OutputStream *ost, int eof)
{
    Muxer *mux = mux_from_of(of);
    MuxStream *ms = ms_from_ost(ost);
    const char *err_msg;
    int ret = 0;

    if (!eof && pkt->dts != AV_NOPTS_VALUE)
        ost->last_mux_dts = av_rescale_q(pkt->dts, ost->mux_timebase, AV_TIME_BASE_Q);

    /* apply the output bitstream filters */
    if (ms->bsf_ctx) {
        int bsf_eof = 0;

        ret = av_bsf_send_packet(ms->bsf_ctx, eof ? NULL : pkt);
        if (ret < 0) {
            err_msg = "submitting a packet for bitstream filtering";
            goto fail;
        }

        while (!bsf_eof) {
            ret = av_bsf_receive_packet(ms->bsf_ctx, pkt);
            if (ret == AVERROR(EAGAIN))
                return;
            else if (ret == AVERROR_EOF)
                bsf_eof = 1;
            else if (ret < 0) {
                err_msg = "applying bitstream filters to a packet";
                goto fail;
            }

            ret = submit_packet(mux, bsf_eof ? NULL : pkt, ost);
            if (ret < 0)
                goto mux_fail;
        }
    } else {
        ret = submit_packet(mux, eof ? NULL : pkt, ost);
        if (ret < 0)
            goto mux_fail;
    }

    return;

mux_fail:
    err_msg = "submitting a packet to the muxer";

fail:
    av_log(NULL, AV_LOG_ERROR, "Error %s for output stream #%d:%d.\n",
           err_msg, ost->file_index, ost->index);
    if (exit_on_error)
        exit_program(1);

}

static int thread_stop(Muxer *mux)
{
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

static int thread_start(Muxer *mux)
{
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

    ret = pthread_create(&mux->thread, NULL, muxer_thread, (void*)mux);
    if (ret) {
        tq_free(&mux->tq);
        return AVERROR(ret);
    }

    /* flush the muxing queues */
    for (int i = 0; i < fc->nb_streams; i++) {
        OutputStream *ost = mux->of.streams[i];
        MuxStream     *ms = ms_from_ost(ost);
        AVPacket *pkt;

        /* try to improve muxing time_base (only possible if nothing has been written yet) */
        if (!av_fifo_can_read(ms->muxing_queue))
            ost->mux_timebase = ost->st->time_base;

        while (av_fifo_read(ms->muxing_queue, &pkt, 1) >= 0) {
            ret = thread_submit_packet(mux, ost, pkt);
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

static int print_sdp(void)
{
    char sdp[16384];
    int i;
    int j, ret;
    AVIOContext *sdp_pb;
    AVFormatContext **avc;

    for (i = 0; i < nb_output_files; i++) {
        if (!mux_from_of(output_files[i])->header_written)
            return 0;
    }

    avc = av_malloc_array(nb_output_files, sizeof(*avc));
    if (!avc)
        return AVERROR(ENOMEM);
    for (i = 0, j = 0; i < nb_output_files; i++) {
        if (!strcmp(output_files[i]->format->name, "rtp")) {
            avc[j] = mux_from_of(output_files[i])->fc;
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

int mux_check_init(Muxer *mux)
{
    OutputFile *of = &mux->of;
    AVFormatContext *fc = mux->fc;
    int ret, i;

    for (i = 0; i < fc->nb_streams; i++) {
        OutputStream *ost = of->streams[i];
        if (!ost->initialized)
            return 0;
    }

    ret = avformat_write_header(fc, &mux->opts);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR,
               "Could not write header for output file #%d "
               "(incorrect codec parameters ?): %s\n",
               of->index, av_err2str(ret));
        return ret;
    }
    //assert_avoptions(of->opts);
    mux->header_written = 1;

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
                ret = thread_start(mux_from_of(output_files[i]));
                if (ret < 0)
                    return ret;
            }
        }
    } else {
        ret = thread_start(mux_from_of(of));
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int bsf_init(MuxStream *ms)
{
    OutputStream *ost = &ms->ost;
    AVBSFContext *ctx = ms->bsf_ctx;
    int ret;

    if (!ctx)
        return 0;

    ret = avcodec_parameters_copy(ctx->par_in, ost->st->codecpar);
    if (ret < 0)
        return ret;

    ctx->time_base_in = ost->st->time_base;

    ret = av_bsf_init(ctx);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error initializing bitstream filter: %s\n",
               ctx->filter->name);
        return ret;
    }

    ret = avcodec_parameters_copy(ost->st->codecpar, ctx->par_out);
    if (ret < 0)
        return ret;
    ost->st->time_base = ctx->time_base_out;

    return 0;
}

int of_stream_init(OutputFile *of, OutputStream *ost)
{
    Muxer *mux = mux_from_of(of);
    MuxStream *ms = ms_from_ost(ost);
    int ret;

    if (ost->sq_idx_mux >= 0)
        sq_set_tb(mux->sq_mux, ost->sq_idx_mux, ost->mux_timebase);

    /* initialize bitstream filters for the output stream
     * needs to be done here, because the codec id for streamcopy is not
     * known until now */
    ret = bsf_init(ms);
    if (ret < 0)
        return ret;

    ost->initialized = 1;

    return mux_check_init(mux);
}

int of_write_trailer(OutputFile *of)
{
    Muxer *mux = mux_from_of(of);
    AVFormatContext *fc = mux->fc;
    int ret;

    if (!mux->tq) {
        av_log(NULL, AV_LOG_ERROR,
               "Nothing was written into output file %d (%s), because "
               "at least one of its streams received no packets.\n",
               of->index, fc->url);
        return AVERROR(EINVAL);
    }

    ret = thread_stop(mux);
    if (ret < 0)
        main_return_code = ret;

    ret = av_write_trailer(fc);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error writing trailer of %s: %s\n", fc->url, av_err2str(ret));
        return ret;
    }

    mux->last_filesize = filesize(fc->pb);

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

static void ost_free(OutputStream **post)
{
    OutputStream *ost = *post;
    MuxStream *ms;

    if (!ost)
        return;
    ms = ms_from_ost(ost);

    if (ost->logfile) {
        if (fclose(ost->logfile))
            av_log(NULL, AV_LOG_ERROR,
                   "Error closing logfile, loss of information possible: %s\n",
                   av_err2str(AVERROR(errno)));
        ost->logfile = NULL;
    }

    if (ms->muxing_queue) {
        AVPacket *pkt;
        while (av_fifo_read(ms->muxing_queue, &pkt, 1) >= 0)
            av_packet_free(&pkt);
        av_fifo_freep2(&ms->muxing_queue);
    }

    av_bsf_free(&ms->bsf_ctx);

    av_frame_free(&ost->filtered_frame);
    av_frame_free(&ost->sq_frame);
    av_frame_free(&ost->last_frame);
    av_packet_free(&ost->pkt);
    av_dict_free(&ost->encoder_opts);

    av_freep(&ost->kf.pts);
    av_expr_free(ost->kf.pexpr);

    av_freep(&ost->avfilter);
    av_freep(&ost->logfile_prefix);
    av_freep(&ost->apad);

#if FFMPEG_OPT_MAP_CHANNEL
    av_freep(&ost->audio_channels_map);
    ost->audio_channels_mapped = 0;
#endif

    av_dict_free(&ost->sws_dict);
    av_dict_free(&ost->swr_opts);

    if (ost->enc_ctx)
        av_freep(&ost->enc_ctx->stats_in);
    avcodec_free_context(&ost->enc_ctx);

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

void of_close(OutputFile **pof)
{
    OutputFile *of = *pof;
    Muxer *mux;

    if (!of)
        return;
    mux = mux_from_of(of);

    thread_stop(mux);

    sq_free(&of->sq_encode);
    sq_free(&mux->sq_mux);

    for (int i = 0; i < of->nb_streams; i++)
        ost_free(&of->streams[i]);
    av_freep(&of->streams);

    av_dict_free(&mux->opts);

    av_packet_free(&mux->sq_pkt);

    fc_close(&mux->fc);

    av_freep(pof);
}

int64_t of_filesize(OutputFile *of)
{
    Muxer *mux = mux_from_of(of);
    return atomic_load(&mux->last_filesize);
}
