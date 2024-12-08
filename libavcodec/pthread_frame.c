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

#include <stdatomic.h>

#include "avcodec.h"
#include "avcodec_internal.h"
#include "codec_desc.h"
#include "codec_internal.h"
#include "decode.h"
#include "hwaccel_internal.h"
#include "hwconfig.h"
#include "internal.h"
#include "packet_internal.h"
#include "pthread_internal.h"
#include "libavutil/refstruct.h"
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
    /// Set when the thread is awaiting a packet.
    STATE_INPUT_READY,
    /// Set before the codec has called ff_thread_finish_setup().
    STATE_SETTING_UP,
    /// Set after the codec has called ff_thread_finish_setup().
    STATE_SETUP_FINISHED,
};

enum {
    UNINITIALIZED,  ///< Thread has not been created, AVCodec->close mustn't be called
    NEEDS_CLOSE,    ///< FFCodec->close needs to be called
    INITIALIZED,    ///< Thread has been properly set up
};

typedef struct DecodedFrames {
    AVFrame  **f;
    size_t  nb_f;
    size_t  nb_f_allocated;
} DecodedFrames;

typedef struct ThreadFrameProgress {
    atomic_int progress[2];
} ThreadFrameProgress;

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

    /**
     * Decoded frames from a single decode iteration.
     */
    DecodedFrames df;
    int     result;                 ///< The result of the last codec decode/encode() call.

    atomic_int state;

    int die;                        ///< Set when the thread should exit.

    int hwaccel_serializing;
    int async_serializing;

    // set to 1 in ff_thread_finish_setup() when a threadsafe hwaccel is used;
    // cannot check hwaccel caps directly, because
    // worked threads clear hwaccel state for thread-unsafe hwaccels
    // after each decode call
    int hwaccel_threadsafe;

    atomic_int debug_threads;       ///< Set if the FF_DEBUG_THREADS option is set.

    /// The following two fields have the same semantics as the DecodeContext field
    int intra_only_flag;
    enum AVPictureType initial_pict_type;
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
     * This lock is used for ensuring threads run in serial when thread-unsafe
     * hwaccel is used.
     */
    pthread_mutex_t hwaccel_mutex;
    pthread_mutex_t async_mutex;
    pthread_cond_t async_cond;
    int async_lock;

    DecodedFrames df;
    int result;

    /**
     * Packet to be submitted to the next thread for decoding.
     */
    AVPacket *next_pkt;

    int next_decoding;             ///< The next context to submit a packet to.
    int next_finished;             ///< The next context to return output from.

    /* hwaccel state for thread-unsafe hwaccels is temporarily stored here in
     * order to transfer its ownership to the next decoding thread without the
     * need for extra synchronization */
    const AVHWAccel *stash_hwaccel;
    void            *stash_hwaccel_context;
    void            *stash_hwaccel_priv;
} FrameThreadContext;

static int hwaccel_serial(const AVCodecContext *avctx)
{
    return avctx->hwaccel && !(ffhwaccel(avctx->hwaccel)->caps_internal & HWACCEL_CAP_THREAD_SAFE);
}

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

static void thread_set_name(PerThreadContext *p)
{
    AVCodecContext *avctx = p->avctx;
    int idx = p - p->parent->threads;
    char name[16];

    snprintf(name, sizeof(name), "av:%.7s:df%d", avctx->codec->name, idx);

    ff_thread_setname(name);
}

// get a free frame to decode into
static AVFrame *decoded_frames_get_free(DecodedFrames *df)
{
    if (df->nb_f == df->nb_f_allocated) {
        AVFrame **tmp = av_realloc_array(df->f, df->nb_f + 1,
                                         sizeof(*df->f));
        if (!tmp)
            return NULL;
        df->f = tmp;

        df->f[df->nb_f] = av_frame_alloc();
        if (!df->f[df->nb_f])
            return NULL;

        df->nb_f_allocated++;
    }

    av_assert0(!df->f[df->nb_f]->buf[0]);

    return df->f[df->nb_f];
}

static void decoded_frames_pop(DecodedFrames *df, AVFrame *dst)
{
    AVFrame *tmp_frame = df->f[0];
    av_frame_move_ref(dst, tmp_frame);
    memmove(df->f, df->f + 1, (df->nb_f - 1) * sizeof(*df->f));
    df->f[--df->nb_f] = tmp_frame;
}

static void decoded_frames_flush(DecodedFrames *df)
{
    for (size_t i = 0; i < df->nb_f; i++)
        av_frame_unref(df->f[i]);
    df->nb_f = 0;
}

static void decoded_frames_free(DecodedFrames *df)
{
    for (size_t i = 0; i < df->nb_f_allocated; i++)
        av_frame_free(&df->f[i]);
    av_freep(&df->f);
    df->nb_f           = 0;
    df->nb_f_allocated = 0;
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

    thread_set_name(p);

    pthread_mutex_lock(&p->mutex);
    while (1) {
        int ret;

        while (atomic_load(&p->state) == STATE_INPUT_READY && !p->die)
            pthread_cond_wait(&p->input_cond, &p->mutex);

        if (p->die) break;

        if (!codec->update_thread_context)
            ff_thread_finish_setup(avctx);

        /* If a decoder supports hwaccel, then it must call ff_get_format().
         * Since that call must happen before ff_thread_finish_setup(), the
         * decoder is required to implement update_thread_context() and call
         * ff_thread_finish_setup() manually. Therefore the above
         * ff_thread_finish_setup() call did not happen and hwaccel_serializing
         * cannot be true here. */
        av_assert0(!p->hwaccel_serializing);

        /* if the previous thread uses thread-unsafe hwaccel then we take the
         * lock to ensure the threads don't run concurrently */
        if (hwaccel_serial(avctx)) {
            pthread_mutex_lock(&p->parent->hwaccel_mutex);
            p->hwaccel_serializing = 1;
        }

        ret = 0;
        while (ret >= 0) {
            AVFrame *frame;

            /* get the frame which will store the output */
            frame = decoded_frames_get_free(&p->df);
            if (!frame) {
                p->result = AVERROR(ENOMEM);
                goto alloc_fail;
            }

            /* do the actual decoding */
            ret = ff_decode_receive_frame_internal(avctx, frame);
            if (ret == 0)
                p->df.nb_f++;
            else if (ret < 0 && frame->buf[0])
                av_frame_unref(frame);

            p->result = (ret == AVERROR(EAGAIN)) ? 0 : ret;
        }

        if (atomic_load(&p->state) == STATE_SETTING_UP)
            ff_thread_finish_setup(avctx);

alloc_fail:
        if (p->hwaccel_serializing) {
            /* wipe hwaccel state for thread-unsafe hwaccels to avoid stale
             * pointers lying around;
             * the state was transferred to FrameThreadContext in
             * ff_thread_finish_setup(), so nothing is leaked */
            avctx->hwaccel                     = NULL;
            avctx->hwaccel_context             = NULL;
            avctx->internal->hwaccel_priv_data = NULL;

            p->hwaccel_serializing = 0;
            pthread_mutex_unlock(&p->parent->hwaccel_mutex);
        }
        av_assert0(!avctx->hwaccel ||
                   (ffhwaccel(avctx->hwaccel)->caps_internal & HWACCEL_CAP_THREAD_SAFE));

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
static int update_context_from_thread(AVCodecContext *dst, const AVCodecContext *src, int for_user)
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
#if FF_API_CODEC_PROPS
FF_DISABLE_DEPRECATION_WARNINGS
        dst->properties   = src->properties;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

        dst->bits_per_coded_sample = src->bits_per_coded_sample;
        dst->sample_aspect_ratio   = src->sample_aspect_ratio;

        dst->profile = src->profile;
        dst->level   = src->level;

        dst->bits_per_raw_sample = src->bits_per_raw_sample;
#if FF_API_TICKS_PER_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
        dst->ticks_per_frame     = src->ticks_per_frame;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        dst->color_primaries     = src->color_primaries;

        dst->color_trc   = src->color_trc;
        dst->colorspace  = src->colorspace;
        dst->color_range = src->color_range;
        dst->chroma_sample_location = src->chroma_sample_location;

        dst->sample_rate    = src->sample_rate;
        dst->sample_fmt     = src->sample_fmt;
        err = av_channel_layout_copy(&dst->ch_layout, &src->ch_layout);
        if (err < 0)
            return err;

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

        av_refstruct_replace(&dst->internal->pool, src->internal->pool);
        ff_decode_internal_sync(dst, src);
    }

    if (for_user) {
        if (codec->update_thread_context_for_user)
            err = codec->update_thread_context_for_user(dst, src);
    } else {
        const PerThreadContext *p_src = src->internal->thread_ctx;
        PerThreadContext       *p_dst = dst->internal->thread_ctx;

        if (codec->update_thread_context) {
            err = codec->update_thread_context(dst, src);
            if (err < 0)
                return err;
        }

        // reset dst hwaccel state if needed
        av_assert0(p_dst->hwaccel_threadsafe ||
                   (!dst->hwaccel && !dst->internal->hwaccel_priv_data));
        if (p_dst->hwaccel_threadsafe &&
            (!p_src->hwaccel_threadsafe || dst->hwaccel != src->hwaccel)) {
            ff_hwaccel_uninit(dst);
            p_dst->hwaccel_threadsafe = 0;
        }

        // propagate hwaccel state for threadsafe hwaccels
        if (p_src->hwaccel_threadsafe) {
            const FFHWAccel *hwaccel = ffhwaccel(src->hwaccel);
            if (!dst->hwaccel) {
                if (hwaccel->priv_data_size) {
                    av_assert0(hwaccel->update_thread_context);

                    dst->internal->hwaccel_priv_data =
                            av_mallocz(hwaccel->priv_data_size);
                    if (!dst->internal->hwaccel_priv_data)
                        return AVERROR(ENOMEM);
                }
                dst->hwaccel = src->hwaccel;
            }
            av_assert0(dst->hwaccel == src->hwaccel);

            if (hwaccel->update_thread_context) {
                err = hwaccel->update_thread_context(dst, src);
                if (err < 0) {
                    av_log(dst, AV_LOG_ERROR, "Error propagating hwaccel state\n");
                    ff_hwaccel_uninit(dst);
                    return err;
                }
            }
            p_dst->hwaccel_threadsafe = 1;
        }
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
static int update_context_from_user(AVCodecContext *dst, const AVCodecContext *src)
{
    int err;

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

    dst->frame_num        = src->frame_num;

    av_packet_unref(dst->internal->last_pkt_props);
    err = av_packet_copy_props(dst->internal->last_pkt_props, src->internal->last_pkt_props);
    if (err < 0)
        return err;

    return 0;
}

static int submit_packet(PerThreadContext *p, AVCodecContext *user_avctx,
                         AVPacket *in_pkt)
{
    FrameThreadContext *fctx = p->parent;
    PerThreadContext *prev_thread = fctx->prev_thread;
    const AVCodec *codec = p->avctx->codec;
    int ret;

    pthread_mutex_lock(&p->mutex);

    av_packet_unref(p->avpkt);
    av_packet_move_ref(p->avpkt, in_pkt);

    if (AVPACKET_IS_EMPTY(p->avpkt))
        p->avctx->internal->draining = 1;

    ret = update_context_from_user(p->avctx, user_avctx);
    if (ret) {
        pthread_mutex_unlock(&p->mutex);
        return ret;
    }
    atomic_store_explicit(&p->debug_threads,
                          (p->avctx->debug & FF_DEBUG_THREADS) != 0,
                          memory_order_relaxed);

    if (prev_thread) {
        if (atomic_load(&prev_thread->state) == STATE_SETTING_UP) {
            pthread_mutex_lock(&prev_thread->progress_mutex);
            while (atomic_load(&prev_thread->state) == STATE_SETTING_UP)
                pthread_cond_wait(&prev_thread->progress_cond, &prev_thread->progress_mutex);
            pthread_mutex_unlock(&prev_thread->progress_mutex);
        }

        /* codecs without delay might not be prepared to be called repeatedly here during
         * flushing (vp3/theora), and also don't need to be, since from this point on, they
         * will always return EOF anyway */
        if (!p->avctx->internal->draining ||
            (codec->capabilities & AV_CODEC_CAP_DELAY)) {
            ret = update_context_from_thread(p->avctx, prev_thread->avctx, 0);
            if (ret) {
                pthread_mutex_unlock(&p->mutex);
                return ret;
            }
        }
    }

    /* transfer the stashed hwaccel state, if any */
    av_assert0(!p->avctx->hwaccel || p->hwaccel_threadsafe);
    if (!p->hwaccel_threadsafe) {
        FFSWAP(const AVHWAccel*, p->avctx->hwaccel,                     fctx->stash_hwaccel);
        FFSWAP(void*,            p->avctx->hwaccel_context,             fctx->stash_hwaccel_context);
        FFSWAP(void*,            p->avctx->internal->hwaccel_priv_data, fctx->stash_hwaccel_priv);
    }

    atomic_store(&p->state, STATE_SETTING_UP);
    pthread_cond_signal(&p->input_cond);
    pthread_mutex_unlock(&p->mutex);

    fctx->prev_thread = p;
    fctx->next_decoding = (fctx->next_decoding + 1) % p->avctx->thread_count;

    return 0;
}

int ff_thread_receive_frame(AVCodecContext *avctx, AVFrame *frame)
{
    FrameThreadContext *fctx = avctx->internal->thread_ctx;
    int ret = 0;

    /* release the async lock, permitting blocked hwaccel threads to
     * go forward while we are in this function */
    async_unlock(fctx);

    /* submit packets to threads while there are no buffered results to return */
    while (!fctx->df.nb_f && !fctx->result) {
        PerThreadContext *p;

        /* get a packet to be submitted to the next thread */
        av_packet_unref(fctx->next_pkt);
        ret = ff_decode_get_packet(avctx, fctx->next_pkt);
        if (ret < 0 && ret != AVERROR_EOF)
            goto finish;

        ret = submit_packet(&fctx->threads[fctx->next_decoding], avctx,
                            fctx->next_pkt);
        if (ret < 0)
             goto finish;

        /* do not return any frames until all threads have something to do */
        if (fctx->next_decoding != fctx->next_finished &&
            !avctx->internal->draining)
            continue;

        p                   = &fctx->threads[fctx->next_finished];
        fctx->next_finished = (fctx->next_finished + 1) % avctx->thread_count;

        if (atomic_load(&p->state) != STATE_INPUT_READY) {
            pthread_mutex_lock(&p->progress_mutex);
            while (atomic_load_explicit(&p->state, memory_order_relaxed) != STATE_INPUT_READY)
                pthread_cond_wait(&p->output_cond, &p->progress_mutex);
            pthread_mutex_unlock(&p->progress_mutex);
        }

        update_context_from_thread(avctx, p->avctx, 1);
        fctx->result = p->result;
        p->result    = 0;
        if (p->df.nb_f)
            FFSWAP(DecodedFrames, fctx->df, p->df);
    }

    /* a thread may return multiple frames AND an error
     * we first return all the frames, then the error */
    if (fctx->df.nb_f) {
        decoded_frames_pop(&fctx->df, frame);
        ret = 0;
    } else {
        ret = fctx->result;
        fctx->result = 0;
    }

finish:
    async_lock(fctx);
    return ret;
}

void ff_thread_report_progress(ThreadFrame *f, int n, int field)
{
    PerThreadContext *p;
    atomic_int *progress = f->progress ? f->progress->progress : NULL;

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

void ff_thread_await_progress(const ThreadFrame *f, int n, int field)
{
    PerThreadContext *p;
    atomic_int *progress = f->progress ? f->progress->progress : NULL;

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
    PerThreadContext *p;

    if (!(avctx->active_thread_type&FF_THREAD_FRAME)) return;

    p = avctx->internal->thread_ctx;

    p->hwaccel_threadsafe = avctx->hwaccel &&
                            (ffhwaccel(avctx->hwaccel)->caps_internal & HWACCEL_CAP_THREAD_SAFE);

    if (hwaccel_serial(avctx) && !p->hwaccel_serializing) {
        pthread_mutex_lock(&p->parent->hwaccel_mutex);
        p->hwaccel_serializing = 1;
    }

    /* this assumes that no hwaccel calls happen before ff_thread_finish_setup() */
    if (avctx->hwaccel &&
        !(ffhwaccel(avctx->hwaccel)->caps_internal & HWACCEL_CAP_ASYNC_SAFE)) {
        p->async_serializing = 1;

        async_lock(p->parent);
    }

    /* thread-unsafe hwaccels share a single private data instance, so we
     * save hwaccel state for passing to the next thread;
     * this is done here so that this worker thread can wipe its own hwaccel
     * state after decoding, without requiring synchronization */
    av_assert0(!p->parent->stash_hwaccel);
    if (hwaccel_serial(avctx)) {
        p->parent->stash_hwaccel         = avctx->hwaccel;
        p->parent->stash_hwaccel_context = avctx->hwaccel_context;
        p->parent->stash_hwaccel_priv    = avctx->internal->hwaccel_priv_data;
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

            /* When using a threadsafe hwaccel, this is where
             * each thread's context is uninit'd and freed. */
            ff_hwaccel_uninit(ctx);

            if (ctx->priv_data) {
                if (codec->p.priv_class)
                    av_opt_free(ctx->priv_data);
                av_freep(&ctx->priv_data);
            }

            av_refstruct_unref(&ctx->internal->pool);
            av_packet_free(&ctx->internal->in_pkt);
            av_packet_free(&ctx->internal->last_pkt_props);
            ff_decode_internal_uninit(ctx);
            av_freep(&ctx->internal);
            av_buffer_unref(&ctx->hw_frames_ctx);
            av_frame_side_data_free(&ctx->decoded_side_data,
                                    &ctx->nb_decoded_side_data);
        }

        decoded_frames_free(&p->df);

        ff_pthread_free(p, per_thread_offsets);
        av_packet_free(&p->avpkt);

        av_freep(&p->avctx);
    }

    decoded_frames_free(&fctx->df);
    av_packet_free(&fctx->next_pkt);

    av_freep(&fctx->threads);
    ff_pthread_free(fctx, thread_ctx_offsets);

    /* if we have stashed hwaccel state, move it to the user-facing context,
     * so it will be freed in ff_codec_close() */
    av_assert0(!avctx->hwaccel);
    FFSWAP(const AVHWAccel*, avctx->hwaccel,                     fctx->stash_hwaccel);
    FFSWAP(void*,            avctx->hwaccel_context,             fctx->stash_hwaccel_context);
    FFSWAP(void*,            avctx->internal->hwaccel_priv_data, fctx->stash_hwaccel_priv);

    av_freep(&avctx->internal->thread_ctx);
}

static av_cold int init_thread(PerThreadContext *p, int *threads_to_free,
                               FrameThreadContext *fctx, AVCodecContext *avctx,
                               const FFCodec *codec, int first)
{
    AVCodecContext *copy;
    int err;

    p->initial_pict_type = AV_PICTURE_TYPE_NONE;
    if (avctx->codec_descriptor->props & AV_CODEC_PROP_INTRA_ONLY) {
        p->intra_only_flag = AV_FRAME_FLAG_KEY;
        if (avctx->codec_type == AVMEDIA_TYPE_VIDEO)
            p->initial_pict_type = AV_PICTURE_TYPE_I;
    }

    atomic_init(&p->state, STATE_INPUT_READY);

    copy = av_memdup(avctx, sizeof(*avctx));
    if (!copy)
        return AVERROR(ENOMEM);
    copy->priv_data = NULL;
    copy->decoded_side_data = NULL;
    copy->nb_decoded_side_data = 0;

    /* From now on, this PerThreadContext will be cleaned up by
     * ff_frame_thread_free in case of errors. */
    (*threads_to_free)++;

    p->parent = fctx;
    p->avctx  = copy;

    copy->internal = ff_decode_internal_alloc();
    if (!copy->internal)
        return AVERROR(ENOMEM);
    ff_decode_internal_sync(copy, avctx);
    copy->internal->thread_ctx = p;
    copy->internal->progress_frame_pool = avctx->internal->progress_frame_pool;

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

    if (!(p->avpkt = av_packet_alloc()))
        return AVERROR(ENOMEM);

    copy->internal->is_frame_mt = 1;
    if (!first)
        copy->internal->is_copy = 1;

    copy->internal->in_pkt = av_packet_alloc();
    if (!copy->internal->in_pkt)
        return AVERROR(ENOMEM);

    copy->internal->last_pkt_props = av_packet_alloc();
    if (!copy->internal->last_pkt_props)
        return AVERROR(ENOMEM);

    if (codec->init) {
        err = codec->init(copy);
        if (err < 0) {
            if (codec->caps_internal & FF_CODEC_CAP_INIT_CLEANUP)
                p->thread_init = NEEDS_CLOSE;
            return err;
        }
    }
    p->thread_init = NEEDS_CLOSE;

    if (first) {
        update_context_from_thread(avctx, copy, 1);

        av_frame_side_data_free(&avctx->decoded_side_data, &avctx->nb_decoded_side_data);
        for (int i = 0; i < copy->nb_decoded_side_data; i++) {
            err = av_frame_side_data_clone(&avctx->decoded_side_data,
                                           &avctx->nb_decoded_side_data,
                                           copy->decoded_side_data[i], 0);
            if (err < 0)
                return err;
        }
    }

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

    fctx->next_pkt = av_packet_alloc();
    if (!fctx->next_pkt)
        return AVERROR(ENOMEM);

    fctx->async_lock = 1;

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
    fctx->prev_thread = NULL;

    decoded_frames_flush(&fctx->df);
    fctx->result = 0;

    for (i = 0; i < avctx->thread_count; i++) {
        PerThreadContext *p = &fctx->threads[i];

        decoded_frames_flush(&p->df);
        p->result = 0;

        avcodec_flush_buffers(p->avctx);
    }
}

int ff_thread_can_start_frame(AVCodecContext *avctx)
{
    if ((avctx->active_thread_type & FF_THREAD_FRAME) &&
        ffcodec(avctx->codec)->update_thread_context) {
        PerThreadContext *p = avctx->internal->thread_ctx;

        if (atomic_load(&p->state) != STATE_SETTING_UP)
            return 0;
    }

    return 1;
}

static int thread_get_buffer_internal(AVCodecContext *avctx, AVFrame *f, int flags)
{
    PerThreadContext *p;
    int err;

    if (!(avctx->active_thread_type & FF_THREAD_FRAME))
        return ff_get_buffer(avctx, f, flags);

    p = avctx->internal->thread_ctx;
    if (atomic_load(&p->state) != STATE_SETTING_UP &&
        ffcodec(avctx->codec)->update_thread_context) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() cannot be called after ff_thread_finish_setup()\n");
        return -1;
    }

    pthread_mutex_lock(&p->parent->buffer_mutex);
    err = ff_get_buffer(avctx, f, flags);

    pthread_mutex_unlock(&p->parent->buffer_mutex);

    return err;
}

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
    if (!(avctx->active_thread_type & FF_THREAD_FRAME))
        return ff_get_buffer(avctx, f->f, flags);

    f->progress = av_refstruct_allocz(sizeof(*f->progress));
    if (!f->progress)
        return AVERROR(ENOMEM);

    atomic_init(&f->progress->progress[0], -1);
    atomic_init(&f->progress->progress[1], -1);

    ret = ff_thread_get_buffer(avctx, f->f, flags);
    if (ret)
        av_refstruct_unref(&f->progress);
    return ret;
}

void ff_thread_release_ext_buffer(ThreadFrame *f)
{
    av_refstruct_unref(&f->progress);
    f->owner[0] = f->owner[1] = NULL;
    if (f->f)
        av_frame_unref(f->f);
}

enum ThreadingStatus ff_thread_sync_ref(AVCodecContext *avctx, size_t offset)
{
    PerThreadContext *p;
    const void *ref;

    if (!avctx->internal->is_copy)
        return avctx->active_thread_type & FF_THREAD_FRAME ?
                  FF_THREAD_IS_FIRST_THREAD : FF_THREAD_NO_FRAME_THREADING;

    p = avctx->internal->thread_ctx;

    av_assert1(memcpy(&ref, (char*)avctx->priv_data + offset, sizeof(ref)) && ref == NULL);

    memcpy(&ref, (const char*)p->parent->threads[0].avctx->priv_data + offset, sizeof(ref));
    av_assert1(ref);
    av_refstruct_replace((char*)avctx->priv_data + offset, ref);

    return FF_THREAD_IS_COPY;
}

int ff_thread_get_packet(AVCodecContext *avctx, AVPacket *pkt)
{
    PerThreadContext *p = avctx->internal->thread_ctx;

    if (!AVPACKET_IS_EMPTY(p->avpkt)) {
        av_packet_move_ref(pkt, p->avpkt);
        return 0;
    }

    return avctx->internal->draining ? AVERROR_EOF : AVERROR(EAGAIN);
}
