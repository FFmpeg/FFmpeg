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

typedef struct JobContext{
    sem_t available_sem;
    int assigned;
    int (*func)(AVCodecContext *c, void *arg);
    void *arg;
    int ret;
}JobContext;

typedef struct WorkerContext{
    AVCodecContext *avctx;
    pthread_t thread;
    int start_index;
    sem_t work_sem;
    sem_t done_sem;
}WorkerContext;

typedef struct ThreadContext{
    WorkerContext *worker;
    JobContext *job;
    int job_count;
    int allocated_job_count;
}ThreadContext;

static void * thread_func(void *v){
    WorkerContext *w= v;
    ThreadContext *c= w->avctx->thread_opaque;
    int i;

    for(;;){
//av_log(w->avctx, AV_LOG_DEBUG, "thread_func %X enter wait\n", (int)v);
        sem_wait(&w->work_sem);
//av_log(w->avctx, AV_LOG_DEBUG, "thread_func %X after wait\n", (int)v);
        if(c->job_count == 0)
           break;
        
        for(i=0; i<c->job_count; i++){
            int index= (i + w->start_index) % c->job_count;
            JobContext *j= &c->job[index];
        
//av_log(w->avctx, AV_LOG_DEBUG, "thread_func %X first check of %d\n", (int)v, index);
            if(j->assigned) continue; //unsynced check, if != 0 it is already given to another worker, it never becomes available before the next execute() call so this should be safe
            
//av_log(w->avctx, AV_LOG_DEBUG, "thread_func %X second check of %d\n", (int)v, index);
            if(sem_trywait(&j->available_sem) == 0){
                j->assigned=1;
                j->ret= j->func(w->avctx, j->arg);
//av_log(w->avctx, AV_LOG_DEBUG, "thread_func %X done %d\n", (int)v, index);
            }
        }
//av_log(w->avctx, AV_LOG_DEBUG, "thread_func %X complete\n", (int)v);
        sem_post(&w->done_sem);
    }
    
    return NULL;
}

/**
 * free what has been allocated by avcodec_thread_init().
 * must be called after decoding has finished, especially dont call while avcodec_thread_execute() is running
 */
void avcodec_thread_free(AVCodecContext *s){
    ThreadContext *c= s->thread_opaque;
    int i, val;
    
    for(i=0; i<c->allocated_job_count; i++){
        sem_getvalue(&c->job[i].available_sem, &val); assert(val == 0);
        sem_destroy(&c->job[i].available_sem);
    }

    c->job_count= 0;
    for(i=0; i<s->thread_count; i++){
        sem_getvalue(&c->worker[i].work_sem, &val); assert(val == 0);
        sem_getvalue(&c->worker[i].done_sem, &val); assert(val == 0);

        sem_post(&c->worker[i].work_sem);
        pthread_join(c->worker[i].thread, NULL);
        sem_destroy(&c->worker[i].work_sem);
        sem_destroy(&c->worker[i].done_sem);
    }

    av_freep(&c->job);
    av_freep(&c->worker);
    av_freep(&s->thread_opaque);
}

int avcodec_thread_execute(AVCodecContext *s, int (*func)(AVCodecContext *c2, void *arg2),void **arg, int *ret, int job_count){
    ThreadContext *c= s->thread_opaque;
    int i, val;
    
    assert(s == c->avctx);
    if(job_count > c->allocated_job_count){
        c->job= av_realloc(c->job, job_count*sizeof(JobContext));

        for(i=c->allocated_job_count; i<job_count; i++){
            memset(&c->job[i], 0, sizeof(JobContext));
            c->allocated_job_count++;

            if(sem_init(&c->job[i].available_sem, 0, 0))
                return -1;
        }
    }
    c->job_count= job_count;
    
    /* note, we can be certain that this is not called with the same AVCodecContext by different threads at the same time */

    for(i=0; i<job_count; i++){
        sem_getvalue(&c->job[i].available_sem, &val); assert(val == 0);
        
        c->job[i].arg= arg[i];
        c->job[i].func= func;
        c->job[i].ret= 12345;
        c->job[i].assigned= 0;
        sem_post(&c->job[i].available_sem);
    }

    for(i=0; i<s->thread_count && i<job_count; i++){
        sem_getvalue(&c->worker[i].work_sem, &val); assert(val == 0);
        sem_getvalue(&c->worker[i].done_sem, &val); assert(val == 0);

        c->worker[i].start_index= (i + job_count/2)/job_count;
//av_log(s, AV_LOG_DEBUG, "start worker %d\n", i);
        sem_post(&c->worker[i].work_sem);
    }

    for(i=0; i<s->thread_count && i<job_count; i++){
//av_log(s, AV_LOG_DEBUG, "wait for worker %d\n", i);
        sem_wait(&c->worker[i].done_sem);

        sem_getvalue(&c->worker[i].work_sem, &val); assert(val == 0);
        sem_getvalue(&c->worker[i].done_sem, &val); assert(val == 0);
    }

    for(i=0; i<job_count; i++){
        sem_getvalue(&c->job[i].available_sem, &val); assert(val == 0);
        
        c->job[i].func= NULL;
        if(ret) ret[i]= c->job[i].ret;
    }

    return 0;
}

int avcodec_thread_init(AVCodecContext *s, int thread_count){
    int i;
    ThreadContext *c;
    WorkerContext *worker;

    s->thread_count= thread_count;

    assert(!s->thread_opaque);
    c= av_mallocz(sizeof(ThreadContext));
    worker= av_mallocz(sizeof(WorkerContext)*thread_count);
    s->thread_opaque= c;
    c->worker= worker;
        
    for(i=0; i<thread_count; i++){
//printf("init semaphors %d\n", i); fflush(stdout);
        worker[i].avctx= s;
        if(sem_init(&worker[i].work_sem, 0, 0))
            goto fail;
        if(sem_init(&worker[i].done_sem, 0, 0))
            goto fail;
//printf("create thread %d\n", i); fflush(stdout);
        if(pthread_create(&worker[i].thread, NULL, thread_func, &worker[i]))
            goto fail;
    }
//printf("init done\n"); fflush(stdout);
    
    s->execute= avcodec_thread_execute;

    return 0;
fail:
    avcodec_thread_free(s);
    return -1;
}
