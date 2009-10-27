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
 * @file libavfilter/vf_null.c
 * null video filter
 */

#include "avfilter.h"

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

AVFilter avfilter_vf_null = {
    .name      = "null",
    .description = NULL_IF_CONFIG_SMALL("Pass the source unchanged to the output."),

    .priv_size = 0,

    .inputs    = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = CODEC_TYPE_VIDEO,
                                    .get_video_buffer = get_video_buffer,
                                    .start_frame      = start_frame,
                                    .end_frame        = end_frame },
                                  { .name = NULL}},

    .outputs   = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = CODEC_TYPE_VIDEO, },
                                  { .name = NULL}},
};
