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
#include "libavutil/internal.h"
#include "libavutil/lfg.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/random_seed.h"
#include "libavutil/avstring.h"
#include "avfilter.h"
#include "filters.h"
#include "video.h"

typedef struct CellAutoContext {
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
    AVRational frame_rate;
    double   random_fill_ratio;
    int64_t random_seed;
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
    { "rate",     "set video rate", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, INT_MAX, FLAGS },
    { "r",        "set video rate", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, INT_MAX, FLAGS },
    { "size",     "set video size", OFFSET(w),    AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL}, 0, 0, FLAGS },
    { "s",        "set video size", OFFSET(w),    AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL}, 0, 0, FLAGS },
    { "rule",     "set rule",       OFFSET(rule), AV_OPT_TYPE_INT,    {.i64 = 110},  0, 255, FLAGS },
    { "random_fill_ratio", "set fill ratio for filling initial grid randomly", OFFSET(random_fill_ratio), AV_OPT_TYPE_DOUBLE, {.dbl = 1/M_PHI}, 0, 1, FLAGS },
    { "ratio",             "set fill ratio for filling initial grid randomly", OFFSET(random_fill_ratio), AV_OPT_TYPE_DOUBLE, {.dbl = 1/M_PHI}, 0, 1, FLAGS },
    { "random_seed", "set the seed for filling the initial grid randomly", OFFSET(random_seed), AV_OPT_TYPE_INT64, {.i64 = -1}, -1, UINT32_MAX, FLAGS },
    { "seed",        "set the seed for filling the initial grid randomly", OFFSET(random_seed), AV_OPT_TYPE_INT64, {.i64 = -1}, -1, UINT32_MAX, FLAGS },
    { "scroll",      "scroll pattern downward", OFFSET(scroll), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, FLAGS },
    { "start_full",  "start filling the whole video", OFFSET(start_full), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, FLAGS },
    { "full",        "start filling the whole video", OFFSET(start_full), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, FLAGS },
    { "stitch",      "stitch boundaries", OFFSET(stitch), AV_OPT_TYPE_BOOL,    {.i64 = 1},   0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(cellauto);

#ifdef DEBUG
static void show_cellauto_row(AVFilterContext *ctx)
{
    CellAutoContext *s = ctx->priv;
    int i;
    uint8_t *row = s->buf + s->w * s->buf_row_idx;
    char *line = av_malloc(s->w + 1);
    if (!line)
        return;

    for (i = 0; i < s->w; i++)
        line[i] = row[i] ? '@' : ' ';
    line[i] = 0;
    av_log(ctx, AV_LOG_DEBUG, "generation:%"PRId64" row:%s|\n", s->generation, line);
    av_free(line);
}
#endif

static int init_pattern_from_string(AVFilterContext *ctx)
{
    CellAutoContext *s = ctx->priv;
    char *p;
    int i, w = 0;

    w = strlen(s->pattern);
    av_log(ctx, AV_LOG_DEBUG, "w:%d\n", w);

    if (s->w) {
        if (w > s->w) {
            av_log(ctx, AV_LOG_ERROR,
                   "The specified width is %d which cannot contain the provided string width of %d\n",
                   s->w, w);
            return AVERROR(EINVAL);
        }
    } else {
        /* width was not specified, set it to width of the provided row */
        s->w = w;
        s->h = (double)s->w * M_PHI;
    }

    s->buf = av_calloc(s->w, s->h * sizeof(*s->buf));
    if (!s->buf)
        return AVERROR(ENOMEM);

    /* fill buf */
    p = s->pattern;
    for (i = (s->w - w)/2;; i++) {
        av_log(ctx, AV_LOG_DEBUG, "%d %c\n", i, *p == '\n' ? 'N' : *p);
        if (*p == '\n' || !*p)
            break;
        else
            s->buf[i] = !!av_isgraph(*(p++));
    }

    return 0;
}

static int init_pattern_from_file(AVFilterContext *ctx)
{
    CellAutoContext *s = ctx->priv;
    int ret;

    ret = av_file_map(s->filename,
                      &s->file_buf, &s->file_bufsize, 0, ctx);
    if (ret < 0)
        return ret;

    /* create a string based on the read file */
    s->pattern = av_malloc(s->file_bufsize + 1);
    if (!s->pattern)
        return AVERROR(ENOMEM);
    memcpy(s->pattern, s->file_buf, s->file_bufsize);
    s->pattern[s->file_bufsize] = 0;

    return init_pattern_from_string(ctx);
}

static av_cold int init(AVFilterContext *ctx)
{
    CellAutoContext *s = ctx->priv;
    int ret;

    if (!s->w && !s->filename && !s->pattern)
        av_opt_set(s, "size", "320x518", 0);

    if (s->filename && s->pattern) {
        av_log(ctx, AV_LOG_ERROR, "Only one of the filename or pattern options can be used\n");
        return AVERROR(EINVAL);
    }

    if (s->filename) {
        if ((ret = init_pattern_from_file(ctx)) < 0)
            return ret;
    } else if (s->pattern) {
        if ((ret = init_pattern_from_string(ctx)) < 0)
            return ret;
    } else {
        /* fill the first row randomly */
        int i;

        s->buf = av_calloc(s->w, s->h * sizeof(*s->buf));
        if (!s->buf)
            return AVERROR(ENOMEM);
        if (s->random_seed == -1)
            s->random_seed = av_get_random_seed();

        av_lfg_init(&s->lfg, s->random_seed);

        for (i = 0; i < s->w; i++) {
            double r = (double)av_lfg_get(&s->lfg) / UINT32_MAX;
            if (r <= s->random_fill_ratio)
                s->buf[i] = 1;
        }
    }

    av_log(ctx, AV_LOG_VERBOSE,
           "s:%dx%d r:%d/%d rule:%d stitch:%d scroll:%d full:%d seed:%"PRId64"\n",
           s->w, s->h, s->frame_rate.num, s->frame_rate.den,
           s->rule, s->stitch, s->scroll, s->start_full,
           s->random_seed);
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    CellAutoContext *s = ctx->priv;

    av_file_unmap(s->file_buf, s->file_bufsize);
    av_freep(&s->buf);
    av_freep(&s->pattern);
}

static int config_props(AVFilterLink *outlink)
{
    CellAutoContext *s = outlink->src->priv;
    FilterLink *l = ff_filter_link(outlink);

    outlink->w = s->w;
    outlink->h = s->h;
    outlink->time_base = av_inv_q(s->frame_rate);
    l->frame_rate = s->frame_rate;

    return 0;
}

static void evolve(AVFilterContext *ctx)
{
    CellAutoContext *s = ctx->priv;
    int i, v, pos[3];
    uint8_t *row, *prev_row = s->buf + s->buf_row_idx * s->w;
    enum { NW, N, NE };

    s->buf_prev_row_idx = s->buf_row_idx;
    s->buf_row_idx      = s->buf_row_idx == s->h-1 ? 0 : s->buf_row_idx+1;
    row = s->buf + s->w * s->buf_row_idx;

    for (i = 0; i < s->w; i++) {
        if (s->stitch) {
            pos[NW] = i-1 < 0 ? s->w-1 : i-1;
            pos[N]  = i;
            pos[NE] = i+1 == s->w ? 0  : i+1;
            v = prev_row[pos[NW]]<<2 | prev_row[pos[N]]<<1 | prev_row[pos[NE]];
        } else {
            v = 0;
            v|= i-1 >= 0          ? prev_row[i-1]<<2 : 0;
            v|=                     prev_row[i  ]<<1    ;
            v|= i+1 < s->w ? prev_row[i+1]    : 0;
        }
        row[i] = !!(s->rule & (1<<v));
        ff_dlog(ctx, "i:%d context:%c%c%c -> cell:%d\n", i,
                v&4?'@':' ', v&2?'@':' ', v&1?'@':' ', row[i]);
    }

    s->generation++;
}

static void fill_picture(AVFilterContext *ctx, AVFrame *picref)
{
    CellAutoContext *s = ctx->priv;
    int i, j, k, row_idx = 0;
    uint8_t *p0 = picref->data[0];

    if (s->scroll && s->generation >= s->h)
        /* show on top the oldest row */
        row_idx = (s->buf_row_idx + 1) % s->h;

    /* fill the output picture with the whole buffer */
    for (i = 0; i < s->h; i++) {
        uint8_t byte = 0;
        uint8_t *row = s->buf + row_idx*s->w;
        uint8_t *p = p0;
        for (k = 0, j = 0; j < s->w; j++) {
            byte |= row[j]<<(7-k++);
            if (k==8 || j == s->w-1) {
                k = 0;
                *p++ = byte;
                byte = 0;
            }
        }
        row_idx = (row_idx + 1) % s->h;
        p0 += picref->linesize[0];
    }
}

static int request_frame(AVFilterLink *outlink)
{
    CellAutoContext *s = outlink->src->priv;
    AVFrame *picref = ff_get_video_buffer(outlink, s->w, s->h);
    if (!picref)
        return AVERROR(ENOMEM);
    picref->sample_aspect_ratio = (AVRational) {1, 1};
    if (s->generation == 0 && s->start_full) {
        int i;
        for (i = 0; i < s->h-1; i++)
            evolve(outlink->src);
    }
    fill_picture(outlink->src, picref);
    evolve(outlink->src);

    picref->pts = s->pts++;
    picref->duration = 1;

#ifdef DEBUG
    show_cellauto_row(outlink->src);
#endif
    return ff_filter_frame(outlink, picref);
}

static const AVFilterPad cellauto_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
        .config_props  = config_props,
    },
};

const FFFilter ff_vsrc_cellauto = {
    .p.name        = "cellauto",
    .p.description = NULL_IF_CONFIG_SMALL("Create pattern generated by an elementary cellular automaton."),
    .p.priv_class  = &cellauto_class,
    .p.inputs      = NULL,
    .priv_size     = sizeof(CellAutoContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_OUTPUTS(cellauto_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_MONOBLACK),
};
