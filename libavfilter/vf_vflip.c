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
 * video vertical flip filter
 */

#include "libavutil/internal.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "internal.h"
#include "video.h"

typedef struct {
    int vsub;   ///< vertical chroma subsampling
} FlipContext;

static int config_input(AVFilterLink *link)
{
    FlipContext *flip = link->dst->priv;

    flip->vsub = av_pix_fmt_descriptors[link->format].log2_chroma_h;

    return 0;
}

static AVFilterBufferRef *get_video_buffer(AVFilterLink *link, int perms,
                                        int w, int h)
{
    FlipContext *flip = link->dst->priv;
    AVFilterBufferRef *picref;
    int i;

    if (!(perms & AV_PERM_NEG_LINESIZES))
        return ff_default_get_video_buffer(link, perms, w, h);

    picref = ff_get_video_buffer(link->dst->outputs[0], perms, w, h);
    if (!picref)
        return NULL;

    for (i = 0; i < 4; i ++) {
        int vsub = i == 1 || i == 2 ? flip->vsub : 0;

        if (picref->data[i]) {
            picref->data[i] += (((h + (1<<vsub)-1) >> vsub)-1) * picref->linesize[i];
            picref->linesize[i] = -picref->linesize[i];
        }
    }

    return picref;
}

static int start_frame(AVFilterLink *link, AVFilterBufferRef *inpicref)
{
    FlipContext *flip = link->dst->priv;
    AVFilterBufferRef *outpicref = avfilter_ref_buffer(inpicref, ~0);
    int i;

    if (!outpicref)
        return AVERROR(ENOMEM);

    for (i = 0; i < 4; i ++) {
        int vsub = i == 1 || i == 2 ? flip->vsub : 0;

        if (outpicref->data[i]) {
            outpicref->data[i] += (((link->h + (1<<vsub)-1)>> vsub)-1) * outpicref->linesize[i];
            outpicref->linesize[i] = -outpicref->linesize[i];
        }
    }

    return ff_start_frame(link->dst->outputs[0], outpicref);
}

static int draw_slice(AVFilterLink *link, int y, int h, int slice_dir)
{
    AVFilterContext *ctx = link->dst;

    return ff_draw_slice(ctx->outputs[0], link->h - (y+h), h, -1 * slice_dir);
}

AVFilter avfilter_vf_vflip = {
    .name      = "vflip",
    .description = NULL_IF_CONFIG_SMALL("Flip the input video vertically."),

    .priv_size = sizeof(FlipContext),

    .inputs    = (const AVFilterPad[]) {{ .name             = "default",
                                          .type             = AVMEDIA_TYPE_VIDEO,
                                          .get_video_buffer = get_video_buffer,
                                          .start_frame      = start_frame,
                                          .draw_slice       = draw_slice,
                                          .config_props     = config_input, },
                                        { .name = NULL}},
    .outputs   = (const AVFilterPad[]) {{ .name             = "default",
                                          .type             = AVMEDIA_TYPE_VIDEO, },
                                        { .name = NULL}},
};
