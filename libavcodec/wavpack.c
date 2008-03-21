/*
 * WavPack lossless audio decoder
 * Copyright (c) 2006 Konstantin Shishkov
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
#include "bitstream.h"
#include "unary.h"

/**
 * @file wavpack.c
 * WavPack lossless audio decoder
 */

#define WV_JOINT_STEREO 0x00000010
#define WV_FALSE_STEREO 0x40000000

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
    WP_ID_FLT,
    WP_ID_CHANINFO
};

#define MAX_TERMS 16

typedef struct Decorr {
    int delta;
    int value;
    int weightA;
    int weightB;
    int samplesA[8];
    int samplesB[8];
} Decorr;

typedef struct WavpackContext {
    AVCodecContext *avctx;
    int stereo, stereo_in;
    int joint;
    uint32_t CRC;
    GetBitContext gb;
    int data_size; // in bits
    int samples;
    int median[6];
    int terms;
    Decorr decorr[MAX_TERMS];
    int zero, one, zeroes;
    int and, or, shift;
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

// macros for manipulating median values
#define GET_MED(n) ((median[n] >> 4) + 1)
#define DEC_MED(n) median[n] -= ((median[n] + (128>>n) - 2) / (128>>n)) * 2
#define INC_MED(n) median[n] += ((median[n] + (128>>n)) / (128>>n)) * 5

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

static int wv_get_value(WavpackContext *ctx, GetBitContext *gb, int *median, int *last)
{
    int t, t2;
    int sign, base, add, ret;

    *last = 0;

    if((ctx->median[0] < 2U) && (ctx->median[3] < 2U) && !ctx->zero && !ctx->one){
        if(ctx->zeroes){
            ctx->zeroes--;
            if(ctx->zeroes)
                return 0;
        }else{
            t = get_unary_0_33(gb);
            if(t >= 2) t = get_bits(gb, t - 1) | (1 << (t-1));
            ctx->zeroes = t;
            if(ctx->zeroes){
                memset(ctx->median, 0, sizeof(ctx->median));
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
    ret = base + get_tail(gb, add);
    sign = get_bits1(gb);
    return sign ? ~ret : ret;
}

static int wv_unpack_stereo(WavpackContext *s, GetBitContext *gb, int16_t *dst)
{
    int i, j, count = 0;
    int last, t;
    int A, B, L, L2, R, R2, bit;
    int pos = 0;
    uint32_t crc = 0xFFFFFFFF;

    s->one = s->zero = s->zeroes = 0;
    do{
        L = wv_get_value(s, gb, s->median, &last);
        if(last) break;
        R = wv_get_value(s, gb, s->median + 3, &last);
        if(last) break;
        for(i = 0; i < s->terms; i++){
            t = s->decorr[i].value;
            j = 0;
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
                L2 = L + ((s->decorr[i].weightA * A + 512) >> 10);
                R2 = R + ((s->decorr[i].weightB * B + 512) >> 10);
                if(A && L) s->decorr[i].weightA -= ((((L ^ A) >> 30) & 2) - 1) * s->decorr[i].delta;
                if(B && R) s->decorr[i].weightB -= ((((R ^ B) >> 30) & 2) - 1) * s->decorr[i].delta;
                s->decorr[i].samplesA[j] = L = L2;
                s->decorr[i].samplesB[j] = R = R2;
            }else if(t == -1){
                L2 = L + ((s->decorr[i].weightA * s->decorr[i].samplesA[0] + 512) >> 10);
                UPDATE_WEIGHT_CLIP(s->decorr[i].weightA, s->decorr[i].delta, s->decorr[i].samplesA[0], L);
                L = L2;
                R2 = R + ((s->decorr[i].weightB * L2 + 512) >> 10);
                UPDATE_WEIGHT_CLIP(s->decorr[i].weightB, s->decorr[i].delta, L2, R);
                R = R2;
                s->decorr[i].samplesA[0] = R;
            }else{
                R2 = R + ((s->decorr[i].weightB * s->decorr[i].samplesB[0] + 512) >> 10);
                UPDATE_WEIGHT_CLIP(s->decorr[i].weightB, s->decorr[i].delta, s->decorr[i].samplesB[0], R);
                R = R2;

                if(t == -3){
                    R2 = s->decorr[i].samplesA[0];
                    s->decorr[i].samplesA[0] = R;
                }

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
        bit = (L & s->and) | s->or;
        *dst++ = ((L + bit) << s->shift) - bit;
        bit = (R & s->and) | s->or;
        *dst++ = ((R + bit) << s->shift) - bit;
        count++;
    }while(!last && count < s->samples);

    if(crc != s->CRC){
        av_log(s->avctx, AV_LOG_ERROR, "CRC error\n");
        return -1;
    }
    return count * 2;
}

static int wv_unpack_mono(WavpackContext *s, GetBitContext *gb, int16_t *dst)
{
    int i, j, count = 0;
    int last, t;
    int A, S, T, bit;
    int pos = 0;
    uint32_t crc = 0xFFFFFFFF;

    s->one = s->zero = s->zeroes = 0;
    do{
        T = wv_get_value(s, gb, s->median, &last);
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
            S = T + ((s->decorr[i].weightA * A + 512) >> 10);
            if(A && T) s->decorr[i].weightA -= ((((T ^ A) >> 30) & 2) - 1) * s->decorr[i].delta;
            s->decorr[i].samplesA[j] = T = S;
        }
        pos = (pos + 1) & 7;
        crc = crc * 3 + S;
        bit = (S & s->and) | s->or;
        *dst++ = ((S + bit) << s->shift) - bit;
        count++;
    }while(!last && count < s->samples);

    if(crc != s->CRC){
        av_log(s->avctx, AV_LOG_ERROR, "CRC error\n");
        return -1;
    }
    return count;
}

static av_cold int wavpack_decode_init(AVCodecContext *avctx)
{
    WavpackContext *s = avctx->priv_data;

    s->avctx = avctx;
    s->stereo = (avctx->channels == 2);

    return 0;
}

static int wavpack_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            const uint8_t *buf, int buf_size)
{
    WavpackContext *s = avctx->priv_data;
    int16_t *samples = data;
    int samplecount;
    int got_terms = 0, got_weights = 0, got_samples = 0, got_entropy = 0, got_bs = 0;
    const uint8_t* buf_end = buf + buf_size;
    int i, j, id, size, ssize, weights, t;

    if (buf_size == 0){
        *data_size = 0;
        return 0;
    }

    memset(s->decorr, 0, MAX_TERMS * sizeof(Decorr));
    memset(s->median, 0, sizeof(s->median));
    s->and = s->or = s->shift = 0;

    s->samples = AV_RL32(buf); buf += 4;
    if(!s->samples){
        *data_size = 0;
        return buf_size;
    }
    /* should not happen but who knows */
    if(s->samples * 2 * avctx->channels > *data_size){
        av_log(avctx, AV_LOG_ERROR, "Packet size is too big to be handled in lavc!\n");
        return -1;
    }
    s->stereo_in = (AV_RL32(buf) & WV_FALSE_STEREO) ? 0 : s->stereo;
    s->joint = AV_RL32(buf) & WV_JOINT_STEREO; buf += 4;
    s->CRC = AV_RL32(buf); buf += 4;
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
            for(i = 0; i < 3 * (s->stereo_in + 1); i++){
                s->median[i] = wp_exp2(AV_RL16(buf));
                buf += 2;
            }
            got_entropy = 1;
            break;
        case WP_ID_INT32INFO:
            if(size != 4 || *buf){
                av_log(avctx, AV_LOG_ERROR, "Invalid INT32INFO, size = %i, sent_bits = %i\n", size, *buf);
                buf += ssize;
                continue;
            }
            if(buf[1])
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
        case WP_ID_DATA:
            init_get_bits(&s->gb, buf, size * 8);
            s->data_size = size * 8;
            buf += size;
            got_bs = 1;
            break;
        default:
            buf += size;
        }
        if(id & WP_IDF_ODD) buf++;
    }
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
    if(!got_bs){
        av_log(avctx, AV_LOG_ERROR, "Packed samples not found\n");
        return -1;
    }

    if(s->stereo_in)
        samplecount = wv_unpack_stereo(s, &s->gb, samples);
    else{
        samplecount = wv_unpack_mono(s, &s->gb, samples);
        if(s->stereo){
            int16_t *dst = samples + samplecount * 2;
            int16_t *src = samples + samplecount;
            int cnt = samplecount;
            while(cnt--){
                *--dst = *--src;
                *--dst = *src;
            }
            samplecount *= 2;
        }
    }
    *data_size = samplecount * 2;

    return buf_size;
}

AVCodec wavpack_decoder = {
    "wavpack",
    CODEC_TYPE_AUDIO,
    CODEC_ID_WAVPACK,
    sizeof(WavpackContext),
    wavpack_decode_init,
    NULL,
    NULL,
    wavpack_decode_frame,
};
