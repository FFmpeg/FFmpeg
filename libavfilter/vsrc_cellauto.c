/*
 * Copyright (c) Stefano Sabatini 2011
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
 * cellular automaton video source, based on Stephen Wolfram "experimentus crucis"
 */

/* #define DEBUG */

#include "libavutil/file.h"
#include "libavutil/lfg.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/random_seed.h"
#include "avfilter.h"
#include "internal.h"
#include "formats.h"
#include "video.h"

typedef struct {
    const AVClass *class;
    int w, h;
    char *filename;
    char *rule_str;
    uint8_t *file_buf;
    size_t file_bufsize;
    uint8_t *buf;
    int buf_prev_row_idx, buf_row_idx;
    uint8_t rule;
    uint64_t pts;
    AVRational time_base;
    char *rate;                 ///< video frame rate
    double   random_fill_ratio;
    uint32_t random_seed;
    int stitch, scroll, start_full;
    int64_t generation;         ///< the generation number, starting from 0
    AVLFG lfg;
    char *pattern;
} CellAutoContext;

#define OFFSET(x) offsetof(CellAutoContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption cellauto_options[] = {
    { "filename", "read initial pattern from file", OFFSET(filename), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { "f",        "read initial pattern from file", OFFSET(filename), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { "pattern",  "set initial pattern", OFFSET(pattern), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { "p",        "set initial pattern", OFFSET(pattern), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { "rate",     "set video rate", OFFSET(rate), AV_OPT_TYPE_STRING, {.str = "25"}, 0, 0, FLAGS },
    { "r",        "set video rate", OFFSET(rate), AV_OPT_TYPE_STRING, {.str = "25"}, 0, 0, FLAGS },
    { "size",     "set video size", OFFSET(w),    AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL}, 0, 0, FLAGS },
    { "s",        "set video size", OFFSET(w),    AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL}, 0, 0, FLAGS },
    { "rule",     "set rule",       OFFSET(rule), AV_OPT_TYPE_INT,    {.i64 = 110},  0, 255, FLAGS },
    { "random_fill_ratio", "set fill ratio for filling initial grid randomly", OFFSET(random_fill_ratio), AV_OPT_TYPE_DOUBLE, {.dbl = 1/M_PHI}, 0, 1, FLAGS },
    { "ratio",             "set fill ratio for filling initial grid randomly", OFFSET(random_fill_ratio), AV_OPT_TYPE_DOUBLE, {.dbl = 1/M_PHI}, 0, 1, FLAGS },
    { "random_seed", "set the seed for filling the initial grid randomly", OFFSET(random_seed), AV_OPT_TYPE_INT, {.i64 = -1}, -1, UINT32_MAX, FLAGS },
    { "seed",        "set the seed for filling the initial grid randomly", OFFSET(random_seed), AV_OPT_TYPE_INT, {.i64 = -1}, -1, UINT32_MAX, FLAGS },
    { "scroll",      "scroll pattern downward", OFFSET(scroll), AV_OPT_TYPE_INT, {.i64 = 1}, 0, 1, FLAGS },
    { "start_full",  "start filling the whole video", OFFSET(start_full), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, FLAGS },
    { "full",        "start filling the whole video", OFFSET(start_full), AV_OPT_TYPE_INT, {.i64 = 1}, 0, 1, FLAGS },
    { "stitch",      "stitch boundaries", OFFSET(stitch), AV_OPT_TYPE_INT,    {.i64 = 1},   0, 1, FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(cellauto);

#ifdef DEBUG
static void show_cellauto_row(AVFilterContext *ctx)
{
    CellAutoContext *cellauto = ctx->priv;
    int i;
    uint8_t *row = cellauto->buf + cellauto->w * cellauto->buf_row_idx;
    char *line = av_malloc(cellauto->w + 1);
    if (!line)
        return;

    for (i = 0; i < cellauto->w; i++)
        line[i] = row[i] ? '@' : ' ';
    line[i] = 0;
    av_log(ctx, AV_LOG_DEBUG, "generation:%"PRId64" row:%s|\n", cellauto->generation, line);
    av_free(line);
}
#endif

static int init_pattern_from_string(AVFilterContext *ctx)
{
    CellAutoContext *cellauto = ctx->priv;
    char *p;
    int i, w = 0;

    w = strlen(cellauto->pattern);
    av_log(ctx, AV_LOG_DEBUG, "w:%d\n", w);

    if (cellauto->w) {
        if (w > cellauto->w) {
            av_log(ctx, AV_LOG_ERROR,
                   "The specified width is %d which cannot contain the provided string width of %d\n",
                   cellauto->w, w);
            return AVERROR(EINVAL);
        }
    } else {
        /* width was not specified, set it to width of the provided row */
        cellauto->w = w;
        cellauto->h = (double)cellauto->w * M_PHI;
    }

    cellauto->buf = av_mallocz(sizeof(uint8_t) * cellauto->w * cellauto->h);
    if (!cellauto->buf)
        return AVERROR(ENOMEM);

    /* fill buf */
    p = cellauto->pattern;
    for (i = (cellauto->w - w)/2;; i++) {
        av_log(ctx, AV_LOG_DEBUG, "%d %c\n", i, *p == '\n' ? 'N' : *p);
        if (*p == '\n' || !*p)
            break;
        else
            cellauto->buf[i] = !!isgraph(*(p++));
    }

    return 0;
}

static int init_pattern_from_file(AVFilterContext *ctx)
{
    CellAutoContext *cellauto = ctx->priv;
    int ret;

    ret = av_file_map(cellauto->filename,
                      &cellauto->file_buf, &cellauto->file_bufsize, 0, ctx);
    if (ret < 0)
        return ret;

    /* create a string based on the read file */
    cellauto->pattern = av_malloc(cellauto->file_bufsize + 1);
    if (!cellauto->pattern)
        return AVERROR(ENOMEM);
    memcpy(cellauto->pattern, cellauto->file_buf, cellauto->file_bufsize);
    cellauto->pattern[cellauto->file_bufsize] = 0;

    return init_pattern_from_string(ctx);
}

static int init(AVFilterContext *ctx, const char *args)
{
    CellAutoContext *cellauto = ctx->priv;
    AVRational frame_rate;
    int ret;

    cellauto->class = &cellauto_class;
    av_opt_set_defaults(cellauto);

    if ((ret = av_set_options_string(cellauto, args, "=", ":")) < 0)
        return ret;

    if ((ret = av_parse_video_rate(&frame_rate, cellauto->rate)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid frame rate: %s\n", cellauto->rate);
        return AVERROR(EINVAL);
    }

    if (!cellauto->w && !cellauto->filename && !cellauto->pattern)
        av_opt_set(cellauto, "size", "320x518", 0);

    cellauto->time_base.num = frame_rate.den;
    cellauto->time_base.den = frame_rate.num;

    if (cellauto->filename && cellauto->pattern) {
        av_log(ctx, AV_LOG_ERROR, "Only one of the filename or pattern options can be used\n");
        return AVERROR(EINVAL);
    }

    if (cellauto->filename) {
        if ((ret = init_pattern_from_file(ctx)) < 0)
            return ret;
    } else if (cellauto->pattern) {
        if ((ret = init_pattern_from_string(ctx)) < 0)
            return ret;
    } else {
        /* fill the first row randomly */
        int i;

        cellauto->buf = av_mallocz(sizeof(uint8_t) * cellauto->w * cellauto->h);
        if (!cellauto->buf)
            return AVERROR(ENOMEM);
        if (cellauto->random_seed == -1)
            cellauto->random_seed = av_get_random_seed();

        av_lfg_init(&cellauto->lfg, cellauto->random_seed);

        for (i = 0; i < cellauto->w; i++) {
            double r = (double)av_lfg_get(&cellauto->lfg) / UINT32_MAX;
            if (r <= cellauto->random_fill_ratio)
                cellauto->buf[i] = 1;
        }
    }

    av_log(ctx, AV_LOG_VERBOSE,
           "s:%dx%d r:%d/%d rule:%d stitch:%d scroll:%d full:%d seed:%u\n",
           cellauto->w, cellauto->h, frame_rate.num, frame_rate.den,
           cellauto->rule, cellauto->stitch, cellauto->scroll, cellauto->start_full,
           cellauto->random_seed);
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    CellAutoContext *cellauto = ctx->priv;

    av_file_unmap(cellauto->file_buf, cellauto->file_bufsize);
    av_freep(&cellauto->buf);
    av_freep(&cellauto->pattern);
}

static int config_props(AVFilterLink *outlink)
{
    CellAutoContext *cellauto = outlink->src->priv;

    outlink->w = cellauto->w;
    outlink->h = cellauto->h;
    outlink->time_base = cellauto->time_base;

    return 0;
}

static void evolve(AVFilterContext *ctx)
{
    CellAutoContext *cellauto = ctx->priv;
    int i, v, pos[3];
    uint8_t *row, *prev_row = cellauto->buf + cellauto->buf_row_idx * cellauto->w;
    enum { NW, N, NE };

    cellauto->buf_prev_row_idx = cellauto->buf_row_idx;
    cellauto->buf_row_idx      = cellauto->buf_row_idx == cellauto->h-1 ? 0 : cellauto->buf_row_idx+1;
    row = cellauto->buf + cellauto->w * cellauto->buf_row_idx;

    for (i = 0; i < cellauto->w; i++) {
        if (cellauto->stitch) {
            pos[NW] = i-1 < 0 ? cellauto->w-1 : i-1;
            pos[N]  = i;
            pos[NE] = i+1 == cellauto->w ? 0  : i+1;
            v = prev_row[pos[NW]]<<2 | prev_row[pos[N]]<<1 | prev_row[pos[NE]];
        } else {
            v = 0;
            v|= i-1 >= 0          ? prev_row[i-1]<<2 : 0;
            v|=                     prev_row[i  ]<<1    ;
            v|= i+1 < cellauto->w ? prev_row[i+1]    : 0;
        }
        row[i] = !!(cellauto->rule & (1<<v));
        av_dlog(ctx, "i:%d context:%c%c%c -> cell:%d\n", i,
                v&4?'@':' ', v&2?'@':' ', v&1?'@':' ', row[i]);
    }

    cellauto->generation++;
}

static void fill_picture(AVFilterContext *ctx, AVFilterBufferRef *picref)
{
    CellAutoContext *cellauto = ctx->priv;
    int i, j, k, row_idx = 0;
    uint8_t *p0 = picref->data[0];

    if (cellauto->scroll && cellauto->generation >= cellauto->h)
        /* show on top the oldest row */
        row_idx = (cellauto->buf_row_idx + 1) % cellauto->h;

    /* fill the output picture with the whole buffer */
    for (i = 0; i < cellauto->h; i++) {
        uint8_t byte = 0;
        uint8_t *row = cellauto->buf + row_idx*cellauto->w;
        uint8_t *p = p0;
        for (k = 0, j = 0; j < cellauto->w; j++) {
            byte |= row[j]<<(7-k++);
            if (k==8 || j == cellauto->w-1) {
                k = 0;
                *p++ = byte;
                byte = 0;
            }
        }
        row_idx = (row_idx + 1) % cellauto->h;
        p0 += picref->linesize[0];
    }
}

static int request_frame(AVFilterLink *outlink)
{
    CellAutoContext *cellauto = outlink->src->priv;
    AVFilterBufferRef *picref =
        ff_get_video_buffer(outlink, AV_PERM_WRITE, cellauto->w, cellauto->h);
    picref->video->sample_aspect_ratio = (AVRational) {1, 1};
    if (cellauto->generation == 0 && cellauto->start_full) {
        int i;
        for (i = 0; i < cellauto->h-1; i++)
            evolve(outlink->src);
    }
    fill_picture(outlink->src, picref);
    evolve(outlink->src);

    picref->pts = cellauto->pts++;
    picref->pos = -1;

#ifdef DEBUG
    show_cellauto_row(outlink->src);
#endif
    ff_filter_frame(outlink, picref);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_MONOBLACK, AV_PIX_FMT_NONE };
    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static const AVFilterPad cellauto_outputs[] = {
    {
        .name            = "default",
        .type            = AVMEDIA_TYPE_VIDEO,
        .request_frame   = request_frame,
        .config_props    = config_props,
    },
    { NULL }
};

AVFilter avfilter_vsrc_cellauto = {
    .name        = "cellauto",
    .description = NULL_IF_CONFIG_SMALL("Create pattern generated by an elementary cellular automaton."),
    .priv_size = sizeof(CellAutoContext),
    .init      = init,
    .uninit    = uninit,
    .query_formats = query_formats,
    .inputs        = NULL,
    .outputs       = cellauto_outputs,
    .priv_class    = &cellauto_class,
};
