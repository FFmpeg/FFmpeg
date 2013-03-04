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
#include "libavutil/eval.h"
#include "libavutil/avassert.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

struct keypoint {
    double x, y;
    struct keypoint *next;
};

#define NB_COMP 3

typedef struct {
    const AVClass *class;
    char *comp_points_str[NB_COMP];
    uint8_t graph[NB_COMP][256];
} CurvesContext;

#define OFFSET(x) offsetof(CurvesContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption curves_options[] = {
    { "red",   "set red points coordinates",   OFFSET(comp_points_str[0]), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "r",     "set red points coordinates",   OFFSET(comp_points_str[0]), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "green", "set green points coordinates", OFFSET(comp_points_str[1]), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "g",     "set green points coordinates", OFFSET(comp_points_str[1]), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "blue",  "set blue points coordinates",  OFFSET(comp_points_str[2]), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "b",     "set blue points coordinates",  OFFSET(comp_points_str[2]), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(curves);

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

#define B 0 /* sub  diagonal (below main) */
#define M 1 /* main diagonal (center) */
#define A 2 /* sup  diagonal (above main) */

    /* left side of the polynomials into a tridiagonal matrix. */
    matrix[0][M] = matrix[n - 1][M] = 1;
    for (i = 1; i < n - 1; i++) {
        matrix[i][B] = h[i-1];
        matrix[i][M] = 2 * (h[i-1] + h[i]);
        matrix[i][A] = h[i];
    }

    /* tridiagonal solving of the linear system */
    for (i = 1; i < n; i++) {
        double den = matrix[i][M] - matrix[i][B] * matrix[i-1][A];
        double k = den ? 1./den : 1.;
        matrix[i][A] *= k;
        r[i] = (r[i] - matrix[i][B] * r[i - 1]) * k;
    }
    for (i = n - 2; i >= 0; i--)
        r[i] = r[i] - matrix[i][A] * r[i + 1];

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

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    int i, j, ret;
    CurvesContext *curves = ctx->priv;
    struct keypoint *comp_points[NB_COMP] = {0};

    curves->class = &curves_class;
    av_opt_set_defaults(curves);

    if ((ret = av_set_options_string(curves, args, "=", ":")) < 0)
        return ret;

    for (i = 0; i < NB_COMP; i++) {
        ret = parse_points_str(ctx, comp_points + i, curves->comp_points_str[i]);
        if (ret < 0)
            return ret;
        ret = interpolate(ctx, curves->graph[i], comp_points[i]);
        if (ret < 0)
            return ret;
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

    for (i = 0; i < NB_COMP; i++) {
        struct keypoint *point = comp_points[i];
        while (point) {
            struct keypoint *next = point->next;
            av_free(point);
            point = next;
        }
    }

    av_opt_free(curves);
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {AV_PIX_FMT_RGB24, AV_PIX_FMT_NONE};
    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    int x, y, i, direct = 0;
    AVFilterContext *ctx = inlink->dst;
    CurvesContext *curves = ctx->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFrame *out;
    uint8_t *dst;
    const uint8_t *src;

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
        uint8_t *dstp = dst;
        const uint8_t *srcp = src;

        for (x = 0; x < inlink->w; x++)
            for (i = 0; i < NB_COMP; i++, dstp++, srcp++)
                *dstp = curves->graph[i][*srcp];
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
};
