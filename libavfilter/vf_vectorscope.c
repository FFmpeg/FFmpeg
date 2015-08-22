/*
 * Copyright (c) 2015 Paul B Mahol
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

#include "libavutil/avassert.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

enum VectorscopeMode {
    GRAY,
    COLOR,
    COLOR2,
    COLOR3,
    MODE_NB
};

typedef struct VectorscopeContext {
    const AVClass *class;
    int mode;
    const uint8_t *bg_color;
    int planewidth[4];
    int planeheight[4];
    int x, y, pd;
    int is_yuv;
} VectorscopeContext;

#define OFFSET(x) offsetof(VectorscopeContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption vectorscope_options[] = {
    { "mode", "set vectorscope mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=0}, 0, MODE_NB-1, FLAGS, "mode"},
    { "gray",   0, 0, AV_OPT_TYPE_CONST, {.i64=GRAY},   0, 0, FLAGS, "mode" },
    { "color",  0, 0, AV_OPT_TYPE_CONST, {.i64=COLOR},  0, 0, FLAGS, "mode" },
    { "color2", 0, 0, AV_OPT_TYPE_CONST, {.i64=COLOR2}, 0, 0, FLAGS, "mode" },
    { "color3", 0, 0, AV_OPT_TYPE_CONST, {.i64=COLOR3}, 0, 0, FLAGS, "mode" },
    { "x", "set color component on X axis", OFFSET(x), AV_OPT_TYPE_INT, {.i64=1}, 0, 2, FLAGS},
    { "y", "set color component on Y axis", OFFSET(y), AV_OPT_TYPE_INT, {.i64=2}, 0, 2, FLAGS},
    { NULL }
};

AVFILTER_DEFINE_CLASS(vectorscope);

static const enum AVPixelFormat out_yuv_pix_fmts[] = {
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat out_rgb_pix_fmts[] = {
    AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRP,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat in1_pix_fmts[] = {
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRP,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat in2_pix_fmts[] = {
    AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRP,
    AV_PIX_FMT_NONE
};

static int query_formats(AVFilterContext *ctx)
{
    VectorscopeContext *s = ctx->priv;
    const enum AVPixelFormat *out_pix_fmts;
    const AVPixFmtDescriptor *desc;
    AVFilterFormats *avff;
    int rgb, i;

    if (!ctx->inputs[0]->in_formats ||
        !ctx->inputs[0]->in_formats->nb_formats) {
        return AVERROR(EAGAIN);
    }

    if (!ctx->inputs[0]->out_formats) {
        const enum AVPixelFormat *in_pix_fmts;

        if ((s->x == 1 && s->y == 2) || (s->x == 2 && s->y == 1))
            in_pix_fmts = in2_pix_fmts;
        else
            in_pix_fmts = in1_pix_fmts;
        ff_formats_ref(ff_make_format_list(in_pix_fmts), &ctx->inputs[0]->out_formats);
    }

    avff = ctx->inputs[0]->in_formats;
    desc = av_pix_fmt_desc_get(avff->formats[0]);
    rgb = desc->flags & AV_PIX_FMT_FLAG_RGB;
    for (i = 1; i < avff->nb_formats; i++) {
        desc = av_pix_fmt_desc_get(avff->formats[i]);
        if (rgb != desc->flags & AV_PIX_FMT_FLAG_RGB)
            return AVERROR(EAGAIN);
    }

    if (rgb)
        out_pix_fmts = out_rgb_pix_fmts;
    else
        out_pix_fmts = out_yuv_pix_fmts;
    ff_formats_ref(ff_make_format_list(out_pix_fmts), &ctx->outputs[0]->in_formats);

    return 0;
}

static const uint8_t black_yuva_color[4] = { 0, 127, 127, 0 };
static const uint8_t black_gbrp_color[4] = { 0, 0, 0, 0 };

static int config_input(AVFilterLink *inlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    VectorscopeContext *s = inlink->dst->priv;

    if (s->mode == GRAY)
        s->pd = 0;
    else {
        if ((s->x == 1 && s->y == 2) || (s->x == 2 && s->y == 1))
            s->pd = 0;
        else if ((s->x == 0 && s->y == 2) || (s->x == 2 && s->y == 0))
            s->pd = 1;
        else if ((s->x == 0 && s->y == 1) || (s->x == 1 && s->y == 0))
            s->pd = 2;
    }

    switch (inlink->format) {
    case AV_PIX_FMT_GBRAP:
    case AV_PIX_FMT_GBRP:
        s->bg_color = black_gbrp_color;
        break;
    default:
        s->bg_color = black_yuva_color;
    }

    s->planeheight[1] = s->planeheight[2] = FF_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->planewidth[1]  = s->planewidth[2]  = FF_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0]  = s->planewidth[3]  = inlink->w;

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    VectorscopeContext *s = outlink->src->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(outlink->format);
    int depth = desc->comp[0].depth_minus1 + 1;

    s->is_yuv = !(desc->flags & AV_PIX_FMT_FLAG_RGB);
    outlink->h = outlink->w = 1 << depth;
    outlink->sample_aspect_ratio = (AVRational){1,1};
    return 0;
}

static void vectorscope(VectorscopeContext *s, AVFrame *in, AVFrame *out, int pd)
{
    const uint8_t * const *src = (const uint8_t * const *)in->data;
    const int slinesizex = in->linesize[s->x];
    const int slinesizey = in->linesize[s->y];
    const int dlinesize = out->linesize[0];
    int i, j, px = s->x, py = s->y;
    const int h = s->planeheight[py];
    const int w = s->planewidth[px];
    const uint8_t *spx = src[px];
    const uint8_t *spy = src[py];
    uint8_t **dst = out->data;
    uint8_t *dpx = dst[px];
    uint8_t *dpy = dst[py];
    uint8_t *dpd = dst[pd];

    switch (s->mode) {
    case COLOR:
    case GRAY:
        if (s->is_yuv) {
            for (i = 0; i < h; i++) {
                const int iwx = i * slinesizex;
                const int iwy = i * slinesizey;
                for (j = 0; j < w; j++) {
                    const int x = spx[iwx + j];
                    const int y = spy[iwy + j];
                    const int pos = y * dlinesize + x;

                    dpd[pos] = FFMIN(dpd[pos] + 1, 255);
                    if (dst[3])
                        dst[3][pos] = 255;
                }
            }
        } else {
            for (i = 0; i < h; i++) {
                const int iwx = i * slinesizex;
                const int iwy = i * slinesizey;
                for (j = 0; j < w; j++) {
                    const int x = spx[iwx + j];
                    const int y = spy[iwy + j];
                    const int pos = y * dlinesize + x;

                    dst[0][pos] = FFMIN(dst[0][pos] + 1, 255);
                    dst[1][pos] = FFMIN(dst[1][pos] + 1, 255);
                    dst[2][pos] = FFMIN(dst[2][pos] + 1, 255);
                    if (dst[3])
                        dst[3][pos] = 255;
                }
            }
        }
        if (s->mode == COLOR) {
            for (i = 0; i < out->height; i++) {
                for (j = 0; j < out->width; j++) {
                    if (!dpd[i * out->linesize[pd] + j]) {
                        dpx[i * out->linesize[px] + j] = j;
                        dpy[i * out->linesize[py] + j] = i;
                    }
                }
            }
        }
        break;
    case COLOR2:
        if (s->is_yuv) {
            for (i = 0; i < h; i++) {
                const int iw1 = i * slinesizex;
                const int iw2 = i * slinesizey;
                for (j = 0; j < w; j++) {
                    const int x = spx[iw1 + j];
                    const int y = spy[iw2 + j];
                    const int pos = y * dlinesize + x;

                    if (!dpd[pos])
                        dpd[pos] = FFABS(128 - x) + FFABS(128 - y);
                    dpx[pos] = x;
                    dpy[pos] = y;
                    if (dst[3])
                        dst[3][pos] = 255;
                }
            }
        } else {
            for (i = 0; i < h; i++) {
                const int iw1 = i * slinesizex;
                const int iw2 = i * slinesizey;
                for (j = 0; j < w; j++) {
                    const int x = spx[iw1 + j];
                    const int y = spy[iw2 + j];
                    const int pos = y * dlinesize + x;

                    if (!dpd[pos])
                        dpd[pos] = FFMIN(x + y, 255);
                    dpx[pos] = x;
                    dpy[pos] = y;
                    if (dst[3])
                        dst[3][pos] = 255;
                }
            }
        }
        break;
    case COLOR3:
        for (i = 0; i < h; i++) {
            const int iw1 = i * slinesizex;
            const int iw2 = i * slinesizey;
            for (j = 0; j < w; j++) {
                const int x = spx[iw1 + j];
                const int y = spy[iw2 + j];
                const int pos = y * dlinesize + x;

                dpd[pos] = FFMIN(255, dpd[pos] + 1);
                dpx[pos] = x;
                dpy[pos] = y;
                if (dst[3])
                    dst[3][pos] = 255;
            }
        }
        break;
    default:
        av_assert0(0);
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx  = inlink->dst;
    VectorscopeContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    uint8_t **dst;
    int i, k;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    out->pts = in->pts;
    dst = out->data;

    for (k = 0; k < 4 && dst[k]; k++)
        for (i = 0; i < outlink->h ; i++)
            memset(dst[k] + i * out->linesize[k],
                   s->mode == COLOR && k == s->pd ? 0 : s->bg_color[k], outlink->w);

    vectorscope(s, in, out, s->pd);

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
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

AVFilter ff_vf_vectorscope = {
    .name          = "vectorscope",
    .description   = NULL_IF_CONFIG_SMALL("Video vectorscope."),
    .priv_size     = sizeof(VectorscopeContext),
    .priv_class    = &vectorscope_class,
    .query_formats = query_formats,
    .inputs        = inputs,
    .outputs       = outputs,
};
