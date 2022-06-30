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

/**
 * @file
 * Slice multithreading support functions
 * @see doc/multithreading.txt
 */

#include "config.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "internal.h"
#include "pthread_internal.h"
#include "thread.h"

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/cpu.h"
#include "libavutil/mem.h"
#include "libavutil/thread.h"
#include "libavutil/slicethread.h"

typedef int (action_func)(AVCodecContext *c, void *arg);
typedef int (action_func2)(AVCodecContext *c, void *arg, int jobnr, int threadnr);
typedef int (main_func)(AVCodecContext *c);

typedef struct Progress {
    pthread_cond_t  cond;
    pthread_mutex_t mutex;
} Progress;

typedef struct SliceThreadContext {
    AVSliceThread *thread;
    action_func *func;
    action_func2 *func2;
    main_func *mainfunc;
    void *args;
    int *rets;
    int job_size;

    int *entries;
    int entries_count;
    int thread_count;
    Progress *progress;
} SliceThreadContext;

static void main_function(void *priv) {
    AVCodecContext *avctx = priv;
    SliceThreadContext *c = avctx->internal->thread_ctx;
    c->mainfunc(avctx);
}

static void worker_func(void *priv, int jobnr, int threadnr, int nb_jobs, int nb_threads)
{
    AVCodecContext *avctx = priv;
    SliceThreadContext *c = avctx->internal->thread_ctx;
    int ret;

    ret = c->func ? c->func(avctx, (char *)c->args + c->job_size * jobnr)
                  : c->func2(avctx, c->args, jobnr, threadnr);
    if (c->rets)
        c->rets[jobnr] = ret;
}

void ff_slice_thread_free(AVCodecContext *avctx)
{
    SliceThreadContext *c = avctx->internal->thread_ctx;
    int i;

    avpriv_slicethread_free(&c->thread);

    for (i = 0; i < c->thread_count; i++) {
        Progress *const progress = &c->progress[i];
        pthread_mutex_destroy(&progress->mutex);
        pthread_cond_destroy(&progress->cond);
    }

    av_freep(&c->entries);
    av_freep(&c->progress);
    av_freep(&avctx->internal->thread_ctx);
}

static int thread_execute(AVCodecContext *avctx, action_func* func, void *arg, int *ret, int job_count, int job_size)
{
    SliceThreadContext *c = avctx->internal->thread_ctx;

    if (!(avctx->active_thread_type&FF_THREAD_SLICE) || avctx->thread_count <= 1)
        return avcodec_default_execute(avctx, func, arg, ret, job_count, job_size);

    if (job_count <= 0)
        return 0;

    c->job_size = job_size;
    c->args = arg;
    c->func = func;
    c->rets = ret;

    avpriv_slicethread_execute(c->thread, job_count, !!c->mainfunc  );
    return 0;
}

static int thread_execute2(AVCodecContext *avctx, action_func2* func2, void *arg, int *ret, int job_count)
{
    SliceThreadContext *c = avctx->internal->thread_ctx;
    c->func2 = func2;
    return thread_execute(avctx, NULL, arg, ret, job_count, 0);
}

int ff_slice_thread_execute_with_mainfunc(AVCodecContext *avctx, action_func2* func2, main_func *mainfunc, void *arg, int *ret, int job_count)
{
    SliceThreadContext *c = avctx->internal->thread_ctx;
    c->func2 = func2;
    c->mainfunc = mainfunc;
    return thread_execute(avctx, NULL, arg, ret, job_count, 0);
}

int ff_slice_thread_init(AVCodecContext *avctx)
{
    SliceThreadContext *c;
    int thread_count = avctx->thread_count;
    void (*mainfunc)(void *);

    // We cannot do this in the encoder init as the threads are created before
    if (av_codec_is_encoder(avctx->codec) &&
        avctx->codec_id == AV_CODEC_ID_MPEG1VIDEO &&
        avctx->height > 2800)
        thread_count = avctx->thread_count = 1;

    if (!thread_count) {
        int nb_cpus = av_cpu_count();
        if  (avctx->height)
            nb_cpus = FFMIN(nb_cpus, (avctx->height+15)/16);
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

    avctx->internal->thread_ctx = c = av_mallocz(sizeof(*c));
    mainfunc = ffcodec(avctx->codec)->caps_internal & FF_CODEC_CAP_SLICE_THREAD_HAS_MF ? &main_function : NULL;
    if (!c || (thread_count = avpriv_slicethread_create(&c->thread, avctx, worker_func, mainfunc, thread_count)) <= 1) {
        if (c)
            avpriv_slicethread_free(&c->thread);
        av_freep(&avctx->internal->thread_ctx);
        avctx->thread_count = 1;
        avctx->active_thread_type = 0;
        return 0;
    }
    avctx->thread_count = thread_count;

    avctx->execute = thread_execute;
    avctx->execute2 = thread_execute2;
    return 0;
}

int av_cold ff_slice_thread_init_progress(AVCodecContext *avctx)
{
    SliceThreadContext *const p = avctx->internal->thread_ctx;
    int err, i = 0, thread_count = avctx->thread_count;

    p->progress = av_calloc(thread_count, sizeof(*p->progress));
    if (!p->progress) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    for (; i < thread_count; i++) {
        Progress *const progress = &p->progress[i];
        err = pthread_mutex_init(&progress->mutex, NULL);
        if (err) {
            err = AVERROR(err);
            goto fail;
        }
        err = pthread_cond_init (&progress->cond,  NULL);
        if (err) {
            err = AVERROR(err);
            pthread_mutex_destroy(&progress->mutex);
            goto fail;
        }
    }
    err = 0;
fail:
    p->thread_count = i;
    return err;
}

void ff_thread_report_progress2(AVCodecContext *avctx, int field, int thread, int n)
{
    SliceThreadContext *p = avctx->internal->thread_ctx;
    Progress *const progress = &p->progress[thread];
    int *entries = p->entries;

    pthread_mutex_lock(&progress->mutex);
    entries[field] +=n;
    pthread_cond_signal(&progress->cond);
    pthread_mutex_unlock(&progress->mutex);
}

void ff_thread_await_progress2(AVCodecContext *avctx, int field, int thread, int shift)
{
    SliceThreadContext *p  = avctx->internal->thread_ctx;
    Progress *progress;
    int *entries      = p->entries;

    if (!entries || !field) return;

    thread = thread ? thread - 1 : p->thread_count - 1;
    progress = &p->progress[thread];

    pthread_mutex_lock(&progress->mutex);
    while ((entries[field - 1] - entries[field]) < shift){
        pthread_cond_wait(&progress->cond, &progress->mutex);
    }
    pthread_mutex_unlock(&progress->mutex);
}

int ff_slice_thread_allocz_entries(AVCodecContext *avctx, int count)
{
    if (avctx->active_thread_type & FF_THREAD_SLICE)  {
        SliceThreadContext *p = avctx->internal->thread_ctx;

        if (p->entries_count == count) {
            memset(p->entries, 0, p->entries_count * sizeof(*p->entries));
            return 0;
        }
        av_freep(&p->entries);

        p->entries       = av_calloc(count, sizeof(*p->entries));
        if (!p->entries) {
            p->entries_count = 0;
            return AVERROR(ENOMEM);
        }
        p->entries_count  = count;
    }

    return 0;
}
