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
#include "filters.h"
#include "video.h"

typedef struct SeparateFieldsContext {
    int nb_planes;
    AVFrame *second;
} SeparateFieldsContext;

static int config_props_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    SeparateFieldsContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    FilterLink       *il = ff_filter_link(inlink);
    FilterLink       *ol = ff_filter_link(outlink);

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    if (inlink->h & 1) {
        av_log(ctx, AV_LOG_ERROR, "height must be even\n");
        return AVERROR_INVALIDDATA;
    }

    outlink->time_base.num = inlink->time_base.num;
    outlink->time_base.den = inlink->time_base.den * 2;
    ol->frame_rate.num = il->frame_rate.num * 2;
    ol->frame_rate.den = il->frame_rate.den;
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
#if FF_API_INTERLACED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    inpicref->interlaced_frame = 0;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    inpicref->flags &= ~AV_FRAME_FLAG_INTERLACED;

    if (!s->second) {
        goto clone;
    } else {
        AVFrame *second = s->second;

        extract_field(second, s->nb_planes, !!(second->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST));

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

    extract_field(inpicref, s->nb_planes, !(inpicref->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST));

    if (inpicref->pts != AV_NOPTS_VALUE)
        inpicref->pts *= 2;

    return ff_filter_frame(outlink, inpicref);
}

static int flush_frame(AVFilterLink *outlink, int64_t pts, int64_t *out_pts)
{
    AVFilterContext *ctx = outlink->src;
    SeparateFieldsContext *s = ctx->priv;
    int ret = 0;

    if (s->second) {
        *out_pts = s->second->pts += pts;
        extract_field(s->second, s->nb_planes, !!(s->second->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST));
        ret = ff_filter_frame(outlink, s->second);
        s->second = NULL;
    }

    return ret;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *in;
    int64_t pts;
    int ret, status;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    ret = ff_inlink_consume_frame(inlink, &in);
    if (ret < 0)
        return ret;
    if (ret > 0)
        return filter_frame(inlink, in);

    if (ff_inlink_acknowledge_status(inlink, &status, &pts)) {
        if (status == AVERROR_EOF) {
            int64_t out_pts = pts;

            ret = flush_frame(outlink, pts, &out_pts);
            ff_outlink_set_status(outlink, status, out_pts);
            return ret;
        }
    }

    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SeparateFieldsContext *s = ctx->priv;

    av_frame_free(&s->second);
}

static const AVFilterPad separatefields_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_props_output,
    },
};

const AVFilter ff_vf_separatefields = {
    .name        = "separatefields",
    .description = NULL_IF_CONFIG_SMALL("Split input video frames into fields."),
    .priv_size   = sizeof(SeparateFieldsContext),
    .activate    = activate,
    .uninit      = uninit,
    FILTER_INPUTS(ff_video_default_filterpad),
    FILTER_OUTPUTS(separatefields_outputs),
};
