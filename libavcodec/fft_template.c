/*
 * FFT/IFFT transforms
 * Copyright (c) 2008 Loren Merritt
 * Copyright (c) 2002 Fabrice Bellard
 * Partly based on libdjbfft by D. J. Bernstein
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
 * FFT/IFFT transforms.
 */

#include <stdlib.h>
#include <string.h>
#include "libavutil/mathematics.h"
#include "libavutil/thread.h"
#include "fft.h"
#include "fft-internal.h"

#if !FFT_FLOAT
#include "fft_table.h"
#else /* !FFT_FLOAT */

/* cos(2*pi*x/n) for 0<=x<=n/4, followed by its reverse */
#if !CONFIG_HARDCODED_TABLES
COSTABLE(16);
COSTABLE(32);
COSTABLE(64);
COSTABLE(128);
COSTABLE(256);
COSTABLE(512);
COSTABLE(1024);
COSTABLE(2048);
COSTABLE(4096);
COSTABLE(8192);
COSTABLE(16384);
COSTABLE(32768);
COSTABLE(65536);
COSTABLE(131072);

static av_cold void init_ff_cos_tabs(int index)
{
    int i;
    int m = 1<<index;
    double freq = 2*M_PI/m;
    FFTSample *tab = FFT_NAME(ff_cos_tabs)[index];
    for(i=0; i<=m/4; i++)
        tab[i] = FIX15(cos(i*freq));
    for(i=1; i<m/4; i++)
        tab[m/2-i] = tab[i];
}

typedef struct CosTabsInitOnce {
    void (*func)(void);
    AVOnce control;
} CosTabsInitOnce;

#define INIT_FF_COS_TABS_FUNC(index, size)          \
static av_cold void init_ff_cos_tabs_ ## size (void)\
{                                                   \
    init_ff_cos_tabs(index);                        \
}

INIT_FF_COS_TABS_FUNC(4, 16)
INIT_FF_COS_TABS_FUNC(5, 32)
INIT_FF_COS_TABS_FUNC(6, 64)
INIT_FF_COS_TABS_FUNC(7, 128)
INIT_FF_COS_TABS_FUNC(8, 256)
INIT_FF_COS_TABS_FUNC(9, 512)
INIT_FF_COS_TABS_FUNC(10, 1024)
INIT_FF_COS_TABS_FUNC(11, 2048)
INIT_FF_COS_TABS_FUNC(12, 4096)
INIT_FF_COS_TABS_FUNC(13, 8192)
INIT_FF_COS_TABS_FUNC(14, 16384)
INIT_FF_COS_TABS_FUNC(15, 32768)
INIT_FF_COS_TABS_FUNC(16, 65536)
INIT_FF_COS_TABS_FUNC(17, 131072)

static CosTabsInitOnce cos_tabs_init_once[] = {
    { NULL },
    { NULL },
    { NULL },
    { NULL },
    { init_ff_cos_tabs_16, AV_ONCE_INIT },
    { init_ff_cos_tabs_32, AV_ONCE_INIT },
    { init_ff_cos_tabs_64, AV_ONCE_INIT },
    { init_ff_cos_tabs_128, AV_ONCE_INIT },
    { init_ff_cos_tabs_256, AV_ONCE_INIT },
    { init_ff_cos_tabs_512, AV_ONCE_INIT },
    { init_ff_cos_tabs_1024, AV_ONCE_INIT },
    { init_ff_cos_tabs_2048, AV_ONCE_INIT },
    { init_ff_cos_tabs_4096, AV_ONCE_INIT },
    { init_ff_cos_tabs_8192, AV_ONCE_INIT },
    { init_ff_cos_tabs_16384, AV_ONCE_INIT },
    { init_ff_cos_tabs_32768, AV_ONCE_INIT },
    { init_ff_cos_tabs_65536, AV_ONCE_INIT },
    { init_ff_cos_tabs_131072, AV_ONCE_INIT },
};

av_cold void ff_init_ff_cos_tabs(int index)
{
    ff_thread_once(&cos_tabs_init_once[index].control, cos_tabs_init_once[index].func);
}
#endif
COSTABLE_CONST FFTSample * const FFT_NAME(ff_cos_tabs)[] = {
    NULL, NULL, NULL, NULL,
    FFT_NAME(ff_cos_16),
    FFT_NAME(ff_cos_32),
    FFT_NAME(ff_cos_64),
    FFT_NAME(ff_cos_128),
    FFT_NAME(ff_cos_256),
    FFT_NAME(ff_cos_512),
    FFT_NAME(ff_cos_1024),
    FFT_NAME(ff_cos_2048),
    FFT_NAME(ff_cos_4096),
    FFT_NAME(ff_cos_8192),
    FFT_NAME(ff_cos_16384),
    FFT_NAME(ff_cos_32768),
    FFT_NAME(ff_cos_65536),
    FFT_NAME(ff_cos_131072),
};

#endif /* FFT_FLOAT */

static void fft_permute_c(FFTContext *s, FFTComplex *z);
static void fft_calc_c(FFTContext *s, FFTComplex *z);

static int split_radix_permutation(int i, int n, int inverse)
{
    int m;
    if(n <= 2) return i&1;
    m = n >> 1;
    if(!(i&m))            return split_radix_permutation(i, m, inverse)*2;
    m >>= 1;
    if(inverse == !(i&m)) return split_radix_permutation(i, m, inverse)*4 + 1;
    else                  return split_radix_permutation(i, m, inverse)*4 - 1;
}


static const int avx_tab[] = {
    0, 4, 1, 5, 8, 12, 9, 13, 2, 6, 3, 7, 10, 14, 11, 15
};

static int is_second_half_of_fft32(int i, int n)
{
    if (n <= 32)
        return i >= 16;
    else if (i < n/2)
        return is_second_half_of_fft32(i, n/2);
    else if (i < 3*n/4)
        return is_second_half_of_fft32(i - n/2, n/4);
    else
        return is_second_half_of_fft32(i - 3*n/4, n/4);
}

static av_cold void fft_perm_avx(FFTContext *s)
{
    int i;
    int n = 1 << s->nbits;

    for (i = 0; i < n; i += 16) {
        int k;
        if (is_second_half_of_fft32(i, n)) {
            for (k = 0; k < 16; k++)
                s->revtab[-split_radix_permutation(i + k, n, s->inverse) & (n - 1)] =
                    i + avx_tab[k];

        } else {
            for (k = 0; k < 16; k++) {
                int j = i + k;
                j = (j & ~7) | ((j >> 1) & 3) | ((j << 2) & 4);
                s->revtab[-split_radix_permutation(i + k, n, s->inverse) & (n - 1)] = j;
            }
        }
    }
}

av_cold int ff_fft_init(FFTContext *s, int nbits, int inverse)
{
    int i, j, n;

    s->revtab = NULL;
    s->revtab32 = NULL;

    if (nbits < 2 || nbits > 17)
        goto fail;
    s->nbits = nbits;
    n = 1 << nbits;

    if (nbits <= 16) {
        s->revtab = av_malloc(n * sizeof(uint16_t));
        if (!s->revtab)
            goto fail;
    } else {
        s->revtab32 = av_malloc(n * sizeof(uint32_t));
        if (!s->revtab32)
            goto fail;
    }
    s->tmp_buf = av_malloc(n * sizeof(FFTComplex));
    if (!s->tmp_buf)
        goto fail;
    s->inverse = inverse;
    s->fft_permutation = FF_FFT_PERM_DEFAULT;

    s->fft_permute = fft_permute_c;
    s->fft_calc    = fft_calc_c;
#if CONFIG_MDCT
    s->imdct_calc  = ff_imdct_calc_c;
    s->imdct_half  = ff_imdct_half_c;
    s->mdct_calc   = ff_mdct_calc_c;
#endif

#if FFT_FLOAT
    if (ARCH_AARCH64) ff_fft_init_aarch64(s);
    if (ARCH_ARM)     ff_fft_init_arm(s);
    if (ARCH_PPC)     ff_fft_init_ppc(s);
    if (ARCH_X86)     ff_fft_init_x86(s);
    if (HAVE_MIPSFPU) ff_fft_init_mips(s);
    for(j=4; j<=nbits; j++) {
        ff_init_ff_cos_tabs(j);
    }
#else /* FFT_FLOAT */
    ff_fft_lut_init();
#endif


    if (ARCH_X86 && FFT_FLOAT && s->fft_permutation == FF_FFT_PERM_AVX) {
        fft_perm_avx(s);
    } else {
#define PROCESS_FFT_PERM_SWAP_LSBS(num) do {\
    for(i = 0; i < n; i++) {\
        int k;\
        j = i;\
        j = (j & ~3) | ((j >> 1) & 1) | ((j << 1) & 2);\
        k = -split_radix_permutation(i, n, s->inverse) & (n - 1);\
        s->revtab##num[k] = j;\
    } \
} while(0);

#define PROCESS_FFT_PERM_DEFAULT(num) do {\
    for(i = 0; i < n; i++) {\
        int k;\
        j = i;\
        k = -split_radix_permutation(i, n, s->inverse) & (n - 1);\
        s->revtab##num[k] = j;\
    } \
} while(0);

#define SPLIT_RADIX_PERMUTATION(num) do { \
    if (s->fft_permutation == FF_FFT_PERM_SWAP_LSBS) {\
        PROCESS_FFT_PERM_SWAP_LSBS(num) \
    } else {\
        PROCESS_FFT_PERM_DEFAULT(num) \
    }\
} while(0);

    if (s->revtab)
        SPLIT_RADIX_PERMUTATION()
    if (s->revtab32)
        SPLIT_RADIX_PERMUTATION(32)

#undef PROCESS_FFT_PERM_DEFAULT
#undef PROCESS_FFT_PERM_SWAP_LSBS
#undef SPLIT_RADIX_PERMUTATION
    }

    return 0;
 fail:
    av_freep(&s->revtab);
    av_freep(&s->revtab32);
    av_freep(&s->tmp_buf);
    return -1;
}

static void fft_permute_c(FFTContext *s, FFTComplex *z)
{
    int j, np;
    const uint16_t *revtab = s->revtab;
    const uint32_t *revtab32 = s->revtab32;
    np = 1 << s->nbits;
    /* TODO: handle split-radix permute in a more optimal way, probably in-place */
    if (revtab) {
        for(j=0;j<np;j++) s->tmp_buf[revtab[j]] = z[j];
    } else
        for(j=0;j<np;j++) s->tmp_buf[revtab32[j]] = z[j];

    memcpy(z, s->tmp_buf, np * sizeof(FFTComplex));
}

av_cold void ff_fft_end(FFTContext *s)
{
    av_freep(&s->revtab);
    av_freep(&s->revtab32);
    av_freep(&s->tmp_buf);
}

#if !FFT_FLOAT

static void fft_calc_c(FFTContext *s, FFTComplex *z) {

    int nbits, i, n, num_transforms, offset, step;
    int n4, n2, n34;
    unsigned tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp8;
    FFTComplex *tmpz;
    const int fft_size = (1 << s->nbits);
    int64_t accu;

    num_transforms = (0x2aab >> (16 - s->nbits)) | 1;

    for (n=0; n<num_transforms; n++){
        offset = ff_fft_offsets_lut[n] << 2;
        tmpz = z + offset;

        tmp1 = tmpz[0].re + (unsigned)tmpz[1].re;
        tmp5 = tmpz[2].re + (unsigned)tmpz[3].re;
        tmp2 = tmpz[0].im + (unsigned)tmpz[1].im;
        tmp6 = tmpz[2].im + (unsigned)tmpz[3].im;
        tmp3 = tmpz[0].re - (unsigned)tmpz[1].re;
        tmp8 = tmpz[2].im - (unsigned)tmpz[3].im;
        tmp4 = tmpz[0].im - (unsigned)tmpz[1].im;
        tmp7 = tmpz[2].re - (unsigned)tmpz[3].re;

        tmpz[0].re = tmp1 + tmp5;
        tmpz[2].re = tmp1 - tmp5;
        tmpz[0].im = tmp2 + tmp6;
        tmpz[2].im = tmp2 - tmp6;
        tmpz[1].re = tmp3 + tmp8;
        tmpz[3].re = tmp3 - tmp8;
        tmpz[1].im = tmp4 - tmp7;
        tmpz[3].im = tmp4 + tmp7;
    }

    if (fft_size < 8)
        return;

    num_transforms = (num_transforms >> 1) | 1;

    for (n=0; n<num_transforms; n++){
        offset = ff_fft_offsets_lut[n] << 3;
        tmpz = z + offset;

        tmp1 = tmpz[4].re + (unsigned)tmpz[5].re;
        tmp3 = tmpz[6].re + (unsigned)tmpz[7].re;
        tmp2 = tmpz[4].im + (unsigned)tmpz[5].im;
        tmp4 = tmpz[6].im + (unsigned)tmpz[7].im;
        tmp5 = tmp1 + tmp3;
        tmp7 = tmp1 - tmp3;
        tmp6 = tmp2 + tmp4;
        tmp8 = tmp2 - tmp4;

        tmp1 = tmpz[4].re - (unsigned)tmpz[5].re;
        tmp2 = tmpz[4].im - (unsigned)tmpz[5].im;
        tmp3 = tmpz[6].re - (unsigned)tmpz[7].re;
        tmp4 = tmpz[6].im - (unsigned)tmpz[7].im;

        tmpz[4].re = tmpz[0].re - tmp5;
        tmpz[0].re = tmpz[0].re + tmp5;
        tmpz[4].im = tmpz[0].im - tmp6;
        tmpz[0].im = tmpz[0].im + tmp6;
        tmpz[6].re = tmpz[2].re - tmp8;
        tmpz[2].re = tmpz[2].re + tmp8;
        tmpz[6].im = tmpz[2].im + tmp7;
        tmpz[2].im = tmpz[2].im - tmp7;

        accu = (int64_t)Q31(M_SQRT1_2)*(int)(tmp1 + tmp2);
        tmp5 = (int32_t)((accu + 0x40000000) >> 31);
        accu = (int64_t)Q31(M_SQRT1_2)*(int)(tmp3 - tmp4);
        tmp7 = (int32_t)((accu + 0x40000000) >> 31);
        accu = (int64_t)Q31(M_SQRT1_2)*(int)(tmp2 - tmp1);
        tmp6 = (int32_t)((accu + 0x40000000) >> 31);
        accu = (int64_t)Q31(M_SQRT1_2)*(int)(tmp3 + tmp4);
        tmp8 = (int32_t)((accu + 0x40000000) >> 31);
        tmp1 = tmp5 + tmp7;
        tmp3 = tmp5 - tmp7;
        tmp2 = tmp6 + tmp8;
        tmp4 = tmp6 - tmp8;

        tmpz[5].re = tmpz[1].re - tmp1;
        tmpz[1].re = tmpz[1].re + tmp1;
        tmpz[5].im = tmpz[1].im - tmp2;
        tmpz[1].im = tmpz[1].im + tmp2;
        tmpz[7].re = tmpz[3].re - tmp4;
        tmpz[3].re = tmpz[3].re + tmp4;
        tmpz[7].im = tmpz[3].im + tmp3;
        tmpz[3].im = tmpz[3].im - tmp3;
    }

    step = 1 << ((MAX_LOG2_NFFT-4) - 4);
    n4 = 4;

    for (nbits=4; nbits<=s->nbits; nbits++){
        n2  = 2*n4;
        n34 = 3*n4;
        num_transforms = (num_transforms >> 1) | 1;

        for (n=0; n<num_transforms; n++){
            const FFTSample *w_re_ptr = ff_w_tab_sr + step;
            const FFTSample *w_im_ptr = ff_w_tab_sr + MAX_FFT_SIZE/(4*16) - step;
            offset = ff_fft_offsets_lut[n] << nbits;
            tmpz = z + offset;

            tmp5 = tmpz[ n2].re + (unsigned)tmpz[n34].re;
            tmp1 = tmpz[ n2].re - (unsigned)tmpz[n34].re;
            tmp6 = tmpz[ n2].im + (unsigned)tmpz[n34].im;
            tmp2 = tmpz[ n2].im - (unsigned)tmpz[n34].im;

            tmpz[ n2].re = tmpz[ 0].re - tmp5;
            tmpz[  0].re = tmpz[ 0].re + tmp5;
            tmpz[ n2].im = tmpz[ 0].im - tmp6;
            tmpz[  0].im = tmpz[ 0].im + tmp6;
            tmpz[n34].re = tmpz[n4].re - tmp2;
            tmpz[ n4].re = tmpz[n4].re + tmp2;
            tmpz[n34].im = tmpz[n4].im + tmp1;
            tmpz[ n4].im = tmpz[n4].im - tmp1;

            for (i=1; i<n4; i++){
                FFTSample w_re = w_re_ptr[0];
                FFTSample w_im = w_im_ptr[0];
                accu  = (int64_t)w_re*tmpz[ n2+i].re;
                accu += (int64_t)w_im*tmpz[ n2+i].im;
                tmp1 = (int32_t)((accu + 0x40000000) >> 31);
                accu  = (int64_t)w_re*tmpz[ n2+i].im;
                accu -= (int64_t)w_im*tmpz[ n2+i].re;
                tmp2 = (int32_t)((accu + 0x40000000) >> 31);
                accu  = (int64_t)w_re*tmpz[n34+i].re;
                accu -= (int64_t)w_im*tmpz[n34+i].im;
                tmp3 = (int32_t)((accu + 0x40000000) >> 31);
                accu  = (int64_t)w_re*tmpz[n34+i].im;
                accu += (int64_t)w_im*tmpz[n34+i].re;
                tmp4 = (int32_t)((accu + 0x40000000) >> 31);

                tmp5 = tmp1 + tmp3;
                tmp1 = tmp1 - tmp3;
                tmp6 = tmp2 + tmp4;
                tmp2 = tmp2 - tmp4;

                tmpz[ n2+i].re = tmpz[   i].re - tmp5;
                tmpz[    i].re = tmpz[   i].re + tmp5;
                tmpz[ n2+i].im = tmpz[   i].im - tmp6;
                tmpz[    i].im = tmpz[   i].im + tmp6;
                tmpz[n34+i].re = tmpz[n4+i].re - tmp2;
                tmpz[ n4+i].re = tmpz[n4+i].re + tmp2;
                tmpz[n34+i].im = tmpz[n4+i].im + tmp1;
                tmpz[ n4+i].im = tmpz[n4+i].im - tmp1;

                w_re_ptr += step;
                w_im_ptr -= step;
            }
        }
        step >>= 1;
        n4   <<= 1;
    }
}

#else /* !FFT_FLOAT */

#define BUTTERFLIES(a0,a1,a2,a3) {\
    BF(t3, t5, t5, t1);\
    BF(a2.re, a0.re, a0.re, t5);\
    BF(a3.im, a1.im, a1.im, t3);\
    BF(t4, t6, t2, t6);\
    BF(a3.re, a1.re, a1.re, t4);\
    BF(a2.im, a0.im, a0.im, t6);\
}

// force loading all the inputs before storing any.
// this is slightly slower for small data, but avoids store->load aliasing
// for addresses separated by large powers of 2.
#define BUTTERFLIES_BIG(a0,a1,a2,a3) {\
    FFTSample r0=a0.re, i0=a0.im, r1=a1.re, i1=a1.im;\
    BF(t3, t5, t5, t1);\
    BF(a2.re, a0.re, r0, t5);\
    BF(a3.im, a1.im, i1, t3);\
    BF(t4, t6, t2, t6);\
    BF(a3.re, a1.re, r1, t4);\
    BF(a2.im, a0.im, i0, t6);\
}

#define TRANSFORM(a0,a1,a2,a3,wre,wim) {\
    CMUL(t1, t2, a2.re, a2.im, wre, -wim);\
    CMUL(t5, t6, a3.re, a3.im, wre,  wim);\
    BUTTERFLIES(a0,a1,a2,a3)\
}

#define TRANSFORM_ZERO(a0,a1,a2,a3) {\
    t1 = a2.re;\
    t2 = a2.im;\
    t5 = a3.re;\
    t6 = a3.im;\
    BUTTERFLIES(a0,a1,a2,a3)\
}

/* z[0...8n-1], w[1...2n-1] */
#define PASS(name)\
static void name(FFTComplex *z, const FFTSample *wre, unsigned int n)\
{\
    FFTDouble t1, t2, t3, t4, t5, t6;\
    int o1 = 2*n;\
    int o2 = 4*n;\
    int o3 = 6*n;\
    const FFTSample *wim = wre+o1;\
    n--;\
\
    TRANSFORM_ZERO(z[0],z[o1],z[o2],z[o3]);\
    TRANSFORM(z[1],z[o1+1],z[o2+1],z[o3+1],wre[1],wim[-1]);\
    do {\
        z += 2;\
        wre += 2;\
        wim -= 2;\
        TRANSFORM(z[0],z[o1],z[o2],z[o3],wre[0],wim[0]);\
        TRANSFORM(z[1],z[o1+1],z[o2+1],z[o3+1],wre[1],wim[-1]);\
    } while(--n);\
}

PASS(pass)
#if !CONFIG_SMALL
#undef BUTTERFLIES
#define BUTTERFLIES BUTTERFLIES_BIG
PASS(pass_big)
#endif

#define DECL_FFT(n,n2,n4)\
static void fft##n(FFTComplex *z)\
{\
    fft##n2(z);\
    fft##n4(z+n4*2);\
    fft##n4(z+n4*3);\
    pass(z,FFT_NAME(ff_cos_##n),n4/2);\
}

static void fft4(FFTComplex *z)
{
    FFTDouble t1, t2, t3, t4, t5, t6, t7, t8;

    BF(t3, t1, z[0].re, z[1].re);
    BF(t8, t6, z[3].re, z[2].re);
    BF(z[2].re, z[0].re, t1, t6);
    BF(t4, t2, z[0].im, z[1].im);
    BF(t7, t5, z[2].im, z[3].im);
    BF(z[3].im, z[1].im, t4, t8);
    BF(z[3].re, z[1].re, t3, t7);
    BF(z[2].im, z[0].im, t2, t5);
}

static void fft8(FFTComplex *z)
{
    FFTDouble t1, t2, t3, t4, t5, t6;

    fft4(z);

    BF(t1, z[5].re, z[4].re, -z[5].re);
    BF(t2, z[5].im, z[4].im, -z[5].im);
    BF(t5, z[7].re, z[6].re, -z[7].re);
    BF(t6, z[7].im, z[6].im, -z[7].im);

    BUTTERFLIES(z[0],z[2],z[4],z[6]);
    TRANSFORM(z[1],z[3],z[5],z[7],sqrthalf,sqrthalf);
}

#if !CONFIG_SMALL
static void fft16(FFTComplex *z)
{
    FFTDouble t1, t2, t3, t4, t5, t6;
    FFTSample cos_16_1 = FFT_NAME(ff_cos_16)[1];
    FFTSample cos_16_3 = FFT_NAME(ff_cos_16)[3];

    fft8(z);
    fft4(z+8);
    fft4(z+12);

    TRANSFORM_ZERO(z[0],z[4],z[8],z[12]);
    TRANSFORM(z[2],z[6],z[10],z[14],sqrthalf,sqrthalf);
    TRANSFORM(z[1],z[5],z[9],z[13],cos_16_1,cos_16_3);
    TRANSFORM(z[3],z[7],z[11],z[15],cos_16_3,cos_16_1);
}
#else
DECL_FFT(16,8,4)
#endif
DECL_FFT(32,16,8)
DECL_FFT(64,32,16)
DECL_FFT(128,64,32)
DECL_FFT(256,128,64)
DECL_FFT(512,256,128)
#if !CONFIG_SMALL
#define pass pass_big
#endif
DECL_FFT(1024,512,256)
DECL_FFT(2048,1024,512)
DECL_FFT(4096,2048,1024)
DECL_FFT(8192,4096,2048)
DECL_FFT(16384,8192,4096)
DECL_FFT(32768,16384,8192)
DECL_FFT(65536,32768,16384)
DECL_FFT(131072,65536,32768)

static void (* const fft_dispatch[])(FFTComplex*) = {
    fft4, fft8, fft16, fft32, fft64, fft128, fft256, fft512, fft1024,
    fft2048, fft4096, fft8192, fft16384, fft32768, fft65536, fft131072
};

static void fft_calc_c(FFTContext *s, FFTComplex *z)
{
    fft_dispatch[s->nbits-2](z);
}
#endif /* !FFT_FLOAT */
