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

#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "pixblockdsp.h"

static void get_pixels_16_c(int16_t *av_restrict block, const uint8_t *pixels,
                            ptrdiff_t stride)
{
    AV_COPY128U(block + 0 * 8, pixels + 0 * stride);
    AV_COPY128U(block + 1 * 8, pixels + 1 * stride);
    AV_COPY128U(block + 2 * 8, pixels + 2 * stride);
    AV_COPY128U(block + 3 * 8, pixels + 3 * stride);
    AV_COPY128U(block + 4 * 8, pixels + 4 * stride);
    AV_COPY128U(block + 5 * 8, pixels + 5 * stride);
    AV_COPY128U(block + 6 * 8, pixels + 6 * stride);
    AV_COPY128U(block + 7 * 8, pixels + 7 * stride);
}

static void get_pixels_8_c(int16_t *av_restrict block, const uint8_t *pixels,
                           ptrdiff_t stride)
{
    int i;

    /* read the pixels */
    for (i = 0; i < 8; i++) {
        block[0] = pixels[0];
        block[1] = pixels[1];
        block[2] = pixels[2];
        block[3] = pixels[3];
        block[4] = pixels[4];
        block[5] = pixels[5];
        block[6] = pixels[6];
        block[7] = pixels[7];
        pixels  += stride;
        block   += 8;
    }
}

static void diff_pixels_c(int16_t *av_restrict block, const uint8_t *s1,
                          const uint8_t *s2, ptrdiff_t stride)
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

    c->diff_pixels_unaligned =
    c->diff_pixels = diff_pixels_c;

    switch (avctx->bits_per_raw_sample) {
    case 9:
    case 10:
    case 12:
    case 14:
        c->get_pixels = get_pixels_16_c;
        break;
    default:
        if (avctx->bits_per_raw_sample<=8 || avctx->codec_type != AVMEDIA_TYPE_VIDEO) {
            c->get_pixels = get_pixels_8_c;
        }
        break;
    }

    if (ARCH_ALPHA)
        ff_pixblockdsp_init_alpha(c, avctx, high_bit_depth);
    if (ARCH_ARM)
        ff_pixblockdsp_init_arm(c, avctx, high_bit_depth);
    if (ARCH_PPC)
        ff_pixblockdsp_init_ppc(c, avctx, high_bit_depth);
    if (ARCH_X86)
        ff_pixblockdsp_init_x86(c, avctx, high_bit_depth);
    if (ARCH_MIPS)
        ff_pixblockdsp_init_mips(c, avctx, high_bit_depth);
}
