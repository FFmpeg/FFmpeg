/*
 * Copyright (c) 2016 The FFmpeg Project
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
#include "libavutil/opt.h"
#include "avfilter.h"
#include "audio.h"
#include "formats.h"

typedef struct CrystalizerContext {
    const AVClass *class;
    float mult;
    int clip;
    AVFrame *prev;
    int (*filter)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} CrystalizerContext;

#define OFFSET(x) offsetof(CrystalizerContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption crystalizer_options[] = {
    { "i", "set intensity",    OFFSET(mult), AV_OPT_TYPE_FLOAT, {.dbl=2.0}, 0, 10, A },
    { "c", "enable clipping",  OFFSET(clip), AV_OPT_TYPE_BOOL,  {.i64=1},   0,  1, A },
    { NULL }
};

AVFILTER_DEFINE_CLASS(crystalizer);

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_DBLP,
        AV_SAMPLE_FMT_NONE
    };
    int ret;

    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ret = ff_set_common_formats(ctx, formats);
    if (ret < 0)
        return ret;

    layouts = ff_all_channel_counts();
    if (!layouts)
        return AVERROR(ENOMEM);

    ret = ff_set_common_channel_layouts(ctx, layouts);
    if (ret < 0)
        return ret;

    formats = ff_all_samplerates();
    return ff_set_common_samplerates(ctx, formats);
}

typedef struct ThreadData {
    void **d;
    void **p;
    const void **s;
    int nb_samples;
    int channels;
    float mult;
    int clip;
} ThreadData;

static int filter_flt(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ThreadData *td = arg;
    void **d = td->d;
    void **p = td->p;
    const void **s = td->s;
    const int nb_samples = td->nb_samples;
    const int channels = td->channels;
    float mult = td->mult;
    const int clip = td->clip;
    const int start = (channels * jobnr) / nb_jobs;
    const int end = (channels * (jobnr+1)) / nb_jobs;
    float *prv = p[0];
    int n, c;

    for (c = start; c < end; c++) {
        const float *src = s[0];
        float *dst = d[0];

        for (n = 0; n < nb_samples; n++) {
            float current = src[c];
            dst[c] = current + (current - prv[c]) * mult;
            prv[c] = current;
            if (clip) {
                dst[c] = av_clipf(dst[c], -1, 1);
            }

            dst += channels;
            src += channels;
        }
    }

    return 0;
}

static int filter_dbl(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ThreadData *td = arg;
    void **d = td->d;
    void **p = td->p;
    const void **s = td->s;
    const int nb_samples = td->nb_samples;
    const int channels = td->channels;
    float mult = td->mult;
    const int clip = td->clip;
    const int start = (channels * jobnr) / nb_jobs;
    const int end = (channels * (jobnr+1)) / nb_jobs;
    double *prv = p[0];
    int n, c;

    for (c = start; c < end; c++) {
        const double *src = s[0];
        double *dst = d[0];

        for (n = 0; n < nb_samples; n++) {
            double current = src[c];

            dst[c] = current + (current - prv[c]) * mult;
            prv[c] = current;
            if (clip) {
                dst[c] = av_clipd(dst[c], -1, 1);
            }

            dst += channels;
            src += channels;
        }
    }

    return 0;
}

static int filter_fltp(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ThreadData *td = arg;
    void **d = td->d;
    void **p = td->p;
    const void **s = td->s;
    const int nb_samples = td->nb_samples;
    const int channels = td->channels;
    float mult = td->mult;
    const int clip = td->clip;
    const int start = (channels * jobnr) / nb_jobs;
    const int end = (channels * (jobnr+1)) / nb_jobs;
    int n, c;

    for (c = start; c < end; c++) {
        const float *src = s[c];
        float *dst = d[c];
        float *prv = p[c];

        for (n = 0; n < nb_samples; n++) {
            float current = src[n];

            dst[n] = current + (current - prv[0]) * mult;
            prv[0] = current;
            if (clip) {
                dst[n] = av_clipf(dst[n], -1, 1);
            }
        }
    }

    return 0;
}

static int filter_dblp(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ThreadData *td = arg;
    void **d = td->d;
    void **p = td->p;
    const void **s = td->s;
    const int nb_samples = td->nb_samples;
    const int channels = td->channels;
    float mult = td->mult;
    const int clip = td->clip;
    const int start = (channels * jobnr) / nb_jobs;
    const int end = (channels * (jobnr+1)) / nb_jobs;
    int n, c;

    for (c = start; c < end; c++) {
        const double *src = s[c];
        double *dst = d[c];
        double *prv = p[c];

        for (n = 0; n < nb_samples; n++) {
            double current = src[n];

            dst[n] = current + (current - prv[0]) * mult;
            prv[0] = current;
            if (clip) {
                dst[n] = av_clipd(dst[n], -1, 1);
            }
        }
    }

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    CrystalizerContext *s = ctx->priv;

    switch (inlink->format) {
    case AV_SAMPLE_FMT_FLT:  s->filter = filter_flt;  break;
    case AV_SAMPLE_FMT_DBL:  s->filter = filter_dbl;  break;
    case AV_SAMPLE_FMT_FLTP: s->filter = filter_fltp; break;
    case AV_SAMPLE_FMT_DBLP: s->filter = filter_dblp; break;
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    CrystalizerContext *s = ctx->priv;
    AVFrame *out;
    ThreadData td;

    if (!s->prev) {
        s->prev = ff_get_audio_buffer(inlink, 1);
        if (!s->prev) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
    }

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_audio_buffer(outlink, in->nb_samples);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    td.d = (void **)out->extended_data;
    td.s = (const void **)in->extended_data;
    td.p = (void **)s->prev->extended_data;
    td.nb_samples = in->nb_samples;
    td.channels = in->channels;
    td.mult = ctx->is_disabled ? 0.f : s->mult;
    td.clip = s->clip;
    ctx->internal->execute(ctx, s->filter, &td, NULL, FFMIN(inlink->channels,
                                                            ff_filter_get_nb_threads(ctx)));

    if (out != in)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    CrystalizerContext *s = ctx->priv;

    av_frame_free(&s->prev);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_crystalizer = {
    .name           = "crystalizer",
    .description    = NULL_IF_CONFIG_SMALL("Simple expand audio dynamic range filter."),
    .query_formats  = query_formats,
    .priv_size      = sizeof(CrystalizerContext),
    .priv_class     = &crystalizer_class,
    .uninit         = uninit,
    .inputs         = inputs,
    .outputs        = outputs,
    .process_command = ff_filter_process_command,
    .flags          = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                      AVFILTER_FLAG_SLICE_THREADS,
};
