/*
 * copyright (c) 2007 Bobby Bingham
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
 * @file libavfilter/vf_slicify.c
 * video slicing filter
 */

#include "avfilter.h"
#include "libavutil/pixdesc.h"

typedef struct {
    int h;          ///< output slice height
    int vshift;     ///< vertical chroma subsampling shift
} SliceContext;

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    SliceContext *slice = ctx->priv;

    slice->h = 16;
    if (args)
        sscanf(args, "%d", &slice->h);

    return 0;
}

static int config_props(AVFilterLink *link)
{
    SliceContext *slice = link->dst->priv;

    slice->vshift = av_pix_fmt_descriptors[link->format].log2_chroma_h;

    /* ensure that slices play nice with chroma subsampling, and enforce
     * a reasonable minimum size for the slices */
    slice->h = FFMAX(8, slice->h & (-1 << slice->vshift));

    av_log(link->dst, AV_LOG_INFO, "h:%d\n", slice->h);

    return 0;
}

static AVFilterPicRef *get_video_buffer(AVFilterLink *link, int perms,
                                        int w, int h)
{
    return avfilter_get_video_buffer(link->dst->outputs[0], perms, w, h);
}

static void start_frame(AVFilterLink *link, AVFilterPicRef *picref)
{
    avfilter_start_frame(link->dst->outputs[0], picref);
}

static void end_frame(AVFilterLink *link)
{
    avfilter_end_frame(link->dst->outputs[0]);
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
    .description = "Pass the images of input video on to next video filter as multiple slices.",

    .init      = init,

    .priv_size = sizeof(SliceContext),

    .inputs    = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = CODEC_TYPE_VIDEO,
                                    .get_video_buffer = get_video_buffer,
                                    .start_frame      = start_frame,
                                    .draw_slice       = draw_slice,
                                    .config_props     = config_props,
                                    .end_frame        = end_frame, },
                                  { .name = NULL}},
    .outputs   = (AVFilterPad[]) {{ .name            = "default",
                                    .type            = CODEC_TYPE_VIDEO, },
                                  { .name = NULL}},
};
