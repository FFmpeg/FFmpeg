/*
 * Copyright (c) 2021 Xuewei Meng
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
#include "formats.h"
#include "framesync.h"
#include "internal.h"
#include "video.h"

enum FilterModes {
    BASIC,
    FAST,
    NB_MODES,
};

enum GuidanceModes {
    OFF,
    ON,
    NB_GUIDANCE_MODES,
};

typedef struct GuidedContext {
    const AVClass *class;
    FFFrameSync fs;

    int radius;
    float eps;
    int mode;
    int sub;
    int guidance;
    int planes;

    int width;
    int height;

    int nb_planes;
    int depth;
    int planewidth[4];
    int planeheight[4];

    int (*box_slice)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} GuidedContext;

#define OFFSET(x) offsetof(GuidedContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption guided_options[] = {
    { "radius",   "set the box radius",                               OFFSET(radius),   AV_OPT_TYPE_INT,   {.i64 = 3    },     1,                    20, FLAGS },
    { "eps",      "set the regularization parameter (with square)",   OFFSET(eps),      AV_OPT_TYPE_FLOAT, {.dbl = 0.01 },   0.0,                     1, FLAGS },
    { "mode",     "set filtering mode (0: basic mode; 1: fast mode)", OFFSET(mode),     AV_OPT_TYPE_INT,   {.i64 = BASIC}, BASIC,          NB_MODES - 1, FLAGS, "mode" },
    { "basic",    "basic guided filter",                              0,                AV_OPT_TYPE_CONST, {.i64 = BASIC},     0,                     0, FLAGS, "mode" },
    { "fast",     "fast guided filter",                               0,                AV_OPT_TYPE_CONST, {.i64 = FAST },     0,                     0, FLAGS, "mode" },
    { "sub",      "subsampling ratio for fast mode",                  OFFSET(sub),      AV_OPT_TYPE_INT,   {.i64 = 4    },     2,                    64, FLAGS },
    { "guidance", "set guidance mode (0: off mode; 1: on mode)",      OFFSET(guidance), AV_OPT_TYPE_INT,   {.i64 = OFF  },   OFF, NB_GUIDANCE_MODES - 1, FLAGS, "guidance" },
    { "off",      "only one input is enabled",                        0,                AV_OPT_TYPE_CONST, {.i64 = OFF  },     0,                     0, FLAGS, "guidance" },
    { "on",       "two inputs are required",                          0,                AV_OPT_TYPE_CONST, {.i64 = ON   },     0,                     0, FLAGS, "guidance" },
    { "planes",   "set planes to filter",                             OFFSET(planes),   AV_OPT_TYPE_INT,   {.i64 = 1    },     0,                   0xF, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(guided);

typedef struct ThreadData {
    int width;
    int height;
    float *src;
    float *dst;
    int srcStride;
    int dstStride;
} ThreadData;

static int box_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    GuidedContext *s = ctx->priv;
    ThreadData *t = arg;

    const int width  = t->width;
    const int height = t->height;
    const int src_stride = t->srcStride;
    const int dst_stride = t->dstStride;
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end   = (height * (jobnr + 1)) / nb_jobs;
    const int radius = s->radius;
    const float *src = t->src;
    float *dst = t->dst;

    int w;
    int numPix;
    w = (radius << 1) + 1;
    numPix = w * w;
    for (int i = slice_start;i < slice_end;i++) {
        for (int j = 0;j < width;j++) {
            float temp = 0.0;
            for (int row = -radius;row <= radius;row++) {
                for (int col = -radius;col <= radius;col++) {
                    int x = i + row;
                    int y = j + col;
                    x = (x < 0) ? 0 : (x >= height ? height - 1 : x);
                    y = (y < 0) ? 0 : (y >= width ? width - 1 : y);
                    temp += src[x * src_stride + y];
                }
            }
            dst[i * dst_stride + j] = temp / numPix;
        }
    }
    return 0;
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P,
    AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV440P12,
    AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV444P14,
    AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA444P9,
    AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
    AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
    AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
    AV_PIX_FMT_NONE
};

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    GuidedContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    if (s->mode == BASIC) {
        s->sub = 1;
    } else if (s->mode == FAST) {
        if (s->radius >= s->sub)
            s->radius = s->radius / s->sub;
        else {
            s->radius = 1;
        }
    }

    s->depth = desc->comp[0].depth;
    s->width = ctx->inputs[0]->w;
    s->height = ctx->inputs[0]->h;

    s->planewidth[1]  = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0]  = s->planewidth[3] = inlink->w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);
    s->box_slice = box_slice;
    return 0;
}

#define GUIDED(type, name)                                                              \
static int guided_##name(AVFilterContext *ctx, GuidedContext *s,                        \
                          const uint8_t *ssrc, const uint8_t *ssrcRef,                  \
                          uint8_t *ddst, int radius, float eps, int width, int height,  \
                          int src_stride, int src_ref_stride, int dst_stride,           \
                          float maxval)                                                 \
{                                                                                       \
    int ret = 0;                                                                        \
    type *dst = (type *)ddst;                                                           \
    const type *src = (const type *)ssrc;                                               \
    const type *srcRef = (const type *)ssrcRef;                                         \
                                                                                        \
    int sub = s->sub;                                                                   \
    int h = (height % sub) == 0 ? height / sub : height / sub + 1;                      \
    int w = (width % sub) == 0 ? width / sub : width / sub + 1;                         \
                                                                                        \
    ThreadData t;                                                                       \
    const int nb_threads = ff_filter_get_nb_threads(ctx);                               \
    float *I;                                                                           \
    float *II;                                                                          \
    float *P;                                                                           \
    float *IP;                                                                          \
    float *meanI;                                                                       \
    float *meanII;                                                                      \
    float *meanP;                                                                       \
    float *meanIP;                                                                      \
    float *A;                                                                           \
    float *B;                                                                           \
    float *meanA;                                                                       \
    float *meanB;                                                                       \
                                                                                        \
    I      = av_calloc(w * h, sizeof(float));                                           \
    II     = av_calloc(w * h, sizeof(float));                                           \
    P      = av_calloc(w * h, sizeof(float));                                           \
    IP     = av_calloc(w * h, sizeof(float));                                           \
    meanI  = av_calloc(w * h, sizeof(float));                                           \
    meanII = av_calloc(w * h, sizeof(float));                                           \
    meanP  = av_calloc(w * h, sizeof(float));                                           \
    meanIP = av_calloc(w * h, sizeof(float));                                           \
                                                                                        \
    A      = av_calloc(w * h, sizeof(float));                                           \
    B      = av_calloc(w * h, sizeof(float));                                           \
    meanA  = av_calloc(w * h, sizeof(float));                                           \
    meanB  = av_calloc(w * h, sizeof(float));                                           \
                                                                                        \
    if (!I || !II || !P || !IP || !meanI || !meanII || !meanP ||                        \
        !meanIP || !A || !B || !meanA || !meanB) {                                      \
        ret = AVERROR(ENOMEM);                                                          \
        goto end;                                                                       \
    }                                                                                   \
    for (int i = 0;i < h;i++) {                                                         \
        for (int j = 0;j < w;j++) {                                                     \
            int x = i * w + j;                                                          \
            I[x]  = src[(i * src_stride + j) * sub] / maxval;                           \
            II[x] = I[x] * I[x];                                                        \
            P[x]  = srcRef[(i * src_ref_stride + j) * sub] / maxval;                    \
            IP[x] = I[x] * P[x];                                                        \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    t.width  = w;                                                                       \
    t.height = h;                                                                       \
    t.srcStride = w;                                                                    \
    t.dstStride = w;                                                                    \
    t.src = I;                                                                          \
    t.dst = meanI;                                                                      \
    ff_filter_execute(ctx, s->box_slice, &t, NULL, FFMIN(h, nb_threads));               \
    t.src = II;                                                                         \
    t.dst = meanII;                                                                     \
    ff_filter_execute(ctx, s->box_slice, &t, NULL, FFMIN(h, nb_threads));               \
    t.src = P;                                                                          \
    t.dst = meanP;                                                                      \
    ff_filter_execute(ctx, s->box_slice, &t, NULL, FFMIN(h, nb_threads));               \
    t.src = IP;                                                                         \
    t.dst = meanIP;                                                                     \
    ff_filter_execute(ctx, s->box_slice, &t, NULL, FFMIN(h, nb_threads));               \
                                                                                        \
    for (int i = 0;i < h;i++) {                                                         \
        for (int j = 0;j < w;j++) {                                                     \
            int x = i * w + j;                                                          \
            float varI = meanII[x] - (meanI[x] * meanI[x]);                             \
            float covIP = meanIP[x] - (meanI[x] * meanP[x]);                            \
            A[x] = covIP / (varI + eps);                                                \
            B[x] = meanP[x] - A[x] * meanI[x];                                          \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    t.src = A;                                                                          \
    t.dst = meanA;                                                                      \
    ff_filter_execute(ctx, s->box_slice, &t, NULL, FFMIN(h, nb_threads));               \
    t.src = B;                                                                          \
    t.dst = meanB;                                                                      \
    ff_filter_execute(ctx, s->box_slice, &t, NULL, FFMIN(h, nb_threads));               \
                                                                                        \
    for (int i = 0;i < height;i++) {                                                    \
        for (int j = 0;j < width;j++) {                                                 \
            int x = i / sub * w + j / sub;                                              \
            dst[i * dst_stride + j] = meanA[x] * src[i * src_stride + j] +              \
                                      meanB[x] * maxval;                                \
        }                                                                               \
    }                                                                                   \
end:                                                                                    \
    av_freep(&I);                                                                       \
    av_freep(&II);                                                                      \
    av_freep(&P);                                                                       \
    av_freep(&IP);                                                                      \
    av_freep(&meanI);                                                                   \
    av_freep(&meanII);                                                                  \
    av_freep(&meanP);                                                                   \
    av_freep(&meanIP);                                                                  \
    av_freep(&A);                                                                       \
    av_freep(&B);                                                                       \
    av_freep(&meanA);                                                                   \
    av_freep(&meanB);                                                                   \
    return ret;                                                                         \
}

GUIDED(uint8_t, byte)
GUIDED(uint16_t, word)

static int filter_frame(AVFilterContext *ctx, AVFrame **out, AVFrame *in, AVFrame *ref)
{
    GuidedContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!*out)
        return AVERROR(ENOMEM);
    av_frame_copy_props(*out, in);

    for (int plane = 0; plane < s->nb_planes; plane++) {
        if (!(s->planes & (1 << plane))) {
            av_image_copy_plane((*out)->data[plane], (*out)->linesize[plane],
                                in->data[plane], in->linesize[plane],
                                s->planewidth[plane] * ((s->depth + 7) / 8), s->planeheight[plane]);
            continue;
        }
        if (s->depth <= 8)
            guided_byte(ctx, s, in->data[plane], ref->data[plane], (*out)->data[plane], s->radius, s->eps,
                        s->planewidth[plane], s->planeheight[plane],
                        in->linesize[plane], ref->linesize[plane], (*out)->linesize[plane], (1 << s->depth) - 1.f);
        else
            guided_word(ctx, s, in->data[plane], ref->data[plane], (*out)->data[plane], s->radius, s->eps,
                        s->planewidth[plane], s->planeheight[plane],
                        in->linesize[plane] / 2, ref->linesize[plane] / 2, (*out)->linesize[plane] / 2, (1 << s->depth) - 1.f);
    }

    return 0;
}

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out_frame = NULL, *main_frame = NULL, *ref_frame = NULL;
    int ret;
    ret = ff_framesync_dualinput_get(fs, &main_frame, &ref_frame);
    if (ret < 0)
        return ret;

    ret = filter_frame(ctx, &out_frame, main_frame, ref_frame);
    if (ret < 0) {
        return ret;
    }
    av_frame_free(&main_frame);

    return ff_filter_frame(outlink, out_frame);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;

    GuidedContext *s = ctx->priv;
    AVFilterLink *mainlink = ctx->inputs[0];
    FFFrameSyncIn *in;
    int ret;

    if (s->guidance == ON) {
        if (ctx->inputs[0]->w != ctx->inputs[1]->w ||
            ctx->inputs[0]->h != ctx->inputs[1]->h) {
            av_log(ctx, AV_LOG_ERROR, "Width and height of input videos must be same.\n");
            return AVERROR(EINVAL);
        }
    }

    outlink->w = mainlink->w;
    outlink->h = mainlink->h;
    outlink->time_base = mainlink->time_base;
    outlink->sample_aspect_ratio = mainlink->sample_aspect_ratio;
    outlink->frame_rate = mainlink->frame_rate;

    if (s->guidance == OFF)
        return 0;

    if ((ret = ff_framesync_init(&s->fs, ctx, 2)) < 0)
        return ret;

    outlink->time_base = s->fs.time_base;

    in = s->fs.in;
    in[0].time_base = mainlink->time_base;
    in[1].time_base = ctx->inputs[1]->time_base;
    in[0].sync   = 2;
    in[0].before = EXT_INFINITY;
    in[0].after  = EXT_INFINITY;
    in[1].sync   = 1;
    in[1].before = EXT_INFINITY;
    in[1].after  = EXT_INFINITY;
    s->fs.opaque   = s;
    s->fs.on_event = process_frame;

    return ff_framesync_configure(&s->fs);
}

static int activate(AVFilterContext *ctx)
{
    GuidedContext *s = ctx->priv;
    AVFrame *frame = NULL;
    AVFrame *out = NULL;
    int ret, status;
    int64_t pts;
    if (s->guidance)
        return ff_framesync_activate(&s->fs);

    FF_FILTER_FORWARD_STATUS_BACK(ctx->outputs[0], ctx->inputs[0]);

    if ((ret = ff_inlink_consume_frame(ctx->inputs[0], &frame)) > 0) {
        ret = filter_frame(ctx, &out, frame, frame);
        av_frame_free(&frame);
        if (ret < 0)
            return ret;
        ret = ff_filter_frame(ctx->outputs[0], out);
    }
    if (ret < 0)
        return ret;
    if (ff_inlink_acknowledge_status(ctx->inputs[0], &status, &pts)) {
        ff_outlink_set_status(ctx->outputs[0], status, pts);
        return 0;
    }
    if (ff_outlink_frame_wanted(ctx->outputs[0]))
        ff_inlink_request_frame(ctx->inputs[0]);
    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    GuidedContext *s = ctx->priv;
    AVFilterPad pad = { 0 };
    int ret;

    pad.type         = AVMEDIA_TYPE_VIDEO;
    pad.name         = "source";
    pad.config_props = config_input;

    if ((ret = ff_append_inpad(ctx, &pad)) < 0)
        return ret;

    if (s->guidance == ON) {
        pad.type         = AVMEDIA_TYPE_VIDEO;
        pad.name         = "guidance";
        pad.config_props = NULL;

        if ((ret = ff_append_inpad(ctx, &pad)) < 0)
            return ret;
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    GuidedContext *s = ctx->priv;
    if (s->guidance == ON)
        ff_framesync_uninit(&s->fs);
    return;
}

static int process_command(AVFilterContext *ctx,
                           const char *cmd,
                           const char *arg,
                           char *res,
                           int res_len,
                           int flags)
{
    int ret = ff_filter_process_command(ctx, cmd, arg, res, res_len, flags);

    if (ret < 0)
        return ret;

    return 0;
}

static const AVFilterPad guided_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

const AVFilter ff_vf_guided = {
    .name            = "guided",
    .description     = NULL_IF_CONFIG_SMALL("Apply Guided filter."),
    .init            = init,
    .uninit          = uninit,
    .priv_size       = sizeof(GuidedContext),
    .priv_class      = &guided_class,
    .activate        = activate,
    .inputs          = NULL,
    FILTER_OUTPUTS(guided_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .flags           = AVFILTER_FLAG_DYNAMIC_INPUTS | AVFILTER_FLAG_SLICE_THREADS |
                       AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
    .process_command = process_command,
};
