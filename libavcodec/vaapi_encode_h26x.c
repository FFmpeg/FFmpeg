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

#include "vaapi_encode_h26x.h"

int ff_vaapi_encode_h26x_nal_unit_to_byte_stream(uint8_t *dst, size_t *dst_bit_len,
                                                 uint8_t *src, size_t src_bit_len)
{
    size_t dp, sp;
    int zero_run = 0;
    size_t dst_len = *dst_bit_len / 8;
    size_t src_len = (src_bit_len + 7) / 8;
    int trailing_zeroes = src_len * 8 - src_bit_len;

    if (dst_len < src_len + 4) {
        // Definitely doesn't fit.
        goto fail;
    }

    // Start code.
    dst[0] = dst[1] = dst[2] = 0;
    dst[3] = 1;
    dp = 4;

    for (sp = 0; sp < src_len; sp++) {
        if (dp >= dst_len)
            goto fail;
        if (zero_run < 2) {
            if (src[sp] == 0)
                ++zero_run;
            else
                zero_run = 0;
        } else {
            if ((src[sp] & ~3) == 0) {
                // emulation_prevention_three_byte
                dst[dp++] = 3;
                if (dp >= dst_len)
                    goto fail;
            }
            zero_run = src[sp] == 0;
        }
        dst[dp++] = src[sp];
    }

    *dst_bit_len = 8 * dp - trailing_zeroes;
    return 0;

fail:
    *dst_bit_len = 0;
    return AVERROR(ENOSPC);
}
