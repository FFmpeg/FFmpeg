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

/*
 * Based on libavutil/base64.c
 */

#include <stdio.h>

int main(void)
{
    static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned i_bits = 0;
    int i_shift     = 0;
    int out_len     = 0;
    int in;

#define putb64()                                        \
    do {                                                \
        putchar(b64[(i_bits << 6 >> i_shift) & 0x3f]);  \
        out_len++;                                      \
        i_shift -= 6;                                   \
    } while (0)

    while ((in = getchar()) != EOF) {
        i_bits   = (i_bits << 8) + in;
        i_shift += 8;
        while (i_shift > 6)
            putb64();
    }
    while (i_shift > 0)
        putb64();
    while (out_len++ & 3)
        putchar('=');
    putchar('\n');

    return 0;
}
