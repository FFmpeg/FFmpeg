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
#include "libavutil/imgutils.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct CASContext {
    const AVClass *class;

    float strength;
    int planes;
    int nb_planes;

    int depth;
    int planeheight[4];
    int planewidth[4];

    AVFrame *in;

    int (*do_slice)(AVFilterContext *s, void *arg,
                    int jobnr, int nb_jobs);
} CASContext;

static inline float lerpf(float v0, float v1, float f)
{
    return v0 + (v1 - v0) * f;
}

static int cas_slice8(AVFilterContext *avctx, void *arg, int jobnr, int nb_jobs)
{
    CASContext *s = avctx->priv;
    const float strength = -lerpf(16.f, 4.01f, s->strength);
    AVFrame *out = arg;
    AVFrame *in = s->in;

    for (int p = 0; p < s->nb_planes; p++) {
        const int slice_start = (s->planeheight[p] * jobnr) / nb_jobs;
        const int slice_end = (s->planeheight[p] * (jobnr+1)) / nb_jobs;
        const int linesize = out->linesize[p];
        const int in_linesize = in->linesize[p];
        const int w = s->planewidth[p];
        const int w1 = w - 1;
        const int h = s->planeheight[p];
        const int h1 = h - 1;
        uint8_t *dst = out->data[p] + slice_start * linesize;
        const uint8_t *src = in->data[p];

        if (!((1 << p) & s->planes)) {
            av_image_copy_plane(dst, linesize, src + slice_start * in_linesize, in_linesize,
                                w, slice_end - slice_start);
            continue;
        }

        for (int y = slice_start; y < slice_end; y++) {
            const int y0 = FFMAX(y - 1, 0);
            const int y1 = FFMIN(y + 1, h1);
            for (int x = 0; x < w; x++) {
                const int x0 = FFMAX(x - 1, 0);
                const int x1 = FFMIN(x + 1, w1);
                int a = src[y0 * in_linesize + x0];
                int b = src[y0 * in_linesize + x];
                int c = src[y0 * in_linesize + x1];
                int d = src[y * in_linesize + x0];
                int e = src[y * in_linesize + x];
                int f = src[y * in_linesize + x1];
                int g = src[y1 * in_linesize + x0];
                int h = src[y1 * in_linesize + x];
                int i = src[y1 * in_linesize + x1];
                int mn, mn2, mx, mx2;
                float amp, weight;

                mn  = FFMIN3(FFMIN3( d, e, f), b, h);
                mn2 = FFMIN3(FFMIN3(mn, a, c), g, i);

                mn = mn + mn2;

                mx  = FFMAX3(FFMAX3( d, e, f), b, h);
                mx2 = FFMAX3(FFMAX3(mx, a, c), g, i);

                mx = mx + mx2;

                amp = sqrtf(av_clipf(FFMIN(mn, 511 - mx) / (float)mx, 0.f, 1.f));

                weight = amp / strength;

                dst[x] = av_clip_uint8(((b + d + f + h) * weight + e) / (1.f + 4.f * weight));
            }
            dst += linesize;
        }
    }

    return 0;
}

static int cas_slice16(AVFilterContext *avctx, void *arg, int jobnr, int nb_jobs)
{
    CASContext *s = avctx->priv;
    const float strength = -lerpf(16.f, 4.01f, s->strength);
    const int max = 2 * (1 << s->depth) - 1;
    AVFrame *out = arg;
    AVFrame *in = s->in;

    for (int p = 0; p < s->nb_planes; p++) {
        const int slice_start = (s->planeheight[p] * jobnr) / nb_jobs;
        const int slice_end = (s->planeheight[p] * (jobnr+1)) / nb_jobs;
        const int linesize = out->linesize[p] / 2;
        const int in_linesize = in->linesize[p] / 2;
        const int w = s->planewidth[p];
        const int w1 = w - 1;
        const int h = s->planeheight[p];
        const int h1 = h - 1;
        uint16_t *dst = ((uint16_t *)out->data[p]) + slice_start * linesize;
        const uint16_t *src = (const uint16_t *)in->data[p];

        if (!((1 << p) & s->planes)) {
            av_image_copy_plane((uint8_t *)dst, linesize * 2, (uint8_t *)(src + slice_start * in_linesize),
                                in_linesize * 2, w * 2, slice_end - slice_start);
            continue;
        }

        for (int y = slice_start; y < slice_end; y++) {
            const int y0 = FFMAX(y - 1, 0);
            const int y1 = FFMIN(y + 1, h1);
            for (int x = 0; x < w; x++) {
                const int x0 = FFMAX(x - 1, 0);
                const int x1 = FFMIN(x + 1, w1);
                int a = src[y0 * in_linesize + x0];
                int b = src[y0 * in_linesize + x];
                int c = src[y0 * in_linesize + x1];
                int d = src[y * in_linesize + x0];
                int e = src[y * in_linesize + x];
                int f = src[y * in_linesize + x1];
                int g = src[y1 * in_linesize + x0];
                int h = src[y1 * in_linesize + x];
                int i = src[y1 * in_linesize + x1];
                int mn, mn2, mx, mx2;
                float amp, weight;

                mn  = FFMIN3(FFMIN3( d, e, f), b, h);
                mn2 = FFMIN3(FFMIN3(mn, a, c), g, i);

                mn = mn + mn2;

                mx  = FFMAX3(FFMAX3( d, e, f), b, h);
                mx2 = FFMAX3(FFMAX3(mx, a, c), g, i);

                mx = mx + mx2;

                amp = sqrtf(av_clipf(FFMIN(mn, max - mx) / (float)mx, 0.f, 1.f));

                weight = amp / strength;

                dst[x] = av_clip_uintp2_c(((b + d + f + h) * weight + e) / (1.f + 4.f * weight), s->depth);
            }
            dst += linesize;
        }
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    CASContext *s = ctx->priv;
    AVFrame *out;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    s->in = in;
    ff_filter_execute(ctx, s->do_slice, out, NULL,
                      FFMIN(in->height, ff_filter_get_nb_threads(ctx)));
    av_frame_free(&in);
    s->in = NULL;

    return ff_filter_frame(ctx->outputs[0], out);
}

static const enum AVPixelFormat pixel_fmts[] = {
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_GRAY9,  AV_PIX_FMT_GRAY10,
    AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
    AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUVJ411P,
    AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV440P10,
    AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV420P12,
    AV_PIX_FMT_YUV440P12,
    AV_PIX_FMT_YUV444P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV420P14,
    AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
    AV_PIX_FMT_YUVA420P,  AV_PIX_FMT_YUVA422P,   AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA444P12, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA422P16,
    AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA420P16,
    AV_PIX_FMT_GBRAP,     AV_PIX_FMT_GBRAP10,    AV_PIX_FMT_GBRAP12,    AV_PIX_FMT_GBRAP16,
    AV_PIX_FMT_NONE
};

static av_cold int config_input(AVFilterLink *inlink)
{
    AVFilterContext *avctx = inlink->dst;
    CASContext *s = avctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->planewidth[1]  = s->planewidth[2]  = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0]  = s->planewidth[3]  = inlink->w;

    s->depth = desc->comp[0].depth;
    s->nb_planes = desc->nb_components;
    s->do_slice = s->depth <= 8 ? cas_slice8 : cas_slice16;

    return 0;
}

static const AVFilterPad cas_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .filter_frame   = filter_frame,
        .config_props   = config_input,
    },
};

static const AVFilterPad cas_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

#define OFFSET(x) offsetof(CASContext, x)
#define VF AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption cas_options[] = {
    { "strength", "set the sharpening strength", OFFSET(strength), AV_OPT_TYPE_FLOAT, {.dbl=0}, 0,  1, VF },
    { "planes",  "set what planes to filter",    OFFSET(planes),   AV_OPT_TYPE_FLAGS, {.i64=7}, 0, 15, VF },
    { NULL }
};

AVFILTER_DEFINE_CLASS(cas);

const AVFilter ff_vf_cas = {
    .name          = "cas",
    .description   = NULL_IF_CONFIG_SMALL("Contrast Adaptive Sharpen."),
    .priv_size     = sizeof(CASContext),
    .priv_class    = &cas_class,
    FILTER_INPUTS(cas_inputs),
    FILTER_OUTPUTS(cas_outputs),
    FILTER_PIXFMTS_ARRAY(pixel_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
