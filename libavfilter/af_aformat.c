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
#include "libavutil/opt.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"

typedef struct AFormatContext {
    const AVClass   *class;

    AVFilterFormats *formats;
    AVFilterFormats *sample_rates;
    AVFilterChannelLayouts *channel_layouts;

    char *formats_str;
    char *sample_rates_str;
    char *channel_layouts_str;
} AFormatContext;

#define OFFSET(x) offsetof(AFormatContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM
static const AVOption aformat_options[] = {
    { "sample_fmts",     "A '|'-separated list of sample formats.",  OFFSET(formats_str),         AV_OPT_TYPE_STRING, .flags = A|F },
    { "f",               "A '|'-separated list of sample formats.",  OFFSET(formats_str),         AV_OPT_TYPE_STRING, .flags = A|F },
    { "sample_rates",    "A '|'-separated list of sample rates.",    OFFSET(sample_rates_str),    AV_OPT_TYPE_STRING, .flags = A|F },
    { "r",               "A '|'-separated list of sample rates.",    OFFSET(sample_rates_str),    AV_OPT_TYPE_STRING, .flags = A|F },
    { "channel_layouts", "A '|'-separated list of channel layouts.", OFFSET(channel_layouts_str), AV_OPT_TYPE_STRING, .flags = A|F },
    { "cl",              "A '|'-separated list of channel layouts.", OFFSET(channel_layouts_str), AV_OPT_TYPE_STRING, .flags = A|F },
    { NULL }
};

AVFILTER_DEFINE_CLASS(aformat);

#define PARSE_FORMATS(str, type, list, add_to_list, get_fmt, none, desc)    \
do {                                                                        \
    char *next, *cur = str, sep;                                            \
    int ret;                                                                \
                                                                            \
    if (str && strchr(str, ',')) {                                          \
        av_log(ctx, AV_LOG_WARNING, "This syntax is deprecated, use '|' to "\
               "separate %s.\n", desc);                                     \
        sep = ',';                                                          \
    } else                                                                  \
        sep = '|';                                                          \
                                                                            \
    while (cur) {                                                           \
        type fmt;                                                           \
        next = strchr(cur, sep);                                            \
        if (next)                                                           \
            *next++ = 0;                                                    \
                                                                            \
        if ((fmt = get_fmt(cur)) == none) {                                 \
            av_log(ctx, AV_LOG_ERROR, "Error parsing " desc ": %s.\n", cur);\
            return AVERROR(EINVAL);                                         \
        }                                                                   \
        if ((ret = add_to_list(&list, fmt)) < 0) {                          \
            return ret;                                                     \
        }                                                                   \
                                                                            \
        cur = next;                                                         \
    }                                                                       \
} while (0)

static int get_sample_rate(const char *samplerate)
{
    int ret = strtol(samplerate, NULL, 0);
    return FFMAX(ret, 0);
}

static av_cold int init(AVFilterContext *ctx)
{
    AFormatContext *s = ctx->priv;

    PARSE_FORMATS(s->formats_str, enum AVSampleFormat, s->formats,
                  ff_add_format, av_get_sample_fmt, AV_SAMPLE_FMT_NONE, "sample format");
    PARSE_FORMATS(s->sample_rates_str, int, s->sample_rates, ff_add_format,
                  get_sample_rate, 0, "sample rate");
    PARSE_FORMATS(s->channel_layouts_str, uint64_t, s->channel_layouts,
                  ff_add_channel_layout, av_get_channel_layout, 0,
                  "channel layout");

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AFormatContext *s = ctx->priv;

    ff_formats_unref(&s->formats);
    ff_formats_unref(&s->sample_rates);
    ff_channel_layouts_unref(&s->channel_layouts);
}

static int query_formats(AVFilterContext *ctx)
{
    AFormatContext *s = ctx->priv;
    int ret;

    ret = ff_set_common_formats(ctx, s->formats ? s->formats :
                                            ff_all_formats(AVMEDIA_TYPE_AUDIO));
    s->formats = NULL;
    if (ret < 0)
        return ret;
    ret = ff_set_common_samplerates(ctx, s->sample_rates ? s->sample_rates :
                                                     ff_all_samplerates());
    s->sample_rates = NULL;
    if (ret < 0)
        return ret;
    ret = ff_set_common_channel_layouts(ctx, s->channel_layouts ? s->channel_layouts :
                                                            ff_all_channel_counts());
    s->channel_layouts = NULL;
    return ret;
}

static const AVFilterPad avfilter_af_aformat_inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

static const AVFilterPad avfilter_af_aformat_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO
    },
    { NULL }
};

AVFilter ff_af_aformat = {
    .name          = "aformat",
    .description   = NULL_IF_CONFIG_SMALL("Convert the input audio to one of the specified formats."),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .priv_size     = sizeof(AFormatContext),
    .priv_class    = &aformat_class,
    .inputs        = avfilter_af_aformat_inputs,
    .outputs       = avfilter_af_aformat_outputs,
};
