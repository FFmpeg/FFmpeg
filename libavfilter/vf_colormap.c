/*
 * Copyright (c) 2022 Paul B Mahol
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
 * Compute a look-up table from map of colors.
 */

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "internal.h"
#include "framesync.h"
#include "video.h"

#define MAX_SIZE 64

enum KernelType {
    EUCLIDEAN,
    WEUCLIDEAN,
    NB_KERNELS,
};

typedef struct ColorMapContext {
    const AVClass *class;
    int w, h;
    int size;
    int nb_maps;
    int changed[2];

    float source[MAX_SIZE][4];
    float ttarget[MAX_SIZE][4];
    float target[MAX_SIZE][4];
    float icoeff[4][4];
    float coeff[MAX_SIZE][4];

    int target_type;
    int kernel_type;
    float (*kernel)(const float *x, const float *y);

    FFFrameSync fs;

    double A[(MAX_SIZE + 4) * (MAX_SIZE + 4)];
    double b[MAX_SIZE + 4];
    int pivot[MAX_SIZE + 4];
} ColorMapContext;

#define OFFSET(x) offsetof(ColorMapContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption colormap_options[] = {
    { "patch_size", "set patch size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "64x64"}, 0, 0, FLAGS },
    { "nb_patches", "set number of patches", OFFSET(size), AV_OPT_TYPE_INT, {.i64 = 0}, 0, MAX_SIZE, FLAGS },
    { "type", "set the target type used",  OFFSET(target_type), AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, .unit = "type" },
    {   "relative", "the target colors are relative", 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 1, FLAGS, .unit = "type" },
    {   "absolute", "the target colors are absolute", 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 1, FLAGS, .unit = "type" },
    { "kernel", "set the kernel used for measuring color difference",  OFFSET(kernel_type), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_KERNELS-1, FLAGS, .unit = "kernel" },
    {   "euclidean",   "square root of sum of squared differences",         0, AV_OPT_TYPE_CONST, {.i64=EUCLIDEAN},   0, 0, FLAGS, .unit = "kernel" },
    {   "weuclidean",  "weighted square root of sum of squared differences",0, AV_OPT_TYPE_CONST, {.i64=WEUCLIDEAN},  0, 0, FLAGS, .unit = "kernel" },
    { NULL }
};

static int gauss_make_triangular(double *A, int *p, int n)
{
    p[n - 1] = n - 1;
    for (int k = 0; k < n; k++) {
        double t1;
        int m = k;

        for (int i = k + 1; i < n; i++)
            if (fabs(A[k + n * i]) > fabs(A[k + n * m]))
                m = i;
        p[k] = m;
        t1 = A[k + n * m];
        A[k + n * m] = A[k + n * k];
        A[k + n * k] = t1;
        if (t1 != 0) {
            for (int i = k + 1; i < n; i++)
                A[k + n * i] /= -t1;
            if (k != m)
                for (int i = k + 1; i < n; i++) {
                    double t2 = A[i + n * m];
                    A[i + n * m] = A[i + n * k];
                    A[i + n * k] = t2;
                }
            for (int j = k + 1; j < n; j++)
                for (int i = k + 1; i < n; i++)
                    A[i + n * j] += A[k + j * n] * A[i + k * n];
        } else {
            return 0;
        }
    }

    return 1;
}

static void gauss_solve_triangular(const double *A, const int *p, double *b, int n)
{
    for(int k = 0; k < n - 1; k++) {
        int m = p[k];
        double t = b[m];
        b[m] = b[k];
        b[k] = t;
        for (int i = k + 1; i < n; i++)
            b[i] += A[k + n * i] * t;
    }

    for(int k = n - 1; k > 0; k--) {
        double t = b[k] /= A[k + n * k];
        for (int i = 0; i < k; i++)
            b[i] -= A[k + n * i] * t;
    }

    b[0] /= A[0 + 0 * n];
}

static int gauss_solve(double *A, double *b, int n)
{
    int p[3] = { 0 };

    av_assert2(n <= FF_ARRAY_ELEMS(p));

    if (!gauss_make_triangular(A, p, n))
        return 1;

    gauss_solve_triangular(A, p, b, n);

    return 0;
}

#define P2(x) ((x)*(x))

static float euclidean_kernel(const float *x, const float *y)
{
    const float d2 = P2(x[0]-y[0]) +
                     P2(x[1]-y[1]) +
                     P2(x[2]-y[2]);
    return sqrtf(d2);
}

static float weuclidean_kernel(const float *x, const float *y)
{
    const float rm = (x[0] + y[0]) * 0.5f;
    const float d2 = P2(x[0]-y[0]) * (2.f + rm) +
                     P2(x[1]-y[1]) * 4.f +
                     P2(x[2]-y[2]) * (3.f - rm);
    return sqrtf(d2);
}

static void build_map(AVFilterContext *ctx)
{
    ColorMapContext *s = ctx->priv;

    for (int j = 0; j < s->nb_maps; j++) {
        s->target[j][0] = s->target_type == 0 ? s->source[j][0] + s->ttarget[j][0] : s->ttarget[j][0];
        s->target[j][1] = s->target_type == 0 ? s->source[j][1] + s->ttarget[j][1] : s->ttarget[j][1];
        s->target[j][2] = s->target_type == 0 ? s->source[j][2] + s->ttarget[j][2] : s->ttarget[j][2];
    }

    for (int c = 0; c < 3; c++) {
        for (int j = 0; j < s->nb_maps; j++)
            s->coeff[j][c] = 0.f;

        for (int j = 0; j < 4; j++) {
            s->icoeff[j][c] = 0;
            s->icoeff[j][c] = 0;
            s->icoeff[j][c] = 0;
        }

        s->icoeff[c+1][c] = 1.f;

        switch (s->nb_maps) {
        case 1:
            {
                float div = fabsf(s->source[0][c]) < 1e-6f ? 1e-6f : s->source[0][c];
                s->icoeff[c][1+c] = s->target[0][c] / div;
            }
            break;
        case 2:
            {
                double A[2 * 2] = { 1, s->source[0][c],
                                    1, s->source[1][c] };
                double b[2] = { s->target[0][c], s->target[1][c] };

                if (gauss_solve(A, b, 2))
                    continue;

                s->icoeff[0  ][c] = b[0];
                s->icoeff[1+c][c] = b[1];
            }
            break;
        case 3:
            {
                const uint8_t idx[3][3] = {{ 0, 1, 2 },
                                           { 1, 0, 2 },
                                           { 2, 0, 1 }};
                const uint8_t didx[3][4] = {{ 0, 1, 2, 2 },
                                            { 0, 2, 1, 2 },
                                            { 0, 2, 2, 1 }};
                const int C0 = idx[c][0];
                const int C1 = idx[c][1];
                const int C2 = idx[c][2];
                double A[3 * 3] = { 1, s->source[0][C0], s->source[0][C1] + s->source[0][C2],
                                    1, s->source[1][C0], s->source[1][C1] + s->source[1][C2],
                                    1, s->source[2][C0], s->source[2][C1] + s->source[2][C2] };
                double b[3] = { s->target[0][c], s->target[1][c], s->target[2][c] };

                if (gauss_solve(A, b, 3))
                    continue;

                s->icoeff[0][c] = b[didx[c][0]];
                s->icoeff[1][c] = b[didx[c][1]];
                s->icoeff[2][c] = b[didx[c][2]];
                s->icoeff[3][c] = b[didx[c][3]];
            }
            break;
        case 4:
            {
                double A[4 * 4] = { 1, s->source[0][0], s->source[0][1], s->source[0][2],
                                    1, s->source[1][0], s->source[1][1], s->source[1][2],
                                    1, s->source[2][0], s->source[2][1], s->source[2][2],
                                    1, s->source[3][0], s->source[3][1], s->source[3][2] };
                double b[4] = { s->target[0][c], s->target[1][c], s->target[2][c], s->target[3][c] };
                int pivot[4];

                if (!gauss_make_triangular(A, pivot, 4))
                    continue;
                gauss_solve_triangular(A, pivot, b, 4);

                s->icoeff[0][c] = b[0];
                s->icoeff[1][c] = b[1];
                s->icoeff[2][c] = b[2];
                s->icoeff[3][c] = b[3];
            }
            break;
        default:
            {
                const int N = s->nb_maps;
                const int N4 = N + 4;
                double *A = s->A;
                double *b = s->b;
                int *pivot = s->pivot;

                for (int j = 0; j < N; j++)
                    for (int i = j; i < N; i++)
                        A[j*N4+i] = A[i*N4+j] = s->kernel(s->source[i], s->source[j]);

                for (int i = 0; i < N; i++)
                    A[i*N4+N+0] = A[(N+0)*N4+i] = 1;
                for (int i = 0; i < N; i++)
                    A[i*N4+N+1] = A[(N+1)*N4+i] = s->source[i][0];
                for (int i = 0; i < N; i++)
                    A[i*N4+N+2] = A[(N+2)*N4+i] = s->source[i][1];
                for (int i = 0; i < N; i++)
                    A[i*N4+N+3] = A[(N+3)*N4+i] = s->source[i][2];

                for (int j = N; j < N4; j++)
                    for (int i = N;i < N4; i++)
                        A[j * N4 + i] = 0.;

                if (gauss_make_triangular(A, pivot, N4)) {
                    for (int i = 0; i < N; i++)
                        b[i] = s->target[i][c];
                    for (int i = N; i < N + 4; i++)
                        b[i] = 0;

                    gauss_solve_triangular(A, pivot, b, N4);

                    for (int i = 0; i < N; i++)
                        s->coeff[i][c] = b[i];

                    for (int i = 0; i < 4; i++)
                        s->icoeff[i][c] = b[N + i];
                }
            }
        }
    }
}

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

static int colormap_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorMapContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int maps = s->nb_maps;
    const int width = out->width;
    const int height = out->height;
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const int sr_linesize = in->linesize[2] / 4;
    const int dr_linesize = out->linesize[2] / 4;
    const int sg_linesize = in->linesize[0] / 4;
    const int dg_linesize = out->linesize[0] / 4;
    const int sb_linesize = in->linesize[1] / 4;
    const int db_linesize = out->linesize[1] / 4;
    const float *sr = (float *)in->data[2] + slice_start * sr_linesize;
    const float *sg = (float *)in->data[0] + slice_start * sg_linesize;
    const float *sb = (float *)in->data[1] + slice_start * sb_linesize;
    float *r = (float *)out->data[2] + slice_start * dr_linesize;
    float *g = (float *)out->data[0] + slice_start * dg_linesize;
    float *b = (float *)out->data[1] + slice_start * db_linesize;
    float (*kernel)(const float *x, const float *y) = s->kernel;
    const float *icoeff[4] = { s->icoeff[0], s->icoeff[1], s->icoeff[2], s->icoeff[3] };

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < width; x++) {
            const float input[3] = { sr[x], sg[x], sb[x] };
            float srv, sgv, sbv;
            float rv, gv, bv;

            srv = sr[x];
            sgv = sg[x];
            sbv = sb[x];

            rv = icoeff[0][0];
            gv = icoeff[0][1];
            bv = icoeff[0][2];

            rv += icoeff[1][0] * srv + icoeff[2][0] * sgv + icoeff[3][0] * sbv;
            gv += icoeff[1][1] * srv + icoeff[2][1] * sgv + icoeff[3][1] * sbv;
            bv += icoeff[1][2] * srv + icoeff[2][2] * sgv + icoeff[3][2] * sbv;

            for (int z = 0; z < maps && maps > 4; z++) {
                const float *coeff = s->coeff[z];
                const float cr = coeff[0];
                const float cg = coeff[1];
                const float cb = coeff[2];
                const float f = kernel(input, s->source[z]);

                rv += f * cr;
                gv += f * cg;
                bv += f * cb;
            }

            r[x] = rv;
            g[x] = gv;
            b[x] = bv;
        }

        sg += sg_linesize;
        g += dg_linesize;
        sb += sb_linesize;
        b += db_linesize;
        sr += sr_linesize;
        r += dr_linesize;
    }

    return 0;
}

static int import_map(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    ColorMapContext *s = ctx->priv;
    const int is_target = FF_INLINK_IDX(inlink) > 1;
    const int pw = s->w;
    const int pw2 = s->w / 2;
    const int ph = s->h;
    const int ph2 = s->h / 2;
    int changed = 0;
    int idx;

    for (int plane = 0; plane < 3; plane++) {
        const int c = plane == 0 ? 1 : plane == 1 ? 2 : 0;

        idx = 0;
        for (int y = ph2; y < in->height && idx < MAX_SIZE; y += ph) {
            const float *src = (const float *)(in->data[plane] + y * in->linesize[plane]);

            for (int x = pw2; x < in->width && idx < MAX_SIZE; x += pw) {
                float value = src[x];

                if (is_target) {
                    if (s->ttarget[idx][c] != value)
                        changed = 1;
                    s->ttarget[idx][c] = value;
                } else {
                    if (s->source[idx][c] != value)
                        changed = 1;
                    s->source[idx][c] = value;
                }

                idx++;
            }
        }
    }

    if (changed)
        s->changed[is_target] = 1;
    if (!s->size)
        s->size = FFMIN(idx, MAX_SIZE);
    if (!is_target)
        s->nb_maps = FFMIN(idx, s->size);

    return 0;
}

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    ColorMapContext *s = fs->opaque;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *in, *out, *source, *target;
    ThreadData td;
    int ret;

    switch (s->kernel_type) {
    case EUCLIDEAN:
        s->kernel = euclidean_kernel;
        break;
    case WEUCLIDEAN:
        s->kernel = weuclidean_kernel;
        break;
    default:
        return AVERROR_BUG;
    }

    if ((ret = ff_framesync_get_frame(&s->fs, 0, &in,     1)) < 0 ||
        (ret = ff_framesync_get_frame(&s->fs, 1, &source, 0)) < 0 ||
        (ret = ff_framesync_get_frame(&s->fs, 2, &target, 0)) < 0)
        return ret;

    import_map(ctx->inputs[1], source);
    import_map(ctx->inputs[2], target);

    if (s->changed[0] || s->changed[1]) {
        build_map(ctx);
        s->changed[0] = s->changed[1] = 0;
    }

    if (!ctx->is_disabled) {
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

        td.in = in;
        td.out = out;
        ff_filter_execute(ctx, colormap_slice, &td, NULL,
                          FFMIN(in->height, ff_filter_get_nb_threads(ctx)));

        if (out != in)
            av_frame_free(&in);
    } else {
        out = in;
    }

    out->pts = av_rescale_q(s->fs.pts, s->fs.time_base, outlink->time_base);

    return ff_filter_frame(outlink, out);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    ColorMapContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *source = ctx->inputs[1];
    AVFilterLink *target = ctx->inputs[2];
    FFFrameSyncIn *in;
    int ret;

    outlink->time_base = inlink->time_base;
    outlink->frame_rate = inlink->frame_rate;
    outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;
    outlink->w = inlink->w;
    outlink->h = inlink->h;

    if ((ret = ff_framesync_init(&s->fs, ctx, 3)) < 0)
        return ret;

    in = s->fs.in;
    in[0].time_base = inlink->time_base;
    in[1].time_base = source->time_base;
    in[2].time_base = target->time_base;
    in[0].sync   = 1;
    in[0].before = EXT_STOP;
    in[0].after  = EXT_INFINITY;
    in[1].sync   = 1;
    in[1].before = EXT_STOP;
    in[1].after  = EXT_INFINITY;
    in[2].sync   = 1;
    in[2].before = EXT_STOP;
    in[2].after  = EXT_INFINITY;
    s->fs.opaque   = s;
    s->fs.on_event = process_frame;

    ret = ff_framesync_configure(&s->fs);
    outlink->time_base = s->fs.time_base;

    return ret;
}

static int activate(AVFilterContext *ctx)
{
    ColorMapContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ColorMapContext *const s = ctx->priv;

    ff_framesync_uninit(&s->fs);
}

static const AVFilterPad inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    {
        .name = "source",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    {
        .name = "target",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
};

AVFILTER_DEFINE_CLASS(colormap);

const AVFilter ff_vf_colormap = {
    .name          = "colormap",
    .description   = NULL_IF_CONFIG_SMALL("Apply custom Color Maps to video stream."),
    .priv_class    = &colormap_class,
    .priv_size     = sizeof(ColorMapContext),
    .activate      = activate,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_PIXFMTS(AV_PIX_FMT_GBRPF32, AV_PIX_FMT_GBRAPF32),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                     AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
    .uninit        = uninit,
};
