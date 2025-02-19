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
#include "filters.h"
#include "formats.h"
#include "video.h"

typedef struct WeaveContext {
    const AVClass *class;
    int first_field;
    int double_weave;
    int nb_planes;
    int planeheight[4];
    int outheight[4];
    int linesize[4];

    AVFrame *prev;
} WeaveContext;

#define OFFSET(x) offsetof(WeaveContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption weave_options[] = {
    { "first_field", "set first field", OFFSET(first_field), AV_OPT_TYPE_INT,   {.i64=0}, 0, 1, FLAGS, .unit = "field"},
        { "top",     "set top field first",               0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, .unit = "field"},
        { "t",       "set top field first",               0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, .unit = "field"},
        { "bottom",  "set bottom field first",            0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, .unit = "field"},
        { "b",       "set bottom field first",            0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, .unit = "field"},
    { NULL }
};

AVFILTER_DEFINE_CLASS_EXT(weave, "(double)weave", weave_options);

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    int reject_flags = AV_PIX_FMT_FLAG_PAL | AV_PIX_FMT_FLAG_HWACCEL;

    return ff_set_common_formats2(ctx, cfg_in, cfg_out,
                                  ff_formats_pixdesc_filter(0, reject_flags));
}

static int config_props_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    WeaveContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int ret;

    if (!s->double_weave) {
        FilterLink *il = ff_filter_link(inlink);
        FilterLink *ol = ff_filter_link(outlink);
        outlink->time_base.num = inlink->time_base.num * 2;
        outlink->time_base.den = inlink->time_base.den;
        ol->frame_rate.num = il->frame_rate.num;
        ol->frame_rate.den = il->frame_rate.den * 2;
    }
    outlink->w = inlink->w;
    outlink->h = inlink->h * 2;

    if ((ret = av_image_fill_linesizes(s->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->outheight[1] = s->outheight[2] = AV_CEIL_RSHIFT(2*inlink->h, desc->log2_chroma_h);
    s->outheight[0] = s->outheight[3] = 2*inlink->h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    return 0;
}

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

static int weave_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    AVFilterLink *inlink = ctx->inputs[0];
    FilterLink *inl = ff_filter_link(inlink);
    WeaveContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;

    const int weave = (s->double_weave && !(inl->frame_count_out & 1));
    const int field1 = weave ? s->first_field : (!s->first_field);
    const int field2 = weave ? (!s->first_field) : s->first_field;

    for (int i = 0; i < s->nb_planes; i++) {
        const int height = s->planeheight[i];
        const int start = (height * jobnr) / nb_jobs;
        const int end = (height * (jobnr+1)) / nb_jobs;
        const int compensation = 2*end > s->outheight[i];

        av_image_copy_plane(out->data[i] + out->linesize[i] * field1 +
                            out->linesize[i] * start * 2,
                            out->linesize[i] * 2,
                            in->data[i] + start * in->linesize[i],
                            in->linesize[i],
                            s->linesize[i], end - start - compensation * field1);
        av_image_copy_plane(out->data[i] + out->linesize[i] * field2 +
                            out->linesize[i] * start * 2,
                            out->linesize[i] * 2,
                            s->prev->data[i] + start * s->prev->linesize[i],
                            s->prev->linesize[i],
                            s->linesize[i], end - start - compensation * field2);
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    WeaveContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    ThreadData td;
    AVFrame *out;

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

    td.out = out, td.in = in;
    ff_filter_execute(ctx, weave_slice, &td, NULL,
                      FFMIN(s->planeheight[1], ff_filter_get_nb_threads(ctx)));

    out->pts = s->double_weave ? s->prev->pts : in->pts / 2;
    out->flags |= AV_FRAME_FLAG_INTERLACED;
    if (s->first_field)
        out->flags &= ~AV_FRAME_FLAG_TOP_FIELD_FIRST;
    else
        out->flags |= AV_FRAME_FLAG_TOP_FIELD_FIRST;

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
};

static const AVFilterPad weave_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_props_output,
    },
};

const FFFilter ff_vf_weave = {
    .p.name        = "weave",
    .p.description = NULL_IF_CONFIG_SMALL("Weave input video fields into frames."),
    .p.priv_class  = &weave_class,
    .p.flags       = AVFILTER_FLAG_SLICE_THREADS,
    .priv_size     = sizeof(WeaveContext),
    .uninit        = uninit,
    FILTER_INPUTS(weave_inputs),
    FILTER_OUTPUTS(weave_outputs),
    FILTER_QUERY_FUNC2(query_formats),
};

static av_cold int init(AVFilterContext *ctx)
{
    WeaveContext *s = ctx->priv;

    if (!strcmp(ctx->filter->name, "doubleweave"))
        s->double_weave = 1;

    return 0;
}

const FFFilter ff_vf_doubleweave = {
    .p.name        = "doubleweave",
    .p.description = NULL_IF_CONFIG_SMALL("Weave input video fields into double number of frames."),
    .p.priv_class  = &weave_class,
    .p.flags       = AVFILTER_FLAG_SLICE_THREADS,
    .priv_size     = sizeof(WeaveContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(weave_inputs),
    FILTER_OUTPUTS(weave_outputs),
    FILTER_QUERY_FUNC2(query_formats),
};
