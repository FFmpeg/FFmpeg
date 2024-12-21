/*
 * Inter-thread scheduling/synchronization.
 * Copyright (c) 2023 Anton Khirnov
 *
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
#include <stddef.h>
#include <stdint.h>

#include "cmdutils.h"
#include "ffmpeg_sched.h"
#include "ffmpeg_utils.h"
#include "sync_queue.h"
#include "thread_queue.h"

#include "libavcodec/packet.h"

#include "libavutil/avassert.h"
#include "libavutil/error.h"
#include "libavutil/fifo.h"
#include "libavutil/frame.h"
#include "libavutil/mem.h"
#include "libavutil/thread.h"
#include "libavutil/threadmessage.h"
#include "libavutil/time.h"

// 100 ms
// FIXME: some other value? make this dynamic?
#define SCHEDULE_TOLERANCE (100 * 1000)

enum QueueType {
    QUEUE_PACKETS,
    QUEUE_FRAMES,
};

typedef struct SchWaiter {
    pthread_mutex_t     lock;
    pthread_cond_t      cond;
    atomic_int          choked;

    // the following are internal state of schedule_update_locked() and must not
    // be accessed outside of it
    int                 choked_prev;
    int                 choked_next;
} SchWaiter;

typedef struct SchTask {
    Scheduler          *parent;
    SchedulerNode       node;

    SchThreadFunc       func;
    void               *func_arg;

    pthread_t           thread;
    int                 thread_running;
} SchTask;

typedef struct SchDecOutput {
    SchedulerNode      *dst;
    uint8_t            *dst_finished;
    unsigned         nb_dst;
} SchDecOutput;

typedef struct SchDec {
    const AVClass      *class;

    SchedulerNode       src;

    SchDecOutput       *outputs;
    unsigned         nb_outputs;

    SchTask             task;
    // Queue for receiving input packets, one stream.
    ThreadQueue        *queue;

    // Queue for sending post-flush end timestamps back to the source
    AVThreadMessageQueue *queue_end_ts;
    int                 expect_end_ts;

    // temporary storage used by sch_dec_send()
    AVFrame            *send_frame;
} SchDec;

typedef struct SchSyncQueue {
    SyncQueue          *sq;
    AVFrame            *frame;
    pthread_mutex_t     lock;

    unsigned           *enc_idx;
    unsigned         nb_enc_idx;
} SchSyncQueue;

typedef struct SchEnc {
    const AVClass      *class;

    SchedulerNode       src;
    SchedulerNode      *dst;
    uint8_t            *dst_finished;
    unsigned         nb_dst;

    // [0] - index of the sync queue in Scheduler.sq_enc,
    // [1] - index of this encoder in the sq
    int                 sq_idx[2];

    /* Opening encoders is somewhat nontrivial due to their interaction with
     * sync queues, which are (among other things) responsible for maintaining
     * constant audio frame size, when it is required by the encoder.
     *
     * Opening the encoder requires stream parameters, obtained from the first
     * frame. However, that frame cannot be properly chunked by the sync queue
     * without knowing the required frame size, which is only available after
     * opening the encoder.
     *
     * This apparent circular dependency is resolved in the following way:
     * - the caller creating the encoder gives us a callback which opens the
     *   encoder and returns the required frame size (if any)
     * - when the first frame is sent to the encoder, the sending thread
     *      - calls this callback, opening the encoder
     *      - passes the returned frame size to the sync queue
     */
    int               (*open_cb)(void *opaque, const AVFrame *frame);
    int                 opened;

    SchTask             task;
    // Queue for receiving input frames, one stream.
    ThreadQueue        *queue;
    // tq_send() to queue returned EOF
    int                 in_finished;

    // temporary storage used by sch_enc_send()
    AVPacket           *send_pkt;
} SchEnc;

typedef struct SchDemuxStream {
    SchedulerNode      *dst;
    uint8_t            *dst_finished;
    unsigned         nb_dst;
} SchDemuxStream;

typedef struct SchDemux {
    const AVClass      *class;

    SchDemuxStream     *streams;
    unsigned         nb_streams;

    SchTask             task;
    SchWaiter           waiter;

    // temporary storage used by sch_demux_send()
    AVPacket           *send_pkt;

    // protected by schedule_lock
    int                 task_exited;
} SchDemux;

typedef struct PreMuxQueue {
    /**
     * Queue for buffering the packets before the muxer task can be started.
     */
    AVFifo         *fifo;
    /**
     * Maximum number of packets in fifo.
     */
    int             max_packets;
    /*
     * The size of the AVPackets' buffers in queue.
     * Updated when a packet is either pushed or pulled from the queue.
     */
    size_t          data_size;
    /* Threshold after which max_packets will be in effect */
    size_t          data_threshold;
} PreMuxQueue;

typedef struct SchMuxStream {
    SchedulerNode       src;
    SchedulerNode       src_sched;

    unsigned           *sub_heartbeat_dst;
    unsigned         nb_sub_heartbeat_dst;

    PreMuxQueue         pre_mux_queue;

    // an EOF was generated while flushing the pre-mux queue
    int                 init_eof;

    ////////////////////////////////////////////////////////////
    // The following are protected by Scheduler.schedule_lock //

    /* dts+duration of the last packet sent to this stream
       in AV_TIME_BASE_Q */
    int64_t             last_dts;
    // this stream no longer accepts input
    int                 source_finished;
    ////////////////////////////////////////////////////////////
} SchMuxStream;

typedef struct SchMux {
    const AVClass      *class;

    SchMuxStream       *streams;
    unsigned         nb_streams;
    unsigned         nb_streams_ready;

    int               (*init)(void *arg);

    SchTask             task;
    /**
     * Set to 1 after starting the muxer task and flushing the
     * pre-muxing queues.
     * Set either before any tasks have started, or with
     * Scheduler.mux_ready_lock held.
     */
    atomic_int          mux_started;
    ThreadQueue        *queue;
    unsigned            queue_size;

    AVPacket           *sub_heartbeat_pkt;
} SchMux;

typedef struct SchFilterIn {
    SchedulerNode       src;
    SchedulerNode       src_sched;
    int                 send_finished;
    int                 receive_finished;
} SchFilterIn;

typedef struct SchFilterOut {
    SchedulerNode       dst;
} SchFilterOut;

typedef struct SchFilterGraph {
    const AVClass      *class;

    SchFilterIn        *inputs;
    unsigned         nb_inputs;
    atomic_uint      nb_inputs_finished_send;
    unsigned         nb_inputs_finished_receive;

    SchFilterOut       *outputs;
    unsigned         nb_outputs;

    SchTask             task;
    // input queue, nb_inputs+1 streams
    // last stream is control
    ThreadQueue        *queue;
    SchWaiter           waiter;

    // protected by schedule_lock
    unsigned            best_input;
    int                 task_exited;
} SchFilterGraph;

enum SchedulerState {
    SCH_STATE_UNINIT,
    SCH_STATE_STARTED,
    SCH_STATE_STOPPED,
};

struct Scheduler {
    const AVClass      *class;

    SchDemux           *demux;
    unsigned         nb_demux;

    SchMux             *mux;
    unsigned         nb_mux;

    unsigned         nb_mux_ready;
    pthread_mutex_t     mux_ready_lock;

    unsigned         nb_mux_done;
    unsigned            task_failed;
    pthread_mutex_t     finish_lock;
    pthread_cond_t      finish_cond;


    SchDec             *dec;
    unsigned         nb_dec;

    SchEnc             *enc;
    unsigned         nb_enc;

    SchSyncQueue       *sq_enc;
    unsigned         nb_sq_enc;

    SchFilterGraph     *filters;
    unsigned         nb_filters;

    char               *sdp_filename;
    int                 sdp_auto;

    enum SchedulerState state;
    atomic_int          terminate;

    pthread_mutex_t     schedule_lock;

    atomic_int_least64_t last_dts;
};

/**
 * Wait until this task is allowed to proceed.
 *
 * @retval 0 the caller should proceed
 * @retval 1 the caller should terminate
 */
static int waiter_wait(Scheduler *sch, SchWaiter *w)
{
    int terminate;

    if (!atomic_load(&w->choked))
        return 0;

    pthread_mutex_lock(&w->lock);

    while (atomic_load(&w->choked) && !atomic_load(&sch->terminate))
        pthread_cond_wait(&w->cond, &w->lock);

    terminate = atomic_load(&sch->terminate);

    pthread_mutex_unlock(&w->lock);

    return terminate;
}

static void waiter_set(SchWaiter *w, int choked)
{
    pthread_mutex_lock(&w->lock);

    atomic_store(&w->choked, choked);
    pthread_cond_signal(&w->cond);

    pthread_mutex_unlock(&w->lock);
}

static int waiter_init(SchWaiter *w)
{
    int ret;

    atomic_init(&w->choked, 0);

    ret = pthread_mutex_init(&w->lock, NULL);
    if (ret)
        return AVERROR(ret);

    ret = pthread_cond_init(&w->cond, NULL);
    if (ret)
        return AVERROR(ret);

    return 0;
}

static void waiter_uninit(SchWaiter *w)
{
    pthread_mutex_destroy(&w->lock);
    pthread_cond_destroy(&w->cond);
}

static int queue_alloc(ThreadQueue **ptq, unsigned nb_streams, unsigned queue_size,
                       enum QueueType type)
{
    ThreadQueue *tq;

    if (queue_size <= 0) {
        if (type == QUEUE_FRAMES)
            queue_size = DEFAULT_FRAME_THREAD_QUEUE_SIZE;
        else
            queue_size = DEFAULT_PACKET_THREAD_QUEUE_SIZE;
    }

    if (type == QUEUE_FRAMES) {
        // This queue length is used in the decoder code to ensure that
        // there are enough entries in fixed-size frame pools to account
        // for frames held in queues inside the ffmpeg utility.  If this
        // can ever dynamically change then the corresponding decode
        // code needs to be updated as well.
        av_assert0(queue_size == DEFAULT_FRAME_THREAD_QUEUE_SIZE);
    }

    tq = tq_alloc(nb_streams, queue_size,
                  (type == QUEUE_PACKETS) ? THREAD_QUEUE_PACKETS : THREAD_QUEUE_FRAMES);
    if (!tq)
        return AVERROR(ENOMEM);

    *ptq = tq;
    return 0;
}

static void *task_wrapper(void *arg);

static int task_start(SchTask *task)
{
    int ret;

    av_log(task->func_arg, AV_LOG_VERBOSE, "Starting thread...\n");

    av_assert0(!task->thread_running);

    ret = pthread_create(&task->thread, NULL, task_wrapper, task);
    if (ret) {
        av_log(task->func_arg, AV_LOG_ERROR, "pthread_create() failed: %s\n",
               strerror(ret));
        return AVERROR(ret);
    }

    task->thread_running = 1;
    return 0;
}

static void task_init(Scheduler *sch, SchTask *task, enum SchedulerNodeType type, unsigned idx,
                      SchThreadFunc func, void *func_arg)
{
    task->parent    = sch;

    task->node.type = type;
    task->node.idx  = idx;

    task->func      = func;
    task->func_arg  = func_arg;
}

static int64_t trailing_dts(const Scheduler *sch, int count_finished)
{
    int64_t min_dts = INT64_MAX;

    for (unsigned i = 0; i < sch->nb_mux; i++) {
        const SchMux *mux = &sch->mux[i];

        for (unsigned j = 0; j < mux->nb_streams; j++) {
            const SchMuxStream *ms = &mux->streams[j];

            if (ms->source_finished && !count_finished)
                continue;
            if (ms->last_dts == AV_NOPTS_VALUE)
                return AV_NOPTS_VALUE;

            min_dts = FFMIN(min_dts, ms->last_dts);
        }
    }

    return min_dts == INT64_MAX ? AV_NOPTS_VALUE : min_dts;
}

void sch_free(Scheduler **psch)
{
    Scheduler *sch = *psch;

    if (!sch)
        return;

    sch_stop(sch, NULL);

    for (unsigned i = 0; i < sch->nb_demux; i++) {
        SchDemux *d = &sch->demux[i];

        for (unsigned j = 0; j < d->nb_streams; j++) {
            SchDemuxStream *ds = &d->streams[j];
            av_freep(&ds->dst);
            av_freep(&ds->dst_finished);
        }
        av_freep(&d->streams);

        av_packet_free(&d->send_pkt);

        waiter_uninit(&d->waiter);
    }
    av_freep(&sch->demux);

    for (unsigned i = 0; i < sch->nb_mux; i++) {
        SchMux *mux = &sch->mux[i];

        for (unsigned j = 0; j < mux->nb_streams; j++) {
            SchMuxStream *ms = &mux->streams[j];

            if (ms->pre_mux_queue.fifo) {
                AVPacket *pkt;
                while (av_fifo_read(ms->pre_mux_queue.fifo, &pkt, 1) >= 0)
                    av_packet_free(&pkt);
                av_fifo_freep2(&ms->pre_mux_queue.fifo);
            }

            av_freep(&ms->sub_heartbeat_dst);
        }
        av_freep(&mux->streams);

        av_packet_free(&mux->sub_heartbeat_pkt);

        tq_free(&mux->queue);
    }
    av_freep(&sch->mux);

    for (unsigned i = 0; i < sch->nb_dec; i++) {
        SchDec *dec = &sch->dec[i];

        tq_free(&dec->queue);

        av_thread_message_queue_free(&dec->queue_end_ts);

        for (unsigned j = 0; j < dec->nb_outputs; j++) {
            SchDecOutput *o = &dec->outputs[j];

            av_freep(&o->dst);
            av_freep(&o->dst_finished);
        }

        av_freep(&dec->outputs);

        av_frame_free(&dec->send_frame);
    }
    av_freep(&sch->dec);

    for (unsigned i = 0; i < sch->nb_enc; i++) {
        SchEnc *enc = &sch->enc[i];

        tq_free(&enc->queue);

        av_packet_free(&enc->send_pkt);

        av_freep(&enc->dst);
        av_freep(&enc->dst_finished);
    }
    av_freep(&sch->enc);

    for (unsigned i = 0; i < sch->nb_sq_enc; i++) {
        SchSyncQueue *sq = &sch->sq_enc[i];
        sq_free(&sq->sq);
        av_frame_free(&sq->frame);
        pthread_mutex_destroy(&sq->lock);
        av_freep(&sq->enc_idx);
    }
    av_freep(&sch->sq_enc);

    for (unsigned i = 0; i < sch->nb_filters; i++) {
        SchFilterGraph *fg = &sch->filters[i];

        tq_free(&fg->queue);

        av_freep(&fg->inputs);
        av_freep(&fg->outputs);

        waiter_uninit(&fg->waiter);
    }
    av_freep(&sch->filters);

    av_freep(&sch->sdp_filename);

    pthread_mutex_destroy(&sch->schedule_lock);

    pthread_mutex_destroy(&sch->mux_ready_lock);

    pthread_mutex_destroy(&sch->finish_lock);
    pthread_cond_destroy(&sch->finish_cond);

    av_freep(psch);
}

static const AVClass scheduler_class = {
    .class_name = "Scheduler",
    .version    = LIBAVUTIL_VERSION_INT,
};

Scheduler *sch_alloc(void)
{
    Scheduler *sch;
    int ret;

    sch = av_mallocz(sizeof(*sch));
    if (!sch)
        return NULL;

    sch->class    = &scheduler_class;
    sch->sdp_auto = 1;

    ret = pthread_mutex_init(&sch->schedule_lock, NULL);
    if (ret)
        goto fail;

    ret = pthread_mutex_init(&sch->mux_ready_lock, NULL);
    if (ret)
        goto fail;

    ret = pthread_mutex_init(&sch->finish_lock, NULL);
    if (ret)
        goto fail;

    ret = pthread_cond_init(&sch->finish_cond, NULL);
    if (ret)
        goto fail;

    return sch;
fail:
    sch_free(&sch);
    return NULL;
}

int sch_sdp_filename(Scheduler *sch, const char *sdp_filename)
{
    av_freep(&sch->sdp_filename);
    sch->sdp_filename = av_strdup(sdp_filename);
    return sch->sdp_filename ? 0 : AVERROR(ENOMEM);
}

static const AVClass sch_mux_class = {
    .class_name                = "SchMux",
    .version                   = LIBAVUTIL_VERSION_INT,
    .parent_log_context_offset = offsetof(SchMux, task.func_arg),
};

int sch_add_mux(Scheduler *sch, SchThreadFunc func, int (*init)(void *),
                void *arg, int sdp_auto, unsigned thread_queue_size)
{
    const unsigned idx = sch->nb_mux;

    SchMux *mux;
    int ret;

    ret = GROW_ARRAY(sch->mux, sch->nb_mux);
    if (ret < 0)
        return ret;

    mux             = &sch->mux[idx];
    mux->class      = &sch_mux_class;
    mux->init       = init;
    mux->queue_size = thread_queue_size;

    task_init(sch, &mux->task, SCH_NODE_TYPE_MUX, idx, func, arg);

    sch->sdp_auto &= sdp_auto;

    return idx;
}

int sch_add_mux_stream(Scheduler *sch, unsigned mux_idx)
{
    SchMux       *mux;
    SchMuxStream *ms;
    unsigned      stream_idx;
    int ret;

    av_assert0(mux_idx < sch->nb_mux);
    mux = &sch->mux[mux_idx];

    ret = GROW_ARRAY(mux->streams, mux->nb_streams);
    if (ret < 0)
        return ret;
    stream_idx = mux->nb_streams - 1;

    ms = &mux->streams[stream_idx];

    ms->pre_mux_queue.fifo = av_fifo_alloc2(8, sizeof(AVPacket*), 0);
    if (!ms->pre_mux_queue.fifo)
        return AVERROR(ENOMEM);

    ms->last_dts = AV_NOPTS_VALUE;

    return stream_idx;
}

static const AVClass sch_demux_class = {
    .class_name                = "SchDemux",
    .version                   = LIBAVUTIL_VERSION_INT,
    .parent_log_context_offset = offsetof(SchDemux, task.func_arg),
};

int sch_add_demux(Scheduler *sch, SchThreadFunc func, void *ctx)
{
    const unsigned idx = sch->nb_demux;

    SchDemux *d;
    int ret;

    ret = GROW_ARRAY(sch->demux, sch->nb_demux);
    if (ret < 0)
        return ret;

    d = &sch->demux[idx];

    task_init(sch, &d->task, SCH_NODE_TYPE_DEMUX, idx, func, ctx);

    d->class    = &sch_demux_class;
    d->send_pkt = av_packet_alloc();
    if (!d->send_pkt)
        return AVERROR(ENOMEM);

    ret = waiter_init(&d->waiter);
    if (ret < 0)
        return ret;

    return idx;
}

int sch_add_demux_stream(Scheduler *sch, unsigned demux_idx)
{
    SchDemux *d;
    int ret;

    av_assert0(demux_idx < sch->nb_demux);
    d = &sch->demux[demux_idx];

    ret = GROW_ARRAY(d->streams, d->nb_streams);
    return ret < 0 ? ret : d->nb_streams - 1;
}

int sch_add_dec_output(Scheduler *sch, unsigned dec_idx)
{
    SchDec *dec;
    int ret;

    av_assert0(dec_idx < sch->nb_dec);
    dec = &sch->dec[dec_idx];

    ret = GROW_ARRAY(dec->outputs, dec->nb_outputs);
    if (ret < 0)
        return ret;

    return dec->nb_outputs - 1;
}

static const AVClass sch_dec_class = {
    .class_name                = "SchDec",
    .version                   = LIBAVUTIL_VERSION_INT,
    .parent_log_context_offset = offsetof(SchDec, task.func_arg),
};

int sch_add_dec(Scheduler *sch, SchThreadFunc func, void *ctx, int send_end_ts)
{
    const unsigned idx = sch->nb_dec;

    SchDec *dec;
    int ret;

    ret = GROW_ARRAY(sch->dec, sch->nb_dec);
    if (ret < 0)
        return ret;

    dec = &sch->dec[idx];

    task_init(sch, &dec->task, SCH_NODE_TYPE_DEC, idx, func, ctx);

    dec->class      = &sch_dec_class;
    dec->send_frame = av_frame_alloc();
    if (!dec->send_frame)
        return AVERROR(ENOMEM);

    ret = sch_add_dec_output(sch, idx);
    if (ret < 0)
        return ret;

    ret = queue_alloc(&dec->queue, 1, 0, QUEUE_PACKETS);
    if (ret < 0)
        return ret;

    if (send_end_ts) {
        ret = av_thread_message_queue_alloc(&dec->queue_end_ts, 1, sizeof(Timestamp));
        if (ret < 0)
            return ret;
    }

    return idx;
}

static const AVClass sch_enc_class = {
    .class_name                = "SchEnc",
    .version                   = LIBAVUTIL_VERSION_INT,
    .parent_log_context_offset = offsetof(SchEnc, task.func_arg),
};

int sch_add_enc(Scheduler *sch, SchThreadFunc func, void *ctx,
                int (*open_cb)(void *opaque, const AVFrame *frame))
{
    const unsigned idx = sch->nb_enc;

    SchEnc *enc;
    int ret;

    ret = GROW_ARRAY(sch->enc, sch->nb_enc);
    if (ret < 0)
        return ret;

    enc             = &sch->enc[idx];

    enc->class      = &sch_enc_class;
    enc->open_cb    = open_cb;
    enc->sq_idx[0]  = -1;
    enc->sq_idx[1]  = -1;

    task_init(sch, &enc->task, SCH_NODE_TYPE_ENC, idx, func, ctx);

    enc->send_pkt = av_packet_alloc();
    if (!enc->send_pkt)
        return AVERROR(ENOMEM);

    ret = queue_alloc(&enc->queue, 1, 0, QUEUE_FRAMES);
    if (ret < 0)
        return ret;

    return idx;
}

static const AVClass sch_fg_class = {
    .class_name                = "SchFilterGraph",
    .version                   = LIBAVUTIL_VERSION_INT,
    .parent_log_context_offset = offsetof(SchFilterGraph, task.func_arg),
};

int sch_add_filtergraph(Scheduler *sch, unsigned nb_inputs, unsigned nb_outputs,
                        SchThreadFunc func, void *ctx)
{
    const unsigned idx = sch->nb_filters;

    SchFilterGraph *fg;
    int ret;

    ret = GROW_ARRAY(sch->filters, sch->nb_filters);
    if (ret < 0)
        return ret;
    fg = &sch->filters[idx];

    fg->class = &sch_fg_class;

    task_init(sch, &fg->task, SCH_NODE_TYPE_FILTER_IN, idx, func, ctx);

    if (nb_inputs) {
        fg->inputs = av_calloc(nb_inputs, sizeof(*fg->inputs));
        if (!fg->inputs)
            return AVERROR(ENOMEM);
        fg->nb_inputs = nb_inputs;
    }

    if (nb_outputs) {
        fg->outputs = av_calloc(nb_outputs, sizeof(*fg->outputs));
        if (!fg->outputs)
            return AVERROR(ENOMEM);
        fg->nb_outputs = nb_outputs;
    }

    ret = waiter_init(&fg->waiter);
    if (ret < 0)
        return ret;

    ret = queue_alloc(&fg->queue, fg->nb_inputs + 1, 0, QUEUE_FRAMES);
    if (ret < 0)
        return ret;

    return idx;
}

int sch_add_sq_enc(Scheduler *sch, uint64_t buf_size_us, void *logctx)
{
    SchSyncQueue *sq;
    int ret;

    ret = GROW_ARRAY(sch->sq_enc, sch->nb_sq_enc);
    if (ret < 0)
        return ret;
    sq = &sch->sq_enc[sch->nb_sq_enc - 1];

    sq->sq = sq_alloc(SYNC_QUEUE_FRAMES, buf_size_us, logctx);
    if (!sq->sq)
        return AVERROR(ENOMEM);

    sq->frame = av_frame_alloc();
    if (!sq->frame)
        return AVERROR(ENOMEM);

    ret = pthread_mutex_init(&sq->lock, NULL);
    if (ret)
        return AVERROR(ret);

    return sq - sch->sq_enc;
}

int sch_sq_add_enc(Scheduler *sch, unsigned sq_idx, unsigned enc_idx,
                   int limiting, uint64_t max_frames)
{
    SchSyncQueue *sq;
    SchEnc *enc;
    int ret;

    av_assert0(sq_idx < sch->nb_sq_enc);
    sq = &sch->sq_enc[sq_idx];

    av_assert0(enc_idx < sch->nb_enc);
    enc = &sch->enc[enc_idx];

    ret = GROW_ARRAY(sq->enc_idx, sq->nb_enc_idx);
    if (ret < 0)
        return ret;
    sq->enc_idx[sq->nb_enc_idx - 1] = enc_idx;

    ret = sq_add_stream(sq->sq, limiting);
    if (ret < 0)
        return ret;

    enc->sq_idx[0] = sq_idx;
    enc->sq_idx[1] = ret;

    if (max_frames != INT64_MAX)
        sq_limit_frames(sq->sq, enc->sq_idx[1], max_frames);

    return 0;
}

int sch_connect(Scheduler *sch, SchedulerNode src, SchedulerNode dst)
{
    int ret;

    switch (src.type) {
    case SCH_NODE_TYPE_DEMUX: {
        SchDemuxStream *ds;

        av_assert0(src.idx < sch->nb_demux &&
                   src.idx_stream < sch->demux[src.idx].nb_streams);
        ds = &sch->demux[src.idx].streams[src.idx_stream];

        ret = GROW_ARRAY(ds->dst, ds->nb_dst);
        if (ret < 0)
            return ret;

        ds->dst[ds->nb_dst - 1] = dst;

        // demuxed packets go to decoding or streamcopy
        switch (dst.type) {
        case SCH_NODE_TYPE_DEC: {
            SchDec *dec;

            av_assert0(dst.idx < sch->nb_dec);
            dec = &sch->dec[dst.idx];

            av_assert0(!dec->src.type);
            dec->src = src;
            break;
            }
        case SCH_NODE_TYPE_MUX: {
            SchMuxStream *ms;

            av_assert0(dst.idx < sch->nb_mux &&
                       dst.idx_stream < sch->mux[dst.idx].nb_streams);
            ms = &sch->mux[dst.idx].streams[dst.idx_stream];

            av_assert0(!ms->src.type);
            ms->src = src;

            break;
            }
        default: av_assert0(0);
        }

        break;
        }
    case SCH_NODE_TYPE_DEC: {
        SchDec *dec;
        SchDecOutput *o;

        av_assert0(src.idx < sch->nb_dec);
        dec = &sch->dec[src.idx];

        av_assert0(src.idx_stream < dec->nb_outputs);
        o = &dec->outputs[src.idx_stream];

        ret = GROW_ARRAY(o->dst, o->nb_dst);
        if (ret < 0)
            return ret;

        o->dst[o->nb_dst - 1] = dst;

        // decoded frames go to filters or encoding
        switch (dst.type) {
        case SCH_NODE_TYPE_FILTER_IN: {
            SchFilterIn *fi;

            av_assert0(dst.idx < sch->nb_filters &&
                       dst.idx_stream < sch->filters[dst.idx].nb_inputs);
            fi = &sch->filters[dst.idx].inputs[dst.idx_stream];

            av_assert0(!fi->src.type);
            fi->src = src;
            break;
            }
        case SCH_NODE_TYPE_ENC: {
            SchEnc *enc;

            av_assert0(dst.idx < sch->nb_enc);
            enc = &sch->enc[dst.idx];

            av_assert0(!enc->src.type);
            enc->src = src;
            break;
            }
        default: av_assert0(0);
        }

        break;
        }
    case SCH_NODE_TYPE_FILTER_OUT: {
        SchFilterOut *fo;

        av_assert0(src.idx < sch->nb_filters &&
                   src.idx_stream < sch->filters[src.idx].nb_outputs);
        fo = &sch->filters[src.idx].outputs[src.idx_stream];

        av_assert0(!fo->dst.type);
        fo->dst = dst;

        // filtered frames go to encoding or another filtergraph
        switch (dst.type) {
        case SCH_NODE_TYPE_ENC: {
            SchEnc *enc;

            av_assert0(dst.idx < sch->nb_enc);
            enc = &sch->enc[dst.idx];

            av_assert0(!enc->src.type);
            enc->src = src;
            break;
            }
        case SCH_NODE_TYPE_FILTER_IN: {
            SchFilterIn *fi;

            av_assert0(dst.idx < sch->nb_filters &&
                       dst.idx_stream < sch->filters[dst.idx].nb_inputs);
            fi = &sch->filters[dst.idx].inputs[dst.idx_stream];

            av_assert0(!fi->src.type);
            fi->src = src;
            break;
            }
        default: av_assert0(0);
        }


        break;
        }
    case SCH_NODE_TYPE_ENC: {
        SchEnc       *enc;

        av_assert0(src.idx < sch->nb_enc);
        enc = &sch->enc[src.idx];

        ret = GROW_ARRAY(enc->dst, enc->nb_dst);
        if (ret < 0)
            return ret;

        enc->dst[enc->nb_dst - 1] = dst;

        // encoding packets go to muxing or decoding
        switch (dst.type) {
        case SCH_NODE_TYPE_MUX: {
            SchMuxStream *ms;

            av_assert0(dst.idx        < sch->nb_mux &&
                       dst.idx_stream < sch->mux[dst.idx].nb_streams);
            ms = &sch->mux[dst.idx].streams[dst.idx_stream];

            av_assert0(!ms->src.type);
            ms->src  = src;

            break;
            }
        case SCH_NODE_TYPE_DEC: {
            SchDec *dec;

            av_assert0(dst.idx < sch->nb_dec);
            dec = &sch->dec[dst.idx];

            av_assert0(!dec->src.type);
            dec->src = src;

            break;
            }
        default: av_assert0(0);
        }

        break;
        }
    default: av_assert0(0);
    }

    return 0;
}

static int mux_task_start(SchMux *mux)
{
    int ret = 0;

    ret = task_start(&mux->task);
    if (ret < 0)
        return ret;

    /* flush the pre-muxing queues */
    while (1) {
        int       min_stream = -1;
        Timestamp min_ts     = { .ts = AV_NOPTS_VALUE };

        AVPacket *pkt;

        // find the stream with the earliest dts or EOF in pre-muxing queue
        for (unsigned i = 0; i < mux->nb_streams; i++) {
            SchMuxStream *ms = &mux->streams[i];

            if (av_fifo_peek(ms->pre_mux_queue.fifo, &pkt, 1, 0) < 0)
                continue;

            if (!pkt || pkt->dts == AV_NOPTS_VALUE) {
                min_stream = i;
                break;
            }

            if (min_ts.ts == AV_NOPTS_VALUE ||
                av_compare_ts(min_ts.ts, min_ts.tb, pkt->dts, pkt->time_base) > 0) {
                min_stream = i;
                min_ts     = (Timestamp){ .ts = pkt->dts, .tb = pkt->time_base };
            }
        }

        if (min_stream >= 0) {
            SchMuxStream *ms = &mux->streams[min_stream];

            ret = av_fifo_read(ms->pre_mux_queue.fifo, &pkt, 1);
            av_assert0(ret >= 0);

            if (pkt) {
                if (!ms->init_eof)
                    ret = tq_send(mux->queue, min_stream, pkt);
                av_packet_free(&pkt);
                if (ret == AVERROR_EOF)
                    ms->init_eof = 1;
                else if (ret < 0)
                    return ret;
            } else
                tq_send_finish(mux->queue, min_stream);

            continue;
        }

        break;
    }

    atomic_store(&mux->mux_started, 1);

    return 0;
}

int print_sdp(const char *filename);

static int mux_init(Scheduler *sch, SchMux *mux)
{
    int ret;

    ret = mux->init(mux->task.func_arg);
    if (ret < 0)
        return ret;

    sch->nb_mux_ready++;

    if (sch->sdp_filename || sch->sdp_auto) {
        if (sch->nb_mux_ready < sch->nb_mux)
            return 0;

        ret = print_sdp(sch->sdp_filename);
        if (ret < 0) {
            av_log(sch, AV_LOG_ERROR, "Error writing the SDP.\n");
            return ret;
        }

        /* SDP is written only after all the muxers are ready, so now we
         * start ALL the threads */
        for (unsigned i = 0; i < sch->nb_mux; i++) {
            ret = mux_task_start(&sch->mux[i]);
            if (ret < 0)
                return ret;
        }
    } else {
        ret = mux_task_start(mux);
        if (ret < 0)
            return ret;
    }

    return 0;
}

void sch_mux_stream_buffering(Scheduler *sch, unsigned mux_idx, unsigned stream_idx,
                              size_t data_threshold, int max_packets)
{
    SchMux       *mux;
    SchMuxStream *ms;

    av_assert0(mux_idx < sch->nb_mux);
    mux = &sch->mux[mux_idx];

    av_assert0(stream_idx < mux->nb_streams);
    ms = &mux->streams[stream_idx];

    ms->pre_mux_queue.max_packets    = max_packets;
    ms->pre_mux_queue.data_threshold = data_threshold;
}

int sch_mux_stream_ready(Scheduler *sch, unsigned mux_idx, unsigned stream_idx)
{
    SchMux *mux;
    int ret = 0;

    av_assert0(mux_idx < sch->nb_mux);
    mux = &sch->mux[mux_idx];

    av_assert0(stream_idx < mux->nb_streams);

    pthread_mutex_lock(&sch->mux_ready_lock);

    av_assert0(mux->nb_streams_ready < mux->nb_streams);

    // this may be called during initialization - do not start
    // threads before sch_start() is called
    if (++mux->nb_streams_ready == mux->nb_streams &&
        sch->state >= SCH_STATE_STARTED)
        ret = mux_init(sch, mux);

    pthread_mutex_unlock(&sch->mux_ready_lock);

    return ret;
}

int sch_mux_sub_heartbeat_add(Scheduler *sch, unsigned mux_idx, unsigned stream_idx,
                              unsigned dec_idx)
{
    SchMux       *mux;
    SchMuxStream *ms;
    int ret = 0;

    av_assert0(mux_idx < sch->nb_mux);
    mux = &sch->mux[mux_idx];

    av_assert0(stream_idx < mux->nb_streams);
    ms = &mux->streams[stream_idx];

    ret = GROW_ARRAY(ms->sub_heartbeat_dst, ms->nb_sub_heartbeat_dst);
    if (ret < 0)
        return ret;

    av_assert0(dec_idx < sch->nb_dec);
    ms->sub_heartbeat_dst[ms->nb_sub_heartbeat_dst - 1] = dec_idx;

    if (!mux->sub_heartbeat_pkt) {
        mux->sub_heartbeat_pkt = av_packet_alloc();
        if (!mux->sub_heartbeat_pkt)
            return AVERROR(ENOMEM);
    }

    return 0;
}

static void unchoke_for_stream(Scheduler *sch, SchedulerNode src)
{
    while (1) {
        SchFilterGraph *fg;

        // fed directly by a demuxer (i.e. not through a filtergraph)
        if (src.type == SCH_NODE_TYPE_DEMUX) {
            sch->demux[src.idx].waiter.choked_next = 0;
            return;
        }

        av_assert0(src.type == SCH_NODE_TYPE_FILTER_OUT);
        fg = &sch->filters[src.idx];

        // the filtergraph contains internal sources and
        // requested to be scheduled directly
        if (fg->best_input == fg->nb_inputs) {
            fg->waiter.choked_next = 0;
            return;
        }

        src = fg->inputs[fg->best_input].src_sched;
    }
}

static void schedule_update_locked(Scheduler *sch)
{
    int64_t dts;
    int have_unchoked = 0;

    // on termination request all waiters are choked,
    // we are not to unchoke them
    if (atomic_load(&sch->terminate))
        return;

    dts = trailing_dts(sch, 0);

    atomic_store(&sch->last_dts, dts);

    // initialize our internal state
    for (unsigned type = 0; type < 2; type++)
        for (unsigned i = 0; i < (type ? sch->nb_filters : sch->nb_demux); i++) {
            SchWaiter *w = type ? &sch->filters[i].waiter : &sch->demux[i].waiter;
            w->choked_prev = atomic_load(&w->choked);
            w->choked_next = 1;
        }

    // figure out the sources that are allowed to proceed
    for (unsigned i = 0; i < sch->nb_mux; i++) {
        SchMux *mux = &sch->mux[i];

        for (unsigned j = 0; j < mux->nb_streams; j++) {
            SchMuxStream *ms = &mux->streams[j];

            // unblock sources for output streams that are not finished
            // and not too far ahead of the trailing stream
            if (ms->source_finished)
                continue;
            if (dts == AV_NOPTS_VALUE && ms->last_dts != AV_NOPTS_VALUE)
                continue;
            if (dts != AV_NOPTS_VALUE && ms->last_dts - dts >= SCHEDULE_TOLERANCE)
                continue;

            // resolve the source to unchoke
            unchoke_for_stream(sch, ms->src_sched);
            have_unchoked = 1;
        }
    }

    // make sure to unchoke at least one source, if still available
    for (unsigned type = 0; !have_unchoked && type < 2; type++)
        for (unsigned i = 0; i < (type ? sch->nb_filters : sch->nb_demux); i++) {
            int exited = type ? sch->filters[i].task_exited : sch->demux[i].task_exited;
            SchWaiter *w = type ? &sch->filters[i].waiter : &sch->demux[i].waiter;
            if (!exited) {
                w->choked_next = 0;
                have_unchoked  = 1;
                break;
            }
        }


    for (unsigned type = 0; type < 2; type++)
        for (unsigned i = 0; i < (type ? sch->nb_filters : sch->nb_demux); i++) {
            SchWaiter *w = type ? &sch->filters[i].waiter : &sch->demux[i].waiter;
            if (w->choked_prev != w->choked_next)
                waiter_set(w, w->choked_next);
        }

}

enum {
    CYCLE_NODE_NEW = 0,
    CYCLE_NODE_STARTED,
    CYCLE_NODE_DONE,
};

static int
check_acyclic_for_output(const Scheduler *sch, SchedulerNode src,
                         uint8_t *filters_visited, SchedulerNode *filters_stack)
{
    unsigned nb_filters_stack = 0;

    memset(filters_visited, 0, sch->nb_filters * sizeof(*filters_visited));

    while (1) {
        const SchFilterGraph *fg = &sch->filters[src.idx];

        filters_visited[src.idx] = CYCLE_NODE_STARTED;

        // descend into every input, depth first
        if (src.idx_stream < fg->nb_inputs) {
            const SchFilterIn *fi = &fg->inputs[src.idx_stream++];

            // connected to demuxer, no cycles possible
            if (fi->src_sched.type == SCH_NODE_TYPE_DEMUX)
                continue;

            // otherwise connected to another filtergraph
            av_assert0(fi->src_sched.type == SCH_NODE_TYPE_FILTER_OUT);

            // found a cycle
            if (filters_visited[fi->src_sched.idx] == CYCLE_NODE_STARTED)
                return AVERROR(EINVAL);

            // place current position on stack and descend
            av_assert0(nb_filters_stack < sch->nb_filters);
            filters_stack[nb_filters_stack++] = src;
            src = (SchedulerNode){ .idx = fi->src_sched.idx, .idx_stream = 0 };
            continue;
        }

        filters_visited[src.idx] = CYCLE_NODE_DONE;

        // previous search finished,
        if (nb_filters_stack) {
            src = filters_stack[--nb_filters_stack];
            continue;
        }
        return 0;
    }
}

static int check_acyclic(Scheduler *sch)
{
    uint8_t       *filters_visited = NULL;
    SchedulerNode *filters_stack   = NULL;

    int ret = 0;

    if (!sch->nb_filters)
        return 0;

    filters_visited = av_malloc_array(sch->nb_filters, sizeof(*filters_visited));
    if (!filters_visited)
        return AVERROR(ENOMEM);

    filters_stack = av_malloc_array(sch->nb_filters, sizeof(*filters_stack));
    if (!filters_stack) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    // trace the transcoding graph upstream from every filtegraph
    for (unsigned i = 0; i < sch->nb_filters; i++) {
        ret = check_acyclic_for_output(sch, (SchedulerNode){ .idx = i },
                                       filters_visited, filters_stack);
        if (ret < 0) {
            av_log(&sch->filters[i], AV_LOG_ERROR, "Transcoding graph has a cycle\n");
            goto fail;
        }
    }

fail:
    av_freep(&filters_visited);
    av_freep(&filters_stack);
    return ret;
}

static int start_prepare(Scheduler *sch)
{
    int ret;

    for (unsigned i = 0; i < sch->nb_demux; i++) {
        SchDemux *d = &sch->demux[i];

        for (unsigned j = 0; j < d->nb_streams; j++) {
            SchDemuxStream *ds = &d->streams[j];

            if (!ds->nb_dst) {
                av_log(d, AV_LOG_ERROR,
                       "Demuxer stream %u not connected to any sink\n", j);
                return AVERROR(EINVAL);
            }

            ds->dst_finished = av_calloc(ds->nb_dst, sizeof(*ds->dst_finished));
            if (!ds->dst_finished)
                return AVERROR(ENOMEM);
        }
    }

    for (unsigned i = 0; i < sch->nb_dec; i++) {
        SchDec *dec = &sch->dec[i];

        if (!dec->src.type) {
            av_log(dec, AV_LOG_ERROR,
                   "Decoder not connected to a source\n");
            return AVERROR(EINVAL);
        }

        for (unsigned j = 0; j < dec->nb_outputs; j++) {
            SchDecOutput *o = &dec->outputs[j];

            if (!o->nb_dst) {
                av_log(dec, AV_LOG_ERROR,
                       "Decoder output %u not connected to any sink\n", j);
                return AVERROR(EINVAL);
            }

            o->dst_finished = av_calloc(o->nb_dst, sizeof(*o->dst_finished));
            if (!o->dst_finished)
                return AVERROR(ENOMEM);
        }
    }

    for (unsigned i = 0; i < sch->nb_enc; i++) {
        SchEnc *enc = &sch->enc[i];

        if (!enc->src.type) {
            av_log(enc, AV_LOG_ERROR,
                   "Encoder not connected to a source\n");
            return AVERROR(EINVAL);
        }
        if (!enc->nb_dst) {
            av_log(enc, AV_LOG_ERROR,
                   "Encoder not connected to any sink\n");
            return AVERROR(EINVAL);
        }

        enc->dst_finished = av_calloc(enc->nb_dst, sizeof(*enc->dst_finished));
        if (!enc->dst_finished)
            return AVERROR(ENOMEM);
    }

    for (unsigned i = 0; i < sch->nb_mux; i++) {
        SchMux *mux = &sch->mux[i];

        for (unsigned j = 0; j < mux->nb_streams; j++) {
            SchMuxStream *ms = &mux->streams[j];

            switch (ms->src.type) {
            case SCH_NODE_TYPE_ENC: {
                SchEnc *enc = &sch->enc[ms->src.idx];
                if (enc->src.type == SCH_NODE_TYPE_DEC) {
                    ms->src_sched = sch->dec[enc->src.idx].src;
                    av_assert0(ms->src_sched.type == SCH_NODE_TYPE_DEMUX);
                } else {
                    ms->src_sched = enc->src;
                    av_assert0(ms->src_sched.type == SCH_NODE_TYPE_FILTER_OUT);
                }
                break;
                }
            case SCH_NODE_TYPE_DEMUX:
                ms->src_sched = ms->src;
                break;
            default:
                av_log(mux, AV_LOG_ERROR,
                       "Muxer stream #%u not connected to a source\n", j);
                return AVERROR(EINVAL);
            }
        }

        ret = queue_alloc(&mux->queue, mux->nb_streams, mux->queue_size,
                          QUEUE_PACKETS);
        if (ret < 0)
            return ret;
    }

    for (unsigned i = 0; i < sch->nb_filters; i++) {
        SchFilterGraph *fg = &sch->filters[i];

        for (unsigned j = 0; j < fg->nb_inputs; j++) {
            SchFilterIn *fi = &fg->inputs[j];
            SchDec     *dec;

            if (!fi->src.type) {
                av_log(fg, AV_LOG_ERROR,
                       "Filtergraph input %u not connected to a source\n", j);
                return AVERROR(EINVAL);
            }

            if (fi->src.type == SCH_NODE_TYPE_FILTER_OUT)
                fi->src_sched = fi->src;
            else {
                av_assert0(fi->src.type == SCH_NODE_TYPE_DEC);
                dec = &sch->dec[fi->src.idx];

                switch (dec->src.type) {
                case SCH_NODE_TYPE_DEMUX: fi->src_sched = dec->src;                   break;
                case SCH_NODE_TYPE_ENC:   fi->src_sched = sch->enc[dec->src.idx].src; break;
                default: av_assert0(0);
                }
            }
        }

        for (unsigned j = 0; j < fg->nb_outputs; j++) {
            SchFilterOut *fo = &fg->outputs[j];

            if (!fo->dst.type) {
                av_log(fg, AV_LOG_ERROR,
                       "Filtergraph %u output %u not connected to a sink\n", i, j);
                return AVERROR(EINVAL);
            }
        }
    }

    // Check that the transcoding graph has no cycles.
    ret = check_acyclic(sch);
    if (ret < 0)
        return ret;

    return 0;
}

int sch_start(Scheduler *sch)
{
    int ret;

    ret = start_prepare(sch);
    if (ret < 0)
        return ret;

    av_assert0(sch->state == SCH_STATE_UNINIT);
    sch->state = SCH_STATE_STARTED;

    for (unsigned i = 0; i < sch->nb_mux; i++) {
        SchMux *mux = &sch->mux[i];

        if (mux->nb_streams_ready == mux->nb_streams) {
            ret = mux_init(sch, mux);
            if (ret < 0)
                goto fail;
        }
    }

    for (unsigned i = 0; i < sch->nb_enc; i++) {
        SchEnc *enc = &sch->enc[i];

        ret = task_start(&enc->task);
        if (ret < 0)
            goto fail;
    }

    for (unsigned i = 0; i < sch->nb_filters; i++) {
        SchFilterGraph *fg = &sch->filters[i];

        ret = task_start(&fg->task);
        if (ret < 0)
            goto fail;
    }

    for (unsigned i = 0; i < sch->nb_dec; i++) {
        SchDec *dec = &sch->dec[i];

        ret = task_start(&dec->task);
        if (ret < 0)
            goto fail;
    }

    for (unsigned i = 0; i < sch->nb_demux; i++) {
        SchDemux *d = &sch->demux[i];

        if (!d->nb_streams)
            continue;

        ret = task_start(&d->task);
        if (ret < 0)
            goto fail;
    }

    pthread_mutex_lock(&sch->schedule_lock);
    schedule_update_locked(sch);
    pthread_mutex_unlock(&sch->schedule_lock);

    return 0;
fail:
    sch_stop(sch, NULL);
    return ret;
}

int sch_wait(Scheduler *sch, uint64_t timeout_us, int64_t *transcode_ts)
{
    int ret;

    // convert delay to absolute timestamp
    timeout_us += av_gettime();

    pthread_mutex_lock(&sch->finish_lock);

    if (sch->nb_mux_done < sch->nb_mux) {
        struct timespec tv = { .tv_sec  =  timeout_us / 1000000,
                               .tv_nsec = (timeout_us % 1000000) * 1000 };
        pthread_cond_timedwait(&sch->finish_cond, &sch->finish_lock, &tv);
    }

    // abort transcoding if any task failed
    ret = sch->nb_mux_done == sch->nb_mux || sch->task_failed;

    pthread_mutex_unlock(&sch->finish_lock);

    *transcode_ts = atomic_load(&sch->last_dts);

    return ret;
}

static int enc_open(Scheduler *sch, SchEnc *enc, const AVFrame *frame)
{
    int ret;

    ret = enc->open_cb(enc->task.func_arg, frame);
    if (ret < 0)
        return ret;

    // ret>0 signals audio frame size, which means sync queue must
    // have been enabled during encoder creation
    if (ret > 0) {
        SchSyncQueue *sq;

        av_assert0(enc->sq_idx[0] >= 0);
        sq = &sch->sq_enc[enc->sq_idx[0]];

        pthread_mutex_lock(&sq->lock);

        sq_frame_samples(sq->sq, enc->sq_idx[1], ret);

        pthread_mutex_unlock(&sq->lock);
    }

    return 0;
}

static int send_to_enc_thread(Scheduler *sch, SchEnc *enc, AVFrame *frame)
{
    int ret;

    if (!frame) {
        tq_send_finish(enc->queue, 0);
        return 0;
    }

    if (enc->in_finished)
        return AVERROR_EOF;

    ret = tq_send(enc->queue, 0, frame);
    if (ret < 0)
        enc->in_finished = 1;

    return ret;
}

static int send_to_enc_sq(Scheduler *sch, SchEnc *enc, AVFrame *frame)
{
    SchSyncQueue *sq = &sch->sq_enc[enc->sq_idx[0]];
    int ret = 0;

    // inform the scheduling code that no more input will arrive along this path;
    // this is necessary because the sync queue may not send an EOF downstream
    // until other streams finish
    // TODO: consider a cleaner way of passing this information through
    //       the pipeline
    if (!frame) {
        for (unsigned i = 0; i < enc->nb_dst; i++) {
            SchMux      *mux;
            SchMuxStream *ms;

            if (enc->dst[i].type != SCH_NODE_TYPE_MUX)
                continue;

            mux = &sch->mux[enc->dst[i].idx];
            ms = &mux->streams[enc->dst[i].idx_stream];

            pthread_mutex_lock(&sch->schedule_lock);

            ms->source_finished = 1;
            schedule_update_locked(sch);

            pthread_mutex_unlock(&sch->schedule_lock);
        }
    }

    pthread_mutex_lock(&sq->lock);

    ret = sq_send(sq->sq, enc->sq_idx[1], SQFRAME(frame));
    if (ret < 0)
        goto finish;

    while (1) {
        SchEnc *enc;

        // TODO: the SQ API should be extended to allow returning EOF
        // for individual streams
        ret = sq_receive(sq->sq, -1, SQFRAME(sq->frame));
        if (ret < 0) {
            ret = (ret == AVERROR(EAGAIN)) ? 0 : ret;
            break;
        }

        enc = &sch->enc[sq->enc_idx[ret]];
        ret = send_to_enc_thread(sch, enc, sq->frame);
        if (ret < 0) {
            av_frame_unref(sq->frame);
            if (ret != AVERROR_EOF)
                break;

            sq_send(sq->sq, enc->sq_idx[1], SQFRAME(NULL));
            continue;
        }
    }

    if (ret < 0) {
        // close all encoders fed from this sync queue
        for (unsigned i = 0; i < sq->nb_enc_idx; i++) {
            int err = send_to_enc_thread(sch, &sch->enc[sq->enc_idx[i]], NULL);

            // if the sync queue error is EOF and closing the encoder
            // produces a more serious error, make sure to pick the latter
            ret = err_merge((ret == AVERROR_EOF && err < 0) ? 0 : ret, err);
        }
    }

finish:
    pthread_mutex_unlock(&sq->lock);

    return ret;
}

static int send_to_enc(Scheduler *sch, SchEnc *enc, AVFrame *frame)
{
    if (enc->open_cb && frame && !enc->opened) {
        int ret = enc_open(sch, enc, frame);
        if (ret < 0)
            return ret;
        enc->opened = 1;

        // discard empty frames that only carry encoder init parameters
        if (!frame->buf[0]) {
            av_frame_unref(frame);
            return 0;
        }
    }

    return (enc->sq_idx[0] >= 0)                ?
           send_to_enc_sq    (sch, enc, frame)  :
           send_to_enc_thread(sch, enc, frame);
}

static int mux_queue_packet(SchMux *mux, SchMuxStream *ms, AVPacket *pkt)
{
    PreMuxQueue *q = &ms->pre_mux_queue;
    AVPacket *tmp_pkt = NULL;
    int ret;

    if (!av_fifo_can_write(q->fifo)) {
        size_t     packets = av_fifo_can_read(q->fifo);
        size_t    pkt_size = pkt ? pkt->size : 0;
        int thresh_reached = (q->data_size + pkt_size) > q->data_threshold;
        size_t max_packets = thresh_reached ? q->max_packets : SIZE_MAX;
        size_t new_size = FFMIN(2 * packets, max_packets);

        if (new_size <= packets) {
            av_log(mux, AV_LOG_ERROR,
                   "Too many packets buffered for output stream.\n");
            return AVERROR_BUFFER_TOO_SMALL;
        }
        ret = av_fifo_grow2(q->fifo, new_size - packets);
        if (ret < 0)
            return ret;
    }

    if (pkt) {
        tmp_pkt = av_packet_alloc();
        if (!tmp_pkt)
            return AVERROR(ENOMEM);

        av_packet_move_ref(tmp_pkt, pkt);
        q->data_size += tmp_pkt->size;
    }
    av_fifo_write(q->fifo, &tmp_pkt, 1);

    return 0;
}

static int send_to_mux(Scheduler *sch, SchMux *mux, unsigned stream_idx,
                       AVPacket *pkt)
{
    SchMuxStream *ms = &mux->streams[stream_idx];
    int64_t dts = (pkt && pkt->dts != AV_NOPTS_VALUE)                                    ?
                  av_rescale_q(pkt->dts + pkt->duration, pkt->time_base, AV_TIME_BASE_Q) :
                  AV_NOPTS_VALUE;

    // queue the packet if the muxer cannot be started yet
    if (!atomic_load(&mux->mux_started)) {
        int queued = 0;

        // the muxer could have started between the above atomic check and
        // locking the mutex, then this block falls through to normal send path
        pthread_mutex_lock(&sch->mux_ready_lock);

        if (!atomic_load(&mux->mux_started)) {
            int ret = mux_queue_packet(mux, ms, pkt);
            queued = ret < 0 ? ret : 1;
        }

        pthread_mutex_unlock(&sch->mux_ready_lock);

        if (queued < 0)
            return queued;
        else if (queued)
            goto update_schedule;
    }

    if (pkt) {
        int ret;

        if (ms->init_eof)
            return AVERROR_EOF;

        ret = tq_send(mux->queue, stream_idx, pkt);
        if (ret < 0)
            return ret;
    } else
        tq_send_finish(mux->queue, stream_idx);

update_schedule:
    // TODO: use atomics to check whether this changes trailing dts
    // to avoid locking unnecesarily
    if (dts != AV_NOPTS_VALUE || !pkt) {
        pthread_mutex_lock(&sch->schedule_lock);

        if (pkt) ms->last_dts = dts;
        else     ms->source_finished = 1;

        schedule_update_locked(sch);

        pthread_mutex_unlock(&sch->schedule_lock);
    }

    return 0;
}

static int
demux_stream_send_to_dst(Scheduler *sch, const SchedulerNode dst,
                         uint8_t *dst_finished, AVPacket *pkt, unsigned flags)
{
    int ret;

    if (*dst_finished)
        return AVERROR_EOF;

    if (pkt && dst.type == SCH_NODE_TYPE_MUX &&
        (flags & DEMUX_SEND_STREAMCOPY_EOF)) {
        av_packet_unref(pkt);
        pkt = NULL;
    }

    if (!pkt)
        goto finish;

    ret = (dst.type == SCH_NODE_TYPE_MUX) ?
          send_to_mux(sch, &sch->mux[dst.idx], dst.idx_stream, pkt) :
          tq_send(sch->dec[dst.idx].queue, 0, pkt);
    if (ret == AVERROR_EOF)
        goto finish;

    return ret;

finish:
    if (dst.type == SCH_NODE_TYPE_MUX)
        send_to_mux(sch, &sch->mux[dst.idx], dst.idx_stream, NULL);
    else
        tq_send_finish(sch->dec[dst.idx].queue, 0);

    *dst_finished = 1;
    return AVERROR_EOF;
}

static int demux_send_for_stream(Scheduler *sch, SchDemux *d, SchDemuxStream *ds,
                                 AVPacket *pkt, unsigned flags)
{
    unsigned nb_done = 0;

    for (unsigned i = 0; i < ds->nb_dst; i++) {
        AVPacket *to_send = pkt;
        uint8_t *finished = &ds->dst_finished[i];

        int ret;

        // sending a packet consumes it, so make a temporary reference if needed
        if (pkt && i < ds->nb_dst - 1) {
            to_send = d->send_pkt;

            ret = av_packet_ref(to_send, pkt);
            if (ret < 0)
                return ret;
        }

        ret = demux_stream_send_to_dst(sch, ds->dst[i], finished, to_send, flags);
        if (to_send)
            av_packet_unref(to_send);
        if (ret == AVERROR_EOF)
            nb_done++;
        else if (ret < 0)
            return ret;
    }

    return (nb_done == ds->nb_dst) ? AVERROR_EOF : 0;
}

static int demux_flush(Scheduler *sch, SchDemux *d, AVPacket *pkt)
{
    Timestamp max_end_ts = (Timestamp){ .ts = AV_NOPTS_VALUE };

    av_assert0(!pkt->buf && !pkt->data && !pkt->side_data_elems);

    for (unsigned i = 0; i < d->nb_streams; i++) {
        SchDemuxStream *ds = &d->streams[i];

        for (unsigned j = 0; j < ds->nb_dst; j++) {
            const SchedulerNode *dst = &ds->dst[j];
            SchDec *dec;
            int ret;

            if (ds->dst_finished[j] || dst->type != SCH_NODE_TYPE_DEC)
                continue;

            dec = &sch->dec[dst->idx];

            ret = tq_send(dec->queue, 0, pkt);
            if (ret < 0)
                return ret;

            if (dec->queue_end_ts) {
                Timestamp ts;
                ret = av_thread_message_queue_recv(dec->queue_end_ts, &ts, 0);
                if (ret < 0)
                    return ret;

                if (max_end_ts.ts == AV_NOPTS_VALUE ||
                    (ts.ts != AV_NOPTS_VALUE &&
                     av_compare_ts(max_end_ts.ts, max_end_ts.tb, ts.ts, ts.tb) < 0))
                    max_end_ts = ts;

            }
        }
    }

    pkt->pts       = max_end_ts.ts;
    pkt->time_base = max_end_ts.tb;

    return 0;
}

int sch_demux_send(Scheduler *sch, unsigned demux_idx, AVPacket *pkt,
                   unsigned flags)
{
    SchDemux *d;
    int terminate;

    av_assert0(demux_idx < sch->nb_demux);
    d = &sch->demux[demux_idx];

    terminate = waiter_wait(sch, &d->waiter);
    if (terminate)
        return AVERROR_EXIT;

    // flush the downstreams after seek
    if (pkt->stream_index == -1)
        return demux_flush(sch, d, pkt);

    av_assert0(pkt->stream_index < d->nb_streams);

    return demux_send_for_stream(sch, d, &d->streams[pkt->stream_index], pkt, flags);
}

static int demux_done(Scheduler *sch, unsigned demux_idx)
{
    SchDemux *d = &sch->demux[demux_idx];
    int ret = 0;

    for (unsigned i = 0; i < d->nb_streams; i++) {
        int err = demux_send_for_stream(sch, d, &d->streams[i], NULL, 0);
        if (err != AVERROR_EOF)
            ret = err_merge(ret, err);
    }

    pthread_mutex_lock(&sch->schedule_lock);

    d->task_exited = 1;

    schedule_update_locked(sch);

    pthread_mutex_unlock(&sch->schedule_lock);

    return ret;
}

int sch_mux_receive(Scheduler *sch, unsigned mux_idx, AVPacket *pkt)
{
    SchMux *mux;
    int ret, stream_idx;

    av_assert0(mux_idx < sch->nb_mux);
    mux = &sch->mux[mux_idx];

    ret = tq_receive(mux->queue, &stream_idx, pkt);
    pkt->stream_index = stream_idx;
    return ret;
}

void sch_mux_receive_finish(Scheduler *sch, unsigned mux_idx, unsigned stream_idx)
{
    SchMux *mux;

    av_assert0(mux_idx < sch->nb_mux);
    mux = &sch->mux[mux_idx];

    av_assert0(stream_idx < mux->nb_streams);
    tq_receive_finish(mux->queue, stream_idx);

    pthread_mutex_lock(&sch->schedule_lock);
    mux->streams[stream_idx].source_finished = 1;

    schedule_update_locked(sch);

    pthread_mutex_unlock(&sch->schedule_lock);
}

int sch_mux_sub_heartbeat(Scheduler *sch, unsigned mux_idx, unsigned stream_idx,
                          const AVPacket *pkt)
{
    SchMux       *mux;
    SchMuxStream *ms;

    av_assert0(mux_idx < sch->nb_mux);
    mux = &sch->mux[mux_idx];

    av_assert0(stream_idx < mux->nb_streams);
    ms = &mux->streams[stream_idx];

    for (unsigned i = 0; i < ms->nb_sub_heartbeat_dst; i++) {
        SchDec *dst = &sch->dec[ms->sub_heartbeat_dst[i]];
        int ret;

        ret = av_packet_copy_props(mux->sub_heartbeat_pkt, pkt);
        if (ret < 0)
            return ret;

        tq_send(dst->queue, 0, mux->sub_heartbeat_pkt);
    }

    return 0;
}

static int mux_done(Scheduler *sch, unsigned mux_idx)
{
    SchMux *mux = &sch->mux[mux_idx];

    pthread_mutex_lock(&sch->schedule_lock);

    for (unsigned i = 0; i < mux->nb_streams; i++) {
        tq_receive_finish(mux->queue, i);
        mux->streams[i].source_finished = 1;
    }

    schedule_update_locked(sch);

    pthread_mutex_unlock(&sch->schedule_lock);

    pthread_mutex_lock(&sch->finish_lock);

    av_assert0(sch->nb_mux_done < sch->nb_mux);
    sch->nb_mux_done++;

    pthread_cond_signal(&sch->finish_cond);

    pthread_mutex_unlock(&sch->finish_lock);

    return 0;
}

int sch_dec_receive(Scheduler *sch, unsigned dec_idx, AVPacket *pkt)
{
    SchDec *dec;
    int ret, dummy;

    av_assert0(dec_idx < sch->nb_dec);
    dec = &sch->dec[dec_idx];

    // the decoder should have given us post-flush end timestamp in pkt
    if (dec->expect_end_ts) {
        Timestamp ts = (Timestamp){ .ts = pkt->pts, .tb = pkt->time_base };
        ret = av_thread_message_queue_send(dec->queue_end_ts, &ts, 0);
        if (ret < 0)
            return ret;

        dec->expect_end_ts = 0;
    }

    ret = tq_receive(dec->queue, &dummy, pkt);
    av_assert0(dummy <= 0);

    // got a flush packet, on the next call to this function the decoder
    // will give us post-flush end timestamp
    if (ret >= 0 && !pkt->data && !pkt->side_data_elems && dec->queue_end_ts)
        dec->expect_end_ts = 1;

    return ret;
}

static int send_to_filter(Scheduler *sch, SchFilterGraph *fg,
                          unsigned in_idx, AVFrame *frame)
{
    if (frame)
        return tq_send(fg->queue, in_idx, frame);

    if (!fg->inputs[in_idx].send_finished) {
        fg->inputs[in_idx].send_finished = 1;
        tq_send_finish(fg->queue, in_idx);

        // close the control stream when all actual inputs are done
        if (atomic_fetch_add(&fg->nb_inputs_finished_send, 1) == fg->nb_inputs - 1)
            tq_send_finish(fg->queue, fg->nb_inputs);
    }
    return 0;
}

static int dec_send_to_dst(Scheduler *sch, const SchedulerNode dst,
                           uint8_t *dst_finished, AVFrame *frame)
{
    int ret;

    if (*dst_finished)
        return AVERROR_EOF;

    if (!frame)
        goto finish;

    ret = (dst.type == SCH_NODE_TYPE_FILTER_IN) ?
          send_to_filter(sch, &sch->filters[dst.idx], dst.idx_stream, frame) :
          send_to_enc(sch, &sch->enc[dst.idx], frame);
    if (ret == AVERROR_EOF)
        goto finish;

    return ret;

finish:
    if (dst.type == SCH_NODE_TYPE_FILTER_IN)
        send_to_filter(sch, &sch->filters[dst.idx], dst.idx_stream, NULL);
    else
        send_to_enc(sch, &sch->enc[dst.idx], NULL);

    *dst_finished = 1;

    return AVERROR_EOF;
}

int sch_dec_send(Scheduler *sch, unsigned dec_idx,
                 unsigned out_idx, AVFrame *frame)
{
    SchDec *dec;
    SchDecOutput *o;
    int ret;
    unsigned nb_done = 0;

    av_assert0(dec_idx < sch->nb_dec);
    dec = &sch->dec[dec_idx];

    av_assert0(out_idx < dec->nb_outputs);
    o = &dec->outputs[out_idx];

    for (unsigned i = 0; i < o->nb_dst; i++) {
        uint8_t *finished = &o->dst_finished[i];
        AVFrame *to_send  = frame;

        // sending a frame consumes it, so make a temporary reference if needed
        if (i < o->nb_dst - 1) {
            to_send = dec->send_frame;

            // frame may sometimes contain props only,
            // e.g. to signal EOF timestamp
            ret = frame->buf[0] ? av_frame_ref(to_send, frame) :
                                  av_frame_copy_props(to_send, frame);
            if (ret < 0)
                return ret;
        }

        ret = dec_send_to_dst(sch, o->dst[i], finished, to_send);
        if (ret < 0) {
            av_frame_unref(to_send);
            if (ret == AVERROR_EOF) {
                nb_done++;
                continue;
            }
            return ret;
        }
    }

    return (nb_done == o->nb_dst) ? AVERROR_EOF : 0;
}

static int dec_done(Scheduler *sch, unsigned dec_idx)
{
    SchDec *dec = &sch->dec[dec_idx];
    int ret = 0;

    tq_receive_finish(dec->queue, 0);

    // make sure our source does not get stuck waiting for end timestamps
    // that will never arrive
    if (dec->queue_end_ts)
        av_thread_message_queue_set_err_recv(dec->queue_end_ts, AVERROR_EOF);

    for (unsigned i = 0; i < dec->nb_outputs; i++) {
        SchDecOutput *o = &dec->outputs[i];

        for (unsigned j = 0; j < o->nb_dst; j++) {
            int err = dec_send_to_dst(sch, o->dst[j], &o->dst_finished[j], NULL);
            if (err < 0 && err != AVERROR_EOF)
                ret = err_merge(ret, err);
        }
    }

    return ret;
}

int sch_enc_receive(Scheduler *sch, unsigned enc_idx, AVFrame *frame)
{
    SchEnc *enc;
    int ret, dummy;

    av_assert0(enc_idx < sch->nb_enc);
    enc = &sch->enc[enc_idx];

    ret = tq_receive(enc->queue, &dummy, frame);
    av_assert0(dummy <= 0);

    return ret;
}

static int enc_send_to_dst(Scheduler *sch, const SchedulerNode dst,
                           uint8_t *dst_finished, AVPacket *pkt)
{
    int ret;

    if (*dst_finished)
        return AVERROR_EOF;

    if (!pkt)
        goto finish;

    ret = (dst.type == SCH_NODE_TYPE_MUX) ?
          send_to_mux(sch, &sch->mux[dst.idx], dst.idx_stream, pkt) :
          tq_send(sch->dec[dst.idx].queue, 0, pkt);
    if (ret == AVERROR_EOF)
        goto finish;

    return ret;

finish:
    if (dst.type == SCH_NODE_TYPE_MUX)
        send_to_mux(sch, &sch->mux[dst.idx], dst.idx_stream, NULL);
    else
        tq_send_finish(sch->dec[dst.idx].queue, 0);

    *dst_finished = 1;

    return AVERROR_EOF;
}

int sch_enc_send(Scheduler *sch, unsigned enc_idx, AVPacket *pkt)
{
    SchEnc *enc;
    int ret;

    av_assert0(enc_idx < sch->nb_enc);
    enc = &sch->enc[enc_idx];

    for (unsigned i = 0; i < enc->nb_dst; i++) {
        uint8_t *finished = &enc->dst_finished[i];
        AVPacket *to_send = pkt;

        // sending a packet consumes it, so make a temporary reference if needed
        if (i < enc->nb_dst - 1) {
            to_send = enc->send_pkt;

            ret = av_packet_ref(to_send, pkt);
            if (ret < 0)
                return ret;
        }

        ret = enc_send_to_dst(sch, enc->dst[i], finished, to_send);
        if (ret < 0) {
            av_packet_unref(to_send);
            if (ret == AVERROR_EOF)
                continue;
            return ret;
        }
    }

    return 0;
}

static int enc_done(Scheduler *sch, unsigned enc_idx)
{
    SchEnc *enc = &sch->enc[enc_idx];
    int ret = 0;

    tq_receive_finish(enc->queue, 0);

    for (unsigned i = 0; i < enc->nb_dst; i++) {
        int err = enc_send_to_dst(sch, enc->dst[i], &enc->dst_finished[i], NULL);
        if (err < 0 && err != AVERROR_EOF)
            ret = err_merge(ret, err);
    }

    return ret;
}

int sch_filter_receive(Scheduler *sch, unsigned fg_idx,
                       unsigned *in_idx, AVFrame *frame)
{
    SchFilterGraph *fg;

    av_assert0(fg_idx < sch->nb_filters);
    fg = &sch->filters[fg_idx];

    av_assert0(*in_idx <= fg->nb_inputs);

    // update scheduling to account for desired input stream, if it changed
    //
    // this check needs no locking because only the filtering thread
    // updates this value
    if (*in_idx != fg->best_input) {
        pthread_mutex_lock(&sch->schedule_lock);

        fg->best_input = *in_idx;
        schedule_update_locked(sch);

        pthread_mutex_unlock(&sch->schedule_lock);
    }

    if (*in_idx == fg->nb_inputs) {
        int terminate = waiter_wait(sch, &fg->waiter);
        return terminate ? AVERROR_EOF : AVERROR(EAGAIN);
    }

    while (1) {
        int ret, idx;

        ret = tq_receive(fg->queue, &idx, frame);
        if (idx < 0)
            return AVERROR_EOF;
        else if (ret >= 0) {
            *in_idx = idx;
            return 0;
        }

        // disregard EOFs for specific streams - they should always be
        // preceded by an EOF frame
    }
}

void sch_filter_receive_finish(Scheduler *sch, unsigned fg_idx, unsigned in_idx)
{
    SchFilterGraph *fg;
    SchFilterIn    *fi;

    av_assert0(fg_idx < sch->nb_filters);
    fg = &sch->filters[fg_idx];

    av_assert0(in_idx < fg->nb_inputs);
    fi = &fg->inputs[in_idx];

    if (!fi->receive_finished) {
        fi->receive_finished = 1;
        tq_receive_finish(fg->queue, in_idx);

        // close the control stream when all actual inputs are done
        if (++fg->nb_inputs_finished_receive == fg->nb_inputs)
            tq_receive_finish(fg->queue, fg->nb_inputs);
    }
}

int sch_filter_send(Scheduler *sch, unsigned fg_idx, unsigned out_idx, AVFrame *frame)
{
    SchFilterGraph *fg;
    SchedulerNode  dst;

    av_assert0(fg_idx < sch->nb_filters);
    fg = &sch->filters[fg_idx];

    av_assert0(out_idx < fg->nb_outputs);
    dst = fg->outputs[out_idx].dst;

    return (dst.type == SCH_NODE_TYPE_ENC)                                    ?
           send_to_enc   (sch, &sch->enc[dst.idx],                     frame) :
           send_to_filter(sch, &sch->filters[dst.idx], dst.idx_stream, frame);
}

static int filter_done(Scheduler *sch, unsigned fg_idx)
{
    SchFilterGraph *fg = &sch->filters[fg_idx];
    int ret = 0;

    for (unsigned i = 0; i <= fg->nb_inputs; i++)
        tq_receive_finish(fg->queue, i);

    for (unsigned i = 0; i < fg->nb_outputs; i++) {
        SchedulerNode dst = fg->outputs[i].dst;
        int err = (dst.type == SCH_NODE_TYPE_ENC)                                   ?
                  send_to_enc   (sch, &sch->enc[dst.idx],                     NULL) :
                  send_to_filter(sch, &sch->filters[dst.idx], dst.idx_stream, NULL);

        if (err < 0 && err != AVERROR_EOF)
            ret = err_merge(ret, err);
    }

    pthread_mutex_lock(&sch->schedule_lock);

    fg->task_exited = 1;

    schedule_update_locked(sch);

    pthread_mutex_unlock(&sch->schedule_lock);

    return ret;
}

int sch_filter_command(Scheduler *sch, unsigned fg_idx, AVFrame *frame)
{
    SchFilterGraph *fg;

    av_assert0(fg_idx < sch->nb_filters);
    fg = &sch->filters[fg_idx];

    return send_to_filter(sch, fg, fg->nb_inputs, frame);
}

static int task_cleanup(Scheduler *sch, SchedulerNode node)
{
    switch (node.type) {
    case SCH_NODE_TYPE_DEMUX:       return demux_done (sch, node.idx);
    case SCH_NODE_TYPE_MUX:         return mux_done   (sch, node.idx);
    case SCH_NODE_TYPE_DEC:         return dec_done   (sch, node.idx);
    case SCH_NODE_TYPE_ENC:         return enc_done   (sch, node.idx);
    case SCH_NODE_TYPE_FILTER_IN:   return filter_done(sch, node.idx);
    default: av_assert0(0);
    }
}

static void *task_wrapper(void *arg)
{
    SchTask  *task = arg;
    Scheduler *sch = task->parent;
    int ret;
    int err = 0;

    ret = task->func(task->func_arg);
    if (ret < 0)
        av_log(task->func_arg, AV_LOG_ERROR,
               "Task finished with error code: %d (%s)\n", ret, av_err2str(ret));

    err = task_cleanup(sch, task->node);
    ret = err_merge(ret, err);

    // EOF is considered normal termination
    if (ret == AVERROR_EOF)
        ret = 0;
    if (ret < 0) {
        pthread_mutex_lock(&sch->finish_lock);
        sch->task_failed = 1;
        pthread_cond_signal(&sch->finish_cond);
        pthread_mutex_unlock(&sch->finish_lock);
    }

    av_log(task->func_arg, ret < 0 ? AV_LOG_ERROR : AV_LOG_VERBOSE,
           "Terminating thread with return code %d (%s)\n", ret,
           ret < 0 ? av_err2str(ret) : "success");

    return (void*)(intptr_t)ret;
}

static int task_stop(Scheduler *sch, SchTask *task)
{
    int ret;
    void *thread_ret;

    if (!task->thread_running)
        return task_cleanup(sch, task->node);

    ret = pthread_join(task->thread, &thread_ret);
    av_assert0(ret == 0);

    task->thread_running = 0;

    return (intptr_t)thread_ret;
}

int sch_stop(Scheduler *sch, int64_t *finish_ts)
{
    int ret = 0, err;

    if (sch->state != SCH_STATE_STARTED)
        return 0;

    atomic_store(&sch->terminate, 1);

    for (unsigned type = 0; type < 2; type++)
        for (unsigned i = 0; i < (type ? sch->nb_demux : sch->nb_filters); i++) {
            SchWaiter *w = type ? &sch->demux[i].waiter : &sch->filters[i].waiter;
            waiter_set(w, 1);
        }

    for (unsigned i = 0; i < sch->nb_demux; i++) {
        SchDemux *d = &sch->demux[i];

        err = task_stop(sch, &d->task);
        ret = err_merge(ret, err);
    }

    for (unsigned i = 0; i < sch->nb_dec; i++) {
        SchDec *dec = &sch->dec[i];

        err = task_stop(sch, &dec->task);
        ret = err_merge(ret, err);
    }

    for (unsigned i = 0; i < sch->nb_filters; i++) {
        SchFilterGraph *fg = &sch->filters[i];

        err = task_stop(sch, &fg->task);
        ret = err_merge(ret, err);
    }

    for (unsigned i = 0; i < sch->nb_enc; i++) {
        SchEnc *enc = &sch->enc[i];

        err = task_stop(sch, &enc->task);
        ret = err_merge(ret, err);
    }

    for (unsigned i = 0; i < sch->nb_mux; i++) {
        SchMux *mux = &sch->mux[i];

        err = task_stop(sch, &mux->task);
        ret = err_merge(ret, err);
    }

    if (finish_ts)
        *finish_ts = trailing_dts(sch, 1);

    sch->state = SCH_STATE_STOPPED;

    return ret;
}
