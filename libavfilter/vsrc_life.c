/*
 * Copyright (c) Stefano Sabatini 2010
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
 * life video source, based on John Conways' Life Game
 */

/* #define DEBUG */

#include "libavutil/file.h"
#include "libavutil/lfg.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/random_seed.h"
#include "avfilter.h"

typedef struct {
    const AVClass *class;
    int w, h;
    char *filename;
    char *rule_str;
    uint8_t *file_buf;
    size_t file_bufsize;
    char *buf[2];
    uint8_t  buf_idx;
    uint16_t stay_rule;         ///< encode the behavior for filled cells
    uint16_t born_rule;         ///< encode the behavior for empty cells
    uint64_t pts;
    AVRational time_base;
    char *size;                 ///< video frame size
    char *rate;                 ///< video frame rate
    double   random_fill_ratio;
    uint32_t random_seed;
    int stitch;
    AVLFG lfg;
} LifeContext;

#define OFFSET(x) offsetof(LifeContext, x)

static const AVOption life_options[] = {
    { "filename", "set source file",  OFFSET(filename), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0 },
    { "f",        "set source file",  OFFSET(filename), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0 },
    { "size",     "set video size",   OFFSET(size),     AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0 },
    { "s",        "set video size",   OFFSET(size),     AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0 },
    { "rate",     "set video rate",   OFFSET(rate),     AV_OPT_TYPE_STRING, {.str = "25"}, 0, 0 },
    { "r",        "set video rate",   OFFSET(rate),     AV_OPT_TYPE_STRING, {.str = "25"}, 0, 0 },
    { "rule",     "set rule",         OFFSET(rule_str), AV_OPT_TYPE_STRING, {.str = "B3/S23"}, CHAR_MIN, CHAR_MAX },
    { "random_fill_ratio", "set fill ratio for filling initial grid randomly", OFFSET(random_fill_ratio), AV_OPT_TYPE_DOUBLE, {.dbl=1/M_PHI}, 0, 1 },
    { "ratio",             "set fill ratio for filling initial grid randomly", OFFSET(random_fill_ratio), AV_OPT_TYPE_DOUBLE, {.dbl=1/M_PHI}, 0, 1 },
    { "random_seed", "set the seed for filling the initial grid randomly", OFFSET(random_seed), AV_OPT_TYPE_INT, {.dbl=-1}, -1, UINT32_MAX },
    { "seed",        "set the seed for filling the initial grid randomly", OFFSET(random_seed), AV_OPT_TYPE_INT, {.dbl=-1}, -1, UINT32_MAX },
    { "stitch",      "stitch boundaries", OFFSET(stitch), AV_OPT_TYPE_INT, {.dbl=1}, 0, 1 },
    { NULL },
};

static const char *life_get_name(void *ctx)
{
    return "life";
}

static const AVClass life_class = {
    "LifeContext",
    life_get_name,
    life_options
};

static int parse_rule(uint16_t *born_rule, uint16_t *stay_rule,
                      const char *rule_str, void *log_ctx)
{
    char *tail;
    const char *p = rule_str;
    *born_rule = 0;
    *stay_rule = 0;

    if (strchr("bBsS", *p)) {
        /* parse rule as a Born / Stay Alive code, see
         * http://en.wikipedia.org/wiki/Conway%27s_Game_of_Life */
        do {
            uint16_t *rule = (*p == 'b' || *p == 'B') ? born_rule : stay_rule;
            p++;
            while (*p >= '0' && *p <= '8') {
                *rule += 1<<(*p - '0');
                p++;
            }
            if (*p != '/')
                break;
            p++;
        } while (strchr("bBsS", *p));

        if (*p)
            goto error;
    } else {
        /* parse rule as a number, expressed in the form STAY|(BORN<<9),
         * where STAY and BORN encode the corresponding 9-bits rule */
        long int rule = strtol(rule_str, &tail, 10);
        if (*tail)
            goto error;
        *born_rule  = ((1<<9)-1) & rule;
        *stay_rule = rule >> 9;
    }

    return 0;

error:
    av_log(log_ctx, AV_LOG_ERROR, "Invalid rule code '%s' provided\n", rule_str);
    return AVERROR(EINVAL);
}

#ifdef DEBUG
static void show_life_grid(AVFilterContext *ctx)
{
    LifeContext *life = ctx->priv;
    int i, j;

    char *line = av_malloc(life->w + 1);
    if (!line)
        return;
    for (i = 0; i < life->h; i++) {
        for (j = 0; j < life->w; j++)
            line[j] = life->buf[life->buf_idx][i*life->w + j] ? '@' : ' ';
        line[j] = 0;
        av_log(ctx, AV_LOG_DEBUG, "%3d: %s\n", i, line);
    }
    av_free(line);
}
#endif

static int init_pattern_from_file(AVFilterContext *ctx)
{
    LifeContext *life = ctx->priv;
    char *p;
    int ret, i, i0, j, h = 0, w, max_w = 0;

    if ((ret = av_file_map(life->filename, &life->file_buf, &life->file_bufsize,
                           0, ctx)) < 0)
        return ret;

    /* prescan file to get the number of lines and the maximum width */
    w = 0;
    for (i = 0; i < life->file_bufsize; i++) {
        if (life->file_buf[i] == '\n') {
            h++; max_w = FFMAX(w, max_w); w = 0;
        } else {
            w++;
        }
    }
    av_log(ctx, AV_LOG_DEBUG, "h:%d max_w:%d\n", h, max_w);

    if (life->size) {
        if (max_w > life->w || h > life->h) {
            av_log(ctx, AV_LOG_ERROR,
                   "The specified size is %dx%d which cannot contain the provided file size of %dx%d\n",
                   life->w, life->h, max_w, h);
            return AVERROR(EINVAL);
        }
    } else {
        /* size was not specified, set it to size of the grid */
        life->w = max_w;
        life->h = h;
    }

    if (!(life->buf[0] = av_mallocz(sizeof(char) * life->h * life->w)) ||
        !(life->buf[1] = av_mallocz(sizeof(char) * life->h * life->w))) {
        av_free(life->buf[0]);
        av_free(life->buf[1]);
        return AVERROR(ENOMEM);
    }

    /* fill buf[0] */
    p = life->file_buf;
    for (i0 = 0, i = (life->h - h)/2; i0 < h; i0++, i++) {
        for (j = (life->w - max_w)/2;; j++) {
            av_log(ctx, AV_LOG_DEBUG, "%d:%d %c\n", i, j, *p == '\n' ? 'N' : *p);
            if (*p == '\n') {
                p++; break;
            } else
                life->buf[0][i*life->w + j] = !!isgraph(*(p++));
        }
    }
    life->buf_idx = 0;

    return 0;
}

static int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    LifeContext *life = ctx->priv;
    AVRational frame_rate;
    int ret;

    life->class = &life_class;
    av_opt_set_defaults(life);

    if ((ret = av_set_options_string(life, args, "=", ":")) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error parsing options string: '%s'\n", args);
        return ret;
    }

    if ((ret = av_parse_video_rate(&frame_rate, life->rate)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid frame rate: %s\n", life->rate);
        return AVERROR(EINVAL);
    }

    if (!life->size && !life->filename)
        av_opt_set(life, "size", "320x240", 0);

    if (life->size &&
        (ret = av_parse_video_size(&life->w, &life->h, life->size)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid frame size: %s\n", life->size);
        return ret;
    }

    if ((ret = parse_rule(&life->born_rule, &life->stay_rule, life->rule_str, ctx)) < 0)
        return ret;

    life->time_base.num = frame_rate.den;
    life->time_base.den = frame_rate.num;

    if (!life->filename) {
        /* fill the grid randomly */
        int i;

        if (!(life->buf[0] = av_mallocz(sizeof(char) * life->h * life->w)) ||
            !(life->buf[1] = av_mallocz(sizeof(char) * life->h * life->w))) {
            av_free(life->buf[0]);
            av_free(life->buf[1]);
            return AVERROR(ENOMEM);
        }
        if (life->random_seed == -1)
            life->random_seed = av_get_random_seed();

        av_lfg_init(&life->lfg, life->random_seed);

        for (i = 0; i < life->w * life->h; i++) {
            double r = (double)av_lfg_get(&life->lfg) / UINT32_MAX;
            if (r <= life->random_fill_ratio)
                life->buf[0][i] = 1;
        }
        life->buf_idx = 0;
    } else {
        if ((ret = init_pattern_from_file(ctx)) < 0)
            return ret;
    }

    av_log(ctx, AV_LOG_INFO,
           "s:%dx%d r:%d/%d rule:%s stay_rule:%d born_rule:%d stitch:%d seed:%u\n",
           life->w, life->h, frame_rate.num, frame_rate.den,
           life->rule_str, life->stay_rule, life->born_rule, life->stitch,
           life->random_seed);
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    LifeContext *life = ctx->priv;

    av_file_unmap(life->file_buf, life->file_bufsize);
    av_freep(&life->rule_str);
    av_freep(&life->buf[0]);
    av_freep(&life->buf[1]);
}

static int config_props(AVFilterLink *outlink)
{
    LifeContext *life = outlink->src->priv;

    outlink->w = life->w;
    outlink->h = life->h;
    outlink->time_base = life->time_base;

    return 0;
}

static void evolve(AVFilterContext *ctx)
{
    LifeContext *life = ctx->priv;
    int i, j, v;
    uint8_t *oldbuf = life->buf[ life->buf_idx];
    uint8_t *newbuf = life->buf[!life->buf_idx];

    enum { NW, N, NE, W, E, SW, S, SE };

    /* evolve the grid */
    for (i = 0; i < life->h; i++) {
        for (j = 0; j < life->w; j++) {
            int pos[8][2], n;
            if (life->stitch) {
                pos[NW][0] = (i-1) < 0 ? life->h-1 : i-1; pos[NW][1] = (j-1) < 0 ? life->w-1 : j-1;
                pos[N ][0] = (i-1) < 0 ? life->h-1 : i-1; pos[N ][1] =                         j  ;
                pos[NE][0] = (i-1) < 0 ? life->h-1 : i-1; pos[NE][1] = (j+1) == life->w ?  0 : j+1;
                pos[W ][0] =                         i  ; pos[W ][1] = (j-1) < 0 ? life->w-1 : j-1;
                pos[E ][0] =                         i  ; pos[E ][1] = (j+1) == life->w ? 0  : j+1;
                pos[SW][0] = (i+1) == life->h ?  0 : i+1; pos[SW][1] = (j-1) < 0 ? life->w-1 : j-1;
                pos[S ][0] = (i+1) == life->h ?  0 : i+1; pos[S ][1] =                         j  ;
                pos[SE][0] = (i+1) == life->h ?  0 : i+1; pos[SE][1] = (j+1) == life->w ?  0 : j+1;
            } else {
                pos[NW][0] = (i-1) < 0 ? -1        : i-1; pos[NW][1] = (j-1) < 0 ? -1        : j-1;
                pos[N ][0] = (i-1) < 0 ? -1        : i-1; pos[N ][1] =                         j  ;
                pos[NE][0] = (i-1) < 0 ? -1        : i-1; pos[NE][1] = (j+1) == life->w ? -1 : j+1;
                pos[W ][0] =                         i  ; pos[W ][1] = (j-1) < 0 ? -1        : j-1;
                pos[E ][0] =                         i  ; pos[E ][1] = (j+1) == life->w ? -1 : j+1;
                pos[SW][0] = (i+1) == life->h ? -1 : i+1; pos[SW][1] = (j-1) < 0 ? -1        : j-1;
                pos[S ][0] = (i+1) == life->h ? -1 : i+1; pos[S ][1] =                         j  ;
                pos[SE][0] = (i+1) == life->h ? -1 : i+1; pos[SE][1] = (j+1) == life->w ? -1 : j+1;
            }

            /* compute the number of live neighbor cells */
            n = (pos[NW][0] == -1 || pos[NW][1] == -1 ? 0 : oldbuf[pos[NW][0]*life->w + pos[NW][1]]) +
                (pos[N ][0] == -1 || pos[N ][1] == -1 ? 0 : oldbuf[pos[N ][0]*life->w + pos[N ][1]]) +
                (pos[NE][0] == -1 || pos[NE][1] == -1 ? 0 : oldbuf[pos[NE][0]*life->w + pos[NE][1]]) +
                (pos[W ][0] == -1 || pos[W ][1] == -1 ? 0 : oldbuf[pos[W ][0]*life->w + pos[W ][1]]) +
                (pos[E ][0] == -1 || pos[E ][1] == -1 ? 0 : oldbuf[pos[E ][0]*life->w + pos[E ][1]]) +
                (pos[SW][0] == -1 || pos[SW][1] == -1 ? 0 : oldbuf[pos[SW][0]*life->w + pos[SW][1]]) +
                (pos[S ][0] == -1 || pos[S ][1] == -1 ? 0 : oldbuf[pos[S ][0]*life->w + pos[S ][1]]) +
                (pos[SE][0] == -1 || pos[SE][1] == -1 ? 0 : oldbuf[pos[SE][0]*life->w + pos[SE][1]]);
            v = !!(1<<n & (oldbuf[i*life->w + j] ? life->stay_rule : life->born_rule));
            av_dlog(ctx, "i:%d j:%d live_neighbors:%d cell:%d -> cell:%d\n", i, j, n, oldbuf[i*life->w + j], v);
            newbuf[i*life->w+j] = v;
        }
    }

    life->buf_idx = !life->buf_idx;
}

static void fill_picture(AVFilterContext *ctx, AVFilterBufferRef *picref)
{
    LifeContext *life = ctx->priv;
    uint8_t *buf = life->buf[life->buf_idx];
    int i, j, k;

    /* fill the output picture with the old grid buffer */
    for (i = 0; i < life->h; i++) {
        uint8_t byte = 0;
        uint8_t *p = picref->data[0] + i * picref->linesize[0];
        for (k = 0, j = 0; j < life->w; j++) {
            byte |= buf[i*life->w+j]<<(7-k++);
            if (k==8 || j == life->w-1) {
                k = 0;
                *p++ = byte;
                byte = 0;
            }
        }
    }
}

static int request_frame(AVFilterLink *outlink)
{
    LifeContext *life = outlink->src->priv;
    AVFilterBufferRef *picref = avfilter_get_video_buffer(outlink, AV_PERM_WRITE, life->w, life->h);
    picref->video->sample_aspect_ratio = (AVRational) {1, 1};
    picref->pts = life->pts++;
    picref->pos = -1;

    fill_picture(outlink->src, picref);
    evolve(outlink->src);
#ifdef DEBUG
    show_life_grid(outlink->src);
#endif

    avfilter_start_frame(outlink, avfilter_ref_buffer(picref, ~0));
    avfilter_draw_slice(outlink, 0, life->h, 1);
    avfilter_end_frame(outlink);
    avfilter_unref_buffer(picref);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = { PIX_FMT_MONOBLACK, PIX_FMT_NONE };
    avfilter_set_common_pixel_formats(ctx, avfilter_make_format_list(pix_fmts));
    return 0;
}

AVFilter avfilter_vsrc_life = {
    .name        = "life",
    .description = NULL_IF_CONFIG_SMALL("Create life."),
    .priv_size = sizeof(LifeContext),
    .init      = init,
    .uninit    = uninit,
    .query_formats = query_formats,

    .inputs    = (const AVFilterPad[]) {
        { .name = NULL}
    },
    .outputs   = (const AVFilterPad[]) {
        { .name            = "default",
          .type            = AVMEDIA_TYPE_VIDEO,
          .request_frame   = request_frame,
          .config_props    = config_props },
        { .name = NULL}
    },
};
