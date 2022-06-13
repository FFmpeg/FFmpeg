/*
 * API for creating VLC trees
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2010 Loren Merritt
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

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/error.h"
#include "libavutil/internal.h"
#include "libavutil/log.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"
#include "libavutil/qsort.h"
#include "libavutil/reverse.h"
#include "vlc.h"

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
    case 4:                                                 \
    default:                                                \
        av_assert1(size == 4);                              \
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
        vlc->table = av_realloc_f(vlc->table, vlc->table_allocated, sizeof(*vlc->table));
        if (!vlc->table) {
            vlc->table_allocated = 0;
            vlc->table_size = 0;
            return AVERROR(ENOMEM);
        }
        memset(vlc->table + vlc->table_allocated - (1 << vlc->bits), 0, sizeof(*vlc->table) << vlc->bits);
    }
    return index;
}

#define LOCALBUF_ELEMS 1500 // the maximum currently needed is 1296 by rv34

static av_always_inline uint32_t bitswap_32(uint32_t x)
{
    return (uint32_t)ff_reverse[ x        & 0xFF] << 24 |
           (uint32_t)ff_reverse[(x >> 8)  & 0xFF] << 16 |
           (uint32_t)ff_reverse[(x >> 16) & 0xFF] << 8  |
           (uint32_t)ff_reverse[ x >> 24];
}

typedef struct VLCcode {
    uint8_t bits;
    VLCBaseType symbol;
    /** codeword, with the first bit-to-be-read in the msb
     * (even if intended for a little-endian bitstream reader) */
    uint32_t code;
} VLCcode;

static int vlc_common_init(VLC *vlc, int nb_bits, int nb_codes,
                           VLCcode **buf, int flags)
{
    vlc->bits = nb_bits;
    vlc->table_size = 0;
    if (flags & INIT_VLC_USE_NEW_STATIC) {
        av_assert0(nb_codes <= LOCALBUF_ELEMS);
    } else {
        vlc->table           = NULL;
        vlc->table_allocated = 0;
    }
    if (nb_codes > LOCALBUF_ELEMS) {
        *buf = av_malloc_array(nb_codes, sizeof(VLCcode));
        if (!*buf)
            return AVERROR(ENOMEM);
    }

    return 0;
}

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
    int table_size, table_index;
    VLCElem *table;

    if (table_nb_bits > 30)
       return AVERROR(EINVAL);
    table_size = 1 << table_nb_bits;
    table_index = alloc_table(vlc, table_size, flags & INIT_VLC_USE_NEW_STATIC);
    ff_dlog(NULL, "new table index=%d size=%d\n", table_index, table_size);
    if (table_index < 0)
        return table_index;
    table = &vlc->table[table_index];

    /* first pass: map codes and compute auxiliary table sizes */
    for (int i = 0; i < nb_codes; i++) {
        int         n = codes[i].bits;
        uint32_t code = codes[i].code;
        int    symbol = codes[i].symbol;
        ff_dlog(NULL, "i=%d n=%d code=0x%"PRIx32"\n", i, n, code);
        if (n <= table_nb_bits) {
            /* no need to add another table */
            int   j = code >> (32 - table_nb_bits);
            int  nb = 1 << (table_nb_bits - n);
            int inc = 1;

            if (flags & INIT_VLC_OUTPUT_LE) {
                j = bitswap_32(code);
                inc = 1 << n;
            }
            for (int k = 0; k < nb; k++) {
                int   bits = table[j].len;
                int oldsym = table[j].sym;
                ff_dlog(NULL, "%4x: code=%d n=%d\n", j, i, n);
                if ((bits || oldsym) && (bits != n || oldsym != symbol)) {
                    av_log(NULL, AV_LOG_ERROR, "incorrect codes\n");
                    return AVERROR_INVALIDDATA;
                }
                table[j].len = n;
                table[j].sym = symbol;
                j += inc;
            }
        } else {
            /* fill auxiliary table recursively */
            uint32_t code_prefix;
            int index, subtable_bits, j, k;

            n -= table_nb_bits;
            code_prefix = code >> (32 - table_nb_bits);
            subtable_bits = n;
            codes[i].bits = n;
            codes[i].code = code << table_nb_bits;
            for (k = i + 1; k < nb_codes; k++) {
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
            j = (flags & INIT_VLC_OUTPUT_LE) ? bitswap_32(code_prefix) >> (32 - table_nb_bits) : code_prefix;
            table[j].len = -subtable_bits;
            ff_dlog(NULL, "%4x: n=%d (subtable)\n",
                    j, codes[i].bits + table_nb_bits);
            index = build_table(vlc, subtable_bits, k-i, codes+i, flags);
            if (index < 0)
                return index;
            /* note: realloc has been done, so reload tables */
            table = &vlc->table[table_index];
            table[j].sym = index;
            if (table[j].sym != index) {
                avpriv_request_sample(NULL, "strange codes");
                return AVERROR_PATCHWELCOME;
            }
            i = k-1;
        }
    }

    for (int i = 0; i < table_size; i++) {
        if (table[i].len == 0)
            table[i].sym = -1;
    }

    return table_index;
}

static int vlc_common_end(VLC *vlc, int nb_bits, int nb_codes, VLCcode *codes,
                          int flags, VLCcode localbuf[LOCALBUF_ELEMS])
{
    int ret = build_table(vlc, nb_bits, nb_codes, codes, flags);

    if (flags & INIT_VLC_USE_NEW_STATIC) {
        if (vlc->table_size != vlc->table_allocated &&
            !(flags & (INIT_VLC_STATIC_OVERLONG & ~INIT_VLC_USE_NEW_STATIC)))
            av_log(NULL, AV_LOG_ERROR, "needed %d had %d\n", vlc->table_size, vlc->table_allocated);
        av_assert0(ret >= 0);
    } else {
        if (codes != localbuf)
            av_free(codes);
        if (ret < 0) {
            av_freep(&vlc->table);
            return ret;
        }
    }
    return 0;
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
   or 'codes' tables. Currently 1,2 and 4 are supported.

   'wrap' and 'size' make it possible to use any memory configuration and types
   (byte/word/long) to store the 'bits', 'codes', and 'symbols' tables.
*/
int ff_init_vlc_sparse(VLC *vlc, int nb_bits, int nb_codes,
                       const void *bits, int bits_wrap, int bits_size,
                       const void *codes, int codes_wrap, int codes_size,
                       const void *symbols, int symbols_wrap, int symbols_size,
                       int flags)
{
    VLCcode localbuf[LOCALBUF_ELEMS], *buf = localbuf;
    int j, ret;

    ret = vlc_common_init(vlc, nb_bits, nb_codes, &buf, flags);
    if (ret < 0)
        return ret;

    av_assert0(symbols_size <= 2 || !symbols);
    j = 0;
#define COPY(condition)\
    for (int i = 0; i < nb_codes; i++) {                                    \
        unsigned len;                                                       \
        GET_DATA(len, bits, i, bits_wrap, bits_size);                       \
        if (!(condition))                                                   \
            continue;                                                       \
        if (len > 3*nb_bits || len > 32) {                                  \
            av_log(NULL, AV_LOG_ERROR, "Too long VLC (%u) in init_vlc\n", len);\
            if (buf != localbuf)                                            \
                av_free(buf);                                               \
            return AVERROR(EINVAL);                                         \
        }                                                                   \
        buf[j].bits = len;                                                  \
        GET_DATA(buf[j].code, codes, i, codes_wrap, codes_size);            \
        if (buf[j].code >= (1LL<<buf[j].bits)) {                            \
            av_log(NULL, AV_LOG_ERROR, "Invalid code %"PRIx32" for %d in "  \
                   "init_vlc\n", buf[j].code, i);                           \
            if (buf != localbuf)                                            \
                av_free(buf);                                               \
            return AVERROR(EINVAL);                                         \
        }                                                                   \
        if (flags & INIT_VLC_INPUT_LE)                                      \
            buf[j].code = bitswap_32(buf[j].code);                          \
        else                                                                \
            buf[j].code <<= 32 - buf[j].bits;                               \
        if (symbols)                                                        \
            GET_DATA(buf[j].symbol, symbols, i, symbols_wrap, symbols_size) \
        else                                                                \
            buf[j].symbol = i;                                              \
        j++;                                                                \
    }
    COPY(len > nb_bits);
    // qsort is the slowest part of init_vlc, and could probably be improved or avoided
    AV_QSORT(buf, j, struct VLCcode, compare_vlcspec);
    COPY(len && len <= nb_bits);
    nb_codes = j;

    return vlc_common_end(vlc, nb_bits, nb_codes, buf,
                          flags, localbuf);
}

int ff_init_vlc_from_lengths(VLC *vlc, int nb_bits, int nb_codes,
                             const int8_t *lens, int lens_wrap,
                             const void *symbols, int symbols_wrap, int symbols_size,
                             int offset, int flags, void *logctx)
{
    VLCcode localbuf[LOCALBUF_ELEMS], *buf = localbuf;
    uint64_t code;
    int ret, j, len_max = FFMIN(32, 3 * nb_bits);

    ret = vlc_common_init(vlc, nb_bits, nb_codes, &buf, flags);
    if (ret < 0)
        return ret;

    j = code = 0;
    for (int i = 0; i < nb_codes; i++, lens += lens_wrap) {
        int len = *lens;
        if (len > 0) {
            unsigned sym;

            buf[j].bits = len;
            if (symbols)
                GET_DATA(sym, symbols, i, symbols_wrap, symbols_size)
            else
                sym = i;
            buf[j].symbol = sym + offset;
            buf[j++].code = code;
        } else if (len <  0) {
            len = -len;
        } else
            continue;
        if (len > len_max || code & ((1U << (32 - len)) - 1)) {
            av_log(logctx, AV_LOG_ERROR, "Invalid VLC (length %u)\n", len);
            goto fail;
        }
        code += 1U << (32 - len);
        if (code > UINT32_MAX + 1ULL) {
            av_log(logctx, AV_LOG_ERROR, "Overdetermined VLC tree\n");
            goto fail;
        }
    }
    return vlc_common_end(vlc, nb_bits, j, buf, flags, localbuf);
fail:
    if (buf != localbuf)
        av_free(buf);
    return AVERROR_INVALIDDATA;
}

void ff_free_vlc(VLC *vlc)
{
    av_freep(&vlc->table);
}
