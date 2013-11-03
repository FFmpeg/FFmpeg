/*
 * Copyright (c) 2010 S.N. Hemanth Meenakshisundaram <smeenaks@ucsd.edu>
 * Copyright (c) 2011 Stefano Sabatini
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
 * sample format and channel layout conversion audio filter
 */

#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "libswresample/swresample.h"
#include "avfilter.h"
#include "audio.h"
#include "internal.h"

typedef struct {
    const AVClass       *class;
    enum AVSampleFormat  out_sample_fmt;
    int64_t              out_chlayout;
    struct SwrContext *swr;
    char *format_str;
    char *channel_layout_str;
} AConvertContext;

#define OFFSET(x) offsetof(AConvertContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM
static const AVOption aconvert_options[] = {
    { "sample_fmt",     "", OFFSET(format_str),         AV_OPT_TYPE_STRING, .flags = A|F },
    { "channel_layout", "", OFFSET(channel_layout_str), AV_OPT_TYPE_STRING, .flags = A|F },
    { NULL }
};

AVFILTER_DEFINE_CLASS(aconvert);

static av_cold int init(AVFilterContext *ctx)
{
    AConvertContext *aconvert = ctx->priv;
    int ret = 0;

    av_log(ctx, AV_LOG_WARNING, "This filter is deprecated, use aformat instead\n");

    aconvert->out_sample_fmt  = AV_SAMPLE_FMT_NONE;
    aconvert->out_chlayout    = 0;

    if (aconvert->format_str && strcmp(aconvert->format_str, "auto") &&
        (ret = ff_parse_sample_format(&aconvert->out_sample_fmt, aconvert->format_str, ctx)) < 0)
        return ret;
    if (aconvert->channel_layout_str && strcmp(aconvert->channel_layout_str, "auto"))
        return ff_parse_channel_layout(&aconvert->out_chlayout, NULL, aconvert->channel_layout_str, ctx);
    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AConvertContext *aconvert = ctx->priv;
    swr_free(&aconvert->swr);
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AConvertContext *aconvert = ctx->priv;
    AVFilterLink *inlink  = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    AVFilterChannelLayouts *layouts;

    ff_formats_ref(ff_all_formats(AVMEDIA_TYPE_AUDIO),
                         &inlink->out_formats);
    if (aconvert->out_sample_fmt != AV_SAMPLE_FMT_NONE) {
        formats = NULL;
        ff_add_format(&formats, aconvert->out_sample_fmt);
        ff_formats_ref(formats, &outlink->in_formats);
    } else
        ff_formats_ref(ff_all_formats(AVMEDIA_TYPE_AUDIO),
                             &outlink->in_formats);

    ff_channel_layouts_ref(ff_all_channel_layouts(),
                         &inlink->out_channel_layouts);
    if (aconvert->out_chlayout != 0) {
        layouts = NULL;
        ff_add_channel_layout(&layouts, aconvert->out_chlayout);
        ff_channel_layouts_ref(layouts, &outlink->in_channel_layouts);
    } else
        ff_channel_layouts_ref(ff_all_channel_layouts(),
                             &outlink->in_channel_layouts);

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    int ret;
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    AConvertContext *aconvert = ctx->priv;
    char buf1[64], buf2[64];

    /* if not specified in args, use the format and layout of the output */
    if (aconvert->out_sample_fmt == AV_SAMPLE_FMT_NONE)
        aconvert->out_sample_fmt = outlink->format;
    if (aconvert->out_chlayout   == 0)
        aconvert->out_chlayout   = outlink->channel_layout;

    aconvert->swr = swr_alloc_set_opts(aconvert->swr,
                                       aconvert->out_chlayout, aconvert->out_sample_fmt, inlink->sample_rate,
                                       inlink->channel_layout, inlink->format,           inlink->sample_rate,
                                       0, ctx);
    if (!aconvert->swr)
        return AVERROR(ENOMEM);
    ret = swr_init(aconvert->swr);
    if (ret < 0)
        return ret;

    av_get_channel_layout_string(buf1, sizeof(buf1),
                                 -1, inlink ->channel_layout);
    av_get_channel_layout_string(buf2, sizeof(buf2),
                                 -1, outlink->channel_layout);
    av_log(ctx, AV_LOG_VERBOSE,
           "fmt:%s cl:%s -> fmt:%s cl:%s\n",
           av_get_sample_fmt_name(inlink ->format), buf1,
           av_get_sample_fmt_name(outlink->format), buf2);

    return 0;
}

static int  filter_frame(AVFilterLink *inlink, AVFrame *insamplesref)
{
    AConvertContext *aconvert = inlink->dst->priv;
    const int n = insamplesref->nb_samples;
    AVFilterLink *const outlink = inlink->dst->outputs[0];
    AVFrame *outsamplesref = ff_get_audio_buffer(outlink, n);
    int ret;

    if (!outsamplesref)
        return AVERROR(ENOMEM);
    swr_convert(aconvert->swr, outsamplesref->extended_data, n,
                        (void *)insamplesref->extended_data, n);

    av_frame_copy_props(outsamplesref, insamplesref);
    av_frame_set_channels(outsamplesref, outlink->channels);
    outsamplesref->channel_layout = outlink->channel_layout;

    ret = ff_filter_frame(outlink, outsamplesref);
    av_frame_free(&insamplesref);
    return ret;
}

static const AVFilterPad aconvert_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad aconvert_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter ff_af_aconvert = {
    .name          = "aconvert",
    .description   = NULL_IF_CONFIG_SMALL("Convert the input audio to sample_fmt:channel_layout."),
    .priv_size     = sizeof(AConvertContext),
    .priv_class    = &aconvert_class,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = aconvert_inputs,
    .outputs       = aconvert_outputs,
};
