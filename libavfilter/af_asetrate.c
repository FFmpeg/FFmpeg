/*
 * Copyright (c) 2013 Nicolas George
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

#include "libavutil/opt.h"
#include "avfilter.h"
#include "internal.h"

typedef struct {
    const AVClass *class;
    int sample_rate;
    int rescale_pts;
} ASetRateContext;

#define CONTEXT ASetRateContext
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

#define OPT_GENERIC(name, field, def, min, max, descr, type, deffield, ...) \
    { name, descr, offsetof(CONTEXT, field), AV_OPT_TYPE_ ## type,          \
      { .deffield = def }, min, max, FLAGS, __VA_ARGS__ }

#define OPT_INT(name, field, def, min, max, descr, ...) \
    OPT_GENERIC(name, field, def, min, max, descr, INT, i64, __VA_ARGS__)

static const AVOption asetrate_options[] = {
    OPT_INT("sample_rate", sample_rate, 44100, 1, INT_MAX, "set the sample rate"),
    OPT_INT("r",           sample_rate, 44100, 1, INT_MAX, "set the sample rate"),
    {NULL},
};

AVFILTER_DEFINE_CLASS(asetrate);

static av_cold int query_formats(AVFilterContext *ctx)
{
    ASetRateContext *sr = ctx->priv;
    int sample_rates[] = { sr->sample_rate, -1 };

    ff_formats_ref(ff_make_format_list(sample_rates),
                   &ctx->outputs[0]->in_samplerates);
    return 0;
}

static av_cold int config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    ASetRateContext *sr = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    AVRational intb = ctx->inputs[0]->time_base;
    int inrate = inlink->sample_rate;

    if (intb.num == 1 && intb.den == inrate) {
        outlink->time_base.num = 1;
        outlink->time_base.den = outlink->sample_rate;
    } else {
        outlink->time_base = intb;
        sr->rescale_pts = 1;
        if (av_q2d(intb) > 1.0 / FFMAX(inrate, outlink->sample_rate))
            av_log(ctx, AV_LOG_WARNING, "Time base is inaccurate\n");
    }
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    ASetRateContext *sr = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    frame->sample_rate = outlink->sample_rate;
    if (sr->rescale_pts)
        frame->pts = av_rescale(frame->pts, inlink->sample_rate,
                                           outlink->sample_rate);
    return ff_filter_frame(outlink, frame);
}

static const AVFilterPad asetrate_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad asetrate_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_props,
    },
    { NULL }
};

AVFilter avfilter_af_asetrate = {
    .name          = "asetrate",
    .description   = NULL_IF_CONFIG_SMALL("Change the sample rate without "
                                          "altering the data."),
    .query_formats = query_formats,
    .priv_size     = sizeof(ASetRateContext),
    .inputs        = asetrate_inputs,
    .outputs       = asetrate_outputs,
    .priv_class    = &asetrate_class,
};
