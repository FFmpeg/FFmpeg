/*
 * Copyright (c) 2019 Paul B Mahol
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
 * Sierpinski carpet fractal renderer
 */

#include "avfilter.h"
#include "formats.h"
#include "video.h"
#include "internal.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/lfg.h"
#include "libavutil/random_seed.h"
#include <float.h>
#include <math.h>

typedef struct SierpinskiContext {
    const AVClass *class;
    int w, h;
    int type;
    AVRational frame_rate;
    uint64_t pts;

    int64_t seed;
    int jump;

    int pos_x, pos_y;
    int dest_x, dest_y;

    AVLFG lfg;
    int (*draw_slice)(AVFilterContext *ctx, void *arg, int job, int nb_jobs);
} SierpinskiContext;

#define OFFSET(x) offsetof(SierpinskiContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption sierpinski_options[] = {
    {"size", "set frame size", OFFSET(w),          AV_OPT_TYPE_IMAGE_SIZE, {.str="640x480"}, 0,          0, FLAGS },
    {"s",    "set frame size", OFFSET(w),          AV_OPT_TYPE_IMAGE_SIZE, {.str="640x480"}, 0,          0, FLAGS },
    {"rate", "set frame rate", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str="25"},      0,    INT_MAX, FLAGS },
    {"r",    "set frame rate", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str="25"},      0,    INT_MAX, FLAGS },
    {"seed", "set the seed",   OFFSET(seed),       AV_OPT_TYPE_INT64,      {.i64=-1},       -1, UINT32_MAX, FLAGS },
    {"jump", "set the jump",   OFFSET(jump),       AV_OPT_TYPE_INT,        {.i64=100},       1,      10000, FLAGS },
    {"type","set fractal type",OFFSET(type),       AV_OPT_TYPE_INT,        {.i64=0},         0,          1, FLAGS, "type" },
    {"carpet", "sierpinski carpet", 0,             AV_OPT_TYPE_CONST,      {.i64=0},         0,          0, FLAGS, "type" },
    {"triangle", "sierpinski triangle", 0,         AV_OPT_TYPE_CONST,      {.i64=1},         0,          0, FLAGS, "type" },
    {NULL},
};

AVFILTER_DEFINE_CLASS(sierpinski);

static int fill_sierpinski(SierpinskiContext *s, int x, int y)
{
    int pos_x = x + s->pos_x;
    int pos_y = y + s->pos_y;

    while (pos_x != 0 && pos_y != 0) {
        if (FFABS(pos_x % 3) == 1 && FFABS(pos_y % 3) == 1)
            return 1;

        pos_x /= 3;
        pos_y /= 3;
    }

    return 0;
}

static int draw_triangle_slice(AVFilterContext *ctx, void *arg, int job, int nb_jobs)
{
    SierpinskiContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int width  = frame->width;
    const int height = frame->height;
    const int start = (height *  job   ) / nb_jobs;
    const int end   = (height * (job+1)) / nb_jobs;
    uint8_t *dst = frame->data[0] + start * frame->linesize[0];

    for (int y = start; y < end; y++) {
        for (int x = 0; x < width; x++) {
            if ((s->pos_x + x) & (s->pos_y + y)) {
                AV_WL32(&dst[x*4], 0x00000000);
            } else {
                AV_WL32(&dst[x*4], 0xFFFFFFFF);
            }
        }

        dst += frame->linesize[0];
    }

    return 0;
}

static int draw_carpet_slice(AVFilterContext *ctx, void *arg, int job, int nb_jobs)
{
    SierpinskiContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int width  = frame->width;
    const int height = frame->height;
    const int start = (height *  job   ) / nb_jobs;
    const int end   = (height * (job+1)) / nb_jobs;
    uint8_t *dst = frame->data[0] + start * frame->linesize[0];

    for (int y = start; y < end; y++) {
        for (int x = 0; x < width; x++) {
            if (fill_sierpinski(s, x, y)) {
                AV_WL32(&dst[x*4], 0x00000000);
            } else {
                AV_WL32(&dst[x*4], 0xFFFFFFFF);
            }
        }

        dst += frame->linesize[0];
    }

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    SierpinskiContext *s = ctx->priv;

    if (av_image_check_size(s->w, s->h, 0, ctx) < 0)
        return AVERROR(EINVAL);

    outlink->w = s->w;
    outlink->h = s->h;
    outlink->time_base = av_inv_q(s->frame_rate);
    outlink->sample_aspect_ratio = (AVRational) {1, 1};
    outlink->frame_rate = s->frame_rate;
    if (s->seed == -1)
        s->seed = av_get_random_seed();
    av_lfg_init(&s->lfg, s->seed);

    s->draw_slice = s->type ? draw_triangle_slice : draw_carpet_slice;

    return 0;
}

static void draw_sierpinski(AVFilterContext *ctx, AVFrame *frame)
{
    SierpinskiContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    if (s->pos_x == s->dest_x && s->pos_y == s->dest_y) {
        unsigned int rnd = av_lfg_get(&s->lfg);
        int mod = 2 * s->jump + 1;

        s->dest_x += (int)((rnd & 0xffff) % mod) - s->jump;
        s->dest_y += (int)((rnd >>    16) % mod) - s->jump;
    } else {
        if (s->pos_x < s->dest_x)
            s->pos_x++;
        else if (s->pos_x > s->dest_x)
            s->pos_x--;

        if (s->pos_y < s->dest_y)
            s->pos_y++;
        else if (s->pos_y > s->dest_y)
            s->pos_y--;
    }

    ff_filter_execute(ctx, s->draw_slice, frame, NULL,
                      FFMIN(outlink->h, ff_filter_get_nb_threads(ctx)));
}

static int sierpinski_request_frame(AVFilterLink *link)
{
    SierpinskiContext *s = link->src->priv;
    AVFrame *frame = ff_get_video_buffer(link, s->w, s->h);

    if (!frame)
        return AVERROR(ENOMEM);

    frame->sample_aspect_ratio = (AVRational) {1, 1};
    frame->pts = s->pts++;
    frame->duration = 1;

    draw_sierpinski(link->src, frame);

    return ff_filter_frame(link, frame);
}

static const AVFilterPad sierpinski_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = sierpinski_request_frame,
        .config_props  = config_output,
    },
};

const AVFilter ff_vsrc_sierpinski = {
    .name          = "sierpinski",
    .description   = NULL_IF_CONFIG_SMALL("Render a Sierpinski fractal."),
    .priv_size     = sizeof(SierpinskiContext),
    .priv_class    = &sierpinski_class,
    .inputs        = NULL,
    FILTER_OUTPUTS(sierpinski_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_0BGR32),
    .flags         = AVFILTER_FLAG_SLICE_THREADS,
};
