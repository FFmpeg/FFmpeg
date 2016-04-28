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
#include "libavutil/thread.h"
#include "avcodec.h"
#include "internal.h"
#include "thread.h"

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
       || !(avctx->codec->capabilities & AV_CODEC_CAP_INTRA_ONLY))
        return 0;

    if(   !avctx->thread_count
       && avctx->codec_id == AV_CODEC_ID_MJPEG
       && !(avctx->flags & AV_CODEC_FLAG_QSCALE)) {
        av_log(avctx, AV_LOG_DEBUG,
               "Forcing thread count to 1 for MJPEG encoding, use -thread_type slice "
               "or a constant quantizer if you want to use multiple cpu cores\n");
        avctx->thread_count = 1;
    }
    if(   avctx->thread_count > 1
       && avctx->codec_id == AV_CODEC_ID_MJPEG
       && !(avctx->flags & AV_CODEC_FLAG_QSCALE))
        av_log(avctx, AV_LOG_WARNING,
               "MJPEG CBR encoding works badly with frame multi-threading, consider "
               "using -threads 1, -thread_type slice or a constant quantizer.\n");

    if (avctx->codec_id == AV_CODEC_ID_HUFFYUV ||
        avctx->codec_id == AV_CODEC_ID_FFVHUFF) {
        int warn = 0;
        int context_model = 0;
        AVDictionaryEntry *con = av_dict_get(options, "context", NULL, AV_DICT_MATCH_CASE);

        if (con && con->value)
            context_model = atoi(con->value);

        if (avctx->flags & AV_CODEC_FLAG_PASS1)
            warn = 1;
        else if(context_model > 0) {
            AVDictionaryEntry *t = av_dict_get(options, "non_deterministic",
                                               NULL, AV_DICT_MATCH_CASE);
            warn = !t || !t->value || !atoi(t->value) ? 1 : 0;
        }
        // huffyuv does not support these with multiple frame threads currently
        if (warn) {
            av_log(avctx, AV_LOG_WARNING,
               "Forcing thread count to 1 for huffyuv encoding with first pass or context 1\n");
            avctx->thread_count = 1;
        }
    }

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

    c->task_fifo = av_fifo_alloc_array(BUFFER_SIZE, sizeof(Task));
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
    av_fifo_freep(&c->task_fifo);
    av_freep(&avctx->internal->frame_thread_encoder);
}

int ff_thread_video_encode_frame(AVCodecContext *avctx, AVPacket *pkt, const AVFrame *frame, int *got_packet_ptr){
    ThreadContext *c = avctx->internal->frame_thread_encoder;
    Task task;
    int ret;

    av_assert1(!*got_packet_ptr);

    if(frame){
        AVFrame *new = av_frame_alloc();
        if(!new)
            return AVERROR(ENOMEM);
        ret = av_frame_ref(new, frame);
        if(ret < 0) {
            av_frame_free(&new);
            return ret;
        }

        task.index = c->task_index;
        task.indata = (void*)new;
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
