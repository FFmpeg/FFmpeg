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

void ff_fft2_float_sse3     (AVTXContext *s, void *out, void *in, ptrdiff_t stride);
void ff_fft4_inv_float_sse2 (AVTXContext *s, void *out, void *in, ptrdiff_t stride);
void ff_fft4_fwd_float_sse2 (AVTXContext *s, void *out, void *in, ptrdiff_t stride);
void ff_fft8_float_sse3     (AVTXContext *s, void *out, void *in, ptrdiff_t stride);
void ff_fft8_float_avx      (AVTXContext *s, void *out, void *in, ptrdiff_t stride);
void ff_fft16_float_avx     (AVTXContext *s, void *out, void *in, ptrdiff_t stride);
void ff_fft16_float_fma3    (AVTXContext *s, void *out, void *in, ptrdiff_t stride);
void ff_fft32_float_avx     (AVTXContext *s, void *out, void *in, ptrdiff_t stride);
void ff_fft32_float_fma3    (AVTXContext *s, void *out, void *in, ptrdiff_t stride);

void ff_split_radix_fft_float_avx (AVTXContext *s, void *out, void *in, ptrdiff_t stride);
void ff_split_radix_fft_float_avx2(AVTXContext *s, void *out, void *in, ptrdiff_t stride);

av_cold void ff_tx_init_float_x86(AVTXContext *s, av_tx_fn *tx)
{
    int cpu_flags = av_get_cpu_flags();
    int gen_revtab = 0, basis, revtab_interleave;

    if (s->flags & AV_TX_UNALIGNED)
        return;

    if (ff_tx_type_is_mdct(s->type))
        return;

#define TXFN(fn, gentab, sr_basis, interleave) \
    do {                                       \
        *tx = fn;                              \
        gen_revtab = gentab;                   \
        basis = sr_basis;                      \
        revtab_interleave = interleave;        \
    } while (0)

    if (s->n == 1) {
        if (EXTERNAL_SSE2(cpu_flags)) {
            if (s->m == 4 && s->inv)
                TXFN(ff_fft4_inv_float_sse2, 0, 0, 0);
            else if (s->m == 4)
                TXFN(ff_fft4_fwd_float_sse2, 0, 0, 0);
        }

        if (EXTERNAL_SSE3(cpu_flags)) {
            if (s->m == 2)
                TXFN(ff_fft2_float_sse3, 0, 0, 0);
            else if (s->m == 8)
                TXFN(ff_fft8_float_sse3, 1, 8, 0);
        }

        if (EXTERNAL_AVX_FAST(cpu_flags)) {
            if (s->m == 8)
                TXFN(ff_fft8_float_avx, 1, 8, 0);
            else if (s->m == 16)
                TXFN(ff_fft16_float_avx, 1, 8, 2);
#if ARCH_X86_64
            else if (s->m == 32)
                TXFN(ff_fft32_float_avx, 1, 8, 2);
            else if (s->m >= 64 && s->m <= 131072 && !(s->flags & AV_TX_INPLACE))
                TXFN(ff_split_radix_fft_float_avx, 1, 8, 2);
#endif
        }

        if (EXTERNAL_FMA3_FAST(cpu_flags)) {
            if (s->m == 16)
                TXFN(ff_fft16_float_fma3, 1, 8, 2);
#if ARCH_X86_64
            else if (s->m == 32)
                TXFN(ff_fft32_float_fma3, 1, 8, 2);
#endif
        }

#if ARCH_X86_64
        if (EXTERNAL_AVX2_FAST(cpu_flags)) {
            if (s->m >= 64 && s->m <= 131072 && !(s->flags & AV_TX_INPLACE))
                TXFN(ff_split_radix_fft_float_avx2, 1, 8, 2);
        }
#endif
    }

    if (gen_revtab)
        ff_tx_gen_split_radix_parity_revtab(s->revtab, s->m, s->inv, basis,
                                            revtab_interleave);

#undef TXFN
}
