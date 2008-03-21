/*
 * Common bit i/o utils
 * Copyright (c) 2000, 2001 Fabrice Bellard.
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * alternative bitstream reader & writer by Michael Niedermayer <michaelni@gmx.at>
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
 * @file bitstream.c
 * bitstream api.
 */

#include "avcodec.h"
#include "bitstream.h"

/**
 * Same as av_mallocz_static(), but does a realloc.
 *
 * @param[in] ptr The block of memory to reallocate.
 * @param[in] size The requested size.
 * @return Block of memory of requested size.
 * @deprecated. Code which uses ff_realloc_static is broken/misdesigned
 * and should correctly use static arrays
 */
attribute_deprecated av_alloc_size(2)
void *ff_realloc_static(void *ptr, unsigned int size);

void align_put_bits(PutBitContext *s)
{
#ifdef ALT_BITSTREAM_WRITER
    put_bits(s,(  - s->index) & 7,0);
#else
    put_bits(s,s->bit_left & 7,0);
#endif
}

void ff_put_string(PutBitContext * pbc, const char *s, int put_zero)
{
    while(*s){
        put_bits(pbc, 8, *s);
        s++;
    }
    if(put_zero)
        put_bits(pbc, 8, 0);
}

void ff_copy_bits(PutBitContext *pb, const uint8_t *src, int length)
{
    const uint16_t *srcw= (const uint16_t*)src;
    int words= length>>4;
    int bits= length&15;
    int i;

    if(length==0) return;

    if(ENABLE_SMALL || words < 16 || put_bits_count(pb)&7){
        for(i=0; i<words; i++) put_bits(pb, 16, be2me_16(srcw[i]));
    }else{
        for(i=0; put_bits_count(pb)&31; i++)
            put_bits(pb, 8, src[i]);
        flush_put_bits(pb);
        memcpy(pbBufPtr(pb), src+i, 2*words-i);
        skip_put_bytes(pb, 2*words-i);
    }

    put_bits(pb, bits, be2me_16(srcw[words])>>(16-bits));
}

/* VLC decoding */

//#define DEBUG_VLC

#define GET_DATA(v, table, i, wrap, size) \
{\
    const uint8_t *ptr = (const uint8_t *)table + i * wrap;\
    switch(size) {\
    case 1:\
        v = *(const uint8_t *)ptr;\
        break;\
    case 2:\
        v = *(const uint16_t *)ptr;\
        break;\
    default:\
        v = *(const uint32_t *)ptr;\
        break;\
    }\
}


static int alloc_table(VLC *vlc, int size, int use_static)
{
    int index;
    index = vlc->table_size;
    vlc->table_size += size;
    if (vlc->table_size > vlc->table_allocated) {
        vlc->table_allocated += (1 << vlc->bits);
        if(use_static)
            vlc->table = ff_realloc_static(vlc->table,
                                           sizeof(VLC_TYPE) * 2 * vlc->table_allocated);
        else
            vlc->table = av_realloc(vlc->table,
                                    sizeof(VLC_TYPE) * 2 * vlc->table_allocated);
        if (!vlc->table)
            return -1;
    }
    return index;
}

static int build_table(VLC *vlc, int table_nb_bits,
                       int nb_codes,
                       const void *bits, int bits_wrap, int bits_size,
                       const void *codes, int codes_wrap, int codes_size,
                       const void *symbols, int symbols_wrap, int symbols_size,
                       uint32_t code_prefix, int n_prefix, int flags)
{
    int i, j, k, n, table_size, table_index, nb, n1, index, code_prefix2, symbol;
    uint32_t code;
    VLC_TYPE (*table)[2];

    table_size = 1 << table_nb_bits;
    table_index = alloc_table(vlc, table_size, flags & INIT_VLC_USE_STATIC);
#ifdef DEBUG_VLC
    av_log(NULL,AV_LOG_DEBUG,"new table index=%d size=%d code_prefix=%x n=%d\n",
           table_index, table_size, code_prefix, n_prefix);
#endif
    if (table_index < 0)
        return -1;
    table = &vlc->table[table_index];

    for(i=0;i<table_size;i++) {
        table[i][1] = 0; //bits
        table[i][0] = -1; //codes
    }

    /* first pass: map codes and compute auxillary table sizes */
    for(i=0;i<nb_codes;i++) {
        GET_DATA(n, bits, i, bits_wrap, bits_size);
        GET_DATA(code, codes, i, codes_wrap, codes_size);
        /* we accept tables with holes */
        if (n <= 0)
            continue;
        if (!symbols)
            symbol = i;
        else
            GET_DATA(symbol, symbols, i, symbols_wrap, symbols_size);
#if defined(DEBUG_VLC) && 0
        av_log(NULL,AV_LOG_DEBUG,"i=%d n=%d code=0x%x\n", i, n, code);
#endif
        /* if code matches the prefix, it is in the table */
        n -= n_prefix;
        if(flags & INIT_VLC_LE)
            code_prefix2= code & (n_prefix>=32 ? 0xffffffff : (1 << n_prefix)-1);
        else
            code_prefix2= code >> n;
        if (n > 0 && code_prefix2 == code_prefix) {
            if (n <= table_nb_bits) {
                /* no need to add another table */
                j = (code << (table_nb_bits - n)) & (table_size - 1);
                nb = 1 << (table_nb_bits - n);
                for(k=0;k<nb;k++) {
                    if(flags & INIT_VLC_LE)
                        j = (code >> n_prefix) + (k<<n);
#ifdef DEBUG_VLC
                    av_log(NULL, AV_LOG_DEBUG, "%4x: code=%d n=%d\n",
                           j, i, n);
#endif
                    if (table[j][1] /*bits*/ != 0) {
                        av_log(NULL, AV_LOG_ERROR, "incorrect codes\n");
                        return -1;
                    }
                    table[j][1] = n; //bits
                    table[j][0] = symbol;
                    j++;
                }
            } else {
                n -= table_nb_bits;
                j = (code >> ((flags & INIT_VLC_LE) ? n_prefix : n)) & ((1 << table_nb_bits) - 1);
#ifdef DEBUG_VLC
                av_log(NULL,AV_LOG_DEBUG,"%4x: n=%d (subtable)\n",
                       j, n);
#endif
                /* compute table size */
                n1 = -table[j][1]; //bits
                if (n > n1)
                    n1 = n;
                table[j][1] = -n1; //bits
            }
        }
    }

    /* second pass : fill auxillary tables recursively */
    for(i=0;i<table_size;i++) {
        n = table[i][1]; //bits
        if (n < 0) {
            n = -n;
            if (n > table_nb_bits) {
                n = table_nb_bits;
                table[i][1] = -n; //bits
            }
            index = build_table(vlc, n, nb_codes,
                                bits, bits_wrap, bits_size,
                                codes, codes_wrap, codes_size,
                                symbols, symbols_wrap, symbols_size,
                                (flags & INIT_VLC_LE) ? (code_prefix | (i << n_prefix)) : ((code_prefix << table_nb_bits) | i),
                                n_prefix + table_nb_bits, flags);
            if (index < 0)
                return -1;
            /* note: realloc has been done, so reload tables */
            table = &vlc->table[table_index];
            table[i][0] = index; //code
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

   'symbols' : table which gives the values to be returned from get_vlc().

   'xxx_wrap' : give the number of bytes between each entry of the
   'bits' or 'codes' tables.

   'xxx_size' : gives the number of bytes of each entry of the 'bits'
   or 'codes' tables.

   'wrap' and 'size' allows to use any memory configuration and types
   (byte/word/long) to store the 'bits', 'codes', and 'symbols' tables.

   'use_static' should be set to 1 for tables, which should be freed
   with av_free_static(), 0 if free_vlc() will be used.
*/
int init_vlc_sparse(VLC *vlc, int nb_bits, int nb_codes,
             const void *bits, int bits_wrap, int bits_size,
             const void *codes, int codes_wrap, int codes_size,
             const void *symbols, int symbols_wrap, int symbols_size,
             int flags)
{
    vlc->bits = nb_bits;
    if(!(flags & INIT_VLC_USE_STATIC)) {
        vlc->table = NULL;
        vlc->table_allocated = 0;
        vlc->table_size = 0;
    } else {
        /* Static tables are initially always NULL, return
           if vlc->table != NULL to avoid double allocation */
        if(vlc->table)
            return 0;
    }

#ifdef DEBUG_VLC
    av_log(NULL,AV_LOG_DEBUG,"build table nb_codes=%d\n", nb_codes);
#endif

    if (build_table(vlc, nb_bits, nb_codes,
                    bits, bits_wrap, bits_size,
                    codes, codes_wrap, codes_size,
                    symbols, symbols_wrap, symbols_size,
                    0, 0, flags) < 0) {
        av_freep(&vlc->table);
        return -1;
    }
    return 0;
}


void free_vlc(VLC *vlc)
{
    av_freep(&vlc->table);
}

