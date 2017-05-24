/*
 * WavPack lossless audio encoder
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

#define BITSTREAM_WRITER_LE

#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "internal.h"
#include "put_bits.h"
#include "bytestream.h"
#include "wavpackenc.h"
#include "wavpack.h"

#define UPDATE_WEIGHT(weight, delta, source, result) \
    if ((source) && (result)) { \
        int32_t s = (int32_t) ((source) ^ (result)) >> 31; \
        weight = ((delta) ^ s) + ((weight) - s); \
    }

#define APPLY_WEIGHT_F(weight, sample) ((((((sample) & 0xffff) * (weight)) >> 9) + \
    ((((sample) & ~0xffff) >> 9) * (weight)) + 1) >> 1)

#define APPLY_WEIGHT_I(weight, sample) (((weight) * (sample) + 512) >> 10)

#define APPLY_WEIGHT(weight, sample) ((sample) != (short) (sample) ? \
    APPLY_WEIGHT_F(weight, sample) : APPLY_WEIGHT_I (weight, sample))

#define CLEAR(destin) memset(&destin, 0, sizeof(destin));

#define SHIFT_LSB       13
#define SHIFT_MASK      (0x1FU << SHIFT_LSB)

#define MAG_LSB         18
#define MAG_MASK        (0x1FU << MAG_LSB)

#define SRATE_LSB       23
#define SRATE_MASK      (0xFU << SRATE_LSB)

#define EXTRA_TRY_DELTAS     1
#define EXTRA_ADJUST_DELTAS  2
#define EXTRA_SORT_FIRST     4
#define EXTRA_BRANCHES       8
#define EXTRA_SORT_LAST     16

typedef struct WavPackExtraInfo {
    struct Decorr dps[MAX_TERMS];
    int nterms, log_limit, gt16bit;
    uint32_t best_bits;
} WavPackExtraInfo;

typedef struct WavPackWords {
    int pend_data, holding_one, zeros_acc;
    int holding_zero, pend_count;
    WvChannel c[2];
} WavPackWords;

typedef struct WavPackEncodeContext {
    AVClass *class;
    AVCodecContext *avctx;
    PutBitContext pb;
    int block_samples;
    int buffer_size;
    int sample_index;
    int stereo, stereo_in;
    int ch_offset;

    int32_t *samples[2];
    int samples_size[2];

    int32_t *sampleptrs[MAX_TERMS+2][2];
    int sampleptrs_size[MAX_TERMS+2][2];

    int32_t *temp_buffer[2][2];
    int temp_buffer_size[2][2];

    int32_t *best_buffer[2];
    int best_buffer_size[2];

    int32_t *js_left, *js_right;
    int js_left_size, js_right_size;

    int32_t *orig_l, *orig_r;
    int orig_l_size, orig_r_size;

    unsigned extra_flags;
    int optimize_mono;
    int decorr_filter;
    int joint;
    int num_branches;

    uint32_t flags;
    uint32_t crc_x;
    WavPackWords w;

    uint8_t int32_sent_bits, int32_zeros, int32_ones, int32_dups;
    uint8_t float_flags, float_shift, float_max_exp, max_exp;
    int32_t shifted_ones, shifted_zeros, shifted_both;
    int32_t false_zeros, neg_zeros, ordata;

    int num_terms, shift, joint_stereo, false_stereo;
    int num_decorrs, num_passes, best_decorr, mask_decorr;
    struct Decorr decorr_passes[MAX_TERMS];
    const WavPackDecorrSpec *decorr_specs;
    float delta_decay;
} WavPackEncodeContext;

static av_cold int wavpack_encode_init(AVCodecContext *avctx)
{
    WavPackEncodeContext *s = avctx->priv_data;

    s->avctx = avctx;

    if (avctx->channels > 255) {
        av_log(avctx, AV_LOG_ERROR, "Invalid channel count: %d\n", avctx->channels);
        return AVERROR(EINVAL);
    }

    if (!avctx->frame_size) {
        int block_samples;
        if (!(avctx->sample_rate & 1))
            block_samples = avctx->sample_rate / 2;
        else
            block_samples = avctx->sample_rate;

        while (block_samples * avctx->channels > WV_MAX_SAMPLES)
            block_samples /= 2;

        while (block_samples * avctx->channels < 40000)
            block_samples *= 2;
        avctx->frame_size = block_samples;
    } else if (avctx->frame_size && (avctx->frame_size < 128 ||
                              avctx->frame_size > WV_MAX_SAMPLES)) {
        av_log(avctx, AV_LOG_ERROR, "invalid block size: %d\n", avctx->frame_size);
        return AVERROR(EINVAL);
    }

    if (avctx->compression_level != FF_COMPRESSION_DEFAULT) {
        if (avctx->compression_level >= 3) {
            s->decorr_filter = 3;
            s->num_passes = 9;
            if      (avctx->compression_level >= 8) {
                s->num_branches = 4;
                s->extra_flags = EXTRA_TRY_DELTAS|EXTRA_ADJUST_DELTAS|EXTRA_SORT_FIRST|EXTRA_SORT_LAST|EXTRA_BRANCHES;
            } else if (avctx->compression_level >= 7) {
                s->num_branches = 3;
                s->extra_flags = EXTRA_TRY_DELTAS|EXTRA_ADJUST_DELTAS|EXTRA_SORT_FIRST|EXTRA_BRANCHES;
            } else if (avctx->compression_level >= 6) {
                s->num_branches = 2;
                s->extra_flags = EXTRA_TRY_DELTAS|EXTRA_ADJUST_DELTAS|EXTRA_SORT_FIRST|EXTRA_BRANCHES;
            } else if (avctx->compression_level >= 5) {
                s->num_branches = 1;
                s->extra_flags = EXTRA_TRY_DELTAS|EXTRA_ADJUST_DELTAS|EXTRA_SORT_FIRST|EXTRA_BRANCHES;
            } else if (avctx->compression_level >= 4) {
                s->num_branches = 1;
                s->extra_flags = EXTRA_TRY_DELTAS|EXTRA_ADJUST_DELTAS|EXTRA_BRANCHES;
            }
        } else if (avctx->compression_level == 2) {
            s->decorr_filter = 2;
            s->num_passes = 4;
        } else if (avctx->compression_level == 1) {
            s->decorr_filter = 1;
            s->num_passes = 2;
        } else if (avctx->compression_level < 1) {
            s->decorr_filter = 0;
            s->num_passes = 0;
        }
    }

    s->num_decorrs = decorr_filter_sizes[s->decorr_filter];
    s->decorr_specs = decorr_filters[s->decorr_filter];

    s->delta_decay = 2.0;

    return 0;
}

static void shift_mono(int32_t *samples, int nb_samples, int shift)
{
    int i;
    for (i = 0; i < nb_samples; i++)
        samples[i] >>= shift;
}

static void shift_stereo(int32_t *left, int32_t *right,
                         int nb_samples, int shift)
{
    int i;
    for (i = 0; i < nb_samples; i++) {
        left [i] >>= shift;
        right[i] >>= shift;
    }
}

#define FLOAT_SHIFT_ONES 1
#define FLOAT_SHIFT_SAME 2
#define FLOAT_SHIFT_SENT 4
#define FLOAT_ZEROS_SENT 8
#define FLOAT_NEG_ZEROS  0x10
#define FLOAT_EXCEPTIONS 0x20

#define get_mantissa(f)     ((f) & 0x7fffff)
#define get_exponent(f)     (((f) >> 23) & 0xff)
#define get_sign(f)         (((f) >> 31) & 0x1)

static void process_float(WavPackEncodeContext *s, int32_t *sample)
{
    int32_t shift_count, value, f = *sample;

    if (get_exponent(f) == 255) {
        s->float_flags |= FLOAT_EXCEPTIONS;
        value = 0x1000000;
        shift_count = 0;
    } else if (get_exponent(f)) {
        shift_count = s->max_exp - get_exponent(f);
        value = 0x800000 + get_mantissa(f);
    } else {
        shift_count = s->max_exp ? s->max_exp - 1 : 0;
        value = get_mantissa(f);
    }

    if (shift_count < 25)
        value >>= shift_count;
    else
        value = 0;

    if (!value) {
        if (get_exponent(f) || get_mantissa(f))
            s->false_zeros++;
        else if (get_sign(f))
            s->neg_zeros++;
    } else if (shift_count) {
        int32_t mask = (1 << shift_count) - 1;

        if (!(get_mantissa(f) & mask))
            s->shifted_zeros++;
        else if ((get_mantissa(f) & mask) == mask)
            s->shifted_ones++;
        else
            s->shifted_both++;
    }

    s->ordata |= value;
    *sample = get_sign(f) ? -value : value;
}

static int scan_float(WavPackEncodeContext *s,
                      int32_t *samples_l, int32_t *samples_r,
                      int nb_samples)
{
    uint32_t crc = 0xffffffffu;
    int i;

    s->shifted_ones = s->shifted_zeros = s->shifted_both = s->ordata = 0;
    s->float_shift = s->float_flags = 0;
    s->false_zeros = s->neg_zeros = 0;
    s->max_exp = 0;

    if (s->flags & WV_MONO_DATA) {
        for (i = 0; i < nb_samples; i++) {
            int32_t f = samples_l[i];
            crc = crc * 27 + get_mantissa(f) * 9 + get_exponent(f) * 3 + get_sign(f);

            if (get_exponent(f) > s->max_exp && get_exponent(f) < 255)
                s->max_exp = get_exponent(f);
        }
    } else {
        for (i = 0; i < nb_samples; i++) {
            int32_t f;

            f = samples_l[i];
            crc = crc * 27 + get_mantissa(f) * 9 + get_exponent(f) * 3 + get_sign(f);
            if (get_exponent(f) > s->max_exp && get_exponent(f) < 255)
                s->max_exp = get_exponent(f);

            f = samples_r[i];
            crc = crc * 27 + get_mantissa(f) * 9 + get_exponent(f) * 3 + get_sign(f);

            if (get_exponent(f) > s->max_exp && get_exponent(f) < 255)
                s->max_exp = get_exponent(f);
        }
    }

    s->crc_x = crc;

    if (s->flags & WV_MONO_DATA) {
        for (i = 0; i < nb_samples; i++)
            process_float(s, &samples_l[i]);
    } else {
        for (i = 0; i < nb_samples; i++) {
            process_float(s, &samples_l[i]);
            process_float(s, &samples_r[i]);
        }
    }

    s->float_max_exp = s->max_exp;

    if (s->shifted_both)
        s->float_flags |= FLOAT_SHIFT_SENT;
    else if (s->shifted_ones && !s->shifted_zeros)
        s->float_flags |= FLOAT_SHIFT_ONES;
    else if (s->shifted_ones && s->shifted_zeros)
        s->float_flags |= FLOAT_SHIFT_SAME;
    else if (s->ordata && !(s->ordata & 1)) {
        do {
            s->float_shift++;
            s->ordata >>= 1;
        } while (!(s->ordata & 1));

        if (s->flags & WV_MONO_DATA)
            shift_mono(samples_l, nb_samples, s->float_shift);
        else
            shift_stereo(samples_l, samples_r, nb_samples, s->float_shift);
    }

    s->flags &= ~MAG_MASK;

    while (s->ordata) {
        s->flags += 1 << MAG_LSB;
        s->ordata >>= 1;
    }

    if (s->false_zeros || s->neg_zeros)
        s->float_flags |= FLOAT_ZEROS_SENT;

    if (s->neg_zeros)
        s->float_flags |= FLOAT_NEG_ZEROS;

    return s->float_flags & (FLOAT_EXCEPTIONS | FLOAT_ZEROS_SENT |
                             FLOAT_SHIFT_SENT | FLOAT_SHIFT_SAME);
}

static void scan_int23(WavPackEncodeContext *s,
                       int32_t *samples_l, int32_t *samples_r,
                       int nb_samples)
{
    uint32_t magdata = 0, ordata = 0, xordata = 0, anddata = ~0;
    int i, total_shift = 0;

    s->int32_sent_bits = s->int32_zeros = s->int32_ones = s->int32_dups = 0;

    if (s->flags & WV_MONO_DATA) {
        for (i = 0; i < nb_samples; i++) {
            int32_t M = samples_l[i];

            magdata |= (M < 0) ? ~M : M;
            xordata |= M ^ -(M & 1);
            anddata &= M;
            ordata  |= M;

            if ((ordata & 1) && !(anddata & 1) && (xordata & 2))
                return;
        }
    } else {
        for (i = 0; i < nb_samples; i++) {
            int32_t L = samples_l[i];
            int32_t R = samples_r[i];

            magdata |= (L < 0) ? ~L : L;
            magdata |= (R < 0) ? ~R : R;
            xordata |= L ^ -(L & 1);
            xordata |= R ^ -(R & 1);
            anddata &= L & R;
            ordata  |= L | R;

            if ((ordata & 1) && !(anddata & 1) && (xordata & 2))
                return;
        }
    }

    s->flags &= ~MAG_MASK;

    while (magdata) {
        s->flags += 1 << MAG_LSB;
        magdata >>= 1;
    }

    if (!(s->flags & MAG_MASK))
        return;

    if (!(ordata & 1)) {
        do {
            s->flags -= 1 << MAG_LSB;
            s->int32_zeros++;
            total_shift++;
            ordata >>= 1;
        } while (!(ordata & 1));
    } else if (anddata & 1) {
        do {
            s->flags -= 1 << MAG_LSB;
            s->int32_ones++;
            total_shift++;
            anddata >>= 1;
        } while (anddata & 1);
    } else if (!(xordata & 2)) {
        do {
            s->flags -= 1 << MAG_LSB;
            s->int32_dups++;
            total_shift++;
            xordata >>= 1;
        } while (!(xordata & 2));
    }

    if (total_shift) {
        s->flags |= WV_INT32_DATA;

        if (s->flags & WV_MONO_DATA)
            shift_mono(samples_l, nb_samples, total_shift);
        else
            shift_stereo(samples_l, samples_r, nb_samples, total_shift);
    }
}

static int scan_int32(WavPackEncodeContext *s,
                      int32_t *samples_l, int32_t *samples_r,
                      int nb_samples)
{
    uint32_t magdata = 0, ordata = 0, xordata = 0, anddata = ~0;
    uint32_t crc = 0xffffffffu;
    int i, total_shift = 0;

    s->int32_sent_bits = s->int32_zeros = s->int32_ones = s->int32_dups = 0;

    if (s->flags & WV_MONO_DATA) {
        for (i = 0; i < nb_samples; i++) {
            int32_t M = samples_l[i];

            crc = crc * 9 + (M & 0xffff) * 3 + ((M >> 16) & 0xffff);
            magdata |= (M < 0) ? ~M : M;
            xordata |= M ^ -(M & 1);
            anddata &= M;
            ordata  |= M;
        }
    } else {
        for (i = 0; i < nb_samples; i++) {
            int32_t L = samples_l[i];
            int32_t R = samples_r[i];

            crc = crc * 9 + (L & 0xffff) * 3 + ((L >> 16) & 0xffff);
            crc = crc * 9 + (R & 0xffff) * 3 + ((R >> 16) & 0xffff);
            magdata |= (L < 0) ? ~L : L;
            magdata |= (R < 0) ? ~R : R;
            xordata |= L ^ -(L & 1);
            xordata |= R ^ -(R & 1);
            anddata &= L & R;
            ordata  |= L | R;
        }
    }

    s->crc_x = crc;
    s->flags &= ~MAG_MASK;

    while (magdata) {
        s->flags += 1 << MAG_LSB;
        magdata >>= 1;
    }

    if (!((s->flags & MAG_MASK) >> MAG_LSB)) {
        s->flags &= ~WV_INT32_DATA;
        return 0;
    }

    if (!(ordata & 1))
        do {
            s->flags -= 1 << MAG_LSB;
            s->int32_zeros++;
            total_shift++;
            ordata >>= 1;
        } while (!(ordata & 1));
    else if (anddata & 1)
        do {
            s->flags -= 1 << MAG_LSB;
            s->int32_ones++;
            total_shift++;
            anddata >>= 1;
        } while (anddata & 1);
    else if (!(xordata & 2))
        do {
            s->flags -= 1 << MAG_LSB;
            s->int32_dups++;
            total_shift++;
            xordata >>= 1;
        } while (!(xordata & 2));

    if (((s->flags & MAG_MASK) >> MAG_LSB) > 23) {
        s->int32_sent_bits = (uint8_t)(((s->flags & MAG_MASK) >> MAG_LSB) - 23);
        total_shift += s->int32_sent_bits;
        s->flags &= ~MAG_MASK;
        s->flags += 23 << MAG_LSB;
    }

    if (total_shift) {
        s->flags |= WV_INT32_DATA;

        if (s->flags & WV_MONO_DATA)
            shift_mono(samples_l, nb_samples, total_shift);
        else
            shift_stereo(samples_l, samples_r, nb_samples, total_shift);
    }

    return s->int32_sent_bits;
}

static int8_t store_weight(int weight)
{
    weight = av_clip(weight, -1024, 1024);
    if (weight > 0)
        weight -= (weight + 64) >> 7;

    return (weight + 4) >> 3;
}

static int restore_weight(int8_t weight)
{
    int result;

    if ((result = (int) weight << 3) > 0)
        result += (result + 64) >> 7;

    return result;
}

static int log2s(int32_t value)
{
    return (value < 0) ? -wp_log2(-value) : wp_log2(value);
}

static void decorr_mono(int32_t *in_samples, int32_t *out_samples,
                        int nb_samples, struct Decorr *dpp, int dir)
{
    int m = 0, i;

    dpp->sumA = 0;

    if (dir < 0) {
        out_samples += (nb_samples - 1);
        in_samples  += (nb_samples - 1);
    }

    dpp->weightA = restore_weight(store_weight(dpp->weightA));

    for (i = 0; i < MAX_TERM; i++)
        dpp->samplesA[i] = wp_exp2(log2s(dpp->samplesA[i]));

    if (dpp->value > MAX_TERM) {
        while (nb_samples--) {
            int32_t left, sam_A;

            sam_A = ((3 - (dpp->value & 1)) * dpp->samplesA[0] - dpp->samplesA[1]) >> !(dpp->value & 1);

            dpp->samplesA[1] = dpp->samplesA[0];
            dpp->samplesA[0] = left = in_samples[0];

            left -= APPLY_WEIGHT(dpp->weightA, sam_A);
            UPDATE_WEIGHT(dpp->weightA, dpp->delta, sam_A, left);
            dpp->sumA += dpp->weightA;
            out_samples[0] = left;
            in_samples += dir;
            out_samples += dir;
        }
    } else if (dpp->value > 0) {
        while (nb_samples--) {
            int k = (m + dpp->value) & (MAX_TERM - 1);
            int32_t left, sam_A;

            sam_A = dpp->samplesA[m];
            dpp->samplesA[k] = left = in_samples[0];
            m = (m + 1) & (MAX_TERM - 1);

            left -= APPLY_WEIGHT(dpp->weightA, sam_A);
            UPDATE_WEIGHT(dpp->weightA, dpp->delta, sam_A, left);
            dpp->sumA += dpp->weightA;
            out_samples[0] = left;
            in_samples += dir;
            out_samples += dir;
        }
    }

    if (m && dpp->value > 0 && dpp->value <= MAX_TERM) {
        int32_t temp_A[MAX_TERM];

        memcpy(temp_A, dpp->samplesA, sizeof(dpp->samplesA));

        for (i = 0; i < MAX_TERM; i++) {
            dpp->samplesA[i] = temp_A[m];
            m = (m + 1) & (MAX_TERM - 1);
        }
    }
}

static void reverse_mono_decorr(struct Decorr *dpp)
{
    if (dpp->value > MAX_TERM) {
        int32_t sam_A;

        if (dpp->value & 1)
            sam_A = 2 * dpp->samplesA[0] - dpp->samplesA[1];
        else
            sam_A = (3 * dpp->samplesA[0] - dpp->samplesA[1]) >> 1;

        dpp->samplesA[1] = dpp->samplesA[0];
        dpp->samplesA[0] = sam_A;

        if (dpp->value & 1)
            sam_A = 2 * dpp->samplesA[0] - dpp->samplesA[1];
        else
            sam_A = (3 * dpp->samplesA[0] - dpp->samplesA[1]) >> 1;

        dpp->samplesA[1] = sam_A;
    } else if (dpp->value > 1) {
        int i, j, k;

        for (i = 0, j = dpp->value - 1, k = 0; k < dpp->value / 2; i++, j--, k++) {
            i &= (MAX_TERM - 1);
            j &= (MAX_TERM - 1);
            dpp->samplesA[i] ^= dpp->samplesA[j];
            dpp->samplesA[j] ^= dpp->samplesA[i];
            dpp->samplesA[i] ^= dpp->samplesA[j];
        }
    }
}

static uint32_t log2sample(uint32_t v, int limit, uint32_t *result)
{
    uint32_t dbits;

    if ((v += v >> 9) < (1 << 8)) {
        dbits = nbits_table[v];
        *result += (dbits << 8) + wp_log2_table[(v << (9 - dbits)) & 0xff];
    } else {
        if (v < (1 << 16))
            dbits = nbits_table[v >> 8] + 8;
        else if (v < (1 << 24))
            dbits = nbits_table[v >> 16] + 16;
        else
            dbits = nbits_table[v >> 24] + 24;

        *result += dbits = (dbits << 8) + wp_log2_table[(v >> (dbits - 9)) & 0xff];

        if (limit && dbits >= limit)
            return 1;
    }

    return 0;
}

static uint32_t log2mono(int32_t *samples, int nb_samples, int limit)
{
    uint32_t result = 0;
    while (nb_samples--) {
        if (log2sample(abs(*samples++), limit, &result))
            return UINT32_MAX;
    }
    return result;
}

static uint32_t log2stereo(int32_t *samples_l, int32_t *samples_r,
                           int nb_samples, int limit)
{
    uint32_t result = 0;
    while (nb_samples--) {
        if (log2sample(abs(*samples_l++), limit, &result) ||
            log2sample(abs(*samples_r++), limit, &result))
            return UINT32_MAX;
    }
    return result;
}

static void decorr_mono_buffer(int32_t *samples, int32_t *outsamples,
                               int nb_samples, struct Decorr *dpp,
                               int tindex)
{
    struct Decorr dp, *dppi = dpp + tindex;
    int delta = dppi->delta, pre_delta, term = dppi->value;

    if (delta == 7)
        pre_delta = 7;
    else if (delta < 2)
        pre_delta = 3;
    else
        pre_delta = delta + 1;

    CLEAR(dp);
    dp.value = term;
    dp.delta = pre_delta;
    decorr_mono(samples, outsamples, FFMIN(2048, nb_samples), &dp, -1);
    dp.delta = delta;

    if (tindex == 0)
        reverse_mono_decorr(&dp);
    else
        CLEAR(dp.samplesA);

    memcpy(dppi->samplesA, dp.samplesA, sizeof(dp.samplesA));
    dppi->weightA = dp.weightA;

    if (delta == 0) {
        dp.delta = 1;
        decorr_mono(samples, outsamples, nb_samples, &dp, 1);
        dp.delta = 0;
        memcpy(dp.samplesA, dppi->samplesA, sizeof(dp.samplesA));
        dppi->weightA = dp.weightA = dp.sumA / nb_samples;
    }

    decorr_mono(samples, outsamples, nb_samples, &dp, 1);
}

static void recurse_mono(WavPackEncodeContext *s, WavPackExtraInfo *info,
                         int depth, int delta, uint32_t input_bits)
{
    int term, branches = s->num_branches - depth;
    int32_t *samples, *outsamples;
    uint32_t term_bits[22], bits;

    if (branches < 1 || depth + 1 == info->nterms)
        branches = 1;

    CLEAR(term_bits);
    samples = s->sampleptrs[depth][0];
    outsamples = s->sampleptrs[depth + 1][0];

    for (term = 1; term <= 18; term++) {
        if (term == 17 && branches == 1 && depth + 1 < info->nterms)
            continue;

        if (term > 8 && term < 17)
            continue;

        if (!s->extra_flags && (term > 4 && term < 17))
            continue;

        info->dps[depth].value = term;
        info->dps[depth].delta = delta;
        decorr_mono_buffer(samples, outsamples, s->block_samples, info->dps, depth);
        bits = log2mono(outsamples, s->block_samples, info->log_limit);

        if (bits < info->best_bits) {
            info->best_bits = bits;
            CLEAR(s->decorr_passes);
            memcpy(s->decorr_passes, info->dps, sizeof(info->dps[0]) * (depth + 1));
            memcpy(s->sampleptrs[info->nterms + 1][0],
                   s->sampleptrs[depth + 1][0], s->block_samples * 4);
        }

        term_bits[term + 3] = bits;
    }

    while (depth + 1 < info->nterms && branches--) {
        uint32_t local_best_bits = input_bits;
        int best_term = 0, i;

        for (i = 0; i < 22; i++)
            if (term_bits[i] && term_bits[i] < local_best_bits) {
                local_best_bits = term_bits[i];
                best_term = i - 3;
            }

        if (!best_term)
            break;

        term_bits[best_term + 3] = 0;

        info->dps[depth].value = best_term;
        info->dps[depth].delta = delta;
        decorr_mono_buffer(samples, outsamples, s->block_samples, info->dps, depth);

        recurse_mono(s, info, depth + 1, delta, local_best_bits);
    }
}

static void sort_mono(WavPackEncodeContext *s, WavPackExtraInfo *info)
{
    int reversed = 1;
    uint32_t bits;

    while (reversed) {
        int ri, i;

        memcpy(info->dps, s->decorr_passes, sizeof(s->decorr_passes));
        reversed = 0;

        for (ri = 0; ri < info->nterms && s->decorr_passes[ri].value; ri++) {

            if (ri + 1 >= info->nterms || !s->decorr_passes[ri+1].value)
                break;

            if (s->decorr_passes[ri].value == s->decorr_passes[ri+1].value) {
                decorr_mono_buffer(s->sampleptrs[ri][0], s->sampleptrs[ri+1][0],
                                   s->block_samples, info->dps, ri);
                continue;
            }

            info->dps[ri  ] = s->decorr_passes[ri+1];
            info->dps[ri+1] = s->decorr_passes[ri  ];

            for (i = ri; i < info->nterms && s->decorr_passes[i].value; i++)
                decorr_mono_buffer(s->sampleptrs[i][0], s->sampleptrs[i+1][0],
                                   s->block_samples, info->dps, i);

            bits = log2mono(s->sampleptrs[i][0], s->block_samples, info->log_limit);
            if (bits < info->best_bits) {
                reversed = 1;
                info->best_bits = bits;
                CLEAR(s->decorr_passes);
                memcpy(s->decorr_passes, info->dps, sizeof(info->dps[0]) * i);
                memcpy(s->sampleptrs[info->nterms + 1][0], s->sampleptrs[i][0],
                       s->block_samples * 4);
            } else {
                info->dps[ri  ] = s->decorr_passes[ri];
                info->dps[ri+1] = s->decorr_passes[ri+1];
                decorr_mono_buffer(s->sampleptrs[ri][0], s->sampleptrs[ri+1][0],
                                   s->block_samples, info->dps, ri);
            }
        }
    }
}

static void delta_mono(WavPackEncodeContext *s, WavPackExtraInfo *info)
{
    int lower = 0, delta, d;
    uint32_t bits;

    if (!s->decorr_passes[0].value)
        return;
    delta = s->decorr_passes[0].delta;

    for (d = delta - 1; d >= 0; d--) {
        int i;

        for (i = 0; i < info->nterms && s->decorr_passes[i].value; i++) {
            info->dps[i].value = s->decorr_passes[i].value;
            info->dps[i].delta = d;
            decorr_mono_buffer(s->sampleptrs[i][0], s->sampleptrs[i+1][0],
                               s->block_samples, info->dps, i);
        }

        bits = log2mono(s->sampleptrs[i][0], s->block_samples, info->log_limit);
        if (bits >= info->best_bits)
            break;

        lower = 1;
        info->best_bits = bits;
        CLEAR(s->decorr_passes);
        memcpy(s->decorr_passes, info->dps, sizeof(info->dps[0]) * i);
        memcpy(s->sampleptrs[info->nterms + 1][0],  s->sampleptrs[i][0],
               s->block_samples * 4);
    }

    for (d = delta + 1; !lower && d <= 7; d++) {
        int i;

        for (i = 0; i < info->nterms && s->decorr_passes[i].value; i++) {
            info->dps[i].value = s->decorr_passes[i].value;
            info->dps[i].delta = d;
            decorr_mono_buffer(s->sampleptrs[i][0], s->sampleptrs[i+1][0],
                               s->block_samples, info->dps, i);
        }

        bits = log2mono(s->sampleptrs[i][0], s->block_samples, info->log_limit);
        if (bits >= info->best_bits)
            break;

        info->best_bits = bits;
        CLEAR(s->decorr_passes);
        memcpy(s->decorr_passes, info->dps, sizeof(info->dps[0]) * i);
        memcpy(s->sampleptrs[info->nterms + 1][0], s->sampleptrs[i][0],
               s->block_samples * 4);
    }
}

static int allocate_buffers2(WavPackEncodeContext *s, int nterms)
{
    int i;

    for (i = 0; i < nterms + 2; i++) {
        av_fast_padded_malloc(&s->sampleptrs[i][0], &s->sampleptrs_size[i][0],
                              s->block_samples * 4);
        if (!s->sampleptrs[i][0])
            return AVERROR(ENOMEM);
        if (!(s->flags & WV_MONO_DATA)) {
            av_fast_padded_malloc(&s->sampleptrs[i][1], &s->sampleptrs_size[i][1],
                                  s->block_samples * 4);
            if (!s->sampleptrs[i][1])
                return AVERROR(ENOMEM);
        }
    }

    return 0;
}

static int allocate_buffers(WavPackEncodeContext *s)
{
    int i;

    for (i = 0; i < 2; i++) {
        av_fast_padded_malloc(&s->best_buffer[0], &s->best_buffer_size[0],
                              s->block_samples * 4);
        if (!s->best_buffer[0])
            return AVERROR(ENOMEM);

        av_fast_padded_malloc(&s->temp_buffer[i][0], &s->temp_buffer_size[i][0],
                              s->block_samples * 4);
        if (!s->temp_buffer[i][0])
            return AVERROR(ENOMEM);
        if (!(s->flags & WV_MONO_DATA)) {
            av_fast_padded_malloc(&s->best_buffer[1], &s->best_buffer_size[1],
                                  s->block_samples * 4);
            if (!s->best_buffer[1])
                return AVERROR(ENOMEM);

            av_fast_padded_malloc(&s->temp_buffer[i][1], &s->temp_buffer_size[i][1],
                                  s->block_samples * 4);
            if (!s->temp_buffer[i][1])
                return AVERROR(ENOMEM);
        }
    }

    return 0;
}

static void analyze_mono(WavPackEncodeContext *s, int32_t *samples, int do_samples)
{
    WavPackExtraInfo info;
    int i;

    info.log_limit = (((s->flags & MAG_MASK) >> MAG_LSB) + 4) * 256;
    info.log_limit = FFMIN(6912, info.log_limit);

    info.nterms = s->num_terms;

    if (allocate_buffers2(s, s->num_terms))
        return;

    memcpy(info.dps, s->decorr_passes, sizeof(info.dps));
    memcpy(s->sampleptrs[0][0], samples, s->block_samples * 4);

    for (i = 0; i < info.nterms && info.dps[i].value; i++)
        decorr_mono(s->sampleptrs[i][0], s->sampleptrs[i + 1][0],
                    s->block_samples, info.dps + i, 1);

    info.best_bits = log2mono(s->sampleptrs[info.nterms][0], s->block_samples, 0) * 1;
    memcpy(s->sampleptrs[info.nterms + 1][0], s->sampleptrs[i][0], s->block_samples * 4);

    if (s->extra_flags & EXTRA_BRANCHES)
        recurse_mono(s, &info, 0, (int) floor(s->delta_decay + 0.5),
                     log2mono(s->sampleptrs[0][0], s->block_samples, 0));

    if (s->extra_flags & EXTRA_SORT_FIRST)
        sort_mono(s, &info);

    if (s->extra_flags & EXTRA_TRY_DELTAS) {
        delta_mono(s, &info);

        if ((s->extra_flags & EXTRA_ADJUST_DELTAS) && s->decorr_passes[0].value)
            s->delta_decay = (float)((s->delta_decay * 2.0 + s->decorr_passes[0].delta) / 3.0);
        else
            s->delta_decay = 2.0;
    }

    if (s->extra_flags & EXTRA_SORT_LAST)
        sort_mono(s, &info);

    if (do_samples)
        memcpy(samples, s->sampleptrs[info.nterms + 1][0], s->block_samples * 4);

    for (i = 0; i < info.nterms; i++)
        if (!s->decorr_passes[i].value)
            break;

    s->num_terms = i;
}

static void scan_word(WavPackEncodeContext *s, WvChannel *c,
                      int32_t *samples, int nb_samples, int dir)
{
    if (dir < 0)
        samples += nb_samples - 1;

    while (nb_samples--) {
        uint32_t low, value = labs(samples[0]);

        if (value < GET_MED(0)) {
            DEC_MED(0);
        } else {
            low = GET_MED(0);
            INC_MED(0);

            if (value - low < GET_MED(1)) {
                DEC_MED(1);
            } else {
                low += GET_MED(1);
                INC_MED(1);

                if (value - low < GET_MED(2)) {
                    DEC_MED(2);
                } else {
                    INC_MED(2);
                }
            }
        }
        samples += dir;
    }
}

static int wv_mono(WavPackEncodeContext *s, int32_t *samples,
                   int no_history, int do_samples)
{
    struct Decorr temp_decorr_pass, save_decorr_passes[MAX_TERMS] = {{0}};
    int nb_samples = s->block_samples;
    int buf_size = sizeof(int32_t) * nb_samples;
    uint32_t best_size = UINT32_MAX, size;
    int log_limit, pi, i, ret;

    for (i = 0; i < nb_samples; i++)
        if (samples[i])
            break;

    if (i == nb_samples) {
        CLEAR(s->decorr_passes);
        CLEAR(s->w);
        s->num_terms = 0;
        return 0;
    }

    log_limit = (((s->flags & MAG_MASK) >> MAG_LSB) + 4) * 256;
    log_limit = FFMIN(6912, log_limit);

    if ((ret = allocate_buffers(s)) < 0)
        return ret;

    if (no_history || s->num_passes >= 7)
        s->best_decorr = s->mask_decorr = 0;

    for (pi = 0; pi < s->num_passes;) {
        const WavPackDecorrSpec *wpds;
        int nterms, c, j;

        if (!pi) {
            c = s->best_decorr;
        } else {
            if (s->mask_decorr == 0)
                c = 0;
            else
                c = (s->best_decorr & (s->mask_decorr - 1)) | s->mask_decorr;

            if (c == s->best_decorr) {
                s->mask_decorr = s->mask_decorr ? ((s->mask_decorr << 1) & (s->num_decorrs - 1)) : 1;
                continue;
            }
        }

        wpds = &s->decorr_specs[c];
        nterms = decorr_filter_nterms[s->decorr_filter];

        while (1) {
        memcpy(s->temp_buffer[0][0], samples, buf_size);
        CLEAR(save_decorr_passes);

        for (j = 0; j < nterms; j++) {
            CLEAR(temp_decorr_pass);
            temp_decorr_pass.delta = wpds->delta;
            temp_decorr_pass.value = wpds->terms[j];

            if (temp_decorr_pass.value < 0)
                temp_decorr_pass.value = 1;

            decorr_mono(s->temp_buffer[j&1][0], s->temp_buffer[~j&1][0],
                        FFMIN(nb_samples, 2048), &temp_decorr_pass, -1);

            if (j) {
                CLEAR(temp_decorr_pass.samplesA);
            } else {
                reverse_mono_decorr(&temp_decorr_pass);
            }

            memcpy(save_decorr_passes + j, &temp_decorr_pass, sizeof(struct Decorr));
            decorr_mono(s->temp_buffer[j&1][0], s->temp_buffer[~j&1][0],
                        nb_samples, &temp_decorr_pass, 1);
        }

        size = log2mono(s->temp_buffer[j&1][0], nb_samples, log_limit);
        if (size != UINT32_MAX || !nterms)
            break;
        nterms >>= 1;
        }

        if (size < best_size) {
            memcpy(s->best_buffer[0], s->temp_buffer[j&1][0], buf_size);
            memcpy(s->decorr_passes, save_decorr_passes, sizeof(struct Decorr) * MAX_TERMS);
            s->num_terms = nterms;
            s->best_decorr = c;
            best_size = size;
        }

        if (pi++)
            s->mask_decorr = s->mask_decorr ? ((s->mask_decorr << 1) & (s->num_decorrs - 1)) : 1;
    }

    if (s->extra_flags)
        analyze_mono(s, samples, do_samples);
    else if (do_samples)
        memcpy(samples, s->best_buffer[0], buf_size);

    if (no_history || s->extra_flags) {
        CLEAR(s->w);
        scan_word(s, &s->w.c[0], s->best_buffer[0], nb_samples, -1);
    }
    return 0;
}

static void decorr_stereo(int32_t *in_left, int32_t *in_right,
                          int32_t *out_left, int32_t *out_right,
                          int nb_samples, struct Decorr *dpp, int dir)
{
    int m = 0, i;

    dpp->sumA = dpp->sumB = 0;

    if (dir < 0) {
        out_left  += nb_samples - 1;
        out_right += nb_samples - 1;
        in_left   += nb_samples - 1;
        in_right  += nb_samples - 1;
    }

    dpp->weightA = restore_weight(store_weight(dpp->weightA));
    dpp->weightB = restore_weight(store_weight(dpp->weightB));

    for (i = 0; i < MAX_TERM; i++) {
        dpp->samplesA[i] = wp_exp2(log2s(dpp->samplesA[i]));
        dpp->samplesB[i] = wp_exp2(log2s(dpp->samplesB[i]));
    }

    switch (dpp->value) {
    case 2:
        while (nb_samples--) {
            int32_t sam, tmp;

            sam = dpp->samplesA[0];
            dpp->samplesA[0] = dpp->samplesA[1];
            out_left[0] = tmp = (dpp->samplesA[1] = in_left[0]) - APPLY_WEIGHT(dpp->weightA, sam);
            UPDATE_WEIGHT(dpp->weightA, dpp->delta, sam, tmp);
            dpp->sumA += dpp->weightA;

            sam = dpp->samplesB[0];
            dpp->samplesB[0] = dpp->samplesB[1];
            out_right[0] = tmp = (dpp->samplesB[1] = in_right[0]) - APPLY_WEIGHT(dpp->weightB, sam);
            UPDATE_WEIGHT(dpp->weightB, dpp->delta, sam, tmp);
            dpp->sumB += dpp->weightB;

            in_left   += dir;
            out_left  += dir;
            in_right  += dir;
            out_right += dir;
        }
        break;
    case 17:
        while (nb_samples--) {
            int32_t sam, tmp;

            sam = 2 * dpp->samplesA[0] - dpp->samplesA[1];
            dpp->samplesA[1] = dpp->samplesA[0];
            out_left[0] = tmp = (dpp->samplesA[0] = in_left[0]) - APPLY_WEIGHT(dpp->weightA, sam);
            UPDATE_WEIGHT(dpp->weightA, dpp->delta, sam, tmp);
            dpp->sumA += dpp->weightA;

            sam = 2 * dpp->samplesB[0] - dpp->samplesB[1];
            dpp->samplesB[1] = dpp->samplesB[0];
            out_right[0] = tmp = (dpp->samplesB[0] = in_right[0]) - APPLY_WEIGHT (dpp->weightB, sam);
            UPDATE_WEIGHT(dpp->weightB, dpp->delta, sam, tmp);
            dpp->sumB += dpp->weightB;

            in_left   += dir;
            out_left  += dir;
            in_right  += dir;
            out_right += dir;
        }
        break;
    case 18:
        while (nb_samples--) {
            int32_t sam, tmp;

            sam = dpp->samplesA[0] + ((dpp->samplesA[0] - dpp->samplesA[1]) >> 1);
            dpp->samplesA[1] = dpp->samplesA[0];
            out_left[0] = tmp = (dpp->samplesA[0] = in_left[0]) - APPLY_WEIGHT(dpp->weightA, sam);
            UPDATE_WEIGHT(dpp->weightA, dpp->delta, sam, tmp);
            dpp->sumA += dpp->weightA;

            sam = dpp->samplesB[0] + ((dpp->samplesB[0] - dpp->samplesB[1]) >> 1);
            dpp->samplesB[1] = dpp->samplesB[0];
            out_right[0] = tmp = (dpp->samplesB[0] = in_right[0]) - APPLY_WEIGHT(dpp->weightB, sam);
            UPDATE_WEIGHT(dpp->weightB, dpp->delta, sam, tmp);
            dpp->sumB += dpp->weightB;

            in_left   += dir;
            out_left  += dir;
            in_right  += dir;
            out_right += dir;
        }
        break;
    default: {
        int k = dpp->value & (MAX_TERM - 1);

        while (nb_samples--) {
            int32_t sam, tmp;

            sam = dpp->samplesA[m];
            out_left[0] = tmp = (dpp->samplesA[k] = in_left[0]) - APPLY_WEIGHT(dpp->weightA, sam);
            UPDATE_WEIGHT(dpp->weightA, dpp->delta, sam, tmp);
            dpp->sumA += dpp->weightA;

            sam = dpp->samplesB[m];
            out_right[0] = tmp = (dpp->samplesB[k] = in_right[0]) - APPLY_WEIGHT(dpp->weightB, sam);
            UPDATE_WEIGHT(dpp->weightB, dpp->delta, sam, tmp);
            dpp->sumB += dpp->weightB;

            in_left   += dir;
            out_left  += dir;
            in_right  += dir;
            out_right += dir;
            m = (m + 1) & (MAX_TERM - 1);
            k = (k + 1) & (MAX_TERM - 1);
        }

        if (m) {
            int32_t temp_A[MAX_TERM], temp_B[MAX_TERM];
            int k;

            memcpy(temp_A, dpp->samplesA, sizeof(dpp->samplesA));
            memcpy(temp_B, dpp->samplesB, sizeof(dpp->samplesB));

            for (k = 0; k < MAX_TERM; k++) {
                dpp->samplesA[k] = temp_A[m];
                dpp->samplesB[k] = temp_B[m];
                m = (m + 1) & (MAX_TERM - 1);
            }
        }
        break;
        }
    case -1:
        while (nb_samples--) {
            int32_t sam_A, sam_B, tmp;

            sam_A = dpp->samplesA[0];
            out_left[0] = tmp = (sam_B = in_left[0]) - APPLY_WEIGHT(dpp->weightA, sam_A);
            UPDATE_WEIGHT_CLIP(dpp->weightA, dpp->delta, sam_A, tmp);
            dpp->sumA += dpp->weightA;

            out_right[0] = tmp = (dpp->samplesA[0] = in_right[0]) - APPLY_WEIGHT(dpp->weightB, sam_B);
            UPDATE_WEIGHT_CLIP(dpp->weightB, dpp->delta, sam_B, tmp);
            dpp->sumB += dpp->weightB;

            in_left   += dir;
            out_left  += dir;
            in_right  += dir;
            out_right += dir;
        }
        break;
    case -2:
        while (nb_samples--) {
            int32_t sam_A, sam_B, tmp;

            sam_B = dpp->samplesB[0];
            out_right[0] = tmp = (sam_A = in_right[0]) - APPLY_WEIGHT(dpp->weightB, sam_B);
            UPDATE_WEIGHT_CLIP(dpp->weightB, dpp->delta, sam_B, tmp);
            dpp->sumB += dpp->weightB;

            out_left[0] = tmp = (dpp->samplesB[0] = in_left[0]) - APPLY_WEIGHT(dpp->weightA, sam_A);
            UPDATE_WEIGHT_CLIP(dpp->weightA, dpp->delta, sam_A, tmp);
            dpp->sumA += dpp->weightA;

            in_left   += dir;
            out_left  += dir;
            in_right  += dir;
            out_right += dir;
        }
        break;
    case -3:
        while (nb_samples--) {
            int32_t sam_A, sam_B, tmp;

            sam_A = dpp->samplesA[0];
            sam_B = dpp->samplesB[0];

            dpp->samplesA[0] = tmp = in_right[0];
            out_right[0] = tmp -= APPLY_WEIGHT(dpp->weightB, sam_B);
            UPDATE_WEIGHT_CLIP(dpp->weightB, dpp->delta, sam_B, tmp);
            dpp->sumB += dpp->weightB;

            dpp->samplesB[0] = tmp = in_left[0];
            out_left[0] = tmp -= APPLY_WEIGHT(dpp->weightA, sam_A);
            UPDATE_WEIGHT_CLIP(dpp->weightA, dpp->delta, sam_A, tmp);
            dpp->sumA += dpp->weightA;

            in_left   += dir;
            out_left  += dir;
            in_right  += dir;
            out_right += dir;
        }
        break;
    }
}

static void reverse_decorr(struct Decorr *dpp)
{
    if (dpp->value > MAX_TERM) {
        int32_t sam_A, sam_B;

        if (dpp->value & 1) {
            sam_A = 2 * dpp->samplesA[0] - dpp->samplesA[1];
            sam_B = 2 * dpp->samplesB[0] - dpp->samplesB[1];
        } else {
            sam_A = (3 * dpp->samplesA[0] - dpp->samplesA[1]) >> 1;
            sam_B = (3 * dpp->samplesB[0] - dpp->samplesB[1]) >> 1;
        }

        dpp->samplesA[1] = dpp->samplesA[0];
        dpp->samplesB[1] = dpp->samplesB[0];
        dpp->samplesA[0] = sam_A;
        dpp->samplesB[0] = sam_B;

        if (dpp->value & 1) {
            sam_A = 2 * dpp->samplesA[0] - dpp->samplesA[1];
            sam_B = 2 * dpp->samplesB[0] - dpp->samplesB[1];
        } else {
            sam_A = (3 * dpp->samplesA[0] - dpp->samplesA[1]) >> 1;
            sam_B = (3 * dpp->samplesB[0] - dpp->samplesB[1]) >> 1;
        }

        dpp->samplesA[1] = sam_A;
        dpp->samplesB[1] = sam_B;
    } else if (dpp->value > 1) {
        int i, j, k;

        for (i = 0, j = dpp->value - 1, k = 0; k < dpp->value / 2; i++, j--, k++) {
            i &= (MAX_TERM - 1);
            j &= (MAX_TERM - 1);
            dpp->samplesA[i] ^= dpp->samplesA[j];
            dpp->samplesA[j] ^= dpp->samplesA[i];
            dpp->samplesA[i] ^= dpp->samplesA[j];
            dpp->samplesB[i] ^= dpp->samplesB[j];
            dpp->samplesB[j] ^= dpp->samplesB[i];
            dpp->samplesB[i] ^= dpp->samplesB[j];
        }
    }
}

static void decorr_stereo_quick(int32_t *in_left,  int32_t *in_right,
                                int32_t *out_left, int32_t *out_right,
                                int nb_samples, struct Decorr *dpp)
{
    int m = 0, i;

    dpp->weightA = restore_weight(store_weight(dpp->weightA));
    dpp->weightB = restore_weight(store_weight(dpp->weightB));

    for (i = 0; i < MAX_TERM; i++) {
        dpp->samplesA[i] = wp_exp2(log2s(dpp->samplesA[i]));
        dpp->samplesB[i] = wp_exp2(log2s(dpp->samplesB[i]));
    }

    switch (dpp->value) {
    case 2:
        for (i = 0; i < nb_samples; i++) {
            int32_t sam, tmp;

            sam = dpp->samplesA[0];
            dpp->samplesA[0] = dpp->samplesA[1];
            out_left[i] = tmp = (dpp->samplesA[1] = in_left[i]) - APPLY_WEIGHT_I(dpp->weightA, sam);
            UPDATE_WEIGHT(dpp->weightA, dpp->delta, sam, tmp);

            sam = dpp->samplesB[0];
            dpp->samplesB[0] = dpp->samplesB[1];
            out_right[i] = tmp = (dpp->samplesB[1] = in_right[i]) - APPLY_WEIGHT_I(dpp->weightB, sam);
            UPDATE_WEIGHT(dpp->weightB, dpp->delta, sam, tmp);
        }
        break;
    case 17:
        for (i = 0; i < nb_samples; i++) {
            int32_t sam, tmp;

            sam = 2 * dpp->samplesA[0] - dpp->samplesA[1];
            dpp->samplesA[1] = dpp->samplesA[0];
            out_left[i] = tmp = (dpp->samplesA[0] = in_left[i]) - APPLY_WEIGHT_I(dpp->weightA, sam);
            UPDATE_WEIGHT(dpp->weightA, dpp->delta, sam, tmp);

            sam = 2 * dpp->samplesB[0] - dpp->samplesB[1];
            dpp->samplesB[1] = dpp->samplesB[0];
            out_right[i] = tmp = (dpp->samplesB[0] = in_right[i]) - APPLY_WEIGHT_I(dpp->weightB, sam);
            UPDATE_WEIGHT(dpp->weightB, dpp->delta, sam, tmp);
        }
        break;
    case 18:
        for (i = 0; i < nb_samples; i++) {
            int32_t sam, tmp;

            sam = dpp->samplesA[0] + ((dpp->samplesA[0] - dpp->samplesA[1]) >> 1);
            dpp->samplesA[1] = dpp->samplesA[0];
            out_left[i] = tmp = (dpp->samplesA[0] = in_left[i]) - APPLY_WEIGHT_I(dpp->weightA, sam);
            UPDATE_WEIGHT(dpp->weightA, dpp->delta, sam, tmp);

            sam = dpp->samplesB[0] + ((dpp->samplesB[0] - dpp->samplesB[1]) >> 1);
            dpp->samplesB[1] = dpp->samplesB[0];
            out_right[i] = tmp = (dpp->samplesB[0] = in_right[i]) - APPLY_WEIGHT_I(dpp->weightB, sam);
            UPDATE_WEIGHT(dpp->weightB, dpp->delta, sam, tmp);
        }
        break;
    default: {
        int k = dpp->value & (MAX_TERM - 1);

        for (i = 0; i < nb_samples; i++) {
            int32_t sam, tmp;

            sam = dpp->samplesA[m];
            out_left[i] = tmp = (dpp->samplesA[k] = in_left[i]) - APPLY_WEIGHT_I(dpp->weightA, sam);
            UPDATE_WEIGHT(dpp->weightA, dpp->delta, sam, tmp);

            sam = dpp->samplesB[m];
            out_right[i] = tmp = (dpp->samplesB[k] = in_right[i]) - APPLY_WEIGHT_I(dpp->weightB, sam);
            UPDATE_WEIGHT(dpp->weightB, dpp->delta, sam, tmp);

            m = (m + 1) & (MAX_TERM - 1);
            k = (k + 1) & (MAX_TERM - 1);
        }

        if (m) {
            int32_t temp_A[MAX_TERM], temp_B[MAX_TERM];
            int k;

            memcpy(temp_A, dpp->samplesA, sizeof(dpp->samplesA));
            memcpy(temp_B, dpp->samplesB, sizeof(dpp->samplesB));

            for (k = 0; k < MAX_TERM; k++) {
                dpp->samplesA[k] = temp_A[m];
                dpp->samplesB[k] = temp_B[m];
                m = (m + 1) & (MAX_TERM - 1);
            }
        }
        break;
    }
    case -1:
        for (i = 0; i < nb_samples; i++) {
            int32_t sam_A, sam_B, tmp;

            sam_A = dpp->samplesA[0];
            out_left[i] = tmp = (sam_B = in_left[i]) - APPLY_WEIGHT_I(dpp->weightA, sam_A);
            UPDATE_WEIGHT_CLIP(dpp->weightA, dpp->delta, sam_A, tmp);

            out_right[i] = tmp = (dpp->samplesA[0] = in_right[i]) - APPLY_WEIGHT_I(dpp->weightB, sam_B);
            UPDATE_WEIGHT_CLIP(dpp->weightB, dpp->delta, sam_B, tmp);
        }
        break;
    case -2:
        for (i = 0; i < nb_samples; i++) {
            int32_t sam_A, sam_B, tmp;

            sam_B = dpp->samplesB[0];
            out_right[i] = tmp = (sam_A = in_right[i]) - APPLY_WEIGHT_I(dpp->weightB, sam_B);
            UPDATE_WEIGHT_CLIP(dpp->weightB, dpp->delta, sam_B, tmp);

            out_left[i] = tmp = (dpp->samplesB[0] = in_left[i]) - APPLY_WEIGHT_I(dpp->weightA, sam_A);
            UPDATE_WEIGHT_CLIP(dpp->weightA, dpp->delta, sam_A, tmp);
        }
        break;
    case -3:
        for (i = 0; i < nb_samples; i++) {
            int32_t sam_A, sam_B, tmp;

            sam_A = dpp->samplesA[0];
            sam_B = dpp->samplesB[0];

            dpp->samplesA[0] = tmp = in_right[i];
            out_right[i] = tmp -= APPLY_WEIGHT_I(dpp->weightB, sam_B);
            UPDATE_WEIGHT_CLIP(dpp->weightB, dpp->delta, sam_B, tmp);

            dpp->samplesB[0] = tmp = in_left[i];
            out_left[i] = tmp -= APPLY_WEIGHT_I(dpp->weightA, sam_A);
            UPDATE_WEIGHT_CLIP(dpp->weightA, dpp->delta, sam_A, tmp);
        }
        break;
    }
}

static void decorr_stereo_buffer(WavPackExtraInfo *info,
                                 int32_t *in_left,  int32_t *in_right,
                                 int32_t *out_left, int32_t *out_right,
                                 int nb_samples, int tindex)
{
    struct Decorr dp = {0}, *dppi = info->dps + tindex;
    int delta = dppi->delta, pre_delta;
    int term = dppi->value;

    if (delta == 7)
        pre_delta = 7;
    else if (delta < 2)
        pre_delta = 3;
    else
        pre_delta = delta + 1;

    dp.value = term;
    dp.delta = pre_delta;
    decorr_stereo(in_left, in_right, out_left, out_right,
                  FFMIN(2048, nb_samples), &dp, -1);
    dp.delta = delta;

    if (tindex == 0) {
        reverse_decorr(&dp);
    } else {
        CLEAR(dp.samplesA);
        CLEAR(dp.samplesB);
    }

    memcpy(dppi->samplesA, dp.samplesA, sizeof(dp.samplesA));
    memcpy(dppi->samplesB, dp.samplesB, sizeof(dp.samplesB));
    dppi->weightA = dp.weightA;
    dppi->weightB = dp.weightB;

    if (delta == 0) {
        dp.delta = 1;
        decorr_stereo(in_left, in_right, out_left, out_right, nb_samples, &dp, 1);
        dp.delta = 0;
        memcpy(dp.samplesA, dppi->samplesA, sizeof(dp.samplesA));
        memcpy(dp.samplesB, dppi->samplesB, sizeof(dp.samplesB));
        dppi->weightA = dp.weightA = dp.sumA / nb_samples;
        dppi->weightB = dp.weightB = dp.sumB / nb_samples;
    }

    if (info->gt16bit)
        decorr_stereo(in_left, in_right, out_left, out_right,
                           nb_samples, &dp, 1);
    else
        decorr_stereo_quick(in_left, in_right, out_left, out_right,
                            nb_samples, &dp);
}

static void sort_stereo(WavPackEncodeContext *s, WavPackExtraInfo *info)
{
    int reversed = 1;
    uint32_t bits;

    while (reversed) {
        int ri, i;

        memcpy(info->dps, s->decorr_passes, sizeof(s->decorr_passes));
        reversed = 0;

        for (ri = 0; ri < info->nterms && s->decorr_passes[ri].value; ri++) {

            if (ri + 1 >= info->nterms || !s->decorr_passes[ri+1].value)
                break;

            if (s->decorr_passes[ri].value == s->decorr_passes[ri+1].value) {
                decorr_stereo_buffer(info,
                                     s->sampleptrs[ri  ][0], s->sampleptrs[ri  ][1],
                                     s->sampleptrs[ri+1][0], s->sampleptrs[ri+1][1],
                                     s->block_samples, ri);
                continue;
            }

            info->dps[ri  ] = s->decorr_passes[ri+1];
            info->dps[ri+1] = s->decorr_passes[ri  ];

            for (i = ri; i < info->nterms && s->decorr_passes[i].value; i++)
                decorr_stereo_buffer(info,
                                     s->sampleptrs[i  ][0], s->sampleptrs[i  ][1],
                                     s->sampleptrs[i+1][0], s->sampleptrs[i+1][1],
                                     s->block_samples, i);

            bits = log2stereo(s->sampleptrs[i][0], s->sampleptrs[i][1],
                              s->block_samples, info->log_limit);

            if (bits < info->best_bits) {
                reversed = 1;
                info->best_bits = bits;
                CLEAR(s->decorr_passes);
                memcpy(s->decorr_passes, info->dps, sizeof(info->dps[0]) * i);
                memcpy(s->sampleptrs[info->nterms + 1][0],
                       s->sampleptrs[i][0], s->block_samples * 4);
                memcpy(s->sampleptrs[info->nterms + 1][1],
                       s->sampleptrs[i][1], s->block_samples * 4);
            } else {
                info->dps[ri  ] = s->decorr_passes[ri  ];
                info->dps[ri+1] = s->decorr_passes[ri+1];
                decorr_stereo_buffer(info,
                                     s->sampleptrs[ri  ][0], s->sampleptrs[ri  ][1],
                                     s->sampleptrs[ri+1][0], s->sampleptrs[ri+1][1],
                                     s->block_samples, ri);
            }
        }
    }
}

static void delta_stereo(WavPackEncodeContext *s, WavPackExtraInfo *info)
{
    int lower = 0, delta, d, i;
    uint32_t bits;

    if (!s->decorr_passes[0].value)
        return;
    delta = s->decorr_passes[0].delta;

    for (d = delta - 1; d >= 0; d--) {
        for (i = 0; i < info->nterms && s->decorr_passes[i].value; i++) {
            info->dps[i].value = s->decorr_passes[i].value;
            info->dps[i].delta = d;
            decorr_stereo_buffer(info,
                                 s->sampleptrs[i  ][0], s->sampleptrs[i  ][1],
                                 s->sampleptrs[i+1][0], s->sampleptrs[i+1][1],
                                 s->block_samples, i);
        }

        bits = log2stereo(s->sampleptrs[i][0], s->sampleptrs[i][1],
                          s->block_samples, info->log_limit);
        if (bits >= info->best_bits)
            break;
        lower = 1;
        info->best_bits = bits;
        CLEAR(s->decorr_passes);
        memcpy(s->decorr_passes, info->dps, sizeof(info->dps[0]) * i);
        memcpy(s->sampleptrs[info->nterms + 1][0], s->sampleptrs[i][0],
               s->block_samples * 4);
        memcpy(s->sampleptrs[info->nterms + 1][1], s->sampleptrs[i][1],
               s->block_samples * 4);
    }

    for (d = delta + 1; !lower && d <= 7; d++) {
        for (i = 0; i < info->nterms && s->decorr_passes[i].value; i++) {
            info->dps[i].value = s->decorr_passes[i].value;
            info->dps[i].delta = d;
            decorr_stereo_buffer(info,
                                 s->sampleptrs[i  ][0], s->sampleptrs[i  ][1],
                                 s->sampleptrs[i+1][0], s->sampleptrs[i+1][1],
                                 s->block_samples, i);
        }

        bits = log2stereo(s->sampleptrs[i][0], s->sampleptrs[i][1],
                          s->block_samples, info->log_limit);

        if (bits < info->best_bits) {
            info->best_bits = bits;
            CLEAR(s->decorr_passes);
            memcpy(s->decorr_passes, info->dps, sizeof(info->dps[0]) * i);
            memcpy(s->sampleptrs[info->nterms + 1][0],
                   s->sampleptrs[i][0], s->block_samples * 4);
            memcpy(s->sampleptrs[info->nterms + 1][1],
                   s->sampleptrs[i][1], s->block_samples * 4);
        }
        else
            break;
    }
}

static void recurse_stereo(WavPackEncodeContext *s, WavPackExtraInfo *info,
                           int depth, int delta, uint32_t input_bits)
{
    int term, branches = s->num_branches - depth;
    int32_t *in_left, *in_right, *out_left, *out_right;
    uint32_t term_bits[22], bits;

    if (branches < 1 || depth + 1 == info->nterms)
        branches = 1;

    CLEAR(term_bits);
    in_left   = s->sampleptrs[depth    ][0];
    in_right  = s->sampleptrs[depth    ][1];
    out_left  = s->sampleptrs[depth + 1][0];
    out_right = s->sampleptrs[depth + 1][1];

    for (term = -3; term <= 18; term++) {
        if (!term || (term > 8 && term < 17))
            continue;

        if (term == 17 && branches == 1 && depth + 1 < info->nterms)
            continue;

        if (term == -1 || term == -2)
            if (!(s->flags & WV_CROSS_DECORR))
                continue;

        if (!s->extra_flags && (term > 4 && term < 17))
            continue;

        info->dps[depth].value = term;
        info->dps[depth].delta = delta;
        decorr_stereo_buffer(info, in_left, in_right, out_left, out_right,
                             s->block_samples, depth);
        bits = log2stereo(out_left, out_right, s->block_samples, info->log_limit);

        if (bits < info->best_bits) {
            info->best_bits = bits;
            CLEAR(s->decorr_passes);
            memcpy(s->decorr_passes, info->dps, sizeof(info->dps[0]) * (depth + 1));
            memcpy(s->sampleptrs[info->nterms + 1][0], s->sampleptrs[depth + 1][0],
                   s->block_samples * 4);
            memcpy(s->sampleptrs[info->nterms + 1][1], s->sampleptrs[depth + 1][1],
                   s->block_samples * 4);
        }

        term_bits[term + 3] = bits;
    }

    while (depth + 1 < info->nterms && branches--) {
        uint32_t local_best_bits = input_bits;
        int best_term = 0, i;

        for (i = 0; i < 22; i++)
            if (term_bits[i] && term_bits[i] < local_best_bits) {
                local_best_bits = term_bits[i];
                best_term = i - 3;
            }

        if (!best_term)
            break;

        term_bits[best_term + 3] = 0;

        info->dps[depth].value = best_term;
        info->dps[depth].delta = delta;
        decorr_stereo_buffer(info, in_left, in_right, out_left, out_right,
                             s->block_samples, depth);

        recurse_stereo(s, info, depth + 1, delta, local_best_bits);
    }
}

static void analyze_stereo(WavPackEncodeContext *s,
                           int32_t *in_left, int32_t *in_right,
                           int do_samples)
{
    WavPackExtraInfo info;
    int i;

    info.gt16bit = ((s->flags & MAG_MASK) >> MAG_LSB) >= 16;

    info.log_limit = (((s->flags & MAG_MASK) >> MAG_LSB) + 4) * 256;
    info.log_limit = FFMIN(6912, info.log_limit);

    info.nterms = s->num_terms;

    if (allocate_buffers2(s, s->num_terms))
        return;

    memcpy(info.dps, s->decorr_passes, sizeof(info.dps));
    memcpy(s->sampleptrs[0][0], in_left,  s->block_samples * 4);
    memcpy(s->sampleptrs[0][1], in_right, s->block_samples * 4);

    for (i = 0; i < info.nterms && info.dps[i].value; i++)
        if (info.gt16bit)
            decorr_stereo(s->sampleptrs[i    ][0], s->sampleptrs[i    ][1],
                          s->sampleptrs[i + 1][0], s->sampleptrs[i + 1][1],
                          s->block_samples, info.dps + i, 1);
        else
            decorr_stereo_quick(s->sampleptrs[i    ][0], s->sampleptrs[i    ][1],
                                s->sampleptrs[i + 1][0], s->sampleptrs[i + 1][1],
                                s->block_samples, info.dps + i);

    info.best_bits = log2stereo(s->sampleptrs[info.nterms][0], s->sampleptrs[info.nterms][1],
                                s->block_samples, 0);

    memcpy(s->sampleptrs[info.nterms + 1][0], s->sampleptrs[i][0], s->block_samples * 4);
    memcpy(s->sampleptrs[info.nterms + 1][1], s->sampleptrs[i][1], s->block_samples * 4);

    if (s->extra_flags & EXTRA_BRANCHES)
        recurse_stereo(s, &info, 0, (int) floor(s->delta_decay + 0.5),
                       log2stereo(s->sampleptrs[0][0], s->sampleptrs[0][1],
                                  s->block_samples, 0));

    if (s->extra_flags & EXTRA_SORT_FIRST)
        sort_stereo(s, &info);

    if (s->extra_flags & EXTRA_TRY_DELTAS) {
        delta_stereo(s, &info);

        if ((s->extra_flags & EXTRA_ADJUST_DELTAS) && s->decorr_passes[0].value)
            s->delta_decay = (float)((s->delta_decay * 2.0 + s->decorr_passes[0].delta) / 3.0);
        else
            s->delta_decay = 2.0;
    }

    if (s->extra_flags & EXTRA_SORT_LAST)
        sort_stereo(s, &info);

    if (do_samples) {
        memcpy(in_left,  s->sampleptrs[info.nterms + 1][0], s->block_samples * 4);
        memcpy(in_right, s->sampleptrs[info.nterms + 1][1], s->block_samples * 4);
    }

    for (i = 0; i < info.nterms; i++)
        if (!s->decorr_passes[i].value)
            break;

    s->num_terms = i;
}

static int wv_stereo(WavPackEncodeContext *s,
                     int32_t *samples_l, int32_t *samples_r,
                     int no_history, int do_samples)
{
    struct Decorr temp_decorr_pass, save_decorr_passes[MAX_TERMS] = {{0}};
    int nb_samples = s->block_samples, ret;
    int buf_size = sizeof(int32_t) * nb_samples;
    int log_limit, force_js = 0, force_ts = 0, got_js = 0, pi, i;
    uint32_t best_size = UINT32_MAX, size;

    for (i = 0; i < nb_samples; i++)
        if (samples_l[i] || samples_r[i])
            break;

    if (i == nb_samples) {
        s->flags &= ~((uint32_t) WV_JOINT_STEREO);
        CLEAR(s->decorr_passes);
        CLEAR(s->w);
        s->num_terms = 0;
        return 0;
    }

    log_limit = (((s->flags & MAG_MASK) >> MAG_LSB) + 4) * 256;
    log_limit = FFMIN(6912, log_limit);

    if (s->joint != -1) {
        force_js =  s->joint;
        force_ts = !s->joint;
    }

    if ((ret = allocate_buffers(s)) < 0)
        return ret;

    if (no_history || s->num_passes >= 7)
        s->best_decorr = s->mask_decorr = 0;

    for (pi = 0; pi < s->num_passes;) {
        const WavPackDecorrSpec *wpds;
        int nterms, c, j;

        if (!pi)
            c = s->best_decorr;
        else {
            if (s->mask_decorr == 0)
                c = 0;
            else
                c = (s->best_decorr & (s->mask_decorr - 1)) | s->mask_decorr;

            if (c == s->best_decorr) {
                s->mask_decorr = s->mask_decorr ? ((s->mask_decorr << 1) & (s->num_decorrs - 1)) : 1;
                continue;
            }
        }

        wpds = &s->decorr_specs[c];
        nterms = decorr_filter_nterms[s->decorr_filter];

        while (1) {
            if (force_js || (wpds->joint_stereo && !force_ts)) {
                if (!got_js) {
                    av_fast_padded_malloc(&s->js_left,  &s->js_left_size,  buf_size);
                    av_fast_padded_malloc(&s->js_right, &s->js_right_size, buf_size);
                    memcpy(s->js_left,  samples_l, buf_size);
                    memcpy(s->js_right, samples_r, buf_size);

                    for (i = 0; i < nb_samples; i++)
                        s->js_right[i] += ((s->js_left[i] -= s->js_right[i]) >> 1);
                    got_js = 1;
                }

                memcpy(s->temp_buffer[0][0], s->js_left,  buf_size);
                memcpy(s->temp_buffer[0][1], s->js_right, buf_size);
            } else {
                memcpy(s->temp_buffer[0][0], samples_l, buf_size);
                memcpy(s->temp_buffer[0][1], samples_r, buf_size);
            }

            CLEAR(save_decorr_passes);

            for (j = 0; j < nterms; j++) {
                CLEAR(temp_decorr_pass);
                temp_decorr_pass.delta = wpds->delta;
                temp_decorr_pass.value = wpds->terms[j];

                if (temp_decorr_pass.value < 0 && !(s->flags & WV_CROSS_DECORR))
                    temp_decorr_pass.value = -3;

                decorr_stereo(s->temp_buffer[ j&1][0], s->temp_buffer[ j&1][1],
                              s->temp_buffer[~j&1][0], s->temp_buffer[~j&1][1],
                              FFMIN(2048, nb_samples), &temp_decorr_pass, -1);

                if (j) {
                    CLEAR(temp_decorr_pass.samplesA);
                    CLEAR(temp_decorr_pass.samplesB);
                } else {
                    reverse_decorr(&temp_decorr_pass);
                }

                memcpy(save_decorr_passes + j, &temp_decorr_pass, sizeof(struct Decorr));

                if (((s->flags & MAG_MASK) >> MAG_LSB) >= 16)
                    decorr_stereo(s->temp_buffer[ j&1][0], s->temp_buffer[ j&1][1],
                                  s->temp_buffer[~j&1][0], s->temp_buffer[~j&1][1],
                                  nb_samples, &temp_decorr_pass, 1);
                else
                    decorr_stereo_quick(s->temp_buffer[ j&1][0], s->temp_buffer[ j&1][1],
                                        s->temp_buffer[~j&1][0], s->temp_buffer[~j&1][1],
                                        nb_samples, &temp_decorr_pass);
            }

            size = log2stereo(s->temp_buffer[j&1][0], s->temp_buffer[j&1][1],
                              nb_samples, log_limit);
            if (size != UINT32_MAX || !nterms)
                break;
            nterms >>= 1;
        }

        if (size < best_size) {
            memcpy(s->best_buffer[0], s->temp_buffer[j&1][0], buf_size);
            memcpy(s->best_buffer[1], s->temp_buffer[j&1][1], buf_size);
            memcpy(s->decorr_passes, save_decorr_passes, sizeof(struct Decorr) * MAX_TERMS);
            s->num_terms = nterms;
            s->best_decorr = c;
            best_size = size;
        }

        if (pi++)
            s->mask_decorr = s->mask_decorr ? ((s->mask_decorr << 1) & (s->num_decorrs - 1)) : 1;
    }

    if (force_js || (s->decorr_specs[s->best_decorr].joint_stereo && !force_ts))
        s->flags |= WV_JOINT_STEREO;
    else
        s->flags &= ~((uint32_t) WV_JOINT_STEREO);

    if (s->extra_flags) {
        if (s->flags & WV_JOINT_STEREO) {
            analyze_stereo(s, s->js_left, s->js_right, do_samples);

            if (do_samples) {
                memcpy(samples_l, s->js_left,  buf_size);
                memcpy(samples_r, s->js_right, buf_size);
            }
        } else
            analyze_stereo(s, samples_l, samples_r, do_samples);
    } else if (do_samples) {
        memcpy(samples_l, s->best_buffer[0], buf_size);
        memcpy(samples_r, s->best_buffer[1], buf_size);
    }

    if (s->extra_flags || no_history ||
        s->joint_stereo != s->decorr_specs[s->best_decorr].joint_stereo) {
        s->joint_stereo = s->decorr_specs[s->best_decorr].joint_stereo;
        CLEAR(s->w);
        scan_word(s, &s->w.c[0], s->best_buffer[0], nb_samples, -1);
        scan_word(s, &s->w.c[1], s->best_buffer[1], nb_samples, -1);
    }
    return 0;
}

#define count_bits(av) ( \
 (av) < (1 << 8) ? nbits_table[av] : \
  ( \
   (av) < (1 << 16) ? nbits_table[(av) >> 8] + 8 : \
   ((av) < (1 << 24) ? nbits_table[(av) >> 16] + 16 : nbits_table[(av) >> 24] + 24) \
  ) \
)

static void encode_flush(WavPackEncodeContext *s)
{
    WavPackWords *w = &s->w;
    PutBitContext *pb = &s->pb;

    if (w->zeros_acc) {
        int cbits = count_bits(w->zeros_acc);

        do {
            if (cbits > 31) {
                put_bits(pb, 31, 0x7FFFFFFF);
                cbits -= 31;
            } else {
                put_bits(pb, cbits, (1 << cbits) - 1);
                cbits = 0;
            }
        } while (cbits);

        put_bits(pb, 1, 0);

        while (w->zeros_acc > 1) {
            put_bits(pb, 1, w->zeros_acc & 1);
            w->zeros_acc >>= 1;
        }

        w->zeros_acc = 0;
    }

    if (w->holding_one) {
        if (w->holding_one >= 16) {
            int cbits;

            put_bits(pb, 16, (1 << 16) - 1);
            put_bits(pb, 1, 0);
            w->holding_one -= 16;
            cbits = count_bits(w->holding_one);

            do {
                if (cbits > 31) {
                    put_bits(pb, 31, 0x7FFFFFFF);
                    cbits -= 31;
                } else {
                    put_bits(pb, cbits, (1 << cbits) - 1);
                    cbits = 0;
                }
            } while (cbits);

            put_bits(pb, 1, 0);

            while (w->holding_one > 1) {
                put_bits(pb, 1, w->holding_one & 1);
                w->holding_one >>= 1;
            }

            w->holding_zero = 0;
        } else {
            put_bits(pb, w->holding_one, (1 << w->holding_one) - 1);
        }

        w->holding_one = 0;
    }

    if (w->holding_zero) {
        put_bits(pb, 1, 0);
        w->holding_zero = 0;
    }

    if (w->pend_count) {
        put_bits(pb, w->pend_count, w->pend_data);
        w->pend_data = w->pend_count = 0;
    }
}

static void wavpack_encode_sample(WavPackEncodeContext *s, WvChannel *c, int32_t sample)
{
    WavPackWords *w = &s->w;
    uint32_t ones_count, low, high;
    int sign = sample < 0;

    if (s->w.c[0].median[0] < 2 && !s->w.holding_zero && s->w.c[1].median[0] < 2) {
        if (w->zeros_acc) {
            if (sample)
                encode_flush(s);
            else {
                w->zeros_acc++;
                return;
            }
        } else if (sample) {
            put_bits(&s->pb, 1, 0);
        } else {
            CLEAR(s->w.c[0].median);
            CLEAR(s->w.c[1].median);
            w->zeros_acc = 1;
            return;
        }
    }

    if (sign)
        sample = ~sample;

    if (sample < (int32_t) GET_MED(0)) {
        ones_count = low = 0;
        high = GET_MED(0) - 1;
        DEC_MED(0);
    } else {
        low = GET_MED(0);
        INC_MED(0);

        if (sample - low < GET_MED(1)) {
            ones_count = 1;
            high = low + GET_MED(1) - 1;
            DEC_MED(1);
        } else {
            low += GET_MED(1);
            INC_MED(1);

            if (sample - low < GET_MED(2)) {
                ones_count = 2;
                high = low + GET_MED(2) - 1;
                DEC_MED(2);
            } else {
                ones_count = 2 + (sample - low) / GET_MED(2);
                low += (ones_count - 2) * GET_MED(2);
                high = low + GET_MED(2) - 1;
                INC_MED(2);
            }
        }
    }

    if (w->holding_zero) {
        if (ones_count)
            w->holding_one++;

        encode_flush(s);

        if (ones_count) {
            w->holding_zero = 1;
            ones_count--;
        } else
            w->holding_zero = 0;
    } else
        w->holding_zero = 1;

    w->holding_one = ones_count * 2;

    if (high != low) {
        uint32_t maxcode = high - low, code = sample - low;
        int bitcount = count_bits(maxcode);
        uint32_t extras = (1 << bitcount) - maxcode - 1;

        if (code < extras) {
            w->pend_data |= code << w->pend_count;
            w->pend_count += bitcount - 1;
        } else {
            w->pend_data |= ((code + extras) >> 1) << w->pend_count;
            w->pend_count += bitcount - 1;
            w->pend_data |= ((code + extras) & 1) << w->pend_count++;
        }
    }

    w->pend_data |= ((int32_t) sign << w->pend_count++);

    if (!w->holding_zero)
        encode_flush(s);
}

static void pack_int32(WavPackEncodeContext *s,
                       int32_t *samples_l, int32_t *samples_r,
                       int nb_samples)
{
    const int sent_bits = s->int32_sent_bits;
    PutBitContext *pb = &s->pb;
    int i, pre_shift;

    pre_shift = s->int32_zeros + s->int32_ones + s->int32_dups;

    if (!sent_bits)
        return;

    if (s->flags & WV_MONO_DATA) {
        for (i = 0; i < nb_samples; i++) {
            put_sbits(pb, sent_bits, samples_l[i] >> pre_shift);
        }
    } else {
        for (i = 0; i < nb_samples; i++) {
            put_sbits(pb, sent_bits, samples_l[i] >> pre_shift);
            put_sbits(pb, sent_bits, samples_r[i] >> pre_shift);
        }
    }
}

static void pack_float_sample(WavPackEncodeContext *s, int32_t *sample)
{
    const int max_exp = s->float_max_exp;
    PutBitContext *pb = &s->pb;
    int32_t value, shift_count;

    if (get_exponent(*sample) == 255) {
        if (get_mantissa(*sample)) {
            put_bits(pb, 1, 1);
            put_bits(pb, 23, get_mantissa(*sample));
        } else {
            put_bits(pb, 1, 0);
        }

        value = 0x1000000;
        shift_count = 0;
    } else if (get_exponent(*sample)) {
        shift_count = max_exp - get_exponent(*sample);
        value = 0x800000 + get_mantissa(*sample);
    } else {
        shift_count = max_exp ? max_exp - 1 : 0;
        value = get_mantissa(*sample);
    }

    if (shift_count < 25)
        value >>= shift_count;
    else
        value = 0;

    if (!value) {
        if (s->float_flags & FLOAT_ZEROS_SENT) {
            if (get_exponent(*sample) || get_mantissa(*sample)) {
                put_bits(pb, 1, 1);
                put_bits(pb, 23, get_mantissa(*sample));

                if (max_exp >= 25)
                    put_bits(pb, 8, get_exponent(*sample));

                put_bits(pb, 1, get_sign(*sample));
            } else {
                put_bits(pb, 1, 0);

                if (s->float_flags & FLOAT_NEG_ZEROS)
                    put_bits(pb, 1, get_sign(*sample));
            }
        }
    } else if (shift_count) {
        if (s->float_flags & FLOAT_SHIFT_SENT) {
            put_sbits(pb, shift_count, get_mantissa(*sample));
        } else if (s->float_flags & FLOAT_SHIFT_SAME) {
            put_bits(pb, 1, get_mantissa(*sample) & 1);
        }
    }
}

static void pack_float(WavPackEncodeContext *s,
                       int32_t *samples_l, int32_t *samples_r,
                       int nb_samples)
{
    int i;

    if (s->flags & WV_MONO_DATA) {
        for (i = 0; i < nb_samples; i++)
            pack_float_sample(s, &samples_l[i]);
    } else {
        for (i = 0; i < nb_samples; i++) {
            pack_float_sample(s, &samples_l[i]);
            pack_float_sample(s, &samples_r[i]);
        }
    }
}

static void decorr_stereo_pass2(struct Decorr *dpp,
                                int32_t *samples_l, int32_t *samples_r,
                                int nb_samples)
{
    int i, m, k;

    switch (dpp->value) {
    case 17:
        for (i = 0; i < nb_samples; i++) {
            int32_t sam, tmp;

            sam = 2 * dpp->samplesA[0] - dpp->samplesA[1];
            dpp->samplesA[1] = dpp->samplesA[0];
            samples_l[i] = tmp = (dpp->samplesA[0] = samples_l[i]) - APPLY_WEIGHT(dpp->weightA, sam);
            UPDATE_WEIGHT(dpp->weightA, dpp->delta, sam, tmp);

            sam = 2 * dpp->samplesB[0] - dpp->samplesB[1];
            dpp->samplesB[1] = dpp->samplesB[0];
            samples_r[i] = tmp = (dpp->samplesB[0] = samples_r[i]) - APPLY_WEIGHT(dpp->weightB, sam);
            UPDATE_WEIGHT(dpp->weightB, dpp->delta, sam, tmp);
        }
        break;
    case 18:
        for (i = 0; i < nb_samples; i++) {
            int32_t sam, tmp;

            sam = dpp->samplesA[0] + ((dpp->samplesA[0] - dpp->samplesA[1]) >> 1);
            dpp->samplesA[1] = dpp->samplesA[0];
            samples_l[i] = tmp = (dpp->samplesA[0] = samples_l[i]) - APPLY_WEIGHT(dpp->weightA, sam);
            UPDATE_WEIGHT(dpp->weightA, dpp->delta, sam, tmp);

            sam = dpp->samplesB[0] + ((dpp->samplesB[0] - dpp->samplesB[1]) >> 1);
            dpp->samplesB[1] = dpp->samplesB[0];
            samples_r[i] = tmp = (dpp->samplesB[0] = samples_r[i]) - APPLY_WEIGHT(dpp->weightB, sam);
            UPDATE_WEIGHT(dpp->weightB, dpp->delta, sam, tmp);
        }
        break;
    default:
        for (m = 0, k = dpp->value & (MAX_TERM - 1), i = 0; i < nb_samples; i++) {
            int32_t sam, tmp;

            sam = dpp->samplesA[m];
            samples_l[i] = tmp = (dpp->samplesA[k] = samples_l[i]) - APPLY_WEIGHT(dpp->weightA, sam);
            UPDATE_WEIGHT(dpp->weightA, dpp->delta, sam, tmp);

            sam = dpp->samplesB[m];
            samples_r[i] = tmp = (dpp->samplesB[k] = samples_r[i]) - APPLY_WEIGHT(dpp->weightB, sam);
            UPDATE_WEIGHT(dpp->weightB, dpp->delta, sam, tmp);

            m = (m + 1) & (MAX_TERM - 1);
            k = (k + 1) & (MAX_TERM - 1);
        }
        if (m) {
            int32_t temp_A[MAX_TERM], temp_B[MAX_TERM];

            memcpy(temp_A, dpp->samplesA, sizeof (dpp->samplesA));
            memcpy(temp_B, dpp->samplesB, sizeof (dpp->samplesB));

            for (k = 0; k < MAX_TERM; k++) {
                dpp->samplesA[k] = temp_A[m];
                dpp->samplesB[k] = temp_B[m];
                m = (m + 1) & (MAX_TERM - 1);
            }
        }
        break;
    case -1:
        for (i = 0; i < nb_samples; i++) {
            int32_t sam_A, sam_B, tmp;

            sam_A = dpp->samplesA[0];
            samples_l[i] = tmp = (sam_B = samples_l[i]) - APPLY_WEIGHT(dpp->weightA, sam_A);
            UPDATE_WEIGHT_CLIP(dpp->weightA, dpp->delta, sam_A, tmp);

            samples_r[i] = tmp = (dpp->samplesA[0] = samples_r[i]) - APPLY_WEIGHT(dpp->weightB, sam_B);
            UPDATE_WEIGHT_CLIP(dpp->weightB, dpp->delta, sam_B, tmp);
        }
        break;
    case -2:
        for (i = 0; i < nb_samples; i++) {
            int32_t sam_A, sam_B, tmp;

            sam_B = dpp->samplesB[0];
            samples_r[i] = tmp = (sam_A = samples_r[i]) - APPLY_WEIGHT(dpp->weightB, sam_B);
            UPDATE_WEIGHT_CLIP(dpp->weightB, dpp->delta, sam_B, tmp);

            samples_l[i] = tmp = (dpp->samplesB[0] = samples_l[i]) - APPLY_WEIGHT(dpp->weightA, sam_A);
            UPDATE_WEIGHT_CLIP(dpp->weightA, dpp->delta, sam_A, tmp);
        }
        break;
    case -3:
        for (i = 0; i < nb_samples; i++) {
            int32_t sam_A, sam_B, tmp;

            sam_A = dpp->samplesA[0];
            sam_B = dpp->samplesB[0];

            dpp->samplesA[0] = tmp = samples_r[i];
            samples_r[i] = tmp -= APPLY_WEIGHT(dpp->weightB, sam_B);
            UPDATE_WEIGHT_CLIP(dpp->weightB, dpp->delta, sam_B, tmp);

            dpp->samplesB[0] = tmp = samples_l[i];
            samples_l[i] = tmp -= APPLY_WEIGHT(dpp->weightA, sam_A);
            UPDATE_WEIGHT_CLIP(dpp->weightA, dpp->delta, sam_A, tmp);
        }
        break;
    }
}

#define update_weight_d2(weight, delta, source, result) \
    if (source && result) \
        weight -= (((source ^ result) >> 29) & 4) - 2;

#define update_weight_clip_d2(weight, delta, source, result) \
    if (source && result) { \
        const int32_t s = (source ^ result) >> 31; \
        if ((weight = (weight ^ s) + (2 - s)) > 1024) weight = 1024; \
        weight = (weight ^ s) - s; \
    }

static void decorr_stereo_pass_id2(struct Decorr *dpp,
                                   int32_t *samples_l, int32_t *samples_r,
                                   int nb_samples)
{
    int i, m, k;

    switch (dpp->value) {
    case 17:
        for (i = 0; i < nb_samples; i++) {
            int32_t sam, tmp;

            sam = 2 * dpp->samplesA[0] - dpp->samplesA[1];
            dpp->samplesA[1] = dpp->samplesA[0];
            samples_l[i] = tmp = (dpp->samplesA[0] = samples_l[i]) - APPLY_WEIGHT_I(dpp->weightA, sam);
            update_weight_d2(dpp->weightA, dpp->delta, sam, tmp);

            sam = 2 * dpp->samplesB[0] - dpp->samplesB[1];
            dpp->samplesB[1] = dpp->samplesB[0];
            samples_r[i] = tmp = (dpp->samplesB[0] = samples_r[i]) - APPLY_WEIGHT_I(dpp->weightB, sam);
            update_weight_d2(dpp->weightB, dpp->delta, sam, tmp);
        }
        break;
    case 18:
        for (i = 0; i < nb_samples; i++) {
            int32_t sam, tmp;

            sam = dpp->samplesA[0] + ((dpp->samplesA[0] - dpp->samplesA[1]) >> 1);
            dpp->samplesA[1] = dpp->samplesA[0];
            samples_l[i] = tmp = (dpp->samplesA[0] = samples_l[i]) - APPLY_WEIGHT_I(dpp->weightA, sam);
            update_weight_d2(dpp->weightA, dpp->delta, sam, tmp);

            sam = dpp->samplesB[0] + ((dpp->samplesB[0] - dpp->samplesB[1]) >> 1);
            dpp->samplesB[1] = dpp->samplesB[0];
            samples_r[i] = tmp = (dpp->samplesB[0] = samples_r[i]) - APPLY_WEIGHT_I(dpp->weightB, sam);
            update_weight_d2(dpp->weightB, dpp->delta, sam, tmp);
        }
        break;
    default:
        for (m = 0, k = dpp->value & (MAX_TERM - 1), i = 0; i < nb_samples; i++) {
            int32_t sam, tmp;

            sam = dpp->samplesA[m];
            samples_l[i] = tmp = (dpp->samplesA[k] = samples_l[i]) - APPLY_WEIGHT_I(dpp->weightA, sam);
            update_weight_d2(dpp->weightA, dpp->delta, sam, tmp);

            sam = dpp->samplesB[m];
            samples_r[i] = tmp = (dpp->samplesB[k] = samples_r[i]) - APPLY_WEIGHT_I(dpp->weightB, sam);
            update_weight_d2(dpp->weightB, dpp->delta, sam, tmp);

            m = (m + 1) & (MAX_TERM - 1);
            k = (k + 1) & (MAX_TERM - 1);
        }

        if (m) {
            int32_t temp_A[MAX_TERM], temp_B[MAX_TERM];

            memcpy(temp_A, dpp->samplesA, sizeof(dpp->samplesA));
            memcpy(temp_B, dpp->samplesB, sizeof(dpp->samplesB));

            for (k = 0; k < MAX_TERM; k++) {
                dpp->samplesA[k] = temp_A[m];
                dpp->samplesB[k] = temp_B[m];
                m = (m + 1) & (MAX_TERM - 1);
            }
        }
        break;
    case -1:
        for (i = 0; i < nb_samples; i++) {
            int32_t sam_A, sam_B, tmp;

            sam_A = dpp->samplesA[0];
            samples_l[i] = tmp = (sam_B = samples_l[i]) - APPLY_WEIGHT_I(dpp->weightA, sam_A);
            update_weight_clip_d2(dpp->weightA, dpp->delta, sam_A, tmp);

            samples_r[i] = tmp = (dpp->samplesA[0] = samples_r[i]) - APPLY_WEIGHT_I(dpp->weightB, sam_B);
            update_weight_clip_d2(dpp->weightB, dpp->delta, sam_B, tmp);
        }
        break;
    case -2:
        for (i = 0; i < nb_samples; i++) {
            int32_t sam_A, sam_B, tmp;

            sam_B = dpp->samplesB[0];
            samples_r[i] = tmp = (sam_A = samples_r[i]) - APPLY_WEIGHT_I(dpp->weightB, sam_B);
            update_weight_clip_d2(dpp->weightB, dpp->delta, sam_B, tmp);

            samples_l[i] = tmp = (dpp->samplesB[0] = samples_l[i]) - APPLY_WEIGHT_I(dpp->weightA, sam_A);
            update_weight_clip_d2(dpp->weightA, dpp->delta, sam_A, tmp);
        }
        break;
    case -3:
        for (i = 0; i < nb_samples; i++) {
            int32_t sam_A, sam_B, tmp;

            sam_A = dpp->samplesA[0];
            sam_B = dpp->samplesB[0];

            dpp->samplesA[0] = tmp = samples_r[i];
            samples_r[i] = tmp -= APPLY_WEIGHT_I(dpp->weightB, sam_B);
            update_weight_clip_d2(dpp->weightB, dpp->delta, sam_B, tmp);

            dpp->samplesB[0] = tmp = samples_l[i];
            samples_l[i] = tmp -= APPLY_WEIGHT_I(dpp->weightA, sam_A);
            update_weight_clip_d2(dpp->weightA, dpp->delta, sam_A, tmp);
        }
        break;
    }
}

static void put_metadata_block(PutByteContext *pb, int flags, int size)
{
    if (size & 1)
        flags |= WP_IDF_ODD;

    bytestream2_put_byte(pb, flags);
    bytestream2_put_byte(pb, (size + 1) >> 1);
}

static int wavpack_encode_block(WavPackEncodeContext *s,
                                int32_t *samples_l, int32_t *samples_r,
                                uint8_t *out, int out_size)
{
    int block_size, start, end, data_size, tcount, temp, m = 0;
    int i, j, ret = 0, got_extra = 0, nb_samples = s->block_samples;
    uint32_t crc = 0xffffffffu;
    struct Decorr *dpp;
    PutByteContext pb;

    if (s->flags & WV_MONO_DATA) {
        CLEAR(s->w);
    }
    if (!(s->flags & WV_MONO) && s->optimize_mono) {
        int32_t lor = 0, diff = 0;

        for (i = 0; i < nb_samples; i++) {
            lor  |= samples_l[i] | samples_r[i];
            diff |= samples_l[i] - samples_r[i];

            if (lor && diff)
                break;
        }

        if (i == nb_samples && lor && !diff) {
            s->flags &= ~(WV_JOINT_STEREO | WV_CROSS_DECORR);
            s->flags |= WV_FALSE_STEREO;

            if (!s->false_stereo) {
                s->false_stereo = 1;
                s->num_terms = 0;
                CLEAR(s->w);
            }
        } else if (s->false_stereo) {
            s->false_stereo = 0;
            s->num_terms = 0;
            CLEAR(s->w);
        }
    }

    if (s->flags & SHIFT_MASK) {
        int shift = (s->flags & SHIFT_MASK) >> SHIFT_LSB;
        int mag = (s->flags & MAG_MASK) >> MAG_LSB;

        if (s->flags & WV_MONO_DATA)
            shift_mono(samples_l, nb_samples, shift);
        else
            shift_stereo(samples_l, samples_r, nb_samples, shift);

        if ((mag -= shift) < 0)
            s->flags &= ~MAG_MASK;
        else
            s->flags -= (1 << MAG_LSB) * shift;
    }

    if ((s->flags & WV_FLOAT_DATA) || (s->flags & MAG_MASK) >> MAG_LSB >= 24) {
        av_fast_padded_malloc(&s->orig_l, &s->orig_l_size, sizeof(int32_t) * nb_samples);
        memcpy(s->orig_l, samples_l, sizeof(int32_t) * nb_samples);
        if (!(s->flags & WV_MONO_DATA)) {
            av_fast_padded_malloc(&s->orig_r, &s->orig_r_size, sizeof(int32_t) * nb_samples);
            memcpy(s->orig_r, samples_r, sizeof(int32_t) * nb_samples);
        }

        if (s->flags & WV_FLOAT_DATA)
            got_extra = scan_float(s, samples_l, samples_r, nb_samples);
        else
            got_extra = scan_int32(s, samples_l, samples_r, nb_samples);
        s->num_terms = 0;
    } else {
        scan_int23(s, samples_l, samples_r, nb_samples);
        if (s->shift != s->int32_zeros + s->int32_ones + s->int32_dups) {
            s->shift = s->int32_zeros + s->int32_ones + s->int32_dups;
            s->num_terms = 0;
        }
    }

    if (!s->num_passes && !s->num_terms) {
        s->num_passes = 1;

        if (s->flags & WV_MONO_DATA)
            ret = wv_mono(s, samples_l, 1, 0);
        else
            ret = wv_stereo(s, samples_l, samples_r, 1, 0);

        s->num_passes = 0;
    }
    if (s->flags & WV_MONO_DATA) {
        for (i = 0; i < nb_samples; i++)
            crc += (crc << 1) + samples_l[i];

        if (s->num_passes)
            ret = wv_mono(s, samples_l, !s->num_terms, 1);
    } else {
        for (i = 0; i < nb_samples; i++)
            crc += (crc << 3) + (samples_l[i] << 1) + samples_l[i] + samples_r[i];

        if (s->num_passes)
            ret = wv_stereo(s, samples_l, samples_r, !s->num_terms, 1);
    }
    if (ret < 0)
        return ret;

    if (!s->ch_offset)
        s->flags |= WV_INITIAL_BLOCK;

    s->ch_offset += 1 + !(s->flags & WV_MONO);

    if (s->ch_offset == s->avctx->channels)
        s->flags |= WV_FINAL_BLOCK;

    bytestream2_init_writer(&pb, out, out_size);
    bytestream2_put_le32(&pb, MKTAG('w', 'v', 'p', 'k'));
    bytestream2_put_le32(&pb, 0);
    bytestream2_put_le16(&pb, 0x410);
    bytestream2_put_le16(&pb, 0);
    bytestream2_put_le32(&pb, 0);
    bytestream2_put_le32(&pb, s->sample_index);
    bytestream2_put_le32(&pb, nb_samples);
    bytestream2_put_le32(&pb, s->flags);
    bytestream2_put_le32(&pb, crc);

    if (s->flags & WV_INITIAL_BLOCK &&
        s->avctx->channel_layout != AV_CH_LAYOUT_MONO &&
        s->avctx->channel_layout != AV_CH_LAYOUT_STEREO) {
        put_metadata_block(&pb, WP_ID_CHANINFO, 5);
        bytestream2_put_byte(&pb, s->avctx->channels);
        bytestream2_put_le32(&pb, s->avctx->channel_layout);
        bytestream2_put_byte(&pb, 0);
    }

    if ((s->flags & SRATE_MASK) == SRATE_MASK) {
        put_metadata_block(&pb, WP_ID_SAMPLE_RATE, 3);
        bytestream2_put_le24(&pb, s->avctx->sample_rate);
        bytestream2_put_byte(&pb, 0);
    }

    put_metadata_block(&pb, WP_ID_DECTERMS, s->num_terms);
    for (i = 0; i < s->num_terms; i++) {
        struct Decorr *dpp = &s->decorr_passes[i];
        bytestream2_put_byte(&pb, ((dpp->value + 5) & 0x1f) | ((dpp->delta << 5) & 0xe0));
    }
    if (s->num_terms & 1)
        bytestream2_put_byte(&pb, 0);

#define WRITE_DECWEIGHT(type) do {            \
        temp = store_weight(type);    \
        bytestream2_put_byte(&pb, temp);      \
        type = restore_weight(temp);  \
    } while (0)

    bytestream2_put_byte(&pb, WP_ID_DECWEIGHTS);
    bytestream2_put_byte(&pb, 0);
    start = bytestream2_tell_p(&pb);
    for (i = s->num_terms - 1; i >= 0; --i) {
        struct Decorr *dpp = &s->decorr_passes[i];

        if (store_weight(dpp->weightA) ||
            (!(s->flags & WV_MONO_DATA) && store_weight(dpp->weightB)))
                break;
    }
    tcount = i + 1;
    for (i = 0; i < s->num_terms; i++) {
        struct Decorr *dpp = &s->decorr_passes[i];
        if (i < tcount) {
            WRITE_DECWEIGHT(dpp->weightA);
            if (!(s->flags & WV_MONO_DATA))
                WRITE_DECWEIGHT(dpp->weightB);
        } else {
            dpp->weightA = dpp->weightB = 0;
        }
    }
    end = bytestream2_tell_p(&pb);
    out[start - 2] = WP_ID_DECWEIGHTS | (((end - start) & 1) ? WP_IDF_ODD: 0);
    out[start - 1] = (end - start + 1) >> 1;
    if ((end - start) & 1)
        bytestream2_put_byte(&pb, 0);

#define WRITE_DECSAMPLE(type) do {        \
        temp = log2s(type);               \
        type = wp_exp2(temp);             \
        bytestream2_put_le16(&pb, temp);  \
    } while (0)

    bytestream2_put_byte(&pb, WP_ID_DECSAMPLES);
    bytestream2_put_byte(&pb, 0);
    start = bytestream2_tell_p(&pb);
    for (i = 0; i < s->num_terms; i++) {
        struct Decorr *dpp = &s->decorr_passes[i];
        if (i == 0) {
            if (dpp->value > MAX_TERM) {
                WRITE_DECSAMPLE(dpp->samplesA[0]);
                WRITE_DECSAMPLE(dpp->samplesA[1]);
                if (!(s->flags & WV_MONO_DATA)) {
                    WRITE_DECSAMPLE(dpp->samplesB[0]);
                    WRITE_DECSAMPLE(dpp->samplesB[1]);
                }
            } else if (dpp->value < 0) {
                WRITE_DECSAMPLE(dpp->samplesA[0]);
                WRITE_DECSAMPLE(dpp->samplesB[0]);
            } else {
                for (j = 0; j < dpp->value; j++) {
                    WRITE_DECSAMPLE(dpp->samplesA[j]);
                    if (!(s->flags & WV_MONO_DATA))
                        WRITE_DECSAMPLE(dpp->samplesB[j]);
                }
            }
        } else {
            CLEAR(dpp->samplesA);
            CLEAR(dpp->samplesB);
        }
    }
    end = bytestream2_tell_p(&pb);
    out[start - 1] = (end - start) >> 1;

#define WRITE_CHAN_ENTROPY(chan) do {               \
        for (i = 0; i < 3; i++) {                   \
            temp = wp_log2(s->w.c[chan].median[i]); \
            bytestream2_put_le16(&pb, temp);        \
            s->w.c[chan].median[i] = wp_exp2(temp); \
        }                                           \
    } while (0)

    put_metadata_block(&pb, WP_ID_ENTROPY, 6 * (1 + (!(s->flags & WV_MONO_DATA))));
    WRITE_CHAN_ENTROPY(0);
    if (!(s->flags & WV_MONO_DATA))
        WRITE_CHAN_ENTROPY(1);

    if (s->flags & WV_FLOAT_DATA) {
        put_metadata_block(&pb, WP_ID_FLOATINFO, 4);
        bytestream2_put_byte(&pb, s->float_flags);
        bytestream2_put_byte(&pb, s->float_shift);
        bytestream2_put_byte(&pb, s->float_max_exp);
        bytestream2_put_byte(&pb, 127);
    }

    if (s->flags & WV_INT32_DATA) {
        put_metadata_block(&pb, WP_ID_INT32INFO, 4);
        bytestream2_put_byte(&pb, s->int32_sent_bits);
        bytestream2_put_byte(&pb, s->int32_zeros);
        bytestream2_put_byte(&pb, s->int32_ones);
        bytestream2_put_byte(&pb, s->int32_dups);
    }

    if (s->flags & WV_MONO_DATA && !s->num_passes) {
        for (i = 0; i < nb_samples; i++) {
            int32_t code = samples_l[i];

            for (tcount = s->num_terms, dpp = s->decorr_passes; tcount--; dpp++) {
                int32_t sam;

                if (dpp->value > MAX_TERM) {
                    if (dpp->value & 1)
                        sam = 2 * dpp->samplesA[0] - dpp->samplesA[1];
                    else
                        sam = (3 * dpp->samplesA[0] - dpp->samplesA[1]) >> 1;

                    dpp->samplesA[1] = dpp->samplesA[0];
                    dpp->samplesA[0] = code;
                } else {
                    sam = dpp->samplesA[m];
                    dpp->samplesA[(m + dpp->value) & (MAX_TERM - 1)] = code;
                }

                code -= APPLY_WEIGHT(dpp->weightA, sam);
                UPDATE_WEIGHT(dpp->weightA, dpp->delta, sam, code);
            }

            m = (m + 1) & (MAX_TERM - 1);
            samples_l[i] = code;
        }
        if (m) {
            for (tcount = s->num_terms, dpp = s->decorr_passes; tcount--; dpp++)
                if (dpp->value > 0 && dpp->value <= MAX_TERM) {
                int32_t temp_A[MAX_TERM], temp_B[MAX_TERM];
                int k;

                memcpy(temp_A, dpp->samplesA, sizeof(dpp->samplesA));
                memcpy(temp_B, dpp->samplesB, sizeof(dpp->samplesB));

                for (k = 0; k < MAX_TERM; k++) {
                    dpp->samplesA[k] = temp_A[m];
                    dpp->samplesB[k] = temp_B[m];
                    m = (m + 1) & (MAX_TERM - 1);
                }
            }
        }
    } else if (!s->num_passes) {
        if (s->flags & WV_JOINT_STEREO) {
            for (i = 0; i < nb_samples; i++)
                samples_r[i] += ((samples_l[i] -= samples_r[i]) >> 1);
        }

        for (i = 0; i < s->num_terms; i++) {
            struct Decorr *dpp = &s->decorr_passes[i];
            if (((s->flags & MAG_MASK) >> MAG_LSB) >= 16 || dpp->delta != 2)
                decorr_stereo_pass2(dpp, samples_l, samples_r, nb_samples);
            else
                decorr_stereo_pass_id2(dpp, samples_l, samples_r, nb_samples);
        }
    }

    bytestream2_put_byte(&pb, WP_ID_DATA | WP_IDF_LONG);
    init_put_bits(&s->pb, pb.buffer + 3, bytestream2_get_bytes_left_p(&pb));
    if (s->flags & WV_MONO_DATA) {
        for (i = 0; i < nb_samples; i++)
            wavpack_encode_sample(s, &s->w.c[0], s->samples[0][i]);
    } else {
        for (i = 0; i < nb_samples; i++) {
            wavpack_encode_sample(s, &s->w.c[0], s->samples[0][i]);
            wavpack_encode_sample(s, &s->w.c[1], s->samples[1][i]);
        }
    }
    encode_flush(s);
    flush_put_bits(&s->pb);
    data_size = put_bits_count(&s->pb) >> 3;
    bytestream2_put_le24(&pb, (data_size + 1) >> 1);
    bytestream2_skip_p(&pb, data_size);
    if (data_size & 1)
        bytestream2_put_byte(&pb, 0);

    if (got_extra) {
        bytestream2_put_byte(&pb, WP_ID_EXTRABITS | WP_IDF_LONG);
        init_put_bits(&s->pb, pb.buffer + 7, bytestream2_get_bytes_left_p(&pb));
        if (s->flags & WV_FLOAT_DATA)
            pack_float(s, s->orig_l, s->orig_r, nb_samples);
        else
            pack_int32(s, s->orig_l, s->orig_r, nb_samples);
        flush_put_bits(&s->pb);
        data_size = put_bits_count(&s->pb) >> 3;
        bytestream2_put_le24(&pb, (data_size + 5) >> 1);
        bytestream2_put_le32(&pb, s->crc_x);
        bytestream2_skip_p(&pb, data_size);
        if (data_size & 1)
            bytestream2_put_byte(&pb, 0);
    }

    block_size = bytestream2_tell_p(&pb);
    AV_WL32(out + 4, block_size - 8);

    av_assert0(!bytestream2_get_eof(&pb));

    return block_size;
}

static void fill_buffer(WavPackEncodeContext *s,
                        const int8_t *src, int32_t *dst,
                        int nb_samples)
{
    int i;

#define COPY_SAMPLES(type, offset, shift) do {            \
        const type *sptr = (const type *)src;             \
        for (i = 0; i < nb_samples; i++)                  \
            dst[i] = (sptr[i] - offset) >> shift;         \
    } while (0)

    switch (s->avctx->sample_fmt) {
    case AV_SAMPLE_FMT_U8P:
        COPY_SAMPLES(int8_t, 0x80, 0);
        break;
    case AV_SAMPLE_FMT_S16P:
        COPY_SAMPLES(int16_t, 0, 0);
        break;
    case AV_SAMPLE_FMT_S32P:
        if (s->avctx->bits_per_raw_sample <= 24) {
            COPY_SAMPLES(int32_t, 0, 8);
            break;
        }
    case AV_SAMPLE_FMT_FLTP:
        memcpy(dst, src, nb_samples * 4);
    }
}

static void set_samplerate(WavPackEncodeContext *s)
{
    int i;

    for (i = 0; i < 15; i++) {
        if (wv_rates[i] == s->avctx->sample_rate)
            break;
    }

    s->flags = i << SRATE_LSB;
}

static int wavpack_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                                const AVFrame *frame, int *got_packet_ptr)
{
    WavPackEncodeContext *s = avctx->priv_data;
    int buf_size, ret;
    uint8_t *buf;

    s->block_samples = frame->nb_samples;
    av_fast_padded_malloc(&s->samples[0], &s->samples_size[0],
                          sizeof(int32_t) * s->block_samples);
    if (!s->samples[0])
        return AVERROR(ENOMEM);
    if (avctx->channels > 1) {
        av_fast_padded_malloc(&s->samples[1], &s->samples_size[1],
                              sizeof(int32_t) * s->block_samples);
        if (!s->samples[1])
            return AVERROR(ENOMEM);
    }

    buf_size = s->block_samples * avctx->channels * 8
             + 200 * avctx->channels /* for headers */;
    if ((ret = ff_alloc_packet2(avctx, avpkt, buf_size, 0)) < 0)
        return ret;
    buf = avpkt->data;

    for (s->ch_offset = 0; s->ch_offset < avctx->channels;) {
        set_samplerate(s);

        switch (s->avctx->sample_fmt) {
        case AV_SAMPLE_FMT_S16P: s->flags |= 1; break;
        case AV_SAMPLE_FMT_S32P: s->flags |= 3 - (s->avctx->bits_per_raw_sample <= 24); break;
        case AV_SAMPLE_FMT_FLTP: s->flags |= 3 | WV_FLOAT_DATA;
        }

        fill_buffer(s, frame->extended_data[s->ch_offset], s->samples[0], s->block_samples);
        if (avctx->channels - s->ch_offset == 1) {
            s->flags |= WV_MONO;
        } else {
            s->flags |= WV_CROSS_DECORR;
            fill_buffer(s, frame->extended_data[s->ch_offset + 1], s->samples[1], s->block_samples);
        }

        s->flags += (1 << MAG_LSB) * ((s->flags & 3) * 8 + 7);

        if ((ret = wavpack_encode_block(s, s->samples[0], s->samples[1],
                                        buf, buf_size)) < 0)
            return ret;

        buf      += ret;
        buf_size -= ret;
    }
    s->sample_index += frame->nb_samples;

    avpkt->pts      = frame->pts;
    avpkt->size     = buf - avpkt->data;
    avpkt->duration = ff_samples_to_time_base(avctx, frame->nb_samples);
    *got_packet_ptr = 1;
    return 0;
}

static av_cold int wavpack_encode_close(AVCodecContext *avctx)
{
    WavPackEncodeContext *s = avctx->priv_data;
    int i;

    for (i = 0; i < MAX_TERMS + 2; i++) {
        av_freep(&s->sampleptrs[i][0]);
        av_freep(&s->sampleptrs[i][1]);
        s->sampleptrs_size[i][0] = s->sampleptrs_size[i][1] = 0;
    }

    for (i = 0; i < 2; i++) {
        av_freep(&s->samples[i]);
        s->samples_size[i] = 0;

        av_freep(&s->best_buffer[i]);
        s->best_buffer_size[i] = 0;

        av_freep(&s->temp_buffer[i][0]);
        av_freep(&s->temp_buffer[i][1]);
        s->temp_buffer_size[i][0] = s->temp_buffer_size[i][1] = 0;
    }

    av_freep(&s->js_left);
    av_freep(&s->js_right);
    s->js_left_size = s->js_right_size = 0;

    av_freep(&s->orig_l);
    av_freep(&s->orig_r);
    s->orig_l_size = s->orig_r_size = 0;

    return 0;
}

#define OFFSET(x) offsetof(WavPackEncodeContext, x)
#define FLAGS AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM
static const AVOption options[] = {
    { "joint_stereo",  "", OFFSET(joint), AV_OPT_TYPE_BOOL, {.i64=-1}, -1, 1, FLAGS },
    { "optimize_mono", "", OFFSET(optimize_mono), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { NULL },
};

static const AVClass wavpack_encoder_class = {
    .class_name = "WavPack encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_wavpack_encoder = {
    .name           = "wavpack",
    .long_name      = NULL_IF_CONFIG_SMALL("WavPack"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_WAVPACK,
    .priv_data_size = sizeof(WavPackEncodeContext),
    .priv_class     = &wavpack_encoder_class,
    .init           = wavpack_encode_init,
    .encode2        = wavpack_encode_frame,
    .close          = wavpack_encode_close,
    .capabilities   = AV_CODEC_CAP_SMALL_LAST_FRAME,
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_U8P,
                                                     AV_SAMPLE_FMT_S16P,
                                                     AV_SAMPLE_FMT_S32P,
                                                     AV_SAMPLE_FMT_FLTP,
                                                     AV_SAMPLE_FMT_NONE },
};
