/*
 * simple math operations
 *
 * Copyright (C) 2007 Marc Hoffman <mmhoffm@gmail.com>
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
#ifndef AVCODEC_BFIN_MATHOPS_H
#define AVCODEC_BFIN_MATHOPS_H

#include "config.h"

#define MULH(X,Y) ({ int xxo;                           \
    __asm__ (                                               \
        "a1 = %2.L * %1.L (FU);\n\t"                    \
        "a1 = a1 >> 16;\n\t"                            \
        "a1 += %2.H * %1.L (IS,M);\n\t"                 \
        "a0 = %1.H * %2.H, a1+= %1.H * %2.L (IS,M);\n\t"\
        "a1 = a1 >>> 16;\n\t"                           \
        "%0 = (a0 += a1);\n\t"                          \
        : "=d" (xxo) : "d" (X), "d" (Y) : "A0","A1"); xxo; })

/* signed 16x16 -> 32 multiply */
#define MUL16(a, b) ({ int xxo;                         \
    __asm__ (                                               \
       "%0 = %1.l*%2.l (is);\n\t"                       \
       : "=W" (xxo) : "d" (a), "d" (b) : "A1");         \
    xxo; })

#endif /* AVCODEC_BFIN_MATHOPS_H */
