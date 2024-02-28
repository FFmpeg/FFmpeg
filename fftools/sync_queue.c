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
#include "libavutil/channel_layout.h"
#include "libavutil/cpu.h"
#include "libavutil/error.h"
#include "libavutil/fifo.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"
#include "libavutil/samplefmt.h"
#include "libavutil/timestamp.h"

#include "objpool.h"
#include "sync_queue.h"

/*
 * How this works:
 * --------------
 * time:   0    1    2    3    4    5    6    7    8    9    10   11   12   13
 *         -------------------------------------------------------------------
 *         |    |    |    |    |    |    |    |    |    |    |    |    |    |
 *         |    ┌───┐┌────────┐┌───┐┌─────────────┐
 * stream 0|    │d=1││  d=2   ││d=1││    d=3      │
 *         |    └───┘└────────┘└───┘└─────────────┘
 *         ┌───┐               ┌───────────────────────┐
 * stream 1│d=1│               │         d=5           │
 *         └───┘               └───────────────────────┘
 *         |    ┌───┐┌───┐┌───┐┌───┐
 * stream 2|    │d=1││d=1││d=1││d=1│ <- stream 2 is the head stream of the queue
 *         |    └───┘└───┘└───┘└───┘
 *                  ^              ^
 *          [stream 2 tail] [stream 2 head]
 *
 * We have N streams (N=3 in the diagram), each stream is a FIFO. The *tail* of
 * each FIFO is the frame with smallest end time, the *head* is the frame with
 * the largest end time. Frames submitted to the queue with sq_send() are placed
 * after the head, frames returned to the caller with sq_receive() are taken
 * from the tail.
 *
 * The head stream of the whole queue (SyncQueue.head_stream) is the limiting
 * stream with the *smallest* head timestamp, i.e. the stream whose source lags
 * furthest behind all other streams. It determines which frames can be output
 * from the queue.
 *
 * In the diagram, the head stream is 2, because it head time is t=5, while
 * streams 0 and 1 end at t=8 and t=9 respectively. All frames that _end_ at
 * or before t=5 can be output, i.e. the first 3 frames from stream 0, first
 * frame from stream 1, and all 4 frames from stream 2.
 */

typedef struct SyncQueueStream {
    AVFifo          *fifo;
    AVRational       tb;

    /* number of audio samples in fifo */
    uint64_t         samples_queued;
    /* stream head: largest timestamp seen */
    int64_t          head_ts;
    int              limiting;
    /* no more frames will be sent for this stream */
    int              finished;

    uint64_t         frames_sent;
    uint64_t         samples_sent;
    uint64_t         frames_max;
    int              frame_samples;
} SyncQueueStream;

struct SyncQueue {
    enum SyncQueueType type;

    void *logctx;

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

    int have_limiting;

    uintptr_t align_mask;
};

static void frame_move(const SyncQueue *sq, SyncQueueFrame dst,
                       SyncQueueFrame src)
{
    if (sq->type == SYNC_QUEUE_PACKETS)
        av_packet_move_ref(dst.p, src.p);
    else
        av_frame_move_ref(dst.f, src.f);
}

/**
 * Compute the end timestamp of a frame. If nb_samples is provided, consider
 * the frame to have this number of audio samples, otherwise use frame duration.
 */
static int64_t frame_end(const SyncQueue *sq, SyncQueueFrame frame, int nb_samples)
{
    if (nb_samples) {
        int64_t d = av_rescale_q(nb_samples, (AVRational){ 1, frame.f->sample_rate},
                                 frame.f->time_base);
        return frame.f->pts + d;
    }

    return (sq->type == SYNC_QUEUE_PACKETS) ?
           frame.p->pts + frame.p->duration :
           frame.f->pts + frame.f->duration;
}

static int frame_samples(const SyncQueue *sq, SyncQueueFrame frame)
{
    return (sq->type == SYNC_QUEUE_PACKETS) ? 0 : frame.f->nb_samples;
}

static int frame_null(const SyncQueue *sq, SyncQueueFrame frame)
{
    return (sq->type == SYNC_QUEUE_PACKETS) ? (frame.p == NULL) : (frame.f == NULL);
}

static void tb_update(const SyncQueue *sq, SyncQueueStream *st,
                      const SyncQueueFrame frame)
{
    AVRational tb = (sq->type == SYNC_QUEUE_PACKETS) ?
                    frame.p->time_base : frame.f->time_base;

    av_assert0(tb.num > 0 && tb.den > 0);

    if (tb.num == st->tb.num && tb.den == st->tb.den)
        return;

    // timebase should not change after the first frame
    av_assert0(!av_fifo_can_read(st->fifo));

    if (st->head_ts != AV_NOPTS_VALUE)
        st->head_ts = av_rescale_q(st->head_ts, st->tb, tb);

    st->tb = tb;
}

static void finish_stream(SyncQueue *sq, unsigned int stream_idx)
{
    SyncQueueStream *st = &sq->streams[stream_idx];

    if (!st->finished)
        av_log(sq->logctx, AV_LOG_DEBUG,
               "sq: finish %u; head ts %s\n", stream_idx,
               av_ts2timestr(st->head_ts, &st->tb));

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
                av_compare_ts(st->head_ts, st->tb, st1->head_ts, st1->tb) <= 0) {
                if (!st1->finished)
                    av_log(sq->logctx, AV_LOG_DEBUG,
                           "sq: finish secondary %u; head ts %s\n", i,
                           av_ts2timestr(st1->head_ts, &st1->tb));

                st1->finished = 1;
            }
        }
    }

    /* mark the whole queue as finished if all streams are finished */
    for (unsigned int i = 0; i < sq->nb_streams; i++) {
        if (!sq->streams[i].finished)
            return;
    }
    sq->finished = 1;

    av_log(sq->logctx, AV_LOG_DEBUG, "sq: finish queue\n");
}

static void queue_head_update(SyncQueue *sq)
{
    av_assert0(sq->have_limiting);

    if (sq->head_stream < 0) {
        unsigned first_limiting = UINT_MAX;

        /* wait for one timestamp in each stream before determining
         * the queue head */
        for (unsigned int i = 0; i < sq->nb_streams; i++) {
            SyncQueueStream *st = &sq->streams[i];
            if (!st->limiting)
                continue;
            if (st->head_ts == AV_NOPTS_VALUE)
                return;
            if (first_limiting == UINT_MAX)
                first_limiting = i;
        }

        // placeholder value, correct one will be found below
        av_assert0(first_limiting < UINT_MAX);
        sq->head_stream = first_limiting;
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
        tail_ts = frame_end(sq, frame, 0);

    /* overflow triggers when the tail is over specified duration behind the head */
    if (tail_ts == AV_NOPTS_VALUE || tail_ts >= st->head_ts ||
        av_rescale_q(st->head_ts - tail_ts, st->tb, AV_TIME_BASE_Q) < sq->buf_size_us)
        return 0;

    /* signal a fake timestamp for all streams that prevent tail_ts from being output */
    tail_ts++;
    for (unsigned int i = 0; i < sq->nb_streams; i++) {
        const SyncQueueStream *st1 = &sq->streams[i];
        int64_t ts;

        if (st == st1 || st1->finished ||
            (st1->head_ts != AV_NOPTS_VALUE &&
             av_compare_ts(tail_ts, st->tb, st1->head_ts, st1->tb) <= 0))
            continue;

        ts = av_rescale_q(tail_ts, st->tb, st1->tb);
        if (st1->head_ts != AV_NOPTS_VALUE)
            ts = FFMAX(st1->head_ts + 1, ts);

        av_log(sq->logctx, AV_LOG_DEBUG, "sq: %u overflow heardbeat %s -> %s\n",
               i, av_ts2timestr(st1->head_ts, &st1->tb), av_ts2timestr(ts, &st1->tb));

        stream_update_ts(sq, i, ts);
    }

    return 1;
}

int sq_send(SyncQueue *sq, unsigned int stream_idx, SyncQueueFrame frame)
{
    SyncQueueStream *st;
    SyncQueueFrame dst;
    int64_t ts;
    int ret, nb_samples;

    av_assert0(stream_idx < sq->nb_streams);
    st = &sq->streams[stream_idx];

    if (frame_null(sq, frame)) {
        av_log(sq->logctx, AV_LOG_DEBUG, "sq: %u EOF\n", stream_idx);
        finish_stream(sq, stream_idx);
        return 0;
    }
    if (st->finished)
        return AVERROR_EOF;

    tb_update(sq, st, frame);

    ret = objpool_get(sq->pool, (void**)&dst);
    if (ret < 0)
        return ret;

    frame_move(sq, dst, frame);

    nb_samples = frame_samples(sq, dst);
    // make sure frame duration is consistent with sample count
    if (nb_samples) {
        av_assert0(dst.f->sample_rate > 0);
        dst.f->duration = av_rescale_q(nb_samples, (AVRational){ 1, dst.f->sample_rate },
                                       dst.f->time_base);
    }

    ts = frame_end(sq, dst, 0);

    av_log(sq->logctx, AV_LOG_DEBUG, "sq: send %u ts %s\n", stream_idx,
           av_ts2timestr(ts, &st->tb));

    ret = av_fifo_write(st->fifo, &dst, 1);
    if (ret < 0) {
        frame_move(sq, frame, dst);
        objpool_release(sq->pool, (void**)&dst);
        return ret;
    }

    stream_update_ts(sq, stream_idx, ts);

    st->samples_queued += nb_samples;
    st->samples_sent   += nb_samples;

    if (st->frame_samples)
        st->frames_sent = st->samples_sent / st->frame_samples;
    else
        st->frames_sent++;

    if (st->frames_sent >= st->frames_max) {
        av_log(sq->logctx, AV_LOG_DEBUG, "sq: %u frames_max %"PRIu64" reached\n",
               stream_idx, st->frames_max);

        finish_stream(sq, stream_idx);
    }

    return 0;
}

static void offset_audio(AVFrame *f, int nb_samples)
{
    const int planar = av_sample_fmt_is_planar(f->format);
    const int planes = planar ? f->ch_layout.nb_channels : 1;
    const int    bps = av_get_bytes_per_sample(f->format);
    const int offset = nb_samples * bps * (planar ? 1 : f->ch_layout.nb_channels);

    av_assert0(bps > 0);
    av_assert0(nb_samples < f->nb_samples);

    for (int i = 0; i < planes; i++) {
        f->extended_data[i] += offset;
        if (i < FF_ARRAY_ELEMS(f->data))
            f->data[i] = f->extended_data[i];
    }
    f->linesize[0] -= offset;
    f->nb_samples  -= nb_samples;
    f->duration     = av_rescale_q(f->nb_samples, (AVRational){ 1, f->sample_rate },
                                   f->time_base);
    f->pts         += av_rescale_q(nb_samples,    (AVRational){ 1, f->sample_rate },
                                   f->time_base);
}

static int frame_is_aligned(const SyncQueue *sq, const AVFrame *frame)
{
    // only checks linesize[0], so only works for audio
    av_assert0(frame->nb_samples > 0);
    av_assert0(sq->align_mask);

    // only check data[0], because we always offset all data pointers
    // by the same offset, so if one is aligned, all are
    if (!((uintptr_t)frame->data[0] & sq->align_mask) &&
        !(frame->linesize[0]        & sq->align_mask) &&
        frame->linesize[0] > sq->align_mask)
        return 1;

    return 0;
}

static int receive_samples(SyncQueue *sq, SyncQueueStream *st,
                           AVFrame *dst, int nb_samples)
{
    SyncQueueFrame src;
    int ret;

    av_assert0(st->samples_queued >= nb_samples);

    ret = av_fifo_peek(st->fifo, &src, 1, 0);
    av_assert0(ret >= 0);

    // peeked frame has enough samples and its data is aligned
    // -> we can just make a reference and limit its sample count
    if (src.f->nb_samples > nb_samples && frame_is_aligned(sq, src.f)) {
        ret = av_frame_ref(dst, src.f);
        if (ret < 0)
            return ret;

        dst->nb_samples = nb_samples;
        offset_audio(src.f, nb_samples);
        st->samples_queued -= nb_samples;

        goto finish;
    }

    // otherwise allocate a new frame and copy the data
    ret = av_channel_layout_copy(&dst->ch_layout, &src.f->ch_layout);
    if (ret < 0)
        return ret;

    dst->format     = src.f->format;
    dst->nb_samples = nb_samples;

    ret = av_frame_get_buffer(dst, 0);
    if (ret < 0)
        goto fail;

    ret = av_frame_copy_props(dst, src.f);
    if (ret < 0)
        goto fail;

    dst->nb_samples = 0;
    while (dst->nb_samples < nb_samples) {
        int to_copy;

        ret = av_fifo_peek(st->fifo, &src, 1, 0);
        av_assert0(ret >= 0);

        to_copy = FFMIN(nb_samples - dst->nb_samples, src.f->nb_samples);

        av_samples_copy(dst->extended_data, src.f->extended_data, dst->nb_samples,
                        0, to_copy, dst->ch_layout.nb_channels, dst->format);

        if (to_copy < src.f->nb_samples)
            offset_audio(src.f, to_copy);
        else {
            av_frame_unref(src.f);
            objpool_release(sq->pool, (void**)&src);
            av_fifo_drain2(st->fifo, 1);
        }
        st->samples_queued -= to_copy;

        dst->nb_samples += to_copy;
    }

finish:
    dst->duration   = av_rescale_q(nb_samples, (AVRational){ 1, dst->sample_rate },
                                   dst->time_base);

    return 0;

fail:
    av_frame_unref(dst);
    return ret;
}

static int receive_for_stream(SyncQueue *sq, unsigned int stream_idx,
                              SyncQueueFrame frame)
{
    const SyncQueueStream *st_head = sq->head_stream >= 0 ?
                                     &sq->streams[sq->head_stream] : NULL;
    SyncQueueStream *st;

    av_assert0(stream_idx < sq->nb_streams);
    st = &sq->streams[stream_idx];

    if (av_fifo_can_read(st->fifo) &&
        (st->frame_samples <= st->samples_queued || st->finished)) {
        int nb_samples = st->frame_samples;
        SyncQueueFrame peek;
        int64_t ts;
        int cmp = 1;

        if (st->finished)
            nb_samples = FFMIN(nb_samples, st->samples_queued);

        av_fifo_peek(st->fifo, &peek, 1, 0);
        ts = frame_end(sq, peek, nb_samples);

        /* check if this stream's tail timestamp does not overtake
         * the overall queue head */
        if (ts != AV_NOPTS_VALUE && st_head)
            cmp = av_compare_ts(ts, st->tb, st_head->head_ts, st_head->tb);

        /* We can release frames that do not end after the queue head.
         * Frames with no timestamps are just passed through with no conditions.
         * Frames are also passed through when there are no limiting streams.
         */
        if (cmp <= 0 || ts == AV_NOPTS_VALUE || !sq->have_limiting) {
            if (nb_samples &&
                (nb_samples != peek.f->nb_samples || !frame_is_aligned(sq, peek.f))) {
                int ret = receive_samples(sq, st, frame.f, nb_samples);
                if (ret < 0)
                    return ret;
            } else {
                frame_move(sq, frame, peek);
                objpool_release(sq->pool, (void**)&peek);
                av_fifo_drain2(st->fifo, 1);
                av_assert0(st->samples_queued >= frame_samples(sq, frame));
                st->samples_queued -= frame_samples(sq, frame);
            }

            av_log(sq->logctx, AV_LOG_DEBUG,
                   "sq: receive %u ts %s queue head %d ts %s\n", stream_idx,
                   av_ts2timestr(frame_end(sq, frame, 0), &st->tb),
                   sq->head_stream,
                   st_head ? av_ts2timestr(st_head->head_ts, &st_head->tb) : "N/A");

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

    sq->have_limiting |= limiting;

    return sq->nb_streams++;
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

void sq_frame_samples(SyncQueue *sq, unsigned int stream_idx,
                      int frame_samples)
{
    SyncQueueStream *st;

    av_assert0(sq->type == SYNC_QUEUE_FRAMES);
    av_assert0(stream_idx < sq->nb_streams);
    st = &sq->streams[stream_idx];

    st->frame_samples = frame_samples;

    sq->align_mask = av_cpu_max_align() - 1;
}

SyncQueue *sq_alloc(enum SyncQueueType type, int64_t buf_size_us, void *logctx)
{
    SyncQueue *sq = av_mallocz(sizeof(*sq));

    if (!sq)
        return NULL;

    sq->type                 = type;
    sq->buf_size_us          = buf_size_us;
    sq->logctx               = logctx;

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
