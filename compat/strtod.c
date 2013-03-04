/*
 * C99-compatible strtod() implementation
 * Copyright (c) 2012 Ronald S. Bultje <rsbultje@gmail.com>
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

#include <limits.h>
#include <stdlib.h>

#include "libavutil/avstring.h"
#include "libavutil/mathematics.h"

static char *check_nan_suffix(char *s)
{
    char *start = s;

    if (*s++ != '(')
        return start;

    while ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
           (*s >= '0' && *s <= '9') ||  *s == '_')
        s++;

    return *s == ')' ? s + 1 : start;
}

#undef strtod
double strtod(const char *, char **);

double avpriv_strtod(const char *nptr, char **endptr)
{
    char *end;
    double res;

    /* Skip leading spaces */
    while (av_isspace(*nptr))
        nptr++;

    if (!av_strncasecmp(nptr, "infinity", 8)) {
        end = nptr + 8;
        res = INFINITY;
    } else if (!av_strncasecmp(nptr, "inf", 3)) {
        end = nptr + 3;
        res = INFINITY;
    } else if (!av_strncasecmp(nptr, "+infinity", 9)) {
        end = nptr + 9;
        res = INFINITY;
    } else if (!av_strncasecmp(nptr, "+inf", 4)) {
        end = nptr + 4;
        res = INFINITY;
    } else if (!av_strncasecmp(nptr, "-infinity", 9)) {
        end = nptr + 9;
        res = -INFINITY;
    } else if (!av_strncasecmp(nptr, "-inf", 4)) {
        end = nptr + 4;
        res = -INFINITY;
    } else if (!av_strncasecmp(nptr, "nan", 3)) {
        end = check_nan_suffix(nptr + 3);
        res = NAN;
    } else if (!av_strncasecmp(nptr, "+nan", 4) ||
               !av_strncasecmp(nptr, "-nan", 4)) {
        end = check_nan_suffix(nptr + 4);
        res = NAN;
    } else if (!av_strncasecmp(nptr, "0x", 2) ||
               !av_strncasecmp(nptr, "-0x", 3) ||
               !av_strncasecmp(nptr, "+0x", 3)) {
        /* FIXME this doesn't handle exponents, non-integers (float/double)
         * and numbers too large for long long */
        res = strtoll(nptr, &end, 16);
    } else {
        res = strtod(nptr, &end);
    }

    if (endptr)
        *endptr = end;

    return res;
}
