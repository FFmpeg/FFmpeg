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

#include "libavutil/softfloat.h"
#include "libavutil/common.h"
#include "libavutil/log.h"

#include <stdio.h>

static const SoftFloat FLOAT_0_017776489257 = {0x1234, 12};
static const SoftFloat FLOAT_1374_40625 = {0xabcd, 25};
static const SoftFloat FLOAT_0_1249694824218 = {0xFFF, 15};


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


    sf1 = (SoftFloat){ 0x20000000,   MIN_EXP };
    sf1 = av_mul_sf(sf1, sf1);
    printf("test5 softfloat: %.10lf (0x%08x %d)\n", (double)av_sf2double(sf1), sf1.mant, sf1.exp);

    sf1 = (SoftFloat){ 0x20000000,   MIN_EXP };
    sf2 = (SoftFloat){ 0x20000000,   MAX_EXP };
    i = av_cmp_sf(sf1, sf2);
    j = av_cmp_sf(sf2, sf1);
    sf1 = av_div_sf(sf1, sf2);
    printf("test6 softfloat: %.10lf (0x%08x %d) %d %d\n", (double)av_sf2double(sf1), sf1.mant, sf1.exp, i, j);

    for(i= -50; i<50; i++) {
        sf1= av_int2sf(i, 0);
        for(j= -50; j<50; j++) {
            int c;
            sf2= av_int2sf(j, 0);
            c = av_cmp_sf(sf1, sf2);
            if (FFDIFFSIGN(i,j) != c && (FFDIFFSIGN(i,j)^c)<0) {
                printf("av_cmp_sf failed at %d %d as %X\n", i, j, c);
            }
            c = av_gt_sf(sf1, sf2);
            if ((i>j) != c) {
                printf("av_gt_sf failed at %d %d as %X\n", i, j, c);
            }
        }
        sf1 = av_int2sf(1, i);
        for(j = -50; j < 50; j++) {
            int c;
            sf2 = av_int2sf(1, j);
            c = av_cmp_sf(sf2, sf1);
            if (FFDIFFSIGN(i,j) != c && (FFDIFFSIGN(i,j)^c) < 0) {
                printf("av_cmp_sf failed2 at %d %d as %X\n", i, j, c);
            }
            c = av_gt_sf(sf1, sf2);
            if ((i<j) != c) {
                printf("av_gt_sf failed2 at %d %d as %X\n", i, j, c);
            }
        }
    }


    for(i= 0; i<4*36; i++){
        int s, c;
        double errs, errc;

        av_sincos_sf(i*(1ULL<<32)/36/4, &s, &c);
        errs = (double)s/ (1<<30) - sin(i*M_PI/36);
        errc = (double)c/ (1<<30) - cos(i*M_PI/36);
        if (fabs(errs) > 0.00000002 || fabs(errc) >0.001) {
            printf("sincos FAIL %d %f %f %f %f\n", i, (float)s/ (1<<30), (float)c/ (1<<30), sin(i*M_PI/36), cos(i*M_PI/36));
        }

    }
    return 0;

}
