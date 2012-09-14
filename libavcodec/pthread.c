/*
 * Copyright (c) 2004 Roman Shaposhnik
 * Copyright (c) 2008 Alexander Strange (astrange@ithinksw.com)
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
 */

/**
 * @file
 * Multithreading support functions
 * @see doc/multithreading.txt
 */

#include "config.h"

#if HAVE_SCHED_GETAFFINITY
#define _GNU_SOURCE
#include <sched.h>
#endif
#if HAVE_GETPROCESSAFFINITYMASK
#include <windows.h>
#endif
#if HAVE_SYSCTL
#if HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#endif
#if HAVE_SYSCONF
#include <unistd.h>
#endif

#include "avcodec.h"
#include "internal.h"
#include "thread.h"

#if HAVE_PTHREADS
#include <pthread.h>
#elif HAVE_W32THREADS
#include "w32pthreads.h"
#elif HAVE_OS2THREADS
#include "os2threads.h"
#endif

typedef int (action_func)(AVCodecContext *c, void *arg);
typedef int (action_func2)(AVCodecContext *c, void *arg, int jobnr, int threadnr);

typedef struct ThreadContext {
    pthread_t *workers;
    action_func *func;
    action_func2 *func2;
    void *args;
    int *rets;
    int rets_count;
    int job_count;
    int job_size;

    pthread_cond_t last_job_cond;
    pthread_cond_t current_job_cond;
    pthread_mutex_t current_job_lock;
    int current_job;
    unsigned int current_execute;
    int done;
} ThreadContext;

/// Max number of frame buffers that can be allocated when using frame threads.
#define MAX_BUFFERS (32+1)

/**
 * Context used by codec threads and stored in their AVCodecContext thread_opaque.
 */
typedef struct PerThreadContext {
    struct FrameThreadContext *parent;

    pthread_t      thread;
    int            thread_init;
    pthread_cond_t input_cond;      ///< Used to wait for a new packet from the main thread.
    pthread_cond_t progress_cond;   ///< Used by child threads to wait for progress to change.
    pthread_cond_t output_cond;     ///< Used by the main thread to wait for frames to finish.

    pthread_mutex_t mutex;          ///< Mutex used to protect the contents of the PerThreadContext.
    pthread_mutex_t progress_mutex; ///< Mutex used to protect frame progress values and progress_cond.

    AVCodecContext *avctx;          ///< Context used to decode packets passed to this thread.

    AVPacket       avpkt;           ///< Input packet (for decoding) or output (for encoding).
    int            allocated_buf_size; ///< Size allocated for avpkt.data

    AVFrame frame;                  ///< Output frame (for decoding) or input (for encoding).
    int     got_frame;              ///< The output of got_picture_ptr from the last avcodec_decode_video() call.
    int     result;                 ///< The result of the last codec decode/encode() call.

    enum {
        STATE_INPUT_READY,          ///< Set when the thread is awaiting a packet.
        STATE_SETTING_UP,           ///< Set before the codec has called ff_thread_finish_setup().
        STATE_GET_BUFFER,           /**<
                                     * Set when the codec calls get_buffer().
                                     * State is returned to STATE_SETTING_UP afterwards.
                                     */
        STATE_SETUP_FINISHED        ///< Set after the codec has called ff_thread_finish_setup().
    } state;

    /**
     * Array of frames passed to ff_thread_release_buffer().
     * Frames are released after all threads referencing them are finished.
     */
    AVFrame released_buffers[MAX_BUFFERS];
    int     num_released_buffers;

    /**
     * Array of progress values used by ff_thread_get_buffer().
     */
    int     progress[MAX_BUFFERS][2];
    uint8_t progress_used[MAX_BUFFERS];

    AVFrame *requested_frame;       ///< AVFrame the codec passed to get_buffer()
} PerThreadContext;

/**
 * Context stored in the client AVCodecContext thread_opaque.
 */
typedef struct FrameThreadContext {
    PerThreadContext *threads;     ///< The contexts for each thread.
    PerThreadContext *prev_thread; ///< The last thread submit_packet() was called on.

    pthread_mutex_t buffer_mutex;  ///< Mutex used to protect get/release_buffer().

    int next_decoding;             ///< The next context to submit a packet to.
    int next_finished;             ///< The next context to return output from.

    int delaying;                  /**<
                                    * Set for the first N packets, where N is the number of threads.
                                    * While it is set, ff_thread_en/decode_frame won't return any results.
                                    */

    int die;                       ///< Set when threads should exit.
} FrameThreadContext;


/* H264 slice threading seems to be buggy with more than 16 threads,
 * limit the number of threads to 16 for automatic detection */
#define MAX_AUTO_THREADS 16

static int get_logical_cpus(AVCodecContext *avctx)
{
    int ret, nb_cpus = 1;
#if HAVE_SCHED_GETAFFINITY && defined(CPU_COUNT)
    cpu_set_t cpuset;

    CPU_ZERO(&cpuset);

    ret = sched_getaffinity(0, sizeof(cpuset), &cpuset);
    if (!ret) {
        nb_cpus = CPU_COUNT(&cpuset);
    }
#elif HAVE_GETPROCESSAFFINITYMASK
    DWORD_PTR proc_aff, sys_aff;
    ret = GetProcessAffinityMask(GetCurrentProcess(), &proc_aff, &sys_aff);
    if (ret)
        nb_cpus = av_popcount64(proc_aff);
#elif HAVE_SYSCTL && defined(HW_NCPU)
    int mib[2] = { CTL_HW, HW_NCPU };
    size_t len = sizeof(nb_cpus);

    ret = sysctl(mib, 2, &nb_cpus, &len, NULL, 0);
    if (ret == -1)
        nb_cpus = 0;
#elif HAVE_SYSCONF && defined(_SC_NPROC_ONLN)
    nb_cpus = sysconf(_SC_NPROC_ONLN);
#elif HAVE_SYSCONF && defined(_SC_NPROCESSORS_ONLN)
    nb_cpus = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    av_log(avctx, AV_LOG_DEBUG, "detected %d logical cores\n", nb_cpus);

    if  (avctx->height)
        nb_cpus = FFMIN(nb_cpus, (avctx->height+15)/16);

    return nb_cpus;
}


static void* attribute_align_arg worker(void *v)
{
    AVCodecContext *avctx = v;
    ThreadContext *c = avctx->thread_opaque;
    int our_job = c->job_count;
    int last_execute = 0;
    int thread_count = avctx->thread_count;
    int self_id;

    pthread_mutex_lock(&c->current_job_lock);
    self_id = c->current_job++;
    for (;;){
        while (our_job >= c->job_count) {
            if (c->current_job == thread_count + c->job_count)
                pthread_cond_signal(&c->last_job_cond);

            while (last_execute == c->current_execute && !c->done)
                pthread_cond_wait(&c->current_job_cond, &c->current_job_lock);
            last_execute = c->current_execute;
            our_job = self_id;

            if (c->done) {
                pthread_mutex_unlock(&c->current_job_lock);
                return NULL;
            }
        }
        pthread_mutex_unlock(&c->current_job_lock);

        c->rets[our_job%c->rets_count] = c->func ? c->func(avctx, (char*)c->args + our_job*c->job_size):
                                                   c->func2(avctx, c->args, our_job, self_id);

        pthread_mutex_lock(&c->current_job_lock);
        our_job = c->current_job++;
    }
}

static av_always_inline void avcodec_thread_park_workers(ThreadContext *c, int thread_count)
{
    while (c->current_job != thread_count + c->job_count)
        pthread_cond_wait(&c->last_job_cond, &c->current_job_lock);
    pthread_mutex_unlock(&c->current_job_lock);
}

static void thread_free(AVCodecContext *avctx)
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
    av_freep(&avctx->thread_opaque);
}

static int avcodec_thread_execute(AVCodecContext *avctx, action_func* func, void *arg, int *ret, int job_count, int job_size)
{
    ThreadContext *c= avctx->thread_opaque;
    int dummy_ret;

    if (!(avctx->active_thread_type&FF_THREAD_SLICE) || avctx->thread_count <= 1)
        return avcodec_default_execute(avctx, func, arg, ret, job_count, job_size);

    if (job_count <= 0)
        return 0;

    pthread_mutex_lock(&c->current_job_lock);

    c->current_job = avctx->thread_count;
    c->job_count = job_count;
    c->job_size = job_size;
    c->args = arg;
    c->func = func;
    if (ret) {
        c->rets = ret;
        c->rets_count = job_count;
    } else {
        c->rets = &dummy_ret;
        c->rets_count = 1;
    }
    c->current_execute++;
    pthread_cond_broadcast(&c->current_job_cond);

    avcodec_thread_park_workers(c, avctx->thread_count);

    return 0;
}

static int avcodec_thread_execute2(AVCodecContext *avctx, action_func2* func2, void *arg, int *ret, int job_count)
{
    ThreadContext *c= avctx->thread_opaque;
    c->func2 = func2;
    return avcodec_thread_execute(avctx, NULL, arg, ret, job_count, 0);
}

static int thread_init(AVCodecContext *avctx)
{
    int i;
    ThreadContext *c;
    int thread_count = avctx->thread_count;

    if (!thread_count) {
        int nb_cpus = get_logical_cpus(avctx);
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

    c = av_mallocz(sizeof(ThreadContext));
    if (!c)
        return -1;

    c->workers = av_mallocz(sizeof(pthread_t)*thread_count);
    if (!c->workers) {
        av_free(c);
        return -1;
    }

    avctx->thread_opaque = c;
    c->current_job = 0;
    c->job_count = 0;
    c->job_size = 0;
    c->done = 0;
    pthread_cond_init(&c->current_job_cond, NULL);
    pthread_cond_init(&c->last_job_cond, NULL);
    pthread_mutex_init(&c->current_job_lock, NULL);
    pthread_mutex_lock(&c->current_job_lock);
    for (i=0; i<thread_count; i++) {
        if(pthread_create(&c->workers[i], NULL, worker, avctx)) {
           avctx->thread_count = i;
           pthread_mutex_unlock(&c->current_job_lock);
           ff_thread_free(avctx);
           return -1;
        }
    }

    avcodec_thread_park_workers(c, thread_count);

    avctx->execute = avcodec_thread_execute;
    avctx->execute2 = avcodec_thread_execute2;
    return 0;
}

/**
 * Codec worker thread.
 *
 * Automatically calls ff_thread_finish_setup() if the codec does
 * not provide an update_thread_context method, or if the codec returns
 * before calling it.
 */
static attribute_align_arg void *frame_worker_thread(void *arg)
{
    PerThreadContext *p = arg;
    FrameThreadContext *fctx = p->parent;
    AVCodecContext *avctx = p->avctx;
    AVCodec *codec = avctx->codec;

    while (1) {
        int i;
        if (p->state == STATE_INPUT_READY && !fctx->die) {
            pthread_mutex_lock(&p->mutex);
            while (p->state == STATE_INPUT_READY && !fctx->die)
                pthread_cond_wait(&p->input_cond, &p->mutex);
            pthread_mutex_unlock(&p->mutex);
        }

        if (fctx->die) break;

        if (!codec->update_thread_context && (avctx->thread_safe_callbacks || avctx->get_buffer == avcodec_default_get_buffer))
            ff_thread_finish_setup(avctx);

        pthread_mutex_lock(&p->mutex);
        avcodec_get_frame_defaults(&p->frame);
        p->got_frame = 0;
        p->result = codec->decode(avctx, &p->frame, &p->got_frame, &p->avpkt);

        if (p->state == STATE_SETTING_UP) ff_thread_finish_setup(avctx);

        p->state = STATE_INPUT_READY;

        pthread_mutex_lock(&p->progress_mutex);
        for (i = 0; i < MAX_BUFFERS; i++)
            if (p->progress_used[i] && (p->got_frame || p->result<0 || avctx->codec_id != CODEC_ID_H264)) {
                p->progress[i][0] = INT_MAX;
                p->progress[i][1] = INT_MAX;
            }
        pthread_cond_broadcast(&p->progress_cond);
        pthread_cond_signal(&p->output_cond);
        pthread_mutex_unlock(&p->progress_mutex);

        pthread_mutex_unlock(&p->mutex);
    }

    return NULL;
}

/**
 * Update the next thread's AVCodecContext with values from the reference thread's context.
 *
 * @param dst The destination context.
 * @param src The source context.
 * @param for_user 0 if the destination is a codec thread, 1 if the destination is the user's thread
 */
static int update_context_from_thread(AVCodecContext *dst, AVCodecContext *src, int for_user)
{
    int err = 0;

    if (dst != src) {
        dst->sub_id    = src->sub_id;
        dst->time_base = src->time_base;
        dst->width     = src->width;
        dst->height    = src->height;
        dst->pix_fmt   = src->pix_fmt;

        dst->coded_width  = src->coded_width;
        dst->coded_height = src->coded_height;

        dst->has_b_frames = src->has_b_frames;
        dst->idct_algo    = src->idct_algo;

        dst->bits_per_coded_sample = src->bits_per_coded_sample;
        dst->sample_aspect_ratio   = src->sample_aspect_ratio;
        dst->dtg_active_format     = src->dtg_active_format;

        dst->profile = src->profile;
        dst->level   = src->level;

        dst->bits_per_raw_sample = src->bits_per_raw_sample;
        dst->ticks_per_frame     = src->ticks_per_frame;
        dst->color_primaries     = src->color_primaries;

        dst->color_trc   = src->color_trc;
        dst->colorspace  = src->colorspace;
        dst->color_range = src->color_range;
        dst->chroma_sample_location = src->chroma_sample_location;
    }

    if (for_user) {
        dst->delay       = src->thread_count - 1;
        dst->coded_frame = src->coded_frame;
    } else {
        if (dst->codec->update_thread_context)
            err = dst->codec->update_thread_context(dst, src);
    }

    return err;
}

/**
 * Update the next thread's AVCodecContext with values set by the user.
 *
 * @param dst The destination context.
 * @param src The source context.
 * @return 0 on success, negative error code on failure
 */
static int update_context_from_user(AVCodecContext *dst, AVCodecContext *src)
{
#define copy_fields(s, e) memcpy(&dst->s, &src->s, (char*)&dst->e - (char*)&dst->s);
    dst->flags          = src->flags;

    dst->draw_horiz_band= src->draw_horiz_band;
    dst->get_buffer     = src->get_buffer;
    dst->release_buffer = src->release_buffer;

    dst->opaque   = src->opaque;
    dst->dsp_mask = src->dsp_mask;
    dst->debug    = src->debug;
    dst->debug_mv = src->debug_mv;

    dst->slice_flags = src->slice_flags;
    dst->flags2      = src->flags2;

    copy_fields(skip_loop_filter, bidir_refine);

    dst->frame_number     = src->frame_number;
    dst->reordered_opaque = src->reordered_opaque;
    dst->thread_safe_callbacks = src->thread_safe_callbacks;

    if (src->slice_count && src->slice_offset) {
        if (dst->slice_count < src->slice_count) {
            int *tmp = av_realloc(dst->slice_offset, src->slice_count *
                                  sizeof(*dst->slice_offset));
            if (!tmp) {
                av_free(dst->slice_offset);
                return AVERROR(ENOMEM);
            }
            dst->slice_offset = tmp;
        }
        memcpy(dst->slice_offset, src->slice_offset,
               src->slice_count * sizeof(*dst->slice_offset));
    }
    dst->slice_count = src->slice_count;
    return 0;
#undef copy_fields
}

static void free_progress(AVFrame *f)
{
    PerThreadContext *p = f->owner->thread_opaque;
    int *progress = f->thread_opaque;

    p->progress_used[(progress - p->progress[0]) / 2] = 0;
}

/// Releases the buffers that this decoding thread was the last user of.
static void release_delayed_buffers(PerThreadContext *p)
{
    FrameThreadContext *fctx = p->parent;

    while (p->num_released_buffers > 0) {
        AVFrame *f;

        pthread_mutex_lock(&fctx->buffer_mutex);
        f = &p->released_buffers[--p->num_released_buffers];
        free_progress(f);
        f->thread_opaque = NULL;

        f->owner->release_buffer(f->owner, f);
        pthread_mutex_unlock(&fctx->buffer_mutex);
    }
}

static int submit_packet(PerThreadContext *p, AVPacket *avpkt)
{
    FrameThreadContext *fctx = p->parent;
    PerThreadContext *prev_thread = fctx->prev_thread;
    AVCodec *codec = p->avctx->codec;
    uint8_t *buf = p->avpkt.data;

    if (!avpkt->size && !(codec->capabilities & CODEC_CAP_DELAY)) return 0;

    pthread_mutex_lock(&p->mutex);

    release_delayed_buffers(p);

    if (prev_thread) {
        int err;
        if (prev_thread->state == STATE_SETTING_UP) {
            pthread_mutex_lock(&prev_thread->progress_mutex);
            while (prev_thread->state == STATE_SETTING_UP)
                pthread_cond_wait(&prev_thread->progress_cond, &prev_thread->progress_mutex);
            pthread_mutex_unlock(&prev_thread->progress_mutex);
        }

        err = update_context_from_thread(p->avctx, prev_thread->avctx, 0);
        if (err) {
            pthread_mutex_unlock(&p->mutex);
            return err;
        }
    }

    av_fast_malloc(&buf, &p->allocated_buf_size, avpkt->size + FF_INPUT_BUFFER_PADDING_SIZE);
    p->avpkt = *avpkt;
    p->avpkt.data = buf;
    memcpy(buf, avpkt->data, avpkt->size);
    memset(buf + avpkt->size, 0, FF_INPUT_BUFFER_PADDING_SIZE);

    p->state = STATE_SETTING_UP;
    pthread_cond_signal(&p->input_cond);
    pthread_mutex_unlock(&p->mutex);

    /*
     * If the client doesn't have a thread-safe get_buffer(),
     * then decoding threads call back to the main thread,
     * and it calls back to the client here.
     */

    if (!p->avctx->thread_safe_callbacks &&
         p->avctx->get_buffer != avcodec_default_get_buffer) {
        while (p->state != STATE_SETUP_FINISHED && p->state != STATE_INPUT_READY) {
            pthread_mutex_lock(&p->progress_mutex);
            while (p->state == STATE_SETTING_UP)
                pthread_cond_wait(&p->progress_cond, &p->progress_mutex);

            if (p->state == STATE_GET_BUFFER) {
                p->result = p->avctx->get_buffer(p->avctx, p->requested_frame);
                p->state  = STATE_SETTING_UP;
                pthread_cond_signal(&p->progress_cond);
            }
            pthread_mutex_unlock(&p->progress_mutex);
        }
    }

    fctx->prev_thread = p;
    fctx->next_decoding++;

    return 0;
}

int ff_thread_decode_frame(AVCodecContext *avctx,
                           AVFrame *picture, int *got_picture_ptr,
                           AVPacket *avpkt)
{
    FrameThreadContext *fctx = avctx->thread_opaque;
    int finished = fctx->next_finished;
    PerThreadContext *p;
    int err;

    /*
     * Submit a packet to the next decoding thread.
     */

    p = &fctx->threads[fctx->next_decoding];
    err = update_context_from_user(p->avctx, avctx);
    if (err) return err;
    err = submit_packet(p, avpkt);
    if (err) return err;

    /*
     * If we're still receiving the initial packets, don't return a frame.
     */

    if (fctx->delaying && avpkt->size) {
        if (fctx->next_decoding >= (avctx->thread_count-1)) fctx->delaying = 0;

        *got_picture_ptr=0;
        return avpkt->size;
    }

    /*
     * Return the next available frame from the oldest thread.
     * If we're at the end of the stream, then we have to skip threads that
     * didn't output a frame, because we don't want to accidentally signal
     * EOF (avpkt->size == 0 && *got_picture_ptr == 0).
     */

    do {
        p = &fctx->threads[finished++];

        if (p->state != STATE_INPUT_READY) {
            pthread_mutex_lock(&p->progress_mutex);
            while (p->state != STATE_INPUT_READY)
                pthread_cond_wait(&p->output_cond, &p->progress_mutex);
            pthread_mutex_unlock(&p->progress_mutex);
        }

        *picture = p->frame;
        *got_picture_ptr = p->got_frame;
        picture->pkt_dts = p->avpkt.dts;
        picture->sample_aspect_ratio = avctx->sample_aspect_ratio;
        picture->width  = avctx->width;
        picture->height = avctx->height;
        picture->format = avctx->pix_fmt;

        /*
         * A later call with avkpt->size == 0 may loop over all threads,
         * including this one, searching for a frame to return before being
         * stopped by the "finished != fctx->next_finished" condition.
         * Make sure we don't mistakenly return the same frame again.
         */
        p->got_frame = 0;

        if (finished >= avctx->thread_count) finished = 0;
    } while (!avpkt->size && !*got_picture_ptr && finished != fctx->next_finished);

    update_context_from_thread(avctx, p->avctx, 1);

    if (fctx->next_decoding >= avctx->thread_count) fctx->next_decoding = 0;

    fctx->next_finished = finished;

    /* return the size of the consumed packet if no error occurred */
    return (p->result >= 0) ? avpkt->size : p->result;
}

void ff_thread_report_progress(AVFrame *f, int n, int field)
{
    PerThreadContext *p;
    int *progress = f->thread_opaque;

    if (!progress || progress[field] >= n) return;

    p = f->owner->thread_opaque;

    if (f->owner->debug&FF_DEBUG_THREADS)
        av_log(f->owner, AV_LOG_DEBUG, "%p finished %d field %d\n", progress, n, field);

    pthread_mutex_lock(&p->progress_mutex);
    progress[field] = n;
    pthread_cond_broadcast(&p->progress_cond);
    pthread_mutex_unlock(&p->progress_mutex);
}

void ff_thread_await_progress(AVFrame *f, int n, int field)
{
    PerThreadContext *p;
    int *progress = f->thread_opaque;

    if (!progress || progress[field] >= n) return;

    p = f->owner->thread_opaque;

    if (f->owner->debug&FF_DEBUG_THREADS)
        av_log(f->owner, AV_LOG_DEBUG, "thread awaiting %d field %d from %p\n", n, field, progress);

    pthread_mutex_lock(&p->progress_mutex);
    while (progress[field] < n)
        pthread_cond_wait(&p->progress_cond, &p->progress_mutex);
    pthread_mutex_unlock(&p->progress_mutex);
}

void ff_thread_finish_setup(AVCodecContext *avctx) {
    PerThreadContext *p = avctx->thread_opaque;

    if (!(avctx->active_thread_type&FF_THREAD_FRAME)) return;

    if(p->state == STATE_SETUP_FINISHED){
        av_log(avctx, AV_LOG_WARNING, "Multiple ff_thread_finish_setup() calls\n");
    }

    pthread_mutex_lock(&p->progress_mutex);
    p->state = STATE_SETUP_FINISHED;
    pthread_cond_broadcast(&p->progress_cond);
    pthread_mutex_unlock(&p->progress_mutex);
}

/// Waits for all threads to finish.
static void park_frame_worker_threads(FrameThreadContext *fctx, int thread_count)
{
    int i;

    for (i = 0; i < thread_count; i++) {
        PerThreadContext *p = &fctx->threads[i];

        if (p->state != STATE_INPUT_READY) {
            pthread_mutex_lock(&p->progress_mutex);
            while (p->state != STATE_INPUT_READY)
                pthread_cond_wait(&p->output_cond, &p->progress_mutex);
            pthread_mutex_unlock(&p->progress_mutex);
        }
        p->got_frame = 0;
    }
}

static void frame_thread_free(AVCodecContext *avctx, int thread_count)
{
    FrameThreadContext *fctx = avctx->thread_opaque;
    AVCodec *codec = avctx->codec;
    int i;

    park_frame_worker_threads(fctx, thread_count);

    if (fctx->prev_thread && fctx->prev_thread != fctx->threads)
        update_context_from_thread(fctx->threads->avctx, fctx->prev_thread->avctx, 0);

    fctx->die = 1;

    for (i = 0; i < thread_count; i++) {
        PerThreadContext *p = &fctx->threads[i];

        pthread_mutex_lock(&p->mutex);
        pthread_cond_signal(&p->input_cond);
        pthread_mutex_unlock(&p->mutex);

        if (p->thread_init)
            pthread_join(p->thread, NULL);
        p->thread_init=0;

        if (codec->close)
            codec->close(p->avctx);

        avctx->codec = NULL;

        release_delayed_buffers(p);
    }

    for (i = 0; i < thread_count; i++) {
        PerThreadContext *p = &fctx->threads[i];

        avcodec_default_free_buffers(p->avctx);

        pthread_mutex_destroy(&p->mutex);
        pthread_mutex_destroy(&p->progress_mutex);
        pthread_cond_destroy(&p->input_cond);
        pthread_cond_destroy(&p->progress_cond);
        pthread_cond_destroy(&p->output_cond);
        av_freep(&p->avpkt.data);

        if (i) {
            av_freep(&p->avctx->priv_data);
            av_freep(&p->avctx->internal);
            av_freep(&p->avctx->slice_offset);
        }

        av_freep(&p->avctx);
    }

    av_freep(&fctx->threads);
    pthread_mutex_destroy(&fctx->buffer_mutex);
    av_freep(&avctx->thread_opaque);
}

static int frame_thread_init(AVCodecContext *avctx)
{
    int thread_count = avctx->thread_count;
    AVCodec *codec = avctx->codec;
    AVCodecContext *src = avctx;
    FrameThreadContext *fctx;
    int i, err = 0;

    if (!thread_count) {
        int nb_cpus = get_logical_cpus(avctx);
        if ((avctx->debug & (FF_DEBUG_VIS_QP | FF_DEBUG_VIS_MB_TYPE)) || avctx->debug_mv)
            nb_cpus = 1;
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

    avctx->thread_opaque = fctx = av_mallocz(sizeof(FrameThreadContext));

    fctx->threads = av_mallocz(sizeof(PerThreadContext) * thread_count);
    pthread_mutex_init(&fctx->buffer_mutex, NULL);
    fctx->delaying = 1;

    for (i = 0; i < thread_count; i++) {
        AVCodecContext *copy = av_malloc(sizeof(AVCodecContext));
        PerThreadContext *p  = &fctx->threads[i];

        pthread_mutex_init(&p->mutex, NULL);
        pthread_mutex_init(&p->progress_mutex, NULL);
        pthread_cond_init(&p->input_cond, NULL);
        pthread_cond_init(&p->progress_cond, NULL);
        pthread_cond_init(&p->output_cond, NULL);

        p->parent = fctx;
        p->avctx  = copy;

        if (!copy) {
            err = AVERROR(ENOMEM);
            goto error;
        }

        *copy = *src;
        copy->thread_opaque = p;
        copy->pkt = &p->avpkt;

        if (!i) {
            src = copy;

            if (codec->init)
                err = codec->init(copy);

            update_context_from_thread(avctx, copy, 1);
        } else {
            copy->priv_data = av_malloc(codec->priv_data_size);
            if (!copy->priv_data) {
                err = AVERROR(ENOMEM);
                goto error;
            }
            memcpy(copy->priv_data, src->priv_data, codec->priv_data_size);
            copy->internal = av_malloc(sizeof(AVCodecInternal));
            if (!copy->internal) {
                err = AVERROR(ENOMEM);
                goto error;
            }
            *copy->internal = *src->internal;
            copy->internal->is_copy = 1;

            if (codec->init_thread_copy)
                err = codec->init_thread_copy(copy);
        }

        if (err) goto error;

        p->thread_init= !pthread_create(&p->thread, NULL, frame_worker_thread, p);
        if(!p->thread_init)
            goto error;
    }

    return 0;

error:
    frame_thread_free(avctx, i+1);

    return err;
}

void ff_thread_flush(AVCodecContext *avctx)
{
    FrameThreadContext *fctx = avctx->thread_opaque;

    if (!avctx->thread_opaque) return;

    park_frame_worker_threads(fctx, avctx->thread_count);
    if (fctx->prev_thread) {
        if (fctx->prev_thread != &fctx->threads[0])
            update_context_from_thread(fctx->threads[0].avctx, fctx->prev_thread->avctx, 0);
        if (avctx->codec->flush)
            avctx->codec->flush(fctx->threads[0].avctx);
    }

    fctx->next_decoding = fctx->next_finished = 0;
    fctx->delaying = 1;
    fctx->prev_thread = NULL;
}

static int *allocate_progress(PerThreadContext *p)
{
    int i;

    for (i = 0; i < MAX_BUFFERS; i++)
        if (!p->progress_used[i]) break;

    if (i == MAX_BUFFERS) {
        av_log(p->avctx, AV_LOG_ERROR, "allocate_progress() overflow\n");
        return NULL;
    }

    p->progress_used[i] = 1;

    return p->progress[i];
}

int ff_thread_get_buffer(AVCodecContext *avctx, AVFrame *f)
{
    PerThreadContext *p = avctx->thread_opaque;
    int *progress, err;

    f->owner = avctx;

    ff_init_buffer_info(avctx, f);

    if (!(avctx->active_thread_type&FF_THREAD_FRAME)) {
        f->thread_opaque = NULL;
        return avctx->get_buffer(avctx, f);
    }

    if (p->state != STATE_SETTING_UP &&
        (avctx->codec->update_thread_context || (!avctx->thread_safe_callbacks &&
                avctx->get_buffer != avcodec_default_get_buffer))) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() cannot be called after ff_thread_finish_setup()\n");
        return -1;
    }

    pthread_mutex_lock(&p->parent->buffer_mutex);
    f->thread_opaque = progress = allocate_progress(p);

    if (!progress) {
        pthread_mutex_unlock(&p->parent->buffer_mutex);
        return -1;
    }

    progress[0] =
    progress[1] = -1;

    if (avctx->thread_safe_callbacks ||
        avctx->get_buffer == avcodec_default_get_buffer) {
        err = avctx->get_buffer(avctx, f);
    } else {
        p->requested_frame = f;
        p->state = STATE_GET_BUFFER;
        pthread_mutex_lock(&p->progress_mutex);
        pthread_cond_broadcast(&p->progress_cond);

        while (p->state != STATE_SETTING_UP)
            pthread_cond_wait(&p->progress_cond, &p->progress_mutex);

        err = p->result;

        pthread_mutex_unlock(&p->progress_mutex);

        if (!avctx->codec->update_thread_context)
            ff_thread_finish_setup(avctx);
    }

    pthread_mutex_unlock(&p->parent->buffer_mutex);

    return err;
}

void ff_thread_release_buffer(AVCodecContext *avctx, AVFrame *f)
{
    PerThreadContext *p = avctx->thread_opaque;
    FrameThreadContext *fctx;

    if (!(avctx->active_thread_type&FF_THREAD_FRAME)) {
        avctx->release_buffer(avctx, f);
        return;
    }

    if (p->num_released_buffers >= MAX_BUFFERS) {
        av_log(p->avctx, AV_LOG_ERROR, "too many thread_release_buffer calls!\n");
        return;
    }

    if(avctx->debug & FF_DEBUG_BUFFERS)
        av_log(avctx, AV_LOG_DEBUG, "thread_release_buffer called on pic %p\n", f);

    fctx = p->parent;
    pthread_mutex_lock(&fctx->buffer_mutex);
    p->released_buffers[p->num_released_buffers++] = *f;
    pthread_mutex_unlock(&fctx->buffer_mutex);
    memset(f->data, 0, sizeof(f->data));
}

/**
 * Set the threading algorithms used.
 *
 * Threading requires more than one thread.
 * Frame threading requires entire frames to be passed to the codec,
 * and introduces extra decoding delay, so is incompatible with low_delay.
 *
 * @param avctx The context.
 */
static void validate_thread_parameters(AVCodecContext *avctx)
{
    int frame_threading_supported = (avctx->codec->capabilities & CODEC_CAP_FRAME_THREADS)
                                && !(avctx->flags & CODEC_FLAG_TRUNCATED)
                                && !(avctx->flags & CODEC_FLAG_LOW_DELAY)
                                && !(avctx->flags2 & CODEC_FLAG2_CHUNKS);
    if (avctx->thread_count == 1) {
        avctx->active_thread_type = 0;
    } else if (frame_threading_supported && (avctx->thread_type & FF_THREAD_FRAME)) {
        avctx->active_thread_type = FF_THREAD_FRAME;
    } else if (avctx->codec->capabilities & CODEC_CAP_SLICE_THREADS &&
               avctx->thread_type & FF_THREAD_SLICE) {
        avctx->active_thread_type = FF_THREAD_SLICE;
    } else if (!(avctx->codec->capabilities & CODEC_CAP_AUTO_THREADS)) {
        avctx->thread_count       = 1;
        avctx->active_thread_type = 0;
    }
}

int ff_thread_init(AVCodecContext *avctx)
{
    if (avctx->thread_opaque) {
        av_log(avctx, AV_LOG_ERROR, "avcodec_thread_init is ignored after avcodec_open\n");
        return -1;
    }

#if HAVE_W32THREADS
    w32thread_init();
#endif

    if (avctx->codec) {
        validate_thread_parameters(avctx);

        if (avctx->active_thread_type&FF_THREAD_SLICE)
            return thread_init(avctx);
        else if (avctx->active_thread_type&FF_THREAD_FRAME)
            return frame_thread_init(avctx);
    }

    return 0;
}

void ff_thread_free(AVCodecContext *avctx)
{
    if (avctx->active_thread_type&FF_THREAD_FRAME)
        frame_thread_free(avctx, avctx->thread_count);
    else
        thread_free(avctx);
}
