/*
 * Copyright (c) 2004 Roman Shaposhnik.
 *
 * Many thanks to Steven M. Schultz for providing clever ideas and
 * to Michael Niedermayer <michaelni@gmx.at> for writing initial
 * implementation.
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
 *
 */
#include <pthread.h>

#include "avcodec.h"
#include "common.h"

typedef int (action_t)(AVCodecContext *c, void *arg);

typedef struct ThreadContext {
    pthread_t *workers;
    action_t *func;
    void **args;
    int *rets;
    int rets_count;
    int job_count;

    pthread_cond_t last_job_cond;
    pthread_cond_t current_job_cond;
    pthread_mutex_t current_job_lock;
    int current_job;
    int done;
} ThreadContext;

static void* worker(void *v)
{
    AVCodecContext *avctx = v;
    ThreadContext *c = avctx->thread_opaque;
    int our_job = c->job_count;
    int thread_count = avctx->thread_count;
    int self_id;

    pthread_mutex_lock(&c->current_job_lock);
    self_id = c->current_job++;
    for (;;){
        while (our_job >= c->job_count) {
            if (c->current_job == thread_count + c->job_count)
                pthread_cond_signal(&c->last_job_cond);

            pthread_cond_wait(&c->current_job_cond, &c->current_job_lock);
            our_job = self_id;

            if (c->done) {
                pthread_mutex_unlock(&c->current_job_lock);
                return NULL;
            }
        }
        pthread_mutex_unlock(&c->current_job_lock);

        c->rets[our_job%c->rets_count] = c->func(avctx, c->args[our_job]);

        pthread_mutex_lock(&c->current_job_lock);
        our_job = c->current_job++;
    }
}

static av_always_inline void avcodec_thread_park_workers(ThreadContext *c, int thread_count)
{
    pthread_cond_wait(&c->last_job_cond, &c->current_job_lock);
    pthread_mutex_unlock(&c->current_job_lock);
}

void avcodec_thread_free(AVCodecContext *avctx)
{
    ThreadContext *c = avctx->thread_opaque;
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
    av_free(c);
}

int avcodec_thread_execute(AVCodecContext *avctx, action_t* func, void **arg, int *ret, int job_count)
{
    ThreadContext *c= avctx->thread_opaque;
    int dummy_ret;

    if (job_count <= 0)
        return 0;

    pthread_mutex_lock(&c->current_job_lock);

    c->current_job = avctx->thread_count;
    c->job_count = job_count;
    c->args = arg;
    c->func = func;
    if (ret) {
        c->rets = ret;
        c->rets_count = job_count;
    } else {
        c->rets = &dummy_ret;
        c->rets_count = 1;
    }
    pthread_cond_broadcast(&c->current_job_cond);

    avcodec_thread_park_workers(c, avctx->thread_count);

    return 0;
}

int avcodec_thread_init(AVCodecContext *avctx, int thread_count)
{
    int i;
    ThreadContext *c;

    c = av_mallocz(sizeof(ThreadContext));
    if (!c)
        return -1;

    c->workers = av_mallocz(sizeof(pthread_t)*thread_count);
    if (!c->workers) {
        av_free(c);
        return -1;
    }

    avctx->thread_opaque = c;
    avctx->thread_count = thread_count;
    c->current_job = 0;
    c->job_count = 0;
    c->done = 0;
    pthread_cond_init(&c->current_job_cond, NULL);
    pthread_cond_init(&c->last_job_cond, NULL);
    pthread_mutex_init(&c->current_job_lock, NULL);
    pthread_mutex_lock(&c->current_job_lock);
    for (i=0; i<thread_count; i++) {
        if(pthread_create(&c->workers[i], NULL, worker, avctx)) {
           avctx->thread_count = i;
           pthread_mutex_unlock(&c->current_job_lock);
           avcodec_thread_free(avctx);
           return -1;
        }
    }

    avcodec_thread_park_workers(c, thread_count);

    avctx->execute = avcodec_thread_execute;
    return 0;
}
