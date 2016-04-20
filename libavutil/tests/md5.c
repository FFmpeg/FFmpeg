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

#include "libavutil/md5.h"

static void print_md5(uint8_t *md5)
{
    int i;
    for (i = 0; i < 16; i++)
        printf("%02x", md5[i]);
    printf("\n");
}

int main(void)
{
    uint8_t md5val[16];
    int i;
    uint8_t in[1000];

    for (i = 0; i < 1000; i++)
        in[i] = i * i;
    av_md5_sum(md5val, in, 1000);
    print_md5(md5val);
    av_md5_sum(md5val, in, 63);
    print_md5(md5val);
    av_md5_sum(md5val, in, 64);
    print_md5(md5val);
    av_md5_sum(md5val, in, 65);
    print_md5(md5val);
    for (i = 0; i < 1000; i++)
        in[i] = i % 127;
    av_md5_sum(md5val, in, 999);
    print_md5(md5val);

    return 0;
}
