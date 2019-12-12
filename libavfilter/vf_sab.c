/*
 * Copyright (c) 2002 Michael Niedermayer <michaelni@gmx.at>
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
 * Shape Adaptive Blur filter, ported from MPlayer libmpcodecs/vf_sab.c
 */

#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libswscale/swscale.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"

typedef struct FilterParam {
    float radius;
    float pre_filter_radius;
    float strength;
    float quality;
    struct SwsContext *pre_filter_context;
    uint8_t *pre_filter_buf;
    int pre_filter_linesize;
    int dist_width;
    int dist_linesize;
    int *dist_coeff;
#define COLOR_DIFF_COEFF_SIZE 512
    int color_diff_coeff[COLOR_DIFF_COEFF_SIZE];
} FilterParam;

typedef struct SabContext {
    const AVClass *class;
    FilterParam  luma;
    FilterParam  chroma;
    int          hsub;
    int          vsub;
    unsigned int sws_flags;
} SabContext;

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_NONE
    };
    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

#define RADIUS_MIN 0.1
#define RADIUS_MAX 4.0

#define PRE_FILTER_RADIUS_MIN 0.1
#define PRE_FILTER_RADIUS_MAX 2.0

#define STRENGTH_MIN 0.1
#define STRENGTH_MAX 100.0

#define OFFSET(x) offsetof(SabContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption sab_options[] = {
    { "luma_radius",            "set luma radius", OFFSET(luma.radius), AV_OPT_TYPE_FLOAT, {.dbl=1.0}, RADIUS_MIN, RADIUS_MAX, .flags=FLAGS },
    { "lr"         ,            "set luma radius", OFFSET(luma.radius), AV_OPT_TYPE_FLOAT, {.dbl=1.0}, RADIUS_MIN, RADIUS_MAX, .flags=FLAGS },
    { "luma_pre_filter_radius", "set luma pre-filter radius", OFFSET(luma.pre_filter_radius), AV_OPT_TYPE_FLOAT, {.dbl=1.0}, PRE_FILTER_RADIUS_MIN, PRE_FILTER_RADIUS_MAX, .flags=FLAGS },
    { "lpfr",                   "set luma pre-filter radius", OFFSET(luma.pre_filter_radius), AV_OPT_TYPE_FLOAT, {.dbl=1.0}, PRE_FILTER_RADIUS_MIN, PRE_FILTER_RADIUS_MAX, .flags=FLAGS },
    { "luma_strength",          "set luma strength", OFFSET(luma.strength), AV_OPT_TYPE_FLOAT, {.dbl=1.0}, STRENGTH_MIN, STRENGTH_MAX, .flags=FLAGS },
    { "ls",                     "set luma strength", OFFSET(luma.strength), AV_OPT_TYPE_FLOAT, {.dbl=1.0}, STRENGTH_MIN, STRENGTH_MAX, .flags=FLAGS },

    { "chroma_radius",            "set chroma radius", OFFSET(chroma.radius), AV_OPT_TYPE_FLOAT, {.dbl=RADIUS_MIN-1}, RADIUS_MIN-1, RADIUS_MAX, .flags=FLAGS },
    { "cr",                       "set chroma radius", OFFSET(chroma.radius), AV_OPT_TYPE_FLOAT, {.dbl=RADIUS_MIN-1}, RADIUS_MIN-1, RADIUS_MAX, .flags=FLAGS },
    { "chroma_pre_filter_radius", "set chroma pre-filter radius",  OFFSET(chroma.pre_filter_radius), AV_OPT_TYPE_FLOAT, {.dbl=PRE_FILTER_RADIUS_MIN-1},
                                  PRE_FILTER_RADIUS_MIN-1, PRE_FILTER_RADIUS_MAX, .flags=FLAGS },
    { "cpfr",                     "set chroma pre-filter radius",  OFFSET(chroma.pre_filter_radius), AV_OPT_TYPE_FLOAT, {.dbl=PRE_FILTER_RADIUS_MIN-1},
                                  PRE_FILTER_RADIUS_MIN-1, PRE_FILTER_RADIUS_MAX, .flags=FLAGS },
    { "chroma_strength",          "set chroma strength", OFFSET(chroma.strength), AV_OPT_TYPE_FLOAT, {.dbl=STRENGTH_MIN-1}, STRENGTH_MIN-1, STRENGTH_MAX, .flags=FLAGS },
    { "cs",                       "set chroma strength", OFFSET(chroma.strength), AV_OPT_TYPE_FLOAT, {.dbl=STRENGTH_MIN-1}, STRENGTH_MIN-1, STRENGTH_MAX, .flags=FLAGS },

    { NULL }
};

AVFILTER_DEFINE_CLASS(sab);

static av_cold int init(AVFilterContext *ctx)
{
    SabContext *s = ctx->priv;

    /* make chroma default to luma values, if not explicitly set */
    if (s->chroma.radius < RADIUS_MIN)
        s->chroma.radius = s->luma.radius;
    if (s->chroma.pre_filter_radius < PRE_FILTER_RADIUS_MIN)
        s->chroma.pre_filter_radius = s->luma.pre_filter_radius;
    if (s->chroma.strength < STRENGTH_MIN)
        s->chroma.strength = s->luma.strength;

    s->luma.quality = s->chroma.quality = 3.0;
    s->sws_flags = SWS_POINT;

    av_log(ctx, AV_LOG_VERBOSE,
           "luma_radius:%f luma_pre_filter_radius::%f luma_strength:%f "
           "chroma_radius:%f chroma_pre_filter_radius:%f chroma_strength:%f\n",
           s->luma  .radius, s->luma  .pre_filter_radius, s->luma  .strength,
           s->chroma.radius, s->chroma.pre_filter_radius, s->chroma.strength);
    return 0;
}

static void close_filter_param(FilterParam *f)
{
    if (f->pre_filter_context) {
        sws_freeContext(f->pre_filter_context);
        f->pre_filter_context = NULL;
    }
    av_freep(&f->pre_filter_buf);
    av_freep(&f->dist_coeff);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SabContext *s = ctx->priv;

    close_filter_param(&s->luma);
    close_filter_param(&s->chroma);
}

static int open_filter_param(FilterParam *f, int width, int height, unsigned int sws_flags)
{
    SwsVector *vec;
    SwsFilter sws_f;
    int i, x, y;
    int linesize = FFALIGN(width, 8);

    f->pre_filter_buf = av_malloc(linesize * height);
    if (!f->pre_filter_buf)
        return AVERROR(ENOMEM);

    f->pre_filter_linesize = linesize;
    vec = sws_getGaussianVec(f->pre_filter_radius, f->quality);
    sws_f.lumH = sws_f.lumV = vec;
    sws_f.chrH = sws_f.chrV = NULL;
    f->pre_filter_context = sws_getContext(width, height, AV_PIX_FMT_GRAY8,
                                           width, height, AV_PIX_FMT_GRAY8,
                                           sws_flags, &sws_f, NULL, NULL);
    sws_freeVec(vec);

    vec = sws_getGaussianVec(f->strength, 5.0);
    for (i = 0; i < COLOR_DIFF_COEFF_SIZE; i++) {
        double d;
        int index = i-COLOR_DIFF_COEFF_SIZE/2 + vec->length/2;

        if (index < 0 || index >= vec->length) d = 0.0;
        else                                   d = vec->coeff[index];

        f->color_diff_coeff[i] = (int)(d/vec->coeff[vec->length/2]*(1<<12) + 0.5);
    }
    sws_freeVec(vec);

    vec = sws_getGaussianVec(f->radius, f->quality);
    f->dist_width    = vec->length;
    f->dist_linesize = FFALIGN(vec->length, 8);
    f->dist_coeff    = av_malloc_array(f->dist_width, f->dist_linesize * sizeof(*f->dist_coeff));
    if (!f->dist_coeff) {
        sws_freeVec(vec);
        return AVERROR(ENOMEM);
    }

    for (y = 0; y < vec->length; y++) {
        for (x = 0; x < vec->length; x++) {
            double d = vec->coeff[x] * vec->coeff[y];
            f->dist_coeff[x + y*f->dist_linesize] = (int)(d*(1<<10) + 0.5);
        }
    }
    sws_freeVec(vec);

    return 0;
}

static int config_props(AVFilterLink *inlink)
{
    SabContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int ret;

    s->hsub = desc->log2_chroma_w;
    s->vsub = desc->log2_chroma_h;

    close_filter_param(&s->luma);
    ret = open_filter_param(&s->luma, inlink->w, inlink->h, s->sws_flags);
    if (ret < 0)
        return ret;

    close_filter_param(&s->chroma);
    ret = open_filter_param(&s->chroma,
                            AV_CEIL_RSHIFT(inlink->w, s->hsub),
                            AV_CEIL_RSHIFT(inlink->h, s->vsub), s->sws_flags);
    return ret;
}

#define NB_PLANES 4

static void blur(uint8_t       *dst, const int dst_linesize,
                 const uint8_t *src, const int src_linesize,
                 const int w, const int h, FilterParam *fp)
{
    int x, y;
    FilterParam f = *fp;
    const int radius = f.dist_width/2;

    const uint8_t * const src2[NB_PLANES] = { src };
    int          src2_linesize[NB_PLANES] = { src_linesize };
    uint8_t     *dst2[NB_PLANES] = { f.pre_filter_buf };
    int dst2_linesize[NB_PLANES] = { f.pre_filter_linesize };

    sws_scale(f.pre_filter_context, src2, src2_linesize, 0, h, dst2, dst2_linesize);

#define UPDATE_FACTOR do {                                              \
        int factor;                                                     \
        factor = f.color_diff_coeff[COLOR_DIFF_COEFF_SIZE/2 + pre_val - \
                 f.pre_filter_buf[ix + iy*f.pre_filter_linesize]] * f.dist_coeff[dx + dy*f.dist_linesize]; \
        sum += src[ix + iy*src_linesize] * factor;                      \
        div += factor;                                                  \
    } while (0)

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int sum = 0;
            int div = 0;
            int dy;
            const int pre_val = f.pre_filter_buf[x + y*f.pre_filter_linesize];
            if (x >= radius && x < w - radius) {
                for (dy = 0; dy < radius*2 + 1; dy++) {
                    int dx;
                    int iy = y+dy - radius;
                    iy = avpriv_mirror(iy, h-1);

                    for (dx = 0; dx < radius*2 + 1; dx++) {
                        const int ix = x+dx - radius;
                        UPDATE_FACTOR;
                    }
                }
            } else {
                for (dy = 0; dy < radius*2+1; dy++) {
                    int dx;
                    int iy = y+dy - radius;
                    iy = avpriv_mirror(iy, h-1);

                    for (dx = 0; dx < radius*2 + 1; dx++) {
                        int ix = x+dx - radius;
                        ix = avpriv_mirror(ix, w-1);
                        UPDATE_FACTOR;
                    }
                }
            }
            dst[x + y*dst_linesize] = (sum + div/2) / div;
        }
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *inpic)
{
    SabContext  *s = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFrame *outpic;

    outpic = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!outpic) {
        av_frame_free(&inpic);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(outpic, inpic);

    blur(outpic->data[0], outpic->linesize[0], inpic->data[0],  inpic->linesize[0],
         inlink->w, inlink->h, &s->luma);
    if (inpic->data[2]) {
        int cw = AV_CEIL_RSHIFT(inlink->w, s->hsub);
        int ch = AV_CEIL_RSHIFT(inlink->h, s->vsub);
        blur(outpic->data[1], outpic->linesize[1], inpic->data[1], inpic->linesize[1], cw, ch, &s->chroma);
        blur(outpic->data[2], outpic->linesize[2], inpic->data[2], inpic->linesize[2], cw, ch, &s->chroma);
    }

    av_frame_free(&inpic);
    return ff_filter_frame(outlink, outpic);
}

static const AVFilterPad sab_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_props,
    },
    { NULL }
};

static const AVFilterPad sab_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_sab = {
    .name          = "sab",
    .description   = NULL_IF_CONFIG_SMALL("Apply shape adaptive blur."),
    .priv_size     = sizeof(SabContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = sab_inputs,
    .outputs       = sab_outputs,
    .priv_class    = &sab_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
