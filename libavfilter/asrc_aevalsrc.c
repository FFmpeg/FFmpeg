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

#include "libavutil/audioconvert.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/opt.h"
#include "avfilter.h"
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
    int nb_channels;
    int64_t pts;
    AVExpr *expr[8];
    char *expr_str[8];
    int nb_samples;             ///< number of samples per requested frame
    uint64_t n;
    double var_values[VAR_VARS_NB];
} EvalContext;

#define OFFSET(x) offsetof(EvalContext, x)

static const AVOption eval_options[]= {
    { "nb_samples",  "set the number of samples per requested frame", OFFSET(nb_samples),      AV_OPT_TYPE_INT,    {.dbl = 1024},    0,        INT_MAX },
    { "n",           "set the number of samples per requested frame", OFFSET(nb_samples),      AV_OPT_TYPE_INT,    {.dbl = 1024},    0,        INT_MAX },
    { "sample_rate", "set the sample rate",                           OFFSET(sample_rate_str), AV_OPT_TYPE_STRING, {.str = "44100"}, CHAR_MIN, CHAR_MAX },
    { "s",           "set the sample rate",                           OFFSET(sample_rate_str), AV_OPT_TYPE_STRING, {.str = "44100"}, CHAR_MIN, CHAR_MAX },
{NULL},
};

static const char *eval_get_name(void *ctx)
{
    return "aevalsrc";
}

static const AVClass eval_class = {
    "AEvalSrcContext",
    eval_get_name,
    eval_options
};

static int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    EvalContext *eval = ctx->priv;
    char *args1 = av_strdup(args);
    char *expr, *buf, *bufptr;
    int ret, i;

    eval->class = &eval_class;
    av_opt_set_defaults(eval);

    /* parse expressions */
    buf = args1;
    i = 0;
    while (expr = av_strtok(buf, ":", &bufptr)) {
        if (i >= 8) {
            av_log(ctx, AV_LOG_ERROR,
                   "More than 8 expressions provided, unsupported.\n");
            ret = AVERROR(EINVAL);
            return ret;
        }
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

    /* guess channel layout from nb expressions/channels */
    eval->nb_channels = i;
    eval->chlayout = av_get_default_channel_layout(eval->nb_channels);
    if (!eval->chlayout) {
        av_log(ctx, AV_LOG_ERROR, "Invalid number of channels '%d' provided\n",
               eval->nb_channels);
        ret = AVERROR(EINVAL);
        goto end;
    }

    if (bufptr && (ret = av_set_options_string(eval, bufptr, "=", ":")) < 0)
        goto end;

    if ((ret = ff_parse_sample_rate(&eval->sample_rate, eval->sample_rate_str, ctx)))
        goto end;
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

    av_log(outlink->src, AV_LOG_INFO,
           "sample_rate:%d chlayout:%s\n", eval->sample_rate, buf);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    EvalContext *eval = ctx->priv;
    enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_NONE };
    int64_t chlayouts[] = { eval->chlayout, -1 };
    int packing_fmts[] = { AVFILTER_PLANAR, -1 };

    avfilter_set_common_sample_formats (ctx, avfilter_make_format_list(sample_fmts));
    avfilter_set_common_channel_layouts(ctx, avfilter_make_format64_list(chlayouts));
    avfilter_set_common_packing_formats(ctx, avfilter_make_format_list(packing_fmts));

    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    EvalContext *eval = outlink->src->priv;
    AVFilterBufferRef *samplesref;
    int i, j;

    samplesref = avfilter_get_audio_buffer(outlink, AV_PERM_WRITE, eval->nb_samples);

    /* evaluate expression for each single sample and for each channel */
    for (i = 0; i < eval->nb_samples; i++, eval->n++) {
        eval->var_values[VAR_N] = eval->n;
        eval->var_values[VAR_T] = eval->var_values[VAR_N] * (double)1/eval->sample_rate;

        for (j = 0; j < eval->nb_channels; j++) {
            *((double *) samplesref->data[j] + i) =
                av_expr_eval(eval->expr[j], eval->var_values, NULL);
        }
    }

    samplesref->pts = eval->pts;
    samplesref->pos = -1;
    samplesref->audio->sample_rate = eval->sample_rate;
    eval->pts += eval->nb_samples;

    avfilter_filter_samples(outlink, samplesref);

    return 0;
}

AVFilter avfilter_asrc_aevalsrc = {
    .name        = "aevalsrc",
    .description = NULL_IF_CONFIG_SMALL("Generate an audio signal generated by an expression."),

    .query_formats = query_formats,
    .init        = init,
    .uninit      = uninit,
    .priv_size   = sizeof(EvalContext),

    .inputs      = (const AVFilterPad[]) {{ .name = NULL}},

    .outputs     = (const AVFilterPad[]) {{ .name = "default",
                                      .type = AVMEDIA_TYPE_AUDIO,
                                      .config_props = config_props,
                                      .request_frame = request_frame, },
                                    { .name = NULL}},
};
