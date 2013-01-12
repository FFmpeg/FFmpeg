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
    "n",            ///< number of frame
    "t",            ///< timestamp expressed in seconds
    "s",            ///< sample rate
    NULL
};

enum var_name {
    VAR_N,
    VAR_T,
    VAR_S,
    VAR_VARS_NB
};

typedef struct {
    const AVClass *class;
    char *sample_rate_str;
    int sample_rate;
    int64_t chlayout;
    char *chlayout_str;
    int nb_channels;
    int64_t pts;
    AVExpr *expr[8];
    char *expr_str[8];
    int nb_samples;             ///< number of samples per requested frame
    char *duration_str;         ///< total duration of the generated audio
    double duration;
    uint64_t n;
    double var_values[VAR_VARS_NB];
} EvalContext;

#define OFFSET(x) offsetof(EvalContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption aevalsrc_options[]= {
    { "nb_samples",  "set the number of samples per requested frame", OFFSET(nb_samples),      AV_OPT_TYPE_INT,    {.i64 = 1024},    0,        INT_MAX, FLAGS },
    { "n",           "set the number of samples per requested frame", OFFSET(nb_samples),      AV_OPT_TYPE_INT,    {.i64 = 1024},    0,        INT_MAX, FLAGS },
    { "sample_rate", "set the sample rate",                           OFFSET(sample_rate_str), AV_OPT_TYPE_STRING, {.str = "44100"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "s",           "set the sample rate",                           OFFSET(sample_rate_str), AV_OPT_TYPE_STRING, {.str = "44100"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "duration",    "set audio duration", OFFSET(duration_str), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { "d",           "set audio duration", OFFSET(duration_str), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { "channel_layout", "set channel layout", OFFSET(chlayout_str), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { "c",              "set channel layout", OFFSET(chlayout_str), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
{NULL},
};

AVFILTER_DEFINE_CLASS(aevalsrc);

static int init(AVFilterContext *ctx, const char *args)
{
    EvalContext *eval = ctx->priv;
    char *args1 = av_strdup(args);
    char *expr, *buf, *bufptr;
    int ret, i;

    eval->class = &aevalsrc_class;
    av_opt_set_defaults(eval);

    if (!args1) {
        av_log(ctx, AV_LOG_ERROR, "Argument is empty\n");
        ret = args ? AVERROR(ENOMEM) : AVERROR(EINVAL);
        goto end;
    }

    /* parse expressions */
    buf = args1;
    i = 0;
    while (expr = av_strtok(buf, ":", &bufptr)) {
        ret = av_expr_parse(&eval->expr[i], expr, var_names,
                            NULL, NULL, NULL, NULL, 0, ctx);
        if (ret < 0)
            goto end;
        i++;
        if (bufptr && *bufptr == ':') { /* found last expression */
            bufptr++;
            break;
        }
        buf = NULL;
    }
    eval->nb_channels = i;

    if (bufptr && (ret = av_set_options_string(eval, bufptr, "=", ":")) < 0)
        goto end;

    if (eval->chlayout_str) {
        int n;
        ret = ff_parse_channel_layout(&eval->chlayout, eval->chlayout_str, ctx);
        if (ret < 0)
            goto end;

        n = av_get_channel_layout_nb_channels(eval->chlayout);
        if (n != eval->nb_channels) {
            av_log(ctx, AV_LOG_ERROR,
                   "Mismatch between the specified number of channels '%d' "
                   "and the number of channels '%d' in the specified channel layout '%s'\n",
                   eval->nb_channels, n, eval->chlayout_str);
            ret = AVERROR(EINVAL);
            goto end;
        }
    } else {
        /* guess channel layout from nb expressions/channels */
        eval->chlayout = av_get_default_channel_layout(eval->nb_channels);
        if (!eval->chlayout) {
            av_log(ctx, AV_LOG_ERROR, "Invalid number of channels '%d' provided\n",
                   eval->nb_channels);
            ret = AVERROR(EINVAL);
            goto end;
        }
    }

    if ((ret = ff_parse_sample_rate(&eval->sample_rate, eval->sample_rate_str, ctx)))
        goto end;

    eval->duration = -1;
    if (eval->duration_str) {
        int64_t us = -1;
        if ((ret = av_parse_time(&us, eval->duration_str, 1)) < 0) {
            av_log(ctx, AV_LOG_ERROR, "Invalid duration: '%s'\n", eval->duration_str);
            goto end;
        }
        eval->duration = (double)us / 1000000;
    }
    eval->n = 0;

end:
    av_free(args1);
    return ret;
}

static void uninit(AVFilterContext *ctx)
{
    EvalContext *eval = ctx->priv;
    int i;

    for (i = 0; i < 8; i++) {
        av_expr_free(eval->expr[i]);
        eval->expr[i] = NULL;
    }
    av_freep(&eval->chlayout_str);
    av_freep(&eval->duration_str);
    av_freep(&eval->sample_rate_str);
}

static int config_props(AVFilterLink *outlink)
{
    EvalContext *eval = outlink->src->priv;
    char buf[128];

    outlink->time_base = (AVRational){1, eval->sample_rate};
    outlink->sample_rate = eval->sample_rate;

    eval->var_values[VAR_S] = eval->sample_rate;

    av_get_channel_layout_string(buf, sizeof(buf), 0, eval->chlayout);

    av_log(outlink->src, AV_LOG_VERBOSE,
           "sample_rate:%d chlayout:%s duration:%f\n",
           eval->sample_rate, buf, eval->duration);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    EvalContext *eval = ctx->priv;
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_DBLP, AV_SAMPLE_FMT_NONE };
    int64_t chlayouts[] = { eval->chlayout, -1 };
    int sample_rates[] = { eval->sample_rate, -1 };

    ff_set_common_formats (ctx, ff_make_format_list(sample_fmts));
    ff_set_common_channel_layouts(ctx, avfilter_make_format64_list(chlayouts));
    ff_set_common_samplerates(ctx, ff_make_format_list(sample_rates));

    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    EvalContext *eval = outlink->src->priv;
    AVFilterBufferRef *samplesref;
    int i, j;
    double t = eval->n * (double)1/eval->sample_rate;

    if (eval->duration >= 0 && t >= eval->duration)
        return AVERROR_EOF;

    samplesref = ff_get_audio_buffer(outlink, AV_PERM_WRITE, eval->nb_samples);

    /* evaluate expression for each single sample and for each channel */
    for (i = 0; i < eval->nb_samples; i++, eval->n++) {
        eval->var_values[VAR_N] = eval->n;
        eval->var_values[VAR_T] = eval->var_values[VAR_N] * (double)1/eval->sample_rate;

        for (j = 0; j < eval->nb_channels; j++) {
            *((double *) samplesref->extended_data[j] + i) =
                av_expr_eval(eval->expr[j], eval->var_values, NULL);
        }
    }

    samplesref->pts = eval->pts;
    samplesref->pos = -1;
    samplesref->audio->sample_rate = eval->sample_rate;
    eval->pts += eval->nb_samples;

    ff_filter_frame(outlink, samplesref);

    return 0;
}

static const AVFilterPad aevalsrc_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_props,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter avfilter_asrc_aevalsrc = {
    .name        = "aevalsrc",
    .description = NULL_IF_CONFIG_SMALL("Generate an audio signal generated by an expression."),

    .query_formats = query_formats,
    .init        = init,
    .uninit      = uninit,
    .priv_size   = sizeof(EvalContext),
    .inputs      = NULL,
    .outputs     = aevalsrc_outputs,
    .priv_class  = &aevalsrc_class,
};
