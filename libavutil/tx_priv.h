/*
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

#ifndef AVUTIL_TX_PRIV_H
#define AVUTIL_TX_PRIV_H

#include "tx.h"
#include <stddef.h>
#include "thread.h"
#include "mem.h"
#include "avassert.h"
#include "attributes.h"

#ifdef TX_FLOAT
#define TX_NAME(x) x ## _float
#define SCALE_TYPE float
typedef float FFTSample;
typedef AVComplexFloat FFTComplex;
#elif defined(TX_DOUBLE)
#define TX_NAME(x) x ## _double
#define SCALE_TYPE double
typedef double FFTSample;
typedef AVComplexDouble FFTComplex;
#elif defined(TX_INT32)
#define TX_NAME(x) x ## _int32
#define SCALE_TYPE float
typedef int32_t FFTSample;
typedef AVComplexInt32 FFTComplex;
#else
typedef void FFTComplex;
#endif

#if defined(TX_FLOAT) || defined(TX_DOUBLE)

#define CMUL(dre, dim, are, aim, bre, bim) do {                                \
        (dre) = (are) * (bre) - (aim) * (bim);                                 \
        (dim) = (are) * (bim) + (aim) * (bre);                                 \
    } while (0)

#define SMUL(dre, dim, are, aim, bre, bim) do {                                \
        (dre) = (are) * (bre) - (aim) * (bim);                                 \
        (dim) = (are) * (bim) - (aim) * (bre);                                 \
    } while (0)

#define RESCALE(x) (x)

#define FOLD(a, b) ((a) + (b))

#elif defined(TX_INT32)

/* Properly rounds the result */
#define CMUL(dre, dim, are, aim, bre, bim) do {                                \
        int64_t accu;                                                          \
        (accu)  = (int64_t)(bre) * (are);                                      \
        (accu) -= (int64_t)(bim) * (aim);                                      \
        (dre)   = (int)(((accu) + 0x40000000) >> 31);                          \
        (accu)  = (int64_t)(bim) * (are);                                      \
        (accu) += (int64_t)(bre) * (aim);                                      \
        (dim)   = (int)(((accu) + 0x40000000) >> 31);                          \
    } while (0)

#define SMUL(dre, dim, are, aim, bre, bim) do {                                \
        int64_t accu;                                                          \
        (accu)  = (int64_t)(bre) * (are);                                      \
        (accu) -= (int64_t)(bim) * (aim);                                      \
        (dre)   = (int)(((accu) + 0x40000000) >> 31);                          \
        (accu)  = (int64_t)(bim) * (are);                                      \
        (accu) -= (int64_t)(bre) * (aim);                                      \
        (dim)   = (int)(((accu) + 0x40000000) >> 31);                          \
    } while (0)

#define RESCALE(x) (lrintf((x) * 2147483648.0))

#define FOLD(x, y) ((int)((x) + (unsigned)(y) + 32) >> 6)

#endif

#define BF(x, y, a, b) do {                                                    \
        x = (a) - (b);                                                         \
        y = (a) + (b);                                                         \
    } while (0)

#define CMUL3(c, a, b)                                                         \
    CMUL((c).re, (c).im, (a).re, (a).im, (b).re, (b).im)

#define COSTABLE(size) \
    DECLARE_ALIGNED(32, FFTSample, TX_NAME(ff_cos_##size))[size/2]

/* Used by asm, reorder with care */
struct AVTXContext {
    int n;              /* Nptwo part */
    int m;              /* Ptwo part */
    int inv;            /* Is inverted */
    int type;           /* Type */

    FFTComplex *exptab; /* MDCT exptab */
    FFTComplex *tmp;    /* Temporary buffer needed for all compound transforms */
    int        *pfatab; /* Input/Output mapping for compound transforms */
    int        *revtab; /* Input mapping for power of two transforms */
};

/* Shared functions */
int ff_tx_type_is_mdct(enum AVTXType type);
int ff_tx_gen_compound_mapping(AVTXContext *s);
int ff_tx_gen_ptwo_revtab(AVTXContext *s);

/* Also used by SIMD init */
static inline int split_radix_permutation(int i, int n, int inverse)
{
    int m;
    if (n <= 2)
        return i & 1;
    m = n >> 1;
    if (!(i & m))
        return split_radix_permutation(i, m, inverse)*2;
    m >>= 1;
    if (inverse == !(i & m))
        return split_radix_permutation(i, m, inverse)*4 + 1;
    else
        return split_radix_permutation(i, m, inverse)*4 - 1;
}

/* Templated functions */
int ff_tx_init_mdct_fft_float(AVTXContext *s, av_tx_fn *tx,
                              enum AVTXType type, int inv, int len,
                              const void *scale, uint64_t flags);
int ff_tx_init_mdct_fft_double(AVTXContext *s, av_tx_fn *tx,
                               enum AVTXType type, int inv, int len,
                               const void *scale, uint64_t flags);
int ff_tx_init_mdct_fft_int32(AVTXContext *s, av_tx_fn *tx,
                              enum AVTXType type, int inv, int len,
                              const void *scale, uint64_t flags);

typedef struct CosTabsInitOnce {
    void (*func)(void);
    AVOnce control;
} CosTabsInitOnce;

#endif /* AVUTIL_TX_PRIV_H */
