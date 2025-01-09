/*
 * Copyright (c) 2012 Steven Robertson
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
 * copy an alpha component from another video's luma
 */

#include <string.h>

#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixfmt.h"
#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "filters.h"
#include "framesync.h"
#include "video.h"

enum { Y, U, V, A };

typedef struct AlphaMergeContext {
    const AVClass *class;

    int is_packed_rgb;
    uint8_t rgba_map[4];

    FFFrameSync fs;
} AlphaMergeContext;

static int do_alphamerge(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    AlphaMergeContext *s = ctx->priv;
    AVFrame *main_buf, *alpha_buf;
    int ret;

    ret = ff_framesync_dualinput_get_writable(fs, &main_buf, &alpha_buf);
    if (ret < 0)
        return ret;
    if (!alpha_buf)
        return ff_filter_frame(ctx->outputs[0], main_buf);

    if (alpha_buf->color_range == AVCOL_RANGE_MPEG) {
        av_log(ctx, AV_LOG_WARNING, "alpha plane color range tagged as %s, "
               "output will be wrong!\n",
               av_color_range_name(alpha_buf->color_range));
    }

    if (s->is_packed_rgb) {
        int x, y;
        uint8_t *pin, *pout;
        for (y = 0; y < main_buf->height; y++) {
            pin = alpha_buf->data[0] + y * alpha_buf->linesize[0];
            pout = main_buf->data[0] + y * main_buf->linesize[0] + s->rgba_map[A];
            for (x = 0; x < main_buf->width; x++) {
                *pout = *pin;
                pin += 1;
                pout += 4;
            }
        }
    } else {
        const int main_linesize = main_buf->linesize[A];
        const int alpha_linesize = alpha_buf->linesize[Y];
        av_image_copy_plane(main_buf->data[A], main_linesize,
                            alpha_buf->data[Y], alpha_linesize,
                            FFMIN(main_linesize, alpha_linesize), alpha_buf->height);
    }

    return ff_filter_frame(ctx->outputs[0], main_buf);
}

static av_cold int init(AVFilterContext *ctx)
{
    AlphaMergeContext *s = ctx->priv;

    s->fs.on_event = do_alphamerge;
    return 0;
}

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    static const enum AVPixelFormat main_fmts[] = {
        AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_GBRAP,
        AV_PIX_FMT_RGBA, AV_PIX_FMT_BGRA, AV_PIX_FMT_ARGB, AV_PIX_FMT_ABGR,
        AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat alpha_fmts[] = { AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE };
    int ret;

    ret = ff_formats_ref(ff_make_format_list(alpha_fmts),
                         &cfg_in[1]->formats);
    if (ret < 0)
        return ret;

    ret = ff_set_common_formats_from_list2(ctx, cfg_in, cfg_out, main_fmts);
    if (ret < 0)
        return ret;

    return 0;
}

static int config_input_main(AVFilterLink *inlink)
{
    AlphaMergeContext *s = inlink->dst->priv;
    s->is_packed_rgb =
        ff_fill_rgba_map(s->rgba_map, inlink->format) >= 0 &&
        inlink->format != AV_PIX_FMT_GBRAP;
    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    FilterLink *outl = ff_filter_link(outlink);
    AVFilterContext *ctx = outlink->src;
    AlphaMergeContext *s = ctx->priv;
    AVFilterLink *mainlink = ctx->inputs[0];
    FilterLink *ml = ff_filter_link(mainlink);
    AVFilterLink *alphalink = ctx->inputs[1];
    int ret;

    if (mainlink->w != alphalink->w || mainlink->h != alphalink->h) {
        av_log(ctx, AV_LOG_ERROR,
               "Input frame sizes do not match (%dx%d vs %dx%d).\n",
               mainlink->w, mainlink->h,
               alphalink->w, alphalink->h);
        return AVERROR(EINVAL);
    }

    if ((ret = ff_framesync_init_dualinput(&s->fs, ctx)) < 0)
        return ret;

    outlink->w = mainlink->w;
    outlink->h = mainlink->h;
    outlink->time_base = mainlink->time_base;
    outlink->sample_aspect_ratio = mainlink->sample_aspect_ratio;
    outl->frame_rate = ml->frame_rate;

    return ff_framesync_configure(&s->fs);
}

static int activate(AVFilterContext *ctx)
{
    AlphaMergeContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AlphaMergeContext *s = ctx->priv;

    ff_framesync_uninit(&s->fs);
}

static const AVFilterPad alphamerge_inputs[] = {
    {
        .name             = "main",
        .type             = AVMEDIA_TYPE_VIDEO,
        .config_props     = config_input_main,
    },{
        .name             = "alpha",
        .type             = AVMEDIA_TYPE_VIDEO,
    },
};

static const AVFilterPad alphamerge_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

static const AVOption alphamerge_options[] = {
    { NULL }
};

FRAMESYNC_DEFINE_CLASS(alphamerge, AlphaMergeContext, fs);

const FFFilter ff_vf_alphamerge = {
    .p.name         = "alphamerge",
    .p.description  = NULL_IF_CONFIG_SMALL("Copy the luma value of the second "
                      "input into the alpha channel of the first input."),
    .p.priv_class   = &alphamerge_class,
    .p.flags        = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
    .preinit        = alphamerge_framesync_preinit,
    .priv_size      = sizeof(AlphaMergeContext),
    .init           = init,
    FILTER_INPUTS(alphamerge_inputs),
    FILTER_OUTPUTS(alphamerge_outputs),
    FILTER_QUERY_FUNC2(query_formats),
    .uninit         = uninit,
    .activate       = activate,
};
