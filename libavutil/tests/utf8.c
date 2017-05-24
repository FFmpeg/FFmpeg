/*
 * Copyright (c) 2013 Stefano Sabatini
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

#include <stdio.h>

#include "libavutil/avstring.h"
#include "libavutil/file.h"

static void print_sequence(const char *p, int l, int indent)
{
    int i;
    for (i = 0; i < l; i++)
        printf("%02X", (uint8_t)p[i]);
    printf("%*s", indent-l*2, "");
}

int main(int argc, char **argv)
{
    int ret;
    char *filename = argv[1];
    uint8_t *file_buf;
    size_t file_buf_size;
    uint32_t code;
    const uint8_t *p, *endp;

    ret = av_file_map(filename, &file_buf, &file_buf_size, 0, NULL);
    if (ret < 0)
        return 1;

    p = file_buf;
    endp = file_buf + file_buf_size;
    while (p < endp) {
        int l, r;
        const uint8_t *p0 = p;
        code = UINT32_MAX;
        r = av_utf8_decode(&code, &p, endp, 0);
        l = (int)(p-p0);
        print_sequence(p0, l, 20);
        if (code != UINT32_MAX) {
            printf("%-10d 0x%-10X %-5d ", code, code, l);
            if (r >= 0) {
                if (*p0 == '\n') printf("\\n\n");
                else             printf ("%.*s\n", l, p0);
            } else {
                printf("invalid code range\n");
            }
        } else {
            printf("invalid sequence\n");
        }
    }

    av_file_unmap(file_buf, file_buf_size);
    return 0;
}
