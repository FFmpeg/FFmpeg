/*
 * Copyright (c) 2013 Clément Bœsch
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
#include "libavutil/bprint.h"
#include "libavutil/eval.h"
#include "libavutil/file.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/avassert.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

#define R 0
#define G 1
#define B 2
#define A 3

struct keypoint {
    double x, y;
    struct keypoint *next;
};

#define NB_COMP 3

enum preset {
    PRESET_NONE,
    PRESET_COLOR_NEGATIVE,
    PRESET_CROSS_PROCESS,
    PRESET_DARKER,
    PRESET_INCREASE_CONTRAST,
    PRESET_LIGHTER,
    PRESET_LINEAR_CONTRAST,
    PRESET_MEDIUM_CONTRAST,
    PRESET_NEGATIVE,
    PRESET_STRONG_CONTRAST,
    PRESET_VINTAGE,
    NB_PRESETS,
};

typedef struct CurvesContext {
    const AVClass *class;
    int preset;
    char *comp_points_str[NB_COMP + 1];
    char *comp_points_str_all;
    uint16_t *graph[NB_COMP + 1];
    int lut_size;
    char *psfile;
    uint8_t rgba_map[4];
    int step;
    char *plot_filename;
    int is_16bit;
} CurvesContext;

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

#define OFFSET(x) offsetof(CurvesContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption curves_options[] = {
    { "preset", "select a color curves preset", OFFSET(preset), AV_OPT_TYPE_INT, {.i64=PRESET_NONE}, PRESET_NONE, NB_PRESETS-1, FLAGS, "preset_name" },
        { "none",               NULL, 0, AV_OPT_TYPE_CONST, {.i64=PRESET_NONE},                 INT_MIN, INT_MAX, FLAGS, "preset_name" },
        { "color_negative",     NULL, 0, AV_OPT_TYPE_CONST, {.i64=PRESET_COLOR_NEGATIVE},       INT_MIN, INT_MAX, FLAGS, "preset_name" },
        { "cross_process",      NULL, 0, AV_OPT_TYPE_CONST, {.i64=PRESET_CROSS_PROCESS},        INT_MIN, INT_MAX, FLAGS, "preset_name" },
        { "darker",             NULL, 0, AV_OPT_TYPE_CONST, {.i64=PRESET_DARKER},               INT_MIN, INT_MAX, FLAGS, "preset_name" },
        { "increase_contrast",  NULL, 0, AV_OPT_TYPE_CONST, {.i64=PRESET_INCREASE_CONTRAST},    INT_MIN, INT_MAX, FLAGS, "preset_name" },
        { "lighter",            NULL, 0, AV_OPT_TYPE_CONST, {.i64=PRESET_LIGHTER},              INT_MIN, INT_MAX, FLAGS, "preset_name" },
        { "linear_contrast",    NULL, 0, AV_OPT_TYPE_CONST, {.i64=PRESET_LINEAR_CONTRAST},      INT_MIN, INT_MAX, FLAGS, "preset_name" },
        { "medium_contrast",    NULL, 0, AV_OPT_TYPE_CONST, {.i64=PRESET_MEDIUM_CONTRAST},      INT_MIN, INT_MAX, FLAGS, "preset_name" },
        { "negative",           NULL, 0, AV_OPT_TYPE_CONST, {.i64=PRESET_NEGATIVE},             INT_MIN, INT_MAX, FLAGS, "preset_name" },
        { "strong_contrast",    NULL, 0, AV_OPT_TYPE_CONST, {.i64=PRESET_STRONG_CONTRAST},      INT_MIN, INT_MAX, FLAGS, "preset_name" },
        { "vintage",            NULL, 0, AV_OPT_TYPE_CONST, {.i64=PRESET_VINTAGE},              INT_MIN, INT_MAX, FLAGS, "preset_name" },
    { "master","set master points coordinates",OFFSET(comp_points_str[NB_COMP]), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "m",     "set master points coordinates",OFFSET(comp_points_str[NB_COMP]), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "red",   "set red points coordinates",   OFFSET(comp_points_str[0]), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "r",     "set red points coordinates",   OFFSET(comp_points_str[0]), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "green", "set green points coordinates", OFFSET(comp_points_str[1]), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "g",     "set green points coordinates", OFFSET(comp_points_str[1]), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "blue",  "set blue points coordinates",  OFFSET(comp_points_str[2]), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "b",     "set blue points coordinates",  OFFSET(comp_points_str[2]), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "all",   "set points coordinates for all components", OFFSET(comp_points_str_all), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "psfile", "set Photoshop curves file name", OFFSET(psfile), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "plot", "save Gnuplot script of the curves in specified file", OFFSET(plot_filename), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(curves);

static const struct {
    const char *r;
    const char *g;
    const char *b;
    const char *master;
} curves_presets[] = {
    [PRESET_COLOR_NEGATIVE] = {
        "0.129/1 0.466/0.498 0.725/0",
        "0.109/1 0.301/0.498 0.517/0",
        "0.098/1 0.235/0.498 0.423/0",
    },
    [PRESET_CROSS_PROCESS] = {
        "0/0 0.25/0.156 0.501/0.501 0.686/0.745 1/1",
        "0/0 0.25/0.188 0.38/0.501 0.745/0.815 1/0.815",
        "0/0 0.231/0.094 0.709/0.874 1/1",
    },
    [PRESET_DARKER]             = { .master = "0/0 0.5/0.4 1/1" },
    [PRESET_INCREASE_CONTRAST]  = { .master = "0/0 0.149/0.066 0.831/0.905 0.905/0.98 1/1" },
    [PRESET_LIGHTER]            = { .master = "0/0 0.4/0.5 1/1" },
    [PRESET_LINEAR_CONTRAST]    = { .master = "0/0 0.305/0.286 0.694/0.713 1/1" },
    [PRESET_MEDIUM_CONTRAST]    = { .master = "0/0 0.286/0.219 0.639/0.643 1/1" },
    [PRESET_NEGATIVE]           = { .master = "0/1 1/0" },
    [PRESET_STRONG_CONTRAST]    = { .master = "0/0 0.301/0.196 0.592/0.6 0.686/0.737 1/1" },
    [PRESET_VINTAGE] = {
        "0/0.11 0.42/0.51 1/0.95",
        "0/0 0.50/0.48 1/1",
        "0/0.22 0.49/0.44 1/0.8",
    }
};

static struct keypoint *make_point(double x, double y, struct keypoint *next)
{
    struct keypoint *point = av_mallocz(sizeof(*point));

    if (!point)
        return NULL;
    point->x = x;
    point->y = y;
    point->next = next;
    return point;
}

static int parse_points_str(AVFilterContext *ctx, struct keypoint **points, const char *s,
                            int lut_size)
{
    char *p = (char *)s; // strtod won't alter the string
    struct keypoint *last = NULL;
    const int scale = lut_size - 1;

    /* construct a linked list based on the key points string */
    while (p && *p) {
        struct keypoint *point = make_point(0, 0, NULL);
        if (!point)
            return AVERROR(ENOMEM);
        point->x = av_strtod(p, &p); if (p && *p) p++;
        point->y = av_strtod(p, &p); if (p && *p) p++;
        if (point->x < 0 || point->x > 1 || point->y < 0 || point->y > 1) {
            av_log(ctx, AV_LOG_ERROR, "Invalid key point coordinates (%f;%f), "
                   "x and y must be in the [0;1] range.\n", point->x, point->y);
            return AVERROR(EINVAL);
        }
        if (!*points)
            *points = point;
        if (last) {
            if ((int)(last->x * scale) >= (int)(point->x * scale)) {
                av_log(ctx, AV_LOG_ERROR, "Key point coordinates (%f;%f) "
                       "and (%f;%f) are too close from each other or not "
                       "strictly increasing on the x-axis\n",
                       last->x, last->y, point->x, point->y);
                return AVERROR(EINVAL);
            }
            last->next = point;
        }
        last = point;
    }

    if (*points && !(*points)->next) {
        av_log(ctx, AV_LOG_WARNING, "Only one point (at (%f;%f)) is defined, "
               "this is unlikely to behave as you expect. You probably want"
               "at least 2 points.",
               (*points)->x, (*points)->y);
    }

    return 0;
}

static int get_nb_points(const struct keypoint *d)
{
    int n = 0;
    while (d) {
        n++;
        d = d->next;
    }
    return n;
}

/**
 * Natural cubic spline interpolation
 * Finding curves using Cubic Splines notes by Steven Rauch and John Stockie.
 * @see http://people.math.sfu.ca/~stockie/teaching/macm316/notes/splines.pdf
 */

#define CLIP(v) (nbits == 8 ? av_clip_uint8(v) : av_clip_uint16(v))

static inline int interpolate(void *log_ctx, uint16_t *y,
                              const struct keypoint *points, int nbits)
{
    int i, ret = 0;
    const struct keypoint *point = points;
    double xprev = 0;
    const int lut_size = 1<<nbits;
    const int scale = lut_size - 1;

    double (*matrix)[3];
    double *h, *r;
    const int n = get_nb_points(points); // number of splines

    if (n == 0) {
        for (i = 0; i < lut_size; i++)
            y[i] = i;
        return 0;
    }

    if (n == 1) {
        for (i = 0; i < lut_size; i++)
            y[i] = CLIP(point->y * scale);
        return 0;
    }

    matrix = av_calloc(n, sizeof(*matrix));
    h = av_malloc((n - 1) * sizeof(*h));
    r = av_calloc(n, sizeof(*r));

    if (!matrix || !h || !r) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* h(i) = x(i+1) - x(i) */
    i = -1;
    for (point = points; point; point = point->next) {
        if (i != -1)
            h[i] = point->x - xprev;
        xprev = point->x;
        i++;
    }

    /* right-side of the polynomials, will be modified to contains the solution */
    point = points;
    for (i = 1; i < n - 1; i++) {
        const double yp = point->y;
        const double yc = point->next->y;
        const double yn = point->next->next->y;
        r[i] = 6 * ((yn-yc)/h[i] - (yc-yp)/h[i-1]);
        point = point->next;
    }

#define BD 0 /* sub  diagonal (below main) */
#define MD 1 /* main diagonal (center) */
#define AD 2 /* sup  diagonal (above main) */

    /* left side of the polynomials into a tridiagonal matrix. */
    matrix[0][MD] = matrix[n - 1][MD] = 1;
    for (i = 1; i < n - 1; i++) {
        matrix[i][BD] = h[i-1];
        matrix[i][MD] = 2 * (h[i-1] + h[i]);
        matrix[i][AD] = h[i];
    }

    /* tridiagonal solving of the linear system */
    for (i = 1; i < n; i++) {
        const double den = matrix[i][MD] - matrix[i][BD] * matrix[i-1][AD];
        const double k = den ? 1./den : 1.;
        matrix[i][AD] *= k;
        r[i] = (r[i] - matrix[i][BD] * r[i - 1]) * k;
    }
    for (i = n - 2; i >= 0; i--)
        r[i] = r[i] - matrix[i][AD] * r[i + 1];

    point = points;

    /* left padding */
    for (i = 0; i < (int)(point->x * scale); i++)
        y[i] = CLIP(point->y * scale);

    /* compute the graph with x=[x0..xN] */
    i = 0;
    av_assert0(point->next); // always at least 2 key points
    while (point->next) {
        const double yc = point->y;
        const double yn = point->next->y;

        const double a = yc;
        const double b = (yn-yc)/h[i] - h[i]*r[i]/2. - h[i]*(r[i+1]-r[i])/6.;
        const double c = r[i] / 2.;
        const double d = (r[i+1] - r[i]) / (6.*h[i]);

        int x;
        const int x_start = point->x       * scale;
        const int x_end   = point->next->x * scale;

        av_assert0(x_start >= 0 && x_start < lut_size &&
                   x_end   >= 0 && x_end   < lut_size);

        for (x = x_start; x <= x_end; x++) {
            const double xx = (x - x_start) * 1./scale;
            const double yy = a + b*xx + c*xx*xx + d*xx*xx*xx;
            y[x] = CLIP(yy * scale);
            av_log(log_ctx, AV_LOG_DEBUG, "f(%f)=%f -> y[%d]=%d\n", xx, yy, x, y[x]);
        }

        point = point->next;
        i++;
    }

    /* right padding */
    for (i = (int)(point->x * scale); i < lut_size; i++)
        y[i] = CLIP(point->y * scale);

end:
    av_free(matrix);
    av_free(h);
    av_free(r);
    return ret;
}

#define DECLARE_INTERPOLATE_FUNC(nbits)                                     \
static int interpolate##nbits(void *log_ctx, uint16_t *y,                   \
                              const struct keypoint *points)                \
{                                                                           \
    return interpolate(log_ctx, y, points, nbits);                          \
}

DECLARE_INTERPOLATE_FUNC(8)
DECLARE_INTERPOLATE_FUNC(16)

static int parse_psfile(AVFilterContext *ctx, const char *fname)
{
    CurvesContext *curves = ctx->priv;
    uint8_t *buf;
    size_t size;
    int i, ret, av_unused(version), nb_curves;
    AVBPrint ptstr;
    static const int comp_ids[] = {3, 0, 1, 2};

    av_bprint_init(&ptstr, 0, AV_BPRINT_SIZE_AUTOMATIC);

    ret = av_file_map(fname, &buf, &size, 0, NULL);
    if (ret < 0)
        return ret;

#define READ16(dst) do {                \
    if (size < 2) {                     \
        ret = AVERROR_INVALIDDATA;      \
        goto end;                       \
    }                                   \
    dst = AV_RB16(buf);                 \
    buf  += 2;                          \
    size -= 2;                          \
} while (0)

    READ16(version);
    READ16(nb_curves);
    for (i = 0; i < FFMIN(nb_curves, FF_ARRAY_ELEMS(comp_ids)); i++) {
        int nb_points, n;
        av_bprint_clear(&ptstr);
        READ16(nb_points);
        for (n = 0; n < nb_points; n++) {
            int y, x;
            READ16(y);
            READ16(x);
            av_bprintf(&ptstr, "%f/%f ", x / 255., y / 255.);
        }
        if (*ptstr.str) {
            char **pts = &curves->comp_points_str[comp_ids[i]];
            if (!*pts) {
                *pts = av_strdup(ptstr.str);
                av_log(ctx, AV_LOG_DEBUG, "curves %d (intid=%d) [%d points]: [%s]\n",
                       i, comp_ids[i], nb_points, *pts);
                if (!*pts) {
                    ret = AVERROR(ENOMEM);
                    goto end;
                }
            }
        }
    }
end:
    av_bprint_finalize(&ptstr, NULL);
    av_file_unmap(buf, size);
    return ret;
}

static int dump_curves(const char *fname, uint16_t *graph[NB_COMP + 1],
                       struct keypoint *comp_points[NB_COMP + 1],
                       int lut_size)
{
    int i;
    AVBPrint buf;
    const double scale = 1. / (lut_size - 1);
    static const char * const colors[] = { "red", "green", "blue", "#404040", };
    FILE *f = av_fopen_utf8(fname, "w");

    av_assert0(FF_ARRAY_ELEMS(colors) == NB_COMP + 1);

    if (!f) {
        int ret = AVERROR(errno);
        av_log(NULL, AV_LOG_ERROR, "Cannot open file '%s' for writing: %s\n",
               fname, av_err2str(ret));
        return ret;
    }

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);

    av_bprintf(&buf, "set xtics 0.1\n");
    av_bprintf(&buf, "set ytics 0.1\n");
    av_bprintf(&buf, "set size square\n");
    av_bprintf(&buf, "set grid\n");

    for (i = 0; i < FF_ARRAY_ELEMS(colors); i++) {
        av_bprintf(&buf, "%s'-' using 1:2 with lines lc '%s' title ''",
                   i ? ", " : "plot ", colors[i]);
        if (comp_points[i])
            av_bprintf(&buf, ", '-' using 1:2 with points pointtype 3 lc '%s' title ''",
                    colors[i]);
    }
    av_bprintf(&buf, "\n");

    for (i = 0; i < FF_ARRAY_ELEMS(colors); i++) {
        int x;

        /* plot generated values */
        for (x = 0; x < lut_size; x++)
            av_bprintf(&buf, "%f %f\n", x * scale, graph[i][x] * scale);
        av_bprintf(&buf, "e\n");

        /* plot user knots */
        if (comp_points[i]) {
            const struct keypoint *point = comp_points[i];

            while (point) {
                av_bprintf(&buf, "%f %f\n", point->x, point->y);
                point = point->next;
            }
            av_bprintf(&buf, "e\n");
        }
    }

    fwrite(buf.str, 1, buf.len, f);
    fclose(f);
    av_bprint_finalize(&buf, NULL);
    return 0;
}

static av_cold int curves_init(AVFilterContext *ctx)
{
    int i, ret;
    CurvesContext *curves = ctx->priv;
    char **pts = curves->comp_points_str;
    const char *allp = curves->comp_points_str_all;

    //if (!allp && curves->preset != PRESET_NONE && curves_presets[curves->preset].all)
    //    allp = curves_presets[curves->preset].all;

    if (allp) {
        for (i = 0; i < NB_COMP; i++) {
            if (!pts[i])
                pts[i] = av_strdup(allp);
            if (!pts[i])
                return AVERROR(ENOMEM);
        }
    }

    if (curves->psfile) {
        ret = parse_psfile(ctx, curves->psfile);
        if (ret < 0)
            return ret;
    }

    if (curves->preset != PRESET_NONE) {
#define SET_COMP_IF_NOT_SET(n, name) do {                           \
    if (!pts[n] && curves_presets[curves->preset].name) {           \
        pts[n] = av_strdup(curves_presets[curves->preset].name);    \
        if (!pts[n])                                                \
            return AVERROR(ENOMEM);                                 \
    }                                                               \
} while (0)
        SET_COMP_IF_NOT_SET(0, r);
        SET_COMP_IF_NOT_SET(1, g);
        SET_COMP_IF_NOT_SET(2, b);
        SET_COMP_IF_NOT_SET(3, master);
    }

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_RGB24,  AV_PIX_FMT_BGR24,
        AV_PIX_FMT_RGBA,   AV_PIX_FMT_BGRA,
        AV_PIX_FMT_ARGB,   AV_PIX_FMT_ABGR,
        AV_PIX_FMT_0RGB,   AV_PIX_FMT_0BGR,
        AV_PIX_FMT_RGB0,   AV_PIX_FMT_BGR0,
        AV_PIX_FMT_RGB48,  AV_PIX_FMT_BGR48,
        AV_PIX_FMT_RGBA64, AV_PIX_FMT_BGRA64,
        AV_PIX_FMT_NONE
    };
    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int config_input(AVFilterLink *inlink)
{
    int i, j, ret;
    AVFilterContext *ctx = inlink->dst;
    CurvesContext *curves = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    char **pts = curves->comp_points_str;
    struct keypoint *comp_points[NB_COMP + 1] = {0};

    ff_fill_rgba_map(curves->rgba_map, inlink->format);
    curves->is_16bit = desc->comp[0].depth > 8;
    curves->lut_size = curves->is_16bit ? 1<<16 : 1<<8;
    curves->step = av_get_padded_bits_per_pixel(desc) >> (3 + curves->is_16bit);

    for (i = 0; i < NB_COMP + 1; i++) {
        curves->graph[i] = av_mallocz_array(curves->lut_size, sizeof(*curves->graph[0]));
        if (!curves->graph[i])
            return AVERROR(ENOMEM);
        ret = parse_points_str(ctx, comp_points + i, curves->comp_points_str[i], curves->lut_size);
        if (ret < 0)
            return ret;
        if (curves->is_16bit) ret = interpolate16(ctx, curves->graph[i], comp_points[i]);
        else                  ret = interpolate8(ctx, curves->graph[i], comp_points[i]);
        if (ret < 0)
            return ret;
    }

    if (pts[NB_COMP]) {
        for (i = 0; i < NB_COMP; i++)
            for (j = 0; j < curves->lut_size; j++)
                curves->graph[i][j] = curves->graph[NB_COMP][curves->graph[i][j]];
    }

    if (av_log_get_level() >= AV_LOG_VERBOSE) {
        for (i = 0; i < NB_COMP; i++) {
            const struct keypoint *point = comp_points[i];
            av_log(ctx, AV_LOG_VERBOSE, "#%d points:", i);
            while (point) {
                av_log(ctx, AV_LOG_VERBOSE, " (%f;%f)", point->x, point->y);
                point = point->next;
            }
        }
    }

    if (curves->plot_filename)
        dump_curves(curves->plot_filename, curves->graph, comp_points, curves->lut_size);

    for (i = 0; i < NB_COMP + 1; i++) {
        struct keypoint *point = comp_points[i];
        while (point) {
            struct keypoint *next = point->next;
            av_free(point);
            point = next;
        }
    }

    return 0;
}

static int filter_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    int x, y;
    const CurvesContext *curves = ctx->priv;
    const ThreadData *td = arg;
    const AVFrame *in  = td->in;
    const AVFrame *out = td->out;
    const int direct = out == in;
    const int step = curves->step;
    const uint8_t r = curves->rgba_map[R];
    const uint8_t g = curves->rgba_map[G];
    const uint8_t b = curves->rgba_map[B];
    const uint8_t a = curves->rgba_map[A];
    const int slice_start = (in->height *  jobnr   ) / nb_jobs;
    const int slice_end   = (in->height * (jobnr+1)) / nb_jobs;

    if (curves->is_16bit) {
        for (y = slice_start; y < slice_end; y++) {
            uint16_t       *dstp = (      uint16_t *)(out->data[0] + y * out->linesize[0]);
            const uint16_t *srcp = (const uint16_t *)(in ->data[0] + y *  in->linesize[0]);

            for (x = 0; x < in->width * step; x += step) {
                dstp[x + r] = curves->graph[R][srcp[x + r]];
                dstp[x + g] = curves->graph[G][srcp[x + g]];
                dstp[x + b] = curves->graph[B][srcp[x + b]];
                if (!direct && step == 4)
                    dstp[x + a] = srcp[x + a];
            }
        }
    } else {
        uint8_t       *dst = out->data[0] + slice_start * out->linesize[0];
        const uint8_t *src =  in->data[0] + slice_start *  in->linesize[0];

        for (y = slice_start; y < slice_end; y++) {
            for (x = 0; x < in->width * step; x += step) {
                dst[x + r] = curves->graph[R][src[x + r]];
                dst[x + g] = curves->graph[G][src[x + g]];
                dst[x + b] = curves->graph[B][src[x + b]];
                if (!direct && step == 4)
                    dst[x + a] = src[x + a];
            }
            dst += out->linesize[0];
            src += in ->linesize[0];
        }
    }
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    ThreadData td;

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    td.in  = in;
    td.out = out;
    ctx->internal->execute(ctx, filter_slice, &td, NULL, FFMIN(outlink->h, ff_filter_get_nb_threads(ctx)));

    if (out != in)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static av_cold void curves_uninit(AVFilterContext *ctx)
{
    int i;
    CurvesContext *curves = ctx->priv;

    for (i = 0; i < NB_COMP + 1; i++)
        av_freep(&curves->graph[i]);
}

static const AVFilterPad curves_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad curves_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_curves = {
    .name          = "curves",
    .description   = NULL_IF_CONFIG_SMALL("Adjust components curves."),
    .priv_size     = sizeof(CurvesContext),
    .init          = curves_init,
    .uninit        = curves_uninit,
    .query_formats = query_formats,
    .inputs        = curves_inputs,
    .outputs       = curves_outputs,
    .priv_class    = &curves_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};
