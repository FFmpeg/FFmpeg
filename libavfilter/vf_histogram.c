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
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

enum HistogramMode {
    MODE_LEVELS,
    MODE_WAVEFORM,
    MODE_COLOR,
    MODE_COLOR2,
    MODE_NB
};

typedef struct HistogramContext {
    const AVClass *class;               ///< AVClass context for log and options purpose
    enum HistogramMode mode;
    unsigned       histogram[256];
    unsigned       max_hval;
    int            ncomp;
    const uint8_t  *bg_color;
    const uint8_t  *fg_color;
    int            level_height;
    int            scale_height;
    int            step;
    int            waveform_mode;
    int            display_mode;
} HistogramContext;

#define OFFSET(x) offsetof(HistogramContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption histogram_options[] = {
    { "mode", "set histogram mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=MODE_LEVELS}, 0, MODE_NB-1, FLAGS, "mode"},
    { "levels", "standard histogram", 0, AV_OPT_TYPE_CONST, {.i64=MODE_LEVELS}, 0, 0, FLAGS, "mode" },
    { "waveform", "per row/column luminance graph", 0, AV_OPT_TYPE_CONST, {.i64=MODE_WAVEFORM}, 0, 0, FLAGS, "mode" },
    { "color", "chroma values in vectorscope", 0, AV_OPT_TYPE_CONST, {.i64=MODE_COLOR}, 0, 0, FLAGS, "mode" },
    { "color2", "chroma values in vectorscope", 0, AV_OPT_TYPE_CONST, {.i64=MODE_COLOR2}, 0, 0, FLAGS, "mode" },
    { "level_height", "set level height", OFFSET(level_height), AV_OPT_TYPE_INT, {.i64=200}, 50, 2048, FLAGS},
    { "scale_height", "set scale height", OFFSET(scale_height), AV_OPT_TYPE_INT, {.i64=12}, 0, 40, FLAGS},
    { "step", "set waveform step value", OFFSET(step), AV_OPT_TYPE_INT, {.i64=10}, 1, 255, FLAGS},
    { "waveform_mode", "set waveform mode", OFFSET(waveform_mode), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS, "waveform_mode"},
    { "row",   NULL, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "waveform_mode" },
    { "column", NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "waveform_mode" },
    { "display_mode", "set display mode", OFFSET(display_mode), AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "display_mode"},
    { "parade",  NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "display_mode" },
    { "overlay", NULL, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "display_mode" },
    { NULL },
};

AVFILTER_DEFINE_CLASS(histogram);

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    HistogramContext *h = ctx->priv;
    int ret;

    h->class = &histogram_class;
    av_opt_set_defaults(h);

    if ((ret = (av_set_options_string(h, args, "=", ":"))) < 0)
        return ret;

    return 0;
}

static const enum AVPixelFormat color_pix_fmts[] = {
    AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat levels_pix_fmts[] = {
    AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GBRP, AV_PIX_FMT_NONE
};

static int query_formats(AVFilterContext *ctx)
{
    HistogramContext *h = ctx->priv;
    const enum AVPixelFormat *pix_fmts;

    switch (h->mode) {
    case MODE_WAVEFORM:
    case MODE_LEVELS:
        pix_fmts = levels_pix_fmts;
        break;
    case MODE_COLOR:
    case MODE_COLOR2:
        pix_fmts = color_pix_fmts;
        break;
    default:
        av_assert0(0);
    }

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));

    return 0;
}

static const uint8_t black_yuva_color[4] = { 0, 127, 127, 255 };
static const uint8_t black_gbrp_color[4] = { 0, 0, 0, 255 };
static const uint8_t white_yuva_color[4] = { 255, 127, 127, 255 };
static const uint8_t white_gbrp_color[4] = { 255, 255, 255, 255 };

static int config_input(AVFilterLink *inlink)
{
    HistogramContext *h = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    h->ncomp = desc->nb_components;

    switch (inlink->format) {
    case AV_PIX_FMT_GBRP:
        h->bg_color = black_gbrp_color;
        h->fg_color = white_gbrp_color;
        break;
    default:
        h->bg_color = black_yuva_color;
        h->fg_color = white_yuva_color;
    }

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    HistogramContext *h = ctx->priv;

    switch (h->mode) {
    case MODE_LEVELS:
        outlink->w = 256;
        outlink->h = (h->level_height + h->scale_height) * FFMAX(h->ncomp * h->display_mode, 1);
        break;
    case MODE_WAVEFORM:
        if (h->waveform_mode)
            outlink->h = 256 * FFMAX(h->ncomp * h->display_mode, 1);
        else
            outlink->w = 256 * FFMAX(h->ncomp * h->display_mode, 1);
        break;
    case MODE_COLOR:
    case MODE_COLOR2:
        outlink->h = outlink->w = 256;
        break;
    default:
        av_assert0(0);
    }

    outlink->sample_aspect_ratio = (AVRational){1,1};

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFilterBufferRef *in)
{
    HistogramContext *h   = inlink->dst->priv;
    AVFilterContext *ctx  = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFilterBufferRef *out;
    const uint8_t *src;
    uint8_t *dst;
    int i, j, k, l, ret;

    out = ff_get_video_buffer(outlink, AV_PERM_WRITE, outlink->w, outlink->h);
    if (!out) {
        avfilter_unref_bufferp(&in);
        return AVERROR(ENOMEM);
    }

    out->pts = in->pts;
    out->pos = in->pos;

    for (k = 0; k < h->ncomp; k++)
        for (i = 0; i < outlink->h; i++)
            memset(out->data[k] + i * out->linesize[k], h->bg_color[k], outlink->w);

    switch (h->mode) {
    case MODE_LEVELS:
        for (k = 0; k < h->ncomp; k++) {
            int start = k * (h->level_height + h->scale_height) * h->display_mode;

            for (i = 0; i < in->video->h; i++) {
                src = in->data[k] + i * in->linesize[k];
                for (j = 0; j < in->video->w; j++)
                    h->histogram[src[j]]++;
            }

            for (i = 0; i < 256; i++)
                h->max_hval = FFMAX(h->max_hval, h->histogram[i]);

            for (i = 0; i < outlink->w; i++) {
                int col_height = h->level_height - (float)h->histogram[i] / h->max_hval * h->level_height;

                for (j = h->level_height - 1; j >= col_height; j--) {
                    if (h->display_mode) {
                        for (l = 0; l < h->ncomp; l++)
                            out->data[l][(j + start) * out->linesize[l] + i] = h->fg_color[l];
                    } else {
                        out->data[k][(j + start) * out->linesize[k] + i] = 255;
                    }
                }
                for (j = h->level_height + h->scale_height - 1; j >= h->level_height; j--)
                    out->data[k][(j + start) * out->linesize[k] + i] = i;
            }

            memset(h->histogram, 0, 256 * sizeof(unsigned));
            h->max_hval = 0;
        }
        break;
    case MODE_WAVEFORM:
        if (h->waveform_mode) {
            for (k = 0; k < h->ncomp; k++) {
                int offset = k * 256 * h->display_mode;
                for (i = 0; i < inlink->w; i++) {
                    for (j = 0; j < inlink->h; j++) {
                        int pos = (offset +
                                   in->data[k][j * in->linesize[k] + i]) *
                                  out->linesize[k] + i;
                        unsigned value = out->data[k][pos];
                        value = FFMIN(value + h->step, 255);
                        out->data[k][pos] = value;
                    }
                }
            }
        } else {
            for (k = 0; k < h->ncomp; k++) {
                int offset = k * 256 * h->display_mode;
                for (i = 0; i < inlink->h; i++) {
                    src = in ->data[k] + i * in ->linesize[k];
                    dst = out->data[k] + i * out->linesize[k];
                    for (j = 0; j < inlink->w; j++) {
                        int pos = src[j] + offset;
                        unsigned value = dst[pos];
                        value = FFMIN(value + h->step, 255);
                        dst[pos] = value;
                    }
                }
            }
        }
        break;
    case MODE_COLOR:
        for (i = 0; i < inlink->h; i++) {
            int iw1 = i * in->linesize[1];
            int iw2 = i * in->linesize[2];
            for (j = 0; j < inlink->w; j++) {
                int pos = in->data[1][iw1 + j] * out->linesize[0] + in->data[2][iw2 + j];
                if (out->data[0][pos] < 255)
                    out->data[0][pos]++;
            }
        }
        for (i = 0; i < 256; i++) {
            dst = out->data[0] + i * out->linesize[0];
            for (j = 0; j < 256; j++) {
                if (!dst[j]) {
                    out->data[1][i * out->linesize[0] + j] = i;
                    out->data[2][i * out->linesize[0] + j] = j;
                }
            }
        }
        break;
    case MODE_COLOR2:
        for (i = 0; i < inlink->h; i++) {
            int iw1 = i * in->linesize[1];
            int iw2 = i * in->linesize[2];
            for (j = 0; j < inlink->w; j++) {
                int u = in->data[1][iw1 + j];
                int v = in->data[2][iw2 + j];
                int pos = u * out->linesize[0] + v;
                if (!out->data[0][pos])
                    out->data[0][pos] = FFABS(128 - u) + FFABS(128 - v);
                out->data[1][pos] = u;
                out->data[2][pos] = v;
            }
        }
        break;
    default:
        av_assert0(0);
    }

    ret = ff_filter_frame(outlink, out);
    avfilter_unref_bufferp(&in);
    if (ret < 0)
        return ret;
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    HistogramContext *h = ctx->priv;

    av_opt_free(h);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
        .min_perms    = AV_PERM_READ,
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

AVFilter avfilter_vf_histogram = {
    .name          = "histogram",
    .description   = NULL_IF_CONFIG_SMALL("Compute and draw a histogram."),
    .priv_size     = sizeof(HistogramContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = inputs,
    .outputs       = outputs,
    .priv_class    = &histogram_class,
};
