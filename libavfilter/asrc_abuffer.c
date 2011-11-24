/*
 * Copyright (c) 2010 S.N. Hemanth Meenakshisundaram
 * Copyright (c) 2011 Mina Nagy Zaki
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
 * memory buffer source for audio
 */

#include "libavutil/audioconvert.h"
#include "libavutil/avstring.h"
#include "libavutil/fifo.h"
#include "asrc_abuffer.h"
#include "internal.h"

typedef struct {
    // Audio format of incoming buffers
    int sample_rate;
    unsigned int sample_format;
    int64_t channel_layout;
    int packing_format;

    // FIFO buffer of audio buffer ref pointers
    AVFifoBuffer *fifo;

    // Normalization filters
    AVFilterContext *aconvert;
    AVFilterContext *aresample;
} ABufferSourceContext;

#define FIFO_SIZE 8

static void buf_free(AVFilterBuffer *ptr)
{
    av_free(ptr);
    return;
}

static void set_link_source(AVFilterContext *src, AVFilterLink *link)
{
    link->src       = src;
    link->srcpad    = &(src->output_pads[0]);
    src->outputs[0] = link;
}

static int reconfigure_filter(ABufferSourceContext *abuffer, AVFilterContext *filt_ctx)
{
    int ret;
    AVFilterLink * const inlink  = filt_ctx->inputs[0];
    AVFilterLink * const outlink = filt_ctx->outputs[0];

    inlink->format         = abuffer->sample_format;
    inlink->channel_layout = abuffer->channel_layout;
    inlink->planar         = abuffer->packing_format;
    inlink->sample_rate    = abuffer->sample_rate;

    filt_ctx->filter->uninit(filt_ctx);
    memset(filt_ctx->priv, 0, filt_ctx->filter->priv_size);
    if ((ret = filt_ctx->filter->init(filt_ctx, NULL , NULL)) < 0)
        return ret;
    if ((ret = inlink->srcpad->config_props(inlink)) < 0)
        return ret;
    return outlink->srcpad->config_props(outlink);
}

static int insert_filter(ABufferSourceContext *abuffer,
                         AVFilterLink *link, AVFilterContext **filt_ctx,
                         const char *filt_name)
{
    int ret;

    if ((ret = avfilter_open(filt_ctx, avfilter_get_by_name(filt_name), NULL)) < 0)
        return ret;

    link->src->outputs[0] = NULL;
    if ((ret = avfilter_link(link->src, 0, *filt_ctx, 0)) < 0) {
        link->src->outputs[0] = link;
        return ret;
    }

    set_link_source(*filt_ctx, link);

    if ((ret = reconfigure_filter(abuffer, *filt_ctx)) < 0) {
        avfilter_free(*filt_ctx);
        return ret;
    }

    return 0;
}

static void remove_filter(AVFilterContext **filt_ctx)
{
    AVFilterLink *outlink = (*filt_ctx)->outputs[0];
    AVFilterContext *src  = (*filt_ctx)->inputs[0]->src;

    (*filt_ctx)->outputs[0] = NULL;
    avfilter_free(*filt_ctx);
    *filt_ctx = NULL;

    set_link_source(src, outlink);
}

static inline void log_input_change(void *ctx, AVFilterLink *link, AVFilterBufferRef *ref)
{
    char old_layout_str[16], new_layout_str[16];
    av_get_channel_layout_string(old_layout_str, sizeof(old_layout_str),
                                 -1, link->channel_layout);
    av_get_channel_layout_string(new_layout_str, sizeof(new_layout_str),
                                 -1, ref->audio->channel_layout);
    av_log(ctx, AV_LOG_INFO,
           "Audio input format changed: "
           "%s:%s:%d -> %s:%s:%d, normalizing\n",
           av_get_sample_fmt_name(link->format),
           old_layout_str, (int)link->sample_rate,
           av_get_sample_fmt_name(ref->format),
           new_layout_str, ref->audio->sample_rate);
}

int av_asrc_buffer_add_audio_buffer_ref(AVFilterContext *ctx,
                                        AVFilterBufferRef *samplesref,
                                        int av_unused flags)
{
    ABufferSourceContext *abuffer = ctx->priv;
    AVFilterLink *link;
    int ret, logged = 0;

    if (av_fifo_space(abuffer->fifo) < sizeof(samplesref)) {
        av_log(ctx, AV_LOG_ERROR,
               "Buffering limit reached. Please consume some available frames "
               "before adding new ones.\n");
        return AVERROR(EINVAL);
    }

    // Normalize input

    link = ctx->outputs[0];
    if (samplesref->audio->sample_rate != link->sample_rate) {

        log_input_change(ctx, link, samplesref);
        logged = 1;

        abuffer->sample_rate = samplesref->audio->sample_rate;

        if (!abuffer->aresample) {
            ret = insert_filter(abuffer, link, &abuffer->aresample, "aresample");
            if (ret < 0) return ret;
        } else {
            link = abuffer->aresample->outputs[0];
            if (samplesref->audio->sample_rate == link->sample_rate)
                remove_filter(&abuffer->aresample);
            else
                if ((ret = reconfigure_filter(abuffer, abuffer->aresample)) < 0)
                    return ret;
        }
    }

    link = ctx->outputs[0];
    if (samplesref->format                != link->format         ||
        samplesref->audio->channel_layout != link->channel_layout ||
        samplesref->audio->planar         != link->planar) {

        if (!logged) log_input_change(ctx, link, samplesref);

        abuffer->sample_format  = samplesref->format;
        abuffer->channel_layout = samplesref->audio->channel_layout;
        abuffer->packing_format = samplesref->audio->planar;

        if (!abuffer->aconvert) {
            ret = insert_filter(abuffer, link, &abuffer->aconvert, "aconvert");
            if (ret < 0) return ret;
        } else {
            link = abuffer->aconvert->outputs[0];
            if (samplesref->format                == link->format         &&
                samplesref->audio->channel_layout == link->channel_layout &&
                samplesref->audio->planar         == link->planar
               )
                remove_filter(&abuffer->aconvert);
            else
                if ((ret = reconfigure_filter(abuffer, abuffer->aconvert)) < 0)
                    return ret;
        }
    }

    if (sizeof(samplesref) != av_fifo_generic_write(abuffer->fifo, &samplesref,
                                                    sizeof(samplesref), NULL)) {
        av_log(ctx, AV_LOG_ERROR, "Error while writing to FIFO\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

int av_asrc_buffer_add_samples(AVFilterContext *ctx,
                               uint8_t *data[8], int linesize[8],
                               int nb_samples, int sample_rate,
                               int sample_fmt, int64_t channel_layout, int planar,
                               int64_t pts, int av_unused flags)
{
    AVFilterBufferRef *samplesref;

    samplesref = avfilter_get_audio_buffer_ref_from_arrays(
                     data, linesize, AV_PERM_WRITE,
                     nb_samples,
                     sample_fmt, channel_layout, planar);
    if (!samplesref)
        return AVERROR(ENOMEM);

    samplesref->buf->free  = buf_free;
    samplesref->pts = pts;
    samplesref->audio->sample_rate = sample_rate;

    return av_asrc_buffer_add_audio_buffer_ref(ctx, samplesref, 0);
}

int av_asrc_buffer_add_buffer(AVFilterContext *ctx,
                              uint8_t *buf, int buf_size, int sample_rate,
                              int sample_fmt, int64_t channel_layout, int planar,
                              int64_t pts, int av_unused flags)
{
    uint8_t *data[8];
    int linesize[8];
    int nb_channels = av_get_channel_layout_nb_channels(channel_layout),
        nb_samples  = buf_size / nb_channels / av_get_bytes_per_sample(sample_fmt);

    av_samples_fill_arrays(data, linesize,
                           buf, nb_channels, nb_samples,
                           sample_fmt, 16);

    return av_asrc_buffer_add_samples(ctx,
                                      data, linesize, nb_samples,
                                      sample_rate,
                                      sample_fmt, channel_layout, planar,
                                      pts, flags);
}

static av_cold int init(AVFilterContext *ctx, const char *args0, void *opaque)
{
    ABufferSourceContext *abuffer = ctx->priv;
    char *arg = NULL, *ptr, chlayout_str[16];
    char *args = av_strdup(args0);
    int ret;

    arg = av_strtok(args, ":", &ptr);

#define ADD_FORMAT(fmt_name)                                            \
    if (!arg)                                                           \
        goto arg_fail;                                                  \
    if ((ret = ff_parse_##fmt_name(&abuffer->fmt_name, arg, ctx)) < 0) { \
        av_freep(&args);                                                \
        return ret;                                                     \
    }                                                                   \
    if (*args)                                                          \
        arg = av_strtok(NULL, ":", &ptr)

    ADD_FORMAT(sample_rate);
    ADD_FORMAT(sample_format);
    ADD_FORMAT(channel_layout);
    ADD_FORMAT(packing_format);

    abuffer->fifo = av_fifo_alloc(FIFO_SIZE*sizeof(AVFilterBufferRef*));
    if (!abuffer->fifo) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate fifo, filter init failed.\n");
        return AVERROR(ENOMEM);
    }

    av_get_channel_layout_string(chlayout_str, sizeof(chlayout_str),
                                 -1, abuffer->channel_layout);
    av_log(ctx, AV_LOG_INFO, "format:%s layout:%s rate:%d\n",
           av_get_sample_fmt_name(abuffer->sample_format), chlayout_str,
           abuffer->sample_rate);
    av_freep(&args);

    return 0;

arg_fail:
    av_log(ctx, AV_LOG_ERROR, "Invalid arguments, must be of the form "
                              "sample_rate:sample_fmt:channel_layout:packing\n");
    av_freep(&args);
    return AVERROR(EINVAL);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ABufferSourceContext *abuffer = ctx->priv;
    av_fifo_free(abuffer->fifo);
}

static int query_formats(AVFilterContext *ctx)
{
    ABufferSourceContext *abuffer = ctx->priv;
    AVFilterFormats *formats;

    formats = NULL;
    avfilter_add_format(&formats, abuffer->sample_format);
    avfilter_set_common_sample_formats(ctx, formats);

    formats = NULL;
    avfilter_add_format(&formats, abuffer->channel_layout);
    avfilter_set_common_channel_layouts(ctx, formats);

    formats = NULL;
    avfilter_add_format(&formats, abuffer->packing_format);
    avfilter_set_common_packing_formats(ctx, formats);

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    ABufferSourceContext *abuffer = outlink->src->priv;
    outlink->sample_rate = abuffer->sample_rate;
    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    ABufferSourceContext *abuffer = outlink->src->priv;
    AVFilterBufferRef *samplesref;

    if (!av_fifo_size(abuffer->fifo)) {
        av_log(outlink->src, AV_LOG_ERROR,
               "request_frame() called with no available frames!\n");
        return AVERROR(EINVAL);
    }

    av_fifo_generic_read(abuffer->fifo, &samplesref, sizeof(samplesref), NULL);
    avfilter_filter_samples(outlink, avfilter_ref_buffer(samplesref, ~0));
    avfilter_unref_buffer(samplesref);

    return 0;
}

static int poll_frame(AVFilterLink *outlink)
{
    ABufferSourceContext *abuffer = outlink->src->priv;
    return av_fifo_size(abuffer->fifo)/sizeof(AVFilterBufferRef*);
}

AVFilter avfilter_asrc_abuffer = {
    .name        = "abuffer",
    .description = NULL_IF_CONFIG_SMALL("Buffer audio frames, and make them accessible to the filterchain."),
    .priv_size   = sizeof(ABufferSourceContext),
    .query_formats = query_formats,

    .init        = init,
    .uninit      = uninit,

    .inputs      = (const AVFilterPad[]) {{ .name = NULL }},
    .outputs     = (const AVFilterPad[]) {{ .name      = "default",
                                      .type            = AVMEDIA_TYPE_AUDIO,
                                      .request_frame   = request_frame,
                                      .poll_frame      = poll_frame,
                                      .config_props    = config_output, },
                                    { .name = NULL}},
};
