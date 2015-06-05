/*
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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

#include <inttypes.h>
#include <stdio.h>
#include "softfloat.h"
#include "common.h"
#include "log.h"

#undef printf

static const SoftFloat FLOAT_0_017776489257 = {0x1234, 12};
static const SoftFloat FLOAT_1374_40625 = {0xabcd, 25};
static const SoftFloat FLOAT_0_1249694824218 = {0xFFF, 15};


static av_const double av_sf2double(SoftFloat v) {
    v.exp -= ONE_BITS +1;
    if(v.exp > 0) return (double)v.mant * (double)(1 << v.exp);
    else          return (double)v.mant / (double)(1 << (-v.exp));
}

void av_sincos_sf(int a, int *s, int *c)
{
    int idx, sign;
    int sv, cv;
    int st, ct;

    idx = a >> 26;
    sign = (idx << 27) >> 31;
    cv = av_costbl_1_sf[idx & 0xf];
    cv = (cv ^ sign) - sign;

    idx -= 8;
    sign = (idx << 27) >> 31;
    sv = av_costbl_1_sf[idx & 0xf];
    sv = (sv ^ sign) - sign;

    idx = a >> 21;
    ct = av_costbl_2_sf[idx & 0x1f];
    st = av_sintbl_2_sf[idx & 0x1f];

    idx = (int)(((int64_t)cv * ct - (int64_t)sv * st + 0x20000000) >> 30);

    sv = (int)(((int64_t)cv * st + (int64_t)sv * ct + 0x20000000) >> 30);

    cv = idx;

    idx = a >> 16;
    ct = av_costbl_3_sf[idx & 0x1f];
    st = av_sintbl_3_sf[idx & 0x1f];

    idx = (int)(((int64_t)cv * ct - (int64_t)sv * st + 0x20000000) >> 30);

    sv = (int)(((int64_t)cv * st + (int64_t)sv * ct + 0x20000000) >> 30);
    cv = idx;

    idx = a >> 11;

    ct = (int)(((int64_t)av_costbl_4_sf[idx & 0x1f] * (0x800 - (a & 0x7ff)) +
                (int64_t)av_costbl_4_sf[(idx & 0x1f)+1]*(a & 0x7ff) +
                0x400) >> 11);
    st = (int)(((int64_t)av_sintbl_4_sf[idx & 0x1f] * (0x800 - (a & 0x7ff)) +
                (int64_t)av_sintbl_4_sf[(idx & 0x1f) + 1] * (a & 0x7ff) +
                0x400) >> 11);

    *c = (int)(((int64_t)cv * ct + (int64_t)sv * st + 0x20000000) >> 30);

    *s = (int)(((int64_t)cv * st + (int64_t)sv * ct + 0x20000000) >> 30);
}

int main(void){
    SoftFloat one= av_int2sf(1, 0);
    SoftFloat sf1, sf2, sf3;
    double d1, d2, d3;
    int i, j;
    av_log_set_level(AV_LOG_DEBUG);

    d1= 1;
    for(i= 0; i<10; i++){
        d1= 1/(d1+1);
    }
    printf("test1 double=%d\n", (int)(d1 * (1<<24)));

    sf1= one;
    for(i= 0; i<10; i++){
        sf1= av_div_sf(one, av_normalize_sf(av_add_sf(one, sf1)));
    }
    printf("test1 sf    =%d\n", av_sf2int(sf1, 24));


    for(i= 0; i<100; i++){
        START_TIMER
        d1= i;
        d2= i/100.0;
        for(j= 0; j<1000; j++){
            d1= (d1+1)*d2;
        }
        STOP_TIMER("float add mul")
    }
    printf("test2 double=%d\n", (int)(d1 * (1<<24)));

    for(i= 0; i<100; i++){
        START_TIMER
        sf1= av_int2sf(i, 0);
        sf2= av_div_sf(av_int2sf(i, 2), av_int2sf(200, 3));
        for(j= 0; j<1000; j++){
            sf1= av_mul_sf(av_add_sf(sf1, one),sf2);
        }
        STOP_TIMER("softfloat add mul")
    }
    printf("test2 sf    =%d (%d %d)\n", av_sf2int(sf1, 24), sf1.exp, sf1.mant);

    d1 = 0.0177764893;
    d2 = 1374.40625;
    d3 = 0.1249694824;
    d2 += d1;
    d3 += d2;
    printf("test3 double: %.10lf\n", d3);

    sf1 = FLOAT_0_017776489257;
    sf2 = FLOAT_1374_40625;
    sf3 = FLOAT_0_1249694824218;
    sf2 = av_add_sf(sf1, sf2);
    sf3 = av_add_sf(sf3, sf2);
    printf("test3 softfloat: %.10lf (0x%08x %d)\n", (double)av_sf2double(sf3), sf3.mant, sf3.exp);

    sf1 = av_int2sf(0xFFFFFFF0, 0);
    printf("test4 softfloat: %.10lf (0x%08x %d)\n", (double)av_sf2double(sf1), sf1.mant, sf1.exp);
    sf1 = av_int2sf(0x00000010, 0);
    printf("test4 softfloat: %.10lf (0x%08x %d)\n", (double)av_sf2double(sf1), sf1.mant, sf1.exp);

    sf1 = av_int2sf(0x1FFFFFFF, 0);
    printf("test4 softfloat: %.10lf (0x%08x %d)\n", (double)av_sf2double(sf1), sf1.mant, sf1.exp);
    sf1 = av_int2sf(0xE0000001, 0);
    printf("test4 softfloat: %.10lf (0x%08x %d)\n", (double)av_sf2double(sf1), sf1.mant, sf1.exp);

    return 0;

}
