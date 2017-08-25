/*
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
 * Frame multithreading support functions
 * @see doc/multithreading.txt
 */

#include "config.h"

#include <stdatomic.h>
#include <stdint.h>

#include "avcodec.h"
#include "hwaccel.h"
#include "internal.h"
#include "pthread_internal.h"
#include "thread.h"
#include "version.h"

#include "libavutil/avassert.h"
#include "libavutil/buffer.h"
#include "libavutil/common.h"
#include "libavutil/cpu.h"
#include "libavutil/frame.h"
#include "libavutil/internal.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/thread.h"

enum {
    ///< Set when the thread is awaiting a packet.
    STATE_INPUT_READY,
    ///< Set before the codec has called ff_thread_finish_setup().
    STATE_SETTING_UP,
    /**
     * Set when the codec calls get_buffer().
     * State is returned to STATE_SETTING_UP afterwards.
     */
    STATE_GET_BUFFER,
     /**
      * Set when the codec calls get_format().
      * State is returned to STATE_SETTING_UP afterwards.
      */
    STATE_GET_FORMAT,
    ///< Set after the codec has called ff_thread_finish_setup().
    STATE_SETUP_FINISHED,
};

/**
 * Context used by codec threads and stored in their AVCodecInternal thread_ctx.
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

    AVFrame *frame;                 ///< Output frame (for decoding) or input (for encoding).
    int     got_frame;              ///< The output of got_picture_ptr from the last avcodec_decode_video() call.
    int     result;                 ///< The result of the last codec decode/encode() call.

    atomic_int state;

    /**
     * Array of frames passed to ff_thread_release_buffer().
     * Frames are released after all threads referencing them are finished.
     */
    AVFrame *released_buffers;
    int  num_released_buffers;
    int      released_buffers_allocated;

    AVFrame *requested_frame;       ///< AVFrame the codec passed to get_buffer()
    int      requested_flags;       ///< flags passed to get_buffer() for requested_frame

    const enum AVPixelFormat *available_formats; ///< Format array for get_format()
    enum AVPixelFormat result_format;            ///< get_format() result

    int die;                        ///< Set when the thread should exit.

    int hwaccel_serializing;
    int async_serializing;

    atomic_int debug_threads;       ///< Set if the FF_DEBUG_THREADS option is set.
} PerThreadContext;

/**
 * Context stored in the client AVCodecInternal thread_ctx.
 */
typedef struct FrameThreadContext {
    PerThreadContext *threads;     ///< The contexts for each thread.
    PerThreadContext *prev_thread; ///< The last thread submit_packet() was called on.

    pthread_mutex_t buffer_mutex;  ///< Mutex used to protect get/release_buffer().
    /**
     * This lock is used for ensuring threads run in serial when hwaccel
     * is used.
     */
    pthread_mutex_t hwaccel_mutex;
    pthread_mutex_t async_mutex;
    pthread_cond_t async_cond;
    int async_lock;

    int next_decoding;             ///< The next context to submit a packet to.
    int next_finished;             ///< The next context to return output from.

    int delaying;                  /**<
                                    * Set for the first N packets, where N is the number of threads.
                                    * While it is set, ff_thread_en/decode_frame won't return any results.
                                    */
} FrameThreadContext;

#define THREAD_SAFE_CALLBACKS(avctx) \
((avctx)->thread_safe_callbacks || (avctx)->get_buffer2 == avcodec_default_get_buffer2)

static void async_lock(FrameThreadContext *fctx)
{
    pthread_mutex_lock(&fctx->async_mutex);
    while (fctx->async_lock)
        pthread_cond_wait(&fctx->async_cond, &fctx->async_mutex);
    fctx->async_lock = 1;
    pthread_mutex_unlock(&fctx->async_mutex);
}

static void async_unlock(FrameThreadContext *fctx)
{
    pthread_mutex_lock(&fctx->async_mutex);
    av_assert0(fctx->async_lock);
    fctx->async_lock = 0;
    pthread_cond_broadcast(&fctx->async_cond);
    pthread_mutex_unlock(&fctx->async_mutex);
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
    AVCodecContext *avctx = p->avctx;
    const AVCodec *codec = avctx->codec;

    pthread_mutex_lock(&p->mutex);
    while (1) {
        while (atomic_load(&p->state) == STATE_INPUT_READY && !p->die)
            pthread_cond_wait(&p->input_cond, &p->mutex);

        if (p->die) break;

        if (!codec->update_thread_context && THREAD_SAFE_CALLBACKS(avctx))
            ff_thread_finish_setup(avctx);

        /* If a decoder supports hwaccel, then it must call ff_get_format().
         * Since that call must happen before ff_thread_finish_setup(), the
         * decoder is required to implement update_thread_context() and call
         * ff_thread_finish_setup() manually. Therefore the above
         * ff_thread_finish_setup() call did not happen and hwaccel_serializing
         * cannot be true here. */
        av_assert0(!p->hwaccel_serializing);

        /* if the previous thread uses hwaccel then we take the lock to ensure
         * the threads don't run concurrently */
        if (avctx->hwaccel) {
            pthread_mutex_lock(&p->parent->hwaccel_mutex);
            p->hwaccel_serializing = 1;
        }

        av_frame_unref(p->frame);
        p->got_frame = 0;
        p->result = codec->decode(avctx, p->frame, &p->got_frame, &p->avpkt);

        if ((p->result < 0 || !p->got_frame) && p->frame->buf[0]) {
            if (avctx->internal->allocate_progress)
                av_log(avctx, AV_LOG_ERROR, "A frame threaded decoder did not "
                       "free the frame on failure. This is a bug, please report it.\n");
            av_frame_unref(p->frame);
        }

        if (atomic_load(&p->state) == STATE_SETTING_UP)
            ff_thread_finish_setup(avctx);

        if (p->hwaccel_serializing) {
            p->hwaccel_serializing = 0;
            pthread_mutex_unlock(&p->parent->hwaccel_mutex);
        }

        if (p->async_serializing) {
            p->async_serializing = 0;

            async_unlock(p->parent);
        }

        pthread_mutex_lock(&p->progress_mutex);

        atomic_store(&p->state, STATE_INPUT_READY);

        pthread_cond_broadcast(&p->progress_cond);
        pthread_cond_signal(&p->output_cond);
        pthread_mutex_unlock(&p->progress_mutex);
    }
    pthread_mutex_unlock(&p->mutex);

    return NULL;
}

/**
 * Update the next thread's AVCodecContext with values from the reference thread's context.
 *
 * @param dst The destination context.
 * @param src The source context.
 * @param for_user 0 if the destination is a codec thread, 1 if the destination is the user's thread
 * @return 0 on success, negative error code on failure
 */
static int update_context_from_thread(AVCodecContext *dst, AVCodecContext *src, int for_user)
{
    int err = 0;

    if (dst != src && (for_user || !(av_codec_get_codec_descriptor(src)->props & AV_CODEC_PROP_INTRA_ONLY))) {
        dst->time_base = src->time_base;
        dst->framerate = src->framerate;
        dst->width     = src->width;
        dst->height    = src->height;
        dst->pix_fmt   = src->pix_fmt;
        dst->sw_pix_fmt = src->sw_pix_fmt;

        dst->coded_width  = src->coded_width;
        dst->coded_height = src->coded_height;

        dst->has_b_frames = src->has_b_frames;
        dst->idct_algo    = src->idct_algo;

        dst->bits_per_coded_sample = src->bits_per_coded_sample;
        dst->sample_aspect_ratio   = src->sample_aspect_ratio;
#if FF_API_AFD
FF_DISABLE_DEPRECATION_WARNINGS
        dst->dtg_active_format     = src->dtg_active_format;
FF_ENABLE_DEPRECATION_WARNINGS
#endif /* FF_API_AFD */

        dst->profile = src->profile;
        dst->level   = src->level;

        dst->bits_per_raw_sample = src->bits_per_raw_sample;
        dst->ticks_per_frame     = src->ticks_per_frame;
        dst->color_primaries     = src->color_primaries;

        dst->color_trc   = src->color_trc;
        dst->colorspace  = src->colorspace;
        dst->color_range = src->color_range;
        dst->chroma_sample_location = src->chroma_sample_location;

        dst->hwaccel = src->hwaccel;
        dst->hwaccel_context = src->hwaccel_context;

        dst->channels       = src->channels;
        dst->sample_rate    = src->sample_rate;
        dst->sample_fmt     = src->sample_fmt;
        dst->channel_layout = src->channel_layout;
        dst->internal->hwaccel_priv_data = src->internal->hwaccel_priv_data;

        if (!!dst->hw_frames_ctx != !!src->hw_frames_ctx ||
            (dst->hw_frames_ctx && dst->hw_frames_ctx->data != src->hw_frames_ctx->data)) {
            av_buffer_unref(&dst->hw_frames_ctx);

            if (src->hw_frames_ctx) {
                dst->hw_frames_ctx = av_buffer_ref(src->hw_frames_ctx);
                if (!dst->hw_frames_ctx)
                    return AVERROR(ENOMEM);
            }
        }

        dst->hwaccel_flags = src->hwaccel_flags;
    }

    if (for_user) {
        dst->delay       = src->thread_count - 1;
#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
        dst->coded_frame = src->coded_frame;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
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
    dst->get_buffer2    = src->get_buffer2;

    dst->opaque   = src->opaque;
    dst->debug    = src->debug;
    dst->debug_mv = src->debug_mv;

    dst->slice_flags = src->slice_flags;
    dst->flags2      = src->flags2;

    copy_fields(skip_loop_filter, subtitle_header);

    dst->frame_number     = src->frame_number;
    dst->reordered_opaque = src->reordered_opaque;
    dst->thread_safe_callbacks = src->thread_safe_callbacks;

    if (src->slice_count && src->slice_offset) {
        if (dst->slice_count < src->slice_count) {
            int err = av_reallocp_array(&dst->slice_offset, src->slice_count,
                                        sizeof(*dst->slice_offset));
            if (err < 0)
                return err;
        }
        memcpy(dst->slice_offset, src->slice_offset,
               src->slice_count * sizeof(*dst->slice_offset));
    }
    dst->slice_count = src->slice_count;
    return 0;
#undef copy_fields
}

/// Releases the buffers that this decoding thread was the last user of.
static void release_delayed_buffers(PerThreadContext *p)
{
    FrameThreadContext *fctx = p->parent;

    while (p->num_released_buffers > 0) {
        AVFrame *f;

        pthread_mutex_lock(&fctx->buffer_mutex);

        // fix extended data in case the caller screwed it up
        av_assert0(p->avctx->codec_type == AVMEDIA_TYPE_VIDEO ||
                   p->avctx->codec_type == AVMEDIA_TYPE_AUDIO);
        f = &p->released_buffers[--p->num_released_buffers];
        f->extended_data = f->data;
        av_frame_unref(f);

        pthread_mutex_unlock(&fctx->buffer_mutex);
    }
}

static int submit_packet(PerThreadContext *p, AVCodecContext *user_avctx,
                         AVPacket *avpkt)
{
    FrameThreadContext *fctx = p->parent;
    PerThreadContext *prev_thread = fctx->prev_thread;
    const AVCodec *codec = p->avctx->codec;
    int ret;

    if (!avpkt->size && !(codec->capabilities & AV_CODEC_CAP_DELAY))
        return 0;

    pthread_mutex_lock(&p->mutex);

    ret = update_context_from_user(p->avctx, user_avctx);
    if (ret) {
        pthread_mutex_unlock(&p->mutex);
        return ret;
    }
    atomic_store_explicit(&p->debug_threads,
                          (p->avctx->debug & FF_DEBUG_THREADS) != 0,
                          memory_order_relaxed);

    release_delayed_buffers(p);

    if (prev_thread) {
        int err;
        if (atomic_load(&prev_thread->state) == STATE_SETTING_UP) {
            pthread_mutex_lock(&prev_thread->progress_mutex);
            while (atomic_load(&prev_thread->state) == STATE_SETTING_UP)
                pthread_cond_wait(&prev_thread->progress_cond, &prev_thread->progress_mutex);
            pthread_mutex_unlock(&prev_thread->progress_mutex);
        }

        err = update_context_from_thread(p->avctx, prev_thread->avctx, 0);
        if (err) {
            pthread_mutex_unlock(&p->mutex);
            return err;
        }
    }

    av_packet_unref(&p->avpkt);
    ret = av_packet_ref(&p->avpkt, avpkt);
    if (ret < 0) {
        pthread_mutex_unlock(&p->mutex);
        av_log(p->avctx, AV_LOG_ERROR, "av_packet_ref() failed in submit_packet()\n");
        return ret;
    }

    atomic_store(&p->state, STATE_SETTING_UP);
    pthread_cond_signal(&p->input_cond);
    pthread_mutex_unlock(&p->mutex);

    /*
     * If the client doesn't have a thread-safe get_buffer(),
     * then decoding threads call back to the main thread,
     * and it calls back to the client here.
     */

    if (!p->avctx->thread_safe_callbacks && (
         p->avctx->get_format != avcodec_default_get_format ||
         p->avctx->get_buffer2 != avcodec_default_get_buffer2)) {
        while (atomic_load(&p->state) != STATE_SETUP_FINISHED && atomic_load(&p->state) != STATE_INPUT_READY) {
            int call_done = 1;
            pthread_mutex_lock(&p->progress_mutex);
            while (atomic_load(&p->state) == STATE_SETTING_UP)
                pthread_cond_wait(&p->progress_cond, &p->progress_mutex);

            switch (atomic_load_explicit(&p->state, memory_order_acquire)) {
            case STATE_GET_BUFFER:
                p->result = ff_get_buffer(p->avctx, p->requested_frame, p->requested_flags);
                break;
            case STATE_GET_FORMAT:
                p->result_format = ff_get_format(p->avctx, p->available_formats);
                break;
            default:
                call_done = 0;
                break;
            }
            if (call_done) {
                atomic_store(&p->state, STATE_SETTING_UP);
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
    FrameThreadContext *fctx = avctx->internal->thread_ctx;
    int finished = fctx->next_finished;
    PerThreadContext *p;
    int err;

    /* release the async lock, permitting blocked hwaccel threads to
     * go forward while we are in this function */
    async_unlock(fctx);

    /*
     * Submit a packet to the next decoding thread.
     */

    p = &fctx->threads[fctx->next_decoding];
    err = submit_packet(p, avctx, avpkt);
    if (err)
        goto finish;

    /*
     * If we're still receiving the initial packets, don't return a frame.
     */

    if (fctx->next_decoding > (avctx->thread_count-1-(avctx->codec_id == AV_CODEC_ID_FFV1)))
        fctx->delaying = 0;

    if (fctx->delaying) {
        *got_picture_ptr=0;
        if (avpkt->size) {
            err = avpkt->size;
            goto finish;
        }
    }

    /*
     * Return the next available frame from the oldest thread.
     * If we're at the end of the stream, then we have to skip threads that
     * didn't output a frame/error, because we don't want to accidentally signal
     * EOF (avpkt->size == 0 && *got_picture_ptr == 0 && err >= 0).
     */

    do {
        p = &fctx->threads[finished++];

        if (atomic_load(&p->state) != STATE_INPUT_READY) {
            pthread_mutex_lock(&p->progress_mutex);
            while (atomic_load_explicit(&p->state, memory_order_relaxed) != STATE_INPUT_READY)
                pthread_cond_wait(&p->output_cond, &p->progress_mutex);
            pthread_mutex_unlock(&p->progress_mutex);
        }

        av_frame_move_ref(picture, p->frame);
        *got_picture_ptr = p->got_frame;
        picture->pkt_dts = p->avpkt.dts;
        err = p->result;

        /*
         * A later call with avkpt->size == 0 may loop over all threads,
         * including this one, searching for a frame/error to return before being
         * stopped by the "finished != fctx->next_finished" condition.
         * Make sure we don't mistakenly return the same frame/error again.
         */
        p->got_frame = 0;
        p->result = 0;

        if (finished >= avctx->thread_count) finished = 0;
    } while (!avpkt->size && !*got_picture_ptr && err >= 0 && finished != fctx->next_finished);

    update_context_from_thread(avctx, p->avctx, 1);

    if (fctx->next_decoding >= avctx->thread_count) fctx->next_decoding = 0;

    fctx->next_finished = finished;

    /* return the size of the consumed packet if no error occurred */
    if (err >= 0)
        err = avpkt->size;
finish:
    async_lock(fctx);
    return err;
}

void ff_thread_report_progress(ThreadFrame *f, int n, int field)
{
    PerThreadContext *p;
    atomic_int *progress = f->progress ? (atomic_int*)f->progress->data : NULL;

    if (!progress ||
        atomic_load_explicit(&progress[field], memory_order_relaxed) >= n)
        return;

    p = f->owner[field]->internal->thread_ctx;

    if (atomic_load_explicit(&p->debug_threads, memory_order_relaxed))
        av_log(f->owner[field], AV_LOG_DEBUG,
               "%p finished %d field %d\n", progress, n, field);

    pthread_mutex_lock(&p->progress_mutex);

    atomic_store_explicit(&progress[field], n, memory_order_release);

    pthread_cond_broadcast(&p->progress_cond);
    pthread_mutex_unlock(&p->progress_mutex);
}

void ff_thread_await_progress(ThreadFrame *f, int n, int field)
{
    PerThreadContext *p;
    atomic_int *progress = f->progress ? (atomic_int*)f->progress->data : NULL;

    if (!progress ||
        atomic_load_explicit(&progress[field], memory_order_acquire) >= n)
        return;

    p = f->owner[field]->internal->thread_ctx;

    if (atomic_load_explicit(&p->debug_threads, memory_order_relaxed))
        av_log(f->owner[field], AV_LOG_DEBUG,
               "thread awaiting %d field %d from %p\n", n, field, progress);

    pthread_mutex_lock(&p->progress_mutex);
    while (atomic_load_explicit(&progress[field], memory_order_relaxed) < n)
        pthread_cond_wait(&p->progress_cond, &p->progress_mutex);
    pthread_mutex_unlock(&p->progress_mutex);
}

void ff_thread_finish_setup(AVCodecContext *avctx) {
    PerThreadContext *p = avctx->internal->thread_ctx;

    if (!(avctx->active_thread_type&FF_THREAD_FRAME)) return;

    if (avctx->hwaccel && !p->hwaccel_serializing) {
        pthread_mutex_lock(&p->parent->hwaccel_mutex);
        p->hwaccel_serializing = 1;
    }

    /* this assumes that no hwaccel calls happen before ff_thread_finish_setup() */
    if (avctx->hwaccel &&
        !(avctx->hwaccel->caps_internal & HWACCEL_CAP_ASYNC_SAFE)) {
        p->async_serializing = 1;

        async_lock(p->parent);
    }

    pthread_mutex_lock(&p->progress_mutex);
    if(atomic_load(&p->state) == STATE_SETUP_FINISHED){
        av_log(avctx, AV_LOG_WARNING, "Multiple ff_thread_finish_setup() calls\n");
    }

    atomic_store(&p->state, STATE_SETUP_FINISHED);

    pthread_cond_broadcast(&p->progress_cond);
    pthread_mutex_unlock(&p->progress_mutex);
}

/// Waits for all threads to finish.
static void park_frame_worker_threads(FrameThreadContext *fctx, int thread_count)
{
    int i;

    async_unlock(fctx);

    for (i = 0; i < thread_count; i++) {
        PerThreadContext *p = &fctx->threads[i];

        if (atomic_load(&p->state) != STATE_INPUT_READY) {
            pthread_mutex_lock(&p->progress_mutex);
            while (atomic_load(&p->state) != STATE_INPUT_READY)
                pthread_cond_wait(&p->output_cond, &p->progress_mutex);
            pthread_mutex_unlock(&p->progress_mutex);
        }
        p->got_frame = 0;
    }

    async_lock(fctx);
}

void ff_frame_thread_free(AVCodecContext *avctx, int thread_count)
{
    FrameThreadContext *fctx = avctx->internal->thread_ctx;
    const AVCodec *codec = avctx->codec;
    int i;

    park_frame_worker_threads(fctx, thread_count);

    if (fctx->prev_thread && fctx->prev_thread != fctx->threads)
        if (update_context_from_thread(fctx->threads->avctx, fctx->prev_thread->avctx, 0) < 0) {
            av_log(avctx, AV_LOG_ERROR, "Final thread update failed\n");
            fctx->prev_thread->avctx->internal->is_copy = fctx->threads->avctx->internal->is_copy;
            fctx->threads->avctx->internal->is_copy = 1;
        }

    for (i = 0; i < thread_count; i++) {
        PerThreadContext *p = &fctx->threads[i];

        pthread_mutex_lock(&p->mutex);
        p->die = 1;
        pthread_cond_signal(&p->input_cond);
        pthread_mutex_unlock(&p->mutex);

        if (p->thread_init)
            pthread_join(p->thread, NULL);
        p->thread_init=0;

        if (codec->close && p->avctx)
            codec->close(p->avctx);

        release_delayed_buffers(p);
        av_frame_free(&p->frame);
    }

    for (i = 0; i < thread_count; i++) {
        PerThreadContext *p = &fctx->threads[i];

        pthread_mutex_destroy(&p->mutex);
        pthread_mutex_destroy(&p->progress_mutex);
        pthread_cond_destroy(&p->input_cond);
        pthread_cond_destroy(&p->progress_cond);
        pthread_cond_destroy(&p->output_cond);
        av_packet_unref(&p->avpkt);
        av_freep(&p->released_buffers);

        if (i && p->avctx) {
            av_freep(&p->avctx->priv_data);
            av_freep(&p->avctx->slice_offset);
        }

        if (p->avctx) {
            av_freep(&p->avctx->internal);
            av_buffer_unref(&p->avctx->hw_frames_ctx);
        }

        av_freep(&p->avctx);
    }

    av_freep(&fctx->threads);
    pthread_mutex_destroy(&fctx->buffer_mutex);
    pthread_mutex_destroy(&fctx->hwaccel_mutex);
    pthread_mutex_destroy(&fctx->async_mutex);
    pthread_cond_destroy(&fctx->async_cond);

    av_freep(&avctx->internal->thread_ctx);

    if (avctx->priv_data && avctx->codec && avctx->codec->priv_class)
        av_opt_free(avctx->priv_data);
    avctx->codec = NULL;
}

int ff_frame_thread_init(AVCodecContext *avctx)
{
    int thread_count = avctx->thread_count;
    const AVCodec *codec = avctx->codec;
    AVCodecContext *src = avctx;
    FrameThreadContext *fctx;
    int i, err = 0;

#if HAVE_W32THREADS
    w32thread_init();
#endif

    if (!thread_count) {
        int nb_cpus = av_cpu_count();
#if FF_API_DEBUG_MV
        if ((avctx->debug & (FF_DEBUG_VIS_QP | FF_DEBUG_VIS_MB_TYPE)) || avctx->debug_mv)
            nb_cpus = 1;
#endif
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

    avctx->internal->thread_ctx = fctx = av_mallocz(sizeof(FrameThreadContext));
    if (!fctx)
        return AVERROR(ENOMEM);

    fctx->threads = av_mallocz_array(thread_count, sizeof(PerThreadContext));
    if (!fctx->threads) {
        av_freep(&avctx->internal->thread_ctx);
        return AVERROR(ENOMEM);
    }

    pthread_mutex_init(&fctx->buffer_mutex, NULL);
    pthread_mutex_init(&fctx->hwaccel_mutex, NULL);
    pthread_mutex_init(&fctx->async_mutex, NULL);
    pthread_cond_init(&fctx->async_cond, NULL);

    fctx->async_lock = 1;
    fctx->delaying = 1;

    for (i = 0; i < thread_count; i++) {
        AVCodecContext *copy = av_malloc(sizeof(AVCodecContext));
        PerThreadContext *p  = &fctx->threads[i];

        pthread_mutex_init(&p->mutex, NULL);
        pthread_mutex_init(&p->progress_mutex, NULL);
        pthread_cond_init(&p->input_cond, NULL);
        pthread_cond_init(&p->progress_cond, NULL);
        pthread_cond_init(&p->output_cond, NULL);

        p->frame = av_frame_alloc();
        if (!p->frame) {
            av_freep(&copy);
            err = AVERROR(ENOMEM);
            goto error;
        }

        p->parent = fctx;
        p->avctx  = copy;

        if (!copy) {
            err = AVERROR(ENOMEM);
            goto error;
        }

        *copy = *src;

        copy->internal = av_malloc(sizeof(AVCodecInternal));
        if (!copy->internal) {
            copy->priv_data = NULL;
            err = AVERROR(ENOMEM);
            goto error;
        }
        *copy->internal = *src->internal;
        copy->internal->thread_ctx = p;
        copy->internal->last_pkt_props = &p->avpkt;

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
            copy->internal->is_copy = 1;

            if (codec->init_thread_copy)
                err = codec->init_thread_copy(copy);
        }

        if (err) goto error;

        atomic_init(&p->debug_threads, (copy->debug & FF_DEBUG_THREADS) != 0);

        err = AVERROR(pthread_create(&p->thread, NULL, frame_worker_thread, p));
        p->thread_init= !err;
        if(!p->thread_init)
            goto error;
    }

    return 0;

error:
    ff_frame_thread_free(avctx, i+1);

    return err;
}

void ff_thread_flush(AVCodecContext *avctx)
{
    int i;
    FrameThreadContext *fctx = avctx->internal->thread_ctx;

    if (!fctx) return;

    park_frame_worker_threads(fctx, avctx->thread_count);
    if (fctx->prev_thread) {
        if (fctx->prev_thread != &fctx->threads[0])
            update_context_from_thread(fctx->threads[0].avctx, fctx->prev_thread->avctx, 0);
    }

    fctx->next_decoding = fctx->next_finished = 0;
    fctx->delaying = 1;
    fctx->prev_thread = NULL;
    for (i = 0; i < avctx->thread_count; i++) {
        PerThreadContext *p = &fctx->threads[i];
        // Make sure decode flush calls with size=0 won't return old frames
        p->got_frame = 0;
        av_frame_unref(p->frame);
        p->result = 0;

        release_delayed_buffers(p);

        if (avctx->codec->flush)
            avctx->codec->flush(p->avctx);
    }
}

int ff_thread_can_start_frame(AVCodecContext *avctx)
{
    PerThreadContext *p = avctx->internal->thread_ctx;
    if ((avctx->active_thread_type&FF_THREAD_FRAME) && atomic_load(&p->state) != STATE_SETTING_UP &&
        (avctx->codec->update_thread_context || !THREAD_SAFE_CALLBACKS(avctx))) {
        return 0;
    }
    return 1;
}

static int thread_get_buffer_internal(AVCodecContext *avctx, ThreadFrame *f, int flags)
{
    PerThreadContext *p = avctx->internal->thread_ctx;
    int err;

    f->owner[0] = f->owner[1] = avctx;

    ff_init_buffer_info(avctx, f->f);

    if (!(avctx->active_thread_type & FF_THREAD_FRAME))
        return ff_get_buffer(avctx, f->f, flags);

    if (atomic_load(&p->state) != STATE_SETTING_UP &&
        (avctx->codec->update_thread_context || !THREAD_SAFE_CALLBACKS(avctx))) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() cannot be called after ff_thread_finish_setup()\n");
        return -1;
    }

    if (avctx->internal->allocate_progress) {
        atomic_int *progress;
        f->progress = av_buffer_alloc(2 * sizeof(*progress));
        if (!f->progress) {
            return AVERROR(ENOMEM);
        }
        progress = (atomic_int*)f->progress->data;

        atomic_init(&progress[0], -1);
        atomic_init(&progress[1], -1);
    }

    pthread_mutex_lock(&p->parent->buffer_mutex);
    if (avctx->thread_safe_callbacks ||
        avctx->get_buffer2 == avcodec_default_get_buffer2) {
        err = ff_get_buffer(avctx, f->f, flags);
    } else {
        pthread_mutex_lock(&p->progress_mutex);
        p->requested_frame = f->f;
        p->requested_flags = flags;
        atomic_store_explicit(&p->state, STATE_GET_BUFFER, memory_order_release);
        pthread_cond_broadcast(&p->progress_cond);

        while (atomic_load(&p->state) != STATE_SETTING_UP)
            pthread_cond_wait(&p->progress_cond, &p->progress_mutex);

        err = p->result;

        pthread_mutex_unlock(&p->progress_mutex);

    }
    if (!THREAD_SAFE_CALLBACKS(avctx) && !avctx->codec->update_thread_context)
        ff_thread_finish_setup(avctx);
    if (err)
        av_buffer_unref(&f->progress);

    pthread_mutex_unlock(&p->parent->buffer_mutex);

    return err;
}

enum AVPixelFormat ff_thread_get_format(AVCodecContext *avctx, const enum AVPixelFormat *fmt)
{
    enum AVPixelFormat res;
    PerThreadContext *p = avctx->internal->thread_ctx;
    if (!(avctx->active_thread_type & FF_THREAD_FRAME) || avctx->thread_safe_callbacks ||
        avctx->get_format == avcodec_default_get_format)
        return ff_get_format(avctx, fmt);
    if (atomic_load(&p->state) != STATE_SETTING_UP) {
        av_log(avctx, AV_LOG_ERROR, "get_format() cannot be called after ff_thread_finish_setup()\n");
        return -1;
    }
    pthread_mutex_lock(&p->progress_mutex);
    p->available_formats = fmt;
    atomic_store(&p->state, STATE_GET_FORMAT);
    pthread_cond_broadcast(&p->progress_cond);

    while (atomic_load(&p->state) != STATE_SETTING_UP)
        pthread_cond_wait(&p->progress_cond, &p->progress_mutex);

    res = p->result_format;

    pthread_mutex_unlock(&p->progress_mutex);

    return res;
}

int ff_thread_get_buffer(AVCodecContext *avctx, ThreadFrame *f, int flags)
{
    int ret = thread_get_buffer_internal(avctx, f, flags);
    if (ret < 0)
        av_log(avctx, AV_LOG_ERROR, "thread_get_buffer() failed\n");
    return ret;
}

void ff_thread_release_buffer(AVCodecContext *avctx, ThreadFrame *f)
{
    PerThreadContext *p = avctx->internal->thread_ctx;
    FrameThreadContext *fctx;
    AVFrame *dst, *tmp;
    int can_direct_free = !(avctx->active_thread_type & FF_THREAD_FRAME) ||
                          avctx->thread_safe_callbacks                   ||
                          avctx->get_buffer2 == avcodec_default_get_buffer2;

    if (!f->f || !f->f->buf[0])
        return;

    if (avctx->debug & FF_DEBUG_BUFFERS)
        av_log(avctx, AV_LOG_DEBUG, "thread_release_buffer called on pic %p\n", f);

    av_buffer_unref(&f->progress);
    f->owner[0] = f->owner[1] = NULL;

    if (can_direct_free) {
        av_frame_unref(f->f);
        return;
    }

    fctx = p->parent;
    pthread_mutex_lock(&fctx->buffer_mutex);

    if (p->num_released_buffers + 1 >= INT_MAX / sizeof(*p->released_buffers))
        goto fail;
    tmp = av_fast_realloc(p->released_buffers, &p->released_buffers_allocated,
                          (p->num_released_buffers + 1) *
                          sizeof(*p->released_buffers));
    if (!tmp)
        goto fail;
    p->released_buffers = tmp;

    dst = &p->released_buffers[p->num_released_buffers];
    av_frame_move_ref(dst, f->f);

    p->num_released_buffers++;

fail:
    pthread_mutex_unlock(&fctx->buffer_mutex);
}
