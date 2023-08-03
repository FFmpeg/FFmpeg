/*
 * Copyright (c) 2017 Paul B Mahol
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

#include "libavutil/opt.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "filters.h"
#include "internal.h"
#include "video.h"

typedef struct Points {
    uint16_t x, y;
} Points;

typedef struct FloodfillContext {
    const AVClass *class;

    int x, y;
    int s[4];
    int S[4];
    int d[4];

    int nb_planes;
    int back, front;
    Points *points;

    int (*is_same)(const AVFrame *frame, int x, int y,
                   unsigned s0, unsigned s1, unsigned s2, unsigned s3);
    void (*set_pixel)(AVFrame *frame, int x, int y,
                      unsigned d0, unsigned d1, unsigned d2, unsigned d3);
    void (*pick_pixel)(const AVFrame *frame, int x, int y,
                       int *s0, int *s1, int *s2, int *s3);
} FloodfillContext;

static int is_inside(int x, int y, int w, int h)
{
    if (x >= 0 && x < w && y >= 0 && y < h)
        return 1;
    return 0;
}

static int is_same4(const AVFrame *frame, int x, int y,
                    unsigned s0, unsigned s1, unsigned s2, unsigned s3)
{
    unsigned c0 = frame->data[0][y * frame->linesize[0] + x];
    unsigned c1 = frame->data[1][y * frame->linesize[1] + x];
    unsigned c2 = frame->data[2][y * frame->linesize[2] + x];
    unsigned c3 = frame->data[3][y * frame->linesize[3] + x];

    if (s0 == c0 && s1 == c1 && s2 == c2 && s3 == c3)
        return 1;
    return 0;
}

static int is_same4_16(const AVFrame *frame, int x, int y,
                       unsigned s0, unsigned s1, unsigned s2, unsigned s3)
{
    unsigned c0 = AV_RN16(frame->data[0] + y * frame->linesize[0] + 2 * x);
    unsigned c1 = AV_RN16(frame->data[1] + y * frame->linesize[1] + 2 * x);
    unsigned c2 = AV_RN16(frame->data[2] + y * frame->linesize[2] + 2 * x);
    unsigned c3 = AV_RN16(frame->data[3] + y * frame->linesize[3] + 2 * x);

    if (s0 == c0 && s1 == c1 && s2 == c2 && s3 == c3)
        return 1;
    return 0;
}

static int is_same3(const AVFrame *frame, int x, int y,
                    unsigned s0, unsigned s1, unsigned s2, unsigned s3)
{
    unsigned c0 = frame->data[0][y * frame->linesize[0] + x];
    unsigned c1 = frame->data[1][y * frame->linesize[1] + x];
    unsigned c2 = frame->data[2][y * frame->linesize[2] + x];

    if (s0 == c0 && s1 == c1 && s2 == c2)
        return 1;
    return 0;
}

static int is_same3_16(const AVFrame *frame, int x, int y,
                       unsigned s0, unsigned s1, unsigned s2, unsigned s3)
{
    unsigned c0 = AV_RN16(frame->data[0] + y * frame->linesize[0] + 2 * x);
    unsigned c1 = AV_RN16(frame->data[1] + y * frame->linesize[1] + 2 * x);
    unsigned c2 = AV_RN16(frame->data[2] + y * frame->linesize[2] + 2 * x);

    if (s0 == c0 && s1 == c1 && s2 == c2)
        return 1;
    return 0;
}

static int is_same1(const AVFrame *frame, int x, int y,
                    unsigned s0, unsigned s1, unsigned s2, unsigned s3)
{
    unsigned c0 = frame->data[0][y * frame->linesize[0] + x];

    if (s0 == c0)
        return 1;
    return 0;
}

static int is_same1_16(const AVFrame *frame, int x, int y,
                       unsigned s0, unsigned s1, unsigned s2, unsigned s3)
{
    unsigned c0 = AV_RN16(frame->data[0] + y * frame->linesize[0] + 2 * x);

    if (s0 == c0)
        return 1;
    return 0;
}

static void set_pixel1(AVFrame *frame, int x, int y,
                       unsigned d0, unsigned d1, unsigned d2, unsigned d3)
{
    frame->data[0][y * frame->linesize[0] + x] = d0;
}

static void set_pixel1_16(AVFrame *frame, int x, int y,
                          unsigned d0, unsigned d1, unsigned d2, unsigned d3)
{
    AV_WN16(frame->data[0] + y * frame->linesize[0] + 2 * x, d0);
}

static void set_pixel3(AVFrame *frame, int x, int y,
                       unsigned d0, unsigned d1, unsigned d2, unsigned d3)
{
    frame->data[0][y * frame->linesize[0] + x] = d0;
    frame->data[1][y * frame->linesize[1] + x] = d1;
    frame->data[2][y * frame->linesize[2] + x] = d2;
}

static void set_pixel3_16(AVFrame *frame, int x, int y,
                          unsigned d0, unsigned d1, unsigned d2, unsigned d3)
{
    AV_WN16(frame->data[0] + y * frame->linesize[0] + 2 * x, d0);
    AV_WN16(frame->data[1] + y * frame->linesize[1] + 2 * x, d1);
    AV_WN16(frame->data[2] + y * frame->linesize[2] + 2 * x, d2);
}

static void set_pixel4(AVFrame *frame, int x, int y,
                       unsigned d0, unsigned d1, unsigned d2, unsigned d3)
{
    frame->data[0][y * frame->linesize[0] + x] = d0;
    frame->data[1][y * frame->linesize[1] + x] = d1;
    frame->data[2][y * frame->linesize[2] + x] = d2;
    frame->data[3][y * frame->linesize[3] + x] = d3;
}

static void set_pixel4_16(AVFrame *frame, int x, int y,
                          unsigned d0, unsigned d1, unsigned d2, unsigned d3)
{
    AV_WN16(frame->data[0] + y * frame->linesize[0] + 2 * x, d0);
    AV_WN16(frame->data[1] + y * frame->linesize[1] + 2 * x, d1);
    AV_WN16(frame->data[2] + y * frame->linesize[2] + 2 * x, d2);
    AV_WN16(frame->data[3] + y * frame->linesize[3] + 2 * x, d3);
}

static void pick_pixel1(const AVFrame *frame, int x, int y,
                        int *s0, int *s1, int *s2, int *s3)
{
    if (*s0 < 0)
        *s0 = frame->data[0][y * frame->linesize[0] + x];
}

static void pick_pixel1_16(const AVFrame *frame, int x, int y,
                           int *s0, int *s1, int *s2, int *s3)
{
    if (*s0 < 0)
        *s0 = AV_RN16(frame->data[0] + y * frame->linesize[0] + 2 * x);
}

static void pick_pixel3(const AVFrame *frame, int x, int y,
                        int *s0, int *s1, int *s2, int *s3)
{
    if (*s0 < 0)
        *s0 = frame->data[0][y * frame->linesize[0] + x];
    if (*s1 < 0)
        *s1 = frame->data[1][y * frame->linesize[1] + x];
    if (*s2 < 0)
        *s2 = frame->data[2][y * frame->linesize[2] + x];
}

static void pick_pixel3_16(const AVFrame *frame, int x, int y,
                           int *s0, int *s1, int *s2, int *s3)
{
    if (*s0 < 0)
        *s0 = AV_RN16(frame->data[0] + y * frame->linesize[0] + 2 * x);
    if (*s1 < 0)
        *s1 = AV_RN16(frame->data[1] + y * frame->linesize[1] + 2 * x);
    if (*s2 < 0)
        *s2 = AV_RN16(frame->data[2] + y * frame->linesize[2] + 2 * x);
}

static void pick_pixel4(const AVFrame *frame, int x, int y,
                        int *s0, int *s1, int *s2, int *s3)
{
    if (*s0 < 0)
        *s0 = frame->data[0][y * frame->linesize[0] + x];
    if (*s1 < 0)
        *s1 = frame->data[1][y * frame->linesize[1] + x];
    if (*s2 < 0)
        *s2 = frame->data[2][y * frame->linesize[2] + x];
    if (*s3 < 0)
        *s3 = frame->data[3][y * frame->linesize[3] + x];
}

static void pick_pixel4_16(const AVFrame *frame, int x, int y,
                           int *s0, int *s1, int *s2, int *s3)
{
    if (*s0 < 0)
        *s0 = AV_RN16(frame->data[0] + y * frame->linesize[0] + 2 * x);
    if (*s1 < 0)
        *s1 = AV_RN16(frame->data[1] + y * frame->linesize[1] + 2 * x);
    if (*s2 < 0)
        *s2 = AV_RN16(frame->data[2] + y * frame->linesize[2] + 2 * x);
    if (*s3 < 0)
        *s3 = AV_RN16(frame->data[3] + y * frame->linesize[3] + 2 * x);
}

static int config_input(AVFilterLink *inlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    AVFilterContext *ctx = inlink->dst;
    FloodfillContext *s = ctx->priv;
    int depth;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);
    depth = desc->comp[0].depth;
    if (depth == 8) {
        switch (s->nb_planes) {
        case 1: s->set_pixel  = set_pixel1;
                s->is_same    = is_same1;
                s->pick_pixel = pick_pixel1; break;
        case 3: s->set_pixel  = set_pixel3;
                s->is_same    = is_same3;
                s->pick_pixel = pick_pixel3; break;
        case 4: s->set_pixel  = set_pixel4;
                s->is_same    = is_same4;
                s->pick_pixel = pick_pixel4; break;
       }
    } else {
        switch (s->nb_planes) {
        case 1: s->set_pixel  = set_pixel1_16;
                s->is_same    = is_same1_16;
                s->pick_pixel = pick_pixel1_16; break;
        case 3: s->set_pixel  = set_pixel3_16;
                s->is_same    = is_same3_16;
                s->pick_pixel = pick_pixel3_16; break;
        case 4: s->set_pixel  = set_pixel4_16;
                s->is_same    = is_same4_16;
                s->pick_pixel = pick_pixel4_16; break;
       }
    }

    s->front = s->back = 0;
    s->points = av_calloc(inlink->w * inlink->h, 4 * sizeof(Points));
    if (!s->points)
        return AVERROR(ENOMEM);

    return 0;
}

static int filter_frame(AVFilterLink *link, AVFrame *frame)
{
    AVFilterContext *ctx = link->dst;
    FloodfillContext *s = ctx->priv;
    const unsigned d0 = s->d[0];
    const unsigned d1 = s->d[1];
    const unsigned d2 = s->d[2];
    const unsigned d3 = s->d[3];
    int s0 = s->s[0];
    int s1 = s->s[1];
    int s2 = s->s[2];
    int s3 = s->s[3];
    const int w = frame->width;
    const int h = frame->height;
    int i, ret;

    if (is_inside(s->x, s->y, w, h)) {
        s->pick_pixel(frame, s->x, s->y, &s0, &s1, &s2, &s3);

        s->S[0] = s0;
        s->S[1] = s1;
        s->S[2] = s2;
        s->S[3] = s3;
        for (i = 0; i < s->nb_planes; i++) {
            if (s->S[i] != s->d[i])
                break;
        }

        if (i == s->nb_planes)
            goto end;

        if (s->is_same(frame, s->x, s->y, s0, s1, s2, s3)) {
            s->points[s->front].x = s->x;
            s->points[s->front].y = s->y;
            s->front++;
        }

        if (ret = ff_inlink_make_frame_writable(link, &frame)) {
            av_frame_free(&frame);
            return ret;
        }

        while (s->front > s->back) {
            int x, y;

            s->front--;
            x = s->points[s->front].x;
            y = s->points[s->front].y;

            if (s->is_same(frame, x, y, s0, s1, s2, s3)) {
                s->set_pixel(frame, x, y, d0, d1, d2, d3);

                if (is_inside(x + 1, y, w, h)) {
                    s->points[s->front]  .x = x + 1;
                    s->points[s->front++].y = y;
                }

                if (is_inside(x - 1, y, w, h)) {
                    s->points[s->front]  .x = x - 1;
                    s->points[s->front++].y = y;
                }

                if (is_inside(x, y + 1, w, h)) {
                    s->points[s->front]  .x = x;
                    s->points[s->front++].y = y + 1;
                }

                if (is_inside(x, y - 1, w, h)) {
                    s->points[s->front]  .x = x;
                    s->points[s->front++].y = y - 1;
                }
            }
        }
    }

end:
    return ff_filter_frame(ctx->outputs[0], frame);
}

static const enum AVPixelFormat pixel_fmts[] = {
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
    AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRAP10,
    AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
    AV_PIX_FMT_GBRAP16, AV_PIX_FMT_GBRAP,
    AV_PIX_FMT_YUV444P9, AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUVA444P10,
    AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV444P14, AV_PIX_FMT_YUV444P16, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_NONE
};

static av_cold void uninit(AVFilterContext *ctx)
{
    FloodfillContext *s = ctx->priv;

    av_freep(&s->points);
}

static const AVFilterPad floodfill_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

#define OFFSET(x) offsetof(FloodfillContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption floodfill_options[] = {
    { "x",  "set pixel x coordinate",             OFFSET(x),    AV_OPT_TYPE_INT, {.i64=0}, 0, UINT16_MAX, FLAGS },
    { "y",  "set pixel y coordinate",             OFFSET(y),    AV_OPT_TYPE_INT, {.i64=0}, 0, UINT16_MAX, FLAGS },
    { "s0", "set source #0 component value",      OFFSET(s[0]), AV_OPT_TYPE_INT, {.i64=0},-1, UINT16_MAX, FLAGS },
    { "s1", "set source #1 component value",      OFFSET(s[1]), AV_OPT_TYPE_INT, {.i64=0},-1, UINT16_MAX, FLAGS },
    { "s2", "set source #2 component value",      OFFSET(s[2]), AV_OPT_TYPE_INT, {.i64=0},-1, UINT16_MAX, FLAGS },
    { "s3", "set source #3 component value",      OFFSET(s[3]), AV_OPT_TYPE_INT, {.i64=0},-1, UINT16_MAX, FLAGS },
    { "d0", "set destination #0 component value", OFFSET(d[0]), AV_OPT_TYPE_INT, {.i64=0}, 0, UINT16_MAX, FLAGS },
    { "d1", "set destination #1 component value", OFFSET(d[1]), AV_OPT_TYPE_INT, {.i64=0}, 0, UINT16_MAX, FLAGS },
    { "d2", "set destination #2 component value", OFFSET(d[2]), AV_OPT_TYPE_INT, {.i64=0}, 0, UINT16_MAX, FLAGS },
    { "d3", "set destination #3 component value", OFFSET(d[3]), AV_OPT_TYPE_INT, {.i64=0}, 0, UINT16_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(floodfill);

const AVFilter ff_vf_floodfill = {
    .name          = "floodfill",
    .description   = NULL_IF_CONFIG_SMALL("Fill area with same color with another color."),
    .priv_size     = sizeof(FloodfillContext),
    .priv_class    = &floodfill_class,
    .uninit        = uninit,
    FILTER_INPUTS(floodfill_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS_ARRAY(pixel_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
