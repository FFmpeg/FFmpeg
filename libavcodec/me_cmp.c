/*
 * DSP utils
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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

#include "libavutil/attributes.h"
#include "libavutil/internal.h"
#include "avcodec.h"
#include "copy_block.h"
#include "simple_idct.h"
#include "me_cmp.h"
#include "mpegvideo.h"
#include "config.h"

uint32_t ff_square_tab[512] = { 0, };

static int sse4_c(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                  ptrdiff_t stride, int h)
{
    int s = 0, i;
    uint32_t *sq = ff_square_tab + 256;

    for (i = 0; i < h; i++) {
        s    += sq[pix1[0] - pix2[0]];
        s    += sq[pix1[1] - pix2[1]];
        s    += sq[pix1[2] - pix2[2]];
        s    += sq[pix1[3] - pix2[3]];
        pix1 += stride;
        pix2 += stride;
    }
    return s;
}

static int sse8_c(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                  ptrdiff_t stride, int h)
{
    int s = 0, i;
    uint32_t *sq = ff_square_tab + 256;

    for (i = 0; i < h; i++) {
        s    += sq[pix1[0] - pix2[0]];
        s    += sq[pix1[1] - pix2[1]];
        s    += sq[pix1[2] - pix2[2]];
        s    += sq[pix1[3] - pix2[3]];
        s    += sq[pix1[4] - pix2[4]];
        s    += sq[pix1[5] - pix2[5]];
        s    += sq[pix1[6] - pix2[6]];
        s    += sq[pix1[7] - pix2[7]];
        pix1 += stride;
        pix2 += stride;
    }
    return s;
}

static int sse16_c(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                   ptrdiff_t stride, int h)
{
    int s = 0, i;
    uint32_t *sq = ff_square_tab + 256;

    for (i = 0; i < h; i++) {
        s += sq[pix1[0]  - pix2[0]];
        s += sq[pix1[1]  - pix2[1]];
        s += sq[pix1[2]  - pix2[2]];
        s += sq[pix1[3]  - pix2[3]];
        s += sq[pix1[4]  - pix2[4]];
        s += sq[pix1[5]  - pix2[5]];
        s += sq[pix1[6]  - pix2[6]];
        s += sq[pix1[7]  - pix2[7]];
        s += sq[pix1[8]  - pix2[8]];
        s += sq[pix1[9]  - pix2[9]];
        s += sq[pix1[10] - pix2[10]];
        s += sq[pix1[11] - pix2[11]];
        s += sq[pix1[12] - pix2[12]];
        s += sq[pix1[13] - pix2[13]];
        s += sq[pix1[14] - pix2[14]];
        s += sq[pix1[15] - pix2[15]];

        pix1 += stride;
        pix2 += stride;
    }
    return s;
}

static int sum_abs_dctelem_c(int16_t *block)
{
    int sum = 0, i;

    for (i = 0; i < 64; i++)
        sum += FFABS(block[i]);
    return sum;
}

#define avg2(a, b) (((a) + (b) + 1) >> 1)
#define avg4(a, b, c, d) (((a) + (b) + (c) + (d) + 2) >> 2)

static inline int pix_abs16_c(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                              ptrdiff_t stride, int h)
{
    int s = 0, i;

    for (i = 0; i < h; i++) {
        s    += abs(pix1[0]  - pix2[0]);
        s    += abs(pix1[1]  - pix2[1]);
        s    += abs(pix1[2]  - pix2[2]);
        s    += abs(pix1[3]  - pix2[3]);
        s    += abs(pix1[4]  - pix2[4]);
        s    += abs(pix1[5]  - pix2[5]);
        s    += abs(pix1[6]  - pix2[6]);
        s    += abs(pix1[7]  - pix2[7]);
        s    += abs(pix1[8]  - pix2[8]);
        s    += abs(pix1[9]  - pix2[9]);
        s    += abs(pix1[10] - pix2[10]);
        s    += abs(pix1[11] - pix2[11]);
        s    += abs(pix1[12] - pix2[12]);
        s    += abs(pix1[13] - pix2[13]);
        s    += abs(pix1[14] - pix2[14]);
        s    += abs(pix1[15] - pix2[15]);
        pix1 += stride;
        pix2 += stride;
    }
    return s;
}

static inline int pix_median_abs16_c(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                             ptrdiff_t stride, int h)
{
    int s = 0, i, j;

#define V(x) (pix1[x] - pix2[x])

    s    += abs(V(0));
    s    += abs(V(1) - V(0));
    s    += abs(V(2) - V(1));
    s    += abs(V(3) - V(2));
    s    += abs(V(4) - V(3));
    s    += abs(V(5) - V(4));
    s    += abs(V(6) - V(5));
    s    += abs(V(7) - V(6));
    s    += abs(V(8) - V(7));
    s    += abs(V(9) - V(8));
    s    += abs(V(10) - V(9));
    s    += abs(V(11) - V(10));
    s    += abs(V(12) - V(11));
    s    += abs(V(13) - V(12));
    s    += abs(V(14) - V(13));
    s    += abs(V(15) - V(14));

    pix1 += stride;
    pix2 += stride;

    for (i = 1; i < h; i++) {
        s    += abs(V(0) - V(-stride));
        for (j = 1; j < 16; j++)
            s    += abs(V(j) - mid_pred(V(j-stride), V(j-1), V(j-stride) + V(j-1) - V(j-stride-1)));
        pix1 += stride;
        pix2 += stride;

    }
#undef V
    return s;
}

static int pix_abs16_x2_c(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                          ptrdiff_t stride, int h)
{
    int s = 0, i;

    for (i = 0; i < h; i++) {
        s    += abs(pix1[0]  - avg2(pix2[0],  pix2[1]));
        s    += abs(pix1[1]  - avg2(pix2[1],  pix2[2]));
        s    += abs(pix1[2]  - avg2(pix2[2],  pix2[3]));
        s    += abs(pix1[3]  - avg2(pix2[3],  pix2[4]));
        s    += abs(pix1[4]  - avg2(pix2[4],  pix2[5]));
        s    += abs(pix1[5]  - avg2(pix2[5],  pix2[6]));
        s    += abs(pix1[6]  - avg2(pix2[6],  pix2[7]));
        s    += abs(pix1[7]  - avg2(pix2[7],  pix2[8]));
        s    += abs(pix1[8]  - avg2(pix2[8],  pix2[9]));
        s    += abs(pix1[9]  - avg2(pix2[9],  pix2[10]));
        s    += abs(pix1[10] - avg2(pix2[10], pix2[11]));
        s    += abs(pix1[11] - avg2(pix2[11], pix2[12]));
        s    += abs(pix1[12] - avg2(pix2[12], pix2[13]));
        s    += abs(pix1[13] - avg2(pix2[13], pix2[14]));
        s    += abs(pix1[14] - avg2(pix2[14], pix2[15]));
        s    += abs(pix1[15] - avg2(pix2[15], pix2[16]));
        pix1 += stride;
        pix2 += stride;
    }
    return s;
}

static int pix_abs16_y2_c(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                          ptrdiff_t stride, int h)
{
    int s = 0, i;
    uint8_t *pix3 = pix2 + stride;

    for (i = 0; i < h; i++) {
        s    += abs(pix1[0]  - avg2(pix2[0],  pix3[0]));
        s    += abs(pix1[1]  - avg2(pix2[1],  pix3[1]));
        s    += abs(pix1[2]  - avg2(pix2[2],  pix3[2]));
        s    += abs(pix1[3]  - avg2(pix2[3],  pix3[3]));
        s    += abs(pix1[4]  - avg2(pix2[4],  pix3[4]));
        s    += abs(pix1[5]  - avg2(pix2[5],  pix3[5]));
        s    += abs(pix1[6]  - avg2(pix2[6],  pix3[6]));
        s    += abs(pix1[7]  - avg2(pix2[7],  pix3[7]));
        s    += abs(pix1[8]  - avg2(pix2[8],  pix3[8]));
        s    += abs(pix1[9]  - avg2(pix2[9],  pix3[9]));
        s    += abs(pix1[10] - avg2(pix2[10], pix3[10]));
        s    += abs(pix1[11] - avg2(pix2[11], pix3[11]));
        s    += abs(pix1[12] - avg2(pix2[12], pix3[12]));
        s    += abs(pix1[13] - avg2(pix2[13], pix3[13]));
        s    += abs(pix1[14] - avg2(pix2[14], pix3[14]));
        s    += abs(pix1[15] - avg2(pix2[15], pix3[15]));
        pix1 += stride;
        pix2 += stride;
        pix3 += stride;
    }
    return s;
}

static int pix_abs16_xy2_c(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                           ptrdiff_t stride, int h)
{
    int s = 0, i;
    uint8_t *pix3 = pix2 + stride;

    for (i = 0; i < h; i++) {
        s    += abs(pix1[0]  - avg4(pix2[0],  pix2[1],  pix3[0],  pix3[1]));
        s    += abs(pix1[1]  - avg4(pix2[1],  pix2[2],  pix3[1],  pix3[2]));
        s    += abs(pix1[2]  - avg4(pix2[2],  pix2[3],  pix3[2],  pix3[3]));
        s    += abs(pix1[3]  - avg4(pix2[3],  pix2[4],  pix3[3],  pix3[4]));
        s    += abs(pix1[4]  - avg4(pix2[4],  pix2[5],  pix3[4],  pix3[5]));
        s    += abs(pix1[5]  - avg4(pix2[5],  pix2[6],  pix3[5],  pix3[6]));
        s    += abs(pix1[6]  - avg4(pix2[6],  pix2[7],  pix3[6],  pix3[7]));
        s    += abs(pix1[7]  - avg4(pix2[7],  pix2[8],  pix3[7],  pix3[8]));
        s    += abs(pix1[8]  - avg4(pix2[8],  pix2[9],  pix3[8],  pix3[9]));
        s    += abs(pix1[9]  - avg4(pix2[9],  pix2[10], pix3[9],  pix3[10]));
        s    += abs(pix1[10] - avg4(pix2[10], pix2[11], pix3[10], pix3[11]));
        s    += abs(pix1[11] - avg4(pix2[11], pix2[12], pix3[11], pix3[12]));
        s    += abs(pix1[12] - avg4(pix2[12], pix2[13], pix3[12], pix3[13]));
        s    += abs(pix1[13] - avg4(pix2[13], pix2[14], pix3[13], pix3[14]));
        s    += abs(pix1[14] - avg4(pix2[14], pix2[15], pix3[14], pix3[15]));
        s    += abs(pix1[15] - avg4(pix2[15], pix2[16], pix3[15], pix3[16]));
        pix1 += stride;
        pix2 += stride;
        pix3 += stride;
    }
    return s;
}

static inline int pix_abs8_c(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                             ptrdiff_t stride, int h)
{
    int s = 0, i;

    for (i = 0; i < h; i++) {
        s    += abs(pix1[0] - pix2[0]);
        s    += abs(pix1[1] - pix2[1]);
        s    += abs(pix1[2] - pix2[2]);
        s    += abs(pix1[3] - pix2[3]);
        s    += abs(pix1[4] - pix2[4]);
        s    += abs(pix1[5] - pix2[5]);
        s    += abs(pix1[6] - pix2[6]);
        s    += abs(pix1[7] - pix2[7]);
        pix1 += stride;
        pix2 += stride;
    }
    return s;
}

static inline int pix_median_abs8_c(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                             ptrdiff_t stride, int h)
{
    int s = 0, i, j;

#define V(x) (pix1[x] - pix2[x])

    s    += abs(V(0));
    s    += abs(V(1) - V(0));
    s    += abs(V(2) - V(1));
    s    += abs(V(3) - V(2));
    s    += abs(V(4) - V(3));
    s    += abs(V(5) - V(4));
    s    += abs(V(6) - V(5));
    s    += abs(V(7) - V(6));

    pix1 += stride;
    pix2 += stride;

    for (i = 1; i < h; i++) {
        s    += abs(V(0) - V(-stride));
        for (j = 1; j < 8; j++)
            s    += abs(V(j) - mid_pred(V(j-stride), V(j-1), V(j-stride) + V(j-1) - V(j-stride-1)));
        pix1 += stride;
        pix2 += stride;

    }
#undef V
    return s;
}

static int pix_abs8_x2_c(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                         ptrdiff_t stride, int h)
{
    int s = 0, i;

    for (i = 0; i < h; i++) {
        s    += abs(pix1[0] - avg2(pix2[0], pix2[1]));
        s    += abs(pix1[1] - avg2(pix2[1], pix2[2]));
        s    += abs(pix1[2] - avg2(pix2[2], pix2[3]));
        s    += abs(pix1[3] - avg2(pix2[3], pix2[4]));
        s    += abs(pix1[4] - avg2(pix2[4], pix2[5]));
        s    += abs(pix1[5] - avg2(pix2[5], pix2[6]));
        s    += abs(pix1[6] - avg2(pix2[6], pix2[7]));
        s    += abs(pix1[7] - avg2(pix2[7], pix2[8]));
        pix1 += stride;
        pix2 += stride;
    }
    return s;
}

static int pix_abs8_y2_c(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                         ptrdiff_t stride, int h)
{
    int s = 0, i;
    uint8_t *pix3 = pix2 + stride;

    for (i = 0; i < h; i++) {
        s    += abs(pix1[0] - avg2(pix2[0], pix3[0]));
        s    += abs(pix1[1] - avg2(pix2[1], pix3[1]));
        s    += abs(pix1[2] - avg2(pix2[2], pix3[2]));
        s    += abs(pix1[3] - avg2(pix2[3], pix3[3]));
        s    += abs(pix1[4] - avg2(pix2[4], pix3[4]));
        s    += abs(pix1[5] - avg2(pix2[5], pix3[5]));
        s    += abs(pix1[6] - avg2(pix2[6], pix3[6]));
        s    += abs(pix1[7] - avg2(pix2[7], pix3[7]));
        pix1 += stride;
        pix2 += stride;
        pix3 += stride;
    }
    return s;
}

static int pix_abs8_xy2_c(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                          ptrdiff_t stride, int h)
{
    int s = 0, i;
    uint8_t *pix3 = pix2 + stride;

    for (i = 0; i < h; i++) {
        s    += abs(pix1[0] - avg4(pix2[0], pix2[1], pix3[0], pix3[1]));
        s    += abs(pix1[1] - avg4(pix2[1], pix2[2], pix3[1], pix3[2]));
        s    += abs(pix1[2] - avg4(pix2[2], pix2[3], pix3[2], pix3[3]));
        s    += abs(pix1[3] - avg4(pix2[3], pix2[4], pix3[3], pix3[4]));
        s    += abs(pix1[4] - avg4(pix2[4], pix2[5], pix3[4], pix3[5]));
        s    += abs(pix1[5] - avg4(pix2[5], pix2[6], pix3[5], pix3[6]));
        s    += abs(pix1[6] - avg4(pix2[6], pix2[7], pix3[6], pix3[7]));
        s    += abs(pix1[7] - avg4(pix2[7], pix2[8], pix3[7], pix3[8]));
        pix1 += stride;
        pix2 += stride;
        pix3 += stride;
    }
    return s;
}

static int nsse16_c(MpegEncContext *c, uint8_t *s1, uint8_t *s2,
                    ptrdiff_t stride, int h)
{
    int score1 = 0, score2 = 0, x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < 16; x++)
            score1 += (s1[x] - s2[x]) * (s1[x] - s2[x]);
        if (y + 1 < h) {
            for (x = 0; x < 15; x++)
                score2 += FFABS(s1[x]     - s1[x + stride] -
                                s1[x + 1] + s1[x + stride + 1]) -
                          FFABS(s2[x]     - s2[x + stride] -
                                s2[x + 1] + s2[x + stride + 1]);
        }
        s1 += stride;
        s2 += stride;
    }

    if (c)
        return score1 + FFABS(score2) * c->avctx->nsse_weight;
    else
        return score1 + FFABS(score2) * 8;
}

static int nsse8_c(MpegEncContext *c, uint8_t *s1, uint8_t *s2,
                   ptrdiff_t stride, int h)
{
    int score1 = 0, score2 = 0, x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < 8; x++)
            score1 += (s1[x] - s2[x]) * (s1[x] - s2[x]);
        if (y + 1 < h) {
            for (x = 0; x < 7; x++)
                score2 += FFABS(s1[x]     - s1[x + stride] -
                                s1[x + 1] + s1[x + stride + 1]) -
                          FFABS(s2[x]     - s2[x + stride] -
                                s2[x + 1] + s2[x + stride + 1]);
        }
        s1 += stride;
        s2 += stride;
    }

    if (c)
        return score1 + FFABS(score2) * c->avctx->nsse_weight;
    else
        return score1 + FFABS(score2) * 8;
}

static int zero_cmp(MpegEncContext *s, uint8_t *a, uint8_t *b,
                    ptrdiff_t stride, int h)
{
    return 0;
}

void ff_set_cmp(MECmpContext *c, me_cmp_func *cmp, int type)
{
    int i;

    memset(cmp, 0, sizeof(void *) * 6);

    for (i = 0; i < 6; i++) {
        switch (type & 0xFF) {
        case FF_CMP_SAD:
            cmp[i] = c->sad[i];
            break;
        case FF_CMP_MEDIAN_SAD:
            cmp[i] = c->median_sad[i];
            break;
        case FF_CMP_SATD:
            cmp[i] = c->hadamard8_diff[i];
            break;
        case FF_CMP_SSE:
            cmp[i] = c->sse[i];
            break;
        case FF_CMP_DCT:
            cmp[i] = c->dct_sad[i];
            break;
        case FF_CMP_DCT264:
            cmp[i] = c->dct264_sad[i];
            break;
        case FF_CMP_DCTMAX:
            cmp[i] = c->dct_max[i];
            break;
        case FF_CMP_PSNR:
            cmp[i] = c->quant_psnr[i];
            break;
        case FF_CMP_BIT:
            cmp[i] = c->bit[i];
            break;
        case FF_CMP_RD:
            cmp[i] = c->rd[i];
            break;
        case FF_CMP_VSAD:
            cmp[i] = c->vsad[i];
            break;
        case FF_CMP_VSSE:
            cmp[i] = c->vsse[i];
            break;
        case FF_CMP_ZERO:
            cmp[i] = zero_cmp;
            break;
        case FF_CMP_NSSE:
            cmp[i] = c->nsse[i];
            break;
#if CONFIG_DWT
        case FF_CMP_W53:
            cmp[i]= c->w53[i];
            break;
        case FF_CMP_W97:
            cmp[i]= c->w97[i];
            break;
#endif
        default:
            av_log(NULL, AV_LOG_ERROR,
                   "internal error in cmp function selection\n");
        }
    }
}

#define BUTTERFLY2(o1, o2, i1, i2)              \
    o1 = (i1) + (i2);                           \
    o2 = (i1) - (i2);

#define BUTTERFLY1(x, y)                        \
    {                                           \
        int a, b;                               \
        a = x;                                  \
        b = y;                                  \
        x = a + b;                              \
        y = a - b;                              \
    }

#define BUTTERFLYA(x, y) (FFABS((x) + (y)) + FFABS((x) - (y)))

static int hadamard8_diff8x8_c(MpegEncContext *s, uint8_t *dst,
                               uint8_t *src, ptrdiff_t stride, int h)
{
    int i, temp[64], sum = 0;

    av_assert2(h == 8);

    for (i = 0; i < 8; i++) {
        // FIXME: try pointer walks
        BUTTERFLY2(temp[8 * i + 0], temp[8 * i + 1],
                   src[stride * i + 0] - dst[stride * i + 0],
                   src[stride * i + 1] - dst[stride * i + 1]);
        BUTTERFLY2(temp[8 * i + 2], temp[8 * i + 3],
                   src[stride * i + 2] - dst[stride * i + 2],
                   src[stride * i + 3] - dst[stride * i + 3]);
        BUTTERFLY2(temp[8 * i + 4], temp[8 * i + 5],
                   src[stride * i + 4] - dst[stride * i + 4],
                   src[stride * i + 5] - dst[stride * i + 5]);
        BUTTERFLY2(temp[8 * i + 6], temp[8 * i + 7],
                   src[stride * i + 6] - dst[stride * i + 6],
                   src[stride * i + 7] - dst[stride * i + 7]);

        BUTTERFLY1(temp[8 * i + 0], temp[8 * i + 2]);
        BUTTERFLY1(temp[8 * i + 1], temp[8 * i + 3]);
        BUTTERFLY1(temp[8 * i + 4], temp[8 * i + 6]);
        BUTTERFLY1(temp[8 * i + 5], temp[8 * i + 7]);

        BUTTERFLY1(temp[8 * i + 0], temp[8 * i + 4]);
        BUTTERFLY1(temp[8 * i + 1], temp[8 * i + 5]);
        BUTTERFLY1(temp[8 * i + 2], temp[8 * i + 6]);
        BUTTERFLY1(temp[8 * i + 3], temp[8 * i + 7]);
    }

    for (i = 0; i < 8; i++) {
        BUTTERFLY1(temp[8 * 0 + i], temp[8 * 1 + i]);
        BUTTERFLY1(temp[8 * 2 + i], temp[8 * 3 + i]);
        BUTTERFLY1(temp[8 * 4 + i], temp[8 * 5 + i]);
        BUTTERFLY1(temp[8 * 6 + i], temp[8 * 7 + i]);

        BUTTERFLY1(temp[8 * 0 + i], temp[8 * 2 + i]);
        BUTTERFLY1(temp[8 * 1 + i], temp[8 * 3 + i]);
        BUTTERFLY1(temp[8 * 4 + i], temp[8 * 6 + i]);
        BUTTERFLY1(temp[8 * 5 + i], temp[8 * 7 + i]);

        sum += BUTTERFLYA(temp[8 * 0 + i], temp[8 * 4 + i]) +
               BUTTERFLYA(temp[8 * 1 + i], temp[8 * 5 + i]) +
               BUTTERFLYA(temp[8 * 2 + i], temp[8 * 6 + i]) +
               BUTTERFLYA(temp[8 * 3 + i], temp[8 * 7 + i]);
    }
    return sum;
}

static int hadamard8_intra8x8_c(MpegEncContext *s, uint8_t *src,
                                uint8_t *dummy, ptrdiff_t stride, int h)
{
    int i, temp[64], sum = 0;

    av_assert2(h == 8);

    for (i = 0; i < 8; i++) {
        // FIXME: try pointer walks
        BUTTERFLY2(temp[8 * i + 0], temp[8 * i + 1],
                   src[stride * i + 0], src[stride * i + 1]);
        BUTTERFLY2(temp[8 * i + 2], temp[8 * i + 3],
                   src[stride * i + 2], src[stride * i + 3]);
        BUTTERFLY2(temp[8 * i + 4], temp[8 * i + 5],
                   src[stride * i + 4], src[stride * i + 5]);
        BUTTERFLY2(temp[8 * i + 6], temp[8 * i + 7],
                   src[stride * i + 6], src[stride * i + 7]);

        BUTTERFLY1(temp[8 * i + 0], temp[8 * i + 2]);
        BUTTERFLY1(temp[8 * i + 1], temp[8 * i + 3]);
        BUTTERFLY1(temp[8 * i + 4], temp[8 * i + 6]);
        BUTTERFLY1(temp[8 * i + 5], temp[8 * i + 7]);

        BUTTERFLY1(temp[8 * i + 0], temp[8 * i + 4]);
        BUTTERFLY1(temp[8 * i + 1], temp[8 * i + 5]);
        BUTTERFLY1(temp[8 * i + 2], temp[8 * i + 6]);
        BUTTERFLY1(temp[8 * i + 3], temp[8 * i + 7]);
    }

    for (i = 0; i < 8; i++) {
        BUTTERFLY1(temp[8 * 0 + i], temp[8 * 1 + i]);
        BUTTERFLY1(temp[8 * 2 + i], temp[8 * 3 + i]);
        BUTTERFLY1(temp[8 * 4 + i], temp[8 * 5 + i]);
        BUTTERFLY1(temp[8 * 6 + i], temp[8 * 7 + i]);

        BUTTERFLY1(temp[8 * 0 + i], temp[8 * 2 + i]);
        BUTTERFLY1(temp[8 * 1 + i], temp[8 * 3 + i]);
        BUTTERFLY1(temp[8 * 4 + i], temp[8 * 6 + i]);
        BUTTERFLY1(temp[8 * 5 + i], temp[8 * 7 + i]);

        sum +=
            BUTTERFLYA(temp[8 * 0 + i], temp[8 * 4 + i])
            + BUTTERFLYA(temp[8 * 1 + i], temp[8 * 5 + i])
            + BUTTERFLYA(temp[8 * 2 + i], temp[8 * 6 + i])
            + BUTTERFLYA(temp[8 * 3 + i], temp[8 * 7 + i]);
    }

    sum -= FFABS(temp[8 * 0] + temp[8 * 4]); // -mean

    return sum;
}

static int dct_sad8x8_c(MpegEncContext *s, uint8_t *src1,
                        uint8_t *src2, ptrdiff_t stride, int h)
{
    LOCAL_ALIGNED_16(int16_t, temp, [64]);

    av_assert2(h == 8);

    s->pdsp.diff_pixels(temp, src1, src2, stride);
    s->fdsp.fdct(temp);
    return s->mecc.sum_abs_dctelem(temp);
}

#if CONFIG_GPL
#define DCT8_1D                                         \
    {                                                   \
        const int s07 = SRC(0) + SRC(7);                \
        const int s16 = SRC(1) + SRC(6);                \
        const int s25 = SRC(2) + SRC(5);                \
        const int s34 = SRC(3) + SRC(4);                \
        const int a0  = s07 + s34;                      \
        const int a1  = s16 + s25;                      \
        const int a2  = s07 - s34;                      \
        const int a3  = s16 - s25;                      \
        const int d07 = SRC(0) - SRC(7);                \
        const int d16 = SRC(1) - SRC(6);                \
        const int d25 = SRC(2) - SRC(5);                \
        const int d34 = SRC(3) - SRC(4);                \
        const int a4  = d16 + d25 + (d07 + (d07 >> 1)); \
        const int a5  = d07 - d34 - (d25 + (d25 >> 1)); \
        const int a6  = d07 + d34 - (d16 + (d16 >> 1)); \
        const int a7  = d16 - d25 + (d34 + (d34 >> 1)); \
        DST(0, a0 + a1);                                \
        DST(1, a4 + (a7 >> 2));                         \
        DST(2, a2 + (a3 >> 1));                         \
        DST(3, a5 + (a6 >> 2));                         \
        DST(4, a0 - a1);                                \
        DST(5, a6 - (a5 >> 2));                         \
        DST(6, (a2 >> 1) - a3);                         \
        DST(7, (a4 >> 2) - a7);                         \
    }

static int dct264_sad8x8_c(MpegEncContext *s, uint8_t *src1,
                           uint8_t *src2, ptrdiff_t stride, int h)
{
    int16_t dct[8][8];
    int i, sum = 0;

    s->pdsp.diff_pixels(dct[0], src1, src2, stride);

#define SRC(x) dct[i][x]
#define DST(x, v) dct[i][x] = v
    for (i = 0; i < 8; i++)
        DCT8_1D
#undef SRC
#undef DST

#define SRC(x) dct[x][i]
#define DST(x, v) sum += FFABS(v)
        for (i = 0; i < 8; i++)
            DCT8_1D
#undef SRC
#undef DST
            return sum;
}
#endif

static int dct_max8x8_c(MpegEncContext *s, uint8_t *src1,
                        uint8_t *src2, ptrdiff_t stride, int h)
{
    LOCAL_ALIGNED_16(int16_t, temp, [64]);
    int sum = 0, i;

    av_assert2(h == 8);

    s->pdsp.diff_pixels(temp, src1, src2, stride);
    s->fdsp.fdct(temp);

    for (i = 0; i < 64; i++)
        sum = FFMAX(sum, FFABS(temp[i]));

    return sum;
}

static int quant_psnr8x8_c(MpegEncContext *s, uint8_t *src1,
                           uint8_t *src2, ptrdiff_t stride, int h)
{
    LOCAL_ALIGNED_16(int16_t, temp, [64 * 2]);
    int16_t *const bak = temp + 64;
    int sum = 0, i;

    av_assert2(h == 8);
    s->mb_intra = 0;

    s->pdsp.diff_pixels(temp, src1, src2, stride);

    memcpy(bak, temp, 64 * sizeof(int16_t));

    s->block_last_index[0 /* FIXME */] =
        s->fast_dct_quantize(s, temp, 0 /* FIXME */, s->qscale, &i);
    s->dct_unquantize_inter(s, temp, 0, s->qscale);
    ff_simple_idct_8(temp); // FIXME

    for (i = 0; i < 64; i++)
        sum += (temp[i] - bak[i]) * (temp[i] - bak[i]);

    return sum;
}

static int rd8x8_c(MpegEncContext *s, uint8_t *src1, uint8_t *src2,
                   ptrdiff_t stride, int h)
{
    const uint8_t *scantable = s->intra_scantable.permutated;
    LOCAL_ALIGNED_16(int16_t, temp, [64]);
    LOCAL_ALIGNED_16(uint8_t, lsrc1, [64]);
    LOCAL_ALIGNED_16(uint8_t, lsrc2, [64]);
    int i, last, run, bits, level, distortion, start_i;
    const int esc_length = s->ac_esc_length;
    uint8_t *length, *last_length;

    av_assert2(h == 8);

    copy_block8(lsrc1, src1, 8, stride, 8);
    copy_block8(lsrc2, src2, 8, stride, 8);

    s->pdsp.diff_pixels(temp, lsrc1, lsrc2, 8);

    s->block_last_index[0 /* FIXME */] =
    last                               =
        s->fast_dct_quantize(s, temp, 0 /* FIXME */, s->qscale, &i);

    bits = 0;

    if (s->mb_intra) {
        start_i     = 1;
        length      = s->intra_ac_vlc_length;
        last_length = s->intra_ac_vlc_last_length;
        bits       += s->luma_dc_vlc_length[temp[0] + 256]; // FIXME: chroma
    } else {
        start_i     = 0;
        length      = s->inter_ac_vlc_length;
        last_length = s->inter_ac_vlc_last_length;
    }

    if (last >= start_i) {
        run = 0;
        for (i = start_i; i < last; i++) {
            int j = scantable[i];
            level = temp[j];

            if (level) {
                level += 64;
                if ((level & (~127)) == 0)
                    bits += length[UNI_AC_ENC_INDEX(run, level)];
                else
                    bits += esc_length;
                run = 0;
            } else
                run++;
        }
        i = scantable[last];

        level = temp[i] + 64;

        av_assert2(level - 64);

        if ((level & (~127)) == 0) {
            bits += last_length[UNI_AC_ENC_INDEX(run, level)];
        } else
            bits += esc_length;
    }

    if (last >= 0) {
        if (s->mb_intra)
            s->dct_unquantize_intra(s, temp, 0, s->qscale);
        else
            s->dct_unquantize_inter(s, temp, 0, s->qscale);
    }

    s->idsp.idct_add(lsrc2, 8, temp);

    distortion = s->mecc.sse[1](NULL, lsrc2, lsrc1, 8, 8);

    return distortion + ((bits * s->qscale * s->qscale * 109 + 64) >> 7);
}

static int bit8x8_c(MpegEncContext *s, uint8_t *src1, uint8_t *src2,
                    ptrdiff_t stride, int h)
{
    const uint8_t *scantable = s->intra_scantable.permutated;
    LOCAL_ALIGNED_16(int16_t, temp, [64]);
    int i, last, run, bits, level, start_i;
    const int esc_length = s->ac_esc_length;
    uint8_t *length, *last_length;

    av_assert2(h == 8);

    s->pdsp.diff_pixels(temp, src1, src2, stride);

    s->block_last_index[0 /* FIXME */] =
    last                               =
        s->fast_dct_quantize(s, temp, 0 /* FIXME */, s->qscale, &i);

    bits = 0;

    if (s->mb_intra) {
        start_i     = 1;
        length      = s->intra_ac_vlc_length;
        last_length = s->intra_ac_vlc_last_length;
        bits       += s->luma_dc_vlc_length[temp[0] + 256]; // FIXME: chroma
    } else {
        start_i     = 0;
        length      = s->inter_ac_vlc_length;
        last_length = s->inter_ac_vlc_last_length;
    }

    if (last >= start_i) {
        run = 0;
        for (i = start_i; i < last; i++) {
            int j = scantable[i];
            level = temp[j];

            if (level) {
                level += 64;
                if ((level & (~127)) == 0)
                    bits += length[UNI_AC_ENC_INDEX(run, level)];
                else
                    bits += esc_length;
                run = 0;
            } else
                run++;
        }
        i = scantable[last];

        level = temp[i] + 64;

        av_assert2(level - 64);

        if ((level & (~127)) == 0)
            bits += last_length[UNI_AC_ENC_INDEX(run, level)];
        else
            bits += esc_length;
    }

    return bits;
}

#define VSAD_INTRA(size)                                                \
static int vsad_intra ## size ## _c(MpegEncContext *c,                  \
                                    uint8_t *s, uint8_t *dummy,         \
                                    ptrdiff_t stride, int h)            \
{                                                                       \
    int score = 0, x, y;                                                \
                                                                        \
    for (y = 1; y < h; y++) {                                           \
        for (x = 0; x < size; x += 4) {                                 \
            score += FFABS(s[x]     - s[x + stride])     +              \
                     FFABS(s[x + 1] - s[x + stride + 1]) +              \
                     FFABS(s[x + 2] - s[x + 2 + stride]) +              \
                     FFABS(s[x + 3] - s[x + 3 + stride]);               \
        }                                                               \
        s += stride;                                                    \
    }                                                                   \
                                                                        \
    return score;                                                       \
}
VSAD_INTRA(8)
VSAD_INTRA(16)

#define VSAD(size)                                                             \
static int vsad ## size ## _c(MpegEncContext *c,                               \
                              uint8_t *s1, uint8_t *s2,                        \
                              ptrdiff_t stride, int h)                               \
{                                                                              \
    int score = 0, x, y;                                                       \
                                                                               \
    for (y = 1; y < h; y++) {                                                  \
        for (x = 0; x < size; x++)                                             \
            score += FFABS(s1[x] - s2[x] - s1[x + stride] + s2[x + stride]);   \
        s1 += stride;                                                          \
        s2 += stride;                                                          \
    }                                                                          \
                                                                               \
    return score;                                                              \
}
VSAD(8)
VSAD(16)

#define SQ(a) ((a) * (a))
#define VSSE_INTRA(size)                                                \
static int vsse_intra ## size ## _c(MpegEncContext *c,                  \
                                    uint8_t *s, uint8_t *dummy,         \
                                    ptrdiff_t stride, int h)            \
{                                                                       \
    int score = 0, x, y;                                                \
                                                                        \
    for (y = 1; y < h; y++) {                                           \
        for (x = 0; x < size; x += 4) {                                 \
            score += SQ(s[x]     - s[x + stride]) +                     \
                     SQ(s[x + 1] - s[x + stride + 1]) +                 \
                     SQ(s[x + 2] - s[x + stride + 2]) +                 \
                     SQ(s[x + 3] - s[x + stride + 3]);                  \
        }                                                               \
        s += stride;                                                    \
    }                                                                   \
                                                                        \
    return score;                                                       \
}
VSSE_INTRA(8)
VSSE_INTRA(16)

#define VSSE(size)                                                             \
static int vsse ## size ## _c(MpegEncContext *c, uint8_t *s1, uint8_t *s2,     \
                              ptrdiff_t stride, int h)                         \
{                                                                              \
    int score = 0, x, y;                                                       \
                                                                               \
    for (y = 1; y < h; y++) {                                                  \
        for (x = 0; x < size; x++)                                             \
            score += SQ(s1[x] - s2[x] - s1[x + stride] + s2[x + stride]);      \
        s1 += stride;                                                          \
        s2 += stride;                                                          \
    }                                                                          \
                                                                               \
    return score;                                                              \
}
VSSE(8)
VSSE(16)

#define WRAPPER8_16_SQ(name8, name16)                                   \
static int name16(MpegEncContext *s, uint8_t *dst, uint8_t *src,        \
                  ptrdiff_t stride, int h)                              \
{                                                                       \
    int score = 0;                                                      \
                                                                        \
    score += name8(s, dst, src, stride, 8);                             \
    score += name8(s, dst + 8, src + 8, stride, 8);                     \
    if (h == 16) {                                                      \
        dst   += 8 * stride;                                            \
        src   += 8 * stride;                                            \
        score += name8(s, dst, src, stride, 8);                         \
        score += name8(s, dst + 8, src + 8, stride, 8);                 \
    }                                                                   \
    return score;                                                       \
}

WRAPPER8_16_SQ(hadamard8_diff8x8_c, hadamard8_diff16_c)
WRAPPER8_16_SQ(hadamard8_intra8x8_c, hadamard8_intra16_c)
WRAPPER8_16_SQ(dct_sad8x8_c, dct_sad16_c)
#if CONFIG_GPL
WRAPPER8_16_SQ(dct264_sad8x8_c, dct264_sad16_c)
#endif
WRAPPER8_16_SQ(dct_max8x8_c, dct_max16_c)
WRAPPER8_16_SQ(quant_psnr8x8_c, quant_psnr16_c)
WRAPPER8_16_SQ(rd8x8_c, rd16_c)
WRAPPER8_16_SQ(bit8x8_c, bit16_c)

av_cold void ff_me_cmp_init_static(void)
{
    int i;

    for (i = 0; i < 512; i++)
        ff_square_tab[i] = (i - 256) * (i - 256);
}

int ff_check_alignment(void)
{
    static int did_fail = 0;
    LOCAL_ALIGNED_16(int, aligned, [4]);

    if ((intptr_t)aligned & 15) {
        if (!did_fail) {
#if HAVE_MMX || HAVE_ALTIVEC
            av_log(NULL, AV_LOG_ERROR,
                "Compiler did not align stack variables. Libavcodec has been miscompiled\n"
                "and may be very slow or crash. This is not a bug in libavcodec,\n"
                "but in the compiler. You may try recompiling using gcc >= 4.2.\n"
                "Do not report crashes to FFmpeg developers.\n");
#endif
            did_fail=1;
        }
        return -1;
    }
    return 0;
}

av_cold void ff_me_cmp_init(MECmpContext *c, AVCodecContext *avctx)
{
    ff_check_alignment();

    c->sum_abs_dctelem = sum_abs_dctelem_c;

    /* TODO [0] 16  [1] 8 */
    c->pix_abs[0][0] = pix_abs16_c;
    c->pix_abs[0][1] = pix_abs16_x2_c;
    c->pix_abs[0][2] = pix_abs16_y2_c;
    c->pix_abs[0][3] = pix_abs16_xy2_c;
    c->pix_abs[1][0] = pix_abs8_c;
    c->pix_abs[1][1] = pix_abs8_x2_c;
    c->pix_abs[1][2] = pix_abs8_y2_c;
    c->pix_abs[1][3] = pix_abs8_xy2_c;

#define SET_CMP_FUNC(name)                      \
    c->name[0] = name ## 16_c;                  \
    c->name[1] = name ## 8x8_c;

    SET_CMP_FUNC(hadamard8_diff)
    c->hadamard8_diff[4] = hadamard8_intra16_c;
    c->hadamard8_diff[5] = hadamard8_intra8x8_c;
    SET_CMP_FUNC(dct_sad)
    SET_CMP_FUNC(dct_max)
#if CONFIG_GPL
    SET_CMP_FUNC(dct264_sad)
#endif
    c->sad[0] = pix_abs16_c;
    c->sad[1] = pix_abs8_c;
    c->sse[0] = sse16_c;
    c->sse[1] = sse8_c;
    c->sse[2] = sse4_c;
    SET_CMP_FUNC(quant_psnr)
    SET_CMP_FUNC(rd)
    SET_CMP_FUNC(bit)
    c->vsad[0] = vsad16_c;
    c->vsad[1] = vsad8_c;
    c->vsad[4] = vsad_intra16_c;
    c->vsad[5] = vsad_intra8_c;
    c->vsse[0] = vsse16_c;
    c->vsse[1] = vsse8_c;
    c->vsse[4] = vsse_intra16_c;
    c->vsse[5] = vsse_intra8_c;
    c->nsse[0] = nsse16_c;
    c->nsse[1] = nsse8_c;
#if CONFIG_SNOW_DECODER || CONFIG_SNOW_ENCODER
    ff_dsputil_init_dwt(c);
#endif

    if (ARCH_ALPHA)
        ff_me_cmp_init_alpha(c, avctx);
    if (ARCH_ARM)
        ff_me_cmp_init_arm(c, avctx);
    if (ARCH_PPC)
        ff_me_cmp_init_ppc(c, avctx);
    if (ARCH_X86)
        ff_me_cmp_init_x86(c, avctx);
    if (ARCH_MIPS)
        ff_me_cmp_init_mips(c, avctx);

    c->median_sad[0] = pix_median_abs16_c;
    c->median_sad[1] = pix_median_abs8_c;
}
