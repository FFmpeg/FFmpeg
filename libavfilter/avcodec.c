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
 * libavcodec/libavfilter gluing utilities
 */

#include "avcodec.h"
#include "libavutil/opt.h"

int avfilter_copy_frame_props(AVFilterBufferRef *dst, const AVFrame *src)
{
    dst->pts    = src->pts;
    dst->pos    = src->pkt_pos;
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

AVFilterBufferRef *avfilter_get_video_buffer_ref_from_frame(const AVFrame *frame,
                                                            int perms)
{
    AVFilterBufferRef *picref =
        avfilter_get_video_buffer_ref_from_arrays(frame->data, frame->linesize, perms,
                                                  frame->width, frame->height,
                                                  frame->format);
    if (!picref)
        return NULL;
    avfilter_copy_frame_props(picref, frame);
    return picref;
}

AVFilterBufferRef *avfilter_get_audio_buffer_ref_from_frame(const AVFrame *frame,
                                                            int perms)
{
    AVFilterBufferRef *picref =
        avfilter_get_audio_buffer_ref_from_arrays((uint8_t **)frame->data, (int *)frame->linesize, perms,
                                                  frame->nb_samples, frame->format,
                                                  av_frame_get_channel_layout(frame),
                                                  av_sample_fmt_is_planar(frame->format));
    if (!picref)
        return NULL;
    avfilter_copy_frame_props(picref, frame);
    return picref;
}

int avfilter_fill_frame_from_audio_buffer_ref(AVFrame *frame,
                                              const AVFilterBufferRef *samplesref)
{
    if (!samplesref || !samplesref->audio || !frame)
        return AVERROR(EINVAL);

    memcpy(frame->data, samplesref->data, sizeof(frame->data));
    frame->pkt_pos    = samplesref->pos;
    frame->format     = samplesref->format;
    frame->nb_samples = samplesref->audio->nb_samples;
    frame->pts        = samplesref->pts;

    return 0;
}

int avfilter_fill_frame_from_video_buffer_ref(AVFrame *frame,
                                              const AVFilterBufferRef *picref)
{
    if (!picref || !picref->video || !frame)
        return AVERROR(EINVAL);

    memcpy(frame->data,     picref->data,     sizeof(frame->data));
    memcpy(frame->linesize, picref->linesize, sizeof(frame->linesize));
    frame->pkt_pos          = picref->pos;
    frame->interlaced_frame = picref->video->interlaced;
    frame->top_field_first  = picref->video->top_field_first;
    frame->key_frame        = picref->video->key_frame;
    frame->pict_type        = picref->video->pict_type;
    frame->sample_aspect_ratio = picref->video->sample_aspect_ratio;
    frame->width            = picref->video->w;
    frame->height           = picref->video->h;
    frame->format           = picref->format;
    frame->pts              = picref->pts;

    return 0;
}

int avfilter_fill_frame_from_buffer_ref(AVFrame *frame,
                                        const AVFilterBufferRef *ref)
{
    if (!ref)
        return AVERROR(EINVAL);
    return ref->video ? avfilter_fill_frame_from_video_buffer_ref(frame, ref)
                      : avfilter_fill_frame_from_audio_buffer_ref(frame, ref);
}
