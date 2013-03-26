/*
 * DCA compatible decoder data
 *
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
#include <string.h>

#include "put_bits.h"
#include "dca.h"

const uint32_t avpriv_dca_sample_rates[16] =
{
    0, 8000, 16000, 32000, 0, 0, 11025, 22050, 44100, 0, 0,
    12000, 24000, 48000, 96000, 192000
};

int ff_dca_convert_bitstream(const uint8_t *src, int src_size, uint8_t *dst,
                             int max_size)
{
    uint32_t mrk;
    int i, tmp;
    const uint16_t *ssrc = (const uint16_t *) src;
    uint16_t *sdst = (uint16_t *) dst;
    PutBitContext pb;

    if ((unsigned) src_size > (unsigned) max_size)
        src_size = max_size;

    mrk = AV_RB32(src);
    switch (mrk) {
    case DCA_MARKER_RAW_BE:
        memcpy(dst, src, src_size);
        return src_size;
    case DCA_MARKER_RAW_LE:
        for (i = 0; i < (src_size + 1) >> 1; i++)
            *sdst++ = av_bswap16(*ssrc++);
        return src_size;
    case DCA_MARKER_14B_BE:
    case DCA_MARKER_14B_LE:
        init_put_bits(&pb, dst, max_size);
        for (i = 0; i < (src_size + 1) >> 1; i++, src += 2) {
            tmp = ((mrk == DCA_MARKER_14B_BE) ? AV_RB16(src) : AV_RL16(src)) & 0x3FFF;
            put_bits(&pb, 14, tmp);
        }
        flush_put_bits(&pb);
        return (put_bits_count(&pb) + 7) >> 3;
    default:
        return AVERROR_INVALIDDATA;
    }
}
