/*
 * Copyright (c) 2022 Caleb Etemesi <etemesicaleb@gmail.com>
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

/*
 * Copyright 2019 - 2021, Osamu Watanabe
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS” AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "libavutil/avassert.h"
#include "libavutil/mem.h"
#include "jpeg2000htdec.h"
#include "jpeg2000.h"
#include "jpeg2000dec.h"

#define J2K_Q1 0
#define J2K_Q2 1

#define HT_SHIFT_SIGMA 0
#define HT_SHIFT_SCAN 4
#define HT_SHIFT_REF 3
#define HT_SHIFT_REF_IND 2

/* See Rec. ITU-T T.800, Table 2 */
const static uint8_t mel_e[13] = { 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 4, 5 };

static const uint16_t dec_cxt_vlc_table1[1024];
static const uint16_t dec_cxt_vlc_table0[1024];

typedef struct StateVars {
    int32_t pos;
    uint32_t bits;
    uint32_t tmp;
    uint32_t last;
    uint8_t bits_left;
    uint64_t bit_buf;
} StateVars;

typedef struct MelDecoderState {
    uint8_t k;
    uint8_t run;
    uint8_t one;
} MelDecoderState;

/**
 *  Given a precomputed c, checks whether n % d == 0. c is precomputed from d
 *  using precompute_c().
 */
av_always_inline
static uint32_t is_divisible(uint32_t n, uint64_t c)
{
    return n * c <= c - 1;
}

/**
 *  Precompute the number c used by is_divisible().
 */
av_always_inline
static uint64_t precompute_c(uint32_t d)
{
    return 1 + (0xffffffffffffffffull / d);
}

static void jpeg2000_init_zero(StateVars *s)
{
    s->bits_left = 0;
    s->bit_buf   = 0;
    s->tmp       = 0;
    s->bits      = 0;
    s->pos       = 0;
    s->last      = 0;
}

static void jpeg2000_init_mel(StateVars *s, uint32_t Pcup)
{
    jpeg2000_init_zero(s);
    s->pos = Pcup;
}

static void jpeg2000_init_mag_ref(StateVars *s, uint32_t Lref)
{
    s->pos       = Lref - 1;
    s->bits      = 0;
    s->last      = 0xFF;
    s->tmp       = 0;
    s->bits_left = 0;
    s->bit_buf   = 0;
}

static void jpeg2000_init_mel_decoder(MelDecoderState *mel_state)
{
    mel_state->k   = 0;
    mel_state->run = 0;
    mel_state->one = 0;
}

/**
 * Refill the buffer backwards in little endian while skipping over stuffing
 * bits. Stuffing bits are those that appear in the position of any byte whose
 * LSBs are all 1's if the last consumed byte was larger than 0x8F.
 */
static int jpeg2000_bitbuf_refill_backwards(StateVars *buffer, const uint8_t *array)
{
    uint64_t tmp = 0;
    uint32_t new_bits = 32;

    buffer->last = array[buffer->pos + 1];

    if (buffer->bits_left >= 32)
        return 0; // enough data, no need to pull in more bits

    /**
     *  Unstuff bits. Load a temporary byte, which precedes the position we
     *  currently at, to ensure that we can also un-stuff if the stuffed bit is
     *  the bottom most bits.
     */

    if (buffer->pos >= 3) {  // Common case; we have at least 4 bytes available
         tmp = array[buffer->pos - 3];
         tmp = (tmp << 8) | array[buffer->pos - 2];
         tmp = (tmp << 8) | array[buffer->pos - 1];
         tmp = (tmp << 8) | array[buffer->pos];
         tmp = (tmp << 8) | buffer->last;  // For stuffing bit detection
         buffer->pos -= 4;
    } else {
        if (buffer->pos >= 2)
            tmp = array[buffer->pos - 2];
        if (buffer->pos >= 1)
            tmp = (tmp << 8) | array[buffer->pos - 1];
        if (buffer->pos >= 0)
            tmp = (tmp << 8) | array[buffer->pos];
        buffer->pos = 0;
        tmp = (tmp << 8) | buffer->last;  // For stuffing bit detection
    }
    // Now remove any stuffing bits, shifting things down as we go
    if ((tmp & 0x7FFF000000) > 0x7F8F000000) {
        tmp &= 0x7FFFFFFFFF;
        new_bits--;
    }
    if ((tmp & 0x007FFF0000) > 0x007F8F0000) {
        tmp = (tmp & 0x007FFFFFFF) + ((tmp & 0xFF00000000) >> 1);
        new_bits--;
    }
    if ((tmp & 0x00007FFF00) > 0x00007F8F00) {
        tmp = (tmp & 0x00007FFFFF) + ((tmp & 0xFFFF000000) >> 1);
        new_bits--;
    }
    if ((tmp & 0x0000007FFF) > 0x0000007F8F) {
        tmp = (tmp & 0x0000007FFF) + ((tmp & 0xFFFFFF0000) >> 1);
        new_bits--;
    }
    tmp >>= 8;  // Shifts away the extra byte we imported

    /* Add bits to the MSB of the bit buffer */
    buffer->bit_buf |= tmp << buffer->bits_left;
    buffer->bits_left += new_bits;
    return 0;
}

/**
 * Refill the bit-buffer reading new bits going forward
 * in the stream while skipping over stuffed bits.
 */
static void jpeg2000_bitbuf_refill_forward(StateVars *buffer, const uint8_t *array,
                                           uint32_t length)
{
    while (buffer->bits_left < 32) {
        buffer->tmp = 0xFF;
        buffer->bits = (buffer->last == 0xFF) ? 7 : 8;
        if (buffer->pos < length) {
            buffer->tmp = array[buffer->pos];
            buffer->pos += 1;
            buffer->last = buffer->tmp;
        }
        buffer->bit_buf |= ((uint64_t) buffer->tmp) << buffer->bits_left;
        buffer->bits_left += buffer->bits;
    }
}

/**
 * Drops bits from lower bits in the bit buffer. buf contains the bit buffers.
 * nbits is the number of bits to remove.
 */
av_always_inline
static void jpeg2000_bitbuf_drop_bits_lsb(StateVars *buf, uint8_t nbits)
{
    av_assert2(buf->bits_left >= nbits); // cannot read more bits than available
    buf->bit_buf >>= nbits;
    buf->bits_left -= nbits;
}

/**
 * Get bits from the bit buffer reading them from the least significant bits
 * moving to the most significant bits. In case there are fewer bits, refill
 * from buf moving backwards.
 */
av_always_inline
static uint64_t jpeg2000_bitbuf_get_bits_lsb(StateVars *bit_stream, uint8_t nbits,
                                             const uint8_t *buf)
{
    uint64_t bits;
    uint64_t mask = (1ull << nbits) - 1;
    if (bit_stream->bits_left < nbits)
        jpeg2000_bitbuf_refill_backwards(bit_stream, buf);
    bits = bit_stream->bit_buf & mask;
    jpeg2000_bitbuf_drop_bits_lsb(bit_stream, nbits);
    return bits;
}

/**
 * Get bits from the bit buffer reading them from the least significant bits
 * moving to the most significant bits. In case there are fewer bits, refill from
 * buf moving forward.
 */
av_always_inline
static uint64_t jpeg2000_bitbuf_get_bits_lsb_forward(StateVars *bit_stream,
                                                     uint8_t nbits, const uint8_t *buf,
                                                     uint32_t length)
{
    uint64_t bits;
    uint64_t mask = (1ull << nbits) - 1;

    if (bit_stream->bits_left <= nbits)
        jpeg2000_bitbuf_refill_forward(bit_stream, buf, length);
    bits = bit_stream->bit_buf & mask;
    jpeg2000_bitbuf_drop_bits_lsb(bit_stream, nbits);
    return bits;
}

/**
 * Look ahead bit buffer without discarding bits.
 */
av_always_inline
static uint64_t jpeg2000_bitbuf_peek_bits_lsb(StateVars *stream, uint8_t nbits)
{
    uint64_t mask = (1ull << nbits) - 1;
    return stream->bit_buf & mask;
}

static void jpeg2000_init_vlc(StateVars *s, uint32_t Lcup, uint32_t Pcup,
                              const uint8_t *Dcup)
{
    s->bits_left = 0;
    s->bit_buf   = 0;
    s->pos       = Lcup - 2 - Pcup;
    s->last      = Dcup[Lcup - 2];
    s->tmp       = (s->last) >> 4;
    s->bits      = ((s->tmp & 7) < 7) ? 4 : 3;

    jpeg2000_bitbuf_refill_backwards(s, Dcup + Pcup);
    jpeg2000_bitbuf_drop_bits_lsb(s, 4);
}

/**
 * Decode prefix codes for VLC segment. See Rec. ITU-T T.814, 7.3.5.
 */
av_always_inline
static int  jpeg2000_decode_ctx_vlc(const Jpeg2000DecoderContext *s,
                                    StateVars *vlc_stream, const uint16_t *table,
                                    const uint8_t *Dcup, uint8_t *sig_pat,
                                    uint8_t *res_off, uint8_t *emb_pat_k,
                                    uint8_t *emb_pat_1, uint8_t pos,
                                    uint32_t Pcup, uint16_t context)
{
    uint32_t value;
    uint8_t len;
    uint64_t index;
    uint64_t code_word;

    jpeg2000_bitbuf_refill_backwards(vlc_stream, Dcup + Pcup);

    code_word = vlc_stream->bit_buf & 0x7f;
    index = code_word + (context << 7);

    av_assert0(index < 1024); // The CxtVLC table has 1024 entries.

    value = table[index];

    len = (value & 0x000F) >> 1;

    res_off[pos] = (uint8_t) (value & 1);
    sig_pat[pos] = (uint8_t) ((value & 0x00F0) >> 4);
    emb_pat_k[pos] = (uint8_t) ((value & 0x0F00) >> 8);
    emb_pat_1[pos] = (uint8_t) ((value & 0xF000) >> 12);

    jpeg2000_bitbuf_drop_bits_lsb(vlc_stream, len);
    return 0;
}

/**
 * Decode variable length u-vlc prefix. See decodeUPrefix procedure at Rec.
 * ITU-T T.814, 7.3.6.
 */
av_always_inline
static uint8_t vlc_decode_u_prefix(StateVars *vlc_stream, const uint8_t *refill_array)
{
    static const uint8_t return_value[8] = { 5, 1, 2, 1, 3, 1, 2, 1 };
    static const uint8_t drop_bits[8]    = { 3, 1, 2, 1, 3, 1, 2, 1 };

    uint8_t bits;

    if (vlc_stream->bits_left < 3)
        jpeg2000_bitbuf_refill_backwards(vlc_stream, refill_array);

    bits = jpeg2000_bitbuf_peek_bits_lsb(vlc_stream, 3);

    jpeg2000_bitbuf_drop_bits_lsb(vlc_stream, drop_bits[bits]);
    return return_value[bits];
}

/**
 * Decode variable length u-vlc suffix. See decodeUSuffix procedure at Rec.
 * ITU-T T.814, 7.3.6.
 */
av_always_inline
static uint8_t vlc_decode_u_suffix(StateVars *vlc_stream, uint8_t suffix,
                                   const uint8_t *refill_array)
{
    static const int mask[]      = { 1, 31 };
    static const int drop_bits[] = { 1, 5  };

    uint8_t bits;
    int cond = suffix != 3;
    if (suffix < 3)
        return 0;

    if (vlc_stream->bits_left < 5)
        jpeg2000_bitbuf_refill_backwards(vlc_stream, refill_array);

    bits = jpeg2000_bitbuf_peek_bits_lsb(vlc_stream, 5);

    jpeg2000_bitbuf_drop_bits_lsb(vlc_stream, drop_bits[cond]);
    return bits & mask[cond];
}

/**
 * Decode u-vlc extension values. See decodeUExtension procedure at Rec. ITU-T
 * T.814, 7.3.6.
 */
av_always_inline
static uint8_t vlc_decode_u_extension(StateVars *vlc_stream, uint8_t suffix,
                                      const uint8_t *refill_array)
{
    return jpeg2000_bitbuf_get_bits_lsb(vlc_stream, 4 * (suffix >= 28), refill_array);
}

/**
 * Magnitude and Sign decode procedures. See decodeMagSgnValue procedure at Rec.
 * ITU-T T.814, 7.3.8.
 */
av_always_inline
static int32_t jpeg2000_decode_mag_sgn(StateVars *mag_sgn_stream, int32_t m_n,
                                       int32_t i_n, const uint8_t *buf, uint32_t length)
{
    int32_t val = 0;
    if (m_n > 0) {
        val = jpeg2000_bitbuf_get_bits_lsb_forward(mag_sgn_stream,m_n,buf,length);
        val += (i_n << m_n);
    }
    return val;
}

av_always_inline
static void recover_mag_sgn(StateVars *mag_sgn, uint8_t pos, uint16_t q, int32_t m_n[2],
                            int32_t known_1[2], const uint8_t emb_pat_1[2],
                            int32_t v[2][4], int32_t m[2][4], uint8_t *E,
                            uint32_t *mu_n, const uint8_t *Dcup, uint32_t Pcup,
                            uint32_t pLSB)
{
    for (int i = 0; i < 4; i++) {
        int32_t n = 4 * q + i;
        m_n[pos] = m[pos][i];
        known_1[pos] = (emb_pat_1[pos] >> i) & 1;
        v[pos][i] = jpeg2000_decode_mag_sgn(mag_sgn, m_n[pos], known_1[pos], Dcup, Pcup);

        if (m_n[pos] != 0) {
            E[n] = 32 - ff_clz(v[pos][i] | 1);
            mu_n[n] = (v[pos][i] >> 1) + 1;
            mu_n[n] <<= pLSB;
            mu_n[n] |= (1 << (pLSB - 1)); // Add 0.5 (reconstruction parameter = 1/2)
            mu_n[n] |= ((uint32_t) (v[pos][i] & 1)) << 31; // sign bit.
        }
    }
}

static int jpeg2000_import_bit(StateVars *stream, const uint8_t *array, uint32_t length)
{
    int cond = stream->pos < length;
    int pos = FFMIN(stream->pos, length - 1);
    if (stream->bits == 0) {
        stream->bits = (stream->tmp == 0xFF) ? 7 : 8;
        stream->pos += cond;
        stream->tmp = cond ? array[pos] : 0xFF;
    }
    stream->bits -= 1;
    return (stream->tmp >> stream->bits) & 1;
}

static int jpeg2000_peek_bit(StateVars *stream, const uint8_t *array, uint32_t length)
{
    uint8_t bit;

    if (stream->bits == 0) {
        stream->bits = (stream->last == 0xFF) ? 7 : 8;
        if (stream->pos < length) {
            stream->tmp = array[stream->pos];
            stream->pos++;
        } else {
            stream->tmp = 0;
        }
        stream->last = stream->tmp;
    }
    bit = stream->tmp & 1;
    stream->tmp >>= 1;
    stream->bits--;
    return  bit;
}

static int jpeg2000_decode_mel_sym(MelDecoderState *mel_state,
                                   StateVars *mel_stream,
                                   const uint8_t *Dcup,
                                   uint32_t Lcup)
{

    if (mel_state->run == 0 && mel_state->one == 0) {
        uint8_t eval;
        uint8_t bit;

        eval = mel_e[mel_state->k];
        bit = jpeg2000_import_bit(mel_stream, Dcup, Lcup);
        if (bit == 1) {
            mel_state->run = 1 << eval;
            mel_state->k = FFMIN(12, mel_state->k + 1);
        } else {
            mel_state->run = 0;
            while (eval > 0) {
                bit = jpeg2000_import_bit(mel_stream, Dcup, Lcup);
                mel_state->run = (2 * (mel_state->run)) + bit;
                eval -= 1;
            }
            mel_state->k = FFMAX(0, mel_state->k - 1);
            mel_state->one = 1;
        }
    }
    if (mel_state->run > 0) {
        mel_state->run -= 1;
        return 0;
    } else {
        mel_state->one = 0;
        return 1;
    }
}

/**
 * Magref decoding procedures.
 */
av_always_inline
static int jpeg2000_import_magref_bit(StateVars *stream, const uint8_t *array,
                                      uint32_t length)
{
    return jpeg2000_bitbuf_get_bits_lsb(stream, 1, array);
}

/**
 * Signal EMB decode.
 */
static int jpeg2000_decode_sig_emb(const Jpeg2000DecoderContext *s, MelDecoderState *mel_state,
                                   StateVars *mel_stream, StateVars *vlc_stream,
                                   const uint16_t *vlc_table, const uint8_t *Dcup,
                                   uint8_t *sig_pat, uint8_t *res_off, uint8_t *emb_pat_k,
                                   uint8_t *emb_pat_1, uint8_t pos, uint16_t context,
                                   uint32_t Lcup, uint32_t Pcup)
{
    if (context == 0) {
        uint8_t sym;
        sym = jpeg2000_decode_mel_sym(mel_state, mel_stream, Dcup, Lcup);
        if (sym == 0) {
            sig_pat[pos] = 0;
            res_off[pos] = 0;
            emb_pat_k[pos] = 0;
            emb_pat_1[pos] = 0;
            return 0;
        }
    }
    return jpeg2000_decode_ctx_vlc(s, vlc_stream, vlc_table, Dcup, sig_pat,
                                   res_off, emb_pat_k, emb_pat_1, pos, Pcup,
                                   context);
}

av_always_inline
static int jpeg2000_get_state(int x1, int x2, int stride, int shift_by,
                              const uint8_t *block_states)
{
    return (block_states[(x1 + 1) * stride + (x2 + 1)] >> shift_by) & 1;
}

av_always_inline
static void jpeg2000_modify_state(int x1, int x2, int stride,
                                  int value, uint8_t *block_states)
{
    block_states[(x1 + 1) * stride + (x2 + 1)] |= value;
}

av_always_inline
static int jpeg2000_decode_ht_cleanup_segment(const Jpeg2000DecoderContext *s,
                                              Jpeg2000Cblk *cblk, Jpeg2000T1Context *t1,
                                              MelDecoderState *mel_state,
                                              StateVars *mel_stream, StateVars *vlc_stream,
                                              StateVars *mag_sgn_stream, const uint8_t *Dcup,
                                              uint32_t Lcup, uint32_t Pcup, uint8_t pLSB,
                                              int width, int height, const int stride,
                                              int32_t *sample_buf, uint8_t *block_states)
{
    uint16_t q                      = 0;     // Represents current quad position
    uint16_t q1, q2;
    uint16_t context1, context2;
    uint16_t context                = 0;

    uint8_t sig_pat[2]              = { 0 }; // significance pattern
    uint8_t res_off[2]              = { 0 }; // residual offset
    uint8_t emb_pat_k[2]            = { 0 }; // exponent Max Bound pattern K
    uint8_t emb_pat_1[2]            = { 0 }; // exponent Max Bound pattern 1
    uint8_t gamma[2]                = { 0 };

    uint8_t E_n[2]                  = { 0 };
    uint8_t E_ne[2]                 = { 0 };
    uint8_t E_nw[2]                 = { 0 };
    uint8_t E_nf[2]                 = { 0 };

    uint8_t max_e[2]                = { 0 };
    uint8_t u_pfx[2]                = { 0 };
    uint8_t u_sfx[2]                = { 0 };
    uint8_t u_ext[2]                = { 0 };

    int32_t u[2]                    = { 0 };
    int32_t U[2]                    = { 0 }; // exponent bound
    int32_t m_n[2]                  = { 0 };
    int32_t known_1[2]              = { 0 };

    int32_t m[2][4]                 = { 0 };
    int32_t v[2][4]                 = { 0 };

    uint8_t kappa[2]                = { 1, 1 };

    int ret                         = 0;

    int sp;

    uint64_t c;

    uint8_t *sigma, *sigma_n, *E;
    uint32_t *mu, *mu_n;

    const uint8_t *vlc_buf = Dcup + Pcup;

    /*
     * Bound on the precision needed to process the codeblock. The number of
     * decoded bit planes is equal to at most cblk->zbp + 2 since S_blk = P if
     * there are no placeholder passes or HT Sets and P = cblk->zbp. See Rec.
     * ITU-T T.814, 7.6.
     */
    int maxbp = cblk->zbp + 2;

    /* convert to raster-scan */
    const uint16_t is_border_x = width % 2;
    const uint16_t is_border_y = height % 2;

    const uint16_t quad_width  = ff_jpeg2000_ceildivpow2(width, 1);
    const uint16_t quad_height = ff_jpeg2000_ceildivpow2(height, 1);

    size_t buf_size = 4 * quad_width * quad_height;

    /* do we have enough precision, assuming a 32-bit decoding path */
    if (maxbp >= 32)
        return AVERROR_INVALIDDATA;

    sigma_n = av_calloc(buf_size, sizeof(uint8_t));
    E       = av_calloc(buf_size, sizeof(uint8_t));
    mu_n    = av_calloc(buf_size, sizeof(uint32_t));

    if (!sigma_n || !E || !mu_n) {
        ret = AVERROR(ENOMEM);
        goto free;
    }

    sigma = sigma_n;
    mu = mu_n;

    while (q < quad_width - 1) {
        q1 = q;
        q2 = q1 + 1;

        if ((ret = jpeg2000_decode_sig_emb(s, mel_state, mel_stream, vlc_stream,
                                           dec_cxt_vlc_table0, Dcup, sig_pat, res_off,
                                           emb_pat_k, emb_pat_1, J2K_Q1, context, Lcup,
                                           Pcup)) < 0)
            goto free;

        for (int i = 0; i < 4; i++)
            sigma_n[4 * q1 + i] = (sig_pat[J2K_Q1] >> i) & 1;

        /* calculate context */
        context  = sigma_n[4 * q1];           // f
        context |= sigma_n[4 * q1 + 1];       // sf
        context += sigma_n[4 * q1 + 2] << 1;  // w << 1
        context += sigma_n[4 * q1 + 3] << 2;

        if ((ret = jpeg2000_decode_sig_emb(s, mel_state, mel_stream, vlc_stream,
                                           dec_cxt_vlc_table0, Dcup, sig_pat, res_off,
                                           emb_pat_k, emb_pat_1, J2K_Q2, context, Lcup,
                                           Pcup)) < 0)
            goto free;

        for (int i = 0; i < 4; i++)
            sigma_n[4 * q2 + i] = (sig_pat[J2K_Q2] >> i) & 1;

        /* calculate context for the next quad */
        context  = sigma_n[4 * q2];           // f
        context |= sigma_n[4 * q2 + 1];       // sf
        context += sigma_n[4 * q2 + 2] << 1;  // w << 1
        context += sigma_n[4 * q2 + 3] << 2;  // sw << 2

        u[0] = 0;
        u[1] = 0;

        jpeg2000_bitbuf_refill_backwards(vlc_stream, vlc_buf);

        if (res_off[J2K_Q1] == 1 && res_off[J2K_Q2] == 1) {

            if (jpeg2000_decode_mel_sym(mel_state, mel_stream, Dcup, Lcup) == 1) {

                u_pfx[J2K_Q1] = vlc_decode_u_prefix(vlc_stream, vlc_buf);
                u_pfx[J2K_Q2] = vlc_decode_u_prefix(vlc_stream, vlc_buf);

                u_sfx[J2K_Q1] = vlc_decode_u_suffix(vlc_stream, u_pfx[J2K_Q1], vlc_buf);
                u_sfx[J2K_Q2] = vlc_decode_u_suffix(vlc_stream, u_pfx[J2K_Q2], vlc_buf);

                u_ext[J2K_Q1] = vlc_decode_u_extension(vlc_stream, u_sfx[J2K_Q1], vlc_buf);
                u_ext[J2K_Q2] = vlc_decode_u_extension(vlc_stream, u_sfx[J2K_Q2], vlc_buf);

                u[J2K_Q1] = 2 + u_pfx[J2K_Q1] + u_sfx[J2K_Q1] + (u_ext[J2K_Q1] * 4);
                u[J2K_Q2] = 2 + u_pfx[J2K_Q2] + u_sfx[J2K_Q2] + (u_ext[J2K_Q2] * 4);

            } else {
                u_pfx[J2K_Q1] = vlc_decode_u_prefix(vlc_stream, vlc_buf);

                if (u_pfx[J2K_Q1] > 2) {
                    u[J2K_Q2] = jpeg2000_bitbuf_get_bits_lsb(vlc_stream, 1, vlc_buf) + 1;
                    u_sfx[J2K_Q1] = vlc_decode_u_suffix(vlc_stream, u_pfx[J2K_Q1], vlc_buf);
                    u_ext[J2K_Q1] = vlc_decode_u_extension(vlc_stream, u_sfx[J2K_Q1], vlc_buf);
                } else {
                    u_pfx[J2K_Q2] = vlc_decode_u_prefix(vlc_stream, vlc_buf);
                    u_sfx[J2K_Q1] = vlc_decode_u_suffix(vlc_stream, u_pfx[J2K_Q1], vlc_buf);
                    u_sfx[J2K_Q2] = vlc_decode_u_suffix(vlc_stream, u_pfx[J2K_Q2], vlc_buf);
                    u_ext[J2K_Q1] = vlc_decode_u_extension(vlc_stream, u_sfx[J2K_Q1], vlc_buf);
                    u_ext[J2K_Q2] = vlc_decode_u_extension(vlc_stream, u_sfx[J2K_Q2], vlc_buf);
                    u[J2K_Q2] = u_pfx[J2K_Q2] + u_sfx[J2K_Q2] + (u_ext[J2K_Q2] * 4);
                }
                /* See Rec. ITU-T T.814, 7.3.6(3) */
                u[J2K_Q1] = u_pfx[J2K_Q1] + u_sfx[J2K_Q1] + (u_ext[J2K_Q1] * 4);
            }

        } else if (res_off[J2K_Q1] == 1 || res_off[J2K_Q2] == 1) {
            uint8_t pos = res_off[J2K_Q1] == 1 ? 0 : 1;
            u_pfx[pos] = vlc_decode_u_prefix(vlc_stream, vlc_buf);
            u_sfx[pos] = vlc_decode_u_suffix(vlc_stream, u_pfx[pos], vlc_buf);
            u_ext[pos] = vlc_decode_u_extension(vlc_stream, u_sfx[pos], vlc_buf);
            u[pos] = u_pfx[pos] + u_sfx[pos] + (u_ext[pos] * 4);
        }
        U[J2K_Q1] = kappa[J2K_Q1] + u[J2K_Q1];
        U[J2K_Q2] = kappa[J2K_Q2] + u[J2K_Q2];
        if (U[J2K_Q1] > maxbp || U[J2K_Q2] > maxbp) {
            ret = AVERROR_INVALIDDATA;
            goto free;
        }

        for (int i = 0; i < 4; i++) {
            m[J2K_Q1][i] = sigma_n[4 * q1 + i] * U[J2K_Q1] - ((emb_pat_k[J2K_Q1] >> i) & 1);
            m[J2K_Q2][i] = sigma_n[4 * q2 + i] * U[J2K_Q2] - ((emb_pat_k[J2K_Q2] >> i) & 1);
        }

        recover_mag_sgn(mag_sgn_stream, J2K_Q1, q1, m_n, known_1, emb_pat_1, v, m,
                        E, mu_n, Dcup, Pcup, pLSB);

        recover_mag_sgn(mag_sgn_stream, J2K_Q2, q2, m_n, known_1, emb_pat_1, v, m,
                        E, mu_n, Dcup, Pcup, pLSB);

        q += 2; // Move to the next quad pair
    }

    if (quad_width % 2 == 1) {
        q1 = q;

        if ((ret = jpeg2000_decode_sig_emb(s, mel_state, mel_stream, vlc_stream,
                                           dec_cxt_vlc_table0, Dcup, sig_pat, res_off,
                                           emb_pat_k, emb_pat_1, J2K_Q1, context, Lcup,
                                           Pcup)) < 0)
            goto free;

        for (int i = 0; i < 4; i++)
            sigma_n[4 * q1 + i] = (sig_pat[J2K_Q1] >> i) & 1;

        u[J2K_Q1] = 0;

        if (res_off[J2K_Q1] == 1) {
            u_pfx[J2K_Q1] = vlc_decode_u_prefix(vlc_stream, vlc_buf);
            u_sfx[J2K_Q1] = vlc_decode_u_suffix(vlc_stream, u_pfx[J2K_Q1], vlc_buf);
            u_ext[J2K_Q1] = vlc_decode_u_extension(vlc_stream, u_sfx[J2K_Q1], vlc_buf);
            u[J2K_Q1] = u_pfx[J2K_Q1] + u_sfx[J2K_Q1] + (u_ext[J2K_Q1] * 4);
        }

        U[J2K_Q1] = kappa[J2K_Q1] + u[J2K_Q1];
        if (U[J2K_Q1] > maxbp) {
            ret = AVERROR_INVALIDDATA;
            goto free;
        }

        for (int i = 0; i < 4; i++)
            m[J2K_Q1][i] = sigma_n[4 * q1 + i] * U[J2K_Q1] - ((emb_pat_k[J2K_Q1] >> i) & 1);

        recover_mag_sgn(mag_sgn_stream, J2K_Q1, q1, m_n, known_1, emb_pat_1, v, m,
                        E, mu_n, Dcup, Pcup, pLSB);

        q++; // move to next quad pair
    }

    /**
     * Initial line pair end. As an optimization, we can replace modulo
     * operations with checking if a number is divisible , since that's the only
     * thing we need. This is paired with is_divisible. Credits to Daniel Lemire
     * blog post [1].
     *
     * [1]
     * https://lemire.me/blog/2019/02/08/faster-remainders-when-the-divisor-is-a-constant-beating-compilers-and-libdivide/
     *
     * It's UB on zero, but the spec doesn't allow a quad being zero, so we
     * error out early in case that's the case.
     */
    c = precompute_c(quad_width);

    for (int row = 1; row < quad_height; row++) {
        while ((q - (row * quad_width)) < quad_width - 1 && q < (quad_height * quad_width)) {
            q1 = q;
            q2 = q + 1;
            context1  = sigma_n[4 * (q1 - quad_width) + 1];
            context1 += sigma_n[4 * (q1 - quad_width) + 3] << 2;              // ne

            if (!is_divisible(q1, c)) {
                context1 |= sigma_n[4 * (q1 - quad_width) - 1];               // nw
                context1 += (sigma_n[4 * q1 - 1] | sigma_n[4 * q1 - 2]) << 1; // sw | q
            }
            if (!is_divisible(q1 + 1, c))
                context1 |= sigma_n[4 * (q1 - quad_width) + 5] << 2;

            if ((ret = jpeg2000_decode_sig_emb(s, mel_state, mel_stream, vlc_stream,
                                               dec_cxt_vlc_table1, Dcup, sig_pat, res_off,
                                               emb_pat_k, emb_pat_1, J2K_Q1, context1, Lcup,
                                               Pcup))
                < 0)
                goto free;

            for (int i = 0; i < 4; i++)
                sigma_n[4 * q1 + i] = (sig_pat[J2K_Q1] >> i) & 1;

            context2  = sigma_n[4 * (q2 - quad_width) + 1];
            context2 += sigma_n[4 * (q2 - quad_width) + 3] << 2;

            if (!is_divisible(q2, c)) {
                context2 |= sigma_n[4 * (q2 - quad_width) - 1];
                context2 += (sigma_n[4 * q2 - 1] | sigma_n[4 * q2 - 2]) << 1;
            }
            if (!is_divisible(q2 + 1, c))
                context2 |= sigma_n[4 * (q2 - quad_width) + 5] << 2;

            if ((ret = jpeg2000_decode_sig_emb(s, mel_state, mel_stream, vlc_stream,
                                               dec_cxt_vlc_table1, Dcup, sig_pat, res_off,
                                               emb_pat_k, emb_pat_1, J2K_Q2, context2, Lcup,
                                               Pcup))
                < 0)
                goto free;

            for (int i = 0; i < 4; i++)
                sigma_n[4 * q2 + i] = (sig_pat[J2K_Q2] >> i) & 1;

            u[J2K_Q1] = 0;
            u[J2K_Q2] = 0;

            jpeg2000_bitbuf_refill_backwards(vlc_stream, vlc_buf);

            if (res_off[J2K_Q1] == 1 && res_off[J2K_Q2] == 1) {
                u_pfx[J2K_Q1] = vlc_decode_u_prefix(vlc_stream, vlc_buf);
                u_pfx[J2K_Q2] = vlc_decode_u_prefix(vlc_stream, vlc_buf);

                u_sfx[J2K_Q1] = vlc_decode_u_suffix(vlc_stream, u_pfx[J2K_Q1], vlc_buf);
                u_sfx[J2K_Q2] = vlc_decode_u_suffix(vlc_stream, u_pfx[J2K_Q2], vlc_buf);

                u_ext[J2K_Q1] = vlc_decode_u_extension(vlc_stream, u_sfx[J2K_Q1], vlc_buf);
                u_ext[J2K_Q2] = vlc_decode_u_extension(vlc_stream, u_sfx[J2K_Q2], vlc_buf);

                u[J2K_Q1] = u_pfx[J2K_Q1] + u_sfx[J2K_Q1] + (u_ext[J2K_Q1] << 2);
                u[J2K_Q2] = u_pfx[J2K_Q2] + u_sfx[J2K_Q2] + (u_ext[J2K_Q2] << 2);

            } else if (res_off[J2K_Q1] == 1 || res_off[J2K_Q2] == 1) {
                uint8_t pos = res_off[J2K_Q1] == 1 ? 0 : 1;

                u_pfx[pos] = vlc_decode_u_prefix(vlc_stream, vlc_buf);
                u_sfx[pos] = vlc_decode_u_suffix(vlc_stream, u_pfx[pos], vlc_buf);
                u_ext[pos] = vlc_decode_u_extension(vlc_stream, u_sfx[pos], vlc_buf);

                u[pos] = u_pfx[pos] + u_sfx[pos] + (u_ext[pos] << 2);
            }
            sp = sig_pat[J2K_Q1];

            gamma[J2K_Q1] = 1;

            if (sp == 0 || sp == 1 || sp == 2 || sp == 4 || sp == 8)
                gamma[J2K_Q1] = 0;

            sp = sig_pat[J2K_Q2];

            gamma[J2K_Q2] = 1;

            if (sp == 0 || sp == 1 || sp == 2 || sp == 4 || sp == 8)
                gamma[J2K_Q2] = 0;

            E_n[J2K_Q1] = E[4 * (q1 - quad_width) + 1];
            E_n[J2K_Q2] = E[4 * (q2 - quad_width) + 1];

            E_ne[J2K_Q1] = E[4 * (q1 - quad_width) + 3];
            E_ne[J2K_Q2] = E[4 * (q2 - quad_width) + 3];

            E_nw[J2K_Q1] = (!is_divisible(q1, c)) * E[FFMAX((4 * (q1 - quad_width) - 1), 0)];
            E_nw[J2K_Q2] = (!is_divisible(q2, c)) * E[FFMAX((4 * (q2 - quad_width) - 1), 0)];

            E_nf[J2K_Q1] = (!is_divisible(q1 + 1, c)) * E[4 * (q1 - quad_width) + 5];
            E_nf[J2K_Q2] = (!is_divisible(q2 + 1, c)) * E[4 * (q2 - quad_width) + 5];

            max_e[J2K_Q1] = FFMAX(E_nw[J2K_Q1], FFMAX3(E_n[J2K_Q1], E_ne[J2K_Q1], E_nf[J2K_Q1]));
            max_e[J2K_Q2] = FFMAX(E_nw[J2K_Q2], FFMAX3(E_n[J2K_Q2], E_ne[J2K_Q2], E_nf[J2K_Q2]));

            kappa[J2K_Q1] = FFMAX(1, gamma[J2K_Q1] * (max_e[J2K_Q1] - 1));
            kappa[J2K_Q2] = FFMAX(1, gamma[J2K_Q2] * (max_e[J2K_Q2] - 1));

            U[J2K_Q1] = kappa[J2K_Q1] + u[J2K_Q1];
            U[J2K_Q2] = kappa[J2K_Q2] + u[J2K_Q2];
            if (U[J2K_Q1] > maxbp || U[J2K_Q2] > maxbp) {
                ret = AVERROR_INVALIDDATA;
                goto free;
            }

            for (int i = 0; i < 4; i++) {
                m[J2K_Q1][i] = sigma_n[4 * q1 + i] * U[J2K_Q1] - ((emb_pat_k[J2K_Q1] >> i) & 1);
                m[J2K_Q2][i] = sigma_n[4 * q2 + i] * U[J2K_Q2] - ((emb_pat_k[J2K_Q2] >> i) & 1);
            }
            recover_mag_sgn(mag_sgn_stream, J2K_Q1, q1, m_n, known_1, emb_pat_1, v, m,
                            E, mu_n, Dcup, Pcup, pLSB);

            recover_mag_sgn(mag_sgn_stream, J2K_Q2, q2, m_n, known_1, emb_pat_1, v, m,
                            E, mu_n, Dcup, Pcup, pLSB);

            q += 2; // Move to the next quad pair
        }

        if (quad_width % 2 == 1) {
            q1 = q;

            /* calculate context for current quad */
            context1  = sigma_n[4 * (q1 - quad_width) + 1];
            context1 += (sigma_n[4 * (q1 - quad_width) + 3] << 2);

            if (!is_divisible(q1, c)) {
                context1 |= sigma_n[4 * (q1 - quad_width) - 1];
                context1 += (sigma_n[4 * q1 - 1] | sigma_n[4 * q1 - 2]) << 1;
            }
            if (!is_divisible(q1 + 1, c))
                context1 |= sigma_n[4 * (q1 - quad_width) + 5] << 2;

            if ((ret = jpeg2000_decode_sig_emb(s, mel_state, mel_stream, vlc_stream,
                                               dec_cxt_vlc_table1, Dcup, sig_pat, res_off,
                                               emb_pat_k, emb_pat_1, J2K_Q1, context1, Lcup,
                                               Pcup)) < 0)
                goto free;

            for (int i = 0; i < 4; i++)
                sigma_n[4 * q1 + i] = (sig_pat[J2K_Q1] >> i) & 1;

            u[J2K_Q1] = 0;

            /* Recover mag_sgn value */
            if (res_off[J2K_Q1] == 1) {
                u_pfx[J2K_Q1] = vlc_decode_u_prefix(vlc_stream, vlc_buf);
                u_sfx[J2K_Q1] = vlc_decode_u_suffix(vlc_stream, u_pfx[J2K_Q1], vlc_buf);
                u_ext[J2K_Q1] = vlc_decode_u_extension(vlc_stream, u_sfx[J2K_Q1], vlc_buf);

                u[J2K_Q1] = u_pfx[J2K_Q1] + u_sfx[J2K_Q1] + (u_ext[J2K_Q1] << 2);
            }

            sp = sig_pat[J2K_Q1];

            gamma[J2K_Q1] = 1;

            if (sp == 0 || sp == 1 || sp == 2 || sp == 4 || sp == 8)
                gamma[J2K_Q1] = 0;

            E_n[J2K_Q1] = E[4 * (q1 - quad_width) + 1];

            E_ne[J2K_Q1] = E[4 * (q1 - quad_width) + 3];

            E_nw[J2K_Q1] = (!is_divisible(q1, c)) * E[FFMAX((4 * (q1 - quad_width) - 1), 0)];

            E_nf[J2K_Q1] = (!is_divisible(q1 + 1, c)) * E[4 * (q1 - quad_width) + 5];

            max_e[J2K_Q1] = FFMAX(E_nw[J2K_Q1], FFMAX3(E_n[J2K_Q1], E_ne[J2K_Q1], E_nf[J2K_Q1]));

            kappa[J2K_Q1] = FFMAX(1, gamma[J2K_Q1] * (max_e[J2K_Q1] - 1));

            U[J2K_Q1] = kappa[J2K_Q1] + u[J2K_Q1];
            if (U[J2K_Q1] > maxbp) {
                ret = AVERROR_INVALIDDATA;
                goto free;
            }

            for (int i = 0; i < 4; i++)
                m[J2K_Q1][i] = sigma_n[4 * q1 + i] * U[J2K_Q1] - ((emb_pat_k[J2K_Q1] >> i) & 1);

            recover_mag_sgn(mag_sgn_stream, J2K_Q1, q1, m_n, known_1, emb_pat_1, v, m,
                            E, mu_n, Dcup, Pcup, pLSB);
            q += 1;
        }
    }

    // convert to raster-scan
    for (int y = 0; y < quad_height; y++) {
        for (int x = 0; x < quad_width; x++) {
            int j1, j2;
            int x1, x2 , x3;

            j1 = 2 * y;
            j2 = 2 * x;

            sample_buf[j2 + (j1 * stride)] = (int32_t)*mu;
            jpeg2000_modify_state(j1, j2, stride, *sigma, block_states);
            sigma += 1;
            mu += 1;

            x1 = y != quad_height - 1 || is_border_y == 0;
            sample_buf[j2 + ((j1 + 1) * stride)] = ((int32_t)*mu) * x1;
            jpeg2000_modify_state(j1 + 1, j2, stride, (*sigma) * x1, block_states);
            sigma += 1;
            mu += 1;

            x2 = x != quad_width - 1 || is_border_x == 0;
            sample_buf[(j2 + 1) + (j1 * stride)] = ((int32_t)*mu) * x2;
            jpeg2000_modify_state(j1, j2 + 1, stride, (*sigma) * x2, block_states);
            sigma += 1;
            mu += 1;

            x3 = x1 | x2;
            sample_buf[(j2 + 1) + (j1 + 1) * stride] = ((int32_t)*mu) * x3;
            jpeg2000_modify_state(j1 + 1, j2 + 1, stride, (*sigma) * x3, block_states);
            sigma += 1;
            mu += 1;
        }
    }
    ret = 1;
free:
    av_freep(&sigma_n);
    av_freep(&E);
    av_freep(&mu_n);
    return ret;
}

static void jpeg2000_calc_mbr(uint8_t *mbr, const uint16_t i, const uint16_t j,
                              const uint32_t mbr_info, uint8_t causal_cond,
                              uint8_t *block_states, int stride)
{
    uint8_t *state_p0 = block_states + i * stride + j;
    uint8_t *state_p1 = block_states + (i + 1) * stride + j;
    uint8_t *state_p2 = block_states + (i + 2) * stride + j;

    uint8_t mbr0 = state_p0[0] | state_p0[1] | state_p0[2];
    uint8_t mbr1 = state_p1[0] | state_p1[2];
    uint8_t mbr2 = state_p2[0] | state_p2[1] | state_p2[2];
    *mbr  = mbr0 | mbr1 | (mbr2 & causal_cond);
    *mbr |= (mbr0 >> HT_SHIFT_REF) & (mbr0 >> HT_SHIFT_SCAN);
    *mbr |= (mbr1 >> HT_SHIFT_REF) & (mbr1 >> HT_SHIFT_SCAN);
    *mbr |= (mbr2 >> HT_SHIFT_REF) & (mbr2 >> HT_SHIFT_SCAN) & causal_cond;
    *mbr &= 1;
}

static void jpeg2000_process_stripes_block(StateVars *sig_prop, int i_s, int j_s,
                                           int width, int height, int stride, int pLSB,
                                           int32_t *sample_buf, uint8_t *block_states,
                                           uint8_t *magref_segment, uint32_t magref_length,
                                           uint8_t is_causal)
{
    for (int j = j_s; j < j_s + width; j++) {
        uint32_t  mbr_info = 0;
        for (int i = i_s; i < i_s + height; i++) {
            int modify_state;
            uint8_t bit;
            uint8_t causal_cond = (is_causal == 0) || (i != (i_s + height - 1));
            int32_t *sp = &sample_buf[j + (i * (stride))];
            uint8_t mbr = 0;

            if (jpeg2000_get_state(i, j, stride, HT_SHIFT_SIGMA, block_states) == 0)
                jpeg2000_calc_mbr(&mbr, i, j, mbr_info & 0x1EF, causal_cond, block_states, stride);
            mbr_info >>= 3;

            modify_state = block_states[(i + 1) * stride + (j + 1)];
            modify_state |= 1 << HT_SHIFT_SCAN;
            if (mbr != 0) {
                modify_state |= 1 << HT_SHIFT_REF_IND;
                bit = jpeg2000_peek_bit(sig_prop, magref_segment, magref_length);
                modify_state |= bit << HT_SHIFT_REF;
                *sp |= bit << pLSB;
                *sp |= bit << (pLSB - 1); // Add 0.5 (reconstruction parameter = 1/2)
            }
            jpeg2000_modify_state(i, j, stride, modify_state, block_states);
        }
    }
    // decode sign
    for (int j = j_s; j < j_s + width; j++) {
        for (int i = i_s; i < i_s + height; i++) {
            uint8_t bit;
            int32_t *sp = &sample_buf[j + (i * (stride))];
            uint8_t *state_p = block_states + (i + 1) * stride + (j + 1);
            if ((state_p[0] >> HT_SHIFT_REF) & 1) {
                bit = jpeg2000_peek_bit(sig_prop, magref_segment, magref_length);
                *sp |= (int32_t)bit << 31;
            }
        }
    }
}

/**
 * See procedure decodeSigPropMag at Rec. ITU-T T.814, 7.4.
*/
av_noinline
static void jpeg2000_decode_sigprop_segment(Jpeg2000Cblk *cblk, uint16_t width, uint16_t height,
                                            const int stride, uint8_t *magref_segment,
                                            uint32_t magref_length, uint8_t pLSB,
                                            int32_t *sample_buf, uint8_t *block_states)
{
    StateVars sp_dec;

    const uint16_t num_v_stripe = height / 4;
    const uint16_t num_h_stripe = width / 4;
    int b_width                 = 4;
    int b_height                = 4;

    int last_width;
    uint16_t i = 0, j = 0;
    uint8_t is_causal = cblk->modes & JPEG2000_CBLK_VSC;

    jpeg2000_init_zero(&sp_dec);

    for (int n1 = 0; n1 < num_v_stripe; n1++) {
        j = 0;
        for (int n2 = 0; n2 < num_h_stripe; n2++) {
            jpeg2000_process_stripes_block(&sp_dec, i, j, b_width, b_height, stride,
                                           pLSB, sample_buf, block_states, magref_segment,
                                           magref_length, is_causal);
            j += 4;
        }
        last_width = width % 4;
        if (last_width)
            jpeg2000_process_stripes_block(&sp_dec, i, j, last_width, b_height, stride,
                                           pLSB, sample_buf, block_states, magref_segment,
                                           magref_length, is_causal);
        i += 4;
    }

    /* Decode remaining height stripes */
    b_height = height % 4;
    j = 0;
    for (int n2 = 0; n2 < num_h_stripe; n2++) {
        jpeg2000_process_stripes_block(&sp_dec, i, j, b_width, b_height, stride,
                                       pLSB, sample_buf, block_states, magref_segment,
                                       magref_length, is_causal);
        j += 4;
    }
    last_width = width % 4;
    if (last_width)
        jpeg2000_process_stripes_block(&sp_dec, i, j, last_width, b_height, stride,
                                       pLSB, sample_buf, block_states, magref_segment,
                                       magref_length, is_causal);
}

/**
 * See procedure decodeSigPropMag at Rec. ITU-T T.814, 7.5.
*/
static void
jpeg2000_decode_magref_segment( uint16_t width, uint16_t block_height, const int stride,
                                uint8_t *magref_segment,uint32_t magref_length,
                                uint8_t pLSB, int32_t *sample_buf, uint8_t *block_states)
{

    StateVars mag_ref           = { 0 };
    const uint16_t num_v_stripe = block_height / 4;
    uint16_t height             = 4;
    uint16_t i_start            = 0;
    int32_t *sp;
    int32_t bit;
    int32_t tmp;
    jpeg2000_init_mag_ref(&mag_ref, magref_length);

    for (int n1 = 0; n1 < num_v_stripe; n1++) {
        for (int j = 0; j < width; j++) {
            for (int i = i_start; i < i_start + height; i++) {
                /**
                 *  We move column wise, going from one quad to another. See
                 *  Rec. ITU-T T.814, Figure 7.
                 */
                sp = &sample_buf[j + i * stride];
                if (jpeg2000_get_state(i, j, stride, HT_SHIFT_SIGMA, block_states) != 0) {
                    jpeg2000_modify_state(i, j, stride, 1 << HT_SHIFT_REF_IND, block_states);
                    bit = jpeg2000_import_magref_bit(&mag_ref, magref_segment, magref_length);
                    tmp = 0xFFFFFFFE | (uint32_t)bit;
                    tmp <<= pLSB;
                    sp[0] &= tmp;
                    sp[0] |= 1 << (pLSB - 1); // Add 0.5 (reconstruction parameter = 1/2)
                }
            }
        }
        i_start += 4;
    }
    height = block_height % 4;
    for (int j = 0; j < width; j++) {
        for (int i = i_start; i < i_start + height; i++) {
            sp = &sample_buf[j + i * stride];
            if (jpeg2000_get_state(i, j, stride, HT_SHIFT_SIGMA, block_states) != 0) {
                jpeg2000_modify_state(i, j, stride, 1 << HT_SHIFT_REF_IND, block_states);
                bit = jpeg2000_import_magref_bit(&mag_ref, magref_segment, magref_length);
                tmp = 0xFFFFFFFE | (uint32_t)bit;
                tmp <<= pLSB;
                sp[0] &= tmp;
                sp[0] |= 1 << (pLSB - 1); // Add 0.5 (reconstruction parameter = 1/2)
            }
        }
    }
}


int
ff_jpeg2000_decode_htj2k(const Jpeg2000DecoderContext *s, Jpeg2000CodingStyle *codsty, Jpeg2000T1Context *t1, Jpeg2000Cblk *cblk,
                         int width, int height, int M_b, uint8_t roi_shift)
{
    uint8_t p0 = 0;             // 3 * p0 = Number of placeholder passes
    uint32_t Lcup;              // Length of HT cleanup segment
    uint32_t Lref;              // Length of Refinement segment
    uint32_t Scup;              // HT cleanup segment suffix length
    uint32_t Pcup;              // HT cleanup segment prefix length

    uint8_t S_blk;              // Number of skipped magnitude bitplanes
    uint8_t pLSB;

    uint8_t *Dcup;              // Byte of an HT cleanup segment
    uint8_t *Dref;              // Byte of an HT refinement segment

    int z_blk;                  // Number of ht coding pass

    uint8_t num_plhd_passes;    // Number of placeholder passes

    StateVars mag_sgn;          // Magnitude and Sign
    StateVars mel;              // Adaptive run-length coding
    StateVars vlc;              // Variable Length coding
    StateVars sig_prop;         // Significance propagation

    MelDecoderState mel_state;

    int ret;

    /* Temporary buffers */
    int32_t *sample_buf = NULL;
    uint8_t *block_states = NULL;

    int32_t n, val;             // Post-processing
    const uint32_t mask  = UINT32_MAX >> (M_b + 1); // bit mask for ROI detection

    uint8_t num_rempass;

    const int quad_buf_width = width + 4;
    const int quad_buf_height = height + 4;

    /* codeblock size as constrained by Rec. ITU-T T.800, Table A.18 */
    av_assert0(width <= 1024U && height <= 1024U);
    av_assert0(width * height <= 4096);
    av_assert0(width * height > 0);

    memset(t1->data, 0, t1->stride * height * sizeof(*t1->data));
    memset(t1->flags, 0, t1->stride * (height + 2) * sizeof(*t1->flags));

    if (cblk->npasses == 0)
        return 0;

    num_rempass = cblk->npasses % 3;  // Number of remainder passes
    num_plhd_passes = num_rempass ? cblk->npasses - num_rempass : cblk->npasses - 3;
    av_assert0(num_plhd_passes % 3 == 0);
    p0 = num_plhd_passes / 3;
    z_blk = cblk->npasses - num_plhd_passes;

    if (z_blk <= 0)
        return 0; // No passes within this set, continue

    Lcup = cblk->pass_lengths[0];
    Lref = cblk->pass_lengths[1];

    if (Lcup < 2) {
        av_log(s->avctx, AV_LOG_ERROR,
               "Cleanup pass length must be at least 2 bytes in length\n");
        return AVERROR_INVALIDDATA;
    }
    Dcup = cblk->data;
    Dref  = cblk->data + Lcup; // Dref comes after the refinement segment

    cblk->data[cblk->length] = 0xFF; // an extra byte for refinement segment (buffer->last)

    S_blk = p0 + cblk->zbp;
    cblk->zbp = S_blk - 1;
    pLSB  = 30 - S_blk;

    Scup = (Dcup[Lcup - 1] << 4) + (Dcup[Lcup - 2] & 0x0F);

    if (Scup < 2 || Scup > Lcup || Scup > 4079) {
        av_log(s->avctx, AV_LOG_ERROR, "Cleanup pass suffix length is invalid %d\n",
               Scup);
        ret = AVERROR_INVALIDDATA;
        goto free;
    }
    Pcup = Lcup - Scup;

    /* modDcup shall be done before the creation of vlc instance. */
    Dcup[Lcup - 1] = 0xFF;
    Dcup[Lcup - 2] |= 0x0F;

    /* Magnitude and refinement */
    jpeg2000_init_zero(&mag_sgn);
    jpeg2000_bitbuf_refill_forward(&mag_sgn, Dcup, Pcup);

    /* Significance propagation */
    jpeg2000_init_zero(&sig_prop);

    /* Adaptive run length */
    jpeg2000_init_mel(&mel, Pcup);

    /* Variable Length coding */
    jpeg2000_init_vlc(&vlc, Lcup, Pcup, Dcup);

    jpeg2000_init_mel_decoder(&mel_state);

    sample_buf = av_calloc(quad_buf_width * quad_buf_height, sizeof(int32_t));
    block_states = av_calloc(quad_buf_width * quad_buf_height, sizeof(uint8_t));

    if (!sample_buf || !block_states) {
        ret = AVERROR(ENOMEM);
        goto free;
    }
    if ((ret = jpeg2000_decode_ht_cleanup_segment(s, cblk, t1, &mel_state, &mel, &vlc,
                                                  &mag_sgn, Dcup, Lcup, Pcup, pLSB, width,
                                                  height, quad_buf_width, sample_buf, block_states)) < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Bad HT cleanup segment\n");
        goto free;
    }

    if (z_blk > 1)
        jpeg2000_decode_sigprop_segment(cblk, width, height, quad_buf_width, Dref, Lref,
                                        pLSB - 1, sample_buf, block_states);

    if (z_blk > 2)
        jpeg2000_decode_magref_segment(width, height, quad_buf_width, Dref, Lref,
                                       pLSB - 1, sample_buf, block_states);

    /* Reconstruct the sample values */
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int32_t sign;

            n = x + (y * t1->stride);
            val = sample_buf[x + (y * quad_buf_width)];
            sign = val & INT32_MIN;
            val &= INT32_MAX;
            /* ROI shift, if necessary */
            if (roi_shift && (((uint32_t)val & ~mask) == 0))
                val <<= roi_shift;
            t1->data[n] = val | sign; /* NOTE: Binary point for reconstruction value is located in 31 - M_b */
        }
    }
free:
    av_freep(&sample_buf);
    av_freep(&block_states);
    return ret;
}

/**
 * CtxVLC tables (see Rec. ITU-T T.800, Annex C) as found at
 * https://github.com/osamu620/OpenHTJ2K (author: Osamu Watanabe)
 */
static const uint16_t dec_cxt_vlc_table1[1024] = {
        0x0016, 0x006A, 0x0046, 0x00DD, 0x0086, 0x888B, 0x0026, 0x444D, 0x0016, 0x00AA, 0x0046, 0x88AD, 0x0086,
        0x003A, 0x0026, 0x00DE, 0x0016, 0x00CA, 0x0046, 0x009D, 0x0086, 0x005A, 0x0026, 0x222D, 0x0016, 0x009A,
        0x0046, 0x007D, 0x0086, 0x01FD, 0x0026, 0x007E, 0x0016, 0x006A, 0x0046, 0x88CD, 0x0086, 0x888B, 0x0026,
        0x111D, 0x0016, 0x00AA, 0x0046, 0x005D, 0x0086, 0x003A, 0x0026, 0x00EE, 0x0016, 0x00CA, 0x0046, 0x00BD,
        0x0086, 0x005A, 0x0026, 0x11FF, 0x0016, 0x009A, 0x0046, 0x003D, 0x0086, 0x04ED, 0x0026, 0x2AAF, 0x0016,
        0x006A, 0x0046, 0x00DD, 0x0086, 0x888B, 0x0026, 0x444D, 0x0016, 0x00AA, 0x0046, 0x88AD, 0x0086, 0x003A,
        0x0026, 0x44EF, 0x0016, 0x00CA, 0x0046, 0x009D, 0x0086, 0x005A, 0x0026, 0x222D, 0x0016, 0x009A, 0x0046,
        0x007D, 0x0086, 0x01FD, 0x0026, 0x00BE, 0x0016, 0x006A, 0x0046, 0x88CD, 0x0086, 0x888B, 0x0026, 0x111D,
        0x0016, 0x00AA, 0x0046, 0x005D, 0x0086, 0x003A, 0x0026, 0x4CCF, 0x0016, 0x00CA, 0x0046, 0x00BD, 0x0086,
        0x005A, 0x0026, 0x00FE, 0x0016, 0x009A, 0x0046, 0x003D, 0x0086, 0x04ED, 0x0026, 0x006F, 0x0002, 0x0088,
        0x0002, 0x005C, 0x0002, 0x0018, 0x0002, 0x00DE, 0x0002, 0x0028, 0x0002, 0x009C, 0x0002, 0x004A, 0x0002,
        0x007E, 0x0002, 0x0088, 0x0002, 0x00CC, 0x0002, 0x0018, 0x0002, 0x888F, 0x0002, 0x0028, 0x0002, 0x00FE,
        0x0002, 0x003A, 0x0002, 0x222F, 0x0002, 0x0088, 0x0002, 0x04FD, 0x0002, 0x0018, 0x0002, 0x00BE, 0x0002,
        0x0028, 0x0002, 0x00BF, 0x0002, 0x004A, 0x0002, 0x006E, 0x0002, 0x0088, 0x0002, 0x00AC, 0x0002, 0x0018,
        0x0002, 0x444F, 0x0002, 0x0028, 0x0002, 0x00EE, 0x0002, 0x003A, 0x0002, 0x113F, 0x0002, 0x0088, 0x0002,
        0x005C, 0x0002, 0x0018, 0x0002, 0x00CF, 0x0002, 0x0028, 0x0002, 0x009C, 0x0002, 0x004A, 0x0002, 0x006F,
        0x0002, 0x0088, 0x0002, 0x00CC, 0x0002, 0x0018, 0x0002, 0x009F, 0x0002, 0x0028, 0x0002, 0x00EF, 0x0002,
        0x003A, 0x0002, 0x233F, 0x0002, 0x0088, 0x0002, 0x04FD, 0x0002, 0x0018, 0x0002, 0x00AF, 0x0002, 0x0028,
        0x0002, 0x44FF, 0x0002, 0x004A, 0x0002, 0x005F, 0x0002, 0x0088, 0x0002, 0x00AC, 0x0002, 0x0018, 0x0002,
        0x007F, 0x0002, 0x0028, 0x0002, 0x00DF, 0x0002, 0x003A, 0x0002, 0x111F, 0x0002, 0x0028, 0x0002, 0x005C,
        0x0002, 0x008A, 0x0002, 0x00BF, 0x0002, 0x0018, 0x0002, 0x00FE, 0x0002, 0x00CC, 0x0002, 0x007E, 0x0002,
        0x0028, 0x0002, 0x8FFF, 0x0002, 0x004A, 0x0002, 0x007F, 0x0002, 0x0018, 0x0002, 0x00DF, 0x0002, 0x00AC,
        0x0002, 0x133F, 0x0002, 0x0028, 0x0002, 0x222D, 0x0002, 0x008A, 0x0002, 0x00BE, 0x0002, 0x0018, 0x0002,
        0x44EF, 0x0002, 0x2AAD, 0x0002, 0x006E, 0x0002, 0x0028, 0x0002, 0x15FF, 0x0002, 0x004A, 0x0002, 0x009E,
        0x0002, 0x0018, 0x0002, 0x00CF, 0x0002, 0x003C, 0x0002, 0x223F, 0x0002, 0x0028, 0x0002, 0x005C, 0x0002,
        0x008A, 0x0002, 0x2BBF, 0x0002, 0x0018, 0x0002, 0x04EF, 0x0002, 0x00CC, 0x0002, 0x006F, 0x0002, 0x0028,
        0x0002, 0x27FF, 0x0002, 0x004A, 0x0002, 0x009F, 0x0002, 0x0018, 0x0002, 0x00DE, 0x0002, 0x00AC, 0x0002,
        0x444F, 0x0002, 0x0028, 0x0002, 0x222D, 0x0002, 0x008A, 0x0002, 0x8AAF, 0x0002, 0x0018, 0x0002, 0x00EE,
        0x0002, 0x2AAD, 0x0002, 0x005F, 0x0002, 0x0028, 0x0002, 0x44FF, 0x0002, 0x004A, 0x0002, 0x888F, 0x0002,
        0x0018, 0x0002, 0xAAAF, 0x0002, 0x003C, 0x0002, 0x111F, 0x0004, 0x8FFD, 0x0028, 0x005C, 0x0004, 0x00BC,
        0x008A, 0x66FF, 0x0004, 0x00CD, 0x0018, 0x111D, 0x0004, 0x009C, 0x003A, 0x8AAF, 0x0004, 0x00FC, 0x0028,
        0x133D, 0x0004, 0x00AC, 0x004A, 0x3BBF, 0x0004, 0x2BBD, 0x0018, 0x5FFF, 0x0004, 0x006C, 0x157D, 0x455F,
        0x0004, 0x2FFD, 0x0028, 0x222D, 0x0004, 0x22AD, 0x008A, 0x44EF, 0x0004, 0x00CC, 0x0018, 0x4FFF, 0x0004,
        0x007C, 0x003A, 0x447F, 0x0004, 0x04DD, 0x0028, 0x233D, 0x0004, 0x009D, 0x004A, 0x00DE, 0x0004, 0x88BD,
        0x0018, 0xAFFF, 0x0004, 0x115D, 0x1FFD, 0x444F, 0x0004, 0x8FFD, 0x0028, 0x005C, 0x0004, 0x00BC, 0x008A,
        0x8CEF, 0x0004, 0x00CD, 0x0018, 0x111D, 0x0004, 0x009C, 0x003A, 0x888F, 0x0004, 0x00FC, 0x0028, 0x133D,
        0x0004, 0x00AC, 0x004A, 0x44DF, 0x0004, 0x2BBD, 0x0018, 0x8AFF, 0x0004, 0x006C, 0x157D, 0x006F, 0x0004,
        0x2FFD, 0x0028, 0x222D, 0x0004, 0x22AD, 0x008A, 0x00EE, 0x0004, 0x00CC, 0x0018, 0x2EEF, 0x0004, 0x007C,
        0x003A, 0x277F, 0x0004, 0x04DD, 0x0028, 0x233D, 0x0004, 0x009D, 0x004A, 0x1BBF, 0x0004, 0x88BD, 0x0018,
        0x37FF, 0x0004, 0x115D, 0x1FFD, 0x333F, 0x0002, 0x0088, 0x0002, 0x02ED, 0x0002, 0x00CA, 0x0002, 0x4CCF,
        0x0002, 0x0048, 0x0002, 0x23FF, 0x0002, 0x001A, 0x0002, 0x888F, 0x0002, 0x0088, 0x0002, 0x006C, 0x0002,
        0x002A, 0x0002, 0x00AF, 0x0002, 0x0048, 0x0002, 0x22EF, 0x0002, 0x00AC, 0x0002, 0x005F, 0x0002, 0x0088,
        0x0002, 0x444D, 0x0002, 0x00CA, 0x0002, 0xCCCF, 0x0002, 0x0048, 0x0002, 0x00FE, 0x0002, 0x001A, 0x0002,
        0x006F, 0x0002, 0x0088, 0x0002, 0x005C, 0x0002, 0x002A, 0x0002, 0x009F, 0x0002, 0x0048, 0x0002, 0x00DF,
        0x0002, 0x03FD, 0x0002, 0x222F, 0x0002, 0x0088, 0x0002, 0x02ED, 0x0002, 0x00CA, 0x0002, 0x8CCF, 0x0002,
        0x0048, 0x0002, 0x11FF, 0x0002, 0x001A, 0x0002, 0x007E, 0x0002, 0x0088, 0x0002, 0x006C, 0x0002, 0x002A,
        0x0002, 0x007F, 0x0002, 0x0048, 0x0002, 0x00EE, 0x0002, 0x00AC, 0x0002, 0x003E, 0x0002, 0x0088, 0x0002,
        0x444D, 0x0002, 0x00CA, 0x0002, 0x00BE, 0x0002, 0x0048, 0x0002, 0x00BF, 0x0002, 0x001A, 0x0002, 0x003F,
        0x0002, 0x0088, 0x0002, 0x005C, 0x0002, 0x002A, 0x0002, 0x009E, 0x0002, 0x0048, 0x0002, 0x00DE, 0x0002,
        0x03FD, 0x0002, 0x111F, 0x0004, 0x8AED, 0x0048, 0x888D, 0x0004, 0x00DC, 0x00CA, 0x3FFF, 0x0004, 0xCFFD,
        0x002A, 0x003D, 0x0004, 0x00BC, 0x005A, 0x8DDF, 0x0004, 0x8FFD, 0x0048, 0x006C, 0x0004, 0x027D, 0x008A,
        0x99FF, 0x0004, 0x00EC, 0x00FA, 0x003C, 0x0004, 0x00AC, 0x001A, 0x009F, 0x0004, 0x2FFD, 0x0048, 0x007C,
        0x0004, 0x44CD, 0x00CA, 0x67FF, 0x0004, 0x1FFD, 0x002A, 0x444D, 0x0004, 0x00AD, 0x005A, 0x8CCF, 0x0004,
        0x4FFD, 0x0048, 0x445D, 0x0004, 0x01BD, 0x008A, 0x4EEF, 0x0004, 0x45DD, 0x00FA, 0x111D, 0x0004, 0x009C,
        0x001A, 0x222F, 0x0004, 0x8AED, 0x0048, 0x888D, 0x0004, 0x00DC, 0x00CA, 0xAFFF, 0x0004, 0xCFFD, 0x002A,
        0x003D, 0x0004, 0x00BC, 0x005A, 0x11BF, 0x0004, 0x8FFD, 0x0048, 0x006C, 0x0004, 0x027D, 0x008A, 0x22EF,
        0x0004, 0x00EC, 0x00FA, 0x003C, 0x0004, 0x00AC, 0x001A, 0x227F, 0x0004, 0x2FFD, 0x0048, 0x007C, 0x0004,
        0x44CD, 0x00CA, 0x5DFF, 0x0004, 0x1FFD, 0x002A, 0x444D, 0x0004, 0x00AD, 0x005A, 0x006F, 0x0004, 0x4FFD,
        0x0048, 0x445D, 0x0004, 0x01BD, 0x008A, 0x11DF, 0x0004, 0x45DD, 0x00FA, 0x111D, 0x0004, 0x009C, 0x001A,
        0x155F, 0x0006, 0x00FC, 0x0018, 0x111D, 0x0048, 0x888D, 0x00AA, 0x4DDF, 0x0006, 0x2AAD, 0x005A, 0x67FF,
        0x0028, 0x223D, 0x00BC, 0xAAAF, 0x0006, 0x00EC, 0x0018, 0x5FFF, 0x0048, 0x006C, 0x008A, 0xCCCF, 0x0006,
        0x009D, 0x00CA, 0x44EF, 0x0028, 0x003C, 0x8FFD, 0x137F, 0x0006, 0x8EED, 0x0018, 0x1FFF, 0x0048, 0x007C,
        0x00AA, 0x4CCF, 0x0006, 0x227D, 0x005A, 0x1DDF, 0x0028, 0x444D, 0x4FFD, 0x155F, 0x0006, 0x00DC, 0x0018,
        0x2EEF, 0x0048, 0x445D, 0x008A, 0x22BF, 0x0006, 0x009C, 0x00CA, 0x8CDF, 0x0028, 0x222D, 0x2FFD, 0x226F,
        0x0006, 0x00FC, 0x0018, 0x111D, 0x0048, 0x888D, 0x00AA, 0x1BBF, 0x0006, 0x2AAD, 0x005A, 0x33FF, 0x0028,
        0x223D, 0x00BC, 0x8AAF, 0x0006, 0x00EC, 0x0018, 0x9BFF, 0x0048, 0x006C, 0x008A, 0x8ABF, 0x0006, 0x009D,
        0x00CA, 0x4EEF, 0x0028, 0x003C, 0x8FFD, 0x466F, 0x0006, 0x8EED, 0x0018, 0xCFFF, 0x0048, 0x007C, 0x00AA,
        0x8CCF, 0x0006, 0x227D, 0x005A, 0xAEEF, 0x0028, 0x444D, 0x4FFD, 0x477F, 0x0006, 0x00DC, 0x0018, 0xAFFF,
        0x0048, 0x445D, 0x008A, 0x2BBF, 0x0006, 0x009C, 0x00CA, 0x44DF, 0x0028, 0x222D, 0x2FFD, 0x133F, 0x00F6,
        0xAFFD, 0x1FFB, 0x003C, 0x0008, 0x23BD, 0x007A, 0x11DF, 0x00F6, 0x45DD, 0x2FFB, 0x4EEF, 0x00DA, 0x177D,
        0xCFFD, 0x377F, 0x00F6, 0x3FFD, 0x8FFB, 0x111D, 0x0008, 0x009C, 0x005A, 0x1BBF, 0x00F6, 0x00CD, 0x00BA,
        0x8DDF, 0x4FFB, 0x006C, 0x9BFD, 0x455F, 0x00F6, 0x67FD, 0x1FFB, 0x002C, 0x0008, 0x00AC, 0x007A, 0x009F,
        0x00F6, 0x00AD, 0x2FFB, 0x7FFF, 0x00DA, 0x004C, 0x5FFD, 0x477F, 0x00F6, 0x00EC, 0x8FFB, 0x001C, 0x0008,
        0x008C, 0x005A, 0x888F, 0x00F6, 0x00CC, 0x00BA, 0x2EEF, 0x4FFB, 0x115D, 0x8AED, 0x113F, 0x00F6, 0xAFFD,
        0x1FFB, 0x003C, 0x0008, 0x23BD, 0x007A, 0x1DDF, 0x00F6, 0x45DD, 0x2FFB, 0xBFFF, 0x00DA, 0x177D, 0xCFFD,
        0x447F, 0x00F6, 0x3FFD, 0x8FFB, 0x111D, 0x0008, 0x009C, 0x005A, 0x277F, 0x00F6, 0x00CD, 0x00BA, 0x22EF,
        0x4FFB, 0x006C, 0x9BFD, 0x444F, 0x00F6, 0x67FD, 0x1FFB, 0x002C, 0x0008, 0x00AC, 0x007A, 0x11BF, 0x00F6,
        0x00AD, 0x2FFB, 0xFFFF, 0x00DA, 0x004C, 0x5FFD, 0x233F, 0x00F6, 0x00EC, 0x8FFB, 0x001C, 0x0008, 0x008C,
        0x005A, 0x006F, 0x00F6, 0x00CC, 0x00BA, 0x8BBF, 0x4FFB, 0x115D, 0x8AED, 0x222F};

static const uint16_t dec_cxt_vlc_table0[1024] = {
        0x0026, 0x00AA, 0x0046, 0x006C, 0x0086, 0x8AED, 0x0018, 0x8DDF, 0x0026, 0x01BD, 0x0046, 0x5FFF, 0x0086,
        0x027D, 0x005A, 0x155F, 0x0026, 0x003A, 0x0046, 0x444D, 0x0086, 0x4CCD, 0x0018, 0xCCCF, 0x0026, 0x2EFD,
        0x0046, 0x99FF, 0x0086, 0x009C, 0x00CA, 0x133F, 0x0026, 0x00AA, 0x0046, 0x445D, 0x0086, 0x8CCD, 0x0018,
        0x11DF, 0x0026, 0x4FFD, 0x0046, 0xCFFF, 0x0086, 0x009D, 0x005A, 0x007E, 0x0026, 0x003A, 0x0046, 0x1FFF,
        0x0086, 0x88AD, 0x0018, 0x00BE, 0x0026, 0x8FFD, 0x0046, 0x4EEF, 0x0086, 0x888D, 0x00CA, 0x111F, 0x0026,
        0x00AA, 0x0046, 0x006C, 0x0086, 0x8AED, 0x0018, 0x45DF, 0x0026, 0x01BD, 0x0046, 0x22EF, 0x0086, 0x027D,
        0x005A, 0x227F, 0x0026, 0x003A, 0x0046, 0x444D, 0x0086, 0x4CCD, 0x0018, 0x11BF, 0x0026, 0x2EFD, 0x0046,
        0x00FE, 0x0086, 0x009C, 0x00CA, 0x223F, 0x0026, 0x00AA, 0x0046, 0x445D, 0x0086, 0x8CCD, 0x0018, 0x00DE,
        0x0026, 0x4FFD, 0x0046, 0xABFF, 0x0086, 0x009D, 0x005A, 0x006F, 0x0026, 0x003A, 0x0046, 0x6EFF, 0x0086,
        0x88AD, 0x0018, 0x2AAF, 0x0026, 0x8FFD, 0x0046, 0x00EE, 0x0086, 0x888D, 0x00CA, 0x222F, 0x0004, 0x00CA,
        0x0088, 0x027D, 0x0004, 0x4CCD, 0x0028, 0x00FE, 0x0004, 0x2AFD, 0x0048, 0x005C, 0x0004, 0x009D, 0x0018,
        0x00DE, 0x0004, 0x01BD, 0x0088, 0x006C, 0x0004, 0x88AD, 0x0028, 0x11DF, 0x0004, 0x8AED, 0x0048, 0x003C,
        0x0004, 0x888D, 0x0018, 0x111F, 0x0004, 0x00CA, 0x0088, 0x006D, 0x0004, 0x88CD, 0x0028, 0x88FF, 0x0004,
        0x8BFD, 0x0048, 0x444D, 0x0004, 0x009C, 0x0018, 0x00BE, 0x0004, 0x4EFD, 0x0088, 0x445D, 0x0004, 0x00AC,
        0x0028, 0x00EE, 0x0004, 0x45DD, 0x0048, 0x222D, 0x0004, 0x003D, 0x0018, 0x007E, 0x0004, 0x00CA, 0x0088,
        0x027D, 0x0004, 0x4CCD, 0x0028, 0x1FFF, 0x0004, 0x2AFD, 0x0048, 0x005C, 0x0004, 0x009D, 0x0018, 0x11BF,
        0x0004, 0x01BD, 0x0088, 0x006C, 0x0004, 0x88AD, 0x0028, 0x22EF, 0x0004, 0x8AED, 0x0048, 0x003C, 0x0004,
        0x888D, 0x0018, 0x227F, 0x0004, 0x00CA, 0x0088, 0x006D, 0x0004, 0x88CD, 0x0028, 0x4EEF, 0x0004, 0x8BFD,
        0x0048, 0x444D, 0x0004, 0x009C, 0x0018, 0x2AAF, 0x0004, 0x4EFD, 0x0088, 0x445D, 0x0004, 0x00AC, 0x0028,
        0x8DDF, 0x0004, 0x45DD, 0x0048, 0x222D, 0x0004, 0x003D, 0x0018, 0x155F, 0x0004, 0x005A, 0x0088, 0x006C,
        0x0004, 0x88DD, 0x0028, 0x23FF, 0x0004, 0x11FD, 0x0048, 0x444D, 0x0004, 0x00AD, 0x0018, 0x00BE, 0x0004,
        0x137D, 0x0088, 0x155D, 0x0004, 0x00CC, 0x0028, 0x00DE, 0x0004, 0x02ED, 0x0048, 0x111D, 0x0004, 0x009D,
        0x0018, 0x007E, 0x0004, 0x005A, 0x0088, 0x455D, 0x0004, 0x44CD, 0x0028, 0x00EE, 0x0004, 0x1FFD, 0x0048,
        0x003C, 0x0004, 0x00AC, 0x0018, 0x555F, 0x0004, 0x47FD, 0x0088, 0x113D, 0x0004, 0x02BD, 0x0028, 0x477F,
        0x0004, 0x4CDD, 0x0048, 0x8FFF, 0x0004, 0x009C, 0x0018, 0x222F, 0x0004, 0x005A, 0x0088, 0x006C, 0x0004,
        0x88DD, 0x0028, 0x00FE, 0x0004, 0x11FD, 0x0048, 0x444D, 0x0004, 0x00AD, 0x0018, 0x888F, 0x0004, 0x137D,
        0x0088, 0x155D, 0x0004, 0x00CC, 0x0028, 0x8CCF, 0x0004, 0x02ED, 0x0048, 0x111D, 0x0004, 0x009D, 0x0018,
        0x006F, 0x0004, 0x005A, 0x0088, 0x455D, 0x0004, 0x44CD, 0x0028, 0x1DDF, 0x0004, 0x1FFD, 0x0048, 0x003C,
        0x0004, 0x00AC, 0x0018, 0x227F, 0x0004, 0x47FD, 0x0088, 0x113D, 0x0004, 0x02BD, 0x0028, 0x22BF, 0x0004,
        0x4CDD, 0x0048, 0x22EF, 0x0004, 0x009C, 0x0018, 0x233F, 0x0006, 0x4DDD, 0x4FFB, 0xCFFF, 0x0018, 0x113D,
        0x005A, 0x888F, 0x0006, 0x23BD, 0x008A, 0x00EE, 0x002A, 0x155D, 0xAAFD, 0x277F, 0x0006, 0x44CD, 0x8FFB,
        0x44EF, 0x0018, 0x467D, 0x004A, 0x2AAF, 0x0006, 0x00AC, 0x555B, 0x99DF, 0x1FFB, 0x003C, 0x5FFD, 0x266F,
        0x0006, 0x1DDD, 0x4FFB, 0x6EFF, 0x0018, 0x177D, 0x005A, 0x1BBF, 0x0006, 0x88AD, 0x008A, 0x5DDF, 0x002A,
        0x444D, 0x2FFD, 0x667F, 0x0006, 0x00CC, 0x8FFB, 0x2EEF, 0x0018, 0x455D, 0x004A, 0x119F, 0x0006, 0x009C,
        0x555B, 0x8CCF, 0x1FFB, 0x111D, 0x8CED, 0x006E, 0x0006, 0x4DDD, 0x4FFB, 0x3FFF, 0x0018, 0x113D, 0x005A,
        0x11BF, 0x0006, 0x23BD, 0x008A, 0x8DDF, 0x002A, 0x155D, 0xAAFD, 0x222F, 0x0006, 0x44CD, 0x8FFB, 0x00FE,
        0x0018, 0x467D, 0x004A, 0x899F, 0x0006, 0x00AC, 0x555B, 0x00DE, 0x1FFB, 0x003C, 0x5FFD, 0x446F, 0x0006,
        0x1DDD, 0x4FFB, 0x9BFF, 0x0018, 0x177D, 0x005A, 0x00BE, 0x0006, 0x88AD, 0x008A, 0xCDDF, 0x002A, 0x444D,
        0x2FFD, 0x007E, 0x0006, 0x00CC, 0x8FFB, 0x4EEF, 0x0018, 0x455D, 0x004A, 0x377F, 0x0006, 0x009C, 0x555B,
        0x8BBF, 0x1FFB, 0x111D, 0x8CED, 0x233F, 0x0004, 0x00AA, 0x0088, 0x047D, 0x0004, 0x01DD, 0x0028, 0x11DF,
        0x0004, 0x27FD, 0x0048, 0x005C, 0x0004, 0x8AAD, 0x0018, 0x2BBF, 0x0004, 0x009C, 0x0088, 0x006C, 0x0004,
        0x00CC, 0x0028, 0x00EE, 0x0004, 0x8CED, 0x0048, 0x222D, 0x0004, 0x888D, 0x0018, 0x007E, 0x0004, 0x00AA,
        0x0088, 0x006D, 0x0004, 0x88CD, 0x0028, 0x00FE, 0x0004, 0x19FD, 0x0048, 0x003C, 0x0004, 0x2AAD, 0x0018,
        0xAAAF, 0x0004, 0x8BFD, 0x0088, 0x005D, 0x0004, 0x00BD, 0x0028, 0x4CCF, 0x0004, 0x44ED, 0x0048, 0x4FFF,
        0x0004, 0x223D, 0x0018, 0x111F, 0x0004, 0x00AA, 0x0088, 0x047D, 0x0004, 0x01DD, 0x0028, 0x99FF, 0x0004,
        0x27FD, 0x0048, 0x005C, 0x0004, 0x8AAD, 0x0018, 0x00BE, 0x0004, 0x009C, 0x0088, 0x006C, 0x0004, 0x00CC,
        0x0028, 0x00DE, 0x0004, 0x8CED, 0x0048, 0x222D, 0x0004, 0x888D, 0x0018, 0x444F, 0x0004, 0x00AA, 0x0088,
        0x006D, 0x0004, 0x88CD, 0x0028, 0x2EEF, 0x0004, 0x19FD, 0x0048, 0x003C, 0x0004, 0x2AAD, 0x0018, 0x447F,
        0x0004, 0x8BFD, 0x0088, 0x005D, 0x0004, 0x00BD, 0x0028, 0x009F, 0x0004, 0x44ED, 0x0048, 0x67FF, 0x0004,
        0x223D, 0x0018, 0x133F, 0x0006, 0x00CC, 0x008A, 0x9DFF, 0x2FFB, 0x467D, 0x1FFD, 0x99BF, 0x0006, 0x2AAD,
        0x002A, 0x66EF, 0x4FFB, 0x005C, 0x2EED, 0x377F, 0x0006, 0x89BD, 0x004A, 0x00FE, 0x8FFB, 0x006C, 0x67FD,
        0x889F, 0x0006, 0x888D, 0x001A, 0x5DDF, 0x00AA, 0x222D, 0x89DD, 0x444F, 0x0006, 0x2BBD, 0x008A, 0xCFFF,
        0x2FFB, 0x226D, 0x009C, 0x00BE, 0x0006, 0xAAAD, 0x002A, 0x1DDF, 0x4FFB, 0x003C, 0x4DDD, 0x466F, 0x0006,
        0x8AAD, 0x004A, 0xAEEF, 0x8FFB, 0x445D, 0x8EED, 0x177F, 0x0006, 0x233D, 0x001A, 0x4CCF, 0x00AA, 0xAFFF,
        0x88CD, 0x133F, 0x0006, 0x00CC, 0x008A, 0x77FF, 0x2FFB, 0x467D, 0x1FFD, 0x3BBF, 0x0006, 0x2AAD, 0x002A,
        0x00EE, 0x4FFB, 0x005C, 0x2EED, 0x007E, 0x0006, 0x89BD, 0x004A, 0x4EEF, 0x8FFB, 0x006C, 0x67FD, 0x667F,
        0x0006, 0x888D, 0x001A, 0x00DE, 0x00AA, 0x222D, 0x89DD, 0x333F, 0x0006, 0x2BBD, 0x008A, 0x57FF, 0x2FFB,
        0x226D, 0x009C, 0x199F, 0x0006, 0xAAAD, 0x002A, 0x99DF, 0x4FFB, 0x003C, 0x4DDD, 0x155F, 0x0006, 0x8AAD,
        0x004A, 0xCEEF, 0x8FFB, 0x445D, 0x8EED, 0x277F, 0x0006, 0x233D, 0x001A, 0x1BBF, 0x00AA, 0x3FFF, 0x88CD,
        0x111F, 0x0006, 0x45DD, 0x2FFB, 0x111D, 0x0018, 0x467D, 0x8FFD, 0xCCCF, 0x0006, 0x19BD, 0x004A, 0x22EF,
        0x002A, 0x222D, 0x3FFD, 0x888F, 0x0006, 0x00CC, 0x008A, 0x00FE, 0x0018, 0x115D, 0xCFFD, 0x8AAF, 0x0006,
        0x00AC, 0x003A, 0x8CDF, 0x1FFB, 0x133D, 0x66FD, 0x466F, 0x0006, 0x8CCD, 0x2FFB, 0x5FFF, 0x0018, 0x006C,
        0x4FFD, 0xABBF, 0x0006, 0x22AD, 0x004A, 0x00EE, 0x002A, 0x233D, 0xAEFD, 0x377F, 0x0006, 0x2BBD, 0x008A,
        0x55DF, 0x0018, 0x005C, 0x177D, 0x119F, 0x0006, 0x009C, 0x003A, 0x4CCF, 0x1FFB, 0x333D, 0x8EED, 0x444F,
        0x0006, 0x45DD, 0x2FFB, 0x111D, 0x0018, 0x467D, 0x8FFD, 0x99BF, 0x0006, 0x19BD, 0x004A, 0x2EEF, 0x002A,
        0x222D, 0x3FFD, 0x667F, 0x0006, 0x00CC, 0x008A, 0x4EEF, 0x0018, 0x115D, 0xCFFD, 0x899F, 0x0006, 0x00AC,
        0x003A, 0x00DE, 0x1FFB, 0x133D, 0x66FD, 0x226F, 0x0006, 0x8CCD, 0x2FFB, 0x9BFF, 0x0018, 0x006C, 0x4FFD,
        0x00BE, 0x0006, 0x22AD, 0x004A, 0x1DDF, 0x002A, 0x233D, 0xAEFD, 0x007E, 0x0006, 0x2BBD, 0x008A, 0xCEEF,
        0x0018, 0x005C, 0x177D, 0x277F, 0x0006, 0x009C, 0x003A, 0x8BBF, 0x1FFB, 0x333D, 0x8EED, 0x455F, 0x1FF9,
        0x1DDD, 0xAFFB, 0x00DE, 0x8FF9, 0x001C, 0xFFFB, 0x477F, 0x4FF9, 0x177D, 0x3FFB, 0x3BBF, 0x2FF9, 0xAEEF,
        0x8EED, 0x444F, 0x1FF9, 0x22AD, 0x000A, 0x8BBF, 0x8FF9, 0x00FE, 0xCFFD, 0x007E, 0x4FF9, 0x115D, 0x5FFB,
        0x577F, 0x2FF9, 0x8DDF, 0x2EED, 0x333F, 0x1FF9, 0x2BBD, 0xAFFB, 0x88CF, 0x8FF9, 0xBFFF, 0xFFFB, 0x377F,
        0x4FF9, 0x006D, 0x3FFB, 0x00BE, 0x2FF9, 0x66EF, 0x9FFD, 0x133F, 0x1FF9, 0x009D, 0x000A, 0xABBF, 0x8FF9,
        0xDFFF, 0x6FFD, 0x006E, 0x4FF9, 0x002C, 0x5FFB, 0x888F, 0x2FF9, 0xCDDF, 0x4DDD, 0x222F, 0x1FF9, 0x1DDD,
        0xAFFB, 0x4CCF, 0x8FF9, 0x001C, 0xFFFB, 0x277F, 0x4FF9, 0x177D, 0x3FFB, 0x99BF, 0x2FF9, 0xCEEF, 0x8EED,
        0x004E, 0x1FF9, 0x22AD, 0x000A, 0x00AE, 0x8FF9, 0x7FFF, 0xCFFD, 0x005E, 0x4FF9, 0x115D, 0x5FFB, 0x009E,
        0x2FF9, 0x5DDF, 0x2EED, 0x003E, 0x1FF9, 0x2BBD, 0xAFFB, 0x00CE, 0x8FF9, 0xEFFF, 0xFFFB, 0x667F, 0x4FF9,
        0x006D, 0x3FFB, 0x8AAF, 0x2FF9, 0x00EE, 0x9FFD, 0x233F, 0x1FF9, 0x009D, 0x000A, 0x1BBF, 0x8FF9, 0x4EEF,
        0x6FFD, 0x455F, 0x4FF9, 0x002C, 0x5FFB, 0x008E, 0x2FF9, 0x99DF, 0x4DDD, 0x111F};
