/*
 * Copyright (c) 2002 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2013 Paul B Mahol
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
 * (de)interleave fields filter
 */

#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "internal.h"

enum FilterMode {
    MODE_NONE,
    MODE_INTERLEAVE,
    MODE_DEINTERLEAVE
};

typedef struct IlContext {
    const AVClass *class;
    int luma_mode, chroma_mode, alpha_mode; ///<FilterMode
    int luma_swap, chroma_swap, alpha_swap;
    int nb_planes;
    int linesize[4], chroma_height;
    int has_alpha;
} IlContext;

#define OFFSET(x) offsetof(IlContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption il_options[] = {
    {"luma_mode",   "select luma mode", OFFSET(luma_mode), AV_OPT_TYPE_INT, {.i64=MODE_NONE}, MODE_NONE, MODE_DEINTERLEAVE, FLAGS, "luma_mode"},
    {"l",           "select luma mode", OFFSET(luma_mode), AV_OPT_TYPE_INT, {.i64=MODE_NONE}, MODE_NONE, MODE_DEINTERLEAVE, FLAGS, "luma_mode"},
    {"none",         NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_NONE},         0, 0, FLAGS, "luma_mode"},
    {"interleave",   NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_INTERLEAVE},   0, 0, FLAGS, "luma_mode"},
    {"i",            NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_INTERLEAVE},   0, 0, FLAGS, "luma_mode"},
    {"deinterleave", NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_DEINTERLEAVE}, 0, 0, FLAGS, "luma_mode"},
    {"d",            NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_DEINTERLEAVE}, 0, 0, FLAGS, "luma_mode"},
    {"chroma_mode", "select chroma mode", OFFSET(chroma_mode), AV_OPT_TYPE_INT, {.i64=MODE_NONE}, MODE_NONE, MODE_DEINTERLEAVE, FLAGS, "chroma_mode"},
    {"c",           "select chroma mode", OFFSET(chroma_mode), AV_OPT_TYPE_INT, {.i64=MODE_NONE}, MODE_NONE, MODE_DEINTERLEAVE, FLAGS, "chroma_mode"},
    {"none",         NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_NONE},         0, 0, FLAGS, "chroma_mode"},
    {"interleave",   NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_INTERLEAVE},   0, 0, FLAGS, "chroma_mode"},
    {"i",            NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_INTERLEAVE},   0, 0, FLAGS, "chroma_mode"},
    {"deinterleave", NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_DEINTERLEAVE}, 0, 0, FLAGS, "chroma_mode"},
    {"d",            NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_DEINTERLEAVE}, 0, 0, FLAGS, "chroma_mode"},
    {"alpha_mode", "select alpha mode", OFFSET(alpha_mode), AV_OPT_TYPE_INT, {.i64=MODE_NONE}, MODE_NONE, MODE_DEINTERLEAVE, FLAGS, "alpha_mode"},
    {"a",          "select alpha mode", OFFSET(alpha_mode), AV_OPT_TYPE_INT, {.i64=MODE_NONE}, MODE_NONE, MODE_DEINTERLEAVE, FLAGS, "alpha_mode"},
    {"none",         NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_NONE},         0, 0, FLAGS, "alpha_mode"},
    {"interleave",   NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_INTERLEAVE},   0, 0, FLAGS, "alpha_mode"},
    {"i",            NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_INTERLEAVE},   0, 0, FLAGS, "alpha_mode"},
    {"deinterleave", NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_DEINTERLEAVE}, 0, 0, FLAGS, "alpha_mode"},
    {"d",            NULL, 0, AV_OPT_TYPE_CONST, {.i64=MODE_DEINTERLEAVE}, 0, 0, FLAGS, "alpha_mode"},
    {"luma_swap",   "swap luma fields",   OFFSET(luma_swap),   AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"ls",          "swap luma fields",   OFFSET(luma_swap),   AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"chroma_swap", "swap chroma fields", OFFSET(chroma_swap), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"cs",          "swap chroma fields", OFFSET(chroma_swap), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"alpha_swap",  "swap alpha fields",  OFFSET(alpha_swap),  AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"as",          "swap alpha fields",  OFFSET(alpha_swap),  AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {NULL}
};

AVFILTER_DEFINE_CLASS(il);

static int query_formats(AVFilterContext *ctx)
{
    int reject_flags = AV_PIX_FMT_FLAG_PAL | AV_PIX_FMT_FLAG_HWACCEL;

    return ff_set_common_formats(ctx, ff_formats_pixdesc_filter(0, reject_flags));
}

static int config_input(AVFilterLink *inlink)
{
    IlContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int ret;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    s->has_alpha = !!(desc->flags & AV_PIX_FMT_FLAG_ALPHA);
    if ((ret = av_image_fill_linesizes(s->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    s->chroma_height = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);

    return 0;
}

static void interleave(uint8_t *dst, uint8_t *src, int w, int h,
                       int dst_linesize, int src_linesize,
                       enum FilterMode mode, int swap)
{
    const int a = swap;
    const int b = 1 - a;
    const int m = h >> 1;
    int y;

    switch (mode) {
    case MODE_DEINTERLEAVE:
        for (y = 0; y < m; y++) {
            memcpy(dst + dst_linesize *  y     , src + src_linesize * (y * 2 + a), w);
            memcpy(dst + dst_linesize * (y + m), src + src_linesize * (y * 2 + b), w);
        }
        break;
    case MODE_NONE:
        for (y = 0; y < m; y++) {
            memcpy(dst + dst_linesize *  y * 2     , src + src_linesize * (y * 2 + a), w);
            memcpy(dst + dst_linesize * (y * 2 + 1), src + src_linesize * (y * 2 + b), w);
        }
        break;
    case MODE_INTERLEAVE:
        for (y = 0; y < m; y++) {
            memcpy(dst + dst_linesize * (y * 2 + a), src + src_linesize *  y     , w);
            memcpy(dst + dst_linesize * (y * 2 + b), src + src_linesize * (y + m), w);
        }
        break;
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *inpicref)
{
    IlContext *s = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFrame *out;
    int comp;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&inpicref);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, inpicref);

    interleave(out->data[0], inpicref->data[0],
               s->linesize[0], inlink->h,
               out->linesize[0], inpicref->linesize[0],
               s->luma_mode, s->luma_swap);

    for (comp = 1; comp < (s->nb_planes - s->has_alpha); comp++) {
        interleave(out->data[comp], inpicref->data[comp],
                   s->linesize[comp], s->chroma_height,
                   out->linesize[comp], inpicref->linesize[comp],
                   s->chroma_mode, s->chroma_swap);
    }

    if (s->has_alpha) {
        comp = s->nb_planes - 1;
        interleave(out->data[comp], inpicref->data[comp],
                   s->linesize[comp], inlink->h,
                   out->linesize[comp], inpicref->linesize[comp],
                   s->alpha_mode, s->alpha_swap);
    }

    av_frame_free(&inpicref);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

const AVFilter ff_vf_il = {
    .name          = "il",
    .description   = NULL_IF_CONFIG_SMALL("Deinterleave or interleave fields."),
    .priv_size     = sizeof(IlContext),
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_QUERY_FUNC(query_formats),
    .priv_class    = &il_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
    .process_command = ff_filter_process_command,
};
