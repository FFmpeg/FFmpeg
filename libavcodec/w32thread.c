/*
 * Copyright (c) 2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
//#define DEBUG

#include "avcodec.h"
#include "common.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>

typedef struct ThreadContext{
    AVCodecContext *avctx;
    HANDLE thread;
    HANDLE work_sem;
    HANDLE done_sem;
    int (*func)(AVCodecContext *c, void *arg);
    void *arg;
    int ret;
}ThreadContext;


static unsigned __stdcall thread_func(void *v){
    ThreadContext *c= v;

    for(;;){
//printf("thread_func %X enter wait\n", (int)v); fflush(stdout);
        WaitForSingleObject(c->work_sem, INFINITE);
//printf("thread_func %X after wait (func=%X)\n", (int)v, (int)c->func); fflush(stdout);
        if(c->func)
            c->ret= c->func(c->avctx, c->arg);
        else
            return 0;
//printf("thread_func %X signal complete\n", (int)v); fflush(stdout);
        ReleaseSemaphore(c->done_sem, 1, 0);
    }
    
    return 0;
}

/**
 * free what has been allocated by avcodec_thread_init().
 * must be called after decoding has finished, especially dont call while avcodec_thread_execute() is running
 */
void avcodec_thread_free(AVCodecContext *s){
    ThreadContext *c= s->thread_opaque;
    int i;

    for(i=0; i<s->thread_count; i++){
        
        c[i].func= NULL;
        ReleaseSemaphore(c[i].work_sem, 1, 0);
        WaitForSingleObject(c[i].thread, INFINITE);
        if(c[i].work_sem) CloseHandle(c[i].work_sem);
        if(c[i].done_sem) CloseHandle(c[i].done_sem);
    }

    av_freep(&s->thread_opaque);
}

int avcodec_thread_execute(AVCodecContext *s, int (*func)(AVCodecContext *c2, void *arg2),void **arg, int *ret, int count){
    ThreadContext *c= s->thread_opaque;
    int i;
    
    assert(s == c->avctx);
    assert(count <= s->thread_count);
    
    /* note, we can be certain that this is not called with the same AVCodecContext by different threads at the same time */

    for(i=0; i<count; i++){
        c[i].arg= arg[i];
        c[i].func= func;
        c[i].ret= 12345;

        ReleaseSemaphore(c[i].work_sem, 1, 0);
    }
    for(i=0; i<count; i++){
        WaitForSingleObject(c[i].done_sem, INFINITE);
        
        c[i].func= NULL;
        if(ret) ret[i]= c[i].ret;
    }
    return 0;
}

int avcodec_thread_init(AVCodecContext *s, int thread_count){
    int i;
    ThreadContext *c;
    uint32_t threadid;

    s->thread_count= thread_count;

    assert(!s->thread_opaque);
    c= av_mallocz(sizeof(ThreadContext)*thread_count);
    s->thread_opaque= c;
    
    for(i=0; i<thread_count; i++){
//printf("init semaphors %d\n", i); fflush(stdout);
        c[i].avctx= s;

        if(!(c[i].work_sem = CreateSemaphore(NULL, 0, s->thread_count, NULL)))
            goto fail;
        if(!(c[i].done_sem = CreateSemaphore(NULL, 0, s->thread_count, NULL)))
            goto fail;

//printf("create thread %d\n", i); fflush(stdout);
        c[i].thread = (HANDLE)_beginthreadex(NULL, 0, thread_func, &c[i], 0, &threadid );
        if( !c[i].thread ) goto fail;
    }
//printf("init done\n"); fflush(stdout);
    
    s->execute= avcodec_thread_execute;

    return 0;
fail:
    avcodec_thread_free(s);
    return -1;
}
