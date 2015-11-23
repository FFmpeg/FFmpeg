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
 * Replacements for frequently missing libm functions
 */

#ifndef AVUTIL_LIBM_H
#define AVUTIL_LIBM_H

#include <math.h>
#include "config.h"
#include "attributes.h"
#include "intfloat.h"

#if HAVE_MIPSFPU && HAVE_INLINE_ASM
#include "libavutil/mips/libm_mips.h"
#endif /* HAVE_MIPSFPU && HAVE_INLINE_ASM*/

#if !HAVE_ATANF
#undef atanf
#define atanf(x) ((float)atan(x))
#endif

#if !HAVE_ATAN2F
#undef atan2f
#define atan2f(y, x) ((float)atan2(y, x))
#endif

#if !HAVE_POWF
#undef powf
#define powf(x, y) ((float)pow(x, y))
#endif

#if !HAVE_CBRT
static av_always_inline double cbrt(double x)
{
    return x < 0 ? -pow(-x, 1.0 / 3.0) : pow(x, 1.0 / 3.0);
}
#endif

#if !HAVE_CBRTF
static av_always_inline float cbrtf(float x)
{
    return x < 0 ? -powf(-x, 1.0 / 3.0) : powf(x, 1.0 / 3.0);
}
#endif

#if !HAVE_COSF
#undef cosf
#define cosf(x) ((float)cos(x))
#endif

#if !HAVE_EXPF
#undef expf
#define expf(x) ((float)exp(x))
#endif

#if !HAVE_EXP2
#undef exp2
#define exp2(x) exp((x) * 0.693147180559945)
#endif /* HAVE_EXP2 */

#if !HAVE_EXP2F
#undef exp2f
#define exp2f(x) ((float)exp2(x))
#endif /* HAVE_EXP2F */

#if !HAVE_ISINF
#undef isinf
/* Note: these do not follow the BSD/Apple/GNU convention of returning -1 for
-Inf, +1 for Inf, 0 otherwise, but merely follow the POSIX/ISO mandated spec of
returning a non-zero value for +/-Inf, 0 otherwise. */
static av_always_inline av_const int avpriv_isinff(float x)
{
    uint32_t v = av_float2int(x);
    if ((v & 0x7f800000) != 0x7f800000)
        return 0;
    return !(v & 0x007fffff);
}

static av_always_inline av_const int avpriv_isinf(double x)
{
    uint64_t v = av_double2int(x);
    if ((v & 0x7ff0000000000000) != 0x7ff0000000000000)
        return 0;
    return !(v & 0x000fffffffffffff);
}

#define isinf(x)                  \
    (sizeof(x) == sizeof(float)   \
        ? avpriv_isinff(x)        \
        : avpriv_isinf(x))
#endif /* HAVE_ISINF */

#if !HAVE_ISNAN
static av_always_inline av_const int avpriv_isnanf(float x)
{
    uint32_t v = av_float2int(x);
    if ((v & 0x7f800000) != 0x7f800000)
        return 0;
    return v & 0x007fffff;
}

static av_always_inline av_const int avpriv_isnan(double x)
{
    uint64_t v = av_double2int(x);
    if ((v & 0x7ff0000000000000) != 0x7ff0000000000000)
        return 0;
    return v & 0x000fffffffffffff;
}

#define isnan(x)                  \
    (sizeof(x) == sizeof(float)   \
        ? avpriv_isnanf(x)        \
        : avpriv_isnan(x))
#endif /* HAVE_ISNAN */

#if !HAVE_HYPOT
#undef hypot
static inline av_const double hypot(double x, double y)
{
    double ret, temp;
    x = fabs(x);
    y = fabs(y);

    if (isinf(x) || isinf(y))
        return av_int2double(0x7ff0000000000000);
    if (x == 0 || y == 0)
        return x + y;
    if (x < y) {
        temp = x;
        x = y;
        y = temp;
    }

    y = y/x;
    return x*sqrt(1 + y*y);
}
#endif /* HAVE_HYPOT */

#if !HAVE_LDEXPF
#undef ldexpf
#define ldexpf(x, exp) ((float)ldexp(x, exp))
#endif

#if !HAVE_LLRINT
#undef llrint
#define llrint(x) ((long long)rint(x))
#endif /* HAVE_LLRINT */

#if !HAVE_LLRINTF
#undef llrintf
#define llrintf(x) ((long long)rint(x))
#endif /* HAVE_LLRINT */

#if !HAVE_LOG2
#undef log2
#define log2(x) (log(x) * 1.44269504088896340736)
#endif /* HAVE_LOG2 */

#if !HAVE_LOG2F
#undef log2f
#define log2f(x) ((float)log2(x))
#endif /* HAVE_LOG2F */

#if !HAVE_LOG10F
#undef log10f
#define log10f(x) ((float)log10(x))
#endif

#if !HAVE_SINF
#undef sinf
#define sinf(x) ((float)sin(x))
#endif

#if !HAVE_RINT
static inline double rint(double x)
{
    return x >= 0 ? floor(x + 0.5) : ceil(x - 0.5);
}
#endif /* HAVE_RINT */

#if !HAVE_LRINT
static av_always_inline av_const long int lrint(double x)
{
    return rint(x);
}
#endif /* HAVE_LRINT */

#if !HAVE_LRINTF
static av_always_inline av_const long int lrintf(float x)
{
    return (int)(rint(x));
}
#endif /* HAVE_LRINTF */

#if !HAVE_ROUND
static av_always_inline av_const double round(double x)
{
    return (x > 0) ? floor(x + 0.5) : ceil(x - 0.5);
}
#endif /* HAVE_ROUND */

#if !HAVE_ROUNDF
static av_always_inline av_const float roundf(float x)
{
    return (x > 0) ? floor(x + 0.5) : ceil(x - 0.5);
}
#endif /* HAVE_ROUNDF */

#if !HAVE_TRUNC
static av_always_inline av_const double trunc(double x)
{
    return (x > 0) ? floor(x) : ceil(x);
}
#endif /* HAVE_TRUNC */

#if !HAVE_TRUNCF
static av_always_inline av_const float truncf(float x)
{
    return (x > 0) ? floor(x) : ceil(x);
}
#endif /* HAVE_TRUNCF */

#endif /* AVUTIL_LIBM_H */
