/*
 * portable IEEE float/double read/write functions
 *
 * Copyright (c) 2005 Michael Niedermayer <michaelni@gmx.at>
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
 
/**
 * @file intfloat_readwrite.c
 * Portable IEEE float/double read/write functions.
 */
 
#include "common.h"

double av_int2dbl(int64_t v){
    if(v+v > 0xFFELLU<<52)
        return 0.0/0.0;
    return ldexp(((v&((1LL<<52)-1)) + (1LL<<52)) * (v>>63|1), (v>>52&0x7FF)-1075);
}

float av_int2flt(int32_t v){
    if(v+v > 0xFF000000U)
        return 0.0/0.0;
    return ldexp(((v&0x7FFFFF) + (1<<23)) * (v>>31|1), (v>>23&0xFF)-150);
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
