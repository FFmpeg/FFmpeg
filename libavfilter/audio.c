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

int avfilter_ref_get_channels(AVFilterBufferRef *ref)
{
    return ref->audio ? ref->audio->channels : 0;
}

AVFilterBufferRef *ff_null_get_audio_buffer(AVFilterLink *link, int perms,
                                            int nb_samples)
{
    return ff_get_audio_buffer(link->dst->outputs[0], perms, nb_samples);
}

AVFilterBufferRef *ff_default_get_audio_buffer(AVFilterLink *link, int perms,
                                               int nb_samples)
{
    AVFilterBufferRef *samplesref = NULL;
    uint8_t **data;
    int planar      = av_sample_fmt_is_planar(link->format);
    int nb_channels = link->channels;
    int planes      = planar ? nb_channels : 1;
    int linesize;
    int full_perms = AV_PERM_READ | AV_PERM_WRITE | AV_PERM_PRESERVE |
                     AV_PERM_REUSE | AV_PERM_REUSE2 | AV_PERM_ALIGN;

    av_assert1(!(perms & ~(full_perms | AV_PERM_NEG_LINESIZES)));

    if (!(data = av_mallocz(sizeof(*data) * planes)))
        goto fail;

    if (av_samples_alloc(data, &linesize, nb_channels, nb_samples, link->format, 0) < 0)
        goto fail;

    samplesref = avfilter_get_audio_buffer_ref_from_arrays_channels(
        data, linesize, full_perms, nb_samples, link->format,
        link->channels, link->channel_layout);
    if (!samplesref)
        goto fail;

    samplesref->audio->sample_rate = link->sample_rate;

    av_freep(&data);

fail:
    if (data)
        av_freep(&data[0]);
    av_freep(&data);
    return samplesref;
}

AVFilterBufferRef *ff_get_audio_buffer(AVFilterLink *link, int perms,
                                       int nb_samples)
{
    AVFilterBufferRef *ret = NULL;

    if (link->dstpad->get_audio_buffer)
        ret = link->dstpad->get_audio_buffer(link, perms, nb_samples);

    if (!ret)
        ret = ff_default_get_audio_buffer(link, perms, nb_samples);

    if (ret)
        ret->type = AVMEDIA_TYPE_AUDIO;

    return ret;
}

AVFilterBufferRef* avfilter_get_audio_buffer_ref_from_arrays_channels(uint8_t **data,
                                                                      int linesize,
                                                                      int perms,
                                                                      int nb_samples,
                                                                      enum AVSampleFormat sample_fmt,
                                                                      int channels,
                                                                      uint64_t channel_layout)
{
    int planes;
    AVFilterBuffer    *samples    = av_mallocz(sizeof(*samples));
    AVFilterBufferRef *samplesref = av_mallocz(sizeof(*samplesref));

    if (!samples || !samplesref)
        goto fail;

    av_assert0(channels);
    av_assert0(channel_layout == 0 ||
               channels == av_get_channel_layout_nb_channels(channel_layout));

    samplesref->buf         = samples;
    samplesref->buf->free   = ff_avfilter_default_free_buffer;
    if (!(samplesref->audio = av_mallocz(sizeof(*samplesref->audio))))
        goto fail;

    samplesref->audio->nb_samples     = nb_samples;
    samplesref->audio->channel_layout = channel_layout;
    samplesref->audio->channels       = channels;

    planes = av_sample_fmt_is_planar(sample_fmt) ? channels : 1;

    /* make sure the buffer gets read permission or it's useless for output */
    samplesref->perms = perms | AV_PERM_READ;

    samples->refcount  = 1;
    samplesref->type   = AVMEDIA_TYPE_AUDIO;
    samplesref->format = sample_fmt;

    memcpy(samples->data, data,
           FFMIN(FF_ARRAY_ELEMS(samples->data), planes)*sizeof(samples->data[0]));
    memcpy(samplesref->data, samples->data, sizeof(samples->data));

    samples->linesize[0] = samplesref->linesize[0] = linesize;

    if (planes > FF_ARRAY_ELEMS(samples->data)) {
        samples->   extended_data = av_mallocz(sizeof(*samples->extended_data) *
                                               planes);
        samplesref->extended_data = av_mallocz(sizeof(*samplesref->extended_data) *
                                               planes);

        if (!samples->extended_data || !samplesref->extended_data)
            goto fail;

        memcpy(samples->   extended_data, data, sizeof(*data)*planes);
        memcpy(samplesref->extended_data, data, sizeof(*data)*planes);
    } else {
        samples->extended_data    = samples->data;
        samplesref->extended_data = samplesref->data;
    }

    samplesref->pts = AV_NOPTS_VALUE;

    return samplesref;

fail:
    if (samples && samples->extended_data != samples->data)
        av_freep(&samples->extended_data);
    if (samplesref) {
        av_freep(&samplesref->audio);
        if (samplesref->extended_data != samplesref->data)
            av_freep(&samplesref->extended_data);
    }
    av_freep(&samplesref);
    av_freep(&samples);
    return NULL;
}

AVFilterBufferRef* avfilter_get_audio_buffer_ref_from_arrays(uint8_t **data,
                                                             int linesize,int perms,
                                                             int nb_samples,
                                                             enum AVSampleFormat sample_fmt,
                                                             uint64_t channel_layout)
{
    int channels = av_get_channel_layout_nb_channels(channel_layout);
    return avfilter_get_audio_buffer_ref_from_arrays_channels(data, linesize, perms,
                                                              nb_samples, sample_fmt,
                                                              channels, channel_layout);
}
