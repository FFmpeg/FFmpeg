/*
 * Copyright (c) 2007-2008 CSIRO
 * Copyright (c) 2007-2009 Xiph.Org Foundation
 * Copyright (c) 2008-2009 Gregory Maxwell
 * Copyright (c) 2012 Andrew D'Addesio
 * Copyright (c) 2013-2014 Mozilla Corporation
 * Copyright (c) 2017 Rostislav Pehlivanov <atomnuker@gmail.com>
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

#include "opustab.h"
#include "opus_pvq.h"

#define CELT_PVQ_U(n, k) (ff_celt_pvq_u_row[FFMIN(n, k)][FFMAX(n, k)])
#define CELT_PVQ_V(n, k) (CELT_PVQ_U(n, k) + CELT_PVQ_U(n, (k) + 1))

static inline int16_t celt_cos(int16_t x)
{
    x = (MUL16(x, x) + 4096) >> 13;
    x = (32767-x) + ROUND_MUL16(x, (-7651 + ROUND_MUL16(x, (8277 + ROUND_MUL16(-626, x)))));
    return x + 1;
}

static inline int celt_log2tan(int isin, int icos)
{
    int lc, ls;
    lc = opus_ilog(icos);
    ls = opus_ilog(isin);
    icos <<= 15 - lc;
    isin <<= 15 - ls;
    return (ls << 11) - (lc << 11) +
           ROUND_MUL16(isin, ROUND_MUL16(isin, -2597) + 7932) -
           ROUND_MUL16(icos, ROUND_MUL16(icos, -2597) + 7932);
}

static inline int celt_bits2pulses(const uint8_t *cache, int bits)
{
    // TODO: Find the size of cache and make it into an array in the parameters list
    int i, low = 0, high;

    high = cache[0];
    bits--;

    for (i = 0; i < 6; i++) {
        int center = (low + high + 1) >> 1;
        if (cache[center] >= bits)
            high = center;
        else
            low = center;
    }

    return (bits - (low == 0 ? -1 : cache[low]) <= cache[high] - bits) ? low : high;
}

static inline int celt_pulses2bits(const uint8_t *cache, int pulses)
{
    // TODO: Find the size of cache and make it into an array in the parameters list
   return (pulses == 0) ? 0 : cache[pulses] + 1;
}

static inline void celt_normalize_residual(const int * av_restrict iy, float * av_restrict X,
                                           int N, float g)
{
    int i;
    for (i = 0; i < N; i++)
        X[i] = g * iy[i];
}

static void celt_exp_rotation_impl(float *X, uint32_t len, uint32_t stride,
                                   float c, float s)
{
    float *Xptr;
    int i;

    Xptr = X;
    for (i = 0; i < len - stride; i++) {
        float x1     = Xptr[0];
        float x2     = Xptr[stride];
        Xptr[stride] = c * x2 + s * x1;
        *Xptr++      = c * x1 - s * x2;
    }

    Xptr = &X[len - 2 * stride - 1];
    for (i = len - 2 * stride - 1; i >= 0; i--) {
        float x1     = Xptr[0];
        float x2     = Xptr[stride];
        Xptr[stride] = c * x2 + s * x1;
        *Xptr--      = c * x1 - s * x2;
    }
}

static inline void celt_exp_rotation(float *X, uint32_t len,
                                     uint32_t stride, uint32_t K,
                                     enum CeltSpread spread, const int encode)
{
    uint32_t stride2 = 0;
    float c, s;
    float gain, theta;
    int i;

    if (2*K >= len || spread == CELT_SPREAD_NONE)
        return;

    gain = (float)len / (len + (20 - 5*spread) * K);
    theta = M_PI * gain * gain / 4;

    c = cosf(theta);
    s = sinf(theta);

    if (len >= stride << 3) {
        stride2 = 1;
        /* This is just a simple (equivalent) way of computing sqrt(len/stride) with rounding.
        It's basically incrementing long as (stride2+0.5)^2 < len/stride. */
        while ((stride2 * stride2 + stride2) * stride + (stride >> 2) < len)
            stride2++;
    }

    len /= stride;
    for (i = 0; i < stride; i++) {
        if (encode) {
            celt_exp_rotation_impl(X + i * len, len, 1, c, -s);
            if (stride2)
                celt_exp_rotation_impl(X + i * len, len, stride2, s, -c);
        } else {
            if (stride2)
                celt_exp_rotation_impl(X + i * len, len, stride2, s, c);
            celt_exp_rotation_impl(X + i * len, len, 1, c, s);
        }
    }
}

static inline uint32_t celt_extract_collapse_mask(const int *iy, uint32_t N, uint32_t B)
{
    int i, j, N0 = N / B;
    uint32_t collapse_mask = 0;

    if (B <= 1)
        return 1;

    for (i = 0; i < B; i++)
        for (j = 0; j < N0; j++)
            collapse_mask |= (!!iy[i*N0+j]) << i;
    return collapse_mask;
}

static inline void celt_stereo_merge(float *X, float *Y, float mid, int N)
{
    int i;
    float xp = 0, side = 0;
    float E[2];
    float mid2;
    float gain[2];

    /* Compute the norm of X+Y and X-Y as |X|^2 + |Y|^2 +/- sum(xy) */
    for (i = 0; i < N; i++) {
        xp   += X[i] * Y[i];
        side += Y[i] * Y[i];
    }

    /* Compensating for the mid normalization */
    xp *= mid;
    mid2 = mid;
    E[0] = mid2 * mid2 + side - 2 * xp;
    E[1] = mid2 * mid2 + side + 2 * xp;
    if (E[0] < 6e-4f || E[1] < 6e-4f) {
        for (i = 0; i < N; i++)
            Y[i] = X[i];
        return;
    }

    gain[0] = 1.0f / sqrtf(E[0]);
    gain[1] = 1.0f / sqrtf(E[1]);

    for (i = 0; i < N; i++) {
        float value[2];
        /* Apply mid scaling (side is already scaled) */
        value[0] = mid * X[i];
        value[1] = Y[i];
        X[i] = gain[0] * (value[0] - value[1]);
        Y[i] = gain[1] * (value[0] + value[1]);
    }
}

static void celt_interleave_hadamard(float *tmp, float *X, int N0,
                                     int stride, int hadamard)
{
    int i, j, N = N0*stride;
    const uint8_t *order = &ff_celt_hadamard_order[hadamard ? stride - 2 : 30];

    for (i = 0; i < stride; i++)
        for (j = 0; j < N0; j++)
            tmp[j*stride+i] = X[order[i]*N0+j];

    memcpy(X, tmp, N*sizeof(float));
}

static void celt_deinterleave_hadamard(float *tmp, float *X, int N0,
                                       int stride, int hadamard)
{
    int i, j, N = N0*stride;
    const uint8_t *order = &ff_celt_hadamard_order[hadamard ? stride - 2 : 30];

    for (i = 0; i < stride; i++)
        for (j = 0; j < N0; j++)
            tmp[order[i]*N0+j] = X[j*stride+i];

    memcpy(X, tmp, N*sizeof(float));
}

static void celt_haar1(float *X, int N0, int stride)
{
    int i, j;
    N0 >>= 1;
    for (i = 0; i < stride; i++) {
        for (j = 0; j < N0; j++) {
            float x0 = X[stride * (2 * j + 0) + i];
            float x1 = X[stride * (2 * j + 1) + i];
            X[stride * (2 * j + 0) + i] = (x0 + x1) * M_SQRT1_2;
            X[stride * (2 * j + 1) + i] = (x0 - x1) * M_SQRT1_2;
        }
    }
}

static inline int celt_compute_qn(int N, int b, int offset, int pulse_cap,
                                  int stereo)
{
    int qn, qb;
    int N2 = 2 * N - 1;
    if (stereo && N == 2)
        N2--;

    /* The upper limit ensures that in a stereo split with itheta==16384, we'll
     * always have enough bits left over to code at least one pulse in the
     * side; otherwise it would collapse, since it doesn't get folded. */
    qb = FFMIN3(b - pulse_cap - (4 << 3), (b + N2 * offset) / N2, 8 << 3);
    qn = (qb < (1 << 3 >> 1)) ? 1 : ((ff_celt_qn_exp2[qb & 0x7] >> (14 - (qb >> 3))) + 1) >> 1 << 1;
    return qn;
}

/* Convert the quantized vector to an index */
static inline uint32_t celt_icwrsi(uint32_t N, uint32_t K, const int *y)
{
    int i, idx = 0, sum = 0;
    for (i = N - 1; i >= 0; i--) {
        const uint32_t i_s = CELT_PVQ_U(N - i, sum + FFABS(y[i]) + 1);
        idx += CELT_PVQ_U(N - i, sum) + (y[i] < 0)*i_s;
        sum += FFABS(y[i]);
    }
    return idx;
}

// this code was adapted from libopus
static inline uint64_t celt_cwrsi(uint32_t N, uint32_t K, uint32_t i, int *y)
{
    uint64_t norm = 0;
    uint32_t q, p;
    int s, val;
    int k0;

    while (N > 2) {
        /*Lots of pulses case:*/
        if (K >= N) {
            const uint32_t *row = ff_celt_pvq_u_row[N];

            /* Are the pulses in this dimension negative? */
            p  = row[K + 1];
            s  = -(i >= p);
            i -= p & s;

            /*Count how many pulses were placed in this dimension.*/
            k0 = K;
            q = row[N];
            if (q > i) {
                K = N;
                do {
                    p = ff_celt_pvq_u_row[--K][N];
                } while (p > i);
            } else
                for (p = row[K]; p > i; p = row[K])
                    K--;

            i    -= p;
            val   = (k0 - K + s) ^ s;
            norm += val * val;
            *y++  = val;
        } else { /*Lots of dimensions case:*/
            /*Are there any pulses in this dimension at all?*/
            p = ff_celt_pvq_u_row[K    ][N];
            q = ff_celt_pvq_u_row[K + 1][N];

            if (p <= i && i < q) {
                i -= p;
                *y++ = 0;
            } else {
                /*Are the pulses in this dimension negative?*/
                s  = -(i >= q);
                i -= q & s;

                /*Count how many pulses were placed in this dimension.*/
                k0 = K;
                do p = ff_celt_pvq_u_row[--K][N];
                while (p > i);

                i    -= p;
                val   = (k0 - K + s) ^ s;
                norm += val * val;
                *y++  = val;
            }
        }
        N--;
    }

    /* N == 2 */
    p  = 2 * K + 1;
    s  = -(i >= p);
    i -= p & s;
    k0 = K;
    K  = (i + 1) / 2;

    if (K)
        i -= 2 * K - 1;

    val   = (k0 - K + s) ^ s;
    norm += val * val;
    *y++  = val;

    /* N==1 */
    s     = -i;
    val   = (K + s) ^ s;
    norm += val * val;
    *y    = val;

    return norm;
}

static inline void celt_encode_pulses(OpusRangeCoder *rc, int *y, uint32_t N, uint32_t K)
{
    ff_opus_rc_enc_uint(rc, celt_icwrsi(N, K, y), CELT_PVQ_V(N, K));
}

static inline float celt_decode_pulses(OpusRangeCoder *rc, int *y, uint32_t N, uint32_t K)
{
    const uint32_t idx = ff_opus_rc_dec_uint(rc, CELT_PVQ_V(N, K));
    return celt_cwrsi(N, K, idx, y);
}

/*
 * Faster than libopus's search, operates entirely in the signed domain.
 * Slightly worse/better depending on N, K and the input vector.
 */
static float ppp_pvq_search_c(float *X, int *y, int K, int N)
{
    int i, y_norm = 0;
    float res = 0.0f, xy_norm = 0.0f;

    for (i = 0; i < N; i++)
        res += FFABS(X[i]);

    res = K/(res + FLT_EPSILON);

    for (i = 0; i < N; i++) {
        y[i] = lrintf(res*X[i]);
        y_norm  += y[i]*y[i];
        xy_norm += y[i]*X[i];
        K -= FFABS(y[i]);
    }

    while (K) {
        int max_idx = 0, phase = FFSIGN(K);
        float max_num = 0.0f;
        float max_den = 1.0f;
        y_norm += 1.0f;

        for (i = 0; i < N; i++) {
            /* If the sum has been overshot and the best place has 0 pulses allocated
             * to it, attempting to decrease it further will actually increase the
             * sum. Prevent this by disregarding any 0 positions when decrementing. */
            const int ca = 1 ^ ((y[i] == 0) & (phase < 0));
            const int y_new = y_norm  + 2*phase*FFABS(y[i]);
            float xy_new = xy_norm + 1*phase*FFABS(X[i]);
            xy_new = xy_new * xy_new;
            if (ca && (max_den*xy_new) > (y_new*max_num)) {
                max_den = y_new;
                max_num = xy_new;
                max_idx = i;
            }
        }

        K -= phase;

        phase *= FFSIGN(X[max_idx]);
        xy_norm += 1*phase*X[max_idx];
        y_norm  += 2*phase*y[max_idx];
        y[max_idx] += phase;
    }

    return (float)y_norm;
}

static uint32_t celt_alg_quant(OpusRangeCoder *rc, float *X, uint32_t N, uint32_t K,
                               enum CeltSpread spread, uint32_t blocks, float gain,
                               CeltPVQ *pvq)
{
    int *y = pvq->qcoeff;

    celt_exp_rotation(X, N, blocks, K, spread, 1);
    gain /= sqrtf(pvq->pvq_search(X, y, K, N));
    celt_encode_pulses(rc, y,  N, K);
    celt_normalize_residual(y, X, N, gain);
    celt_exp_rotation(X, N, blocks, K, spread, 0);
    return celt_extract_collapse_mask(y, N, blocks);
}

/** Decode pulse vector and combine the result with the pitch vector to produce
    the final normalised signal in the current band. */
static uint32_t celt_alg_unquant(OpusRangeCoder *rc, float *X, uint32_t N, uint32_t K,
                                 enum CeltSpread spread, uint32_t blocks, float gain,
                                 CeltPVQ *pvq)
{
    int *y = pvq->qcoeff;

    gain /= sqrtf(celt_decode_pulses(rc, y, N, K));
    celt_normalize_residual(y, X, N, gain);
    celt_exp_rotation(X, N, blocks, K, spread, 0);
    return celt_extract_collapse_mask(y, N, blocks);
}

static int celt_calc_theta(const float *X, const float *Y, int coupling, int N)
{
    int i;
    float e[2] = { 0.0f, 0.0f };
    if (coupling) { /* Coupling case */
        for (i = 0; i < N; i++) {
            e[0] += (X[i] + Y[i])*(X[i] + Y[i]);
            e[1] += (X[i] - Y[i])*(X[i] - Y[i]);
        }
    } else {
        for (i = 0; i < N; i++) {
            e[0] += X[i]*X[i];
            e[1] += Y[i]*Y[i];
        }
    }
    return lrintf(32768.0f*atan2f(sqrtf(e[1]), sqrtf(e[0]))/M_PI);
}

static void celt_stereo_is_decouple(float *X, float *Y, float e_l, float e_r, int N)
{
    int i;
    const float energy_n = 1.0f/(sqrtf(e_l*e_l + e_r*e_r) + FLT_EPSILON);
    e_l *= energy_n;
    e_r *= energy_n;
    for (i = 0; i < N; i++)
        X[i] = e_l*X[i] + e_r*Y[i];
}

static void celt_stereo_ms_decouple(float *X, float *Y, int N)
{
    int i;
    for (i = 0; i < N; i++) {
        const float Xret = X[i];
        X[i] = (X[i] + Y[i])*M_SQRT1_2;
        Y[i] = (Y[i] - Xret)*M_SQRT1_2;
    }
}

static av_always_inline uint32_t quant_band_template(CeltPVQ *pvq, CeltFrame *f,
                                                     OpusRangeCoder *rc,
                                                     const int band, float *X,
                                                     float *Y, int N, int b,
                                                     uint32_t blocks, float *lowband,
                                                     int duration, float *lowband_out,
                                                     int level, float gain,
                                                     float *lowband_scratch,
                                                     int fill, int quant)
{
    int i;
    const uint8_t *cache;
    int stereo = !!Y, split = stereo;
    int imid = 0, iside = 0;
    uint32_t N0 = N;
    int N_B = N / blocks;
    int N_B0 = N_B;
    int B0 = blocks;
    int time_divide = 0;
    int recombine = 0;
    int inv = 0;
    float mid = 0, side = 0;
    int longblocks = (B0 == 1);
    uint32_t cm = 0;

    if (N == 1) {
        float *x = X;
        for (i = 0; i <= stereo; i++) {
            int sign = 0;
            if (f->remaining2 >= 1 << 3) {
                if (quant) {
                    sign = x[0] < 0;
                    ff_opus_rc_put_raw(rc, sign, 1);
                } else {
                    sign = ff_opus_rc_get_raw(rc, 1);
                }
                f->remaining2 -= 1 << 3;
            }
            x[0] = 1.0f - 2.0f*sign;
            x = Y;
        }
        if (lowband_out)
            lowband_out[0] = X[0];
        return 1;
    }

    if (!stereo && level == 0) {
        int tf_change = f->tf_change[band];
        int k;
        if (tf_change > 0)
            recombine = tf_change;
        /* Band recombining to increase frequency resolution */

        if (lowband &&
            (recombine || ((N_B & 1) == 0 && tf_change < 0) || B0 > 1)) {
            for (i = 0; i < N; i++)
                lowband_scratch[i] = lowband[i];
            lowband = lowband_scratch;
        }

        for (k = 0; k < recombine; k++) {
            if (quant || lowband)
                celt_haar1(quant ? X : lowband, N >> k, 1 << k);
            fill = ff_celt_bit_interleave[fill & 0xF] | ff_celt_bit_interleave[fill >> 4] << 2;
        }
        blocks >>= recombine;
        N_B <<= recombine;

        /* Increasing the time resolution */
        while ((N_B & 1) == 0 && tf_change < 0) {
            if (quant || lowband)
                celt_haar1(quant ? X : lowband, N_B, blocks);
            fill |= fill << blocks;
            blocks <<= 1;
            N_B >>= 1;
            time_divide++;
            tf_change++;
        }
        B0 = blocks;
        N_B0 = N_B;

        /* Reorganize the samples in time order instead of frequency order */
        if (B0 > 1 && (quant || lowband))
            celt_deinterleave_hadamard(pvq->hadamard_tmp, quant ? X : lowband,
                                       N_B >> recombine, B0 << recombine,
                                       longblocks);
    }

    /* If we need 1.5 more bit than we can produce, split the band in two. */
    cache = ff_celt_cache_bits +
            ff_celt_cache_index[(duration + 1) * CELT_MAX_BANDS + band];
    if (!stereo && duration >= 0 && b > cache[cache[0]] + 12 && N > 2) {
        N >>= 1;
        Y = X + N;
        split = 1;
        duration -= 1;
        if (blocks == 1)
            fill = (fill & 1) | (fill << 1);
        blocks = (blocks + 1) >> 1;
    }

    if (split) {
        int qn;
        int itheta = quant ? celt_calc_theta(X, Y, stereo, N) : 0;
        int mbits, sbits, delta;
        int qalloc;
        int pulse_cap;
        int offset;
        int orig_fill;
        int tell;

        /* Decide on the resolution to give to the split parameter theta */
        pulse_cap = ff_celt_log_freq_range[band] + duration * 8;
        offset = (pulse_cap >> 1) - (stereo && N == 2 ? CELT_QTHETA_OFFSET_TWOPHASE :
                                                          CELT_QTHETA_OFFSET);
        qn = (stereo && band >= f->intensity_stereo) ? 1 :
             celt_compute_qn(N, b, offset, pulse_cap, stereo);
        tell = opus_rc_tell_frac(rc);
        if (qn != 1) {
            if (quant)
                itheta = (itheta*qn + 8192) >> 14;
            /* Entropy coding of the angle. We use a uniform pdf for the
             * time split, a step for stereo, and a triangular one for the rest. */
            if (quant) {
                if (stereo && N > 2)
                    ff_opus_rc_enc_uint_step(rc, itheta, qn / 2);
                else if (stereo || B0 > 1)
                    ff_opus_rc_enc_uint(rc, itheta, qn + 1);
                else
                    ff_opus_rc_enc_uint_tri(rc, itheta, qn);
                itheta = itheta * 16384 / qn;
                if (stereo) {
                    if (itheta == 0)
                        celt_stereo_is_decouple(X, Y, f->block[0].lin_energy[band],
                                                f->block[1].lin_energy[band], N);
                    else
                        celt_stereo_ms_decouple(X, Y, N);
                }
            } else {
                if (stereo && N > 2)
                    itheta = ff_opus_rc_dec_uint_step(rc, qn / 2);
                else if (stereo || B0 > 1)
                    itheta = ff_opus_rc_dec_uint(rc, qn+1);
                else
                    itheta = ff_opus_rc_dec_uint_tri(rc, qn);
                itheta = itheta * 16384 / qn;
            }
        } else if (stereo) {
            if (quant) {
                inv = itheta > 8192;
                 if (inv) {
                    for (i = 0; i < N; i++)
                       Y[i] *= -1;
                 }
                 celt_stereo_is_decouple(X, Y, f->block[0].lin_energy[band],
                                         f->block[1].lin_energy[band], N);

                if (b > 2 << 3 && f->remaining2 > 2 << 3) {
                    ff_opus_rc_enc_log(rc, inv, 2);
                } else {
                    inv = 0;
                }
            } else {
                inv = (b > 2 << 3 && f->remaining2 > 2 << 3) ? ff_opus_rc_dec_log(rc, 2) : 0;
                inv = f->apply_phase_inv ? inv : 0;
            }
            itheta = 0;
        }
        qalloc = opus_rc_tell_frac(rc) - tell;
        b -= qalloc;

        orig_fill = fill;
        if (itheta == 0) {
            imid = 32767;
            iside = 0;
            fill = av_mod_uintp2(fill, blocks);
            delta = -16384;
        } else if (itheta == 16384) {
            imid = 0;
            iside = 32767;
            fill &= ((1 << blocks) - 1) << blocks;
            delta = 16384;
        } else {
            imid = celt_cos(itheta);
            iside = celt_cos(16384-itheta);
            /* This is the mid vs side allocation that minimizes squared error
            in that band. */
            delta = ROUND_MUL16((N - 1) << 7, celt_log2tan(iside, imid));
        }

        mid  = imid  / 32768.0f;
        side = iside / 32768.0f;

        /* This is a special case for N=2 that only works for stereo and takes
        advantage of the fact that mid and side are orthogonal to encode
        the side with just one bit. */
        if (N == 2 && stereo) {
            int c;
            int sign = 0;
            float tmp;
            float *x2, *y2;
            mbits = b;
            /* Only need one bit for the side */
            sbits = (itheta != 0 && itheta != 16384) ? 1 << 3 : 0;
            mbits -= sbits;
            c = (itheta > 8192);
            f->remaining2 -= qalloc+sbits;

            x2 = c ? Y : X;
            y2 = c ? X : Y;
            if (sbits) {
                if (quant) {
                    sign = x2[0]*y2[1] - x2[1]*y2[0] < 0;
                    ff_opus_rc_put_raw(rc, sign, 1);
                } else {
                    sign = ff_opus_rc_get_raw(rc, 1);
                }
            }
            sign = 1 - 2 * sign;
            /* We use orig_fill here because we want to fold the side, but if
            itheta==16384, we'll have cleared the low bits of fill. */
            cm = pvq->quant_band(pvq, f, rc, band, x2, NULL, N, mbits, blocks, lowband, duration,
                                 lowband_out, level, gain, lowband_scratch, orig_fill);
            /* We don't split N=2 bands, so cm is either 1 or 0 (for a fold-collapse),
            and there's no need to worry about mixing with the other channel. */
            y2[0] = -sign * x2[1];
            y2[1] =  sign * x2[0];
            X[0] *= mid;
            X[1] *= mid;
            Y[0] *= side;
            Y[1] *= side;
            tmp = X[0];
            X[0] = tmp - Y[0];
            Y[0] = tmp + Y[0];
            tmp = X[1];
            X[1] = tmp - Y[1];
            Y[1] = tmp + Y[1];
        } else {
            /* "Normal" split code */
            float *next_lowband2     = NULL;
            float *next_lowband_out1 = NULL;
            int next_level = 0;
            int rebalance;
            uint32_t cmt;

            /* Give more bits to low-energy MDCTs than they would
             * otherwise deserve */
            if (B0 > 1 && !stereo && (itheta & 0x3fff)) {
                if (itheta > 8192)
                    /* Rough approximation for pre-echo masking */
                    delta -= delta >> (4 - duration);
                else
                    /* Corresponds to a forward-masking slope of
                     * 1.5 dB per 10 ms */
                    delta = FFMIN(0, delta + (N << 3 >> (5 - duration)));
            }
            mbits = av_clip((b - delta) / 2, 0, b);
            sbits = b - mbits;
            f->remaining2 -= qalloc;

            if (lowband && !stereo)
                next_lowband2 = lowband + N; /* >32-bit split case */

            /* Only stereo needs to pass on lowband_out.
             * Otherwise, it's handled at the end */
            if (stereo)
                next_lowband_out1 = lowband_out;
            else
                next_level = level + 1;

            rebalance = f->remaining2;
            if (mbits >= sbits) {
                /* In stereo mode, we do not apply a scaling to the mid
                 * because we need the normalized mid for folding later */
                cm = pvq->quant_band(pvq, f, rc, band, X, NULL, N, mbits, blocks,
                                     lowband, duration, next_lowband_out1, next_level,
                                     stereo ? 1.0f : (gain * mid), lowband_scratch, fill);
                rebalance = mbits - (rebalance - f->remaining2);
                if (rebalance > 3 << 3 && itheta != 0)
                    sbits += rebalance - (3 << 3);

                /* For a stereo split, the high bits of fill are always zero,
                 * so no folding will be done to the side. */
                cmt = pvq->quant_band(pvq, f, rc, band, Y, NULL, N, sbits, blocks,
                                      next_lowband2, duration, NULL, next_level,
                                      gain * side, NULL, fill >> blocks);
                cm |= cmt << ((B0 >> 1) & (stereo - 1));
            } else {
                /* For a stereo split, the high bits of fill are always zero,
                 * so no folding will be done to the side. */
                cm = pvq->quant_band(pvq, f, rc, band, Y, NULL, N, sbits, blocks,
                                     next_lowband2, duration, NULL, next_level,
                                     gain * side, NULL, fill >> blocks);
                cm <<= ((B0 >> 1) & (stereo - 1));
                rebalance = sbits - (rebalance - f->remaining2);
                if (rebalance > 3 << 3 && itheta != 16384)
                    mbits += rebalance - (3 << 3);

                /* In stereo mode, we do not apply a scaling to the mid because
                 * we need the normalized mid for folding later */
                cm |= pvq->quant_band(pvq, f, rc, band, X, NULL, N, mbits, blocks,
                                      lowband, duration, next_lowband_out1, next_level,
                                      stereo ? 1.0f : (gain * mid), lowband_scratch, fill);
            }
        }
    } else {
        /* This is the basic no-split case */
        uint32_t q         = celt_bits2pulses(cache, b);
        uint32_t curr_bits = celt_pulses2bits(cache, q);
        f->remaining2 -= curr_bits;

        /* Ensures we can never bust the budget */
        while (f->remaining2 < 0 && q > 0) {
            f->remaining2 += curr_bits;
            curr_bits      = celt_pulses2bits(cache, --q);
            f->remaining2 -= curr_bits;
        }

        if (q != 0) {
            /* Finally do the actual (de)quantization */
            if (quant) {
                cm = celt_alg_quant(rc, X, N, (q < 8) ? q : (8 + (q & 7)) << ((q >> 3) - 1),
                                    f->spread, blocks, gain, pvq);
            } else {
                cm = celt_alg_unquant(rc, X, N, (q < 8) ? q : (8 + (q & 7)) << ((q >> 3) - 1),
                                      f->spread, blocks, gain, pvq);
            }
        } else {
            /* If there's no pulse, fill the band anyway */
            uint32_t cm_mask = (1 << blocks) - 1;
            fill &= cm_mask;
            if (fill) {
                if (!lowband) {
                    /* Noise */
                    for (i = 0; i < N; i++)
                        X[i] = (((int32_t)celt_rng(f)) >> 20);
                    cm = cm_mask;
                } else {
                    /* Folded spectrum */
                    for (i = 0; i < N; i++) {
                        /* About 48 dB below the "normal" folding level */
                        X[i] = lowband[i] + (((celt_rng(f)) & 0x8000) ? 1.0f / 256 : -1.0f / 256);
                    }
                    cm = fill;
                }
                celt_renormalize_vector(X, N, gain);
            } else {
                memset(X, 0, N*sizeof(float));
            }
        }
    }

    /* This code is used by the decoder and by the resynthesis-enabled encoder */
    if (stereo) {
        if (N > 2)
            celt_stereo_merge(X, Y, mid, N);
        if (inv) {
            for (i = 0; i < N; i++)
                Y[i] *= -1;
        }
    } else if (level == 0) {
        int k;

        /* Undo the sample reorganization going from time order to frequency order */
        if (B0 > 1)
            celt_interleave_hadamard(pvq->hadamard_tmp, X, N_B >> recombine,
                                     B0 << recombine, longblocks);

        /* Undo time-freq changes that we did earlier */
        N_B = N_B0;
        blocks = B0;
        for (k = 0; k < time_divide; k++) {
            blocks >>= 1;
            N_B <<= 1;
            cm |= cm >> blocks;
            celt_haar1(X, N_B, blocks);
        }

        for (k = 0; k < recombine; k++) {
            cm = ff_celt_bit_deinterleave[cm];
            celt_haar1(X, N0>>k, 1<<k);
        }
        blocks <<= recombine;

        /* Scale output for later folding */
        if (lowband_out) {
            float n = sqrtf(N0);
            for (i = 0; i < N0; i++)
                lowband_out[i] = n * X[i];
        }
        cm = av_mod_uintp2(cm, blocks);
    }

    return cm;
}

static QUANT_FN(pvq_decode_band)
{
#if CONFIG_OPUS_DECODER
    return quant_band_template(pvq, f, rc, band, X, Y, N, b, blocks, lowband, duration,
                               lowband_out, level, gain, lowband_scratch, fill, 0);
#else
    return 0;
#endif
}

static QUANT_FN(pvq_encode_band)
{
#if CONFIG_OPUS_ENCODER
    return quant_band_template(pvq, f, rc, band, X, Y, N, b, blocks, lowband, duration,
                               lowband_out, level, gain, lowband_scratch, fill, 1);
#else
    return 0;
#endif
}

int av_cold ff_celt_pvq_init(CeltPVQ **pvq, int encode)
{
    CeltPVQ *s = av_malloc(sizeof(CeltPVQ));
    if (!s)
        return AVERROR(ENOMEM);

    s->pvq_search = ppp_pvq_search_c;
    s->quant_band = encode ? pvq_encode_band : pvq_decode_band;

    if (CONFIG_OPUS_ENCODER && ARCH_X86)
        ff_celt_pvq_init_x86(s);

    *pvq = s;

    return 0;
}

void av_cold ff_celt_pvq_uninit(CeltPVQ **pvq)
{
    av_freep(pvq);
}
