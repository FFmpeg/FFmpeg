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
    double min_diff, max_diff;
    double diff1_sum;
    uint64_t mask;
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
    int metadata;
    int reset_count;
    int nb_frames;
} AudioStatsContext;

#define OFFSET(x) offsetof(AudioStatsContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption astats_options[] = {
    { "length", "set the window length", OFFSET(time_constant), AV_OPT_TYPE_DOUBLE, {.dbl=.05}, .01, 10, FLAGS },
    { "metadata", "inject metadata in the filtergraph", OFFSET(metadata), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS },
    { "reset", "recalculate stats after this many frames", OFFSET(reset_count), AV_OPT_TYPE_INT, {.i64=0}, 0, INT_MAX, FLAGS },
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
    int ret;

    layouts = ff_all_channel_layouts();
    if (!layouts)
        return AVERROR(ENOMEM);
    ret = ff_set_common_channel_layouts(ctx, layouts);
    if (ret < 0)
        return ret;

    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ret = ff_set_common_formats(ctx, formats);
    if (ret < 0)
        return ret;

    formats = ff_all_samplerates();
    if (!formats)
        return AVERROR(ENOMEM);
    return ff_set_common_samplerates(ctx, formats);
}

static void reset_stats(AudioStatsContext *s)
{
    int c;

    memset(s->chstats, 0, sizeof(*s->chstats));

    for (c = 0; c < s->nb_channels; c++) {
        ChannelStats *p = &s->chstats[c];

        p->min = p->min_sigma_x2 = DBL_MAX;
        p->max = p->max_sigma_x2 = DBL_MIN;
        p->min_diff = p->max_diff = -1;
    }
}

static int config_output(AVFilterLink *outlink)
{
    AudioStatsContext *s = outlink->src->priv;

    s->chstats = av_calloc(sizeof(*s->chstats), outlink->channels);
    if (!s->chstats)
        return AVERROR(ENOMEM);
    s->nb_channels = outlink->channels;
    s->mult = exp((-1 / s->time_constant / outlink->sample_rate));
    s->tc_samples = 5 * s->time_constant * outlink->sample_rate + .5;

    reset_stats(s);

    return 0;
}

static unsigned bit_depth(uint64_t mask)
{
    unsigned result = 64;

    for (; result && !(mask & 1); --result, mask >>= 1);

    return result;
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
    p->min_diff = FFMIN(p->min_diff == -1 ? DBL_MAX : p->min_diff, FFABS(d - (p->min_diff == -1 ? DBL_MAX : p->last)));
    p->max_diff = FFMAX(p->max_diff, FFABS(d - (p->max_diff == -1 ? d : p->last)));
    p->diff1_sum += FFABS(d - p->last);
    p->last = d;
    p->mask |= llrint(d * (UINT64_C(1) << 63));

    if (p->nb_samples >= s->tc_samples) {
        p->max_sigma_x2 = FFMAX(p->max_sigma_x2, p->avg_sigma_x2);
        p->min_sigma_x2 = FFMIN(p->min_sigma_x2, p->avg_sigma_x2);
    }
    p->nb_samples++;
}

static void set_meta(AVDictionary **metadata, int chan, const char *key,
                     const char *fmt, double val)
{
    uint8_t value[128];
    uint8_t key2[128];

    snprintf(value, sizeof(value), fmt, val);
    if (chan)
        snprintf(key2, sizeof(key2), "lavfi.astats.%d.%s", chan, key);
    else
        snprintf(key2, sizeof(key2), "lavfi.astats.%s", key);
    av_dict_set(metadata, key2, value, 0);
}

#define LINEAR_TO_DB(x) (log10(x) * 20)

static void set_metadata(AudioStatsContext *s, AVDictionary **metadata)
{
    uint64_t mask = 0, min_count = 0, max_count = 0, nb_samples = 0;
    double min_runs = 0, max_runs = 0,
           min = DBL_MAX, max = DBL_MIN, min_diff = DBL_MAX, max_diff = 0,
           max_sigma_x = 0,
           diff1_sum = 0,
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
        min_diff = FFMIN(min_diff, p->min_diff);
        max_diff = FFMAX(max_diff, p->max_diff);
        diff1_sum += p->diff1_sum,
        min_sigma_x2 = FFMIN(min_sigma_x2, p->min_sigma_x2);
        max_sigma_x2 = FFMAX(max_sigma_x2, p->max_sigma_x2);
        sigma_x += p->sigma_x;
        sigma_x2 += p->sigma_x2;
        min_count += p->min_count;
        max_count += p->max_count;
        min_runs += p->min_runs;
        max_runs += p->max_runs;
        mask |= p->mask;
        nb_samples += p->nb_samples;
        if (fabs(p->sigma_x) > fabs(max_sigma_x))
            max_sigma_x = p->sigma_x;

        set_meta(metadata, c + 1, "DC_offset", "%f", p->sigma_x / p->nb_samples);
        set_meta(metadata, c + 1, "Min_level", "%f", p->min);
        set_meta(metadata, c + 1, "Max_level", "%f", p->max);
        set_meta(metadata, c + 1, "Min_difference", "%f", p->min_diff);
        set_meta(metadata, c + 1, "Max_difference", "%f", p->max_diff);
        set_meta(metadata, c + 1, "Mean_difference", "%f", p->diff1_sum / (p->nb_samples - 1));
        set_meta(metadata, c + 1, "Peak_level", "%f", LINEAR_TO_DB(FFMAX(-p->min, p->max)));
        set_meta(metadata, c + 1, "RMS_level", "%f", LINEAR_TO_DB(sqrt(p->sigma_x2 / p->nb_samples)));
        set_meta(metadata, c + 1, "RMS_peak", "%f", LINEAR_TO_DB(sqrt(p->max_sigma_x2)));
        set_meta(metadata, c + 1, "RMS_trough", "%f", LINEAR_TO_DB(sqrt(p->min_sigma_x2)));
        set_meta(metadata, c + 1, "Crest_factor", "%f", p->sigma_x2 ? FFMAX(-p->min, p->max) / sqrt(p->sigma_x2 / p->nb_samples) : 1);
        set_meta(metadata, c + 1, "Flat_factor", "%f", LINEAR_TO_DB((p->min_runs + p->max_runs) / (p->min_count + p->max_count)));
        set_meta(metadata, c + 1, "Peak_count", "%f", (float)(p->min_count + p->max_count));
        set_meta(metadata, c + 1, "Bit_depth", "%f", bit_depth(p->mask));
    }

    set_meta(metadata, 0, "Overall.DC_offset", "%f", max_sigma_x / (nb_samples / s->nb_channels));
    set_meta(metadata, 0, "Overall.Min_level", "%f", min);
    set_meta(metadata, 0, "Overall.Max_level", "%f", max);
    set_meta(metadata, 0, "Overall.Min_difference", "%f", min_diff);
    set_meta(metadata, 0, "Overall.Max_difference", "%f", max_diff);
    set_meta(metadata, 0, "Overall.Mean_difference", "%f", diff1_sum / (nb_samples - s->nb_channels));
    set_meta(metadata, 0, "Overall.Peak_level", "%f", LINEAR_TO_DB(FFMAX(-min, max)));
    set_meta(metadata, 0, "Overall.RMS_level", "%f", LINEAR_TO_DB(sqrt(sigma_x2 / nb_samples)));
    set_meta(metadata, 0, "Overall.RMS_peak", "%f", LINEAR_TO_DB(sqrt(max_sigma_x2)));
    set_meta(metadata, 0, "Overall.RMS_trough", "%f", LINEAR_TO_DB(sqrt(min_sigma_x2)));
    set_meta(metadata, 0, "Overall.Flat_factor", "%f", LINEAR_TO_DB((min_runs + max_runs) / (min_count + max_count)));
    set_meta(metadata, 0, "Overall.Peak_count", "%f", (float)(min_count + max_count) / (double)s->nb_channels);
    set_meta(metadata, 0, "Overall.Bit_depth", "%f", bit_depth(mask));
    set_meta(metadata, 0, "Overall.Number_of_samples", "%f", nb_samples / s->nb_channels);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *buf)
{
    AudioStatsContext *s = inlink->dst->priv;
    AVDictionary **metadata = avpriv_frame_get_metadatap(buf);
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

    if (s->metadata)
        set_metadata(s, metadata);

    if (s->reset_count > 0) {
        s->nb_frames++;
        if (s->nb_frames >= s->reset_count) {
            reset_stats(s);
            s->nb_frames = 0;
        }
    }

    return ff_filter_frame(inlink->dst->outputs[0], buf);
}

static void print_stats(AVFilterContext *ctx)
{
    AudioStatsContext *s = ctx->priv;
    uint64_t mask = 0, min_count = 0, max_count = 0, nb_samples = 0;
    double min_runs = 0, max_runs = 0,
           min = DBL_MAX, max = DBL_MIN, min_diff = DBL_MAX, max_diff = 0,
           max_sigma_x = 0,
           diff1_sum = 0,
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
        min_diff = FFMIN(min_diff, p->min_diff);
        max_diff = FFMAX(max_diff, p->max_diff);
        diff1_sum += p->diff1_sum,
        min_sigma_x2 = FFMIN(min_sigma_x2, p->min_sigma_x2);
        max_sigma_x2 = FFMAX(max_sigma_x2, p->max_sigma_x2);
        sigma_x += p->sigma_x;
        sigma_x2 += p->sigma_x2;
        min_count += p->min_count;
        max_count += p->max_count;
        min_runs += p->min_runs;
        max_runs += p->max_runs;
        mask |= p->mask;
        nb_samples += p->nb_samples;
        if (fabs(p->sigma_x) > fabs(max_sigma_x))
            max_sigma_x = p->sigma_x;

        av_log(ctx, AV_LOG_INFO, "Channel: %d\n", c + 1);
        av_log(ctx, AV_LOG_INFO, "DC offset: %f\n", p->sigma_x / p->nb_samples);
        av_log(ctx, AV_LOG_INFO, "Min level: %f\n", p->min);
        av_log(ctx, AV_LOG_INFO, "Max level: %f\n", p->max);
        av_log(ctx, AV_LOG_INFO, "Min difference: %f\n", p->min_diff);
        av_log(ctx, AV_LOG_INFO, "Max difference: %f\n", p->max_diff);
        av_log(ctx, AV_LOG_INFO, "Mean difference: %f\n", p->diff1_sum / (p->nb_samples - 1));
        av_log(ctx, AV_LOG_INFO, "Peak level dB: %f\n", LINEAR_TO_DB(FFMAX(-p->min, p->max)));
        av_log(ctx, AV_LOG_INFO, "RMS level dB: %f\n", LINEAR_TO_DB(sqrt(p->sigma_x2 / p->nb_samples)));
        av_log(ctx, AV_LOG_INFO, "RMS peak dB: %f\n", LINEAR_TO_DB(sqrt(p->max_sigma_x2)));
        if (p->min_sigma_x2 != 1)
            av_log(ctx, AV_LOG_INFO, "RMS trough dB: %f\n",LINEAR_TO_DB(sqrt(p->min_sigma_x2)));
        av_log(ctx, AV_LOG_INFO, "Crest factor: %f\n", p->sigma_x2 ? FFMAX(-p->min, p->max) / sqrt(p->sigma_x2 / p->nb_samples) : 1);
        av_log(ctx, AV_LOG_INFO, "Flat factor: %f\n", LINEAR_TO_DB((p->min_runs + p->max_runs) / (p->min_count + p->max_count)));
        av_log(ctx, AV_LOG_INFO, "Peak count: %"PRId64"\n", p->min_count + p->max_count);
        av_log(ctx, AV_LOG_INFO, "Bit depth: %u\n", bit_depth(p->mask));
    }

    av_log(ctx, AV_LOG_INFO, "Overall\n");
    av_log(ctx, AV_LOG_INFO, "DC offset: %f\n", max_sigma_x / (nb_samples / s->nb_channels));
    av_log(ctx, AV_LOG_INFO, "Min level: %f\n", min);
    av_log(ctx, AV_LOG_INFO, "Max level: %f\n", max);
    av_log(ctx, AV_LOG_INFO, "Min difference: %f\n", min_diff);
    av_log(ctx, AV_LOG_INFO, "Max difference: %f\n", max_diff);
    av_log(ctx, AV_LOG_INFO, "Mean difference: %f\n", diff1_sum / (nb_samples - s->nb_channels));
    av_log(ctx, AV_LOG_INFO, "Peak level dB: %f\n", LINEAR_TO_DB(FFMAX(-min, max)));
    av_log(ctx, AV_LOG_INFO, "RMS level dB: %f\n", LINEAR_TO_DB(sqrt(sigma_x2 / nb_samples)));
    av_log(ctx, AV_LOG_INFO, "RMS peak dB: %f\n", LINEAR_TO_DB(sqrt(max_sigma_x2)));
    if (min_sigma_x2 != 1)
        av_log(ctx, AV_LOG_INFO, "RMS trough dB: %f\n", LINEAR_TO_DB(sqrt(min_sigma_x2)));
    av_log(ctx, AV_LOG_INFO, "Flat factor: %f\n", LINEAR_TO_DB((min_runs + max_runs) / (min_count + max_count)));
    av_log(ctx, AV_LOG_INFO, "Peak count: %f\n", (min_count + max_count) / (double)s->nb_channels);
    av_log(ctx, AV_LOG_INFO, "Bit depth: %u\n", bit_depth(mask));
    av_log(ctx, AV_LOG_INFO, "Number of samples: %"PRId64"\n", nb_samples / s->nb_channels);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioStatsContext *s = ctx->priv;

    if (s->nb_channels)
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

AVFilter ff_af_astats = {
    .name          = "astats",
    .description   = NULL_IF_CONFIG_SMALL("Show time domain statistics about audio frames."),
    .query_formats = query_formats,
    .priv_size     = sizeof(AudioStatsContext),
    .priv_class    = &astats_class,
    .uninit        = uninit,
    .inputs        = astats_inputs,
    .outputs       = astats_outputs,
};
