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

#include "libavutil/opt.h"
#include "avfilter.h"
#include "internal.h"
#include "video.h"

enum SetFieldMode {
    MODE_AUTO = -1,
    MODE_BFF,
    MODE_TFF,
    MODE_PROG,
};

typedef struct {
    const AVClass *class;
    int mode;                   ///< SetFieldMode
} SetFieldContext;

#define OFFSET(x) offsetof(SetFieldContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption setfield_options[] = {
    {"mode", "select interlace mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=MODE_AUTO}, -1, MODE_PROG, FLAGS, "mode"},
    {"auto", "keep the same input field",  0, AV_OPT_TYPE_CONST, {.i64=MODE_AUTO}, INT_MIN, INT_MAX, FLAGS, "mode"},
    {"bff",  "mark as bottom-field-first", 0, AV_OPT_TYPE_CONST, {.i64=MODE_BFF},  INT_MIN, INT_MAX, FLAGS, "mode"},
    {"tff",  "mark as top-field-first",    0, AV_OPT_TYPE_CONST, {.i64=MODE_TFF},  INT_MIN, INT_MAX, FLAGS, "mode"},
    {"prog", "mark as progressive",        0, AV_OPT_TYPE_CONST, {.i64=MODE_PROG}, INT_MIN, INT_MAX, FLAGS, "mode"},
    {NULL}
};

AVFILTER_DEFINE_CLASS(setfield);

static int filter_frame(AVFilterLink *inlink, AVFrame *picref)
{
    SetFieldContext *setfield = inlink->dst->priv;

    if (setfield->mode == MODE_PROG) {
        picref->interlaced_frame = 0;
    } else if (setfield->mode != MODE_AUTO) {
        picref->interlaced_frame = 1;
        picref->top_field_first = setfield->mode;
    }
    return ff_filter_frame(inlink->dst->outputs[0], picref);
}

static const AVFilterPad setfield_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad setfield_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_setfield = {
    .name        = "setfield",
    .description = NULL_IF_CONFIG_SMALL("Force field for the output video frame."),
    .priv_size   = sizeof(SetFieldContext),
    .priv_class  = &setfield_class,
    .inputs      = setfield_inputs,
    .outputs     = setfield_outputs,
};
