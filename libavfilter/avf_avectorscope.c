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

/**
 * @file
 * audio to video multimedia vectorscope filter
 */

#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "avfilter.h"
#include "formats.h"
#include "audio.h"
#include "video.h"
#include "internal.h"

enum VectorScopeMode {
    LISSAJOUS,
    LISSAJOUS_XY,
    POLAR,
    MODE_NB,
};

enum VectorScopeDraw {
    DOT,
    LINE,
    DRAW_NB,
};

enum VectorScopeScale {
    LIN,
    SQRT,
    CBRT,
    LOG,
    SCALE_NB,
};

typedef struct AudioVectorScopeContext {
    const AVClass *class;
    AVFrame *outpicref;
    int w, h;
    int hw, hh;
    int mode;
    int draw;
    int scale;
    int contrast[4];
    int fade[4];
    double zoom;
    unsigned prev_x, prev_y;
    AVRational frame_rate;
} AudioVectorScopeContext;

#define OFFSET(x) offsetof(AudioVectorScopeContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption avectorscope_options[] = {
    { "mode", "set mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=LISSAJOUS}, 0, MODE_NB-1, FLAGS, "mode" },
    { "m",    "set mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=LISSAJOUS}, 0, MODE_NB-1, FLAGS, "mode" },
    { "lissajous",    "", 0, AV_OPT_TYPE_CONST, {.i64=LISSAJOUS},    0, 0, FLAGS, "mode" },
    { "lissajous_xy", "", 0, AV_OPT_TYPE_CONST, {.i64=LISSAJOUS_XY}, 0, 0, FLAGS, "mode" },
    { "polar",        "", 0, AV_OPT_TYPE_CONST, {.i64=POLAR},        0, 0, FLAGS, "mode" },
    { "rate", "set video rate", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str="25"}, 0, INT_MAX, FLAGS },
    { "r",    "set video rate", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str="25"}, 0, INT_MAX, FLAGS },
    { "size", "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str="400x400"}, 0, 0, FLAGS },
    { "s",    "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str="400x400"}, 0, 0, FLAGS },
    { "rc", "set red contrast",   OFFSET(contrast[0]), AV_OPT_TYPE_INT, {.i64=40},  0, 255, FLAGS },
    { "gc", "set green contrast", OFFSET(contrast[1]), AV_OPT_TYPE_INT, {.i64=160}, 0, 255, FLAGS },
    { "bc", "set blue contrast",  OFFSET(contrast[2]), AV_OPT_TYPE_INT, {.i64=80},  0, 255, FLAGS },
    { "ac", "set alpha contrast", OFFSET(contrast[3]), AV_OPT_TYPE_INT, {.i64=255}, 0, 255, FLAGS },
    { "rf", "set red fade",       OFFSET(fade[0]), AV_OPT_TYPE_INT, {.i64=15}, 0, 255, FLAGS },
    { "gf", "set green fade",     OFFSET(fade[1]), AV_OPT_TYPE_INT, {.i64=10}, 0, 255, FLAGS },
    { "bf", "set blue fade",      OFFSET(fade[2]), AV_OPT_TYPE_INT, {.i64=5},  0, 255, FLAGS },
    { "af", "set alpha fade",     OFFSET(fade[3]), AV_OPT_TYPE_INT, {.i64=5},  0, 255, FLAGS },
    { "zoom", "set zoom factor",  OFFSET(zoom), AV_OPT_TYPE_DOUBLE, {.dbl=1},  1, 10, FLAGS },
    { "draw", "set draw mode", OFFSET(draw), AV_OPT_TYPE_INT, {.i64=DOT}, 0, DRAW_NB-1, FLAGS, "draw" },
    { "dot",   "", 0, AV_OPT_TYPE_CONST, {.i64=DOT} , 0, 0, FLAGS, "draw" },
    { "line",  "", 0, AV_OPT_TYPE_CONST, {.i64=LINE}, 0, 0, FLAGS, "draw" },
    { "scale", "set amplitude scale mode", OFFSET(scale), AV_OPT_TYPE_INT, {.i64=LIN}, 0, SCALE_NB-1, FLAGS, "scale" },
    { "lin",   "linear",      0, AV_OPT_TYPE_CONST, {.i64=LIN},  0, 0, FLAGS, "scale" },
    { "sqrt",  "square root", 0, AV_OPT_TYPE_CONST, {.i64=SQRT}, 0, 0, FLAGS, "scale" },
    { "cbrt",  "cube root",   0, AV_OPT_TYPE_CONST, {.i64=CBRT}, 0, 0, FLAGS, "scale" },
    { "log",   "logarithmic", 0, AV_OPT_TYPE_CONST, {.i64=LOG},  0, 0, FLAGS, "scale" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(avectorscope);

static void draw_dot(AudioVectorScopeContext *s, unsigned x, unsigned y)
{
    const int linesize = s->outpicref->linesize[0];
    uint8_t *dst;

    if (s->zoom > 1) {
        if (y >= s->h || x >= s->w)
            return;
    } else {
        y = FFMIN(y, s->h - 1);
        x = FFMIN(x, s->w - 1);
    }

    dst = &s->outpicref->data[0][y * linesize + x * 4];
    dst[0] = FFMIN(dst[0] + s->contrast[0], 255);
    dst[1] = FFMIN(dst[1] + s->contrast[1], 255);
    dst[2] = FFMIN(dst[2] + s->contrast[2], 255);
    dst[3] = FFMIN(dst[3] + s->contrast[3], 255);
}

static void draw_line(AudioVectorScopeContext *s, int x0, int y0, int x1, int y1)
{
    int dx = FFABS(x1-x0), sx = x0 < x1 ? 1 : -1;
    int dy = FFABS(y1-y0), sy = y0 < y1 ? 1 : -1;
    int err = (dx>dy ? dx : -dy) / 2, e2;

    for (;;) {
        draw_dot(s, x0, y0);

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

static void fade(AudioVectorScopeContext *s)
{
    const int linesize = s->outpicref->linesize[0];
    int i, j;

    if (s->fade[0] || s->fade[1] || s->fade[2]) {
        uint8_t *d = s->outpicref->data[0];
        for (i = 0; i < s->h; i++) {
            for (j = 0; j < s->w*4; j+=4) {
                d[j+0] = FFMAX(d[j+0] - s->fade[0], 0);
                d[j+1] = FFMAX(d[j+1] - s->fade[1], 0);
                d[j+2] = FFMAX(d[j+2] - s->fade[2], 0);
                d[j+3] = FFMAX(d[j+3] - s->fade[3], 0);
            }
            d += linesize;
        }
    }
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layout = NULL;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_NONE };
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_RGBA, AV_PIX_FMT_NONE };
    int ret;

    formats = ff_make_format_list(sample_fmts);
    if ((ret = ff_formats_ref         (formats, &inlink->out_formats        )) < 0 ||
        (ret = ff_add_channel_layout  (&layout, AV_CH_LAYOUT_STEREO         )) < 0 ||
        (ret = ff_channel_layouts_ref (layout , &inlink->out_channel_layouts)) < 0)
        return ret;

    formats = ff_all_samplerates();
    if ((ret = ff_formats_ref(formats, &inlink->out_samplerates)) < 0)
        return ret;

    formats = ff_make_format_list(pix_fmts);
    if ((ret = ff_formats_ref(formats, &outlink->in_formats)) < 0)
        return ret;

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioVectorScopeContext *s = ctx->priv;
    int nb_samples;

    nb_samples = FFMAX(1024, ((double)inlink->sample_rate / av_q2d(s->frame_rate)) + 0.5);
    inlink->partial_buf_size =
    inlink->min_samples =
    inlink->max_samples = nb_samples;

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AudioVectorScopeContext *s = outlink->src->priv;

    outlink->w = s->w;
    outlink->h = s->h;
    outlink->sample_aspect_ratio = (AVRational){1,1};
    outlink->frame_rate = s->frame_rate;

    s->prev_x = s->hw = s->w / 2;
    s->prev_y = s->hh = s->mode == POLAR ? s->h - 1 : s->h / 2;

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *insamples)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AudioVectorScopeContext *s = ctx->priv;
    const int hw = s->hw;
    const int hh = s->hh;
    unsigned x, y;
    unsigned prev_x = s->prev_x, prev_y = s->prev_y;
    const double zoom = s->zoom;
    int i;

    if (!s->outpicref || s->outpicref->width  != outlink->w ||
                         s->outpicref->height != outlink->h) {
        av_frame_free(&s->outpicref);
        s->outpicref = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!s->outpicref) {
            av_frame_free(&insamples);
            return AVERROR(ENOMEM);
        }

        for (i = 0; i < outlink->h; i++)
            memset(s->outpicref->data[0] + i * s->outpicref->linesize[0], 0, outlink->w * 4);
    }
    s->outpicref->pts = insamples->pts;

    fade(s);

    for (i = 0; i < insamples->nb_samples; i++) {
        int16_t *samples = (int16_t *)insamples->data[0] + i * 2;
        float *samplesf = (float *)insamples->data[0] + i * 2;
        float src[2];

        switch (insamples->format) {
        case AV_SAMPLE_FMT_S16:
            src[0] = samples[0] / (float)INT16_MAX;
            src[1] = samples[1] / (float)INT16_MAX;
            break;
        case AV_SAMPLE_FMT_FLT:
            src[0] = samplesf[0];
            src[1] = samplesf[1];
            break;
        }

        switch (s->scale) {
        case SQRT:
            src[0] = FFSIGN(src[0]) * sqrtf(FFABS(src[0]));
            src[1] = FFSIGN(src[1]) * sqrtf(FFABS(src[1]));
            break;
        case CBRT:
            src[0] = FFSIGN(src[0]) * cbrtf(FFABS(src[0]));
            src[1] = FFSIGN(src[1]) * cbrtf(FFABS(src[1]));
            break;
        case LOG:
            src[0] = FFSIGN(src[0]) * logf(1 + FFABS(src[0])) / logf(2);
            src[1] = FFSIGN(src[1]) * logf(1 + FFABS(src[1])) / logf(2);
            break;
        }

        if (s->mode == LISSAJOUS) {
            x = ((src[1] - src[0]) * zoom / 2 + 1) * hw;
            y = (1.0 - (src[0] + src[1]) * zoom / 2) * hh;
        } else if (s->mode == LISSAJOUS_XY) {
            x = (src[1] * zoom + 1) * hw;
            y = (src[0] * zoom + 1) * hh;
        } else {
            float sx, sy, cx, cy;

            sx = src[1] * zoom;
            sy = src[0] * zoom;
            cx = sx * sqrtf(1 - 0.5 * sy * sy);
            cy = sy * sqrtf(1 - 0.5 * sx * sx);
            x = hw + hw * FFSIGN(cx + cy) * (cx - cy) * .7;
            y = s->h - s->h * fabsf(cx + cy) * .7;
        }

        if (s->draw == DOT) {
            draw_dot(s, x, y);
        } else {
            draw_line(s, x, y, prev_x, prev_y);
        }
        prev_x = x;
        prev_y = y;
    }

    s->prev_x = x, s->prev_y = y;
    av_frame_free(&insamples);

    return ff_filter_frame(outlink, av_frame_clone(s->outpicref));
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioVectorScopeContext *s = ctx->priv;

    av_frame_free(&s->outpicref);
}

static const AVFilterPad audiovectorscope_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad audiovectorscope_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter ff_avf_avectorscope = {
    .name          = "avectorscope",
    .description   = NULL_IF_CONFIG_SMALL("Convert input audio to vectorscope video output."),
    .uninit        = uninit,
    .query_formats = query_formats,
    .priv_size     = sizeof(AudioVectorScopeContext),
    .inputs        = audiovectorscope_inputs,
    .outputs       = audiovectorscope_outputs,
    .priv_class    = &avectorscope_class,
};
