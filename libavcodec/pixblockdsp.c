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

#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "avcodec.h"
#include "pixblockdsp.h"

#define BIT_DEPTH 16
#include "pixblockdsp_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 8
#include "pixblockdsp_template.c"

static void diff_pixels_c(int16_t *restrict block, const uint8_t *s1,
                          const uint8_t *s2, int stride)
{
    int i;

    /* read the pixels */
    for (i = 0; i < 8; i++) {
        block[0] = s1[0] - s2[0];
        block[1] = s1[1] - s2[1];
        block[2] = s1[2] - s2[2];
        block[3] = s1[3] - s2[3];
        block[4] = s1[4] - s2[4];
        block[5] = s1[5] - s2[5];
        block[6] = s1[6] - s2[6];
        block[7] = s1[7] - s2[7];
        s1      += stride;
        s2      += stride;
        block   += 8;
    }
}

av_cold void ff_pixblockdsp_init(PixblockDSPContext *c, AVCodecContext *avctx)
{
    const unsigned high_bit_depth = avctx->bits_per_raw_sample > 8;

    c->diff_pixels = diff_pixels_c;

    switch (avctx->bits_per_raw_sample) {
    case 9:
    case 10:
        c->get_pixels = get_pixels_16_c;
        break;
    default:
        c->get_pixels = get_pixels_8_c;
        break;
    }

    if (ARCH_ARM)
        ff_pixblockdsp_init_arm(c, avctx, high_bit_depth);
    if (ARCH_PPC)
        ff_pixblockdsp_init_ppc(c, avctx, high_bit_depth);
    if (ARCH_X86)
        ff_pixblockdsp_init_x86(c, avctx, high_bit_depth);
}
