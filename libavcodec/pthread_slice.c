/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Slice multithreading support functions
 * @see doc/multithreading.txt
 */

#include "config.h"

#if HAVE_PTHREADS
#include <pthread.h>
#elif HAVE_W32THREADS
#include "compat/w32pthreads.h"
#endif

#include "avcodec.h"
#include "internal.h"
#include "pthread_internal.h"
#include "thread.h"

#include "libavutil/common.h"
#include "libavutil/cpu.h"
#include "libavutil/mem.h"

typedef int (action_func)(AVCodecContext *c, void *arg);
typedef int (action_func2)(AVCodecContext *c, void *arg, int jobnr, int threadnr);

typedef struct SliceThreadContext {
    pthread_t *workers;
    action_func *func;
    action_func2 *func2;
    void *args;
    int *rets;
    int rets_count;
    int job_count;
    int job_size;

    pthread_cond_t last_job_cond;
    pthread_cond_t current_job_cond;
    pthread_mutex_t current_job_lock;
    unsigned current_execute;
    int current_job;
    int done;
} SliceThreadContext;

static void* attribute_align_arg worker(void *v)
{
    AVCodecContext *avctx = v;
    SliceThreadContext *c = avctx->internal->thread_ctx;
    unsigned last_execute = 0;
    int our_job = c->job_count;
    int thread_count = avctx->thread_count;
    int self_id;

    pthread_mutex_lock(&c->current_job_lock);
    self_id = c->current_job++;
    for (;;){
        while (our_job >= c->job_count) {
            if (c->current_job == thread_count + c->job_count)
                pthread_cond_signal(&c->last_job_cond);

            while (last_execute == c->current_execute && !c->done)
                pthread_cond_wait(&c->current_job_cond, &c->current_job_lock);
            last_execute = c->current_execute;
            our_job = self_id;

            if (c->done) {
                pthread_mutex_unlock(&c->current_job_lock);
                return NULL;
            }
        }
        pthread_mutex_unlock(&c->current_job_lock);

        c->rets[our_job%c->rets_count] = c->func ? c->func(avctx, (char*)c->args + our_job*c->job_size):
                                                   c->func2(avctx, c->args, our_job, self_id);

        pthread_mutex_lock(&c->current_job_lock);
        our_job = c->current_job++;
    }
}

void ff_slice_thread_free(AVCodecContext *avctx)
{
    SliceThreadContext *c = avctx->internal->thread_ctx;
    int i;

    pthread_mutex_lock(&c->current_job_lock);
    c->done = 1;
    pthread_cond_broadcast(&c->current_job_cond);
    pthread_mutex_unlock(&c->current_job_lock);

    for (i=0; i<avctx->thread_count; i++)
         pthread_join(c->workers[i], NULL);

    pthread_mutex_destroy(&c->current_job_lock);
    pthread_cond_destroy(&c->current_job_cond);
    pthread_cond_destroy(&c->last_job_cond);
    av_free(c->workers);
    av_freep(&avctx->internal->thread_ctx);
}

static av_always_inline void thread_park_workers(SliceThreadContext *c, int thread_count)
{
    while (c->current_job != thread_count + c->job_count)
        pthread_cond_wait(&c->last_job_cond, &c->current_job_lock);
    pthread_mutex_unlock(&c->current_job_lock);
}

static int thread_execute(AVCodecContext *avctx, action_func* func, void *arg, int *ret, int job_count, int job_size)
{
    SliceThreadContext *c = avctx->internal->thread_ctx;
    int dummy_ret;

    if (!(avctx->active_thread_type&FF_THREAD_SLICE) || avctx->thread_count <= 1)
        return avcodec_default_execute(avctx, func, arg, ret, job_count, job_size);

    if (job_count <= 0)
        return 0;

    pthread_mutex_lock(&c->current_job_lock);

    c->current_job = avctx->thread_count;
    c->job_count = job_count;
    c->job_size = job_size;
    c->args = arg;
    c->func = func;
    if (ret) {
        c->rets = ret;
        c->rets_count = job_count;
    } else {
        c->rets = &dummy_ret;
        c->rets_count = 1;
    }
    c->current_execute++;
    pthread_cond_broadcast(&c->current_job_cond);

    thread_park_workers(c, avctx->thread_count);

    return 0;
}

static int thread_execute2(AVCodecContext *avctx, action_func2* func2, void *arg, int *ret, int job_count)
{
    SliceThreadContext *c = avctx->internal->thread_ctx;
    c->func2 = func2;
    return thread_execute(avctx, NULL, arg, ret, job_count, 0);
}

int ff_slice_thread_init(AVCodecContext *avctx)
{
    int i;
    SliceThreadContext *c;
    int thread_count = avctx->thread_count;

#if HAVE_W32THREADS
    w32thread_init();
#endif

    if (!thread_count) {
        int nb_cpus = av_cpu_count();
        av_log(avctx, AV_LOG_DEBUG, "detected %d logical cores\n", nb_cpus);
        // use number of cores + 1 as thread count if there is more than one
        if (nb_cpus > 1)
            thread_count = avctx->thread_count = FFMIN(nb_cpus + 1, MAX_AUTO_THREADS);
        else
            thread_count = avctx->thread_count = 1;
    }

    if (thread_count <= 1) {
        avctx->active_thread_type = 0;
        return 0;
    }

    c = av_mallocz(sizeof(SliceThreadContext));
    if (!c)
        return -1;

    c->workers = av_mallocz(sizeof(pthread_t)*thread_count);
    if (!c->workers) {
        av_free(c);
        return -1;
    }

    avctx->internal->thread_ctx = c;
    c->current_job = 0;
    c->job_count = 0;
    c->job_size = 0;
    c->done = 0;
    pthread_cond_init(&c->current_job_cond, NULL);
    pthread_cond_init(&c->last_job_cond, NULL);
    pthread_mutex_init(&c->current_job_lock, NULL);
    pthread_mutex_lock(&c->current_job_lock);
    for (i=0; i<thread_count; i++) {
        if(pthread_create(&c->workers[i], NULL, worker, avctx)) {
           avctx->thread_count = i;
           pthread_mutex_unlock(&c->current_job_lock);
           ff_thread_free(avctx);
           return -1;
        }
    }

    thread_park_workers(c, thread_count);

    avctx->execute = thread_execute;
    avctx->execute2 = thread_execute2;
    return 0;
}
