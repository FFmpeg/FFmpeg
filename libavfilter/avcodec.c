/*
 * Copyright 2011 Stefano Sabatini | stefasab at gmail.com
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
 * libavcodec/libavfilter gluing utilities
 */

#include "avcodec.h"
#include "libavutil/avassert.h"
#include "libavutil/opt.h"

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
        av_freep(&dst->video->qp_table);
        dst->video->qp_table_linesize = 0;
        if (src->qscale_table) {
            int qsize = src->qstride ? src->qstride * ((src->height+15)/16) : (src->width+15)/16;
            dst->video->qp_table = av_malloc(qsize);
            if (!dst->video->qp_table)
                return AVERROR(ENOMEM);
            dst->video->qp_table_linesize = src->qstride;
            dst->video->qp_table_size     = qsize;
            memcpy(dst->video->qp_table, src->qscale_table, qsize);
        }
        break;
    case AVMEDIA_TYPE_AUDIO:
        dst->audio->sample_rate         = src->sample_rate;
        dst->audio->channel_layout      = src->channel_layout;
        dst->audio->channels            = src->channels;
        if(src->channels < av_get_channel_layout_nb_channels(src->channel_layout)) {
            av_log(NULL, AV_LOG_ERROR, "libavfilter does not support this channel layout\n");
            return AVERROR(EINVAL);
        }
        break;
    default:
        return AVERROR(EINVAL);
    }

    return 0;
}

AVFilterBufferRef *avfilter_get_video_buffer_ref_from_frame(const AVFrame *frame,
                                                            int perms)
{
    AVFilterBufferRef *picref =
        avfilter_get_video_buffer_ref_from_arrays(frame->data, frame->linesize, perms,
                                                  frame->width, frame->height,
                                                  frame->format);
    if (!picref)
        return NULL;
    if (avfilter_copy_frame_props(picref, frame) < 0) {
        picref->buf->data[0] = NULL;
        avfilter_unref_bufferp(&picref);
    }
    return picref;
}

AVFilterBufferRef *avfilter_get_audio_buffer_ref_from_frame(const AVFrame *frame,
                                                            int perms)
{
    AVFilterBufferRef *samplesref;
    int channels = av_frame_get_channels(frame);
    int64_t layout = av_frame_get_channel_layout(frame);

    if (layout && av_get_channel_layout_nb_channels(layout) != av_frame_get_channels(frame)) {
        av_log(0, AV_LOG_ERROR, "Layout indicates a different number of channels than actually present\n");
        return NULL;
    }

    samplesref = avfilter_get_audio_buffer_ref_from_arrays_channels(
        (uint8_t **)frame->extended_data, frame->linesize[0], perms,
        frame->nb_samples, frame->format, channels, layout);
    if (!samplesref)
        return NULL;
    if (avfilter_copy_frame_props(samplesref, frame) < 0) {
        samplesref->buf->data[0] = NULL;
        avfilter_unref_bufferp(&samplesref);
    }
    return samplesref;
}

AVFilterBufferRef *avfilter_get_buffer_ref_from_frame(enum AVMediaType type,
                                                      const AVFrame *frame,
                                                      int perms)
{
    switch (type) {
    case AVMEDIA_TYPE_VIDEO:
        return avfilter_get_video_buffer_ref_from_frame(frame, perms);
    case AVMEDIA_TYPE_AUDIO:
        return avfilter_get_audio_buffer_ref_from_frame(frame, perms);
    default:
        return NULL;
    }
}

int avfilter_copy_buf_props(AVFrame *dst, const AVFilterBufferRef *src)
{
    int planes, nb_channels;

    if (!dst)
        return AVERROR(EINVAL);
    /* abort in case the src is NULL and dst is not, avoid inconsistent state in dst */
    av_assert0(src);

    memcpy(dst->data, src->data, sizeof(dst->data));
    memcpy(dst->linesize, src->linesize, sizeof(dst->linesize));

    dst->pts     = src->pts;
    dst->format  = src->format;
    av_frame_set_pkt_pos(dst, src->pos);

    switch (src->type) {
    case AVMEDIA_TYPE_VIDEO:
        av_assert0(src->video);
        dst->width               = src->video->w;
        dst->height              = src->video->h;
        dst->sample_aspect_ratio = src->video->sample_aspect_ratio;
        dst->interlaced_frame    = src->video->interlaced;
        dst->top_field_first     = src->video->top_field_first;
        dst->key_frame           = src->video->key_frame;
        dst->pict_type           = src->video->pict_type;
        break;
    case AVMEDIA_TYPE_AUDIO:
        av_assert0(src->audio);
        nb_channels = av_get_channel_layout_nb_channels(src->audio->channel_layout);
        planes      = av_sample_fmt_is_planar(src->format) ? nb_channels : 1;

        if (planes > FF_ARRAY_ELEMS(dst->data)) {
            dst->extended_data = av_mallocz(planes * sizeof(*dst->extended_data));
            if (!dst->extended_data)
                return AVERROR(ENOMEM);
            memcpy(dst->extended_data, src->extended_data,
                   planes * sizeof(*dst->extended_data));
        } else
            dst->extended_data = dst->data;
        dst->nb_samples          = src->audio->nb_samples;
        av_frame_set_sample_rate   (dst, src->audio->sample_rate);
        av_frame_set_channel_layout(dst, src->audio->channel_layout);
        av_frame_set_channels      (dst, src->audio->channels);
        break;
    default:
        return AVERROR(EINVAL);
    }

    return 0;
}

#ifdef FF_API_FILL_FRAME
int avfilter_fill_frame_from_audio_buffer_ref(AVFrame *frame,
                                              const AVFilterBufferRef *samplesref)
{
    return avfilter_copy_buf_props(frame, samplesref);
}

int avfilter_fill_frame_from_video_buffer_ref(AVFrame *frame,
                                              const AVFilterBufferRef *picref)
{
    return avfilter_copy_buf_props(frame, picref);
}

int avfilter_fill_frame_from_buffer_ref(AVFrame *frame,
                                        const AVFilterBufferRef *ref)
{
    return avfilter_copy_buf_props(frame, ref);
}
#endif
