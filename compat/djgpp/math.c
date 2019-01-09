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

#include <math.h>

#define FUN(name, type, op) \
type name(type x, type y) \
{ \
    if (fpclassify(x) == FP_NAN) return y; \
    if (fpclassify(y) == FP_NAN) return x; \
    return x op y ? x : y; \
}

FUN(fmin, double, <)
FUN(fmax, double, >)
FUN(fminf, float, <)
FUN(fmaxf, float, >)

long double fmodl(long double x, long double y)
{
    return fmod(x, y);
}

long double scalbnl(long double x, int exp)
{
    return scalbn(x, exp);
}

long double copysignl(long double x, long double y)
{
    return copysign(x, y);
}
