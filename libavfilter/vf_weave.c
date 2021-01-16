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

#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "internal.h"

typedef struct WeaveContext {
    const AVClass *class;
    int first_field;
    int double_weave;
    int nb_planes;
    int planeheight[4];
    int linesize[4];

    AVFrame *prev;
} WeaveContext;

#define OFFSET(x) offsetof(WeaveContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption weave_options[] = {
    { "first_field", "set first field", OFFSET(first_field), AV_OPT_TYPE_INT,   {.i64=0}, 0, 1, FLAGS, "field"},
        { "top",     "set top field first",               0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "field"},
        { "t",       "set top field first",               0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "field"},
        { "bottom",  "set bottom field first",            0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "field"},
        { "b",       "set bottom field first",            0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "field"},
    { NULL }
};

AVFILTER_DEFINE_CLASS(weave);

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    int ret;

    ret = ff_formats_pixdesc_filter(&formats, 0,
                                    AV_PIX_FMT_FLAG_PAL |
                                    AV_PIX_FMT_FLAG_HWACCEL);
    if (ret < 0)
        return ret;
    return ff_set_common_formats(ctx, formats);
}

static int config_props_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    WeaveContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int ret;

    if (!s->double_weave) {
        outlink->time_base.num = inlink->time_base.num * 2;
        outlink->time_base.den = inlink->time_base.den;
        outlink->frame_rate.num = inlink->frame_rate.num;
        outlink->frame_rate.den = inlink->frame_rate.den * 2;
    }
    outlink->w = inlink->w;
    outlink->h = inlink->h * 2;

    if ((ret = av_image_fill_linesizes(s->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    WeaveContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    int i;
    int weave;
    int field1, field2;

    if (!s->prev) {
        s->prev = in;
        return 0;
    }

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        av_frame_free(&s->prev);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    weave = (s->double_weave && !(inlink->frame_count_out & 1));
    field1 = weave ? s->first_field : (!s->first_field);
    field2 = weave ? (!s->first_field) : s->first_field;
    for (i = 0; i < s->nb_planes; i++) {
        av_image_copy_plane(out->data[i] + out->linesize[i] * field1,
                            out->linesize[i] * 2,
                            in->data[i], in->linesize[i],
                            s->linesize[i], s->planeheight[i]);
        av_image_copy_plane(out->data[i] + out->linesize[i] * field2,
                            out->linesize[i] * 2,
                            s->prev->data[i], s->prev->linesize[i],
                            s->linesize[i], s->planeheight[i]);
    }

    out->pts = s->double_weave ? s->prev->pts : in->pts / 2;
    out->interlaced_frame = 1;
    out->top_field_first = !s->first_field;

    if (!s->double_weave)
        av_frame_free(&in);
    av_frame_free(&s->prev);
    if (s->double_weave)
        s->prev = in;
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    WeaveContext *s = ctx->priv;

    av_frame_free(&s->prev);
}

static const AVFilterPad weave_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .filter_frame     = filter_frame,
    },
    { NULL }
};

static const AVFilterPad weave_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_props_output,
    },
    { NULL }
};

AVFilter ff_vf_weave = {
    .name          = "weave",
    .description   = NULL_IF_CONFIG_SMALL("Weave input video fields into frames."),
    .priv_size     = sizeof(WeaveContext),
    .priv_class    = &weave_class,
    .query_formats = query_formats,
    .uninit        = uninit,
    .inputs        = weave_inputs,
    .outputs       = weave_outputs,
};

static av_cold int init(AVFilterContext *ctx)
{
    WeaveContext *s = ctx->priv;

    if (!strcmp(ctx->filter->name, "doubleweave"))
        s->double_weave = 1;

    return 0;
}

#define doubleweave_options weave_options
AVFILTER_DEFINE_CLASS(doubleweave);

AVFilter ff_vf_doubleweave = {
    .name          = "doubleweave",
    .description   = NULL_IF_CONFIG_SMALL("Weave input video fields into double number of frames."),
    .priv_size     = sizeof(WeaveContext),
    .priv_class    = &doubleweave_class,
    .query_formats = query_formats,
    .init          = init,
    .uninit        = uninit,
    .inputs        = weave_inputs,
    .outputs       = weave_outputs,
};
