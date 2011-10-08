/*
 * copyright (c) 2004 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file
 * bitstream writer API
 */

#ifndef AVCODEC_PUT_BITS_H
#define AVCODEC_PUT_BITS_H

#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include "libavutil/bswap.h"
#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "mathops.h"
#include "config.h"

//#define ALT_BITSTREAM_WRITER
//#define ALIGNED_BITSTREAM_WRITER

/* buf and buf_end must be present and used by every alternative writer. */
typedef struct PutBitContext {
#ifdef ALT_BITSTREAM_WRITER
    uint8_t *buf, *buf_end;
    int index;
#else
    uint32_t bit_buf;
    int bit_left;
    uint8_t *buf, *buf_ptr, *buf_end;
#endif
    int size_in_bits;
} PutBitContext;

/**
 * Initialize the PutBitContext s.
 *
 * @param buffer the buffer where to put bits
 * @param buffer_size the size in bytes of buffer
 */
static inline void init_put_bits(PutBitContext *s, uint8_t *buffer, int buffer_size)
{
    if(buffer_size < 0) {
        buffer_size = 0;
        buffer = NULL;
    }

    s->size_in_bits= 8*buffer_size;
    s->buf = buffer;
    s->buf_end = s->buf + buffer_size;
#ifdef ALT_BITSTREAM_WRITER
    s->index=0;
    ((uint32_t*)(s->buf))[0]=0;
//    memset(buffer, 0, buffer_size);
#else
    s->buf_ptr = s->buf;
    s->bit_left=32;
    s->bit_buf=0;
#endif
}

/**
 * @return the total number of bits written to the bitstream.
 */
static inline int put_bits_count(PutBitContext *s)
{
#ifdef ALT_BITSTREAM_WRITER
    return s->index;
#else
    return (s->buf_ptr - s->buf) * 8 + 32 - s->bit_left;
#endif
}

/**
 * Pad the end of the output stream with zeros.
 */
static inline void flush_put_bits(PutBitContext *s)
{
#ifdef ALT_BITSTREAM_WRITER
    align_put_bits(s);
#else
#ifndef BITSTREAM_WRITER_LE
    if (s->bit_left < 32)
        s->bit_buf<<= s->bit_left;
#endif
    while (s->bit_left < 32) {
        /* XXX: should test end of buffer */
#ifdef BITSTREAM_WRITER_LE
        *s->buf_ptr++=s->bit_buf;
        s->bit_buf>>=8;
#else
        *s->buf_ptr++=s->bit_buf >> 24;
        s->bit_buf<<=8;
#endif
        s->bit_left+=8;
    }
    s->bit_left=32;
    s->bit_buf=0;
#endif
}

#if defined(ALT_BITSTREAM_WRITER) || defined(BITSTREAM_WRITER_LE)
#define align_put_bits align_put_bits_unsupported_here
#define ff_put_string ff_put_string_unsupported_here
#define ff_copy_bits ff_copy_bits_unsupported_here
#else
/**
 * Pad the bitstream with zeros up to the next byte boundary.
 */
void align_put_bits(PutBitContext *s);

/**
 * Put the string string in the bitstream.
 *
 * @param terminate_string 0-terminates the written string if value is 1
 */
void ff_put_string(PutBitContext *pb, const char *string, int terminate_string);

/**
 * Copy the content of src to the bitstream.
 *
 * @param length the number of bits of src to copy
 */
void ff_copy_bits(PutBitContext *pb, const uint8_t *src, int length);
#endif

/**
 * Write up to 31 bits into a bitstream.
 * Use put_bits32 to write 32 bits.
 */
static inline void put_bits(PutBitContext *s, int n, unsigned int value)
#ifndef ALT_BITSTREAM_WRITER
{
    unsigned int bit_buf;
    int bit_left;

    //    printf("put_bits=%d %x\n", n, value);
    assert(n <= 31 && value < (1U << n));

    bit_buf = s->bit_buf;
    bit_left = s->bit_left;

    //    printf("n=%d value=%x cnt=%d buf=%x\n", n, value, bit_cnt, bit_buf);
    /* XXX: optimize */
#ifdef BITSTREAM_WRITER_LE
    bit_buf |= value << (32 - bit_left);
    if (n >= bit_left) {
#if !HAVE_FAST_UNALIGNED
        if (3 & (intptr_t) s->buf_ptr) {
            AV_WL32(s->buf_ptr, bit_buf);
        } else
#endif
        *(uint32_t *)s->buf_ptr = av_le2ne32(bit_buf);
        s->buf_ptr+=4;
        bit_buf = (bit_left==32)?0:value >> bit_left;
        bit_left+=32;
    }
    bit_left-=n;
#else
    if (n < bit_left) {
        bit_buf = (bit_buf<<n) | value;
        bit_left-=n;
    } else {
        bit_buf<<=bit_left;
        bit_buf |= value >> (n - bit_left);
#if !HAVE_FAST_UNALIGNED
        if (3 & (intptr_t) s->buf_ptr) {
            AV_WB32(s->buf_ptr, bit_buf);
        } else
#endif
        *(uint32_t *)s->buf_ptr = av_be2ne32(bit_buf);
        //printf("bitbuf = %08x\n", bit_buf);
        s->buf_ptr+=4;
        bit_left+=32 - n;
        bit_buf = value;
    }
#endif

    s->bit_buf = bit_buf;
    s->bit_left = bit_left;
}
#else  /* ALT_BITSTREAM_WRITER defined */
{
#    ifdef ALIGNED_BITSTREAM_WRITER
#        if ARCH_X86
    __asm__ volatile(
        "movl %0, %%ecx                 \n\t"
        "xorl %%eax, %%eax              \n\t"
        "shrdl %%cl, %1, %%eax          \n\t"
        "shrl %%cl, %1                  \n\t"
        "movl %0, %%ecx                 \n\t"
        "shrl $3, %%ecx                 \n\t"
        "andl $0xFFFFFFFC, %%ecx        \n\t"
        "bswapl %1                      \n\t"
        "orl %1, (%2, %%ecx)            \n\t"
        "bswapl %%eax                   \n\t"
        "addl %3, %0                    \n\t"
        "movl %%eax, 4(%2, %%ecx)       \n\t"
        : "=&r" (s->index), "=&r" (value)
        : "r" (s->buf), "r" (n), "0" (s->index), "1" (value<<(-n))
        : "%eax", "%ecx"
    );
#        else
    int index= s->index;
    uint32_t *ptr= ((uint32_t *)s->buf)+(index>>5);

    value<<= 32-n;

    ptr[0] |= av_be2ne32(value>>(index&31));
    ptr[1]  = av_be2ne32(value<<(32-(index&31)));
//if(n>24) printf("%d %d\n", n, value);
    index+= n;
    s->index= index;
#        endif
#    else //ALIGNED_BITSTREAM_WRITER
#        if ARCH_X86
    __asm__ volatile(
        "movl $7, %%ecx                 \n\t"
        "andl %0, %%ecx                 \n\t"
        "addl %3, %%ecx                 \n\t"
        "negl %%ecx                     \n\t"
        "shll %%cl, %1                  \n\t"
        "bswapl %1                      \n\t"
        "movl %0, %%ecx                 \n\t"
        "shrl $3, %%ecx                 \n\t"
        "orl %1, (%%ecx, %2)            \n\t"
        "addl %3, %0                    \n\t"
        "movl $0, 4(%%ecx, %2)          \n\t"
        : "=&r" (s->index), "=&r" (value)
        : "r" (s->buf), "r" (n), "0" (s->index), "1" (value)
        : "%ecx"
    );
#        else
    int index= s->index;
    uint32_t *ptr= (uint32_t*)(((uint8_t *)s->buf)+(index>>3));

    ptr[0] |= av_be2ne32(value<<(32-n-(index&7) ));
    ptr[1] = 0;
//if(n>24) printf("%d %d\n", n, value);
    index+= n;
    s->index= index;
#        endif
#    endif //!ALIGNED_BITSTREAM_WRITER
}
#endif

static inline void put_sbits(PutBitContext *pb, int n, int32_t value)
{
    assert(n >= 0 && n <= 31);

    put_bits(pb, n, value & ((1<<n)-1));
}

/**
 * Write exactly 32 bits into a bitstream.
 */
static void av_unused put_bits32(PutBitContext *s, uint32_t value)
{
    int lo = value & 0xffff;
    int hi = value >> 16;
#ifdef BITSTREAM_WRITER_LE
    put_bits(s, 16, lo);
    put_bits(s, 16, hi);
#else
    put_bits(s, 16, hi);
    put_bits(s, 16, lo);
#endif
}

/**
 * Return the pointer to the byte where the bitstream writer will put
 * the next bit.
 */
static inline uint8_t* put_bits_ptr(PutBitContext *s)
{
#ifdef ALT_BITSTREAM_WRITER
        return s->buf + (s->index>>3);
#else
        return s->buf_ptr;
#endif
}

/**
 * Skip the given number of bytes.
 * PutBitContext must be flushed & aligned to a byte boundary before calling this.
 */
static inline void skip_put_bytes(PutBitContext *s, int n)
{
        assert((put_bits_count(s)&7)==0);
#ifdef ALT_BITSTREAM_WRITER
        FIXME may need some cleaning of the buffer
        s->index += n<<3;
#else
        assert(s->bit_left==32);
        s->buf_ptr += n;
#endif
}

/**
 * Skip the given number of bits.
 * Must only be used if the actual values in the bitstream do not matter.
 * If n is 0 the behavior is undefined.
 */
static inline void skip_put_bits(PutBitContext *s, int n)
{
#ifdef ALT_BITSTREAM_WRITER
    s->index += n;
#else
    s->bit_left -= n;
    s->buf_ptr-= 4*(s->bit_left>>5);
    s->bit_left &= 31;
#endif
}

/**
 * Change the end of the buffer.
 *
 * @param size the new size in bytes of the buffer where to put bits
 */
static inline void set_put_bits_buffer_size(PutBitContext *s, int size)
{
    s->buf_end= s->buf + size;
}

#endif /* AVCODEC_PUT_BITS_H */
