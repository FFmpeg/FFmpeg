/* DCT test. (c) 2001 Gerard Lantau. 
   Started from sample code by Juan J. Sierralta P.
*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "dsputil.h"

extern void fdct(DCTELEM *block);
extern void init_fdct();

#define AANSCALE_BITS 12
static const unsigned short aanscales[64] = {
    /* precomputed values scaled up by 14 bits */
    16384, 22725, 21407, 19266, 16384, 12873,  8867,  4520,
    22725, 31521, 29692, 26722, 22725, 17855, 12299,  6270,
    21407, 29692, 27969, 25172, 21407, 16819, 11585,  5906,
    19266, 26722, 25172, 22654, 19266, 15137, 10426,  5315,
    16384, 22725, 21407, 19266, 16384, 12873,  8867,  4520,
    12873, 17855, 16819, 15137, 12873, 10114,  6967,  3552,
    8867, 12299, 11585, 10426,  8867,  6967,  4799,  2446,
    4520,  6270,  5906,  5315,  4520,  3552,  2446,  1247
};

INT64 gettime(void)
{
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (INT64)tv.tv_sec * 1000000 + tv.tv_usec;
}

#define NB_ITS 20000
#define NB_ITS_SPEED 50000

void dct_error(const char *name,
               void (*fdct_func)(DCTELEM *block))
{
    int it, i, scale;
    DCTELEM block[64], block1[64];
    int err_inf, v;
    INT64 err2, ti, ti1, it1;

    srandom(0);

    err_inf = 0;
    err2 = 0;
    for(it=0;it<NB_ITS;it++) {
        for(i=0;i<64;i++) 
            block1[i] = random() % 256;
        memcpy(block, block1, sizeof(DCTELEM) * 64);
        
        fdct_func(block);
        if (fdct_func == jpeg_fdct_ifast) {
            for(i=0; i<64; i++) {
                scale = (1 << (AANSCALE_BITS + 11)) / aanscales[i];
                block[i] = (block[i] * scale) >> AANSCALE_BITS;
            }
        }

        fdct(block1);

        for(i=0;i<64;i++) {
            v = abs(block[i] - block1[i]);
            if (v > err_inf)
                err_inf = v;
            err2 += v * v;
        }
    }
    printf("DCT %s: err_inf=%d err2=%0.2f\n", 
           name, err_inf, (double)err2 / NB_ITS / 64.0);

    /* speed test */
    for(i=0;i<64;i++) 
        block1[i] = 255 - 63 + i;

    ti = gettime();
    it1 = 0;
    do {
        for(it=0;it<NB_ITS_SPEED;it++) {
            memcpy(block, block1, sizeof(DCTELEM) * 64);
            fdct_func(block);
        }
        it1 += NB_ITS_SPEED;
        ti1 = gettime() - ti;
    } while (ti1 < 1000000);

    printf("DCT %s: %0.1f kdct/s\n", 
           name, (double)it1 * 1000.0 / (double)ti1);
}

int main(int argc, char **argv)
{
    init_fdct();
    
    printf("ffmpeg DCT test\n");

    dct_error("REF", fdct); /* only to verify code ! */
    dct_error("AAN", jpeg_fdct_ifast);
    dct_error("MMX", fdct_mmx);
    return 0;
}
	
