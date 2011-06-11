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

void avfilter_copy_frame_props(AVFilterBufferRef *dst, const AVFrame *src)
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
    }
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

    return 0;
}
