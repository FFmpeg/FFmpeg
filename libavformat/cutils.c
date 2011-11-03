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
#include "avformat.h"
#include "internal.h"

#define ISLEAP(y) (((y) % 4 == 0) && (((y) % 100) != 0 || ((y) % 400) == 0))
#define LEAPS_COUNT(y) ((y)/4 - (y)/100 + (y)/400)

/* This is our own gmtime_r. It differs from its POSIX counterpart in a
   couple of places, though. */
struct tm *brktimegm(time_t secs, struct tm *tm)
{
    int days, y, ny, m;
    int md[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    days = secs / 86400;
    secs %= 86400;
    tm->tm_hour = secs / 3600;
    tm->tm_min = (secs % 3600) / 60;
    tm->tm_sec =  secs % 60;

    /* oh well, may be someone some day will invent a formula for this stuff */
    y = 1970; /* start "guessing" */
    while (days > 365) {
        ny = (y + days/366);
        days -= (ny - y) * 365 + LEAPS_COUNT(ny - 1) - LEAPS_COUNT(y - 1);
        y = ny;
    }
    if (days==365 && !ISLEAP(y)) { days=0; y++; }
    md[1] = ISLEAP(y)?29:28;
    for (m=0; days >= md[m]; m++)
         days -= md[m];

    tm->tm_year = y;  /* unlike gmtime_r we store complete year here */
    tm->tm_mon = m+1; /* unlike gmtime_r tm_mon is from 1 to 12 */
    tm->tm_mday = days+1;

    return tm;
}
