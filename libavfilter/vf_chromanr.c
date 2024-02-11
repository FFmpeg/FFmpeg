/*
 * Copyright (c) 2020 Paul B Mahol
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
#include "internal.h"
#include "video.h"

typedef struct ChromaNRContext {
    const AVClass *class;

    float threshold;
    float threshold_y;
    float threshold_u;
    float threshold_v;
    int distance;
    int thres;
    int thres_y;
    int thres_u;
    int thres_v;
    int sizew;
    int sizeh;
    int stepw;
    int steph;
    int depth;
    int chroma_w;
    int chroma_h;
    int nb_planes;
    int linesize[4];
    int planeheight[4];
    int planewidth[4];

    AVFrame *out;
    int (*filter_slice)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} ChromaNRContext;

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ411P,
    AV_PIX_FMT_YUV420P9,   AV_PIX_FMT_YUV422P9,   AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_YUV420P10,  AV_PIX_FMT_YUV422P10,  AV_PIX_FMT_YUV440P10, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV444P12,  AV_PIX_FMT_YUV422P12,  AV_PIX_FMT_YUV440P12, AV_PIX_FMT_YUV420P12,
    AV_PIX_FMT_YUV444P14,  AV_PIX_FMT_YUV422P14,  AV_PIX_FMT_YUV420P14,
    AV_PIX_FMT_YUV420P16,  AV_PIX_FMT_YUV422P16,  AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_YUVA420P9,  AV_PIX_FMT_YUVA422P9,  AV_PIX_FMT_YUVA444P9,
    AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
    AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA444P12,
    AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_NONE
};

#define MANHATTAN_DISTANCE(x, y, z) ((x) + (y) + (z))
#define EUCLIDEAN_DISTANCE(x, y, z) (sqrtf((x)*(x) + (y)*(y) + (z)*(z)))

#define FILTER_FUNC(distance, name, ctype, type, fun, extra)                             \
static int distance ## _slice##name(AVFilterContext *ctx, void *arg,                     \
                                    int jobnr, int nb_jobs)                              \
{                                                                                        \
    ChromaNRContext *s = ctx->priv;                                                      \
    AVFrame *in = arg;                                                                   \
    AVFrame *out = s->out;                                                               \
    const int in_ylinesize = in->linesize[0];                                            \
    const int in_ulinesize = in->linesize[1];                                            \
    const int in_vlinesize = in->linesize[2];                                            \
    const int out_ulinesize = out->linesize[1];                                          \
    const int out_vlinesize = out->linesize[2];                                          \
    const int chroma_w = s->chroma_w;                                                    \
    const int chroma_h = s->chroma_h;                                                    \
    const int stepw = s->stepw;                                                          \
    const int steph = s->steph;                                                          \
    const int sizew = s->sizew;                                                          \
    const int sizeh = s->sizeh;                                                          \
    const int thres = s->thres;                                                          \
    const int thres_y = s->thres_y;                                                      \
    const int thres_u = s->thres_u;                                                      \
    const int thres_v = s->thres_v;                                                      \
    const int h = s->planeheight[1];                                                     \
    const int w = s->planewidth[1];                                                      \
    const int slice_start = (h * jobnr) / nb_jobs;                                       \
    const int slice_end = (h * (jobnr+1)) / nb_jobs;                                     \
    type *out_uptr = (type *)(out->data[1] + slice_start * out_ulinesize);               \
    type *out_vptr = (type *)(out->data[2] + slice_start * out_vlinesize);               \
                                                                                         \
    {                                                                                    \
        const int h = s->planeheight[0];                                                 \
        const int slice_start = (h * jobnr) / nb_jobs;                                   \
        const int slice_end = (h * (jobnr+1)) / nb_jobs;                                 \
                                                                                         \
        av_image_copy_plane(out->data[0] + slice_start * out->linesize[0],               \
                            out->linesize[0],                                            \
                            in->data[0] + slice_start * in->linesize[0],                 \
                            in->linesize[0],                                             \
                            s->linesize[0], slice_end - slice_start);                    \
                                                                                         \
        if (s->nb_planes == 4) {                                                         \
            av_image_copy_plane(out->data[3] + slice_start * out->linesize[3],           \
                                out->linesize[3],                                        \
                                in->data[3] + slice_start * in->linesize[3],             \
                                in->linesize[3],                                         \
                                s->linesize[3], slice_end - slice_start);                \
        }                                                                                \
    }                                                                                    \
                                                                                         \
    for (int y = slice_start; y < slice_end; y++) {                                      \
        const type *in_yptr = (const type *)(in->data[0] + y * chroma_h * in_ylinesize); \
        const type *in_uptr = (const type *)(in->data[1] + y * in_ulinesize);            \
        const type *in_vptr = (const type *)(in->data[2] + y * in_vlinesize);            \
        const int yystart = FFMAX(0, y - sizeh);                                         \
        const int yystop  = FFMIN(h - 1, y + sizeh);                                     \
                                                                                         \
        for (int x = 0; x < w; x++) {                                                    \
            const int xxstart = FFMAX(0, x - sizew);                                     \
            const int xxstop  = FFMIN(w - 1, x + sizew);                                 \
            const int cy = in_yptr[x * chroma_w];                                        \
            const int cu = in_uptr[x];                                                   \
            const int cv = in_vptr[x];                                                   \
            int su = cu;                                                                 \
            int sv = cv;                                                                 \
            int cn = 1;                                                                  \
                                                                                         \
            for (int yy = yystart; yy <= yystop; yy += steph) {                          \
                const type *in_yptr = (const type *)(in->data[0] + yy * chroma_h * in_ylinesize); \
                const type *in_uptr = (const type *)(in->data[1] + yy * in_ulinesize);            \
                const type *in_vptr = (const type *)(in->data[2] + yy * in_vlinesize);            \
                                                                                                  \
                for (int xx = xxstart; xx <= xxstop; xx += stepw) {                    \
                    const ctype Y = in_yptr[xx * chroma_w];                            \
                    const ctype U = in_uptr[xx];                                       \
                    const ctype V = in_vptr[xx];                                       \
                    const ctype cyY = FFABS(cy - Y);                                   \
                    const ctype cuU = FFABS(cu - U);                                   \
                    const ctype cvV = FFABS(cv - V);                                   \
                                                                                       \
                    if (extra && fun(cyY, cuU, cvV) < thres &&                         \
                        cuU < thres_u && cvV < thres_v &&                              \
                        cyY < thres_y) {                                               \
                        su += U;                                                       \
                        sv += V;                                                       \
                        cn++;                                                          \
                    } else if (!extra && fun(cyY, cuU, cvV) < thres) {                 \
                        su += U;                                                       \
                        sv += V;                                                       \
                        cn++;                                                          \
                    }                                                                  \
                }                                                                      \
            }                                                                          \
                                                                                       \
            out_uptr[x] = (su + (cn >> 1)) / cn;                                       \
            out_vptr[x] = (sv + (cn >> 1)) / cn;                                       \
        }                                                                              \
                                                                                       \
        out_uptr += out_ulinesize / sizeof(type);                                      \
        out_vptr += out_vlinesize / sizeof(type);                                      \
    }                                                                                  \
                                                                                       \
    return 0;                                                                          \
}

FILTER_FUNC(manhattan, 8,  int, uint8_t, MANHATTAN_DISTANCE, 0)
FILTER_FUNC(manhattan, 16, int, uint16_t, MANHATTAN_DISTANCE, 0)

FILTER_FUNC(euclidean, 8,  int, uint8_t, EUCLIDEAN_DISTANCE, 0)
FILTER_FUNC(euclidean, 16, int64_t, uint16_t, EUCLIDEAN_DISTANCE, 0)

FILTER_FUNC(manhattan_e, 8,  int, uint8_t, MANHATTAN_DISTANCE, 1)
FILTER_FUNC(manhattan_e, 16, int, uint16_t, MANHATTAN_DISTANCE, 1)

FILTER_FUNC(euclidean_e, 8,  int, uint8_t, EUCLIDEAN_DISTANCE, 1)
FILTER_FUNC(euclidean_e, 16, int64_t, uint16_t, EUCLIDEAN_DISTANCE, 1)

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    ChromaNRContext *s = ctx->priv;
    AVFrame *out;

    switch (s->distance) {
    case 0:
        s->filter_slice = s->depth <= 8 ? manhattan_slice8 : manhattan_slice16;
        break;
    case 1:
        s->filter_slice = s->depth <= 8 ? euclidean_slice8 : euclidean_slice16;
        break;
    }

    s->thres = s->threshold * (1 << (s->depth - 8));
    s->thres_y = s->threshold_y * (1 << (s->depth - 8));
    s->thres_u = s->threshold_u * (1 << (s->depth - 8));
    s->thres_v = s->threshold_v * (1 << (s->depth - 8));

    if (s->threshold_y < 200.f || s->threshold_u < 200.f || s->threshold_v < 200.f) {
        switch (s->distance) {
        case 0:
            s->filter_slice = s->depth <= 8 ? manhattan_e_slice8 : manhattan_e_slice16;
            break;
        case 1:
            s->filter_slice = s->depth <= 8 ? euclidean_e_slice8 : euclidean_e_slice16;
            break;
        }
    }

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    av_frame_copy_props(out, in);
    s->out = out;
    ff_filter_execute(ctx, s->filter_slice, in, NULL,
                      FFMIN3(s->planeheight[1],
                             s->planeheight[2],
                             ff_filter_get_nb_threads(ctx)));

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ChromaNRContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int ret;

    s->nb_planes = desc->nb_components;
    s->depth = desc->comp[0].depth;
    s->chroma_w = 1 << desc->log2_chroma_w;
    s->chroma_h = 1 << desc->log2_chroma_h;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;

    if ((ret = av_image_fill_linesizes(s->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    return 0;
}

#define OFFSET(x) offsetof(ChromaNRContext, x)
#define VF AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption chromanr_options[] = {
    { "thres", "set y+u+v threshold", OFFSET(threshold), AV_OPT_TYPE_FLOAT, {.dbl=30}, 1,   200, VF },
    { "sizew", "set horizontal patch size", OFFSET(sizew),     AV_OPT_TYPE_INT,   {.i64=5},  1,   100, VF },
    { "sizeh", "set vertical patch size",   OFFSET(sizeh),     AV_OPT_TYPE_INT,   {.i64=5},  1,   100, VF },
    { "stepw", "set horizontal step", OFFSET(stepw),     AV_OPT_TYPE_INT,   {.i64=1},  1,    50, VF },
    { "steph", "set vertical step",   OFFSET(steph),     AV_OPT_TYPE_INT,   {.i64=1},  1,    50, VF },
    { "threy", "set y threshold",   OFFSET(threshold_y), AV_OPT_TYPE_FLOAT, {.dbl=200},1,   200, VF },
    { "threu", "set u threshold",   OFFSET(threshold_u), AV_OPT_TYPE_FLOAT, {.dbl=200},1,   200, VF },
    { "threv", "set v threshold",   OFFSET(threshold_v), AV_OPT_TYPE_FLOAT, {.dbl=200},1,   200, VF },
    { "distance", "set distance type", OFFSET(distance), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, VF, .unit = "distance" },
    {   "manhattan", "", 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, VF, .unit = "distance" },
    {   "euclidean", "", 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, VF, .unit = "distance" },
    { NULL }
};

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

AVFILTER_DEFINE_CLASS(chromanr);

const AVFilter ff_vf_chromanr = {
    .name          = "chromanr",
    .description   = NULL_IF_CONFIG_SMALL("Reduce chrominance noise."),
    .priv_size     = sizeof(ChromaNRContext),
    .priv_class    = &chromanr_class,
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_INPUTS(inputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
