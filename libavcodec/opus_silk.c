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
 * Opus SILK decoder
 */

#include <stdint.h>

#include "opus.h"
#include "opustab.h"

typedef struct SilkFrame {
    int coded;
    int log_gain;
    int16_t nlsf[16];
    float    lpc[16];

    float output     [2 * SILK_HISTORY];
    float lpc_history[2 * SILK_HISTORY];
    int primarylag;

    int prev_voiced;
} SilkFrame;

struct SilkContext {
    AVCodecContext *avctx;
    int output_channels;

    int midonly;
    int subframes;
    int sflength;
    int flength;
    int nlsf_interp_factor;

    enum OpusBandwidth bandwidth;
    int wb;

    SilkFrame frame[2];
    float prev_stereo_weights[2];
    float stereo_weights[2];

    int prev_coded_channels;
};

static inline void silk_stabilize_lsf(int16_t nlsf[16], int order, const uint16_t min_delta[17])
{
    int pass, i;
    for (pass = 0; pass < 20; pass++) {
        int k, min_diff = 0;
        for (i = 0; i < order+1; i++) {
            int low  = i != 0     ? nlsf[i-1] : 0;
            int high = i != order ? nlsf[i]   : 32768;
            int diff = (high - low) - (min_delta[i]);

            if (diff < min_diff) {
                min_diff = diff;
                k = i;

                if (pass == 20)
                    break;
            }
        }
        if (min_diff == 0) /* no issues; stabilized */
            return;

        /* wiggle one or two LSFs */
        if (k == 0) {
            /* repel away from lower bound */
            nlsf[0] = min_delta[0];
        } else if (k == order) {
            /* repel away from higher bound */
            nlsf[order-1] = 32768 - min_delta[order];
        } else {
            /* repel away from current position */
            int min_center = 0, max_center = 32768, center_val;

            /* lower extent */
            for (i = 0; i < k; i++)
                min_center += min_delta[i];
            min_center += min_delta[k] >> 1;

            /* upper extent */
            for (i = order; i > k; i--)
                max_center -= min_delta[i];
            max_center -= min_delta[k] >> 1;

            /* move apart */
            center_val = nlsf[k - 1] + nlsf[k];
            center_val = (center_val >> 1) + (center_val & 1); // rounded divide by 2
            center_val = FFMIN(max_center, FFMAX(min_center, center_val));

            nlsf[k - 1] = center_val - (min_delta[k] >> 1);
            nlsf[k]     = nlsf[k - 1] + min_delta[k];
        }
    }

    /* resort to the fall-back method, the standard method for LSF stabilization */

    /* sort; as the LSFs should be nearly sorted, use insertion sort */
    for (i = 1; i < order; i++) {
        int j, value = nlsf[i];
        for (j = i - 1; j >= 0 && nlsf[j] > value; j--)
            nlsf[j + 1] = nlsf[j];
        nlsf[j + 1] = value;
    }

    /* push forwards to increase distance */
    if (nlsf[0] < min_delta[0])
        nlsf[0] = min_delta[0];
    for (i = 1; i < order; i++)
        nlsf[i] = FFMAX(nlsf[i], FFMIN(nlsf[i - 1] + min_delta[i], 32767));

    /* push backwards to increase distance */
    if (nlsf[order-1] > 32768 - min_delta[order])
        nlsf[order-1] = 32768 - min_delta[order];
    for (i = order-2; i >= 0; i--)
        if (nlsf[i] > nlsf[i + 1] - min_delta[i+1])
            nlsf[i] = nlsf[i + 1] - min_delta[i+1];

    return;
}

static inline int silk_is_lpc_stable(const int16_t lpc[16], int order)
{
    int k, j, DC_resp = 0;
    int32_t lpc32[2][16];       // Q24
    int totalinvgain = 1 << 30; // 1.0 in Q30
    int32_t *row = lpc32[0], *prevrow;

    /* initialize the first row for the Levinson recursion */
    for (k = 0; k < order; k++) {
        DC_resp += lpc[k];
        row[k] = lpc[k] * 4096;
    }

    if (DC_resp >= 4096)
        return 0;

    /* check if prediction gain pushes any coefficients too far */
    for (k = order - 1; 1; k--) {
        int rc;      // Q31; reflection coefficient
        int gaindiv; // Q30; inverse of the gain (the divisor)
        int gain;    // gain for this reflection coefficient
        int fbits;   // fractional bits used for the gain
        int error;   // Q29; estimate of the error of our partial estimate of 1/gaindiv

        if (FFABS(row[k]) > 16773022)
            return 0;

        rc      = -(row[k] * 128);
        gaindiv = (1 << 30) - MULH(rc, rc);

        totalinvgain = MULH(totalinvgain, gaindiv) << 2;
        if (k == 0)
            return (totalinvgain >= 107374);

        /* approximate 1.0/gaindiv */
        fbits = opus_ilog(gaindiv);
        gain  = ((1 << 29) - 1) / (gaindiv >> (fbits + 1 - 16)); // Q<fbits-16>
        error = (1 << 29) - MULL(gaindiv << (15 + 16 - fbits), gain, 16);
        gain  = ((gain << 16) + (error * gain >> 13));

        /* switch to the next row of the LPC coefficients */
        prevrow = row;
        row = lpc32[k & 1];

        for (j = 0; j < k; j++) {
            int x = prevrow[j] - ROUND_MULL(prevrow[k - j - 1], rc, 31);
            row[j] = ROUND_MULL(x, gain, fbits);
        }
    }
}

static void silk_lsp2poly(const int32_t lsp[16], int32_t pol[16], int half_order)
{
    int i, j;

    pol[0] = 65536; // 1.0 in Q16
    pol[1] = -lsp[0];

    for (i = 1; i < half_order; i++) {
        pol[i + 1] = pol[i - 1] * 2 - ROUND_MULL(lsp[2 * i], pol[i], 16);
        for (j = i; j > 1; j--)
            pol[j] += pol[j - 2] - ROUND_MULL(lsp[2 * i], pol[j - 1], 16);

        pol[1] -= lsp[2 * i];
    }
}

static void silk_lsf2lpc(const int16_t nlsf[16], float lpcf[16], int order)
{
    int i, k;
    int32_t lsp[16];     // Q17; 2*cos(LSF)
    int32_t p[9], q[9];  // Q16
    int32_t lpc32[16];   // Q17
    int16_t lpc[16];     // Q12

    /* convert the LSFs to LSPs, i.e. 2*cos(LSF) */
    for (k = 0; k < order; k++) {
        int index = nlsf[k] >> 8;
        int offset = nlsf[k] & 255;
        int k2 = (order == 10) ? ff_silk_lsf_ordering_nbmb[k] : ff_silk_lsf_ordering_wb[k];

        /* interpolate and round */
        lsp[k2]  = ff_silk_cosine[index] * 256;
        lsp[k2] += (ff_silk_cosine[index + 1] - ff_silk_cosine[index]) * offset;
        lsp[k2]  = (lsp[k2] + 4) >> 3;
    }

    silk_lsp2poly(lsp    , p, order >> 1);
    silk_lsp2poly(lsp + 1, q, order >> 1);

    /* reconstruct A(z) */
    for (k = 0; k < order>>1; k++) {
        int32_t p_tmp = p[k + 1] + p[k];
        int32_t q_tmp = q[k + 1] - q[k];
        lpc32[k]         = -q_tmp - p_tmp;
        lpc32[order-k-1] =  q_tmp - p_tmp;
    }

    /* limit the range of the LPC coefficients to each fit within an int16_t */
    for (i = 0; i < 10; i++) {
        int j;
        unsigned int maxabs = 0;
        for (j = 0, k = 0; j < order; j++) {
            unsigned int x = FFABS(lpc32[k]);
            if (x > maxabs) {
                maxabs = x; // Q17
                k      = j;
            }
        }

        maxabs = (maxabs + 16) >> 5; // convert to Q12

        if (maxabs > 32767) {
            /* perform bandwidth expansion */
            unsigned int chirp, chirp_base; // Q16
            maxabs = FFMIN(maxabs, 163838); // anything above this overflows chirp's numerator
            chirp_base = chirp = 65470 - ((maxabs - 32767) << 14) / ((maxabs * (k+1)) >> 2);

            for (k = 0; k < order; k++) {
                lpc32[k] = ROUND_MULL(lpc32[k], chirp, 16);
                chirp    = (chirp_base * chirp + 32768) >> 16;
            }
        } else break;
    }

    if (i == 10) {
        /* time's up: just clamp */
        for (k = 0; k < order; k++) {
            int x = (lpc32[k] + 16) >> 5;
            lpc[k] = av_clip_int16(x);
            lpc32[k] = lpc[k] << 5; // shortcut mandated by the spec; drops lower 5 bits
        }
    } else {
        for (k = 0; k < order; k++)
            lpc[k] = (lpc32[k] + 16) >> 5;
    }

    /* if the prediction gain causes the LPC filter to become unstable,
       apply further bandwidth expansion on the Q17 coefficients */
    for (i = 1; i <= 16 && !silk_is_lpc_stable(lpc, order); i++) {
        unsigned int chirp, chirp_base;
        chirp_base = chirp = 65536 - (1 << i);

        for (k = 0; k < order; k++) {
            lpc32[k] = ROUND_MULL(lpc32[k], chirp, 16);
            lpc[k]   = (lpc32[k] + 16) >> 5;
            chirp    = (chirp_base * chirp + 32768) >> 16;
        }
    }

    for (i = 0; i < order; i++)
        lpcf[i] = lpc[i] / 4096.0f;
}

static inline void silk_decode_lpc(SilkContext *s, SilkFrame *frame,
                                   OpusRangeCoder *rc,
                                   float lpc_leadin[16], float lpc[16],
                                   int *lpc_order, int *has_lpc_leadin, int voiced)
{
    int i;
    int order;                   // order of the LP polynomial; 10 for NB/MB and 16 for WB
    int8_t  lsf_i1, lsf_i2[16];  // stage-1 and stage-2 codebook indices
    int16_t lsf_res[16];         // residual as a Q10 value
    int16_t nlsf[16];            // Q15

    *lpc_order = order = s->wb ? 16 : 10;

    /* obtain LSF stage-1 and stage-2 indices */
    lsf_i1 = ff_opus_rc_dec_cdf(rc, ff_silk_model_lsf_s1[s->wb][voiced]);
    for (i = 0; i < order; i++) {
        int index = s->wb ? ff_silk_lsf_s2_model_sel_wb  [lsf_i1][i] :
                            ff_silk_lsf_s2_model_sel_nbmb[lsf_i1][i];
        lsf_i2[i] = ff_opus_rc_dec_cdf(rc, ff_silk_model_lsf_s2[index]) - 4;
        if (lsf_i2[i] == -4)
            lsf_i2[i] -= ff_opus_rc_dec_cdf(rc, ff_silk_model_lsf_s2_ext);
        else if (lsf_i2[i] == 4)
            lsf_i2[i] += ff_opus_rc_dec_cdf(rc, ff_silk_model_lsf_s2_ext);
    }

    /* reverse the backwards-prediction step */
    for (i = order - 1; i >= 0; i--) {
        int qstep = s->wb ? 9830 : 11796;

        lsf_res[i] = lsf_i2[i] * 1024;
        if (lsf_i2[i] < 0)      lsf_res[i] += 102;
        else if (lsf_i2[i] > 0) lsf_res[i] -= 102;
        lsf_res[i] = (lsf_res[i] * qstep) >> 16;

        if (i + 1 < order) {
            int weight = s->wb ? ff_silk_lsf_pred_weights_wb  [ff_silk_lsf_weight_sel_wb  [lsf_i1][i]][i] :
                                 ff_silk_lsf_pred_weights_nbmb[ff_silk_lsf_weight_sel_nbmb[lsf_i1][i]][i];
            lsf_res[i] += (lsf_res[i+1] * weight) >> 8;
        }
    }

    /* reconstruct the NLSF coefficients from the supplied indices */
    for (i = 0; i < order; i++) {
        const uint8_t * codebook = s->wb ? ff_silk_lsf_codebook_wb  [lsf_i1] :
                                           ff_silk_lsf_codebook_nbmb[lsf_i1];
        int cur, prev, next, weight_sq, weight, ipart, fpart, y, value;

        /* find the weight of the residual */
        /* TODO: precompute */
        cur = codebook[i];
        prev = i ? codebook[i - 1] : 0;
        next = i + 1 < order ? codebook[i + 1] : 256;
        weight_sq = (1024 / (cur - prev) + 1024 / (next - cur)) << 16;

        /* approximate square-root with mandated fixed-point arithmetic */
        ipart = opus_ilog(weight_sq);
        fpart = (weight_sq >> (ipart-8)) & 127;
        y = ((ipart & 1) ? 32768 : 46214) >> ((32 - ipart)>>1);
        weight = y + ((213 * fpart * y) >> 16);

        value = cur * 128 + (lsf_res[i] * 16384) / weight;
        nlsf[i] = av_clip_uintp2(value, 15);
    }

    /* stabilize the NLSF coefficients */
    silk_stabilize_lsf(nlsf, order, s->wb ? ff_silk_lsf_min_spacing_wb :
                                            ff_silk_lsf_min_spacing_nbmb);

    /* produce an interpolation for the first 2 subframes, */
    /* and then convert both sets of NLSFs to LPC coefficients */
    *has_lpc_leadin = 0;
    if (s->subframes == 4) {
        int offset = ff_opus_rc_dec_cdf(rc, ff_silk_model_lsf_interpolation_offset);
        if (offset != 4 && frame->coded) {
            *has_lpc_leadin = 1;
            if (offset != 0) {
                int16_t nlsf_leadin[16];
                for (i = 0; i < order; i++)
                    nlsf_leadin[i] = frame->nlsf[i] +
                        ((nlsf[i] - frame->nlsf[i]) * offset >> 2);
                silk_lsf2lpc(nlsf_leadin, lpc_leadin, order);
            } else  /* avoid re-computation for a (roughly) 1-in-4 occurrence */
                memcpy(lpc_leadin, frame->lpc, 16 * sizeof(float));
        } else
            offset = 4;
        s->nlsf_interp_factor = offset;

        silk_lsf2lpc(nlsf, lpc, order);
    } else {
        s->nlsf_interp_factor = 4;
        silk_lsf2lpc(nlsf, lpc, order);
    }

    memcpy(frame->nlsf, nlsf, order * sizeof(nlsf[0]));
    memcpy(frame->lpc,  lpc,  order * sizeof(lpc[0]));
}

static inline void silk_count_children(OpusRangeCoder *rc, int model, int32_t total,
                                       int32_t child[2])
{
    if (total != 0) {
        child[0] = ff_opus_rc_dec_cdf(rc,
                       ff_silk_model_pulse_location[model] + (((total - 1 + 5) * (total - 1)) >> 1));
        child[1] = total - child[0];
    } else {
        child[0] = 0;
        child[1] = 0;
    }
}

static inline void silk_decode_excitation(SilkContext *s, OpusRangeCoder *rc,
                                          float* excitationf,
                                          int qoffset_high, int active, int voiced)
{
    int i;
    uint32_t seed;
    int shellblocks;
    int ratelevel;
    uint8_t pulsecount[20];     // total pulses in each shell block
    uint8_t lsbcount[20] = {0}; // raw lsbits defined for each pulse in each shell block
    int32_t excitation[320];    // Q23

    /* excitation parameters */
    seed = ff_opus_rc_dec_cdf(rc, ff_silk_model_lcg_seed);
    shellblocks = ff_silk_shell_blocks[s->bandwidth][s->subframes >> 2];
    ratelevel = ff_opus_rc_dec_cdf(rc, ff_silk_model_exc_rate[voiced]);

    for (i = 0; i < shellblocks; i++) {
        pulsecount[i] = ff_opus_rc_dec_cdf(rc, ff_silk_model_pulse_count[ratelevel]);
        if (pulsecount[i] == 17) {
            while (pulsecount[i] == 17 && ++lsbcount[i] != 10)
                pulsecount[i] = ff_opus_rc_dec_cdf(rc, ff_silk_model_pulse_count[9]);
            if (lsbcount[i] == 10)
                pulsecount[i] = ff_opus_rc_dec_cdf(rc, ff_silk_model_pulse_count[10]);
        }
    }

    /* decode pulse locations using PVQ */
    for (i = 0; i < shellblocks; i++) {
        if (pulsecount[i] != 0) {
            int a, b, c, d;
            int32_t * location = excitation + 16*i;
            int32_t branch[4][2];
            branch[0][0] = pulsecount[i];

            /* unrolled tail recursion */
            for (a = 0; a < 1; a++) {
                silk_count_children(rc, 0, branch[0][a], branch[1]);
                for (b = 0; b < 2; b++) {
                    silk_count_children(rc, 1, branch[1][b], branch[2]);
                    for (c = 0; c < 2; c++) {
                        silk_count_children(rc, 2, branch[2][c], branch[3]);
                        for (d = 0; d < 2; d++) {
                            silk_count_children(rc, 3, branch[3][d], location);
                            location += 2;
                        }
                    }
                }
            }
        } else
            memset(excitation + 16*i, 0, 16*sizeof(int32_t));
    }

    /* decode least significant bits */
    for (i = 0; i < shellblocks << 4; i++) {
        int bit;
        for (bit = 0; bit < lsbcount[i >> 4]; bit++)
            excitation[i] = (excitation[i] << 1) |
                            ff_opus_rc_dec_cdf(rc, ff_silk_model_excitation_lsb);
    }

    /* decode signs */
    for (i = 0; i < shellblocks << 4; i++) {
        if (excitation[i] != 0) {
            int sign = ff_opus_rc_dec_cdf(rc, ff_silk_model_excitation_sign[active +
                                         voiced][qoffset_high][FFMIN(pulsecount[i >> 4], 6)]);
            if (sign == 0)
                excitation[i] *= -1;
        }
    }

    /* assemble the excitation */
    for (i = 0; i < shellblocks << 4; i++) {
        int value = excitation[i];
        excitation[i] = value * 256 | ff_silk_quant_offset[voiced][qoffset_high];
        if (value < 0)      excitation[i] += 20;
        else if (value > 0) excitation[i] -= 20;

        /* invert samples pseudorandomly */
        seed = 196314165 * seed + 907633515;
        if (seed & 0x80000000)
            excitation[i] *= -1;
        seed += value;

        excitationf[i] = excitation[i] / 8388608.0f;
    }
}

/** Maximum residual history according to 4.2.7.6.1 */
#define SILK_MAX_LAG  (288 + LTP_ORDER / 2)

/** Order of the LTP filter */
#define LTP_ORDER 5

static void silk_decode_frame(SilkContext *s, OpusRangeCoder *rc,
                              int frame_num, int channel, int coded_channels, int active, int active1)
{
    /* per frame */
    int voiced;       // combines with active to indicate inactive, active, or active+voiced
    int qoffset_high;
    int order;                             // order of the LPC coefficients
    float lpc_leadin[16], lpc_body[16], residual[SILK_MAX_LAG + SILK_HISTORY];
    int has_lpc_leadin;
    float ltpscale;

    /* per subframe */
    struct {
        float gain;
        int pitchlag;
        float ltptaps[5];
    } sf[4];

    SilkFrame * const frame = s->frame + channel;

    int i;

    /* obtain stereo weights */
    if (coded_channels == 2 && channel == 0) {
        int n, wi[2], ws[2], w[2];
        n     = ff_opus_rc_dec_cdf(rc, ff_silk_model_stereo_s1);
        wi[0] = ff_opus_rc_dec_cdf(rc, ff_silk_model_stereo_s2) + 3 * (n / 5);
        ws[0] = ff_opus_rc_dec_cdf(rc, ff_silk_model_stereo_s3);
        wi[1] = ff_opus_rc_dec_cdf(rc, ff_silk_model_stereo_s2) + 3 * (n % 5);
        ws[1] = ff_opus_rc_dec_cdf(rc, ff_silk_model_stereo_s3);

        for (i = 0; i < 2; i++)
            w[i] = ff_silk_stereo_weights[wi[i]] +
                   (((ff_silk_stereo_weights[wi[i] + 1] - ff_silk_stereo_weights[wi[i]]) * 6554) >> 16)
                    * (ws[i]*2 + 1);

        s->stereo_weights[0] = (w[0] - w[1]) / 8192.0;
        s->stereo_weights[1] = w[1]          / 8192.0;

        /* and read the mid-only flag */
        s->midonly = active1 ? 0 : ff_opus_rc_dec_cdf(rc, ff_silk_model_mid_only);
    }

    /* obtain frame type */
    if (!active) {
        qoffset_high = ff_opus_rc_dec_cdf(rc, ff_silk_model_frame_type_inactive);
        voiced = 0;
    } else {
        int type = ff_opus_rc_dec_cdf(rc, ff_silk_model_frame_type_active);
        qoffset_high = type & 1;
        voiced = type >> 1;
    }

    /* obtain subframe quantization gains */
    for (i = 0; i < s->subframes; i++) {
        int log_gain;     //Q7
        int ipart, fpart, lingain;

        if (i == 0 && (frame_num == 0 || !frame->coded)) {
            /* gain is coded absolute */
            int x = ff_opus_rc_dec_cdf(rc, ff_silk_model_gain_highbits[active + voiced]);
            log_gain = (x<<3) | ff_opus_rc_dec_cdf(rc, ff_silk_model_gain_lowbits);

            if (frame->coded)
                log_gain = FFMAX(log_gain, frame->log_gain - 16);
        } else {
            /* gain is coded relative */
            int delta_gain = ff_opus_rc_dec_cdf(rc, ff_silk_model_gain_delta);
            log_gain = av_clip_uintp2(FFMAX((delta_gain<<1) - 16,
                                     frame->log_gain + delta_gain - 4), 6);
        }

        frame->log_gain = log_gain;

        /* approximate 2**(x/128) with a Q7 (i.e. non-integer) input */
        log_gain = (log_gain * 0x1D1C71 >> 16) + 2090;
        ipart = log_gain >> 7;
        fpart = log_gain & 127;
        lingain = (1 << ipart) + ((-174 * fpart * (128-fpart) >>16) + fpart) * ((1<<ipart) >> 7);
        sf[i].gain = lingain / 65536.0f;
    }

    /* obtain LPC filter coefficients */
    silk_decode_lpc(s, frame, rc, lpc_leadin, lpc_body, &order, &has_lpc_leadin, voiced);

    /* obtain pitch lags, if this is a voiced frame */
    if (voiced) {
        int lag_absolute = (!frame_num || !frame->prev_voiced);
        int primarylag;         // primary pitch lag for the entire SILK frame
        int ltpfilter;
        const int8_t * offsets;

        if (!lag_absolute) {
            int delta = ff_opus_rc_dec_cdf(rc, ff_silk_model_pitch_delta);
            if (delta)
                primarylag = frame->primarylag + delta - 9;
            else
                lag_absolute = 1;
        }

        if (lag_absolute) {
            /* primary lag is coded absolute */
            int highbits, lowbits;
            static const uint16_t * const model[] = {
                ff_silk_model_pitch_lowbits_nb, ff_silk_model_pitch_lowbits_mb,
                ff_silk_model_pitch_lowbits_wb
            };
            highbits = ff_opus_rc_dec_cdf(rc, ff_silk_model_pitch_highbits);
            lowbits  = ff_opus_rc_dec_cdf(rc, model[s->bandwidth]);

            primarylag = ff_silk_pitch_min_lag[s->bandwidth] +
                         highbits*ff_silk_pitch_scale[s->bandwidth] + lowbits;
        }
        frame->primarylag = primarylag;

        if (s->subframes == 2)
            offsets = (s->bandwidth == OPUS_BANDWIDTH_NARROWBAND)
                     ? ff_silk_pitch_offset_nb10ms[ff_opus_rc_dec_cdf(rc,
                                                ff_silk_model_pitch_contour_nb10ms)]
                     : ff_silk_pitch_offset_mbwb10ms[ff_opus_rc_dec_cdf(rc,
                                                ff_silk_model_pitch_contour_mbwb10ms)];
        else
            offsets = (s->bandwidth == OPUS_BANDWIDTH_NARROWBAND)
                     ? ff_silk_pitch_offset_nb20ms[ff_opus_rc_dec_cdf(rc,
                                                ff_silk_model_pitch_contour_nb20ms)]
                     : ff_silk_pitch_offset_mbwb20ms[ff_opus_rc_dec_cdf(rc,
                                                ff_silk_model_pitch_contour_mbwb20ms)];

        for (i = 0; i < s->subframes; i++)
            sf[i].pitchlag = av_clip(primarylag + offsets[i],
                                     ff_silk_pitch_min_lag[s->bandwidth],
                                     ff_silk_pitch_max_lag[s->bandwidth]);

        /* obtain LTP filter coefficients */
        ltpfilter = ff_opus_rc_dec_cdf(rc, ff_silk_model_ltp_filter);
        for (i = 0; i < s->subframes; i++) {
            int index, j;
            static const uint16_t * const filter_sel[] = {
                ff_silk_model_ltp_filter0_sel, ff_silk_model_ltp_filter1_sel,
                ff_silk_model_ltp_filter2_sel
            };
            static const int8_t (* const filter_taps[])[5] = {
                ff_silk_ltp_filter0_taps, ff_silk_ltp_filter1_taps, ff_silk_ltp_filter2_taps
            };
            index = ff_opus_rc_dec_cdf(rc, filter_sel[ltpfilter]);
            for (j = 0; j < 5; j++)
                sf[i].ltptaps[j] = filter_taps[ltpfilter][index][j] / 128.0f;
        }
    }

    /* obtain LTP scale factor */
    if (voiced && frame_num == 0)
        ltpscale = ff_silk_ltp_scale_factor[ff_opus_rc_dec_cdf(rc,
                                         ff_silk_model_ltp_scale_index)] / 16384.0f;
    else ltpscale = 15565.0f/16384.0f;

    /* generate the excitation signal for the entire frame */
    silk_decode_excitation(s, rc, residual + SILK_MAX_LAG, qoffset_high,
                           active, voiced);

    /* skip synthesising the side channel if we want mono-only */
    if (s->output_channels == channel)
        return;

    /* generate the output signal */
    for (i = 0; i < s->subframes; i++) {
        const float * lpc_coeff = (i < 2 && has_lpc_leadin) ? lpc_leadin : lpc_body;
        float *dst    = frame->output      + SILK_HISTORY + i * s->sflength;
        float *resptr = residual           + SILK_MAX_LAG + i * s->sflength;
        float *lpc    = frame->lpc_history + SILK_HISTORY + i * s->sflength;
        float sum;
        int j, k;

        if (voiced) {
            int out_end;
            float scale;

            if (i < 2 || s->nlsf_interp_factor == 4) {
                out_end = -i * s->sflength;
                scale   = ltpscale;
            } else {
                out_end = -(i - 2) * s->sflength;
                scale   = 1.0f;
            }

            /* when the LPC coefficients change, a re-whitening filter is used */
            /* to produce a residual that accounts for the change */
            for (j = - sf[i].pitchlag - LTP_ORDER/2; j < out_end; j++) {
                sum = dst[j];
                for (k = 0; k < order; k++)
                    sum -= lpc_coeff[k] * dst[j - k - 1];
                resptr[j] = av_clipf(sum, -1.0f, 1.0f) * scale / sf[i].gain;
            }

            if (out_end) {
                float rescale = sf[i-1].gain / sf[i].gain;
                for (j = out_end; j < 0; j++)
                    resptr[j] *= rescale;
            }

            /* LTP synthesis */
            for (j = 0; j < s->sflength; j++) {
                sum = resptr[j];
                for (k = 0; k < LTP_ORDER; k++)
                    sum += sf[i].ltptaps[k] * resptr[j - sf[i].pitchlag + LTP_ORDER/2 - k];
                resptr[j] = sum;
            }
        }

        /* LPC synthesis */
        for (j = 0; j < s->sflength; j++) {
            sum = resptr[j] * sf[i].gain;
            for (k = 1; k <= order; k++)
                sum += lpc_coeff[k - 1] * lpc[j - k];

            lpc[j] = sum;
            dst[j] = av_clipf(sum, -1.0f, 1.0f);
        }
    }

    frame->prev_voiced = voiced;
    memmove(frame->lpc_history, frame->lpc_history + s->flength, SILK_HISTORY * sizeof(float));
    memmove(frame->output,      frame->output      + s->flength, SILK_HISTORY * sizeof(float));

    frame->coded = 1;
}

static void silk_unmix_ms(SilkContext *s, float *l, float *r)
{
    float *mid    = s->frame[0].output + SILK_HISTORY - s->flength;
    float *side   = s->frame[1].output + SILK_HISTORY - s->flength;
    float w0_prev = s->prev_stereo_weights[0];
    float w1_prev = s->prev_stereo_weights[1];
    float w0      = s->stereo_weights[0];
    float w1      = s->stereo_weights[1];
    int n1        = ff_silk_stereo_interp_len[s->bandwidth];
    int i;

    for (i = 0; i < n1; i++) {
        float interp0 = w0_prev + i * (w0 - w0_prev) / n1;
        float interp1 = w1_prev + i * (w1 - w1_prev) / n1;
        float p0      = 0.25 * (mid[i - 2] + 2 * mid[i - 1] + mid[i]);

        l[i] = av_clipf((1 + interp1) * mid[i - 1] + side[i - 1] + interp0 * p0, -1.0, 1.0);
        r[i] = av_clipf((1 - interp1) * mid[i - 1] - side[i - 1] - interp0 * p0, -1.0, 1.0);
    }

    for (; i < s->flength; i++) {
        float p0 = 0.25 * (mid[i - 2] + 2 * mid[i - 1] + mid[i]);

        l[i] = av_clipf((1 + w1) * mid[i - 1] + side[i - 1] + w0 * p0, -1.0, 1.0);
        r[i] = av_clipf((1 - w1) * mid[i - 1] - side[i - 1] - w0 * p0, -1.0, 1.0);
    }

    memcpy(s->prev_stereo_weights, s->stereo_weights, sizeof(s->stereo_weights));
}

static void silk_flush_frame(SilkFrame *frame)
{
    if (!frame->coded)
        return;

    memset(frame->output,      0, sizeof(frame->output));
    memset(frame->lpc_history, 0, sizeof(frame->lpc_history));

    memset(frame->lpc,  0, sizeof(frame->lpc));
    memset(frame->nlsf, 0, sizeof(frame->nlsf));

    frame->log_gain = 0;

    frame->primarylag  = 0;
    frame->prev_voiced = 0;
    frame->coded       = 0;
}

int ff_silk_decode_superframe(SilkContext *s, OpusRangeCoder *rc,
                              float *output[2],
                              enum OpusBandwidth bandwidth,
                              int coded_channels,
                              int duration_ms)
{
    int active[2][6], redundancy[2];
    int nb_frames, i, j;

    if (bandwidth > OPUS_BANDWIDTH_WIDEBAND ||
        coded_channels > 2 || duration_ms > 60) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid parameters passed "
               "to the SILK decoder.\n");
        return AVERROR(EINVAL);
    }

    nb_frames = 1 + (duration_ms > 20) + (duration_ms > 40);
    s->subframes = duration_ms / nb_frames / 5;         // 5ms subframes
    s->sflength  = 20 * (bandwidth + 2);
    s->flength   = s->sflength * s->subframes;
    s->bandwidth = bandwidth;
    s->wb        = bandwidth == OPUS_BANDWIDTH_WIDEBAND;

    /* make sure to flush the side channel when switching from mono to stereo */
    if (coded_channels > s->prev_coded_channels)
        silk_flush_frame(&s->frame[1]);
    s->prev_coded_channels = coded_channels;

    /* read the LP-layer header bits */
    for (i = 0; i < coded_channels; i++) {
        for (j = 0; j < nb_frames; j++)
            active[i][j] = ff_opus_rc_dec_log(rc, 1);

        redundancy[i] = ff_opus_rc_dec_log(rc, 1);
        if (redundancy[i]) {
            avpriv_report_missing_feature(s->avctx, "LBRR frames");
            return AVERROR_PATCHWELCOME;
        }
    }

    for (i = 0; i < nb_frames; i++) {
        for (j = 0; j < coded_channels && !s->midonly; j++)
            silk_decode_frame(s, rc, i, j, coded_channels, active[j][i], active[1][i]);

        /* reset the side channel if it is not coded */
        if (s->midonly && s->frame[1].coded)
            silk_flush_frame(&s->frame[1]);

        if (coded_channels == 1 || s->output_channels == 1) {
            for (j = 0; j < s->output_channels; j++) {
                memcpy(output[j] + i * s->flength,
                       s->frame[0].output + SILK_HISTORY - s->flength - 2,
                       s->flength * sizeof(float));
            }
        } else {
            silk_unmix_ms(s, output[0] + i * s->flength, output[1] + i * s->flength);
        }

        s->midonly        = 0;
    }

    return nb_frames * s->flength;
}

void ff_silk_free(SilkContext **ps)
{
    av_freep(ps);
}

void ff_silk_flush(SilkContext *s)
{
    silk_flush_frame(&s->frame[0]);
    silk_flush_frame(&s->frame[1]);

    memset(s->prev_stereo_weights, 0, sizeof(s->prev_stereo_weights));
}

int ff_silk_init(AVCodecContext *avctx, SilkContext **ps, int output_channels)
{
    SilkContext *s;

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

    ff_silk_flush(s);

    *ps = s;

    return 0;
}
