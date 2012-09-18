/*
 * Copyright (c) 2010 S.N. Hemanth Meenakshisundaram <smeenaks@ucsd.edu>
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

#include "libavutil/internal.h"
#include "avfilter.h"
#include "internal.h"

static int null_filter_samples(AVFilterLink *link, AVFilterBufferRef *samplesref)
{
    return 0;
}

AVFilter avfilter_asink_anullsink = {
    .name        = "anullsink",
    .description = NULL_IF_CONFIG_SMALL("Do absolutely nothing with the input audio."),

    .priv_size = 0,

    .inputs    = (const AVFilterPad[]) {
        {
            .name            = "default",
            .type            = AVMEDIA_TYPE_AUDIO,
            .filter_samples  = null_filter_samples,
        },
        { .name = NULL},
    },
    .outputs   = NULL,
};
