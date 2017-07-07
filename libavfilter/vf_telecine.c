/*
 * Copyright (c) 2012 Rudolf Polzer
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

/**
 * @file telecine filter, heavily based from mpv-player:TOOLS/vf_dlopen/telecine.c by
 * Rudolf Polzer.
 */

#include "libavutil/avstring.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct TelecineContext {
    const AVClass *class;
    int first_field;
    char *pattern;
    unsigned int pattern_pos;
    int64_t start_time;

    AVRational pts;
    AVRational ts_unit;
    int out_cnt;
    int occupied;

    int nb_planes;
    int planeheight[4];
    int stride[4];

    AVFrame *frame[5];
    AVFrame *temp;
} TelecineContext;

#define OFFSET(x) offsetof(TelecineContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption telecine_options[] = {
    {"first_field", "select first field", OFFSET(first_field), AV_OPT_TYPE_INT,   {.i64=0}, 0, 1, FLAGS, "field"},
        {"top",    "select top field first",                0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "field"},
        {"t",      "select top field first",                0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "field"},
        {"bottom", "select bottom field first",             0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "field"},
        {"b",      "select bottom field first",             0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "field"},
    {"pattern", "pattern that describe for how many fields a frame is to be displayed", OFFSET(pattern), AV_OPT_TYPE_STRING, {.str="23"}, 0, 0, FLAGS},
    {NULL}
};

AVFILTER_DEFINE_CLASS(telecine);

static av_cold int init(AVFilterContext *ctx)
{
    TelecineContext *s = ctx->priv;
    const char *p;
    int max = 0;

    if (!strlen(s->pattern)) {
        av_log(ctx, AV_LOG_ERROR, "No pattern provided.\n");
        return AVERROR_INVALIDDATA;
    }

    for (p = s->pattern; *p; p++) {
        if (!av_isdigit(*p)) {
            av_log(ctx, AV_LOG_ERROR, "Provided pattern includes non-numeric characters.\n");
            return AVERROR_INVALIDDATA;
        }

        max = FFMAX(*p - '0', max);
        s->pts.num += 2;
        s->pts.den += *p - '0';
    }

    s->start_time = AV_NOPTS_VALUE;

    s->out_cnt = (max + 1) / 2;
    av_log(ctx, AV_LOG_INFO, "Telecine pattern %s yields up to %d frames per frame, pts advance factor: %d/%d\n",
           s->pattern, s->out_cnt, s->pts.num, s->pts.den);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *pix_fmts = NULL;
    int fmt, ret;

    for (fmt = 0; av_pix_fmt_desc_get(fmt); fmt++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
        if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL ||
              desc->flags & AV_PIX_FMT_FLAG_PAL     ||
              desc->flags & AV_PIX_FMT_FLAG_BITSTREAM) &&
            (ret = ff_add_format(&pix_fmts, fmt)) < 0)
            return ret;
    }

    return ff_set_common_formats(ctx, pix_fmts);
}

static int config_input(AVFilterLink *inlink)
{
    TelecineContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int i, ret;

    s->temp = ff_get_video_buffer(inlink, inlink->w, inlink->h);
    if (!s->temp)
        return AVERROR(ENOMEM);
    for (i = 0; i < s->out_cnt; i++) {
        s->frame[i] = ff_get_video_buffer(inlink, inlink->w, inlink->h);
        if (!s->frame[i])
            return AVERROR(ENOMEM);
    }

    if ((ret = av_image_fill_linesizes(s->stride, inlink->format, inlink->w)) < 0)
        return ret;

    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    TelecineContext *s = ctx->priv;
    const AVFilterLink *inlink = ctx->inputs[0];
    AVRational fps = inlink->frame_rate;

    if (!fps.num || !fps.den) {
        av_log(ctx, AV_LOG_ERROR, "The input needs a constant frame rate; "
               "current rate of %d/%d is invalid\n", fps.num, fps.den);
        return AVERROR(EINVAL);
    }
    fps = av_mul_q(fps, av_inv_q(s->pts));
    av_log(ctx, AV_LOG_VERBOSE, "FPS: %d/%d -> %d/%d\n",
           inlink->frame_rate.num, inlink->frame_rate.den, fps.num, fps.den);

    outlink->frame_rate = fps;
    outlink->time_base = av_mul_q(inlink->time_base, s->pts);
    av_log(ctx, AV_LOG_VERBOSE, "TB: %d/%d -> %d/%d\n",
           inlink->time_base.num, inlink->time_base.den, outlink->time_base.num, outlink->time_base.den);

    s->ts_unit = av_inv_q(av_mul_q(fps, outlink->time_base));

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *inpicref)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    TelecineContext *s = ctx->priv;
    int i, len, ret = 0, nout = 0;

    if (s->start_time == AV_NOPTS_VALUE)
        s->start_time = inpicref->pts;

    len = s->pattern[s->pattern_pos] - '0';

    s->pattern_pos++;
    if (!s->pattern[s->pattern_pos])
        s->pattern_pos = 0;

    if (!len) { // do not output any field from this frame
        av_frame_free(&inpicref);
        return 0;
    }

    if (s->occupied) {
        av_frame_make_writable(s->frame[nout]);
        for (i = 0; i < s->nb_planes; i++) {
            // fill in the EARLIER field from the buffered pic
            av_image_copy_plane(s->frame[nout]->data[i] + s->frame[nout]->linesize[i] * s->first_field,
                                s->frame[nout]->linesize[i] * 2,
                                s->temp->data[i] + s->temp->linesize[i] * s->first_field,
                                s->temp->linesize[i] * 2,
                                s->stride[i],
                                (s->planeheight[i] - s->first_field + 1) / 2);
            // fill in the LATER field from the new pic
            av_image_copy_plane(s->frame[nout]->data[i] + s->frame[nout]->linesize[i] * !s->first_field,
                                s->frame[nout]->linesize[i] * 2,
                                inpicref->data[i] + inpicref->linesize[i] * !s->first_field,
                                inpicref->linesize[i] * 2,
                                s->stride[i],
                                (s->planeheight[i] - !s->first_field + 1) / 2);
        }
        nout++;
        len--;
        s->occupied = 0;
    }

    while (len >= 2) {
        // output THIS image as-is
        av_frame_make_writable(s->frame[nout]);
        for (i = 0; i < s->nb_planes; i++)
            av_image_copy_plane(s->frame[nout]->data[i], s->frame[nout]->linesize[i],
                                inpicref->data[i], inpicref->linesize[i],
                                s->stride[i],
                                s->planeheight[i]);
        nout++;
        len -= 2;
    }

    if (len >= 1) {
        // copy THIS image to the buffer, we need it later
        for (i = 0; i < s->nb_planes; i++)
            av_image_copy_plane(s->temp->data[i], s->temp->linesize[i],
                                inpicref->data[i], inpicref->linesize[i],
                                s->stride[i],
                                s->planeheight[i]);
        s->occupied = 1;
    }

    for (i = 0; i < nout; i++) {
        AVFrame *frame = av_frame_clone(s->frame[i]);

        if (!frame) {
            av_frame_free(&inpicref);
            return AVERROR(ENOMEM);
        }

        av_frame_copy_props(frame, inpicref);
        frame->pts = ((s->start_time == AV_NOPTS_VALUE) ? 0 : s->start_time) +
                     av_rescale(outlink->frame_count_in, s->ts_unit.num,
                                s->ts_unit.den);
        ret = ff_filter_frame(outlink, frame);
    }
    av_frame_free(&inpicref);

    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    TelecineContext *s = ctx->priv;
    int i;

    av_frame_free(&s->temp);
    for (i = 0; i < s->out_cnt; i++)
        av_frame_free(&s->frame[i]);
}

static const AVFilterPad telecine_inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .filter_frame  = filter_frame,
        .config_props  = config_input,
    },
    { NULL }
};

static const AVFilterPad telecine_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
    { NULL }
};

AVFilter ff_vf_telecine = {
    .name          = "telecine",
    .description   = NULL_IF_CONFIG_SMALL("Apply a telecine pattern."),
    .priv_size     = sizeof(TelecineContext),
    .priv_class    = &telecine_class,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = telecine_inputs,
    .outputs       = telecine_outputs,
};
