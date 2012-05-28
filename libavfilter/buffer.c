/*
 * Copyright Stefano Sabatini <stefasab gmail com>
 * Copyright Anton Khirnov <anton khirnov net>
 * Copyright Michael Niedermayer <michaelni gmx at>
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

#include "libavutil/audioconvert.h"
#include "libavutil/avassert.h"
#include "libavcodec/avcodec.h"

#include "avfilter.h"
#include "internal.h"
#include "avcodec.h"

void ff_avfilter_default_free_buffer(AVFilterBuffer *ptr)
{
    if (ptr->extended_data != ptr->data)
        av_freep(&ptr->extended_data);
    av_free(ptr->data[0]);
    av_free(ptr);
}

AVFilterBufferRef *avfilter_ref_buffer(AVFilterBufferRef *ref, int pmask)
{
    AVFilterBufferRef *ret = av_malloc(sizeof(AVFilterBufferRef));
    if (!ret)
        return NULL;
    *ret = *ref;
    if (ref->type == AVMEDIA_TYPE_VIDEO) {
        ret->video = av_malloc(sizeof(AVFilterBufferRefVideoProps));
        if (!ret->video) {
            av_free(ret);
            return NULL;
        }
        *ret->video = *ref->video;
        ret->extended_data = ret->data;
    } else if (ref->type == AVMEDIA_TYPE_AUDIO) {
        ret->audio = av_malloc(sizeof(AVFilterBufferRefAudioProps));
        if (!ret->audio) {
            av_free(ret);
            return NULL;
        }
        *ret->audio = *ref->audio;

        if (ref->extended_data && ref->extended_data != ref->data) {
            int nb_channels = av_get_channel_layout_nb_channels(ref->audio->channel_layout);
            if (!(ret->extended_data = av_malloc(sizeof(*ret->extended_data) *
                                                 nb_channels))) {
                av_freep(&ret->audio);
                av_freep(&ret);
                return NULL;
            }
            memcpy(ret->extended_data, ref->extended_data,
                   sizeof(*ret->extended_data) * nb_channels);
        } else
            ret->extended_data = ret->data;
    }
    ret->perms &= pmask;
    ret->buf->refcount ++;
    return ret;
}

void ff_free_pool(AVFilterPool *pool)
{
    int i;

    av_assert0(pool->refcount > 0);

    for (i = 0; i < POOL_SIZE; i++) {
        if (pool->pic[i]) {
            AVFilterBufferRef *picref = pool->pic[i];
            /* free buffer: picrefs stored in the pool are not
             * supposed to contain a free callback */
            av_assert0(!picref->buf->refcount);
            av_freep(&picref->buf->data[0]);
            av_freep(&picref->buf);

            av_freep(&picref->audio);
            av_freep(&picref->video);
            av_freep(&pool->pic[i]);
            pool->count--;
        }
    }
    pool->draining = 1;

    if (!--pool->refcount) {
        av_assert0(!pool->count);
        av_free(pool);
    }
}

static void store_in_pool(AVFilterBufferRef *ref)
{
    int i;
    AVFilterPool *pool= ref->buf->priv;

    av_assert0(ref->buf->data[0]);
    av_assert0(pool->refcount>0);

    if (pool->count == POOL_SIZE) {
        AVFilterBufferRef *ref1 = pool->pic[0];
        av_freep(&ref1->video);
        av_freep(&ref1->audio);
        av_freep(&ref1->buf->data[0]);
        av_freep(&ref1->buf);
        av_free(ref1);
        memmove(&pool->pic[0], &pool->pic[1], sizeof(void*)*(POOL_SIZE-1));
        pool->count--;
        pool->pic[POOL_SIZE-1] = NULL;
    }

    for (i = 0; i < POOL_SIZE; i++) {
        if (!pool->pic[i]) {
            pool->pic[i] = ref;
            pool->count++;
            break;
        }
    }
    if (pool->draining) {
        ff_free_pool(pool);
    } else
        --pool->refcount;
}

void avfilter_unref_buffer(AVFilterBufferRef *ref)
{
    if (!ref)
        return;
    av_assert0(ref->buf->refcount > 0);
    if (!(--ref->buf->refcount)) {
        if (!ref->buf->free) {
            store_in_pool(ref);
            return;
        }
        ref->buf->free(ref->buf);
    }
    if (ref->extended_data != ref->data)
        av_freep(&ref->extended_data);
    av_freep(&ref->video);
    av_freep(&ref->audio);
    av_free(ref);
}

void avfilter_unref_bufferp(AVFilterBufferRef **ref)
{
    avfilter_unref_buffer(*ref);
    *ref = NULL;
}

int avfilter_copy_frame_props(AVFilterBufferRef *dst, const AVFrame *src)
{
    dst->pts    = src->pts;
    dst->pos    = av_frame_get_pkt_pos(src);
    dst->format = src->format;

    switch (dst->type) {
    case AVMEDIA_TYPE_VIDEO:
        dst->video->w                   = src->width;
        dst->video->h                   = src->height;
        dst->video->sample_aspect_ratio = src->sample_aspect_ratio;
        dst->video->interlaced          = src->interlaced_frame;
        dst->video->top_field_first     = src->top_field_first;
        dst->video->key_frame           = src->key_frame;
        dst->video->pict_type           = src->pict_type;
        break;
    case AVMEDIA_TYPE_AUDIO:
        dst->audio->sample_rate         = src->sample_rate;
        dst->audio->channel_layout      = src->channel_layout;
        break;
    default:
        return AVERROR(EINVAL);
    }

    return 0;
}

int avfilter_copy_buf_props(AVFrame *dst, const AVFilterBufferRef *src)
{
    int planes, nb_channels;

    memcpy(dst->data, src->data, sizeof(dst->data));
    memcpy(dst->linesize, src->linesize, sizeof(dst->linesize));

    dst->pts     = src->pts;
    dst->format  = src->format;

    switch (src->type) {
    case AVMEDIA_TYPE_VIDEO:
        dst->width               = src->video->w;
        dst->height              = src->video->h;
        dst->sample_aspect_ratio = src->video->sample_aspect_ratio;
        dst->interlaced_frame    = src->video->interlaced;
        dst->top_field_first     = src->video->top_field_first;
        dst->key_frame           = src->video->key_frame;
        dst->pict_type           = src->video->pict_type;
        break;
    case AVMEDIA_TYPE_AUDIO:
        nb_channels = av_get_channel_layout_nb_channels(src->audio->channel_layout);
        planes      = av_sample_fmt_is_planar(src->format) ? nb_channels : 1;

        if (planes > FF_ARRAY_ELEMS(dst->data)) {
            dst->extended_data = av_mallocz(planes * sizeof(*dst->extended_data));
            if (!dst->extended_data)
                return AVERROR(ENOMEM);
            memcpy(dst->extended_data, src->extended_data,
                   planes * sizeof(dst->extended_data));
        } else
            dst->extended_data = dst->data;

        dst->sample_rate         = src->audio->sample_rate;
        dst->channel_layout      = src->audio->channel_layout;
        dst->nb_samples          = src->audio->nb_samples;
        break;
    default:
        return AVERROR(EINVAL);
    }

    return 0;
}

void avfilter_copy_buffer_ref_props(AVFilterBufferRef *dst, AVFilterBufferRef *src)
{
    // copy common properties
    dst->pts             = src->pts;
    dst->pos             = src->pos;

    switch (src->type) {
    case AVMEDIA_TYPE_VIDEO: *dst->video = *src->video; break;
    case AVMEDIA_TYPE_AUDIO: *dst->audio = *src->audio; break;
    default: break;
    }
}
