/*
 * Copyright (c) 2012 Stefano Sabatini
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
 * set field order
 */

#include "avfilter.h"

typedef struct {
    int top_field_first;
} SetFieldContext;

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    SetFieldContext *setfield = ctx->priv;

    setfield->top_field_first = -1;

    if (args) {
        char c;
        if (sscanf(args, "%d%c", &setfield->top_field_first, &c) != 1) {
            if      (!strcmp("tff",  args)) setfield->top_field_first = 1;
            else if (!strcmp("bff",  args)) setfield->top_field_first = 0;
            else if (!strcmp("auto", args)) setfield->top_field_first = -1;
            else {
                av_log(ctx, AV_LOG_ERROR, "Invalid argument '%s'\n", args);
                return AVERROR(EINVAL);
            }
        }
    }

    if (setfield->top_field_first < -1 || setfield->top_field_first > 1) {
        av_log(ctx, AV_LOG_ERROR,
               "Provided integer value %d must be included between -1 and +1\n",
               setfield->top_field_first);
        return AVERROR(EINVAL);
    }

    return 0;
}

static void start_frame(AVFilterLink *inlink, AVFilterBufferRef *inpicref)
{
    SetFieldContext *setfield = inlink->dst->priv;
    AVFilterBufferRef *outpicref = avfilter_ref_buffer(inpicref, ~0);

    if (setfield->top_field_first != -1) {
        outpicref->video->interlaced = 1;
        outpicref->video->top_field_first = setfield->top_field_first;
    }
    avfilter_start_frame(inlink->dst->outputs[0], outpicref);
}

AVFilter avfilter_vf_setfield = {
    .name      = "setfield",
    .description = NULL_IF_CONFIG_SMALL("Force field for the output video frame."),
    .init      = init,

    .priv_size = sizeof(SetFieldContext),

    .inputs = (const AVFilterPad[]) {
        { .name             = "default",
          .type             = AVMEDIA_TYPE_VIDEO,
          .get_video_buffer = avfilter_null_get_video_buffer,
          .start_frame      = start_frame, },
        { .name = NULL }
    },
    .outputs = (const AVFilterPad[]) {
        { .name             = "default",
          .type             = AVMEDIA_TYPE_VIDEO, },
        { .name = NULL }
    },
};
