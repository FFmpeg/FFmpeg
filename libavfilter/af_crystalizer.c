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
    int (*filter[2][2])(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} CrystalizerContext;

#define OFFSET(x) offsetof(CrystalizerContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption crystalizer_options[] = {
    { "i", "set intensity",    OFFSET(mult), AV_OPT_TYPE_FLOAT, {.dbl=2.0},-10, 10, A },
    { "c", "enable clipping",  OFFSET(clip), AV_OPT_TYPE_BOOL,  {.i64=1},   0,  1, A },
    { NULL }
};

AVFILTER_DEFINE_CLASS(crystalizer);

typedef struct ThreadData {
    void **d;
    void **p;
    const void **s;
    int nb_samples;
    int channels;
    float mult;
} ThreadData;

static av_always_inline int filter_flt(AVFilterContext *ctx, void *arg,
                                       int jobnr, int nb_jobs,
                                       int inverse, int clip)
{
    ThreadData *td = arg;
    void **d = td->d;
    void **p = td->p;
    const void **s = td->s;
    const int nb_samples = td->nb_samples;
    const int channels = td->channels;
    const float mult = td->mult;
    const float scale = 1.f / (-mult + 1.f);
    const int start = (channels * jobnr) / nb_jobs;
    const int end = (channels * (jobnr+1)) / nb_jobs;
    float *prv = p[0];
    int n, c;

    for (c = start; c < end; c++) {
        const float *src = s[0];
        float *dst = d[0];

        for (n = 0; n < nb_samples; n++) {
            float current = src[c];

            if (inverse) {
                dst[c] = (current - prv[c] * mult) * scale;
                prv[c] = dst[c];
            } else {
                dst[c] = current + (current - prv[c]) * mult;
                prv[c] = current;
            }
            if (clip) {
                dst[c] = av_clipf(dst[c], -1.f, 1.f);
            }

            dst += channels;
            src += channels;
        }
    }

    return 0;
}

static av_always_inline int filter_dbl(AVFilterContext *ctx, void *arg,
                                       int jobnr, int nb_jobs,
                                       int inverse, int clip)
{
    ThreadData *td = arg;
    void **d = td->d;
    void **p = td->p;
    const void **s = td->s;
    const int nb_samples = td->nb_samples;
    const int channels = td->channels;
    const double mult = td->mult;
    const double scale = 1.0 / (-mult + 1.0);
    const int start = (channels * jobnr) / nb_jobs;
    const int end = (channels * (jobnr+1)) / nb_jobs;
    double *prv = p[0];
    int n, c;

    for (c = start; c < end; c++) {
        const double *src = s[0];
        double *dst = d[0];

        for (n = 0; n < nb_samples; n++) {
            double current = src[c];

            if (inverse) {
                dst[c] = (current - prv[c] * mult) * scale;
                prv[c] = dst[c];
            } else {
                dst[c] = current + (current - prv[c]) * mult;
                prv[c] = current;
            }
            if (clip) {
                dst[c] = av_clipd(dst[c], -1., 1.);
            }

            dst += channels;
            src += channels;
        }
    }

    return 0;
}

static av_always_inline int filter_fltp(AVFilterContext *ctx, void *arg,
                                        int jobnr, int nb_jobs,
                                        int inverse, int clip)
{
    ThreadData *td = arg;
    void **d = td->d;
    void **p = td->p;
    const void **s = td->s;
    const int nb_samples = td->nb_samples;
    const int channels = td->channels;
    const float mult = td->mult;
    const float scale = 1.f / (-mult + 1.f);
    const int start = (channels * jobnr) / nb_jobs;
    const int end = (channels * (jobnr+1)) / nb_jobs;
    int n, c;

    for (c = start; c < end; c++) {
        const float *src = s[c];
        float *dst = d[c];
        float *prv = p[c];

        for (n = 0; n < nb_samples; n++) {
            float current = src[n];

            if (inverse) {
                dst[n] = (current - prv[0] * mult) * scale;
                prv[0] = dst[n];
            } else {
                dst[n] = current + (current - prv[0]) * mult;
                prv[0] = current;
            }
            if (clip) {
                dst[n] = av_clipf(dst[n], -1.f, 1.f);
            }
        }
    }

    return 0;
}

static av_always_inline int filter_dblp(AVFilterContext *ctx, void *arg,
                                        int jobnr, int nb_jobs,
                                        int inverse, int clip)
{
    ThreadData *td = arg;
    void **d = td->d;
    void **p = td->p;
    const void **s = td->s;
    const int nb_samples = td->nb_samples;
    const int channels = td->channels;
    const double mult = td->mult;
    const double scale = 1.0 / (-mult + 1.0);
    const int start = (channels * jobnr) / nb_jobs;
    const int end = (channels * (jobnr+1)) / nb_jobs;
    int n, c;

    for (c = start; c < end; c++) {
        const double *src = s[c];
        double *dst = d[c];
        double *prv = p[c];

        for (n = 0; n < nb_samples; n++) {
            double current = src[n];

            if (inverse) {
                dst[n] = (current - prv[0] * mult) * scale;
                prv[0] = dst[n];
            } else {
                dst[n] = current + (current - prv[0]) * mult;
                prv[0] = current;
            }
            if (clip) {
                dst[n] = av_clipd(dst[n], -1., 1.);
            }
        }
    }

    return 0;
}

#define filters(fmt, inverse, clip, i, c) \
static int filter_## inverse ##_## fmt ##_## clip(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs) \
{ \
    return filter_## fmt(ctx, arg, jobnr, nb_jobs, i, c); \
}

filters(flt, inverse, noclip, 1, 0)
filters(flt, inverse, clip, 1, 1)
filters(flt, noinverse, noclip, 0, 0)
filters(flt, noinverse, clip, 0, 1)

filters(fltp, inverse, noclip, 1, 0)
filters(fltp, inverse, clip, 1, 1)
filters(fltp, noinverse, noclip, 0, 0)
filters(fltp, noinverse, clip, 0, 1)

filters(dbl, inverse, noclip, 1, 0)
filters(dbl, inverse, clip, 1, 1)
filters(dbl, noinverse, noclip, 0, 0)
filters(dbl, noinverse, clip, 0, 1)

filters(dblp, inverse, noclip, 1, 0)
filters(dblp, inverse, clip, 1, 1)
filters(dblp, noinverse, noclip, 0, 0)
filters(dblp, noinverse, clip, 0, 1)

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    CrystalizerContext *s = ctx->priv;

    switch (inlink->format) {
    case AV_SAMPLE_FMT_FLT:
        s->filter[0][0] = filter_inverse_flt_noclip;
        s->filter[1][0] = filter_noinverse_flt_noclip;
        s->filter[0][1] = filter_inverse_flt_clip;
        s->filter[1][1] = filter_noinverse_flt_clip;
        break;
    case AV_SAMPLE_FMT_FLTP:
        s->filter[0][0] = filter_inverse_fltp_noclip;
        s->filter[1][0] = filter_noinverse_fltp_noclip;
        s->filter[0][1] = filter_inverse_fltp_clip;
        s->filter[1][1] = filter_noinverse_fltp_clip;
        break;
    case AV_SAMPLE_FMT_DBL:
        s->filter[0][0] = filter_inverse_dbl_noclip;
        s->filter[1][0] = filter_noinverse_dbl_noclip;
        s->filter[0][1] = filter_inverse_dbl_clip;
        s->filter[1][1] = filter_noinverse_dbl_clip;
        break;
    case AV_SAMPLE_FMT_DBLP:
        s->filter[0][0] = filter_inverse_dblp_noclip;
        s->filter[1][0] = filter_noinverse_dblp_noclip;
        s->filter[0][1] = filter_inverse_dblp_clip;
        s->filter[1][1] = filter_noinverse_dblp_clip;
        break;
    default:
        return AVERROR_BUG;
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
    ff_filter_execute(ctx, s->filter[td.mult >= 0.f][s->clip], &td, NULL,
                      FFMIN(inlink->channels, ff_filter_get_nb_threads(ctx)));

    if (out != in)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    CrystalizerContext *s = ctx->priv;

    av_frame_free(&s->prev);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    return config_input(ctx->inputs[0]);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
};

const AVFilter ff_af_crystalizer = {
    .name           = "crystalizer",
    .description    = NULL_IF_CONFIG_SMALL("Simple audio noise sharpening filter."),
    .priv_size      = sizeof(CrystalizerContext),
    .priv_class     = &crystalizer_class,
    .uninit         = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_SAMPLEFMTS(AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_FLTP,
                      AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_DBLP),
    .process_command = process_command,
    .flags          = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                      AVFILTER_FLAG_SLICE_THREADS,
};
