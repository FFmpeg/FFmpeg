/*
 * High quality image resampling with polyphase filters
 * Copyright (c) 2001 Fabrice Bellard
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
 * @file libavcodec/ppc/imgresample_altivec.c
 * High quality image resampling with polyphase filters - AltiVec bits
 */

#include "util_altivec.h"
#define FILTER_BITS   8

typedef         union {
    vector signed short v;
    signed short s[8];
} vec_ss;

void v_resample16_altivec(uint8_t *dst, int dst_width, const uint8_t *src,
                          int wrap, int16_t *filter)
{
    int sum, i;
    const uint8_t *s;
    vector unsigned char *tv, tmp, dstv, zero;
    vec_ss srchv[4], srclv[4], fv[4];
    vector signed short zeros, sumhv, sumlv;
    s = src;

    for(i=0;i<4;i++) {
        /*
           The vec_madds later on does an implicit >>15 on the result.
           Since FILTER_BITS is 8, and we have 15 bits of magnitude in
           a signed short, we have just enough bits to pre-shift our
           filter constants <<7 to compensate for vec_madds.
        */
        fv[i].s[0] = filter[i] << (15-FILTER_BITS);
        fv[i].v = vec_splat(fv[i].v, 0);
    }

    zero = vec_splat_u8(0);
    zeros = vec_splat_s16(0);


    /*
       When we're resampling, we'd ideally like both our input buffers,
       and output buffers to be 16-byte aligned, so we can do both aligned
       reads and writes. Sadly we can't always have this at the moment, so
       we opt for aligned writes, as unaligned writes have a huge overhead.
       To do this, do enough scalar resamples to get dst 16-byte aligned.
    */
    i = (-(int)dst) & 0xf;
    while(i>0) {
        sum = s[0 * wrap] * filter[0] +
        s[1 * wrap] * filter[1] +
        s[2 * wrap] * filter[2] +
        s[3 * wrap] * filter[3];
        sum = sum >> FILTER_BITS;
        if (sum<0) sum = 0; else if (sum>255) sum=255;
        dst[0] = sum;
        dst++;
        s++;
        dst_width--;
        i--;
    }

    /* Do our altivec resampling on 16 pixels at once. */
    while(dst_width>=16) {
        /* Read 16 (potentially unaligned) bytes from each of
           4 lines into 4 vectors, and split them into shorts.
           Interleave the multipy/accumulate for the resample
           filter with the loads to hide the 3 cycle latency
           the vec_madds have. */
        tv = (vector unsigned char *) &s[0 * wrap];
        tmp = vec_perm(tv[0], tv[1], vec_lvsl(0, &s[i * wrap]));
        srchv[0].v = (vector signed short) vec_mergeh(zero, tmp);
        srclv[0].v = (vector signed short) vec_mergel(zero, tmp);
        sumhv = vec_madds(srchv[0].v, fv[0].v, zeros);
        sumlv = vec_madds(srclv[0].v, fv[0].v, zeros);

        tv = (vector unsigned char *) &s[1 * wrap];
        tmp = vec_perm(tv[0], tv[1], vec_lvsl(0, &s[1 * wrap]));
        srchv[1].v = (vector signed short) vec_mergeh(zero, tmp);
        srclv[1].v = (vector signed short) vec_mergel(zero, tmp);
        sumhv = vec_madds(srchv[1].v, fv[1].v, sumhv);
        sumlv = vec_madds(srclv[1].v, fv[1].v, sumlv);

        tv = (vector unsigned char *) &s[2 * wrap];
        tmp = vec_perm(tv[0], tv[1], vec_lvsl(0, &s[2 * wrap]));
        srchv[2].v = (vector signed short) vec_mergeh(zero, tmp);
        srclv[2].v = (vector signed short) vec_mergel(zero, tmp);
        sumhv = vec_madds(srchv[2].v, fv[2].v, sumhv);
        sumlv = vec_madds(srclv[2].v, fv[2].v, sumlv);

        tv = (vector unsigned char *) &s[3 * wrap];
        tmp = vec_perm(tv[0], tv[1], vec_lvsl(0, &s[3 * wrap]));
        srchv[3].v = (vector signed short) vec_mergeh(zero, tmp);
        srclv[3].v = (vector signed short) vec_mergel(zero, tmp);
        sumhv = vec_madds(srchv[3].v, fv[3].v, sumhv);
        sumlv = vec_madds(srclv[3].v, fv[3].v, sumlv);

        /* Pack the results into our destination vector,
           and do an aligned write of that back to memory. */
        dstv = vec_packsu(sumhv, sumlv) ;
        vec_st(dstv, 0, (vector unsigned char *) dst);

        dst+=16;
        s+=16;
        dst_width-=16;
    }

    /* If there are any leftover pixels, resample them
       with the slow scalar method. */
    while(dst_width>0) {
        sum = s[0 * wrap] * filter[0] +
        s[1 * wrap] * filter[1] +
        s[2 * wrap] * filter[2] +
        s[3 * wrap] * filter[3];
        sum = sum >> FILTER_BITS;
        if (sum<0) sum = 0; else if (sum>255) sum=255;
        dst[0] = sum;
        dst++;
        s++;
        dst_width--;
    }
}

