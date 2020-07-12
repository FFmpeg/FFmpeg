/*
 * Copyright (c) 2012-2019 Paul B Mahol
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
#include "libavutil/colorspace.h"
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
    int            thistogram;
    int            envelope;
    unsigned       histogram[256*256];
    int            histogram_size;
    int            width;
    int            x_pos;
    int            mult;
    int            ncomp;
    int            dncomp;
    uint8_t        bg_color[4];
    uint8_t        fg_color[4];
    uint8_t        envelope_rgba[4];
    uint8_t        envelope_color[4];
    int            level_height;
    int            scale_height;
    int            display_mode;
    int            levels_mode;
    const AVPixFmtDescriptor *desc, *odesc;
    int            components;
    float          fgopacity;
    float          bgopacity;
    int            planewidth[4];
    int            planeheight[4];
    int            start[4];
    AVFrame       *out;
} HistogramContext;

#define OFFSET(x) offsetof(HistogramContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

#define COMMON_OPTIONS \
    { "display_mode", "set display mode", OFFSET(display_mode), AV_OPT_TYPE_INT, {.i64=2}, 0, 2, FLAGS, "display_mode"}, \
    { "d",            "set display mode", OFFSET(display_mode), AV_OPT_TYPE_INT, {.i64=2}, 0, 2, FLAGS, "display_mode"}, \
        { "overlay", NULL, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "display_mode" }, \
        { "parade",  NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "display_mode" }, \
        { "stack",   NULL, 0, AV_OPT_TYPE_CONST, {.i64=2}, 0, 0, FLAGS, "display_mode" }, \
    { "levels_mode", "set levels mode", OFFSET(levels_mode), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS, "levels_mode"}, \
    { "m",           "set levels mode", OFFSET(levels_mode), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS, "levels_mode"}, \
        { "linear",      NULL, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "levels_mode" }, \
        { "logarithmic", NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "levels_mode" }, \
    { "components", "set color components to display", OFFSET(components), AV_OPT_TYPE_INT, {.i64=7}, 1, 15, FLAGS}, \
    { "c",          "set color components to display", OFFSET(components), AV_OPT_TYPE_INT, {.i64=7}, 1, 15, FLAGS},

static const AVOption histogram_options[] = {
    { "level_height", "set level height", OFFSET(level_height), AV_OPT_TYPE_INT, {.i64=200}, 50, 2048, FLAGS},
    { "scale_height", "set scale height", OFFSET(scale_height), AV_OPT_TYPE_INT, {.i64=12}, 0, 40, FLAGS},
    COMMON_OPTIONS
    { "fgopacity", "set foreground opacity", OFFSET(fgopacity), AV_OPT_TYPE_FLOAT, {.dbl=0.7}, 0, 1, FLAGS},
    { "f",         "set foreground opacity", OFFSET(fgopacity), AV_OPT_TYPE_FLOAT, {.dbl=0.7}, 0, 1, FLAGS},
    { "bgopacity", "set background opacity", OFFSET(bgopacity), AV_OPT_TYPE_FLOAT, {.dbl=0.5}, 0, 1, FLAGS},
    { "b",         "set background opacity", OFFSET(bgopacity), AV_OPT_TYPE_FLOAT, {.dbl=0.5}, 0, 1, FLAGS},
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
    AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV440P12,
    AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA444P12,
    AV_PIX_FMT_GBRAP,    AV_PIX_FMT_GBRP,
    AV_PIX_FMT_GBRP9,    AV_PIX_FMT_GBRP10,  AV_PIX_FMT_GBRAP10,
    AV_PIX_FMT_GBRP12,   AV_PIX_FMT_GBRAP12,
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

static const enum AVPixelFormat levels_out_yuv12_pix_fmts[] = {
    AV_PIX_FMT_YUVA444P12, AV_PIX_FMT_YUV444P12,
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
    AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRAP10,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat levels_out_rgb12_pix_fmts[] = {
    AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRAP12,
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
    else if (rgb && bits == 12)
        out_pix_fmts = levels_out_rgb12_pix_fmts;
    else if (bits == 8)
        out_pix_fmts = levels_out_yuv8_pix_fmts;
    else if (bits == 9)
        out_pix_fmts = levels_out_yuv9_pix_fmts;
    else if (bits == 10)
        out_pix_fmts = levels_out_yuv10_pix_fmts;
    else if (bits == 12)
        out_pix_fmts = levels_out_yuv12_pix_fmts;
    else
        return AVERROR(EAGAIN);
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
    HistogramContext *s = inlink->dst->priv;

    s->desc  = av_pix_fmt_desc_get(inlink->format);
    s->ncomp = s->desc->nb_components;
    s->histogram_size = 1 << s->desc->comp[0].depth;
    s->mult = s->histogram_size / 256;

    switch (inlink->format) {
    case AV_PIX_FMT_GBRAP12:
    case AV_PIX_FMT_GBRP12:
    case AV_PIX_FMT_GBRAP10:
    case AV_PIX_FMT_GBRP10:
    case AV_PIX_FMT_GBRP9:
    case AV_PIX_FMT_GBRAP:
    case AV_PIX_FMT_GBRP:
        memcpy(s->bg_color, black_gbrp_color, 4);
        memcpy(s->fg_color, white_gbrp_color, 4);
        s->start[0] = s->start[1] = s->start[2] = s->start[3] = 0;
        memcpy(s->envelope_color, s->envelope_rgba, 4);
        break;
    default:
        memcpy(s->bg_color, black_yuva_color, 4);
        memcpy(s->fg_color, white_yuva_color, 4);
        s->start[0] = s->start[3] = 0;
        s->start[1] = s->start[2] = s->histogram_size / 2;
        s->envelope_color[0] = RGB_TO_Y_BT709(s->envelope_rgba[0], s->envelope_rgba[1], s->envelope_rgba[2]);
        s->envelope_color[1] = RGB_TO_U_BT709(s->envelope_rgba[0], s->envelope_rgba[1], s->envelope_rgba[2], 0);
        s->envelope_color[2] = RGB_TO_V_BT709(s->envelope_rgba[0], s->envelope_rgba[1], s->envelope_rgba[2], 0);
        s->envelope_color[3] = s->envelope_rgba[3];
    }

    s->fg_color[3] = s->fgopacity * 255;
    s->bg_color[3] = s->bgopacity * 255;

    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, s->desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->planewidth[1]  = s->planewidth[2]  = AV_CEIL_RSHIFT(inlink->w, s->desc->log2_chroma_w);
    s->planewidth[0]  = s->planewidth[3]  = inlink->w;

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    HistogramContext *s = ctx->priv;
    int ncomp = 0, i;

    if (!strcmp(ctx->filter->name, "thistogram"))
        s->thistogram = 1;

    for (i = 0; i < s->ncomp; i++) {
        if ((1 << i) & s->components)
            ncomp++;
    }

    if (s->thistogram) {
        if (!s->width)
            s->width = ctx->inputs[0]->w;
        outlink->w = s->width * FFMAX(ncomp * (s->display_mode == 1), 1);
        outlink->h = s->histogram_size * FFMAX(ncomp * (s->display_mode == 2), 1);
    } else {
        outlink->w = s->histogram_size * FFMAX(ncomp * (s->display_mode == 1), 1);
        outlink->h = (s->level_height + s->scale_height) * FFMAX(ncomp * (s->display_mode == 2), 1);
    }

    s->odesc = av_pix_fmt_desc_get(outlink->format);
    s->dncomp = s->odesc->nb_components;
    outlink->sample_aspect_ratio = (AVRational){1,1};

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    HistogramContext *s   = inlink->dst->priv;
    AVFilterContext *ctx  = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out = s->out;
    int i, j, k, l, m;

    if (!s->thistogram || !out) {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        s->out = out;

        for (k = 0; k < 4 && out->data[k]; k++) {
            const int is_chroma = (k == 1 || k == 2);
            const int dst_h = AV_CEIL_RSHIFT(outlink->h, (is_chroma ? s->odesc->log2_chroma_h : 0));
            const int dst_w = AV_CEIL_RSHIFT(outlink->w, (is_chroma ? s->odesc->log2_chroma_w : 0));

            if (s->histogram_size <= 256) {
                for (i = 0; i < dst_h ; i++)
                    memset(out->data[s->odesc->comp[k].plane] +
                           i * out->linesize[s->odesc->comp[k].plane],
                           s->bg_color[k], dst_w);
            } else {
                const int mult = s->mult;

                for (i = 0; i < dst_h ; i++)
                    for (j = 0; j < dst_w; j++)
                        AV_WN16(out->data[s->odesc->comp[k].plane] +
                            i * out->linesize[s->odesc->comp[k].plane] + j * 2,
                            s->bg_color[k] * mult);
            }
        }
    }

    for (m = 0, k = 0; k < s->ncomp; k++) {
        const int p = s->desc->comp[k].plane;
        const int max_value = s->histogram_size - 1 - s->start[p];
        const int height = s->planeheight[p];
        const int width = s->planewidth[p];
        double max_hval_log;
        unsigned max_hval = 0;
        int starty, startx;

        if (!((1 << k) & s->components))
            continue;
        if (s->thistogram) {
            starty = m * s->histogram_size * (s->display_mode == 2);
            startx = m++ * s->width * (s->display_mode == 1);
        } else {
            startx = m * s->histogram_size * (s->display_mode == 1);
            starty = m++ * (s->level_height + s->scale_height) * (s->display_mode == 2);
        }

        if (s->histogram_size <= 256) {
            for (i = 0; i < height; i++) {
                const uint8_t *src = in->data[p] + i * in->linesize[p];
                for (j = 0; j < width; j++)
                    s->histogram[src[j]]++;
            }
        } else {
            for (i = 0; i < height; i++) {
                const uint16_t *src = (const uint16_t *)(in->data[p] + i * in->linesize[p]);
                for (j = 0; j < width; j++)
                    s->histogram[src[j]]++;
            }
        }

        for (i = 0; i < s->histogram_size; i++)
            max_hval = FFMAX(max_hval, s->histogram[i]);
        max_hval_log = log2(max_hval + 1);

        if (s->thistogram) {
            int minh = s->histogram_size - 1, maxh = 0;

            for (int i = 0; i < s->histogram_size; i++) {
                int idx = s->histogram_size - i - 1;
                int value = s->start[p];

                if (s->envelope && s->histogram[idx]) {
                    minh = FFMIN(minh, i);
                    maxh = FFMAX(maxh, i);
                }

                if (s->levels_mode)
                    value += lrint(max_value * (log2(s->histogram[idx] + 1) / max_hval_log));
                else
                    value += lrint(max_value * s->histogram[idx] / (float)max_hval);

                if (s->histogram_size <= 256) {
                    s->out->data[p][(i + starty) * s->out->linesize[p] + startx + s->x_pos] = value;
                } else {
                    AV_WN16(s->out->data[p] + (i + starty) * s->out->linesize[p] + startx * 2 + s->x_pos * 2, value);
                }
            }

            if (s->envelope) {
                if (s->histogram_size <= 256) {
                    s->out->data[0][(minh + starty) * s->out->linesize[p] + startx + s->x_pos] = s->envelope_color[0];
                    s->out->data[0][(maxh + starty) * s->out->linesize[p] + startx + s->x_pos] = s->envelope_color[0];
                    if (s->dncomp >= 3) {
                        s->out->data[1][(minh + starty) * s->out->linesize[p] + startx + s->x_pos] = s->envelope_color[1];
                        s->out->data[2][(minh + starty) * s->out->linesize[p] + startx + s->x_pos] = s->envelope_color[2];
                        s->out->data[1][(maxh + starty) * s->out->linesize[p] + startx + s->x_pos] = s->envelope_color[1];
                        s->out->data[2][(maxh + starty) * s->out->linesize[p] + startx + s->x_pos] = s->envelope_color[2];
                    }
                } else {
                    const int mult = s->mult;

                    AV_WN16(s->out->data[0] + (minh + starty) * s->out->linesize[p] + startx * 2 + s->x_pos * 2, s->envelope_color[0] * mult);
                    AV_WN16(s->out->data[0] + (maxh + starty) * s->out->linesize[p] + startx * 2 + s->x_pos * 2, s->envelope_color[0] * mult);
                    if (s->dncomp >= 3) {
                        AV_WN16(s->out->data[1] + (minh + starty) * s->out->linesize[p] + startx * 2 + s->x_pos * 2, s->envelope_color[1] * mult);
                        AV_WN16(s->out->data[2] + (minh + starty) * s->out->linesize[p] + startx * 2 + s->x_pos * 2, s->envelope_color[2] * mult);
                        AV_WN16(s->out->data[1] + (maxh + starty) * s->out->linesize[p] + startx * 2 + s->x_pos * 2, s->envelope_color[1] * mult);
                        AV_WN16(s->out->data[2] + (maxh + starty) * s->out->linesize[p] + startx * 2 + s->x_pos * 2, s->envelope_color[2] * mult);
                    }
                }
            }
        } else {
            for (i = 0; i < s->histogram_size; i++) {
                int col_height;

                if (s->levels_mode)
                    col_height = lrint(s->level_height * (1. - (log2(s->histogram[i] + 1) / max_hval_log)));
                else
                    col_height = s->level_height - (s->histogram[i] * (int64_t)s->level_height + max_hval - 1) / max_hval;

                if (s->histogram_size <= 256) {
                    for (j = s->level_height - 1; j >= col_height; j--) {
                        if (s->display_mode) {
                            for (l = 0; l < s->dncomp; l++)
                                out->data[l][(j + starty) * out->linesize[l] + startx + i] = s->fg_color[l];
                        } else {
                            out->data[p][(j + starty) * out->linesize[p] + startx + i] = 255;
                        }
                    }
                    for (j = s->level_height + s->scale_height - 1; j >= s->level_height; j--)
                        out->data[p][(j + starty) * out->linesize[p] + startx + i] = i;
                } else {
                    const int mult = s->mult;

                    for (j = s->level_height - 1; j >= col_height; j--) {
                        if (s->display_mode) {
                            for (l = 0; l < s->dncomp; l++)
                                AV_WN16(out->data[l] + (j + starty) * out->linesize[l] + startx * 2 + i * 2, s->fg_color[l] * mult);
                        } else {
                            AV_WN16(out->data[p] + (j + starty) * out->linesize[p] + startx * 2 + i * 2, 255 * mult);
                        }
                    }
                    for (j = s->level_height + s->scale_height - 1; j >= s->level_height; j--)
                        AV_WN16(out->data[p] + (j + starty) * out->linesize[p] + startx * 2 + i * 2, i);
                }
            }
        }

        memset(s->histogram, 0, s->histogram_size * sizeof(unsigned));
    }

    out->pts = in->pts;
    av_frame_free(&in);
    s->x_pos++;
    if (s->x_pos >= s->width)
        s->x_pos = 0;

    if (s->thistogram) {
        AVFrame *clone = av_frame_clone(out);

        if (!clone)
            return AVERROR(ENOMEM);
        return ff_filter_frame(outlink, clone);
    }
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

#if CONFIG_HISTOGRAM_FILTER

AVFilter ff_vf_histogram = {
    .name          = "histogram",
    .description   = NULL_IF_CONFIG_SMALL("Compute and draw a histogram."),
    .priv_size     = sizeof(HistogramContext),
    .query_formats = query_formats,
    .inputs        = inputs,
    .outputs       = outputs,
    .priv_class    = &histogram_class,
};

#endif /* CONFIG_HISTOGRAM_FILTER */

#if CONFIG_THISTOGRAM_FILTER

static const AVOption thistogram_options[] = {
    { "width", "set width", OFFSET(width), AV_OPT_TYPE_INT, {.i64=0}, 0, 8192, FLAGS},
    { "w",     "set width", OFFSET(width), AV_OPT_TYPE_INT, {.i64=0}, 0, 8192, FLAGS},
    COMMON_OPTIONS
    { "bgopacity", "set background opacity", OFFSET(bgopacity), AV_OPT_TYPE_FLOAT, {.dbl=0.9}, 0, 1, FLAGS},
    { "b",         "set background opacity", OFFSET(bgopacity), AV_OPT_TYPE_FLOAT, {.dbl=0.9}, 0, 1, FLAGS},
    { "envelope", "display envelope", OFFSET(envelope), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { "e",        "display envelope", OFFSET(envelope), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { "ecolor", "set envelope color", OFFSET(envelope_rgba), AV_OPT_TYPE_COLOR, {.str="gold"}, 0, 0, FLAGS },
    { "ec",     "set envelope color", OFFSET(envelope_rgba), AV_OPT_TYPE_COLOR, {.str="gold"}, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(thistogram);

AVFilter ff_vf_thistogram = {
    .name          = "thistogram",
    .description   = NULL_IF_CONFIG_SMALL("Compute and draw a temporal histogram."),
    .priv_size     = sizeof(HistogramContext),
    .query_formats = query_formats,
    .inputs        = inputs,
    .outputs       = outputs,
    .priv_class    = &thistogram_class,
};

#endif /* CONFIG_THISTOGRAM_FILTER */
