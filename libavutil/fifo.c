/*
 * a very simple circular buffer FIFO implementation
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

AVFifoBuffer *av_fifo_alloc(unsigned int size)
{
    AVFifoBuffer *f= av_mallocz(sizeof(AVFifoBuffer));
    if(!f)
        return NULL;
    f->buffer = av_malloc(size);
    f->end = f->buffer + size;
    av_fifo_reset(f);
    if (!f->buffer)
        av_freep(&f);
    return f;
}

void av_fifo_free(AVFifoBuffer *f)
{
    if(f){
        av_freep(&f->buffer);
        av_free(f);
    }
}

void av_fifo_reset(AVFifoBuffer *f)
{
    f->wptr = f->rptr = f->buffer;
    f->wndx = f->rndx = 0;
}

int av_fifo_size(AVFifoBuffer *f)
{
    return (uint32_t)(f->wndx - f->rndx);
}

int av_fifo_space(AVFifoBuffer *f)
{
    return f->end - f->buffer - av_fifo_size(f);
}

int av_fifo_realloc2(AVFifoBuffer *f, unsigned int new_size) {
    unsigned int old_size= f->end - f->buffer;

    if(old_size < new_size){
        int len= av_fifo_size(f);
        AVFifoBuffer *f2= av_fifo_alloc(new_size);

        if (!f2)
            return -1;
        av_fifo_generic_read(f, f2->buffer, len, NULL);
        f2->wptr += len;
        f2->wndx += len;
        av_free(f->buffer);
        *f= *f2;
        av_free(f2);
    }
    return 0;
}

// src must NOT be const as it can be a context for func that may need updating (like a pointer or byte counter)
int av_fifo_generic_write(AVFifoBuffer *f, void *src, int size, int (*func)(void*, void*, int))
{
    int total = size;
    do {
        int len = FFMIN(f->end - f->wptr, size);
        if(func) {
            if(func(src, f->wptr, len) <= 0)
                break;
        } else {
            memcpy(f->wptr, src, len);
            src = (uint8_t*)src + len;
        }
// Write memory barrier needed for SMP here in theory
        f->wptr += len;
        if (f->wptr >= f->end)
            f->wptr = f->buffer;
        f->wndx += len;
        size -= len;
    } while (size > 0);
    return total - size;
}


int av_fifo_generic_read(AVFifoBuffer *f, void *dest, int buf_size, void (*func)(void*, void*, int))
{
// Read memory barrier needed for SMP here in theory
    do {
        int len = FFMIN(f->end - f->rptr, buf_size);
        if(func) func(dest, f->rptr, len);
        else{
            memcpy(dest, f->rptr, len);
            dest = (uint8_t*)dest + len;
        }
// memory barrier needed for SMP here in theory
        av_fifo_drain(f, len);
        buf_size -= len;
    } while (buf_size > 0);
    return 0;
}

/** Discard data from the FIFO. */
void av_fifo_drain(AVFifoBuffer *f, int size)
{
    f->rptr += size;
    if (f->rptr >= f->end)
        f->rptr -= f->end - f->buffer;
    f->rndx += size;
}
