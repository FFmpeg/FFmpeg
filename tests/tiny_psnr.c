/*
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

#define F 100
#define SIZE 2048

uint64_t exp16_table[20]={
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
};
#if 1
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
// 16.16 fixpoint log()
static uint64_t log16(uint64_t a){
    int i;
    int out=0;
    
    assert(a >= (1<<16));
    a<<=16;
    
    for(i=19;i>=0;i--){
        int64_t b= exp16_table[i];
        if(a<(b<<16)) continue;
        out |= 1<<i;
        a = ((a/b)<<16) + (((a%b)<<16) + b/2)/b;
    }
    return out;
}

#endif
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
    
    if(argc!=3){
        printf("tiny_psnr <file1> <file2>\n");
        return -1;
    }
    
    f[0]= fopen(argv[1], "r");
    f[1]= fopen(argv[2], "r");

    for(i=0;;){
        if( fread(buf[0], SIZE, 1, f[0]) != 1) break;
        if( fread(buf[1], SIZE, 1, f[1]) != 1) break;
        
        for(j=0; j<SIZE; i++,j++){
            const int a= buf[0][j];
            const int b= buf[1][j];
            sse += (a-b) * (a-b);
        }
    }
    
    dev= int_sqrt((sse*F*F)/i);
    if(sse)
        psnr= (log16(256*256*255*255LL*i/sse)*284619LL*F + (1<<31)) / (1LL<<32);
    else
        psnr= 100*F-1; //floating point free infinity :)
    
    printf("stddev:%3d.%02d PSNR:%2d.%02d bytes:%d\n", 
        (int)(dev/F), (int)(dev%F), 
        (int)(psnr/F), (int)(psnr%F),
        i);
    return 0;
}


