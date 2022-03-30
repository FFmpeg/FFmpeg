/*
 * WavPack lossless audio decoder
 * Copyright (c) 2006,2011 Konstantin Shishkov
 * Copyright (c) 2020 David Bryant
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

#include "libavutil/buffer.h"
#include "libavutil/channel_layout.h"

#define BITSTREAM_READER_LE
#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "get_bits.h"
#include "thread.h"
#include "threadframe.h"
#include "unary.h"
#include "wavpack.h"
#include "dsd.h"

/**
 * @file
 * WavPack lossless audio decoder
 */

#define DSD_BYTE_READY(low,high) (!(((low) ^ (high)) & 0xff000000))

#define PTABLE_BITS 8
#define PTABLE_BINS (1<<PTABLE_BITS)
#define PTABLE_MASK (PTABLE_BINS-1)

#define UP   0x010000fe
#define DOWN 0x00010000
#define DECAY 8

#define PRECISION 20
#define VALUE_ONE (1 << PRECISION)
#define PRECISION_USE 12

#define RATE_S 20

#define MAX_HISTORY_BITS    5
#define MAX_HISTORY_BINS    (1 << MAX_HISTORY_BITS)
#define MAX_BIN_BYTES       1280    // for value_lookup, per bin (2k - 512 - 256)

typedef enum {
    MODULATION_PCM,     // pulse code modulation
    MODULATION_DSD      // pulse density modulation (aka DSD)
} Modulation;

typedef struct WavpackFrameContext {
    AVCodecContext *avctx;
    int frame_flags;
    int stereo, stereo_in;
    int joint;
    uint32_t CRC;
    GetBitContext gb;
    int got_extra_bits;
    uint32_t crc_extra_bits;
    GetBitContext gb_extra_bits;
    int samples;
    int terms;
    Decorr decorr[MAX_TERMS];
    int zero, one, zeroes;
    int extra_bits;
    int and, or, shift;
    int post_shift;
    int hybrid, hybrid_bitrate;
    int hybrid_maxclip, hybrid_minclip;
    int float_flag;
    int float_shift;
    int float_max_exp;
    WvChannel ch[2];

    GetByteContext gbyte;
    int ptable [PTABLE_BINS];
    uint8_t value_lookup_buffer[MAX_HISTORY_BINS*MAX_BIN_BYTES];
    uint16_t summed_probabilities[MAX_HISTORY_BINS][256];
    uint8_t probabilities[MAX_HISTORY_BINS][256];
    uint8_t *value_lookup[MAX_HISTORY_BINS];
} WavpackFrameContext;

#define WV_MAX_FRAME_DECODERS 14

typedef struct WavpackContext {
    AVCodecContext *avctx;

    WavpackFrameContext *fdec[WV_MAX_FRAME_DECODERS];
    int fdec_num;

    int block;
    int samples;
    int ch_offset;

    AVFrame *frame;
    ThreadFrame curr_frame, prev_frame;
    Modulation modulation;

    AVBufferRef *dsd_ref;
    DSDContext *dsdctx;
    int dsd_channels;
} WavpackContext;

#define LEVEL_DECAY(a)  (((a) + 0x80) >> 8)

static av_always_inline unsigned get_tail(GetBitContext *gb, int k)
{
    int p, e, res;

    if (k < 1)
        return 0;
    p   = av_log2(k);
    e   = (1 << (p + 1)) - k - 1;
    res = get_bitsz(gb, p);
    if (res >= e)
        res = (res << 1) - e + get_bits1(gb);
    return res;
}

static int update_error_limit(WavpackFrameContext *ctx)
{
    int i, br[2], sl[2];

    for (i = 0; i <= ctx->stereo_in; i++) {
        if (ctx->ch[i].bitrate_acc > UINT_MAX - ctx->ch[i].bitrate_delta)
            return AVERROR_INVALIDDATA;
        ctx->ch[i].bitrate_acc += ctx->ch[i].bitrate_delta;
        br[i]                   = ctx->ch[i].bitrate_acc >> 16;
        sl[i]                   = LEVEL_DECAY(ctx->ch[i].slow_level);
    }
    if (ctx->stereo_in && ctx->hybrid_bitrate) {
        int balance = (sl[1] - sl[0] + br[1] + 1) >> 1;
        if (balance > br[0]) {
            br[1] = br[0] * 2;
            br[0] = 0;
        } else if (-balance > br[0]) {
            br[0]  *= 2;
            br[1]   = 0;
        } else {
            br[1] = br[0] + balance;
            br[0] = br[0] - balance;
        }
    }
    for (i = 0; i <= ctx->stereo_in; i++) {
        if (ctx->hybrid_bitrate) {
            if (sl[i] - br[i] > -0x100)
                ctx->ch[i].error_limit = wp_exp2(sl[i] - br[i] + 0x100);
            else
                ctx->ch[i].error_limit = 0;
        } else {
            ctx->ch[i].error_limit = wp_exp2(br[i]);
        }
    }

    return 0;
}

static int wv_get_value(WavpackFrameContext *ctx, GetBitContext *gb,
                        int channel, int *last)
{
    int t, t2;
    int sign, base, add, ret;
    WvChannel *c = &ctx->ch[channel];

    *last = 0;

    if ((ctx->ch[0].median[0] < 2U) && (ctx->ch[1].median[0] < 2U) &&
        !ctx->zero && !ctx->one) {
        if (ctx->zeroes) {
            ctx->zeroes--;
            if (ctx->zeroes) {
                c->slow_level -= LEVEL_DECAY(c->slow_level);
                return 0;
            }
        } else {
            t = get_unary_0_33(gb);
            if (t >= 2) {
                if (t >= 32 || get_bits_left(gb) < t - 1)
                    goto error;
                t = get_bits_long(gb, t - 1) | (1 << (t - 1));
            } else {
                if (get_bits_left(gb) < 0)
                    goto error;
            }
            ctx->zeroes = t;
            if (ctx->zeroes) {
                memset(ctx->ch[0].median, 0, sizeof(ctx->ch[0].median));
                memset(ctx->ch[1].median, 0, sizeof(ctx->ch[1].median));
                c->slow_level -= LEVEL_DECAY(c->slow_level);
                return 0;
            }
        }
    }

    if (ctx->zero) {
        t         = 0;
        ctx->zero = 0;
    } else {
        t = get_unary_0_33(gb);
        if (get_bits_left(gb) < 0)
            goto error;
        if (t == 16) {
            t2 = get_unary_0_33(gb);
            if (t2 < 2) {
                if (get_bits_left(gb) < 0)
                    goto error;
                t += t2;
            } else {
                if (t2 >= 32 || get_bits_left(gb) < t2 - 1)
                    goto error;
                t += get_bits_long(gb, t2 - 1) | (1 << (t2 - 1));
            }
        }

        if (ctx->one) {
            ctx->one = t & 1;
            t        = (t >> 1) + 1;
        } else {
            ctx->one = t & 1;
            t      >>= 1;
        }
        ctx->zero = !ctx->one;
    }

    if (ctx->hybrid && !channel) {
        if (update_error_limit(ctx) < 0)
            goto error;
    }

    if (!t) {
        base = 0;
        add  = GET_MED(0) - 1;
        DEC_MED(0);
    } else if (t == 1) {
        base = GET_MED(0);
        add  = GET_MED(1) - 1;
        INC_MED(0);
        DEC_MED(1);
    } else if (t == 2) {
        base = GET_MED(0) + GET_MED(1);
        add  = GET_MED(2) - 1;
        INC_MED(0);
        INC_MED(1);
        DEC_MED(2);
    } else {
        base = GET_MED(0) + GET_MED(1) + GET_MED(2) * (t - 2U);
        add  = GET_MED(2) - 1;
        INC_MED(0);
        INC_MED(1);
        INC_MED(2);
    }
    if (!c->error_limit) {
        if (add >= 0x2000000U) {
            av_log(ctx->avctx, AV_LOG_ERROR, "k %d is too large\n", add);
            goto error;
        }
        ret = base + get_tail(gb, add);
        if (get_bits_left(gb) <= 0)
            goto error;
    } else {
        int mid = (base * 2U + add + 1) >> 1;
        while (add > c->error_limit) {
            if (get_bits_left(gb) <= 0)
                goto error;
            if (get_bits1(gb)) {
                add -= (mid - (unsigned)base);
                base = mid;
            } else
                add = mid - (unsigned)base - 1;
            mid = (base * 2U + add + 1) >> 1;
        }
        ret = mid;
    }
    sign = get_bits1(gb);
    if (ctx->hybrid_bitrate)
        c->slow_level += wp_log2(ret) - LEVEL_DECAY(c->slow_level);
    return sign ? ~ret : ret;

error:
    ret = get_bits_left(gb);
    if (ret <= 0) {
        av_log(ctx->avctx, AV_LOG_ERROR, "Too few bits (%d) left\n", ret);
    }
    *last = 1;
    return 0;
}

static inline int wv_get_value_integer(WavpackFrameContext *s, uint32_t *crc,
                                       unsigned S)
{
    unsigned bit;

    if (s->extra_bits) {
        S *= 1 << s->extra_bits;

        if (s->got_extra_bits &&
            get_bits_left(&s->gb_extra_bits) >= s->extra_bits) {
            S   |= get_bits_long(&s->gb_extra_bits, s->extra_bits);
            *crc = *crc * 9 + (S & 0xffff) * 3 + ((unsigned)S >> 16);
        }
    }

    bit = (S & s->and) | s->or;
    bit = ((S + bit) << s->shift) - bit;

    if (s->hybrid)
        bit = av_clip(bit, s->hybrid_minclip, s->hybrid_maxclip);

    return bit << s->post_shift;
}

static float wv_get_value_float(WavpackFrameContext *s, uint32_t *crc, int S)
{
    union {
        float    f;
        uint32_t u;
    } value;

    unsigned int sign;
    int exp = s->float_max_exp;

    if (s->got_extra_bits) {
        const int max_bits  = 1 + 23 + 8 + 1;
        const int left_bits = get_bits_left(&s->gb_extra_bits);

        if (left_bits + 8 * AV_INPUT_BUFFER_PADDING_SIZE < max_bits)
            return 0.0;
    }

    if (S) {
        S  *= 1U << s->float_shift;
        sign = S < 0;
        if (sign)
            S = -(unsigned)S;
        if (S >= 0x1000000U) {
            if (s->got_extra_bits && get_bits1(&s->gb_extra_bits))
                S = get_bits(&s->gb_extra_bits, 23);
            else
                S = 0;
            exp = 255;
        } else if (exp) {
            int shift = 23 - av_log2(S);
            exp = s->float_max_exp;
            if (exp <= shift)
                shift = --exp;
            exp -= shift;

            if (shift) {
                S <<= shift;
                if ((s->float_flag & WV_FLT_SHIFT_ONES) ||
                    (s->got_extra_bits &&
                     (s->float_flag & WV_FLT_SHIFT_SAME) &&
                     get_bits1(&s->gb_extra_bits))) {
                    S |= (1 << shift) - 1;
                } else if (s->got_extra_bits &&
                           (s->float_flag & WV_FLT_SHIFT_SENT)) {
                    S |= get_bits(&s->gb_extra_bits, shift);
                }
            }
        } else {
            exp = s->float_max_exp;
        }
        S &= 0x7fffff;
    } else {
        sign = 0;
        exp  = 0;
        if (s->got_extra_bits && (s->float_flag & WV_FLT_ZERO_SENT)) {
            if (get_bits1(&s->gb_extra_bits)) {
                S = get_bits(&s->gb_extra_bits, 23);
                if (s->float_max_exp >= 25)
                    exp = get_bits(&s->gb_extra_bits, 8);
                sign = get_bits1(&s->gb_extra_bits);
            } else {
                if (s->float_flag & WV_FLT_ZERO_SIGN)
                    sign = get_bits1(&s->gb_extra_bits);
            }
        }
    }

    *crc = *crc * 27 + S * 9 + exp * 3 + sign;

    value.u = (sign << 31) | (exp << 23) | S;
    return value.f;
}

static inline int wv_check_crc(WavpackFrameContext *s, uint32_t crc,
                               uint32_t crc_extra_bits)
{
    if (crc != s->CRC) {
        av_log(s->avctx, AV_LOG_ERROR, "CRC error\n");
        return AVERROR_INVALIDDATA;
    }
    if (s->got_extra_bits && crc_extra_bits != s->crc_extra_bits) {
        av_log(s->avctx, AV_LOG_ERROR, "Extra bits CRC error\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static void init_ptable(int *table, int rate_i, int rate_s)
{
    int value = 0x808000, rate = rate_i << 8;

    for (int c = (rate + 128) >> 8; c--;)
        value += (DOWN - value) >> DECAY;

    for (int i = 0; i < PTABLE_BINS/2; i++) {
        table[i] = value;
        table[PTABLE_BINS-1-i] = 0x100ffff - value;

        if (value > 0x010000) {
            rate += (rate * rate_s + 128) >> 8;

            for (int c = (rate + 64) >> 7; c--;)
                value += (DOWN - value) >> DECAY;
        }
    }
}

typedef struct {
    int32_t value, fltr0, fltr1, fltr2, fltr3, fltr4, fltr5, fltr6, factor;
    unsigned int byte;
} DSDfilters;

static int wv_unpack_dsd_high(WavpackFrameContext *s, uint8_t *dst_left, uint8_t *dst_right)
{
    uint32_t checksum = 0xFFFFFFFF;
    uint8_t *dst_l = dst_left, *dst_r = dst_right;
    int total_samples = s->samples, stereo = dst_r ? 1 : 0;
    DSDfilters filters[2], *sp = filters;
    int rate_i, rate_s;
    uint32_t low, high, value;

    if (bytestream2_get_bytes_left(&s->gbyte) < (stereo ? 20 : 13))
        return AVERROR_INVALIDDATA;

    rate_i = bytestream2_get_byte(&s->gbyte);
    rate_s = bytestream2_get_byte(&s->gbyte);

    if (rate_s != RATE_S)
        return AVERROR_INVALIDDATA;

    init_ptable(s->ptable, rate_i, rate_s);

    for (int channel = 0; channel < stereo + 1; channel++) {
        DSDfilters *sp = filters + channel;

        sp->fltr1 = bytestream2_get_byte(&s->gbyte) << (PRECISION - 8);
        sp->fltr2 = bytestream2_get_byte(&s->gbyte) << (PRECISION - 8);
        sp->fltr3 = bytestream2_get_byte(&s->gbyte) << (PRECISION - 8);
        sp->fltr4 = bytestream2_get_byte(&s->gbyte) << (PRECISION - 8);
        sp->fltr5 = bytestream2_get_byte(&s->gbyte) << (PRECISION - 8);
        sp->fltr6 = 0;
        sp->factor = bytestream2_get_byte(&s->gbyte) & 0xff;
        sp->factor |= (bytestream2_get_byte(&s->gbyte) << 8) & 0xff00;
        sp->factor = (int32_t)((uint32_t)sp->factor << 16) >> 16;
    }

    value = bytestream2_get_be32(&s->gbyte);
    high = 0xffffffff;
    low = 0x0;

    while (total_samples--) {
        int bitcount = 8;

        sp[0].value = sp[0].fltr1 - sp[0].fltr5 + ((sp[0].fltr6 * sp[0].factor) >> 2);

        if (stereo)
            sp[1].value = sp[1].fltr1 - sp[1].fltr5 + ((sp[1].fltr6 * sp[1].factor) >> 2);

        while (bitcount--) {
            int32_t *pp = s->ptable + ((sp[0].value >> (PRECISION - PRECISION_USE)) & PTABLE_MASK);
            uint32_t split = low + ((high - low) >> 8) * (*pp >> 16);

            if (value <= split) {
                high = split;
                *pp += (UP - *pp) >> DECAY;
                sp[0].fltr0 = -1;
            } else {
                low = split + 1;
                *pp += (DOWN - *pp) >> DECAY;
                sp[0].fltr0 = 0;
            }

            while (DSD_BYTE_READY(high, low) && bytestream2_get_bytes_left(&s->gbyte)) {
                value = (value << 8) | bytestream2_get_byte(&s->gbyte);
                high = (high << 8) | 0xff;
                low <<= 8;
            }

            sp[0].value += sp[0].fltr6 * 8;
            sp[0].byte = (sp[0].byte << 1) | (sp[0].fltr0 & 1);
            sp[0].factor += (((sp[0].value ^ sp[0].fltr0) >> 31) | 1) &
                ((sp[0].value ^ (sp[0].value - (sp[0].fltr6 * 16))) >> 31);
            sp[0].fltr1 += ((sp[0].fltr0 & VALUE_ONE) - sp[0].fltr1) >> 6;
            sp[0].fltr2 += ((sp[0].fltr0 & VALUE_ONE) - sp[0].fltr2) >> 4;
            sp[0].fltr3 += (sp[0].fltr2 - sp[0].fltr3) >> 4;
            sp[0].fltr4 += (sp[0].fltr3 - sp[0].fltr4) >> 4;
            sp[0].value = (sp[0].fltr4 - sp[0].fltr5) >> 4;
            sp[0].fltr5 += sp[0].value;
            sp[0].fltr6 += (sp[0].value - sp[0].fltr6) >> 3;
            sp[0].value = sp[0].fltr1 - sp[0].fltr5 + ((sp[0].fltr6 * sp[0].factor) >> 2);

            if (!stereo)
                continue;

            pp = s->ptable + ((sp[1].value >> (PRECISION - PRECISION_USE)) & PTABLE_MASK);
            split = low + ((high - low) >> 8) * (*pp >> 16);

            if (value <= split) {
                high = split;
                *pp += (UP - *pp) >> DECAY;
                sp[1].fltr0 = -1;
            } else {
                low = split + 1;
                *pp += (DOWN - *pp) >> DECAY;
                sp[1].fltr0 = 0;
            }

            while (DSD_BYTE_READY(high, low) && bytestream2_get_bytes_left(&s->gbyte)) {
                value = (value << 8) | bytestream2_get_byte(&s->gbyte);
                high = (high << 8) | 0xff;
                low <<= 8;
            }

            sp[1].value += sp[1].fltr6 * 8;
            sp[1].byte = (sp[1].byte << 1) | (sp[1].fltr0 & 1);
            sp[1].factor += (((sp[1].value ^ sp[1].fltr0) >> 31) | 1) &
                ((sp[1].value ^ (sp[1].value - (sp[1].fltr6 * 16))) >> 31);
            sp[1].fltr1 += ((sp[1].fltr0 & VALUE_ONE) - sp[1].fltr1) >> 6;
            sp[1].fltr2 += ((sp[1].fltr0 & VALUE_ONE) - sp[1].fltr2) >> 4;
            sp[1].fltr3 += (sp[1].fltr2 - sp[1].fltr3) >> 4;
            sp[1].fltr4 += (sp[1].fltr3 - sp[1].fltr4) >> 4;
            sp[1].value = (sp[1].fltr4 - sp[1].fltr5) >> 4;
            sp[1].fltr5 += sp[1].value;
            sp[1].fltr6 += (sp[1].value - sp[1].fltr6) >> 3;
            sp[1].value = sp[1].fltr1 - sp[1].fltr5 + ((sp[1].fltr6 * sp[1].factor) >> 2);
        }

        checksum += (checksum << 1) + (*dst_l = sp[0].byte & 0xff);
        sp[0].factor -= (sp[0].factor + 512) >> 10;
        dst_l += 4;

        if (stereo) {
            checksum += (checksum << 1) + (*dst_r = filters[1].byte & 0xff);
            filters[1].factor -= (filters[1].factor + 512) >> 10;
            dst_r += 4;
        }
    }

    if (wv_check_crc(s, checksum, 0)) {
        if (s->avctx->err_recognition & AV_EF_CRCCHECK)
            return AVERROR_INVALIDDATA;

        memset(dst_left, 0x69, s->samples * 4);

        if (dst_r)
            memset(dst_right, 0x69, s->samples * 4);
    }

    return 0;
}

static int wv_unpack_dsd_fast(WavpackFrameContext *s, uint8_t *dst_left, uint8_t *dst_right)
{
    uint8_t *dst_l = dst_left, *dst_r = dst_right;
    uint8_t history_bits, max_probability;
    int total_summed_probabilities  = 0;
    int total_samples               = s->samples;
    uint8_t *vlb                    = s->value_lookup_buffer;
    int history_bins, p0, p1, chan;
    uint32_t checksum               = 0xFFFFFFFF;
    uint32_t low, high, value;

    if (!bytestream2_get_bytes_left(&s->gbyte))
        return AVERROR_INVALIDDATA;

    history_bits = bytestream2_get_byte(&s->gbyte);

    if (!bytestream2_get_bytes_left(&s->gbyte) || history_bits > MAX_HISTORY_BITS)
        return AVERROR_INVALIDDATA;

    history_bins = 1 << history_bits;
    max_probability = bytestream2_get_byte(&s->gbyte);

    if (max_probability < 0xff) {
        uint8_t *outptr = (uint8_t *)s->probabilities;
        uint8_t *outend = outptr + sizeof(*s->probabilities) * history_bins;

        while (outptr < outend && bytestream2_get_bytes_left(&s->gbyte)) {
            int code = bytestream2_get_byte(&s->gbyte);

            if (code > max_probability) {
                int zcount = code - max_probability;

                while (outptr < outend && zcount--)
                    *outptr++ = 0;
            } else if (code) {
                *outptr++ = code;
            }
            else {
                break;
            }
        }

        if (outptr < outend ||
            (bytestream2_get_bytes_left(&s->gbyte) && bytestream2_get_byte(&s->gbyte)))
                return AVERROR_INVALIDDATA;
    } else if (bytestream2_get_bytes_left(&s->gbyte) > (int)sizeof(*s->probabilities) * history_bins) {
        bytestream2_get_buffer(&s->gbyte, (uint8_t *)s->probabilities,
            sizeof(*s->probabilities) * history_bins);
    } else {
        return AVERROR_INVALIDDATA;
    }

    for (p0 = 0; p0 < history_bins; p0++) {
        int32_t sum_values = 0;

        for (int i = 0; i < 256; i++)
            s->summed_probabilities[p0][i] = sum_values += s->probabilities[p0][i];

        if (sum_values) {
            total_summed_probabilities += sum_values;

            if (total_summed_probabilities > history_bins * MAX_BIN_BYTES)
                return AVERROR_INVALIDDATA;

            s->value_lookup[p0] = vlb;

            for (int i = 0; i < 256; i++) {
                int c = s->probabilities[p0][i];

                while (c--)
                    *vlb++ = i;
            }
        }
    }

    if (bytestream2_get_bytes_left(&s->gbyte) < 4)
        return AVERROR_INVALIDDATA;

    chan = p0 = p1 = 0;
    low = 0; high = 0xffffffff;
    value = bytestream2_get_be32(&s->gbyte);

    if (dst_r)
        total_samples *= 2;

    while (total_samples--) {
        unsigned int mult, index, code;

        if (!s->summed_probabilities[p0][255])
            return AVERROR_INVALIDDATA;

        mult = (high - low) / s->summed_probabilities[p0][255];

        if (!mult) {
            if (bytestream2_get_bytes_left(&s->gbyte) >= 4)
                value = bytestream2_get_be32(&s->gbyte);

            low = 0;
            high = 0xffffffff;
            mult = high / s->summed_probabilities[p0][255];

            if (!mult)
                return AVERROR_INVALIDDATA;
        }

        index = (value - low) / mult;

        if (index >= s->summed_probabilities[p0][255])
            return AVERROR_INVALIDDATA;

        if (!dst_r) {
            if ((*dst_l = code = s->value_lookup[p0][index]))
                low += s->summed_probabilities[p0][code-1] * mult;

            dst_l += 4;
        } else {
            if ((code = s->value_lookup[p0][index]))
                low += s->summed_probabilities[p0][code-1] * mult;

            if (chan) {
                *dst_r = code;
                dst_r += 4;
            }
            else {
                *dst_l = code;
                dst_l += 4;
            }

            chan ^= 1;
        }

        high = low + s->probabilities[p0][code] * mult - 1;
        checksum += (checksum << 1) + code;

        if (!dst_r) {
            p0 = code & (history_bins-1);
        } else {
            p0 = p1;
            p1 = code & (history_bins-1);
        }

        while (DSD_BYTE_READY(high, low) && bytestream2_get_bytes_left(&s->gbyte)) {
            value = (value << 8) | bytestream2_get_byte(&s->gbyte);
            high = (high << 8) | 0xff;
            low <<= 8;
        }
    }

    if (wv_check_crc(s, checksum, 0)) {
        if (s->avctx->err_recognition & AV_EF_CRCCHECK)
            return AVERROR_INVALIDDATA;

        memset(dst_left, 0x69, s->samples * 4);

        if (dst_r)
            memset(dst_right, 0x69, s->samples * 4);
    }

    return 0;
}

static int wv_unpack_dsd_copy(WavpackFrameContext *s, uint8_t *dst_left, uint8_t *dst_right)
{
    uint8_t *dst_l = dst_left, *dst_r = dst_right;
    int total_samples           = s->samples;
    uint32_t checksum           = 0xFFFFFFFF;

    if (bytestream2_get_bytes_left(&s->gbyte) != total_samples * (dst_r ? 2 : 1))
        return AVERROR_INVALIDDATA;

    while (total_samples--) {
        checksum += (checksum << 1) + (*dst_l = bytestream2_get_byte(&s->gbyte));
        dst_l += 4;

        if (dst_r) {
            checksum += (checksum << 1) + (*dst_r = bytestream2_get_byte(&s->gbyte));
            dst_r += 4;
        }
    }

    if (wv_check_crc(s, checksum, 0)) {
        if (s->avctx->err_recognition & AV_EF_CRCCHECK)
            return AVERROR_INVALIDDATA;

        memset(dst_left, 0x69, s->samples * 4);

        if (dst_r)
            memset(dst_right, 0x69, s->samples * 4);
    }

    return 0;
}

static inline int wv_unpack_stereo(WavpackFrameContext *s, GetBitContext *gb,
                                   void *dst_l, void *dst_r, const int type)
{
    int i, j, count = 0;
    int last, t;
    int A, B, L, L2, R, R2;
    int pos                 = 0;
    uint32_t crc            = 0xFFFFFFFF;
    uint32_t crc_extra_bits = 0xFFFFFFFF;
    int16_t *dst16_l        = dst_l;
    int16_t *dst16_r        = dst_r;
    int32_t *dst32_l        = dst_l;
    int32_t *dst32_r        = dst_r;
    float *dstfl_l          = dst_l;
    float *dstfl_r          = dst_r;

    s->one = s->zero = s->zeroes = 0;
    do {
        L = wv_get_value(s, gb, 0, &last);
        if (last)
            break;
        R = wv_get_value(s, gb, 1, &last);
        if (last)
            break;
        for (i = 0; i < s->terms; i++) {
            t = s->decorr[i].value;
            if (t > 0) {
                if (t > 8) {
                    if (t & 1) {
                        A = 2U * s->decorr[i].samplesA[0] - s->decorr[i].samplesA[1];
                        B = 2U * s->decorr[i].samplesB[0] - s->decorr[i].samplesB[1];
                    } else {
                        A = (int)(3U * s->decorr[i].samplesA[0] - s->decorr[i].samplesA[1]) >> 1;
                        B = (int)(3U * s->decorr[i].samplesB[0] - s->decorr[i].samplesB[1]) >> 1;
                    }
                    s->decorr[i].samplesA[1] = s->decorr[i].samplesA[0];
                    s->decorr[i].samplesB[1] = s->decorr[i].samplesB[0];
                    j                        = 0;
                } else {
                    A = s->decorr[i].samplesA[pos];
                    B = s->decorr[i].samplesB[pos];
                    j = (pos + t) & 7;
                }
                if (type != AV_SAMPLE_FMT_S16P) {
                    L2 = L + ((s->decorr[i].weightA * (int64_t)A + 512) >> 10);
                    R2 = R + ((s->decorr[i].weightB * (int64_t)B + 512) >> 10);
                } else {
                    L2 = L + (unsigned)((int)(s->decorr[i].weightA * (unsigned)A + 512) >> 10);
                    R2 = R + (unsigned)((int)(s->decorr[i].weightB * (unsigned)B + 512) >> 10);
                }
                if (A && L)
                    s->decorr[i].weightA -= ((((L ^ A) >> 30) & 2) - 1) * s->decorr[i].delta;
                if (B && R)
                    s->decorr[i].weightB -= ((((R ^ B) >> 30) & 2) - 1) * s->decorr[i].delta;
                s->decorr[i].samplesA[j] = L = L2;
                s->decorr[i].samplesB[j] = R = R2;
            } else if (t == -1) {
                if (type != AV_SAMPLE_FMT_S16P)
                    L2 = L + ((s->decorr[i].weightA * (int64_t)s->decorr[i].samplesA[0] + 512) >> 10);
                else
                    L2 = L + (unsigned)((int)(s->decorr[i].weightA * (unsigned)s->decorr[i].samplesA[0] + 512) >> 10);
                UPDATE_WEIGHT_CLIP(s->decorr[i].weightA, s->decorr[i].delta, s->decorr[i].samplesA[0], L);
                L = L2;
                if (type != AV_SAMPLE_FMT_S16P)
                    R2 = R + ((s->decorr[i].weightB * (int64_t)L2 + 512) >> 10);
                else
                    R2 = R + (unsigned)((int)(s->decorr[i].weightB * (unsigned)L2 + 512) >> 10);
                UPDATE_WEIGHT_CLIP(s->decorr[i].weightB, s->decorr[i].delta, L2, R);
                R                        = R2;
                s->decorr[i].samplesA[0] = R;
            } else {
                if (type != AV_SAMPLE_FMT_S16P)
                    R2 = R + ((s->decorr[i].weightB * (int64_t)s->decorr[i].samplesB[0] + 512) >> 10);
                else
                    R2 = R + (unsigned)((int)(s->decorr[i].weightB * (unsigned)s->decorr[i].samplesB[0] + 512) >> 10);
                UPDATE_WEIGHT_CLIP(s->decorr[i].weightB, s->decorr[i].delta, s->decorr[i].samplesB[0], R);
                R = R2;

                if (t == -3) {
                    R2                       = s->decorr[i].samplesA[0];
                    s->decorr[i].samplesA[0] = R;
                }

                if (type != AV_SAMPLE_FMT_S16P)
                    L2 = L + ((s->decorr[i].weightA * (int64_t)R2 + 512) >> 10);
                else
                    L2 = L + (unsigned)((int)(s->decorr[i].weightA * (unsigned)R2 + 512) >> 10);
                UPDATE_WEIGHT_CLIP(s->decorr[i].weightA, s->decorr[i].delta, R2, L);
                L                        = L2;
                s->decorr[i].samplesB[0] = L;
            }
        }

        if (type == AV_SAMPLE_FMT_S16P) {
            if (FFABS((int64_t)L) + FFABS((int64_t)R) > (1<<19)) {
                av_log(s->avctx, AV_LOG_ERROR, "sample %d %d too large\n", L, R);
                return AVERROR_INVALIDDATA;
            }
        }

        pos = (pos + 1) & 7;
        if (s->joint)
            L += (unsigned)(R -= (unsigned)(L >> 1));
        crc = (crc * 3 + L) * 3 + R;

        if (type == AV_SAMPLE_FMT_FLTP) {
            *dstfl_l++ = wv_get_value_float(s, &crc_extra_bits, L);
            *dstfl_r++ = wv_get_value_float(s, &crc_extra_bits, R);
        } else if (type == AV_SAMPLE_FMT_S32P) {
            *dst32_l++ = wv_get_value_integer(s, &crc_extra_bits, L);
            *dst32_r++ = wv_get_value_integer(s, &crc_extra_bits, R);
        } else {
            *dst16_l++ = wv_get_value_integer(s, &crc_extra_bits, L);
            *dst16_r++ = wv_get_value_integer(s, &crc_extra_bits, R);
        }
        count++;
    } while (!last && count < s->samples);

    if (last && count < s->samples) {
        int size = av_get_bytes_per_sample(type);
        memset((uint8_t*)dst_l + count*size, 0, (s->samples-count)*size);
        memset((uint8_t*)dst_r + count*size, 0, (s->samples-count)*size);
    }

    if ((s->avctx->err_recognition & AV_EF_CRCCHECK) &&
        wv_check_crc(s, crc, crc_extra_bits))
        return AVERROR_INVALIDDATA;

    return 0;
}

static inline int wv_unpack_mono(WavpackFrameContext *s, GetBitContext *gb,
                                 void *dst, const int type)
{
    int i, j, count = 0;
    int last, t;
    int A, S, T;
    int pos                  = 0;
    uint32_t crc             = 0xFFFFFFFF;
    uint32_t crc_extra_bits  = 0xFFFFFFFF;
    int16_t *dst16           = dst;
    int32_t *dst32           = dst;
    float *dstfl             = dst;

    s->one = s->zero = s->zeroes = 0;
    do {
        T = wv_get_value(s, gb, 0, &last);
        S = 0;
        if (last)
            break;
        for (i = 0; i < s->terms; i++) {
            t = s->decorr[i].value;
            if (t > 8) {
                if (t & 1)
                    A =  2U * s->decorr[i].samplesA[0] - s->decorr[i].samplesA[1];
                else
                    A = (int)(3U * s->decorr[i].samplesA[0] - s->decorr[i].samplesA[1]) >> 1;
                s->decorr[i].samplesA[1] = s->decorr[i].samplesA[0];
                j                        = 0;
            } else {
                A = s->decorr[i].samplesA[pos];
                j = (pos + t) & 7;
            }
            if (type != AV_SAMPLE_FMT_S16P)
                S = T + ((s->decorr[i].weightA * (int64_t)A + 512) >> 10);
            else
                S = T + (unsigned)((int)(s->decorr[i].weightA * (unsigned)A + 512) >> 10);
            if (A && T)
                s->decorr[i].weightA -= ((((T ^ A) >> 30) & 2) - 1) * s->decorr[i].delta;
            s->decorr[i].samplesA[j] = T = S;
        }
        pos = (pos + 1) & 7;
        crc = crc * 3 + S;

        if (type == AV_SAMPLE_FMT_FLTP) {
            *dstfl++ = wv_get_value_float(s, &crc_extra_bits, S);
        } else if (type == AV_SAMPLE_FMT_S32P) {
            *dst32++ = wv_get_value_integer(s, &crc_extra_bits, S);
        } else {
            *dst16++ = wv_get_value_integer(s, &crc_extra_bits, S);
        }
        count++;
    } while (!last && count < s->samples);

    if (last && count < s->samples) {
        int size = av_get_bytes_per_sample(type);
        memset((uint8_t*)dst + count*size, 0, (s->samples-count)*size);
    }

    if (s->avctx->err_recognition & AV_EF_CRCCHECK) {
        int ret = wv_check_crc(s, crc, crc_extra_bits);
        if (ret < 0 && s->avctx->err_recognition & AV_EF_EXPLODE)
            return ret;
    }

    return 0;
}

static av_cold int wv_alloc_frame_context(WavpackContext *c)
{
    if (c->fdec_num == WV_MAX_FRAME_DECODERS)
        return -1;

    c->fdec[c->fdec_num] = av_mallocz(sizeof(**c->fdec));
    if (!c->fdec[c->fdec_num])
        return -1;
    c->fdec_num++;
    c->fdec[c->fdec_num - 1]->avctx = c->avctx;

    return 0;
}

static int wv_dsd_reset(WavpackContext *s, int channels)
{
    int i;

    s->dsdctx = NULL;
    s->dsd_channels = 0;
    av_buffer_unref(&s->dsd_ref);

    if (!channels)
        return 0;

    if (channels > INT_MAX / sizeof(*s->dsdctx))
        return AVERROR(EINVAL);

    s->dsd_ref = av_buffer_allocz(channels * sizeof(*s->dsdctx));
    if (!s->dsd_ref)
        return AVERROR(ENOMEM);
    s->dsdctx = (DSDContext*)s->dsd_ref->data;
    s->dsd_channels = channels;

    for (i = 0; i < channels; i++)
        memset(s->dsdctx[i].buf, 0x69, sizeof(s->dsdctx[i].buf));

    return 0;
}

#if HAVE_THREADS
static int update_thread_context(AVCodecContext *dst, const AVCodecContext *src)
{
    WavpackContext *fsrc = src->priv_data;
    WavpackContext *fdst = dst->priv_data;
    int ret;

    if (dst == src)
        return 0;

    ff_thread_release_ext_buffer(dst, &fdst->curr_frame);
    if (fsrc->curr_frame.f->data[0]) {
        if ((ret = ff_thread_ref_frame(&fdst->curr_frame, &fsrc->curr_frame)) < 0)
            return ret;
    }

    fdst->dsdctx = NULL;
    fdst->dsd_channels = 0;
    ret = av_buffer_replace(&fdst->dsd_ref, fsrc->dsd_ref);
    if (ret < 0)
        return ret;
    if (fsrc->dsd_ref) {
        fdst->dsdctx = (DSDContext*)fdst->dsd_ref->data;
        fdst->dsd_channels = fsrc->dsd_channels;
    }

    return 0;
}
#endif

static av_cold int wavpack_decode_init(AVCodecContext *avctx)
{
    WavpackContext *s = avctx->priv_data;

    s->avctx = avctx;

    s->fdec_num = 0;

    s->curr_frame.f = av_frame_alloc();
    s->prev_frame.f = av_frame_alloc();

    if (!s->curr_frame.f || !s->prev_frame.f)
        return AVERROR(ENOMEM);

    ff_init_dsd_data();

    return 0;
}

static av_cold int wavpack_decode_end(AVCodecContext *avctx)
{
    WavpackContext *s = avctx->priv_data;

    for (int i = 0; i < s->fdec_num; i++)
        av_freep(&s->fdec[i]);
    s->fdec_num = 0;

    ff_thread_release_ext_buffer(avctx, &s->curr_frame);
    av_frame_free(&s->curr_frame.f);

    ff_thread_release_ext_buffer(avctx, &s->prev_frame);
    av_frame_free(&s->prev_frame.f);

    av_buffer_unref(&s->dsd_ref);

    return 0;
}

static int wavpack_decode_block(AVCodecContext *avctx, int block_no,
                                const uint8_t *buf, int buf_size)
{
    WavpackContext *wc = avctx->priv_data;
    WavpackFrameContext *s;
    GetByteContext gb;
    enum AVSampleFormat sample_fmt;
    void *samples_l = NULL, *samples_r = NULL;
    int ret;
    int got_terms   = 0, got_weights = 0, got_samples = 0,
        got_entropy = 0, got_pcm     = 0, got_float   = 0, got_hybrid = 0;
    int got_dsd = 0;
    int i, j, id, size, ssize, weights, t;
    int bpp, chan = 0, orig_bpp, sample_rate = 0, rate_x = 1, dsd_mode = 0;
    int multiblock;
    uint64_t chmask = 0;

    if (block_no >= wc->fdec_num && wv_alloc_frame_context(wc) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error creating frame decode context\n");
        return AVERROR_INVALIDDATA;
    }

    s = wc->fdec[block_no];
    if (!s) {
        av_log(avctx, AV_LOG_ERROR, "Context for block %d is not present\n",
               block_no);
        return AVERROR_INVALIDDATA;
    }

    memset(s->decorr, 0, MAX_TERMS * sizeof(Decorr));
    memset(s->ch, 0, sizeof(s->ch));
    s->extra_bits     = 0;
    s->and            = s->or = s->shift = 0;
    s->got_extra_bits = 0;

    bytestream2_init(&gb, buf, buf_size);

    s->samples = bytestream2_get_le32(&gb);
    if (s->samples != wc->samples) {
        av_log(avctx, AV_LOG_ERROR, "Mismatching number of samples in "
               "a sequence: %d and %d\n", wc->samples, s->samples);
        return AVERROR_INVALIDDATA;
    }
    s->frame_flags = bytestream2_get_le32(&gb);

    if (s->frame_flags & (WV_FLOAT_DATA | WV_DSD_DATA))
        sample_fmt = AV_SAMPLE_FMT_FLTP;
    else if ((s->frame_flags & 0x03) <= 1)
        sample_fmt = AV_SAMPLE_FMT_S16P;
    else
        sample_fmt          = AV_SAMPLE_FMT_S32P;

    if (wc->ch_offset && avctx->sample_fmt != sample_fmt)
        return AVERROR_INVALIDDATA;

    bpp            = av_get_bytes_per_sample(sample_fmt);
    orig_bpp       = ((s->frame_flags & 0x03) + 1) << 3;
    multiblock     = (s->frame_flags & WV_SINGLE_BLOCK) != WV_SINGLE_BLOCK;

    s->stereo         = !(s->frame_flags & WV_MONO);
    s->stereo_in      =  (s->frame_flags & WV_FALSE_STEREO) ? 0 : s->stereo;
    s->joint          =   s->frame_flags & WV_JOINT_STEREO;
    s->hybrid         =   s->frame_flags & WV_HYBRID_MODE;
    s->hybrid_bitrate =   s->frame_flags & WV_HYBRID_BITRATE;
    s->post_shift     = bpp * 8 - orig_bpp + ((s->frame_flags >> 13) & 0x1f);
    if (s->post_shift < 0 || s->post_shift > 31) {
        return AVERROR_INVALIDDATA;
    }
    s->hybrid_maxclip =  ((1LL << (orig_bpp - 1)) - 1);
    s->hybrid_minclip = ((-1UL << (orig_bpp - 1)));
    s->CRC            = bytestream2_get_le32(&gb);

    // parse metadata blocks
    while (bytestream2_get_bytes_left(&gb)) {
        id   = bytestream2_get_byte(&gb);
        size = bytestream2_get_byte(&gb);
        if (id & WP_IDF_LONG)
            size |= (bytestream2_get_le16u(&gb)) << 8;
        size <<= 1; // size is specified in words
        ssize  = size;
        if (id & WP_IDF_ODD)
            size--;
        if (size < 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "Got incorrect block %02X with size %i\n", id, size);
            break;
        }
        if (bytestream2_get_bytes_left(&gb) < ssize) {
            av_log(avctx, AV_LOG_ERROR,
                   "Block size %i is out of bounds\n", size);
            break;
        }
        switch (id & WP_IDF_MASK) {
        case WP_ID_DECTERMS:
            if (size > MAX_TERMS) {
                av_log(avctx, AV_LOG_ERROR, "Too many decorrelation terms\n");
                s->terms = 0;
                bytestream2_skip(&gb, ssize);
                continue;
            }
            s->terms = size;
            for (i = 0; i < s->terms; i++) {
                uint8_t val = bytestream2_get_byte(&gb);
                s->decorr[s->terms - i - 1].value = (val & 0x1F) - 5;
                s->decorr[s->terms - i - 1].delta =  val >> 5;
            }
            got_terms = 1;
            break;
        case WP_ID_DECWEIGHTS:
            if (!got_terms) {
                av_log(avctx, AV_LOG_ERROR, "No decorrelation terms met\n");
                continue;
            }
            weights = size >> s->stereo_in;
            if (weights > MAX_TERMS || weights > s->terms) {
                av_log(avctx, AV_LOG_ERROR, "Too many decorrelation weights\n");
                bytestream2_skip(&gb, ssize);
                continue;
            }
            for (i = 0; i < weights; i++) {
                t = (int8_t)bytestream2_get_byte(&gb);
                s->decorr[s->terms - i - 1].weightA = t * (1 << 3);
                if (s->decorr[s->terms - i - 1].weightA > 0)
                    s->decorr[s->terms - i - 1].weightA +=
                        (s->decorr[s->terms - i - 1].weightA + 64) >> 7;
                if (s->stereo_in) {
                    t = (int8_t)bytestream2_get_byte(&gb);
                    s->decorr[s->terms - i - 1].weightB = t * (1 << 3);
                    if (s->decorr[s->terms - i - 1].weightB > 0)
                        s->decorr[s->terms - i - 1].weightB +=
                            (s->decorr[s->terms - i - 1].weightB + 64) >> 7;
                }
            }
            got_weights = 1;
            break;
        case WP_ID_DECSAMPLES:
            if (!got_terms) {
                av_log(avctx, AV_LOG_ERROR, "No decorrelation terms met\n");
                continue;
            }
            t = 0;
            for (i = s->terms - 1; (i >= 0) && (t < size); i--) {
                if (s->decorr[i].value > 8) {
                    s->decorr[i].samplesA[0] =
                        wp_exp2(bytestream2_get_le16(&gb));
                    s->decorr[i].samplesA[1] =
                        wp_exp2(bytestream2_get_le16(&gb));

                    if (s->stereo_in) {
                        s->decorr[i].samplesB[0] =
                            wp_exp2(bytestream2_get_le16(&gb));
                        s->decorr[i].samplesB[1] =
                            wp_exp2(bytestream2_get_le16(&gb));
                        t                       += 4;
                    }
                    t += 4;
                } else if (s->decorr[i].value < 0) {
                    s->decorr[i].samplesA[0] =
                        wp_exp2(bytestream2_get_le16(&gb));
                    s->decorr[i].samplesB[0] =
                        wp_exp2(bytestream2_get_le16(&gb));
                    t                       += 4;
                } else {
                    for (j = 0; j < s->decorr[i].value; j++) {
                        s->decorr[i].samplesA[j] =
                            wp_exp2(bytestream2_get_le16(&gb));
                        if (s->stereo_in) {
                            s->decorr[i].samplesB[j] =
                                wp_exp2(bytestream2_get_le16(&gb));
                        }
                    }
                    t += s->decorr[i].value * 2 * (s->stereo_in + 1);
                }
            }
            got_samples = 1;
            break;
        case WP_ID_ENTROPY:
            if (size != 6 * (s->stereo_in + 1)) {
                av_log(avctx, AV_LOG_ERROR,
                       "Entropy vars size should be %i, got %i.\n",
                       6 * (s->stereo_in + 1), size);
                bytestream2_skip(&gb, ssize);
                continue;
            }
            for (j = 0; j <= s->stereo_in; j++)
                for (i = 0; i < 3; i++) {
                    s->ch[j].median[i] = wp_exp2(bytestream2_get_le16(&gb));
                }
            got_entropy = 1;
            break;
        case WP_ID_HYBRID:
            if (s->hybrid_bitrate) {
                for (i = 0; i <= s->stereo_in; i++) {
                    s->ch[i].slow_level = wp_exp2(bytestream2_get_le16(&gb));
                    size               -= 2;
                }
            }
            for (i = 0; i < (s->stereo_in + 1); i++) {
                s->ch[i].bitrate_acc = bytestream2_get_le16(&gb) << 16;
                size                -= 2;
            }
            if (size > 0) {
                for (i = 0; i < (s->stereo_in + 1); i++) {
                    s->ch[i].bitrate_delta =
                        wp_exp2((int16_t)bytestream2_get_le16(&gb));
                }
            } else {
                for (i = 0; i < (s->stereo_in + 1); i++)
                    s->ch[i].bitrate_delta = 0;
            }
            got_hybrid = 1;
            break;
        case WP_ID_INT32INFO: {
            uint8_t val[4];
            if (size != 4) {
                av_log(avctx, AV_LOG_ERROR,
                       "Invalid INT32INFO, size = %i\n",
                       size);
                bytestream2_skip(&gb, ssize - 4);
                continue;
            }
            bytestream2_get_buffer(&gb, val, 4);
            if (val[0] > 30) {
                av_log(avctx, AV_LOG_ERROR,
                       "Invalid INT32INFO, extra_bits = %d (> 30)\n", val[0]);
                continue;
            } else {
                s->extra_bits = val[0];
            }
            if (val[1])
                s->shift = val[1];
            if (val[2]) {
                s->and   = s->or = 1;
                s->shift = val[2];
            }
            if (val[3]) {
                s->and   = 1;
                s->shift = val[3];
            }
            if (s->shift > 31) {
                av_log(avctx, AV_LOG_ERROR,
                       "Invalid INT32INFO, shift = %d (> 31)\n", s->shift);
                s->and = s->or = s->shift = 0;
                continue;
            }
            /* original WavPack decoder forces 32-bit lossy sound to be treated
             * as 24-bit one in order to have proper clipping */
            if (s->hybrid && bpp == 4 && s->post_shift < 8 && s->shift > 8) {
                s->post_shift      += 8;
                s->shift           -= 8;
                s->hybrid_maxclip >>= 8;
                s->hybrid_minclip >>= 8;
            }
            break;
        }
        case WP_ID_FLOATINFO:
            if (size != 4) {
                av_log(avctx, AV_LOG_ERROR,
                       "Invalid FLOATINFO, size = %i\n", size);
                bytestream2_skip(&gb, ssize);
                continue;
            }
            s->float_flag    = bytestream2_get_byte(&gb);
            s->float_shift   = bytestream2_get_byte(&gb);
            s->float_max_exp = bytestream2_get_byte(&gb);
            if (s->float_shift > 31) {
                av_log(avctx, AV_LOG_ERROR,
                       "Invalid FLOATINFO, shift = %d (> 31)\n", s->float_shift);
                s->float_shift = 0;
                continue;
            }
            got_float        = 1;
            bytestream2_skip(&gb, 1);
            break;
        case WP_ID_DATA:
            if ((ret = init_get_bits8(&s->gb, gb.buffer, size)) < 0)
                return ret;
            bytestream2_skip(&gb, size);
            got_pcm      = 1;
            break;
        case WP_ID_DSD_DATA:
            if (size < 2) {
                av_log(avctx, AV_LOG_ERROR, "Invalid DSD_DATA, size = %i\n",
                       size);
                bytestream2_skip(&gb, ssize);
                continue;
            }
            rate_x = bytestream2_get_byte(&gb);
            if (rate_x > 30)
                return AVERROR_INVALIDDATA;
            rate_x = 1 << rate_x;
            dsd_mode = bytestream2_get_byte(&gb);
            if (dsd_mode && dsd_mode != 1 && dsd_mode != 3) {
                av_log(avctx, AV_LOG_ERROR, "Invalid DSD encoding mode: %d\n",
                    dsd_mode);
                return AVERROR_INVALIDDATA;
            }
            bytestream2_init(&s->gbyte, gb.buffer, size-2);
            bytestream2_skip(&gb, size-2);
            got_dsd      = 1;
            break;
        case WP_ID_EXTRABITS:
            if (size <= 4) {
                av_log(avctx, AV_LOG_ERROR, "Invalid EXTRABITS, size = %i\n",
                       size);
                bytestream2_skip(&gb, size);
                continue;
            }
            if ((ret = init_get_bits8(&s->gb_extra_bits, gb.buffer, size)) < 0)
                return ret;
            s->crc_extra_bits  = get_bits_long(&s->gb_extra_bits, 32);
            bytestream2_skip(&gb, size);
            s->got_extra_bits  = 1;
            break;
        case WP_ID_CHANINFO:
            if (size <= 1) {
                av_log(avctx, AV_LOG_ERROR,
                       "Insufficient channel information\n");
                return AVERROR_INVALIDDATA;
            }
            chan = bytestream2_get_byte(&gb);
            switch (size - 2) {
            case 0:
                chmask = bytestream2_get_byte(&gb);
                break;
            case 1:
                chmask = bytestream2_get_le16(&gb);
                break;
            case 2:
                chmask = bytestream2_get_le24(&gb);
                break;
            case 3:
                chmask = bytestream2_get_le32(&gb);
                break;
            case 4:
                size = bytestream2_get_byte(&gb);
                chan  |= (bytestream2_get_byte(&gb) & 0xF) << 8;
                chan  += 1;
                if (avctx->ch_layout.nb_channels != chan)
                    av_log(avctx, AV_LOG_WARNING, "%i channels signalled"
                           " instead of %i.\n", chan, avctx->ch_layout.nb_channels);
                chmask = bytestream2_get_le24(&gb);
                break;
            case 5:
                size = bytestream2_get_byte(&gb);
                chan  |= (bytestream2_get_byte(&gb) & 0xF) << 8;
                chan  += 1;
                if (avctx->ch_layout.nb_channels != chan)
                    av_log(avctx, AV_LOG_WARNING, "%i channels signalled"
                           " instead of %i.\n", chan, avctx->ch_layout.nb_channels);
                chmask = bytestream2_get_le32(&gb);
                break;
            default:
                av_log(avctx, AV_LOG_ERROR, "Invalid channel info size %d\n",
                       size);
            }
            break;
        case WP_ID_SAMPLE_RATE:
            if (size != 3) {
                av_log(avctx, AV_LOG_ERROR, "Invalid custom sample rate.\n");
                return AVERROR_INVALIDDATA;
            }
            sample_rate = bytestream2_get_le24(&gb);
            break;
        default:
            bytestream2_skip(&gb, size);
        }
        if (id & WP_IDF_ODD)
            bytestream2_skip(&gb, 1);
    }

    if (got_pcm) {
        if (!got_terms) {
            av_log(avctx, AV_LOG_ERROR, "No block with decorrelation terms\n");
            return AVERROR_INVALIDDATA;
        }
        if (!got_weights) {
            av_log(avctx, AV_LOG_ERROR, "No block with decorrelation weights\n");
            return AVERROR_INVALIDDATA;
        }
        if (!got_samples) {
            av_log(avctx, AV_LOG_ERROR, "No block with decorrelation samples\n");
            return AVERROR_INVALIDDATA;
        }
        if (!got_entropy) {
            av_log(avctx, AV_LOG_ERROR, "No block with entropy info\n");
            return AVERROR_INVALIDDATA;
        }
        if (s->hybrid && !got_hybrid) {
            av_log(avctx, AV_LOG_ERROR, "Hybrid config not found\n");
            return AVERROR_INVALIDDATA;
        }
        if (!got_float && sample_fmt == AV_SAMPLE_FMT_FLTP) {
            av_log(avctx, AV_LOG_ERROR, "Float information not found\n");
            return AVERROR_INVALIDDATA;
        }
        if (s->got_extra_bits && sample_fmt != AV_SAMPLE_FMT_FLTP) {
            const int size   = get_bits_left(&s->gb_extra_bits);
            const int wanted = s->samples * s->extra_bits << s->stereo_in;
            if (size < wanted) {
                av_log(avctx, AV_LOG_ERROR, "Too small EXTRABITS\n");
                s->got_extra_bits = 0;
            }
        }
    }

    if (!got_pcm && !got_dsd) {
        av_log(avctx, AV_LOG_ERROR, "Packed samples not found\n");
        return AVERROR_INVALIDDATA;
    }

    if ((got_pcm && wc->modulation != MODULATION_PCM) ||
        (got_dsd && wc->modulation != MODULATION_DSD)) {
            av_log(avctx, AV_LOG_ERROR, "Invalid PCM/DSD mix encountered\n");
            return AVERROR_INVALIDDATA;
    }

    if (!wc->ch_offset) {
        AVChannelLayout new_ch_layout = { 0 };
        int new_samplerate;
        int sr = (s->frame_flags >> 23) & 0xf;
        if (sr == 0xf) {
            if (!sample_rate) {
                av_log(avctx, AV_LOG_ERROR, "Custom sample rate missing.\n");
                return AVERROR_INVALIDDATA;
            }
            new_samplerate = sample_rate;
        } else
            new_samplerate = wv_rates[sr];

        if (new_samplerate * (uint64_t)rate_x > INT_MAX)
            return AVERROR_INVALIDDATA;
        new_samplerate *= rate_x;

        if (multiblock) {
            if (chmask) {
                av_channel_layout_from_mask(&new_ch_layout, chmask);
                if (chan && new_ch_layout.nb_channels != chan) {
                    av_log(avctx, AV_LOG_ERROR, "Channel mask does not match the channel count\n");
                    return AVERROR_INVALIDDATA;
                }
            } else {
                ret = av_channel_layout_copy(&new_ch_layout, &avctx->ch_layout);
                if (ret < 0) {
                    av_log(avctx, AV_LOG_ERROR, "Error copying channel layout\n");
                    return ret;
                }
            }
        } else {
            av_channel_layout_default(&new_ch_layout, s->stereo + 1);
        }

        /* clear DSD state if stream properties change */
        if (new_ch_layout.nb_channels != wc->dsd_channels ||
            av_channel_layout_compare(&new_ch_layout, &avctx->ch_layout) ||
            new_samplerate != avctx->sample_rate    ||
            !!got_dsd      != !!wc->dsdctx) {
            ret = wv_dsd_reset(wc, got_dsd ? new_ch_layout.nb_channels : 0);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "Error reinitializing the DSD context\n");
                return ret;
            }
            ff_thread_release_ext_buffer(avctx, &wc->curr_frame);
        }
        av_channel_layout_uninit(&avctx->ch_layout);
        av_channel_layout_copy(&avctx->ch_layout, &new_ch_layout);
        avctx->sample_rate         = new_samplerate;
        avctx->sample_fmt          = sample_fmt;
        avctx->bits_per_raw_sample = orig_bpp;

        ff_thread_release_ext_buffer(avctx, &wc->prev_frame);
        FFSWAP(ThreadFrame, wc->curr_frame, wc->prev_frame);

        /* get output buffer */
        wc->curr_frame.f->nb_samples = s->samples;
        ret = ff_thread_get_ext_buffer(avctx, &wc->curr_frame,
                                       AV_GET_BUFFER_FLAG_REF);
        if (ret < 0)
            return ret;

        wc->frame = wc->curr_frame.f;
        ff_thread_finish_setup(avctx);
    }

    if (wc->ch_offset + s->stereo >= avctx->ch_layout.nb_channels) {
        av_log(avctx, AV_LOG_WARNING, "Too many channels coded in a packet.\n");
        return ((avctx->err_recognition & AV_EF_EXPLODE) || !wc->ch_offset) ? AVERROR_INVALIDDATA : 0;
    }

    samples_l = wc->frame->extended_data[wc->ch_offset];
    if (s->stereo)
        samples_r = wc->frame->extended_data[wc->ch_offset + 1];

    wc->ch_offset += 1 + s->stereo;

    if (s->stereo_in) {
        if (got_dsd) {
            if (dsd_mode == 3) {
                ret = wv_unpack_dsd_high(s, samples_l, samples_r);
            } else if (dsd_mode == 1) {
                ret = wv_unpack_dsd_fast(s, samples_l, samples_r);
            } else {
                ret = wv_unpack_dsd_copy(s, samples_l, samples_r);
            }
        } else {
            ret = wv_unpack_stereo(s, &s->gb, samples_l, samples_r, avctx->sample_fmt);
        }
        if (ret < 0)
            return ret;
    } else {
        if (got_dsd) {
            if (dsd_mode == 3) {
                ret = wv_unpack_dsd_high(s, samples_l, NULL);
            } else if (dsd_mode == 1) {
                ret = wv_unpack_dsd_fast(s, samples_l, NULL);
            } else {
                ret = wv_unpack_dsd_copy(s, samples_l, NULL);
            }
        } else {
            ret = wv_unpack_mono(s, &s->gb, samples_l, avctx->sample_fmt);
        }
        if (ret < 0)
            return ret;

        if (s->stereo)
            memcpy(samples_r, samples_l, bpp * s->samples);
    }

    return 0;
}

static void wavpack_decode_flush(AVCodecContext *avctx)
{
    WavpackContext *s = avctx->priv_data;

    wv_dsd_reset(s, 0);
}

static int dsd_channel(AVCodecContext *avctx, void *frmptr, int jobnr, int threadnr)
{
    WavpackContext *s  = avctx->priv_data;
    AVFrame *frame = frmptr;

    ff_dsd2pcm_translate (&s->dsdctx [jobnr], s->samples, 0,
        (uint8_t *)frame->extended_data[jobnr], 4,
        (float *)frame->extended_data[jobnr], 1);

    return 0;
}

static int wavpack_decode_frame(AVCodecContext *avctx, AVFrame *rframe,
                                int *got_frame_ptr, AVPacket *avpkt)
{
    WavpackContext *s  = avctx->priv_data;
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    int frame_size, ret, frame_flags;

    if (avpkt->size <= WV_HEADER_SIZE)
        return AVERROR_INVALIDDATA;

    s->frame     = NULL;
    s->block     = 0;
    s->ch_offset = 0;

    /* determine number of samples */
    s->samples  = AV_RL32(buf + 20);
    frame_flags = AV_RL32(buf + 24);
    if (s->samples <= 0 || s->samples > WV_MAX_SAMPLES) {
        av_log(avctx, AV_LOG_ERROR, "Invalid number of samples: %d\n",
               s->samples);
        return AVERROR_INVALIDDATA;
    }

    s->modulation = (frame_flags & WV_DSD_DATA) ? MODULATION_DSD : MODULATION_PCM;

    while (buf_size > WV_HEADER_SIZE) {
        frame_size = AV_RL32(buf + 4) - 12;
        buf       += 20;
        buf_size  -= 20;
        if (frame_size <= 0 || frame_size > buf_size) {
            av_log(avctx, AV_LOG_ERROR,
                   "Block %d has invalid size (size %d vs. %d bytes left)\n",
                   s->block, frame_size, buf_size);
            ret = AVERROR_INVALIDDATA;
            goto error;
        }
        if ((ret = wavpack_decode_block(avctx, s->block, buf, frame_size)) < 0)
            goto error;
        s->block++;
        buf      += frame_size;
        buf_size -= frame_size;
    }

    if (s->ch_offset != avctx->ch_layout.nb_channels) {
        av_log(avctx, AV_LOG_ERROR, "Not enough channels coded in a packet.\n");
        ret = AVERROR_INVALIDDATA;
        goto error;
    }

    ff_thread_await_progress(&s->prev_frame, INT_MAX, 0);
    ff_thread_release_ext_buffer(avctx, &s->prev_frame);

    if (s->modulation == MODULATION_DSD)
        avctx->execute2(avctx, dsd_channel, s->frame, NULL, avctx->ch_layout.nb_channels);

    ff_thread_report_progress(&s->curr_frame, INT_MAX, 0);

    if ((ret = av_frame_ref(rframe, s->frame)) < 0)
        return ret;

    *got_frame_ptr = 1;

    return avpkt->size;

error:
    if (s->frame) {
        ff_thread_await_progress(&s->prev_frame, INT_MAX, 0);
        ff_thread_release_ext_buffer(avctx, &s->prev_frame);
        ff_thread_report_progress(&s->curr_frame, INT_MAX, 0);
    }

    return ret;
}

const FFCodec ff_wavpack_decoder = {
    .p.name         = "wavpack",
    .p.long_name    = NULL_IF_CONFIG_SMALL("WavPack"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_WAVPACK,
    .priv_data_size = sizeof(WavpackContext),
    .init           = wavpack_decode_init,
    .close          = wavpack_decode_end,
    FF_CODEC_DECODE_CB(wavpack_decode_frame),
    .flush          = wavpack_decode_flush,
    .update_thread_context = ONLY_IF_THREADS_ENABLED(update_thread_context),
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS |
                      AV_CODEC_CAP_SLICE_THREADS | AV_CODEC_CAP_CHANNEL_CONF,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP |
                      FF_CODEC_CAP_ALLOCATE_PROGRESS,
};
