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
    AVFifoBuffer *fifo = av_fifo_alloc(13 * sizeof(int));
    int i, j, n, *p;

    /* fill data */
    for (i = 0; av_fifo_space(fifo) >= sizeof(int); i++)
        av_fifo_generic_write(fifo, &i, sizeof(int), NULL);

    /* peek at FIFO */
    n = av_fifo_size(fifo) / sizeof(int);
    for (i = -n + 1; i < n; i++) {
        int *v = (int *)av_fifo_peek2(fifo, i * sizeof(int));
        printf("%d: %d\n", i, *v);
    }
    printf("\n");

    /* peek_at at FIFO */
    n = av_fifo_size(fifo) / sizeof(int);
    for (i = 0; i < n; i++) {
        av_fifo_generic_peek_at(fifo, &j, i * sizeof(int), sizeof(j), NULL);
        printf("%d: %d\n", i, j);
    }
    printf("\n");

    /* generic peek at FIFO */

    n = av_fifo_size(fifo);
    p = malloc(n);
    if (p == NULL) {
        fprintf(stderr, "failed to allocate memory.\n");
        exit(1);
    }

    (void) av_fifo_generic_peek(fifo, p, n, NULL);

    /* read data at p */
    n /= sizeof(int);
    for(i = 0; i < n; ++i)
        printf("%d: %d\n", i, p[i]);

    putchar('\n');

    /* read data */
    for (i = 0; av_fifo_size(fifo) >= sizeof(int); i++) {
        av_fifo_generic_read(fifo, &j, sizeof(int), NULL);
        printf("%d ", j);
    }
    printf("\n");

    /* test *ndx overflow */
    av_fifo_reset(fifo);
    fifo->rndx = fifo->wndx = ~(uint32_t)0 - 5;

    /* fill data */
    for (i = 0; av_fifo_space(fifo) >= sizeof(int); i++)
        av_fifo_generic_write(fifo, &i, sizeof(int), NULL);

    /* peek_at at FIFO */
    n = av_fifo_size(fifo) / sizeof(int);
    for (i = 0; i < n; i++) {
        av_fifo_generic_peek_at(fifo, &j, i * sizeof(int), sizeof(j), NULL);
        printf("%d: %d\n", i, j);
    }
    putchar('\n');

    /* test fifo_grow */
    (void) av_fifo_grow(fifo, 15 * sizeof(int));

    /* fill data */
    n = av_fifo_size(fifo) / sizeof(int);
    for (i = n; av_fifo_space(fifo) >= sizeof(int); ++i)
        av_fifo_generic_write(fifo, &i, sizeof(int), NULL);

    /* peek_at at FIFO */
    n = av_fifo_size(fifo) / sizeof(int);
    for (i = 0; i < n; i++) {
        av_fifo_generic_peek_at(fifo, &j, i * sizeof(int), sizeof(j), NULL);
        printf("%d: %d\n", i, j);
    }

    av_fifo_free(fifo);
    free(p);

    return 0;
}
