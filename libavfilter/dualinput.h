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
 * @file
 * Double input streams helper for filters
 */

#ifndef AVFILTER_DUALINPUT_H
#define AVFILTER_DUALINPUT_H

#include <stdint.h>
#include "bufferqueue.h"
#include "framesync.h"
#include "internal.h"

typedef struct {
    FFFrameSync fs;

    AVFrame *(*process)(AVFilterContext *ctx, AVFrame *main, const AVFrame *second);
    int shortest;               ///< terminate stream when the second input terminates
    int repeatlast;             ///< repeat last second frame
    int skip_initial_unpaired;  ///< Skip initial frames that do not have a 2nd input
} FFDualInputContext;

int ff_dualinput_init(AVFilterContext *ctx, FFDualInputContext *s);
int ff_dualinput_filter_frame(FFDualInputContext *s, AVFilterLink *inlink, AVFrame *in);
int ff_dualinput_request_frame(FFDualInputContext *s, AVFilterLink *outlink);
void ff_dualinput_uninit(FFDualInputContext *s);

#endif /* AVFILTER_DUALINPUT_H */
