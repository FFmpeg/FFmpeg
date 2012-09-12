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
#include "libavutil/common.h"

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
    uint8_t **data;
    int planar      = av_sample_fmt_is_planar(link->format);
    int nb_channels = av_get_channel_layout_nb_channels(link->channel_layout);
    int planes      = planar ? nb_channels : 1;
    int linesize;
    int full_perms = AV_PERM_READ | AV_PERM_WRITE | AV_PERM_PRESERVE |
                     AV_PERM_REUSE | AV_PERM_REUSE2 | AV_PERM_ALIGN;

    av_assert1(!(perms & ~(full_perms | AV_PERM_NEG_LINESIZES)));

    if (!(data = av_mallocz(sizeof(*data) * planes)))
        goto fail;

    if (av_samples_alloc(data, &linesize, nb_channels, nb_samples, link->format, 0) < 0)
        goto fail;

    samplesref = avfilter_get_audio_buffer_ref_from_arrays(data, linesize, full_perms,
                                                           nb_samples, link->format,
                                                           link->channel_layout);
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

AVFilterBufferRef* avfilter_get_audio_buffer_ref_from_arrays(uint8_t **data,
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

    planes = av_sample_fmt_is_planar(sample_fmt) ?
        av_get_channel_layout_nb_channels(channel_layout) : 1;

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

static int default_filter_samples(AVFilterLink *link,
                                  AVFilterBufferRef *samplesref)
{
    return ff_filter_samples(link->dst->outputs[0], samplesref);
}

int ff_filter_samples_framed(AVFilterLink *link, AVFilterBufferRef *samplesref)
{
    int (*filter_samples)(AVFilterLink *, AVFilterBufferRef *);
    AVFilterPad *src = link->srcpad;
    AVFilterPad *dst = link->dstpad;
    int64_t pts;
    AVFilterBufferRef *buf_out;
    int ret;

    FF_TPRINTF_START(NULL, filter_samples); ff_tlog_link(NULL, link, 1);

    if (link->closed) {
        avfilter_unref_buffer(samplesref);
        return AVERROR_EOF;
    }

    if (!(filter_samples = dst->filter_samples))
        filter_samples = default_filter_samples;

    av_assert1((samplesref->perms & src->min_perms) == src->min_perms);
    samplesref->perms &= ~ src->rej_perms;

    /* prepare to copy the samples if the buffer has insufficient permissions */
    if ((dst->min_perms & samplesref->perms) != dst->min_perms ||
        dst->rej_perms & samplesref->perms) {
        av_log(link->dst, AV_LOG_DEBUG,
               "Copying audio data in avfilter (have perms %x, need %x, reject %x)\n",
               samplesref->perms, link->dstpad->min_perms, link->dstpad->rej_perms);

        buf_out = ff_default_get_audio_buffer(link, dst->min_perms,
                                              samplesref->audio->nb_samples);
        if (!buf_out) {
            avfilter_unref_buffer(samplesref);
            return AVERROR(ENOMEM);
        }
        buf_out->pts                = samplesref->pts;
        buf_out->audio->sample_rate = samplesref->audio->sample_rate;

        /* Copy actual data into new samples buffer */
        av_samples_copy(buf_out->extended_data, samplesref->extended_data,
                        0, 0, samplesref->audio->nb_samples,
                        av_get_channel_layout_nb_channels(link->channel_layout),
                        link->format);

        avfilter_unref_buffer(samplesref);
    } else
        buf_out = samplesref;

    link->cur_buf = buf_out;
    pts = buf_out->pts;
    ret = filter_samples(link, buf_out);
    ff_update_link_current_pts(link, pts);
    return ret;
}

int ff_filter_samples(AVFilterLink *link, AVFilterBufferRef *samplesref)
{
    int insamples = samplesref->audio->nb_samples, inpos = 0, nb_samples;
    AVFilterBufferRef *pbuf = link->partial_buf;
    int nb_channels = av_get_channel_layout_nb_channels(link->channel_layout);
    int ret = 0;

    av_assert1(samplesref->format                == link->format);
    av_assert1(samplesref->audio->channel_layout == link->channel_layout);
    av_assert1(samplesref->audio->sample_rate    == link->sample_rate);

    if (!link->min_samples ||
        (!pbuf &&
         insamples >= link->min_samples && insamples <= link->max_samples)) {
        return ff_filter_samples_framed(link, samplesref);
    }
    /* Handle framing (min_samples, max_samples) */
    while (insamples) {
        if (!pbuf) {
            AVRational samples_tb = { 1, link->sample_rate };
            int perms = link->dstpad->min_perms | AV_PERM_WRITE;
            pbuf = ff_get_audio_buffer(link, perms, link->partial_buf_size);
            if (!pbuf) {
                av_log(link->dst, AV_LOG_WARNING,
                       "Samples dropped due to memory allocation failure.\n");
                return 0;
            }
            avfilter_copy_buffer_ref_props(pbuf, samplesref);
            pbuf->pts = samplesref->pts +
                        av_rescale_q(inpos, samples_tb, link->time_base);
            pbuf->audio->nb_samples = 0;
        }
        nb_samples = FFMIN(insamples,
                           link->partial_buf_size - pbuf->audio->nb_samples);
        av_samples_copy(pbuf->extended_data, samplesref->extended_data,
                        pbuf->audio->nb_samples, inpos,
                        nb_samples, nb_channels, link->format);
        inpos                   += nb_samples;
        insamples               -= nb_samples;
        pbuf->audio->nb_samples += nb_samples;
        if (pbuf->audio->nb_samples >= link->min_samples) {
            ret = ff_filter_samples_framed(link, pbuf);
            pbuf = NULL;
        }
    }
    avfilter_unref_buffer(samplesref);
    link->partial_buf = pbuf;
    return ret;
}
