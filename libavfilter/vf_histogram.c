/*
 * Copyright (c) 2012-2013 Paul B Mahol
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
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct HistogramContext {
    const AVClass *class;               ///< AVClass context for log and options purpose
    unsigned       histogram[256*256];
    int            histogram_size;
    int            mult;
    int            ncomp;
    const uint8_t  *bg_color;
    const uint8_t  *fg_color;
    int            level_height;
    int            scale_height;
    int            display_mode;
    int            levels_mode;
    const AVPixFmtDescriptor *desc, *odesc;
    int            components;
    int            planewidth[4];
    int            planeheight[4];
} HistogramContext;

#define OFFSET(x) offsetof(HistogramContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption histogram_options[] = {
    { "level_height", "set level height", OFFSET(level_height), AV_OPT_TYPE_INT, {.i64=200}, 50, 2048, FLAGS},
    { "scale_height", "set scale height", OFFSET(scale_height), AV_OPT_TYPE_INT, {.i64=12}, 0, 40, FLAGS},
    { "display_mode", "set display mode", OFFSET(display_mode), AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "display_mode"},
    { "parade",  NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "display_mode" },
    { "overlay", NULL, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "display_mode" },
    { "levels_mode", "set levels mode", OFFSET(levels_mode), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS, "levels_mode"},
    { "linear",      NULL, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "levels_mode" },
    { "logarithmic", NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "levels_mode" },
    { "components", "set color components to display", OFFSET(components), AV_OPT_TYPE_INT, {.i64=7}, 1, 15, FLAGS},
    { NULL }
};

AVFILTER_DEFINE_CLASS(histogram);

static const enum AVPixelFormat levels_in_pix_fmts[] = {
    AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUVJ411P,
    AV_PIX_FMT_YUV440P,  AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA444P9,
    AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
    AV_PIX_FMT_GBRAP,    AV_PIX_FMT_GBRP,
    AV_PIX_FMT_GBRP9,    AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat levels_out_yuv8_pix_fmts[] = {
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat levels_out_yuv9_pix_fmts[] = {
    AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat levels_out_yuv10_pix_fmts[] = {
    AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat levels_out_rgb8_pix_fmts[] = {
    AV_PIX_FMT_GBRAP,    AV_PIX_FMT_GBRP,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat levels_out_rgb9_pix_fmts[] = {
    AV_PIX_FMT_GBRP9,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat levels_out_rgb10_pix_fmts[] = {
    AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_NONE
};

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *avff;
    const AVPixFmtDescriptor *desc;
    const enum AVPixelFormat *out_pix_fmts;
    int rgb, i, bits;
    int ret;

    if (!ctx->inputs[0]->in_formats ||
        !ctx->inputs[0]->in_formats->nb_formats) {
        return AVERROR(EAGAIN);
    }

    if (!ctx->inputs[0]->out_formats)
        if ((ret = ff_formats_ref(ff_make_format_list(levels_in_pix_fmts), &ctx->inputs[0]->out_formats)) < 0)
            return ret;
    avff = ctx->inputs[0]->in_formats;
    desc = av_pix_fmt_desc_get(avff->formats[0]);
    rgb = desc->flags & AV_PIX_FMT_FLAG_RGB;
    bits = desc->comp[0].depth;
    for (i = 1; i < avff->nb_formats; i++) {
        desc = av_pix_fmt_desc_get(avff->formats[i]);
        if ((rgb != (desc->flags & AV_PIX_FMT_FLAG_RGB)) ||
            (bits != desc->comp[0].depth))
            return AVERROR(EAGAIN);
    }

    if (rgb && bits == 8)
        out_pix_fmts = levels_out_rgb8_pix_fmts;
    else if (rgb && bits == 9)
        out_pix_fmts = levels_out_rgb9_pix_fmts;
    else if (rgb && bits == 10)
        out_pix_fmts = levels_out_rgb10_pix_fmts;
    else if (bits == 8)
        out_pix_fmts = levels_out_yuv8_pix_fmts;
    else if (bits == 9)
        out_pix_fmts = levels_out_yuv9_pix_fmts;
    else // if (bits == 10)
        out_pix_fmts = levels_out_yuv10_pix_fmts;
    if ((ret = ff_formats_ref(ff_make_format_list(out_pix_fmts), &ctx->outputs[0]->in_formats)) < 0)
        return ret;

    return 0;
}

static const uint8_t black_yuva_color[4] = { 0, 127, 127, 255 };
static const uint8_t black_gbrp_color[4] = { 0, 0, 0, 255 };
static const uint8_t white_yuva_color[4] = { 255, 127, 127, 255 };
static const uint8_t white_gbrp_color[4] = { 255, 255, 255, 255 };

static int config_input(AVFilterLink *inlink)
{
    HistogramContext *h = inlink->dst->priv;

    h->desc  = av_pix_fmt_desc_get(inlink->format);
    h->ncomp = h->desc->nb_components;
    h->histogram_size = 1 << h->desc->comp[0].depth;
    h->mult = h->histogram_size / 256;

    switch (inlink->format) {
    case AV_PIX_FMT_GBRP10:
    case AV_PIX_FMT_GBRP9:
    case AV_PIX_FMT_GBRAP:
    case AV_PIX_FMT_GBRP:
        h->bg_color = black_gbrp_color;
        h->fg_color = white_gbrp_color;
        break;
    default:
        h->bg_color = black_yuva_color;
        h->fg_color = white_yuva_color;
    }

    h->planeheight[1] = h->planeheight[2] = FF_CEIL_RSHIFT(inlink->h, h->desc->log2_chroma_h);
    h->planeheight[0] = h->planeheight[3] = inlink->h;
    h->planewidth[1]  = h->planewidth[2]  = FF_CEIL_RSHIFT(inlink->w, h->desc->log2_chroma_w);
    h->planewidth[0]  = h->planewidth[3]  = inlink->w;

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    HistogramContext *h = ctx->priv;
    int ncomp = 0, i;

    for (i = 0; i < h->ncomp; i++) {
        if ((1 << i) & h->components)
            ncomp++;
    }
    outlink->w = h->histogram_size;
    outlink->h = (h->level_height + h->scale_height) * FFMAX(ncomp * h->display_mode, 1);

    h->odesc = av_pix_fmt_desc_get(outlink->format);
    outlink->sample_aspect_ratio = (AVRational){1,1};

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    HistogramContext *h   = inlink->dst->priv;
    AVFilterContext *ctx  = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    int i, j, k, l, m;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    out->pts = in->pts;

    for (k = 0; k < 4 && out->data[k]; k++) {
        const int is_chroma = (k == 1 || k == 2);
        const int dst_h = FF_CEIL_RSHIFT(outlink->h, (is_chroma ? h->odesc->log2_chroma_h : 0));
        const int dst_w = FF_CEIL_RSHIFT(outlink->w, (is_chroma ? h->odesc->log2_chroma_w : 0));

        if (h->histogram_size <= 256) {
            for (i = 0; i < dst_h ; i++)
                memset(out->data[h->odesc->comp[k].plane] +
                       i * out->linesize[h->odesc->comp[k].plane],
                       h->bg_color[k], dst_w);
        } else {
            const int mult = h->mult;

            for (i = 0; i < dst_h ; i++)
                for (j = 0; j < dst_w; j++)
                    AV_WN16(out->data[h->odesc->comp[k].plane] +
                        i * out->linesize[h->odesc->comp[k].plane] + j * 2,
                        h->bg_color[k] * mult);
        }
    }

    for (m = 0, k = 0; k < h->ncomp; k++) {
        const int p = h->desc->comp[k].plane;
        const int height = h->planeheight[p];
        const int width = h->planewidth[p];
        double max_hval_log;
        unsigned max_hval = 0;
        int start;

        if (!((1 << k) & h->components))
            continue;
        start = m++ * (h->level_height + h->scale_height) * h->display_mode;

        if (h->histogram_size <= 256) {
            for (i = 0; i < height; i++) {
                const uint8_t *src = in->data[p] + i * in->linesize[p];
                for (j = 0; j < width; j++)
                    h->histogram[src[j]]++;
            }
        } else {
            for (i = 0; i < height; i++) {
                const uint16_t *src = (const uint16_t *)(in->data[p] + i * in->linesize[p]);
                for (j = 0; j < width; j++)
                    h->histogram[src[j]]++;
            }
        }

        for (i = 0; i < h->histogram_size; i++)
            max_hval = FFMAX(max_hval, h->histogram[i]);
        max_hval_log = log2(max_hval + 1);

        for (i = 0; i < outlink->w; i++) {
            int col_height;

            if (h->levels_mode)
                col_height = lrint(h->level_height * (1. - (log2(h->histogram[i] + 1) / max_hval_log)));
            else
                col_height = h->level_height - (h->histogram[i] * (int64_t)h->level_height + max_hval - 1) / max_hval;

            if (h->histogram_size <= 256) {
                for (j = h->level_height - 1; j >= col_height; j--) {
                    if (h->display_mode) {
                        for (l = 0; l < h->ncomp; l++)
                            out->data[l][(j + start) * out->linesize[l] + i] = h->fg_color[l];
                    } else {
                        out->data[p][(j + start) * out->linesize[p] + i] = 255;
                    }
                }
                for (j = h->level_height + h->scale_height - 1; j >= h->level_height; j--)
                    out->data[p][(j + start) * out->linesize[p] + i] = i;
            } else {
                const int mult = h->mult;

                for (j = h->level_height - 1; j >= col_height; j--) {
                    if (h->display_mode) {
                        for (l = 0; l < h->ncomp; l++)
                            AV_WN16(out->data[l] + (j + start) * out->linesize[l] + i * 2, h->fg_color[l] * mult);
                    } else {
                        AV_WN16(out->data[p] + (j + start) * out->linesize[p] + i * 2, 255 * mult);
                    }
                }
                for (j = h->level_height + h->scale_height - 1; j >= h->level_height; j--)
                    AV_WN16(out->data[p] + (j + start) * out->linesize[p] + i * 2, i);
            }
        }

        memset(h->histogram, 0, h->histogram_size * sizeof(unsigned));
    }

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

AVFilter ff_vf_histogram = {
    .name          = "histogram",
    .description   = NULL_IF_CONFIG_SMALL("Compute and draw a histogram."),
    .priv_size     = sizeof(HistogramContext),
    .query_formats = query_formats,
    .inputs        = inputs,
    .outputs       = outputs,
    .priv_class    = &histogram_class,
};
