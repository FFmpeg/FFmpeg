/*
 * Filter layer - default implementations
 * Copyright (c) 2007 Bobby Bingham
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

#include "libavutil/audioconvert.h"
#include "libavutil/imgutils.h"
#include "libavutil/samplefmt.h"
#include "avfilter.h"
#include "internal.h"

/* TODO: buffer pool.  see comment for avfilter_default_get_video_buffer() */
void ff_avfilter_default_free_buffer(AVFilterBuffer *ptr)
{
    av_free(ptr->data[0]);
    av_free(ptr);
}

/* TODO: set the buffer's priv member to a context structure for the whole
 * filter chain.  This will allow for a buffer pool instead of the constant
 * alloc & free cycle currently implemented. */
AVFilterBufferRef *avfilter_default_get_video_buffer(AVFilterLink *link, int perms, int w, int h)
{
    int linesize[4];
    uint8_t *data[4];
    AVFilterBufferRef *picref = NULL;

    // +2 is needed for swscaler, +16 to be SIMD-friendly
    if (av_image_alloc(data, linesize, w, h, link->format, 16) < 0)
        return NULL;

    picref = avfilter_get_video_buffer_ref_from_arrays(data, linesize,
                                                       perms, w, h, link->format);
    if (!picref) {
        av_free(data[0]);
        return NULL;
    }

    return picref;
}

AVFilterBufferRef *avfilter_default_get_audio_buffer(AVFilterLink *link, int perms,
                                                     enum AVSampleFormat sample_fmt, int size,
                                                     int64_t channel_layout, int planar)
{
    AVFilterBuffer *samples = av_mallocz(sizeof(AVFilterBuffer));
    AVFilterBufferRef *ref = NULL;
    int i, sample_size, chans_nb, bufsize, per_channel_size, step_size = 0;
    char *buf;

    if (!samples || !(ref = av_mallocz(sizeof(AVFilterBufferRef))))
        goto fail;

    ref->buf                   = samples;
    ref->format                = sample_fmt;

    ref->audio = av_mallocz(sizeof(AVFilterBufferRefAudioProps));
    if (!ref->audio)
        goto fail;

    ref->audio->channel_layout = channel_layout;
    ref->audio->size           = size;
    ref->audio->planar         = planar;

    /* make sure the buffer gets read permission or it's useless for output */
    ref->perms = perms | AV_PERM_READ;

    samples->refcount   = 1;
    samples->free       = ff_avfilter_default_free_buffer;

    sample_size = av_get_bits_per_sample_fmt(sample_fmt) >>3;
    chans_nb = av_get_channel_layout_nb_channels(channel_layout);

    per_channel_size = size/chans_nb;
    ref->audio->nb_samples = per_channel_size/sample_size;

    /* Set the number of bytes to traverse to reach next sample of a particular channel:
     * For planar, this is simply the sample size.
     * For packed, this is the number of samples * sample_size.
     */
    for (i = 0; i < chans_nb; i++)
        samples->linesize[i] = planar > 0 ? per_channel_size : sample_size;
    memset(&samples->linesize[chans_nb], 0, (8-chans_nb) * sizeof(samples->linesize[0]));

    /* Calculate total buffer size, round to multiple of 16 to be SIMD friendly */
    bufsize = (size + 15)&~15;
    buf = av_malloc(bufsize);
    if (!buf)
        goto fail;

    /* For planar, set the start point of each channel's data within the buffer
     * For packed, set the start point of the entire buffer only
     */
    samples->data[0] = buf;
    if (buf && planar) {
        for (i = 1; i < chans_nb; i++) {
            step_size += per_channel_size;
            samples->data[i] = buf + step_size;
        }
    } else {
        for (i = 1; i < chans_nb; i++)
            samples->data[i] = buf;
    }

    memset(&samples->data[chans_nb], 0, (8-chans_nb) * sizeof(samples->data[0]));

    memcpy(ref->data,     samples->data,     sizeof(ref->data));
    memcpy(ref->linesize, samples->linesize, sizeof(ref->linesize));

    return ref;

fail:
    if (ref)
        av_free(ref->audio);
    av_free(ref);
    av_free(samples);
    return NULL;
}

void avfilter_default_start_frame(AVFilterLink *inlink, AVFilterBufferRef *picref)
{
    AVFilterLink *outlink = NULL;

    if (inlink->dst->output_count)
        outlink = inlink->dst->outputs[0];

    if (outlink) {
        outlink->out_buf = avfilter_get_video_buffer(outlink, AV_PERM_WRITE, outlink->w, outlink->h);
        avfilter_copy_buffer_ref_props(outlink->out_buf, picref);
        avfilter_start_frame(outlink, avfilter_ref_buffer(outlink->out_buf, ~0));
    }
}

void avfilter_default_draw_slice(AVFilterLink *inlink, int y, int h, int slice_dir)
{
    AVFilterLink *outlink = NULL;

    if (inlink->dst->output_count)
        outlink = inlink->dst->outputs[0];

    if (outlink)
        avfilter_draw_slice(outlink, y, h, slice_dir);
}

void avfilter_default_end_frame(AVFilterLink *inlink)
{
    AVFilterLink *outlink = NULL;

    if (inlink->dst->output_count)
        outlink = inlink->dst->outputs[0];

    avfilter_unref_buffer(inlink->cur_buf);
    inlink->cur_buf = NULL;

    if (outlink) {
        if (outlink->out_buf) {
            avfilter_unref_buffer(outlink->out_buf);
            outlink->out_buf = NULL;
        }
        avfilter_end_frame(outlink);
    }
}

/* FIXME: samplesref is same as link->cur_buf. Need to consider removing the redundant parameter. */
void avfilter_default_filter_samples(AVFilterLink *inlink, AVFilterBufferRef *samplesref)
{
    AVFilterLink *outlink = NULL;

    if (inlink->dst->output_count)
        outlink = inlink->dst->outputs[0];

    if (outlink) {
        outlink->out_buf = avfilter_default_get_audio_buffer(inlink, AV_PERM_WRITE, samplesref->format,
                                                             samplesref->audio->size,
                                                             samplesref->audio->channel_layout,
                                                             samplesref->audio->planar);
        outlink->out_buf->pts                = samplesref->pts;
        outlink->out_buf->audio->sample_rate = samplesref->audio->sample_rate;
        avfilter_filter_samples(outlink, avfilter_ref_buffer(outlink->out_buf, ~0));
        avfilter_unref_buffer(outlink->out_buf);
        outlink->out_buf = NULL;
    }
    avfilter_unref_buffer(samplesref);
    inlink->cur_buf = NULL;
}

/**
 * default config_link() implementation for output video links to simplify
 * the implementation of one input one output video filters */
int avfilter_default_config_output_link(AVFilterLink *link)
{
    if (link->src->input_count && link->src->inputs[0]) {
        if (link->type == AVMEDIA_TYPE_VIDEO) {
            link->w = link->src->inputs[0]->w;
            link->h = link->src->inputs[0]->h;
            link->time_base = link->src->inputs[0]->time_base;
        } else if (link->type == AVMEDIA_TYPE_AUDIO) {
            link->channel_layout = link->src->inputs[0]->channel_layout;
            link->sample_rate    = link->src->inputs[0]->sample_rate;
        }
    } else {
        /* XXX: any non-simple filter which would cause this branch to be taken
         * really should implement its own config_props() for this link. */
        return -1;
    }

    return 0;
}

/**
 * A helper for query_formats() which sets all links to the same list of
 * formats. If there are no links hooked to this filter, the list of formats is
 * freed.
 *
 * FIXME: this will need changed for filters with a mix of pad types
 * (video + audio, etc)
 */
void avfilter_set_common_formats(AVFilterContext *ctx, AVFilterFormats *formats)
{
    int count = 0, i;

    for (i = 0; i < ctx->input_count; i++) {
        if (ctx->inputs[i]) {
            avfilter_formats_ref(formats, &ctx->inputs[i]->out_formats);
            count++;
        }
    }
    for (i = 0; i < ctx->output_count; i++) {
        if (ctx->outputs[i]) {
            avfilter_formats_ref(formats, &ctx->outputs[i]->in_formats);
            count++;
        }
    }

    if (!count) {
        av_free(formats->formats);
        av_free(formats->refs);
        av_free(formats);
    }
}

int avfilter_default_query_formats(AVFilterContext *ctx)
{
    enum AVMediaType type = ctx->inputs  && ctx->inputs [0] ? ctx->inputs [0]->type :
                            ctx->outputs && ctx->outputs[0] ? ctx->outputs[0]->type :
                            AVMEDIA_TYPE_VIDEO;

    avfilter_set_common_formats(ctx, avfilter_all_formats(type));
    return 0;
}

void avfilter_null_start_frame(AVFilterLink *link, AVFilterBufferRef *picref)
{
    avfilter_start_frame(link->dst->outputs[0], picref);
}

void avfilter_null_draw_slice(AVFilterLink *link, int y, int h, int slice_dir)
{
    avfilter_draw_slice(link->dst->outputs[0], y, h, slice_dir);
}

void avfilter_null_end_frame(AVFilterLink *link)
{
    avfilter_end_frame(link->dst->outputs[0]);
}

void avfilter_null_filter_samples(AVFilterLink *link, AVFilterBufferRef *samplesref)
{
    avfilter_filter_samples(link->dst->outputs[0], samplesref);
}

AVFilterBufferRef *avfilter_null_get_video_buffer(AVFilterLink *link, int perms, int w, int h)
{
    return avfilter_get_video_buffer(link->dst->outputs[0], perms, w, h);
}

AVFilterBufferRef *avfilter_null_get_audio_buffer(AVFilterLink *link, int perms,
                                                  enum AVSampleFormat sample_fmt, int size,
                                                  int64_t channel_layout, int packed)
{
    return avfilter_get_audio_buffer(link->dst->outputs[0], perms, sample_fmt,
                                     size, channel_layout, packed);
}

