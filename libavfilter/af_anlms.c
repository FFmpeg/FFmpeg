/*
 * Copyright (c) 2019 Paul B Mahol
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

#include "libavutil/channel_layout.h"
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
    NB_OMODES
};

typedef struct AudioNLMSContext {
    const AVClass *class;

    int order;
    float mu;
    float eps;
    float leakage;
    int output_mode;

    int kernel_size;
    AVFrame *offset;
    AVFrame *delay;
    AVFrame *coeffs;
    AVFrame *tmp;

    AVFrame *frame[2];

    int anlmf;

    AVFloatDSPContext *fdsp;
} AudioNLMSContext;

#define OFFSET(x) offsetof(AudioNLMSContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define AT AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption anlms_options[] = {
    { "order",   "set the filter order",   OFFSET(order),   AV_OPT_TYPE_INT,   {.i64=256},  1, INT16_MAX, A },
    { "mu",      "set the filter mu",      OFFSET(mu),      AV_OPT_TYPE_FLOAT, {.dbl=0.75}, 0, 2, AT },
    { "eps",     "set the filter eps",     OFFSET(eps),     AV_OPT_TYPE_FLOAT, {.dbl=1},    0, 1, AT },
    { "leakage", "set the filter leakage", OFFSET(leakage), AV_OPT_TYPE_FLOAT, {.dbl=0},    0, 1, AT },
    { "out_mode", "set output mode",       OFFSET(output_mode), AV_OPT_TYPE_INT, {.i64=OUT_MODE}, 0, NB_OMODES-1, AT, "mode" },
    {  "i", "input",                 0,          AV_OPT_TYPE_CONST,    {.i64=IN_MODE},      0, 0, AT, "mode" },
    {  "d", "desired",               0,          AV_OPT_TYPE_CONST,    {.i64=DESIRED_MODE}, 0, 0, AT, "mode" },
    {  "o", "output",                0,          AV_OPT_TYPE_CONST,    {.i64=OUT_MODE},     0, 0, AT, "mode" },
    {  "n", "noise",                 0,          AV_OPT_TYPE_CONST,    {.i64=NOISE_MODE},   0, 0, AT, "mode" },
    { NULL }
};

AVFILTER_DEFINE_CLASS_EXT(anlms, "anlm(f|s)", anlms_options);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_NONE
    };
    int ret = ff_set_common_all_channel_counts(ctx);
    if (ret < 0)
        return ret;

    ret = ff_set_common_formats_from_list(ctx, sample_fmts);
    if (ret < 0)
        return ret;

    return ff_set_common_all_samplerates(ctx);
}

static float fir_sample(AudioNLMSContext *s, float sample, float *delay,
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

static float process_sample(AudioNLMSContext *s, float input, float desired,
                            float *delay, float *coeffs, float *tmp, int *offsetp)
{
    const int order = s->order;
    const float leakage = s->leakage;
    const float mu = s->mu;
    const float a = 1.f - leakage * mu;
    float sum, output, e, norm, b;
    int offset = *offsetp;

    delay[offset + order] = input;

    output = fir_sample(s, input, delay, coeffs, tmp, offsetp);
    e = desired - output;

    sum = s->fdsp->scalarproduct_float(delay, delay, s->kernel_size);

    norm = s->eps + sum;
    b = mu * e / norm;
    if (s->anlmf)
        b *= 4.f * e * e;

    memcpy(tmp, delay + offset, order * sizeof(float));

    s->fdsp->vector_fmul_scalar(coeffs, coeffs, a, s->kernel_size);

    s->fdsp->vector_fmac_scalar(coeffs, tmp, b, s->kernel_size);

    memcpy(coeffs + order, coeffs, order * sizeof(float));

    switch (s->output_mode) {
    case IN_MODE:       output = input;         break;
    case DESIRED_MODE:  output = desired;       break;
    case OUT_MODE: /*output = output;*/         break;
    case NOISE_MODE: output = desired - output; break;
    }
    return output;
}

static int process_channels(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    AudioNLMSContext *s = ctx->priv;
    AVFrame *out = arg;
    const int start = (out->ch_layout.nb_channels * jobnr) / nb_jobs;
    const int end = (out->ch_layout.nb_channels * (jobnr+1)) / nb_jobs;

    for (int c = start; c < end; c++) {
        const float *input = (const float *)s->frame[0]->extended_data[c];
        const float *desired = (const float *)s->frame[1]->extended_data[c];
        float *delay = (float *)s->delay->extended_data[c];
        float *coeffs = (float *)s->coeffs->extended_data[c];
        float *tmp = (float *)s->tmp->extended_data[c];
        int *offset = (int *)s->offset->extended_data[c];
        float *output = (float *)out->extended_data[c];

        for (int n = 0; n < out->nb_samples; n++)
            output[n] = process_sample(s, input[n], desired[n], delay, coeffs, tmp, offset);
    }

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    AudioNLMSContext *s = ctx->priv;
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
    AudioNLMSContext *s = ctx->priv;

    s->anlmf = !strcmp(ctx->filter->name, "anlmf");
    s->kernel_size = FFALIGN(s->order, 16);

    if (!s->offset)
        s->offset = ff_get_audio_buffer(outlink, 1);
    if (!s->delay)
        s->delay = ff_get_audio_buffer(outlink, 2 * s->kernel_size);
    if (!s->coeffs)
        s->coeffs = ff_get_audio_buffer(outlink, 2 * s->kernel_size);
    if (!s->tmp)
        s->tmp = ff_get_audio_buffer(outlink, s->kernel_size);
    if (!s->delay || !s->coeffs || !s->offset || !s->tmp)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    AudioNLMSContext *s = ctx->priv;

    s->fdsp = avpriv_float_dsp_alloc(0);
    if (!s->fdsp)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioNLMSContext *s = ctx->priv;

    av_freep(&s->fdsp);
    av_frame_free(&s->delay);
    av_frame_free(&s->coeffs);
    av_frame_free(&s->offset);
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

const AVFilter ff_af_anlms = {
    .name           = "anlms",
    .description    = NULL_IF_CONFIG_SMALL("Apply Normalized Least-Mean-Squares algorithm to first audio stream."),
    .priv_size      = sizeof(AudioNLMSContext),
    .priv_class     = &anlms_class,
    .init           = init,
    .uninit         = uninit,
    .activate       = activate,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_QUERY_FUNC(query_formats),
    .flags          = AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};

const AVFilter ff_af_anlmf = {
    .name           = "anlmf",
    .description    = NULL_IF_CONFIG_SMALL("Apply Normalized Least-Mean-Fourth algorithm to first audio stream."),
    .priv_size      = sizeof(AudioNLMSContext),
    .priv_class     = &anlms_class,
    .init           = init,
    .uninit         = uninit,
    .activate       = activate,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_QUERY_FUNC(query_formats),
    .flags          = AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
