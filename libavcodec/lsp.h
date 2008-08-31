/*
 * LSP computing for ACELP-based codecs
 *
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

#ifndef AVCODEC_LSP_H
#define AVCODEC_LSP_H

#include <stdint.h>

/**
  (I.F) means fixed-point value with F fractional and I integer bits
*/

/**
 * \brief ensure a minimum distance between LSFs
 * \param lsfq [in/out] LSF to check and adjust
 * \param lsfq_min_distance minimum distance between LSFs
 * \param lsfq_min minimum allowed LSF value
 * \param lsfq_max maximum allowed LSF value
 * \param lp_order LP filter order
 */
void ff_acelp_reorder_lsf(int16_t* lsfq, int lsfq_min_distance, int lsfq_min, int lsfq_max, int lp_order);

/**
 * \brief Convert LSF to LSP
 * \param lsp [out] LSP coefficients (-0x8000 <= (0.15) < 0x8000)
 * \param lsf normalized LSF coefficients (0 <= (2.13) < 0x2000 * PI)
 * \param lp_order LP filter order
 *
 * \remark It is safe to pass the same array into the lsf and lsp parameters.
 */
void ff_acelp_lsf2lsp(int16_t *lsp, const int16_t *lsf, int lp_order);

/**
 * \brief LSP to LP conversion (3.2.6 of G.729)
 * \param lp [out] decoded LP coefficients (-0x8000 <= (3.12) < 0x8000)
 * \param lsp LSP coefficients (-0x8000 <= (0.15) < 0x8000)
 * \param lp_half_order LP filter order, divided by 2
 */
void ff_acelp_lsp2lpc(int16_t* lp, const int16_t* lsp, int lp_half_order);

/**
 * \brief Interpolate LSP for the first subframe and convert LSP -> LP for both subframes (3.2.5 and 3.2.6 of G.729)
 * \param lp_1st [out] decoded LP coefficients for first subframe (-0x8000 <= (3.12) < 0x8000)
 * \param lp_2nd [out] decoded LP coefficients for second subframe (-0x8000 <= (3.12) < 0x8000)
 * \param lsp_2nd LSP coefficients of the second subframe (-0x8000 <= (0.15) < 0x8000)
 * \param lsp_prev LSP coefficients from the second subframe of the previous frame (-0x8000 <= (0.15) < 0x8000)
 * \param lp_order LP filter order
 */
void ff_acelp_lp_decode(int16_t* lp_1st, int16_t* lp_2nd, const int16_t* lsp_2nd, const int16_t* lsp_prev, int lp_order);

#endif /* AVCODEC_LSP_H */
