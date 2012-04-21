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

enum SetFieldMode {
    MODE_AUTO = -1,
    MODE_BFF,
    MODE_TFF,
    MODE_PROG,
};

typedef struct {
    enum SetFieldMode mode;
} SetFieldContext;

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    SetFieldContext *setfield = ctx->priv;

    setfield->mode = MODE_AUTO;

    if (args) {
        char c;
        if (sscanf(args, "%d%c", &setfield->mode, &c) != 1) {
            if      (!strcmp("tff",  args)) setfield->mode = MODE_TFF;
            else if (!strcmp("bff",  args)) setfield->mode = MODE_BFF;
            else if (!strcmp("prog", args)) setfield->mode = MODE_PROG;
            else if (!strcmp("auto", args)) setfield->mode = MODE_AUTO;
            else {
                av_log(ctx, AV_LOG_ERROR, "Invalid argument '%s'\n", args);
                return AVERROR(EINVAL);
            }
        } else {
            if (setfield->mode < -1 || setfield->mode > 1) {
                av_log(ctx, AV_LOG_ERROR,
                       "Provided integer value %d must be included between -1 and +1\n",
                       setfield->mode);
                return AVERROR(EINVAL);
            }
            av_log(ctx, AV_LOG_WARNING,
                   "Using -1/0/1 is deprecated, use auto/tff/bff/prog\n");
        }
    }

    return 0;
}

static void start_frame(AVFilterLink *inlink, AVFilterBufferRef *inpicref)
{
    SetFieldContext *setfield = inlink->dst->priv;
    AVFilterBufferRef *outpicref = avfilter_ref_buffer(inpicref, ~0);

    if (setfield->mode == MODE_PROG) {
        outpicref->video->interlaced = 0;
    } else if (setfield->mode != MODE_AUTO) {
        outpicref->video->interlaced = 1;
        outpicref->video->top_field_first = setfield->mode;
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
