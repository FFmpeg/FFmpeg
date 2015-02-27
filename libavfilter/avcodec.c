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
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"

#if FF_API_AVFILTERBUFFER
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
        av_log(NULL, AV_LOG_ERROR, "Layout indicates a different number of channels than actually present\n");
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
            dst->extended_data = av_mallocz_array(planes, sizeof(*dst->extended_data));
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
#endif
