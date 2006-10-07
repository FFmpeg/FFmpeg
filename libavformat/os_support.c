/*
 * Various utilities for ffmpeg system
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
#include "config.h"
#include "avformat.h"
#if defined(CONFIG_WINCE)
/* Skip includes on WinCE. */
#elif defined(__MINGW32__)
#include <sys/types.h>
#include <sys/timeb.h>
#elif defined(CONFIG_OS2)
#include <string.h>
#include <sys/time.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#endif
#include <time.h>

/**
 * gets the current time in micro seconds.
 */
int64_t av_gettime(void)
{
#if defined(CONFIG_WINCE)
    return timeGetTime() * int64_t_C(1000);
#elif defined(__MINGW32__)
    struct timeb tb;
    _ftime(&tb);
    return ((int64_t)tb.time * int64_t_C(1000) + (int64_t)tb.millitm) * int64_t_C(1000);
#else
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
#endif
}

#if !defined(CONFIG_WINCE)
#if !defined(HAVE_LOCALTIME_R)
struct tm *localtime_r(const time_t *t, struct tm *tp)
{
    struct tm *l;

    l = localtime(t);
    if (!l)
        return 0;
    *tp = *l;
    return tp;
}
#endif /* !defined(HAVE_LOCALTIME_R) */
#endif /* !defined(CONFIG_WINCE) */
