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
#include "libavutil/audioconvert.h"

#include "audio.h"
#include "avfilter.h"
#include "internal.h"

AVFilterBufferRef *ff_null_get_audio_buffer(AVFilterLink *link, int perms,
                                            int nb_samples)
{
    return ff_get_audio_buffer(link->dst->outputs[0], perms, nb_samples);
}

AVFilterBufferRef *ff_default_get_audio_buffer(AVFilterLink *link, int perms,
                                                     int nb_samples)
{
    AVFilterBufferRef *samplesref = NULL;
    int linesize[8] = {0};
    uint8_t *data[8] = {0};
    int ch, nb_channels = av_get_channel_layout_nb_channels(link->channel_layout);

    /* right now we don't support more than 8 channels */
    av_assert0(nb_channels <= 8);

    /* Calculate total buffer size, round to multiple of 16 to be SIMD friendly */
    if (av_samples_alloc(data, linesize,
                         nb_channels, nb_samples,
                         av_get_alt_sample_fmt(link->format, link->planar),
                         16) < 0)
        return NULL;

    for (ch = 1; link->planar && ch < nb_channels; ch++)
        linesize[ch] = linesize[0];
    samplesref =
        avfilter_get_audio_buffer_ref_from_arrays(data, linesize, perms,
                                                  nb_samples, link->format,
                                                  link->channel_layout, link->planar);
    if (!samplesref) {
        av_free(data[0]);
        return NULL;
    }

    return samplesref;
}

static AVFilterBufferRef *ff_default_get_audio_buffer_alt(AVFilterLink *link, int perms,
                                               int nb_samples)
{
    AVFilterBufferRef *samplesref = NULL;
    uint8_t **data;
    int planar      = av_sample_fmt_is_planar(link->format);
    int nb_channels = av_get_channel_layout_nb_channels(link->channel_layout);
    int planes      = planar ? nb_channels : 1;
    int linesize;

    if (!(data = av_mallocz(sizeof(*data) * planes)))
        goto fail;

    if (av_samples_alloc(data, &linesize, nb_channels, nb_samples, link->format, 0) < 0)
        goto fail;

    samplesref = avfilter_get_audio_buffer_ref_from_arrays_alt(data, linesize, perms,
                                                           nb_samples, link->format,
                                                           link->channel_layout);
    if (!samplesref)
        goto fail;

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

AVFilterBufferRef *
avfilter_get_audio_buffer_ref_from_arrays(uint8_t *data[8], int linesize[8], int perms,
                                          int nb_samples, enum AVSampleFormat sample_fmt,
                                          uint64_t channel_layout, int planar)
{
    AVFilterBuffer *samples = av_mallocz(sizeof(AVFilterBuffer));
    AVFilterBufferRef *samplesref = av_mallocz(sizeof(AVFilterBufferRef));

    if (!samples || !samplesref)
        goto fail;

    samplesref->buf = samples;
    samplesref->buf->free = ff_avfilter_default_free_buffer;
    if (!(samplesref->audio = av_mallocz(sizeof(AVFilterBufferRefAudioProps))))
        goto fail;

    samplesref->audio->nb_samples     = nb_samples;
    samplesref->audio->channel_layout = channel_layout;
    samplesref->audio->planar         = planar;

    /* make sure the buffer gets read permission or it's useless for output */
    samplesref->perms = perms | AV_PERM_READ;

    samples->refcount = 1;
    samplesref->type = AVMEDIA_TYPE_AUDIO;
    samplesref->format = sample_fmt;

    memcpy(samples->data,        data,     sizeof(samples->data));
    memcpy(samples->linesize,    linesize, sizeof(samples->linesize));
    memcpy(samplesref->data,     data,     sizeof(samplesref->data));
    memcpy(samplesref->linesize, linesize, sizeof(samplesref->linesize));

    return samplesref;

fail:
    if (samplesref && samplesref->audio)
        av_freep(&samplesref->audio);
    av_freep(&samplesref);
    av_freep(&samples);
    return NULL;
}

AVFilterBufferRef* avfilter_get_audio_buffer_ref_from_arrays_alt(uint8_t **data,
                                                             int linesize,int perms,
                                                             int nb_samples,
                                                             enum AVSampleFormat sample_fmt,
                                                             uint64_t channel_layout)
{
    int planes;
    AVFilterBuffer    *samples    = av_mallocz(sizeof(*samples));
    AVFilterBufferRef *samplesref = av_mallocz(sizeof(*samplesref));

    if (!samples || !samplesref)
        goto fail;

    samplesref->buf         = samples;
    samplesref->buf->free   = ff_avfilter_default_free_buffer;
    if (!(samplesref->audio = av_mallocz(sizeof(*samplesref->audio))))
        goto fail;

    samplesref->audio->nb_samples     = nb_samples;
    samplesref->audio->channel_layout = channel_layout;
    samplesref->audio->planar         = av_sample_fmt_is_planar(sample_fmt);

    planes = samplesref->audio->planar ? av_get_channel_layout_nb_channels(channel_layout) : 1;

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

void ff_null_filter_samples(AVFilterLink *link, AVFilterBufferRef *samplesref)
{
    ff_filter_samples(link->dst->outputs[0], samplesref);
}

/* FIXME: samplesref is same as link->cur_buf. Need to consider removing the redundant parameter. */
void ff_default_filter_samples(AVFilterLink *inlink, AVFilterBufferRef *samplesref)
{
    AVFilterLink *outlink = NULL;

    if (inlink->dst->output_count)
        outlink = inlink->dst->outputs[0];

    if (outlink) {
        outlink->out_buf = ff_default_get_audio_buffer(inlink, AV_PERM_WRITE,
                                                       samplesref->audio->nb_samples);
        outlink->out_buf->pts                = samplesref->pts;
        outlink->out_buf->audio->sample_rate = samplesref->audio->sample_rate;
        ff_filter_samples(outlink, avfilter_ref_buffer(outlink->out_buf, ~0));
        avfilter_unref_buffer(outlink->out_buf);
        outlink->out_buf = NULL;
    }
    avfilter_unref_buffer(samplesref);
    inlink->cur_buf = NULL;
}

void ff_filter_samples(AVFilterLink *link, AVFilterBufferRef *samplesref)
{
    void (*filter_samples)(AVFilterLink *, AVFilterBufferRef *);
    AVFilterPad *dst = link->dstpad;
    int64_t pts;

    FF_DPRINTF_START(NULL, filter_samples); ff_dlog_link(NULL, link, 1);

    if (!(filter_samples = dst->filter_samples))
        filter_samples = ff_default_filter_samples;

    /* prepare to copy the samples if the buffer has insufficient permissions */
    if ((dst->min_perms & samplesref->perms) != dst->min_perms ||
        dst->rej_perms & samplesref->perms) {
        int  i, planar = av_sample_fmt_is_planar(samplesref->format);
        int planes = !planar ? 1:
                     av_get_channel_layout_nb_channels(samplesref->audio->channel_layout);

        av_log(link->dst, AV_LOG_DEBUG,
               "Copying audio data in avfilter (have perms %x, need %x, reject %x)\n",
               samplesref->perms, link->dstpad->min_perms, link->dstpad->rej_perms);

        link->cur_buf = ff_default_get_audio_buffer(link, dst->min_perms,
                                                    samplesref->audio->nb_samples);
        link->cur_buf->pts                = samplesref->pts;
        link->cur_buf->audio->sample_rate = samplesref->audio->sample_rate;

        /* Copy actual data into new samples buffer */
        for (i = 0; samplesref->data[i] && i < 8; i++)
            memcpy(link->cur_buf->data[i], samplesref->data[i], samplesref->linesize[0]);
        for (i = 0; i < planes; i++)
            memcpy(link->cur_buf->extended_data[i], samplesref->extended_data[i], samplesref->linesize[0]);

        avfilter_unref_buffer(samplesref);
    } else
        link->cur_buf = samplesref;

    pts = link->cur_buf->pts;
    filter_samples(link, link->cur_buf);
    ff_update_link_current_pts(link, pts);
}
