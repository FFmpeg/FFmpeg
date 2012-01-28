/*
 * portable IEEE float/double read/write functions
 *
 * Copyright (c) 2005 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file
 * portable IEEE float/double read/write functions
 */

#include <stdint.h>
#include "mathematics.h"
#include "intfloat_readwrite.h"

double av_int2dbl(int64_t v){
    if((uint64_t)v+v > 0xFFEULL<<52)
        return NAN;
    return ldexp(((v&((1LL<<52)-1)) + (1LL<<52)) * (v>>63|1), (v>>52&0x7FF)-1075);
}

float av_int2flt(int32_t v){
    if((uint32_t)v+v > 0xFF000000U)
        return NAN;
    return ldexp(((v&0x7FFFFF) + (1<<23)) * (v>>31|1), (v>>23&0xFF)-150);
}

double av_ext2dbl(const AVExtFloat ext){
    uint64_t m = 0;
    int e, i;

    for (i = 0; i < 8; i++)
        m = (m<<8) + ext.mantissa[i];
    e = (((int)ext.exponent[0]&0x7f)<<8) | ext.exponent[1];
    if (e == 0x7fff && m)
        return NAN;
    e -= 16383 + 63;        /* In IEEE 80 bits, the whole (i.e. 1.xxxx)
                             * mantissa bit is written as opposed to the
                             * single and double precision formats. */
    if (ext.exponent[0]&0x80)
        m= -m;
    return ldexp(m, e);
}

int64_t av_dbl2int(double d){
    int e;
    if     ( !d) return 0;
    else if(d-d) return 0x7FF0000000000000LL + ((int64_t)(d<0)<<63) + (d!=d);
    d= frexp(d, &e);
    return (int64_t)(d<0)<<63 | (e+1022LL)<<52 | (int64_t)((fabs(d)-0.5)*(1LL<<53));
}

int32_t av_flt2int(float d){
    int e;
    if     ( !d) return 0;
    else if(d-d) return 0x7F800000 + ((d<0)<<31) + (d!=d);
    d= frexp(d, &e);
    return (d<0)<<31 | (e+126)<<23 | (int64_t)((fabs(d)-0.5)*(1<<24));
}

AVExtFloat av_dbl2ext(double d){
    struct AVExtFloat ext= {{0}};
    int e, i; double f; uint64_t m;

    f = fabs(frexp(d, &e));
    if (f >= 0.5 && f < 1) {
        e += 16382;
        ext.exponent[0] = e>>8;
        ext.exponent[1] = e;
        m = (uint64_t)ldexp(f, 64);
        for (i=0; i < 8; i++)
            ext.mantissa[i] = m>>(56-(i<<3));
    } else if (f != 0.0) {
        ext.exponent[0] = 0x7f; ext.exponent[1] = 0xff;
        if (f != INFINITY)
            ext.mantissa[0] = ~0;
    }
    if (d < 0)
        ext.exponent[0] |= 0x80;
    return ext;
}
