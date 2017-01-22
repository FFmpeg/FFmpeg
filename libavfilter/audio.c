/*
 * Copyright (c) Stefano Sabatini | stefasab at gmail.com
 * Copyright (c) S.N. Hemanth Meenakshisundaram | smeenaks at ucsd.edu
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

#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"

#include "audio.h"
#include "avfilter.h"
#include "internal.h"

#define BUFFER_ALIGN 0


AVFrame *ff_null_get_audio_buffer(AVFilterLink *link, int nb_samples)
{
    return ff_get_audio_buffer(link->dst->outputs[0], nb_samples);
}

AVFrame *ff_default_get_audio_buffer(AVFilterLink *link, int nb_samples)
{
    AVFrame *frame = NULL;
    int channels = link->channels;

    av_assert0(channels == av_get_channel_layout_nb_channels(link->channel_layout) || !av_get_channel_layout_nb_channels(link->channel_layout));

    if (!link->frame_pool) {
        link->frame_pool = ff_frame_pool_audio_init(av_buffer_allocz, channels,
                                                    nb_samples, link->format, BUFFER_ALIGN);
        if (!link->frame_pool)
            return NULL;
    } else {
        int pool_channels = 0;
        int pool_nb_samples = 0;
        int pool_align = 0;
        enum AVSampleFormat pool_format = AV_SAMPLE_FMT_NONE;

        if (ff_frame_pool_get_audio_config(link->frame_pool,
                                           &pool_channels, &pool_nb_samples,
                                           &pool_format, &pool_align) < 0) {
            return NULL;
        }

        if (pool_channels != channels || pool_nb_samples < nb_samples ||
            pool_format != link->format || pool_align != BUFFER_ALIGN) {

            ff_frame_pool_uninit((FFFramePool **)&link->frame_pool);
            link->frame_pool = ff_frame_pool_audio_init(av_buffer_allocz, channels,
                                                        nb_samples, link->format, BUFFER_ALIGN);
            if (!link->frame_pool)
                return NULL;
        }
    }

    frame = ff_frame_pool_get(link->frame_pool);
    if (!frame)
        return NULL;

    frame->nb_samples = nb_samples;
    frame->channel_layout = link->channel_layout;
    frame->sample_rate = link->sample_rate;

    av_samples_set_silence(frame->extended_data, 0, nb_samples, channels, link->format);

    return frame;
}

AVFrame *ff_get_audio_buffer(AVFilterLink *link, int nb_samples)
{
    AVFrame *ret = NULL;

    if (link->dstpad->get_audio_buffer)
        ret = link->dstpad->get_audio_buffer(link, nb_samples);

    if (!ret)
        ret = ff_default_get_audio_buffer(link, nb_samples);

    return ret;
}
