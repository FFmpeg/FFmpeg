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

/**
 * @file
 * Display frame palette (AV_PIX_FMT_PAL8)
 */

#include "libavutil/avassert.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct {
    const AVClass *class;
    int size;
} ShowPaletteContext;

#define OFFSET(x) offsetof(ShowPaletteContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption showpalette_options[] = {
    { "s", "set pixel box size", OFFSET(size), AV_OPT_TYPE_INT, {.i64=30}, 1, 100, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(showpalette);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat in_fmts[]  = {AV_PIX_FMT_PAL8,  AV_PIX_FMT_NONE};
    static const enum AVPixelFormat out_fmts[] = {AV_PIX_FMT_RGB32, AV_PIX_FMT_NONE};
    int ret;
    AVFilterFormats *in  = ff_make_format_list(in_fmts);
    AVFilterFormats *out = ff_make_format_list(out_fmts);
    if (!in || !out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if ((ret = ff_formats_ref(in , &ctx->inputs[0]->out_formats)) < 0 ||
        (ret = ff_formats_ref(out, &ctx->outputs[0]->in_formats)) < 0)
        goto fail;
    return 0;
fail:
    if (in)
        av_freep(&in->formats);
    av_freep(&in);
    if (out)
        av_freep(&out->formats);
    av_freep(&out);
    return ret;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    const ShowPaletteContext *s = ctx->priv;
    outlink->w = outlink->h = 16 * s->size;
    return 0;
}

static int disp_palette(AVFrame *out, const AVFrame *in, int size)
{
    int x, y, i, j;
    uint32_t *dst = (uint32_t *)out->data[0];
    const int dst_linesize = out->linesize[0] >> 2;
    const uint32_t *pal = (uint32_t *)in->data[1];

    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++)
            for (j = 0; j < size; j++)
                for (i = 0; i < size; i++)
                    dst[(y*dst_linesize + x) * size + j*dst_linesize + i] = pal[y*16 + x];
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    int ret;
    AVFrame *out;
    AVFilterContext *ctx = inlink->dst;
    const ShowPaletteContext *s= ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);
    ret = disp_palette(out, in, s->size);
    av_frame_free(&in);
    return ret < 0 ? ret : ff_filter_frame(outlink, out);
}

static const AVFilterPad showpalette_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad showpalette_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter ff_vf_showpalette = {
    .name          = "showpalette",
    .description   = NULL_IF_CONFIG_SMALL("Display frame palette."),
    .priv_size     = sizeof(ShowPaletteContext),
    .query_formats = query_formats,
    .inputs        = showpalette_inputs,
    .outputs       = showpalette_outputs,
    .priv_class    = &showpalette_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
