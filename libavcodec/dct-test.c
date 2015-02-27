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
 * @file
 * DCT test (c) 2001 Fabrice Bellard
 * Started from sample code by Juan J. Sierralta P.
 */

#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <math.h>

#include "libavutil/cpu.h"
#include "libavutil/common.h"
#include "libavutil/lfg.h"
#include "libavutil/time.h"

#include "dct.h"
#include "idctdsp.h"
#include "simple_idct.h"
#include "xvididct.h"
#include "aandcttab.h"
#include "faandct.h"
#include "faanidct.h"
#include "dctref.h"

struct algo {
    const char *name;
    void (*func)(int16_t *block);
    enum idct_permutation_type perm_type;
    int cpu_flag;
    int nonspec;
};

static const struct algo fdct_tab[] = {
    { "REF-DBL",     ff_ref_fdct,          FF_IDCT_PERM_NONE },
    { "IJG-AAN-INT", ff_fdct_ifast,        FF_IDCT_PERM_NONE },
    { "IJG-LLM-INT", ff_jpeg_fdct_islow_8, FF_IDCT_PERM_NONE },
#if CONFIG_FAANDCT
    { "FAAN",        ff_faandct,           FF_IDCT_PERM_NONE },
#endif /* CONFIG_FAANDCT */
};

static void ff_prores_idct_wrap(int16_t *dst){
    DECLARE_ALIGNED(16, static int16_t, qmat)[64];
    int i;

    for(i=0; i<64; i++){
        qmat[i]=4;
    }
    ff_prores_idct(dst, qmat);
    for(i=0; i<64; i++) {
         dst[i] -= 512;
    }
}

static const struct algo idct_tab[] = {
    { "REF-DBL",     ff_ref_idct,          FF_IDCT_PERM_NONE },
    { "INT",         ff_j_rev_dct,         FF_IDCT_PERM_LIBMPEG2 },
    { "SIMPLE-C",    ff_simple_idct_8,     FF_IDCT_PERM_NONE },
    { "PR-C",        ff_prores_idct_wrap,  FF_IDCT_PERM_NONE, 0, 1 },
#if CONFIG_FAANIDCT
    { "FAANI",       ff_faanidct,          FF_IDCT_PERM_NONE },
#endif /* CONFIG_FAANIDCT */
#if CONFIG_MPEG4_DECODER
    { "XVID",        ff_xvid_idct,         FF_IDCT_PERM_NONE, 0, 1 },
#endif /* CONFIG_MPEG4_DECODER */
};

#if ARCH_ARM
#include "arm/dct-test.c"
#elif ARCH_PPC
#include "ppc/dct-test.c"
#elif ARCH_X86
#include "x86/dct-test.c"
#else
static const struct algo fdct_tab_arch[] = { { 0 } };
static const struct algo idct_tab_arch[] = { { 0 } };
#endif

#define AANSCALE_BITS 12

#define NB_ITS 20000
#define NB_ITS_SPEED 50000

DECLARE_ALIGNED(16, static int16_t, block)[64];
DECLARE_ALIGNED(8,  static int16_t, block1)[64];

static void init_block(int16_t block[64], int test, int is_idct, AVLFG *prng, int vals)
{
    int i, j;

    memset(block, 0, 64 * sizeof(*block));

    switch (test) {
    case 0:
        for (i = 0; i < 64; i++)
            block[i] = (av_lfg_get(prng) % (2*vals)) -vals;
        if (is_idct) {
            ff_ref_fdct(block);
            for (i = 0; i < 64; i++)
                block[i] >>= 3;
        }
        break;
    case 1:
        j = av_lfg_get(prng) % 10 + 1;
        for (i = 0; i < j; i++) {
            int idx = av_lfg_get(prng) % 64;
            block[idx] = av_lfg_get(prng) % (2*vals) -vals;
        }
        break;
    case 2:
        block[ 0] = av_lfg_get(prng) % (16*vals) - (8*vals);
        block[63] = (block[0] & 1) ^ 1;
        break;
    }
}

static void permute(int16_t dst[64], const int16_t src[64],
                    enum idct_permutation_type perm_type)
{
    int i;

#if ARCH_X86
    if (permute_x86(dst, src, perm_type))
        return;
#endif

    switch (perm_type) {
    case FF_IDCT_PERM_LIBMPEG2:
        for (i = 0; i < 64; i++)
            dst[(i & 0x38) | ((i & 6) >> 1) | ((i & 1) << 2)] = src[i];
        break;
    case FF_IDCT_PERM_PARTTRANS:
        for (i = 0; i < 64; i++)
            dst[(i & 0x24) | ((i & 3) << 3) | ((i >> 3) & 3)] = src[i];
        break;
    case FF_IDCT_PERM_TRANSPOSE:
        for (i = 0; i < 64; i++)
            dst[(i>>3) | ((i<<3)&0x38)] = src[i];
        break;
    default:
        for (i = 0; i < 64; i++)
            dst[i] = src[i];
        break;
    }
}

static int dct_error(const struct algo *dct, int test, int is_idct, int speed, const int bits)
{
    void (*ref)(int16_t *block) = is_idct ? ff_ref_idct : ff_ref_fdct;
    int it, i, scale;
    int err_inf, v;
    int64_t err2, ti, ti1, it1, err_sum = 0;
    int64_t sysErr[64], sysErrMax = 0;
    int maxout = 0;
    int blockSumErrMax = 0, blockSumErr;
    AVLFG prng;
    const int vals=1<<bits;
    double omse, ome;
    int spec_err;

    av_lfg_init(&prng, 1);

    err_inf = 0;
    err2 = 0;
    for (i = 0; i < 64; i++)
        sysErr[i] = 0;
    for (it = 0; it < NB_ITS; it++) {
        init_block(block1, test, is_idct, &prng, vals);
        permute(block, block1, dct->perm_type);

        dct->func(block);
        emms_c();

        if (!strcmp(dct->name, "IJG-AAN-INT")) {
            for (i = 0; i < 64; i++) {
                scale = 8 * (1 << (AANSCALE_BITS + 11)) / ff_aanscales[i];
                block[i] = (block[i] * scale) >> AANSCALE_BITS;
            }
        }

        ref(block1);
        if (!strcmp(dct->name, "PR-SSE2"))
            for (i = 0; i < 64; i++)
                block1[i] = av_clip(block1[i], 4-512, 1019-512);

        blockSumErr = 0;
        for (i = 0; i < 64; i++) {
            int err = block[i] - block1[i];
            err_sum += err;
            v = abs(err);
            if (v > err_inf)
                err_inf = v;
            err2 += v * v;
            sysErr[i] += block[i] - block1[i];
            blockSumErr += v;
            if (abs(block[i]) > maxout)
                maxout = abs(block[i]);
        }
        if (blockSumErrMax < blockSumErr)
            blockSumErrMax = blockSumErr;
    }
    for (i = 0; i < 64; i++)
        sysErrMax = FFMAX(sysErrMax, FFABS(sysErr[i]));

    for (i = 0; i < 64; i++) {
        if (i % 8 == 0)
            printf("\n");
        printf("%7d ", (int) sysErr[i]);
    }
    printf("\n");

    omse = (double) err2 / NB_ITS / 64;
    ome  = (double) err_sum / NB_ITS / 64;

    spec_err = is_idct && (err_inf > 1 || omse > 0.02 || fabs(ome) > 0.0015);

    printf("%s %s: max_err=%d omse=%0.8f ome=%0.8f syserr=%0.8f maxout=%d blockSumErr=%d\n",
           is_idct ? "IDCT" : "DCT", dct->name, err_inf,
           omse, ome, (double) sysErrMax / NB_ITS,
           maxout, blockSumErrMax);

    if (spec_err && !dct->nonspec)
        return 1;

    if (!speed)
        return 0;

    /* speed test */

    init_block(block, test, is_idct, &prng, vals);
    permute(block1, block, dct->perm_type);

    ti = av_gettime_relative();
    it1 = 0;
    do {
        for (it = 0; it < NB_ITS_SPEED; it++) {
            memcpy(block, block1, sizeof(block));
            dct->func(block);
        }
        emms_c();
        it1 += NB_ITS_SPEED;
        ti1 = av_gettime_relative() - ti;
    } while (ti1 < 1000000);

    printf("%s %s: %0.1f kdct/s\n", is_idct ? "IDCT" : "DCT", dct->name,
           (double) it1 * 1000.0 / (double) ti1);

    return 0;
}

DECLARE_ALIGNED(8, static uint8_t, img_dest)[64];
DECLARE_ALIGNED(8, static uint8_t, img_dest1)[64];

static void idct248_ref(uint8_t *dest, int linesize, int16_t *block)
{
    static int init;
    static double c8[8][8];
    static double c4[4][4];
    double block1[64], block2[64], block3[64];
    double s, sum, v;
    int i, j, k;

    if (!init) {
        init = 1;

        for (i = 0; i < 8; i++) {
            sum = 0;
            for (j = 0; j < 8; j++) {
                s = (i == 0) ? sqrt(1.0 / 8.0) : sqrt(1.0 / 4.0);
                c8[i][j] = s * cos(M_PI * i * (j + 0.5) / 8.0);
                sum += c8[i][j] * c8[i][j];
            }
        }

        for (i = 0; i < 4; i++) {
            sum = 0;
            for (j = 0; j < 4; j++) {
                s = (i == 0) ? sqrt(1.0 / 4.0) : sqrt(1.0 / 2.0);
                c4[i][j] = s * cos(M_PI * i * (j + 0.5) / 4.0);
                sum += c4[i][j] * c4[i][j];
            }
        }
    }

    /* butterfly */
    s = 0.5 * sqrt(2.0);
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 8; j++) {
            block1[8 * (2 * i) + j] =
                (block[8 * (2 * i) + j] + block[8 * (2 * i + 1) + j]) * s;
            block1[8 * (2 * i + 1) + j] =
                (block[8 * (2 * i) + j] - block[8 * (2 * i + 1) + j]) * s;
        }
    }

    /* idct8 on lines */
    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++) {
            sum = 0;
            for (k = 0; k < 8; k++)
                sum += c8[k][j] * block1[8 * i + k];
            block2[8 * i + j] = sum;
        }
    }

    /* idct4 */
    for (i = 0; i < 8; i++) {
        for (j = 0; j < 4; j++) {
            /* top */
            sum = 0;
            for (k = 0; k < 4; k++)
                sum += c4[k][j] * block2[8 * (2 * k) + i];
            block3[8 * (2 * j) + i] = sum;

            /* bottom */
            sum = 0;
            for (k = 0; k < 4; k++)
                sum += c4[k][j] * block2[8 * (2 * k + 1) + i];
            block3[8 * (2 * j + 1) + i] = sum;
        }
    }

    /* clamp and store the result */
    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++) {
            v = block3[8 * i + j];
            if      (v < 0)   v = 0;
            else if (v > 255) v = 255;
            dest[i * linesize + j] = (int) rint(v);
        }
    }
}

static void idct248_error(const char *name,
                          void (*idct248_put)(uint8_t *dest, int line_size,
                                              int16_t *block),
                          int speed)
{
    int it, i, it1, ti, ti1, err_max, v;
    AVLFG prng;

    av_lfg_init(&prng, 1);

    /* just one test to see if code is correct (precision is less
       important here) */
    err_max = 0;
    for (it = 0; it < NB_ITS; it++) {
        /* XXX: use forward transform to generate values */
        for (i = 0; i < 64; i++)
            block1[i] = av_lfg_get(&prng) % 256 - 128;
        block1[0] += 1024;

        for (i = 0; i < 64; i++)
            block[i] = block1[i];
        idct248_ref(img_dest1, 8, block);

        for (i = 0; i < 64; i++)
            block[i] = block1[i];
        idct248_put(img_dest, 8, block);

        for (i = 0; i < 64; i++) {
            v = abs((int) img_dest[i] - (int) img_dest1[i]);
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
    printf("%s %s: err_inf=%d\n", 1 ? "IDCT248" : "DCT248", name, err_max);

    if (!speed)
        return;

    ti = av_gettime_relative();
    it1 = 0;
    do {
        for (it = 0; it < NB_ITS_SPEED; it++) {
            for (i = 0; i < 64; i++)
                block[i] = block1[i];
            idct248_put(img_dest, 8, block);
        }
        emms_c();
        it1 += NB_ITS_SPEED;
        ti1 = av_gettime_relative() - ti;
    } while (ti1 < 1000000);

    printf("%s %s: %0.1f kdct/s\n", 1 ? "IDCT248" : "DCT248", name,
           (double) it1 * 1000.0 / (double) ti1);
}

static void help(void)
{
    printf("dct-test [-i] [<test-number>] [<bits>]\n"
           "test-number 0 -> test with random matrixes\n"
           "            1 -> test with random sparse matrixes\n"
           "            2 -> do 3. test from mpeg4 std\n"
           "bits        Number of time domain bits to use, 8 is default\n"
           "-i          test IDCT implementations\n"
           "-4          test IDCT248 implementations\n"
           "-t          speed test\n");
}

#if !HAVE_GETOPT
#include "compat/getopt.c"
#endif

int main(int argc, char **argv)
{
    int test_idct = 0, test_248_dct = 0;
    int c, i;
    int test = 1;
    int speed = 0;
    int err = 0;
    int bits=8;

    ff_ref_dct_init();

    for (;;) {
        c = getopt(argc, argv, "ih4t");
        if (c == -1)
            break;
        switch (c) {
        case 'i':
            test_idct = 1;
            break;
        case '4':
            test_248_dct = 1;
            break;
        case 't':
            speed = 1;
            break;
        default:
        case 'h':
            help();
            return 0;
        }
    }

    if (optind < argc)
        test = atoi(argv[optind]);
    if(optind+1 < argc) bits= atoi(argv[optind+1]);

    printf("ffmpeg DCT/IDCT test\n");

    if (test_248_dct) {
        idct248_error("SIMPLE-C", ff_simple_idct248_put, speed);
    } else {
        const int cpu_flags = av_get_cpu_flags();
        if (test_idct) {
            for (i = 0; i < FF_ARRAY_ELEMS(idct_tab); i++)
                err |= dct_error(&idct_tab[i], test, test_idct, speed, bits);

            for (i = 0; idct_tab_arch[i].name; i++)
                if (!(~cpu_flags & idct_tab_arch[i].cpu_flag))
                    err |= dct_error(&idct_tab_arch[i], test, test_idct, speed, bits);
        }
#if CONFIG_FDCTDSP
        else {
            for (i = 0; i < FF_ARRAY_ELEMS(fdct_tab); i++)
                err |= dct_error(&fdct_tab[i], test, test_idct, speed, bits);

            for (i = 0; fdct_tab_arch[i].name; i++)
                if (!(~cpu_flags & fdct_tab_arch[i].cpu_flag))
                    err |= dct_error(&fdct_tab_arch[i], test, test_idct, speed, bits);
        }
#endif /* CONFIG_FDCTDSP */
    }

    if (err)
        printf("Error: %d.\n", err);

    return !!err;
}
