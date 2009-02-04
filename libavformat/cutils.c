/*
 * Various simple utilities for ffmpeg system
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

/* add one element to a dynamic array */
void ff_dynarray_add(intptr_t **tab_ptr, int *nb_ptr, intptr_t elem)
{
    int nb, nb_alloc;
    intptr_t *tab;

    nb = *nb_ptr;
    tab = *tab_ptr;
    if ((nb & (nb - 1)) == 0) {
        if (nb == 0)
            nb_alloc = 1;
        else
            nb_alloc = nb * 2;
        tab = av_realloc(tab, nb_alloc * sizeof(intptr_t));
        *tab_ptr = tab;
    }
    tab[nb++] = elem;
    *nb_ptr = nb;
}

time_t mktimegm(struct tm *tm)
{
    time_t t;

    int y = tm->tm_year + 1900, m = tm->tm_mon + 1, d = tm->tm_mday;

    if (m < 3) {
        m += 12;
        y--;
    }

    t = 86400 *
        (d + (153 * m - 457) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 719469);

    t += 3600 * tm->tm_hour + 60 * tm->tm_min + tm->tm_sec;

    return t;
}

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

/* get a positive number between n_min and n_max, for a maximum length
   of len_max. Return -1 if error. */
static int date_get_num(const char **pp,
                        int n_min, int n_max, int len_max)
{
    int i, val, c;
    const char *p;

    p = *pp;
    val = 0;
    for(i = 0; i < len_max; i++) {
        c = *p;
        if (!isdigit(c))
            break;
        val = (val * 10) + c - '0';
        p++;
    }
    /* no number read ? */
    if (p == *pp)
        return -1;
    if (val < n_min || val > n_max)
        return -1;
    *pp = p;
    return val;
}

/* small strptime for ffmpeg */
const char *small_strptime(const char *p, const char *fmt,
                           struct tm *dt)
{
    int c, val;

    for(;;) {
        c = *fmt++;
        if (c == '\0') {
            return p;
        } else if (c == '%') {
            c = *fmt++;
            switch(c) {
            case 'H':
                val = date_get_num(&p, 0, 23, 2);
                if (val == -1)
                    return NULL;
                dt->tm_hour = val;
                break;
            case 'M':
                val = date_get_num(&p, 0, 59, 2);
                if (val == -1)
                    return NULL;
                dt->tm_min = val;
                break;
            case 'S':
                val = date_get_num(&p, 0, 59, 2);
                if (val == -1)
                    return NULL;
                dt->tm_sec = val;
                break;
            case 'Y':
                val = date_get_num(&p, 0, 9999, 4);
                if (val == -1)
                    return NULL;
                dt->tm_year = val - 1900;
                break;
            case 'm':
                val = date_get_num(&p, 1, 12, 2);
                if (val == -1)
                    return NULL;
                dt->tm_mon = val - 1;
                break;
            case 'd':
                val = date_get_num(&p, 1, 31, 2);
                if (val == -1)
                    return NULL;
                dt->tm_mday = val;
                break;
            case '%':
                goto match;
            default:
                return NULL;
            }
        } else {
        match:
            if (c != *p)
                return NULL;
            p++;
        }
    }
    return p;
}

