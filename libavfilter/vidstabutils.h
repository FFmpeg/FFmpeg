/*
 * Copyright (c) 2013 Georg Martius <georg dot martius at web dot de>
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

#ifndef AVFILTER_VIDSTABUTILS_H
#define AVFILTER_VIDSTABUTILS_H

#include <vid.stab/libvidstab.h>

#include "avfilter.h"

/* ** some conversions from avlib to vid.stab constants and functions *** */

/** converts the pixelformat of avlib into the one of the vid.stab library */
VSPixelFormat av_2_vs_pixel_format(AVFilterContext *ctx, enum AVPixelFormat pf);

/** sets the memory allocation function and logging constants to av versions */
void vs_set_mem_and_log_functions(void);

#endif
