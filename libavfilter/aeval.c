/*
 * Copyright (c) 2011 Stefano Sabatini
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
 * eval audio source
 */

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/eval.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "avfilter.h"
#include "audio.h"
#include "internal.h"

static const char * const var_names[] = {
    "ch",           ///< the value of the current channel
    "n",            ///< number of frame
    "nb_in_channels",
    "nb_out_channels",
    "t",            ///< timestamp expressed in seconds
    "s",            ///< sample rate
    NULL
};

enum var_name {
    VAR_CH,
    VAR_N,
    VAR_NB_IN_CHANNELS,
    VAR_NB_OUT_CHANNELS,
    VAR_T,
    VAR_S,
    VAR_VARS_NB
};

typedef struct EvalContext {
    const AVClass *class;
    char *sample_rate_str;
    int sample_rate;
    int64_t chlayout;
    char *chlayout_str;
    int nb_channels;            ///< number of output channels
    int nb_in_channels;         ///< number of input channels
    int same_chlayout;          ///< set output as input channel layout
    int64_t pts;
    AVExpr **expr;
    char *exprs;
    int nb_samples;             ///< number of samples per requested frame
    int64_t duration;
    uint64_t n;
    double var_values[VAR_VARS_NB];
    double *channel_values;
    int64_t out_channel_layout;
} EvalContext;

static double val(void *priv, double ch)
{
    EvalContext *eval = priv;
    return eval->channel_values[FFMIN((int)ch, eval->nb_in_channels-1)];
}

static double (* const aeval_func1[])(void *, double) = { val, NULL };
static const char * const aeval_func1_names[] = { "val", NULL };

#define OFFSET(x) offsetof(EvalContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption aevalsrc_options[]= {
    { "exprs",       "set the '|'-separated list of channels expressions", OFFSET(exprs), AV_OPT_TYPE_STRING, {.str = NULL}, .flags = FLAGS },
    { "nb_samples",  "set the number of samples per requested frame", OFFSET(nb_samples),      AV_OPT_TYPE_INT,    {.i64 = 1024},    0,        INT_MAX, FLAGS },
    { "n",           "set the number of samples per requested frame", OFFSET(nb_samples),      AV_OPT_TYPE_INT,    {.i64 = 1024},    0,        INT_MAX, FLAGS },
    { "sample_rate", "set the sample rate",                           OFFSET(sample_rate_str), AV_OPT_TYPE_STRING, {.str = "44100"}, 0, 0, FLAGS },
    { "s",           "set the sample rate",                           OFFSET(sample_rate_str), AV_OPT_TYPE_STRING, {.str = "44100"}, 0, 0, FLAGS },
    { "duration",    "set audio duration", OFFSET(duration), AV_OPT_TYPE_DURATION, {.i64 = -1}, -1, INT64_MAX, FLAGS },
    { "d",           "set audio duration", OFFSET(duration), AV_OPT_TYPE_DURATION, {.i64 = -1}, -1, INT64_MAX, FLAGS },
    { "channel_layout", "set channel layout", OFFSET(chlayout_str), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { "c",              "set channel layout", OFFSET(chlayout_str), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(aevalsrc);

static int parse_channel_expressions(AVFilterContext *ctx,
                                     int expected_nb_channels)
{
    EvalContext *eval = ctx->priv;
    char *args1 = av_strdup(eval->exprs);
    char *expr, *last_expr = NULL, *buf;
    double (* const *func1)(void *, double) = NULL;
    const char * const *func1_names = NULL;
    int i, ret = 0;

    if (!args1)
        return AVERROR(ENOMEM);

    if (!eval->exprs) {
        av_log(ctx, AV_LOG_ERROR, "Channels expressions list is empty\n");
        return AVERROR(EINVAL);
    }

    if (!strcmp(ctx->filter->name, "aeval")) {
        func1 = aeval_func1;
        func1_names = aeval_func1_names;
    }

#define ADD_EXPRESSION(expr_) do {                                      \
        if (!av_dynarray2_add((void **)&eval->expr, &eval->nb_channels, \
                              sizeof(*eval->expr), NULL)) {             \
            ret = AVERROR(ENOMEM);                                      \
            goto end;                                                   \
        }                                                               \
        eval->expr[eval->nb_channels-1] = NULL;                         \
        ret = av_expr_parse(&eval->expr[eval->nb_channels - 1], expr_,  \
                            var_names, func1_names, func1,              \
                            NULL, NULL, 0, ctx);                        \
        if (ret < 0)                                                    \
            goto end;                                                   \
    } while (0)

    /* reset expressions */
    for (i = 0; i < eval->nb_channels; i++) {
        av_expr_free(eval->expr[i]);
        eval->expr[i] = NULL;
    }
    av_freep(&eval->expr);
    eval->nb_channels = 0;

    buf = args1;
    while (expr = av_strtok(buf, "|", &buf)) {
        ADD_EXPRESSION(expr);
        last_expr = expr;
    }

    if (expected_nb_channels > eval->nb_channels)
        for (i = eval->nb_channels; i < expected_nb_channels; i++)
            ADD_EXPRESSION(last_expr);

    if (expected_nb_channels > 0 && eval->nb_channels != expected_nb_channels) {
        av_log(ctx, AV_LOG_ERROR,
               "Mismatch between the specified number of channel expressions '%d' "
               "and the number of expected output channels '%d' for the specified channel layout\n",
               eval->nb_channels, expected_nb_channels);
        ret = AVERROR(EINVAL);
        goto end;
    }

end:
    av_free(args1);
    return ret;
}

static av_cold int init(AVFilterContext *ctx)
{
    EvalContext *eval = ctx->priv;
    int ret = 0;

    if (eval->chlayout_str) {
        if (!strcmp(eval->chlayout_str, "same") && !strcmp(ctx->filter->name, "aeval")) {
            eval->same_chlayout = 1;
        } else {
            ret = ff_parse_channel_layout(&eval->chlayout, NULL, eval->chlayout_str, ctx);
            if (ret < 0)
                return ret;

            ret = parse_channel_expressions(ctx, av_get_channel_layout_nb_channels(eval->chlayout));
            if (ret < 0)
                return ret;
        }
    } else {
        /* guess channel layout from nb expressions/channels */
        if ((ret = parse_channel_expressions(ctx, -1)) < 0)
            return ret;

        eval->chlayout = av_get_default_channel_layout(eval->nb_channels);
        if (!eval->chlayout && eval->nb_channels <= 0) {
            av_log(ctx, AV_LOG_ERROR, "Invalid number of channels '%d' provided\n",
                   eval->nb_channels);
            return AVERROR(EINVAL);
        }
    }

    if (eval->sample_rate_str)
        if ((ret = ff_parse_sample_rate(&eval->sample_rate, eval->sample_rate_str, ctx)))
            return ret;
    eval->n = 0;

    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    EvalContext *eval = ctx->priv;
    int i;

    for (i = 0; i < eval->nb_channels; i++) {
        av_expr_free(eval->expr[i]);
        eval->expr[i] = NULL;
    }
    av_freep(&eval->expr);
    av_freep(&eval->channel_values);
}

static int config_props(AVFilterLink *outlink)
{
    EvalContext *eval = outlink->src->priv;
    char buf[128];

    outlink->time_base = (AVRational){1, eval->sample_rate};
    outlink->sample_rate = eval->sample_rate;

    eval->var_values[VAR_S] = eval->sample_rate;
    eval->var_values[VAR_NB_IN_CHANNELS] = NAN;
    eval->var_values[VAR_NB_OUT_CHANNELS] = outlink->channels;

    av_get_channel_layout_string(buf, sizeof(buf), 0, eval->chlayout);

    av_log(outlink->src, AV_LOG_VERBOSE,
           "sample_rate:%d chlayout:%s duration:%"PRId64"\n",
           eval->sample_rate, buf, eval->duration);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    EvalContext *eval = ctx->priv;
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_DBLP, AV_SAMPLE_FMT_NONE };
    int64_t chlayouts[] = { eval->chlayout ? eval->chlayout : FF_COUNT2LAYOUT(eval->nb_channels) , -1 };
    int sample_rates[] = { eval->sample_rate, -1 };
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;
    int ret;

    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ret = ff_set_common_formats (ctx, formats);
    if (ret < 0)
        return ret;

    layouts = ff_make_format64_list(chlayouts);
    if (!layouts)
        return AVERROR(ENOMEM);
    ret = ff_set_common_channel_layouts(ctx, layouts);
    if (ret < 0)
        return ret;

    formats = ff_make_format_list(sample_rates);
    if (!formats)
        return AVERROR(ENOMEM);
    return ff_set_common_samplerates(ctx, formats);
}

static int request_frame(AVFilterLink *outlink)
{
    EvalContext *eval = outlink->src->priv;
    AVFrame *samplesref;
    int i, j;
    int64_t t = av_rescale(eval->n, AV_TIME_BASE, eval->sample_rate);
    int nb_samples;

    if (eval->duration >= 0 && t >= eval->duration)
        return AVERROR_EOF;

    if (eval->duration >= 0) {
        nb_samples = FFMIN(eval->nb_samples, av_rescale(eval->duration, eval->sample_rate, AV_TIME_BASE) - eval->pts);
        if (!nb_samples)
            return AVERROR_EOF;
    } else {
        nb_samples = eval->nb_samples;
    }
    samplesref = ff_get_audio_buffer(outlink, nb_samples);
    if (!samplesref)
        return AVERROR(ENOMEM);

    /* evaluate expression for each single sample and for each channel */
    for (i = 0; i < nb_samples; i++, eval->n++) {
        eval->var_values[VAR_N] = eval->n;
        eval->var_values[VAR_T] = eval->var_values[VAR_N] * (double)1/eval->sample_rate;

        for (j = 0; j < eval->nb_channels; j++) {
            *((double *) samplesref->extended_data[j] + i) =
                av_expr_eval(eval->expr[j], eval->var_values, NULL);
        }
    }

    samplesref->pts = eval->pts;
    samplesref->sample_rate = eval->sample_rate;
    eval->pts += nb_samples;

    return ff_filter_frame(outlink, samplesref);
}

#if CONFIG_AEVALSRC_FILTER
static const AVFilterPad aevalsrc_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_props,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_asrc_aevalsrc = {
    .name          = "aevalsrc",
    .description   = NULL_IF_CONFIG_SMALL("Generate an audio signal generated by an expression."),
    .query_formats = query_formats,
    .init          = init,
    .uninit        = uninit,
    .priv_size     = sizeof(EvalContext),
    .inputs        = NULL,
    .outputs       = aevalsrc_outputs,
    .priv_class    = &aevalsrc_class,
};

#endif /* CONFIG_AEVALSRC_FILTER */

#define OFFSET(x) offsetof(EvalContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption aeval_options[]= {
    { "exprs", "set the '|'-separated list of channels expressions", OFFSET(exprs), AV_OPT_TYPE_STRING, {.str = NULL}, .flags = FLAGS },
    { "channel_layout", "set channel layout", OFFSET(chlayout_str), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { "c",              "set channel layout", OFFSET(chlayout_str), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(aeval);

static int aeval_query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts;
    AVFilterLink *inlink  = ctx->inputs[0];
    AVFilterLink *outlink  = ctx->outputs[0];
    EvalContext *eval = ctx->priv;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_DBLP, AV_SAMPLE_FMT_NONE
    };
    int ret;

    // inlink supports any channel layout
    layouts = ff_all_channel_counts();
    if ((ret = ff_channel_layouts_ref(layouts, &inlink->outcfg.channel_layouts)) < 0)
        return ret;

    if (eval->same_chlayout) {
        layouts = ff_all_channel_counts();
        if ((ret = ff_set_common_channel_layouts(ctx, layouts)) < 0)
            return ret;
    } else {
        // outlink supports only requested output channel layout
        layouts = NULL;
        if ((ret = ff_add_channel_layout(&layouts,
                              eval->out_channel_layout ? eval->out_channel_layout :
                              FF_COUNT2LAYOUT(eval->nb_channels))) < 0)
            return ret;
        if ((ret = ff_channel_layouts_ref(layouts, &outlink->incfg.channel_layouts)) < 0)
            return ret;
    }

    formats = ff_make_format_list(sample_fmts);
    if ((ret = ff_set_common_formats(ctx, formats)) < 0)
        return ret;

    formats = ff_all_samplerates();
    return ff_set_common_samplerates(ctx, formats);
}

static int aeval_config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    EvalContext *eval = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    int ret;

    if (eval->same_chlayout) {
        eval->chlayout = inlink->channel_layout;

        if ((ret = parse_channel_expressions(ctx, inlink->channels)) < 0)
            return ret;
    }

    eval->n = 0;
    eval->nb_in_channels = eval->var_values[VAR_NB_IN_CHANNELS] = inlink->channels;
    eval->var_values[VAR_NB_OUT_CHANNELS] = outlink->channels;
    eval->var_values[VAR_S] = inlink->sample_rate;
    eval->var_values[VAR_T] = NAN;

    eval->channel_values = av_realloc_f(eval->channel_values,
                                        inlink->channels, sizeof(*eval->channel_values));
    if (!eval->channel_values)
        return AVERROR(ENOMEM);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    EvalContext *eval     = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    int nb_samples        = in->nb_samples;
    AVFrame *out;
    double t0;
    int i, j;

    out = ff_get_audio_buffer(outlink, nb_samples);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    t0 = TS2T(in->pts, inlink->time_base);

    /* evaluate expression for each single sample and for each channel */
    for (i = 0; i < nb_samples; i++, eval->n++) {
        eval->var_values[VAR_N] = eval->n;
        eval->var_values[VAR_T] = t0 + i * (double)1/inlink->sample_rate;

        for (j = 0; j < inlink->channels; j++)
            eval->channel_values[j] = *((double *) in->extended_data[j] + i);

        for (j = 0; j < outlink->channels; j++) {
            eval->var_values[VAR_CH] = j;
            *((double *) out->extended_data[j] + i) =
                av_expr_eval(eval->expr[j], eval->var_values, eval);
        }
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

#if CONFIG_AEVAL_FILTER

static const AVFilterPad aeval_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_AUDIO,
        .filter_frame   = filter_frame,
    },
    { NULL }
};

static const AVFilterPad aeval_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = aeval_config_output,
    },
    { NULL }
};

AVFilter ff_af_aeval = {
    .name          = "aeval",
    .description   = NULL_IF_CONFIG_SMALL("Filter audio signal according to a specified expression."),
    .query_formats = aeval_query_formats,
    .init          = init,
    .uninit        = uninit,
    .priv_size     = sizeof(EvalContext),
    .inputs        = aeval_inputs,
    .outputs       = aeval_outputs,
    .priv_class    = &aeval_class,
};

#endif /* CONFIG_AEVAL_FILTER */
