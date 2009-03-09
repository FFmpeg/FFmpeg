/*
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
 * @file libavutil/mathematics.c
 * miscellaneous math routines and tables
 */

#include <assert.h>
#include "avutil.h"
#include "common.h"
#include "mathematics.h"

const uint8_t ff_sqrt_tab[256]={
  0, 16, 23, 28, 32, 36, 40, 43, 46, 48, 51, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 77, 79, 80, 82, 84, 85, 87, 88, 90,
 91, 92, 94, 95, 96, 98, 99,100,102,103,104,105,107,108,109,110,111,112,114,115,116,117,118,119,120,121,122,123,124,125,126,127,
128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,144,145,146,147,148,149,150,151,151,152,153,154,155,156,156,
157,158,159,160,160,161,162,163,164,164,165,166,167,168,168,169,170,171,171,172,173,174,174,175,176,176,177,178,179,179,180,181,
182,182,183,184,184,185,186,186,187,188,188,189,190,190,191,192,192,193,194,194,195,196,196,197,198,198,199,200,200,201,202,202,
203,204,204,205,205,206,207,207,208,208,209,210,210,211,212,212,213,213,214,215,215,216,216,217,218,218,219,219,220,220,221,222,
222,223,223,224,224,225,226,226,227,227,228,228,229,230,230,231,231,232,232,233,233,234,235,235,236,236,237,237,238,238,239,239,
240,240,241,242,242,243,243,244,244,245,245,246,246,247,247,248,248,249,249,250,250,251,251,252,252,253,253,254,254,255,255,255
};

const uint8_t ff_log2_tab[256]={
        0,0,1,1,2,2,2,2,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
        6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
        6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7
};

int64_t av_gcd(int64_t a, int64_t b){
    if(b) return av_gcd(b, a%b);
    else  return a;
}

int64_t av_rescale_rnd(int64_t a, int64_t b, int64_t c, enum AVRounding rnd){
    int64_t r=0;
    assert(c > 0);
    assert(b >=0);
    assert(rnd >=0 && rnd<=5 && rnd!=4);

    if(a<0 && a != INT64_MIN) return -av_rescale_rnd(-a, b, c, rnd ^ ((rnd>>1)&1));

    if(rnd==AV_ROUND_NEAR_INF) r= c/2;
    else if(rnd&1)             r= c-1;

    if(b<=INT_MAX && c<=INT_MAX){
        if(a<=INT_MAX)
            return (a * b + r)/c;
        else
            return a/c*b + (a%c*b + r)/c;
    }else{
#if 1
        uint64_t a0= a&0xFFFFFFFF;
        uint64_t a1= a>>32;
        uint64_t b0= b&0xFFFFFFFF;
        uint64_t b1= b>>32;
        uint64_t t1= a0*b1 + a1*b0;
        uint64_t t1a= t1<<32;
        int i;

        a0 = a0*b0 + t1a;
        a1 = a1*b1 + (t1>>32) + (a0<t1a);
        a0 += r;
        a1 += a0<r;

        for(i=63; i>=0; i--){
//            int o= a1 & 0x8000000000000000ULL;
            a1+= a1 + ((a0>>i)&1);
            t1+=t1;
            if(/*o || */c <= a1){
                a1 -= c;
                t1++;
            }
        }
        return t1;
    }
#else
        AVInteger ai;
        ai= av_mul_i(av_int2i(a), av_int2i(b));
        ai= av_add_i(ai, av_int2i(r));

        return av_i2int(av_div_i(ai, av_int2i(c)));
    }
#endif
}

int64_t av_rescale(int64_t a, int64_t b, int64_t c){
    return av_rescale_rnd(a, b, c, AV_ROUND_NEAR_INF);
}

int64_t av_rescale_q(int64_t a, AVRational bq, AVRational cq){
    int64_t b= bq.num * (int64_t)cq.den;
    int64_t c= cq.num * (int64_t)bq.den;
    return av_rescale_rnd(a, b, c, AV_ROUND_NEAR_INF);
}

#ifdef TEST
#include "integer.h"
#undef printf
int main(void){
    int64_t a,b,c,d,e;

    for(a=7; a<(1LL<<62); a+=a/3+1){
        for(b=3; b<(1LL<<62); b+=b/4+1){
            for(c=9; c<(1LL<<62); c+=(c*2)/5+3){
                int64_t r= c/2;
                AVInteger ai;
                ai= av_mul_i(av_int2i(a), av_int2i(b));
                ai= av_add_i(ai, av_int2i(r));

                d= av_i2int(av_div_i(ai, av_int2i(c)));

                e= av_rescale(a,b,c);

                if((double)a * (double)b / (double)c > (1LL<<63))
                    continue;

                if(d!=e) printf("%"PRId64"*%"PRId64"/%"PRId64"= %"PRId64"=%"PRId64"\n", a, b, c, d, e);
            }
        }
    }
    return 0;
}
#endif
