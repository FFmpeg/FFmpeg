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

static AVFilterBufferRef *get_video_buffer(AVFilterLink *link, int perms,
                                           int w, int h)
{
    AVFilterBufferRef *picref =
        avfilter_default_get_video_buffer(link, perms, w, h);
    uint8_t *tmp;
    int tmp2;

    tmp             = picref->data[2];
    picref->data[2] = picref->data[1];
    picref->data[1] = tmp;

    tmp2                = picref->linesize[2];
    picref->linesize[2] = picref->linesize[1];
    picref->linesize[1] = tmp2;

    return picref;
}

static void start_frame(AVFilterLink *link, AVFilterBufferRef *inpicref)
{
    AVFilterBufferRef *outpicref = avfilter_ref_buffer(inpicref, ~0);

    outpicref->data[1] = inpicref->data[2];
    outpicref->data[2] = inpicref->data[1];

    outpicref->linesize[1] = inpicref->linesize[2];
    outpicref->linesize[2] = inpicref->linesize[1];

    avfilter_start_frame(link->dst->outputs[0], outpicref);
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        PIX_FMT_YUV420P, PIX_FMT_YUVJ420P, PIX_FMT_YUVA420P,
        PIX_FMT_YUV444P, PIX_FMT_YUVJ444P,
        PIX_FMT_YUV440P, PIX_FMT_YUVJ440P,
        PIX_FMT_YUV422P, PIX_FMT_YUVJ422P,
        PIX_FMT_YUV411P,
        PIX_FMT_NONE,
    };

    avfilter_set_common_pixel_formats(ctx, avfilter_make_format_list(pix_fmts));
    return 0;
}

AVFilter avfilter_vf_swapuv = {
    .name      = "swapuv",
    .description = NULL_IF_CONFIG_SMALL("Swap U and V components."),
    .priv_size = 0,
    .query_formats = query_formats,

    .inputs = (const AVFilterPad[]) {
        { .name             = "default",
          .type             = AVMEDIA_TYPE_VIDEO,
          .get_video_buffer = get_video_buffer,
          .start_frame      = start_frame, },
        { .name = NULL }
    },
    .outputs = (const AVFilterPad[]) {
        { .name             = "default",
          .type             = AVMEDIA_TYPE_VIDEO, },
        { .name             = NULL }
    },
};
