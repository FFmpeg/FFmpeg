/*
 * Various utilities for ffmpeg system
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "config.h"
#include "avformat.h"
#ifdef CONFIG_WIN32
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

int64_t av_gettime(void)
{
#ifdef CONFIG_WIN32
    struct timeb tb;
    _ftime(&tb);
    return ((int64_t)tb.time * int64_t_C(1000) + (int64_t)tb.millitm) * int64_t_C(1000);
#else
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
#endif
}

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
