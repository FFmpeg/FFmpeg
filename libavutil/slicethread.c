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
#include "slicethread.h"
#include "mem.h"
#include "thread.h"
#include "avassert.h"

#if HAVE_PTHREADS || HAVE_W32THREADS || HAVE_OS2THREADS

typedef struct WorkerContext {
    AVSliceThread   *ctx;
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    pthread_t       thread;
    int             done;
} WorkerContext;

struct AVSliceThread {
    WorkerContext   *workers;
    int             nb_threads;
    int             nb_active_threads;
    int             nb_jobs;

    atomic_uint     first_job;
    atomic_uint     current_job;
    pthread_mutex_t done_mutex;
    pthread_cond_t  done_cond;
    int             done;
    int             finished;

    void            *priv;
    void            (*worker_func)(void *priv, int jobnr, int threadnr, int nb_jobs, int nb_threads);
    void            (*main_func)(void *priv);
};

static int run_jobs(AVSliceThread *ctx)
{
    unsigned nb_jobs    = ctx->nb_jobs;
    unsigned nb_active_threads = ctx->nb_active_threads;
    unsigned first_job    = atomic_fetch_add_explicit(&ctx->first_job, 1, memory_order_acq_rel);
    unsigned current_job  = first_job;

    do {
        ctx->worker_func(ctx->priv, current_job, first_job, nb_jobs, nb_active_threads);
    } while ((current_job = atomic_fetch_add_explicit(&ctx->current_job, 1, memory_order_acq_rel)) < nb_jobs);

    return current_job == nb_jobs + nb_active_threads - 1;
}

static void *attribute_align_arg thread_worker(void *v)
{
    WorkerContext *w = v;
    AVSliceThread *ctx = w->ctx;

    pthread_mutex_lock(&w->mutex);
    pthread_cond_signal(&w->cond);

    while (1) {
        w->done = 1;
        while (w->done)
            pthread_cond_wait(&w->cond, &w->mutex);

        if (ctx->finished) {
            pthread_mutex_unlock(&w->mutex);
            return NULL;
        }

        if (run_jobs(ctx)) {
            pthread_mutex_lock(&ctx->done_mutex);
            ctx->done = 1;
            pthread_cond_signal(&ctx->done_cond);
            pthread_mutex_unlock(&ctx->done_mutex);
        }
    }
}

int avpriv_slicethread_create(AVSliceThread **pctx, void *priv,
                              void (*worker_func)(void *priv, int jobnr, int threadnr, int nb_jobs, int nb_threads),
                              void (*main_func)(void *priv),
                              int nb_threads)
{
    AVSliceThread *ctx;
    int nb_workers, i;

#if HAVE_W32THREADS
    w32thread_init();
#endif

    av_assert0(nb_threads >= 0);
    if (!nb_threads) {
        int nb_cpus = av_cpu_count();
        if (nb_cpus > 1)
            nb_threads = nb_cpus + 1;
        else
            nb_threads = 1;
    }

    nb_workers = nb_threads;
    if (!main_func)
        nb_workers--;

    *pctx = ctx = av_mallocz(sizeof(*ctx));
    if (!ctx)
        return AVERROR(ENOMEM);

    if (nb_workers && !(ctx->workers = av_calloc(nb_workers, sizeof(*ctx->workers)))) {
        av_freep(pctx);
        return AVERROR(ENOMEM);
    }

    ctx->priv        = priv;
    ctx->worker_func = worker_func;
    ctx->main_func   = main_func;
    ctx->nb_threads  = nb_threads;
    ctx->nb_active_threads = 0;
    ctx->nb_jobs     = 0;
    ctx->finished    = 0;

    atomic_init(&ctx->first_job, 0);
    atomic_init(&ctx->current_job, 0);
    pthread_mutex_init(&ctx->done_mutex, NULL);
    pthread_cond_init(&ctx->done_cond, NULL);
    ctx->done        = 0;

    for (i = 0; i < nb_workers; i++) {
        WorkerContext *w = &ctx->workers[i];
        int ret;
        w->ctx = ctx;
        pthread_mutex_init(&w->mutex, NULL);
        pthread_cond_init(&w->cond, NULL);
        pthread_mutex_lock(&w->mutex);
        w->done = 0;

        if (ret = pthread_create(&w->thread, NULL, thread_worker, w)) {
            ctx->nb_threads = main_func ? i : i + 1;
            pthread_mutex_unlock(&w->mutex);
            pthread_cond_destroy(&w->cond);
            pthread_mutex_destroy(&w->mutex);
            avpriv_slicethread_free(pctx);
            return AVERROR(ret);
        }

        while (!w->done)
            pthread_cond_wait(&w->cond, &w->mutex);
        pthread_mutex_unlock(&w->mutex);
    }

    return nb_threads;
}

void avpriv_slicethread_execute(AVSliceThread *ctx, int nb_jobs, int execute_main)
{
    int nb_workers, i, is_last = 0;

    av_assert0(nb_jobs > 0);
    ctx->nb_jobs           = nb_jobs;
    ctx->nb_active_threads = FFMIN(nb_jobs, ctx->nb_threads);
    atomic_store_explicit(&ctx->first_job, 0, memory_order_relaxed);
    atomic_store_explicit(&ctx->current_job, ctx->nb_active_threads, memory_order_relaxed);
    nb_workers             = ctx->nb_active_threads;
    if (!ctx->main_func || !execute_main)
        nb_workers--;

    for (i = 0; i < nb_workers; i++) {
        WorkerContext *w = &ctx->workers[i];
        pthread_mutex_lock(&w->mutex);
        w->done = 0;
        pthread_cond_signal(&w->cond);
        pthread_mutex_unlock(&w->mutex);
    }

    if (ctx->main_func && execute_main)
        ctx->main_func(ctx->priv);
    else
        is_last = run_jobs(ctx);

    if (!is_last) {
        pthread_mutex_lock(&ctx->done_mutex);
        while (!ctx->done)
            pthread_cond_wait(&ctx->done_cond, &ctx->done_mutex);
        ctx->done = 0;
        pthread_mutex_unlock(&ctx->done_mutex);
    }
}

void avpriv_slicethread_free(AVSliceThread **pctx)
{
    AVSliceThread *ctx;
    int nb_workers, i;

    if (!pctx || !*pctx)
        return;

    ctx = *pctx;
    nb_workers = ctx->nb_threads;
    if (!ctx->main_func)
        nb_workers--;

    ctx->finished = 1;
    for (i = 0; i < nb_workers; i++) {
        WorkerContext *w = &ctx->workers[i];
        pthread_mutex_lock(&w->mutex);
        w->done = 0;
        pthread_cond_signal(&w->cond);
        pthread_mutex_unlock(&w->mutex);
    }

    for (i = 0; i < nb_workers; i++) {
        WorkerContext *w = &ctx->workers[i];
        pthread_join(w->thread, NULL);
        pthread_cond_destroy(&w->cond);
        pthread_mutex_destroy(&w->mutex);
    }

    pthread_cond_destroy(&ctx->done_cond);
    pthread_mutex_destroy(&ctx->done_mutex);
    av_freep(&ctx->workers);
    av_freep(pctx);
}

#else /* HAVE_PTHREADS || HAVE_W32THREADS || HAVE_OS32THREADS */

int avpriv_slicethread_create(AVSliceThread **pctx, void *priv,
                              void (*worker_func)(void *priv, int jobnr, int threadnr, int nb_jobs, int nb_threads),
                              void (*main_func)(void *priv),
                              int nb_threads)
{
    *pctx = NULL;
    return AVERROR(EINVAL);
}

void avpriv_slicethread_execute(AVSliceThread *ctx, int nb_jobs, int execute_main)
{
    av_assert0(0);
}

void avpriv_slicethread_free(AVSliceThread **pctx)
{
    av_assert0(!pctx || !*pctx);
}

#endif /* HAVE_PTHREADS || HAVE_W32THREADS || HAVE_OS32THREADS */
