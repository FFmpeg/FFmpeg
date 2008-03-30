/*
 * (c) 2001 Fabrice Bellard
 *     2007 Marc Hoffman <marc.hoffman@analog.com>
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
 * @file dct-test.c
 * DCT test. (c) 2001 Fabrice Bellard.
 * Started from sample code by Juan J. Sierralta P.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <math.h>

#include "dsputil.h"

#include "simple_idct.h"
#include "faandct.h"
#include "faanidct.h"

#ifndef MAX
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))
#endif

#undef printf
#undef random

void *fast_memcpy(void *a, const void *b, size_t c){return memcpy(a,b,c);};

/* reference fdct/idct */
extern void fdct(DCTELEM *block);
extern void idct(DCTELEM *block);
extern void ff_idct_xvid_mmx(DCTELEM *block);
extern void ff_idct_xvid_mmx2(DCTELEM *block);
extern void init_fdct();

extern void ff_mmx_idct(DCTELEM *data);
extern void ff_mmxext_idct(DCTELEM *data);

extern void odivx_idct_c (short *block);

// BFIN
extern void ff_bfin_idct (DCTELEM *block)  ;
extern void ff_bfin_fdct (DCTELEM *block) ;

// ALTIVEC
extern void fdct_altivec (DCTELEM *block);
//extern void idct_altivec (DCTELEM *block);?? no routine


struct algo {
  char *name;
  enum { FDCT, IDCT } is_idct;
  void (* func) (DCTELEM *block);
  void (* ref)  (DCTELEM *block);
  enum formattag { NO_PERM,MMX_PERM, MMX_SIMPLE_PERM, SCALE_PERM } format;
  int  mm_support;
};

#ifndef FAAN_POSTSCALE
#define FAAN_SCALE SCALE_PERM
#else
#define FAAN_SCALE NO_PERM
#endif

struct algo algos[] = {
  {"REF-DBL",         0, fdct,               fdct, NO_PERM},
  {"FAAN",            0, ff_faandct,         fdct, FAAN_SCALE},
  {"FAANI",           1, ff_faanidct,        idct, NO_PERM},
  {"IJG-AAN-INT",     0, fdct_ifast,         fdct, SCALE_PERM},
  {"IJG-LLM-INT",     0, ff_jpeg_fdct_islow, fdct, NO_PERM},
  {"REF-DBL",         1, idct,               idct, NO_PERM},
  {"INT",             1, j_rev_dct,          idct, MMX_PERM},
  {"SIMPLE-C",        1, ff_simple_idct,     idct, NO_PERM},

#ifdef HAVE_MMX
  {"MMX",             0, ff_fdct_mmx,        fdct, NO_PERM, MM_MMX},
#ifdef HAVE_MMX2
  {"MMX2",            0, ff_fdct_mmx2,       fdct, NO_PERM, MM_MMXEXT},
#endif

#ifdef CONFIG_GPL
  {"LIBMPEG2-MMX",    1, ff_mmx_idct,        idct, MMX_PERM, MM_MMX},
  {"LIBMPEG2-MMXEXT", 1, ff_mmxext_idct,     idct, MMX_PERM, MM_MMXEXT},
#endif
  {"SIMPLE-MMX",      1, ff_simple_idct_mmx, idct, MMX_SIMPLE_PERM, MM_MMX},
  {"XVID-MMX",        1, ff_idct_xvid_mmx,   idct, NO_PERM, MM_MMX},
  {"XVID-MMX2",       1, ff_idct_xvid_mmx2,  idct, NO_PERM, MM_MMXEXT},
#endif

#ifdef HAVE_ALTIVEC
  {"altivecfdct",     0, fdct_altivec,       fdct, NO_PERM, MM_ALTIVEC},
#endif

#ifdef ARCH_BFIN
  {"BFINfdct",        0, ff_bfin_fdct,       fdct, NO_PERM},
  {"BFINidct",        1, ff_bfin_idct,       idct, NO_PERM},
#endif

  { 0 }
};

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

uint8_t cropTbl[256 + 2 * MAX_NEG_CROP];

int64_t gettime(void)
{
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
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
//        idct_simple_mmx_perm[i] = simple_block_permute_op(i);
    }
}

static DCTELEM block[64] __attribute__ ((aligned (16)));
static DCTELEM block1[64] __attribute__ ((aligned (8)));
static DCTELEM block_org[64] __attribute__ ((aligned (8)));

void dct_error(const char *name, int is_idct,
               void (*fdct_func)(DCTELEM *block),
               void (*fdct_ref)(DCTELEM *block), int form, int test)
{
    int it, i, scale;
    int err_inf, v;
    int64_t err2, ti, ti1, it1;
    int64_t sysErr[64], sysErrMax=0;
    int maxout=0;
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

        if (form == MMX_PERM) {
            for(i=0;i<64;i++)
                block[idct_mmx_perm[i]] = block1[i];
            } else if (form == MMX_SIMPLE_PERM) {
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
        emms_c(); /* for ff_mmx_idct */

        if (form == SCALE_PERM) {
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
    for(i=0; i<64; i++) sysErrMax= MAX(sysErrMax, FFABS(sysErr[i]));

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

    if (form == MMX_PERM) {
        for(i=0;i<64;i++)
            block[idct_mmx_perm[i]] = block1[i];
    } else if(form == MMX_SIMPLE_PERM) {
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
// do not memcpy especially not fastmemcpy because it does movntq !!!
            fdct_func(block);
        }
        it1 += NB_ITS_SPEED;
        ti1 = gettime() - ti;
    } while (ti1 < 1000000);
    emms_c();

    printf("%s %s: %0.1f kdct/s\n",
           is_idct ? "IDCT" : "DCT",
           name, (double)it1 * 1000.0 / (double)ti1);
#endif
}

static uint8_t img_dest[64] __attribute__ ((aligned (8)));
static uint8_t img_dest1[64] __attribute__ ((aligned (8)));

void idct248_ref(uint8_t *dest, int linesize, int16_t *block)
{
    static int init;
    static double c8[8][8];
    static double c4[4][4];
    double block1[64], block2[64], block3[64];
    double s, sum, v;
    int i, j, k;

    if (!init) {
        init = 1;

        for(i=0;i<8;i++) {
            sum = 0;
            for(j=0;j<8;j++) {
                s = (i==0) ? sqrt(1.0/8.0) : sqrt(1.0/4.0);
                c8[i][j] = s * cos(M_PI * i * (j + 0.5) / 8.0);
                sum += c8[i][j] * c8[i][j];
            }
        }

        for(i=0;i<4;i++) {
            sum = 0;
            for(j=0;j<4;j++) {
                s = (i==0) ? sqrt(1.0/4.0) : sqrt(1.0/2.0);
                c4[i][j] = s * cos(M_PI * i * (j + 0.5) / 4.0);
                sum += c4[i][j] * c4[i][j];
            }
        }
    }

    /* butterfly */
    s = 0.5 * sqrt(2.0);
    for(i=0;i<4;i++) {
        for(j=0;j<8;j++) {
            block1[8*(2*i)+j] = (block[8*(2*i)+j] + block[8*(2*i+1)+j]) * s;
            block1[8*(2*i+1)+j] = (block[8*(2*i)+j] - block[8*(2*i+1)+j]) * s;
        }
    }

    /* idct8 on lines */
    for(i=0;i<8;i++) {
        for(j=0;j<8;j++) {
            sum = 0;
            for(k=0;k<8;k++)
                sum += c8[k][j] * block1[8*i+k];
            block2[8*i+j] = sum;
        }
    }

    /* idct4 */
    for(i=0;i<8;i++) {
        for(j=0;j<4;j++) {
            /* top */
            sum = 0;
            for(k=0;k<4;k++)
                sum += c4[k][j] * block2[8*(2*k)+i];
            block3[8*(2*j)+i] = sum;

            /* bottom */
            sum = 0;
            for(k=0;k<4;k++)
                sum += c4[k][j] * block2[8*(2*k+1)+i];
            block3[8*(2*j+1)+i] = sum;
        }
    }

    /* clamp and store the result */
    for(i=0;i<8;i++) {
        for(j=0;j<8;j++) {
            v = block3[8*i+j];
            if (v < 0)
                v = 0;
            else if (v > 255)
                v = 255;
            dest[i * linesize + j] = (int)rint(v);
        }
    }
}

void idct248_error(const char *name,
                    void (*idct248_put)(uint8_t *dest, int line_size, int16_t *block))
{
    int it, i, it1, ti, ti1, err_max, v;

    srandom(0);

    /* just one test to see if code is correct (precision is less
       important here) */
    err_max = 0;
    for(it=0;it<NB_ITS;it++) {

        /* XXX: use forward transform to generate values */
        for(i=0;i<64;i++)
            block1[i] = (random() % 256) - 128;
        block1[0] += 1024;

        for(i=0; i<64; i++)
            block[i]= block1[i];
        idct248_ref(img_dest1, 8, block);

        for(i=0; i<64; i++)
            block[i]= block1[i];
        idct248_put(img_dest, 8, block);

        for(i=0;i<64;i++) {
            v = abs((int)img_dest[i] - (int)img_dest1[i]);
            if (v == 255)
                printf("%d %d\n", img_dest[i], img_dest1[i]);
            if (v > err_max)
                err_max = v;
        }
#if 0
        printf("ref=\n");
        for(i=0;i<8;i++) {
            int j;
            for(j=0;j<8;j++) {
                printf(" %3d", img_dest1[i*8+j]);
            }
            printf("\n");
        }

        printf("out=\n");
        for(i=0;i<8;i++) {
            int j;
            for(j=0;j<8;j++) {
                printf(" %3d", img_dest[i*8+j]);
            }
            printf("\n");
        }
#endif
    }
    printf("%s %s: err_inf=%d\n",
           1 ? "IDCT248" : "DCT248",
           name, err_max);

    ti = gettime();
    it1 = 0;
    do {
        for(it=0;it<NB_ITS_SPEED;it++) {
            for(i=0; i<64; i++)
                block[i]= block1[i];
//            memcpy(block, block1, sizeof(DCTELEM) * 64);
// do not memcpy especially not fastmemcpy because it does movntq !!!
            idct248_put(img_dest, 8, block);
        }
        it1 += NB_ITS_SPEED;
        ti1 = gettime() - ti;
    } while (ti1 < 1000000);
    emms_c();

    printf("%s %s: %0.1f kdct/s\n",
           1 ? "IDCT248" : "DCT248",
           name, (double)it1 * 1000.0 / (double)ti1);
}

void help(void)
{
    printf("dct-test [-i] [<test-number>]\n"
           "test-number 0 -> test with random matrixes\n"
           "            1 -> test with random sparse matrixes\n"
           "            2 -> do 3. test from mpeg4 std\n"
           "-i          test IDCT implementations\n"
           "-4          test IDCT248 implementations\n");
}

int main(int argc, char **argv)
{
    int test_idct = 0, test_248_dct = 0;
    int c,i;
    int test=1;

    init_fdct();
    idct_mmx_init();
    mm_flags = mm_support();

    for(i=0;i<256;i++) cropTbl[i + MAX_NEG_CROP] = i;
    for(i=0;i<MAX_NEG_CROP;i++) {
        cropTbl[i] = 0;
        cropTbl[i + MAX_NEG_CROP + 256] = 255;
    }

    for(;;) {
        c = getopt(argc, argv, "ih4");
        if (c == -1)
            break;
        switch(c) {
        case 'i':
            test_idct = 1;
            break;
        case '4':
            test_248_dct = 1;
            break;
        default :
        case 'h':
            help();
            return 0;
        }
    }

    if(optind <argc) test= atoi(argv[optind]);

    printf("ffmpeg DCT/IDCT test\n");

    if (test_248_dct) {
        idct248_error("SIMPLE-C", ff_simple_idct248_put);
    } else {
      for (i=0;algos[i].name;i++)
        if (algos[i].is_idct == test_idct && !(~mm_flags & algos[i].mm_support)) {
          dct_error (algos[i].name, algos[i].is_idct, algos[i].func, algos[i].ref, algos[i].format, test);
        }
    }
    return 0;
}
