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

#include "tx_priv.h"

int ff_tx_type_is_mdct(enum AVTXType type)
{
    switch (type) {
    case AV_TX_FLOAT_MDCT:
    case AV_TX_DOUBLE_MDCT:
    case AV_TX_INT32_MDCT:
        return 1;
    default:
        return 0;
    }
}

/* Calculates the modular multiplicative inverse */
static av_always_inline int mulinv(int n, int m)
{
    n = n % m;
    for (int x = 1; x < m; x++)
        if (((n * x) % m) == 1)
            return x;
    av_assert0(0); /* Never reached */
    return 0;
}

/* Guaranteed to work for any n, m where gcd(n, m) == 1 */
int ff_tx_gen_compound_mapping(AVTXContext *s)
{
    int *in_map, *out_map;
    const int n     = s->n;
    const int m     = s->m;
    const int inv   = s->inv;
    const int len   = n*m;
    const int m_inv = mulinv(m, n);
    const int n_inv = mulinv(n, m);
    const int mdct  = ff_tx_type_is_mdct(s->type);

    if (!(s->pfatab = av_malloc(2*len*sizeof(*s->pfatab))))
        return AVERROR(ENOMEM);

    in_map  = s->pfatab;
    out_map = s->pfatab + n*m;

    /* Ruritanian map for input, CRT map for output, can be swapped */
    for (int j = 0; j < m; j++) {
        for (int i = 0; i < n; i++) {
            /* Shifted by 1 to simplify MDCTs */
            in_map[j*n + i] = ((i*m + j*n) % len) << mdct;
            out_map[(i*m*m_inv + j*n*n_inv) % len] = i*m + j;
        }
    }

    /* Change transform direction by reversing all ACs */
    if (inv) {
        for (int i = 0; i < m; i++) {
            int *in = &in_map[i*n + 1]; /* Skip the DC */
            for (int j = 0; j < ((n - 1) >> 1); j++)
                FFSWAP(int, in[j], in[n - j - 2]);
        }
    }

    /* Our 15-point transform is also a compound one, so embed its input map */
    if (n == 15) {
        for (int k = 0; k < m; k++) {
            int tmp[15];
            memcpy(tmp, &in_map[k*15], 15*sizeof(*tmp));
            for (int i = 0; i < 5; i++) {
                for (int j = 0; j < 3; j++)
                    in_map[k*15 + i*3 + j] = tmp[(i*3 + j*5) % 15];
            }
        }
    }

    return 0;
}

static inline int split_radix_permutation(int i, int m, int inverse)
{
    m >>= 1;
    if (m <= 1)
        return i & 1;
    if (!(i & m))
        return split_radix_permutation(i, m, inverse) * 2;
    m >>= 1;
    return split_radix_permutation(i, m, inverse) * 4 + 1 - 2*(!(i & m) ^ inverse);
}

int ff_tx_gen_ptwo_revtab(AVTXContext *s, int invert_lookup)
{
    const int m = s->m, inv = s->inv;

    if (!(s->revtab = av_malloc(s->m*sizeof(*s->revtab))))
        return AVERROR(ENOMEM);
    if (!(s->revtab_c = av_malloc(m*sizeof(*s->revtab_c))))
        return AVERROR(ENOMEM);

    /* Default */
    for (int i = 0; i < m; i++) {
        int k = -split_radix_permutation(i, m, inv) & (m - 1);
        if (invert_lookup)
            s->revtab[i] = s->revtab_c[i] = k;
        else
            s->revtab[i] = s->revtab_c[k] = i;
    }

    return 0;
}

int ff_tx_gen_ptwo_inplace_revtab_idx(AVTXContext *s, int *revtab)
{
    int nb_inplace_idx = 0;

    if (!(s->inplace_idx = av_malloc(s->m*sizeof(*s->inplace_idx))))
        return AVERROR(ENOMEM);

    /* The first coefficient is always already in-place */
    for (int src = 1; src < s->m; src++) {
        int dst = revtab[src];
        int found = 0;

        if (dst <= src)
            continue;

        /* This just checks if a closed loop has been encountered before,
         * and if so, skips it, since to fully permute a loop we must only
         * enter it once. */
        do {
            for (int j = 0; j < nb_inplace_idx; j++) {
                if (dst == s->inplace_idx[j]) {
                    found = 1;
                    break;
                }
            }
            dst = revtab[dst];
        } while (dst != src && !found);

        if (!found)
            s->inplace_idx[nb_inplace_idx++] = src;
    }

    s->inplace_idx[nb_inplace_idx++] = 0;

    return 0;
}

static void parity_revtab_generator(int *revtab, int n, int inv, int offset,
                                    int is_dual, int dual_high, int len,
                                    int basis, int dual_stride)
{
    len >>= 1;

    if (len <= basis) {
        int k1, k2, *even, *odd, stride;

        is_dual = is_dual && dual_stride;
        dual_high = is_dual & dual_high;
        stride = is_dual ? FFMIN(dual_stride, len) : 0;

        even = &revtab[offset + dual_high*(stride - 2*len)];
        odd  = &even[len + (is_dual && !dual_high)*len + dual_high*len];

        for (int i = 0; i < len; i++) {
            k1 = -split_radix_permutation(offset + i*2 + 0, n, inv) & (n - 1);
            k2 = -split_radix_permutation(offset + i*2 + 1, n, inv) & (n - 1);
            *even++ = k1;
            *odd++  = k2;
            if (stride && !((i + 1) % stride)) {
                even += stride;
                odd  += stride;
            }
        }

        return;
    }

    parity_revtab_generator(revtab, n, inv, offset,
                            0, 0, len >> 0, basis, dual_stride);
    parity_revtab_generator(revtab, n, inv, offset + (len >> 0),
                            1, 0, len >> 1, basis, dual_stride);
    parity_revtab_generator(revtab, n, inv, offset + (len >> 0) + (len >> 1),
                            1, 1, len >> 1, basis, dual_stride);
}

void ff_tx_gen_split_radix_parity_revtab(int *revtab, int len, int inv,
                                         int basis, int dual_stride)
{
    basis >>= 1;
    if (len < basis)
        return;
    av_assert0(!dual_stride || !(dual_stride & (dual_stride - 1)));
    av_assert0(dual_stride <= basis);
    parity_revtab_generator(revtab, len, inv, 0, 0, 0, len, basis, dual_stride);
}

av_cold void av_tx_uninit(AVTXContext **ctx)
{
    if (!(*ctx))
        return;

    av_free((*ctx)->pfatab);
    av_free((*ctx)->exptab);
    av_free((*ctx)->revtab);
    av_free((*ctx)->revtab_c);
    av_free((*ctx)->inplace_idx);
    av_free((*ctx)->tmp);

    av_freep(ctx);
}

av_cold int av_tx_init(AVTXContext **ctx, av_tx_fn *tx, enum AVTXType type,
                       int inv, int len, const void *scale, uint64_t flags)
{
    int err;
    AVTXContext *s = av_mallocz(sizeof(*s));
    if (!s)
        return AVERROR(ENOMEM);

    switch (type) {
    case AV_TX_FLOAT_FFT:
    case AV_TX_FLOAT_MDCT:
        if ((err = ff_tx_init_mdct_fft_float(s, tx, type, inv, len, scale, flags)))
            goto fail;
        if (ARCH_X86)
            ff_tx_init_float_x86(s, tx);
        break;
    case AV_TX_DOUBLE_FFT:
    case AV_TX_DOUBLE_MDCT:
        if ((err = ff_tx_init_mdct_fft_double(s, tx, type, inv, len, scale, flags)))
            goto fail;
        break;
    case AV_TX_INT32_FFT:
    case AV_TX_INT32_MDCT:
        if ((err = ff_tx_init_mdct_fft_int32(s, tx, type, inv, len, scale, flags)))
            goto fail;
        break;
    default:
        err = AVERROR(EINVAL);
        goto fail;
    }

    *ctx = s;

    return 0;

fail:
    av_tx_uninit(&s);
    *tx = NULL;
    return err;
}
