/*
 * various simple utilities for libavformat
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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

#include "libavutil/time_internal.h"
#include "avformat.h"
#include "internal.h"

#define ISLEAP(y) (((y) % 4 == 0) && (((y) % 100) != 0 || ((y) % 400) == 0))
#define LEAPS_COUNT(y) ((y)/4 - (y)/100 + (y)/400)

/* This is our own gmtime_r. It differs from its POSIX counterpart in a
   couple of places, though. */
struct tm *ff_brktimegm(time_t secs, struct tm *tm)
{
    tm = gmtime_r(&secs, tm);

    tm->tm_year += 1900; /* unlike gmtime_r we store complete year here */
    tm->tm_mon  += 1;    /* unlike gmtime_r tm_mon is from 1 to 12 */

    return tm;
}
