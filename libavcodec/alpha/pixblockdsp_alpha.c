/*
 * SIMD-optimized pixel operations
 *
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

#include "libavutil/attributes.h"
#include "libavcodec/dsputil.h"
#include "libavcodec/pixblockdsp.h"
#include "asm.h"

static void get_pixels_mvi(int16_t *restrict block,
                           const uint8_t *restrict pixels, int line_size)
{
    int h = 8;

    do {
        uint64_t p;

        p = ldq(pixels);
        stq(unpkbw(p),       block);
        stq(unpkbw(p >> 32), block + 4);

        pixels += line_size;
        block += 8;
    } while (--h);
}

static void diff_pixels_mvi(int16_t *block, const uint8_t *s1, const uint8_t *s2,
                            int stride) {
    int h = 8;
    uint64_t mask = 0x4040;

    mask |= mask << 16;
    mask |= mask << 32;
    do {
        uint64_t x, y, c, d, a;
        uint64_t signs;

        x = ldq(s1);
        y = ldq(s2);
        c = cmpbge(x, y);
        d = x - y;
        a = zap(mask, c);       /* We use 0x4040404040404040 here...  */
        d += 4 * a;             /* ...so we can use s4addq here.      */
        signs = zap(-1, c);

        stq(unpkbw(d)       | (unpkbw(signs)       << 8), block);
        stq(unpkbw(d >> 32) | (unpkbw(signs >> 32) << 8), block + 4);

        s1 += stride;
        s2 += stride;
        block += 8;
    } while (--h);
}

av_cold void ff_pixblockdsp_init_alpha(PixblockDSPContext *c, AVCodecContext *avctx,
                                       unsigned high_bit_depth)
{
    if (amask(AMASK_MVI) == 0) {
        if (!high_bit_depth)
            c->get_pixels = get_pixels_mvi;
        c->diff_pixels = diff_pixels_mvi;
    }
}
