/*
 * Copyright (c) 2002 Jindrich Makovicka <makovick@gmail.com>
 * Copyright (c) 2011 Stefano Sabatini
 * Copyright (c) 2013, 2015 Jean Delvare <jdelvare@suse.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @file
 * A very simple tv station logo remover
 * Originally imported from MPlayer libmpcodecs/vf_delogo.c,
 * the algorithm was later improved.
 */

#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/eval.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
static const char * const var_names[] = {
    "x",
    "y",
    "w",
    "h",
    "n",            ///< number of frame
    "t",            ///< timestamp expressed in seconds
    NULL
};

enum var_name {
    VAR_X,
    VAR_Y,
    VAR_W,
    VAR_H,
    VAR_N,
    VAR_T,
    VAR_VARS_NB
};

static int set_expr(AVExpr **pexpr, const char *expr, const char *option, void *log_ctx)
{
    int ret;
    AVExpr *old = NULL;

    if (*pexpr)
        old = *pexpr;
    ret = av_expr_parse(pexpr, expr, var_names, NULL, NULL, NULL, NULL, 0, log_ctx);
    if (ret < 0) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Error when parsing the expression '%s' for %s\n",
               expr, option);
        *pexpr = old;
        return ret;
    }

    av_expr_free(old);
    return 0;
}


/**
 * Apply a simple delogo algorithm to the image in src and put the
 * result in dst.
 *
 * The algorithm is only applied to the region specified by the logo
 * parameters.
 *
 * @param w      width of the input image
 * @param h      height of the input image
 * @param logo_x x coordinate of the top left corner of the logo region
 * @param logo_y y coordinate of the top left corner of the logo region
 * @param logo_w width of the logo
 * @param logo_h height of the logo
 * @param band   the size of the band around the processed area
 * @param show   show a rectangle around the processed area, useful for
 *               parameters tweaking
 * @param direct if non-zero perform in-place processing
 */
static void apply_delogo(uint8_t *dst, int dst_linesize,
                         uint8_t *src, int src_linesize,
                         int w, int h, AVRational sar,
                         int logo_x, int logo_y, int logo_w, int logo_h,
                         unsigned int band, int show, int direct)
{
    int x, y;
    uint64_t interp, weightl, weightr, weightt, weightb, weight;
    uint8_t *xdst, *xsrc;

    uint8_t *topleft, *botleft, *topright;
    unsigned int left_sample, right_sample;
    int xclipl, xclipr, yclipt, yclipb;
    int logo_x1, logo_x2, logo_y1, logo_y2;

    xclipl = FFMAX(-logo_x, 0);
    xclipr = FFMAX(logo_x+logo_w-w, 0);
    yclipt = FFMAX(-logo_y, 0);
    yclipb = FFMAX(logo_y+logo_h-h, 0);

    logo_x1 = logo_x + xclipl;
    logo_x2 = logo_x + logo_w - xclipr - 1;
    logo_y1 = logo_y + yclipt;
    logo_y2 = logo_y + logo_h - yclipb - 1;

    topleft  = src+logo_y1 * src_linesize+logo_x1;
    topright = src+logo_y1 * src_linesize+logo_x2;
    botleft  = src+logo_y2 * src_linesize+logo_x1;

    if (!direct)
        av_image_copy_plane(dst, dst_linesize, src, src_linesize, w, h);

    dst += (logo_y1 + 1) * dst_linesize;
    src += (logo_y1 + 1) * src_linesize;

    for (y = logo_y1+1; y < logo_y2; y++) {
        left_sample = topleft[src_linesize*(y-logo_y1)]   +
                      topleft[src_linesize*(y-logo_y1-1)] +
                      topleft[src_linesize*(y-logo_y1+1)];
        right_sample = topright[src_linesize*(y-logo_y1)]   +
                       topright[src_linesize*(y-logo_y1-1)] +
                       topright[src_linesize*(y-logo_y1+1)];

        for (x = logo_x1+1,
             xdst = dst+logo_x1+1,
             xsrc = src+logo_x1+1; x < logo_x2; x++, xdst++, xsrc++) {

            if (show && (y == logo_y1+1 || y == logo_y2-1 ||
                         x == logo_x1+1 || x == logo_x2-1)) {
                *xdst = 0;
                continue;
            }

            /* Weighted interpolation based on relative distances, taking SAR into account */
            weightl = (uint64_t)              (logo_x2-x) * (y-logo_y1) * (logo_y2-y) * sar.den;
            weightr = (uint64_t)(x-logo_x1)               * (y-logo_y1) * (logo_y2-y) * sar.den;
            weightt = (uint64_t)(x-logo_x1) * (logo_x2-x)               * (logo_y2-y) * sar.num;
            weightb = (uint64_t)(x-logo_x1) * (logo_x2-x) * (y-logo_y1)               * sar.num;

            interp =
                left_sample * weightl
                +
                right_sample * weightr
                +
                (topleft[x-logo_x1]    +
                 topleft[x-logo_x1-1]  +
                 topleft[x-logo_x1+1]) * weightt
                +
                (botleft[x-logo_x1]    +
                 botleft[x-logo_x1-1]  +
                 botleft[x-logo_x1+1]) * weightb;
            weight = (weightl + weightr + weightt + weightb) * 3U;
            interp = (interp + (weight >> 1)) / weight;

            if (y >= logo_y+band && y < logo_y+logo_h-band &&
                x >= logo_x+band && x < logo_x+logo_w-band) {
                *xdst = interp;
            } else {
                unsigned dist = 0;

                if      (x < logo_x+band)
                    dist = FFMAX(dist, logo_x-x+band);
                else if (x >= logo_x+logo_w-band)
                    dist = FFMAX(dist, x-(logo_x+logo_w-1-band));

                if      (y < logo_y+band)
                    dist = FFMAX(dist, logo_y-y+band);
                else if (y >= logo_y+logo_h-band)
                    dist = FFMAX(dist, y-(logo_y+logo_h-1-band));

                *xdst = (*xsrc*dist + interp*(band-dist))/band;
            }
        }

        dst += dst_linesize;
        src += src_linesize;
    }
}

typedef struct DelogoContext {
    const AVClass *class;
    int x, y, w, h, band, show;
    char *x_expr, *y_expr, *w_expr, *h_expr;
    AVExpr *x_pexpr, *y_pexpr, *w_pexpr, *h_pexpr;
    double var_values[VAR_VARS_NB];
}  DelogoContext;

#define OFFSET(x) offsetof(DelogoContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption delogo_options[]= {
    { "x",    "set logo x position",       OFFSET(x_expr),    AV_OPT_TYPE_STRING, { .str = "-1" }, 0, 0, FLAGS },
    { "y",    "set logo y position",       OFFSET(y_expr),    AV_OPT_TYPE_STRING, { .str = "-1" }, 0, 0, FLAGS },
    { "w",    "set logo width",            OFFSET(w_expr),    AV_OPT_TYPE_STRING, { .str = "-1" }, 0, 0, FLAGS },
    { "h",    "set logo height",           OFFSET(h_expr),    AV_OPT_TYPE_STRING, { .str = "-1" }, 0, 0, FLAGS },
    { "show", "show delogo area",          OFFSET(show),      AV_OPT_TYPE_BOOL,   { .i64 =  0 },   0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(delogo);
static av_cold void uninit(AVFilterContext *ctx)
{
    DelogoContext *s = ctx->priv;

    av_expr_free(s->x_pexpr);    s->x_pexpr = NULL;
    av_expr_free(s->y_pexpr);    s->y_pexpr = NULL;
    av_expr_free(s->w_pexpr);    s->w_pexpr = NULL;
    av_expr_free(s->h_pexpr);    s->h_pexpr = NULL;
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,  AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_YUVA420P, AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_NONE
};

static av_cold int init(AVFilterContext *ctx)
{
    DelogoContext *s = ctx->priv;
    int ret = 0;

    if ((ret = set_expr(&s->x_pexpr, s->x_expr, "x", ctx)) < 0 ||
        (ret = set_expr(&s->y_pexpr, s->y_expr, "y", ctx)) < 0 ||
        (ret = set_expr(&s->w_pexpr, s->w_expr, "w", ctx)) < 0 ||
        (ret = set_expr(&s->h_pexpr, s->h_expr, "h", ctx)) < 0 )
        return ret;

    s->x = av_expr_eval(s->x_pexpr, s->var_values, s);
    s->y = av_expr_eval(s->y_pexpr, s->var_values, s);
    s->w = av_expr_eval(s->w_pexpr, s->var_values, s);
    s->h = av_expr_eval(s->h_pexpr, s->var_values, s);

#define CHECK_UNSET_OPT(opt)                                            \
    if (s->opt == -1) {                                            \
        av_log(s, AV_LOG_ERROR, "Option %s was not set.\n", #opt); \
        return AVERROR(EINVAL);                                         \
    }
    CHECK_UNSET_OPT(x);
    CHECK_UNSET_OPT(y);
    CHECK_UNSET_OPT(w);
    CHECK_UNSET_OPT(h);

    s->band = 1;

    av_log(ctx, AV_LOG_VERBOSE, "x:%d y:%d, w:%d h:%d band:%d show:%d\n",
           s->x, s->y, s->w, s->h, s->band, s->show);

    s->w += s->band*2;
    s->h += s->band*2;
    s->x -= s->band;
    s->y -= s->band;

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    DelogoContext *s = inlink->dst->priv;

    /* Check whether the logo area fits in the frame */
    if (s->x + (s->band - 1) < 0 || s->x + s->w - (s->band*2 - 2) > inlink->w ||
        s->y + (s->band - 1) < 0 || s->y + s->h - (s->band*2 - 2) > inlink->h) {
        av_log(s, AV_LOG_ERROR, "Logo area is outside of the frame.\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    DelogoContext *s = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    AVFrame *out;
    int hsub0 = desc->log2_chroma_w;
    int vsub0 = desc->log2_chroma_h;
    int direct = 0;
    int plane;
    AVRational sar;
    int ret;

    s->var_values[VAR_N] = inlink->frame_count_out;
    s->var_values[VAR_T] = TS2T(in->pts, inlink->time_base);
    s->x = av_expr_eval(s->x_pexpr, s->var_values, s);
    s->y = av_expr_eval(s->y_pexpr, s->var_values, s);
    s->w = av_expr_eval(s->w_pexpr, s->var_values, s);
    s->h = av_expr_eval(s->h_pexpr, s->var_values, s);

    if (s->x + (s->band - 1) <= 0 || s->x + s->w - (s->band*2 - 2) > inlink->w ||
        s->y + (s->band - 1) <= 0 || s->y + s->h - (s->band*2 - 2) > inlink->h) {
        av_log(s, AV_LOG_WARNING, "Logo area is outside of the frame,"
               " auto set the area inside of the frame\n");
    }

    if (s->x + (s->band - 1) <= 0)
        s->x = 1 + s->band;
    if (s->y + (s->band - 1) <= 0)
        s->y = 1 + s->band;
    if (s->x + s->w - (s->band*2 - 2) > inlink->w)
        s->w = inlink->w - s->x - (s->band*2 - 2);
    if (s->y + s->h - (s->band*2 - 2) > inlink->h)
        s->h = inlink->h - s->y - (s->band*2 - 2);

    ret = config_input(inlink);
    if (ret < 0) {
        av_frame_free(&in);
        return ret;
    }

    s->w += s->band*2;
    s->h += s->band*2;
    s->x -= s->band;
    s->y -= s->band;

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

    sar = in->sample_aspect_ratio;
    /* Assume square pixels if SAR is unknown */
    if (!sar.num)
        sar.num = sar.den = 1;

    for (plane = 0; plane < desc->nb_components; plane++) {
        int hsub = plane == 1 || plane == 2 ? hsub0 : 0;
        int vsub = plane == 1 || plane == 2 ? vsub0 : 0;

        apply_delogo(out->data[plane], out->linesize[plane],
                     in ->data[plane], in ->linesize[plane],
                     AV_CEIL_RSHIFT(inlink->w, hsub),
                     AV_CEIL_RSHIFT(inlink->h, vsub),
                     sar, s->x>>hsub, s->y>>vsub,
                     /* Up and left borders were rounded down, inject lost bits
                      * into width and height to avoid error accumulation */
                     AV_CEIL_RSHIFT(s->w + (s->x & ((1<<hsub)-1)), hsub),
                     AV_CEIL_RSHIFT(s->h + (s->y & ((1<<vsub)-1)), vsub),
                     s->band>>FFMIN(hsub, vsub),
                     s->show, direct);
    }

    if (!direct)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static const AVFilterPad avfilter_vf_delogo_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

static const AVFilterPad avfilter_vf_delogo_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

const AVFilter ff_vf_delogo = {
    .name          = "delogo",
    .description   = NULL_IF_CONFIG_SMALL("Remove logo from input video."),
    .priv_size     = sizeof(DelogoContext),
    .priv_class    = &delogo_class,
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(avfilter_vf_delogo_inputs),
    FILTER_OUTPUTS(avfilter_vf_delogo_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
