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
#include "simple_idct.h"

/* reference fdct/idct */
extern void fdct(DCTELEM *block);
extern void idct(DCTELEM *block);
extern void init_fdct();

extern void j_rev_dct(DCTELEM *data);
extern void ff_mmx_idct(DCTELEM *data);
extern void ff_mmxext_idct(DCTELEM *data);

extern void odivx_idct_c (short *block);

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

UINT8 cropTbl[256 + 2 * MAX_NEG_CROP];

INT64 gettime(void)
{
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (INT64)tv.tv_sec * 1000000 + tv.tv_usec;
}

#define NB_ITS 20000
#define NB_ITS_SPEED 50000

static short idct_mmx_perm[64];

static short idct_simple_mmx_perm[64]={
	0x00, 0x08, 0x04, 0x09, 0x01, 0x0C, 0x05, 0x0D, 
	0x10, 0x18, 0x14, 0x19, 0x11, 0x1C, 0x15, 0x1D, 
	0x20, 0x28, 0x24, 0x29, 0x21, 0x2C, 0x25, 0x2D, 
	0x12, 0x1A, 0x16, 0x1B, 0x13, 0x1E, 0x17, 0x1F, 
	0x02, 0x0A, 0x06, 0x0B, 0x03, 0x0E, 0x07, 0x0F, 
	0x30, 0x38, 0x34, 0x39, 0x31, 0x3C, 0x35, 0x3D, 
	0x22, 0x2A, 0x26, 0x2B, 0x23, 0x2E, 0x27, 0x2F, 
	0x32, 0x3A, 0x36, 0x3B, 0x33, 0x3E, 0x37, 0x3F,
};

void idct_mmx_init(void)
{
    int i;

    /* the mmx/mmxext idct uses a reordered input, so we patch scan tables */
    for (i = 0; i < 64; i++) {
	idct_mmx_perm[i] = (i & 0x38) | ((i & 6) >> 1) | ((i & 1) << 2);
//	idct_simple_mmx_perm[i] = simple_block_permute_op(i);
    }
}

static DCTELEM block[64] __attribute__ ((aligned (8)));
static DCTELEM block1[64] __attribute__ ((aligned (8)));
static DCTELEM block_org[64] __attribute__ ((aligned (8)));

void dct_error(const char *name, int is_idct,
               void (*fdct_func)(DCTELEM *block),
               void (*fdct_ref)(DCTELEM *block), int test)
{
    int it, i, scale;
    int err_inf, v;
    INT64 err2, ti, ti1, it1;
    INT64 sysErr[64], sysErrMax=0;
    int maxout=0;
    int max_sum=0;
    int blockSumErrMax=0, blockSumErr;

    srandom(0);

    err_inf = 0;
    err2 = 0;
    for(i=0; i<64; i++) sysErr[i]=0;
    for(it=0;it<NB_ITS;it++) {
        for(i=0;i<64;i++)
            block1[i] = 0;
        switch(test){
        case 0: 
            for(i=0;i<64;i++)
                block1[i] = (random() % 512) -256;
            if (is_idct){
                fdct(block1);

                for(i=0;i<64;i++)
                    block1[i]>>=3;
            }
        break;
        case 1:{
            int num= (random()%10)+1;
            for(i=0;i<num;i++)
                block1[random()%64] = (random() % 512) -256;
        }break;
        case 2:
            block1[0]= (random()%4096)-2048;
            block1[63]= (block1[0]&1)^1;
        break;
        }

#if 0 // simulate mismatch control
{ int sum=0;
        for(i=0;i<64;i++)
           sum+=block1[i];

        if((sum&1)==0) block1[63]^=1; 
}
#endif

        for(i=0; i<64; i++)
            block_org[i]= block1[i];

        if (fdct_func == ff_mmx_idct ||
            fdct_func == j_rev_dct || fdct_func == ff_mmxext_idct) {
            for(i=0;i<64;i++)
                block[idct_mmx_perm[i]] = block1[i];
        } else if(fdct_func == simple_idct_mmx ) {
            for(i=0;i<64;i++)
                block[idct_simple_mmx_perm[i]] = block1[i];

	} else {
            for(i=0; i<64; i++)
                block[i]= block1[i];
        }
#if 0 // simulate mismatch control for tested IDCT but not the ref
{ int sum=0;
        for(i=0;i<64;i++)
           sum+=block[i];

        if((sum&1)==0) block[63]^=1; 
}
#endif

        fdct_func(block);
        emms(); /* for ff_mmx_idct */

        if (fdct_func == fdct_ifast) {
            for(i=0; i<64; i++) {
                scale = 8*(1 << (AANSCALE_BITS + 11)) / aanscales[i];
                block[i] = (block[i] * scale /*+ (1<<(AANSCALE_BITS-1))*/) >> AANSCALE_BITS;
            }
        }

        fdct_ref(block1);

        blockSumErr=0;
        for(i=0;i<64;i++) {
            v = abs(block[i] - block1[i]);
            if (v > err_inf)
                err_inf = v;
            err2 += v * v;
	    sysErr[i] += block[i] - block1[i];
	    blockSumErr += v;
	    if( abs(block[i])>maxout) maxout=abs(block[i]);
        }
        if(blockSumErrMax < blockSumErr) blockSumErrMax= blockSumErr;
#if 0 // print different matrix pairs
        if(blockSumErr){
            printf("\n");
            for(i=0; i<64; i++){
                if((i&7)==0) printf("\n");
                printf("%4d ", block_org[i]);
            }
            for(i=0; i<64; i++){
                if((i&7)==0) printf("\n");
                printf("%4d ", block[i] - block1[i]);
            }
        }
#endif
    }
    for(i=0; i<64; i++) sysErrMax= MAX(sysErrMax, ABS(sysErr[i]));
    
#if 1 // dump systematic errors
    for(i=0; i<64; i++){
	if(i%8==0) printf("\n");
        printf("%5d ", (int)sysErr[i]);
    }
    printf("\n");
#endif
    
    printf("%s %s: err_inf=%d err2=%0.8f syserr=%0.8f maxout=%d blockSumErr=%d\n",
           is_idct ? "IDCT" : "DCT",
           name, err_inf, (double)err2 / NB_ITS / 64.0, (double)sysErrMax / NB_ITS, maxout, blockSumErrMax);
#if 1 //Speed test
    /* speed test */
    for(i=0;i<64;i++)
        block1[i] = 0;
    switch(test){
    case 0: 
        for(i=0;i<64;i++)
            block1[i] = (random() % 512) -256;
        if (is_idct){
            fdct(block1);

            for(i=0;i<64;i++)
                block1[i]>>=3;
        }
    break;
    case 1:{
    case 2:
        block1[0] = (random() % 512) -256;
        block1[1] = (random() % 512) -256;
        block1[2] = (random() % 512) -256;
        block1[3] = (random() % 512) -256;
    }break;
    }

    if (fdct_func == ff_mmx_idct ||
        fdct_func == j_rev_dct || fdct_func == ff_mmxext_idct) {
        for(i=0;i<64;i++)
            block[idct_mmx_perm[i]] = block1[i];
    } else if(fdct_func == simple_idct_mmx ) {
        for(i=0;i<64;i++)
            block[idct_simple_mmx_perm[i]] = block1[i];
    } else {
        for(i=0; i<64; i++)
            block[i]= block1[i];
    }

    ti = gettime();
    it1 = 0;
    do {
        for(it=0;it<NB_ITS_SPEED;it++) {
            for(i=0; i<64; i++)
                block[i]= block1[i];
//            memcpy(block, block1, sizeof(DCTELEM) * 64);
// dont memcpy especially not fastmemcpy because it does movntq !!!
            fdct_func(block);
        }
        it1 += NB_ITS_SPEED;
        ti1 = gettime() - ti;
    } while (ti1 < 1000000);
    emms();

    printf("%s %s: %0.1f kdct/s\n",
           is_idct ? "IDCT" : "DCT",
           name, (double)it1 * 1000.0 / (double)ti1);
#endif
}

void help(void)
{
    printf("dct-test [-i] [<test-number>]\n"
           "test-number 0 -> test with random matrixes\n"
           "            1 -> test with random sparse matrixes\n"
           "            2 -> do 3. test from mpeg4 std\n"
           "-i          test IDCT implementations\n");
    exit(1);
}

int main(int argc, char **argv)
{
    int test_idct = 0;
    int c,i;
    int test=1;

    init_fdct();
    idct_mmx_init();

    for(i=0;i<256;i++) cropTbl[i + MAX_NEG_CROP] = i;
    for(i=0;i<MAX_NEG_CROP;i++) {
        cropTbl[i] = 0;
        cropTbl[i + MAX_NEG_CROP + 256] = 255;
    }
    
    for(;;) {
        c = getopt(argc, argv, "ih");
        if (c == -1)
            break;
        switch(c) {
        case 'i':
            test_idct = 1;
            break;
        default :
        case 'h':
            help();
            break;
        }
    }
    
    if(optind <argc) test= atoi(argv[optind]);
               
    printf("ffmpeg DCT/IDCT test\n");

    if (!test_idct) {
        dct_error("REF-DBL", 0, fdct, fdct, test); /* only to verify code ! */
        dct_error("IJG-AAN-INT", 0, fdct_ifast, fdct, test);
        dct_error("IJG-LLM-INT", 0, ff_jpeg_fdct_islow, fdct, test);
        dct_error("MMX", 0, ff_fdct_mmx, fdct, test);
    } else {
        dct_error("REF-DBL", 1, idct, idct, test);
        dct_error("INT", 1, j_rev_dct, idct, test);
        dct_error("LIBMPEG2-MMX", 1, ff_mmx_idct, idct, test);
        dct_error("LIBMPEG2-MMXEXT", 1, ff_mmxext_idct, idct, test);
        dct_error("SIMPLE-C", 1, simple_idct, idct, test);
        dct_error("SIMPLE-MMX", 1, simple_idct_mmx, idct, test);
//        dct_error("ODIVX-C", 1, odivx_idct_c, idct);
//printf(" test against odivx idct\n");
//	dct_error("REF", 1, idct, odivx_idct_c);
//        dct_error("INT", 1, j_rev_dct, odivx_idct_c);
//        dct_error("MMX", 1, ff_mmx_idct, odivx_idct_c);
//        dct_error("MMXEXT", 1, ff_mmxext_idct, odivx_idct_c);
//        dct_error("SIMPLE-C", 1, simple_idct, odivx_idct_c);
//        dct_error("SIMPLE-MMX", 1, simple_idct_mmx, odivx_idct_c);
//        dct_error("ODIVX-C", 1, odivx_idct_c, odivx_idct_c);
    }
    return 0;
}
