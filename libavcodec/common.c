/*
 * Common bit i/o utils
 * Copyright (c) 2000, 2001 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * alternative bitstream reader & writer by Michael Niedermayer <michaelni@gmx.at>
 */
#include "common.h"

void init_put_bits(PutBitContext *s, 
                   UINT8 *buffer, int buffer_size,
                   void *opaque,
                   void (*write_data)(void *, UINT8 *, int))
{
    s->buf = buffer;
    s->buf_end = s->buf + buffer_size;
    s->data_out_size = 0;
    if(write_data!=NULL) 
    {
    	fprintf(stderr, "write Data callback is not supported\n");
    }
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

/* return the number of bits output */
INT64 get_bit_count(PutBitContext *s)
{
#ifdef ALT_BITSTREAM_WRITER
    return s->data_out_size * 8 + s->index;
#else
    return (s->buf_ptr - s->buf + s->data_out_size) * 8 + 32 - (INT64)s->bit_left;
#endif
}

void align_put_bits(PutBitContext *s)
{
#ifdef ALT_BITSTREAM_WRITER
    put_bits(s,(  - s->index) & 7,0);
#else
    put_bits(s,s->bit_left & 7,0);
#endif
}

/* pad the end of the output stream with zeros */
void flush_put_bits(PutBitContext *s)
{
#ifdef ALT_BITSTREAM_WRITER
    align_put_bits(s);
#else
    s->bit_buf<<= s->bit_left;
    while (s->bit_left < 32) {
        /* XXX: should test end of buffer */
        *s->buf_ptr++=s->bit_buf >> 24;
        s->bit_buf<<=8;
        s->bit_left+=8;
    }
    s->bit_left=32;
    s->bit_buf=0;
#endif
}

/* pad the end of the output stream with zeros */
#ifndef ALT_BITSTREAM_WRITER
void jflush_put_bits(PutBitContext *s)
{
    unsigned int b;
    s->bit_buf<<= s->bit_left;
    s->bit_buf |= ~1U >> (32 - s->bit_left); /* set all the unused bits to one */

    while (s->bit_left < 32) {
        b = s->bit_buf >> 24;
        *s->buf_ptr++ = b;
        if (b == 0xff)
            *s->buf_ptr++ = 0;
        s->bit_buf<<=8;
        s->bit_left+=8;
    }
    s->bit_left=32;
    s->bit_buf=0;
}
#else
void jflush_put_bits(PutBitContext *s)
{
    int num= (  - s->index) & 7;
    jput_bits(s, num,0xFF>>(8-num));
}
#endif

void put_string(PutBitContext * pbc, char *s)
{
    while(*s){
        put_bits(pbc, 8, *s);
        s++;
    }
    put_bits(pbc, 8, 0);
}

/* bit input functions */

void init_get_bits(GetBitContext *s, 
                   UINT8 *buffer, int buffer_size)
{
#ifdef ALT_BITSTREAM_READER
    s->index=0;
    s->buffer= buffer;
#else
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
#endif
    s->size= buffer_size;
}

#ifndef ALT_BITSTREAM_READER
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
#ifdef ARCH_X86
	    bit_buf = bswap_32(*((unsigned long*)(&buf_ptr[-4])));
#else
	    bit_buf = (buf_ptr[-4] << 24) |
		(buf_ptr[-3] << 16) |
                (buf_ptr[-2] << 8) |
                (buf_ptr[-1]);	    
#endif
            val |= bit_buf >> (32 + bit_cnt);
            bit_buf <<= - bit_cnt;
            bit_cnt += 32;
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

            val |= bit_buf >> (32 + bit_cnt);
            bit_buf <<= - bit_cnt;
            bit_cnt += 8*(buf_ptr - s->buf_ptr);
            if(bit_cnt<0) bit_cnt=0;
        }
        s->buf_ptr = buf_ptr;
    }
    s->bit_buf = bit_buf;
    s->bit_cnt = bit_cnt;
    return val;
}
#endif

void align_get_bits(GetBitContext *s)
{
#ifdef ALT_BITSTREAM_READER
    s->index= (s->index + 7) & (~7); 
#else
    int n;
    n = s->bit_cnt & 7;
    if (n > 0) {
        get_bits(s, n);
    }
#endif
}

int check_marker(GetBitContext *s, char *msg)
{
    int bit= get_bits1(s);
    if(!bit) printf("Marker bit missing %s\n", msg);

    return bit;
}

#ifndef ALT_BITSTREAM_READER
/* This function is identical to get_bits_long(), the */
/* only diference is that it doesn't touch the buffer */
/* it is usefull to see the buffer.                   */

unsigned int show_bits_long(GetBitContext *s, int n)
{
    unsigned int val;
    int bit_cnt;
    unsigned int bit_buf;
	UINT8 *buf_ptr;
	
    bit_buf = s->bit_buf;
    bit_cnt = s->bit_cnt - n;

    val = bit_buf >> (32 - n);
    buf_ptr = s->buf_ptr;
    buf_ptr += 4;

    /* handle common case: we can read everything */
    if (buf_ptr <= s->buf_end) {
#ifdef ARCH_X86
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
    val |= bit_buf >> (32 + bit_cnt);
    bit_buf <<= - bit_cnt;
    bit_cnt += 32;
    
    return val;
}
#endif

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
        av_free(vlc->table_bits);
        av_free(vlc->table_codes);
        return -1;
    }
    return 0;
}


void free_vlc(VLC *vlc)
{
    av_free(vlc->table_bits);
    av_free(vlc->table_codes);
}

int ff_gcd(int a, int b){
    if(b) return ff_gcd(b, a%b);
    else  return a;
}
