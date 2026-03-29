/*
 * This file is part of FFmpeg.
 *
 * Copyright (c) 2015 Matthieu Bouron <matthieu.bouron stupeflix.com>
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

#ifndef AVFILTER_FRAMEPOOL_H
#define AVFILTER_FRAMEPOOL_H

#include "libavutil/buffer.h"
#include "libavutil/frame.h"
#include "libavutil/internal.h"

/**
 * Frame pool. This structure must be allocated with
 * ff_frame_pool_{video,audio}_reinit() and freed with ff_frame_pool_uninit().
 */
typedef struct FFFramePool {

    enum AVMediaType type;
    union {
        enum AVPixelFormat pix_fmt;
        enum AVSampleFormat sample_fmt;
    };

    /* video */
    int width;
    int height;

    /* audio */
    int planes;
    int channels;
    int nb_samples;

    /* common */
    int align;
    int linesize[4];
    AVBufferPool *pools[4]; /* for audio, only pools[0] is used */

} FFFramePool;

/**
 * Recreate the video frame pool if its current configuration differs from the
 * provided configuration. If initialization fails, the old pool is kept
 * unchanged.
 *
 * @param pool pointer to the frame pool to be reinitialized, or a pointer to
 *        NULL to create a new pool.
 * @param width width of each frame in this pool
 * @param height height of each frame in this pool
 * @param format format of each frame in this pool
 * @param align buffers alignment of each frame in this pool
 * @return 0 on success, a negative AVERROR otherwise.
 */
int ff_frame_pool_video_reinit(FFFramePool **pool,
                               int width,
                               int height,
                               enum AVPixelFormat format,
                               int align);

/**
 * Recreate the audio frame pool if its current configuration differs from the
 * provided configuration. If initialization fails, the old pool is kept
 * unchanged.
 *
 * @param pool pointer to the frame pool to be reinitialized, or a pointer to
 *        NULL to create a new pool.
 * @param channels channels of each frame in this pool
 * @param nb_samples number of samples of each frame in this pool
 * @param format format of each frame in this pool
 * @param align buffers alignment of each frame in this pool
 * @return 0 on success, a negative AVERROR otherwise.
 */
int ff_frame_pool_audio_reinit(FFFramePool **pool,
                               int channels,
                               int nb_samples,
                               enum AVSampleFormat format,
                               int align);

/**
 * Deallocate the frame pool. It is safe to call this function while
 * some of the allocated frame are still in use.
 *
 * @param pool pointer to the frame pool to be freed. It will be set to NULL.
 */
void ff_frame_pool_uninit(FFFramePool **pool);

/**
 * Allocate a new AVFrame, reusing old buffers from the pool when available.
 * This function may be called simultaneously from multiple threads.
 *
 * @return a new AVFrame on success, NULL on error.
 */
AVFrame *ff_frame_pool_get(FFFramePool *pool);


#endif /* AVFILTER_FRAMEPOOL_H */
