/*
 * Common bit/dsp utils
 * Copyright (c) 2000 Gerard Lantau.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <math.h>
#include "common.h"

#define NDEBUG
#include <assert.h>

void init_put_bits(PutBitContext *s, 
                   UINT8 *buffer, int buffer_size,
                   void *opaque,
                   void (*write_data)(void *, UINT8 *, int))
{
    s->buf = buffer;
    s->buf_ptr = s->buf;
    s->buf_end = s->buf + buffer_size;
    s->bit_cnt=0;
    s->bit_buf=0;
    s->data_out_size = 0;
    s->write_data = write_data;
    s->opaque = opaque;
}

static void flush_buffer(PutBitContext *s)
{
    int size;
    if (s->write_data) {
        size = s->buf_ptr - s->buf;
        if (size > 0)
            s->write_data(s->opaque, s->buf, size);
        s->buf_ptr = s->buf;
        s->data_out_size += size;
    }
}

void put_bits(PutBitContext *s, int n, unsigned int value)
{
    unsigned int bit_buf;
    int bit_cnt;

    assert(n == 32 || value < (1U << n));

    bit_buf = s->bit_buf;
    bit_cnt = s->bit_cnt;

    //    printf("n=%d value=%x cnt=%d buf=%x\n", n, value, bit_cnt, bit_buf);
    /* XXX: optimize */
    if (n < (32-bit_cnt)) {
        bit_buf |= value << (32 - n - bit_cnt);
        bit_cnt+=n;
    } else {
        bit_buf |= value >> (n + bit_cnt - 32);
        *(UINT32 *)s->buf_ptr = htonl(bit_buf);
        //printf("bitbuf = %08x\n", bit_buf);
        s->buf_ptr+=4;
        if (s->buf_ptr >= s->buf_end)
            flush_buffer(s);
        bit_cnt=bit_cnt + n - 32;
        if (bit_cnt == 0) {
            bit_buf = 0;
        } else {
            bit_buf = value << (32 - bit_cnt);
        }
    }
    
    s->bit_buf = bit_buf;
    s->bit_cnt = bit_cnt;
}

/* return the number of bits output */
long long get_bit_count(PutBitContext *s)
{
    return (s->buf_ptr - s->buf + s->data_out_size) * 8 + (long long)s->bit_cnt;
}

void align_put_bits(PutBitContext *s)
{
    put_bits(s,(8 - s->bit_cnt) & 7,0);
}

/* pad the end of the output stream with zeros */
void flush_put_bits(PutBitContext *s)
{
    while (s->bit_cnt > 0) {
        /* XXX: should test end of buffer */
        *s->buf_ptr++=s->bit_buf >> 24;
        s->bit_buf<<=8;
        s->bit_cnt-=8;
    }
    flush_buffer(s);
    s->bit_cnt=0;
    s->bit_buf=0;
}

/* for jpeg : espace 0xff with 0x00 after it */
void jput_bits(PutBitContext *s, int n, unsigned int value)
{
    unsigned int bit_buf, b;
    int bit_cnt, i;
    
    assert(n == 32 || value < (1U << n));

    bit_buf = s->bit_buf;
    bit_cnt = s->bit_cnt;

    //printf("n=%d value=%x cnt=%d buf=%x\n", n, value, bit_cnt, bit_buf);
    /* XXX: optimize */
    if (n < (32-bit_cnt)) {
        bit_buf |= value << (32 - n - bit_cnt);
        bit_cnt+=n;
    } else {
        bit_buf |= value >> (n + bit_cnt - 32);
        /* handle escape */
        for(i=0;i<4;i++) {
            b = (bit_buf >> 24);
            *(s->buf_ptr++) = b;
            if (b == 0xff)
                *(s->buf_ptr++) = 0;
            bit_buf <<= 8;
        }
        /* we flush the buffer sooner to handle worst case */
        if (s->buf_ptr >= (s->buf_end - 8))
            flush_buffer(s);

        bit_cnt=bit_cnt + n - 32;
        if (bit_cnt == 0) {
            bit_buf = 0;
        } else {
            bit_buf = value << (32 - bit_cnt);
        }
    }
    
    s->bit_buf = bit_buf;
    s->bit_cnt = bit_cnt;
}

/* pad the end of the output stream with zeros */
void jflush_put_bits(PutBitContext *s)
{
    unsigned int b;

    while (s->bit_cnt > 0) {
        b = s->bit_buf >> 24;
        *s->buf_ptr++ = b;
        if (b == 0xff)
            *s->buf_ptr++ = 0;
        s->bit_buf<<=8;
        s->bit_cnt-=8;
    }
    flush_buffer(s);
    s->bit_cnt=0;
    s->bit_buf=0;
}

