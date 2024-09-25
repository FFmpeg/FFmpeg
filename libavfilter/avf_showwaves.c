/*
 * Copyright (c) 2012 Stefano Sabatini
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
 * audio to video multimedia filter
 */

#include "config_components.h"

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "audio.h"
#include "video.h"

enum ShowWavesMode {
    MODE_POINT,
    MODE_LINE,
    MODE_P2P,
    MODE_CENTERED_LINE,
    MODE_NB,
};

enum ShowWavesScale {
    SCALE_LIN,
    SCALE_LOG,
    SCALE_SQRT,
    SCALE_CBRT,
    SCALE_NB,
};

enum ShowWavesDrawMode {
    DRAW_SCALE,
    DRAW_FULL,
    DRAW_NB,
};

enum ShowWavesFilterMode {
    FILTER_AVERAGE,
    FILTER_PEAK,
    FILTER_NB,
};

struct frame_node {
    AVFrame *frame;
    struct frame_node *next;
};

typedef struct ShowWavesContext {
    const AVClass *class;
    int w, h;
    AVRational rate;
    char *colors;
    int buf_idx;
    int16_t *buf_idy;    /* y coordinate of previous sample for each channel */
    int16_t *history;
    int history_nb_samples;
    int history_index;
    AVFrame *outpicref;
    AVRational n, q, c;
    int pixstep;
    int mode;                   ///< ShowWavesMode
    int scale;                  ///< ShowWavesScale
    int draw_mode;              ///< ShowWavesDrawMode
    int split_channels;
    int filter_mode;
    uint8_t *fg;

    int (*get_h)(int16_t sample, int height);
    void (*draw_sample)(uint8_t *buf, int height, int linesize,
                        int16_t *prev_y, const uint8_t color[4], int h);

    /* single picture */
    int single_pic;
    struct frame_node *audio_frames;
    struct frame_node *last_frame;
    int64_t total_samples;
    int64_t *sum; /* abs sum of the samples per channel */
} ShowWavesContext;

#define OFFSET(x) offsetof(ShowWavesContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption showwaves_options[] = {
    { "size", "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "600x240"}, 0, 0, FLAGS },
    { "s",    "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "600x240"}, 0, 0, FLAGS },
    { "mode", "select display mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=MODE_POINT}, 0, MODE_NB-1,        .flags=FLAGS, .unit="mode"},
        { "point", "draw a point for each sample",         0, AV_OPT_TYPE_CONST, {.i64=MODE_POINT},         .flags=FLAGS, .unit="mode"},
        { "line",  "draw a line for each sample",          0, AV_OPT_TYPE_CONST, {.i64=MODE_LINE},          .flags=FLAGS, .unit="mode"},
        { "p2p",   "draw a line between samples",          0, AV_OPT_TYPE_CONST, {.i64=MODE_P2P},           .flags=FLAGS, .unit="mode"},
        { "cline", "draw a centered line for each sample", 0, AV_OPT_TYPE_CONST, {.i64=MODE_CENTERED_LINE}, .flags=FLAGS, .unit="mode"},
    { "n",    "set how many samples to show in the same point", OFFSET(n), AV_OPT_TYPE_RATIONAL, {.i64 = 0}, 0, INT_MAX, FLAGS },
    { "rate", "set video rate", OFFSET(rate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, INT_MAX, FLAGS },
    { "r",    "set video rate", OFFSET(rate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, INT_MAX, FLAGS },
    { "split_channels", "draw channels separately", OFFSET(split_channels), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, FLAGS },
    { "colors", "set channels colors", OFFSET(colors), AV_OPT_TYPE_STRING, {.str = "red|green|blue|yellow|orange|lime|pink|magenta|brown" }, 0, 0, FLAGS },
    { "scale", "set amplitude scale", OFFSET(scale), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, SCALE_NB-1, FLAGS, .unit="scale" },
        { "lin", "linear",         0, AV_OPT_TYPE_CONST, {.i64=SCALE_LIN}, .flags=FLAGS, .unit="scale"},
        { "log", "logarithmic",    0, AV_OPT_TYPE_CONST, {.i64=SCALE_LOG}, .flags=FLAGS, .unit="scale"},
        { "sqrt", "square root",   0, AV_OPT_TYPE_CONST, {.i64=SCALE_SQRT}, .flags=FLAGS, .unit="scale"},
        { "cbrt", "cubic root",    0, AV_OPT_TYPE_CONST, {.i64=SCALE_CBRT}, .flags=FLAGS, .unit="scale"},
    { "draw", "set draw mode", OFFSET(draw_mode), AV_OPT_TYPE_INT, {.i64 = DRAW_SCALE}, 0, DRAW_NB-1, FLAGS, .unit="draw" },
        { "scale", "scale pixel values for each drawn sample", 0, AV_OPT_TYPE_CONST, {.i64=DRAW_SCALE}, .flags=FLAGS, .unit="draw"},
        { "full",  "draw every pixel for sample directly",     0, AV_OPT_TYPE_CONST, {.i64=DRAW_FULL},  .flags=FLAGS, .unit="draw"},
    { NULL }
};

AVFILTER_DEFINE_CLASS(showwaves);

static av_cold void uninit(AVFilterContext *ctx)
{
    ShowWavesContext *showwaves = ctx->priv;

    av_frame_free(&showwaves->outpicref);
    av_freep(&showwaves->buf_idy);
    av_freep(&showwaves->history);
    av_freep(&showwaves->fg);

    if (showwaves->single_pic) {
        struct frame_node *node = showwaves->audio_frames;
        while (node) {
            struct frame_node *tmp = node;

            node = node->next;
            av_frame_free(&tmp->frame);
            av_freep(&tmp);
        }
        av_freep(&showwaves->sum);
        showwaves->last_frame = NULL;
    }
}

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    AVFilterFormats *formats = NULL;
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE };
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_RGBA, AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE };
    int ret;

    /* set input audio formats */
    formats = ff_make_format_list(sample_fmts);
    if ((ret = ff_formats_ref(formats, &cfg_in[0]->formats)) < 0)
        return ret;

    /* set output video format */
    formats = ff_make_format_list(pix_fmts);
    if ((ret = ff_formats_ref(formats, &cfg_out[0]->formats)) < 0)
        return ret;

    return 0;
}

static int get_lin_h(int16_t sample, int height)
{
    return height/2 - av_rescale(sample, height/2, INT16_MAX);
}

static int get_lin_h2(int16_t sample, int height)
{
    return av_rescale(FFABS(sample), height, INT16_MAX);
}

static int get_log_h(int16_t sample, int height)
{
    return height/2 - FFSIGN(sample) * (log10(1 + FFABS(sample)) * (height/2) / log10(1 + INT16_MAX));
}

static int get_log_h2(int16_t sample, int height)
{
    return log10(1 + FFABS(sample)) * height / log10(1 + INT16_MAX);
}

static int get_sqrt_h(int16_t sample, int height)
{
    return height/2 - FFSIGN(sample) * (sqrt(FFABS(sample)) * (height/2) / sqrt(INT16_MAX));
}

static int get_sqrt_h2(int16_t sample, int height)
{
    return sqrt(FFABS(sample)) * height / sqrt(INT16_MAX);
}

static int get_cbrt_h(int16_t sample, int height)
{
    return height/2 - FFSIGN(sample) * (cbrt(FFABS(sample)) * (height/2) / cbrt(INT16_MAX));
}

static int get_cbrt_h2(int16_t sample, int height)
{
    return cbrt(FFABS(sample)) * height / cbrt(INT16_MAX);
}

static void draw_sample_point_rgba_scale(uint8_t *buf, int height, int linesize,
                                         int16_t *prev_y,
                                         const uint8_t color[4], int h)
{
    if (h >= 0 && h < height) {
        buf[h * linesize + 0] += color[0];
        buf[h * linesize + 1] += color[1];
        buf[h * linesize + 2] += color[2];
        buf[h * linesize + 3] += color[3];
    }
}

static void draw_sample_point_rgba_full(uint8_t *buf, int height, int linesize,
                                   int16_t *prev_y,
                                   const uint8_t color[4], int h)
{
    uint32_t clr = AV_RN32(color);
    if (h >= 0 && h < height)
        AV_WN32(buf + h * linesize, clr);
}

static void draw_sample_line_rgba_scale(uint8_t *buf, int height, int linesize,
                                        int16_t *prev_y,
                                        const uint8_t color[4], int h)
{
    int start   = height/2;
    int end     = av_clip(h, 0, height-1);
    uint8_t *bufk;
    if (start > end)
        FFSWAP(int16_t, start, end);
    bufk = buf + start * linesize;
    for (int k = start; k < end; k++, bufk += linesize) {
        bufk[0] += color[0];
        bufk[1] += color[1];
        bufk[2] += color[2];
        bufk[3] += color[3];
    }
}

static void draw_sample_line_rgba_full(uint8_t *buf, int height, int linesize,
                                       int16_t *prev_y,
                                       const uint8_t color[4], int h)
{
    int start   = height/2;
    int end     = av_clip(h, 0, height-1);
    uint32_t clr = AV_RN32(color);
    uint8_t *bufk;
    if (start > end)
        FFSWAP(int16_t, start, end);
    bufk = buf + start * linesize;
    for (int k = start; k < end; k++, bufk += linesize)
        AV_WN32(bufk, clr);
}

static void draw_sample_p2p_rgba_scale(uint8_t *buf, int height, int linesize,
                                       int16_t *prev_y,
                                       const uint8_t color[4], int h)
{
    if (h >= 0 && h < height) {
        buf[h * linesize + 0] += color[0];
        buf[h * linesize + 1] += color[1];
        buf[h * linesize + 2] += color[2];
        buf[h * linesize + 3] += color[3];
        if (*prev_y && h != *prev_y) {
            int start = *prev_y;
            uint8_t *bufk;
            int end = av_clip(h, 0, height-1);
            if (start > end)
                FFSWAP(int16_t, start, end);
            bufk = buf + (start + 1) * linesize;
            for (int k = start + 1; k < end; k++, bufk += linesize) {
                bufk[0] += color[0];
                bufk[1] += color[1];
                bufk[2] += color[2];
                bufk[3] += color[3];
            }
        }
    }
    *prev_y = h;
}

static void draw_sample_p2p_rgba_full(uint8_t *buf, int height, int linesize,
                                      int16_t *prev_y,
                                      const uint8_t color[4], int h)
{
    uint32_t clr = AV_RN32(color);
    if (h >= 0 && h < height) {
        AV_WN32(buf + h * linesize, clr);
        if (*prev_y && h != *prev_y) {
            int start = *prev_y;
            uint8_t *bufk;
            int end = av_clip(h, 0, height-1);
            if (start > end)
                FFSWAP(int16_t, start, end);
            bufk = buf + (start + 1) * linesize;
            for (int k = start + 1; k < end; k++, bufk += linesize)
                AV_WN32(bufk, clr);
        }
    }
    *prev_y = h;
}

static void draw_sample_cline_rgba_scale(uint8_t *buf, int height, int linesize,
                                         int16_t *prev_y,
                                         const uint8_t color[4], int h)
{
    const int start = (height - h) / 2;
    const int end   = start + h;
    uint8_t *bufk = buf + start * linesize;
    for (int k = start; k < end; k++, bufk += linesize) {
        bufk[0] += color[0];
        bufk[1] += color[1];
        bufk[2] += color[2];
        bufk[3] += color[3];
    }
}

static void draw_sample_cline_rgba_full(uint8_t *buf, int height, int linesize,
                                         int16_t *prev_y,
                                         const uint8_t color[4], int h)
{
    uint32_t clr = AV_RN32(color);
    const int start = (height - h) / 2;
    const int end   = start + h;
    uint8_t *bufk = buf + start * linesize;
    for (int k = start; k < end; k++, bufk += linesize)
        AV_WN32(bufk, clr);
}

static void draw_sample_point_gray(uint8_t *buf, int height, int linesize,
                                   int16_t *prev_y,
                                   const uint8_t color[4], int h)
{
    if (h >= 0 && h < height)
        buf[h * linesize] += color[0];
}

static void draw_sample_line_gray(uint8_t *buf, int height, int linesize,
                                  int16_t *prev_y,
                                  const uint8_t color[4], int h)
{
    int k;
    int start   = height/2;
    int end     = av_clip(h, 0, height-1);
    if (start > end)
        FFSWAP(int16_t, start, end);
    for (k = start; k < end; k++)
        buf[k * linesize] += color[0];
}

static void draw_sample_p2p_gray(uint8_t *buf, int height, int linesize,
                                 int16_t *prev_y,
                                 const uint8_t color[4], int h)
{
    int k;
    if (h >= 0 && h < height) {
        buf[h * linesize] += color[0];
        if (*prev_y && h != *prev_y) {
            int start = *prev_y;
            int end = av_clip(h, 0, height-1);
            if (start > end)
                FFSWAP(int16_t, start, end);
            for (k = start + 1; k < end; k++)
                buf[k * linesize] += color[0];
        }
    }
    *prev_y = h;
}

static void draw_sample_cline_gray(uint8_t *buf, int height, int linesize,
                                   int16_t *prev_y,
                                   const uint8_t color[4], int h)
{
    int k;
    const int start = (height - h) / 2;
    const int end   = start + h;
    for (k = start; k < end; k++)
        buf[k * linesize] += color[0];
}

static int config_output(AVFilterLink *outlink)
{
    FilterLink *l = ff_filter_link(outlink);
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    ShowWavesContext *showwaves = ctx->priv;
    int nb_channels = inlink->ch_layout.nb_channels;
    char *colors, *saveptr = NULL;
    uint8_t x;
    int ch;

    showwaves->q = av_make_q(0, 1);
    showwaves->c = av_make_q(0, 1);

    if (showwaves->single_pic) {
        showwaves->n = av_make_q(1, 1);
        l->frame_rate = av_make_q(1, 1);
    } else {
        if (!showwaves->n.num || !showwaves->n.den) {
            showwaves->n = av_mul_q(av_make_q(inlink->sample_rate,
                                              showwaves->w), av_inv_q(showwaves->rate));
            l->frame_rate = showwaves->rate;
        } else {
            l->frame_rate = av_div_q(av_make_q(inlink->sample_rate, showwaves->w), showwaves->n);
        }
    }

    showwaves->buf_idx = 0;
    if (!FF_ALLOCZ_TYPED_ARRAY(showwaves->buf_idy, nb_channels)) {
        av_log(ctx, AV_LOG_ERROR, "Could not allocate showwaves buffer\n");
        return AVERROR(ENOMEM);
    }

    showwaves->history_nb_samples = av_rescale(showwaves->w * nb_channels * 2,
                                               showwaves->n.num, showwaves->n.den);
    if (showwaves->history_nb_samples <= 0)
        return AVERROR(EINVAL);
    showwaves->history = av_calloc(showwaves->history_nb_samples,
                                   sizeof(*showwaves->history));
    if (!showwaves->history)
        return AVERROR(ENOMEM);

    outlink->time_base = av_inv_q(l->frame_rate);
    outlink->w = showwaves->w;
    outlink->h = showwaves->h;
    outlink->sample_aspect_ratio = (AVRational){1,1};

    av_log(ctx, AV_LOG_VERBOSE, "s:%dx%d r:%f n:%f\n",
           showwaves->w, showwaves->h, av_q2d(l->frame_rate), av_q2d(showwaves->n));

    switch (outlink->format) {
    case AV_PIX_FMT_GRAY8:
        switch (showwaves->mode) {
        case MODE_POINT:         showwaves->draw_sample = draw_sample_point_gray; break;
        case MODE_LINE:          showwaves->draw_sample = draw_sample_line_gray;  break;
        case MODE_P2P:           showwaves->draw_sample = draw_sample_p2p_gray;   break;
        case MODE_CENTERED_LINE: showwaves->draw_sample = draw_sample_cline_gray; break;
        default:
            return AVERROR_BUG;
        }
        showwaves->pixstep = 1;
        break;
    case AV_PIX_FMT_RGBA:
        switch (showwaves->mode) {
        case MODE_POINT:         showwaves->draw_sample = showwaves->draw_mode == DRAW_SCALE ? draw_sample_point_rgba_scale : draw_sample_point_rgba_full; break;
        case MODE_LINE:          showwaves->draw_sample = showwaves->draw_mode == DRAW_SCALE ? draw_sample_line_rgba_scale  : draw_sample_line_rgba_full;  break;
        case MODE_P2P:           showwaves->draw_sample = showwaves->draw_mode == DRAW_SCALE ? draw_sample_p2p_rgba_scale   : draw_sample_p2p_rgba_full;   break;
        case MODE_CENTERED_LINE: showwaves->draw_sample = showwaves->draw_mode == DRAW_SCALE ? draw_sample_cline_rgba_scale : draw_sample_cline_rgba_full; break;
        default:
            return AVERROR_BUG;
        }
        showwaves->pixstep = 4;
        break;
    }

    switch (showwaves->scale) {
    case SCALE_LIN:
        switch (showwaves->mode) {
        case MODE_POINT:
        case MODE_LINE:
        case MODE_P2P:           showwaves->get_h = get_lin_h;  break;
        case MODE_CENTERED_LINE: showwaves->get_h = get_lin_h2; break;
        default:
            return AVERROR_BUG;
        }
        break;
    case SCALE_LOG:
        switch (showwaves->mode) {
        case MODE_POINT:
        case MODE_LINE:
        case MODE_P2P:           showwaves->get_h = get_log_h;  break;
        case MODE_CENTERED_LINE: showwaves->get_h = get_log_h2; break;
        default:
            return AVERROR_BUG;
        }
        break;
    case SCALE_SQRT:
        switch (showwaves->mode) {
        case MODE_POINT:
        case MODE_LINE:
        case MODE_P2P:           showwaves->get_h = get_sqrt_h;  break;
        case MODE_CENTERED_LINE: showwaves->get_h = get_sqrt_h2; break;
        default:
            return AVERROR_BUG;
        }
        break;
    case SCALE_CBRT:
        switch (showwaves->mode) {
        case MODE_POINT:
        case MODE_LINE:
        case MODE_P2P:           showwaves->get_h = get_cbrt_h;  break;
        case MODE_CENTERED_LINE: showwaves->get_h = get_cbrt_h2; break;
        default:
            return AVERROR_BUG;
        }
        break;
    }

    showwaves->fg = av_malloc_array(nb_channels, 4 * sizeof(*showwaves->fg));
    if (!showwaves->fg)
        return AVERROR(ENOMEM);

    colors = av_strdup(showwaves->colors);
    if (!colors)
        return AVERROR(ENOMEM);

    if (showwaves->draw_mode == DRAW_SCALE) {
        /* multiplication factor, pre-computed to avoid in-loop divisions */
        x = (showwaves->n.den * 255) / ((showwaves->split_channels ? 1 : nb_channels) * showwaves->n.num);
    } else {
        x = 255;
    }
    if (outlink->format == AV_PIX_FMT_RGBA) {
        uint8_t fg[4] = { 0xff, 0xff, 0xff, 0xff };

        for (ch = 0; ch < nb_channels; ch++) {
            char *color;

            color = av_strtok(ch == 0 ? colors : NULL, " |", &saveptr);
            if (color)
                av_parse_color(fg, color, -1, ctx);
            showwaves->fg[4*ch + 0] = fg[0] * x / 255.;
            showwaves->fg[4*ch + 1] = fg[1] * x / 255.;
            showwaves->fg[4*ch + 2] = fg[2] * x / 255.;
            showwaves->fg[4*ch + 3] = fg[3] * x / 255.;
        }
    } else {
        for (ch = 0; ch < nb_channels; ch++)
            showwaves->fg[4 * ch + 0] = x;
    }
    av_free(colors);

    return 0;
}

inline static int push_frame(AVFilterLink *outlink, int i, int64_t pts)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    ShowWavesContext *showwaves = outlink->src->priv;
    int nb_channels = inlink->ch_layout.nb_channels;
    int ret;

    showwaves->outpicref->duration = 1;
    showwaves->outpicref->pts = av_rescale_q(pts + i,
                                             inlink->time_base,
                                             outlink->time_base);

    ret = ff_filter_frame(outlink, showwaves->outpicref);
    showwaves->outpicref = NULL;
    showwaves->buf_idx = 0;
    for (int i = 0; i < nb_channels; i++)
        showwaves->buf_idy[i] = 0;
    return ret;
}

static int push_single_pic(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    ShowWavesContext *showwaves = ctx->priv;
    int64_t n = 0, column_max_samples = showwaves->total_samples / outlink->w;
    int64_t remaining_samples = showwaves->total_samples - (column_max_samples * outlink->w);
    int64_t last_column_samples = column_max_samples + remaining_samples;
    AVFrame *out = showwaves->outpicref;
    struct frame_node *node;
    const int nb_channels = inlink->ch_layout.nb_channels;
    const int ch_height = showwaves->split_channels ? outlink->h / nb_channels : outlink->h;
    const int linesize = out->linesize[0];
    const int pixstep = showwaves->pixstep;
    int col = 0;
    int64_t *sum = showwaves->sum;

    if (column_max_samples == 0) {
        av_log(ctx, AV_LOG_ERROR, "Too few samples\n");
        return AVERROR(EINVAL);
    }

    av_log(ctx, AV_LOG_DEBUG, "Create frame averaging %"PRId64" samples per column\n", column_max_samples);

    memset(sum, 0, nb_channels * sizeof(*sum));

    for (node = showwaves->audio_frames; node; node = node->next) {
        int i;
        const AVFrame *frame = node->frame;
        const int16_t *p = (const int16_t *)frame->data[0];

        for (i = 0; i < frame->nb_samples; i++) {
            int64_t max_samples = col == outlink->w - 1 ? last_column_samples: column_max_samples;
            int ch;

            switch (showwaves->filter_mode) {
            case FILTER_AVERAGE:
                for (ch = 0; ch < nb_channels; ch++)
                    sum[ch] += abs(p[ch + i*nb_channels]);
                break;
            case FILTER_PEAK:
                for (ch = 0; ch < nb_channels; ch++)
                    sum[ch] = FFMAX(sum[ch], abs(p[ch + i*nb_channels]));
                break;
            }

            n++;
            if (n == max_samples) {
                for (ch = 0; ch < nb_channels; ch++) {
                    int16_t sample = sum[ch] / (showwaves->filter_mode == FILTER_AVERAGE ? max_samples : 1);
                    uint8_t *buf = out->data[0] + col * pixstep;
                    int h;

                    if (showwaves->split_channels)
                        buf += ch*ch_height*linesize;
                    av_assert0(col < outlink->w);
                    h = showwaves->get_h(sample, ch_height);
                    showwaves->draw_sample(buf, ch_height, linesize, &showwaves->buf_idy[ch], &showwaves->fg[ch * 4], h);
                    sum[ch] = 0;
                }
                col++;
                n = 0;
            }
        }
    }

    return push_frame(outlink, 0, 0);
}


static int request_frame(AVFilterLink *outlink)
{
    ShowWavesContext *showwaves = outlink->src->priv;
    AVFilterLink *inlink = outlink->src->inputs[0];
    int ret;

    ret = ff_request_frame(inlink);
    if (ret == AVERROR_EOF && showwaves->outpicref) {
        push_single_pic(outlink);
    }

    return ret;
}

static int alloc_out_frame(ShowWavesContext *showwaves,
                           AVFilterLink *outlink)
{
    if (!showwaves->outpicref) {
        AVFrame *out = showwaves->outpicref =
            ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out)
            return AVERROR(ENOMEM);
        out->width  = outlink->w;
        out->height = outlink->h;
        for (int j = 0; j < outlink->h; j++)
            memset(out->data[0] + j*out->linesize[0], 0, outlink->w * showwaves->pixstep);
    }
    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    ShowWavesContext *showwaves = ctx->priv;

    if (!strcmp(ctx->filter->name, "showwavespic")) {
        showwaves->single_pic = 1;
        showwaves->mode = MODE_CENTERED_LINE;
    }

    return 0;
}

#if CONFIG_SHOWWAVES_FILTER

static int showwaves_filter_frame(AVFilterLink *inlink, AVFrame *insamples)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    ShowWavesContext *showwaves = ctx->priv;
    const int nb_samples = insamples->nb_samples;
    AVFrame *outpicref = showwaves->outpicref;
    const int16_t *p = (const int16_t *)insamples->data[0];
    int16_t *history = showwaves->history;
    const int nb_channels = inlink->ch_layout.nb_channels;
    int i, j, ret = 0, linesize;
    const int pixstep = showwaves->pixstep;
    const int ch_height = showwaves->split_channels ? outlink->h / nb_channels : outlink->h;
    const int history_nb_samples = showwaves->history_nb_samples;
    const int split_channels = showwaves->split_channels;
    const AVRational i_n = av_inv_q(showwaves->n);
    const AVRational u_q = av_make_q(1, 1);
    const AVRational z_q = av_make_q(0, 1);
    int16_t *buf_idy = showwaves->buf_idy;
    int idx = showwaves->history_index;
    int buf_idx = showwaves->buf_idx;
    const uint8_t *fg = showwaves->fg;
    const int w = showwaves->w;
    uint8_t *dst;

    for (int n = 0; n < nb_samples * nb_channels; n++) {
        history[idx++] = p[n];
        if (idx >= history_nb_samples)
            idx = 0;
    }
    showwaves->history_index = idx;

    ret = alloc_out_frame(showwaves, outlink);
    if (ret < 0)
        goto end;
    outpicref = showwaves->outpicref;
    linesize = outpicref->linesize[0];

    /* draw data in the buffer */
    dst = outpicref->data[0];
    for (i = 0; i < history_nb_samples; i++) {
        for (j = 0; j < nb_channels; j++) {
            uint8_t *buf = dst + buf_idx * pixstep;
            int h;

            if (split_channels)
                buf += j*ch_height*linesize;
            h = showwaves->get_h(history[idx++], ch_height);
            if (idx >= history_nb_samples)
                idx = 0;
            showwaves->draw_sample(buf, ch_height, linesize,
                                   &buf_idy[j], &fg[j * 4], h);
        }

        showwaves->c = av_add_q(showwaves->c, i_n);
        if (av_cmp_q(showwaves->c, u_q) >= 0) {
            showwaves->c = z_q;
            buf_idx++;
        }
        if (buf_idx == w)
            break;
    }

    showwaves->buf_idx = buf_idx;

    if ((ret = push_frame(outlink, history_nb_samples - i - 1, insamples->pts)) < 0)
        goto end;
    outpicref = showwaves->outpicref;
end:
    av_frame_free(&insamples);
    return ret;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    ShowWavesContext *showwaves = ctx->priv;
    AVRational q;
    AVFrame *in;
    int nb_samples;
    int ret;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    q = av_add_q(showwaves->q, av_mul_q(av_make_q(outlink->w, 1), showwaves->n));
    nb_samples = (q.num + (q.den / 2)) / q.den;
    ret = ff_inlink_consume_samples(inlink, nb_samples, nb_samples, &in);
    if (ret < 0)
        return ret;
    if (ret > 0) {
        showwaves->q = av_sub_q(q, av_make_q(nb_samples, 1));
        return showwaves_filter_frame(inlink, in);
    }

    FF_FILTER_FORWARD_STATUS(inlink, outlink);
    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static const AVFilterPad showwaves_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

const AVFilter ff_avf_showwaves = {
    .name          = "showwaves",
    .description   = NULL_IF_CONFIG_SMALL("Convert input audio to a video output."),
    .init          = init,
    .uninit        = uninit,
    .priv_size     = sizeof(ShowWavesContext),
    FILTER_INPUTS(ff_audio_default_filterpad),
    .activate      = activate,
    FILTER_OUTPUTS(showwaves_outputs),
    FILTER_QUERY_FUNC2(query_formats),
    .priv_class    = &showwaves_class,
};

#endif // CONFIG_SHOWWAVES_FILTER

#if CONFIG_SHOWWAVESPIC_FILTER

#define OFFSET(x) offsetof(ShowWavesContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption showwavespic_options[] = {
    { "size", "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "600x240"}, 0, 0, FLAGS },
    { "s",    "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "600x240"}, 0, 0, FLAGS },
    { "split_channels", "draw channels separately", OFFSET(split_channels), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, FLAGS },
    { "colors", "set channels colors", OFFSET(colors), AV_OPT_TYPE_STRING, {.str = "red|green|blue|yellow|orange|lime|pink|magenta|brown" }, 0, 0, FLAGS },
    { "scale", "set amplitude scale", OFFSET(scale), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, SCALE_NB-1, FLAGS, .unit="scale" },
        { "lin", "linear",         0, AV_OPT_TYPE_CONST, {.i64=SCALE_LIN}, .flags=FLAGS, .unit="scale"},
        { "log", "logarithmic",    0, AV_OPT_TYPE_CONST, {.i64=SCALE_LOG}, .flags=FLAGS, .unit="scale"},
        { "sqrt", "square root",   0, AV_OPT_TYPE_CONST, {.i64=SCALE_SQRT}, .flags=FLAGS, .unit="scale"},
        { "cbrt", "cubic root",    0, AV_OPT_TYPE_CONST, {.i64=SCALE_CBRT}, .flags=FLAGS, .unit="scale"},
    { "draw", "set draw mode", OFFSET(draw_mode), AV_OPT_TYPE_INT, {.i64 = DRAW_SCALE}, 0, DRAW_NB-1, FLAGS, .unit="draw" },
        { "scale", "scale pixel values for each drawn sample", 0, AV_OPT_TYPE_CONST, {.i64=DRAW_SCALE}, .flags=FLAGS, .unit="draw"},
        { "full",  "draw every pixel for sample directly",     0, AV_OPT_TYPE_CONST, {.i64=DRAW_FULL},  .flags=FLAGS, .unit="draw"},
    { "filter", "set filter mode", OFFSET(filter_mode), AV_OPT_TYPE_INT, {.i64 = FILTER_AVERAGE}, 0, FILTER_NB-1, FLAGS, .unit="filter" },
        { "average", "use average samples", 0, AV_OPT_TYPE_CONST, {.i64=FILTER_AVERAGE}, .flags=FLAGS, .unit="filter"},
        { "peak",    "use peak samples",    0, AV_OPT_TYPE_CONST, {.i64=FILTER_PEAK},    .flags=FLAGS, .unit="filter"},
    { NULL }
};

AVFILTER_DEFINE_CLASS(showwavespic);

static int showwavespic_config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ShowWavesContext *showwaves = ctx->priv;

    if (showwaves->single_pic) {
        showwaves->sum = av_calloc(inlink->ch_layout.nb_channels, sizeof(*showwaves->sum));
        if (!showwaves->sum)
            return AVERROR(ENOMEM);
    }

    return 0;
}

static int showwavespic_filter_frame(AVFilterLink *inlink, AVFrame *insamples)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    ShowWavesContext *showwaves = ctx->priv;
    int ret = 0;

    if (showwaves->single_pic) {
        struct frame_node *f;

        ret = alloc_out_frame(showwaves, outlink);
        if (ret < 0)
            goto end;

        /* queue the audio frame */
        f = av_malloc(sizeof(*f));
        if (!f) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
        f->frame = insamples;
        f->next = NULL;
        if (!showwaves->last_frame) {
            showwaves->audio_frames =
            showwaves->last_frame   = f;
        } else {
            showwaves->last_frame->next = f;
            showwaves->last_frame = f;
        }
        showwaves->total_samples += insamples->nb_samples;

        return 0;
    }

end:
    av_frame_free(&insamples);
    return ret;
}

static const AVFilterPad showwavespic_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = showwavespic_config_input,
        .filter_frame = showwavespic_filter_frame,
    },
};

static const AVFilterPad showwavespic_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
};

const AVFilter ff_avf_showwavespic = {
    .name          = "showwavespic",
    .description   = NULL_IF_CONFIG_SMALL("Convert input audio to a video output single picture."),
    .init          = init,
    .uninit        = uninit,
    .priv_size     = sizeof(ShowWavesContext),
    FILTER_INPUTS(showwavespic_inputs),
    FILTER_OUTPUTS(showwavespic_outputs),
    FILTER_QUERY_FUNC2(query_formats),
    .priv_class    = &showwavespic_class,
};

#endif // CONFIG_SHOWWAVESPIC_FILTER
