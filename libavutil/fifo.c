/*
 * A very simple circular buffer FIFO implementation
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 * Copyright (c) 2006 Roman Shaposhnik
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
#include "common.h"
#include "fifo.h"

int av_fifo_init(AVFifoBuffer *f, int size)
{
    f->buffer = av_malloc(size);
    if (!f->buffer)
        return -1;
    f->end = f->buffer + size;
    f->wptr = f->rptr = f->buffer;
    return 0;
}

void av_fifo_free(AVFifoBuffer *f)
{
    av_free(f->buffer);
}

int av_fifo_size(AVFifoBuffer *f)
{
    int size = f->wptr - f->rptr;
    if (size < 0)
        size += f->end - f->buffer;
    return size;
}

/**
 * Get data from the fifo (returns -1 if not enough data).
 */
int av_fifo_read(AVFifoBuffer *f, uint8_t *buf, int buf_size)
{
    int size = av_fifo_size(f);

    if (size < buf_size)
        return -1;
    while (buf_size > 0) {
        int len = FFMIN(f->end - f->rptr, buf_size);
        memcpy(buf, f->rptr, len);
        buf += len;
        f->rptr += len;
        if (f->rptr >= f->end)
            f->rptr = f->buffer;
        buf_size -= len;
    }
    return 0;
}

/**
 * Resizes a FIFO.
 */
void av_fifo_realloc(AVFifoBuffer *f, unsigned int new_size) {
    unsigned int old_size= f->end - f->buffer;

    if(old_size < new_size){
        uint8_t *old= f->buffer;

        f->buffer= av_realloc(f->buffer, new_size);

        f->rptr += f->buffer - old;
        f->wptr += f->buffer - old;

        if(f->wptr < f->rptr){
            memmove(f->rptr + new_size - old_size, f->rptr, f->buffer + old_size - f->rptr);
            f->rptr += new_size - old_size;
        }
        f->end= f->buffer + new_size;
    }
}

void av_fifo_write(AVFifoBuffer *f, const uint8_t *buf, int size)
{
    int len;

    while (size > 0) {
        len = FFMIN(f->end - f->wptr, size);
        memcpy(f->wptr, buf, len);
        f->wptr += len;
        if (f->wptr >= f->end)
            f->wptr = f->buffer;
        buf += len;
        size -= len;
    }
}


/* get data from the fifo (return -1 if not enough data) */
int av_fifo_generic_read(AVFifoBuffer *f, int buf_size, void (*func)(void*, void*, int), void* dest)
{
    int len;
    int size = f->wptr - f->rptr;
    if (size < 0)
        size += f->end - f->buffer;

    if (size < buf_size)
        return -1;
    while (buf_size > 0) {
        len = FFMIN(f->end - f->rptr, buf_size);
        func(dest, f->rptr, len);
        f->rptr += len;
        if (f->rptr >= f->end)
            f->rptr = f->buffer;
        buf_size -= len;
    }
    return 0;
}

/* discard data from the fifo */
void av_fifo_drain(AVFifoBuffer *f, int size)
{
    f->rptr += size;
    if (f->rptr >= f->end)
        f->rptr -= f->end - f->buffer;
}
