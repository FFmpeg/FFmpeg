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

#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/xga_font_data.h"
#include "avfilter.h"
#include "drawutils.h"
#include "filters.h"
#include "formats.h"
#include "video.h"

typedef struct DatascopeContext {
    const AVClass *class;
    int ow, oh;
    int x, y;
    int mode;
    int dformat;
    int axis;
    int components;
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
#define FLAGSR AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption datascope_options[] = {
    { "size", "set output size", OFFSET(ow),   AV_OPT_TYPE_IMAGE_SIZE, {.str="hd720"}, 0, 0, FLAGS },
    { "s",    "set output size", OFFSET(ow),   AV_OPT_TYPE_IMAGE_SIZE, {.str="hd720"}, 0, 0, FLAGS },
    { "x",    "set x offset", OFFSET(x),    AV_OPT_TYPE_INT, {.i64=0}, 0, INT_MAX, FLAGSR },
    { "y",    "set y offset", OFFSET(y),    AV_OPT_TYPE_INT, {.i64=0}, 0, INT_MAX, FLAGSR },
    { "mode", "set scope mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=0}, 0, 2, FLAGSR, .unit = "mode" },
    {   "mono",   NULL, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGSR, .unit = "mode" },
    {   "color",  NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGSR, .unit = "mode" },
    {   "color2", NULL, 0, AV_OPT_TYPE_CONST, {.i64=2}, 0, 0, FLAGSR, .unit = "mode" },
    { "axis",    "draw column/row numbers", OFFSET(axis), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGSR },
    { "opacity", "set background opacity", OFFSET(opacity), AV_OPT_TYPE_FLOAT, {.dbl=0.75}, 0, 1, FLAGSR },
    { "format", "set display number format", OFFSET(dformat), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGSR, .unit = "format" },
    {   "hex",  NULL, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGSR, .unit = "format" },
    {   "dec",  NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGSR, .unit = "format" },
    { "components", "set components to display", OFFSET(components), AV_OPT_TYPE_INT, {.i64=15}, 1, 15, FLAGSR },
    { NULL }
};

AVFILTER_DEFINE_CLASS(datascope);

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    return ff_set_common_formats2(ctx, cfg_in, cfg_out,
                                  ff_draw_supported_pixel_formats(0));
}

static void draw_text(FFDrawContext *draw, AVFrame *frame, FFDrawColor *color,
                      int x0, int y0, const uint8_t *text, int vertical)
{
    int x = x0;

    for (; *text; text++) {
        if (*text == '\n') {
            x = x0;
            y0 += 8;
            continue;
        }
        ff_blend_mask(draw, color, frame->data, frame->linesize,
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
    int xoff, yoff, PP;
} ThreadData;

static int filter_color2(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    DatascopeContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFilterLink *inlink = ctx->inputs[0];
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int PP = td->PP;
    const int xoff = td->xoff;
    const int yoff = td->yoff;
    const int P = FFMAX(s->nb_planes, s->nb_comps);
    const int C = s->chars;
    const int D = ((s->chars - s->dformat) >> 2) + s->dformat * 2;
    const int W = (outlink->w - xoff) / (C * 10);
    const int H = (outlink->h - yoff) / (PP * 12);
    const char *format[4] = {"%02X\n", "%04X\n", "%03d\n", "%05d\n"};
    const int slice_start = (W * jobnr) / nb_jobs;
    const int slice_end = (W * (jobnr+1)) / nb_jobs;
    int x, y, p;

    for (y = 0; y < H && (y + s->y < inlink->h); y++) {
        for (x = slice_start; x < slice_end && (x + s->x < inlink->w); x++) {
            FFDrawColor color = { { 0 } };
            FFDrawColor reverse = { { 0 } };
            int value[4] = { 0 }, pp = 0;

            s->pick_color(&s->draw, &color, in, x + s->x, y + s->y, value);
            s->reverse_color(&s->draw, &color, &reverse);
            ff_fill_rectangle(&s->draw, &color, out->data, out->linesize,
                              xoff + x * C * 10, yoff + y * PP * 12, C * 10, PP * 12);

            for (p = 0; p < P; p++) {
                char text[256];

                if (!(s->components & (1 << p)))
                    continue;
                snprintf(text, sizeof(text), format[D], value[p]);
                draw_text(&s->draw, out, &reverse, xoff + x * C * 10 + 2, yoff + y * PP * 12 + pp * 10 + 2, text, 0);
                pp++;
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
    const int PP = td->PP;
    const int xoff = td->xoff;
    const int yoff = td->yoff;
    const int P = FFMAX(s->nb_planes, s->nb_comps);
    const int C = s->chars;
    const int D = ((s->chars - s->dformat) >> 2) + s->dformat * 2;
    const int W = (outlink->w - xoff) / (C * 10);
    const int H = (outlink->h - yoff) / (PP * 12);
    const char *format[4] = {"%02X\n", "%04X\n", "%03d\n", "%05d\n"};
    const int slice_start = (W * jobnr) / nb_jobs;
    const int slice_end = (W * (jobnr+1)) / nb_jobs;
    int x, y, p;

    for (y = 0; y < H && (y + s->y < inlink->h); y++) {
        for (x = slice_start; x < slice_end && (x + s->x < inlink->w); x++) {
            FFDrawColor color = { { 0 } };
            int value[4] = { 0 }, pp = 0;

            s->pick_color(&s->draw, &color, in, x + s->x, y + s->y, value);

            for (p = 0; p < P; p++) {
                char text[256];

                if (!(s->components & (1 << p)))
                    continue;
                snprintf(text, sizeof(text), format[D], value[p]);
                draw_text(&s->draw, out, &color, xoff + x * C * 10 + 2, yoff + y * PP * 12 + pp * 10 + 2, text, 0);
                pp++;
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
    const int PP = td->PP;
    const int xoff = td->xoff;
    const int yoff = td->yoff;
    const int P = FFMAX(s->nb_planes, s->nb_comps);
    const int C = s->chars;
    const int D = ((s->chars - s->dformat) >> 2) + s->dformat * 2;
    const int W = (outlink->w - xoff) / (C * 10);
    const int H = (outlink->h - yoff) / (PP * 12);
    const char *format[4] = {"%02X\n", "%04X\n", "%03d\n", "%05d\n"};
    const int slice_start = (W * jobnr) / nb_jobs;
    const int slice_end = (W * (jobnr+1)) / nb_jobs;
    int x, y, p;

    for (y = 0; y < H && (y + s->y < inlink->h); y++) {
        for (x = slice_start; x < slice_end && (x + s->x < inlink->w); x++) {
            FFDrawColor color = { { 0 } };
            int value[4] = { 0 }, pp = 0;

            s->pick_color(&s->draw, &color, in, x + s->x, y + s->y, value);
            for (p = 0; p < P; p++) {
                char text[256];

                if (!(s->components & (1 << p)))
                    continue;
                snprintf(text, sizeof(text), format[D], value[p]);
                draw_text(&s->draw, out, &s->white, xoff + x * C * 10 + 2, yoff + y * PP * 12 + pp * 10 + 2, text, 0);
                pp++;
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
    const int P = FFMAX(s->nb_planes, s->nb_comps);
    ThreadData td = { 0 };
    int ymaxlen = 0;
    int xmaxlen = 0;
    int PP = 0;
    AVFrame *out;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    ff_fill_rectangle(&s->draw, &s->black, out->data, out->linesize,
                      0, 0, outlink->w, outlink->h);

    for (int p = 0; p < P; p++) {
        if (s->components & (1 << p))
            PP++;
    }
    PP = FFMAX(PP, 1);

    if (s->axis) {
        const int C = s->chars;
        int Y = outlink->h / (PP * 12);
        int X = outlink->w / (C * 10);
        char text[256] = { 0 };
        int x, y;

        snprintf(text, sizeof(text), "%d", s->y + Y);
        ymaxlen = strlen(text);
        ymaxlen *= 10;
        snprintf(text, sizeof(text), "%d", s->x + X);
        xmaxlen = strlen(text);
        xmaxlen *= 10;

        Y = (outlink->h - xmaxlen) / (PP * 12);
        X = (outlink->w - ymaxlen) / (C * 10);

        for (y = 0; y < Y; y++) {
            snprintf(text, sizeof(text), "%d", s->y + y);

            ff_fill_rectangle(&s->draw, &s->gray, out->data, out->linesize,
                              0, xmaxlen + y * PP * 12 + (PP + 1) * PP - 2, ymaxlen, 10);

            draw_text(&s->draw, out, &s->yellow, 2, xmaxlen + y * PP * 12 + (PP + 1) * PP, text, 0);
        }

        for (x = 0; x < X; x++) {
            snprintf(text, sizeof(text), "%d", s->x + x);

            ff_fill_rectangle(&s->draw, &s->gray, out->data, out->linesize,
                              ymaxlen + x * C * 10 + 2 * C - 2, 0, 10, xmaxlen);

            draw_text(&s->draw, out, &s->yellow, ymaxlen + x * C * 10 + 2 * C, 2, text, 1);
        }
    }

    td.in = in; td.out = out, td.yoff = xmaxlen, td.xoff = ymaxlen, td.PP = PP;
    ff_filter_execute(ctx, s->filter, &td, NULL,
                      FFMIN(ff_filter_get_nb_threads(ctx), FFMAX(outlink->w / 20, 1)));

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    DatascopeContext *s = ctx->priv;

    uint8_t alpha = s->opacity * 255;
    int ret;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);
    ret = ff_draw_init2(&s->draw, inlink->format, inlink->colorspace, inlink->color_range, 0);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to initialize FFDrawContext\n");
        return ret;
    }
    ff_draw_color(&s->draw, &s->white,  (uint8_t[]){ 255, 255, 255, 255} );
    ff_draw_color(&s->draw, &s->black,  (uint8_t[]){ 0, 0, 0, alpha} );
    ff_draw_color(&s->draw, &s->yellow, (uint8_t[]){ 255, 255, 0, 255} );
    ff_draw_color(&s->draw, &s->gray,   (uint8_t[]){ 77, 77, 77, 255} );
    s->chars = (s->draw.desc->comp[0].depth + 7) / 8 * 2 + s->dformat;
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

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    return config_input(ctx->inputs[0]);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
};

const FFFilter ff_vf_datascope = {
    .p.name        = "datascope",
    .p.description = NULL_IF_CONFIG_SMALL("Video data analysis."),
    .p.priv_class  = &datascope_class,
    .p.flags       = AVFILTER_FLAG_SLICE_THREADS,
    .priv_size     = sizeof(DatascopeContext),
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_QUERY_FUNC2(query_formats),
    .process_command = process_command,
};

typedef struct PixscopeContext {
    const AVClass *class;

    float xpos, ypos;
    float wx, wy;
    int w, h;
    float o;

    int x, y;
    int ww, wh;

    int nb_planes;
    int nb_comps;
    int is_rgb;
    uint8_t rgba_map[4];
    FFDrawContext draw;
    FFDrawColor   dark;
    FFDrawColor   black;
    FFDrawColor   white;
    FFDrawColor   green;
    FFDrawColor   blue;
    FFDrawColor   red;
    FFDrawColor  *colors[4];

    uint16_t values[4][80][80];

    void (*pick_color)(FFDrawContext *draw, FFDrawColor *color, AVFrame *in, int x, int y, int *value);
} PixscopeContext;

#define POFFSET(x) offsetof(PixscopeContext, x)

static const AVOption pixscope_options[] = {
    { "x",  "set scope x offset",  POFFSET(xpos), AV_OPT_TYPE_FLOAT, {.dbl=0.5}, 0,  1, FLAGSR },
    { "y",  "set scope y offset",  POFFSET(ypos), AV_OPT_TYPE_FLOAT, {.dbl=0.5}, 0,  1, FLAGSR },
    { "w",  "set scope width",     POFFSET(w),    AV_OPT_TYPE_INT,   {.i64=7},   1, 80, FLAGSR },
    { "h",  "set scope height",    POFFSET(h),    AV_OPT_TYPE_INT,   {.i64=7},   1, 80, FLAGSR },
    { "o",  "set window opacity",  POFFSET(o),    AV_OPT_TYPE_FLOAT, {.dbl=0.5}, 0,  1, FLAGSR },
    { "wx", "set window x offset", POFFSET(wx),   AV_OPT_TYPE_FLOAT, {.dbl=-1}, -1,  1, FLAGSR },
    { "wy", "set window y offset", POFFSET(wy),   AV_OPT_TYPE_FLOAT, {.dbl=-1}, -1,  1, FLAGSR },
    { NULL }
};

AVFILTER_DEFINE_CLASS(pixscope);

static int pixscope_config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    PixscopeContext *s = ctx->priv;
    int ret;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);
    ret = ff_draw_init(&s->draw, inlink->format, 0);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to initialize FFDrawContext\n");
        return ret;
    }
    ff_draw_color(&s->draw, &s->dark,  (uint8_t[]){ 0, 0, 0, s->o * 255} );
    ff_draw_color(&s->draw, &s->black, (uint8_t[]){ 0, 0, 0, 255} );
    ff_draw_color(&s->draw, &s->white, (uint8_t[]){ 255, 255, 255, 255} );
    ff_draw_color(&s->draw, &s->green, (uint8_t[]){   0, 255,   0, 255} );
    ff_draw_color(&s->draw, &s->blue,  (uint8_t[]){   0,   0, 255, 255} );
    ff_draw_color(&s->draw, &s->red,   (uint8_t[]){ 255,   0,   0, 255} );
    s->nb_comps = s->draw.desc->nb_components;
    s->is_rgb   = s->draw.desc->flags & AV_PIX_FMT_FLAG_RGB;

    if (s->is_rgb) {
        s->colors[0] = &s->red;
        s->colors[1] = &s->green;
        s->colors[2] = &s->blue;
        s->colors[3] = &s->white;
        ff_fill_rgba_map(s->rgba_map, inlink->format);
    } else {
        s->colors[0] = &s->white;
        s->colors[1] = &s->blue;
        s->colors[2] = &s->red;
        s->colors[3] = &s->white;
        s->rgba_map[0] = 0;
        s->rgba_map[1] = 1;
        s->rgba_map[2] = 2;
        s->rgba_map[3] = 3;
    }

    if (s->draw.desc->comp[0].depth <= 8) {
        s->pick_color = pick_color8;
    } else {
        s->pick_color = pick_color16;
    }

    if (inlink->w < 640 || inlink->h < 480) {
        av_log(inlink->dst, AV_LOG_ERROR, "min supported resolution is 640x480\n");
        return AVERROR(EINVAL);
    }

    s->ww = 300;
    s->wh = 300 * 1.6;
    s->x = s->xpos * (inlink->w - 1);
    s->y = s->ypos * (inlink->h - 1);
    if (s->x + s->w >= inlink->w || s->y + s->h >= inlink->h) {
        av_log(inlink->dst, AV_LOG_WARNING, "scope position is out of range, clipping\n");
        s->x = FFMIN(s->x, inlink->w - s->w);
        s->y = FFMIN(s->y, inlink->h - s->h);
    }

    return 0;
}

#define SQR(x) ((x)*(x))

static int pixscope_filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx  = inlink->dst;
    PixscopeContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out = ff_get_video_buffer(outlink, in->width, in->height);
    int max[4] = { 0 }, min[4] = { INT_MAX, INT_MAX, INT_MAX, INT_MAX };
    float average[4] = { 0 };
    double std[4] = { 0 }, rms[4] = { 0 };
    const char rgba[4] = { 'R', 'G', 'B', 'A' };
    const char yuva[4] = { 'Y', 'U', 'V', 'A' };
    int x, y, X, Y, i, w, h;
    char text[128];

    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);
    av_frame_copy(out, in);

    w = s->ww / s->w;
    h = s->ww / s->h;

    if (s->wx >= 0) {
        X = (in->width - s->ww) * s->wx;
    } else {
        X = (in->width - s->ww) * -s->wx;
    }
    if (s->wy >= 0) {
        Y = (in->height - s->wh) * s->wy;
    } else {
        Y = (in->height - s->wh) * -s->wy;
    }

    if (s->wx < 0) {
        if (s->x + s->w >= X && (s->x + s->w <= X + s->ww) &&
            s->y + s->h >= Y && (s->y + s->h <= Y + s->wh)) {
            X = (in->width - s->ww) * (1 + s->wx);
        }
    }

    if (s->wy < 0) {
        if (s->x + s->w >= X && (s->x + s->w <= X + s->ww) &&
            s->y + s->h >= Y && (s->y + s->h <= Y + s->wh)) {
            Y = (in->height - s->wh) * (1 + s->wy);
        }
    }

    ff_blend_rectangle(&s->draw, &s->dark, out->data, out->linesize,
                       out->width, out->height,
                       X,
                       Y,
                       s->ww,
                       s->wh);

    for (y = 0; y < s->h; y++) {
        for (x = 0; x < s->w; x++) {
            FFDrawColor color = { { 0 } };
            int value[4] = { 0 };

            s->pick_color(&s->draw, &color, in, x + s->x, y + s->y, value);
            ff_fill_rectangle(&s->draw, &color, out->data, out->linesize,
                              x * w + (s->ww - 4 - (s->w * w)) / 2 + X, y * h + 2 + Y, w, h);
            for (i = 0; i < 4; i++) {
                s->values[i][x][y] = value[i];
                rms[i]     += (double)value[i] * (double)value[i];
                average[i] += value[i];
                min[i]      = FFMIN(min[i], value[i]);
                max[i]      = FFMAX(max[i], value[i]);
            }
        }
    }

    ff_blend_rectangle(&s->draw, &s->black, out->data, out->linesize,
                       out->width, out->height,
                       s->x - 2, s->y - 2, s->w + 4, 1);

    ff_blend_rectangle(&s->draw, &s->white, out->data, out->linesize,
                       out->width, out->height,
                       s->x - 1, s->y - 1, s->w + 2, 1);

    ff_blend_rectangle(&s->draw, &s->white, out->data, out->linesize,
                       out->width, out->height,
                       s->x - 1, s->y - 1, 1, s->h + 2);

    ff_blend_rectangle(&s->draw, &s->black, out->data, out->linesize,
                       out->width, out->height,
                       s->x - 2, s->y - 2, 1, s->h + 4);

    ff_blend_rectangle(&s->draw, &s->white, out->data, out->linesize,
                       out->width, out->height,
                       s->x - 1, s->y + 1 + s->h, s->w + 3, 1);

    ff_blend_rectangle(&s->draw, &s->black, out->data, out->linesize,
                       out->width, out->height,
                       s->x - 2, s->y + 2 + s->h, s->w + 4, 1);

    ff_blend_rectangle(&s->draw, &s->white, out->data, out->linesize,
                       out->width, out->height,
                       s->x + 1 + s->w, s->y - 1, 1, s->h + 2);

    ff_blend_rectangle(&s->draw, &s->black, out->data, out->linesize,
                       out->width, out->height,
                       s->x + 2 + s->w, s->y - 2, 1, s->h + 5);

    for (i = 0; i < 4; i++) {
        rms[i] /= s->w * s->h;
        rms[i]  = sqrt(rms[i]);
        average[i] /= s->w * s->h;
    }

    for (y = 0; y < s->h; y++) {
        for (x = 0; x < s->w; x++) {
            for (i = 0; i < 4; i++)
                std[i] += SQR(s->values[i][x][y] - average[i]);
        }
    }

    for (i = 0; i < 4; i++) {
        std[i] /= s->w * s->h;
        std[i]  = sqrt(std[i]);
    }

    snprintf(text, sizeof(text), "CH   AVG    MIN    MAX    RMS\n");
    draw_text(&s->draw, out, &s->white,        X + 28, Y + s->ww +  5,           text, 0);
    for (i = 0; i < s->nb_comps; i++) {
        int c = s->rgba_map[i];

        snprintf(text, sizeof(text), "%c  %07.1f %05d %05d %07.1f\n", s->is_rgb ? rgba[i] : yuva[i], average[c], min[c], max[c], rms[c]);
        draw_text(&s->draw, out, s->colors[i], X + 28, Y + s->ww + 15 * (i + 1), text, 0);
    }
    snprintf(text, sizeof(text), "CH   STD\n");
    draw_text(&s->draw, out, &s->white,        X + 28, Y + s->ww + 15 * (0 + 5), text, 0);
    for (i = 0; i < s->nb_comps; i++) {
        int c = s->rgba_map[i];

        snprintf(text, sizeof(text), "%c  %07.2f\n", s->is_rgb ? rgba[i] : yuva[i], std[c]);
        draw_text(&s->draw, out, s->colors[i], X + 28, Y + s->ww + 15 * (i + 6), text, 0);
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int pixscope_process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                                    char *res, int res_len, int flags)
{
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    return pixscope_config_input(ctx->inputs[0]);
}

static const AVFilterPad pixscope_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .filter_frame   = pixscope_filter_frame,
        .config_props   = pixscope_config_input,
    },
};

const FFFilter ff_vf_pixscope = {
    .p.name        = "pixscope",
    .p.description = NULL_IF_CONFIG_SMALL("Pixel data analysis."),
    .p.priv_class  = &pixscope_class,
    .p.flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
    .priv_size     = sizeof(PixscopeContext),
    FILTER_INPUTS(pixscope_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_QUERY_FUNC2(query_formats),
    .process_command = pixscope_process_command,
};

typedef struct PixelValues {
    uint16_t p[4];
} PixelValues;

typedef struct OscilloscopeContext {
    const AVClass *class;

    float xpos, ypos;
    float tx, ty;
    float size;
    float tilt;
    float theight, twidth;
    float o;
    int components;
    int grid;
    int statistics;
    int scope;

    int x1, y1, x2, y2;
    int ox, oy;
    int height, width;

    int max;
    int nb_planes;
    int nb_comps;
    int is_rgb;
    uint8_t rgba_map[4];
    FFDrawContext draw;
    FFDrawColor   dark;
    FFDrawColor   black;
    FFDrawColor   white;
    FFDrawColor   green;
    FFDrawColor   blue;
    FFDrawColor   red;
    FFDrawColor   cyan;
    FFDrawColor   magenta;
    FFDrawColor   gray;
    FFDrawColor  *colors[4];

    int nb_values;
    PixelValues  *values;

    void (*pick_color)(FFDrawContext *draw, FFDrawColor *color, AVFrame *in, int x, int y, int *value);
    void (*draw_trace)(struct OscilloscopeContext *s, AVFrame *frame);
} OscilloscopeContext;

#define OOFFSET(x) offsetof(OscilloscopeContext, x)

static const AVOption oscilloscope_options[] = {
    { "x",  "set scope x position",    OOFFSET(xpos),       AV_OPT_TYPE_FLOAT, {.dbl=0.5}, 0, 1,  FLAGSR },
    { "y",  "set scope y position",    OOFFSET(ypos),       AV_OPT_TYPE_FLOAT, {.dbl=0.5}, 0, 1,  FLAGSR },
    { "s",  "set scope size",          OOFFSET(size),       AV_OPT_TYPE_FLOAT, {.dbl=0.8}, 0, 1,  FLAGSR },
    { "t",  "set scope tilt",          OOFFSET(tilt),       AV_OPT_TYPE_FLOAT, {.dbl=0.5}, 0, 1,  FLAGSR },
    { "o",  "set trace opacity",       OOFFSET(o),          AV_OPT_TYPE_FLOAT, {.dbl=0.8}, 0, 1,  FLAGSR },
    { "tx", "set trace x position",    OOFFSET(tx),         AV_OPT_TYPE_FLOAT, {.dbl=0.5}, 0, 1,  FLAGSR },
    { "ty", "set trace y position",    OOFFSET(ty),         AV_OPT_TYPE_FLOAT, {.dbl=0.9}, 0, 1,  FLAGSR },
    { "tw", "set trace width",         OOFFSET(twidth),     AV_OPT_TYPE_FLOAT, {.dbl=0.8},.1, 1,  FLAGSR },
    { "th", "set trace height",        OOFFSET(theight),    AV_OPT_TYPE_FLOAT, {.dbl=0.3},.1, 1,  FLAGSR },
    { "c",  "set components to trace", OOFFSET(components), AV_OPT_TYPE_INT,   {.i64=7},   0, 15, FLAGSR },
    { "g",  "draw trace grid",         OOFFSET(grid),       AV_OPT_TYPE_BOOL,  {.i64=1},   0, 1,  FLAGSR },
    { "st", "draw statistics",         OOFFSET(statistics), AV_OPT_TYPE_BOOL,  {.i64=1},   0, 1,  FLAGSR },
    { "sc", "draw scope",              OOFFSET(scope),      AV_OPT_TYPE_BOOL,  {.i64=1},   0, 1,  FLAGSR },
    { NULL }
};

AVFILTER_DEFINE_CLASS(oscilloscope);

static void oscilloscope_uninit(AVFilterContext *ctx)
{
    OscilloscopeContext *s = ctx->priv;

    av_freep(&s->values);
}

static void draw_line(FFDrawContext *draw, int x0, int y0, int x1, int y1,
                      AVFrame *out, FFDrawColor *color)
{
    int dx = FFABS(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = FFABS(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = (dx > dy ? dx : -dy) / 2, e2;
    int p, i;

    for (;;) {
        if (x0 >= 0 && y0 >= 0 && x0 < out->width && y0 < out->height) {
            for (p = 0; p < draw->nb_planes; p++) {
                if (draw->desc->comp[p].depth == 8) {
                    if (draw->nb_planes == 1) {
                        for (i = 0; i < draw->desc->nb_components; i++) {
                            out->data[0][y0 * out->linesize[0] + x0 * draw->pixelstep[0] + i] = color->comp[0].u8[i];
                        }
                    } else {
                        out->data[p][out->linesize[p] * (y0 >> draw->vsub[p]) + (x0 >> draw->hsub[p])] = color->comp[p].u8[0];
                    }
                } else {
                    if (draw->nb_planes == 1) {
                        for (i = 0; i < draw->desc->nb_components; i++) {
                            AV_WN16(out->data[0] + y0 * out->linesize[0] + (x0 * draw->pixelstep[0] + i), color->comp[0].u16[i]);
                        }
                    } else {
                        AV_WN16(out->data[p] + out->linesize[p] * (y0 >> draw->vsub[p]) + (x0 >> draw->hsub[p]) * 2, color->comp[p].u16[0]);
                    }
                }
            }
        }

        if (x0 == x1 && y0 == y1)
            break;

        e2 = err;

        if (e2 >-dx) {
            err -= dy;
            x0 += sx;
        }

        if (e2 < dy) {
            err += dx;
            y0 += sy;
        }
    }
}

static void draw_trace8(OscilloscopeContext *s, AVFrame *frame)
{
    int i, c;

    for (i = 1; i < s->nb_values; i++) {
        for (c = 0; c < s->nb_comps; c++) {
            if ((1 << c) & s->components) {
                int x = i * s->width / s->nb_values;
                int px = (i - 1) * s->width / s->nb_values;
                int py = s->height - s->values[i-1].p[s->rgba_map[c]] * s->height / 256;
                int y = s->height - s->values[i].p[s->rgba_map[c]] * s->height / 256;

                draw_line(&s->draw, s->ox + x, s->oy + y, s->ox + px, s->oy + py, frame, s->colors[c]);
            }
        }
    }
}


static void draw_trace16(OscilloscopeContext *s, AVFrame *frame)
{
    int i, c;

    for (i = 1; i < s->nb_values; i++) {
        for (c = 0; c < s->nb_comps; c++) {
            if ((1 << c) & s->components) {
                int x = i * s->width / s->nb_values;
                int px = (i - 1) * s->width / s->nb_values;
                int py = s->height - s->values[i-1].p[s->rgba_map[c]] * s->height / s->max;
                int y = s->height - s->values[i].p[s->rgba_map[c]] * s->height / s->max;

                draw_line(&s->draw, s->ox + x, s->oy + y, s->ox + px, s->oy + py, frame, s->colors[c]);
            }
        }
    }
}

static void update_oscilloscope(AVFilterContext *ctx)
{
    OscilloscopeContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    int cx, cy, size;
    double tilt;

    ff_draw_color(&s->draw, &s->dark,    (uint8_t[]){   0,   0,   0, s->o * 255} );
    s->height = s->theight * inlink->h;
    s->width = s->twidth * inlink->w;
    size = hypot(inlink->w, inlink->h);
    size *= s->size;
    tilt  = (s->tilt - 0.5) * M_PI;
    cx = s->xpos * (inlink->w - 1);
    cy = s->ypos * (inlink->h - 1);
    s->x1 = cx - size / 2.0 * cos(tilt);
    s->x2 = cx + size / 2.0 * cos(tilt);
    s->y1 = cy - size / 2.0 * sin(tilt);
    s->y2 = cy + size / 2.0 * sin(tilt);
    s->ox = (inlink->w - s->width) * s->tx;
    s->oy = (inlink->h - s->height) * s->ty;
}

static int oscilloscope_config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    OscilloscopeContext *s = ctx->priv;
    int size;
    int ret;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);
    ret = ff_draw_init(&s->draw, inlink->format, 0);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to initialize FFDrawContext\n");
        return ret;
    }
    ff_draw_color(&s->draw, &s->black,   (uint8_t[]){   0,   0,   0, 255} );
    ff_draw_color(&s->draw, &s->white,   (uint8_t[]){ 255, 255, 255, 255} );
    ff_draw_color(&s->draw, &s->green,   (uint8_t[]){   0, 255,   0, 255} );
    ff_draw_color(&s->draw, &s->blue,    (uint8_t[]){   0,   0, 255, 255} );
    ff_draw_color(&s->draw, &s->red,     (uint8_t[]){ 255,   0,   0, 255} );
    ff_draw_color(&s->draw, &s->cyan,    (uint8_t[]){   0, 255, 255, 255} );
    ff_draw_color(&s->draw, &s->magenta, (uint8_t[]){ 255,   0, 255, 255} );
    ff_draw_color(&s->draw, &s->gray,    (uint8_t[]){ 128, 128, 128, 255} );
    s->nb_comps = s->draw.desc->nb_components;
    s->is_rgb   = s->draw.desc->flags & AV_PIX_FMT_FLAG_RGB;

    if (s->is_rgb) {
        s->colors[0] = &s->red;
        s->colors[1] = &s->green;
        s->colors[2] = &s->blue;
        s->colors[3] = &s->white;
        ff_fill_rgba_map(s->rgba_map, inlink->format);
    } else {
        s->colors[0] = &s->white;
        s->colors[1] = &s->cyan;
        s->colors[2] = &s->magenta;
        s->colors[3] = &s->white;
        s->rgba_map[0] = 0;
        s->rgba_map[1] = 1;
        s->rgba_map[2] = 2;
        s->rgba_map[3] = 3;
    }

    if (s->draw.desc->comp[0].depth <= 8) {
        s->pick_color = pick_color8;
        s->draw_trace = draw_trace8;
    } else {
        s->pick_color = pick_color16;
        s->draw_trace = draw_trace16;
    }

    s->max = (1 << s->draw.desc->comp[0].depth);
    size = hypot(inlink->w, inlink->h);

    s->values = av_calloc(size, sizeof(*s->values));
    if (!s->values)
        return AVERROR(ENOMEM);

    update_oscilloscope(inlink->dst);

    return 0;
}

static void draw_scope(OscilloscopeContext *s, int x0, int y0, int x1, int y1,
                       AVFrame *out, PixelValues *p, int state)
{
    int dx = FFABS(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = FFABS(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = (dx > dy ? dx : -dy) / 2, e2;

    for (;;) {
        if (x0 >= 0 && y0 >= 0 && x0 < out->width && y0 < out->height) {
            FFDrawColor color = { { 0 } };
            int value[4] = { 0 };

            s->pick_color(&s->draw, &color, out, x0, y0, value);
            s->values[s->nb_values].p[0] = value[0];
            s->values[s->nb_values].p[1] = value[1];
            s->values[s->nb_values].p[2] = value[2];
            s->values[s->nb_values].p[3] = value[3];
            s->nb_values++;

            if (s->scope) {
                if (s->draw.desc->comp[0].depth == 8) {
                    if (s->draw.nb_planes == 1) {
                        int i;

                        for (i = 0; i < s->nb_comps; i++)
                            out->data[0][out->linesize[0] * y0 + x0 * s->draw.pixelstep[0] + i] = 255 * ((s->nb_values + state) & 1);
                    } else {
                        out->data[0][out->linesize[0] * y0 + x0] = 255 * ((s->nb_values + state) & 1);
                    }
                } else {
                    if (s->draw.nb_planes == 1) {
                        int i;

                        for (i = 0; i < s->nb_comps; i++)
                            AV_WN16(out->data[0] + out->linesize[0] * y0 + x0 * s->draw.pixelstep[0] + i, (s->max - 1) * ((s->nb_values + state) & 1));
                    } else {
                        AV_WN16(out->data[0] + out->linesize[0] * y0 + 2 * x0, (s->max - 1) * ((s->nb_values + state) & 1));
                    }
                }
            }
        }

        if (x0 == x1 && y0 == y1)
            break;

        e2 = err;

        if (e2 >-dx) {
            err -= dy;
            x0 += sx;
        }

        if (e2 < dy) {
            err += dx;
            y0 += sy;
        }
    }
}

static int oscilloscope_filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    FilterLink *inl = ff_filter_link(inlink);
    AVFilterContext *ctx  = inlink->dst;
    OscilloscopeContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    float average[4] = { 0 };
    int max[4] = { 0 };
    int min[4] = { INT_MAX, INT_MAX, INT_MAX, INT_MAX };
    int i, c;

    s->nb_values = 0;
    draw_scope(s, s->x1, s->y1, s->x2, s->y2, frame, s->values, inl->frame_count_in & 1);
    ff_blend_rectangle(&s->draw, &s->dark, frame->data, frame->linesize,
                       frame->width, frame->height,
                       s->ox, s->oy, s->width, s->height + 20 * s->statistics);

    if (s->grid && outlink->h >= 10) {
        ff_fill_rectangle(&s->draw, &s->gray, frame->data, frame->linesize,
                          s->ox, s->oy, s->width - 1, 1);

        for (i = 1; i < 5; i++) {
            ff_fill_rectangle(&s->draw, &s->gray, frame->data, frame->linesize,
                              s->ox, s->oy + i * (s->height - 1) / 4, s->width, 1);
        }

        for (i = 0; i < 10; i++) {
            ff_fill_rectangle(&s->draw, &s->gray, frame->data, frame->linesize,
                              s->ox + i * (s->width - 1) / 10, s->oy, 1, s->height);
        }

        ff_fill_rectangle(&s->draw, &s->gray, frame->data, frame->linesize,
                          s->ox + s->width - 1, s->oy, 1, s->height);
    }

    s->draw_trace(s, frame);

    for (i = 0; i < s->nb_values; i++) {
        for (c = 0; c < s->nb_comps; c++) {
            if ((1 << c) & s->components) {
                max[c] = FFMAX(max[c], s->values[i].p[s->rgba_map[c]]);
                min[c] = FFMIN(min[c], s->values[i].p[s->rgba_map[c]]);
                average[c] += s->values[i].p[s->rgba_map[c]];
            }
        }
    }
    for (c = 0; c < s->nb_comps; c++) {
        average[c] /= s->nb_values;
    }

    if (s->statistics && s->height > 10 && s->width > 280 * av_popcount(s->components)) {
        for (c = 0, i = 0; c < s->nb_comps; c++) {
            if ((1 << c) & s->components) {
                const char rgba[4] = { 'R', 'G', 'B', 'A' };
                const char yuva[4] = { 'Y', 'U', 'V', 'A' };
                char text[128];

                snprintf(text, sizeof(text), "%c avg:%.1f min:%d max:%d\n", s->is_rgb ? rgba[c] : yuva[c], average[c], min[c], max[c]);
                draw_text(&s->draw, frame, &s->white, s->ox +  2 + 280 * i++, s->oy + s->height + 4, text, 0);
            }
        }
    }

    return ff_filter_frame(outlink, frame);
}

static int oscilloscope_process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                                        char *res, int res_len, int flags)
{
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    update_oscilloscope(ctx);

    return 0;
}

static const AVFilterPad oscilloscope_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .flags          = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        .filter_frame   = oscilloscope_filter_frame,
        .config_props   = oscilloscope_config_input,
    },
};

const FFFilter ff_vf_oscilloscope = {
    .p.name        = "oscilloscope",
    .p.description = NULL_IF_CONFIG_SMALL("2D Video Oscilloscope."),
    .p.priv_class  = &oscilloscope_class,
    .p.flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
    .priv_size     = sizeof(OscilloscopeContext),
    .uninit        = oscilloscope_uninit,
    FILTER_INPUTS(oscilloscope_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_QUERY_FUNC2(query_formats),
    .process_command = oscilloscope_process_command,
};
