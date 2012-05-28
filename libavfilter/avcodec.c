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
#include "libavutil/opt.h"

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
        avfilter_get_audio_buffer_ref_from_arrays((uint8_t **)frame->data, frame->linesize[0], perms,
                                                  frame->nb_samples, frame->format,
                                                  av_frame_get_channel_layout(frame));
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
    memcpy(frame->linesize, samplesref->linesize, sizeof(frame->linesize));
    av_frame_set_pkt_pos(frame, samplesref->pos);
    frame->format         = samplesref->format;
    frame->nb_samples     = samplesref->audio->nb_samples;
    frame->pts            = samplesref->pts;
    frame->sample_rate    = samplesref->audio->sample_rate;
    frame->channel_layout = samplesref->audio->channel_layout;

    return 0;
}

int avfilter_fill_frame_from_video_buffer_ref(AVFrame *frame,
                                              const AVFilterBufferRef *picref)
{
    if (!picref || !picref->video || !frame)
        return AVERROR(EINVAL);

    memcpy(frame->data,     picref->data,     sizeof(frame->data));
    memcpy(frame->linesize, picref->linesize, sizeof(frame->linesize));
    av_frame_set_pkt_pos(frame, picref->pos);
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
