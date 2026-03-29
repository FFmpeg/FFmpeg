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

#include "framepool.h"
#include "libavutil/avassert.h"
#include "libavutil/avutil.h"
#include "libavutil/buffer.h"
#include "libavutil/frame.h"
#include "libavutil/imgutils.h"
#include "libavutil/imgutils_internal.h"
#include "libavutil/mem.h"
#include "libavutil/pixfmt.h"

struct FFFramePool {

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
    AVBufferPool *pools[4];

};

static av_cold FFFramePool *frame_pool_video_init(int width, int height,
                                                  enum AVPixelFormat format,
                                                  int align)
{
    int i, ret;
    FFFramePool *pool;
    ptrdiff_t linesizes[4];
    size_t sizes[4];

    pool = av_mallocz(sizeof(FFFramePool));
    if (!pool)
        return NULL;

    pool->type = AVMEDIA_TYPE_VIDEO;
    pool->width = width;
    pool->height = height;
    pool->pix_fmt = format;
    pool->align = align;

    if ((ret = av_image_check_size2(width, height, INT64_MAX, format, 0, NULL)) < 0) {
        goto fail;
    }

    ret = av_image_fill_linesizes(pool->linesize, pool->pix_fmt,
                                    FFALIGN(pool->width, align));
    if (ret < 0) {
        goto fail;
    }

    for (i = 0; i < 4 && pool->linesize[i]; i++) {
        pool->linesize[i] = FFALIGN(pool->linesize[i], pool->align);
        if ((pool->linesize[i] & (pool->align - 1)))
            goto fail;
    }

    for (i = 0; i < 4; i++)
        linesizes[i] = pool->linesize[i];

    if (av_image_fill_plane_sizes(sizes, pool->pix_fmt,
                                  FFALIGN(pool->height, align),
                                  linesizes) < 0) {
        goto fail;
    }

    for (i = 0; i < 4 && sizes[i]; i++) {
        if (sizes[i] > SIZE_MAX - align)
            goto fail;
        pool->pools[i] = av_buffer_pool_init(sizes[i] + align,
                                             CONFIG_MEMORY_POISONING
                                                 ? NULL
                                                 : av_buffer_allocz);
        if (!pool->pools[i])
            goto fail;
    }

    return pool;

fail:
    ff_frame_pool_uninit(&pool);
    return NULL;
}

static av_cold FFFramePool *frame_pool_audio_init(int channels, int nb_samples,
                                                  enum AVSampleFormat format,
                                                  int align)
{
    int ret, planar;
    FFFramePool *pool;

    pool = av_mallocz(sizeof(FFFramePool));
    if (!pool)
        return NULL;

    planar = av_sample_fmt_is_planar(format);

    pool->type = AVMEDIA_TYPE_AUDIO;
    pool->planes = planar ? channels : 1;
    pool->channels = channels;
    pool->nb_samples = nb_samples;
    pool->sample_fmt = format;
    pool->align = align;

    ret = av_samples_get_buffer_size(&pool->linesize[0], channels,
                                     nb_samples, format, 0);
    if (ret < 0)
        goto fail;

    if (pool->linesize[0] > SIZE_MAX - align)
        goto fail;
    pool->pools[0] = av_buffer_pool_init(pool->linesize[0] + align,
                                         av_buffer_allocz);
    if (!pool->pools[0])
        goto fail;

    return pool;

fail:
    ff_frame_pool_uninit(&pool);
    return NULL;
}

AVFrame *ff_frame_pool_get(FFFramePool *pool)
{
    int i;
    AVFrame *frame;
    const AVPixFmtDescriptor *desc;

    frame = av_frame_alloc();
    if (!frame) {
        return NULL;
    }

    switch(pool->type) {
    case AVMEDIA_TYPE_VIDEO:
        desc = av_pix_fmt_desc_get(pool->pix_fmt);
        if (!desc) {
            goto fail;
        }

        frame->width = pool->width;
        frame->height = pool->height;
        frame->format = pool->pix_fmt;

        for (i = 0; i < 4; i++) {
            frame->linesize[i] = pool->linesize[i];
            if (!pool->pools[i])
                break;

            frame->buf[i] = av_buffer_pool_get(pool->pools[i]);
            if (!frame->buf[i])
                goto fail;

            frame->data[i] = (uint8_t *)FFALIGN((uintptr_t)frame->buf[i]->data, pool->align);
        }

        if (desc->flags & AV_PIX_FMT_FLAG_PAL) {
            enum AVPixelFormat format = pool->pix_fmt;
            if (format == AV_PIX_FMT_PAL8)
                format = AV_PIX_FMT_BGR8;

            av_assert0(frame->data[1] != NULL);
            if (avpriv_set_systematic_pal2((uint32_t *)frame->data[1], format) < 0)
                goto fail;
        }

        frame->extended_data = frame->data;
        break;
    case AVMEDIA_TYPE_AUDIO:
        frame->nb_samples = pool->nb_samples;
        frame->ch_layout.nb_channels = pool->channels;
        frame->format = pool->sample_fmt;
        frame->linesize[0] = pool->linesize[0];

        if (pool->planes > AV_NUM_DATA_POINTERS) {
            frame->extended_data = av_calloc(pool->planes,
                                             sizeof(*frame->extended_data));
            frame->nb_extended_buf = pool->planes - AV_NUM_DATA_POINTERS;
            frame->extended_buf  = av_calloc(frame->nb_extended_buf,
                                             sizeof(*frame->extended_buf));
            if (!frame->extended_data || !frame->extended_buf)
                goto fail;
        } else {
            frame->extended_data = frame->data;
            av_assert0(frame->nb_extended_buf == 0);
        }

        for (i = 0; i < FFMIN(pool->planes, AV_NUM_DATA_POINTERS); i++) {
            frame->buf[i] = av_buffer_pool_get(pool->pools[0]);
            if (!frame->buf[i])
                goto fail;
            frame->extended_data[i] = frame->data[i] =
                (uint8_t *)FFALIGN((uintptr_t)frame->buf[i]->data, pool->align);
        }
        for (i = 0; i < frame->nb_extended_buf; i++) {
            frame->extended_buf[i] = av_buffer_pool_get(pool->pools[0]);
            if (!frame->extended_buf[i])
                goto fail;
            frame->extended_data[i + AV_NUM_DATA_POINTERS] =
                (uint8_t *)FFALIGN((uintptr_t)frame->extended_buf[i]->data, pool->align);
        }

        break;
    default:
        av_unreachable("only audio and video frame pools exist");
    }

    return frame;
fail:
    av_frame_free(&frame);
    return NULL;
}

av_cold void ff_frame_pool_uninit(FFFramePool **pool)
{
    int i;

    if (!*pool)
        return;

    for (i = 0; i < 4; i++) {
        av_buffer_pool_uninit(&(*pool)->pools[i]);
    }

    av_freep(pool);
}

int ff_frame_pool_video_reinit(FFFramePool **pool,
                               int width,
                               int height,
                               enum AVPixelFormat format,
                               int align)
{
    FFFramePool *cur = *pool;
    if (cur && cur->pix_fmt == format &&
        FFALIGN(cur->width,  cur->align) == FFALIGN(width,  align) &&
        FFALIGN(cur->height, cur->align) == FFALIGN(height, align) &&
        cur->align == align)
    {
        av_assert1(cur->type == AVMEDIA_TYPE_VIDEO);
        cur->width = width;
        cur->height = height;
        return 0;
    }

    FFFramePool *new = frame_pool_video_init(width, height, format, align);
    if (!new)
        return AVERROR(ENOMEM);

    *pool = new;
    ff_frame_pool_uninit(&cur);
    return 0;
}

int ff_frame_pool_audio_reinit(FFFramePool **pool,
                               int channels,
                               int nb_samples,
                               enum AVSampleFormat format,
                               int align)
{
    FFFramePool *cur = *pool;
    if (cur && cur->sample_fmt == format &&
        cur->channels == channels &&
        cur->nb_samples == nb_samples &&
        cur->align == align)
    {
        av_assert1(cur->type == AVMEDIA_TYPE_AUDIO);
        return 0;
    }

    FFFramePool *new = frame_pool_audio_init(channels, nb_samples, format, align);
    if (!new)
        return AVERROR(ENOMEM);

    *pool = new;
    ff_frame_pool_uninit(&cur);
    return 0;
}
