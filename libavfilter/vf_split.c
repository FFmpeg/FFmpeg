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
 * Video splitter
 */

#include "avfilter.h"

static void start_frame(AVFilterLink *inlink, AVFilterBufferRef *picref)
{
    avfilter_start_frame(inlink->dst->outputs[0],
                         avfilter_ref_buffer(picref, ~AV_PERM_WRITE));
    avfilter_start_frame(inlink->dst->outputs[1],
                         avfilter_ref_buffer(picref, ~AV_PERM_WRITE));
}

static void draw_slice(AVFilterLink *inlink, int y, int h, int slice_dir)
{
    avfilter_draw_slice(inlink->dst->outputs[0], y, h, slice_dir);
    avfilter_draw_slice(inlink->dst->outputs[1], y, h, slice_dir);
}

static void end_frame(AVFilterLink *inlink)
{
    avfilter_end_frame(inlink->dst->outputs[0]);
    avfilter_end_frame(inlink->dst->outputs[1]);

    avfilter_unref_buffer(inlink->cur_buf);
}

AVFilter avfilter_vf_split = {
    .name      = "split",
    .description = NULL_IF_CONFIG_SMALL("Pass on the input to two outputs."),

    .inputs    = (const AVFilterPad[]) {{ .name      = "default",
                                    .type            = AVMEDIA_TYPE_VIDEO,
                                    .get_video_buffer= avfilter_null_get_video_buffer,
                                    .start_frame     = start_frame,
                                    .draw_slice      = draw_slice,
                                    .end_frame       = end_frame, },
                                  { .name = NULL}},
    .outputs   = (const AVFilterPad[]) {{ .name      = "output1",
                                    .type            = AVMEDIA_TYPE_VIDEO, },
                                  { .name            = "output2",
                                    .type            = AVMEDIA_TYPE_VIDEO, },
                                  { .name = NULL}},
};
