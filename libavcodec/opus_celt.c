/*
 * Copyright (c) 2012 Andrew D'Addesio
 * Copyright (c) 2013-2014 Mozilla Corporation
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
 * Opus CELT decoder
 */

#include "opus_celt.h"
#include "opustab.h"
#include "opus_pvq.h"

static void celt_decode_coarse_energy(CeltContext *s, OpusRangeCoder *rc)
{
    int i, j;
    float prev[2] = {0};
    float alpha, beta;
    const uint8_t *model;

    /* use the 2D z-transform to apply prediction in both */
    /* the time domain (alpha) and the frequency domain (beta) */

    if (opus_rc_tell(rc)+3 <= s->framebits && ff_opus_rc_dec_log(rc, 3)) {
        /* intra frame */
        alpha = 0;
        beta  = 1.0f - 4915.0f/32768.0f;
        model = ff_celt_coarse_energy_dist[s->duration][1];
    } else {
        alpha = ff_celt_alpha_coef[s->duration];
        beta  = 1.0f - ff_celt_beta_coef[s->duration];
        model = ff_celt_coarse_energy_dist[s->duration][0];
    }

    for (i = 0; i < CELT_MAX_BANDS; i++) {
        for (j = 0; j < s->coded_channels; j++) {
            CeltFrame *frame = &s->frame[j];
            float value;
            int available;

            if (i < s->startband || i >= s->endband) {
                frame->energy[i] = 0.0;
                continue;
            }

            available = s->framebits - opus_rc_tell(rc);
            if (available >= 15) {
                /* decode using a Laplace distribution */
                int k = FFMIN(i, 20) << 1;
                value = ff_opus_rc_dec_laplace(rc, model[k] << 7, model[k+1] << 6);
            } else if (available >= 2) {
                int x = ff_opus_rc_dec_cdf(rc, ff_celt_model_energy_small);
                value = (x>>1) ^ -(x&1);
            } else if (available >= 1) {
                value = -(float)ff_opus_rc_dec_log(rc, 1);
            } else value = -1;

            frame->energy[i] = FFMAX(-9.0f, frame->energy[i]) * alpha + prev[j] + value;
            prev[j] += beta * value;
        }
    }
}

static void celt_decode_fine_energy(CeltContext *s, OpusRangeCoder *rc)
{
    int i;
    for (i = s->startband; i < s->endband; i++) {
        int j;
        if (!s->fine_bits[i])
            continue;

        for (j = 0; j < s->coded_channels; j++) {
            CeltFrame *frame = &s->frame[j];
            int q2;
            float offset;
            q2 = ff_opus_rc_get_raw(rc, s->fine_bits[i]);
            offset = (q2 + 0.5f) * (1 << (14 - s->fine_bits[i])) / 16384.0f - 0.5f;
            frame->energy[i] += offset;
        }
    }
}

static void celt_decode_final_energy(CeltContext *s, OpusRangeCoder *rc,
                                     int bits_left)
{
    int priority, i, j;

    for (priority = 0; priority < 2; priority++) {
        for (i = s->startband; i < s->endband && bits_left >= s->coded_channels; i++) {
            if (s->fine_priority[i] != priority || s->fine_bits[i] >= CELT_MAX_FINE_BITS)
                continue;

            for (j = 0; j < s->coded_channels; j++) {
                int q2;
                float offset;
                q2 = ff_opus_rc_get_raw(rc, 1);
                offset = (q2 - 0.5f) * (1 << (14 - s->fine_bits[i] - 1)) / 16384.0f;
                s->frame[j].energy[i] += offset;
                bits_left--;
            }
        }
    }
}

static void celt_decode_tf_changes(CeltContext *s, OpusRangeCoder *rc,
                                   int transient)
{
    int i, diff = 0, tf_select = 0, tf_changed = 0, tf_select_bit;
    int consumed, bits = transient ? 2 : 4;

    consumed = opus_rc_tell(rc);
    tf_select_bit = (s->duration != 0 && consumed+bits+1 <= s->framebits);

    for (i = s->startband; i < s->endband; i++) {
        if (consumed+bits+tf_select_bit <= s->framebits) {
            diff ^= ff_opus_rc_dec_log(rc, bits);
            consumed = opus_rc_tell(rc);
            tf_changed |= diff;
        }
        s->tf_change[i] = diff;
        bits = transient ? 4 : 5;
    }

    if (tf_select_bit && ff_celt_tf_select[s->duration][transient][0][tf_changed] !=
                         ff_celt_tf_select[s->duration][transient][1][tf_changed])
        tf_select = ff_opus_rc_dec_log(rc, 1);

    for (i = s->startband; i < s->endband; i++) {
        s->tf_change[i] = ff_celt_tf_select[s->duration][transient][tf_select][s->tf_change[i]];
    }
}

static void celt_decode_allocation(CeltContext *s, OpusRangeCoder *rc)
{
    // approx. maximum bit allocation for each band before boost/trim
    int cap[CELT_MAX_BANDS];
    int boost[CELT_MAX_BANDS];
    int threshold[CELT_MAX_BANDS];
    int bits1[CELT_MAX_BANDS];
    int bits2[CELT_MAX_BANDS];
    int trim_offset[CELT_MAX_BANDS];

    int skip_startband = s->startband;
    int dynalloc       = 6;
    int alloctrim      = 5;
    int extrabits      = 0;

    int skip_bit            = 0;
    int intensitystereo_bit = 0;
    int dualstereo_bit      = 0;

    int remaining, bandbits;
    int low, high, total, done;
    int totalbits;
    int consumed;
    int i, j;

    consumed = opus_rc_tell(rc);

    /* obtain spread flag */
    s->spread = CELT_SPREAD_NORMAL;
    if (consumed + 4 <= s->framebits)
        s->spread = ff_opus_rc_dec_cdf(rc, ff_celt_model_spread);

    /* generate static allocation caps */
    for (i = 0; i < CELT_MAX_BANDS; i++) {
        cap[i] = (ff_celt_static_caps[s->duration][s->coded_channels - 1][i] + 64)
                 * ff_celt_freq_range[i] << (s->coded_channels - 1) << s->duration >> 2;
    }

    /* obtain band boost */
    totalbits = s->framebits << 3; // convert to 1/8 bits
    consumed = opus_rc_tell_frac(rc);
    for (i = s->startband; i < s->endband; i++) {
        int quanta, band_dynalloc;

        boost[i] = 0;

        quanta = ff_celt_freq_range[i] << (s->coded_channels - 1) << s->duration;
        quanta = FFMIN(quanta << 3, FFMAX(6 << 3, quanta));
        band_dynalloc = dynalloc;
        while (consumed + (band_dynalloc<<3) < totalbits && boost[i] < cap[i]) {
            int add = ff_opus_rc_dec_log(rc, band_dynalloc);
            consumed = opus_rc_tell_frac(rc);
            if (!add)
                break;

            boost[i]     += quanta;
            totalbits    -= quanta;
            band_dynalloc = 1;
        }
        /* dynalloc is more likely to occur if it's already been used for earlier bands */
        if (boost[i])
            dynalloc = FFMAX(2, dynalloc - 1);
    }

    /* obtain allocation trim */
    if (consumed + (6 << 3) <= totalbits)
        alloctrim = ff_opus_rc_dec_cdf(rc, ff_celt_model_alloc_trim);

    /* anti-collapse bit reservation */
    totalbits = (s->framebits << 3) - opus_rc_tell_frac(rc) - 1;
    s->anticollapse_bit = 0;
    if (s->blocks > 1 && s->duration >= 2 &&
        totalbits >= ((s->duration + 2) << 3))
        s->anticollapse_bit = 1 << 3;
    totalbits -= s->anticollapse_bit;

    /* band skip bit reservation */
    if (totalbits >= 1 << 3)
        skip_bit = 1 << 3;
    totalbits -= skip_bit;

    /* intensity/dual stereo bit reservation */
    if (s->coded_channels == 2) {
        intensitystereo_bit = ff_celt_log2_frac[s->endband - s->startband];
        if (intensitystereo_bit <= totalbits) {
            totalbits -= intensitystereo_bit;
            if (totalbits >= 1 << 3) {
                dualstereo_bit = 1 << 3;
                totalbits -= 1 << 3;
            }
        } else
            intensitystereo_bit = 0;
    }

    for (i = s->startband; i < s->endband; i++) {
        int trim     = alloctrim - 5 - s->duration;
        int band     = ff_celt_freq_range[i] * (s->endband - i - 1);
        int duration = s->duration + 3;
        int scale    = duration + s->coded_channels - 1;

        /* PVQ minimum allocation threshold, below this value the band is
         * skipped */
        threshold[i] = FFMAX(3 * ff_celt_freq_range[i] << duration >> 4,
                             s->coded_channels << 3);

        trim_offset[i] = trim * (band << scale) >> 6;

        if (ff_celt_freq_range[i] << s->duration == 1)
            trim_offset[i] -= s->coded_channels << 3;
    }

    /* bisection */
    low  = 1;
    high = CELT_VECTORS - 1;
    while (low <= high) {
        int center = (low + high) >> 1;
        done = total = 0;

        for (i = s->endband - 1; i >= s->startband; i--) {
            bandbits = ff_celt_freq_range[i] * ff_celt_static_alloc[center][i]
                       << (s->coded_channels - 1) << s->duration >> 2;

            if (bandbits)
                bandbits = FFMAX(0, bandbits + trim_offset[i]);
            bandbits += boost[i];

            if (bandbits >= threshold[i] || done) {
                done = 1;
                total += FFMIN(bandbits, cap[i]);
            } else if (bandbits >= s->coded_channels << 3)
                total += s->coded_channels << 3;
        }

        if (total > totalbits)
            high = center - 1;
        else
            low = center + 1;
    }
    high = low--;

    for (i = s->startband; i < s->endband; i++) {
        bits1[i] = ff_celt_freq_range[i] * ff_celt_static_alloc[low][i]
                   << (s->coded_channels - 1) << s->duration >> 2;
        bits2[i] = high >= CELT_VECTORS ? cap[i] :
                   ff_celt_freq_range[i] * ff_celt_static_alloc[high][i]
                   << (s->coded_channels - 1) << s->duration >> 2;

        if (bits1[i])
            bits1[i] = FFMAX(0, bits1[i] + trim_offset[i]);
        if (bits2[i])
            bits2[i] = FFMAX(0, bits2[i] + trim_offset[i]);
        if (low)
            bits1[i] += boost[i];
        bits2[i] += boost[i];

        if (boost[i])
            skip_startband = i;
        bits2[i] = FFMAX(0, bits2[i] - bits1[i]);
    }

    /* bisection */
    low  = 0;
    high = 1 << CELT_ALLOC_STEPS;
    for (i = 0; i < CELT_ALLOC_STEPS; i++) {
        int center = (low + high) >> 1;
        done = total = 0;

        for (j = s->endband - 1; j >= s->startband; j--) {
            bandbits = bits1[j] + (center * bits2[j] >> CELT_ALLOC_STEPS);

            if (bandbits >= threshold[j] || done) {
                done = 1;
                total += FFMIN(bandbits, cap[j]);
            } else if (bandbits >= s->coded_channels << 3)
                total += s->coded_channels << 3;
        }
        if (total > totalbits)
            high = center;
        else
            low = center;
    }

    done = total = 0;
    for (i = s->endband - 1; i >= s->startband; i--) {
        bandbits = bits1[i] + (low * bits2[i] >> CELT_ALLOC_STEPS);

        if (bandbits >= threshold[i] || done)
            done = 1;
        else
            bandbits = (bandbits >= s->coded_channels << 3) ?
                       s->coded_channels << 3 : 0;

        bandbits     = FFMIN(bandbits, cap[i]);
        s->pulses[i] = bandbits;
        total      += bandbits;
    }

    /* band skipping */
    for (s->codedbands = s->endband; ; s->codedbands--) {
        int allocation;
        j = s->codedbands - 1;

        if (j == skip_startband) {
            /* all remaining bands are not skipped */
            totalbits += skip_bit;
            break;
        }

        /* determine the number of bits available for coding "do not skip" markers */
        remaining   = totalbits - total;
        bandbits    = remaining / (ff_celt_freq_bands[j+1] - ff_celt_freq_bands[s->startband]);
        remaining  -= bandbits  * (ff_celt_freq_bands[j+1] - ff_celt_freq_bands[s->startband]);
        allocation  = s->pulses[j] + bandbits * ff_celt_freq_range[j]
                      + FFMAX(0, remaining - (ff_celt_freq_bands[j] - ff_celt_freq_bands[s->startband]));

        /* a "do not skip" marker is only coded if the allocation is
           above the chosen threshold */
        if (allocation >= FFMAX(threshold[j], (s->coded_channels + 1) <<3 )) {
            if (ff_opus_rc_dec_log(rc, 1))
                break;

            total      += 1 << 3;
            allocation -= 1 << 3;
        }

        /* the band is skipped, so reclaim its bits */
        total -= s->pulses[j];
        if (intensitystereo_bit) {
            total -= intensitystereo_bit;
            intensitystereo_bit = ff_celt_log2_frac[j - s->startband];
            total += intensitystereo_bit;
        }

        total += s->pulses[j] = (allocation >= s->coded_channels << 3) ?
                              s->coded_channels << 3 : 0;
    }

    /* obtain stereo flags */
    s->intensitystereo = 0;
    s->dualstereo      = 0;
    if (intensitystereo_bit)
        s->intensitystereo = s->startband +
                          ff_opus_rc_dec_uint(rc, s->codedbands + 1 - s->startband);
    if (s->intensitystereo <= s->startband)
        totalbits += dualstereo_bit; /* no intensity stereo means no dual stereo */
    else if (dualstereo_bit)
        s->dualstereo = ff_opus_rc_dec_log(rc, 1);

    /* supply the remaining bits in this frame to lower bands */
    remaining = totalbits - total;
    bandbits  = remaining / (ff_celt_freq_bands[s->codedbands] - ff_celt_freq_bands[s->startband]);
    remaining -= bandbits * (ff_celt_freq_bands[s->codedbands] - ff_celt_freq_bands[s->startband]);
    for (i = s->startband; i < s->codedbands; i++) {
        int bits = FFMIN(remaining, ff_celt_freq_range[i]);

        s->pulses[i] += bits + bandbits * ff_celt_freq_range[i];
        remaining    -= bits;
    }

    for (i = s->startband; i < s->codedbands; i++) {
        int N = ff_celt_freq_range[i] << s->duration;
        int prev_extra = extrabits;
        s->pulses[i] += extrabits;

        if (N > 1) {
            int dof;        // degrees of freedom
            int temp;       // dof * channels * log(dof)
            int offset;     // fine energy quantization offset, i.e.
                            // extra bits assigned over the standard
                            // totalbits/dof
            int fine_bits, max_bits;

            extrabits = FFMAX(0, s->pulses[i] - cap[i]);
            s->pulses[i] -= extrabits;

            /* intensity stereo makes use of an extra degree of freedom */
            dof = N * s->coded_channels
                  + (s->coded_channels == 2 && N > 2 && !s->dualstereo && i < s->intensitystereo);
            temp = dof * (ff_celt_log_freq_range[i] + (s->duration<<3));
            offset = (temp >> 1) - dof * CELT_FINE_OFFSET;
            if (N == 2) /* dof=2 is the only case that doesn't fit the model */
                offset += dof<<1;

            /* grant an additional bias for the first and second pulses */
            if (s->pulses[i] + offset < 2 * (dof << 3))
                offset += temp >> 2;
            else if (s->pulses[i] + offset < 3 * (dof << 3))
                offset += temp >> 3;

            fine_bits = (s->pulses[i] + offset + (dof << 2)) / (dof << 3);
            max_bits  = FFMIN((s->pulses[i]>>3) >> (s->coded_channels - 1),
                              CELT_MAX_FINE_BITS);

            max_bits  = FFMAX(max_bits, 0);

            s->fine_bits[i] = av_clip(fine_bits, 0, max_bits);

            /* if fine_bits was rounded down or capped,
               give priority for the final fine energy pass */
            s->fine_priority[i] = (s->fine_bits[i] * (dof<<3) >= s->pulses[i] + offset);

            /* the remaining bits are assigned to PVQ */
            s->pulses[i] -= s->fine_bits[i] << (s->coded_channels - 1) << 3;
        } else {
            /* all bits go to fine energy except for the sign bit */
            extrabits = FFMAX(0, s->pulses[i] - (s->coded_channels << 3));
            s->pulses[i] -= extrabits;
            s->fine_bits[i] = 0;
            s->fine_priority[i] = 1;
        }

        /* hand back a limited number of extra fine energy bits to this band */
        if (extrabits > 0) {
            int fineextra = FFMIN(extrabits >> (s->coded_channels + 2),
                                  CELT_MAX_FINE_BITS - s->fine_bits[i]);
            s->fine_bits[i] += fineextra;

            fineextra <<= s->coded_channels + 2;
            s->fine_priority[i] = (fineextra >= extrabits - prev_extra);
            extrabits -= fineextra;
        }
    }
    s->remaining = extrabits;

    /* skipped bands dedicate all of their bits for fine energy */
    for (; i < s->endband; i++) {
        s->fine_bits[i]     = s->pulses[i] >> (s->coded_channels - 1) >> 3;
        s->pulses[i]        = 0;
        s->fine_priority[i] = s->fine_bits[i] < 1;
    }
}

static void celt_denormalize(CeltContext *s, CeltFrame *frame, float *data)
{
    int i, j;

    for (i = s->startband; i < s->endband; i++) {
        float *dst = data + (ff_celt_freq_bands[i] << s->duration);
        float norm = exp2(frame->energy[i] + ff_celt_mean_energy[i]);

        for (j = 0; j < ff_celt_freq_range[i] << s->duration; j++)
            dst[j] *= norm;
    }
}

static void celt_postfilter_apply_transition(CeltFrame *frame, float *data)
{
    const int T0 = frame->pf_period_old;
    const int T1 = frame->pf_period;

    float g00, g01, g02;
    float g10, g11, g12;

    float x0, x1, x2, x3, x4;

    int i;

    if (frame->pf_gains[0]     == 0.0 &&
        frame->pf_gains_old[0] == 0.0)
        return;

    g00 = frame->pf_gains_old[0];
    g01 = frame->pf_gains_old[1];
    g02 = frame->pf_gains_old[2];
    g10 = frame->pf_gains[0];
    g11 = frame->pf_gains[1];
    g12 = frame->pf_gains[2];

    x1 = data[-T1 + 1];
    x2 = data[-T1];
    x3 = data[-T1 - 1];
    x4 = data[-T1 - 2];

    for (i = 0; i < CELT_OVERLAP; i++) {
        float w = ff_celt_window2[i];
        x0 = data[i - T1 + 2];

        data[i] +=  (1.0 - w) * g00 * data[i - T0]                          +
                    (1.0 - w) * g01 * (data[i - T0 - 1] + data[i - T0 + 1]) +
                    (1.0 - w) * g02 * (data[i - T0 - 2] + data[i - T0 + 2]) +
                    w         * g10 * x2                                    +
                    w         * g11 * (x1 + x3)                             +
                    w         * g12 * (x0 + x4);
        x4 = x3;
        x3 = x2;
        x2 = x1;
        x1 = x0;
    }
}

static void celt_postfilter_apply(CeltFrame *frame,
                                  float *data, int len)
{
    const int T = frame->pf_period;
    float g0, g1, g2;
    float x0, x1, x2, x3, x4;
    int i;

    if (frame->pf_gains[0] == 0.0 || len <= 0)
        return;

    g0 = frame->pf_gains[0];
    g1 = frame->pf_gains[1];
    g2 = frame->pf_gains[2];

    x4 = data[-T - 2];
    x3 = data[-T - 1];
    x2 = data[-T];
    x1 = data[-T + 1];

    for (i = 0; i < len; i++) {
        x0 = data[i - T + 2];
        data[i] += g0 * x2        +
                   g1 * (x1 + x3) +
                   g2 * (x0 + x4);
        x4 = x3;
        x3 = x2;
        x2 = x1;
        x1 = x0;
    }
}

static void celt_postfilter(CeltContext *s, CeltFrame *frame)
{
    int len = s->blocksize * s->blocks;

    celt_postfilter_apply_transition(frame, frame->buf + 1024);

    frame->pf_period_old = frame->pf_period;
    memcpy(frame->pf_gains_old, frame->pf_gains, sizeof(frame->pf_gains));

    frame->pf_period = frame->pf_period_new;
    memcpy(frame->pf_gains, frame->pf_gains_new, sizeof(frame->pf_gains));

    if (len > CELT_OVERLAP) {
        celt_postfilter_apply_transition(frame, frame->buf + 1024 + CELT_OVERLAP);
        celt_postfilter_apply(frame, frame->buf + 1024 + 2 * CELT_OVERLAP,
                              len - 2 * CELT_OVERLAP);

        frame->pf_period_old = frame->pf_period;
        memcpy(frame->pf_gains_old, frame->pf_gains, sizeof(frame->pf_gains));
    }

    memmove(frame->buf, frame->buf + len, (1024 + CELT_OVERLAP / 2) * sizeof(float));
}

static int parse_postfilter(CeltContext *s, OpusRangeCoder *rc, int consumed)
{
    static const float postfilter_taps[3][3] = {
        { 0.3066406250f, 0.2170410156f, 0.1296386719f },
        { 0.4638671875f, 0.2680664062f, 0.0           },
        { 0.7998046875f, 0.1000976562f, 0.0           }
    };
    int i;

    memset(s->frame[0].pf_gains_new, 0, sizeof(s->frame[0].pf_gains_new));
    memset(s->frame[1].pf_gains_new, 0, sizeof(s->frame[1].pf_gains_new));

    if (s->startband == 0 && consumed + 16 <= s->framebits) {
        int has_postfilter = ff_opus_rc_dec_log(rc, 1);
        if (has_postfilter) {
            float gain;
            int tapset, octave, period;

            octave = ff_opus_rc_dec_uint(rc, 6);
            period = (16 << octave) + ff_opus_rc_get_raw(rc, 4 + octave) - 1;
            gain   = 0.09375f * (ff_opus_rc_get_raw(rc, 3) + 1);
            tapset = (opus_rc_tell(rc) + 2 <= s->framebits) ?
                     ff_opus_rc_dec_cdf(rc, ff_celt_model_tapset) : 0;

            for (i = 0; i < 2; i++) {
                CeltFrame *frame = &s->frame[i];

                frame->pf_period_new = FFMAX(period, CELT_POSTFILTER_MINPERIOD);
                frame->pf_gains_new[0] = gain * postfilter_taps[tapset][0];
                frame->pf_gains_new[1] = gain * postfilter_taps[tapset][1];
                frame->pf_gains_new[2] = gain * postfilter_taps[tapset][2];
            }
        }

        consumed = opus_rc_tell(rc);
    }

    return consumed;
}

static void process_anticollapse(CeltContext *s, CeltFrame *frame, float *X)
{
    int i, j, k;

    for (i = s->startband; i < s->endband; i++) {
        int renormalize = 0;
        float *xptr;
        float prev[2];
        float Ediff, r;
        float thresh, sqrt_1;
        int depth;

        /* depth in 1/8 bits */
        depth = (1 + s->pulses[i]) / (ff_celt_freq_range[i] << s->duration);
        thresh = exp2f(-1.0 - 0.125f * depth);
        sqrt_1 = 1.0f / sqrtf(ff_celt_freq_range[i] << s->duration);

        xptr = X + (ff_celt_freq_bands[i] << s->duration);

        prev[0] = frame->prev_energy[0][i];
        prev[1] = frame->prev_energy[1][i];
        if (s->coded_channels == 1) {
            CeltFrame *frame1 = &s->frame[1];

            prev[0] = FFMAX(prev[0], frame1->prev_energy[0][i]);
            prev[1] = FFMAX(prev[1], frame1->prev_energy[1][i]);
        }
        Ediff = frame->energy[i] - FFMIN(prev[0], prev[1]);
        Ediff = FFMAX(0, Ediff);

        /* r needs to be multiplied by 2 or 2*sqrt(2) depending on LM because
        short blocks don't have the same energy as long */
        r = exp2(1 - Ediff);
        if (s->duration == 3)
            r *= M_SQRT2;
        r = FFMIN(thresh, r) * sqrt_1;
        for (k = 0; k < 1 << s->duration; k++) {
            /* Detect collapse */
            if (!(frame->collapse_masks[i] & 1 << k)) {
                /* Fill with noise */
                for (j = 0; j < ff_celt_freq_range[i]; j++)
                    xptr[(j << s->duration) + k] = (celt_rng(s) & 0x8000) ? r : -r;
                renormalize = 1;
            }
        }

        /* We just added some energy, so we need to renormalize */
        if (renormalize)
            celt_renormalize_vector(xptr, ff_celt_freq_range[i] << s->duration, 1.0f);
    }
}

static void celt_decode_bands(CeltContext *s, OpusRangeCoder *rc)
{
    float lowband_scratch[8 * 22];
    float norm[2 * 8 * 100];

    int totalbits = (s->framebits << 3) - s->anticollapse_bit;

    int update_lowband = 1;
    int lowband_offset = 0;

    int i, j;

    memset(s->coeffs, 0, sizeof(s->coeffs));

    for (i = s->startband; i < s->endband; i++) {
        int band_offset = ff_celt_freq_bands[i] << s->duration;
        int band_size   = ff_celt_freq_range[i] << s->duration;
        float *X = s->coeffs[0] + band_offset;
        float *Y = (s->coded_channels == 2) ? s->coeffs[1] + band_offset : NULL;

        int consumed = opus_rc_tell_frac(rc);
        float *norm2 = norm + 8 * 100;
        int effective_lowband = -1;
        unsigned int cm[2];
        int b;

        /* Compute how many bits we want to allocate to this band */
        if (i != s->startband)
            s->remaining -= consumed;
        s->remaining2 = totalbits - consumed - 1;
        if (i <= s->codedbands - 1) {
            int curr_balance = s->remaining / FFMIN(3, s->codedbands-i);
            b = av_clip_uintp2(FFMIN(s->remaining2 + 1, s->pulses[i] + curr_balance), 14);
        } else
            b = 0;

        if (ff_celt_freq_bands[i] - ff_celt_freq_range[i] >= ff_celt_freq_bands[s->startband] &&
            (update_lowband || lowband_offset == 0))
            lowband_offset = i;

        /* Get a conservative estimate of the collapse_mask's for the bands we're
        going to be folding from. */
        if (lowband_offset != 0 && (s->spread != CELT_SPREAD_AGGRESSIVE ||
                                    s->blocks > 1 || s->tf_change[i] < 0)) {
            int foldstart, foldend;

            /* This ensures we never repeat spectral content within one band */
            effective_lowband = FFMAX(ff_celt_freq_bands[s->startband],
                                      ff_celt_freq_bands[lowband_offset] - ff_celt_freq_range[i]);
            foldstart = lowband_offset;
            while (ff_celt_freq_bands[--foldstart] > effective_lowband);
            foldend = lowband_offset - 1;
            while (ff_celt_freq_bands[++foldend] < effective_lowband + ff_celt_freq_range[i]);

            cm[0] = cm[1] = 0;
            for (j = foldstart; j < foldend; j++) {
                cm[0] |= s->frame[0].collapse_masks[j];
                cm[1] |= s->frame[s->coded_channels - 1].collapse_masks[j];
            }
        } else
            /* Otherwise, we'll be using the LCG to fold, so all blocks will (almost
            always) be non-zero.*/
            cm[0] = cm[1] = (1 << s->blocks) - 1;

        if (s->dualstereo && i == s->intensitystereo) {
            /* Switch off dual stereo to do intensity */
            s->dualstereo = 0;
            for (j = ff_celt_freq_bands[s->startband] << s->duration; j < band_offset; j++)
                norm[j] = (norm[j] + norm2[j]) / 2;
        }

        if (s->dualstereo) {
            cm[0] = ff_celt_decode_band(s, rc, i, X, NULL, band_size, b / 2, s->blocks,
                                        effective_lowband != -1 ? norm + (effective_lowband << s->duration) : NULL, s->duration,
                                        norm + band_offset, 0, 1.0f, lowband_scratch, cm[0]);

            cm[1] = ff_celt_decode_band(s, rc, i, Y, NULL, band_size, b/2, s->blocks,
                                        effective_lowband != -1 ? norm2 + (effective_lowband << s->duration) : NULL, s->duration,
                                        norm2 + band_offset, 0, 1.0f, lowband_scratch, cm[1]);
        } else {
            cm[0] = ff_celt_decode_band(s, rc, i, X, Y, band_size, b, s->blocks,
                                        effective_lowband != -1 ? norm + (effective_lowband << s->duration) : NULL, s->duration,
                                        norm + band_offset, 0, 1.0f, lowband_scratch, cm[0]|cm[1]);
            cm[1] = cm[0];
        }

        s->frame[0].collapse_masks[i]                     = (uint8_t)cm[0];
        s->frame[s->coded_channels - 1].collapse_masks[i] = (uint8_t)cm[1];
        s->remaining += s->pulses[i] + consumed;

        /* Update the folding position only as long as we have 1 bit/sample depth */
        update_lowband = (b > band_size << 3);
    }
}

int ff_celt_decode_frame(CeltContext *s, OpusRangeCoder *rc,
                         float **output, int coded_channels, int frame_size,
                         int startband,  int endband)
{
    int i, j;

    int consumed;           // bits of entropy consumed thus far for this frame
    int silence = 0;
    int transient = 0;
    int anticollapse = 0;
    MDCT15Context *imdct;
    float imdct_scale = 1.0;

    if (coded_channels != 1 && coded_channels != 2) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid number of coded channels: %d\n",
               coded_channels);
        return AVERROR_INVALIDDATA;
    }
    if (startband < 0 || startband > endband || endband > CELT_MAX_BANDS) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid start/end band: %d %d\n",
               startband, endband);
        return AVERROR_INVALIDDATA;
    }

    s->flushed        = 0;
    s->coded_channels = coded_channels;
    s->startband      = startband;
    s->endband        = endband;
    s->framebits      = rc->rb.bytes * 8;

    s->duration = av_log2(frame_size / CELT_SHORT_BLOCKSIZE);
    if (s->duration > CELT_MAX_LOG_BLOCKS ||
        frame_size != CELT_SHORT_BLOCKSIZE * (1 << s->duration)) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid CELT frame size: %d\n",
               frame_size);
        return AVERROR_INVALIDDATA;
    }

    if (!s->output_channels)
        s->output_channels = coded_channels;

    memset(s->frame[0].collapse_masks, 0, sizeof(s->frame[0].collapse_masks));
    memset(s->frame[1].collapse_masks, 0, sizeof(s->frame[1].collapse_masks));

    consumed = opus_rc_tell(rc);

    /* obtain silence flag */
    if (consumed >= s->framebits)
        silence = 1;
    else if (consumed == 1)
        silence = ff_opus_rc_dec_log(rc, 15);


    if (silence) {
        consumed = s->framebits;
        rc->total_bits += s->framebits - opus_rc_tell(rc);
    }

    /* obtain post-filter options */
    consumed = parse_postfilter(s, rc, consumed);

    /* obtain transient flag */
    if (s->duration != 0 && consumed+3 <= s->framebits)
        transient = ff_opus_rc_dec_log(rc, 3);

    s->blocks    = transient ? 1 << s->duration : 1;
    s->blocksize = frame_size / s->blocks;

    imdct = s->imdct[transient ? 0 : s->duration];

    if (coded_channels == 1) {
        for (i = 0; i < CELT_MAX_BANDS; i++)
            s->frame[0].energy[i] = FFMAX(s->frame[0].energy[i], s->frame[1].energy[i]);
    }

    celt_decode_coarse_energy(s, rc);
    celt_decode_tf_changes   (s, rc, transient);
    celt_decode_allocation   (s, rc);
    celt_decode_fine_energy  (s, rc);
    celt_decode_bands        (s, rc);

    if (s->anticollapse_bit)
        anticollapse = ff_opus_rc_get_raw(rc, 1);

    celt_decode_final_energy(s, rc, s->framebits - opus_rc_tell(rc));

    /* apply anti-collapse processing and denormalization to
     * each coded channel */
    for (i = 0; i < s->coded_channels; i++) {
        CeltFrame *frame = &s->frame[i];

        if (anticollapse)
            process_anticollapse(s, frame, s->coeffs[i]);

        celt_denormalize(s, frame, s->coeffs[i]);
    }

    /* stereo -> mono downmix */
    if (s->output_channels < s->coded_channels) {
        s->dsp->vector_fmac_scalar(s->coeffs[0], s->coeffs[1], 1.0, FFALIGN(frame_size, 16));
        imdct_scale = 0.5;
    } else if (s->output_channels > s->coded_channels)
        memcpy(s->coeffs[1], s->coeffs[0], frame_size * sizeof(float));

    if (silence) {
        for (i = 0; i < 2; i++) {
            CeltFrame *frame = &s->frame[i];

            for (j = 0; j < FF_ARRAY_ELEMS(frame->energy); j++)
                frame->energy[j] = CELT_ENERGY_SILENCE;
        }
        memset(s->coeffs, 0, sizeof(s->coeffs));
    }

    /* transform and output for each output channel */
    for (i = 0; i < s->output_channels; i++) {
        CeltFrame *frame = &s->frame[i];
        float m = frame->deemph_coeff;

        /* iMDCT and overlap-add */
        for (j = 0; j < s->blocks; j++) {
            float *dst  = frame->buf + 1024 + j * s->blocksize;

            imdct->imdct_half(imdct, dst + CELT_OVERLAP / 2, s->coeffs[i] + j,
                              s->blocks, imdct_scale);
            s->dsp->vector_fmul_window(dst, dst, dst + CELT_OVERLAP / 2,
                                       ff_celt_window, CELT_OVERLAP / 2);
        }

        /* postfilter */
        celt_postfilter(s, frame);

        /* deemphasis and output scaling */
        for (j = 0; j < frame_size; j++) {
            float tmp = frame->buf[1024 - frame_size + j] + m;
            m = tmp * CELT_DEEMPH_COEFF;
            output[i][j] = tmp / 32768.;
        }
        frame->deemph_coeff = m;
    }

    if (coded_channels == 1)
        memcpy(s->frame[1].energy, s->frame[0].energy, sizeof(s->frame[0].energy));

    for (i = 0; i < 2; i++ ) {
        CeltFrame *frame = &s->frame[i];

        if (!transient) {
            memcpy(frame->prev_energy[1], frame->prev_energy[0], sizeof(frame->prev_energy[0]));
            memcpy(frame->prev_energy[0], frame->energy,         sizeof(frame->prev_energy[0]));
        } else {
            for (j = 0; j < CELT_MAX_BANDS; j++)
                frame->prev_energy[0][j] = FFMIN(frame->prev_energy[0][j], frame->energy[j]);
        }

        for (j = 0; j < s->startband; j++) {
            frame->prev_energy[0][j] = CELT_ENERGY_SILENCE;
            frame->energy[j]         = 0.0;
        }
        for (j = s->endband; j < CELT_MAX_BANDS; j++) {
            frame->prev_energy[0][j] = CELT_ENERGY_SILENCE;
            frame->energy[j]         = 0.0;
        }
    }

    s->seed = rc->range;

    return 0;
}

void ff_celt_flush(CeltContext *s)
{
    int i, j;

    if (s->flushed)
        return;

    for (i = 0; i < 2; i++) {
        CeltFrame *frame = &s->frame[i];

        for (j = 0; j < CELT_MAX_BANDS; j++)
            frame->prev_energy[0][j] = frame->prev_energy[1][j] = CELT_ENERGY_SILENCE;

        memset(frame->energy, 0, sizeof(frame->energy));
        memset(frame->buf,    0, sizeof(frame->buf));

        memset(frame->pf_gains,     0, sizeof(frame->pf_gains));
        memset(frame->pf_gains_old, 0, sizeof(frame->pf_gains_old));
        memset(frame->pf_gains_new, 0, sizeof(frame->pf_gains_new));

        frame->deemph_coeff = 0.0;
    }
    s->seed = 0;

    s->flushed = 1;
}

void ff_celt_free(CeltContext **ps)
{
    CeltContext *s = *ps;
    int i;

    if (!s)
        return;

    for (i = 0; i < FF_ARRAY_ELEMS(s->imdct); i++)
        ff_mdct15_uninit(&s->imdct[i]);

    av_freep(&s->dsp);
    av_freep(ps);
}

int ff_celt_init(AVCodecContext *avctx, CeltContext **ps, int output_channels)
{
    CeltContext *s;
    int i, ret;

    if (output_channels != 1 && output_channels != 2) {
        av_log(avctx, AV_LOG_ERROR, "Invalid number of output channels: %d\n",
               output_channels);
        return AVERROR(EINVAL);
    }

    s = av_mallocz(sizeof(*s));
    if (!s)
        return AVERROR(ENOMEM);

    s->avctx           = avctx;
    s->output_channels = output_channels;

    for (i = 0; i < FF_ARRAY_ELEMS(s->imdct); i++) {
        ret = ff_mdct15_init(&s->imdct[i], 1, i + 3, -1.0f);
        if (ret < 0)
            goto fail;
    }

    s->dsp = avpriv_float_dsp_alloc(avctx->flags & AV_CODEC_FLAG_BITEXACT);
    if (!s->dsp) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ff_celt_flush(s);

    *ps = s;

    return 0;
fail:
    ff_celt_free(&s);
    return ret;
}
