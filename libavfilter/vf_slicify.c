/*
 * Copyright (c) 2007 Bobby Bingham
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
 * video slicing filter
 */

#include "avfilter.h"
#include "video.h"
#include "libavutil/pixdesc.h"

typedef struct {
    int h;          ///< output slice height
    int vshift;     ///< vertical chroma subsampling shift
    uint32_t lcg_state; ///< LCG state used to compute random slice height
    int use_random_h;   ///< enable the use of random slice height values
} SliceContext;

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    SliceContext *slice = ctx->priv;

    slice->h = 16;
    if (args) {
        if (!strcmp(args, "random")) {
            slice->use_random_h = 1;
        } else {
            sscanf(args, "%d", &slice->h);
        }
    }
    return 0;
}

static int config_props(AVFilterLink *link)
{
    SliceContext *slice = link->dst->priv;

    slice->vshift = av_pix_fmt_descriptors[link->format].log2_chroma_h;

    return 0;
}

static void start_frame(AVFilterLink *link, AVFilterBufferRef *picref)
{
    SliceContext *slice = link->dst->priv;

    if (slice->use_random_h) {
        slice->lcg_state = slice->lcg_state * 1664525 + 1013904223;
        slice->h = 8 + (uint64_t)slice->lcg_state * 25 / UINT32_MAX;
    }

    /* ensure that slices play nice with chroma subsampling, and enforce
     * a reasonable minimum size for the slices */
    slice->h = FFMAX(8, slice->h & (-1 << slice->vshift));

    av_log(link->dst, AV_LOG_DEBUG, "h:%d\n", slice->h);

    avfilter_start_frame(link->dst->outputs[0], picref);
}

static void draw_slice(AVFilterLink *link, int y, int h, int slice_dir)
{
    SliceContext *slice = link->dst->priv;
    int y2;

    if (slice_dir == 1) {
        for (y2 = y; y2 + slice->h <= y + h; y2 += slice->h)
            avfilter_draw_slice(link->dst->outputs[0], y2, slice->h, slice_dir);

        if (y2 < y + h)
            avfilter_draw_slice(link->dst->outputs[0], y2, y + h - y2, slice_dir);
    } else if (slice_dir == -1) {
        for (y2 = y + h; y2 - slice->h >= y; y2 -= slice->h)
            avfilter_draw_slice(link->dst->outputs[0], y2 - slice->h, slice->h, slice_dir);

        if (y2 > y)
            avfilter_draw_slice(link->dst->outputs[0], y, y2 - y, slice_dir);
    }
}

AVFilter avfilter_vf_slicify = {
    .name      = "slicify",
    .description = NULL_IF_CONFIG_SMALL("Pass the images of input video on to next video filter as multiple slices."),

    .init      = init,

    .priv_size = sizeof(SliceContext),

    .inputs    = (const AVFilterPad[]) {{ .name       = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .get_video_buffer = ff_null_get_video_buffer,
                                    .start_frame      = start_frame,
                                    .draw_slice       = draw_slice,
                                    .config_props     = config_props,
                                    .end_frame        = ff_null_end_frame, },
                                  { .name = NULL}},
    .outputs   = (const AVFilterPad[]) {{ .name      = "default",
                                    .type            = AVMEDIA_TYPE_VIDEO, },
                                  { .name = NULL}},
};
