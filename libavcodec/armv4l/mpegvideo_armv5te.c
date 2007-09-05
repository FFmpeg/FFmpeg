/*
 * Optimization of some functions from mpegvideo.c for armv5te
 * Copyright (c) 2007 Siarhei Siamashka <ssvb@users.sourceforge.net>
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

#include "dsputil.h"
#include "mpegvideo.h"
#include "avcodec.h"


#ifdef ENABLE_ARM_TESTS
/**
 * h263 dequantizer supplementary function, it is performance critical and needs to
 * have optimized implementations for each architecture. Is also used as a reference
 * implementation in regression tests
 */
static inline void dct_unquantize_h263_helper_c(DCTELEM *block, int qmul, int qadd, int count)
{
    int i, level;
    for (i = 0; i < count; i++) {
        level = block[i];
        if (level) {
            if (level < 0) {
                level = level * qmul - qadd;
            } else {
                level = level * qmul + qadd;
            }
            block[i] = level;
        }
    }
}
#endif

/* GCC 3.1 or higher is required to support symbolic names in assembly code */
#if (__GNUC__ > 3) || ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 1))

/**
 * Special optimized version of dct_unquantize_h263_helper_c, it requires the block
 * to be at least 8 bytes aligned, and may process more elements than requested.
 * But it is guaranteed to never process more than 64 elements provided that
 * xxcount argument is <= 64, so it is safe. This macro is optimized for a common
 * distribution of values for nCoeffs (they are mostly multiple of 8 plus one or
 * two extra elements). So this macro processes data as 8 elements per loop iteration
 * and contains optional 2 elements processing in the end.
 *
 * Inner loop should take 6 cycles per element on arm926ej-s (Nokia 770)
 */
#define dct_unquantize_h263_special_helper_armv5te(xxblock, xxqmul, xxqadd, xxcount) \
({ DCTELEM *xblock = xxblock; \
   int xqmul = xxqmul, xqadd = xxqadd, xcount = xxcount, xtmp; \
   int xdata1, xdata2; \
__asm__ __volatile__( \
        "subs %[count], %[count], #2       \n\t" \
        "ble 2f                            \n\t" \
        "ldrd r4, [%[block], #0]           \n\t" \
        "1:                                \n\t" \
        "ldrd r6, [%[block], #8]           \n\t" \
\
        "rsbs %[data1], %[zero], r4, asr #16 \n\t" \
        "addgt %[data1], %[qadd], #0       \n\t" \
        "rsblt %[data1], %[qadd], #0       \n\t" \
        "smlatbne %[data1], r4, %[qmul], %[data1] \n\t" \
\
        "rsbs %[data2], %[zero], r5, asr #16 \n\t" \
        "addgt %[data2], %[qadd], #0       \n\t" \
        "rsblt %[data2], %[qadd], #0       \n\t" \
        "smlatbne %[data2], r5, %[qmul], %[data2] \n\t" \
\
        "rsbs %[tmp], %[zero], r4, asl #16 \n\t" \
        "addgt %[tmp], %[qadd], #0         \n\t" \
        "rsblt %[tmp], %[qadd], #0         \n\t" \
        "smlabbne r4, r4, %[qmul], %[tmp]  \n\t" \
\
        "rsbs %[tmp], %[zero], r5, asl #16 \n\t" \
        "addgt %[tmp], %[qadd], #0         \n\t" \
        "rsblt %[tmp], %[qadd], #0         \n\t" \
        "smlabbne r5, r5, %[qmul], %[tmp]  \n\t" \
\
        "strh r4, [%[block]], #2           \n\t" \
        "strh %[data1], [%[block]], #2     \n\t" \
        "strh r5, [%[block]], #2           \n\t" \
        "strh %[data2], [%[block]], #2     \n\t" \
\
        "rsbs %[data1], %[zero], r6, asr #16 \n\t" \
        "addgt %[data1], %[qadd], #0        \n\t" \
        "rsblt %[data1], %[qadd], #0        \n\t" \
        "smlatbne %[data1], r6, %[qmul], %[data1] \n\t" \
\
        "rsbs %[data2], %[zero], r7, asr #16 \n\t" \
        "addgt %[data2], %[qadd], #0        \n\t" \
        "rsblt %[data2], %[qadd], #0        \n\t" \
        "smlatbne %[data2], r7, %[qmul], %[data2] \n\t" \
\
        "rsbs %[tmp], %[zero], r6, asl #16  \n\t" \
        "addgt %[tmp], %[qadd], #0          \n\t" \
        "rsblt %[tmp], %[qadd], #0          \n\t" \
        "smlabbne r6, r6, %[qmul], %[tmp]   \n\t" \
\
        "rsbs %[tmp], %[zero], r7, asl #16  \n\t" \
        "addgt %[tmp], %[qadd], #0          \n\t" \
        "rsblt %[tmp], %[qadd], #0          \n\t" \
        "smlabbne r7, r7, %[qmul], %[tmp]   \n\t" \
\
        "strh r6, [%[block]], #2            \n\t" \
        "strh %[data1], [%[block]], #2      \n\t" \
        "strh r7, [%[block]], #2            \n\t" \
        "strh %[data2], [%[block]], #2      \n\t" \
\
        "subs %[count], %[count], #8        \n\t" \
        "ldrgtd r4, [%[block], #0]          \n\t" /* load data early to avoid load/use pipeline stall */ \
        "bgt 1b                             \n\t" \
\
        "adds %[count], %[count], #2        \n\t" \
        "ble  3f                            \n\t" \
        "2:                                 \n\t" \
        "ldrsh %[data1], [%[block], #0]     \n\t" \
        "ldrsh %[data2], [%[block], #2]     \n\t" \
        "mov  %[tmp], %[qadd]               \n\t" \
        "cmp  %[data1], #0                  \n\t" \
        "rsblt %[tmp], %[qadd], #0          \n\t" \
        "smlabbne %[data1], %[data1], %[qmul], %[tmp] \n\t" \
        "mov  %[tmp], %[qadd]               \n\t" \
        "cmp  %[data2], #0                  \n\t" \
        "rsblt %[tmp], %[qadd], #0          \n\t" \
        "smlabbne %[data2], %[data2], %[qmul], %[tmp] \n\t" \
        "strh %[data1], [%[block]], #2      \n\t" \
        "strh %[data2], [%[block]], #2      \n\t" \
        "3:                                 \n\t" \
        : [block] "+&r" (xblock), [count] "+&r" (xcount), [tmp] "=&r" (xtmp), \
          [data1] "=&r" (xdata1), [data2] "=&r" (xdata2)  \
        : [qmul] "r" (xqmul), [qadd] "r" (xqadd), [zero] "r" (0) \
        : "r4", "r5", "r6", "r7", "cc", "memory" \
); \
})

static void dct_unquantize_h263_intra_armv5te(MpegEncContext *s,
                                  DCTELEM *block, int n, int qscale)
{
    int level, qmul, qadd;
    int nCoeffs;

    assert(s->block_last_index[n]>=0);

    qmul = qscale << 1;

    if (!s->h263_aic) {
        if (n < 4)
            level = block[0] * s->y_dc_scale;
        else
            level = block[0] * s->c_dc_scale;
        qadd = (qscale - 1) | 1;
    }else{
        qadd = 0;
        level = block[0];
    }
    if(s->ac_pred)
        nCoeffs=63;
    else
        nCoeffs= s->inter_scantable.raster_end[ s->block_last_index[n] ];

    dct_unquantize_h263_special_helper_armv5te(block, qmul, qadd, nCoeffs + 1);
    block[0] = level;
}

static void dct_unquantize_h263_inter_armv5te(MpegEncContext *s,
                                  DCTELEM *block, int n, int qscale)
{
    int qmul, qadd;
    int nCoeffs;

    assert(s->block_last_index[n]>=0);

    qadd = (qscale - 1) | 1;
    qmul = qscale << 1;

    nCoeffs= s->inter_scantable.raster_end[ s->block_last_index[n] ];

    dct_unquantize_h263_special_helper_armv5te(block, qmul, qadd, nCoeffs + 1);
}

#define HAVE_DCT_UNQUANTIZE_H263_ARMV5TE_OPTIMIZED

#endif

void MPV_common_init_armv5te(MpegEncContext *s)
{
#ifdef HAVE_DCT_UNQUANTIZE_H263_ARMV5TE_OPTIMIZED
    s->dct_unquantize_h263_intra = dct_unquantize_h263_intra_armv5te;
    s->dct_unquantize_h263_inter = dct_unquantize_h263_inter_armv5te;
#endif
}
