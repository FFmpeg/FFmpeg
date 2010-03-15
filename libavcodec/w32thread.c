/*
 * Copyright (c) 2004 Michael Niedermayer <michaelni@gmx.at>
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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>

typedef struct ThreadContext{
    AVCodecContext *avctx;
    HANDLE thread;
    HANDLE work_sem;
    HANDLE job_sem;
    HANDLE done_sem;
    int (*func)(AVCodecContext *c, void *arg);
    int (*func2)(AVCodecContext *c, void *arg, int, int);
    void *arg;
    int argsize;
    int *jobnr;
    int *ret;
    int threadnr;
}ThreadContext;


static unsigned WINAPI attribute_align_arg thread_func(void *v){
    ThreadContext *c= v;

    for(;;){
        int ret, jobnr;
//printf("thread_func %X enter wait\n", (int)v); fflush(stdout);
        WaitForSingleObject(c->work_sem, INFINITE);
        // avoid trying to access jobnr if we should quit
        if (!c->func && !c->func2)
            break;
        WaitForSingleObject(c->job_sem, INFINITE);
        jobnr = (*c->jobnr)++;
        ReleaseSemaphore(c->job_sem, 1, 0);
//printf("thread_func %X after wait (func=%X)\n", (int)v, (int)c->func); fflush(stdout);
        if(c->func)
            ret= c->func(c->avctx, (uint8_t *)c->arg + jobnr*c->argsize);
        else
            ret= c->func2(c->avctx, c->arg, jobnr, c->threadnr);
        if (c->ret)
            c->ret[jobnr] = ret;
//printf("thread_func %X signal complete\n", (int)v); fflush(stdout);
        ReleaseSemaphore(c->done_sem, 1, 0);
    }

    return 0;
}

/**
 * Free what has been allocated by avcodec_thread_init().
 * Must be called after decoding has finished, especially do not call while avcodec_thread_execute() is running.
 */
void avcodec_thread_free(AVCodecContext *s){
    ThreadContext *c= s->thread_opaque;
    int i;

    for(i=0; i<s->thread_count; i++){

        c[i].func= NULL;
        c[i].func2= NULL;
    }
    ReleaseSemaphore(c[0].work_sem, s->thread_count, 0);
    for(i=0; i<s->thread_count; i++){
        WaitForSingleObject(c[i].thread, INFINITE);
        if(c[i].thread)   CloseHandle(c[i].thread);
    }
    if(c[0].work_sem) CloseHandle(c[0].work_sem);
    if(c[0].job_sem)  CloseHandle(c[0].job_sem);
    if(c[0].done_sem) CloseHandle(c[0].done_sem);

    av_freep(&s->thread_opaque);
}

static int avcodec_thread_execute(AVCodecContext *s, int (*func)(AVCodecContext *c2, void *arg2),void *arg, int *ret, int count, int size){
    ThreadContext *c= s->thread_opaque;
    int i;
    int jobnr = 0;

    assert(s == c->avctx);

    /* note, we can be certain that this is not called with the same AVCodecContext by different threads at the same time */

    for(i=0; i<s->thread_count; i++){
        c[i].arg= arg;
        c[i].argsize= size;
        c[i].func= func;
        c[i].ret= ret;
        c[i].jobnr = &jobnr;
    }
    ReleaseSemaphore(c[0].work_sem, count, 0);
    for(i=0; i<count; i++)
        WaitForSingleObject(c[0].done_sem, INFINITE);

    return 0;
}

static int avcodec_thread_execute2(AVCodecContext *s, int (*func)(AVCodecContext *c2, void *arg2, int, int),void *arg, int *ret, int count){
    ThreadContext *c= s->thread_opaque;
    int i;
    for(i=0; i<s->thread_count; i++)
        c[i].func2 = func;
    avcodec_thread_execute(s, NULL, arg, ret, count, 0);
}

int avcodec_thread_init(AVCodecContext *s, int thread_count){
    int i;
    ThreadContext *c;
    uint32_t threadid;

    s->thread_count= thread_count;

    if (thread_count <= 1)
        return 0;

    assert(!s->thread_opaque);
    c= av_mallocz(sizeof(ThreadContext)*thread_count);
    s->thread_opaque= c;
    if(!(c[0].work_sem = CreateSemaphore(NULL, 0, INT_MAX, NULL)))
        goto fail;
    if(!(c[0].job_sem  = CreateSemaphore(NULL, 1, 1, NULL)))
        goto fail;
    if(!(c[0].done_sem = CreateSemaphore(NULL, 0, INT_MAX, NULL)))
        goto fail;

    for(i=0; i<thread_count; i++){
//printf("init semaphors %d\n", i); fflush(stdout);
        c[i].avctx= s;
        c[i].work_sem = c[0].work_sem;
        c[i].job_sem  = c[0].job_sem;
        c[i].done_sem = c[0].done_sem;
        c[i].threadnr = i;

//printf("create thread %d\n", i); fflush(stdout);
        c[i].thread = (HANDLE)_beginthreadex(NULL, 0, thread_func, &c[i], 0, &threadid );
        if( !c[i].thread ) goto fail;
    }
//printf("init done\n"); fflush(stdout);

    s->execute= avcodec_thread_execute;
    s->execute2= avcodec_thread_execute2;

    return 0;
fail:
    avcodec_thread_free(s);
    return -1;
}
