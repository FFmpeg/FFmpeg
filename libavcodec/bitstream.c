/*
 * Common bit i/o utils
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2010 Loren Merritt
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
 * @file
 * bitstream api.
 */

#include "libavutil/atomic.h"
#include "libavutil/avassert.h"
#include "libavutil/qsort.h"
#include "avcodec.h"
#include "internal.h"
#include "mathops.h"
#include "put_bits.h"
#include "vlc.h"

const uint8_t ff_log2_run[41]={
 0, 0, 0, 0, 1, 1, 1, 1,
 2, 2, 2, 2, 3, 3, 3, 3,
 4, 4, 5, 5, 6, 6, 7, 7,
 8, 9,10,11,12,13,14,15,
16,17,18,19,20,21,22,23,
24,
};

void avpriv_align_put_bits(PutBitContext *s)
{
    put_bits(s, s->bit_left & 7, 0);
}

void avpriv_put_string(PutBitContext *pb, const char *string,
                       int terminate_string)
{
    while (*string) {
        put_bits(pb, 8, *string);
        string++;
    }
    if (terminate_string)
        put_bits(pb, 8, 0);
}

void avpriv_copy_bits(PutBitContext *pb, const uint8_t *src, int length)
{
    int words = length >> 4;
    int bits  = length & 15;
    int i;

    if (length == 0)
        return;

    av_assert0(length <= put_bits_left(pb));

    if (CONFIG_SMALL || words < 16 || put_bits_count(pb) & 7) {
        for (i = 0; i < words; i++)
            put_bits(pb, 16, AV_RB16(src + 2 * i));
    } else {
        for (i = 0; put_bits_count(pb) & 31; i++)
            put_bits(pb, 8, src[i]);
        flush_put_bits(pb);
        memcpy(put_bits_ptr(pb), src + i, 2 * words - i);
        skip_put_bytes(pb, 2 * words - i);
    }

    put_bits(pb, bits, AV_RB16(src + 2 * words) >> (16 - bits));
}

/* VLC decoding */

#define GET_DATA(v, table, i, wrap, size)                   \
{                                                           \
    const uint8_t *ptr = (const uint8_t *)table + i * wrap; \
    switch(size) {                                          \
    case 1:                                                 \
        v = *(const uint8_t *)ptr;                          \
        break;                                              \
    case 2:                                                 \
        v = *(const uint16_t *)ptr;                         \
        break;                                              \
    default:                                                \
        v = *(const uint32_t *)ptr;                         \
        break;                                              \
    }                                                       \
}


static int alloc_table(VLC *vlc, int size, int use_static)
{
    int index = vlc->table_size;

    vlc->table_size += size;
    if (vlc->table_size > vlc->table_allocated) {
        if (use_static)
            abort(); // cannot do anything, init_vlc() is used with too little memory
        vlc->table_allocated += (1 << vlc->bits);
        vlc->table = av_realloc_f(vlc->table, vlc->table_allocated, sizeof(VLC_TYPE) * 2);
        if (!vlc->table) {
            vlc->table_allocated = 0;
            vlc->table_size = 0;
            return AVERROR(ENOMEM);
        }
        memset(vlc->table + vlc->table_allocated - (1 << vlc->bits), 0, sizeof(VLC_TYPE) * 2 << vlc->bits);
    }
    return index;
}

static av_always_inline uint32_t bitswap_32(uint32_t x)
{
    return (uint32_t)ff_reverse[ x        & 0xFF] << 24 |
           (uint32_t)ff_reverse[(x >> 8)  & 0xFF] << 16 |
           (uint32_t)ff_reverse[(x >> 16) & 0xFF] << 8  |
           (uint32_t)ff_reverse[ x >> 24];
}

typedef struct VLCcode {
    uint8_t bits;
    uint16_t symbol;
    /** codeword, with the first bit-to-be-read in the msb
     * (even if intended for a little-endian bitstream reader) */
    uint32_t code;
} VLCcode;

static int compare_vlcspec(const void *a, const void *b)
{
    const VLCcode *sa = a, *sb = b;
    return (sa->code >> 1) - (sb->code >> 1);
}
/**
 * Build VLC decoding tables suitable for use with get_vlc().
 *
 * @param vlc            the context to be initialized
 *
 * @param table_nb_bits  max length of vlc codes to store directly in this table
 *                       (Longer codes are delegated to subtables.)
 *
 * @param nb_codes       number of elements in codes[]
 *
 * @param codes          descriptions of the vlc codes
 *                       These must be ordered such that codes going into the same subtable are contiguous.
 *                       Sorting by VLCcode.code is sufficient, though not necessary.
 */
static int build_table(VLC *vlc, int table_nb_bits, int nb_codes,
                       VLCcode *codes, int flags)
{
    int table_size, table_index, index, code_prefix, symbol, subtable_bits;
    int i, j, k, n, nb, inc;
    uint32_t code;
    volatile VLC_TYPE (* volatile table)[2]; // the double volatile is needed to prevent an internal compiler error in gcc 4.2

    table_size = 1 << table_nb_bits;
    if (table_nb_bits > 30)
       return -1;
    table_index = alloc_table(vlc, table_size, flags & INIT_VLC_USE_NEW_STATIC);
    ff_dlog(NULL, "new table index=%d size=%d\n", table_index, table_size);
    if (table_index < 0)
        return table_index;
    table = (volatile VLC_TYPE (*)[2])&vlc->table[table_index];

    /* first pass: map codes and compute auxiliary table sizes */
    for (i = 0; i < nb_codes; i++) {
        n      = codes[i].bits;
        code   = codes[i].code;
        symbol = codes[i].symbol;
        ff_dlog(NULL, "i=%d n=%d code=0x%x\n", i, n, code);
        if (n <= table_nb_bits) {
            /* no need to add another table */
            j = code >> (32 - table_nb_bits);
            nb = 1 << (table_nb_bits - n);
            inc = 1;
            if (flags & INIT_VLC_LE) {
                j = bitswap_32(code);
                inc = 1 << n;
            }
            for (k = 0; k < nb; k++) {
                int bits = table[j][1];
                ff_dlog(NULL, "%4x: code=%d n=%d\n", j, i, n);
                if (bits != 0 && bits != n) {
                    av_log(NULL, AV_LOG_ERROR, "incorrect codes\n");
                    return AVERROR_INVALIDDATA;
                }
                table[j][1] = n; //bits
                table[j][0] = symbol;
                j += inc;
            }
        } else {
            /* fill auxiliary table recursively */
            n -= table_nb_bits;
            code_prefix = code >> (32 - table_nb_bits);
            subtable_bits = n;
            codes[i].bits = n;
            codes[i].code = code << table_nb_bits;
            for (k = i+1; k < nb_codes; k++) {
                n = codes[k].bits - table_nb_bits;
                if (n <= 0)
                    break;
                code = codes[k].code;
                if (code >> (32 - table_nb_bits) != code_prefix)
                    break;
                codes[k].bits = n;
                codes[k].code = code << table_nb_bits;
                subtable_bits = FFMAX(subtable_bits, n);
            }
            subtable_bits = FFMIN(subtable_bits, table_nb_bits);
            j = (flags & INIT_VLC_LE) ? bitswap_32(code_prefix) >> (32 - table_nb_bits) : code_prefix;
            table[j][1] = -subtable_bits;
            ff_dlog(NULL, "%4x: n=%d (subtable)\n",
                    j, codes[i].bits + table_nb_bits);
            index = build_table(vlc, subtable_bits, k-i, codes+i, flags);
            if (index < 0)
                return index;
            /* note: realloc has been done, so reload tables */
            table = (volatile VLC_TYPE (*)[2])&vlc->table[table_index];
            table[j][0] = index; //code
            i = k-1;
        }
    }

    for (i = 0; i < table_size; i++) {
        if (table[i][1] == 0) //bits
            table[i][0] = -1; //codes
    }

    return table_index;
}


/* Build VLC decoding tables suitable for use with get_vlc().

   'nb_bits' sets the decoding table size (2^nb_bits) entries. The
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

   'wrap' and 'size' make it possible to use any memory configuration and types
   (byte/word/long) to store the 'bits', 'codes', and 'symbols' tables.

   'use_static' should be set to 1 for tables, which should be freed
   with av_free_static(), 0 if ff_free_vlc() will be used.
*/
int ff_init_vlc_sparse(VLC *vlc_arg, int nb_bits, int nb_codes,
                       const void *bits, int bits_wrap, int bits_size,
                       const void *codes, int codes_wrap, int codes_size,
                       const void *symbols, int symbols_wrap, int symbols_size,
                       int flags)
{
    VLCcode *buf;
    int i, j, ret;
    VLCcode localbuf[1500]; // the maximum currently needed is 1296 by rv34
    VLC localvlc, *vlc;

    vlc = vlc_arg;
    vlc->bits = nb_bits;
    if (flags & INIT_VLC_USE_NEW_STATIC) {
        av_assert0(nb_codes + 1 <= FF_ARRAY_ELEMS(localbuf));
        buf = localbuf;
        localvlc = *vlc_arg;
        vlc = &localvlc;
        vlc->table_size = 0;
    } else {
        vlc->table           = NULL;
        vlc->table_allocated = 0;
        vlc->table_size      = 0;

        buf = av_malloc_array((nb_codes + 1), sizeof(VLCcode));
        if (!buf)
            return AVERROR(ENOMEM);
    }


    av_assert0(symbols_size <= 2 || !symbols);
    j = 0;
#define COPY(condition)\
    for (i = 0; i < nb_codes; i++) {                                        \
        GET_DATA(buf[j].bits, bits, i, bits_wrap, bits_size);               \
        if (!(condition))                                                   \
            continue;                                                       \
        if (buf[j].bits > 3*nb_bits || buf[j].bits>32) {                    \
            av_log(NULL, AV_LOG_ERROR, "Too long VLC (%d) in init_vlc\n", buf[j].bits);\
            if (!(flags & INIT_VLC_USE_NEW_STATIC))                         \
                av_free(buf);                                               \
            return -1;                                                      \
        }                                                                   \
        GET_DATA(buf[j].code, codes, i, codes_wrap, codes_size);            \
        if (buf[j].code >= (1LL<<buf[j].bits)) {                            \
            av_log(NULL, AV_LOG_ERROR, "Invalid code %x for %d in init_vlc\n", buf[j].code, i);\
            if (!(flags & INIT_VLC_USE_NEW_STATIC))                         \
                av_free(buf);                                               \
            return -1;                                                      \
        }                                                                   \
        if (flags & INIT_VLC_LE)                                            \
            buf[j].code = bitswap_32(buf[j].code);                          \
        else                                                                \
            buf[j].code <<= 32 - buf[j].bits;                               \
        if (symbols)                                                        \
            GET_DATA(buf[j].symbol, symbols, i, symbols_wrap, symbols_size) \
        else                                                                \
            buf[j].symbol = i;                                              \
        j++;                                                                \
    }
    COPY(buf[j].bits > nb_bits);
    // qsort is the slowest part of init_vlc, and could probably be improved or avoided
    AV_QSORT(buf, j, struct VLCcode, compare_vlcspec);
    COPY(buf[j].bits && buf[j].bits <= nb_bits);
    nb_codes = j;

    ret = build_table(vlc, nb_bits, nb_codes, buf, flags);

    if (flags & INIT_VLC_USE_NEW_STATIC) {
        if(vlc->table_size != vlc->table_allocated)
            av_log(NULL, AV_LOG_ERROR, "needed %d had %d\n", vlc->table_size, vlc->table_allocated);

        av_assert0(ret >= 0);
        *vlc_arg = *vlc;
    } else {
        av_free(buf);
        if (ret < 0) {
            av_freep(&vlc->table);
            return ret;
        }
    }
    return 0;
}


void ff_free_vlc(VLC *vlc)
{
    av_freep(&vlc->table);
}
