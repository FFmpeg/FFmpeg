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
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/thread.h"

#include "objpool.h"
#include "thread_queue.h"

enum {
    FINISHED_SEND = (1 << 0),
    FINISHED_RECV = (1 << 1),
};

typedef struct FifoElem {
    void        *obj;
    unsigned int stream_idx;
} FifoElem;

struct ThreadQueue {
    int              *finished;
    unsigned int    nb_streams;

    AVFifo  *fifo;

    ObjPool *obj_pool;
    void   (*obj_move)(void *dst, void *src);

    pthread_mutex_t lock;
    pthread_cond_t  cond;
};

void tq_free(ThreadQueue **ptq)
{
    ThreadQueue *tq = *ptq;

    if (!tq)
        return;

    if (tq->fifo) {
        FifoElem elem;
        while (av_fifo_read(tq->fifo, &elem, 1) >= 0)
            objpool_release(tq->obj_pool, &elem.obj);
    }
    av_fifo_freep2(&tq->fifo);

    objpool_free(&tq->obj_pool);

    av_freep(&tq->finished);

    pthread_cond_destroy(&tq->cond);
    pthread_mutex_destroy(&tq->lock);

    av_freep(ptq);
}

ThreadQueue *tq_alloc(unsigned int nb_streams, size_t queue_size,
                      ObjPool *obj_pool, void (*obj_move)(void *dst, void *src))
{
    ThreadQueue *tq;
    int ret;

    tq = av_mallocz(sizeof(*tq));
    if (!tq)
        return NULL;

    ret = pthread_cond_init(&tq->cond, NULL);
    if (ret) {
        av_freep(&tq);
        return NULL;
    }

    ret = pthread_mutex_init(&tq->lock, NULL);
    if (ret) {
        pthread_cond_destroy(&tq->cond);
        av_freep(&tq);
        return NULL;
    }

    tq->finished = av_calloc(nb_streams, sizeof(*tq->finished));
    if (!tq->finished)
        goto fail;
    tq->nb_streams = nb_streams;

    tq->fifo = av_fifo_alloc2(queue_size, sizeof(FifoElem), 0);
    if (!tq->fifo)
        goto fail;

    tq->obj_pool = obj_pool;
    tq->obj_move = obj_move;

    return tq;
fail:
    tq_free(&tq);
    return NULL;
}

int tq_send(ThreadQueue *tq, unsigned int stream_idx, void *data)
{
    int *finished;
    int ret;

    av_assert0(stream_idx < tq->nb_streams);
    finished = &tq->finished[stream_idx];

    pthread_mutex_lock(&tq->lock);

    if (*finished & FINISHED_SEND) {
        ret = AVERROR(EINVAL);
        goto finish;
    }

    while (!(*finished & FINISHED_RECV) && !av_fifo_can_write(tq->fifo))
        pthread_cond_wait(&tq->cond, &tq->lock);

    if (*finished & FINISHED_RECV) {
        ret = AVERROR_EOF;
        *finished |= FINISHED_SEND;
    } else {
        FifoElem elem = { .stream_idx = stream_idx };

        ret = objpool_get(tq->obj_pool, &elem.obj);
        if (ret < 0)
            goto finish;

        tq->obj_move(elem.obj, data);

        ret = av_fifo_write(tq->fifo, &elem, 1);
        av_assert0(ret >= 0);
        pthread_cond_broadcast(&tq->cond);
    }

finish:
    pthread_mutex_unlock(&tq->lock);

    return ret;
}

static int receive_locked(ThreadQueue *tq, int *stream_idx,
                          void *data)
{
    FifoElem elem;
    unsigned int nb_finished = 0;

    while (av_fifo_read(tq->fifo, &elem, 1) >= 0) {
        if (tq->finished[elem.stream_idx] & FINISHED_RECV) {
            objpool_release(tq->obj_pool, &elem.obj);
            continue;
        }

        tq->obj_move(data, elem.obj);
        objpool_release(tq->obj_pool, &elem.obj);
        *stream_idx = elem.stream_idx;
        return 0;
    }

    for (unsigned int i = 0; i < tq->nb_streams; i++) {
        if (!tq->finished[i])
            continue;

        /* return EOF to the consumer at most once for each stream */
        if (!(tq->finished[i] & FINISHED_RECV)) {
            tq->finished[i] |= FINISHED_RECV;
            *stream_idx   = i;
            return AVERROR_EOF;
        }

        nb_finished++;
    }

    return nb_finished == tq->nb_streams ? AVERROR_EOF : AVERROR(EAGAIN);
}

int tq_receive(ThreadQueue *tq, int *stream_idx, void *data)
{
    int ret;

    *stream_idx = -1;

    pthread_mutex_lock(&tq->lock);

    while (1) {
        size_t can_read = av_fifo_can_read(tq->fifo);

        ret = receive_locked(tq, stream_idx, data);

        // signal other threads if the fifo state changed
        if (can_read != av_fifo_can_read(tq->fifo))
            pthread_cond_broadcast(&tq->cond);

        if (ret == AVERROR(EAGAIN)) {
            pthread_cond_wait(&tq->cond, &tq->lock);
            continue;
        }

        break;
    }

    pthread_mutex_unlock(&tq->lock);

    return ret;
}

void tq_send_finish(ThreadQueue *tq, unsigned int stream_idx)
{
    av_assert0(stream_idx < tq->nb_streams);

    pthread_mutex_lock(&tq->lock);

    /* mark the stream as send-finished;
     * next time the consumer thread tries to read this stream it will get
     * an EOF and recv-finished flag will be set */
    tq->finished[stream_idx] |= FINISHED_SEND;
    pthread_cond_broadcast(&tq->cond);

    pthread_mutex_unlock(&tq->lock);
}

void tq_receive_finish(ThreadQueue *tq, unsigned int stream_idx)
{
    av_assert0(stream_idx < tq->nb_streams);

    pthread_mutex_lock(&tq->lock);

    /* mark the stream as recv-finished;
     * next time the producer thread tries to send for this stream, it will
     * get an EOF and send-finished flag will be set */
    tq->finished[stream_idx] |= FINISHED_RECV;
    pthread_cond_broadcast(&tq->cond);

    pthread_mutex_unlock(&tq->lock);
}
