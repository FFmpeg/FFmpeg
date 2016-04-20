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
#include <stdio.h>

#include "libavutil/crc.h"

int main(void)
{
    uint8_t buf[1999];
    int i;
    static const int p[5][3] = {
        { AV_CRC_32_IEEE_LE, 0xEDB88320, 0x3D5CDD04 },
        { AV_CRC_32_IEEE,    0x04C11DB7, 0xC0F5BAE0 },
        { AV_CRC_16_ANSI_LE,     0xA001,     0xBFD8 },
        { AV_CRC_16_ANSI,        0x8005,     0x1FBB },
        { AV_CRC_8_ATM,            0x07,       0xE3 }
    };
    const AVCRC *ctx;

    for (i = 0; i < sizeof(buf); i++)
        buf[i] = i + i * i;

    for (i = 0; i < 5; i++) {
        ctx = av_crc_get_table(p[i][0]);
        printf("crc %08X = %X\n", p[i][1], av_crc(ctx, 0, buf, sizeof(buf)));
    }
    return 0;
}
