/*
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>

#define FFMIN(a,b) ((a) > (b) ? (b) : (a))
#define F 100
#define SIZE 2048

uint64_t exp16_table[21]={
     65537,
     65538,
     65540,
     65544,
     65552,
     65568,
     65600,
     65664,
     65793,
     66050,
     66568,
     67616,
     69763,
     74262,
     84150,
    108051,
    178145,
    484249,
   3578144,
 195360063,
 582360139072LL,
};

#if 0
// 16.16 fixpoint exp()
static unsigned int exp16(unsigned int a){
    int i;
    int out= 1<<16;

    for(i=19;i>=0;i--){
        if(a&(1<<i))
            out= (out*exp16_table[i] + (1<<15))>>16;
    }

    return out;
}
#endif

// 16.16 fixpoint log()
static int64_t log16(uint64_t a){
    int i;
    int out=0;

    if(a < 1<<16)
        return -log16((1LL<<32) / a);
    a<<=16;

    for(i=20;i>=0;i--){
        int64_t b= exp16_table[i];
        if(a<(b<<16)) continue;
        out |= 1<<i;
        a = ((a/b)<<16) + (((a%b)<<16) + b/2)/b;
    }
    return out;
}

static uint64_t int_sqrt(uint64_t a)
{
    uint64_t ret=0;
    int s;
    uint64_t ret_sq=0;

    for(s=31; s>=0; s--){
        uint64_t b= ret_sq + (1ULL<<(s*2)) + (ret<<s)*2;
        if(b<=a){
            ret_sq=b;
            ret+= 1ULL<<s;
        }
    }
    return ret;
}

int main(int argc,char* argv[]){
    int i, j;
    uint64_t sse=0;
    uint64_t dev;
    FILE *f[2];
    uint8_t buf[2][SIZE];
    uint64_t psnr;
    int len= argc<4 ? 1 : atoi(argv[3]);
    int64_t max= (1<<(8*len))-1;
    int shift= argc<5 ? 0 : atoi(argv[4]);
    int skip_bytes = argc<6 ? 0 : atoi(argv[5]);
    int size0=0;
    int size1=0;

    if(argc<3){
        printf("tiny_psnr <file1> <file2> [<elem size> [<shift> [<skip bytes>]]]\n");
        printf("For WAV files use the following:\n");
        printf("./tiny_psnr file1.wav file2.wav 2 0 44 to skip the header.\n");
        return -1;
    }

    f[0]= fopen(argv[1], "rb");
    f[1]= fopen(argv[2], "rb");
    if(!f[0] || !f[1]){
        fprintf(stderr, "Could not open input files.\n");
        return -1;
    }
    fseek(f[shift<0], shift < 0 ? -shift : shift, SEEK_SET);

    fseek(f[0],skip_bytes,SEEK_CUR);
    fseek(f[1],skip_bytes,SEEK_CUR);

    for(;;){
        int s0= fread(buf[0], 1, SIZE, f[0]);
        int s1= fread(buf[1], 1, SIZE, f[1]);

        for(j=0; j<FFMIN(s0,s1); j++){
            int64_t a= buf[0][j];
            int64_t b= buf[1][j];
            if(len==2){
                a= (int16_t)(a | (buf[0][++j]<<8));
                b= (int16_t)(b | (buf[1][  j]<<8));
            }
            sse += (a-b) * (a-b);
        }
        size0 += s0;
        size1 += s1;
        if(s0+s1<=0)
            break;
    }

    i= FFMIN(size0,size1)/len;
    if(!i) i=1;
    dev= int_sqrt( ((sse/i)*F*F) + (((sse%i)*F*F) + i/2)/i );
    if(sse)
        psnr= ((2*log16(max<<16) + log16(i) - log16(sse))*284619LL*F + (1<<31)) / (1LL<<32);
    else
        psnr= 1000*F-1; //floating point free infinity :)

    printf("stddev:%5d.%02d PSNR:%3d.%02d bytes:%9d/%9d\n",
        (int)(dev/F), (int)(dev%F),
        (int)(psnr/F), (int)(psnr%F),
        size0, size1);
    return 0;
}


