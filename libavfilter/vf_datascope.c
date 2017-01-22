/*
 * Copyright (c) 2016 Paul B Mahol
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

#include "libavutil/avassert.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/xga_font_data.h"
#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct DatascopeContext {
    const AVClass *class;
    int ow, oh;
    int x, y;
    int mode;
    int axis;
    float opacity;

    int nb_planes;
    int nb_comps;
    int chars;
    FFDrawContext draw;
    FFDrawColor yellow;
    FFDrawColor white;
    FFDrawColor black;
    FFDrawColor gray;

    void (*pick_color)(FFDrawContext *draw, FFDrawColor *color, AVFrame *in, int x, int y, int *value);
    void (*reverse_color)(FFDrawContext *draw, FFDrawColor *color, FFDrawColor *reverse);
    int (*filter)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} DatascopeContext;

#define OFFSET(x) offsetof(DatascopeContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption datascope_options[] = {
    { "size", "set output size", OFFSET(ow),   AV_OPT_TYPE_IMAGE_SIZE, {.str="hd720"}, 0, 0, FLAGS },
    { "s",    "set output size", OFFSET(ow),   AV_OPT_TYPE_IMAGE_SIZE, {.str="hd720"}, 0, 0, FLAGS },
    { "x",    "set x offset", OFFSET(x),    AV_OPT_TYPE_INT, {.i64=0}, 0, INT_MAX, FLAGS },
    { "y",    "set y offset", OFFSET(y),    AV_OPT_TYPE_INT, {.i64=0}, 0, INT_MAX, FLAGS },
    { "mode", "set scope mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=0}, 0, 2, FLAGS, "mode" },
    {   "mono",   NULL, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "mode" },
    {   "color",  NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "mode" },
    {   "color2", NULL, 0, AV_OPT_TYPE_CONST, {.i64=2}, 0, 0, FLAGS, "mode" },
    { "axis",    "draw column/row numbers", OFFSET(axis), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { "opacity", "set background opacity", OFFSET(opacity), AV_OPT_TYPE_FLOAT, {.dbl=0.75}, 0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(datascope);

static int query_formats(AVFilterContext *ctx)
{
    return ff_set_common_formats(ctx, ff_draw_supported_pixel_formats(0));
}

static void draw_text(DatascopeContext *s, AVFrame *frame, FFDrawColor *color,
                      int x0, int y0, const uint8_t *text, int vertical)
{
    int x = x0;

    for (; *text; text++) {
        if (*text == '\n') {
            x = x0;
            y0 += 8;
            continue;
        }
        ff_blend_mask(&s->draw, color, frame->data, frame->linesize,
                      frame->width, frame->height,
                      avpriv_cga_font + *text * 8, 1, 8, 8, 0, 0, x, y0);
        if (vertical) {
            x = x0;
            y0 += 8;
        } else {
            x += 8;
        }
    }
}

static void pick_color8(FFDrawContext *draw, FFDrawColor *color, AVFrame *in, int x, int y, int *value)
{
    int p, i;

    color->rgba[3] = 255;
    for (p = 0; p < draw->nb_planes; p++) {
        if (draw->nb_planes == 1) {
            for (i = 0; i < 4; i++) {
                value[i] = in->data[0][y * in->linesize[0] + x * draw->pixelstep[0] + i];
                color->comp[0].u8[i] = value[i];
            }
        } else {
            value[p] = in->data[p][(y >> draw->vsub[p]) * in->linesize[p] + (x >> draw->hsub[p])];
            color->comp[p].u8[0] = value[p];
        }
    }
}

static void pick_color16(FFDrawContext *draw, FFDrawColor *color, AVFrame *in, int x, int y, int *value)
{
    int p, i;

    color->rgba[3] = 255;
    for (p = 0; p < draw->nb_planes; p++) {
        if (draw->nb_planes == 1) {
            for (i = 0; i < 4; i++) {
                value[i] = AV_RL16(in->data[0] + y * in->linesize[0] + x * draw->pixelstep[0] + i * 2);
                color->comp[0].u16[i] = value[i];
            }
        } else {
            value[p] = AV_RL16(in->data[p] + (y >> draw->vsub[p]) * in->linesize[p] + (x >> draw->hsub[p]) * 2);
            color->comp[p].u16[0] = value[p];
        }
    }
}

static void reverse_color8(FFDrawContext *draw, FFDrawColor *color, FFDrawColor *reverse)
{
    int p;

    reverse->rgba[3] = 255;
    for (p = 0; p < draw->nb_planes; p++) {
        reverse->comp[p].u8[0] = color->comp[p].u8[0] > 127 ? 0 : 255;
        reverse->comp[p].u8[1] = color->comp[p].u8[1] > 127 ? 0 : 255;
        reverse->comp[p].u8[2] = color->comp[p].u8[2] > 127 ? 0 : 255;
    }
}

static void reverse_color16(FFDrawContext *draw, FFDrawColor *color, FFDrawColor *reverse)
{
    int p;

    reverse->rgba[3] = 255;
    for (p = 0; p < draw->nb_planes; p++) {
        const unsigned max = (1 << draw->desc->comp[p].depth) - 1;
        const unsigned mid = (max + 1) / 2;

        reverse->comp[p].u16[0] = color->comp[p].u16[0] > mid ? 0 : max;
        reverse->comp[p].u16[1] = color->comp[p].u16[1] > mid ? 0 : max;
        reverse->comp[p].u16[2] = color->comp[p].u16[2] > mid ? 0 : max;
    }
}

typedef struct ThreadData {
    AVFrame *in, *out;
    int xoff, yoff;
} ThreadData;

static int filter_color2(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    DatascopeContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFilterLink *inlink = ctx->inputs[0];
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int xoff = td->xoff;
    const int yoff = td->yoff;
    const int P = FFMAX(s->nb_planes, s->nb_comps);
    const int C = s->chars;
    const int W = (outlink->w - xoff) / (C * 10);
    const int H = (outlink->h - yoff) / (P * 12);
    const char *format[2] = {"%02X\n", "%04X\n"};
    const int slice_start = (W * jobnr) / nb_jobs;
    const int slice_end = (W * (jobnr+1)) / nb_jobs;
    int x, y, p;

    for (y = 0; y < H && (y + s->y < inlink->h); y++) {
        for (x = slice_start; x < slice_end && (x + s->x < inlink->w); x++) {
            FFDrawColor color = { { 0 } };
            FFDrawColor reverse = { { 0 } };
            int value[4] = { 0 };

            s->pick_color(&s->draw, &color, in, x + s->x, y + s->y, value);
            s->reverse_color(&s->draw, &color, &reverse);
            ff_fill_rectangle(&s->draw, &color, out->data, out->linesize,
                              xoff + x * C * 10, yoff + y * P * 12, C * 10, P * 12);

            for (p = 0; p < P; p++) {
                char text[256];

                snprintf(text, sizeof(text), format[C>>2], value[p]);
                draw_text(s, out, &reverse, xoff + x * C * 10 + 2, yoff + y * P * 12 + p * 10 + 2, text, 0);
            }
        }
    }

    return 0;
}

static int filter_color(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    DatascopeContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFilterLink *inlink = ctx->inputs[0];
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int xoff = td->xoff;
    const int yoff = td->yoff;
    const int P = FFMAX(s->nb_planes, s->nb_comps);
    const int C = s->chars;
    const int W = (outlink->w - xoff) / (C * 10);
    const int H = (outlink->h - yoff) / (P * 12);
    const char *format[2] = {"%02X\n", "%04X\n"};
    const int slice_start = (W * jobnr) / nb_jobs;
    const int slice_end = (W * (jobnr+1)) / nb_jobs;
    int x, y, p;

    for (y = 0; y < H && (y + s->y < inlink->h); y++) {
        for (x = slice_start; x < slice_end && (x + s->x < inlink->w); x++) {
            FFDrawColor color = { { 0 } };
            int value[4] = { 0 };

            s->pick_color(&s->draw, &color, in, x + s->x, y + s->y, value);

            for (p = 0; p < P; p++) {
                char text[256];

                snprintf(text, sizeof(text), format[C>>2], value[p]);
                draw_text(s, out, &color, xoff + x * C * 10 + 2, yoff + y * P * 12 + p * 10 + 2, text, 0);
            }
        }
    }

    return 0;
}

static int filter_mono(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    DatascopeContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFilterLink *inlink = ctx->inputs[0];
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int xoff = td->xoff;
    const int yoff = td->yoff;
    const int P = FFMAX(s->nb_planes, s->nb_comps);
    const int C = s->chars;
    const int W = (outlink->w - xoff) / (C * 10);
    const int H = (outlink->h - yoff) / (P * 12);
    const char *format[2] = {"%02X\n", "%04X\n"};
    const int slice_start = (W * jobnr) / nb_jobs;
    const int slice_end = (W * (jobnr+1)) / nb_jobs;
    int x, y, p;

    for (y = 0; y < H && (y + s->y < inlink->h); y++) {
        for (x = slice_start; x < slice_end && (x + s->x < inlink->w); x++) {
            FFDrawColor color = { { 0 } };
            int value[4] = { 0 };

            s->pick_color(&s->draw, &color, in, x + s->x, y + s->y, value);
            for (p = 0; p < P; p++) {
                char text[256];

                snprintf(text, sizeof(text), format[C>>2], value[p]);
                draw_text(s, out, &s->white, xoff + x * C * 10 + 2, yoff + y * P * 12 + p * 10 + 2, text, 0);
            }
        }
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx  = inlink->dst;
    DatascopeContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    ThreadData td = { 0 };
    int ymaxlen = 0;
    int xmaxlen = 0;
    AVFrame *out;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    out->pts = in->pts;

    ff_fill_rectangle(&s->draw, &s->black, out->data, out->linesize,
                      0, 0, outlink->w, outlink->h);

    if (s->axis) {
        const int P = FFMAX(s->nb_planes, s->nb_comps);
        const int C = s->chars;
        int Y = outlink->h / (P * 12);
        int X = outlink->w / (C * 10);
        char text[256] = { 0 };
        int x, y;

        snprintf(text, sizeof(text), "%d", s->y + Y);
        ymaxlen = strlen(text);
        ymaxlen *= 10;
        snprintf(text, sizeof(text), "%d", s->x + X);
        xmaxlen = strlen(text);
        xmaxlen *= 10;

        Y = (outlink->h - xmaxlen) / (P * 12);
        X = (outlink->w - ymaxlen) / (C * 10);

        for (y = 0; y < Y; y++) {
            snprintf(text, sizeof(text), "%d", s->y + y);

            ff_fill_rectangle(&s->draw, &s->gray, out->data, out->linesize,
                              0, xmaxlen + y * P * 12 + (P + 1) * P - 2, ymaxlen, 10);

            draw_text(s, out, &s->yellow, 2, xmaxlen + y * P * 12 + (P + 1) * P, text, 0);
        }

        for (x = 0; x < X; x++) {
            snprintf(text, sizeof(text), "%d", s->x + x);

            ff_fill_rectangle(&s->draw, &s->gray, out->data, out->linesize,
                              ymaxlen + x * C * 10 + 2 * C - 2, 0, 10, xmaxlen);

            draw_text(s, out, &s->yellow, ymaxlen + x * C * 10 + 2 * C, 2, text, 1);
        }
    }

    td.in = in; td.out = out, td.yoff = xmaxlen, td.xoff = ymaxlen;
    ctx->internal->execute(ctx, s->filter, &td, NULL, FFMIN(ff_filter_get_nb_threads(ctx), FFMAX(outlink->w / 20, 1)));

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int config_input(AVFilterLink *inlink)
{
    DatascopeContext *s = inlink->dst->priv;
    uint8_t alpha = s->opacity * 255;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);
    ff_draw_init(&s->draw, inlink->format, 0);
    ff_draw_color(&s->draw, &s->white,  (uint8_t[]){ 255, 255, 255, 255} );
    ff_draw_color(&s->draw, &s->black,  (uint8_t[]){ 0, 0, 0, alpha} );
    ff_draw_color(&s->draw, &s->yellow, (uint8_t[]){ 255, 255, 0, 255} );
    ff_draw_color(&s->draw, &s->gray,   (uint8_t[]){ 77, 77, 77, 255} );
    s->chars = (s->draw.desc->comp[0].depth + 7) / 8 * 2;
    s->nb_comps = s->draw.desc->nb_components;

    switch (s->mode) {
    case 0: s->filter = filter_mono;   break;
    case 1: s->filter = filter_color;  break;
    case 2: s->filter = filter_color2; break;
    }

    if (s->draw.desc->comp[0].depth <= 8) {
        s->pick_color = pick_color8;
        s->reverse_color = reverse_color8;
    } else {
        s->pick_color = pick_color16;
        s->reverse_color = reverse_color16;
    }

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    DatascopeContext *s = outlink->src->priv;

    outlink->h = s->oh;
    outlink->w = s->ow;
    outlink->sample_aspect_ratio = (AVRational){1,1};

    return 0;
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter ff_vf_datascope = {
    .name          = "datascope",
    .description   = NULL_IF_CONFIG_SMALL("Video data analysis."),
    .priv_size     = sizeof(DatascopeContext),
    .priv_class    = &datascope_class,
    .query_formats = query_formats,
    .inputs        = inputs,
    .outputs       = outputs,
    .flags         = AVFILTER_FLAG_SLICE_THREADS,
};
