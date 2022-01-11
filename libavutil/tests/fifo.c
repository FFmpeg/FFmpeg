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

#include <stdio.h>
#include <stdlib.h>
#include "libavutil/fifo.h"

int main(void)
{
    /* create a FIFO buffer */
    AVFifo *fifo = av_fifo_alloc2(13, sizeof(int), 0);
    int i, j, n, *p;

    /* fill data */
    for (i = 0; av_fifo_can_write(fifo); i++)
        av_fifo_write(fifo, &i, 1);

    /* peek_at at FIFO */
    n = av_fifo_can_read(fifo);
    for (i = 0; i < n; i++) {
        av_fifo_peek(fifo, &j, 1, i);
        printf("%d: %d\n", i, j);
    }
    printf("\n");

    /* generic peek at FIFO */

    n = av_fifo_can_read(fifo);
    p = malloc(n * av_fifo_elem_size(fifo));
    if (p == NULL) {
        fprintf(stderr, "failed to allocate memory.\n");
        exit(1);
    }

    (void) av_fifo_peek(fifo, p, n, 0);

    /* read data at p */
    for(i = 0; i < n; ++i)
        printf("%d: %d\n", i, p[i]);

    putchar('\n');

    /* read data */
    for (i = 0; av_fifo_can_read(fifo); i++) {
        av_fifo_read(fifo, &j, 1);
        printf("%d ", j);
    }
    printf("\n");

    /* fill data */
    for (i = 0; av_fifo_can_write(fifo); i++)
        av_fifo_write(fifo, &i, 1);

    /* peek_at at FIFO */
    n = av_fifo_can_read(fifo);
    for (i = 0; i < n; i++) {
        av_fifo_peek(fifo, &j, 1, i);
        printf("%d: %d\n", i, j);
    }
    putchar('\n');

    /* test fifo_grow */
    (void) av_fifo_grow2(fifo, 15);

    /* fill data */
    n = av_fifo_can_read(fifo);
    for (i = n; av_fifo_can_write(fifo); ++i)
        av_fifo_write(fifo, &i, 1);

    /* peek_at at FIFO */
    n = av_fifo_can_read(fifo);
    for (i = 0; i < n; i++) {
        av_fifo_peek(fifo, &j, 1, i);
        printf("%d: %d\n", i, j);
    }

    av_fifo_freep2(&fifo);
    free(p);

    return 0;
}
