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

typedef struct {
    const AVClass *class;
    enum preset preset;
    char *comp_points_str[NB_COMP + 1];
    char *comp_points_str_all;
    uint8_t graph[NB_COMP + 1][256];
    char *psfile;
    uint8_t rgba_map[4];
    int step;
} CurvesContext;

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
        "0/1 0.129/1 0.466/0.498 0.725/0 1/0",
        "0/1 0.109/1 0.301/0.498 0.517/0 1/0",
        "0/1 0.098/1 0.235/0.498 0.423/0 1/0",
    },
    [PRESET_CROSS_PROCESS] = {
        "0.25/0.156 0.501/0.501 0.686/0.745",
        "0.25/0.188 0.38/0.501 0.745/0.815 1/0.815",
        "0.231/0.094 0.709/0.874",
    },
    [PRESET_DARKER]             = { .master = "0.5/0.4" },
    [PRESET_INCREASE_CONTRAST]  = { .master = "0.149/0.066 0.831/0.905 0.905/0.98" },
    [PRESET_LIGHTER]            = { .master = "0.4/0.5" },
    [PRESET_LINEAR_CONTRAST]    = { .master = "0.305/0.286 0.694/0.713" },
    [PRESET_MEDIUM_CONTRAST]    = { .master = "0.286/0.219 0.639/0.643" },
    [PRESET_NEGATIVE]           = { .master = "0/1 1/0" },
    [PRESET_STRONG_CONTRAST]    = { .master = "0.301/0.196 0.592/0.6 0.686/0.737" },
    [PRESET_VINTAGE] = {
        "0/0.11 0.42/0.51 1/0.95",
        "0.50/0.48",
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

static int parse_points_str(AVFilterContext *ctx, struct keypoint **points, const char *s)
{
    char *p = (char *)s; // strtod won't alter the string
    struct keypoint *last = NULL;

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
            if ((int)(last->x * 255) >= (int)(point->x * 255)) {
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

    /* auto insert first key point if missing at x=0 */
    if (!*points) {
        last = make_point(0, 0, NULL);
        if (!last)
            return AVERROR(ENOMEM);
        last->x = last->y = 0;
        *points = last;
    } else if ((*points)->x != 0.) {
        struct keypoint *newfirst = make_point(0, 0, *points);
        if (!newfirst)
            return AVERROR(ENOMEM);
        *points = newfirst;
    }

    av_assert0(last);

    /* auto insert last key point if missing at x=1 */
    if (last->x != 1.) {
        struct keypoint *point = make_point(1, 1, NULL);
        if (!point)
            return AVERROR(ENOMEM);
        last->next = point;
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
static int interpolate(AVFilterContext *ctx, uint8_t *y, const struct keypoint *points)
{
    int i, ret = 0;
    const struct keypoint *point;
    double xprev = 0;

    int n = get_nb_points(points); // number of splines

    double (*matrix)[3] = av_calloc(n, sizeof(*matrix));
    double *h = av_malloc((n - 1) * sizeof(*h));
    double *r = av_calloc(n, sizeof(*r));

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
        double yp = point->y,
               yc = point->next->y,
               yn = point->next->next->y;
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
        double den = matrix[i][MD] - matrix[i][BD] * matrix[i-1][AD];
        double k = den ? 1./den : 1.;
        matrix[i][AD] *= k;
        r[i] = (r[i] - matrix[i][BD] * r[i - 1]) * k;
    }
    for (i = n - 2; i >= 0; i--)
        r[i] = r[i] - matrix[i][AD] * r[i + 1];

    /* compute the graph with x=[0..255] */
    i = 0;
    point = points;
    av_assert0(point->next); // always at least 2 key points
    while (point->next) {
        double yc = point->y;
        double yn = point->next->y;

        double a = yc;
        double b = (yn-yc)/h[i] - h[i]*r[i]/2. - h[i]*(r[i+1]-r[i])/6.;
        double c = r[i] / 2.;
        double d = (r[i+1] - r[i]) / (6.*h[i]);

        int x;
        int x_start = point->x       * 255;
        int x_end   = point->next->x * 255;

        av_assert0(x_start >= 0 && x_start <= 255 &&
                   x_end   >= 0 && x_end   <= 255);

        for (x = x_start; x <= x_end; x++) {
            double xx = (x - x_start) * 1/255.;
            double yy = a + b*xx + c*xx*xx + d*xx*xx*xx;
            y[x] = av_clipf(yy, 0, 1) * 255;
            av_log(ctx, AV_LOG_DEBUG, "f(%f)=%f -> y[%d]=%d\n", xx, yy, x, y[x]);
        }

        point = point->next;
        i++;
    }

end:
    av_free(matrix);
    av_free(h);
    av_free(r);
    return ret;
}

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
    if (size < 2)                       \
        return AVERROR_INVALIDDATA;     \
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

static av_cold int init(AVFilterContext *ctx)
{
    int i, j, ret;
    CurvesContext *curves = ctx->priv;
    struct keypoint *comp_points[NB_COMP + 1] = {0};
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

    for (i = 0; i < NB_COMP + 1; i++) {
        ret = parse_points_str(ctx, comp_points + i, curves->comp_points_str[i]);
        if (ret < 0)
            return ret;
        ret = interpolate(ctx, curves->graph[i], comp_points[i]);
        if (ret < 0)
            return ret;
    }

    if (pts[NB_COMP]) {
        for (i = 0; i < NB_COMP; i++)
            for (j = 0; j < 256; j++)
                curves->graph[i][j] = curves->graph[NB_COMP][curves->graph[i][j]];
    }

    if (av_log_get_level() >= AV_LOG_VERBOSE) {
        for (i = 0; i < NB_COMP; i++) {
            struct keypoint *point = comp_points[i];
            av_log(ctx, AV_LOG_VERBOSE, "#%d points:", i);
            while (point) {
                av_log(ctx, AV_LOG_VERBOSE, " (%f;%f)", point->x, point->y);
                point = point->next;
            }
            av_log(ctx, AV_LOG_VERBOSE, "\n");
            av_log(ctx, AV_LOG_VERBOSE, "#%d values:", i);
            for (j = 0; j < 256; j++)
                av_log(ctx, AV_LOG_VERBOSE, " %02X", curves->graph[i][j]);
            av_log(ctx, AV_LOG_VERBOSE, "\n");
        }
    }

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

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_RGB24,  AV_PIX_FMT_BGR24,
        AV_PIX_FMT_RGBA,   AV_PIX_FMT_BGRA,
        AV_PIX_FMT_ARGB,   AV_PIX_FMT_ABGR,
        AV_PIX_FMT_0RGB,   AV_PIX_FMT_0BGR,
        AV_PIX_FMT_RGB0,   AV_PIX_FMT_BGR0,
        AV_PIX_FMT_NONE
    };
    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    CurvesContext *curves = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    ff_fill_rgba_map(curves->rgba_map, inlink->format);
    curves->step = av_get_padded_bits_per_pixel(desc) >> 3;

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    int x, y, direct = 0;
    AVFilterContext *ctx = inlink->dst;
    CurvesContext *curves = ctx->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFrame *out;
    uint8_t *dst;
    const uint8_t *src;
    const int step = curves->step;
    const uint8_t r = curves->rgba_map[R];
    const uint8_t g = curves->rgba_map[G];
    const uint8_t b = curves->rgba_map[B];
    const uint8_t a = curves->rgba_map[A];

    if (av_frame_is_writable(in)) {
        direct = 1;
        out = in;
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    dst = out->data[0];
    src = in ->data[0];

    for (y = 0; y < inlink->h; y++) {
        for (x = 0; x < inlink->w * step; x += step) {
            dst[x + r] = curves->graph[R][src[x + r]];
            dst[x + g] = curves->graph[G][src[x + g]];
            dst[x + b] = curves->graph[B][src[x + b]];
            if (!direct && step == 4)
                dst[x + a] = src[x + a];
        }
        dst += out->linesize[0];
        src += in ->linesize[0];
    }

    if (!direct)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
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

AVFilter avfilter_vf_curves = {
    .name          = "curves",
    .description   = NULL_IF_CONFIG_SMALL("Adjust components curves."),
    .priv_size     = sizeof(CurvesContext),
    .init          = init,
    .query_formats = query_formats,
    .inputs        = curves_inputs,
    .outputs       = curves_outputs,
    .priv_class    = &curves_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE,
};
