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

#include "libavutil/channel_layout.h"
#include "libavutil/ffmath.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "audio.h"
#include "formats.h"

typedef struct CrossfeedContext {
    const AVClass *class;

    double range;
    double strength;
    double level_in;
    double level_out;

    double a0, a1, a2;
    double b0, b1, b2;

    double i1, i2;
    double o1, o2;
} CrossfeedContext;

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layout = NULL;
    int ret;

    if ((ret = ff_add_format                 (&formats, AV_SAMPLE_FMT_DBL  )) < 0 ||
        (ret = ff_set_common_formats         (ctx     , formats            )) < 0 ||
        (ret = ff_add_channel_layout         (&layout , AV_CH_LAYOUT_STEREO)) < 0 ||
        (ret = ff_set_common_channel_layouts (ctx     , layout             )) < 0 ||
        (ret = ff_set_common_samplerates     (ctx     , ff_all_samplerates())) < 0)
        return ret;

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    CrossfeedContext *s = ctx->priv;
    double A = ff_exp10(s->strength * -30 / 40);
    double w0 = 2 * M_PI * (1. - s->range) * 2100 / inlink->sample_rate;
    double alpha;

    alpha = sin(w0) / 2 * sqrt(2 * (1 / 0.5 - 1) + 2);

    s->a0 =          (A + 1) + (A - 1) * cos(w0) + 2 * sqrt(A) * alpha;
    s->a1 =    -2 * ((A - 1) + (A + 1) * cos(w0));
    s->a2 =          (A + 1) + (A - 1) * cos(w0) - 2 * sqrt(A) * alpha;
    s->b0 =     A * ((A + 1) - (A - 1) * cos(w0) + 2 * sqrt(A) * alpha);
    s->b1 = 2 * A * ((A - 1) - (A + 1) * cos(w0));
    s->b2 =     A * ((A + 1) - (A - 1) * cos(w0) - 2 * sqrt(A) * alpha);

    s->a1 /= s->a0;
    s->a2 /= s->a0;
    s->b0 /= s->a0;
    s->b1 /= s->a0;
    s->b2 /= s->a0;

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    CrossfeedContext *s = ctx->priv;
    const double *src = (const double *)in->data[0];
    const double level_in = s->level_in;
    const double level_out = s->level_out;
    const double b0 = s->b0;
    const double b1 = s->b1;
    const double b2 = s->b2;
    const double a1 = s->a1;
    const double a2 = s->a2;
    AVFrame *out;
    double *dst;
    int n;

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
    dst = (double *)out->data[0];

    for (n = 0; n < out->nb_samples; n++, src += 2, dst += 2) {
        double mid = (src[0] + src[1]) * level_in * .5;
        double side = (src[0] - src[1]) * level_in * .5;
        double oside = side * b0 + s->i1 * b1 + s->i2 * b2 - s->o1 * a1 - s->o2 * a2;

        s->i2 = s->i1;
        s->i1 = side;
        s->o2 = s->o1;
        s->o1 = oside;

        if (ctx->is_disabled) {
            dst[0] = src[0];
            dst[1] = src[1];
        } else {
            dst[0] = (mid + oside) * level_out;
            dst[1] = (mid - oside) * level_out;
        }
    }

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

#define OFFSET(x) offsetof(CrossfeedContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption crossfeed_options[] = {
    { "strength",  "set crossfeed strength",  OFFSET(strength),  AV_OPT_TYPE_DOUBLE, {.dbl=.2}, 0, 1, FLAGS },
    { "range",     "set soundstage wideness", OFFSET(range),     AV_OPT_TYPE_DOUBLE, {.dbl=.5}, 0, 1, FLAGS },
    { "level_in",  "set level in",            OFFSET(level_in),  AV_OPT_TYPE_DOUBLE, {.dbl=.9}, 0, 1, FLAGS },
    { "level_out", "set level out",           OFFSET(level_out), AV_OPT_TYPE_DOUBLE, {.dbl=1.}, 0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(crossfeed);

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

AVFilter ff_af_crossfeed = {
    .name           = "crossfeed",
    .description    = NULL_IF_CONFIG_SMALL("Apply headphone crossfeed filter."),
    .query_formats  = query_formats,
    .priv_size      = sizeof(CrossfeedContext),
    .priv_class     = &crossfeed_class,
    .inputs         = inputs,
    .outputs        = outputs,
    .flags          = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
};
