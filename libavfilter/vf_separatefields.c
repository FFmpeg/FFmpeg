/*
 * Copyright (c) 2013 Paul B Mahol
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

#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "internal.h"

typedef struct {
    int nb_planes;
    double ts_unit;
} SeparateFieldsContext;

static int config_props_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    SeparateFieldsContext *sf = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];

    sf->nb_planes = av_pix_fmt_count_planes(inlink->format);

    if (inlink->h & 1) {
        av_log(ctx, AV_LOG_ERROR, "height must be even\n");
        return AVERROR_INVALIDDATA;
    }

    outlink->time_base.num = inlink->time_base.num;
    outlink->time_base.den = inlink->time_base.den * 2;
    outlink->frame_rate.num = inlink->frame_rate.num * 2;
    outlink->frame_rate.den = inlink->frame_rate.den;
    outlink->w = inlink->w;
    outlink->h = inlink->h / 2;
    sf->ts_unit = av_q2d(av_inv_q(av_mul_q(outlink->frame_rate, outlink->time_base)));

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *inpicref)
{
    AVFilterContext *ctx = inlink->dst;
    SeparateFieldsContext *sf = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *second;
    int i, ret;

    inpicref->height = outlink->h;
    inpicref->interlaced_frame = 0;

    second = av_frame_clone(inpicref);
    if (!second)
        return AVERROR(ENOMEM);

    for (i = 0; i < sf->nb_planes; i++) {
        if (!inpicref->top_field_first)
            inpicref->data[i] = inpicref->data[i] + inpicref->linesize[i];
        else
            second->data[i] = second->data[i] + second->linesize[i];
        inpicref->linesize[i] *= 2;
        second->linesize[i]   *= 2;
    }

    inpicref->pts = outlink->frame_count * sf->ts_unit;
    ret = ff_filter_frame(outlink, inpicref);
    if (ret < 0) {
        av_frame_free(&second);
        return ret;
    }

    second->pts = outlink->frame_count * sf->ts_unit;
    return ff_filter_frame(outlink, second);
}

static const AVFilterPad separatefields_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad separatefields_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props_output,
    },
    { NULL }
};

AVFilter avfilter_vf_separatefields = {
    .name        = "separatefields",
    .description = NULL_IF_CONFIG_SMALL("Split input video frames into fields."),
    .priv_size   = sizeof(SeparateFieldsContext),
    .inputs      = separatefields_inputs,
    .outputs     = separatefields_outputs,
};
