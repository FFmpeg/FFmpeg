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
#include <semaphore.h>
#include <pthread.h>

//#define DEBUG

#include "avcodec.h"
#include "common.h"


typedef struct ThreadContext{
    AVCodecContext *avctx;
    pthread_t thread;
    sem_t work_sem;
    sem_t done_sem;
    int (*func)(AVCodecContext *c, void *arg);
    void *arg;
    int ret;
}ThreadContext;

static void * thread_func(void *v){
    ThreadContext *c= v;

    for(;;){
//printf("thread_func %X enter wait\n", (int)v); fflush(stdout);
        sem_wait(&c->work_sem);
//printf("thread_func %X after wait (func=%X)\n", (int)v, (int)c->func); fflush(stdout);
        if(c->func)
            c->ret= c->func(c->avctx, c->arg);
        else
            return NULL;
//printf("thread_func %X signal complete\n", (int)v); fflush(stdout);
        sem_post(&c->done_sem);
    }
    
    return NULL;
}

/**
 * free what has been allocated by avcodec_pthread_init().
 * must be called after decoding has finished, especially dont call while avcodec_pthread_execute() is running
 */
void avcodec_pthread_free(AVCodecContext *s){
    ThreadContext *c= s->thread_opaque;
    int i;

    for(i=0; i<s->thread_count; i++){
        int val;
        
        sem_getvalue(&c[i].work_sem, &val); assert(val == 0);
        sem_getvalue(&c[i].done_sem, &val); assert(val == 0);

        c[i].func= NULL;
        sem_post(&c[i].work_sem);
        pthread_join(c[i].thread, NULL);
        sem_destroy(&c[i].work_sem);
        sem_destroy(&c[i].done_sem);
    }

    av_freep(&s->thread_opaque);
}

int avcodec_pthread_execute(AVCodecContext *s, int (*func)(AVCodecContext *c2, void *arg2),void **arg, int *ret, int count){
    ThreadContext *c= s->thread_opaque;
    int i, val;
    
    assert(s == c->avctx);
    assert(count <= s->thread_count);
    
    /* note, we can be certain that this is not called with the same AVCodecContext by different threads at the same time */

    for(i=0; i<count; i++){
        sem_getvalue(&c[i].work_sem, &val); assert(val == 0);
        sem_getvalue(&c[i].done_sem, &val); assert(val == 0);
        
        c[i].arg= arg[i];
        c[i].func= func;
        c[i].ret= 12345;
        sem_post(&c[i].work_sem);
    }
    for(i=0; i<count; i++){
        sem_wait(&c[i].done_sem);

        sem_getvalue(&c[i].work_sem, &val); assert(val == 0);
        sem_getvalue(&c[i].done_sem, &val); assert(val == 0);
        
        c[i].func= NULL;
        if(ret) ret[i]= c[i].ret;
    }
    return 0;
}

int avcodec_pthread_init(AVCodecContext *s, int thread_count){
    int i;
    ThreadContext *c;

    s->thread_count= thread_count;

    assert(!s->thread_opaque);
    c= av_mallocz(sizeof(ThreadContext)*thread_count);
    s->thread_opaque= c;
    
    for(i=0; i<thread_count; i++){
//printf("init semaphors %d\n", i); fflush(stdout);
        c[i].avctx= s;
        if(sem_init(&c[i].work_sem, 0, 0))
            goto fail;
        if(sem_init(&c[i].done_sem, 0, 0))
            goto fail;
//printf("create thread %d\n", i); fflush(stdout);
        if(pthread_create(&c[i].thread, NULL, thread_func, &c[i]))
            goto fail;
    }
//printf("init done\n"); fflush(stdout);
    
    s->execute= avcodec_pthread_execute;

    return 0;
fail:
    avcodec_pthread_free(s);
    return -1;
}
