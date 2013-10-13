/*
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

/**
 * @file
 * Libavfilter multithreading support
 */

#include "config.h"

#include "libavutil/common.h"
#include "libavutil/cpu.h"
#include "libavutil/mem.h"

#include "avfilter.h"
#include "internal.h"
#include "thread.h"

#if HAVE_PTHREADS
#include <pthread.h>
#elif HAVE_OS2THREADS
#include "compat/os2threads.h"
#elif HAVE_W32THREADS
#include "compat/w32pthreads.h"
#endif

typedef struct ThreadContext {
    AVFilterGraph *graph;

    int nb_threads;
    pthread_t *workers;
    avfilter_action_func *func;

    /* per-execute perameters */
    AVFilterContext *ctx;
    void *arg;
    int   *rets;
    int nb_rets;
    int nb_jobs;

    pthread_cond_t last_job_cond;
    pthread_cond_t current_job_cond;
    pthread_mutex_t current_job_lock;
    int current_job;
    int done;
} ThreadContext;

static void* attribute_align_arg worker(void *v)
{
    ThreadContext *c = v;
    int our_job      = c->nb_jobs;
    int nb_threads   = c->nb_threads;
    int self_id;

    pthread_mutex_lock(&c->current_job_lock);
    self_id = c->current_job++;
    for (;;) {
        while (our_job >= c->nb_jobs) {
            if (c->current_job == nb_threads + c->nb_jobs)
                pthread_cond_signal(&c->last_job_cond);

            if (!c->done)
                pthread_cond_wait(&c->current_job_cond, &c->current_job_lock);
            our_job = self_id;

            if (c->done) {
                pthread_mutex_unlock(&c->current_job_lock);
                return NULL;
            }
        }
        pthread_mutex_unlock(&c->current_job_lock);

        c->rets[our_job % c->nb_rets] = c->func(c->ctx, c->arg, our_job, c->nb_jobs);

        pthread_mutex_lock(&c->current_job_lock);
        our_job = c->current_job++;
    }
}

static void slice_thread_uninit(ThreadContext *c)
{
    int i;

    pthread_mutex_lock(&c->current_job_lock);
    c->done = 1;
    pthread_cond_broadcast(&c->current_job_cond);
    pthread_mutex_unlock(&c->current_job_lock);

    for (i = 0; i < c->nb_threads; i++)
         pthread_join(c->workers[i], NULL);

    pthread_mutex_destroy(&c->current_job_lock);
    pthread_cond_destroy(&c->current_job_cond);
    pthread_cond_destroy(&c->last_job_cond);
    av_freep(&c->workers);
}

static void slice_thread_park_workers(ThreadContext *c)
{
    pthread_cond_wait(&c->last_job_cond, &c->current_job_lock);
    pthread_mutex_unlock(&c->current_job_lock);
}

static int thread_execute(AVFilterContext *ctx, avfilter_action_func *func,
                          void *arg, int *ret, int nb_jobs)
{
    ThreadContext *c = ctx->graph->internal->thread;
    int dummy_ret;

    if (nb_jobs <= 0)
        return 0;

    pthread_mutex_lock(&c->current_job_lock);

    c->current_job = c->nb_threads;
    c->nb_jobs     = nb_jobs;
    c->ctx         = ctx;
    c->arg         = arg;
    c->func        = func;
    if (ret) {
        c->rets    = ret;
        c->nb_rets = nb_jobs;
    } else {
        c->rets    = &dummy_ret;
        c->nb_rets = 1;
    }
    pthread_cond_broadcast(&c->current_job_cond);

    slice_thread_park_workers(c);

    return 0;
}

static int thread_init_internal(ThreadContext *c, int nb_threads)
{
    int i, ret;

    if (!nb_threads) {
        int nb_cpus = av_cpu_count();
        // use number of cores + 1 as thread count if there is more than one
        if (nb_cpus > 1)
            nb_threads = nb_cpus + 1;
        else
            nb_threads = 1;
    }

    if (nb_threads <= 1)
        return 1;

    c->nb_threads = nb_threads;
    c->workers = av_mallocz(sizeof(*c->workers) * nb_threads);
    if (!c->workers)
        return AVERROR(ENOMEM);

    c->current_job = 0;
    c->nb_jobs     = 0;
    c->done        = 0;

    pthread_cond_init(&c->current_job_cond, NULL);
    pthread_cond_init(&c->last_job_cond,    NULL);

    pthread_mutex_init(&c->current_job_lock, NULL);
    pthread_mutex_lock(&c->current_job_lock);
    for (i = 0; i < nb_threads; i++) {
        ret = pthread_create(&c->workers[i], NULL, worker, c);
        if (ret) {
           pthread_mutex_unlock(&c->current_job_lock);
           c->nb_threads = i;
           slice_thread_uninit(c);
           return AVERROR(ret);
        }
    }

    slice_thread_park_workers(c);

    return c->nb_threads;
}

int ff_graph_thread_init(AVFilterGraph *graph)
{
    int ret;

#if HAVE_W32THREADS
    w32thread_init();
#endif

    if (graph->nb_threads == 1) {
        graph->thread_type = 0;
        return 0;
    }

    graph->internal->thread = av_mallocz(sizeof(ThreadContext));
    if (!graph->internal->thread)
        return AVERROR(ENOMEM);

    ret = thread_init_internal(graph->internal->thread, graph->nb_threads);
    if (ret <= 1) {
        av_freep(&graph->internal->thread);
        graph->thread_type = 0;
        graph->nb_threads  = 1;
        return (ret < 0) ? ret : 0;
    }
    graph->nb_threads = ret;

    graph->internal->thread_execute = thread_execute;

    return 0;
}

void ff_graph_thread_free(AVFilterGraph *graph)
{
    if (graph->internal->thread)
        slice_thread_uninit(graph->internal->thread);
    av_freep(&graph->internal->thread);
}
