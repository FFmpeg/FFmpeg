/*
 * arbitrary precision integers
 * Copyright (c) 2004 Michael Niedermayer <michaelni@gmx.at>
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
 *
 */
 
/**
 * @file integer.c
 * arbitrary precision integers.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "common.h"
#include "integer.h"

AVInteger av_add_i(AVInteger a, AVInteger b){
    int i, carry=0;
    
    for(i=0; i<AV_INTEGER_SIZE; i++){
        carry= (carry>>16) + a.v[i] + b.v[i];
        a.v[i]= carry;
    }
    return a;
}

AVInteger av_sub_i(AVInteger a, AVInteger b){
    int i, carry=0;
    
    for(i=0; i<AV_INTEGER_SIZE; i++){
        carry= (carry>>16) + a.v[i] - b.v[i];
        a.v[i]= carry;
    }
    return a;
}

/**
 * returns the rounded down value of the logarithm of base 2 of the given AVInteger.
 * this is simply the index of the most significant bit which is 1. Or 0 of all bits are 0
 */
int av_log2_i(AVInteger a){
    int i;

    for(i=AV_INTEGER_SIZE-1; i>=0; i--){
        if(a.v[i])
            return av_log2_16bit(a.v[i]) + 16*i;
    }
    return -1;
}

AVInteger av_mul_i(AVInteger a, AVInteger b){
    AVInteger out;
    int i, j;
    int na= (av_log2_i(a)+16) >> 4;
    int nb= (av_log2_i(b)+16) >> 4;
    
    memset(&out, 0, sizeof(out));
    
    for(i=0; i<na; i++){
        unsigned int carry=0;
        
        if(a.v[i])
            for(j=i; j<AV_INTEGER_SIZE && j-i<=nb; j++){
                carry= (carry>>16) + out.v[j] + a.v[i]*b.v[j-i];
                out.v[j]= carry;
            }
    }

    return out;
}

/**
 * returns 0 if a==b, 1 if a>b and -1 if a<b.
 */
int av_cmp_i(AVInteger a, AVInteger b){
    int i; 
    int v= (int16_t)a.v[AV_INTEGER_SIZE-1] - (int16_t)b.v[AV_INTEGER_SIZE-1];
    if(v) return (v>>16)|1;
    
    for(i=AV_INTEGER_SIZE-2; i>=0; i--){
        int v= a.v[i] - b.v[i];
        if(v) return (v>>16)|1;
    }
    return 0;
}

/**
 * bitwise shift.
 * @param s the number of bits by which the value should be shifted right, may be negative for shifting left
 */
AVInteger av_shr_i(AVInteger a, int s){
    AVInteger out;
    int i;

    for(i=0; i<AV_INTEGER_SIZE; i++){
        int index= i + (s>>4);
        unsigned int v=0;
        if(index+1<AV_INTEGER_SIZE && index+1>=0) v = a.v[index+1]<<16;
        if(index  <AV_INTEGER_SIZE && index  >=0) v+= a.v[index  ];
        out.v[i]= v >> (s&15);
    }
    return out;
}

/**
 * returns a % b.
 * @param quot a/b will be stored here
 */
AVInteger av_mod_i(AVInteger *quot, AVInteger a, AVInteger b){
    int i= av_log2_i(a) - av_log2_i(b);
    AVInteger quot_temp;
    if(!quot) quot = &quot_temp;
    
    assert((int16_t)a[AV_INTEGER_SIZE-1] >= 0 && (int16_t)b[AV_INTEGER_SIZE-1] >= 0);
    assert(av_log2(b)>=0);
    
    if(i > 0)
        b= av_shr_i(b, -i);
        
    memset(quot, 0, sizeof(AVInteger));

    while(i-- >= 0){
        *quot= av_shr_i(*quot, -1);
        if(av_cmp_i(a, b) >= 0){
            a= av_sub_i(a, b);
            quot->v[0] += 1;
        }
        b= av_shr_i(b, 1);
    }
    return a;
}

/**
 * returns a/b.
 */
AVInteger av_div_i(AVInteger a, AVInteger b){
    AVInteger quot;
    av_mod_i(&quot, a, b);
    return quot;
}

/**
 * converts the given int64_t to an AVInteger.
 */
AVInteger av_int2i(int64_t a){
    AVInteger out;
    int i;
    
    for(i=0; i<AV_INTEGER_SIZE; i++){
        out.v[i]= a;
        a>>=16;
    }
    return out;
}

/**
 * converts the given AVInteger to an int64_t.
 * if the AVInteger is too large to fit into an int64_t, 
 * then only the least significant 64bit will be used
 */
int64_t av_i2int(AVInteger a){
    int i;
    int64_t out=(int8_t)a.v[AV_INTEGER_SIZE-1];
    
    for(i= AV_INTEGER_SIZE-2; i>=0; i--){
        out = (out<<16) + a.v[i];
    }
    return out;
}

#if 0
#undef NDEBUG
#include <assert.h>

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

main(){
    int64_t a,b;

    for(a=7; a<256*256*256; a+=13215){
        for(b=3; b<256*256*256; b+=27118){
            AVInteger ai= av_int2i(a);
            AVInteger bi= av_int2i(b);
            
            assert(av_i2int(ai) == a);
            assert(av_i2int(bi) == b);
            assert(av_i2int(av_add_i(ai,bi)) == a+b);
            assert(av_i2int(av_sub_i(ai,bi)) == a-b);
            assert(av_i2int(av_mul_i(ai,bi)) == a*b);
            assert(av_i2int(av_shr_i(ai, 9)) == a>>9);
            assert(av_i2int(av_shr_i(ai,-9)) == a<<9);
            assert(av_i2int(av_shr_i(ai, 17)) == a>>17);
            assert(av_i2int(av_shr_i(ai,-17)) == a<<17);
            assert(av_log2_i(ai) == av_log2(a));
            assert(av_i2int(av_div_i(ai,bi)) == a/b);
        }
    }
}
#endif
