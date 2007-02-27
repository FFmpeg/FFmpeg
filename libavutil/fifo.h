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

/**
 * @file fifo.h
 * A very simple circular buffer FIFO implementation.
 */

#ifndef FIFO_H
#define FIFO_H

typedef struct AVFifoBuffer {
    uint8_t *buffer;
    uint8_t *rptr, *wptr, *end;
} AVFifoBuffer;

/**
 * Initializes a FIFO *.
 * @param *f FIFO buffer
 * @param size of FIFO
 * @return <0 for failure >=0 otherwise
 */
int av_fifo_init(AVFifoBuffer *f, int size);

/**
 * Frees a FIFO *.
 * @param *f FIFO buffer
 */
void av_fifo_free(AVFifoBuffer *f);

/**
 * Returns the size of a FIFO *.
 * @param *f FIFO buffer
 * @return size
 */
int av_fifo_size(AVFifoBuffer *f);

/**
 * Reads the data from the FIFO *.
 * @param *f FIFO buffer
 * @param *buf data destination
 * @param buf_size data size
 * @return -1 if not enough data
 */
int av_fifo_read(AVFifoBuffer *f, uint8_t *buf, int buf_size);

/**
 * Reads the data from the FIFO *.
 * @param *f FIFO buffer
 * @param buf_size data size
 * @param *func generic read function
 * @param *dest data destination
 * @return -1 if not enough data
 */
int av_fifo_generic_read(AVFifoBuffer *f, int buf_size, void (*func)(void*, void*, int), void* dest);

/**
 * Writes the data in the FIFO *.
 * @param *f FIFO buffer
 * @param *buf data source
 * @param size data size
 */
void av_fifo_write(AVFifoBuffer *f, const uint8_t *buf, int size);

/**
 * Resizes the FIFO *.
 * @param *f FIFO buffer
 * @param size data size
 */
void av_fifo_realloc(AVFifoBuffer *f, unsigned int size);

/**
 * Discards the data from the FIFO *.
 * @param *f FIFO buffer
 * @param size data size
 */
void av_fifo_drain(AVFifoBuffer *f, int size);

/**
 * Returns a pointer with circular offset from FIFO's read pointer.
 * @param *f FIFO buffer
 * @param offs offset
 * @return ptr=rptr+offs if rptr+offs<end else rptr+offs -(end-begin)
 */
static inline uint8_t av_fifo_peek(AVFifoBuffer *f, int offs)
{
    uint8_t *ptr = f->rptr + offs;
    if (ptr >= f->end)
        ptr -= f->end - f->buffer;
    return *ptr;
}
#endif /* FIFO_H */
