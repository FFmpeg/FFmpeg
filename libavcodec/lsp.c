/*
 * LSP routines for ACELP-based codecs
 *
 * Copyright (c) 2007 Reynaldo H. Verdejo Pinochet (QCELP decoder)
 * Copyright (c) 2008 Vladimir Voroshilov
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

#include <math.h>

#include "config.h"

#define FRAC_BITS 14
#include "libavutil/macros.h"
#include "mathops.h"
#include "lsp.h"
#if ARCH_MIPS
#include "libavcodec/mips/lsp_mips.h"
#endif /* ARCH_MIPS */
#include "libavutil/avassert.h"

void ff_acelp_reorder_lsf(int16_t* lsfq, int lsfq_min_distance, int lsfq_min, int lsfq_max, int lp_order)
{
    int i, j;

    /* sort lsfq in ascending order. float bubble algorithm,
       O(n) if data already sorted, O(n^2) - otherwise */
    for(i=0; i<lp_order-1; i++)
        for(j=i; j>=0 && lsfq[j] > lsfq[j+1]; j--)
            FFSWAP(int16_t, lsfq[j], lsfq[j+1]);

    for(i=0; i<lp_order; i++)
    {
        lsfq[i] = FFMAX(lsfq[i], lsfq_min);
        lsfq_min = lsfq[i] + lsfq_min_distance;
    }
    lsfq[lp_order-1] = FFMIN(lsfq[lp_order-1], lsfq_max);//Is warning required ?
}

void ff_set_min_dist_lsf(float *lsf, double min_spacing, int size)
{
    int i;
    float prev = 0.0;
    for (i = 0; i < size; i++)
        prev = lsf[i] = FFMAX(lsf[i], prev + min_spacing);
}


/* Cosine table: base_cos[i] = (1 << 15) * cos(i * PI / 64) */
static const int16_t tab_cos[65] =
{
  32767,  32738,  32617,  32421,  32145,  31793,  31364,  30860,
  30280,  29629,  28905,  28113,  27252,  26326,  25336,  24285,
  23176,  22011,  20793,  19525,  18210,  16851,  15451,  14014,
  12543,  11043,   9515,   7965,   6395,   4810,   3214,   1609,
      1,  -1607,  -3211,  -4808,  -6393,  -7962,  -9513, -11040,
 -12541, -14012, -15449, -16848, -18207, -19523, -20791, -22009,
 -23174, -24283, -25334, -26324, -27250, -28111, -28904, -29627,
 -30279, -30858, -31363, -31792, -32144, -32419, -32616, -32736, -32768,
};

static int16_t ff_cos(uint16_t arg)
{
    uint8_t offset= arg;
    uint8_t ind = arg >> 8;

    av_assert2(arg <= 0x3fff);

    return tab_cos[ind] + (offset * (tab_cos[ind+1] - tab_cos[ind]) >> 8);
}

void ff_acelp_lsf2lsp(int16_t *lsp, const int16_t *lsf, int lp_order)
{
    int i;

    /* Convert LSF to LSP, lsp=cos(lsf) */
    for(i=0; i<lp_order; i++)
        // 20861 = 2.0 / PI in (0.15)
        lsp[i] = ff_cos(lsf[i] * 20861 >> 15); // divide by PI and (0,13) -> (0,14)
}

void ff_acelp_lsf2lspd(double *lsp, const float *lsf, int lp_order)
{
    int i;

    for(i = 0; i < lp_order; i++)
        lsp[i] = cos(2.0 * M_PI * lsf[i]);
}

/**
 * @brief decodes polynomial coefficients from LSP
 * @param[out] f decoded polynomial coefficients (-0x20000000 <= (3.22) <= 0x1fffffff)
 * @param lsp LSP coefficients (-0x8000 <= (0.15) <= 0x7fff)
 */
static void lsp2poly(int* f, const int16_t* lsp, int lp_half_order)
{
    int i, j;

    f[0] = 0x400000;          // 1.0 in (3.22)
    f[1] = -lsp[0] * 256;     // *2 and (0.15) -> (3.22)

    for(i=2; i<=lp_half_order; i++)
    {
        f[i] = f[i-2];
        for(j=i; j>1; j--)
            f[j] -= MULL(f[j-1], lsp[2*i-2], FRAC_BITS) - f[j-2];

        f[1] -= lsp[2*i-2] * 256;
    }
}

#ifndef lsp2polyf
/**
 * Compute the Pa / (1 + z(-1)) or Qa / (1 - z(-1)) coefficients
 * needed for LSP to LPC conversion.
 * We only need to calculate the 6 first elements of the polynomial.
 *
 * @param lsp line spectral pairs in cosine domain
 * @param[out] f polynomial input/output as a vector
 *
 * TIA/EIA/IS-733 2.4.3.3.5-1/2
 */
static void lsp2polyf(const double *lsp, double *f, int lp_half_order)
{
    f[0] = 1.0;
    f[1] = -2 * lsp[0];
    lsp -= 2;
    for (int i = 2; i <= lp_half_order; i++) {
        double val = -2 * lsp[2*i];
        f[i] = val * f[i-1] + 2*f[i-2];
        for (int j = i-1; j > 1; j--)
            f[j] += f[j-1] * val + f[j-2];
        f[1] += val;
    }
}
#endif /* lsp2polyf */

/**
 * @brief LSP to LP conversion (3.2.6 of G.729)
 * @param[out] lp decoded LP coefficients (-0x8000 <= (3.12) < 0x8000)
 * @param lsp LSP coefficients (-0x8000 <= (0.15) < 0x8000)
 * @param lp_half_order LP filter order, divided by 2
 */
static void acelp_lsp2lpc(int16_t lp[], const int16_t lsp[], int lp_half_order)
{
    int i;
    int f1[MAX_LP_HALF_ORDER+1]; // (3.22)
    int f2[MAX_LP_HALF_ORDER+1]; // (3.22)

    lsp2poly(f1, lsp  , lp_half_order);
    lsp2poly(f2, lsp+1, lp_half_order);

    /* 3.2.6 of G.729, Equations 25 and  26*/
    lp[0] = 4096;
    for(i=1; i<lp_half_order+1; i++)
    {
        int ff1 = f1[i] + f1[i-1]; // (3.22)
        int ff2 = f2[i] - f2[i-1]; // (3.22)

        ff1 += 1 << 10; // for rounding
        lp[i]    = (ff1 + ff2) >> 11; // divide by 2 and (3.22) -> (3.12)
        lp[(lp_half_order << 1) + 1 - i] = (ff1 - ff2) >> 11; // divide by 2 and (3.22) -> (3.12)
    }
}

void ff_amrwb_lsp2lpc(const double *lsp, float *lp, int lp_order)
{
    int lp_half_order = lp_order >> 1;
    double buf[MAX_LP_HALF_ORDER + 1];
    double pa[MAX_LP_HALF_ORDER + 1];
    double *qa = buf + 1;
    int i,j;

    qa[-1] = 0.0;

    lsp2polyf(lsp    , pa, lp_half_order    );
    lsp2polyf(lsp + 1, qa, lp_half_order - 1);

    for (i = 1, j = lp_order - 1; i < lp_half_order; i++, j--) {
        double paf =  pa[i]            * (1 + lsp[lp_order - 1]);
        double qaf = (qa[i] - qa[i-2]) * (1 - lsp[lp_order - 1]);
        lp[i-1]  = (paf + qaf) * 0.5;
        lp[j-1]  = (paf - qaf) * 0.5;
    }

    lp[lp_half_order - 1] = (1.0 + lsp[lp_order - 1]) *
        pa[lp_half_order] * 0.5;

    lp[lp_order - 1] = lsp[lp_order - 1];
}

void ff_acelp_lp_decode(int16_t* lp_1st, int16_t* lp_2nd, const int16_t* lsp_2nd, const int16_t* lsp_prev, int lp_order)
{
    int16_t lsp_1st[MAX_LP_ORDER]; // (0.15)
    int i;

    /* LSP values for first subframe (3.2.5 of G.729, Equation 24)*/
    for(i=0; i<lp_order; i++)
#ifdef G729_BITEXACT
        lsp_1st[i] = (lsp_2nd[i] >> 1) + (lsp_prev[i] >> 1);
#else
        lsp_1st[i] = (lsp_2nd[i] + lsp_prev[i]) >> 1;
#endif

    acelp_lsp2lpc(lp_1st, lsp_1st, lp_order >> 1);

    /* LSP values for second subframe (3.2.5 of G.729)*/
    acelp_lsp2lpc(lp_2nd, lsp_2nd, lp_order >> 1);
}

void ff_acelp_lspd2lpc(const double *lsp, float *lpc, int lp_half_order)
{
    double pa[MAX_LP_HALF_ORDER+1], qa[MAX_LP_HALF_ORDER+1];
    float *lpc2 = lpc + (lp_half_order << 1) - 1;

    av_assert2(lp_half_order <= MAX_LP_HALF_ORDER);

    lsp2polyf(lsp,     pa, lp_half_order);
    lsp2polyf(lsp + 1, qa, lp_half_order);

    while (lp_half_order--) {
        double paf = pa[lp_half_order+1] + pa[lp_half_order];
        double qaf = qa[lp_half_order+1] - qa[lp_half_order];

        lpc [ lp_half_order] = 0.5*(paf+qaf);
        lpc2[-lp_half_order] = 0.5*(paf-qaf);
    }
}

void ff_sort_nearly_sorted_floats(float *vals, int len)
{
    int i,j;

    for (i = 0; i < len - 1; i++)
        for (j = i; j >= 0 && vals[j] > vals[j+1]; j--)
            FFSWAP(float, vals[j], vals[j+1]);
}
