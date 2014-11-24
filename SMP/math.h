/*
 * Correctly include the correct math.h header based on which native
 * Windows compiler is in use.
 * Copyright (c) 2014 Matthew Oliver
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

#ifndef SMC_MATH_H
#define SMC_MATH_H

#ifdef __INTEL_COMPILER
    // Use the Intel Math library. This provides Intel processor optimized functions.
    //  This also avoids an error with the msvc math.h definition of NAN found in msvc12.
#   include <mathimf.h>
    // The Intel header automatically includes defines used with complex.h
    //  that interfere with normal code and need to be removed.
#   undef I
#   undef complex
#elif defined(_MSC_VER)
#   ifndef _USE_MATH_DEFINES
#       define _USE_MATH_DEFINES
#   endif
#   include <../include/math.h>
#else
#   include_next <math.h>
#endif

#endif /* SMC_MATH_H */
