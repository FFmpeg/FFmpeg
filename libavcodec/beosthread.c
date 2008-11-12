/*
 * Copyright (c) 2004 François Revol <revol@free.fr>
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
//#define DEBUG

#include "avcodec.h"

#include <OS.h>

typedef struct ThreadContext{
    AVCodecContext *avctx;
    thread_id thread;
    sem_id work_sem;
    sem_id done_sem;
    int (*func)(AVCodecContext *c, void *arg);
    void *arg;
    int ret;
}ThreadContext;

// it's odd Be never patented that :D
struct benaphore {
        vint32 atom;
        sem_id sem;
};
static inline int lock_ben(struct benaphore *ben)
{
        if (atomic_add(&ben->atom, 1) > 0)
                return acquire_sem(ben->sem);
        return B_OK;
}
static inline int unlock_ben(struct benaphore *ben)
{
        if (atomic_add(&ben->atom, -1) > 1)
                return release_sem(ben->sem);
        return B_OK;
}

static struct benaphore av_thread_lib_ben;

static int32 ff_thread_func(void *v){
    ThreadContext *c= v;

    for(;;){
//printf("thread_func %X enter wait\n", (int)v); fflush(stdout);
        acquire_sem(c->work_sem);
//printf("thread_func %X after wait (func=%X)\n", (int)v, (int)c->func); fflush(stdout);
        if(c->func)
            c->ret= c->func(c->avctx, c->arg);
        else
            return 0;
//printf("thread_func %X signal complete\n", (int)v); fflush(stdout);
        release_sem(c->done_sem);
    }

    return B_OK;
}

/**
 * Free what has been allocated by avcodec_thread_init().
 * Must be called after decoding has finished, especially do not call while avcodec_thread_execute() is running.
 */
void avcodec_thread_free(AVCodecContext *s){
    ThreadContext *c= s->thread_opaque;
    int i;
    int32 ret;

    for(i=0; i<s->thread_count; i++){

        c[i].func= NULL;
        release_sem(c[i].work_sem);
        wait_for_thread(c[i].thread, &ret);
        if(c[i].work_sem > B_OK) delete_sem(c[i].work_sem);
        if(c[i].done_sem > B_OK) delete_sem(c[i].done_sem);
    }

    av_freep(&s->thread_opaque);
}

int avcodec_thread_execute(AVCodecContext *s, int (*func)(AVCodecContext *c2, void *arg2),void *arg, int *ret, int count, int size){
    ThreadContext *c= s->thread_opaque;
    int i;

    assert(s == c->avctx);
    assert(count <= s->thread_count);

    /* note, we can be certain that this is not called with the same AVCodecContext by different threads at the same time */

    for(i=0; i<count; i++){
        c[i].arg= (char*)arg + i*size;
        c[i].func= func;
        c[i].ret= 12345;

        release_sem(c[i].work_sem);
    }
    for(i=0; i<count; i++){
        acquire_sem(c[i].done_sem);

        c[i].func= NULL;
        if(ret) ret[i]= c[i].ret;
    }
    return 0;
}

int avcodec_thread_init(AVCodecContext *s, int thread_count){
    int i;
    ThreadContext *c;

    s->thread_count= thread_count;

    assert(!s->thread_opaque);
    c= av_mallocz(sizeof(ThreadContext)*thread_count);
    s->thread_opaque= c;

    for(i=0; i<thread_count; i++){
//printf("init semaphors %d\n", i); fflush(stdout);
        c[i].avctx= s;

        if((c[i].work_sem = create_sem(0, "ff work sem")) < B_OK)
            goto fail;
        if((c[i].done_sem = create_sem(0, "ff done sem")) < B_OK)
            goto fail;

//printf("create thread %d\n", i); fflush(stdout);
        c[i].thread = spawn_thread(ff_thread_func, "libavcodec thread", B_LOW_PRIORITY, &c[i] );
        if( c[i].thread < B_OK ) goto fail;
        resume_thread(c[i].thread );
    }
//printf("init done\n"); fflush(stdout);

    s->execute= avcodec_thread_execute;

    return 0;
fail:
    avcodec_thread_free(s);
    return -1;
}

/* provide a mean to serialize calls to avcodec_*() for thread safety. */

int avcodec_thread_lock_lib(void)
{
        return lock_ben(&av_thread_lib_ben);
}

int avcodec_thread_unlock_lib(void)
{
        return unlock_ben(&av_thread_lib_ben);
}

/* our versions of _init and _fini (which are called by those actually from crt.o) */

void initialize_after(void)
{
        av_thread_lib_ben.atom = 0;
        av_thread_lib_ben.sem = create_sem(0, "libavcodec benaphore");
}

void uninitialize_before(void)
{
        delete_sem(av_thread_lib_ben.sem);
}



