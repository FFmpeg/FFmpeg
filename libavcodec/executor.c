/*
 * Copyright (C) 2024 Nuo Mi
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

#include "config.h"

#include <stdbool.h>

#include "libavutil/mem.h"
#include "libavutil/thread.h"

#include "executor.h"

#if !HAVE_THREADS

#define ExecutorThread  char

#define executor_thread_create(t, a, s, ar)      0
#define executor_thread_join(t, r)               do {} while(0)

#else

#define ExecutorThread  pthread_t

#define executor_thread_create(t, a, s, ar)      pthread_create(t, a, s, ar)
#define executor_thread_join(t, r)               pthread_join(t, r)

#endif //!HAVE_THREADS

typedef struct ThreadInfo {
    FFExecutor *e;
    ExecutorThread thread;
} ThreadInfo;

typedef struct Queue {
    FFTask *head;
    FFTask *tail;
} Queue;

struct FFExecutor {
    FFTaskCallbacks cb;
    int thread_count;
    bool recursive;

    ThreadInfo *threads;
    uint8_t *local_contexts;

    AVMutex lock;
    AVCond cond;
    int die;

    Queue *q;
};

static FFTask* remove_task(Queue *q)
{
    FFTask *t = q->head;
    if (t) {
        q->head = t->next;
        t->next = NULL;
        if (!q->head)
            q->tail = NULL;
    }
    return t;
}

static void add_task(Queue *q, FFTask *t)
{
    t->next = NULL;
    if (!q->head)
        q->tail = q->head = t;
    else
        q->tail = q->tail->next = t;
}

static int run_one_task(FFExecutor *e, void *lc)
{
    FFTaskCallbacks *cb = &e->cb;
    FFTask *t = NULL;

    for (int i = 0; i < e->cb.priorities && !t; i++)
        t = remove_task(e->q + i);

    if (t) {
        if (e->thread_count > 0)
            ff_mutex_unlock(&e->lock);
        cb->run(t, lc, cb->user_data);
        if (e->thread_count > 0)
            ff_mutex_lock(&e->lock);
        return 1;
    }
    return 0;
}

#if HAVE_THREADS
static void *executor_worker_task(void *data)
{
    ThreadInfo *ti = (ThreadInfo*)data;
    FFExecutor *e  = ti->e;
    void *lc       = e->local_contexts + (ti - e->threads) * e->cb.local_context_size;

    ff_mutex_lock(&e->lock);
    while (1) {
        if (e->die) break;

        if (!run_one_task(e, lc)) {
            //no task in one loop
            ff_cond_wait(&e->cond, &e->lock);
        }
    }
    ff_mutex_unlock(&e->lock);
    return NULL;
}
#endif

static void executor_free(FFExecutor *e, const int has_lock, const int has_cond)
{
    if (e->thread_count) {
        //signal die
        ff_mutex_lock(&e->lock);
        e->die = 1;
        ff_cond_broadcast(&e->cond);
        ff_mutex_unlock(&e->lock);

        for (int i = 0; i < e->thread_count; i++)
            executor_thread_join(e->threads[i].thread, NULL);
    }
    if (has_cond)
        ff_cond_destroy(&e->cond);
    if (has_lock)
        ff_mutex_destroy(&e->lock);

    av_free(e->threads);
    av_free(e->q);
    av_free(e->local_contexts);

    av_free(e);
}

FFExecutor* ff_executor_alloc(const FFTaskCallbacks *cb, int thread_count)
{
    FFExecutor *e;
    int has_lock = 0, has_cond = 0;
    if (!cb || !cb->user_data || !cb->run || !cb->priorities)
        return NULL;

    e = av_mallocz(sizeof(*e));
    if (!e)
        return NULL;
    e->cb = *cb;

    e->local_contexts = av_calloc(FFMAX(thread_count, 1), e->cb.local_context_size);
    if (!e->local_contexts)
        goto free_executor;

    e->q = av_calloc(e->cb.priorities, sizeof(Queue));
    if (!e->q)
        goto free_executor;

    e->threads = av_calloc(FFMAX(thread_count, 1), sizeof(*e->threads));
    if (!e->threads)
        goto free_executor;

    if (!thread_count)
        return e;

    has_lock = !ff_mutex_init(&e->lock, NULL);
    has_cond = !ff_cond_init(&e->cond, NULL);

    if (!has_lock || !has_cond)
        goto free_executor;

    for (/* nothing */; e->thread_count < thread_count; e->thread_count++) {
        ThreadInfo *ti = e->threads + e->thread_count;
        ti->e = e;
        if (executor_thread_create(&ti->thread, NULL, executor_worker_task, ti))
            goto free_executor;
    }
    return e;

free_executor:
    executor_free(e, has_lock, has_cond);
    return NULL;
}

void ff_executor_free(FFExecutor **executor)
{
    int thread_count;

    if (!executor || !*executor)
        return;
    thread_count = (*executor)->thread_count;
    executor_free(*executor, thread_count, thread_count);
    *executor = NULL;
}

void ff_executor_execute(FFExecutor *e, FFTask *t)
{
    if (e->thread_count)
        ff_mutex_lock(&e->lock);
    if (t)
        add_task(e->q + t->priority % e->cb.priorities, t);
    if (e->thread_count) {
        ff_cond_signal(&e->cond);
        ff_mutex_unlock(&e->lock);
    }

    if (!e->thread_count || !HAVE_THREADS) {
        if (e->recursive)
            return;
        e->recursive = true;
        // We are running in a single-threaded environment, so we must handle all tasks ourselves
        while (run_one_task(e, e->local_contexts))
            /* nothing */;
        e->recursive = false;
    }
}
