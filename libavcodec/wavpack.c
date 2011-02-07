/*
 * WavPack lossless audio decoder
 * Copyright (c) 2006,2011 Konstantin Shishkov
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
#define ALT_BITSTREAM_READER_LE
#include "avcodec.h"
#include "get_bits.h"
#include "unary.h"
#include "libavutil/audioconvert.h"

/**
 * @file
 * WavPack lossless audio decoder
 */

#define WV_MONO         0x00000004
#define WV_JOINT_STEREO 0x00000010
#define WV_FALSE_STEREO 0x40000000

#define WV_HYBRID_MODE    0x00000008
#define WV_HYBRID_SHAPE   0x00000008
#define WV_HYBRID_BITRATE 0x00000200
#define WV_HYBRID_BALANCE 0x00000400

#define WV_FLT_SHIFT_ONES 0x01
#define WV_FLT_SHIFT_SAME 0x02
#define WV_FLT_SHIFT_SENT 0x04
#define WV_FLT_ZERO_SENT  0x08
#define WV_FLT_ZERO_SIGN  0x10

enum WP_ID_Flags{
    WP_IDF_MASK   = 0x1F,
    WP_IDF_IGNORE = 0x20,
    WP_IDF_ODD    = 0x40,
    WP_IDF_LONG   = 0x80
};

enum WP_ID{
    WP_ID_DUMMY = 0,
    WP_ID_ENCINFO,
    WP_ID_DECTERMS,
    WP_ID_DECWEIGHTS,
    WP_ID_DECSAMPLES,
    WP_ID_ENTROPY,
    WP_ID_HYBRID,
    WP_ID_SHAPING,
    WP_ID_FLOATINFO,
    WP_ID_INT32INFO,
    WP_ID_DATA,
    WP_ID_CORR,
    WP_ID_EXTRABITS,
    WP_ID_CHANINFO
};

typedef struct SavedContext {
    int offset;
    int size;
    int bits_used;
    uint32_t crc;
} SavedContext;

#define MAX_TERMS 16

typedef struct Decorr {
    int delta;
    int value;
    int weightA;
    int weightB;
    int samplesA[8];
    int samplesB[8];
} Decorr;

typedef struct WvChannel {
    int median[3];
    int slow_level, error_limit;
    int bitrate_acc, bitrate_delta;
} WvChannel;

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
    int data_size; // in bits
    int samples;
    int terms;
    Decorr decorr[MAX_TERMS];
    int zero, one, zeroes;
    int extra_bits;
    int and, or, shift;
    int post_shift;
    int hybrid, hybrid_bitrate;
    int float_flag;
    int float_shift;
    int float_max_exp;
    WvChannel ch[2];
    int samples_left;
    int max_samples;
    int pos;
    SavedContext sc, extra_sc;
} WavpackFrameContext;

#define WV_MAX_FRAME_DECODERS 14

typedef struct WavpackContext {
    AVCodecContext *avctx;

    WavpackFrameContext *fdec[WV_MAX_FRAME_DECODERS];
    int fdec_num;

    int multichannel;
    int mkv_mode;
    int block;
    int samples;
    int samples_left;
    int ch_offset;
} WavpackContext;

// exponent table copied from WavPack source
static const uint8_t wp_exp2_table [256] = {
    0x00, 0x01, 0x01, 0x02, 0x03, 0x03, 0x04, 0x05, 0x06, 0x06, 0x07, 0x08, 0x08, 0x09, 0x0a, 0x0b,
    0x0b, 0x0c, 0x0d, 0x0e, 0x0e, 0x0f, 0x10, 0x10, 0x11, 0x12, 0x13, 0x13, 0x14, 0x15, 0x16, 0x16,
    0x17, 0x18, 0x19, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1d, 0x1e, 0x1f, 0x20, 0x20, 0x21, 0x22, 0x23,
    0x24, 0x24, 0x25, 0x26, 0x27, 0x28, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2c, 0x2d, 0x2e, 0x2f, 0x30,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3a, 0x3b, 0x3c, 0x3d,
    0x3e, 0x3f, 0x40, 0x41, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x48, 0x49, 0x4a, 0x4b,
    0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a,
    0x5b, 0x5c, 0x5d, 0x5e, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
    0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x87, 0x88, 0x89, 0x8a,
    0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b,
    0x9c, 0x9d, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad,
    0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0,
    0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc8, 0xc9, 0xca, 0xcb, 0xcd, 0xce, 0xcf, 0xd0, 0xd2, 0xd3, 0xd4,
    0xd6, 0xd7, 0xd8, 0xd9, 0xdb, 0xdc, 0xdd, 0xde, 0xe0, 0xe1, 0xe2, 0xe4, 0xe5, 0xe6, 0xe8, 0xe9,
    0xea, 0xec, 0xed, 0xee, 0xf0, 0xf1, 0xf2, 0xf4, 0xf5, 0xf6, 0xf8, 0xf9, 0xfa, 0xfc, 0xfd, 0xff
};

static const uint8_t wp_log2_table [] = {
    0x00, 0x01, 0x03, 0x04, 0x06, 0x07, 0x09, 0x0a, 0x0b, 0x0d, 0x0e, 0x10, 0x11, 0x12, 0x14, 0x15,
    0x16, 0x18, 0x19, 0x1a, 0x1c, 0x1d, 0x1e, 0x20, 0x21, 0x22, 0x24, 0x25, 0x26, 0x28, 0x29, 0x2a,
    0x2c, 0x2d, 0x2e, 0x2f, 0x31, 0x32, 0x33, 0x34, 0x36, 0x37, 0x38, 0x39, 0x3b, 0x3c, 0x3d, 0x3e,
    0x3f, 0x41, 0x42, 0x43, 0x44, 0x45, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4d, 0x4e, 0x4f, 0x50, 0x51,
    0x52, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5c, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63,
    0x64, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x74, 0x75,
    0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85,
    0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95,
    0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4,
    0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb2,
    0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0, 0xc0,
    0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcb, 0xcc, 0xcd, 0xce,
    0xcf, 0xd0, 0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd8, 0xd9, 0xda, 0xdb,
    0xdc, 0xdc, 0xdd, 0xde, 0xdf, 0xe0, 0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe4, 0xe5, 0xe6, 0xe7, 0xe7,
    0xe8, 0xe9, 0xea, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xee, 0xef, 0xf0, 0xf1, 0xf1, 0xf2, 0xf3, 0xf4,
    0xf4, 0xf5, 0xf6, 0xf7, 0xf7, 0xf8, 0xf9, 0xf9, 0xfa, 0xfb, 0xfc, 0xfc, 0xfd, 0xfe, 0xff, 0xff
};

static av_always_inline int wp_exp2(int16_t val)
{
    int res, neg = 0;

    if(val < 0){
        val = -val;
        neg = 1;
    }

    res = wp_exp2_table[val & 0xFF] | 0x100;
    val >>= 8;
    res = (val > 9) ? (res << (val - 9)) : (res >> (9 - val));
    return neg ? -res : res;
}

static av_always_inline int wp_log2(int32_t val)
{
    int bits;

    if(!val)
        return 0;
    if(val == 1)
        return 256;
    val += val >> 9;
    bits = av_log2(val) + 1;
    if(bits < 9)
        return (bits << 8) + wp_log2_table[(val << (9 - bits)) & 0xFF];
    else
        return (bits << 8) + wp_log2_table[(val >> (bits - 9)) & 0xFF];
}

#define LEVEL_DECAY(a)  ((a + 0x80) >> 8)

// macros for manipulating median values
#define GET_MED(n) ((c->median[n] >> 4) + 1)
#define DEC_MED(n) c->median[n] -= ((c->median[n] + (128>>n) - 2) / (128>>n)) * 2
#define INC_MED(n) c->median[n] += ((c->median[n] + (128>>n)) / (128>>n)) * 5

// macros for applying weight
#define UPDATE_WEIGHT_CLIP(weight, delta, samples, in) \
        if(samples && in){ \
            if((samples ^ in) < 0){ \
                weight -= delta; \
                if(weight < -1024) weight = -1024; \
            }else{ \
                weight += delta; \
                if(weight > 1024) weight = 1024; \
            } \
        }


static av_always_inline int get_tail(GetBitContext *gb, int k)
{
    int p, e, res;

    if(k<1)return 0;
    p = av_log2(k);
    e = (1 << (p + 1)) - k - 1;
    res = p ? get_bits(gb, p) : 0;
    if(res >= e){
        res = (res<<1) - e + get_bits1(gb);
    }
    return res;
}

static void update_error_limit(WavpackFrameContext *ctx)
{
    int i, br[2], sl[2];

    for(i = 0; i <= ctx->stereo_in; i++){
        ctx->ch[i].bitrate_acc += ctx->ch[i].bitrate_delta;
        br[i] = ctx->ch[i].bitrate_acc >> 16;
        sl[i] = LEVEL_DECAY(ctx->ch[i].slow_level);
    }
    if(ctx->stereo_in && ctx->hybrid_bitrate){
        int balance = (sl[1] - sl[0] + br[1] + 1) >> 1;
        if(balance > br[0]){
            br[1] = br[0] << 1;
            br[0] = 0;
        }else if(-balance > br[0]){
            br[0] <<= 1;
            br[1] = 0;
        }else{
            br[1] = br[0] + balance;
            br[0] = br[0] - balance;
        }
    }
    for(i = 0; i <= ctx->stereo_in; i++){
        if(ctx->hybrid_bitrate){
            if(sl[i] - br[i] > -0x100)
                ctx->ch[i].error_limit = wp_exp2(sl[i] - br[i] + 0x100);
            else
                ctx->ch[i].error_limit = 0;
        }else{
            ctx->ch[i].error_limit = wp_exp2(br[i]);
        }
    }
}

static int wv_get_value(WavpackFrameContext *ctx, GetBitContext *gb, int channel, int *last)
{
    int t, t2;
    int sign, base, add, ret;
    WvChannel *c = &ctx->ch[channel];

    *last = 0;

    if((ctx->ch[0].median[0] < 2U) && (ctx->ch[1].median[0] < 2U) && !ctx->zero && !ctx->one){
        if(ctx->zeroes){
            ctx->zeroes--;
            if(ctx->zeroes){
                c->slow_level -= LEVEL_DECAY(c->slow_level);
                return 0;
            }
        }else{
            t = get_unary_0_33(gb);
            if(t >= 2) t = get_bits(gb, t - 1) | (1 << (t-1));
            ctx->zeroes = t;
            if(ctx->zeroes){
                memset(ctx->ch[0].median, 0, sizeof(ctx->ch[0].median));
                memset(ctx->ch[1].median, 0, sizeof(ctx->ch[1].median));
                c->slow_level -= LEVEL_DECAY(c->slow_level);
                return 0;
            }
        }
    }

    if(get_bits_count(gb) >= ctx->data_size){
        *last = 1;
        return 0;
    }

    if(ctx->zero){
        t = 0;
        ctx->zero = 0;
    }else{
        t = get_unary_0_33(gb);
        if(get_bits_count(gb) >= ctx->data_size){
            *last = 1;
            return 0;
        }
        if(t == 16) {
            t2 = get_unary_0_33(gb);
            if(t2 < 2) t += t2;
            else t += get_bits(gb, t2 - 1) | (1 << (t2 - 1));
        }

        if(ctx->one){
            ctx->one = t&1;
            t = (t>>1) + 1;
        }else{
            ctx->one = t&1;
            t >>= 1;
        }
        ctx->zero = !ctx->one;
    }

    if(ctx->hybrid && !channel)
        update_error_limit(ctx);

    if(!t){
        base = 0;
        add = GET_MED(0) - 1;
        DEC_MED(0);
    }else if(t == 1){
        base = GET_MED(0);
        add = GET_MED(1) - 1;
        INC_MED(0);
        DEC_MED(1);
    }else if(t == 2){
        base = GET_MED(0) + GET_MED(1);
        add = GET_MED(2) - 1;
        INC_MED(0);
        INC_MED(1);
        DEC_MED(2);
    }else{
        base = GET_MED(0) + GET_MED(1) + GET_MED(2) * (t - 2);
        add = GET_MED(2) - 1;
        INC_MED(0);
        INC_MED(1);
        INC_MED(2);
    }
    if(!c->error_limit){
        ret = base + get_tail(gb, add);
    }else{
        int mid = (base*2 + add + 1) >> 1;
        while(add > c->error_limit){
            if(get_bits1(gb)){
                add -= (mid - base);
                base = mid;
            }else
                add = mid - base - 1;
            mid = (base*2 + add + 1) >> 1;
        }
        ret = mid;
    }
    sign = get_bits1(gb);
    if(ctx->hybrid_bitrate)
        c->slow_level += wp_log2(ret) - LEVEL_DECAY(c->slow_level);
    return sign ? ~ret : ret;
}

static inline int wv_get_value_integer(WavpackFrameContext *s, uint32_t *crc, int S)
{
    int bit;

    if(s->extra_bits){
        S <<= s->extra_bits;

        if(s->got_extra_bits){
            S |= get_bits(&s->gb_extra_bits, s->extra_bits);
            *crc = *crc * 9 + (S&0xffff) * 3 + ((unsigned)S>>16);
        }
    }
    bit = (S & s->and) | s->or;
    return (((S + bit) << s->shift) - bit) << s->post_shift;
}

static float wv_get_value_float(WavpackFrameContext *s, uint32_t *crc, int S)
{
    union {
        float    f;
        uint32_t u;
    } value;

    int sign;
    int exp = s->float_max_exp;

    if(s->got_extra_bits){
        const int max_bits = 1 + 23 + 8 + 1;
        const int left_bits = get_bits_left(&s->gb_extra_bits);

        if(left_bits + 8 * FF_INPUT_BUFFER_PADDING_SIZE < max_bits)
            return 0.0;
    }

    if(S){
        S <<= s->float_shift;
        sign = S < 0;
        if(sign)
            S = -S;
        if(S >= 0x1000000){
            if(s->got_extra_bits && get_bits1(&s->gb_extra_bits)){
                S = get_bits(&s->gb_extra_bits, 23);
            }else{
                S = 0;
            }
            exp = 255;
        }else if(exp){
            int shift = 23 - av_log2(S);
            exp = s->float_max_exp;
            if(exp <= shift){
                shift = --exp;
            }
            exp -= shift;

            if(shift){
                S <<= shift;
                if((s->float_flag & WV_FLT_SHIFT_ONES) ||
                   (s->got_extra_bits && (s->float_flag & WV_FLT_SHIFT_SAME) && get_bits1(&s->gb_extra_bits)) ){
                    S |= (1 << shift) - 1;
                } else if(s->got_extra_bits && (s->float_flag & WV_FLT_SHIFT_SENT)){
                    S |= get_bits(&s->gb_extra_bits, shift);
                }
            }
        }else{
            exp = s->float_max_exp;
        }
        S &= 0x7fffff;
    }else{
        sign = 0;
        exp = 0;
        if(s->got_extra_bits && (s->float_flag & WV_FLT_ZERO_SENT)){
            if(get_bits1(&s->gb_extra_bits)){
                S = get_bits(&s->gb_extra_bits, 23);
                if(s->float_max_exp >= 25)
                    exp = get_bits(&s->gb_extra_bits, 8);
                sign = get_bits1(&s->gb_extra_bits);
            }else{
                if(s->float_flag & WV_FLT_ZERO_SIGN)
                    sign = get_bits1(&s->gb_extra_bits);
            }
        }
    }

    *crc = *crc * 27 + S * 9 + exp * 3 + sign;

    value.u = (sign << 31) | (exp << 23) | S;
    return value.f;
}

static void wv_reset_saved_context(WavpackFrameContext *s)
{
    s->pos = 0;
    s->sc.crc = s->extra_sc.crc = 0xFFFFFFFF;
}

static inline int wv_unpack_stereo(WavpackFrameContext *s, GetBitContext *gb, void *dst, const int type)
{
    int i, j, count = 0;
    int last, t;
    int A, B, L, L2, R, R2;
    int pos = s->pos;
    uint32_t crc = s->sc.crc;
    uint32_t crc_extra_bits = s->extra_sc.crc;
    int16_t *dst16 = dst;
    int32_t *dst32 = dst;
    float   *dstfl = dst;
    const int channel_pad = s->avctx->channels - 2;

    if(s->samples_left == s->samples)
        s->one = s->zero = s->zeroes = 0;
    do{
        L = wv_get_value(s, gb, 0, &last);
        if(last) break;
        R = wv_get_value(s, gb, 1, &last);
        if(last) break;
        for(i = 0; i < s->terms; i++){
            t = s->decorr[i].value;
            if(t > 0){
                if(t > 8){
                    if(t & 1){
                        A = 2 * s->decorr[i].samplesA[0] - s->decorr[i].samplesA[1];
                        B = 2 * s->decorr[i].samplesB[0] - s->decorr[i].samplesB[1];
                    }else{
                        A = (3 * s->decorr[i].samplesA[0] - s->decorr[i].samplesA[1]) >> 1;
                        B = (3 * s->decorr[i].samplesB[0] - s->decorr[i].samplesB[1]) >> 1;
                    }
                    s->decorr[i].samplesA[1] = s->decorr[i].samplesA[0];
                    s->decorr[i].samplesB[1] = s->decorr[i].samplesB[0];
                    j = 0;
                }else{
                    A = s->decorr[i].samplesA[pos];
                    B = s->decorr[i].samplesB[pos];
                    j = (pos + t) & 7;
                }
                if(type != AV_SAMPLE_FMT_S16){
                    L2 = L + ((s->decorr[i].weightA * (int64_t)A + 512) >> 10);
                    R2 = R + ((s->decorr[i].weightB * (int64_t)B + 512) >> 10);
                }else{
                    L2 = L + ((s->decorr[i].weightA * A + 512) >> 10);
                    R2 = R + ((s->decorr[i].weightB * B + 512) >> 10);
                }
                if(A && L) s->decorr[i].weightA -= ((((L ^ A) >> 30) & 2) - 1) * s->decorr[i].delta;
                if(B && R) s->decorr[i].weightB -= ((((R ^ B) >> 30) & 2) - 1) * s->decorr[i].delta;
                s->decorr[i].samplesA[j] = L = L2;
                s->decorr[i].samplesB[j] = R = R2;
            }else if(t == -1){
                if(type != AV_SAMPLE_FMT_S16)
                    L2 = L + ((s->decorr[i].weightA * (int64_t)s->decorr[i].samplesA[0] + 512) >> 10);
                else
                    L2 = L + ((s->decorr[i].weightA * s->decorr[i].samplesA[0] + 512) >> 10);
                UPDATE_WEIGHT_CLIP(s->decorr[i].weightA, s->decorr[i].delta, s->decorr[i].samplesA[0], L);
                L = L2;
                if(type != AV_SAMPLE_FMT_S16)
                    R2 = R + ((s->decorr[i].weightB * (int64_t)L2 + 512) >> 10);
                else
                    R2 = R + ((s->decorr[i].weightB * L2 + 512) >> 10);
                UPDATE_WEIGHT_CLIP(s->decorr[i].weightB, s->decorr[i].delta, L2, R);
                R = R2;
                s->decorr[i].samplesA[0] = R;
            }else{
                if(type != AV_SAMPLE_FMT_S16)
                    R2 = R + ((s->decorr[i].weightB * (int64_t)s->decorr[i].samplesB[0] + 512) >> 10);
                else
                    R2 = R + ((s->decorr[i].weightB * s->decorr[i].samplesB[0] + 512) >> 10);
                UPDATE_WEIGHT_CLIP(s->decorr[i].weightB, s->decorr[i].delta, s->decorr[i].samplesB[0], R);
                R = R2;

                if(t == -3){
                    R2 = s->decorr[i].samplesA[0];
                    s->decorr[i].samplesA[0] = R;
                }

                if(type != AV_SAMPLE_FMT_S16)
                    L2 = L + ((s->decorr[i].weightA * (int64_t)R2 + 512) >> 10);
                else
                    L2 = L + ((s->decorr[i].weightA * R2 + 512) >> 10);
                UPDATE_WEIGHT_CLIP(s->decorr[i].weightA, s->decorr[i].delta, R2, L);
                L = L2;
                s->decorr[i].samplesB[0] = L;
            }
        }
        pos = (pos + 1) & 7;
        if(s->joint)
            L += (R -= (L >> 1));
        crc = (crc * 3 + L) * 3 + R;

        if(type == AV_SAMPLE_FMT_FLT){
            *dstfl++ = wv_get_value_float(s, &crc_extra_bits, L);
            *dstfl++ = wv_get_value_float(s, &crc_extra_bits, R);
            dstfl += channel_pad;
        } else if(type == AV_SAMPLE_FMT_S32){
            *dst32++ = wv_get_value_integer(s, &crc_extra_bits, L);
            *dst32++ = wv_get_value_integer(s, &crc_extra_bits, R);
            dst32 += channel_pad;
        } else {
            *dst16++ = wv_get_value_integer(s, &crc_extra_bits, L);
            *dst16++ = wv_get_value_integer(s, &crc_extra_bits, R);
            dst16 += channel_pad;
        }
        count++;
    }while(!last && count < s->max_samples);

    s->samples_left -= count;
    if(!s->samples_left){
        if(crc != s->CRC){
            av_log(s->avctx, AV_LOG_ERROR, "CRC error\n");
            return -1;
        }
        if(s->got_extra_bits && crc_extra_bits != s->crc_extra_bits){
            av_log(s->avctx, AV_LOG_ERROR, "Extra bits CRC error\n");
            return -1;
        }
        wv_reset_saved_context(s);
    }else{
        s->pos = pos;
        s->sc.crc = crc;
        s->sc.bits_used = get_bits_count(&s->gb);
        if(s->got_extra_bits){
            s->extra_sc.crc = crc_extra_bits;
            s->extra_sc.bits_used = get_bits_count(&s->gb_extra_bits);
        }
    }
    return count * 2;
}

static inline int wv_unpack_mono(WavpackFrameContext *s, GetBitContext *gb, void *dst, const int type)
{
    int i, j, count = 0;
    int last, t;
    int A, S, T;
    int pos = s->pos;
    uint32_t crc = s->sc.crc;
    uint32_t crc_extra_bits = s->extra_sc.crc;
    int16_t *dst16 = dst;
    int32_t *dst32 = dst;
    float   *dstfl = dst;
    const int channel_stride = s->avctx->channels;

    if(s->samples_left == s->samples)
        s->one = s->zero = s->zeroes = 0;
    do{
        T = wv_get_value(s, gb, 0, &last);
        S = 0;
        if(last) break;
        for(i = 0; i < s->terms; i++){
            t = s->decorr[i].value;
            if(t > 8){
                if(t & 1)
                    A = 2 * s->decorr[i].samplesA[0] - s->decorr[i].samplesA[1];
                else
                    A = (3 * s->decorr[i].samplesA[0] - s->decorr[i].samplesA[1]) >> 1;
                s->decorr[i].samplesA[1] = s->decorr[i].samplesA[0];
                j = 0;
            }else{
                A = s->decorr[i].samplesA[pos];
                j = (pos + t) & 7;
            }
            if(type != AV_SAMPLE_FMT_S16)
                S = T + ((s->decorr[i].weightA * (int64_t)A + 512) >> 10);
            else
                S = T + ((s->decorr[i].weightA * A + 512) >> 10);
            if(A && T) s->decorr[i].weightA -= ((((T ^ A) >> 30) & 2) - 1) * s->decorr[i].delta;
            s->decorr[i].samplesA[j] = T = S;
        }
        pos = (pos + 1) & 7;
        crc = crc * 3 + S;

        if(type == AV_SAMPLE_FMT_FLT){
            *dstfl = wv_get_value_float(s, &crc_extra_bits, S);
            dstfl += channel_stride;
        }else if(type == AV_SAMPLE_FMT_S32){
            *dst32 = wv_get_value_integer(s, &crc_extra_bits, S);
            dst32 += channel_stride;
        }else{
            *dst16 = wv_get_value_integer(s, &crc_extra_bits, S);
            dst16 += channel_stride;
        }
        count++;
    }while(!last && count < s->max_samples);

    s->samples_left -= count;
    if(!s->samples_left){
        if(crc != s->CRC){
            av_log(s->avctx, AV_LOG_ERROR, "CRC error\n");
            return -1;
        }
        if(s->got_extra_bits && crc_extra_bits != s->crc_extra_bits){
            av_log(s->avctx, AV_LOG_ERROR, "Extra bits CRC error\n");
            return -1;
        }
        wv_reset_saved_context(s);
    }else{
        s->pos = pos;
        s->sc.crc = crc;
        s->sc.bits_used = get_bits_count(&s->gb);
        if(s->got_extra_bits){
            s->extra_sc.crc = crc_extra_bits;
            s->extra_sc.bits_used = get_bits_count(&s->gb_extra_bits);
        }
    }
    return count;
}

static av_cold int wv_alloc_frame_context(WavpackContext *c)
{

    if(c->fdec_num == WV_MAX_FRAME_DECODERS)
        return -1;

    c->fdec[c->fdec_num] = av_mallocz(sizeof(**c->fdec));
    if(!c->fdec[c->fdec_num])
        return -1;
    c->fdec_num++;
    c->fdec[c->fdec_num - 1]->avctx = c->avctx;
    wv_reset_saved_context(c->fdec[c->fdec_num - 1]);

    return 0;
}

static av_cold int wavpack_decode_init(AVCodecContext *avctx)
{
    WavpackContext *s = avctx->priv_data;

    s->avctx = avctx;
    if(avctx->bits_per_coded_sample <= 16)
        avctx->sample_fmt = AV_SAMPLE_FMT_S16;
    else
        avctx->sample_fmt = AV_SAMPLE_FMT_S32;
    if(avctx->channels <= 2 && !avctx->channel_layout)
        avctx->channel_layout = (avctx->channels==2) ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO;

    s->multichannel = avctx->channels > 2;
    /* lavf demuxer does not provide extradata, Matroska stores 0x403
       there, use this to detect decoding mode for multichannel */
    s->mkv_mode = 0;
    if(s->multichannel && avctx->extradata && avctx->extradata_size == 2){
        int ver = AV_RL16(avctx->extradata);
        if(ver >= 0x402 && ver <= 0x410)
            s->mkv_mode = 1;
    }

    s->fdec_num = 0;

    return 0;
}

static av_cold int wavpack_decode_end(AVCodecContext *avctx)
{
    WavpackContext *s = avctx->priv_data;
    int i;

    for(i = 0; i < s->fdec_num; i++)
        av_freep(&s->fdec[i]);
    s->fdec_num = 0;

    return 0;
}

static int wavpack_decode_block(AVCodecContext *avctx, int block_no,
                                void *data, int *data_size,
                                const uint8_t *buf, int buf_size)
{
    WavpackContext *wc = avctx->priv_data;
    WavpackFrameContext *s;
    void *samples = data;
    int samplecount;
    int got_terms = 0, got_weights = 0, got_samples = 0, got_entropy = 0, got_bs = 0, got_float = 0;
    int got_hybrid = 0;
    const uint8_t* orig_buf = buf;
    const uint8_t* buf_end = buf + buf_size;
    int i, j, id, size, ssize, weights, t;
    int bpp, chan, chmask;

    if (buf_size == 0){
        *data_size = 0;
        return 0;
    }

    if(block_no >= wc->fdec_num && wv_alloc_frame_context(wc) < 0){
        av_log(avctx, AV_LOG_ERROR, "Error creating frame decode context\n");
        return -1;
    }

    s = wc->fdec[block_no];
    if(!s){
        av_log(avctx, AV_LOG_ERROR, "Context for block %d is not present\n", block_no);
        return -1;
    }

    if(!s->samples_left){
        memset(s->decorr, 0, MAX_TERMS * sizeof(Decorr));
        memset(s->ch, 0, sizeof(s->ch));
        s->extra_bits = 0;
        s->and = s->or = s->shift = 0;
        s->got_extra_bits = 0;
    }

    if(!wc->mkv_mode){
        s->samples = AV_RL32(buf); buf += 4;
        if(!s->samples){
            *data_size = 0;
            return buf_size;
        }
    }else{
        s->samples = wc->samples;
    }
    s->frame_flags = AV_RL32(buf); buf += 4;
    if(s->frame_flags&0x80){
        bpp = sizeof(float);
        avctx->sample_fmt = AV_SAMPLE_FMT_FLT;
    } else if((s->frame_flags&0x03) <= 1){
        bpp = 2;
        avctx->sample_fmt = AV_SAMPLE_FMT_S16;
    } else {
        bpp = 4;
        avctx->sample_fmt = AV_SAMPLE_FMT_S32;
    }
    samples = (uint8_t*)samples + bpp * wc->ch_offset;

    s->stereo = !(s->frame_flags & WV_MONO);
    s->stereo_in = (s->frame_flags & WV_FALSE_STEREO) ? 0 : s->stereo;
    s->joint = s->frame_flags & WV_JOINT_STEREO;
    s->hybrid = s->frame_flags & WV_HYBRID_MODE;
    s->hybrid_bitrate = s->frame_flags & WV_HYBRID_BITRATE;
    s->post_shift = 8 * (bpp-1-(s->frame_flags&0x03)) + ((s->frame_flags >> 13) & 0x1f);
    s->CRC = AV_RL32(buf); buf += 4;
    if(wc->mkv_mode)
        buf += 4; //skip block size;

    wc->ch_offset += 1 + s->stereo;

    s->max_samples = *data_size / (bpp * avctx->channels);
    s->max_samples = FFMIN(s->max_samples, s->samples);
    if(s->samples_left > 0){
        s->max_samples = FFMIN(s->max_samples, s->samples_left);
        buf = buf_end;
    }

    // parse metadata blocks
    while(buf < buf_end){
        id = *buf++;
        size = *buf++;
        if(id & WP_IDF_LONG) {
            size |= (*buf++) << 8;
            size |= (*buf++) << 16;
        }
        size <<= 1; // size is specified in words
        ssize = size;
        if(id & WP_IDF_ODD) size--;
        if(size < 0){
            av_log(avctx, AV_LOG_ERROR, "Got incorrect block %02X with size %i\n", id, size);
            break;
        }
        if(buf + ssize > buf_end){
            av_log(avctx, AV_LOG_ERROR, "Block size %i is out of bounds\n", size);
            break;
        }
        if(id & WP_IDF_IGNORE){
            buf += ssize;
            continue;
        }
        switch(id & WP_IDF_MASK){
        case WP_ID_DECTERMS:
            s->terms = size;
            if(s->terms > MAX_TERMS){
                av_log(avctx, AV_LOG_ERROR, "Too many decorrelation terms\n");
                buf += ssize;
                continue;
            }
            for(i = 0; i < s->terms; i++) {
                s->decorr[s->terms - i - 1].value = (*buf & 0x1F) - 5;
                s->decorr[s->terms - i - 1].delta = *buf >> 5;
                buf++;
            }
            got_terms = 1;
            break;
        case WP_ID_DECWEIGHTS:
            if(!got_terms){
                av_log(avctx, AV_LOG_ERROR, "No decorrelation terms met\n");
                continue;
            }
            weights = size >> s->stereo_in;
            if(weights > MAX_TERMS || weights > s->terms){
                av_log(avctx, AV_LOG_ERROR, "Too many decorrelation weights\n");
                buf += ssize;
                continue;
            }
            for(i = 0; i < weights; i++) {
                t = (int8_t)(*buf++);
                s->decorr[s->terms - i - 1].weightA = t << 3;
                if(s->decorr[s->terms - i - 1].weightA > 0)
                    s->decorr[s->terms - i - 1].weightA += (s->decorr[s->terms - i - 1].weightA + 64) >> 7;
                if(s->stereo_in){
                    t = (int8_t)(*buf++);
                    s->decorr[s->terms - i - 1].weightB = t << 3;
                    if(s->decorr[s->terms - i - 1].weightB > 0)
                        s->decorr[s->terms - i - 1].weightB += (s->decorr[s->terms - i - 1].weightB + 64) >> 7;
                }
            }
            got_weights = 1;
            break;
        case WP_ID_DECSAMPLES:
            if(!got_terms){
                av_log(avctx, AV_LOG_ERROR, "No decorrelation terms met\n");
                continue;
            }
            t = 0;
            for(i = s->terms - 1; (i >= 0) && (t < size); i--) {
                if(s->decorr[i].value > 8){
                    s->decorr[i].samplesA[0] = wp_exp2(AV_RL16(buf)); buf += 2;
                    s->decorr[i].samplesA[1] = wp_exp2(AV_RL16(buf)); buf += 2;
                    if(s->stereo_in){
                        s->decorr[i].samplesB[0] = wp_exp2(AV_RL16(buf)); buf += 2;
                        s->decorr[i].samplesB[1] = wp_exp2(AV_RL16(buf)); buf += 2;
                        t += 4;
                    }
                    t += 4;
                }else if(s->decorr[i].value < 0){
                    s->decorr[i].samplesA[0] = wp_exp2(AV_RL16(buf)); buf += 2;
                    s->decorr[i].samplesB[0] = wp_exp2(AV_RL16(buf)); buf += 2;
                    t += 4;
                }else{
                    for(j = 0; j < s->decorr[i].value; j++){
                        s->decorr[i].samplesA[j] = wp_exp2(AV_RL16(buf)); buf += 2;
                        if(s->stereo_in){
                            s->decorr[i].samplesB[j] = wp_exp2(AV_RL16(buf)); buf += 2;
                        }
                    }
                    t += s->decorr[i].value * 2 * (s->stereo_in + 1);
                }
            }
            got_samples = 1;
            break;
        case WP_ID_ENTROPY:
            if(size != 6 * (s->stereo_in + 1)){
                av_log(avctx, AV_LOG_ERROR, "Entropy vars size should be %i, got %i", 6 * (s->stereo_in + 1), size);
                buf += ssize;
                continue;
            }
            for(j = 0; j <= s->stereo_in; j++){
                for(i = 0; i < 3; i++){
                    s->ch[j].median[i] = wp_exp2(AV_RL16(buf));
                    buf += 2;
                }
            }
            got_entropy = 1;
            break;
        case WP_ID_HYBRID:
            if(s->hybrid_bitrate){
                for(i = 0; i <= s->stereo_in; i++){
                    s->ch[i].slow_level = wp_exp2(AV_RL16(buf));
                    buf += 2;
                    size -= 2;
                }
            }
            for(i = 0; i < (s->stereo_in + 1); i++){
                s->ch[i].bitrate_acc = AV_RL16(buf) << 16;
                buf += 2;
                size -= 2;
            }
            if(size > 0){
                for(i = 0; i < (s->stereo_in + 1); i++){
                    s->ch[i].bitrate_delta = wp_exp2((int16_t)AV_RL16(buf));
                    buf += 2;
                }
            }else{
                for(i = 0; i < (s->stereo_in + 1); i++)
                    s->ch[i].bitrate_delta = 0;
            }
            got_hybrid = 1;
            break;
        case WP_ID_INT32INFO:
            if(size != 4){
                av_log(avctx, AV_LOG_ERROR, "Invalid INT32INFO, size = %i, sent_bits = %i\n", size, *buf);
                buf += ssize;
                continue;
            }
            if(buf[0])
                s->extra_bits = buf[0];
            else if(buf[1])
                s->shift = buf[1];
            else if(buf[2]){
                s->and = s->or = 1;
                s->shift = buf[2];
            }else if(buf[3]){
                s->and = 1;
                s->shift = buf[3];
            }
            buf += 4;
            break;
        case WP_ID_FLOATINFO:
            if(size != 4){
                av_log(avctx, AV_LOG_ERROR, "Invalid FLOATINFO, size = %i\n", size);
                buf += ssize;
                continue;
            }
            s->float_flag = buf[0];
            s->float_shift = buf[1];
            s->float_max_exp = buf[2];
            buf += 4;
            got_float = 1;
            break;
        case WP_ID_DATA:
            s->sc.offset = buf - orig_buf;
            s->sc.size   = size * 8;
            init_get_bits(&s->gb, buf, size * 8);
            s->data_size = size * 8;
            buf += size;
            got_bs = 1;
            break;
        case WP_ID_EXTRABITS:
            if(size <= 4){
                av_log(avctx, AV_LOG_ERROR, "Invalid EXTRABITS, size = %i\n", size);
                buf += size;
                continue;
            }
            s->extra_sc.offset = buf - orig_buf;
            s->extra_sc.size   = size * 8;
            init_get_bits(&s->gb_extra_bits, buf, size * 8);
            s->crc_extra_bits = get_bits_long(&s->gb_extra_bits, 32);
            buf += size;
            s->got_extra_bits = 1;
            break;
        case WP_ID_CHANINFO:
            if(size <= 1){
                av_log(avctx, AV_LOG_ERROR, "Insufficient channel information\n");
                return -1;
            }
            chan = *buf++;
            switch(size - 2){
            case 0:
                chmask = *buf;
                break;
            case 1:
                chmask = AV_RL16(buf);
                break;
            case 2:
                chmask = AV_RL24(buf);
                break;
            case 3:
                chmask = AV_RL32(buf);
                break;
            case 5:
                chan |= (buf[1] & 0xF) << 8;
                chmask = AV_RL24(buf + 2);
                break;
            default:
                av_log(avctx, AV_LOG_ERROR, "Invalid channel info size %d\n", size);
                chan = avctx->channels;
                chmask = avctx->channel_layout;
            }
            if(chan != avctx->channels){
                av_log(avctx, AV_LOG_ERROR, "Block reports total %d channels, decoder believes it's %d channels\n",
                       chan, avctx->channels);
                return -1;
            }
            if(!avctx->channel_layout)
                avctx->channel_layout = chmask;
            buf += size - 1;
            break;
        default:
            buf += size;
        }
        if(id & WP_IDF_ODD) buf++;
    }
    if(!s->samples_left){
        if(!got_terms){
            av_log(avctx, AV_LOG_ERROR, "No block with decorrelation terms\n");
            return -1;
        }
        if(!got_weights){
            av_log(avctx, AV_LOG_ERROR, "No block with decorrelation weights\n");
            return -1;
        }
        if(!got_samples){
            av_log(avctx, AV_LOG_ERROR, "No block with decorrelation samples\n");
            return -1;
        }
        if(!got_entropy){
            av_log(avctx, AV_LOG_ERROR, "No block with entropy info\n");
            return -1;
        }
        if(s->hybrid && !got_hybrid){
            av_log(avctx, AV_LOG_ERROR, "Hybrid config not found\n");
            return -1;
        }
        if(!got_bs){
            av_log(avctx, AV_LOG_ERROR, "Packed samples not found\n");
            return -1;
        }
        if(!got_float && avctx->sample_fmt == AV_SAMPLE_FMT_FLT){
            av_log(avctx, AV_LOG_ERROR, "Float information not found\n");
            return -1;
        }
        if(s->got_extra_bits && avctx->sample_fmt != AV_SAMPLE_FMT_FLT){
            const int size = get_bits_left(&s->gb_extra_bits);
            const int wanted = s->samples * s->extra_bits << s->stereo_in;
            if(size < wanted){
                av_log(avctx, AV_LOG_ERROR, "Too small EXTRABITS\n");
                s->got_extra_bits = 0;
            }
        }
        s->samples_left = s->samples;
    }else{
        init_get_bits(&s->gb, orig_buf + s->sc.offset, s->sc.size);
        skip_bits_long(&s->gb, s->sc.bits_used);
        if(s->got_extra_bits){
            init_get_bits(&s->gb_extra_bits, orig_buf + s->extra_sc.offset,
                          s->extra_sc.size);
            skip_bits_long(&s->gb_extra_bits, s->extra_sc.bits_used);
        }
    }

    if(s->stereo_in){
        if(avctx->sample_fmt == AV_SAMPLE_FMT_S16)
            samplecount = wv_unpack_stereo(s, &s->gb, samples, AV_SAMPLE_FMT_S16);
        else if(avctx->sample_fmt == AV_SAMPLE_FMT_S32)
            samplecount = wv_unpack_stereo(s, &s->gb, samples, AV_SAMPLE_FMT_S32);
        else
            samplecount = wv_unpack_stereo(s, &s->gb, samples, AV_SAMPLE_FMT_FLT);
        samplecount >>= 1;
    }else{
        const int channel_stride = avctx->channels;

        if(avctx->sample_fmt == AV_SAMPLE_FMT_S16)
            samplecount = wv_unpack_mono(s, &s->gb, samples, AV_SAMPLE_FMT_S16);
        else if(avctx->sample_fmt == AV_SAMPLE_FMT_S32)
            samplecount = wv_unpack_mono(s, &s->gb, samples, AV_SAMPLE_FMT_S32);
        else
            samplecount = wv_unpack_mono(s, &s->gb, samples, AV_SAMPLE_FMT_FLT);

        if(s->stereo && avctx->sample_fmt == AV_SAMPLE_FMT_S16){
            int16_t *dst = (int16_t*)samples + 1;
            int16_t *src = (int16_t*)samples;
            int cnt = samplecount;
            while(cnt--){
                *dst = *src;
                src += channel_stride;
                dst += channel_stride;
            }
        }else if(s->stereo && avctx->sample_fmt == AV_SAMPLE_FMT_S32){
            int32_t *dst = (int32_t*)samples + 1;
            int32_t *src = (int32_t*)samples;
            int cnt = samplecount;
            while(cnt--){
                *dst = *src;
                src += channel_stride;
                dst += channel_stride;
            }
        }else if(s->stereo){
            float *dst = (float*)samples + 1;
            float *src = (float*)samples;
            int cnt = samplecount;
            while(cnt--){
                *dst = *src;
                src += channel_stride;
                dst += channel_stride;
            }
        }
    }

    wc->samples_left = s->samples_left;

    return samplecount * bpp;
}

static int wavpack_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            AVPacket *avpkt)
{
    WavpackContext *s = avctx->priv_data;
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    int frame_size;
    int samplecount = 0;

    s->block = 0;
    s->samples_left = 0;
    s->ch_offset = 0;

    if(s->mkv_mode){
        s->samples = AV_RL32(buf); buf += 4;
    }
    while(buf_size > 0){
        if(!s->multichannel){
            frame_size = buf_size;
        }else{
            if(!s->mkv_mode){
                frame_size = AV_RL32(buf) - 12; buf += 4; buf_size -= 4;
            }else{
                if(buf_size < 12) //MKV files can have zero flags after last block
                    break;
                frame_size = AV_RL32(buf + 8) + 12;
            }
        }
        if(frame_size < 0 || frame_size > buf_size){
            av_log(avctx, AV_LOG_ERROR, "Block %d has invalid size (size %d vs. %d bytes left)\n",
                   s->block, frame_size, buf_size);
            return -1;
        }
        if((samplecount = wavpack_decode_block(avctx, s->block, data,
                                               data_size, buf, frame_size)) < 0)
            return -1;
        s->block++;
        buf += frame_size; buf_size -= frame_size;
    }
    *data_size = samplecount * avctx->channels;

    return s->samples_left > 0 ? 0 : avpkt->size;
}

AVCodec ff_wavpack_decoder = {
    "wavpack",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_WAVPACK,
    sizeof(WavpackContext),
    wavpack_decode_init,
    NULL,
    wavpack_decode_end,
    wavpack_decode_frame,
    .capabilities = CODEC_CAP_SUBFRAMES,
    .long_name = NULL_IF_CONFIG_SMALL("WavPack"),
};
