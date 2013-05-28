/*
 * Copyright (c) 2012 Michael Niedermayer <michaelni@gmx.at>
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

#include "frame_thread_encoder.h"

#include "libavutil/fifo.h"
#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "internal.h"
#include "thread.h"

#if HAVE_PTHREADS
#include <pthread.h>
#elif HAVE_W32THREADS
#include "compat/w32pthreads.h"
#elif HAVE_OS2THREADS
#include "compat/os2threads.h"
#endif

#define MAX_THREADS 64
#define BUFFER_SIZE (2*MAX_THREADS)

typedef struct{
    void *indata;
    void *outdata;
    int64_t return_code;
    unsigned index;
} Task;

typedef struct{
    AVCodecContext *parent_avctx;
    pthread_mutex_t buffer_mutex;

    AVFifoBuffer *task_fifo;
    pthread_mutex_t task_fifo_mutex;
    pthread_cond_t task_fifo_cond;

    Task finished_tasks[BUFFER_SIZE];
    pthread_mutex_t finished_task_mutex;
    pthread_cond_t finished_task_cond;

    unsigned task_index;
    unsigned finished_task_index;

    pthread_t worker[MAX_THREADS];
    int exit;
} ThreadContext;

static void * attribute_align_arg worker(void *v){
    AVCodecContext *avctx = v;
    ThreadContext *c = avctx->internal->frame_thread_encoder;
    AVPacket *pkt = NULL;

    while(!c->exit){
        int got_packet, ret;
        AVFrame *frame;
        Task task;

        if(!pkt) pkt= av_mallocz(sizeof(*pkt));
        if(!pkt) continue;
        av_init_packet(pkt);

        pthread_mutex_lock(&c->task_fifo_mutex);
        while (av_fifo_size(c->task_fifo) <= 0 || c->exit) {
            if(c->exit){
                pthread_mutex_unlock(&c->task_fifo_mutex);
                goto end;
            }
            pthread_cond_wait(&c->task_fifo_cond, &c->task_fifo_mutex);
        }
        av_fifo_generic_read(c->task_fifo, &task, sizeof(task), NULL);
        pthread_mutex_unlock(&c->task_fifo_mutex);
        frame = task.indata;

        ret = avcodec_encode_video2(avctx, pkt, frame, &got_packet);
        pthread_mutex_lock(&c->buffer_mutex);
        av_frame_unref(frame);
        pthread_mutex_unlock(&c->buffer_mutex);
        av_frame_free(&frame);
        if(got_packet) {
            av_dup_packet(pkt);
        } else {
            pkt->data = NULL;
            pkt->size = 0;
        }
        pthread_mutex_lock(&c->finished_task_mutex);
        c->finished_tasks[task.index].outdata = pkt; pkt = NULL;
        c->finished_tasks[task.index].return_code = ret;
        pthread_cond_signal(&c->finished_task_cond);
        pthread_mutex_unlock(&c->finished_task_mutex);
    }
end:
    av_free(pkt);
    pthread_mutex_lock(&c->buffer_mutex);
    avcodec_close(avctx);
    pthread_mutex_unlock(&c->buffer_mutex);
    av_freep(&avctx);
    return NULL;
}

int ff_frame_thread_encoder_init(AVCodecContext *avctx, AVDictionary *options){
    int i=0;
    ThreadContext *c;


    if(   !(avctx->thread_type & FF_THREAD_FRAME)
       || !(avctx->codec->capabilities & CODEC_CAP_INTRA_ONLY))
        return 0;

    if(!avctx->thread_count) {
        avctx->thread_count = av_cpu_count();
        avctx->thread_count = FFMIN(avctx->thread_count, MAX_THREADS);
    }

    if(avctx->thread_count <= 1)
        return 0;

    if(avctx->thread_count > MAX_THREADS)
        return AVERROR(EINVAL);

    av_assert0(!avctx->internal->frame_thread_encoder);
    c = avctx->internal->frame_thread_encoder = av_mallocz(sizeof(ThreadContext));
    if(!c)
        return AVERROR(ENOMEM);

    c->parent_avctx = avctx;

    c->task_fifo = av_fifo_alloc(sizeof(Task) * BUFFER_SIZE);
    if(!c->task_fifo)
        goto fail;

    pthread_mutex_init(&c->task_fifo_mutex, NULL);
    pthread_mutex_init(&c->finished_task_mutex, NULL);
    pthread_mutex_init(&c->buffer_mutex, NULL);
    pthread_cond_init(&c->task_fifo_cond, NULL);
    pthread_cond_init(&c->finished_task_cond, NULL);

    for(i=0; i<avctx->thread_count ; i++){
        AVDictionary *tmp = NULL;
        void *tmpv;
        AVCodecContext *thread_avctx = avcodec_alloc_context3(avctx->codec);
        if(!thread_avctx)
            goto fail;
        tmpv = thread_avctx->priv_data;
        *thread_avctx = *avctx;
        thread_avctx->priv_data = tmpv;
        thread_avctx->internal = NULL;
        memcpy(thread_avctx->priv_data, avctx->priv_data, avctx->codec->priv_data_size);
        thread_avctx->thread_count = 1;
        thread_avctx->active_thread_type &= ~FF_THREAD_FRAME;

        av_dict_copy(&tmp, options, 0);
        av_dict_set(&tmp, "threads", "1", 0);
        if(avcodec_open2(thread_avctx, avctx->codec, &tmp) < 0) {
            av_dict_free(&tmp);
            goto fail;
        }
        av_dict_free(&tmp);
        av_assert0(!thread_avctx->internal->frame_thread_encoder);
        thread_avctx->internal->frame_thread_encoder = c;
        if(pthread_create(&c->worker[i], NULL, worker, thread_avctx)) {
            goto fail;
        }
    }

    avctx->active_thread_type = FF_THREAD_FRAME;

    return 0;
fail:
    avctx->thread_count = i;
    av_log(avctx, AV_LOG_ERROR, "ff_frame_thread_encoder_init failed\n");
    ff_frame_thread_encoder_free(avctx);
    return -1;
}

void ff_frame_thread_encoder_free(AVCodecContext *avctx){
    int i;
    ThreadContext *c= avctx->internal->frame_thread_encoder;

    pthread_mutex_lock(&c->task_fifo_mutex);
    c->exit = 1;
    pthread_cond_broadcast(&c->task_fifo_cond);
    pthread_mutex_unlock(&c->task_fifo_mutex);

    for (i=0; i<avctx->thread_count; i++) {
         pthread_join(c->worker[i], NULL);
    }

    pthread_mutex_destroy(&c->task_fifo_mutex);
    pthread_mutex_destroy(&c->finished_task_mutex);
    pthread_mutex_destroy(&c->buffer_mutex);
    pthread_cond_destroy(&c->task_fifo_cond);
    pthread_cond_destroy(&c->finished_task_cond);
    av_fifo_free(c->task_fifo); c->task_fifo = NULL;
    av_freep(&avctx->internal->frame_thread_encoder);
}

int ff_thread_video_encode_frame(AVCodecContext *avctx, AVPacket *pkt, const AVFrame *frame, int *got_packet_ptr){
    ThreadContext *c = avctx->internal->frame_thread_encoder;
    Task task;
    int ret;

    av_assert1(!*got_packet_ptr);

    if(frame){
        if(!(avctx->flags & CODEC_FLAG_INPUT_PRESERVED)){
            AVFrame *new = av_frame_alloc();
            if(!new)
                return AVERROR(ENOMEM);
            pthread_mutex_lock(&c->buffer_mutex);
            ret = ff_get_buffer(c->parent_avctx, new, 0);
            pthread_mutex_unlock(&c->buffer_mutex);
            if(ret<0)
                return ret;
            new->pts = frame->pts;
            new->quality = frame->quality;
            new->pict_type = frame->pict_type;
            av_image_copy(new->data, new->linesize, (const uint8_t **)frame->data, frame->linesize,
                          avctx->pix_fmt, avctx->width, avctx->height);
            frame = new;
        }

        task.index = c->task_index;
        task.indata = (void*)frame;
        pthread_mutex_lock(&c->task_fifo_mutex);
        av_fifo_generic_write(c->task_fifo, &task, sizeof(task), NULL);
        pthread_cond_signal(&c->task_fifo_cond);
        pthread_mutex_unlock(&c->task_fifo_mutex);

        c->task_index = (c->task_index+1) % BUFFER_SIZE;

        if(!c->finished_tasks[c->finished_task_index].outdata && (c->task_index - c->finished_task_index) % BUFFER_SIZE <= avctx->thread_count)
            return 0;
    }

    if(c->task_index == c->finished_task_index)
        return 0;

    pthread_mutex_lock(&c->finished_task_mutex);
    while (!c->finished_tasks[c->finished_task_index].outdata) {
        pthread_cond_wait(&c->finished_task_cond, &c->finished_task_mutex);
    }
    task = c->finished_tasks[c->finished_task_index];
    *pkt = *(AVPacket*)(task.outdata);
    if(pkt->data)
        *got_packet_ptr = 1;
    av_freep(&c->finished_tasks[c->finished_task_index].outdata);
    c->finished_task_index = (c->finished_task_index+1) % BUFFER_SIZE;
    pthread_mutex_unlock(&c->finished_task_mutex);

    return task.return_code;
}
