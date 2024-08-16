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
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/lfg.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/random_seed.h"
#include "libavutil/avstring.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "video.h"

typedef struct LifeContext {
    const AVClass *class;
    int w, h;
    char *filename;
    char *rule_str;
    uint8_t *file_buf;
    size_t file_bufsize;

    /**
     * The two grid state buffers.
     *
     * A 0xFF (ALIVE_CELL) value means the cell is alive (or new born), while
     * the decreasing values from 0xFE to 0 means the cell is dead; the range
     * of values is used for the slow death effect, or mold (0xFE means dead,
     * 0xFD means very dead, 0xFC means very very dead... and 0x00 means
     * definitely dead/mold).
     */
    uint8_t *buf[2];

    uint8_t  buf_idx;
    uint16_t stay_rule;         ///< encode the behavior for filled cells
    uint16_t born_rule;         ///< encode the behavior for empty cells
    uint64_t pts;
    AVRational frame_rate;
    double   random_fill_ratio;
    int64_t random_seed;
    int stitch;
    int mold;
    uint8_t  life_color[4];
    uint8_t death_color[4];
    uint8_t  mold_color[4];
    AVLFG lfg;
    void (*draw)(AVFilterContext*, AVFrame*);
} LifeContext;

#define ALIVE_CELL 0xFF
#define OFFSET(x) offsetof(LifeContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption life_options[] = {
    { "filename", "set source file",  OFFSET(filename), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { "f",        "set source file",  OFFSET(filename), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { "size",     "set video size",   OFFSET(w),        AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL}, 0, 0, FLAGS },
    { "s",        "set video size",   OFFSET(w),        AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL}, 0, 0, FLAGS },
    { "rate",     "set video rate",   OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, INT_MAX, FLAGS },
    { "r",        "set video rate",   OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, INT_MAX, FLAGS },
    { "rule",     "set rule",         OFFSET(rule_str), AV_OPT_TYPE_STRING, {.str = "B3/S23"}, 0, 0, FLAGS },
    { "random_fill_ratio", "set fill ratio for filling initial grid randomly", OFFSET(random_fill_ratio), AV_OPT_TYPE_DOUBLE, {.dbl=1/M_PHI}, 0, 1, FLAGS },
    { "ratio",             "set fill ratio for filling initial grid randomly", OFFSET(random_fill_ratio), AV_OPT_TYPE_DOUBLE, {.dbl=1/M_PHI}, 0, 1, FLAGS },
    { "random_seed", "set the seed for filling the initial grid randomly", OFFSET(random_seed), AV_OPT_TYPE_INT64, {.i64=-1}, -1, UINT32_MAX, FLAGS },
    { "seed",        "set the seed for filling the initial grid randomly", OFFSET(random_seed), AV_OPT_TYPE_INT64, {.i64=-1}, -1, UINT32_MAX, FLAGS },
    { "stitch",      "stitch boundaries", OFFSET(stitch), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1, FLAGS },
    { "mold",        "set mold speed for dead cells", OFFSET(mold), AV_OPT_TYPE_INT, {.i64=0}, 0, 0xFF, FLAGS },
    { "life_color",  "set life color",  OFFSET( life_color), AV_OPT_TYPE_COLOR, {.str="white"}, 0, 0, FLAGS },
    { "death_color", "set death color", OFFSET(death_color), AV_OPT_TYPE_COLOR, {.str="black"}, 0, 0, FLAGS },
    { "mold_color",  "set mold color",  OFFSET( mold_color), AV_OPT_TYPE_COLOR, {.str="black"}, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(life);

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
            line[j] = life->buf[life->buf_idx][i*life->w + j] == ALIVE_CELL ? '@' : ' ';
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
    av_freep(&life->filename);

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

    if (life->w) {
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

    if (!(life->buf[0] = av_calloc(life->h * life->w, sizeof(*life->buf[0]))) ||
        !(life->buf[1] = av_calloc(life->h * life->w, sizeof(*life->buf[1])))) {
        av_freep(&life->buf[0]);
        av_freep(&life->buf[1]);
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
                life->buf[0][i*life->w + j] = av_isgraph(*(p++)) ? ALIVE_CELL : 0;
        }
    }
    life->buf_idx = 0;

    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    LifeContext *life = ctx->priv;
    int ret;

    if (!life->w && !life->filename)
        av_opt_set(life, "size", "320x240", 0);

    if ((ret = parse_rule(&life->born_rule, &life->stay_rule, life->rule_str, ctx)) < 0)
        return ret;

    if (!life->mold && memcmp(life->mold_color, "\x00\x00\x00", 3))
        av_log(ctx, AV_LOG_WARNING,
               "Mold color is set while mold isn't, ignoring the color.\n");

    if (!life->filename) {
        /* fill the grid randomly */
        int i;

        if (!(life->buf[0] = av_calloc(life->h * life->w, sizeof(*life->buf[0]))) ||
            !(life->buf[1] = av_calloc(life->h * life->w, sizeof(*life->buf[1])))) {
            av_freep(&life->buf[0]);
            av_freep(&life->buf[1]);
            return AVERROR(ENOMEM);
        }
        if (life->random_seed == -1)
            life->random_seed = av_get_random_seed();

        av_lfg_init(&life->lfg, life->random_seed);

        for (i = 0; i < life->w * life->h; i++) {
            double r = (double)av_lfg_get(&life->lfg) / UINT32_MAX;
            if (r <= life->random_fill_ratio)
                life->buf[0][i] = ALIVE_CELL;
        }
        life->buf_idx = 0;
    } else {
        if ((ret = init_pattern_from_file(ctx)) < 0)
            return ret;
    }

    av_log(ctx, AV_LOG_VERBOSE,
           "s:%dx%d r:%d/%d rule:%s stay_rule:%d born_rule:%d stitch:%d seed:%"PRId64"\n",
           life->w, life->h, life->frame_rate.num, life->frame_rate.den,
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
    FilterLink *l = ff_filter_link(outlink);

    outlink->w = life->w;
    outlink->h = life->h;
    outlink->time_base = av_inv_q(life->frame_rate);
    l->frame_rate = life->frame_rate;

    return 0;
}

static void evolve(AVFilterContext *ctx)
{
    LifeContext *life = ctx->priv;
    int i, j;
    uint8_t *oldbuf = life->buf[ life->buf_idx];
    uint8_t *newbuf = life->buf[!life->buf_idx];

    enum { NW, N, NE, W, E, SW, S, SE };

    /* evolve the grid */
    for (i = 0; i < life->h; i++) {
        for (j = 0; j < life->w; j++) {
            int pos[8][2], n, alive, cell;
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
            n = (pos[NW][0] == -1 || pos[NW][1] == -1 ? 0 : oldbuf[pos[NW][0]*life->w + pos[NW][1]] == ALIVE_CELL) +
                (pos[N ][0] == -1 || pos[N ][1] == -1 ? 0 : oldbuf[pos[N ][0]*life->w + pos[N ][1]] == ALIVE_CELL) +
                (pos[NE][0] == -1 || pos[NE][1] == -1 ? 0 : oldbuf[pos[NE][0]*life->w + pos[NE][1]] == ALIVE_CELL) +
                (pos[W ][0] == -1 || pos[W ][1] == -1 ? 0 : oldbuf[pos[W ][0]*life->w + pos[W ][1]] == ALIVE_CELL) +
                (pos[E ][0] == -1 || pos[E ][1] == -1 ? 0 : oldbuf[pos[E ][0]*life->w + pos[E ][1]] == ALIVE_CELL) +
                (pos[SW][0] == -1 || pos[SW][1] == -1 ? 0 : oldbuf[pos[SW][0]*life->w + pos[SW][1]] == ALIVE_CELL) +
                (pos[S ][0] == -1 || pos[S ][1] == -1 ? 0 : oldbuf[pos[S ][0]*life->w + pos[S ][1]] == ALIVE_CELL) +
                (pos[SE][0] == -1 || pos[SE][1] == -1 ? 0 : oldbuf[pos[SE][0]*life->w + pos[SE][1]] == ALIVE_CELL);
            cell  = oldbuf[i*life->w + j];
            alive = 1<<n & (cell == ALIVE_CELL ? life->stay_rule : life->born_rule);
            if (alive)     *newbuf = ALIVE_CELL; // new cell is alive
            else if (cell) *newbuf = cell - 1;   // new cell is dead and in the process of mold
            else           *newbuf = 0;          // new cell is definitely dead
            ff_dlog(ctx, "i:%d j:%d live_neighbors:%d cell:%d -> cell:%d\n", i, j, n, cell, *newbuf);
            newbuf++;
        }
    }

    life->buf_idx = !life->buf_idx;
}

static void fill_picture_monoblack(AVFilterContext *ctx, AVFrame *picref)
{
    LifeContext *life = ctx->priv;
    uint8_t *buf = life->buf[life->buf_idx];
    int i, j, k;

    /* fill the output picture with the old grid buffer */
    for (i = 0; i < life->h; i++) {
        uint8_t byte = 0;
        uint8_t *p = picref->data[0] + i * picref->linesize[0];
        for (k = 0, j = 0; j < life->w; j++) {
            byte |= (buf[i*life->w+j] == ALIVE_CELL)<<(7-k++);
            if (k==8 || j == life->w-1) {
                k = 0;
                *p++ = byte;
                byte = 0;
            }
        }
    }
}

// divide by 255 and round to nearest
// apply a fast variant: (X+127)/255 = ((X+127)*257+257)>>16 = ((X+128)*257)>>16
#define FAST_DIV255(x) ((((x) + 128) * 257) >> 16)

static void fill_picture_rgb(AVFilterContext *ctx, AVFrame *picref)
{
    LifeContext *life = ctx->priv;
    uint8_t *buf = life->buf[life->buf_idx];
    int i, j;

    /* fill the output picture with the old grid buffer */
    for (i = 0; i < life->h; i++) {
        uint8_t *p = picref->data[0] + i * picref->linesize[0];
        for (j = 0; j < life->w; j++) {
            uint8_t v = buf[i*life->w + j];
            if (life->mold && v != ALIVE_CELL) {
                const uint8_t *c1 = life-> mold_color;
                const uint8_t *c2 = life->death_color;
                int death_age = FFMIN((0xff - v) * life->mold, 0xff);
                *p++ = FAST_DIV255((c2[0] << 8) + ((int)c1[0] - (int)c2[0]) * death_age);
                *p++ = FAST_DIV255((c2[1] << 8) + ((int)c1[1] - (int)c2[1]) * death_age);
                *p++ = FAST_DIV255((c2[2] << 8) + ((int)c1[2] - (int)c2[2]) * death_age);
            } else {
                const uint8_t *c = v == ALIVE_CELL ? life->life_color : life->death_color;
                AV_WB24(p, c[0]<<16 | c[1]<<8 | c[2]);
                p += 3;
            }
        }
    }
}

static int request_frame(AVFilterLink *outlink)
{
    LifeContext *life = outlink->src->priv;
    AVFrame *picref = ff_get_video_buffer(outlink, life->w, life->h);
    if (!picref)
        return AVERROR(ENOMEM);
    picref->sample_aspect_ratio = (AVRational) {1, 1};
    picref->pts = life->pts++;
    picref->duration = 1;

    life->draw(outlink->src, picref);
    evolve(outlink->src);
#ifdef DEBUG
    show_life_grid(outlink->src);
#endif
    return ff_filter_frame(outlink, picref);
}

static int query_formats(AVFilterContext *ctx)
{
    LifeContext *life = ctx->priv;
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_NONE, AV_PIX_FMT_NONE };

    if (life->mold || memcmp(life-> life_color, "\xff\xff\xff", 3)
                   || memcmp(life->death_color, "\x00\x00\x00", 3)) {
        pix_fmts[0] = AV_PIX_FMT_RGB24;
        life->draw = fill_picture_rgb;
    } else {
        pix_fmts[0] = AV_PIX_FMT_MONOBLACK;
        life->draw = fill_picture_monoblack;
    }

    return ff_set_common_formats_from_list(ctx, pix_fmts);
}

static const AVFilterPad life_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
        .config_props  = config_props,
    },
};

const AVFilter ff_vsrc_life = {
    .name          = "life",
    .description   = NULL_IF_CONFIG_SMALL("Create life."),
    .priv_size     = sizeof(LifeContext),
    .priv_class    = &life_class,
    .init          = init,
    .uninit        = uninit,
    .inputs        = NULL,
    FILTER_OUTPUTS(life_outputs),
    FILTER_QUERY_FUNC(query_formats),
};
