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

#include "libavutil/attributes.h"
#include "libavutil/bswap.h"
#include "bswapdsp.h"

static void bswap_buf(uint32_t *dst, const uint32_t *src, int w)
{
    int i;

    for (i = 0; i + 8 <= w; i += 8) {
        dst[i + 0] = av_bswap32(src[i + 0]);
        dst[i + 1] = av_bswap32(src[i + 1]);
        dst[i + 2] = av_bswap32(src[i + 2]);
        dst[i + 3] = av_bswap32(src[i + 3]);
        dst[i + 4] = av_bswap32(src[i + 4]);
        dst[i + 5] = av_bswap32(src[i + 5]);
        dst[i + 6] = av_bswap32(src[i + 6]);
        dst[i + 7] = av_bswap32(src[i + 7]);
    }
    for (; i < w; i++)
        dst[i + 0] = av_bswap32(src[i + 0]);
}

static void bswap16_buf(uint16_t *dst, const uint16_t *src, int len)
{
    while (len--)
        *dst++ = av_bswap16(*src++);
}

av_cold void ff_bswapdsp_init(BswapDSPContext *c)
{
    c->bswap_buf   = bswap_buf;
    c->bswap16_buf = bswap16_buf;

    if (ARCH_X86)
        ff_bswapdsp_init_x86(c);
}
