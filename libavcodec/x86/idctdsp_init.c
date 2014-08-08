/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/idctdsp.h"
#include "idctdsp.h"
#include "simple_idct.h"

/* Input permutation for the simple_idct_mmx */
static const uint8_t simple_mmx_permutation[64] = {
    0x00, 0x08, 0x04, 0x09, 0x01, 0x0C, 0x05, 0x0D,
    0x10, 0x18, 0x14, 0x19, 0x11, 0x1C, 0x15, 0x1D,
    0x20, 0x28, 0x24, 0x29, 0x21, 0x2C, 0x25, 0x2D,
    0x12, 0x1A, 0x16, 0x1B, 0x13, 0x1E, 0x17, 0x1F,
    0x02, 0x0A, 0x06, 0x0B, 0x03, 0x0E, 0x07, 0x0F,
    0x30, 0x38, 0x34, 0x39, 0x31, 0x3C, 0x35, 0x3D,
    0x22, 0x2A, 0x26, 0x2B, 0x23, 0x2E, 0x27, 0x2F,
    0x32, 0x3A, 0x36, 0x3B, 0x33, 0x3E, 0x37, 0x3F,
};

static const uint8_t idct_sse2_row_perm[8] = { 0, 4, 1, 5, 2, 6, 3, 7 };

av_cold int ff_init_scantable_permutation_x86(uint8_t *idct_permutation,
                                              enum idct_permutation_type perm_type)
{
    int i;

    switch (perm_type) {
    case FF_IDCT_PERM_SIMPLE:
        for (i = 0; i < 64; i++)
            idct_permutation[i] = simple_mmx_permutation[i];
        return 1;
    case FF_IDCT_PERM_SSE2:
        for (i = 0; i < 64; i++)
            idct_permutation[i] = (i & 0x38) | idct_sse2_row_perm[i & 7];
        return 1;
    }

    return 0;
}

av_cold void ff_idctdsp_init_x86(IDCTDSPContext *c, AVCodecContext *avctx,
                                 unsigned high_bit_depth)
{
    int cpu_flags = av_get_cpu_flags();

    if (INLINE_MMX(cpu_flags)) {
        c->put_pixels_clamped        = ff_put_pixels_clamped_mmx;
        c->put_signed_pixels_clamped = ff_put_signed_pixels_clamped_mmx;
        c->add_pixels_clamped        = ff_add_pixels_clamped_mmx;

        if (!high_bit_depth &&
            (avctx->idct_algo == FF_IDCT_AUTO ||
             avctx->idct_algo == FF_IDCT_SIMPLEMMX)) {
                c->idct_put  = ff_simple_idct_put_mmx;
                c->idct_add  = ff_simple_idct_add_mmx;
                c->idct      = ff_simple_idct_mmx;
                c->perm_type = FF_IDCT_PERM_SIMPLE;
        }
    }
}
