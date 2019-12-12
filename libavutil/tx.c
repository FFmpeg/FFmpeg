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

/* Calculates the modular multiplicative inverse, not fast, replace */
static av_always_inline int mulinv(int n, int m)
{
    n = n % m;
    for (int x = 1; x < m; x++)
        if (((n * x) % m) == 1)
            return x;
    av_assert0(0); /* Never reached */
}

/* Guaranteed to work for any n, m where gcd(n, m) == 1 */
int ff_tx_gen_compound_mapping(AVTXContext *s)
{
    int *in_map, *out_map;
    const int n     = s->n;
    const int m     = s->m;
    const int inv   = s->inv;
    const int type  = s->type;
    const int len   = n*m;
    const int m_inv = mulinv(m, n);
    const int n_inv = mulinv(n, m);
    const int mdct  = type == AV_TX_FLOAT_MDCT || type == AV_TX_DOUBLE_MDCT;

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

int ff_tx_gen_ptwo_revtab(AVTXContext *s)
{
    const int m = s->m, inv = s->inv;

    if (!(s->revtab = av_malloc(m*sizeof(*s->revtab))))
        return AVERROR(ENOMEM);

    /* Default */
    for (int i = 0; i < m; i++) {
        int k = -split_radix_permutation(i, m, inv) & (m - 1);
        s->revtab[k] = i;
    }

    return 0;
}

av_cold void av_tx_uninit(AVTXContext **ctx)
{
    if (!(*ctx))
        return;

    av_free((*ctx)->pfatab);
    av_free((*ctx)->exptab);
    av_free((*ctx)->revtab);
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
        break;
    case AV_TX_DOUBLE_FFT:
    case AV_TX_DOUBLE_MDCT:
        if ((err = ff_tx_init_mdct_fft_double(s, tx, type, inv, len, scale, flags)))
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
