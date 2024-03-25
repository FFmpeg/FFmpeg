/*
 * RKA decoder
 * Copyright (c) 2023 Paul B Mahol
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
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "bytestream.h"
#include "decode.h"

typedef struct ACoder {
    GetByteContext gb;
    uint32_t low, high;
    uint32_t value;
} ACoder;

typedef struct FiltCoeffs {
    int32_t coeffs[257];
    unsigned size;
} FiltCoeffs;

typedef struct Model64 {
    uint32_t zero[2];
    uint32_t sign[2];
    unsigned size;
    int bits;

    uint16_t val4[65];
    uint16_t val1[65];
} Model64;

typedef struct AdaptiveModel {
    int last;
    int total;
    int buf_size;
    int16_t sum;
    uint16_t aprob0;
    uint16_t aprob1;
    uint16_t *prob[2];
} AdaptiveModel;

typedef struct ChContext {
    int qfactor;
    int vrq;
    int last_nb_decoded;
    unsigned srate_pad;
    unsigned pos_idx;

    AdaptiveModel *filt_size;
    AdaptiveModel *filt_bits;

    uint32_t *bprob[2];

    AdaptiveModel position;
    AdaptiveModel fshift;
    AdaptiveModel nb_segments;
    AdaptiveModel coeff_bits[11];

    Model64 mdl64[4][11];

    int32_t buf0[131072+2560];
    int32_t buf1[131072+2560];
} ChContext;

typedef struct RKAContext {
    AVClass *class;

    ACoder ac;
    ChContext ch[2];

    int bps;
    int align;
    int channels;
    int correlated;
    int frame_samples;
    int last_nb_samples;
    uint32_t total_nb_samples;
    uint32_t samples_left;

    uint32_t bprob[2][257];

    AdaptiveModel filt_size;
    AdaptiveModel filt_bits;
} RKAContext;

static int adaptive_model_init(AdaptiveModel *am, int buf_size)
{
    am->buf_size = buf_size;
    am->sum = 2000;
    am->aprob0 = 0;
    am->aprob1 = 0;
    am->total = 0;

    if (!am->prob[0])
        am->prob[0] = av_malloc_array(buf_size + 5, sizeof(*am->prob[0]));
    if (!am->prob[1])
        am->prob[1] = av_malloc_array(buf_size + 5, sizeof(*am->prob[1]));

    if (!am->prob[0] || !am->prob[1])
        return AVERROR(ENOMEM);
    memset(am->prob[0], 0, (buf_size + 5) * sizeof(*am->prob[0]));
    memset(am->prob[1], 0, (buf_size + 5) * sizeof(*am->prob[1]));
    return 0;
}

static void adaptive_model_free(AdaptiveModel *am)
{
    av_freep(&am->prob[0]);
    av_freep(&am->prob[1]);
}

static av_cold int rka_decode_init(AVCodecContext *avctx)
{
    RKAContext *s = avctx->priv_data;
    int qfactor;

    if (avctx->extradata_size < 16)
        return AVERROR_INVALIDDATA;

    s->bps = avctx->bits_per_raw_sample = avctx->extradata[13];

    switch (s->bps) {
    case 8:
        avctx->sample_fmt = AV_SAMPLE_FMT_U8P;
        break;
    case 16:
        avctx->sample_fmt = AV_SAMPLE_FMT_S16P;
        break;
    default:
        return AVERROR_INVALIDDATA;
    }

    av_channel_layout_uninit(&avctx->ch_layout);
    s->channels = avctx->ch_layout.nb_channels = avctx->extradata[12];
    if (s->channels < 1 || s->channels > 2)
        return AVERROR_INVALIDDATA;

    s->align = (s->channels * (avctx->bits_per_raw_sample >> 3));
    s->samples_left = s->total_nb_samples = (AV_RL32(avctx->extradata + 4)) / s->align;
    s->frame_samples = 131072 / s->align;
    s->last_nb_samples = s->total_nb_samples % s->frame_samples;
    s->correlated = avctx->extradata[15] & 1;

    qfactor = avctx->extradata[14] & 0xf;
    if ((avctx->extradata[15] & 4) != 0)
        qfactor = -qfactor;

    s->ch[0].qfactor = s->ch[1].qfactor = qfactor < 0 ? 2 : qfactor;
    s->ch[0].vrq = qfactor < 0 ? -qfactor : 0;
    s->ch[1].vrq = qfactor < 0 ? -qfactor : 0;
    if (qfactor < 0) {
        s->ch[0].vrq = av_clip(s->ch[0].vrq, 1, 8);
        s->ch[1].vrq = av_clip(s->ch[1].vrq, 1, 8);
    }
    av_log(avctx, AV_LOG_DEBUG, "qfactor: %d\n", qfactor);

    return 0;
}

static void model64_init(Model64 *m, unsigned bits)
{
    unsigned x;

    m->bits = bits;
    m->size = 64;
    m->zero[0] = 1;

    x = (1 << (bits >> 1)) + 3;
    x = FFMIN(x, 20);

    m->zero[1] = x;
    m->sign[0] = 1;
    m->sign[1] = 1;

    for (int i = 0; i < FF_ARRAY_ELEMS(m->val4); i++) {
        m->val4[i] = 4;
        m->val1[i] = 1;
    }
}

static int chctx_init(RKAContext *s, ChContext *c,
                      int sample_rate, int bps)
{
    int ret;

    memset(c->buf0, 0, sizeof(c->buf0));
    memset(c->buf1, 0, sizeof(c->buf1));

    c->filt_size = &s->filt_size;
    c->filt_bits = &s->filt_bits;

    c->bprob[0] = s->bprob[0];
    c->bprob[1] = s->bprob[1];

    c->srate_pad = ((int64_t)sample_rate << 13) / 44100 & 0xFFFFFFFCU;
    c->pos_idx = 1;

    for (int i = 0; i < FF_ARRAY_ELEMS(s->bprob[0]); i++)
        c->bprob[0][i] = c->bprob[1][i] = 1;

    for (int i = 0; i < 11; i++) {
        ret = adaptive_model_init(&c->coeff_bits[i], 32);
        if (ret < 0)
            return ret;

        model64_init(&c->mdl64[0][i], i);
        model64_init(&c->mdl64[1][i], i);
        model64_init(&c->mdl64[2][i], i+1);
        model64_init(&c->mdl64[3][i], i+1);
    }

    ret = adaptive_model_init(c->filt_size, 256);
    if (ret < 0)
        return ret;
    ret = adaptive_model_init(c->filt_bits, 16);
    if (ret < 0)
        return ret;
    ret = adaptive_model_init(&c->position, 16);
    if (ret < 0)
        return ret;
    ret = adaptive_model_init(&c->nb_segments, 8);
    if (ret < 0)
        return ret;
    return adaptive_model_init(&c->fshift, 32);
}

static void init_acoder(ACoder *ac)
{
    ac->low = 0x0;
    ac->high = 0xffffffff;
    ac->value = bytestream2_get_be32(&ac->gb);
}

static int ac_decode_bool(ACoder *ac, int freq1, int freq2)
{
    unsigned help, add, high, value;
    int low;

    low = ac->low;
    help = ac->high / (unsigned)(freq2 + freq1);
    value = ac->value;
    add = freq1 * help;
    ac->high = help;

    if (value - low >= add) {
        ac->low = low = add + low;
        ac->high = high = freq2 * help;
        while (1) {
            if ((low ^ (high + low)) > 0xFFFFFF) {
                if (high > 0xFFFF)
                    return 1;
                ac->high = (uint16_t)-(int16_t)low;
            }

            if (bytestream2_get_bytes_left(&ac->gb) <= 0)
                break;
            ac->value = bytestream2_get_byteu(&ac->gb) | (ac->value << 8);
            ac->high = high = ac->high << 8;
            low = ac->low = ac->low << 8;
        }
        return -1;
    }

    ac->high = add;
    while (1) {
        if ((low ^ (add + low)) > 0xFFFFFF) {
            if (add > 0xFFFF)
                return 0;
            ac->high = (uint16_t)-(int16_t)low;
        }

        if (bytestream2_get_bytes_left(&ac->gb) <= 0)
            break;
        ac->value = bytestream2_get_byteu(&ac->gb) | (ac->value << 8);
        ac->high = add = ac->high << 8;
        low = ac->low = ac->low << 8;
    }
    return -1;
}

static int decode_bool(ACoder *ac, ChContext *c, int idx)
{
    uint32_t x;
    int b;

    x = c->bprob[0][idx];
    if (x + c->bprob[1][idx] > 4096) {
        c->bprob[0][idx] = (x >> 1) + 1;
        c->bprob[1][idx] = (c->bprob[1][idx] >> 1) + 1;
    }

    b = ac_decode_bool(ac, c->bprob[0][idx], c->bprob[1][idx]);
    if (b < 0)
        return b;

    c->bprob[b][idx]++;

    return b;
}

static int ac_get_freq(ACoder *ac, unsigned freq, int *result)
{
    uint32_t new_high;

    if (freq == 0)
        return -1;

    new_high = ac->high / freq;
    ac->high = new_high;

    if (new_high == 0)
        return -1;

    *result = (ac->value - ac->low) / new_high;

    return 0;
}

static int ac_update(ACoder *ac, int freq, int mul)
{
    uint32_t low, high;

    low = ac->low = ac->high * freq + ac->low;
    high = ac->high = ac->high * mul;

    while (1) {
        if (((high + low) ^ low) > 0xffffff) {
            if (high > 0xffff)
                return 0;
            ac->high = (uint16_t)-(int16_t)low;
        }

        if (bytestream2_get_bytes_left(&ac->gb) <= 0)
            break;

        ac->value = (ac->value << 8) | bytestream2_get_byteu(&ac->gb);
        low = ac->low = ac->low << 8;
        high = ac->high = ac->high << 8;
    }

    return -1;
}

static void amdl_update_prob(AdaptiveModel *am, int val, int diff)
{
    am->aprob0 += diff;
    if (val <= 0) {
        am->prob[0][0] += diff;
    } else {
        do {
            am->prob[0][val] += diff;
            val += (val & -val);
        } while (val < am->buf_size);
    }
}

static void update_ch_subobj(AdaptiveModel *am)
{
    int idx2, idx = am->buf_size - 1;

    if (idx >= 0) {
        do {
            uint16_t *prob = am->prob[0];
            int diff, prob_idx = prob[idx];

            idx2 = idx - 1;
            if (idx > 0) {
                int idx3 = idx - 1;

                if ((idx2 & idx) != idx2) {
                    do {
                        prob_idx -= prob[idx3];
                        idx3 &= idx3 - 1;
                    } while ((idx2 & idx) != idx3);
                }
            }

            diff = ((prob_idx > 0) - prob_idx) >> 1;
            amdl_update_prob(am, idx, diff);
            idx--;
        } while (idx2 >= 0);
    }

    if (am->sum < 8000)
        am->sum += 200;

    am->aprob1 = (am->aprob1 + 1) >> 1;
}

static int amdl_decode_int(AdaptiveModel *am, ACoder *ac, unsigned *dst, unsigned size)
{
    unsigned freq, size2, val, mul;
    int j;

    size = FFMIN(size, am->buf_size - 1);

    if (am->aprob0 >= am->sum)
        update_ch_subobj(am);

    if (am->aprob1 && (am->total == am->buf_size ||
                       ac_decode_bool(ac, am->aprob0, am->aprob1) == 0)) {
        if (am->total <= 1) {
            dst[0] = am->last;
            amdl_update_prob(am, dst[0], 1);
            return 0;
        }
        if (size == am->buf_size - 1) {
            freq = am->aprob0;
        } else {
            freq = am->prob[0][0];
            for (int j = size; j > 0; j &= (j - 1) )
                freq += am->prob[0][j];
        }
        ac_get_freq(ac, freq, &freq);
        size2 = am->buf_size >> 1;
        val = am->prob[0][0];
        if (freq >= val) {
            int sum = 0;
            for (j = freq - val; size2; size2 >>= 1) {
                unsigned v = am->prob[0][size2 + sum];
                if (j >= v) {
                    sum += size2;
                    j -= v;
                }
            }
            freq -= j;
            val = sum + 1;
        } else {
            freq = 0;
            val = 0;
        }
        dst[0] = val;
        mul = am->prob[0][val];
        if (val > 0) {
            for (int k = val - 1; (val & (val - 1)) != k; k &= k - 1)
                mul -= am->prob[0][k];
        }
        ac_update(ac, freq, mul);
        amdl_update_prob(am, dst[0], 1);
        return 0;
    }
    am->aprob1++;
    if (size == am->buf_size - 1) {
        ac_get_freq(ac, am->buf_size - am->total, &val);
    } else {
        freq = 1;
        for (dst[0] = 0; dst[0] < size; dst[0]++) {
            if (!am->prob[1][dst[0]])
                freq++;
        }
        ac_get_freq(ac, freq, &val);
    }
    freq = 0;
    dst[0] = 0;
    if (val > 0 && am->buf_size > 0) {
        for (dst[0] = 0; dst[0] < size & freq < val; dst[0]++) {
            if (!am->prob[1][dst[0]])
                freq++;
        }
    }
    if (am->prob[1][dst[0]]) {
        do {
            val = dst[0]++;
        } while (val + 1 < am->buf_size && am->prob[1][val + 1]);
    }
    ac_update(ac, freq, 1);
    am->prob[1][dst[0]]++;
    am->total++;
    amdl_update_prob(am, dst[0], 1);
    am->last = dst[0];

    return 0;
}

static int decode_filt_coeffs(RKAContext *s, ChContext *ctx, ACoder *ac, FiltCoeffs *dst)
{
    unsigned val, bits;
    int idx = 0;

    if (amdl_decode_int(ctx->filt_size, ac, &dst->size, 256) < 0)
        return -1;

    if (dst->size == 0)
        return 0;

    if (amdl_decode_int(ctx->filt_bits, ac, &bits, 10) < 0)
        return -1;

    do {
        if (((idx == 8) || (idx == 20)) && (0 < bits))
            bits--;

        if (bits > 10)
            return -1;

        if (amdl_decode_int(&ctx->coeff_bits[bits], ac, &val, 31) < 0)
            return -1;

        if (val == 31) {
            ac_get_freq(ac, 65536, &val);
            ac_update(ac, val, 1);
        }

        if (val == 0) {
            dst->coeffs[idx++] = 0;
        } else {
            unsigned freq = 0;
            int sign;

            if (bits > 0) {
                ac_get_freq(ac, 1 << bits, &freq);
                ac_update(ac, freq, 1);
            }
            dst->coeffs[idx] = freq + 1 + ((val - 1U) << bits);
            sign = decode_bool(ac, ctx, idx);
            if (sign < 0)
                return -1;
            if (sign == 1)
                dst->coeffs[idx] = -dst->coeffs[idx];
            idx++;
        }
    } while (idx < dst->size);

    return 0;
}

static int ac_dec_bit(ACoder *ac)
{
    uint32_t high, low;

    low = ac->low;
    ac->high = high = ac->high >> 1;
    if (ac->value - low < high) {
        do {
            if (((high + low) ^ low) > 0xffffff) {
                if (high > 0xffff)
                    return 0;
                ac->high = (uint16_t)-(int16_t)low;
            }

            if (bytestream2_get_bytes_left(&ac->gb) <= 0)
                break;

            ac->value = (ac->value << 8) | bytestream2_get_byteu(&ac->gb);
            ac->high = high = ac->high << 8;
            ac->low = low = ac->low << 8;
        } while (1);

        return -1;
    }
    ac->low = low = low + high;
    do {
        if (((high + low) ^ low) > 0xffffff) {
            if (high > 0xffff)
                return 1;
            ac->high = (uint16_t)-(int16_t)low;
        }

        if (bytestream2_get_bytes_left(&ac->gb) <= 0)
            break;

        ac->value = (ac->value << 8) | bytestream2_get_byteu(&ac->gb);
        ac->high = high = ac->high << 8;
        ac->low = low = ac->low << 8;
    } while (1);

    return -1;
}

static int mdl64_decode(ACoder *ac, Model64 *ctx, int *dst)
{
    int sign, idx, bits;
    unsigned val = 0;

    if (ctx->zero[0] + ctx->zero[1] > 4000U) {
        ctx->zero[0] = (ctx->zero[0] >> 1) + 1;
        ctx->zero[1] = (ctx->zero[1] >> 1) + 1;
    }
    if (ctx->sign[0] + ctx->sign[1] > 4000U) {
        ctx->sign[0] = (ctx->sign[0] >> 1) + 1;
        ctx->sign[1] = (ctx->sign[1] >> 1) + 1;
    }
    sign = ac_decode_bool(ac, ctx->zero[0], ctx->zero[1]);
    if (sign == 0) {
        ctx->zero[0] += 2;
        dst[0] = 0;
        return 0;
    } else if (sign < 0) {
        return -1;
    }

    ctx->zero[1] += 2;
    sign = ac_decode_bool(ac, ctx->sign[0], ctx->sign[1]);
    if (sign < 0)
        return -1;
    ctx->sign[sign]++;
    bits = ctx->bits;
    if (bits > 0) {
        if (bits < 13) {
            ac_get_freq(ac, 1 << bits, &val);
            ac_update(ac, val, 1);
        } else {
            int hbits = bits / 2;
            ac_get_freq(ac, 1 << hbits, &val);
            ac_update(ac, val, 1);
            ac_get_freq(ac, 1 << (ctx->bits - (hbits)), &bits);
            ac_update(ac, val, 1);
            val += (bits << hbits);
        }
    }
    bits = ctx->size;
    idx = 0;
    if (bits >= 0) {
        do {
            uint16_t *val4 = ctx->val4;
            int b;

            if (val4[idx] + ctx->val1[idx] > 2000U) {
                val4[idx] = (val4[idx] >> 1) + 1;
                ctx->val1[idx] = (ctx->val1[idx] >> 1) + 1;
            }
            b = ac_decode_bool(ac, ctx->val4[idx], ctx->val1[idx]);
            if (b == 1) {
                ctx->val1[idx] += 4;
                break;
            } else if (b < 0) {
                return -1;
            }
            ctx->val4[idx] += 4;
            idx++;
        } while (idx <= ctx->size);
        bits = ctx->size;
        if (idx <= bits) {
            dst[0] = val + 1 + (idx << ctx->bits);
            if (sign)
                dst[0] = -dst[0];
            return 0;
        }
    }
    bits++;
    while (ac_dec_bit(ac) == 0)
        bits += 64;
    ac_get_freq(ac, 64, &idx);
    ac_update(ac, idx, 1);
    idx += bits;
    dst[0] = val + 1 + (idx << ctx->bits);
    if (sign)
        dst[0] = -dst[0];

    return 0;
}

static const uint8_t vrq_qfactors[8] = { 3, 3, 2, 2, 1, 1, 1, 1 };

static int decode_filter(RKAContext *s, ChContext *ctx, ACoder *ac, int off, unsigned size)
{
    FiltCoeffs filt;
    Model64 *mdl64;
    int split, val, last_val = 0, ret;
    unsigned rsize, idx = 3, bits = 0, m = 0;

    if (ctx->qfactor == 0) {
        if (amdl_decode_int(&ctx->fshift, ac, &bits, 15) < 0)
            return -1;
        bits &= 31U;
    }

    ret = decode_filt_coeffs(s, ctx, ac, &filt);
    if (ret < 0)
        return ret;

    if (size < 512)
        split = size / 2;
    else
        split = size >> 4;

    if (size <= 1)
        return 0;

    for (int x = 0; x < size;) {
        if (amdl_decode_int(&ctx->position, ac, &idx, 10) < 0)
            return -1;

        m = 0;
        idx = (ctx->pos_idx + idx) % 11;
        ctx->pos_idx = idx;

        rsize = FFMIN(split, size - x);
        for (int y = 0; y < rsize; y++, off++) {
            int midx, shift = idx, *src, sum = 16;

            if (off >= FF_ARRAY_ELEMS(ctx->buf0))
                return -1;

            midx = FFABS(last_val) >> shift;
            if (midx >= 15) {
                mdl64 = &ctx->mdl64[3][idx];
            } else if (midx >= 7) {
                mdl64 = &ctx->mdl64[2][idx];
            } else if (midx >= 4) {
                mdl64 = &ctx->mdl64[1][idx];
            } else {
                mdl64 = &ctx->mdl64[0][idx];
            }
            ret = mdl64_decode(ac, mdl64, &val);
            if (ret < 0)
                return -1;
            last_val = val;
            src = &ctx->buf1[off + -1];
            for (int i = 0; i < filt.size && i < 15; i++)
                sum += filt.coeffs[i] * (unsigned)src[-i];
            sum = sum * 2U;
            for (int i = 15; i < filt.size; i++)
                sum += filt.coeffs[i] * (unsigned)src[-i];
            sum = sum >> 6;
            if (ctx->qfactor == 0) {
                if (bits == 0) {
                    ctx->buf1[off] = sum + val;
                } else {
                    ctx->buf1[off] = (val + (sum >> bits)) * (1U << bits) +
                        (((1U << bits) - 1U) & ctx->buf1[off + -1]);
                }
                ctx->buf0[off] = ctx->buf1[off] + (unsigned)ctx->buf0[off + -1];
            } else {
                val *= 1U << ctx->qfactor;
                sum += ctx->buf0[off + -1] + (unsigned)val;
                switch (s->bps) {
                case 16: sum = av_clip_int16(sum); break;
                case  8: sum = av_clip_int8(sum);  break;
                }
                ctx->buf1[off] = sum - ctx->buf0[off + -1];
                ctx->buf0[off] = sum;
                m += (unsigned)FFABS(ctx->buf1[off]);
            }
        }
        if (ctx->vrq != 0) {
            int sum = 0;
            for (unsigned i = (m << 6) / rsize; i > 0; i = i >> 1)
                sum++;
            sum -= (ctx->vrq + 7);
            ctx->qfactor = FFMAX(sum, vrq_qfactors[ctx->vrq - 1]);
        }

        x += split;
    }

    return 0;
}

static int decode_samples(AVCodecContext *avctx, ACoder *ac, ChContext *ctx, int offset)
{
    RKAContext *s = avctx->priv_data;
    int segment_size, offset2, mode, ret;

    ret = amdl_decode_int(&ctx->nb_segments, ac, &mode, 5);
    if (ret < 0)
        return ret;

    if (mode == 5) {
        ret = ac_get_freq(ac, ctx->srate_pad >> 2, &segment_size);
        if (ret < 0)
            return ret;
        ac_update(ac, segment_size, 1);
        segment_size *= 4;
        ret = decode_filter(s, ctx, ac, offset, segment_size);
        if (ret < 0)
            return ret;
    } else {
        segment_size = ctx->srate_pad;

        if (mode) {
            if (mode > 2) {
                ret = decode_filter(s, ctx, ac, offset, segment_size / 4);
                if (ret < 0)
                    return ret;
                offset2 = segment_size / 4 + offset;
                ret = decode_filter(s, ctx, ac, offset2, segment_size / 4);
                if (ret < 0)
                    return ret;
                offset2 = segment_size / 4 + offset2;
            } else {
                ret = decode_filter(s, ctx, ac, offset, segment_size / 2);
                if (ret < 0)
                    return ret;
                offset2 = segment_size / 2 + offset;
            }
            if (mode & 1) {
                ret = decode_filter(s, ctx, ac, offset2, segment_size / 2);
                if (ret < 0)
                    return ret;
            } else {
                ret = decode_filter(s, ctx, ac, offset2, segment_size / 4);
                if (ret < 0)
                    return ret;
                ret = decode_filter(s, ctx, ac, segment_size / 4 + offset2, segment_size / 4);
                if (ret < 0)
                    return ret;
            }
        } else {
            ret = decode_filter(s, ctx, ac, offset, ctx->srate_pad);
            if (ret < 0)
                return ret;
        }
    }

    return segment_size;
}

static int decode_ch_samples(AVCodecContext *avctx, ChContext *c)
{
    RKAContext *s = avctx->priv_data;
    ACoder *ac = &s->ac;
    int nb_decoded = 0;

    if (bytestream2_get_bytes_left(&ac->gb) <= 0)
        return 0;

    memmove(c->buf0, &c->buf0[c->last_nb_decoded], 2560 * sizeof(*c->buf0));
    memmove(c->buf1, &c->buf1[c->last_nb_decoded], 2560 * sizeof(*c->buf1));

    nb_decoded = decode_samples(avctx, ac, c, 2560);
    if (nb_decoded < 0)
        return nb_decoded;
    c->last_nb_decoded = nb_decoded;

    return nb_decoded;
}

static int rka_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                            int *got_frame_ptr, AVPacket *avpkt)
{
    RKAContext *s = avctx->priv_data;
    ACoder *ac = &s->ac;
    int ret;

    bytestream2_init(&ac->gb, avpkt->data, avpkt->size);
    init_acoder(ac);

    for (int ch = 0; ch < s->channels; ch++) {
        ret = chctx_init(s, &s->ch[ch], avctx->sample_rate,
                         avctx->bits_per_raw_sample);
        if (ret < 0)
            return ret;
    }

    frame->nb_samples = s->frame_samples;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    if (s->channels == 2 && s->correlated) {
        int16_t *l16 = (int16_t *)frame->extended_data[0];
        int16_t *r16 = (int16_t *)frame->extended_data[1];
        uint8_t *l8 = frame->extended_data[0];
        uint8_t *r8 = frame->extended_data[1];

        for (int n = 0; n < frame->nb_samples;) {
            ret = decode_ch_samples(avctx, &s->ch[0]);
            if (ret == 0) {
                frame->nb_samples = n;
                break;
            }
            if (ret < 0 || n + ret > frame->nb_samples)
                return AVERROR_INVALIDDATA;

            ret = decode_ch_samples(avctx, &s->ch[1]);
            if (ret == 0) {
                frame->nb_samples = n;
                break;
            }
            if (ret < 0 || n + ret > frame->nb_samples)
                return AVERROR_INVALIDDATA;

            switch (avctx->sample_fmt) {
            case AV_SAMPLE_FMT_S16P:
                for (int i = 0; i < ret; i++) {
                    int l = s->ch[0].buf0[2560 + i];
                    int r = s->ch[1].buf0[2560 + i];

                    l16[n + i] = (l * 2 + r + 1) >> 1;
                    r16[n + i] = (l * 2 - r + 1) >> 1;
                }
                break;
            case AV_SAMPLE_FMT_U8P:
                for (int i = 0; i < ret; i++) {
                    int l = s->ch[0].buf0[2560 + i];
                    int r = s->ch[1].buf0[2560 + i];

                    l8[n + i] = ((l * 2 + r + 1) >> 1) + 0x7f;
                    r8[n + i] = ((l * 2 - r + 1) >> 1) + 0x7f;
                }
                break;
            default:
                return AVERROR_INVALIDDATA;
            }

            n += ret;
        }
    } else {
        for (int n = 0; n < frame->nb_samples;) {
            for (int ch = 0; ch < s->channels; ch++) {
                int16_t *m16 = (int16_t *)frame->data[ch];
                uint8_t *m8 = frame->data[ch];

                ret = decode_ch_samples(avctx, &s->ch[ch]);
                if (ret == 0) {
                    frame->nb_samples = n;
                    break;
                }

                if (ret < 0 || n + ret > frame->nb_samples)
                    return AVERROR_INVALIDDATA;

                switch (avctx->sample_fmt) {
                case AV_SAMPLE_FMT_S16P:
                    for (int i = 0; i < ret; i++) {
                        int m = s->ch[ch].buf0[2560 + i];

                        m16[n + i] = m;
                    }
                    break;
                case AV_SAMPLE_FMT_U8P:
                    for (int i = 0; i < ret; i++) {
                        int m = s->ch[ch].buf0[2560 + i];

                        m8[n + i] = m + 0x7f;
                    }
                    break;
                default:
                    return AVERROR_INVALIDDATA;
                }
            }

            n += ret;
        }
    }

    if (frame->nb_samples < s->frame_samples &&
        frame->nb_samples > s->last_nb_samples)
        frame->nb_samples = s->last_nb_samples;

    *got_frame_ptr = 1;

    return avpkt->size;
}

static av_cold int rka_decode_close(AVCodecContext *avctx)
{
    RKAContext *s = avctx->priv_data;

    for (int ch = 0; ch < 2; ch++) {
        ChContext *c = &s->ch[ch];

        for (int i = 0; i < 11; i++)
            adaptive_model_free(&c->coeff_bits[i]);

        adaptive_model_free(&c->position);
        adaptive_model_free(&c->nb_segments);
        adaptive_model_free(&c->fshift);
    }

    adaptive_model_free(&s->filt_size);
    adaptive_model_free(&s->filt_bits);

    return 0;
}

const FFCodec ff_rka_decoder = {
    .p.name         = "rka",
    CODEC_LONG_NAME("RKA (RK Audio)"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_RKA,
    .priv_data_size = sizeof(RKAContext),
    .init           = rka_decode_init,
    .close          = rka_decode_close,
    FF_CODEC_DECODE_CB(rka_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
