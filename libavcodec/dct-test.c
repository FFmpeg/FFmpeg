/* DCT test. (c) 2001 Fabrice Bellard. 
   Started from sample code by Juan J. Sierralta P.
*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <getopt.h>

#include "dsputil.h"

#include "i386/mmx.h"

/* reference fdct/idct */
extern void fdct(DCTELEM *block);
extern void idct(DCTELEM *block);
extern void init_fdct();

extern void j_rev_dct(DCTELEM *data);
extern void ff_mmx_idct(DCTELEM *data);
extern void ff_mmxext_idct(DCTELEM *data);

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

static short idct_mmx_perm[64];

void idct_mmx_init(void)
{
    int i;

    /* the mmx/mmxext idct uses a reordered input, so we patch scan tables */
    for (i = 0; i < 64; i++) {
	idct_mmx_perm[i] = (i & 0x38) | ((i & 6) >> 1) | ((i & 1) << 2);
    }
}

static DCTELEM block[64] __attribute__ ((aligned (8)));
static DCTELEM block1[64] __attribute__ ((aligned (8)));

void dct_error(const char *name, int is_idct,
               void (*fdct_func)(DCTELEM *block),
               void (*fdct_ref)(DCTELEM *block))
{
    int it, i, scale;
    int err_inf, v;
    INT64 err2, ti, ti1, it1;

    srandom(0);

    err_inf = 0;
    err2 = 0;
    for(it=0;it<NB_ITS;it++) {
        for(i=0;i<64;i++) 
            block1[i] = random() % 256;

        /* for idct test, generate inverse idct data */
        if (is_idct)
            fdct(block1);

        if (fdct_func == ff_mmx_idct ||
            fdct_func == j_rev_dct) {
            for(i=0;i<64;i++) 
                block[idct_mmx_perm[i]] = block1[i];
        } else {
            memcpy(block, block1, sizeof(DCTELEM) * 64);
        }

        fdct_func(block);
        emms(); /* for ff_mmx_idct */

        if (fdct_func == fdct_ifast) {
            for(i=0; i<64; i++) {
                scale = (1 << (AANSCALE_BITS + 11)) / aanscales[i];
                block[i] = (block[i] * scale) >> AANSCALE_BITS;
            }
        }

        fdct_ref(block1);

        for(i=0;i<64;i++) {
            v = abs(block[i] - block1[i]);
            if (v > err_inf)
                err_inf = v;
            err2 += v * v;
        }
    }
    printf("%s %s: err_inf=%d err2=%0.2f\n", 
           is_idct ? "IDCT" : "DCT",
           name, err_inf, (double)err2 / NB_ITS / 64.0);

    /* speed test */
    for(i=0;i<64;i++) 
        block1[i] = 255 - 63 + i;

    /* for idct test, generate inverse idct data */
    if (is_idct)
        fdct(block1);
    if (fdct_func == ff_mmx_idct ||
        fdct_func == j_rev_dct) {
        for(i=0;i<64;i++) 
            block[idct_mmx_perm[i]] = block1[i];
    }

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
    emms();

    printf("%s %s: %0.1f kdct/s\n", 
           is_idct ? "IDCT" : "DCT",
           name, (double)it1 * 1000.0 / (double)ti1);
}

void help(void)
{
    printf("dct-test [-i]\n"
           "test DCT implementations\n");
    exit(1);
}

int main(int argc, char **argv)
{
    int test_idct = 0;
    int c;

    init_fdct();
    idct_mmx_init();

    for(;;) {
        c = getopt(argc, argv, "ih");
        if (c == -1)
            break;
        switch(c) {
        case 'i':
            test_idct = 1;
            break;
        case 'h':
            help();
            break;
        }
    }
               
    printf("ffmpeg DCT/IDCT test\n");

    if (!test_idct) {
        dct_error("REF", 0, fdct, fdct); /* only to verify code ! */
        dct_error("AAN", 0, fdct_ifast, fdct);
        dct_error("MMX", 0, fdct_mmx, fdct);
    } else {
        dct_error("REF", 1, idct, idct);
        dct_error("INT", 1, j_rev_dct, idct);
        dct_error("MMX", 1, ff_mmx_idct, idct);
        //    dct_error("MMX", 1, ff_mmxext_idct, idct);
    }
    return 0;
}
