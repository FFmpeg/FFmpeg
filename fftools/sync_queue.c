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

#include <stdint.h>
#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/error.h"
#include "libavutil/fifo.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"

#include "objpool.h"
#include "sync_queue.h"

typedef struct SyncQueueStream {
    AVFifo          *fifo;
    AVRational       tb;

    /* stream head: largest timestamp seen */
    int64_t          head_ts;
    int              limiting;
    /* no more frames will be sent for this stream */
    int              finished;

    uint64_t         frames_sent;
    uint64_t         frames_max;
} SyncQueueStream;

struct SyncQueue {
    enum SyncQueueType type;

    /* no more frames will be sent for any stream */
    int finished;
    /* sync head: the stream with the _smallest_ head timestamp
     * this stream determines which frames can be output */
    int head_stream;
    /* the finished stream with the smallest finish timestamp or -1 */
    int head_finished_stream;

    // maximum buffering duration in microseconds
    int64_t buf_size_us;

    SyncQueueStream *streams;
    unsigned int  nb_streams;

    // pool of preallocated frames to avoid constant allocations
    ObjPool *pool;
};

static void frame_move(const SyncQueue *sq, SyncQueueFrame dst,
                       SyncQueueFrame src)
{
    if (sq->type == SYNC_QUEUE_PACKETS)
        av_packet_move_ref(dst.p, src.p);
    else
        av_frame_move_ref(dst.f, src.f);
}

static int64_t frame_ts(const SyncQueue *sq, SyncQueueFrame frame)
{
    return (sq->type == SYNC_QUEUE_PACKETS) ?
           frame.p->pts + frame.p->duration :
           frame.f->pts + frame.f->duration;
}

static int frame_null(const SyncQueue *sq, SyncQueueFrame frame)
{
    return (sq->type == SYNC_QUEUE_PACKETS) ? (frame.p == NULL) : (frame.f == NULL);
}

static void finish_stream(SyncQueue *sq, unsigned int stream_idx)
{
    SyncQueueStream *st = &sq->streams[stream_idx];

    st->finished = 1;

    if (st->limiting && st->head_ts != AV_NOPTS_VALUE) {
        /* check if this stream is the new finished head */
        if (sq->head_finished_stream < 0 ||
            av_compare_ts(st->head_ts, st->tb,
                          sq->streams[sq->head_finished_stream].head_ts,
                          sq->streams[sq->head_finished_stream].tb) < 0) {
            sq->head_finished_stream = stream_idx;
        }

        /* mark as finished all streams that should no longer receive new frames,
         * due to them being ahead of some finished stream */
        st = &sq->streams[sq->head_finished_stream];
        for (unsigned int i = 0; i < sq->nb_streams; i++) {
            SyncQueueStream *st1 = &sq->streams[i];
            if (st != st1 && st1->head_ts != AV_NOPTS_VALUE &&
                av_compare_ts(st->head_ts, st->tb, st1->head_ts, st1->tb) <= 0)
                st1->finished = 1;
        }
    }

    /* mark the whole queue as finished if all streams are finished */
    for (unsigned int i = 0; i < sq->nb_streams; i++) {
        if (!sq->streams[i].finished)
            return;
    }
    sq->finished = 1;
}

static void queue_head_update(SyncQueue *sq)
{
    if (sq->head_stream < 0) {
        /* wait for one timestamp in each stream before determining
         * the queue head */
        for (unsigned int i = 0; i < sq->nb_streams; i++) {
            SyncQueueStream *st = &sq->streams[i];
            if (st->limiting && st->head_ts == AV_NOPTS_VALUE)
                return;
        }

        // placeholder value, correct one will be found below
        sq->head_stream = 0;
    }

    for (unsigned int i = 0; i < sq->nb_streams; i++) {
        SyncQueueStream *st_head  = &sq->streams[sq->head_stream];
        SyncQueueStream *st_other = &sq->streams[i];
        if (st_other->limiting && st_other->head_ts != AV_NOPTS_VALUE &&
            av_compare_ts(st_other->head_ts, st_other->tb,
                          st_head->head_ts,  st_head->tb) < 0)
            sq->head_stream = i;
    }
}

/* update this stream's head timestamp */
static void stream_update_ts(SyncQueue *sq, unsigned int stream_idx, int64_t ts)
{
    SyncQueueStream *st = &sq->streams[stream_idx];

    if (ts == AV_NOPTS_VALUE ||
        (st->head_ts != AV_NOPTS_VALUE && st->head_ts >= ts))
        return;

    st->head_ts = ts;

    /* if this stream is now ahead of some finished stream, then
     * this stream is also finished */
    if (sq->head_finished_stream >= 0 &&
        av_compare_ts(sq->streams[sq->head_finished_stream].head_ts,
                      sq->streams[sq->head_finished_stream].tb,
                      ts, st->tb) <= 0)
        finish_stream(sq, stream_idx);

    /* update the overall head timestamp if it could have changed */
    if (st->limiting &&
        (sq->head_stream < 0 || sq->head_stream == stream_idx))
        queue_head_update(sq);
}

/* If the queue for the given stream (or all streams when stream_idx=-1)
 * is overflowing, trigger a fake heartbeat on lagging streams.
 *
 * @return 1 if heartbeat triggered, 0 otherwise
 */
static int overflow_heartbeat(SyncQueue *sq, int stream_idx)
{
    SyncQueueStream *st;
    SyncQueueFrame frame;
    int64_t tail_ts = AV_NOPTS_VALUE;

    /* if no stream specified, pick the one that is most ahead */
    if (stream_idx < 0) {
        int64_t ts = AV_NOPTS_VALUE;

        for (int i = 0; i < sq->nb_streams; i++) {
            st = &sq->streams[i];
            if (st->head_ts != AV_NOPTS_VALUE &&
                (ts == AV_NOPTS_VALUE ||
                 av_compare_ts(ts, sq->streams[stream_idx].tb,
                               st->head_ts, st->tb) < 0)) {
                ts = st->head_ts;
                stream_idx = i;
            }
        }
        /* no stream has a timestamp yet -> nothing to do */
        if (stream_idx < 0)
            return 0;
    }

    st = &sq->streams[stream_idx];

    /* get the chosen stream's tail timestamp */
    for (size_t i = 0; tail_ts == AV_NOPTS_VALUE &&
                       av_fifo_peek(st->fifo, &frame, 1, i) >= 0; i++)
        tail_ts = frame_ts(sq, frame);

    /* overflow triggers when the tail is over specified duration behind the head */
    if (tail_ts == AV_NOPTS_VALUE || tail_ts >= st->head_ts ||
        av_rescale_q(st->head_ts - tail_ts, st->tb, AV_TIME_BASE_Q) < sq->buf_size_us)
        return 0;

    /* signal a fake timestamp for all streams that prevent tail_ts from being output */
    tail_ts++;
    for (unsigned int i = 0; i < sq->nb_streams; i++) {
        SyncQueueStream *st1 = &sq->streams[i];
        int64_t ts;

        if (st == st1 || st1->finished ||
            (st1->head_ts != AV_NOPTS_VALUE &&
             av_compare_ts(tail_ts, st->tb, st1->head_ts, st1->tb) <= 0))
            continue;

        ts = av_rescale_q(tail_ts, st->tb, st1->tb);
        if (st1->head_ts != AV_NOPTS_VALUE)
            ts = FFMAX(st1->head_ts + 1, ts);

        stream_update_ts(sq, i, ts);
    }

    return 1;
}

int sq_send(SyncQueue *sq, unsigned int stream_idx, SyncQueueFrame frame)
{
    SyncQueueStream *st;
    SyncQueueFrame dst;
    int64_t ts;
    int ret;

    av_assert0(stream_idx < sq->nb_streams);
    st = &sq->streams[stream_idx];

    av_assert0(st->tb.num > 0 && st->tb.den > 0);

    if (frame_null(sq, frame)) {
        finish_stream(sq, stream_idx);
        return 0;
    }
    if (st->finished)
        return AVERROR_EOF;

    ret = objpool_get(sq->pool, (void**)&dst);
    if (ret < 0)
        return ret;

    frame_move(sq, dst, frame);

    ts = frame_ts(sq, dst);

    ret = av_fifo_write(st->fifo, &dst, 1);
    if (ret < 0) {
        frame_move(sq, frame, dst);
        objpool_release(sq->pool, (void**)&dst);
        return ret;
    }

    stream_update_ts(sq, stream_idx, ts);

    st->frames_sent++;
    if (st->frames_sent >= st->frames_max)
        finish_stream(sq, stream_idx);

    return 0;
}

static int receive_for_stream(SyncQueue *sq, unsigned int stream_idx,
                              SyncQueueFrame frame)
{
    SyncQueueStream *st_head = sq->head_stream >= 0 ?
                               &sq->streams[sq->head_stream] : NULL;
    SyncQueueStream *st;

    av_assert0(stream_idx < sq->nb_streams);
    st = &sq->streams[stream_idx];

    if (av_fifo_can_read(st->fifo)) {
        SyncQueueFrame peek;
        int64_t ts;
        int cmp = 1;

        av_fifo_peek(st->fifo, &peek, 1, 0);
        ts = frame_ts(sq, peek);

        /* check if this stream's tail timestamp does not overtake
         * the overall queue head */
        if (ts != AV_NOPTS_VALUE && st_head)
            cmp = av_compare_ts(ts, st->tb, st_head->head_ts, st_head->tb);

        /* We can release frames that do not end after the queue head.
         * Frames with no timestamps are just passed through with no conditions.
         */
        if (cmp <= 0 || ts == AV_NOPTS_VALUE) {
            frame_move(sq, frame, peek);
            objpool_release(sq->pool, (void**)&peek);
            av_fifo_drain2(st->fifo, 1);
            return 0;
        }
    }

    return (sq->finished || (st->finished && !av_fifo_can_read(st->fifo))) ?
            AVERROR_EOF : AVERROR(EAGAIN);
}

static int receive_internal(SyncQueue *sq, int stream_idx, SyncQueueFrame frame)
{
    int nb_eof = 0;
    int ret;

    /* read a frame for a specific stream */
    if (stream_idx >= 0) {
        ret = receive_for_stream(sq, stream_idx, frame);
        return (ret < 0) ? ret : stream_idx;
    }

    /* read a frame for any stream with available output */
    for (unsigned int i = 0; i < sq->nb_streams; i++) {
        ret = receive_for_stream(sq, i, frame);
        if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
            nb_eof += (ret == AVERROR_EOF);
            continue;
        }
        return (ret < 0) ? ret : i;
    }

    return (nb_eof == sq->nb_streams) ? AVERROR_EOF : AVERROR(EAGAIN);
}

int sq_receive(SyncQueue *sq, int stream_idx, SyncQueueFrame frame)
{
    int ret = receive_internal(sq, stream_idx, frame);

    /* try again if the queue overflowed and triggered a fake heartbeat
     * for lagging streams */
    if (ret == AVERROR(EAGAIN) && overflow_heartbeat(sq, stream_idx))
        ret = receive_internal(sq, stream_idx, frame);

    return ret;
}

int sq_add_stream(SyncQueue *sq, int limiting)
{
    SyncQueueStream *tmp, *st;

    tmp = av_realloc_array(sq->streams, sq->nb_streams + 1, sizeof(*sq->streams));
    if (!tmp)
        return AVERROR(ENOMEM);
    sq->streams = tmp;

    st = &sq->streams[sq->nb_streams];
    memset(st, 0, sizeof(*st));

    st->fifo = av_fifo_alloc2(1, sizeof(SyncQueueFrame), AV_FIFO_FLAG_AUTO_GROW);
    if (!st->fifo)
        return AVERROR(ENOMEM);

    /* we set a valid default, so that a pathological stream that never
     * receives even a real timebase (and no frames) won't stall all other
     * streams forever; cf. overflow_heartbeat() */
    st->tb      = (AVRational){ 1, 1 };
    st->head_ts = AV_NOPTS_VALUE;
    st->frames_max = UINT64_MAX;
    st->limiting   = limiting;

    return sq->nb_streams++;
}

void sq_set_tb(SyncQueue *sq, unsigned int stream_idx, AVRational tb)
{
    SyncQueueStream *st;

    av_assert0(stream_idx < sq->nb_streams);
    st = &sq->streams[stream_idx];

    av_assert0(!av_fifo_can_read(st->fifo));

    if (st->head_ts != AV_NOPTS_VALUE)
        st->head_ts = av_rescale_q(st->head_ts, st->tb, tb);

    st->tb = tb;
}

void sq_limit_frames(SyncQueue *sq, unsigned int stream_idx, uint64_t frames)
{
    SyncQueueStream *st;

    av_assert0(stream_idx < sq->nb_streams);
    st = &sq->streams[stream_idx];

    st->frames_max = frames;
    if (st->frames_sent >= st->frames_max)
        finish_stream(sq, stream_idx);
}

SyncQueue *sq_alloc(enum SyncQueueType type, int64_t buf_size_us)
{
    SyncQueue *sq = av_mallocz(sizeof(*sq));

    if (!sq)
        return NULL;

    sq->type                 = type;
    sq->buf_size_us          = buf_size_us;

    sq->head_stream          = -1;
    sq->head_finished_stream = -1;

    sq->pool = (type == SYNC_QUEUE_PACKETS) ? objpool_alloc_packets() :
                                              objpool_alloc_frames();
    if (!sq->pool) {
        av_freep(&sq);
        return NULL;
    }

    return sq;
}

void sq_free(SyncQueue **psq)
{
    SyncQueue *sq = *psq;

    if (!sq)
        return;

    for (unsigned int i = 0; i < sq->nb_streams; i++) {
        SyncQueueFrame frame;
        while (av_fifo_read(sq->streams[i].fifo, &frame, 1) >= 0)
            objpool_release(sq->pool, (void**)&frame);

        av_fifo_freep2(&sq->streams[i].fifo);
    }

    av_freep(&sq->streams);

    objpool_free(&sq->pool);

    av_freep(psq);
}
