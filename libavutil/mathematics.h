/*
 * copyright (c) 2005 Michael Niedermayer <michaelni@gmx.at>
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

#ifndef FFMPEG_MATHEMATICS_H
#define FFMPEG_MATHEMATICS_H

#include <stdint.h>
#include "rational.h"

enum AVRounding {
    AV_ROUND_ZERO     = 0, ///< round toward zero
    AV_ROUND_INF      = 1, ///< round away from zero
    AV_ROUND_DOWN     = 2, ///< round toward -infinity
    AV_ROUND_UP       = 3, ///< round toward +infinity
    AV_ROUND_NEAR_INF = 5, ///< round to nearest and halfway cases away from zero
};

/**
 * rescale a 64bit integer with rounding to nearest.
 * a simple a*b/c isn't possible as it can overflow
 */
int64_t av_rescale(int64_t a, int64_t b, int64_t c) av_const;

/**
 * rescale a 64bit integer with specified rounding.
 * a simple a*b/c isn't possible as it can overflow
 */
int64_t av_rescale_rnd(int64_t a, int64_t b, int64_t c, enum AVRounding) av_const;

/**
 * rescale a 64bit integer by 2 rational numbers.
 */
int64_t av_rescale_q(int64_t a, AVRational bq, AVRational cq) av_const;

#endif /* FFMPEG_MATHEMATICS_H */
