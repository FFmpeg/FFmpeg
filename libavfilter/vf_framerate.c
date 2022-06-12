/*
 * Copyright (C) 2012 Mark Himsley
 *
 * get_scene_score() Copyright (c) 2011 Stefano Sabatini
 * taken from libavfilter/vf_select.c
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
 * @file
 * filter for upsampling or downsampling a progressive source
 */

#define DEBUG

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "internal.h"
#include "video.h"
#include "filters.h"
#include "framerate.h"
#include "scene_sad.h"

#define OFFSET(x) offsetof(FrameRateContext, x)
#define V AV_OPT_FLAG_VIDEO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM
#define FRAMERATE_FLAG_SCD 01

static const AVOption framerate_options[] = {
    {"fps",                 "required output frames per second rate", OFFSET(dest_frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str="50"},             0,       INT_MAX, V|F },

    {"interp_start",        "point to start linear interpolation",    OFFSET(interp_start),    AV_OPT_TYPE_INT,      {.i64=15},                 0,       255,     V|F },
    {"interp_end",          "point to end linear interpolation",      OFFSET(interp_end),      AV_OPT_TYPE_INT,      {.i64=240},                0,       255,     V|F },
    {"scene",               "scene change level",                     OFFSET(scene_score),     AV_OPT_TYPE_DOUBLE,   {.dbl=8.2},                0,       100., V|F },

    {"flags",               "set flags",                              OFFSET(flags),           AV_OPT_TYPE_FLAGS,    {.i64=1},                  0,       INT_MAX, V|F, "flags" },
    {"scene_change_detect", "enable scene change detection",          0,                       AV_OPT_TYPE_CONST,    {.i64=FRAMERATE_FLAG_SCD}, INT_MIN, INT_MAX, V|F, "flags" },
    {"scd",                 "enable scene change detection",          0,                       AV_OPT_TYPE_CONST,    {.i64=FRAMERATE_FLAG_SCD}, INT_MIN, INT_MAX, V|F, "flags" },

    {NULL}
};

AVFILTER_DEFINE_CLASS(framerate);

static double get_scene_score(AVFilterContext *ctx, AVFrame *crnt, AVFrame *next)
{
    FrameRateContext *s = ctx->priv;
    double ret = 0;

    ff_dlog(ctx, "get_scene_score()\n");

    if (crnt->height == next->height &&
        crnt->width  == next->width) {
        uint64_t sad;
        double mafd, diff;

        ff_dlog(ctx, "get_scene_score() process\n");
        s->sad(crnt->data[0], crnt->linesize[0], next->data[0], next->linesize[0], crnt->width, crnt->height, &sad);
        emms_c();
        mafd = (double)sad * 100.0 / (crnt->width * crnt->height) / (1 << s->bitdepth);
        diff = fabs(mafd - s->prev_mafd);
        ret  = av_clipf(FFMIN(mafd, diff), 0, 100.0);
        s->prev_mafd = mafd;
    }
    ff_dlog(ctx, "get_scene_score() result is:%f\n", ret);
    return ret;
}

typedef struct ThreadData {
    AVFrame *copy_src1, *copy_src2;
    uint16_t src1_factor, src2_factor;
} ThreadData;

static int filter_slice(AVFilterContext *ctx, void *arg, int job, int nb_jobs)
{
    FrameRateContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *work = s->work;
    AVFrame *src1 = td->copy_src1;
    AVFrame *src2 = td->copy_src2;
    uint16_t src1_factor = td->src1_factor;
    uint16_t src2_factor = td->src2_factor;
    int plane;

    for (plane = 0; plane < 4 && src1->data[plane] && src2->data[plane]; plane++) {
        const int start = (s->height[plane] *  job   ) / nb_jobs;
        const int end   = (s->height[plane] * (job+1)) / nb_jobs;
        uint8_t *src1_data = src1->data[plane] + start * src1->linesize[plane];
        uint8_t *src2_data = src2->data[plane] + start * src2->linesize[plane];
        uint8_t *dst_data  = work->data[plane] + start * work->linesize[plane];

        s->blend(src1_data, src1->linesize[plane], src2_data, src2->linesize[plane],
                 dst_data,  work->linesize[plane], s->line_size[plane], end - start,
                 src1_factor, src2_factor, s->blend_factor_max >> 1);
    }

    return 0;
}

static int blend_frames(AVFilterContext *ctx, int interpolate)
{
    FrameRateContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    double interpolate_scene_score = 0;

    if ((s->flags & FRAMERATE_FLAG_SCD)) {
        if (s->score >= 0.0)
            interpolate_scene_score = s->score;
        else
            interpolate_scene_score = s->score = get_scene_score(ctx, s->f0, s->f1);
        ff_dlog(ctx, "blend_frames() interpolate scene score:%f\n", interpolate_scene_score);
    }
    // decide if the shot-change detection allows us to blend two frames
    if (interpolate_scene_score < s->scene_score) {
        ThreadData td;
        td.copy_src1 = s->f0;
        td.copy_src2 = s->f1;
        td.src2_factor = interpolate;
        td.src1_factor = s->blend_factor_max - td.src2_factor;

        // get work-space for output frame
        s->work = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!s->work)
            return AVERROR(ENOMEM);

        av_frame_copy_props(s->work, s->f0);

        ff_dlog(ctx, "blend_frames() INTERPOLATE to create work frame\n");
        ff_filter_execute(ctx, filter_slice, &td, NULL,
                          FFMIN(FFMAX(1, outlink->h >> 2), ff_filter_get_nb_threads(ctx)));
        return 1;
    }
    return 0;
}

static int process_work_frame(AVFilterContext *ctx)
{
    FrameRateContext *s = ctx->priv;
    int64_t work_pts;
    int64_t interpolate, interpolate8;
    int ret;

    if (!s->f1)
        return 0;
    if (!s->f0 && !s->flush)
        return 0;

    work_pts = s->start_pts + av_rescale_q(s->n, av_inv_q(s->dest_frame_rate), s->dest_time_base);

    if (work_pts >= s->pts1 && !s->flush)
        return 0;

    if (!s->f0) {
        av_assert1(s->flush);
        s->work = s->f1;
        s->f1 = NULL;
    } else {
        if (work_pts >= s->pts1 + s->delta && s->flush)
            return 0;

        interpolate = av_rescale(work_pts - s->pts0, s->blend_factor_max, s->delta);
        interpolate8 = av_rescale(work_pts - s->pts0, 256, s->delta);
        ff_dlog(ctx, "process_work_frame() interpolate: %"PRId64"/256\n", interpolate8);
        if (interpolate >= s->blend_factor_max || interpolate8 > s->interp_end) {
            s->work = av_frame_clone(s->f1);
        } else if (interpolate <= 0 || interpolate8 < s->interp_start) {
            s->work = av_frame_clone(s->f0);
        } else {
            ret = blend_frames(ctx, interpolate);
            if (ret < 0)
                return ret;
            if (ret == 0)
                s->work = av_frame_clone(interpolate > (s->blend_factor_max >> 1) ? s->f1 : s->f0);
        }
    }

    if (!s->work)
        return AVERROR(ENOMEM);

    s->work->pts = work_pts;
    s->n++;

    return 1;
}

static av_cold int init(AVFilterContext *ctx)
{
    FrameRateContext *s = ctx->priv;
    s->start_pts = AV_NOPTS_VALUE;
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    FrameRateContext *s = ctx->priv;
    av_frame_free(&s->f0);
    av_frame_free(&s->f1);
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUVJ411P,
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUVJ440P,
    AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV420P12,
    AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV422P12,
    AV_PIX_FMT_YUV444P9, AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUV444P12,
    AV_PIX_FMT_NONE
};

#define BLEND_FRAME_FUNC(nbits)                         \
static void blend_frames##nbits##_c(BLEND_FUNC_PARAMS)  \
{                                                       \
    int line, pixel;                                    \
    uint##nbits##_t *dstw  = (uint##nbits##_t *)dst;    \
    uint##nbits##_t *src1w = (uint##nbits##_t *)src1;   \
    uint##nbits##_t *src2w = (uint##nbits##_t *)src2;   \
    int bytes = nbits / 8;                              \
    width /= bytes;                                     \
    src1_linesize /= bytes;                             \
    src2_linesize /= bytes;                             \
    dst_linesize /= bytes;                              \
    for (line = 0; line < height; line++) {             \
        for (pixel = 0; pixel < width; pixel++)         \
            dstw[pixel] = ((src1w[pixel] * factor1) +   \
                    (src2w[pixel] * factor2) + half)    \
                    >> BLEND_FACTOR_DEPTH(nbits);       \
        src1w += src1_linesize;                         \
        src2w += src2_linesize;                         \
        dstw  += dst_linesize;                          \
    }                                                   \
}
BLEND_FRAME_FUNC(8)
BLEND_FRAME_FUNC(16)

void ff_framerate_init(FrameRateContext *s)
{
    if (s->bitdepth == 8) {
        s->blend_factor_max = 1 << BLEND_FACTOR_DEPTH(8);
        s->blend = blend_frames8_c;
    } else {
        s->blend_factor_max = 1 << BLEND_FACTOR_DEPTH(16);
        s->blend = blend_frames16_c;
    }
#if ARCH_X86
    ff_framerate_init_x86(s);
#endif
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    FrameRateContext *s = ctx->priv;
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(inlink->format);
    int plane;

    s->vsub = pix_desc->log2_chroma_h;
    for (plane = 0; plane < 4; plane++) {
        s->line_size[plane] = av_image_get_linesize(inlink->format, inlink->w, plane);
        s->height[plane] = inlink->h >> ((plane == 1 || plane == 2) ? s->vsub : 0);
    }

    s->bitdepth = pix_desc->comp[0].depth;

    s->sad = ff_scene_sad_get_fn(s->bitdepth == 8 ? 8 : 16);
    if (!s->sad)
        return AVERROR(EINVAL);

    s->srce_time_base = inlink->time_base;

    ff_framerate_init(s);

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    int ret, status;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    FrameRateContext *s = ctx->priv;
    AVFrame *inpicref;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

retry:
    ret = process_work_frame(ctx);
    if (ret < 0)
        return ret;
    else if (ret == 1)
        return ff_filter_frame(outlink, s->work);

    ret = ff_inlink_consume_frame(inlink, &inpicref);
    if (ret < 0)
        return ret;

    if (inpicref) {
        if (inpicref->interlaced_frame)
            av_log(ctx, AV_LOG_WARNING, "Interlaced frame found - the output will not be correct.\n");

        if (inpicref->pts == AV_NOPTS_VALUE) {
            av_log(ctx, AV_LOG_WARNING, "Ignoring frame without PTS.\n");
            av_frame_free(&inpicref);
        }
    }

    if (inpicref) {
        pts = av_rescale_q(inpicref->pts, s->srce_time_base, s->dest_time_base);

        if (s->f1 && pts == s->pts1) {
            av_log(ctx, AV_LOG_WARNING, "Ignoring frame with same PTS.\n");
            av_frame_free(&inpicref);
        }
    }

    if (inpicref) {
        av_frame_free(&s->f0);
        s->f0 = s->f1;
        s->pts0 = s->pts1;
        s->f1 = inpicref;
        s->pts1 = pts;
        s->delta = s->pts1 - s->pts0;
        s->score = -1.0;

        if (s->delta < 0) {
            av_log(ctx, AV_LOG_WARNING, "PTS discontinuity.\n");
            s->start_pts = s->pts1;
            s->n = 0;
            av_frame_free(&s->f0);
        }

        if (s->start_pts == AV_NOPTS_VALUE)
            s->start_pts = s->pts1;

        goto retry;
    }

    if (ff_inlink_acknowledge_status(inlink, &status, &pts)) {
        if (!s->flush) {
            s->flush = 1;
            goto retry;
        }
        ff_outlink_set_status(outlink, status, pts);
        return 0;
    }

    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    FrameRateContext *s = ctx->priv;
    int exact;

    ff_dlog(ctx, "config_output()\n");

    ff_dlog(ctx,
           "config_output() input time base:%u/%u (%f)\n",
           ctx->inputs[0]->time_base.num,ctx->inputs[0]->time_base.den,
           av_q2d(ctx->inputs[0]->time_base));

    // make sure timebase is small enough to hold the framerate

    exact = av_reduce(&s->dest_time_base.num, &s->dest_time_base.den,
                      av_gcd((int64_t)s->srce_time_base.num * s->dest_frame_rate.num,
                             (int64_t)s->srce_time_base.den * s->dest_frame_rate.den ),
                      (int64_t)s->srce_time_base.den * s->dest_frame_rate.num, INT_MAX);

    av_log(ctx, AV_LOG_INFO,
           "time base:%u/%u -> %u/%u exact:%d\n",
           s->srce_time_base.num, s->srce_time_base.den,
           s->dest_time_base.num, s->dest_time_base.den, exact);
    if (!exact) {
        av_log(ctx, AV_LOG_WARNING, "Timebase conversion is not exact\n");
    }

    outlink->frame_rate = s->dest_frame_rate;
    outlink->time_base = s->dest_time_base;

    ff_dlog(ctx,
           "config_output() output time base:%u/%u (%f) w:%d h:%d\n",
           outlink->time_base.num, outlink->time_base.den,
           av_q2d(outlink->time_base),
           outlink->w, outlink->h);


    av_log(ctx, AV_LOG_INFO, "fps -> fps:%u/%u scene score:%f interpolate start:%d end:%d\n",
            s->dest_frame_rate.num, s->dest_frame_rate.den,
            s->scene_score, s->interp_start, s->interp_end);

    return 0;
}

static const AVFilterPad framerate_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
    },
};

static const AVFilterPad framerate_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

const AVFilter ff_vf_framerate = {
    .name          = "framerate",
    .description   = NULL_IF_CONFIG_SMALL("Upsamples or downsamples progressive source between specified frame rates."),
    .priv_size     = sizeof(FrameRateContext),
    .priv_class    = &framerate_class,
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(framerate_inputs),
    FILTER_OUTPUTS(framerate_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .flags         = AVFILTER_FLAG_SLICE_THREADS,
    .activate      = activate,
};
