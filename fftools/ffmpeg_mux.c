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

#include <stdio.h>
#include <string.h>

#include "ffmpeg.h"
#include "sync_queue.h"

#include "libavutil/fifo.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/timestamp.h"

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
    MuxStream *streams;

    AVDictionary *opts;

    /* filesize limit expressed in bytes */
    int64_t limit_filesize;
    int64_t final_filesize;
    int header_written;

    AVPacket *sq_pkt;
};

static int want_sdp = 1;

static void close_all_output_streams(OutputStream *ost, OSTFinished this_stream, OSTFinished others)
{
    int i;
    for (i = 0; i < nb_output_streams; i++) {
        OutputStream *ost2 = output_streams[i];
        ost2->finished |= ost == ost2 ? this_stream : others;
    }
}

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

static void write_packet(OutputFile *of, OutputStream *ost, AVPacket *pkt)
{
    MuxStream *ms = &of->mux->streams[ost->index];
    AVFormatContext *s = of->ctx;
    AVStream *st = ost->st;
    int ret;

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
                if (exit_on_error) {
                    av_log(NULL, AV_LOG_FATAL, "aborting.\n");
                    exit_program(1);
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

    ost->data_size += pkt->size;
    ost->packets_written++;

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
        main_return_code = 1;
        close_all_output_streams(ost, MUXER_FINISHED | ENCODER_FINISHED, ENCODER_FINISHED);
    }
}

static void submit_packet(OutputFile *of, OutputStream *ost, AVPacket *pkt)
{
    if (ost->sq_idx_mux >= 0) {
        int ret = sq_send(of->sq_mux, ost->sq_idx_mux, SQPKT(pkt));
        if (ret < 0) {
            if (pkt)
                av_packet_unref(pkt);
            if (ret == AVERROR_EOF) {
                ost->finished |= MUXER_FINISHED;
                return;
            } else
                exit_program(1);
        }

        while (1) {
            ret = sq_receive(of->sq_mux, -1, SQPKT(of->mux->sq_pkt));
            if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
                return;
            else if (ret < 0)
                exit_program(1);

            write_packet(of, output_streams[of->ost_index + ret],
                         of->mux->sq_pkt);
        }
    } else {
        if (pkt)
            write_packet(of, ost, pkt);
        else
            ost->finished |= MUXER_FINISHED;
    }
}

void of_submit_packet(OutputFile *of, AVPacket *pkt, OutputStream *ost)
{
    AVStream *st = ost->st;
    int ret;

    if (pkt) {
        /*
         * Audio encoders may split the packets --  #frames in != #packets out.
         * But there is no reordering, so we can limit the number of output packets
         * by simply dropping them here.
         * Counting encoded video frames needs to be done separately because of
         * reordering, see do_video_out().
         */
        if (!(st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && ost->encoding_needed)) {
            if (ost->frame_number >= ost->max_frames) {
                av_packet_unref(pkt);
                return;
            }
            ost->frame_number++;
        }
    }

    if (of->mux->header_written) {
        submit_packet(of, ost, pkt);
    } else {
        /* the muxer is not initialized yet, buffer the packet */
        ret = queue_packet(of, ost, pkt);
        if (ret < 0) {
            av_packet_unref(pkt);
            exit_program(1);
        }
    }
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
        exit_program(1);
    for (i = 0, j = 0; i < nb_output_files; i++) {
        if (!strcmp(output_files[i]->ctx->oformat->name, "rtp")) {
            avc[j] = output_files[i]->ctx;
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

fail:
    av_freep(&avc);
    return ret;
}

/* open the muxer when all the streams are initialized */
int of_check_init(OutputFile *of)
{
    int ret, i;

    for (i = 0; i < of->ctx->nb_streams; i++) {
        OutputStream *ost = output_streams[of->ost_index + i];
        if (!ost->initialized)
            return 0;
    }

    ret = avformat_write_header(of->ctx, &of->mux->opts);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR,
               "Could not write header for output file #%d "
               "(incorrect codec parameters ?): %s\n",
               of->index, av_err2str(ret));
        return ret;
    }
    //assert_avoptions(of->opts);
    of->mux->header_written = 1;

    av_dump_format(of->ctx, of->index, of->ctx->url, 1);
    nb_output_dumped++;

    if (sdp_filename || want_sdp) {
        ret = print_sdp();
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error writing the SDP.\n");
            return ret;
        }
    }

    /* flush the muxing queues */
    for (i = 0; i < of->ctx->nb_streams; i++) {
        MuxStream     *ms = &of->mux->streams[i];
        OutputStream *ost = output_streams[of->ost_index + i];
        AVPacket *pkt;

        /* try to improve muxing time_base (only possible if nothing has been written yet) */
        if (!av_fifo_can_read(ms->muxing_queue))
            ost->mux_timebase = ost->st->time_base;

        while (av_fifo_read(ms->muxing_queue, &pkt, 1) >= 0) {
            submit_packet(of, ost, pkt);
            if (pkt) {
                ms->muxing_queue_data_size -= pkt->size;
                av_packet_free(&pkt);
            }
        }
    }

    return 0;
}

int of_write_trailer(OutputFile *of)
{
    int ret;

    if (!of->mux->header_written) {
        av_log(NULL, AV_LOG_ERROR,
               "Nothing was written into output file %d (%s), because "
               "at least one of its streams received no packets.\n",
               of->index, of->ctx->url);
        return AVERROR(EINVAL);
    }

    ret = av_write_trailer(of->ctx);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error writing trailer of %s: %s\n", of->ctx->url, av_err2str(ret));
        return ret;
    }

    of->mux->final_filesize = of_filesize(of);

    if (!(of->format->flags & AVFMT_NOFILE)) {
        ret = avio_closep(&of->ctx->pb);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error closing file %s: %s\n",
                   of->ctx->url, av_err2str(ret));
            return ret;
        }
    }

    return 0;
}

static void mux_free(Muxer **pmux, int nb_streams)
{
    Muxer *mux = *pmux;

    if (!mux)
        return;

    for (int i = 0; i < nb_streams; i++) {
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

    av_freep(pmux);
}

void of_close(OutputFile **pof)
{
    OutputFile *of = *pof;
    AVFormatContext *s;

    if (!of)
        return;

    sq_free(&of->sq_encode);
    sq_free(&of->sq_mux);

    s = of->ctx;

    mux_free(&of->mux, s ? s->nb_streams : 0);

    if (s && s->oformat && !(s->oformat->flags & AVFMT_NOFILE))
        avio_closep(&s->pb);
    avformat_free_context(s);

    av_freep(pof);
}

int of_muxer_init(OutputFile *of, AVDictionary *opts, int64_t limit_filesize)
{
    Muxer *mux = av_mallocz(sizeof(*mux));
    int ret = 0;

    if (!mux)
        return AVERROR(ENOMEM);

    mux->streams = av_calloc(of->ctx->nb_streams, sizeof(*mux->streams));
    if (!mux->streams) {
        av_freep(&mux);
        return AVERROR(ENOMEM);
    }

    of->mux  = mux;

    for (int i = 0; i < of->ctx->nb_streams; i++) {
        MuxStream *ms = &mux->streams[i];
        ms->muxing_queue = av_fifo_alloc2(8, sizeof(AVPacket*), 0);
        if (!ms->muxing_queue) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        ms->last_mux_dts = AV_NOPTS_VALUE;
    }

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
    if (of->format->flags & AVFMT_NOSTREAMS && of->ctx->nb_streams == 0) {
        ret = of_check_init(of);
        if (ret < 0)
            goto fail;
    }

fail:
    if (ret < 0)
        mux_free(&of->mux, of->ctx->nb_streams);

    return ret;
}

int of_finished(OutputFile *of)
{
    return of_filesize(of) >= of->mux->limit_filesize;
}

int64_t of_filesize(OutputFile *of)
{
    AVIOContext *pb = of->ctx->pb;
    int64_t ret = -1;

    if (of->mux->final_filesize)
        ret = of->mux->final_filesize;
    else if (pb) {
        ret = avio_size(pb);
        if (ret <= 0) // FIXME improve avio_size() so it works with non seekable output too
            ret = avio_tell(pb);
    }

    return ret;
}

AVChapter * const *
of_get_chapters(OutputFile *of, unsigned int *nb_chapters)
{
    *nb_chapters = of->ctx->nb_chapters;
    return of->ctx->chapters;
}
