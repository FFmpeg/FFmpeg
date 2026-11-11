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

#ifndef AVFILTER_MPDECIMATE_H
#define AVFILTER_MPDECIMATE_H

#include "libavutil/pixelutils.h"

/**
 * Get the SAD function for 8x8 blocks of 16-bit samples.
 * Strides are in bytes.
 */
av_pixelutils_sad_fn ff_mpdecimate_get_sad16_fn(void);

av_pixelutils_sad_fn ff_mpdecimate_get_sad16_fn_x86(void);

#endif /* AVFILTER_MPDECIMATE_H */
