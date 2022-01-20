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
    return ff_tx_gen_split_radix_parity_revtab(s, inv_lookup,                  \
                                               basis, interleave);             \
}

#define ff_tx_fft_sr_codelet_init_b0_i0_x86 NULL
DECL_INIT_FN(8, 0)
DECL_INIT_FN(8, 2)

#define DECL_SR_CD_DEF(fn_name, len, init_fn, fn_prio, cpu, fn_flags)          \
void ff_tx_ ##fn_name(AVTXContext *s, void *out, void *in, ptrdiff_t stride);  \
static const FFTXCodelet ff_tx_ ##fn_name## _def = {                           \
    .name       = #fn_name,                                                    \
    .function   = ff_tx_ ##fn_name,                                            \
    .type       = TX_TYPE(FFT),                                                \
    .flags      = FF_TX_OUT_OF_PLACE | FF_TX_ALIGNED | fn_flags,               \
    .factors[0] = 2,                                                           \
    .min_len    = len,                                                         \
    .max_len    = len,                                                         \
    .init       = ff_tx_fft_sr_codelet_init_ ##init_fn## _x86,                 \
    .cpu_flags  = AV_CPU_FLAG_ ##cpu,                                          \
    .prio       = fn_prio,                                                     \
};

DECL_SR_CD_DEF(fft2_float_sse3,      2, b0_i0, 128, SSE3, AV_TX_INPLACE)
DECL_SR_CD_DEF(fft4_fwd_float_sse2,  4, b0_i0, 128, SSE2, AV_TX_INPLACE | FF_TX_FORWARD_ONLY)
DECL_SR_CD_DEF(fft4_inv_float_sse2,  4, b0_i0, 128, SSE2, AV_TX_INPLACE | FF_TX_INVERSE_ONLY)
DECL_SR_CD_DEF(fft8_float_sse3,      8, b8_i0, 128, SSE3, AV_TX_INPLACE)
DECL_SR_CD_DEF(fft8_float_avx,       8, b8_i0, 256, AVX,  AV_TX_INPLACE)
DECL_SR_CD_DEF(fft16_float_avx,     16, b8_i2, 256, AVX,  AV_TX_INPLACE)
DECL_SR_CD_DEF(fft16_float_fma3,    16, b8_i2, 288, FMA3, AV_TX_INPLACE)

#if ARCH_X86_64
DECL_SR_CD_DEF(fft32_float_avx,     32, b8_i2, 256, AVX,  AV_TX_INPLACE)
DECL_SR_CD_DEF(fft32_float_fma3,    32, b8_i2, 288, FMA3, AV_TX_INPLACE)

void ff_tx_fft_sr_float_avx(AVTXContext *s, void *out, void *in, ptrdiff_t stride);
const FFTXCodelet ff_tx_fft_sr_float_avx_def = {
    .name       = "fft_sr_float_avx",
    .function   = ff_tx_fft_sr_float_avx,
    .type       = TX_TYPE(FFT),
    .flags      = FF_TX_ALIGNED | FF_TX_OUT_OF_PLACE,
    .factors[0] = 2,
    .min_len    = 64,
    .max_len    = 131072,
    .init       = ff_tx_fft_sr_codelet_init_b8_i2_x86,
    .cpu_flags  = AV_CPU_FLAG_AVX,
    .prio       = 256,
};

#if HAVE_AVX2_EXTERNAL
void ff_tx_fft_sr_float_avx2(AVTXContext *s, void *out, void *in, ptrdiff_t stride);
const FFTXCodelet ff_tx_fft_sr_float_avx2_def = {
    .name       = "fft_sr_float_avx2",
    .function   = ff_tx_fft_sr_float_avx2,
    .type       = TX_TYPE(FFT),
    .flags      = FF_TX_ALIGNED | FF_TX_OUT_OF_PLACE,
    .factors[0] = 2,
    .min_len    = 64,
    .max_len    = 131072,
    .init       = ff_tx_fft_sr_codelet_init_b8_i2_x86,
    .cpu_flags  = AV_CPU_FLAG_AVX2,
    .prio       = 288,
};
#endif
#endif

const FFTXCodelet * const ff_tx_codelet_list_float_x86[] = {
    /* Split-Radix codelets */
    &ff_tx_fft2_float_sse3_def,
    &ff_tx_fft4_fwd_float_sse2_def,
    &ff_tx_fft4_inv_float_sse2_def,
    &ff_tx_fft8_float_sse3_def,
    &ff_tx_fft8_float_avx_def,
    &ff_tx_fft16_float_avx_def,
    &ff_tx_fft16_float_fma3_def,

#if ARCH_X86_64
    &ff_tx_fft32_float_avx_def,
    &ff_tx_fft32_float_fma3_def,

    /* Standalone transforms */
    &ff_tx_fft_sr_float_avx_def,
#if HAVE_AVX2_EXTERNAL
    &ff_tx_fft_sr_float_avx2_def,
#endif
#endif

    NULL,
};
