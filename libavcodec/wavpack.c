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

#include "libavutil/channel_layout.h"

#define BITSTREAM_READER_LE
#include "avcodec.h"
#include "bytestream.h"
#include "get_bits.h"
#include "internal.h"
#include "thread.h"
#include "unary.h"
#include "wavpack.h"

/**
 * @file
 * WavPack lossless audio decoder
 */

typedef struct SavedContext {
    int offset;
    int size;
    int bits_used;
    uint32_t crc;
} SavedContext;

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
    int hybrid_maxclip, hybrid_minclip;
    int float_flag;
    int float_shift;
    int float_max_exp;
    WvChannel ch[2];
    int pos;
    SavedContext sc, extra_sc;
} WavpackFrameContext;

#define WV_MAX_FRAME_DECODERS 14

typedef struct WavpackContext {
    AVCodecContext *avctx;

    WavpackFrameContext *fdec[WV_MAX_FRAME_DECODERS];
    int fdec_num;

    int block;
    int samples;
    int ch_offset;
} WavpackContext;

#define LEVEL_DECAY(a)  (((a) + 0x80) >> 8)

static av_always_inline int get_tail(GetBitContext *gb, int k)
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

static void update_error_limit(WavpackFrameContext *ctx)
{
    int i, br[2], sl[2];

    for (i = 0; i <= ctx->stereo_in; i++) {
        ctx->ch[i].bitrate_acc += ctx->ch[i].bitrate_delta;
        br[i]                   = ctx->ch[i].bitrate_acc >> 16;
        sl[i]                   = LEVEL_DECAY(ctx->ch[i].slow_level);
    }
    if (ctx->stereo_in && ctx->hybrid_bitrate) {
        int balance = (sl[1] - sl[0] + br[1] + 1) >> 1;
        if (balance > br[0]) {
            br[1] = br[0] << 1;
            br[0] = 0;
        } else if (-balance > br[0]) {
            br[0] <<= 1;
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
                if (get_bits_left(gb) < t - 1)
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
                if (get_bits_left(gb) < t2 - 1)
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

    if (ctx->hybrid && !channel)
        update_error_limit(ctx);

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
        base = GET_MED(0) + GET_MED(1) + GET_MED(2) * (t - 2);
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
        int mid = (base * 2 + add + 1) >> 1;
        while (add > c->error_limit) {
            if (get_bits_left(gb) <= 0)
                goto error;
            if (get_bits1(gb)) {
                add -= (mid - base);
                base = mid;
            } else
                add = mid - base - 1;
            mid = (base * 2 + add + 1) >> 1;
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
                                       int S)
{
    int bit;

    if (s->extra_bits) {
        S <<= s->extra_bits;

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
        S  <<= s->float_shift;
        sign = S < 0;
        if (sign)
            S = -S;
        if (S >= 0x1000000) {
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

static void wv_reset_saved_context(WavpackFrameContext *s)
{
    s->pos    = 0;
    s->sc.crc = s->extra_sc.crc = 0xFFFFFFFF;
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

static inline int wv_unpack_stereo(WavpackFrameContext *s, GetBitContext *gb,
                                   void *dst_l, void *dst_r, const int type)
{
    int i, j, count = 0;
    int last, t;
    int A, B, L, L2, R, R2;
    int pos                 = s->pos;
    uint32_t crc            = s->sc.crc;
    uint32_t crc_extra_bits = s->extra_sc.crc;
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
                        A = 2 * s->decorr[i].samplesA[0] - s->decorr[i].samplesA[1];
                        B = 2 * s->decorr[i].samplesB[0] - s->decorr[i].samplesB[1];
                    } else {
                        A = (3 * s->decorr[i].samplesA[0] - s->decorr[i].samplesA[1]) >> 1;
                        B = (3 * s->decorr[i].samplesB[0] - s->decorr[i].samplesB[1]) >> 1;
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
                    L2 = L + ((s->decorr[i].weightA * A + 512) >> 10);
                    R2 = R + ((s->decorr[i].weightB * B + 512) >> 10);
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
                    L2 = L + ((s->decorr[i].weightA * s->decorr[i].samplesA[0] + 512) >> 10);
                UPDATE_WEIGHT_CLIP(s->decorr[i].weightA, s->decorr[i].delta, s->decorr[i].samplesA[0], L);
                L = L2;
                if (type != AV_SAMPLE_FMT_S16P)
                    R2 = R + ((s->decorr[i].weightB * (int64_t)L2 + 512) >> 10);
                else
                    R2 = R + ((s->decorr[i].weightB * L2 + 512) >> 10);
                UPDATE_WEIGHT_CLIP(s->decorr[i].weightB, s->decorr[i].delta, L2, R);
                R                        = R2;
                s->decorr[i].samplesA[0] = R;
            } else {
                if (type != AV_SAMPLE_FMT_S16P)
                    R2 = R + ((s->decorr[i].weightB * (int64_t)s->decorr[i].samplesB[0] + 512) >> 10);
                else
                    R2 = R + ((s->decorr[i].weightB * s->decorr[i].samplesB[0] + 512) >> 10);
                UPDATE_WEIGHT_CLIP(s->decorr[i].weightB, s->decorr[i].delta, s->decorr[i].samplesB[0], R);
                R = R2;

                if (t == -3) {
                    R2                       = s->decorr[i].samplesA[0];
                    s->decorr[i].samplesA[0] = R;
                }

                if (type != AV_SAMPLE_FMT_S16P)
                    L2 = L + ((s->decorr[i].weightA * (int64_t)R2 + 512) >> 10);
                else
                    L2 = L + ((s->decorr[i].weightA * R2 + 512) >> 10);
                UPDATE_WEIGHT_CLIP(s->decorr[i].weightA, s->decorr[i].delta, R2, L);
                L                        = L2;
                s->decorr[i].samplesB[0] = L;
            }
        }

        if (type == AV_SAMPLE_FMT_S16P) {
            if (FFABS(L) + FFABS(R) > (1<<19)) {
                av_log(s->avctx, AV_LOG_ERROR, "sample %d %d too large\n", L, R);
                return AVERROR_INVALIDDATA;
            }
        }

        pos = (pos + 1) & 7;
        if (s->joint)
            L += (R -= (L >> 1));
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

    wv_reset_saved_context(s);

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
    int pos                  = s->pos;
    uint32_t crc             = s->sc.crc;
    uint32_t crc_extra_bits  = s->extra_sc.crc;
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
                    A =  2 * s->decorr[i].samplesA[0] - s->decorr[i].samplesA[1];
                else
                    A = (3 * s->decorr[i].samplesA[0] - s->decorr[i].samplesA[1]) >> 1;
                s->decorr[i].samplesA[1] = s->decorr[i].samplesA[0];
                j                        = 0;
            } else {
                A = s->decorr[i].samplesA[pos];
                j = (pos + t) & 7;
            }
            if (type != AV_SAMPLE_FMT_S16P)
                S = T + ((s->decorr[i].weightA * (int64_t)A + 512) >> 10);
            else
                S = T + ((s->decorr[i].weightA * A + 512) >> 10);
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

    wv_reset_saved_context(s);

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
    wv_reset_saved_context(c->fdec[c->fdec_num - 1]);

    return 0;
}

#if HAVE_THREADS
static int init_thread_copy(AVCodecContext *avctx)
{
    WavpackContext *s = avctx->priv_data;
    s->avctx = avctx;
    return 0;
}
#endif

static av_cold int wavpack_decode_init(AVCodecContext *avctx)
{
    WavpackContext *s = avctx->priv_data;

    s->avctx = avctx;

    s->fdec_num = 0;

    return 0;
}

static av_cold int wavpack_decode_end(AVCodecContext *avctx)
{
    WavpackContext *s = avctx->priv_data;
    int i;

    for (i = 0; i < s->fdec_num; i++)
        av_freep(&s->fdec[i]);
    s->fdec_num = 0;

    return 0;
}

static int wavpack_decode_block(AVCodecContext *avctx, int block_no,
                                AVFrame *frame, const uint8_t *buf, int buf_size)
{
    WavpackContext *wc = avctx->priv_data;
    ThreadFrame tframe = { .f = frame };
    WavpackFrameContext *s;
    GetByteContext gb;
    void *samples_l = NULL, *samples_r = NULL;
    int ret;
    int got_terms   = 0, got_weights = 0, got_samples = 0,
        got_entropy = 0, got_bs      = 0, got_float   = 0, got_hybrid = 0;
    int i, j, id, size, ssize, weights, t;
    int bpp, chan = 0, chmask = 0, orig_bpp, sample_rate = 0;
    int multiblock;

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
    bpp            = av_get_bytes_per_sample(avctx->sample_fmt);
    orig_bpp       = ((s->frame_flags & 0x03) + 1) << 3;
    multiblock     = (s->frame_flags & WV_SINGLE_BLOCK) != WV_SINGLE_BLOCK;

    s->stereo         = !(s->frame_flags & WV_MONO);
    s->stereo_in      =  (s->frame_flags & WV_FALSE_STEREO) ? 0 : s->stereo;
    s->joint          =   s->frame_flags & WV_JOINT_STEREO;
    s->hybrid         =   s->frame_flags & WV_HYBRID_MODE;
    s->hybrid_bitrate =   s->frame_flags & WV_HYBRID_BITRATE;
    s->post_shift     = bpp * 8 - orig_bpp + ((s->frame_flags >> 13) & 0x1f);
    s->hybrid_maxclip =  ((1LL << (orig_bpp - 1)) - 1);
    s->hybrid_minclip = ((-1LL << (orig_bpp - 1)));
    s->CRC            = bytestream2_get_le32(&gb);

    // parse metadata blocks
    while (bytestream2_get_bytes_left(&gb)) {
        id   = bytestream2_get_byte(&gb);
        size = bytestream2_get_byte(&gb);
        if (id & WP_IDF_LONG) {
            size |= (bytestream2_get_byte(&gb)) << 8;
            size |= (bytestream2_get_byte(&gb)) << 16;
        }
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
                s->decorr[s->terms - i - 1].weightA = t << 3;
                if (s->decorr[s->terms - i - 1].weightA > 0)
                    s->decorr[s->terms - i - 1].weightA +=
                        (s->decorr[s->terms - i - 1].weightA + 64) >> 7;
                if (s->stereo_in) {
                    t = (int8_t)bytestream2_get_byte(&gb);
                    s->decorr[s->terms - i - 1].weightB = t << 3;
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
            if (val[0] > 32) {
                av_log(avctx, AV_LOG_ERROR,
                       "Invalid INT32INFO, extra_bits = %d (> 32)\n", val[0]);
                continue;
            } else if (val[0]) {
                s->extra_bits = val[0];
            } else if (val[1]) {
                s->shift = val[1];
            } else if (val[2]) {
                s->and   = s->or = 1;
                s->shift = val[2];
            } else if (val[3]) {
                s->and   = 1;
                s->shift = val[3];
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
            got_float        = 1;
            bytestream2_skip(&gb, 1);
            break;
        case WP_ID_DATA:
            s->sc.offset = bytestream2_tell(&gb);
            s->sc.size   = size * 8;
            if ((ret = init_get_bits8(&s->gb, gb.buffer, size)) < 0)
                return ret;
            s->data_size = size * 8;
            bytestream2_skip(&gb, size);
            got_bs       = 1;
            break;
        case WP_ID_EXTRABITS:
            if (size <= 4) {
                av_log(avctx, AV_LOG_ERROR, "Invalid EXTRABITS, size = %i\n",
                       size);
                bytestream2_skip(&gb, size);
                continue;
            }
            s->extra_sc.offset = bytestream2_tell(&gb);
            s->extra_sc.size   = size * 8;
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
            case 5:
                size = bytestream2_get_byte(&gb);
                if (avctx->channels != size)
                    av_log(avctx, AV_LOG_WARNING, "%i channels signalled"
                           " instead of %i.\n", size, avctx->channels);
                chan  |= (bytestream2_get_byte(&gb) & 0xF) << 8;
                chmask = bytestream2_get_le16(&gb);
                break;
            default:
                av_log(avctx, AV_LOG_ERROR, "Invalid channel info size %d\n",
                       size);
                chan   = avctx->channels;
                chmask = avctx->channel_layout;
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
    if (!got_bs) {
        av_log(avctx, AV_LOG_ERROR, "Packed samples not found\n");
        return AVERROR_INVALIDDATA;
    }
    if (!got_float && avctx->sample_fmt == AV_SAMPLE_FMT_FLTP) {
        av_log(avctx, AV_LOG_ERROR, "Float information not found\n");
        return AVERROR_INVALIDDATA;
    }
    if (s->got_extra_bits && avctx->sample_fmt != AV_SAMPLE_FMT_FLTP) {
        const int size   = get_bits_left(&s->gb_extra_bits);
        const int wanted = s->samples * s->extra_bits << s->stereo_in;
        if (size < wanted) {
            av_log(avctx, AV_LOG_ERROR, "Too small EXTRABITS\n");
            s->got_extra_bits = 0;
        }
    }

    if (!wc->ch_offset) {
        int sr = (s->frame_flags >> 23) & 0xf;
        if (sr == 0xf) {
            if (!sample_rate) {
                av_log(avctx, AV_LOG_ERROR, "Custom sample rate missing.\n");
                return AVERROR_INVALIDDATA;
            }
            avctx->sample_rate = sample_rate;
        } else
            avctx->sample_rate = wv_rates[sr];

        if (multiblock) {
            if (chan)
                avctx->channels = chan;
            if (chmask)
                avctx->channel_layout = chmask;
        } else {
            avctx->channels       = s->stereo ? 2 : 1;
            avctx->channel_layout = s->stereo ? AV_CH_LAYOUT_STEREO :
                                                AV_CH_LAYOUT_MONO;
        }

        /* get output buffer */
        frame->nb_samples = s->samples + 1;
        if ((ret = ff_thread_get_buffer(avctx, &tframe, 0)) < 0)
            return ret;
        frame->nb_samples = s->samples;
    }

    if (wc->ch_offset + s->stereo >= avctx->channels) {
        av_log(avctx, AV_LOG_WARNING, "Too many channels coded in a packet.\n");
        return (avctx->err_recognition & AV_EF_EXPLODE) ? AVERROR_INVALIDDATA : 0;
    }

    samples_l = frame->extended_data[wc->ch_offset];
    if (s->stereo)
        samples_r = frame->extended_data[wc->ch_offset + 1];

    wc->ch_offset += 1 + s->stereo;

    if (s->stereo_in) {
        ret = wv_unpack_stereo(s, &s->gb, samples_l, samples_r, avctx->sample_fmt);
        if (ret < 0)
            return ret;
    } else {
        ret = wv_unpack_mono(s, &s->gb, samples_l, avctx->sample_fmt);
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
    int i;

    for (i = 0; i < s->fdec_num; i++)
        wv_reset_saved_context(s->fdec[i]);
}

static int wavpack_decode_frame(AVCodecContext *avctx, void *data,
                                int *got_frame_ptr, AVPacket *avpkt)
{
    WavpackContext *s  = avctx->priv_data;
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    AVFrame *frame     = data;
    int frame_size, ret, frame_flags;

    if (avpkt->size <= WV_HEADER_SIZE)
        return AVERROR_INVALIDDATA;

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

    if (frame_flags & 0x80) {
        avctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    } else if ((frame_flags & 0x03) <= 1) {
        avctx->sample_fmt = AV_SAMPLE_FMT_S16P;
    } else {
        avctx->sample_fmt          = AV_SAMPLE_FMT_S32P;
        avctx->bits_per_raw_sample = ((frame_flags & 0x03) + 1) << 3;
    }

    while (buf_size > 0) {
        if (buf_size <= WV_HEADER_SIZE)
            break;
        frame_size = AV_RL32(buf + 4) - 12;
        buf       += 20;
        buf_size  -= 20;
        if (frame_size <= 0 || frame_size > buf_size) {
            av_log(avctx, AV_LOG_ERROR,
                   "Block %d has invalid size (size %d vs. %d bytes left)\n",
                   s->block, frame_size, buf_size);
            wavpack_decode_flush(avctx);
            return AVERROR_INVALIDDATA;
        }
        if ((ret = wavpack_decode_block(avctx, s->block,
                                        frame, buf, frame_size)) < 0) {
            wavpack_decode_flush(avctx);
            return ret;
        }
        s->block++;
        buf      += frame_size;
        buf_size -= frame_size;
    }

    if (s->ch_offset != avctx->channels) {
        av_log(avctx, AV_LOG_ERROR, "Not enough channels coded in a packet.\n");
        return AVERROR_INVALIDDATA;
    }

    *got_frame_ptr = 1;

    return avpkt->size;
}

AVCodec ff_wavpack_decoder = {
    .name           = "wavpack",
    .long_name      = NULL_IF_CONFIG_SMALL("WavPack"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_WAVPACK,
    .priv_data_size = sizeof(WavpackContext),
    .init           = wavpack_decode_init,
    .close          = wavpack_decode_end,
    .decode         = wavpack_decode_frame,
    .flush          = wavpack_decode_flush,
    .init_thread_copy = ONLY_IF_THREADS_ENABLED(init_thread_copy),
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
};
