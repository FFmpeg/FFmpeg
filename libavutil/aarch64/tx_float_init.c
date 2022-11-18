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

#define TX_FLOAT
#include "libavutil/tx_priv.h"
#include "libavutil/attributes.h"
#include "libavutil/aarch64/cpu.h"

TX_DECL_FN(fft2,      neon)
TX_DECL_FN(fft4_fwd,  neon)
TX_DECL_FN(fft4_inv,  neon)
TX_DECL_FN(fft8,      neon)
TX_DECL_FN(fft8_ns,   neon)
TX_DECL_FN(fft16,     neon)
TX_DECL_FN(fft16_ns,  neon)
TX_DECL_FN(fft32,     neon)
TX_DECL_FN(fft32_ns,  neon)
TX_DECL_FN(fft_sr,    neon)
TX_DECL_FN(fft_sr_ns, neon)

static av_cold int neon_init(AVTXContext *s, const FFTXCodelet *cd,
                             uint64_t flags, FFTXCodeletOptions *opts,
                             int len, int inv, const void *scale)
{
    ff_tx_init_tabs_float(len);
    if (cd->max_len == 2)
        return ff_tx_gen_ptwo_revtab(s, opts);
    else
        return ff_tx_gen_split_radix_parity_revtab(s, len, inv, opts, 8, 0);
}

const FFTXCodelet * const ff_tx_codelet_list_float_aarch64[] = {
    TX_DEF(fft2,      FFT,  2,  2, 2, 0, 128, NULL,      neon, NEON, AV_TX_INPLACE, 0),
    TX_DEF(fft2,      FFT,  2,  2, 2, 0, 192, neon_init, neon, NEON, AV_TX_INPLACE | FF_TX_PRESHUFFLE, 0),
    TX_DEF(fft4_fwd,  FFT,  4,  4, 2, 0, 128, NULL,      neon, NEON, AV_TX_INPLACE | FF_TX_FORWARD_ONLY, 0),
    TX_DEF(fft4_fwd,  FFT,  4,  4, 2, 0, 192, neon_init, neon, NEON, AV_TX_INPLACE | FF_TX_PRESHUFFLE, 0),
    TX_DEF(fft4_inv,  FFT,  4,  4, 2, 0, 128, NULL,      neon, NEON, AV_TX_INPLACE | FF_TX_INVERSE_ONLY, 0),
    TX_DEF(fft8,      FFT,  8,  8, 2, 0, 128, neon_init, neon, NEON, AV_TX_INPLACE, 0),
    TX_DEF(fft8_ns,   FFT,  8,  8, 2, 0, 192, neon_init, neon, NEON, AV_TX_INPLACE | FF_TX_PRESHUFFLE, 0),
    TX_DEF(fft16,     FFT, 16, 16, 2, 0, 128, neon_init, neon, NEON, AV_TX_INPLACE, 0),
    TX_DEF(fft16_ns,  FFT, 16, 16, 2, 0, 192, neon_init, neon, NEON, AV_TX_INPLACE | FF_TX_PRESHUFFLE, 0),
    TX_DEF(fft32,     FFT, 32, 32, 2, 0, 128, neon_init, neon, NEON, AV_TX_INPLACE, 0),
    TX_DEF(fft32_ns,  FFT, 32, 32, 2, 0, 192, neon_init, neon, NEON, AV_TX_INPLACE | FF_TX_PRESHUFFLE, 0),

    TX_DEF(fft_sr,    FFT, 64, 131072, 2, 0, 128, neon_init, neon, NEON, 0, 0),
    TX_DEF(fft_sr_ns, FFT, 64, 131072, 2, 0, 192, neon_init, neon, NEON, AV_TX_INPLACE | FF_TX_PRESHUFFLE, 0),

    NULL,
};
