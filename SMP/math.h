/*
 * Correctly include the correct math.h header based on which native
 * Windows compiler is in use.
 * Copyright (c) 2015 Matthew Oliver
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef SMP_MATH_H
#define SMP_MATH_H

#ifdef __INTEL_COMPILER
    // Use the Intel Math library. This provides Intel processor optimized functions.
    //  This also avoids an error with the msvc math.h definition of NAN found in msvc12.
#   include <mathimf.h>
    // The Intel header automatically includes defines used with complex.h
    //  that interfere with normal code and need to be removed.
#   undef I
#   undef complex
#elif defined(_MSC_VER)
#   include <crtversion.h>
#   ifndef _USE_MATH_DEFINES
#       define _USE_MATH_DEFINES
#   endif
#   if _VC_CRT_MAJOR_VERSION >= 14
#       include <../ucrt/math.h>
#   else
#       include <../include/math.h>
#   endif
#else
#   include_next <math.h>
#endif

#endif /* _SMP_MATH_H */
