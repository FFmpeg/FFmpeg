/*
 * Copyright (c) 2014 Nicolas George
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "fifo.h"
#include "threadmessage.h"
#include "thread.h"

struct AVThreadMessageQueue {
#if HAVE_THREADS
    AVFifoBuffer *fifo;
    pthread_mutex_t lock;
    pthread_cond_t cond_recv;
    pthread_cond_t cond_send;
    int err_send;
    int err_recv;
    unsigned elsize;
    void (*free_func)(void *msg);
#else
    int dummy;
#endif
};

int av_thread_message_queue_alloc(AVThreadMessageQueue **mq,
                                  unsigned nelem,
                                  unsigned elsize)
{
#if HAVE_THREADS
    AVThreadMessageQueue *rmq;
    int ret = 0;

    if (nelem > INT_MAX / elsize)
        return AVERROR(EINVAL);
    if (!(rmq = av_mallocz(sizeof(*rmq))))
        return AVERROR(ENOMEM);
    if ((ret = pthread_mutex_init(&rmq->lock, NULL))) {
        av_free(rmq);
        return AVERROR(ret);
    }
    if ((ret = pthread_cond_init(&rmq->cond_recv, NULL))) {
        pthread_mutex_destroy(&rmq->lock);
        av_free(rmq);
        return AVERROR(ret);
    }
    if ((ret = pthread_cond_init(&rmq->cond_send, NULL))) {
        pthread_cond_destroy(&rmq->cond_recv);
        pthread_mutex_destroy(&rmq->lock);
        av_free(rmq);
        return AVERROR(ret);
    }
    if (!(rmq->fifo = av_fifo_alloc(elsize * nelem))) {
        pthread_cond_destroy(&rmq->cond_send);
        pthread_cond_destroy(&rmq->cond_recv);
        pthread_mutex_destroy(&rmq->lock);
        av_free(rmq);
        return AVERROR(ENOMEM);
    }
    rmq->elsize = elsize;
    *mq = rmq;
    return 0;
#else
    *mq = NULL;
    return AVERROR(ENOSYS);
#endif /* HAVE_THREADS */
}

void av_thread_message_queue_set_free_func(AVThreadMessageQueue *mq,
                                           void (*free_func)(void *msg))
{
#if HAVE_THREADS
    mq->free_func = free_func;
#endif
}

void av_thread_message_queue_free(AVThreadMessageQueue **mq)
{
#if HAVE_THREADS
    if (*mq) {
        av_thread_message_flush(*mq);
        av_fifo_freep(&(*mq)->fifo);
        pthread_cond_destroy(&(*mq)->cond_send);
        pthread_cond_destroy(&(*mq)->cond_recv);
        pthread_mutex_destroy(&(*mq)->lock);
        av_freep(mq);
    }
#endif
}

#if HAVE_THREADS

static int av_thread_message_queue_send_locked(AVThreadMessageQueue *mq,
                                               void *msg,
                                               unsigned flags)
{
    while (!mq->err_send && av_fifo_space(mq->fifo) < mq->elsize) {
        if ((flags & AV_THREAD_MESSAGE_NONBLOCK))
            return AVERROR(EAGAIN);
        pthread_cond_wait(&mq->cond_send, &mq->lock);
    }
    if (mq->err_send)
        return mq->err_send;
    av_fifo_generic_write(mq->fifo, msg, mq->elsize, NULL);
    /* one message is sent, signal one receiver */
    pthread_cond_signal(&mq->cond_recv);
    return 0;
}

static int av_thread_message_queue_recv_locked(AVThreadMessageQueue *mq,
                                               void *msg,
                                               unsigned flags)
{
    while (!mq->err_recv && av_fifo_size(mq->fifo) < mq->elsize) {
        if ((flags & AV_THREAD_MESSAGE_NONBLOCK))
            return AVERROR(EAGAIN);
        pthread_cond_wait(&mq->cond_recv, &mq->lock);
    }
    if (av_fifo_size(mq->fifo) < mq->elsize)
        return mq->err_recv;
    av_fifo_generic_read(mq->fifo, msg, mq->elsize, NULL);
    /* one message space appeared, signal one sender */
    pthread_cond_signal(&mq->cond_send);
    return 0;
}

#endif /* HAVE_THREADS */

int av_thread_message_queue_send(AVThreadMessageQueue *mq,
                                 void *msg,
                                 unsigned flags)
{
#if HAVE_THREADS
    int ret;

    pthread_mutex_lock(&mq->lock);
    ret = av_thread_message_queue_send_locked(mq, msg, flags);
    pthread_mutex_unlock(&mq->lock);
    return ret;
#else
    return AVERROR(ENOSYS);
#endif /* HAVE_THREADS */
}

int av_thread_message_queue_recv(AVThreadMessageQueue *mq,
                                 void *msg,
                                 unsigned flags)
{
#if HAVE_THREADS
    int ret;

    pthread_mutex_lock(&mq->lock);
    ret = av_thread_message_queue_recv_locked(mq, msg, flags);
    pthread_mutex_unlock(&mq->lock);
    return ret;
#else
    return AVERROR(ENOSYS);
#endif /* HAVE_THREADS */
}

void av_thread_message_queue_set_err_send(AVThreadMessageQueue *mq,
                                          int err)
{
#if HAVE_THREADS
    pthread_mutex_lock(&mq->lock);
    mq->err_send = err;
    pthread_cond_broadcast(&mq->cond_send);
    pthread_mutex_unlock(&mq->lock);
#endif /* HAVE_THREADS */
}

void av_thread_message_queue_set_err_recv(AVThreadMessageQueue *mq,
                                          int err)
{
#if HAVE_THREADS
    pthread_mutex_lock(&mq->lock);
    mq->err_recv = err;
    pthread_cond_broadcast(&mq->cond_recv);
    pthread_mutex_unlock(&mq->lock);
#endif /* HAVE_THREADS */
}

#if HAVE_THREADS
static void free_func_wrap(void *arg, void *msg, int size)
{
    AVThreadMessageQueue *mq = arg;
    mq->free_func(msg);
}
#endif

void av_thread_message_flush(AVThreadMessageQueue *mq)
{
#if HAVE_THREADS
    int used, off;
    void *free_func = mq->free_func;

    pthread_mutex_lock(&mq->lock);
    used = av_fifo_size(mq->fifo);
    if (free_func)
        for (off = 0; off < used; off += mq->elsize)
            av_fifo_generic_peek_at(mq->fifo, mq, off, mq->elsize, free_func_wrap);
    av_fifo_drain(mq->fifo, used);
    /* only the senders need to be notified since the queue is empty and there
     * is nothing to read */
    pthread_cond_broadcast(&mq->cond_send);
    pthread_mutex_unlock(&mq->lock);
#endif /* HAVE_THREADS */
}
