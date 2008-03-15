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

// Ported by Vlad Stelmahovsky

#include "avcodec.h"

#define INCL_DOS
#define INCL_DOSERRORS
#define INCL_DOSDEVIOCTL
#include <os2.h>

typedef struct ThreadContext{
    AVCodecContext *avctx;
    int thread;
    HEV work_sem;
    HEV done_sem;
    int (*func)(AVCodecContext *c, void *arg);
    void *arg;
    int ret;
}ThreadContext;


void attribute_align_arg thread_func(void *v){
    ThreadContext *c= v;

    for(;;){
        //printf("thread_func %X enter wait\n", (int)v); fflush(stdout);
        DosWaitEventSem(c->work_sem, SEM_INDEFINITE_WAIT);
//        WaitForSingleObject(c->work_sem, INFINITE);
//printf("thread_func %X after wait (func=%X)\n", (int)v, (int)c->func); fflush(stdout);
        if(c->func)
            c->ret= c->func(c->avctx, c->arg);
        else
            return;
        //printf("thread_func %X signal complete\n", (int)v); fflush(stdout);
        DosPostEventSem(c->done_sem);
//        ReleaseSemaphore(c->done_sem, 1, 0);
    }

    return;
}

/**
 * free what has been allocated by avcodec_thread_init().
 * must be called after decoding has finished, especially do not call while avcodec_thread_execute() is running
 */
void avcodec_thread_free(AVCodecContext *s){
    ThreadContext *c= s->thread_opaque;
    int i;

    for(i=0; i<s->thread_count; i++){

        c[i].func= NULL;
        DosPostEventSem(c[i].work_sem);
        //        ReleaseSemaphore(c[i].work_sem, 1, 0);
        DosWaitThread((PTID)&c[i].thread,DCWW_WAIT);
//        WaitForSingleObject(c[i].thread, INFINITE);
        if(c[i].work_sem) DosCloseEventSem(c[i].work_sem);//CloseHandle(c[i].work_sem);
        if(c[i].done_sem) DosCloseEventSem(c[i].done_sem);//CloseHandle(c[i].done_sem);
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

        DosPostEventSem(c[i].work_sem);
//        ReleaseSemaphore(c[i].work_sem, 1, 0);
    }
    for(i=0; i<count; i++){
        DosWaitEventSem(c[i].done_sem,SEM_INDEFINITE_WAIT);
//        WaitForSingleObject(c[i].done_sem, INFINITE);

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

        if (DosCreateEventSem(NULL,&c[i].work_sem,DC_SEM_SHARED,0))
            goto fail;
        if (DosCreateEventSem(NULL,&c[i].done_sem,DC_SEM_SHARED,0))
            goto fail;

//printf("create thread %d\n", i); fflush(stdout);
//        c[i].thread = (HANDLE)_beginthreadex(NULL, 0, thread_func, &c[i], 0, &threadid );
        c[i].thread = _beginthread(thread_func, NULL, 0x10000, &c[i]);
        if( c[i].thread <= 0 ) goto fail;
    }
//printf("init done\n"); fflush(stdout);

    s->execute= avcodec_thread_execute;

    return 0;
fail:
    avcodec_thread_free(s);
    return -1;
}
