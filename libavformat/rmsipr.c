/*
 * tables and functions for demuxing SIPR audio muxed RealMedia style
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

#include "rmsipr.h"

const unsigned char ff_sipr_subpk_size[4] = { 29, 19, 37, 20 };

static const unsigned char sipr_swaps[38][2] = {
    {  0, 63 }, {  1, 22 }, {  2, 44 }, {  3, 90 },
    {  5, 81 }, {  7, 31 }, {  8, 86 }, {  9, 58 },
    { 10, 36 }, { 12, 68 }, { 13, 39 }, { 14, 73 },
    { 15, 53 }, { 16, 69 }, { 17, 57 }, { 19, 88 },
    { 20, 34 }, { 21, 71 }, { 24, 46 }, { 25, 94 },
    { 26, 54 }, { 28, 75 }, { 29, 50 }, { 32, 70 },
    { 33, 92 }, { 35, 74 }, { 38, 85 }, { 40, 56 },
    { 42, 87 }, { 43, 65 }, { 45, 59 }, { 48, 79 },
    { 49, 93 }, { 51, 89 }, { 55, 95 }, { 61, 76 },
    { 67, 83 }, { 77, 80 }
};

/* This can be optimized, e.g. use memcpy() if data blocks are aligned. */
void ff_rm_reorder_sipr_data(uint8_t *buf, int sub_packet_h, int framesize)
{
    int n, bs = sub_packet_h * framesize * 2 / 96; // nibbles per subpacket

    for (n = 0; n < 38; n++) {
        int j;
        int i = bs * sipr_swaps[n][0];
        int o = bs * sipr_swaps[n][1];

        /* swap 4bit-nibbles of block 'i' with 'o' */
        for (j = 0; j < bs; j++, i++, o++) {
            int x = (buf[i >> 1] >> (4 * (i & 1))) & 0xF,
                y = (buf[o >> 1] >> (4 * (o & 1))) & 0xF;

            buf[o >> 1] = (x << (4 * (o & 1))) |
                (buf[o >> 1] & (0xF << (4 * !(o & 1))));
            buf[i >> 1] = (y << (4 * (i & 1))) |
                (buf[i >> 1] & (0xF << (4 * !(i & 1))));
        }
    }
}
