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
#include "codec_internal.h"
#include "hwconfig.h"
#include "internal.h"
#include "pthread_internal.h"
#include "thread.h"
#include "threadframe.h"
#include "version_major.h"

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

enum {
    UNINITIALIZED,  ///< Thread has not been created, AVCodec->close mustn't be called
    NEEDS_CLOSE,    ///< FFCodec->close needs to be called
    INITIALIZED,    ///< Thread has been properly set up
};

/**
 * Context used by codec threads and stored in their AVCodecInternal thread_ctx.
 */
typedef struct PerThreadContext {
    struct FrameThreadContext *parent;

    pthread_t      thread;
    int            thread_init;
    unsigned       pthread_init_cnt;///< Number of successfully initialized mutexes/conditions
    pthread_cond_t input_cond;      ///< Used to wait for a new packet from the main thread.
    pthread_cond_t progress_cond;   ///< Used by child threads to wait for progress to change.
    pthread_cond_t output_cond;     ///< Used by the main thread to wait for frames to finish.

    pthread_mutex_t mutex;          ///< Mutex used to protect the contents of the PerThreadContext.
    pthread_mutex_t progress_mutex; ///< Mutex used to protect frame progress values and progress_cond.

    AVCodecContext *avctx;          ///< Context used to decode packets passed to this thread.

    AVPacket       *avpkt;          ///< Input packet (for decoding) or output (for encoding).

    AVFrame *frame;                 ///< Output frame (for decoding) or input (for encoding).
    int     got_frame;              ///< The output of got_picture_ptr from the last avcodec_decode_video() call.
    int     result;                 ///< The result of the last codec decode/encode() call.

    atomic_int state;

#if FF_API_THREAD_SAFE_CALLBACKS
    /**
     * Array of frames passed to ff_thread_release_buffer().
     * Frames are released after all threads referencing them are finished.
     */
    AVFrame **released_buffers;
    int   num_released_buffers;
    int       released_buffers_allocated;

    AVFrame *requested_frame;       ///< AVFrame the codec passed to get_buffer()
    int      requested_flags;       ///< flags passed to get_buffer() for requested_frame

    const enum AVPixelFormat *available_formats; ///< Format array for get_format()
    enum AVPixelFormat result_format;            ///< get_format() result
#endif

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

    unsigned    pthread_init_cnt;  ///< Number of successfully initialized mutexes/conditions
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

#if FF_API_THREAD_SAFE_CALLBACKS
#define THREAD_SAFE_CALLBACKS(avctx) \
((avctx)->thread_safe_callbacks || (avctx)->get_buffer2 == avcodec_default_get_buffer2)
#endif

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
    const FFCodec *codec = ffcodec(avctx->codec);

    pthread_mutex_lock(&p->mutex);
    while (1) {
        while (atomic_load(&p->state) == STATE_INPUT_READY && !p->die)
            pthread_cond_wait(&p->input_cond, &p->mutex);

        if (p->die) break;

FF_DISABLE_DEPRECATION_WARNINGS
        if (!codec->update_thread_context
#if FF_API_THREAD_SAFE_CALLBACKS
            && THREAD_SAFE_CALLBACKS(avctx)
#endif
            )
            ff_thread_finish_setup(avctx);
FF_ENABLE_DEPRECATION_WARNINGS

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
        p->result = codec->cb.decode(avctx, p->frame, &p->got_frame, p->avpkt);

        if ((p->result < 0 || !p->got_frame) && p->frame->buf[0])
            ff_thread_release_buffer(avctx, p->frame);

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
    const FFCodec *const codec = ffcodec(dst->codec);
    int err = 0;

    if (dst != src && (for_user || codec->update_thread_context)) {
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
        dst->properties   = src->properties;

        dst->bits_per_coded_sample = src->bits_per_coded_sample;
        dst->sample_aspect_ratio   = src->sample_aspect_ratio;

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

        dst->sample_rate    = src->sample_rate;
        dst->sample_fmt     = src->sample_fmt;
#if FF_API_OLD_CHANNEL_LAYOUT
FF_DISABLE_DEPRECATION_WARNINGS
        dst->channels       = src->channels;
        dst->channel_layout = src->channel_layout;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        err = av_channel_layout_copy(&dst->ch_layout, &src->ch_layout);
        if (err < 0)
            return err;

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

        err = av_buffer_replace(&dst->internal->pool, src->internal->pool);
        if (err < 0)
            return err;
    }

    if (for_user) {
        if (codec->update_thread_context_for_user)
            err = codec->update_thread_context_for_user(dst, src);
    } else {
        if (codec->update_thread_context)
            err = codec->update_thread_context(dst, src);
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
    dst->flags          = src->flags;

    dst->draw_horiz_band= src->draw_horiz_band;
    dst->get_buffer2    = src->get_buffer2;

    dst->opaque   = src->opaque;
    dst->debug    = src->debug;

    dst->slice_flags = src->slice_flags;
    dst->flags2      = src->flags2;
    dst->export_side_data = src->export_side_data;

    dst->skip_loop_filter = src->skip_loop_filter;
    dst->skip_idct        = src->skip_idct;
    dst->skip_frame       = src->skip_frame;

    dst->frame_number     = src->frame_number;
    dst->reordered_opaque = src->reordered_opaque;
#if FF_API_THREAD_SAFE_CALLBACKS
FF_DISABLE_DEPRECATION_WARNINGS
    dst->thread_safe_callbacks = src->thread_safe_callbacks;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

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
}

#if FF_API_THREAD_SAFE_CALLBACKS
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
        f = p->released_buffers[--p->num_released_buffers];
        f->extended_data = f->data;
        av_frame_unref(f);

        pthread_mutex_unlock(&fctx->buffer_mutex);
    }
}
#endif

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

#if FF_API_THREAD_SAFE_CALLBACKS
    release_delayed_buffers(p);
#endif

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

    av_packet_unref(p->avpkt);
    ret = av_packet_ref(p->avpkt, avpkt);
    if (ret < 0) {
        pthread_mutex_unlock(&p->mutex);
        av_log(p->avctx, AV_LOG_ERROR, "av_packet_ref() failed in submit_packet()\n");
        return ret;
    }

    atomic_store(&p->state, STATE_SETTING_UP);
    pthread_cond_signal(&p->input_cond);
    pthread_mutex_unlock(&p->mutex);

#if FF_API_THREAD_SAFE_CALLBACKS
FF_DISABLE_DEPRECATION_WARNINGS
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
FF_ENABLE_DEPRECATION_WARNINGS
#endif

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
        picture->pkt_dts = p->avpkt->dts;
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

#define OFF(member) offsetof(FrameThreadContext, member)
DEFINE_OFFSET_ARRAY(FrameThreadContext, thread_ctx, pthread_init_cnt,
                    (OFF(buffer_mutex), OFF(hwaccel_mutex), OFF(async_mutex)),
                    (OFF(async_cond)));
#undef OFF

#define OFF(member) offsetof(PerThreadContext, member)
DEFINE_OFFSET_ARRAY(PerThreadContext, per_thread, pthread_init_cnt,
                    (OFF(progress_mutex), OFF(mutex)),
                    (OFF(input_cond), OFF(progress_cond), OFF(output_cond)));
#undef OFF

void ff_frame_thread_free(AVCodecContext *avctx, int thread_count)
{
    FrameThreadContext *fctx = avctx->internal->thread_ctx;
    const FFCodec *codec = ffcodec(avctx->codec);
    int i;

    park_frame_worker_threads(fctx, thread_count);

    if (fctx->prev_thread && avctx->internal->hwaccel_priv_data !=
                             fctx->prev_thread->avctx->internal->hwaccel_priv_data) {
        if (update_context_from_thread(avctx, fctx->prev_thread->avctx, 1) < 0) {
            av_log(avctx, AV_LOG_ERROR, "Failed to update user thread.\n");
        }
    }

    for (i = 0; i < thread_count; i++) {
        PerThreadContext *p = &fctx->threads[i];
        AVCodecContext *ctx = p->avctx;

        if (ctx->internal) {
            if (p->thread_init == INITIALIZED) {
                pthread_mutex_lock(&p->mutex);
                p->die = 1;
                pthread_cond_signal(&p->input_cond);
                pthread_mutex_unlock(&p->mutex);

                pthread_join(p->thread, NULL);
            }
            if (codec->close && p->thread_init != UNINITIALIZED)
                codec->close(ctx);

#if FF_API_THREAD_SAFE_CALLBACKS
            release_delayed_buffers(p);
            for (int j = 0; j < p->released_buffers_allocated; j++)
                av_frame_free(&p->released_buffers[j]);
            av_freep(&p->released_buffers);
#endif
            if (ctx->priv_data) {
                if (codec->p.priv_class)
                    av_opt_free(ctx->priv_data);
                av_freep(&ctx->priv_data);
            }

            av_freep(&ctx->slice_offset);

            av_buffer_unref(&ctx->internal->pool);
            av_freep(&ctx->internal);
            av_buffer_unref(&ctx->hw_frames_ctx);
        }

        av_frame_free(&p->frame);

        ff_pthread_free(p, per_thread_offsets);
        av_packet_free(&p->avpkt);

        av_freep(&p->avctx);
    }

    av_freep(&fctx->threads);
    ff_pthread_free(fctx, thread_ctx_offsets);

    av_freep(&avctx->internal->thread_ctx);
}

static av_cold int init_thread(PerThreadContext *p, int *threads_to_free,
                               FrameThreadContext *fctx, AVCodecContext *avctx,
                               const FFCodec *codec, int first)
{
    AVCodecContext *copy;
    int err;

    atomic_init(&p->state, STATE_INPUT_READY);

    copy = av_memdup(avctx, sizeof(*avctx));
    if (!copy)
        return AVERROR(ENOMEM);
    copy->priv_data = NULL;

    /* From now on, this PerThreadContext will be cleaned up by
     * ff_frame_thread_free in case of errors. */
    (*threads_to_free)++;

    p->parent = fctx;
    p->avctx  = copy;

    copy->internal = av_mallocz(sizeof(*copy->internal));
    if (!copy->internal)
        return AVERROR(ENOMEM);
    copy->internal->thread_ctx = p;

    copy->delay = avctx->delay;

    if (codec->priv_data_size) {
        copy->priv_data = av_mallocz(codec->priv_data_size);
        if (!copy->priv_data)
            return AVERROR(ENOMEM);

        if (codec->p.priv_class) {
            *(const AVClass **)copy->priv_data = codec->p.priv_class;
            err = av_opt_copy(copy->priv_data, avctx->priv_data);
            if (err < 0)
                return err;
        }
    }

    err = ff_pthread_init(p, per_thread_offsets);
    if (err < 0)
        return err;

    if (!(p->frame = av_frame_alloc()) ||
        !(p->avpkt = av_packet_alloc()))
        return AVERROR(ENOMEM);
    copy->internal->last_pkt_props = p->avpkt;

    if (!first)
        copy->internal->is_copy = 1;

    if (codec->init) {
        err = codec->init(copy);
        if (err < 0) {
            if (codec->caps_internal & FF_CODEC_CAP_INIT_CLEANUP)
                p->thread_init = NEEDS_CLOSE;
            return err;
        }
    }
    p->thread_init = NEEDS_CLOSE;

    if (first)
        update_context_from_thread(avctx, copy, 1);

    atomic_init(&p->debug_threads, (copy->debug & FF_DEBUG_THREADS) != 0);

    err = AVERROR(pthread_create(&p->thread, NULL, frame_worker_thread, p));
    if (err < 0)
        return err;
    p->thread_init = INITIALIZED;

    return 0;
}

int ff_frame_thread_init(AVCodecContext *avctx)
{
    int thread_count = avctx->thread_count;
    const FFCodec *codec = ffcodec(avctx->codec);
    FrameThreadContext *fctx;
    int err, i = 0;

    if (!thread_count) {
        int nb_cpus = av_cpu_count();
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

    err = ff_pthread_init(fctx, thread_ctx_offsets);
    if (err < 0) {
        ff_pthread_free(fctx, thread_ctx_offsets);
        av_freep(&avctx->internal->thread_ctx);
        return err;
    }

    fctx->async_lock = 1;
    fctx->delaying = 1;

    if (codec->p.type == AVMEDIA_TYPE_VIDEO)
        avctx->delay = avctx->thread_count - 1;

    fctx->threads = av_calloc(thread_count, sizeof(*fctx->threads));
    if (!fctx->threads) {
        err = AVERROR(ENOMEM);
        goto error;
    }

    for (; i < thread_count; ) {
        PerThreadContext *p  = &fctx->threads[i];
        int first = !i;

        err = init_thread(p, &i, fctx, avctx, codec, first);
        if (err < 0)
            goto error;
    }

    return 0;

error:
    ff_frame_thread_free(avctx, i);
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

#if FF_API_THREAD_SAFE_CALLBACKS
        release_delayed_buffers(p);
#endif

        if (ffcodec(avctx->codec)->flush)
            ffcodec(avctx->codec)->flush(p->avctx);
    }
}

int ff_thread_can_start_frame(AVCodecContext *avctx)
{
    PerThreadContext *p = avctx->internal->thread_ctx;
FF_DISABLE_DEPRECATION_WARNINGS
    if ((avctx->active_thread_type&FF_THREAD_FRAME) && atomic_load(&p->state) != STATE_SETTING_UP &&
        (ffcodec(avctx->codec)->update_thread_context
#if FF_API_THREAD_SAFE_CALLBACKS
         || !THREAD_SAFE_CALLBACKS(avctx)
#endif
         )) {
        return 0;
    }
FF_ENABLE_DEPRECATION_WARNINGS
    return 1;
}

static int thread_get_buffer_internal(AVCodecContext *avctx, AVFrame *f, int flags)
{
    PerThreadContext *p;
    int err;

    if (!(avctx->active_thread_type & FF_THREAD_FRAME))
        return ff_get_buffer(avctx, f, flags);

    p = avctx->internal->thread_ctx;
FF_DISABLE_DEPRECATION_WARNINGS
    if (atomic_load(&p->state) != STATE_SETTING_UP &&
        (ffcodec(avctx->codec)->update_thread_context
#if FF_API_THREAD_SAFE_CALLBACKS
         || !THREAD_SAFE_CALLBACKS(avctx)
#endif
         )) {
FF_ENABLE_DEPRECATION_WARNINGS
        av_log(avctx, AV_LOG_ERROR, "get_buffer() cannot be called after ff_thread_finish_setup()\n");
        return -1;
    }

    pthread_mutex_lock(&p->parent->buffer_mutex);
#if !FF_API_THREAD_SAFE_CALLBACKS
    err = ff_get_buffer(avctx, f->f, flags);
#else
FF_DISABLE_DEPRECATION_WARNINGS
    if (THREAD_SAFE_CALLBACKS(avctx)) {
        err = ff_get_buffer(avctx, f, flags);
    } else {
        pthread_mutex_lock(&p->progress_mutex);
        p->requested_frame = f;
        p->requested_flags = flags;
        atomic_store_explicit(&p->state, STATE_GET_BUFFER, memory_order_release);
        pthread_cond_broadcast(&p->progress_cond);

        while (atomic_load(&p->state) != STATE_SETTING_UP)
            pthread_cond_wait(&p->progress_cond, &p->progress_mutex);

        err = p->result;

        pthread_mutex_unlock(&p->progress_mutex);

    }
    if (!THREAD_SAFE_CALLBACKS(avctx) && !ffcodec(avctx->codec)->update_thread_context)
        ff_thread_finish_setup(avctx);
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    pthread_mutex_unlock(&p->parent->buffer_mutex);

    return err;
}

#if FF_API_THREAD_SAFE_CALLBACKS
FF_DISABLE_DEPRECATION_WARNINGS
enum AVPixelFormat ff_thread_get_format(AVCodecContext *avctx, const enum AVPixelFormat *fmt)
{
    enum AVPixelFormat res;
    PerThreadContext *p;
    if (!(avctx->active_thread_type & FF_THREAD_FRAME) || avctx->thread_safe_callbacks ||
        avctx->get_format == avcodec_default_get_format)
        return ff_get_format(avctx, fmt);

    p = avctx->internal->thread_ctx;
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
FF_ENABLE_DEPRECATION_WARNINGS
#endif

int ff_thread_get_buffer(AVCodecContext *avctx, AVFrame *f, int flags)
{
    int ret = thread_get_buffer_internal(avctx, f, flags);
    if (ret < 0)
        av_log(avctx, AV_LOG_ERROR, "thread_get_buffer() failed\n");
    return ret;
}

int ff_thread_get_ext_buffer(AVCodecContext *avctx, ThreadFrame *f, int flags)
{
    int ret;

    f->owner[0] = f->owner[1] = avctx;
    /* Hint: It is possible for this function to be called with codecs
     * that don't support frame threading at all, namely in case
     * a frame-threaded decoder shares code with codecs that are not.
     * This currently affects non-MPEG-4 mpegvideo codecs and and VP7.
     * The following check will always be true for them. */
    if (!(avctx->active_thread_type & FF_THREAD_FRAME))
        return ff_get_buffer(avctx, f->f, flags);

    if (ffcodec(avctx->codec)->caps_internal & FF_CODEC_CAP_ALLOCATE_PROGRESS) {
        atomic_int *progress;
        f->progress = av_buffer_alloc(2 * sizeof(*progress));
        if (!f->progress) {
            return AVERROR(ENOMEM);
        }
        progress = (atomic_int*)f->progress->data;

        atomic_init(&progress[0], -1);
        atomic_init(&progress[1], -1);
    }

    ret = ff_thread_get_buffer(avctx, f->f, flags);
    if (ret)
        av_buffer_unref(&f->progress);
    return ret;
}

void ff_thread_release_buffer(AVCodecContext *avctx, AVFrame *f)
{
#if FF_API_THREAD_SAFE_CALLBACKS
FF_DISABLE_DEPRECATION_WARNINGS
    PerThreadContext *p;
    FrameThreadContext *fctx;
    AVFrame *dst;
    int ret = 0;
    int can_direct_free = !(avctx->active_thread_type & FF_THREAD_FRAME) ||
                          THREAD_SAFE_CALLBACKS(avctx);
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    if (!f)
        return;

    if (avctx->debug & FF_DEBUG_BUFFERS)
        av_log(avctx, AV_LOG_DEBUG, "thread_release_buffer called on pic %p\n", f);

#if !FF_API_THREAD_SAFE_CALLBACKS
    av_frame_unref(f->f);
#else
    // when the frame buffers are not allocated, just reset it to clean state
    if (can_direct_free || !f->buf[0]) {
        av_frame_unref(f);
        return;
    }

    p    = avctx->internal->thread_ctx;
    fctx = p->parent;
    pthread_mutex_lock(&fctx->buffer_mutex);

    if (p->num_released_buffers == p->released_buffers_allocated) {
        AVFrame **tmp = av_realloc_array(p->released_buffers, p->released_buffers_allocated + 1,
                                         sizeof(*p->released_buffers));
        if (tmp) {
            tmp[p->released_buffers_allocated] = av_frame_alloc();
            p->released_buffers = tmp;
        }

        if (!tmp || !tmp[p->released_buffers_allocated]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        p->released_buffers_allocated++;
    }

    dst = p->released_buffers[p->num_released_buffers];
    av_frame_move_ref(dst, f);

    p->num_released_buffers++;

fail:
    pthread_mutex_unlock(&fctx->buffer_mutex);

    // make sure the frame is clean even if we fail to free it
    // this leaks, but it is better than crashing
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Could not queue a frame for freeing, this will leak\n");
        memset(f->buf, 0, sizeof(f->buf));
        if (f->extended_buf)
            memset(f->extended_buf, 0, f->nb_extended_buf * sizeof(*f->extended_buf));
        av_frame_unref(f);
    }
#endif
}

void ff_thread_release_ext_buffer(AVCodecContext *avctx, ThreadFrame *f)
{
    av_buffer_unref(&f->progress);
    f->owner[0] = f->owner[1] = NULL;
    ff_thread_release_buffer(avctx, f->f);
}
