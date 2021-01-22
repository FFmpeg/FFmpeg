/*
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
#include "libavutil/avassert.h"
#include "libavutil/pixdesc.h"
#include "internal.h"

typedef struct EPXContext {
    const AVClass *class;

    int n;

    int (*epx_slice)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} EPXContext;

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

#define OFFSET(x) offsetof(EPXContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption epx_options[] = {
    { "n", "set scale factor", OFFSET(n), AV_OPT_TYPE_INT, {.i64 = 3}, 2, 3, .flags = FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(epx);

static int epx2_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ThreadData *td = arg;
    const AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int slice_start = (in->height *  jobnr   ) / nb_jobs;
    const int slice_end   = (in->height * (jobnr+1)) / nb_jobs;

    for (int p = 0; p < 1; p++) {
        const int width = in->width;
        const int height = in->height;
        const int src_linesize = in->linesize[p] / 4;
        const int dst_linesize = out->linesize[p] / 4;
        const uint32_t *src = (const uint32_t *)in->data[p];
        uint32_t *dst = (uint32_t *)out->data[p];
        const uint32_t *src_line[3];

        src_line[0] = src + src_linesize * FFMAX(slice_start - 1, 0);
        src_line[1] = src + src_linesize * slice_start;
        src_line[2] = src + src_linesize * FFMIN(slice_start + 1, height-1);

        for (int y = slice_start; y < slice_end; y++) {
            uint32_t *dst_line[2];

            dst_line[0] = dst + dst_linesize*2*y;
            dst_line[1] = dst + dst_linesize*(2*y+1);

            for (int x = 0; x < width; x++) {
                uint32_t E0, E1, E2, E3;
                uint32_t B, D, E, F, H;

                B = src_line[0][x];
                D = src_line[1][FFMAX(x-1, 0)];
                E = src_line[1][x];
                F = src_line[1][FFMIN(x+1, width - 1)];
                H = src_line[2][x];

                if (B != H && D != F) {
                    E0 = D == B ? D : E;
                    E1 = B == F ? F : E;
                    E2 = D == H ? D : E;
                    E3 = H == F ? F : E;
                } else {
                    E0 = E;
                    E1 = E;
                    E2 = E;
                    E3 = E;
                }

                dst_line[0][x*2]   = E0;
                dst_line[0][x*2+1] = E1;
                dst_line[1][x*2]   = E2;
                dst_line[1][x*2+1] = E3;
            }

            src_line[0] = src_line[1];
            src_line[1] = src_line[2];
            src_line[2] = src_line[1];

            if (y < height - 1)
                src_line[2] += src_linesize;
        }
    }

    return 0;
}

static int epx3_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ThreadData *td = arg;
    const AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int slice_start = (in->height *  jobnr   ) / nb_jobs;
    const int slice_end   = (in->height * (jobnr+1)) / nb_jobs;

    for (int p = 0; p < 1; p++) {
        const int width = in->width;
        const int height = in->height;
        const int src_linesize = in->linesize[p] / 4;
        const int dst_linesize = out->linesize[p] / 4;
        const uint32_t *src = (const uint32_t *)in->data[p];
        uint32_t *dst = (uint32_t *)out->data[p];
        const uint32_t *src_line[3];

        src_line[0] = src + src_linesize * FFMAX(slice_start - 1, 0);
        src_line[1] = src + src_linesize * slice_start;
        src_line[2] = src + src_linesize * FFMIN(slice_start + 1, height-1);

        for (int y = slice_start; y < slice_end; y++) {
            uint32_t *dst_line[3];

            dst_line[0] = dst + dst_linesize*3*y;
            dst_line[1] = dst + dst_linesize*(3*y+1);
            dst_line[2] = dst + dst_linesize*(3*y+2);

            for (int x = 0; x < width; x++) {
                uint32_t E0, E1, E2, E3, E4, E5, E6, E7, E8;
                uint32_t A, B, C, D, E, F, G, H, I;

                A = src_line[0][FFMAX(x-1, 0)];
                B = src_line[0][x];
                C = src_line[0][FFMIN(x+1, width - 1)];
                D = src_line[1][FFMAX(x-1, 0)];
                E = src_line[1][x];
                F = src_line[1][FFMIN(x+1, width - 1)];
                G = src_line[2][FFMAX(x-1, 0)];
                H = src_line[2][x];
                I = src_line[2][FFMIN(x+1, width - 1)];

                if (B != H && D != F) {
                    E0 = D == B ? D : E;
                    E1 = (D == B && E != C) || (B == F && E != A) ? B : E;
                    E2 = B == F ? F : E;
                    E3 = (D == B && E != G) || (D == H && E != A) ? D : E;
                    E4 = E;
                    E5 = (B == F && E != I) || (H == F && E != C) ? F : E;
                    E6 = D == H ? D : E;
                    E7 = (D == H && E != I) || (H == F && E != G) ? H : E;
                    E8 = H == F ? F : E;
                } else {
                    E0 = E;
                    E1 = E;
                    E2 = E;
                    E3 = E;
                    E4 = E;
                    E5 = E;
                    E6 = E;
                    E7 = E;
                    E8 = E;
                }

                dst_line[0][x*3]   = E0;
                dst_line[0][x*3+1] = E1;
                dst_line[0][x*3+2] = E2;
                dst_line[1][x*3]   = E3;
                dst_line[1][x*3+1] = E4;
                dst_line[1][x*3+2] = E5;
                dst_line[2][x*3]   = E6;
                dst_line[2][x*3+1] = E7;
                dst_line[2][x*3+2] = E8;
            }

            src_line[0] = src_line[1];
            src_line[1] = src_line[2];
            src_line[2] = src_line[1];

            if (y < height - 1)
                src_line[2] += src_linesize;
        }
    }

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    EPXContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    const AVPixFmtDescriptor *desc;

    desc = av_pix_fmt_desc_get(outlink->format);
    if (!desc)
        return AVERROR_BUG;

    outlink->w = inlink->w * s->n;
    outlink->h = inlink->h * s->n;

    switch (s->n) {
    case 2:
        s->epx_slice = epx2_slice;
        break;
    case 3:
        s->epx_slice = epx3_slice;
        break;
    }

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_RGBA, AV_PIX_FMT_BGRA, AV_PIX_FMT_ARGB, AV_PIX_FMT_ABGR,
        AV_PIX_FMT_NONE,
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    EPXContext *s = ctx->priv;
    ThreadData td;

    AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    av_frame_copy_props(out, in);

    td.in = in, td.out = out;
    ctx->internal->execute(ctx, s->epx_slice, &td, NULL, FFMIN(inlink->h, ff_filter_get_nb_threads(ctx)));

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter ff_vf_epx = {
    .name          = "epx",
    .description   = NULL_IF_CONFIG_SMALL("Scale the input using EPX algorithm."),
    .inputs        = inputs,
    .outputs       = outputs,
    .query_formats = query_formats,
    .priv_size     = sizeof(EPXContext),
    .priv_class    = &epx_class,
    .flags         = AVFILTER_FLAG_SLICE_THREADS,
};
