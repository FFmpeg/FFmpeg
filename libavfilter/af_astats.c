/*
 * Copyright (c) 2009 Rob Sykes <robs@users.sourceforge.net>
 * Copyright (c) 2013 Paul B Mahol
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

#include <float.h>

#include "libavutil/opt.h"
#include "audio.h"
#include "avfilter.h"
#include "internal.h"

typedef struct ChannelStats {
    double last;
    double sigma_x, sigma_x2;
    double avg_sigma_x2, min_sigma_x2, max_sigma_x2;
    double min, max;
    double min_run, max_run;
    double min_runs, max_runs;
    uint64_t min_count, max_count;
    uint64_t nb_samples;
} ChannelStats;

typedef struct {
    const AVClass *class;
    ChannelStats *chstats;
    int nb_channels;
    uint64_t tc_samples;
    double time_constant;
    double mult;
} AudioStatsContext;

#define OFFSET(x) offsetof(AudioStatsContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption astats_options[] = {
    { "length", "set the window length", OFFSET(time_constant), AV_OPT_TYPE_DOUBLE, {.dbl=.05}, .01, 10, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(astats);

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_DBLP,
        AV_SAMPLE_FMT_NONE
    };

    layouts = ff_all_channel_layouts();
    if (!layouts)
        return AVERROR(ENOMEM);
    ff_set_common_channel_layouts(ctx, layouts);

    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ff_set_common_formats(ctx, formats);

    formats = ff_all_samplerates();
    if (!formats)
        return AVERROR(ENOMEM);
    ff_set_common_samplerates(ctx, formats);

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AudioStatsContext *s = outlink->src->priv;
    int c;

    s->chstats = av_calloc(sizeof(*s->chstats), outlink->channels);
    if (!s->chstats)
        return AVERROR(ENOMEM);
    s->nb_channels = outlink->channels;
    s->mult = exp((-1 / s->time_constant / outlink->sample_rate));
    s->tc_samples = 5 * s->time_constant * outlink->sample_rate + .5;

    for (c = 0; c < s->nb_channels; c++) {
        ChannelStats *p = &s->chstats[c];

        p->min = p->min_sigma_x2 = DBL_MAX;
        p->max = p->max_sigma_x2 = DBL_MIN;
    }

    return 0;
}

static inline void update_stat(AudioStatsContext *s, ChannelStats *p, double d)
{
    if (d < p->min) {
        p->min = d;
        p->min_run = 1;
        p->min_runs = 0;
        p->min_count = 1;
    } else if (d == p->min) {
        p->min_count++;
        p->min_run = d == p->last ? p->min_run + 1 : 1;
    } else if (p->last == p->min) {
        p->min_runs += p->min_run * p->min_run;
    }

    if (d > p->max) {
        p->max = d;
        p->max_run = 1;
        p->max_runs = 0;
        p->max_count = 1;
    } else if (d == p->max) {
        p->max_count++;
        p->max_run = d == p->last ? p->max_run + 1 : 1;
    } else if (p->last == p->max) {
        p->max_runs += p->max_run * p->max_run;
    }

    p->sigma_x += d;
    p->sigma_x2 += d * d;
    p->avg_sigma_x2 = p->avg_sigma_x2 * s->mult + (1.0 - s->mult) * d * d;
    p->last = d;

    if (p->nb_samples >= s->tc_samples) {
        p->max_sigma_x2 = FFMAX(p->max_sigma_x2, p->avg_sigma_x2);
        p->min_sigma_x2 = FFMIN(p->min_sigma_x2, p->avg_sigma_x2);
    }
    p->nb_samples++;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *buf)
{
    AudioStatsContext *s = inlink->dst->priv;
    const int channels = s->nb_channels;
    const double *src;
    int i, c;

    switch (inlink->format) {
    case AV_SAMPLE_FMT_DBLP:
        for (c = 0; c < channels; c++) {
            ChannelStats *p = &s->chstats[c];
            src = (const double *)buf->extended_data[c];

            for (i = 0; i < buf->nb_samples; i++, src++)
                update_stat(s, p, *src);
        }
        break;
    case AV_SAMPLE_FMT_DBL:
        src = (const double *)buf->extended_data[0];

        for (i = 0; i < buf->nb_samples; i++) {
            for (c = 0; c < channels; c++, src++)
                update_stat(s, &s->chstats[c], *src);
        }
        break;
    }

    return ff_filter_frame(inlink->dst->outputs[0], buf);
}

#define LINEAR_TO_DB(x) (log10(x) * 20)

static void print_stats(AVFilterContext *ctx)
{
    AudioStatsContext *s = ctx->priv;
    uint64_t min_count = 0, max_count = 0, nb_samples = 0;
    double min_runs = 0, max_runs = 0,
           min = DBL_MAX, max = DBL_MIN,
           max_sigma_x = 0,
           sigma_x = 0,
           sigma_x2 = 0,
           min_sigma_x2 = DBL_MAX,
           max_sigma_x2 = DBL_MIN;
    int c;

    for (c = 0; c < s->nb_channels; c++) {
        ChannelStats *p = &s->chstats[c];

        if (p->nb_samples < s->tc_samples)
            p->min_sigma_x2 = p->max_sigma_x2 = p->sigma_x2 / p->nb_samples;

        min = FFMIN(min, p->min);
        max = FFMAX(max, p->max);
        min_sigma_x2 = FFMIN(min_sigma_x2, p->min_sigma_x2);
        max_sigma_x2 = FFMAX(max_sigma_x2, p->max_sigma_x2);
        sigma_x += p->sigma_x;
        sigma_x2 += p->sigma_x2;
        min_count += p->min_count;
        max_count += p->max_count;
        min_runs += p->min_runs;
        max_runs += p->max_runs;
        nb_samples += p->nb_samples;
        if (fabs(p->sigma_x) > fabs(max_sigma_x))
            max_sigma_x = p->sigma_x;

        av_log(ctx, AV_LOG_INFO, "Channel: %d\n", c + 1);
        av_log(ctx, AV_LOG_INFO, "DC offset: %f\n", p->sigma_x / p->nb_samples);
        av_log(ctx, AV_LOG_INFO, "Min level: %f\n", p->min);
        av_log(ctx, AV_LOG_INFO, "Max level: %f\n", p->max);
        av_log(ctx, AV_LOG_INFO, "Peak level dB: %f\n", LINEAR_TO_DB(FFMAX(-p->min, p->max)));
        av_log(ctx, AV_LOG_INFO, "RMS level dB: %f\n", LINEAR_TO_DB(sqrt(p->sigma_x2 / p->nb_samples)));
        av_log(ctx, AV_LOG_INFO, "RMS peak dB: %f\n", LINEAR_TO_DB(sqrt(p->max_sigma_x2)));
        if (p->min_sigma_x2 != 1)
            av_log(ctx, AV_LOG_INFO, "RMS trough dB: %f\n",LINEAR_TO_DB(sqrt(p->min_sigma_x2)));
        av_log(ctx, AV_LOG_INFO, "Crest factor: %f\n", p->sigma_x2 ? FFMAX(-p->min, p->max) / sqrt(p->sigma_x2 / p->nb_samples) : 1);
        av_log(ctx, AV_LOG_INFO, "Flat factor: %f\n", LINEAR_TO_DB((p->min_runs + p->max_runs) / (p->min_count + p->max_count)));
        av_log(ctx, AV_LOG_INFO, "Peak count: %"PRId64"\n", p->min_count + p->max_count);
    }

    av_log(ctx, AV_LOG_INFO, "Overall\n");
    av_log(ctx, AV_LOG_INFO, "DC offset: %f\n", max_sigma_x / (nb_samples / s->nb_channels));
    av_log(ctx, AV_LOG_INFO, "Min level: %f\n", min);
    av_log(ctx, AV_LOG_INFO, "Max level: %f\n", max);
    av_log(ctx, AV_LOG_INFO, "Peak level dB: %f\n", LINEAR_TO_DB(FFMAX(-min, max)));
    av_log(ctx, AV_LOG_INFO, "RMS level dB: %f\n", LINEAR_TO_DB(sqrt(sigma_x2 / nb_samples)));
    av_log(ctx, AV_LOG_INFO, "RMS peak dB: %f\n", LINEAR_TO_DB(sqrt(max_sigma_x2)));
    if (min_sigma_x2 != 1)
        av_log(ctx, AV_LOG_INFO, "RMS trough dB: %f\n", LINEAR_TO_DB(sqrt(min_sigma_x2)));
    av_log(ctx, AV_LOG_INFO, "Flat factor: %f\n", LINEAR_TO_DB((min_runs + max_runs) / (min_count + max_count)));
    av_log(ctx, AV_LOG_INFO, "Peak count: %f\n", (min_count + max_count) / (double)s->nb_channels);
    av_log(ctx, AV_LOG_INFO, "Number of samples: %"PRId64"\n", nb_samples / s->nb_channels);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioStatsContext *s = ctx->priv;

    print_stats(ctx);
    av_freep(&s->chstats);
}

static const AVFilterPad astats_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad astats_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter avfilter_af_astats = {
    .name          = "astats",
    .description   = NULL_IF_CONFIG_SMALL("Show time domain statistics about audio frames."),
    .query_formats = query_formats,
    .priv_size     = sizeof(AudioStatsContext),
    .priv_class    = &astats_class,
    .uninit        = uninit,
    .inputs        = astats_inputs,
    .outputs       = astats_outputs,
};
