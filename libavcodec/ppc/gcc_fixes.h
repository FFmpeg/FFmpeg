/*
 * gcc fixes for altivec.
 * Used to workaround broken gcc (FSF gcc-3 pre gcc-3.3)
 * and to stay somewhat compatible with Darwin.
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

#ifndef _GCC_FIXES_
#define _GCC_FIXES_

#ifdef HAVE_ALTIVEC_H
#include <altivec.h>
#endif

#ifdef CONFIG_DARWIN
# ifndef __MWERKS__
#  define AVV(x...) (x)
# else
#  define AVV
# endif
#define REG_v(a) asm ( #a )
#else

#define AVV(x...) {x}

#if (__GNUC__ < 4)
# define REG_v(a)
#else
# define REG_v(a) asm ( #a )
#endif

#if (__GNUC__ * 100 + __GNUC_MINOR__ < 303)

/* This code was provided to me by Bartosch Pixa
 * as a separate header file (broken_mergel.h).
 * thanks to lu_zero for the workaround.
 *
 * See this mail for more information:
 * http://gcc.gnu.org/ml/gcc/2003-04/msg00967.html
 */

static inline vector signed char ff_vmrglb (vector signed char const A,
                                          vector signed char const B)
{
    static const vector unsigned char lowbyte = {
        0x08, 0x18, 0x09, 0x19, 0x0a, 0x1a, 0x0b,  0x1b,
        0x0c, 0x1c, 0x0d, 0x1d, 0x0e, 0x1e, 0x0f, 0x1f
    };
    return vec_perm (A, B, lowbyte);
}

static inline vector signed short ff_vmrglh (vector signed short const A,
                                          vector signed short const B)
{
    static const vector unsigned char lowhalf = {
        0x08, 0x09, 0x18, 0x19, 0x0a, 0x0b, 0x1a, 0x1b,
        0x0c, 0x0d, 0x1c, 0x1d, 0x0e, 0x0f, 0x1e, 0x1f
    };
    return vec_perm (A, B, lowhalf);
}

static inline vector signed int ff_vmrglw (vector signed int const A,
                                          vector signed int const B)
{
    static const vector unsigned char lowword = {
        0x08, 0x09, 0x0a, 0x0b, 0x18, 0x19, 0x1a, 0x1b,
        0x0c, 0x0d, 0x0e, 0x0f, 0x1c, 0x1d, 0x1e, 0x1f
    };
    return vec_perm (A, B, lowword);
}
/*#define ff_vmrglb ff_vmrglb
#define ff_vmrglh ff_vmrglh
#define ff_vmrglw ff_vmrglw
*/
#undef vec_mergel

#define vec_mergel(a1, a2) \
__ch (__bin_args_eq (vector signed char, (a1), vector signed char, (a2)), \
      ((vector signed char) ff_vmrglb ((vector signed char) (a1), (vector signed char) (a2))), \
__ch (__bin_args_eq (vector unsigned char, (a1), vector unsigned char, (a2)), \
      ((vector unsigned char) ff_vmrglb ((vector signed char) (a1), (vector signed char) (a2))), \
__ch (__bin_args_eq (vector signed short, (a1), vector signed short, (a2)), \
      ((vector signed short) ff_vmrglh ((vector signed short) (a1), (vector signed short) (a2))), \
__ch (__bin_args_eq (vector unsigned short, (a1), vector unsigned short, (a2)), \
      ((vector unsigned short) ff_vmrglh ((vector signed short) (a1), (vector signed short) (a2))), \
__ch (__bin_args_eq (vector float, (a1), vector float, (a2)), \
      ((vector float) ff_vmrglw ((vector signed int) (a1), (vector signed int) (a2))), \
__ch (__bin_args_eq (vector signed int, (a1), vector signed int, (a2)), \
      ((vector signed int) ff_vmrglw ((vector signed int) (a1), (vector signed int) (a2))), \
__ch (__bin_args_eq (vector unsigned int, (a1), vector unsigned int, (a2)), \
      ((vector unsigned int) ff_vmrglw ((vector signed int) (a1), (vector signed int) (a2))), \
    __altivec_link_error_invalid_argument ())))))))

#endif

#endif /* CONFIG_DARWIN */

#ifndef __MWERKS__
#define const_vector const vector
#else
#define const_vector vector
#endif

#endif /* _GCC_FIXES_ */
