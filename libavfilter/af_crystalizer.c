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

#define filters(fmt, type, inverse, clp, inverset, clip, one, clip_fn, packed)  \
static int filter_## inverse ##_## fmt ##_## clp(AVFilterContext *ctx, \
                                                  void *arg, int jobnr,\
                                                  int nb_jobs)         \
{                                                                      \
    ThreadData *td = arg;                                              \
    void **d = td->d;                                                  \
    void **p = td->p;                                                  \
    const void **s = td->s;                                            \
    const int nb_samples = td->nb_samples;                             \
    const int channels = td->channels;                                 \
    const type mult = td->mult;                                        \
    const type scale = one / (-mult + one);                            \
    const int start = (channels * jobnr) / nb_jobs;                    \
    const int end = (channels * (jobnr+1)) / nb_jobs;                  \
                                                                       \
    if (packed) {                                                      \
        type *prv = p[0];                                              \
        for (int c = start; c < end; c++) {                            \
            const type *src = s[0];                                    \
            type *dst = d[0];                                          \
                                                                       \
            for (int n = 0; n < nb_samples; n++) {                     \
                type current = src[c];                                 \
                                                                       \
                if (inverset) {                                        \
                    dst[c] = (current - prv[c] * mult) * scale;        \
                    prv[c] = dst[c];                                   \
                } else {                                               \
                    dst[c] = current + (current - prv[c]) * mult;      \
                    prv[c] = current;                                  \
                }                                                      \
                if (clip) {                                            \
                    dst[c] = clip_fn(dst[c], -one, one);               \
                }                                                      \
                                                                       \
                dst += channels;                                       \
                src += channels;                                       \
            }                                                          \
        }                                                              \
    } else {                                                           \
        for (int c = start; c < end; c++) {                            \
            const type *src = s[c];                                    \
            type *dst = d[c];                                          \
            type *prv = p[c];                                          \
                                                                       \
            for (int n = 0; n < nb_samples; n++) {                     \
                type current = src[n];                                 \
                                                                       \
                if (inverset) {                                        \
                    dst[n] = (current - prv[0] * mult) * scale;        \
                    prv[0] = dst[n];                                   \
                } else {                                               \
                    dst[n] = current + (current - prv[0]) * mult;      \
                    prv[0] = current;                                  \
                }                                                      \
                if (clip) {                                            \
                    dst[n] = clip_fn(dst[n], -one, one);               \
                }                                                      \
            }                                                          \
        }                                                              \
    }                                                                  \
                                                                       \
    return 0;                                                          \
}

filters(flt, float, inverse, noclip, 1, 0, 1.f, av_clipf, 1)
filters(flt, float, inverse, clip, 1, 1, 1.f, av_clipf, 1)
filters(flt, float, noinverse, noclip, 0, 0, 1.f, av_clipf, 1)
filters(flt, float, noinverse, clip, 0, 1, 1.f, av_clipf, 1)

filters(fltp, float, inverse, noclip, 1, 0, 1.f, av_clipf, 0)
filters(fltp, float, inverse, clip, 1, 1, 1.f, av_clipf, 0)
filters(fltp, float, noinverse, noclip, 0, 0, 1.f, av_clipf, 0)
filters(fltp, float, noinverse, clip, 0, 1, 1.f, av_clipf, 0)

filters(dbl, double, inverse, noclip, 1, 0, 1.0, av_clipd, 1)
filters(dbl, double, inverse, clip, 1, 1, 1.0, av_clipd, 1)
filters(dbl, double, noinverse, noclip, 0, 0, 1.0, av_clipd, 1)
filters(dbl, double, noinverse, clip, 0, 1, 1.0, av_clipd, 1)

filters(dblp, double, inverse, noclip, 1, 0, 1.0, av_clipd, 0)
filters(dblp, double, inverse, clip, 1, 1, 1.0, av_clipd, 0)
filters(dblp, double, noinverse, noclip, 0, 0, 1.0, av_clipd, 0)
filters(dblp, double, noinverse, clip, 0, 1, 1.0, av_clipd, 0)

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
    td.channels = in->ch_layout.nb_channels;
    td.mult = ctx->is_disabled ? 0.f : s->mult;
    ff_filter_execute(ctx, s->filter[td.mult >= 0.f][s->clip], &td, NULL,
                      FFMIN(inlink->ch_layout.nb_channels, ff_filter_get_nb_threads(ctx)));

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
};

const AVFilter ff_af_crystalizer = {
    .name           = "crystalizer",
    .description    = NULL_IF_CONFIG_SMALL("Simple audio noise sharpening filter."),
    .priv_size      = sizeof(CrystalizerContext),
    .priv_class     = &crystalizer_class,
    .uninit         = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    FILTER_SAMPLEFMTS(AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_FLTP,
                      AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_DBLP),
    .process_command = ff_filter_process_command,
    .flags          = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                      AVFILTER_FLAG_SLICE_THREADS,
};
