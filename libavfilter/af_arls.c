/*
 * Copyright (c) 2023 Paul B Mahol
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

#include "libavutil/common.h"
#include "libavutil/float_dsp.h"
#include "libavutil/opt.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "filters.h"
#include "internal.h"

enum OutModes {
    IN_MODE,
    DESIRED_MODE,
    OUT_MODE,
    NOISE_MODE,
    ERROR_MODE,
    NB_OMODES
};

typedef struct AudioRLSContext {
    const AVClass *class;

    int order;
    float lambda;
    float delta;
    int output_mode;

    int kernel_size;
    AVFrame *offset;
    AVFrame *delay;
    AVFrame *coeffs;
    AVFrame *p, *dp;
    AVFrame *gains;
    AVFrame *u, *tmp;

    AVFrame *frame[2];

    AVFloatDSPContext *fdsp;
} AudioRLSContext;

#define OFFSET(x) offsetof(AudioRLSContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define AT AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption arls_options[] = {
    { "order",    "set the filter order",  OFFSET(order),  AV_OPT_TYPE_INT,   {.i64=16}, 1, INT16_MAX, A },
    { "lambda",   "set the filter lambda", OFFSET(lambda), AV_OPT_TYPE_FLOAT, {.dbl=1.f}, 0, 1, AT },
    { "delta",    "set the filter delta",  OFFSET(delta),  AV_OPT_TYPE_FLOAT, {.dbl=2.f}, 0, INT16_MAX, A },
    { "out_mode", "set output mode",       OFFSET(output_mode), AV_OPT_TYPE_INT, {.i64=OUT_MODE}, 0, NB_OMODES-1, AT, "mode" },
    {  "i", "input",   0, AV_OPT_TYPE_CONST, {.i64=IN_MODE},      0, 0, AT, "mode" },
    {  "d", "desired", 0, AV_OPT_TYPE_CONST, {.i64=DESIRED_MODE}, 0, 0, AT, "mode" },
    {  "o", "output",  0, AV_OPT_TYPE_CONST, {.i64=OUT_MODE},     0, 0, AT, "mode" },
    {  "n", "noise",   0, AV_OPT_TYPE_CONST, {.i64=NOISE_MODE},   0, 0, AT, "mode" },
    {  "e", "error",   0, AV_OPT_TYPE_CONST, {.i64=ERROR_MODE},   0, 0, AT, "mode" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(arls);

static float fir_sample(AudioRLSContext *s, float sample, float *delay,
                        float *coeffs, float *tmp, int *offset)
{
    const int order = s->order;
    float output;

    delay[*offset] = sample;

    memcpy(tmp, coeffs + order - *offset, order * sizeof(float));

    output = s->fdsp->scalarproduct_float(delay, tmp, s->kernel_size);

    if (--(*offset) < 0)
        *offset = order - 1;

    return output;
}

static float process_sample(AudioRLSContext *s, float input, float desired, int ch)
{
    float *coeffs = (float *)s->coeffs->extended_data[ch];
    float *delay = (float *)s->delay->extended_data[ch];
    float *gains = (float *)s->gains->extended_data[ch];
    float *tmp = (float *)s->tmp->extended_data[ch];
    float *u = (float *)s->u->extended_data[ch];
    float *p = (float *)s->p->extended_data[ch];
    float *dp = (float *)s->dp->extended_data[ch];
    int *offsetp = (int *)s->offset->extended_data[ch];
    const int kernel_size = s->kernel_size;
    const int order = s->order;
    const float lambda = s->lambda;
    int offset = *offsetp;
    float g = lambda;
    float output, e;

    delay[offset + order] = input;

    output = fir_sample(s, input, delay, coeffs, tmp, offsetp);
    e = desired - output;

    for (int i = 0, pos = offset; i < order; i++, pos++) {
        const int ikernel_size = i * kernel_size;

        u[i] = 0.f;
        for (int k = 0, pos = offset; k < order; k++, pos++)
            u[i] += p[ikernel_size + k] * delay[pos];

        g += u[i] * delay[pos];
    }

    g = 1.f / g;

    for (int i = 0; i < order; i++) {
        const int ikernel_size = i * kernel_size;

        gains[i] = u[i] * g;
        coeffs[i] = coeffs[order + i] = coeffs[i] + gains[i] * e;
        tmp[i] = 0.f;
        for (int k = 0, pos = offset; k < order; k++, pos++)
            tmp[i] += p[ikernel_size + k] * delay[pos];
    }

    for (int i = 0; i < order; i++) {
        const int ikernel_size = i * kernel_size;

        for (int k = 0; k < order; k++)
            dp[ikernel_size + k] = gains[i] * tmp[k];
    }

    for (int i = 0; i < order; i++) {
        const int ikernel_size = i * kernel_size;

        for (int k = 0; k < order; k++)
            p[ikernel_size + k] = (p[ikernel_size + k] - (dp[ikernel_size + k] + dp[kernel_size * k + i]) * 0.5f) * lambda;
    }

    switch (s->output_mode) {
    case IN_MODE:       output = input;         break;
    case DESIRED_MODE:  output = desired;       break;
    case OUT_MODE:   output = desired - output; break;
    case NOISE_MODE: output = input - output;   break;
    case ERROR_MODE:                            break;
    }
    return output;
}

static int process_channels(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    AudioRLSContext *s = ctx->priv;
    AVFrame *out = arg;
    const int start = (out->ch_layout.nb_channels * jobnr) / nb_jobs;
    const int end = (out->ch_layout.nb_channels * (jobnr+1)) / nb_jobs;

    for (int c = start; c < end; c++) {
        const float *input = (const float *)s->frame[0]->extended_data[c];
        const float *desired = (const float *)s->frame[1]->extended_data[c];
        float *output = (float *)out->extended_data[c];

        for (int n = 0; n < out->nb_samples; n++) {
            output[n] = process_sample(s, input[n], desired[n], c);
            if (ctx->is_disabled)
                output[n] = input[n];
        }
    }

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    AudioRLSContext *s = ctx->priv;
    int i, ret, status;
    int nb_samples;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK_ALL(ctx->outputs[0], ctx);

    nb_samples = FFMIN(ff_inlink_queued_samples(ctx->inputs[0]),
                       ff_inlink_queued_samples(ctx->inputs[1]));
    for (i = 0; i < ctx->nb_inputs && nb_samples > 0; i++) {
        if (s->frame[i])
            continue;

        if (ff_inlink_check_available_samples(ctx->inputs[i], nb_samples) > 0) {
            ret = ff_inlink_consume_samples(ctx->inputs[i], nb_samples, nb_samples, &s->frame[i]);
            if (ret < 0)
                return ret;
        }
    }

    if (s->frame[0] && s->frame[1]) {
        AVFrame *out;

        out = ff_get_audio_buffer(ctx->outputs[0], s->frame[0]->nb_samples);
        if (!out) {
            av_frame_free(&s->frame[0]);
            av_frame_free(&s->frame[1]);
            return AVERROR(ENOMEM);
        }

        ff_filter_execute(ctx, process_channels, out, NULL,
                          FFMIN(ctx->outputs[0]->ch_layout.nb_channels, ff_filter_get_nb_threads(ctx)));

        out->pts = s->frame[0]->pts;

        av_frame_free(&s->frame[0]);
        av_frame_free(&s->frame[1]);

        ret = ff_filter_frame(ctx->outputs[0], out);
        if (ret < 0)
            return ret;
    }

    if (!nb_samples) {
        for (i = 0; i < 2; i++) {
            if (ff_inlink_acknowledge_status(ctx->inputs[i], &status, &pts)) {
                ff_outlink_set_status(ctx->outputs[0], status, pts);
                return 0;
            }
        }
    }

    if (ff_outlink_frame_wanted(ctx->outputs[0])) {
        for (i = 0; i < 2; i++) {
            if (ff_inlink_queued_samples(ctx->inputs[i]) > 0)
                continue;
            ff_inlink_request_frame(ctx->inputs[i]);
            return 0;
        }
    }
    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioRLSContext *s = ctx->priv;

    s->kernel_size = FFALIGN(s->order, 16);

    if (!s->offset)
        s->offset = ff_get_audio_buffer(outlink, 1);
    if (!s->delay)
        s->delay = ff_get_audio_buffer(outlink, 2 * s->kernel_size);
    if (!s->coeffs)
        s->coeffs = ff_get_audio_buffer(outlink, 2 * s->kernel_size);
    if (!s->gains)
        s->gains = ff_get_audio_buffer(outlink, s->kernel_size);
    if (!s->p)
        s->p = ff_get_audio_buffer(outlink, s->kernel_size * s->kernel_size);
    if (!s->dp)
        s->dp = ff_get_audio_buffer(outlink, s->kernel_size * s->kernel_size);
    if (!s->u)
        s->u = ff_get_audio_buffer(outlink, s->kernel_size);
    if (!s->tmp)
        s->tmp = ff_get_audio_buffer(outlink, s->kernel_size);

    if (!s->delay || !s->coeffs || !s->p || !s->dp || !s->gains || !s->offset || !s->u || !s->tmp)
        return AVERROR(ENOMEM);

    for (int ch = 0; ch < s->offset->ch_layout.nb_channels; ch++) {
        int *dst = (int *)s->offset->extended_data[ch];

        for (int i = 0; i < s->kernel_size; i++)
            dst[0] = s->kernel_size - 1;
    }

    for (int ch = 0; ch < s->p->ch_layout.nb_channels; ch++) {
        float *dst = (float *)s->p->extended_data[ch];

        for (int i = 0; i < s->kernel_size; i++)
            dst[i * s->kernel_size + i] = s->delta;
    }

    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    AudioRLSContext *s = ctx->priv;

    s->fdsp = avpriv_float_dsp_alloc(0);
    if (!s->fdsp)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioRLSContext *s = ctx->priv;

    av_freep(&s->fdsp);
    av_frame_free(&s->delay);
    av_frame_free(&s->coeffs);
    av_frame_free(&s->gains);
    av_frame_free(&s->offset);
    av_frame_free(&s->p);
    av_frame_free(&s->dp);
    av_frame_free(&s->u);
    av_frame_free(&s->tmp);
}

static const AVFilterPad inputs[] = {
    {
        .name = "input",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    {
        .name = "desired",
        .type = AVMEDIA_TYPE_AUDIO,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_output,
    },
};

const AVFilter ff_af_arls = {
    .name           = "arls",
    .description    = NULL_IF_CONFIG_SMALL("Apply Recursive Least Squares algorithm to first audio stream."),
    .priv_size      = sizeof(AudioRLSContext),
    .priv_class     = &arls_class,
    .init           = init,
    .uninit         = uninit,
    .activate       = activate,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_SINGLE_SAMPLEFMT(AV_SAMPLE_FMT_FLTP),
    .flags          = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                      AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
