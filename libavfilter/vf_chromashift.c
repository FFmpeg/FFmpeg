/*
 * Copyright (c) 2018 Paul B Mahol
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

#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "filters.h"
#include "video.h"

typedef struct ChromaShiftContext {
    const AVClass *class;
    int cbh, cbv;
    int crh, crv;
    int rh, rv;
    int gh, gv;
    int bh, bv;
    int ah, av;
    int edge;

    int nb_planes;
    int depth;
    int height[4];
    int width[4];
    int linesize[4];

    AVFrame *in;

    int is_rgbashift;
    int (*filter_slice[2])(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} ChromaShiftContext;

#define DEFINE_SMEAR(depth, type, div)                                                    \
static int smear_slice ## depth(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)  \
{                                                                                         \
    ChromaShiftContext *s = ctx->priv;                                                    \
    AVFrame *in = s->in;                                                                  \
    AVFrame *out = arg;                                                                   \
    const int sulinesize = in->linesize[1] / div;                                         \
    const int svlinesize = in->linesize[2] / div;                                         \
    const int ulinesize = out->linesize[1] / div;                                         \
    const int vlinesize = out->linesize[2] / div;                                         \
    const int cbh = s->cbh;                                                               \
    const int cbv = s->cbv;                                                               \
    const int crh = s->crh;                                                               \
    const int crv = s->crv;                                                               \
    const int h = s->height[1];                                                           \
    const int w = s->width[1];                                                            \
    const int slice_start = (h * jobnr) / nb_jobs;                                        \
    const int slice_end = (h * (jobnr+1)) / nb_jobs;                                      \
    const type *su = (const type *)in->data[1];                                           \
    const type *sv = (const type *)in->data[2];                                           \
    type *du = (type *)out->data[1] + slice_start * ulinesize;                            \
    type *dv = (type *)out->data[2] + slice_start * vlinesize;                            \
                                                                                          \
    for (int y = slice_start; y < slice_end; y++) {                                       \
        const int duy = av_clip(y - cbv, 0, h-1) * sulinesize;                            \
        const int dvy = av_clip(y - crv, 0, h-1) * svlinesize;                            \
                                                                                          \
        for (int x = 0; x < w; x++) {                                                     \
            du[x] = su[av_clip(x - cbh, 0, w - 1) + duy];                                 \
            dv[x] = sv[av_clip(x - crh, 0, w - 1) + dvy];                                 \
        }                                                                                 \
                                                                                          \
        du += ulinesize;                                                                  \
        dv += vlinesize;                                                                  \
    }                                                                                     \
                                                                                          \
    return 0;                                                                             \
}

DEFINE_SMEAR(8, uint8_t, 1)
DEFINE_SMEAR(16, uint16_t, 2)

#define DEFINE_WRAP(depth, type, div)                                                     \
static int wrap_slice ## depth(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)   \
{                                                                                         \
    ChromaShiftContext *s = ctx->priv;                                                    \
    AVFrame *in = s->in;                                                                  \
    AVFrame *out = arg;                                                                   \
    const int sulinesize = in->linesize[1] / div;                                         \
    const int svlinesize = in->linesize[2] / div;                                         \
    const int ulinesize = out->linesize[1] / div;                                         \
    const int vlinesize = out->linesize[2] / div;                                         \
    const int cbh = s->cbh;                                                               \
    const int cbv = s->cbv;                                                               \
    const int crh = s->crh;                                                               \
    const int crv = s->crv;                                                               \
    const int h = s->height[1];                                                           \
    const int w = s->width[1];                                                            \
    const int slice_start = (h * jobnr) / nb_jobs;                                        \
    const int slice_end = (h * (jobnr+1)) / nb_jobs;                                      \
    const type *su = (const type *)in->data[1];                                           \
    const type *sv = (const type *)in->data[2];                                           \
    type *du = (type *)out->data[1] + slice_start * ulinesize;                            \
    type *dv = (type *)out->data[2] + slice_start * vlinesize;                            \
                                                                                          \
    for (int y = slice_start; y < slice_end; y++) {                                       \
        int uy = (y - cbv) % h;                                                           \
        int vy = (y - crv) % h;                                                           \
                                                                                          \
        if (uy < 0)                                                                       \
            uy += h;                                                                      \
        if (vy < 0)                                                                       \
            vy += h;                                                                      \
                                                                                          \
        for (int x = 0; x < w; x++) {                                                     \
            int ux = (x - cbh) % w;                                                       \
            int vx = (x - crh) % w;                                                       \
                                                                                          \
            if (ux < 0)                                                                   \
                ux += w;                                                                  \
            if (vx < 0)                                                                   \
                vx += w;                                                                  \
                                                                                          \
            du[x] = su[ux + uy * sulinesize];                                             \
            dv[x] = sv[vx + vy * svlinesize];                                             \
        }                                                                                 \
                                                                                          \
        du += ulinesize;                                                                  \
        dv += vlinesize;                                                                  \
    }                                                                                     \
                                                                                          \
    return 0;                                                                             \
}

DEFINE_WRAP(8, uint8_t, 1)
DEFINE_WRAP(16, uint16_t, 2)

#define DEFINE_RGBASMEAR(depth, type, div)                                                    \
static int rgbasmear_slice ## depth(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)  \
{                                                                                         \
    ChromaShiftContext *s = ctx->priv;                                                    \
    AVFrame *in = s->in;                                                                  \
    AVFrame *out = arg;                                                                   \
    const int srlinesize = in->linesize[2] / div;                                         \
    const int sglinesize = in->linesize[0] / div;                                         \
    const int sblinesize = in->linesize[1] / div;                                         \
    const int salinesize = in->linesize[3] / div;                                         \
    const int rlinesize = out->linesize[2] / div;                                         \
    const int glinesize = out->linesize[0] / div;                                         \
    const int blinesize = out->linesize[1] / div;                                         \
    const int alinesize = out->linesize[3] / div;                                         \
    const int rh = s->rh;                                                                 \
    const int rv = s->rv;                                                                 \
    const int gh = s->gh;                                                                 \
    const int gv = s->gv;                                                                 \
    const int bh = s->bh;                                                                 \
    const int bv = s->bv;                                                                 \
    const int ah = s->ah;                                                                 \
    const int av = s->av;                                                                 \
    const int h = s->height[1];                                                           \
    const int w = s->width[1];                                                            \
    const int slice_start = (h * jobnr) / nb_jobs;                                        \
    const int slice_end = (h * (jobnr+1)) / nb_jobs;                                      \
    const type *sr = (const type *)in->data[2];                                           \
    const type *sg = (const type *)in->data[0];                                           \
    const type *sb = (const type *)in->data[1];                                           \
    const type *sa = (const type *)in->data[3];                                           \
    type *dr = (type *)out->data[2] + slice_start * rlinesize;                            \
    type *dg = (type *)out->data[0] + slice_start * glinesize;                            \
    type *db = (type *)out->data[1] + slice_start * blinesize;                            \
    type *da = (type *)out->data[3] + slice_start * alinesize;                            \
                                                                                          \
    for (int y = slice_start; y < slice_end; y++) {                                       \
        const int ry = av_clip(y - rv, 0, h-1) * srlinesize;                              \
        const int gy = av_clip(y - gv, 0, h-1) * sglinesize;                              \
        const int by = av_clip(y - bv, 0, h-1) * sblinesize;                              \
        int ay;                                                                           \
                                                                                          \
        for (int x = 0; x < w; x++) {                                                     \
            dr[x] = sr[av_clip(x - rh, 0, w - 1) + ry];                                   \
            dg[x] = sg[av_clip(x - gh, 0, w - 1) + gy];                                   \
            db[x] = sb[av_clip(x - bh, 0, w - 1) + by];                                   \
        }                                                                                 \
                                                                                          \
        dr += rlinesize;                                                                  \
        dg += glinesize;                                                                  \
        db += blinesize;                                                                  \
                                                                                          \
        if (s->nb_planes < 4)                                                             \
            continue;                                                                     \
        ay = av_clip(y - av, 0, h-1) * salinesize;                                        \
        for (int x = 0; x < w; x++) {                                                     \
            da[x] = sa[av_clip(x - ah, 0, w - 1) + ay];                                   \
        }                                                                                 \
                                                                                          \
        da += alinesize;                                                                  \
    }                                                                                     \
                                                                                          \
    return 0;                                                                             \
}

DEFINE_RGBASMEAR(8, uint8_t, 1)
DEFINE_RGBASMEAR(16, uint16_t, 2)

#define DEFINE_RGBAWRAP(depth, type, div)                                                     \
static int rgbawrap_slice ## depth(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)   \
{                                                                                         \
    ChromaShiftContext *s = ctx->priv;                                                    \
    AVFrame *in = s->in;                                                                  \
    AVFrame *out = arg;                                                                   \
    const int srlinesize = in->linesize[2] / div;                                         \
    const int sglinesize = in->linesize[0] / div;                                         \
    const int sblinesize = in->linesize[1] / div;                                         \
    const int salinesize = in->linesize[3] / div;                                         \
    const int rlinesize = out->linesize[2] / div;                                         \
    const int glinesize = out->linesize[0] / div;                                         \
    const int blinesize = out->linesize[1] / div;                                         \
    const int alinesize = out->linesize[3] / div;                                         \
    const int rh = s->rh;                                                                 \
    const int rv = s->rv;                                                                 \
    const int gh = s->gh;                                                                 \
    const int gv = s->gv;                                                                 \
    const int bh = s->bh;                                                                 \
    const int bv = s->bv;                                                                 \
    const int ah = s->ah;                                                                 \
    const int av = s->av;                                                                 \
    const int h = s->height[1];                                                           \
    const int w = s->width[1];                                                            \
    const int slice_start = (h * jobnr) / nb_jobs;                                        \
    const int slice_end = (h * (jobnr+1)) / nb_jobs;                                      \
    const type *sr = (const type *)in->data[2];                                           \
    const type *sg = (const type *)in->data[0];                                           \
    const type *sb = (const type *)in->data[1];                                           \
    const type *sa = (const type *)in->data[3];                                           \
    type *dr = (type *)out->data[2] + slice_start * rlinesize;                            \
    type *dg = (type *)out->data[0] + slice_start * glinesize;                            \
    type *db = (type *)out->data[1] + slice_start * blinesize;                            \
    type *da = (type *)out->data[3] + slice_start * alinesize;                            \
                                                                                          \
    for (int y = slice_start; y < slice_end; y++) {                                       \
        int ry = (y - rv) % h;                                                            \
        int gy = (y - gv) % h;                                                            \
        int by = (y - bv) % h;                                                            \
                                                                                          \
        if (ry < 0)                                                                       \
            ry += h;                                                                      \
        if (gy < 0)                                                                       \
            gy += h;                                                                      \
        if (by < 0)                                                                       \
            by += h;                                                                      \
                                                                                          \
        for (int x = 0; x < w; x++) {                                                     \
            int rx = (x - rh) % w;                                                        \
            int gx = (x - gh) % w;                                                        \
            int bx = (x - bh) % w;                                                        \
                                                                                          \
            if (rx < 0)                                                                   \
                rx += w;                                                                  \
            if (gx < 0)                                                                   \
                gx += w;                                                                  \
            if (bx < 0)                                                                   \
                bx += w;                                                                  \
            dr[x] = sr[rx + ry * srlinesize];                                             \
            dg[x] = sg[gx + gy * sglinesize];                                             \
            db[x] = sb[bx + by * sblinesize];                                             \
        }                                                                                 \
                                                                                          \
        dr += rlinesize;                                                                  \
        dg += glinesize;                                                                  \
        db += blinesize;                                                                  \
                                                                                          \
        if (s->nb_planes < 4)                                                             \
            continue;                                                                     \
        for (int x = 0; x < w; x++) {                                                     \
            int ax = (x - ah) % w;                                                        \
            int ay = (x - av) % h;                                                        \
                                                                                          \
            if (ax < 0)                                                                   \
                ax += w;                                                                  \
            if (ay < 0)                                                                   \
                ay += h;                                                                  \
            da[x] = sa[ax + ay * salinesize];                                             \
        }                                                                                 \
                                                                                          \
        da += alinesize;                                                                  \
    }                                                                                     \
                                                                                          \
    return 0;                                                                             \
}

DEFINE_RGBAWRAP(8, uint8_t, 1)
DEFINE_RGBAWRAP(16, uint16_t, 2)

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    ChromaShiftContext *s = ctx->priv;
    AVFrame *out;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    s->in = in;
    if (!s->is_rgbashift) {
        av_image_copy_plane(out->data[0],
                            out->linesize[0],
                            in->data[0], in->linesize[0],
                            s->linesize[0], s->height[0]);
    }
    ff_filter_execute(ctx, s->filter_slice[s->edge], out, NULL,
                      FFMIN3(s->height[1],
                             s->height[2],
                             ff_filter_get_nb_threads(ctx)));
    s->in = NULL;
    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ChromaShiftContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    s->is_rgbashift = !strcmp(ctx->filter->name, "rgbashift");
    s->depth = desc->comp[0].depth;
    s->nb_planes = desc->nb_components;
    if (s->is_rgbashift) {
        s->filter_slice[1] = s->depth > 8 ? rgbawrap_slice16 : rgbawrap_slice8;
        s->filter_slice[0] = s->depth > 8 ? rgbasmear_slice16 : rgbasmear_slice8;
    } else {
        s->filter_slice[1] = s->depth > 8 ? wrap_slice16 : wrap_slice8;
        s->filter_slice[0] = s->depth > 8 ? smear_slice16 : smear_slice8;
    }
    s->height[1] = s->height[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->height[0] = s->height[3] = inlink->h;
    s->width[1] = s->width[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->width[0] = s->width[3] = inlink->w;

    return av_image_fill_linesizes(s->linesize, inlink->format, inlink->w);
}

#define OFFSET(x) offsetof(ChromaShiftContext, x)
#define VFR AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption chromashift_options[] = {
    { "cbh", "shift chroma-blue horizontally", OFFSET(cbh),  AV_OPT_TYPE_INT,   {.i64=0}, -255, 255, .flags = VFR },
    { "cbv", "shift chroma-blue vertically",   OFFSET(cbv),  AV_OPT_TYPE_INT,   {.i64=0}, -255, 255, .flags = VFR },
    { "crh", "shift chroma-red horizontally",  OFFSET(crh),  AV_OPT_TYPE_INT,   {.i64=0}, -255, 255, .flags = VFR },
    { "crv", "shift chroma-red vertically",    OFFSET(crv),  AV_OPT_TYPE_INT,   {.i64=0}, -255, 255, .flags = VFR },
    { "edge", "set edge operation",            OFFSET(edge), AV_OPT_TYPE_INT,   {.i64=0},    0,   1, .flags = VFR, .unit = "edge" },
    { "smear",                              0,            0, AV_OPT_TYPE_CONST, {.i64=0},    0,   0, .flags = VFR, .unit = "edge" },
    { "wrap",                               0,            0, AV_OPT_TYPE_CONST, {.i64=1},    0,   0, .flags = VFR, .unit = "edge" },
    { NULL },
};

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

static const enum AVPixelFormat yuv_pix_fmts[] = {
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ422P,AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ411P,
    AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUV440P10,
    AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
    AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV440P12,
    AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA444P12,
    AV_PIX_FMT_YUV444P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV420P14,
    AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_NONE
};

AVFILTER_DEFINE_CLASS(chromashift);

const AVFilter ff_vf_chromashift = {
    .name          = "chromashift",
    .description   = NULL_IF_CONFIG_SMALL("Shift chroma."),
    .priv_size     = sizeof(ChromaShiftContext),
    .priv_class    = &chromashift_class,
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_INPUTS(inputs),
    FILTER_PIXFMTS_ARRAY(yuv_pix_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};

static const enum AVPixelFormat rgb_pix_fmts[] = {
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRP9,
    AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRP12,
    AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
    AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,
    AV_PIX_FMT_NONE
};

static const AVOption rgbashift_options[] = {
    { "rh", "shift red horizontally",   OFFSET(rh),   AV_OPT_TYPE_INT,   {.i64=0}, -255, 255, .flags = VFR },
    { "rv", "shift red vertically",     OFFSET(rv),   AV_OPT_TYPE_INT,   {.i64=0}, -255, 255, .flags = VFR },
    { "gh", "shift green horizontally", OFFSET(gh),   AV_OPT_TYPE_INT,   {.i64=0}, -255, 255, .flags = VFR },
    { "gv", "shift green vertically",   OFFSET(gv),   AV_OPT_TYPE_INT,   {.i64=0}, -255, 255, .flags = VFR },
    { "bh", "shift blue horizontally",  OFFSET(bh),   AV_OPT_TYPE_INT,   {.i64=0}, -255, 255, .flags = VFR },
    { "bv", "shift blue vertically",    OFFSET(bv),   AV_OPT_TYPE_INT,   {.i64=0}, -255, 255, .flags = VFR },
    { "ah", "shift alpha horizontally", OFFSET(ah),   AV_OPT_TYPE_INT,   {.i64=0}, -255, 255, .flags = VFR },
    { "av", "shift alpha vertically",   OFFSET(av),   AV_OPT_TYPE_INT,   {.i64=0}, -255, 255, .flags = VFR },
    { "edge", "set edge operation",     OFFSET(edge), AV_OPT_TYPE_INT,   {.i64=0},    0,   1, .flags = VFR, .unit = "edge" },
    { "smear",                          0,         0, AV_OPT_TYPE_CONST, {.i64=0},    0,   0, .flags = VFR, .unit = "edge" },
    { "wrap",                           0,         0, AV_OPT_TYPE_CONST, {.i64=1},    0,   0, .flags = VFR, .unit = "edge" },
    { NULL },
};

AVFILTER_DEFINE_CLASS(rgbashift);

const AVFilter ff_vf_rgbashift = {
    .name          = "rgbashift",
    .description   = NULL_IF_CONFIG_SMALL("Shift RGBA."),
    .priv_size     = sizeof(ChromaShiftContext),
    .priv_class    = &rgbashift_class,
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_INPUTS(inputs),
    FILTER_PIXFMTS_ARRAY(rgb_pix_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
