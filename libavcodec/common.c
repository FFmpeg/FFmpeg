/*
 * Common bit i/o utils
 * Copyright (c) 2000, 2001 Gerard Lantau.
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
#include "common.h"
#include <math.h>

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

#ifdef STATS
    st_out_bit_counts[st_current_index] += n;
#endif
    //    printf("put_bits=%d %x\n", n, value);
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
        *(UINT32 *)s->buf_ptr = be2me_32(bit_buf);
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
INT64 get_bit_count(PutBitContext *s)
{
    return (s->buf_ptr - s->buf + s->data_out_size) * 8 + (INT64)s->bit_cnt;
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

/* for jpeg : escape 0xff with 0x00 after it */
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

/* bit input functions */

void init_get_bits(GetBitContext *s, 
                   UINT8 *buffer, int buffer_size)
{
    s->buf = buffer;
    s->buf_ptr = buffer;
    s->buf_end = buffer + buffer_size;
    s->bit_cnt = 0;
    s->bit_buf = 0;
    while (s->buf_ptr < s->buf_end && 
           s->bit_cnt < 32) {
        s->bit_buf |= (*s->buf_ptr++ << (24 - s->bit_cnt));
        s->bit_cnt += 8;
    }
}

/* n must be >= 1 and <= 32 */
/* also true: n > s->bit_cnt */
unsigned int get_bits_long(GetBitContext *s, int n)
{
    unsigned int val;
    int bit_cnt;
    unsigned int bit_buf;

#ifdef STATS
    st_bit_counts[st_current_index] += n;
#endif

    bit_buf = s->bit_buf;
    bit_cnt = s->bit_cnt - n;
    
//    if (bit_cnt >= 0) {
//        val = bit_buf >> (32 - n);
//        bit_buf <<= n; 
//    } else 
    {
	UINT8 *buf_ptr;
        val = bit_buf >> (32 - n);
        buf_ptr = s->buf_ptr;
        buf_ptr += 4;
        /* handle common case: we can read everything */
        if (buf_ptr <= s->buf_end) {
#if ARCH_X86
	    bit_buf = bswap_32(*((unsigned long*)(&buf_ptr[-4])));
#else
	    bit_buf = (buf_ptr[-4] << 24) |
		(buf_ptr[-3] << 16) |
                (buf_ptr[-2] << 8) |
                (buf_ptr[-1]);	    
#endif
        } else {
            buf_ptr -= 4;
            bit_buf = 0;
            if (buf_ptr < s->buf_end)
                bit_buf |= *buf_ptr++ << 24;
            if (buf_ptr < s->buf_end)
                bit_buf |= *buf_ptr++ << 16;
            if (buf_ptr < s->buf_end)
                bit_buf |= *buf_ptr++ << 8;
            if (buf_ptr < s->buf_end)
                bit_buf |= *buf_ptr++;
        }
        s->buf_ptr = buf_ptr;
        val |= bit_buf >> (32 + bit_cnt);
        bit_buf <<= - bit_cnt;
        bit_cnt += 32;
    }
    s->bit_buf = bit_buf;
    s->bit_cnt = bit_cnt;
    return val;
}

void align_get_bits(GetBitContext *s)
{
    int n;
    n = s->bit_cnt & 7;
    if (n > 0) {
        get_bits(s, n);
    }
}

/* VLC decoding */

//#define DEBUG_VLC

#define GET_DATA(v, table, i, wrap, size) \
{\
    UINT8 *ptr = (UINT8 *)table + i * wrap;\
    switch(size) {\
    case 1:\
        v = *(UINT8 *)ptr;\
        break;\
    case 2:\
        v = *(UINT16 *)ptr;\
        break;\
    default:\
        v = *(UINT32 *)ptr;\
        break;\
    }\
}


static int alloc_table(VLC *vlc, int size)
{
    int index;
    index = vlc->table_size;
    vlc->table_size += size;
    if (vlc->table_size > vlc->table_allocated) {
        vlc->table_allocated += (1 << vlc->bits);
        vlc->table_bits = realloc(vlc->table_bits, 
                                  sizeof(INT8) * vlc->table_allocated);
        vlc->table_codes = realloc(vlc->table_codes,
                                   sizeof(INT16) * vlc->table_allocated);
        if (!vlc->table_bits ||
            !vlc->table_codes)
            return -1;
    }
    return index;
}

static int build_table(VLC *vlc, int table_nb_bits, 
                       int nb_codes,
                       const void *bits, int bits_wrap, int bits_size,
                       const void *codes, int codes_wrap, int codes_size,
                       UINT32 code_prefix, int n_prefix)
{
    int i, j, k, n, table_size, table_index, nb, n1, index;
    UINT32 code;
    INT8 *table_bits;
    INT16 *table_codes;

    table_size = 1 << table_nb_bits;
    table_index = alloc_table(vlc, table_size);
#ifdef DEBUG_VLC
    printf("new table index=%d size=%d code_prefix=%x n=%d\n", 
           table_index, table_size, code_prefix, n_prefix);
#endif
    if (table_index < 0)
        return -1;
    table_bits = &vlc->table_bits[table_index];
    table_codes = &vlc->table_codes[table_index];

    for(i=0;i<table_size;i++) {
        table_bits[i] = 0;
        table_codes[i] = -1;
    }

    /* first pass: map codes and compute auxillary table sizes */
    for(i=0;i<nb_codes;i++) {
        GET_DATA(n, bits, i, bits_wrap, bits_size);
        GET_DATA(code, codes, i, codes_wrap, codes_size);
        /* we accept tables with holes */
        if (n <= 0)
            continue;
#if defined(DEBUG_VLC) && 0
        printf("i=%d n=%d code=0x%x\n", i, n, code);
#endif
        /* if code matches the prefix, it is in the table */
        n -= n_prefix;
        if (n > 0 && (code >> n) == code_prefix) {
            if (n <= table_nb_bits) {
                /* no need to add another table */
                j = (code << (table_nb_bits - n)) & (table_size - 1);
                nb = 1 << (table_nb_bits - n);
                for(k=0;k<nb;k++) {
#ifdef DEBUG_VLC
                    printf("%4x: code=%d n=%d\n",
                           j, i, n);
#endif
                    if (table_bits[j] != 0) {
                        fprintf(stderr, "incorrect codes\n");
                        exit(1);
                    }
                    table_bits[j] = n;
                    table_codes[j] = i;
                    j++;
                }
            } else {
                n -= table_nb_bits;
                j = (code >> n) & ((1 << table_nb_bits) - 1);
#ifdef DEBUG_VLC
                printf("%4x: n=%d (subtable)\n",
                       j, n);
#endif
                /* compute table size */
                n1 = -table_bits[j];
                if (n > n1)
                    n1 = n;
                table_bits[j] = -n1;
            }
        }
    }

    /* second pass : fill auxillary tables recursively */
    for(i=0;i<table_size;i++) {
        n = table_bits[i];
        if (n < 0) {
            n = -n;
            if (n > table_nb_bits) {
                n = table_nb_bits;
                table_bits[i] = -n;
            }
            index = build_table(vlc, n, nb_codes,
                                bits, bits_wrap, bits_size,
                                codes, codes_wrap, codes_size,
                                (code_prefix << table_nb_bits) | i,
                                n_prefix + table_nb_bits);
            if (index < 0)
                return -1;
            /* note: realloc has been done, so reload tables */
            table_bits = &vlc->table_bits[table_index];
            table_codes = &vlc->table_codes[table_index];
            table_codes[i] = index;
        }
    }
    return table_index;
}


/* Build VLC decoding tables suitable for use with get_vlc().

   'nb_bits' set thee decoding table size (2^nb_bits) entries. The
   bigger it is, the faster is the decoding. But it should not be too
   big to save memory and L1 cache. '9' is a good compromise.
   
   'nb_codes' : number of vlcs codes

   'bits' : table which gives the size (in bits) of each vlc code.

   'codes' : table which gives the bit pattern of of each vlc code.

   'xxx_wrap' : give the number of bytes between each entry of the
   'bits' or 'codes' tables.

   'xxx_size' : gives the number of bytes of each entry of the 'bits'
   or 'codes' tables.

   'wrap' and 'size' allows to use any memory configuration and types
   (byte/word/long) to store the 'bits' and 'codes' tables.  
*/
int init_vlc(VLC *vlc, int nb_bits, int nb_codes,
             const void *bits, int bits_wrap, int bits_size,
             const void *codes, int codes_wrap, int codes_size)
{
    vlc->bits = nb_bits;
    vlc->table_bits = NULL;
    vlc->table_codes = NULL;
    vlc->table_allocated = 0;
    vlc->table_size = 0;
#ifdef DEBUG_VLC
    printf("build table nb_codes=%d\n", nb_codes);
#endif

    if (build_table(vlc, nb_bits, nb_codes,
                    bits, bits_wrap, bits_size,
                    codes, codes_wrap, codes_size,
                    0, 0) < 0) {
        if (vlc->table_bits)
            free(vlc->table_bits);
        if (vlc->table_codes)
            free(vlc->table_codes);
        return -1;
    }
    return 0;
}


void free_vlc(VLC *vlc)
{
    free(vlc->table_bits);
    free(vlc->table_codes);
}

int get_vlc(GetBitContext *s, VLC *vlc)
{
    int bit_cnt, code, n, nb_bits, index;
    UINT32 bit_buf;
    INT16 *table_codes;
    INT8 *table_bits;
    UINT8 *buf_ptr;

    SAVE_BITS(s);
    nb_bits = vlc->bits;
    table_codes = vlc->table_codes;
    table_bits = vlc->table_bits;
    for(;;) {
        SHOW_BITS(s, index, nb_bits);
        code = table_codes[index];
        n = table_bits[index];
        if (n > 0) {
            /* most common case */
            FLUSH_BITS(n);
#ifdef STATS
            st_bit_counts[st_current_index] += n;
#endif
            break;
        } else if (n == 0) {
            return -1;
        } else {
            FLUSH_BITS(nb_bits);
#ifdef STATS
            st_bit_counts[st_current_index] += nb_bits;
#endif
            nb_bits = -n;
            table_codes = vlc->table_codes + code;
            table_bits = vlc->table_bits + code;
        }
    }
    RESTORE_BITS(s);
    return code;
}
