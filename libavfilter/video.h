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


#ifndef AVFILTER_VIDEO_H
#define AVFILTER_VIDEO_H

#include "avfilter.h"

AVFilterBufferRef *ff_default_get_video_buffer(AVFilterLink *link,
                                               int perms, int w, int h);
AVFilterBufferRef *ff_null_get_video_buffer(AVFilterLink *link, int perms, int w, int h);


void ff_null_start_frame(AVFilterLink *link, AVFilterBufferRef *picref);
void ff_null_draw_slice(AVFilterLink *link, int y, int h, int slice_dir);
void ff_null_end_frame(AVFilterLink *link);

#endif /* AVFILTER_VIDEO_H */
