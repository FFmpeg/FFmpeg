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
#include "thread.h"
#include "mem_internal.h"
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

#define CMUL(dre, dim, are, aim, bre, bim)                                     \
    do {                                                                       \
        (dre) = (are) * (bre) - (aim) * (bim);                                 \
        (dim) = (are) * (bim) + (aim) * (bre);                                 \
    } while (0)

#define SMUL(dre, dim, are, aim, bre, bim)                                     \
    do {                                                                       \
        (dre) = (are) * (bre) - (aim) * (bim);                                 \
        (dim) = (are) * (bim) - (aim) * (bre);                                 \
    } while (0)

#define UNSCALE(x) (x)
#define RESCALE(x) (x)

#define FOLD(a, b) ((a) + (b))

#elif defined(TX_INT32)

/* Properly rounds the result */
#define CMUL(dre, dim, are, aim, bre, bim)                                     \
    do {                                                                       \
        int64_t accu;                                                          \
        (accu)  = (int64_t)(bre) * (are);                                      \
        (accu) -= (int64_t)(bim) * (aim);                                      \
        (dre)   = (int)(((accu) + 0x40000000) >> 31);                          \
        (accu)  = (int64_t)(bim) * (are);                                      \
        (accu) += (int64_t)(bre) * (aim);                                      \
        (dim)   = (int)(((accu) + 0x40000000) >> 31);                          \
    } while (0)

#define SMUL(dre, dim, are, aim, bre, bim)                                     \
    do {                                                                       \
        int64_t accu;                                                          \
        (accu)  = (int64_t)(bre) * (are);                                      \
        (accu) -= (int64_t)(bim) * (aim);                                      \
        (dre)   = (int)(((accu) + 0x40000000) >> 31);                          \
        (accu)  = (int64_t)(bim) * (are);                                      \
        (accu) -= (int64_t)(bre) * (aim);                                      \
        (dim)   = (int)(((accu) + 0x40000000) >> 31);                          \
    } while (0)

#define UNSCALE(x) ((double)x/2147483648.0)
#define RESCALE(x) (av_clip64(lrintf((x) * 2147483648.0), INT32_MIN, INT32_MAX))

#define FOLD(x, y) ((int)((x) + (unsigned)(y) + 32) >> 6)

#endif

#define BF(x, y, a, b)                                                         \
    do {                                                                       \
        x = (a) - (b);                                                         \
        y = (a) + (b);                                                         \
    } while (0)

#define CMUL3(c, a, b)                                                         \
    CMUL((c).re, (c).im, (a).re, (a).im, (b).re, (b).im)

#define COSTABLE(size)                                                         \
    DECLARE_ALIGNED(32, FFTSample, TX_NAME(ff_cos_##size))[size/4 + 1]

/* Used by asm, reorder with care */
struct AVTXContext {
    int n;              /* Non-power-of-two part */
    int m;              /* Power-of-two part */
    int inv;            /* Is inverse */
    int type;           /* Type */
    uint64_t flags;     /* Flags */
    double scale;       /* Scale */

    FFTComplex *exptab; /* MDCT exptab */
    FFTComplex    *tmp; /* Temporary buffer needed for all compound transforms */
    int        *pfatab; /* Input/Output mapping for compound transforms */
    int        *revtab; /* Input mapping for power of two transforms */
    int   *inplace_idx; /* Required indices to revtab for in-place transforms */

    int      *revtab_c; /* Revtab for only the C transforms, needed because
                         * checkasm makes us reuse the same context. */

    av_tx_fn    top_tx; /* Used for computing transforms derived from other
                         * transforms, like full-length iMDCTs and RDFTs.
                         * NOTE: Do NOT use this to mix assembly with C code. */
};

/* Checks if type is an MDCT */
int ff_tx_type_is_mdct(enum AVTXType type);

/*
 * Generates the PFA permutation table into AVTXContext->pfatab. The end table
 * is appended to the start table.
 */
int ff_tx_gen_compound_mapping(AVTXContext *s);

/*
 * Generates a standard-ish (slightly modified) Split-Radix revtab into
 * AVTXContext->revtab
 */
int ff_tx_gen_ptwo_revtab(AVTXContext *s, int invert_lookup);

/*
 * Generates an index into AVTXContext->inplace_idx that if followed in the
 * specific order,  allows the revtab to be done in-place. AVTXContext->revtab
 * must already exist.
 */
int ff_tx_gen_ptwo_inplace_revtab_idx(AVTXContext *s, int *revtab);

/*
 * This generates a parity-based revtab of length len and direction inv.
 *
 * Parity means even and odd complex numbers will be split, e.g. the even
 * coefficients will come first, after which the odd coefficients will be
 * placed. For example, a 4-point transform's coefficients after reordering:
 * z[0].re, z[0].im, z[2].re, z[2].im, z[1].re, z[1].im, z[3].re, z[3].im
 *
 * The basis argument is the length of the largest non-composite transform
 * supported, and also implies that the basis/2 transform is supported as well,
 * as the split-radix algorithm requires it to be.
 *
 * The dual_stride argument indicates that both the basis, as well as the
 * basis/2 transforms support doing two transforms at once, and the coefficients
 * will be interleaved between each pair in a split-radix like so (stride == 2):
 * tx1[0], tx1[2], tx2[0], tx2[2], tx1[1], tx1[3], tx2[1], tx2[3]
 * A non-zero number switches this on, with the value indicating the stride
 * (how many values of 1 transform to put first before switching to the other).
 * Must be a power of two or 0. Must be less than the basis.
 * Value will be clipped to the transform size, so for a basis of 16 and a
 * dual_stride of 8, dual 8-point transforms will be laid out as if dual_stride
 * was set to 4.
 * Usually you'll set this to half the complex numbers that fit in a single
 * register or 0. This allows to reuse SSE functions as dual-transform
 * functions in AVX mode.
 *
 * If length is smaller than basis/2 this function will not do anything.
 */
void ff_tx_gen_split_radix_parity_revtab(int *revtab, int len, int inv,
                                         int basis, int dual_stride);

/* Templated init functions */
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

void ff_tx_init_float_x86(AVTXContext *s, av_tx_fn *tx);

#endif /* AVUTIL_TX_PRIV_H */
