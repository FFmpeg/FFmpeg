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

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/float_dsp.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "filters.h"

enum OutModes {
    IN_MODE,
    DESIRED_MODE,
    OUT_MODE,
    NOISE_MODE,
    ERROR_MODE,
    NB_OMODES
};

typedef struct AudioAPContext {
    const AVClass *class;

    int order;
    int projection;
    float mu;
    float delta;
    int output_mode;
    int precision;

    int kernel_size;
    AVFrame *offset;
    AVFrame *delay;
    AVFrame *coeffs;
    AVFrame *e;
    AVFrame *p;
    AVFrame *x;
    AVFrame *w;
    AVFrame *dcoeffs;
    AVFrame *tmp;
    AVFrame *tmpm;
    AVFrame *itmpm;

    void **tmpmp;
    void **itmpmp;

    AVFrame *frame[2];

    int (*filter_channels)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);

    AVFloatDSPContext *fdsp;
} AudioAPContext;

#define OFFSET(x) offsetof(AudioAPContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define AT AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption aap_options[] = {
    { "order",      "set the filter order",      OFFSET(order),       AV_OPT_TYPE_INT,   {.i64=16},   1, INT16_MAX, A },
    { "projection", "set the filter projection", OFFSET(projection),  AV_OPT_TYPE_INT,   {.i64=2},    1, 256, A },
    { "mu",         "set the filter mu",         OFFSET(mu),          AV_OPT_TYPE_FLOAT, {.dbl=0.0001},0,1, AT },
    { "delta",      "set the filter delta",      OFFSET(delta),       AV_OPT_TYPE_FLOAT, {.dbl=0.001},0, 1, AT },
    { "out_mode",   "set output mode",           OFFSET(output_mode), AV_OPT_TYPE_INT, {.i64=OUT_MODE}, 0, NB_OMODES-1, AT, .unit = "mode" },
    {  "i", "input",   0, AV_OPT_TYPE_CONST, {.i64=IN_MODE},      0, 0, AT, .unit = "mode" },
    {  "d", "desired", 0, AV_OPT_TYPE_CONST, {.i64=DESIRED_MODE}, 0, 0, AT, .unit = "mode" },
    {  "o", "output",  0, AV_OPT_TYPE_CONST, {.i64=OUT_MODE},     0, 0, AT, .unit = "mode" },
    {  "n", "noise",   0, AV_OPT_TYPE_CONST, {.i64=NOISE_MODE},   0, 0, AT, .unit = "mode" },
    {  "e", "error",   0, AV_OPT_TYPE_CONST, {.i64=ERROR_MODE},   0, 0, AT, .unit = "mode" },
    { "precision", "set processing precision", OFFSET(precision),  AV_OPT_TYPE_INT,    {.i64=0},   0, 2, A, .unit = "precision" },
    {   "auto",  "set auto processing precision",                  0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, A, .unit = "precision" },
    {   "float", "set single-floating point processing precision", 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, A, .unit = "precision" },
    {   "double","set double-floating point processing precision", 0, AV_OPT_TYPE_CONST, {.i64=2}, 0, 0, A, .unit = "precision" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(aap);

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    const AudioAPContext *s = ctx->priv;
    static const enum AVSampleFormat sample_fmts[3][3] = {
        { AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_DBLP, AV_SAMPLE_FMT_NONE },
        { AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_NONE },
        { AV_SAMPLE_FMT_DBLP, AV_SAMPLE_FMT_NONE },
    };
    int ret;

    if ((ret = ff_set_common_formats_from_list2(ctx, cfg_in, cfg_out,
                                                sample_fmts[s->precision])) < 0)
        return ret;

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    AudioAPContext *s = ctx->priv;
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

        ff_filter_execute(ctx, s->filter_channels, out, NULL,
                          FFMIN(ctx->outputs[0]->ch_layout.nb_channels, ff_filter_get_nb_threads(ctx)));

        out->pts = s->frame[0]->pts;
        out->duration = s->frame[0]->duration;

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
            if (s->frame[i] || ff_inlink_queued_samples(ctx->inputs[i]) > 0)
                continue;
            ff_inlink_request_frame(ctx->inputs[i]);
            return 0;
        }
    }
    return 0;
}

#define DEPTH 32
#include "aap_template.c"

#undef DEPTH
#define DEPTH 64
#include "aap_template.c"

static int config_output(AVFilterLink *outlink)
{
    const int channels = outlink->ch_layout.nb_channels;
    AVFilterContext *ctx = outlink->src;
    AudioAPContext *s = ctx->priv;

    s->kernel_size = FFALIGN(s->order, 16);

    if (!s->offset)
        s->offset = ff_get_audio_buffer(outlink, 3);
    if (!s->delay)
        s->delay = ff_get_audio_buffer(outlink, 2 * s->kernel_size);
    if (!s->dcoeffs)
        s->dcoeffs = ff_get_audio_buffer(outlink, s->kernel_size);
    if (!s->coeffs)
        s->coeffs = ff_get_audio_buffer(outlink, 2 * s->kernel_size);
    if (!s->e)
        s->e = ff_get_audio_buffer(outlink, 2 * s->projection);
    if (!s->p)
        s->p = ff_get_audio_buffer(outlink, s->projection + 1);
    if (!s->x)
        s->x = ff_get_audio_buffer(outlink, 2 * (s->projection + s->order));
    if (!s->w)
        s->w = ff_get_audio_buffer(outlink, s->projection);
    if (!s->tmp)
        s->tmp = ff_get_audio_buffer(outlink, s->kernel_size);
    if (!s->tmpm)
        s->tmpm = ff_get_audio_buffer(outlink, s->projection * s->projection);
    if (!s->itmpm)
        s->itmpm = ff_get_audio_buffer(outlink, s->projection * s->projection);

    if (!s->tmpmp)
        s->tmpmp = av_calloc(s->projection * channels, sizeof(*s->tmpmp));
    if (!s->itmpmp)
        s->itmpmp = av_calloc(s->projection * channels, sizeof(*s->itmpmp));

    if (!s->offset || !s->delay || !s->dcoeffs || !s->coeffs || !s->tmpmp || !s->itmpmp ||
        !s->e || !s->p || !s->x || !s->w || !s->tmp || !s->tmpm || !s->itmpm)
        return AVERROR(ENOMEM);

    switch (outlink->format) {
    case AV_SAMPLE_FMT_DBLP:
        for (int ch = 0; ch < channels; ch++) {
            double *itmpm = (double *)s->itmpm->extended_data[ch];
            double *tmpm = (double *)s->tmpm->extended_data[ch];
            double **itmpmp = (double **)&s->itmpmp[s->projection * ch];
            double **tmpmp = (double **)&s->tmpmp[s->projection * ch];

            for (int i = 0; i < s->projection; i++) {
                itmpmp[i] = &itmpm[i * s->projection];
                tmpmp[i] = &tmpm[i * s->projection];
            }
        }

        s->filter_channels = filter_channels_double;
        break;
    case AV_SAMPLE_FMT_FLTP:
        for (int ch = 0; ch < channels; ch++) {
            float *itmpm = (float *)s->itmpm->extended_data[ch];
            float *tmpm = (float *)s->tmpm->extended_data[ch];
            float **itmpmp = (float **)&s->itmpmp[s->projection * ch];
            float **tmpmp = (float **)&s->tmpmp[s->projection * ch];

            for (int i = 0; i < s->projection; i++) {
                itmpmp[i] = &itmpm[i * s->projection];
                tmpmp[i] = &tmpm[i * s->projection];
            }
        }

        s->filter_channels = filter_channels_float;
        break;
    }

    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    AudioAPContext *s = ctx->priv;

    s->fdsp = avpriv_float_dsp_alloc(0);
    if (!s->fdsp)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioAPContext *s = ctx->priv;

    av_freep(&s->fdsp);

    av_frame_free(&s->offset);
    av_frame_free(&s->delay);
    av_frame_free(&s->dcoeffs);
    av_frame_free(&s->coeffs);
    av_frame_free(&s->e);
    av_frame_free(&s->p);
    av_frame_free(&s->w);
    av_frame_free(&s->x);
    av_frame_free(&s->tmp);
    av_frame_free(&s->tmpm);
    av_frame_free(&s->itmpm);

    av_freep(&s->tmpmp);
    av_freep(&s->itmpmp);
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

const FFFilter ff_af_aap = {
    .p.name         = "aap",
    .p.description  = NULL_IF_CONFIG_SMALL("Apply Affine Projection algorithm to first audio stream."),
    .p.priv_class   = &aap_class,
    .p.flags        = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                      AVFILTER_FLAG_SLICE_THREADS,
    .priv_size      = sizeof(AudioAPContext),
    .init           = init,
    .uninit         = uninit,
    .activate       = activate,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_QUERY_FUNC2(query_formats),
    .process_command = ff_filter_process_command,
};
