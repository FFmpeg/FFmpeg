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
#include "libavutil/mem.h"
#include "libavutil/aarch64/cpu.h"

TX_DECL_FN(fft2,      neon)
TX_DECL_FN(fft4_fwd,  neon)
TX_DECL_FN(fft4_inv,  neon)
TX_DECL_FN(fft8,      neon)
TX_DECL_FN(fft8_ns,   neon)
TX_DECL_FN(fft15,     neon)
TX_DECL_FN(fft15_ns,  neon)
TX_DECL_FN(fft16,     neon)
TX_DECL_FN(fft16_ns,  neon)
TX_DECL_FN(fft32,     neon)
TX_DECL_FN(fft32_ns,  neon)
TX_DECL_FN(fft_sr,    neon)
TX_DECL_FN(fft_sr_ns, neon)
TX_DECL_FN(fft_pfa_15xM, neon)
TX_DECL_FN(fft_pfa_15xM_ns, neon)

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

/* Reorder one 15-point map so the loads in the pre-permuted assembly path
 * become simple contiguous chunks. Mirrors the x86 FFT15 init. */
static void fft15_permute_map(int *map)
{
    int cnt = 0, tmp[15];
    memcpy(tmp, map, 15*sizeof(*tmp));
    for (int i = 1; i < 15; i += 3)
        map[cnt++] = tmp[i];
    for (int i = 2; i < 15; i += 3)
        map[cnt++] = tmp[i];
    for (int i = 0; i < 15; i += 3)
        map[cnt++] = tmp[i];
    memmove(&map[7], &map[6], 4*sizeof(int));
    memmove(&map[3], &map[1], 4*sizeof(int));
    map[1] = tmp[2];
    map[2] = tmp[0];
}

static av_cold int fft15_init(AVTXContext *s, const FFTXCodelet *cd,
                              uint64_t flags, FFTXCodeletOptions *opts,
                              int len, int inv, const void *scale)
{
    int ret;
    FFTXCodeletOptions sub_opts = { .map_dir = FF_TX_MAP_GATHER };

    ff_tx_init_tabs_float(len);

    if ((ret = ff_tx_gen_pfa_input_map(s, &sub_opts, 3, 5)) < 0)
        return ret;

    fft15_permute_map(s->map);

    return 0;
}

/* 15xM prime-factor FFT: M inlined 15-point transforms followed by 15 calls to
 * a power-of-two subtransform. Mirrors the x86 fft_pfa_15xM, but the aarch64
 * ABI lets us call the subtransform normally, so no FF_TX_ASM_CALL is needed. */
static av_cold int fft_pfa_init(AVTXContext *s, const FFTXCodelet *cd,
                                uint64_t flags, FFTXCodeletOptions *opts,
                                int len, int inv, const void *scale)
{
    int ret;
    int sub_len = len / cd->factors[0];
    FFTXCodeletOptions sub_opts = { .map_dir = FF_TX_MAP_SCATTER };

    flags &= ~FF_TX_OUT_OF_PLACE; /* We want the subtransform to be */
    flags |=  AV_TX_INPLACE;      /* in-place */
    flags |=  FF_TX_PRESHUFFLE;   /* This function handles the permute step */

    if ((ret = ff_tx_init_subtx(s, TX_TYPE(FFT), flags, &sub_opts,
                                sub_len, inv, scale)))
        return ret;

    if ((ret = ff_tx_gen_compound_mapping(s, opts, s->inv,
                                          cd->factors[0], sub_len)))
        return ret;

    /* The 15-point transform is itself a compound one, so embed its input map
     * and apply the same load-friendly reorder used by fft15_init. */
    TX_EMBED_INPUT_PFA_MAP(s->map, len, 3, 5);
    for (int k = 0; k < sub_len; k++)
        fft15_permute_map(&s->map[k*15]);

    if (!(s->tmp = av_malloc(len*sizeof(*s->tmp))))
        return AVERROR(ENOMEM);

    ff_tx_init_tabs_float(len / sub_len);

    return 0;
}

const FFTXCodelet * const ff_tx_codelet_list_float_aarch64[] = {
    TX_DEF(fft2,      FFT,  2,  2, 2, 0, 128, NULL,      neon, NEON, AV_TX_INPLACE, 0),
    TX_DEF(fft2,      FFT,  2,  2, 2, 0, 192, neon_init, neon, NEON, AV_TX_INPLACE | FF_TX_PRESHUFFLE, 0),
    TX_DEF(fft4_fwd,  FFT,  4,  4, 2, 0, 128, NULL,      neon, NEON, AV_TX_INPLACE | FF_TX_FORWARD_ONLY, 0),
    TX_DEF(fft4_fwd,  FFT,  4,  4, 2, 0, 192, neon_init, neon, NEON, AV_TX_INPLACE | FF_TX_PRESHUFFLE, 0),
    TX_DEF(fft4_inv,  FFT,  4,  4, 2, 0, 128, NULL,      neon, NEON, AV_TX_INPLACE | FF_TX_INVERSE_ONLY, 0),
    TX_DEF(fft8,      FFT,  8,  8, 2, 0, 128, neon_init, neon, NEON, AV_TX_INPLACE, 0),
    TX_DEF(fft8_ns,   FFT,  8,  8, 2, 0, 192, neon_init, neon, NEON, AV_TX_INPLACE | FF_TX_PRESHUFFLE, 0),
    TX_DEF(fft15,     FFT, 15, 15, 15, 0, 128, fft15_init, neon, NEON, AV_TX_INPLACE, 0),
    TX_DEF(fft15_ns,  FFT, 15, 15, 15, 0, 192, fft15_init, neon, NEON, AV_TX_INPLACE | FF_TX_PRESHUFFLE, 0),
    TX_DEF(fft16,     FFT, 16, 16, 2, 0, 128, neon_init, neon, NEON, AV_TX_INPLACE, 0),
    TX_DEF(fft16_ns,  FFT, 16, 16, 2, 0, 192, neon_init, neon, NEON, AV_TX_INPLACE | FF_TX_PRESHUFFLE, 0),
    TX_DEF(fft32,     FFT, 32, 32, 2, 0, 128, neon_init, neon, NEON, AV_TX_INPLACE, 0),
    TX_DEF(fft32_ns,  FFT, 32, 32, 2, 0, 192, neon_init, neon, NEON, AV_TX_INPLACE | FF_TX_PRESHUFFLE, 0),

    TX_DEF(fft_sr,    FFT, 64, 131072, 2, 0, 128, neon_init, neon, NEON, 0, 0),
    TX_DEF(fft_sr_ns, FFT, 64, 131072, 2, 0, 192, neon_init, neon, NEON, AV_TX_INPLACE | FF_TX_PRESHUFFLE, 0),

    TX_DEF(fft_pfa_15xM,    FFT, 60, TX_LEN_UNLIMITED, 15, 2, 128, fft_pfa_init, neon, NEON, AV_TX_INPLACE, 0),
    TX_DEF(fft_pfa_15xM_ns, FFT, 60, TX_LEN_UNLIMITED, 15, 2, 192, fft_pfa_init, neon, NEON, AV_TX_INPLACE | FF_TX_PRESHUFFLE, 0),

    NULL,
};
