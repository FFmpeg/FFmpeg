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
    void (*filter)(void **dst, void **prv, const void **src,
                   int nb_samples, int channels, float mult, int clip);
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

static void filter_flt(void **d, void **p, const void **s,
                       int nb_samples, int channels,
                       float mult, int clip)
{
    const float *src = s[0];
    float *dst = d[0];
    float *prv = p[0];
    int n, c;

    for (n = 0; n < nb_samples; n++) {
        for (c = 0; c < channels; c++) {
            float current = src[c];

            dst[c] = current + (current - prv[c]) * mult;
            prv[c] = current;
            if (clip) {
                dst[c] = av_clipf(dst[c], -1, 1);
            }
        }

        dst += c;
        src += c;
    }
}

static void filter_dbl(void **d, void **p, const void **s,
                       int nb_samples, int channels,
                       float mult, int clip)
{
    const double *src = s[0];
    double *dst = d[0];
    double *prv = p[0];
    int n, c;

    for (n = 0; n < nb_samples; n++) {
        for (c = 0; c < channels; c++) {
            double current = src[c];

            dst[c] = current + (current - prv[c]) * mult;
            prv[c] = current;
            if (clip) {
                dst[c] = av_clipd(dst[c], -1, 1);
            }
        }

        dst += c;
        src += c;
    }
}

static void filter_fltp(void **d, void **p, const void **s,
                        int nb_samples, int channels,
                        float mult, int clip)
{
    int n, c;

    for (c = 0; c < channels; c++) {
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
}

static void filter_dblp(void **d, void **p, const void **s,
                        int nb_samples, int channels,
                        float mult, int clip)
{
    int n, c;

    for (c = 0; c < channels; c++) {
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

    s->filter((void **)out->extended_data, (void **)s->prev->extended_data, (const void **)in->extended_data,
              in->nb_samples, in->channels, ctx->is_disabled ? 0.f : s->mult, s->clip);

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
    .flags          = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
    .process_command = ff_filter_process_command,
};
