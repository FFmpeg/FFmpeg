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
#include "cpu.h"
#include "internal.h"
#include "slicethread.h"
#include "mem.h"
#include "thread.h"
#include "avassert.h"

#define MAX_AUTO_THREADS 16

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
    atomic_int      error;

    void            *priv;
    int            (*worker_func)(void *priv, int jobnr, int threadnr, int nb_jobs, int nb_threads);
    int            (*main_func)(void *priv);

#if LIBAVUTIL_VERSION_MAJOR < 62
    void           (*worker_func_v1)(void *priv, int jobnr, int threadnr, int nb_jobs, int nb_threads);
    void           (*main_func_v1)(void *priv);
    void            *priv_v1;
#endif
};

static int run_jobs(AVSliceThread *ctx)
{
    unsigned nb_jobs    = ctx->nb_jobs;
    unsigned nb_active_threads = ctx->nb_active_threads;
    unsigned first_job    = atomic_fetch_add_explicit(&ctx->first_job, 1, memory_order_acq_rel);
    unsigned current_job  = first_job;

    do {
        int ret = atomic_load_explicit(&ctx->error, memory_order_relaxed);
        if (ret)
            continue;
        ret = ctx->worker_func(ctx->priv, current_job, first_job, nb_jobs, nb_active_threads);
        if (ret) {
            int prev = 0;
            atomic_compare_exchange_strong_explicit(&ctx->error, &prev, ret,
                                                    memory_order_relaxed,
                                                    memory_order_relaxed);
        }
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

av_cold
int avpriv_slicethread_create2(AVSliceThread **pctx, void *priv,
                               int (*worker_func)(void *priv, int jobnr, int threadnr, int nb_jobs, int nb_threads),
                               int (*main_func)(void *priv),
                               int nb_threads)
{
    AVSliceThread *ctx;
    int nb_workers, i;
    int ret;

    av_assert0(nb_threads >= 0);
    if (!nb_threads) {
        int nb_cpus = av_cpu_count();
        if (nb_cpus > 1)
            nb_threads = FFMIN(nb_cpus + 1, MAX_AUTO_THREADS);
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
    ret = pthread_mutex_init(&ctx->done_mutex, NULL);
    if (ret) {
        av_freep(&ctx->workers);
        av_freep(pctx);
        return AVERROR(ret);
    }
    ret = pthread_cond_init(&ctx->done_cond, NULL);
    if (ret) {
        ctx->nb_threads = main_func ? 0 : 1;
        avpriv_slicethread_free(pctx);
        return AVERROR(ret);
    }
    ctx->done        = 0;

    for (i = 0; i < nb_workers; i++) {
        WorkerContext *w = &ctx->workers[i];
        w->ctx = ctx;
        ret = pthread_mutex_init(&w->mutex, NULL);
        if (ret) {
            ctx->nb_threads = main_func ? i : i + 1;
            avpriv_slicethread_free(pctx);
            return AVERROR(ret);
        }
        ret = pthread_cond_init(&w->cond, NULL);
        if (ret) {
            pthread_mutex_destroy(&w->mutex);
            ctx->nb_threads = main_func ? i : i + 1;
            avpriv_slicethread_free(pctx);
            return AVERROR(ret);
        }
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

int avpriv_slicethread_execute2(AVSliceThread *ctx, int nb_jobs, int execute_main)
{
    int nb_workers, i, is_last = 0, ret = 0;

    av_assert0(nb_jobs > 0);
    ctx->nb_jobs           = nb_jobs;
    ctx->nb_active_threads = FFMIN(nb_jobs, ctx->nb_threads);
    atomic_store_explicit(&ctx->error, 0, memory_order_relaxed);
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

    if (ctx->main_func && execute_main) {
        ret = ctx->main_func(ctx->priv);
    } else
        is_last = run_jobs(ctx);

    if (!is_last) {
        pthread_mutex_lock(&ctx->done_mutex);
        while (!ctx->done)
            pthread_cond_wait(&ctx->done_cond, &ctx->done_mutex);
        ctx->done = 0;
        pthread_mutex_unlock(&ctx->done_mutex);
    }

    if (!ret)
        ret = atomic_load_explicit(&ctx->error, memory_order_relaxed);

    return ret;
}

av_cold void avpriv_slicethread_free(AVSliceThread **pctx)
{
    AVSliceThread *ctx = *pctx;
    int nb_workers, i;

    if (!ctx)
        return;

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

/**
 * Backwards compatibility wrapper for the deprecated avpriv_ slicethread API.
 */

#if LIBAVUTIL_VERSION_MAJOR < 62

static int wrapper_worker(void *priv, int jobnr, int threadnr, int nb_jobs, int nb_threads)
{
    AVSliceThread *ctx = priv;
    ctx->worker_func_v1(ctx->priv_v1, jobnr, threadnr, nb_jobs, nb_threads);
    return 0;
}

static int wrapper_main(void *priv)
{
    AVSliceThread *ctx = priv;
    ctx->main_func_v1(ctx->priv_v1);
    return 0;
}

int avpriv_slicethread_create(AVSliceThread **pctx, void *priv,
                              void (*worker_func)(void *priv, int jobnr, int threadnr, int nb_jobs, int nb_threads),
                              void (*main_func)(void *priv),
                              int nb_threads)
{
    int ret = avpriv_slicethread_create2(pctx, NULL, wrapper_worker,
                                         main_func ? wrapper_main : NULL,
                                         nb_threads);
    if (ret < 0)
        return ret;

    (*pctx)->priv           = *pctx;
    (*pctx)->priv_v1        = priv;
    (*pctx)->worker_func_v1 = worker_func;
    (*pctx)->main_func_v1   = main_func;
    return ret;
}

void avpriv_slicethread_execute(AVSliceThread *ctx, int nb_jobs, int execute_main)
{
    avpriv_slicethread_execute2(ctx, nb_jobs, execute_main);
}

#endif /* LIBAVUTIL_VERSION_MAJOR < 62 */

#else /* HAVE_PTHREADS || HAVE_W32THREADS || HAVE_OS32THREADS */

int avpriv_slicethread_create2(AVSliceThread **pctx, void *priv,
                               int (*worker_func)(void *priv, int jobnr, int threadnr, int nb_jobs, int nb_threads),
                               int (*main_func)(void *priv),
                               int nb_threads)
{
    *pctx = NULL;
    return AVERROR(ENOSYS);
}

int avpriv_slicethread_execute2(AVSliceThread *ctx, int nb_jobs, int execute_main)
{
    av_assert0(0);
}

void avpriv_slicethread_free(AVSliceThread **pctx)
{
    av_assert0(!pctx || !*pctx);
}

#if LIBAVUTIL_VERSION_MAJOR < 62

int avpriv_slicethread_create(AVSliceThread **pctx, void *priv,
                              void (*worker_func)(void *priv, int jobnr, int threadnr, int nb_jobs, int nb_threads),
                              void (*main_func)(void *priv),
                              int nb_threads)
{
    *pctx = NULL;
    return AVERROR(ENOSYS);
}

void avpriv_slicethread_execute(AVSliceThread *ctx, int nb_jobs, int execute_main)
{
    av_assert0(0);
}

#endif /* LIBAVUTIL_VERSION_MAJOR < 62 */

#endif /* HAVE_PTHREADS || HAVE_W32THREADS || HAVE_OS32THREADS */
