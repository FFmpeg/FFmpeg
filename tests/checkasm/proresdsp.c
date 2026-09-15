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

#include <string.h>

#include "checkasm.h"

#include "libavcodec/proresdsp.h"

#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/mem_internal.h"

#define IN_IDCT_DEPTH 16
#define PRORES_ONLY

#define BIT_DEPTH 12
#include "libavcodec/simple_idct_template.c"
#undef BIT_DEPTH
#undef IN_IDCT_DEPTH

#define CLIP_MIN (1 << 2)
#define CLIP_MAX_12 ((1 << 12) - CLIP_MIN - 1)

#define BLOCK_SIZE 64

static void permute_block(int16_t *dst, const int16_t *src, const uint8_t *perm)
{
    for (int i = 0; i < BLOCK_SIZE; i++)
        dst[i] = src[perm[i]];
}

static void ref_idct_put_12(uint16_t *dst, ptrdiff_t linesize,
                            const int16_t *src_block, const int16_t *src_qmat)
{
    int16_t block[BLOCK_SIZE];

    for (int i = 0; i < BLOCK_SIZE; i++)
        block[i] = src_block[i] * src_qmat[i];

    for (int i = 0; i < 8; i++)
        idctRowCondDC_int16_12bit(block + i * 8, 0);

    for (int i = 0; i < 8; i++) {
        block[i] += 8192;
        idctSparseCol_int16_12bit(block + i);
    }

    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            dst[y * (linesize >> 1) + x] = av_clip(block[y * 8 + x], CLIP_MIN, CLIP_MAX_12);
}

void checkasm_check_proresdsp(void)
{
    LOCAL_ALIGNED_32(int16_t, block, [BLOCK_SIZE]);
    LOCAL_ALIGNED_32(int16_t, qmat, [BLOCK_SIZE]);
    LOCAL_ALIGNED_16(uint16_t, dst_ref, [BLOCK_SIZE]);
    LOCAL_ALIGNED_16(uint16_t, dst_new, [BLOCK_SIZE]);
    int16_t src_block[BLOCK_SIZE];
    int16_t src_qmat[BLOCK_SIZE];
    ProresDSPContext dsp = { 0 };

    ff_proresdsp_init(&dsp, 12);

    if (check_func(dsp.idct_put, "prores_idct_put_12")) {
        declare_func(void, uint16_t *dst, ptrdiff_t linesize, int16_t *block, const int16_t *qmat);

        for (int i = 0; i < BLOCK_SIZE; i++) {
            src_qmat[i]  = 1 + (rnd() % 255);
            src_block[i] = (int16_t)rnd();
        }

        permute_block(block, src_block, dsp.idct_permutation);
        permute_block(qmat, src_qmat, dsp.idct_permutation);

        ref_idct_put_12(dst_ref, 16, src_block, src_qmat);

        call_new(dst_new, 16, block, qmat);

        if (memcmp(dst_ref, dst_new, BLOCK_SIZE * sizeof(*dst_ref)))
            fail();

        bench_new(dst_new, 16, block, qmat);
    }

    report("prores_idct_put_12");
}
