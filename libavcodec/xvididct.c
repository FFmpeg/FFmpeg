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

#include "config.h"
#include "libavutil/attributes.h"
#include "avcodec.h"
#include "idctdsp.h"
#include "xvididct.h"

static void idct_xvid_put(uint8_t *dest, int line_size, int16_t *block)
{
    ff_idct_xvid(block);
    ff_put_pixels_clamped(block, dest, line_size);
}

static void idct_xvid_add(uint8_t *dest, int line_size, int16_t *block)
{
    ff_idct_xvid(block);
    ff_add_pixels_clamped(block, dest, line_size);
}

av_cold void ff_xvididct_init(IDCTDSPContext *c, AVCodecContext *avctx)
{
    const unsigned high_bit_depth = avctx->bits_per_raw_sample > 8;

    if (high_bit_depth || avctx->lowres ||
        !(avctx->idct_algo == FF_IDCT_AUTO ||
          avctx->idct_algo == FF_IDCT_XVID))
        return;

    if (avctx->idct_algo == FF_IDCT_XVID) {
        c->idct_put  = idct_xvid_put;
        c->idct_add  = idct_xvid_add;
        c->idct      = ff_idct_xvid;
        c->perm_type = FF_IDCT_PERM_NONE;
    }

    if (ARCH_X86)
        ff_xvididct_init_x86(c);

    ff_init_scantable_permutation(c->idct_permutation, c->perm_type);
}
