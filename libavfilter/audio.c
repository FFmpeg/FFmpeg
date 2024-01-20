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
#include "libavutil/cpu.h"

#include "audio.h"
#include "avfilter.h"
#include "avfilter_internal.h"
#include "framepool.h"
#include "internal.h"

const AVFilterPad ff_audio_default_filterpad[1] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    }
};

AVFrame *ff_null_get_audio_buffer(AVFilterLink *link, int nb_samples)
{
    return ff_get_audio_buffer(link->dst->outputs[0], nb_samples);
}

AVFrame *ff_default_get_audio_buffer(AVFilterLink *link, int nb_samples)
{
    AVFrame *frame = NULL;
    FilterLinkInternal *const li = ff_link_internal(link);
    int channels = link->ch_layout.nb_channels;
    int align = av_cpu_max_align();

    if (!li->frame_pool) {
        li->frame_pool = ff_frame_pool_audio_init(av_buffer_allocz, channels,
                                                  nb_samples, link->format, align);
        if (!li->frame_pool)
            return NULL;
    } else {
        int pool_channels = 0;
        int pool_nb_samples = 0;
        int pool_align = 0;
        enum AVSampleFormat pool_format = AV_SAMPLE_FMT_NONE;

        if (ff_frame_pool_get_audio_config(li->frame_pool,
                                           &pool_channels, &pool_nb_samples,
                                           &pool_format, &pool_align) < 0) {
            return NULL;
        }

        if (pool_channels != channels || pool_nb_samples < nb_samples ||
            pool_format != link->format || pool_align != align) {

            ff_frame_pool_uninit(&li->frame_pool);
            li->frame_pool = ff_frame_pool_audio_init(av_buffer_allocz, channels,
                                                      nb_samples, link->format, align);
            if (!li->frame_pool)
                return NULL;
        }
    }

    frame = ff_frame_pool_get(li->frame_pool);
    if (!frame)
        return NULL;

    frame->nb_samples = nb_samples;
    if (link->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC &&
        av_channel_layout_copy(&frame->ch_layout, &link->ch_layout) < 0) {
        av_frame_free(&frame);
        return NULL;
    }
    frame->sample_rate = link->sample_rate;

    av_samples_set_silence(frame->extended_data, 0, nb_samples, channels, link->format);

    return frame;
}

AVFrame *ff_get_audio_buffer(AVFilterLink *link, int nb_samples)
{
    AVFrame *ret = NULL;

    if (link->dstpad->get_buffer.audio)
        ret = link->dstpad->get_buffer.audio(link, nb_samples);

    if (!ret)
        ret = ff_default_get_audio_buffer(link, nb_samples);

    return ret;
}
