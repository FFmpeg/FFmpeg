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

typedef struct SeparateFieldsContext {
    int nb_planes;
    AVFrame *second;
} SeparateFieldsContext;

static int config_props_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    SeparateFieldsContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

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

    return 0;
}

static void extract_field(AVFrame *frame, int nb_planes, int type)
{
    int i;

    for (i = 0; i < nb_planes; i++) {
        if (type)
            frame->data[i] = frame->data[i] + frame->linesize[i];
        frame->linesize[i] *= 2;
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *inpicref)
{
    AVFilterContext *ctx = inlink->dst;
    SeparateFieldsContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int ret;

    inpicref->height = outlink->h;
    inpicref->interlaced_frame = 0;

    if (!s->second) {
        goto clone;
    } else {
        AVFrame *second = s->second;

        extract_field(second, s->nb_planes, second->top_field_first);

        if (second->pts != AV_NOPTS_VALUE &&
            inpicref->pts != AV_NOPTS_VALUE)
            second->pts += inpicref->pts;
        else
            second->pts = AV_NOPTS_VALUE;

        ret = ff_filter_frame(outlink, second);
        if (ret < 0)
            return ret;
clone:
        s->second = av_frame_clone(inpicref);
        if (!s->second)
            return AVERROR(ENOMEM);
    }

    extract_field(inpicref, s->nb_planes, !inpicref->top_field_first);

    if (inpicref->pts != AV_NOPTS_VALUE)
        inpicref->pts *= 2;

    return ff_filter_frame(outlink, inpicref);
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    SeparateFieldsContext *s = ctx->priv;
    int ret;

    ret = ff_request_frame(ctx->inputs[0]);
    if (ret == AVERROR_EOF && s->second) {
        s->second->pts *= 2;
        extract_field(s->second, s->nb_planes, s->second->top_field_first);
        ret = ff_filter_frame(outlink, s->second);
        s->second = 0;
    }

    return ret;
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
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_props_output,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_vf_separatefields = {
    .name        = "separatefields",
    .description = NULL_IF_CONFIG_SMALL("Split input video frames into fields."),
    .priv_size   = sizeof(SeparateFieldsContext),
    .inputs      = separatefields_inputs,
    .outputs     = separatefields_outputs,
};
