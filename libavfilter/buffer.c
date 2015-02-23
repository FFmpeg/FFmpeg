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

#include "libavutil/channel_layout.h"
#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavcodec/avcodec.h"

#include "avfilter.h"
#include "internal.h"
#include "audio.h"
#include "avcodec.h"
#include "version.h"

#if FF_API_AVFILTERBUFFER
void ff_avfilter_default_free_buffer(AVFilterBuffer *ptr)
{
    if (ptr->extended_data != ptr->data)
        av_freep(&ptr->extended_data);
    av_freep(&ptr->data[0]);
    av_free(ptr);
}

static int copy_video_props(AVFilterBufferRefVideoProps *dst, AVFilterBufferRefVideoProps *src) {
    *dst = *src;
    if (src->qp_table) {
        int qsize = src->qp_table_size;
        dst->qp_table = av_malloc(qsize);
        if (!dst->qp_table) {
            av_log(NULL, AV_LOG_ERROR, "Failed to allocate qp_table\n");
            dst->qp_table_size = 0;
            return AVERROR(ENOMEM);
        }
        memcpy(dst->qp_table, src->qp_table, qsize);
    }
    return 0;
}

AVFilterBufferRef *avfilter_ref_buffer(AVFilterBufferRef *ref, int pmask)
{
    AVFilterBufferRef *ret = av_malloc(sizeof(AVFilterBufferRef));
    if (!ret)
        return NULL;
    *ret = *ref;

    ret->metadata = NULL;
    av_dict_copy(&ret->metadata, ref->metadata, 0);

    if (ref->type == AVMEDIA_TYPE_VIDEO) {
        ret->video = av_malloc(sizeof(AVFilterBufferRefVideoProps));
        if (!ret->video) {
            av_free(ret);
            return NULL;
        }
        copy_video_props(ret->video, ref->video);
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
            if (!(ret->extended_data = av_malloc_array(sizeof(*ret->extended_data),
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

void avfilter_unref_buffer(AVFilterBufferRef *ref)
{
    if (!ref)
        return;
    av_assert0(ref->buf->refcount > 0);
    if (!(--ref->buf->refcount))
        ref->buf->free(ref->buf);
    if (ref->extended_data != ref->data)
        av_freep(&ref->extended_data);
    if (ref->video)
        av_freep(&ref->video->qp_table);
    av_freep(&ref->video);
    av_freep(&ref->audio);
    av_dict_free(&ref->metadata);
    av_free(ref);
}

void avfilter_unref_bufferp(AVFilterBufferRef **ref)
{
FF_DISABLE_DEPRECATION_WARNINGS
    avfilter_unref_buffer(*ref);
FF_ENABLE_DEPRECATION_WARNINGS
    *ref = NULL;
}

int avfilter_copy_frame_props(AVFilterBufferRef *dst, const AVFrame *src)
{
    dst->pts    = src->pts;
    dst->pos    = av_frame_get_pkt_pos(src);
    dst->format = src->format;

    av_dict_free(&dst->metadata);
    av_dict_copy(&dst->metadata, av_frame_get_metadata(src), 0);

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

void avfilter_copy_buffer_ref_props(AVFilterBufferRef *dst, const AVFilterBufferRef *src)
{
    // copy common properties
    dst->pts             = src->pts;
    dst->pos             = src->pos;

    switch (src->type) {
    case AVMEDIA_TYPE_VIDEO: {
        if (dst->video->qp_table)
            av_freep(&dst->video->qp_table);
        copy_video_props(dst->video, src->video);
        break;
    }
    case AVMEDIA_TYPE_AUDIO: *dst->audio = *src->audio; break;
    default: break;
    }

    av_dict_free(&dst->metadata);
    av_dict_copy(&dst->metadata, src->metadata, 0);
}
#endif /* FF_API_AVFILTERBUFFER */
