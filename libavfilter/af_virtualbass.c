/*
 * Copyright (c) 2022 Paul B Mahol
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"

#include <float.h>

typedef struct AudioVirtualBassContext {
    const AVClass *class;

    double cutoff;
    double strength;

    double a[3], m[3], cf[2];
} AudioVirtualBassContext;

#define OFFSET(x) offsetof(AudioVirtualBassContext, x)
#define TFLAGS AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_RUNTIME_PARAM
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM

static const AVOption virtualbass_options[] = {
    { "cutoff",   "set virtual bass cutoff",   OFFSET(cutoff),   AV_OPT_TYPE_DOUBLE, {.dbl=250},100,500, FLAGS },
    { "strength", "set virtual bass strength", OFFSET(strength), AV_OPT_TYPE_DOUBLE, {.dbl=3},  0.5,  3, TFLAGS },
    {NULL}
};

AVFILTER_DEFINE_CLASS(virtualbass);

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    static const enum AVSampleFormat formats[] = {
        AV_SAMPLE_FMT_DBLP,
        AV_SAMPLE_FMT_NONE,
    };

    AVFilterChannelLayouts *in_layout = NULL, *out_layout = NULL;
    int ret;

    ret = ff_set_common_formats_from_list2(ctx, cfg_in, cfg_out, formats);
    if (ret < 0)
        return ret;

    if ((ret = ff_add_channel_layout         (&in_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO)) < 0 ||
        (ret = ff_channel_layouts_ref(in_layout, &cfg_in[0]->channel_layouts)) < 0 ||
        (ret = ff_add_channel_layout         (&out_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_2POINT1)) < 0 ||
        (ret = ff_channel_layouts_ref(out_layout, &cfg_out[0]->channel_layouts)) < 0)
        return ret;

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioVirtualBassContext *s = ctx->priv;
    const double Q = 0.707;
    double g, k;

    g = tan(M_PI * s->cutoff / inlink->sample_rate);
    k = 1. / Q;
    s->a[0] = 1. / (1. + g * (g + k));
    s->a[1] = g * s->a[0];
    s->a[2] = g * s->a[1];
    s->m[0] = 0.;
    s->m[1] = 0.;
    s->m[2] = 1.;

    return 0;
}

#define SQR(x) ((x) * (x))

static double vb_fun(double x)
{
    double y = 2.5 * atan(0.9 * x) + 2.5 * sqrt(1. - SQR(0.9 * x)) - 2.5;

    return y < 0. ? sin(y) : y;
}

static void vb_stereo(AVFilterContext *ctx, AVFrame *out, AVFrame *in)
{
    AudioVirtualBassContext *s = ctx->priv;
    const double *lsrc = (const double *)in->extended_data[0];
    const double *rsrc = (const double *)in->extended_data[1];
    double *ldst = (double *)out->extended_data[0];
    double *rdst = (double *)out->extended_data[1];
    double *lfe = (double *)out->extended_data[2];
    const double st = M_PI / s->strength;
    const double a0 = s->a[0];
    const double a1 = s->a[1];
    const double a2 = s->a[2];
    const double m0 = s->m[0];
    const double m1 = s->m[1];
    const double m2 = s->m[2];
    double b0 = s->cf[0];
    double b1 = s->cf[1];

    memcpy(ldst, lsrc, in->nb_samples * sizeof(double));
    memcpy(rdst, rsrc, in->nb_samples * sizeof(double));

    for (int n = 0; n < in->nb_samples; n++) {
        const double center = (lsrc[n] + rsrc[n]) * 0.5;
        const double v0 = center;
        const double v3 = v0 - b1;
        const double v1 = a0 * b0 + a1 * v3;
        const double v2 = b1 + a1 * b0 + a2 * v3;
        double b, vb;

        b0 = 2. * v1 - b0;
        b1 = 2. * v2 - b1;

        b = m0 * v0 + m1 * v1 + m2 * v2;
        vb = sin(vb_fun(b) * st);

        lfe[n] = vb;
    }

    s->cf[0] = b0;
    s->cf[1] = b1;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;

    out = ff_get_audio_buffer(outlink, in->nb_samples);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    vb_stereo(ctx, out, in);

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

const AVFilter ff_af_virtualbass = {
    .name            = "virtualbass",
    .description     = NULL_IF_CONFIG_SMALL("Audio Virtual Bass."),
    .priv_size       = sizeof(AudioVirtualBassContext),
    .priv_class      = &virtualbass_class,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    FILTER_QUERY_FUNC2(query_formats),
    .flags           = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
    .process_command = ff_filter_process_command,
};
