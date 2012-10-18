/*
 * LZW encoder
 * Copyright (c) 2007 Bartlomiej Wolowiec
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
 * LZW encoder
 * @author Bartlomiej Wolowiec
 */

#include "avcodec.h"
#include "put_bits.h"
#include "lzw.h"

#define LZW_MAXBITS 12
#define LZW_SIZTABLE (1<<LZW_MAXBITS)
#define LZW_HASH_SIZE 16411
#define LZW_HASH_SHIFT 6

#define LZW_PREFIX_EMPTY -1
#define LZW_PREFIX_FREE -2

/** One code in hash table */
typedef struct Code{
    /// Hash code of prefix, LZW_PREFIX_EMPTY if empty prefix, or LZW_PREFIX_FREE if no code
    int hash_prefix;
    int code;               ///< LZW code
    uint8_t suffix;         ///< Last character in code block
}Code;

/** LZW encode state */
typedef struct LZWEncodeState {
    int clear_code;          ///< Value of clear code
    int end_code;            ///< Value of end code
    Code tab[LZW_HASH_SIZE]; ///< Hash table
    int tabsize;             ///< Number of values in hash table
    int bits;                ///< Actual bits code
    int bufsize;             ///< Size of output buffer
    PutBitContext pb;        ///< Put bit context for output
    int maxbits;             ///< Max bits code
    int maxcode;             ///< Max value of code
    int output_bytes;        ///< Number of written bytes
    int last_code;           ///< Value of last output code or LZW_PREFIX_EMPTY
    enum FF_LZW_MODES mode;  ///< TIFF or GIF
    void (*put_bits)(PutBitContext *, int, unsigned); ///< GIF is LE while TIFF is BE
}LZWEncodeState;


const int ff_lzw_encode_state_size = sizeof(LZWEncodeState);

/**
 * Hash function adding character
 * @param head LZW code for prefix
 * @param add Character to add
 * @return New hash value
 */
static inline int hash(int head, const int add)
{
    head ^= (add << LZW_HASH_SHIFT);
    if (head >= LZW_HASH_SIZE)
        head -= LZW_HASH_SIZE;
    av_assert2(head >= 0 && head < LZW_HASH_SIZE);
    return head;
}

/**
 * Hash function calculates next hash value
 * @param head Actual hash code
 * @param offset Offset calculated by hashOffset
 * @return New hash value
 */
static inline int hashNext(int head, const int offset)
{
    head -= offset;
    if(head < 0)
        head += LZW_HASH_SIZE;
    return head;
}

/**
 * Hash function calculates hash offset
 * @param head Actual hash code
 * @return Hash offset
 */
static inline int hashOffset(const int head)
{
    return head ? LZW_HASH_SIZE - head : 1;
}

/**
 * Write one code to stream
 * @param s LZW state
 * @param c code to write
 */
static inline void writeCode(LZWEncodeState * s, int c)
{
    assert(0 <= c && c < 1 << s->bits);
    s->put_bits(&s->pb, s->bits, c);
}


/**
 * Find LZW code for block
 * @param s LZW state
 * @param c Last character in block
 * @param hash_prefix LZW code for prefix
 * @return LZW code for block or -1 if not found in table
 */
static inline int findCode(LZWEncodeState * s, uint8_t c, int hash_prefix)
{
    int h = hash(FFMAX(hash_prefix, 0), c);
    int hash_offset = hashOffset(h);

    while (s->tab[h].hash_prefix != LZW_PREFIX_FREE) {
        if ((s->tab[h].suffix == c)
            && (s->tab[h].hash_prefix == hash_prefix))
            return h;
        h = hashNext(h, hash_offset);
    }

    return h;
}

/**
 * Add block to LZW code table
 * @param s LZW state
 * @param c Last character in block
 * @param hash_prefix LZW code for prefix
 * @param hash_code LZW code for bytes block
 */
static inline void addCode(LZWEncodeState * s, uint8_t c, int hash_prefix, int hash_code)
{
    s->tab[hash_code].code = s->tabsize;
    s->tab[hash_code].suffix = c;
    s->tab[hash_code].hash_prefix = hash_prefix;

    s->tabsize++;

    if (s->tabsize >= (1 << s->bits) + (s->mode == FF_LZW_GIF))
        s->bits++;
}

/**
 * Clear LZW code table
 * @param s LZW state
 */
static void clearTable(LZWEncodeState * s)
{
    int i, h;

    writeCode(s, s->clear_code);
    s->bits = 9;
    for (i = 0; i < LZW_HASH_SIZE; i++) {
        s->tab[i].hash_prefix = LZW_PREFIX_FREE;
    }
    for (i = 0; i < 256; i++) {
        h = hash(0, i);
        s->tab[h].code = i;
        s->tab[h].suffix = i;
        s->tab[h].hash_prefix = LZW_PREFIX_EMPTY;
    }
    s->tabsize = 258;
}

/**
 * Calculate number of bytes written
 * @param s LZW encode state
 * @return Number of bytes written
 */
static int writtenBytes(LZWEncodeState *s){
    int ret = put_bits_count(&s->pb) >> 3;
    ret -= s->output_bytes;
    s->output_bytes += ret;
    return ret;
}

/**
 * Initialize LZW encoder. Please set s->clear_code, s->end_code and s->maxbits before run.
 * @param s LZW state
 * @param outbuf Output buffer
 * @param outsize Size of output buffer
 * @param maxbits Maximum length of code
 */
void ff_lzw_encode_init(LZWEncodeState *s, uint8_t *outbuf, int outsize,
                        int maxbits, enum FF_LZW_MODES mode,
                        void (*lzw_put_bits)(PutBitContext *, int, unsigned))
{
    s->clear_code = 256;
    s->end_code = 257;
    s->maxbits = maxbits;
    init_put_bits(&s->pb, outbuf, outsize);
    s->bufsize = outsize;
    assert(s->maxbits >= 9 && s->maxbits <= LZW_MAXBITS);
    s->maxcode = 1 << s->maxbits;
    s->output_bytes = 0;
    s->last_code = LZW_PREFIX_EMPTY;
    s->bits = 9;
    s->mode = mode;
    s->put_bits = lzw_put_bits;
}

/**
 * LZW main compress function
 * @param s LZW state
 * @param inbuf Input buffer
 * @param insize Size of input buffer
 * @return Number of bytes written or -1 on error
 */
int ff_lzw_encode(LZWEncodeState * s, const uint8_t * inbuf, int insize)
{
    int i;

    if(insize * 3 > (s->bufsize - s->output_bytes) * 2){
        return -1;
    }

    if (s->last_code == LZW_PREFIX_EMPTY)
        clearTable(s);

    for (i = 0; i < insize; i++) {
        uint8_t c = *inbuf++;
        int code = findCode(s, c, s->last_code);
        if (s->tab[code].hash_prefix == LZW_PREFIX_FREE) {
            writeCode(s, s->last_code);
            addCode(s, c, s->last_code, code);
            code= hash(0, c);
        }
        s->last_code = s->tab[code].code;
        if (s->tabsize >= s->maxcode - 1) {
            clearTable(s);
        }
    }

    return writtenBytes(s);
}

/**
 * Write end code and flush bitstream
 * @param s LZW state
 * @return Number of bytes written or -1 on error
 */
int ff_lzw_encode_flush(LZWEncodeState *s,
                        void (*lzw_flush_put_bits)(PutBitContext *))
{
    if (s->last_code != -1)
        writeCode(s, s->last_code);
    writeCode(s, s->end_code);
    lzw_flush_put_bits(&s->pb);
    s->last_code = -1;

    return writtenBytes(s);
}
