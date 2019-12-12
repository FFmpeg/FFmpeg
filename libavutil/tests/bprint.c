/*
 * Copyright (c) 2012 Nicolas George
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

#include "libavutil/bprint.c"

#undef printf

static void bprint_pascal(AVBPrint *b, unsigned size)
{
    unsigned i, j;
    unsigned p[42];

    av_assert0(size < FF_ARRAY_ELEMS(p));

    p[0] = 1;
    av_bprintf(b, "%8d\n", 1);
    for (i = 1; i <= size; i++) {
        p[i] = 1;
        for (j = i - 1; j > 0; j--)
            p[j] = p[j] + p[j - 1];
        for (j = 0; j <= i; j++)
            av_bprintf(b, "%8d", p[j]);
        av_bprintf(b, "\n");
    }
}

int main(void)
{
    AVBPrint b;
    char buf[256];
    struct tm testtime = { .tm_year = 100, .tm_mon = 11, .tm_mday = 20 };

    av_bprint_init(&b, 0, AV_BPRINT_SIZE_UNLIMITED);
    bprint_pascal(&b, 5);
    printf("Short text in unlimited buffer: %u/%u\n", (unsigned)strlen(b.str), b.len);
    printf("%s\n", b.str);
    av_bprint_finalize(&b, NULL);

    av_bprint_init(&b, 0, AV_BPRINT_SIZE_UNLIMITED);
    bprint_pascal(&b, 25);
    printf("Long text in unlimited buffer: %u/%u\n", (unsigned)strlen(b.str), b.len);
    av_bprint_finalize(&b, NULL);

    av_bprint_init(&b, 0, 2048);
    bprint_pascal(&b, 25);
    printf("Long text in limited buffer: %u/%u\n", (unsigned)strlen(b.str), b.len);
    av_bprint_finalize(&b, NULL);

    av_bprint_init(&b, 0, AV_BPRINT_SIZE_AUTOMATIC);
    bprint_pascal(&b, 5);
    printf("Short text in automatic buffer: %u/%u\n", (unsigned)strlen(b.str), b.len);

    av_bprint_init(&b, 0, AV_BPRINT_SIZE_AUTOMATIC);
    bprint_pascal(&b, 25);
    printf("Long text in automatic buffer: %u/%u\n", (unsigned)strlen(b.str)/8*8, b.len);
    /* Note that the size of the automatic buffer is arch-dependent. */

    av_bprint_init(&b, 0, AV_BPRINT_SIZE_COUNT_ONLY);
    bprint_pascal(&b, 25);
    printf("Long text count only buffer: %u/%u\n", (unsigned)strlen(b.str), b.len);

    av_bprint_init_for_buffer(&b, buf, sizeof(buf));
    bprint_pascal(&b, 25);
    printf("Long text count only buffer: %u/%u\n", (unsigned)strlen(buf), b.len);

    av_bprint_init(&b, 0, AV_BPRINT_SIZE_UNLIMITED);
    av_bprint_strftime(&b, "%Y-%m-%d", &testtime);
    printf("strftime full: %u/%u \"%s\"\n", (unsigned)strlen(buf), b.len, b.str);
    av_bprint_finalize(&b, NULL);

    av_bprint_init(&b, 0, 8);
    av_bprint_strftime(&b, "%Y-%m-%d", &testtime);
    printf("strftime truncated: %u/%u \"%s\"\n", (unsigned)strlen(buf), b.len, b.str);

    return 0;
}
