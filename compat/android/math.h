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

#ifndef COMPAT_ANDROID_MATH_H
#define COMPAT_ANDROID_MATH_H

#include <math.h>
#include <android/api-level.h>

#if __ANDROID_API__ < 18
#define log2(x)  (log(x) * M_LOG2E)
#define log2f(x) (logf(x) * (float)M_LOG2E)
#endif

#endif /* COMPAT_ANDROID_MATH_H */
