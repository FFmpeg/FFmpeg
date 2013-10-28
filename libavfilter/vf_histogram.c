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
    int            ncomp;
    const uint8_t  *bg_color;
    const uint8_t  *fg_color;
    int            level_height;
    int            scale_height;
    int            step;
    int            waveform_mode;
    int            waveform_mirror;
    int            display_mode;
    int            levels_mode;
    const AVPixFmtDescriptor *desc;
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
    { "waveform_mirror", "set waveform mirroring", OFFSET(waveform_mirror), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS, "waveform_mirror"},
    { "display_mode", "set display mode", OFFSET(display_mode), AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "display_mode"},
    { "parade",  NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "display_mode" },
    { "overlay", NULL, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "display_mode" },
    { "levels_mode", "set levels mode", OFFSET(levels_mode), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS, "levels_mode"},
    { "linear",      NULL, 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "levels_mode" },
    { "logarithmic", NULL, 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "levels_mode" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(histogram);

static const enum AVPixelFormat color_pix_fmts[] = {
    AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat levels_pix_fmts[] = {
    AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP, AV_PIX_FMT_NONE
};

static const enum AVPixelFormat waveform_pix_fmts[] = {
     AV_PIX_FMT_GBRP,     AV_PIX_FMT_GBRAP,
     AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
     AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV440P,
     AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,
     AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUVJ420P,
     AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ444P,
     AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA420P,
     AV_PIX_FMT_GRAY8,
     AV_PIX_FMT_NONE
};

static int query_formats(AVFilterContext *ctx)
{
    HistogramContext *h = ctx->priv;
    const enum AVPixelFormat *pix_fmts;

    switch (h->mode) {
    case MODE_WAVEFORM:
        pix_fmts = waveform_pix_fmts;
        break;
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

    h->desc  = av_pix_fmt_desc_get(inlink->format);
    h->ncomp = h->desc->nb_components;

    switch (inlink->format) {
    case AV_PIX_FMT_GBRAP:
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

static void gen_waveform(HistogramContext *h, AVFrame *inpicref, AVFrame *outpicref,
                         int component, int intensity, int offset, int col_mode)
{
    const int plane = h->desc->comp[component].plane;
    const int mirror = h->waveform_mirror;
    const int is_chroma = (component == 1 || component == 2);
    const int shift_w = (is_chroma ? h->desc->log2_chroma_w : 0);
    const int shift_h = (is_chroma ? h->desc->log2_chroma_h : 0);
    const int src_linesize = inpicref->linesize[plane];
    const int dst_linesize = outpicref->linesize[plane];
    const int dst_signed_linesize = dst_linesize * (mirror == 1 ? -1 : 1);
    uint8_t *src_data = inpicref->data[plane];
    uint8_t *dst_data = outpicref->data[plane] + (col_mode ? (offset >> shift_h) * dst_linesize : offset >> shift_w);
    uint8_t * const dst_bottom_line = dst_data + dst_linesize * ((256 >> shift_h) - 1);
    uint8_t * const dst_line = (mirror ? dst_bottom_line : dst_data);
    const uint8_t max = 255 - intensity;
    const int src_h = FF_CEIL_RSHIFT(inpicref->height, shift_h);
    const int src_w = FF_CEIL_RSHIFT(inpicref->width, shift_w);
    uint8_t *dst, *p;
    int y;

    if (!col_mode && mirror)
        dst_data += 256 >> shift_w;
    for (y = 0; y < src_h; y++) {
        const uint8_t *src_data_end = src_data + src_w;
        dst = dst_line;
        for (p = src_data; p < src_data_end; p++) {
            uint8_t *target;
            if (col_mode) {
                target = dst++ + dst_signed_linesize * (*p >> shift_h);
            } else {
                if (mirror)
                    target = dst_data - (*p >> shift_w);
                else
                    target = dst_data + (*p >> shift_w);
            }
            if (*target <= max)
                *target += intensity;
            else
                *target = 255;
        }
        src_data += src_linesize;
        dst_data += dst_linesize;
    }
}


static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    HistogramContext *h   = inlink->dst->priv;
    AVFilterContext *ctx  = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    const uint8_t *src;
    uint8_t *dst;
    int i, j, k, l;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    out->pts = in->pts;

    for (k = 0; k < h->ncomp; k++) {
        const int is_chroma = (k == 1 || k == 2);
        const int dst_h = FF_CEIL_RSHIFT(outlink->h, (is_chroma ? h->desc->log2_chroma_h : 0));
        const int dst_w = FF_CEIL_RSHIFT(outlink->w, (is_chroma ? h->desc->log2_chroma_w : 0));
        for (i = 0; i < dst_h ; i++)
            memset(out->data[h->desc->comp[k].plane] +
                   i * out->linesize[h->desc->comp[k].plane],
                   h->bg_color[k], dst_w);
    }

    switch (h->mode) {
    case MODE_LEVELS:
        for (k = 0; k < h->ncomp; k++) {
            const int p = h->desc->comp[k].plane;
            const int start = k * (h->level_height + h->scale_height) * h->display_mode;
            double max_hval_log;
            unsigned max_hval = 0;

            for (i = 0; i < in->height; i++) {
                src = in->data[p] + i * in->linesize[p];
                for (j = 0; j < in->width; j++)
                    h->histogram[src[j]]++;
            }

            for (i = 0; i < 256; i++)
                max_hval = FFMAX(max_hval, h->histogram[i]);
            max_hval_log = log2(max_hval + 1);

            for (i = 0; i < outlink->w; i++) {
                int col_height;

                if (h->levels_mode)
                    col_height = round(h->level_height * (1. - (log2(h->histogram[i] + 1) / max_hval_log)));
                else
                    col_height = h->level_height - (h->histogram[i] * (int64_t)h->level_height + max_hval - 1) / max_hval;

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
            }

            memset(h->histogram, 0, 256 * sizeof(unsigned));
        }
        break;
    case MODE_WAVEFORM:
        for (k = 0; k < h->ncomp; k++) {
            const int offset = k * 256 * h->display_mode;
            gen_waveform(h, in, out, k, h->step, offset, h->waveform_mode);
        }
        break;
    case MODE_COLOR:
        for (i = 0; i < inlink->h; i++) {
            const int iw1 = i * in->linesize[1];
            const int iw2 = i * in->linesize[2];
            for (j = 0; j < inlink->w; j++) {
                const int pos = in->data[1][iw1 + j] * out->linesize[0] + in->data[2][iw2 + j];
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
            const int iw1 = i * in->linesize[1];
            const int iw2 = i * in->linesize[2];
            for (j = 0; j < inlink->w; j++) {
                const int u = in->data[1][iw1 + j];
                const int v = in->data[2][iw2 + j];
                const int pos = u * out->linesize[0] + v;
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

AVFilter avfilter_vf_histogram = {
    .name          = "histogram",
    .description   = NULL_IF_CONFIG_SMALL("Compute and draw a histogram."),
    .priv_size     = sizeof(HistogramContext),
    .query_formats = query_formats,
    .inputs        = inputs,
    .outputs       = outputs,
    .priv_class    = &histogram_class,
};
