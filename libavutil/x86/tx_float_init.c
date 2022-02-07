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
#include "libavutil/x86/cpu.h"

#include "config.h"

TX_DECL_FN(fft2,      sse3)
TX_DECL_FN(fft4_fwd,  sse2)
TX_DECL_FN(fft4_inv,  sse2)
TX_DECL_FN(fft8,      sse3)
TX_DECL_FN(fft8_ns,   sse3)
TX_DECL_FN(fft8,      avx)
TX_DECL_FN(fft8_ns,   avx)
TX_DECL_FN(fft16,     avx)
TX_DECL_FN(fft16_ns,  avx)
TX_DECL_FN(fft16,     fma3)
TX_DECL_FN(fft16_ns,  fma3)
TX_DECL_FN(fft32,     avx)
TX_DECL_FN(fft32_ns,  avx)
TX_DECL_FN(fft32,     fma3)
TX_DECL_FN(fft32_ns,  fma3)
TX_DECL_FN(fft_sr,    avx)
TX_DECL_FN(fft_sr_ns, avx)
TX_DECL_FN(fft_sr,    avx2)
TX_DECL_FN(fft_sr_ns, avx2)

#define DECL_INIT_FN(basis, interleave)                                        \
static av_cold int b ##basis## _i ##interleave(AVTXContext *s,                 \
                                               const FFTXCodelet *cd,          \
                                               uint64_t flags,                 \
                                               FFTXCodeletOptions *opts,       \
                                               int len, int inv,               \
                                               const void *scale)              \
{                                                                              \
    const int inv_lookup = opts ? opts->invert_lookup : 1;                     \
    ff_tx_init_tabs_float(len);                                                \
    if (cd->max_len == 2)                                                      \
        return ff_tx_gen_ptwo_revtab(s, inv_lookup);                           \
    else                                                                       \
        return ff_tx_gen_split_radix_parity_revtab(s, inv_lookup,              \
                                                   basis, interleave);         \
}

DECL_INIT_FN(8, 0)
DECL_INIT_FN(8, 2)

const FFTXCodelet * const ff_tx_codelet_list_float_x86[] = {
    TX_DEF(fft2,     FFT,  2,  2, 2, 0, 128, NULL,  sse3, SSE3, AV_TX_INPLACE, 0),
    TX_DEF(fft2,     FFT,  2,  2, 2, 0, 192, b8_i0, sse3, SSE3, AV_TX_INPLACE | FF_TX_PRESHUFFLE, 0),
    TX_DEF(fft4_fwd, FFT,  4,  4, 2, 0, 128, NULL,  sse2, SSE2, AV_TX_INPLACE | FF_TX_FORWARD_ONLY, 0),
    TX_DEF(fft4_fwd, FFT,  4,  4, 2, 0, 192, b8_i0, sse2, SSE2, AV_TX_INPLACE | FF_TX_PRESHUFFLE, 0),
    TX_DEF(fft4_inv, FFT,  4,  4, 2, 0, 128, NULL,  sse2, SSE2, AV_TX_INPLACE | FF_TX_INVERSE_ONLY, 0),
    TX_DEF(fft8,     FFT,  8,  8, 2, 0, 128, b8_i0, sse3, SSE3, AV_TX_INPLACE, 0),
    TX_DEF(fft8_ns,  FFT,  8,  8, 2, 0, 192, b8_i0, sse3, SSE3, AV_TX_INPLACE | FF_TX_PRESHUFFLE, 0),
    TX_DEF(fft8,     FFT,  8,  8, 2, 0, 256, b8_i0, avx,  AVX,  AV_TX_INPLACE, 0),
    TX_DEF(fft8_ns,  FFT,  8,  8, 2, 0, 320, b8_i0, avx,  AVX,  AV_TX_INPLACE | FF_TX_PRESHUFFLE, 0),
    TX_DEF(fft16,    FFT, 16, 16, 2, 0, 256, b8_i2, avx,  AVX,  AV_TX_INPLACE, 0),
    TX_DEF(fft16_ns, FFT, 16, 16, 2, 0, 320, b8_i2, avx,  AVX,  AV_TX_INPLACE | FF_TX_PRESHUFFLE, 0),
    TX_DEF(fft16,    FFT, 16, 16, 2, 0, 288, b8_i2, fma3, FMA3, AV_TX_INPLACE, 0),
    TX_DEF(fft16_ns, FFT, 16, 16, 2, 0, 352, b8_i2, fma3, FMA3, AV_TX_INPLACE | FF_TX_PRESHUFFLE, 0),

#if ARCH_X86_64
    TX_DEF(fft32,    FFT, 32, 32, 2, 0, 256, b8_i2, avx,  AVX,  AV_TX_INPLACE, 0),
    TX_DEF(fft32_ns, FFT, 32, 32, 2, 0, 320, b8_i2, avx,  AVX,  AV_TX_INPLACE | FF_TX_PRESHUFFLE, 0),
    TX_DEF(fft32,    FFT, 32, 32, 2, 0, 288, b8_i2, fma3, FMA3, AV_TX_INPLACE, 0),
    TX_DEF(fft32_ns, FFT, 32, 32, 2, 0, 352, b8_i2, fma3, FMA3, AV_TX_INPLACE | FF_TX_PRESHUFFLE, 0),
#if HAVE_AVX2_EXTERNAL
    TX_DEF(fft_sr,    FFT, 64, 131072, 2, 0, 256, b8_i2, avx,  AVX,  0, 0),
    TX_DEF(fft_sr_ns, FFT, 64, 131072, 2, 0, 320, b8_i2, avx,  AVX,  AV_TX_INPLACE | FF_TX_PRESHUFFLE, 0),
    TX_DEF(fft_sr,    FFT, 64, 131072, 2, 0, 288, b8_i2, avx2, AVX2, 0, AV_CPU_FLAG_AVXSLOW),
    TX_DEF(fft_sr_ns, FFT, 64, 131072, 2, 0, 352, b8_i2, avx2, AVX2, AV_TX_INPLACE | FF_TX_PRESHUFFLE,
           AV_CPU_FLAG_AVXSLOW),
#endif
#endif

    NULL,
};
