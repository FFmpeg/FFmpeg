/*
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
 * format audio filter
 */

#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"

typedef struct AFormatContext {
    const AVClass   *class;

    enum AVSampleFormat *formats;
    unsigned          nb_formats;

    int                 *sample_rates;
    unsigned          nb_sample_rates;

    AVChannelLayout     *channel_layouts;
    unsigned          nb_channel_layouts;
} AFormatContext;

static const AVOptionArrayDef array_def = { .sep = '|' };

#define OFFSET(x) offsetof(AFormatContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM
static const AVOption aformat_options[] = {
    { "sample_fmts",     "A '|'-separated list of sample formats.",  OFFSET(formats),
        AV_OPT_TYPE_SAMPLE_FMT | AV_OPT_TYPE_FLAG_ARRAY, .default_val.arr = &array_def, .flags = A|F },
    { "f",               "A '|'-separated list of sample formats.",  OFFSET(formats),
        AV_OPT_TYPE_SAMPLE_FMT | AV_OPT_TYPE_FLAG_ARRAY, .default_val.arr = &array_def, .flags = A|F },
    { "sample_rates",    "A '|'-separated list of sample rates.",    OFFSET(sample_rates),
        AV_OPT_TYPE_INT | AV_OPT_TYPE_FLAG_ARRAY, .default_val.arr = &array_def, .min = 1, .max = INT_MAX, .flags = A|F },
    { "r",               "A '|'-separated list of sample rates.",    OFFSET(sample_rates),
        AV_OPT_TYPE_INT | AV_OPT_TYPE_FLAG_ARRAY, .default_val.arr = &array_def, .min = 1, .max = INT_MAX, .flags = A|F },
    { "channel_layouts", "A '|'-separated list of channel layouts.", OFFSET(channel_layouts),
        AV_OPT_TYPE_CHLAYOUT | AV_OPT_TYPE_FLAG_ARRAY, .default_val.arr = &array_def, .flags = A|F },
    { "cl",              "A '|'-separated list of channel layouts.", OFFSET(channel_layouts),
        AV_OPT_TYPE_CHLAYOUT | AV_OPT_TYPE_FLAG_ARRAY, .default_val.arr = &array_def, .flags = A|F },
    { NULL }
};

AVFILTER_DEFINE_CLASS(aformat);

static av_cold int init(AVFilterContext *ctx)
{
    AFormatContext *s = ctx->priv;

    // terminate format lists for ff_set*_from_list()
    if (s->nb_formats) {
        void *tmp = av_realloc_array(s->formats, s->nb_formats + 1,
                                     sizeof(*s->formats));
        if (!tmp)
            return AVERROR(ENOMEM);
        s->formats = tmp;
        s->formats[s->nb_formats] = AV_SAMPLE_FMT_NONE;

    }
    if (s->nb_sample_rates) {
        void *tmp = av_realloc_array(s->sample_rates, s->nb_sample_rates + 1,
                                     sizeof(*s->sample_rates));
        if (!tmp)
            return AVERROR(ENOMEM);
        s->sample_rates = tmp;
        s->sample_rates[s->nb_sample_rates] = -1;
    }
    if (s->nb_channel_layouts) {
        void *tmp = av_realloc_array(s->channel_layouts, s->nb_channel_layouts + 1,
                                     sizeof(*s->channel_layouts));
        if (!tmp)
            return AVERROR(ENOMEM);
        s->channel_layouts = tmp;
        s->channel_layouts[s->nb_channel_layouts] = (AVChannelLayout){ .nb_channels = 0 };
    }

    return 0;
}

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    const AFormatContext *s = ctx->priv;
    int ret;

    if (s->nb_formats) {
        ret = ff_set_common_formats_from_list2(ctx, cfg_in, cfg_out, s->formats);
        if (ret < 0)
            return ret;
    }

    if (s->nb_sample_rates) {
        ret = ff_set_common_samplerates_from_list2(ctx, cfg_in, cfg_out, s->sample_rates);
        if (ret < 0)
            return ret;
    }

    if (s->nb_channel_layouts) {
        ret = ff_set_common_channel_layouts_from_list2(ctx, cfg_in, cfg_out, s->channel_layouts);
        if (ret < 0)
            return ret;
    }

    return 0;
}

const AVFilter ff_af_aformat = {
    .name          = "aformat",
    .description   = NULL_IF_CONFIG_SMALL("Convert the input audio to one of the specified formats."),
    .init          = init,
    .priv_size     = sizeof(AFormatContext),
    .priv_class    = &aformat_class,
    .flags         = AVFILTER_FLAG_METADATA_ONLY,
    FILTER_INPUTS(ff_audio_default_filterpad),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    FILTER_QUERY_FUNC2(query_formats),
};
