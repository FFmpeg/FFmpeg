/*
 * QCELP decoder
 * Copyright (c) 2007 Reynaldo H. Verdejo Pinochet
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
 * @file libavcodec/qcelp_lsp.c
 * QCELP decoder
 * @author Reynaldo H. Verdejo Pinochet
 * @remark FFmpeg merging spearheaded by Kenan Gillet
 * @remark Development mentored by Benjamin Larson
 */

#include "libavutil/mathematics.h"

/**
 * Computes the Pa / (1 + z(-1)) or Qa / (1 - z(-1)) coefficients
 * needed for LSP to LPC conversion.
 * We only need to calculate the 6 first elements of the polynomial.
 *
 * @param lspf line spectral pair frequencies
 * @param f [out] polynomial input/output as a vector
 *
 * TIA/EIA/IS-733 2.4.3.3.5-1/2
 */
static void lsp2polyf(const double *lspf, double *f, int lp_half_order)
{
    int i, j;

    f[0] = 1.0;
    f[1] = -2 * lspf[0];
    lspf -= 2;
    for(i=2; i<=lp_half_order; i++)
    {
        double val = -2 * lspf[2*i];
        f[i] = val * f[i-1] + 2*f[i-2];
        for(j=i-1; j>1; j--)
            f[j] += f[j-1] * val + f[j-2];
        f[1] += val;
    }
}

/**
 * Reconstructs LPC coefficients from the line spectral pair frequencies.
 *
 * @param lspf line spectral pair frequencies
 * @param lpc linear predictive coding coefficients
 */
void ff_celp_lspf2lpc(const double *lspf, float *lpc)
{
    double pa[6], qa[6];
    int   i;

    lsp2polyf(lspf,     pa, 5);
    lsp2polyf(lspf + 1, qa, 5);

    for (i=4; i>=0; i--)
    {
        double paf = pa[i+1] + pa[i];
        double qaf = qa[i+1] - qa[i];

        lpc[i  ] = 0.5*(paf+qaf);
        lpc[9-i] = 0.5*(paf-qaf);
    }
}
