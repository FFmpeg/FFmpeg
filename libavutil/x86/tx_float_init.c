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

/* These versions already do what we need them to do. */
#define ff_tx_fft2_ns_float_sse3 ff_tx_fft2_float_sse3
#define ff_tx_fft4_ns_float_sse2 ff_tx_fft4_fwd_float_sse2

#define DECL_INIT_FN(basis, interleave)                                        \
static av_cold int                                                             \
    ff_tx_fft_sr_codelet_init_b ##basis## _i ##interleave## _x86               \
    (AVTXContext *s,                                                           \
     const FFTXCodelet *cd,                                                    \
     uint64_t flags,                                                           \
     FFTXCodeletOptions *opts,                                                 \
     int len, int inv,                                                         \
     const void *scale)                                                        \
{                                                                              \
    const int inv_lookup = opts ? opts->invert_lookup : 1;                     \
    ff_tx_init_tabs_float(len);                                                \
    if (cd->max_len == 2)                                                      \
        return ff_tx_gen_ptwo_revtab(s, inv_lookup);                           \
    else                                                                       \
        return ff_tx_gen_split_radix_parity_revtab(s, inv_lookup,              \
                                                   basis, interleave);         \
}

#define ff_tx_fft_sr_codelet_init_b0_i0_x86 NULL
DECL_INIT_FN(8, 0)
DECL_INIT_FN(8, 2)

#define DECL_CD_DEF(fn, t, min, max, f1, f2, i, p, c, f)                       \
void ff_tx_ ##fn(AVTXContext *s, void *out, void *in, ptrdiff_t stride);       \
static const FFTXCodelet ff_tx_ ##fn## _def = {                                \
    .name       = #fn,                                                         \
    .function   = ff_tx_ ##fn,                                                 \
    .type       = TX_TYPE(t),                                                  \
    .flags      = FF_TX_ALIGNED | f,                                           \
    .factors    = { f1, f2 },                                                  \
    .min_len    = min,                                                         \
    .max_len    = max,                                                         \
    .init       = ff_tx_ ##i## _x86,                                           \
    .cpu_flags  = c,                                                           \
    .prio       = p,                                                           \
};

#define DECL_SR_CD_DEF(fn_name, len, init_fn, fn_prio, cpu, fn_flags)          \
    DECL_CD_DEF(fn_name, FFT, len, len, 2, 0,                                  \
                fft_sr_codelet_init_ ##init_fn, fn_prio,                       \
                AV_CPU_FLAG_ ##cpu, FF_TX_OUT_OF_PLACE | fn_flags)

DECL_SR_CD_DEF(fft2_float_sse3,      2, b0_i0, 128, SSE3, AV_TX_INPLACE)
DECL_SR_CD_DEF(fft2_ns_float_sse3,   2, b8_i0, 192, SSE3, AV_TX_INPLACE | FF_TX_PRESHUFFLE)
DECL_SR_CD_DEF(fft4_fwd_float_sse2,  4, b0_i0, 128, SSE2, AV_TX_INPLACE | FF_TX_FORWARD_ONLY)
DECL_SR_CD_DEF(fft4_inv_float_sse2,  4, b0_i0, 128, SSE2, AV_TX_INPLACE | FF_TX_INVERSE_ONLY)
DECL_SR_CD_DEF(fft4_ns_float_sse2,   4, b8_i0, 192, SSE2, AV_TX_INPLACE | FF_TX_PRESHUFFLE)
DECL_SR_CD_DEF(fft8_float_sse3,      8, b8_i0, 128, SSE3, AV_TX_INPLACE)
DECL_SR_CD_DEF(fft8_ns_float_sse3,   8, b8_i0, 192, SSE3, AV_TX_INPLACE | FF_TX_PRESHUFFLE)
DECL_SR_CD_DEF(fft8_float_avx,       8, b8_i0, 256, AVX,  AV_TX_INPLACE)
DECL_SR_CD_DEF(fft8_ns_float_avx,    8, b8_i0, 320, AVX,  AV_TX_INPLACE | FF_TX_PRESHUFFLE)
DECL_SR_CD_DEF(fft16_float_avx,     16, b8_i2, 256, AVX,  AV_TX_INPLACE)
DECL_SR_CD_DEF(fft16_ns_float_avx,  16, b8_i2, 320, AVX,  AV_TX_INPLACE | FF_TX_PRESHUFFLE)
DECL_SR_CD_DEF(fft16_float_fma3,    16, b8_i2, 288, FMA3, AV_TX_INPLACE)
DECL_SR_CD_DEF(fft16_ns_float_fma3, 16, b8_i2, 352, FMA3, AV_TX_INPLACE | FF_TX_PRESHUFFLE)

#if ARCH_X86_64
DECL_SR_CD_DEF(fft32_float_avx,     32, b8_i2, 256, AVX,  AV_TX_INPLACE)
DECL_SR_CD_DEF(fft32_ns_float_avx,  32, b8_i2, 320, AVX,  AV_TX_INPLACE | FF_TX_PRESHUFFLE)
DECL_SR_CD_DEF(fft32_float_fma3,    32, b8_i2, 288, FMA3, AV_TX_INPLACE)
DECL_SR_CD_DEF(fft32_ns_float_fma3, 32, b8_i2, 352, FMA3, AV_TX_INPLACE | FF_TX_PRESHUFFLE)

DECL_CD_DEF(fft_sr_float_avx, FFT, 64, 131072, 2, 0, fft_sr_codelet_init_b8_i2,
            256, AV_CPU_FLAG_AVX,
            FF_TX_OUT_OF_PLACE)

DECL_CD_DEF(fft_sr_ns_float_avx, FFT, 64, 131072, 2, 0, fft_sr_codelet_init_b8_i2,
            320, AV_CPU_FLAG_AVX,
            FF_TX_OUT_OF_PLACE | AV_TX_INPLACE | FF_TX_PRESHUFFLE)

#if HAVE_AVX2_EXTERNAL
DECL_CD_DEF(fft_sr_float_avx2, FFT, 64, 131072, 2, 0, fft_sr_codelet_init_b8_i2,
            288, AV_CPU_FLAG_AVX2 | AV_CPU_FLAG_AVXSLOW,
            FF_TX_OUT_OF_PLACE)

DECL_CD_DEF(fft_sr_ns_float_avx2, FFT, 64, 131072, 2, 0, fft_sr_codelet_init_b8_i2,
            352, AV_CPU_FLAG_AVX2 | AV_CPU_FLAG_AVXSLOW,
            FF_TX_OUT_OF_PLACE | AV_TX_INPLACE | FF_TX_PRESHUFFLE)
#endif
#endif

const FFTXCodelet * const ff_tx_codelet_list_float_x86[] = {
    &ff_tx_fft2_float_sse3_def,
    &ff_tx_fft2_ns_float_sse3_def,
    &ff_tx_fft4_fwd_float_sse2_def,
    &ff_tx_fft4_inv_float_sse2_def,
    &ff_tx_fft4_ns_float_sse2_def,
    &ff_tx_fft8_float_sse3_def,
    &ff_tx_fft8_ns_float_sse3_def,
    &ff_tx_fft8_float_avx_def,
    &ff_tx_fft8_ns_float_avx_def,
    &ff_tx_fft16_float_avx_def,
    &ff_tx_fft16_ns_float_avx_def,
    &ff_tx_fft16_float_fma3_def,
    &ff_tx_fft16_ns_float_fma3_def,

#if ARCH_X86_64
    &ff_tx_fft32_float_avx_def,
    &ff_tx_fft32_ns_float_avx_def,
    &ff_tx_fft32_float_fma3_def,
    &ff_tx_fft32_ns_float_fma3_def,

    &ff_tx_fft_sr_float_avx_def,
    &ff_tx_fft_sr_ns_float_avx_def,
#if HAVE_AVX2_EXTERNAL
    &ff_tx_fft_sr_float_avx2_def,
    &ff_tx_fft_sr_ns_float_avx2_def,
#endif
#endif

    NULL,
};
