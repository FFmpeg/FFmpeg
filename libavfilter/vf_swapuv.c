/*
 * Copyright (c) 2002 Michael Niedermayer <michaelni@gmx.at>
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
 * swap UV filter
 */

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

static AVFilterBufferRef *get_video_buffer(AVFilterLink *link, int perms,
                                           int w, int h)
{
    AVFilterBufferRef *picref =
        ff_default_get_video_buffer(link, perms, w, h);

    FFSWAP(uint8_t*, picref->data[1], picref->data[2]);
    FFSWAP(int, picref->linesize[1], picref->linesize[2]);

    return picref;
}

static int filter_frame(AVFilterLink *link, AVFilterBufferRef *inpicref)
{
    FFSWAP(uint8_t*, inpicref->data[1], inpicref->data[2]);
    FFSWAP(int, inpicref->linesize[1], inpicref->linesize[2]);

    return ff_filter_frame(link->dst->outputs[0], inpicref);
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVA444P,
        AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_NONE,
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static const AVFilterPad swapuv_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .get_video_buffer = get_video_buffer,
        .filter_frame     = filter_frame,
    },
    { NULL }
};

static const AVFilterPad swapuv_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter avfilter_vf_swapuv = {
    .name      = "swapuv",
    .description = NULL_IF_CONFIG_SMALL("Swap U and V components."),
    .priv_size = 0,
    .query_formats = query_formats,
    .inputs        = swapuv_inputs,
    .outputs       = swapuv_outputs,
};
