/*
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

#include "audio.h"
#include "avfilter.h"
#include "internal.h"

typedef struct ADerivativeContext {
    const AVClass *class;
    AVFrame *prev;
    void (*filter)(void **dst, void **prv, const void **src,
                   int nb_samples, int channels);
} ADerivativeContext;

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    static const enum AVSampleFormat derivative_sample_fmts[] = {
        AV_SAMPLE_FMT_S16P, AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_S32P, AV_SAMPLE_FMT_DBLP,
        AV_SAMPLE_FMT_NONE
    };
    static const enum AVSampleFormat integral_sample_fmts[] = {
        AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_DBLP,
        AV_SAMPLE_FMT_NONE
    };
    int ret;

    formats = ff_make_format_list(strcmp(ctx->filter->name, "aintegral") ?
                                  derivative_sample_fmts : integral_sample_fmts);
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

#define DERIVATIVE(name, type)                                          \
static void aderivative_## name ##p(void **d, void **p, const void **s, \
                                    int nb_samples, int channels)       \
{                                                                       \
    int n, c;                                                           \
                                                                        \
    for (c = 0; c < channels; c++) {                                    \
        const type *src = s[c];                                         \
        type *dst = d[c];                                               \
        type *prv = p[c];                                               \
                                                                        \
        for (n = 0; n < nb_samples; n++) {                              \
            const type current = src[n];                                \
                                                                        \
            dst[n] = current - prv[0];                                  \
            prv[0] = current;                                           \
        }                                                               \
    }                                                                   \
}

DERIVATIVE(flt, float)
DERIVATIVE(dbl, double)
DERIVATIVE(s16, int16_t)
DERIVATIVE(s32, int32_t)

#define INTEGRAL(name, type)                                          \
static void aintegral_## name ##p(void **d, void **p, const void **s, \
                                  int nb_samples, int channels)       \
{                                                                     \
    int n, c;                                                         \
                                                                      \
    for (c = 0; c < channels; c++) {                                  \
        const type *src = s[c];                                       \
        type *dst = d[c];                                             \
        type *prv = p[c];                                             \
                                                                      \
        for (n = 0; n < nb_samples; n++) {                            \
            const type current = src[n];                              \
                                                                      \
            dst[n] = current + prv[0];                                \
            prv[0] = dst[n];                                          \
        }                                                             \
    }                                                                 \
}

INTEGRAL(flt, float)
INTEGRAL(dbl, double)

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ADerivativeContext *s = ctx->priv;

    switch (inlink->format) {
    case AV_SAMPLE_FMT_FLTP: s->filter = aderivative_fltp; break;
    case AV_SAMPLE_FMT_DBLP: s->filter = aderivative_dblp; break;
    case AV_SAMPLE_FMT_S32P: s->filter = aderivative_s32p; break;
    case AV_SAMPLE_FMT_S16P: s->filter = aderivative_s16p; break;
    }

    if (strcmp(ctx->filter->name, "aintegral"))
        return 0;

    switch (inlink->format) {
    case AV_SAMPLE_FMT_FLTP: s->filter = aintegral_fltp; break;
    case AV_SAMPLE_FMT_DBLP: s->filter = aintegral_dblp; break;
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    ADerivativeContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out = ff_get_audio_buffer(outlink, in->nb_samples);

    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    if (!s->prev) {
        s->prev = ff_get_audio_buffer(inlink, 1);
        if (!s->prev) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
    }

    s->filter((void **)out->extended_data, (void **)s->prev->extended_data, (const void **)in->extended_data,
              in->nb_samples, in->channels);

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ADerivativeContext *s = ctx->priv;

    av_frame_free(&s->prev);
}

static const AVFilterPad aderivative_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad aderivative_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_aderivative = {
    .name          = "aderivative",
    .description   = NULL_IF_CONFIG_SMALL("Compute derivative of input audio."),
    .query_formats = query_formats,
    .priv_size     = sizeof(ADerivativeContext),
    .uninit        = uninit,
    .inputs        = aderivative_inputs,
    .outputs       = aderivative_outputs,
};

AVFilter ff_af_aintegral = {
    .name          = "aintegral",
    .description   = NULL_IF_CONFIG_SMALL("Compute integral of input audio."),
    .query_formats = query_formats,
    .priv_size     = sizeof(ADerivativeContext),
    .uninit        = uninit,
    .inputs        = aderivative_inputs,
    .outputs       = aderivative_outputs,
};
