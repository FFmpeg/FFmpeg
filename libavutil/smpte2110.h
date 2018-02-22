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
 * smpte2110 utils, functions that are useful for all essences of SMPTE-2110
 */

#ifndef AVUTIL_SMPTE2110_H
#define AVUTIL_SMPTE2110_H

#include "common.h"
#include "time.h"

struct smpte2110_timestamp;

struct smpte2110_timestamp* smpte2110_alloc(void);

int64_t smpte2110_compute_pts(void *avlc, struct smpte2110_timestamp *ts,
                              uint32_t current_timestamp, AVRational time_base);

#endif
