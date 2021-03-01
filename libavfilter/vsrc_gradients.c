/*
 * Copyright (c) 2020 Paul B Mahol
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

#include "avfilter.h"
#include "filters.h"
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

typedef struct GradientsContext {
    const AVClass *class;
    int w, h;
    int type;
    AVRational frame_rate;
    int64_t pts;
    int64_t duration;           ///< duration expressed in microseconds
    float speed;

    uint8_t color_rgba[8][4];
    int nb_colors;
    int x0, y0, x1, y1;
    float fx0, fy0, fx1, fy1;

    int64_t seed;

    AVLFG lfg;
    int (*draw_slice)(AVFilterContext *ctx, void *arg, int job, int nb_jobs);
} GradientsContext;

#define OFFSET(x) offsetof(GradientsContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption gradients_options[] = {
    {"size",      "set frame size", OFFSET(w),             AV_OPT_TYPE_IMAGE_SIZE, {.str="640x480"},  0, 0, FLAGS },
    {"s",         "set frame size", OFFSET(w),             AV_OPT_TYPE_IMAGE_SIZE, {.str="640x480"},  0, 0, FLAGS },
    {"rate",      "set frame rate", OFFSET(frame_rate),    AV_OPT_TYPE_VIDEO_RATE, {.str="25"},       0, INT_MAX, FLAGS },
    {"r",         "set frame rate", OFFSET(frame_rate),    AV_OPT_TYPE_VIDEO_RATE, {.str="25"},       0, INT_MAX, FLAGS },
    {"c0",        "set 1st color",  OFFSET(color_rgba[0]), AV_OPT_TYPE_COLOR,      {.str = "random"}, 0, 0, FLAGS },
    {"c1",        "set 2nd color",  OFFSET(color_rgba[1]), AV_OPT_TYPE_COLOR,      {.str = "random"}, 0, 0, FLAGS },
    {"c2",        "set 3rd color",  OFFSET(color_rgba[2]), AV_OPT_TYPE_COLOR,      {.str = "random"}, 0, 0, FLAGS },
    {"c3",        "set 4th color",  OFFSET(color_rgba[3]), AV_OPT_TYPE_COLOR,      {.str = "random"}, 0, 0, FLAGS },
    {"c4",        "set 5th color",  OFFSET(color_rgba[4]), AV_OPT_TYPE_COLOR,      {.str = "random"}, 0, 0, FLAGS },
    {"c5",        "set 6th color",  OFFSET(color_rgba[5]), AV_OPT_TYPE_COLOR,      {.str = "random"}, 0, 0, FLAGS },
    {"c6",        "set 7th color",  OFFSET(color_rgba[6]), AV_OPT_TYPE_COLOR,      {.str = "random"}, 0, 0, FLAGS },
    {"c7",        "set 8th color",  OFFSET(color_rgba[7]), AV_OPT_TYPE_COLOR,      {.str = "random"}, 0, 0, FLAGS },
    {"x0",        "set gradient line source x0",      OFFSET(x0), AV_OPT_TYPE_INT, {.i64=-1},        -1, INT_MAX, FLAGS },
    {"y0",        "set gradient line source y0",      OFFSET(y0), AV_OPT_TYPE_INT, {.i64=-1},        -1, INT_MAX, FLAGS },
    {"x1",        "set gradient line destination x1", OFFSET(x1), AV_OPT_TYPE_INT, {.i64=-1},        -1, INT_MAX, FLAGS },
    {"y1",        "set gradient line destination y1", OFFSET(y1), AV_OPT_TYPE_INT, {.i64=-1},        -1, INT_MAX, FLAGS },
    {"nb_colors", "set the number of colors", OFFSET(nb_colors), AV_OPT_TYPE_INT,  {.i64=2},          2, 8, FLAGS },
    {"n",         "set the number of colors", OFFSET(nb_colors), AV_OPT_TYPE_INT,  {.i64=2},          2, 8, FLAGS },
    {"seed",      "set the seed",   OFFSET(seed),          AV_OPT_TYPE_INT64,      {.i64=-1},        -1, UINT32_MAX, FLAGS },
    {"duration",  "set video duration", OFFSET(duration),  AV_OPT_TYPE_DURATION,   {.i64=-1},        -1, INT64_MAX, FLAGS },\
    {"d",         "set video duration", OFFSET(duration),  AV_OPT_TYPE_DURATION,   {.i64=-1},        -1, INT64_MAX, FLAGS },\
    {"speed",     "set gradients rotation speed", OFFSET(speed), AV_OPT_TYPE_FLOAT,{.dbl=0.01}, 0.00001, 1, FLAGS },\
    {NULL},
};

AVFILTER_DEFINE_CLASS(gradients);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_RGBA,
        AV_PIX_FMT_RGBA64,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static uint32_t lerp_color(uint8_t c0[4], uint8_t c1[4], float x)
{
    const float y = 1.f - x;

    return (lrintf(c0[0] * y + c1[0] * x)) << 0  |
           (lrintf(c0[1] * y + c1[1] * x)) << 8  |
           (lrintf(c0[2] * y + c1[2] * x)) << 16 |
           (lrintf(c0[3] * y + c1[3] * x)) << 24;
}

static uint64_t lerp_color16(uint8_t c0[4], uint8_t c1[4], float x)
{
    const float y = 1.f - x;

    return (llrintf((c0[0] * y + c1[0] * x) * 256)) << 0  |
           (llrintf((c0[1] * y + c1[1] * x) * 256)) << 16 |
           (llrintf((c0[2] * y + c1[2] * x) * 256)) << 32 |
           (llrintf((c0[3] * y + c1[3] * x) * 256)) << 48;
}

static uint32_t lerp_colors(uint8_t arr[3][4], int nb_colors, float step)
{
    float scl;
    int i;

    if (nb_colors == 1 || step <= 0.0) {
        return arr[0][0] | (arr[0][1] << 8) | (arr[0][2] << 16) | (arr[0][3] << 24);
    } else if (step >= 1.0) {
        i = nb_colors - 1;
        return arr[i][0] | (arr[i][1] << 8) | (arr[i][2] << 16) | (arr[i][3] << 24);
    }

    scl = step * (nb_colors - 1);
    i = floorf(scl);

    return lerp_color(arr[i], arr[i + 1], scl - i);
}

static uint64_t lerp_colors16(uint8_t arr[3][4], int nb_colors, float step)
{
    float scl;
    int i;

    if (nb_colors == 1 || step <= 0.0) {
        return ((uint64_t)arr[0][0] << 8) | ((uint64_t)arr[0][1] << 24) | ((uint64_t)arr[0][2] << 40) | ((uint64_t)arr[0][3] << 56);
    } else if (step >= 1.0) {
        i = nb_colors - 1;
        return ((uint64_t)arr[i][0] << 8) | ((uint64_t)arr[i][1] << 24) | ((uint64_t)arr[i][2] << 40) | ((uint64_t)arr[i][3] << 56);
    }

    scl = step * (nb_colors - 1);
    i = floorf(scl);

    return lerp_color16(arr[i], arr[i + 1], scl - i);
}

static float project(float origin_x, float origin_y,
                     float dest_x, float dest_y,
                     int point_x, int point_y)
{
    // Rise and run of line.
    float od_x = dest_x - origin_x;
    float od_y = dest_y - origin_y;

    // Distance-squared of line.
    float od_s_q = od_x * od_x + od_y * od_y;

    // Rise and run of projection.
    float op_x = point_x - origin_x;
    float op_y = point_y - origin_y;
    float op_x_od = op_x * od_x + op_y * od_y;

    // Normalize and clamp range.
    return av_clipf(op_x_od / od_s_q, 0.f, 1.f);
}

static int draw_gradients_slice(AVFilterContext *ctx, void *arg, int job, int nb_jobs)
{
    GradientsContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int width  = frame->width;
    const int height = frame->height;
    const int start = (height *  job   ) / nb_jobs;
    const int end   = (height * (job+1)) / nb_jobs;
    const int linesize = frame->linesize[0] / 4;
    uint32_t *dst = (uint32_t *)frame->data[0] + start * linesize;

    for (int y = start; y < end; y++) {
        for (int x = 0; x < width; x++) {
            float factor = project(s->fx0, s->fy0, s->fx1, s->fy1, x, y);
            dst[x] = lerp_colors(s->color_rgba, s->nb_colors, factor);
        }

        dst += linesize;
    }

    return 0;
}

static int draw_gradients_slice16(AVFilterContext *ctx, void *arg, int job, int nb_jobs)
{
    GradientsContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int width  = frame->width;
    const int height = frame->height;
    const int start = (height *  job   ) / nb_jobs;
    const int end   = (height * (job+1)) / nb_jobs;
    const int linesize = frame->linesize[0] / 8;
    uint64_t *dst = (uint64_t *)frame->data[0] + start * linesize;

    for (int y = start; y < end; y++) {
        for (int x = 0; x < width; x++) {
            float factor = project(s->fx0, s->fy0, s->fx1, s->fy1, x, y);
            dst[x] = lerp_colors16(s->color_rgba, s->nb_colors, factor);
        }

        dst += linesize;
    }

    return 0;
}static int config_output(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->src;
    GradientsContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    if (av_image_check_size(s->w, s->h, 0, ctx) < 0)
        return AVERROR(EINVAL);

    inlink->w = s->w;
    inlink->h = s->h;
    inlink->time_base = av_inv_q(s->frame_rate);
    inlink->sample_aspect_ratio = (AVRational) {1, 1};
    if (s->seed == -1)
        s->seed = av_get_random_seed();
    av_lfg_init(&s->lfg, s->seed);

    s->draw_slice = desc->comp[0].depth == 16 ? draw_gradients_slice16 : draw_gradients_slice;

    if (s->x0 < 0 || s->x0 >= s->w)
        s->x0 = av_lfg_get(&s->lfg) % s->w;
    if (s->y0 < 0 || s->y0 >= s->h)
        s->y0 = av_lfg_get(&s->lfg) % s->h;
    if (s->x1 < 0 || s->x1 >= s->w)
        s->x1 = av_lfg_get(&s->lfg) % s->w;
    if (s->y1 < 0 || s->y1 >= s->h)
        s->y1 = av_lfg_get(&s->lfg) % s->h;

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    GradientsContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    if (s->duration >= 0 &&
        av_rescale_q(s->pts, outlink->time_base, AV_TIME_BASE_Q) >= s->duration) {
        ff_outlink_set_status(outlink, AVERROR_EOF, s->pts);
        return 0;
    }

    if (ff_outlink_frame_wanted(outlink)) {
        AVFrame *frame = ff_get_video_buffer(outlink, s->w, s->h);
        float angle = fmodf(s->pts * s->speed, 2.f * M_PI);
        const float w2 = s->w / 2.f;
        const float h2 = s->h / 2.f;

        s->fx0 = (s->x0 - w2) * cosf(angle) - (s->y0 - h2) * sinf(angle) + w2;
        s->fy0 = (s->x0 - w2) * sinf(angle) + (s->y0 - h2) * cosf(angle) + h2;

        s->fx1 = (s->x1 - w2) * cosf(angle) - (s->y1 - h2) * sinf(angle) + w2;
        s->fy1 = (s->x1 - w2) * sinf(angle) + (s->y1 - h2) * cosf(angle) + h2;

        if (!frame)
            return AVERROR(ENOMEM);

        frame->key_frame           = 1;
        frame->interlaced_frame    = 0;
        frame->pict_type           = AV_PICTURE_TYPE_I;
        frame->sample_aspect_ratio = (AVRational) {1, 1};
        frame->pts = s->pts++;

        ctx->internal->execute(ctx, s->draw_slice, frame, NULL,
                               FFMIN(outlink->h, ff_filter_get_nb_threads(ctx)));

        return ff_filter_frame(outlink, frame);
    }

    return FFERROR_NOT_READY;
}

static const AVFilterPad gradients_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
    { NULL }
};

AVFilter ff_vsrc_gradients = {
    .name          = "gradients",
    .description   = NULL_IF_CONFIG_SMALL("Draw a gradients."),
    .priv_size     = sizeof(GradientsContext),
    .priv_class    = &gradients_class,
    .query_formats = query_formats,
    .inputs        = NULL,
    .outputs       = gradients_outputs,
    .activate      = activate,
    .flags         = AVFILTER_FLAG_SLICE_THREADS,
};
