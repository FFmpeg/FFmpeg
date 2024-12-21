/*
 * AAC decoder
 * Copyright (c) 2005-2006 Oded Shimon ( ods15 ods15 dyndns org )
 * Copyright (c) 2006-2007 Maxim Gavrilov ( maxim.gavrilov gmail com )
 * Copyright (c) 2008-2013 Alex Converse <alex.converse@gmail.com>
 *
 * AAC LATM decoder
 * Copyright (c) 2008-2010 Paul Kendall <paul@kcbbs.gen.nz>
 * Copyright (c) 2010      Janne Grunau <janne-libav@jannau.net>
 *
 * AAC decoder fixed-point implementation
 * Copyright (c) 2013
 *      MIPS Technologies, Inc., California.
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

#include "aacdec.h"
#include "libavcodec/lpc_functions.h"

#include "libavcodec/aactab.h"

/**
 * Convert integer scalefactors to the decoder's native expected
 * scalefactor values.
 */
static void AAC_RENAME(dequant_scalefactors)(SingleChannelElement *sce)
{
    IndividualChannelStream *ics = &sce->ics;
    const int *sfo = sce->sfo;
    INTFLOAT *sf = sce->AAC_RENAME(sf);

    int idx = 0;
    for (int g = 0; g < ics->num_window_groups; g++) {
        for (int sfb = 0; sfb < ics->max_sfb; sfb++, idx++) {
            switch (sce->band_type[g*ics->max_sfb + sfb]) {
            case ZERO_BT:
                sf[idx] = FIXR(0.);
                break;
            case INTENSITY_BT: /* fallthrough */
            case INTENSITY_BT2:
#if USE_FIXED
                sf[idx] = 100 - (sfo[idx] + 100);
#else
                sf[idx] = ff_aac_pow2sf_tab[-sfo[idx] - 100 + POW_SF2_ZERO];
#endif /* USE_FIXED */
                break;
            case NOISE_BT:
#if USE_FIXED
                sf[idx] = -(100 + sfo[idx]);
#else
                sf[idx] = -ff_aac_pow2sf_tab[sfo[idx] + POW_SF2_ZERO];
#endif /* USE_FIXED */
                break;
            default:
#if USE_FIXED
                sf[idx] = -sfo[idx] - 100;
#else
                sf[idx] = -ff_aac_pow2sf_tab[sfo[idx] + POW_SF2_ZERO];
#endif /* USE_FIXED */
                break;
            }
        }
    }
}

/**
 * Mid/Side stereo decoding; reference: 4.6.8.1.3.
 */
static void AAC_RENAME(apply_mid_side_stereo)(AACDecContext *ac, ChannelElement *cpe)
{
    const IndividualChannelStream *ics = &cpe->ch[0].ics;
    INTFLOAT *ch0 = cpe->ch[0].AAC_RENAME(coeffs);
    INTFLOAT *ch1 = cpe->ch[1].AAC_RENAME(coeffs);
    const uint16_t *offsets = ics->swb_offset;
    for (int g = 0; g < ics->num_window_groups; g++) {
        for (int sfb = 0; sfb < cpe->max_sfb_ste; sfb++) {
            const int idx = g*cpe->max_sfb_ste + sfb;
            if (cpe->ms_mask[idx] &&
                cpe->ch[0].band_type[idx] < NOISE_BT &&
                cpe->ch[1].band_type[idx] < NOISE_BT) {
                for (int group = 0; group < ics->group_len[g]; group++)
#if USE_FIXED
                    ac->fdsp->butterflies_fixed(ch0 + group * 128 + offsets[sfb],
                                                ch1 + group * 128 + offsets[sfb],
                                                offsets[sfb+1] - offsets[sfb]);
#else
                    ac->fdsp->butterflies_float(ch0 + group * 128 + offsets[sfb],
                                                ch1 + group * 128 + offsets[sfb],
                                                offsets[sfb+1] - offsets[sfb]);
#endif /* USE_FIXED */
            }
        }
        ch0 += ics->group_len[g] * 128;
        ch1 += ics->group_len[g] * 128;
    }
}

/**
 * intensity stereo decoding; reference: 4.6.8.2.3
 *
 * @param   ms_present  Indicates mid/side stereo presence. [0] mask is all 0s;
 *                      [1] mask is decoded from bitstream; [2] mask is all 1s;
 *                      [3] reserved for scalable AAC
 */
static void AAC_RENAME(apply_intensity_stereo)(AACDecContext *ac,
                                               ChannelElement *cpe, int ms_present)
{
    const IndividualChannelStream *ics = &cpe->ch[1].ics;
    SingleChannelElement         *sce1 = &cpe->ch[1];
    INTFLOAT *coef0 = cpe->ch[0].AAC_RENAME(coeffs), *coef1 = cpe->ch[1].AAC_RENAME(coeffs);
    const uint16_t *offsets = ics->swb_offset;
    int c;
    INTFLOAT scale;
    for (int g = 0; g < ics->num_window_groups; g++) {
        for (int sfb = 0; sfb < ics->max_sfb; sfb++) {
            const int idx = g*ics->max_sfb + sfb;
            if (sce1->band_type[idx] == INTENSITY_BT ||
                sce1->band_type[idx] == INTENSITY_BT2) {
                c = -1 + 2 * (sce1->band_type[idx] - 14);
                if (ms_present)
                    c *= 1 - 2 * cpe->ms_mask[idx];
                scale = c * sce1->AAC_RENAME(sf)[idx];
                for (int group = 0; group < ics->group_len[g]; group++)
#if USE_FIXED
                subband_scale(coef1 + group * 128 + offsets[sfb],
                              coef0 + group * 128 + offsets[sfb],
                              scale,
                              23,
                              offsets[sfb + 1] - offsets[sfb], ac->avctx);
#else
                ac->fdsp->vector_fmul_scalar(coef1 + group * 128 + offsets[sfb],
                                             coef0 + group * 128 + offsets[sfb],
                                             scale,
                                             offsets[sfb + 1] - offsets[sfb]);
#endif /* USE_FIXED */
            }
        }
        coef0 += ics->group_len[g] * 128;
        coef1 += ics->group_len[g] * 128;
    }
}

/**
 * Decode Temporal Noise Shaping filter coefficients and apply all-pole filters; reference: 4.6.9.3.
 *
 * @param   decode  1 if tool is used normally, 0 if tool is used in LTP.
 * @param   coef    spectral coefficients
 */
static void AAC_RENAME(apply_tns)(void *_coef_param, TemporalNoiseShaping *tns,
                                  IndividualChannelStream *ics, int decode)
{
    const int mmm = FFMIN(ics->tns_max_bands, ics->max_sfb);
    int w, filt, m, i;
    int bottom, top, order, start, end, size, inc;
    INTFLOAT *coef_param = _coef_param;
    INTFLOAT lpc[TNS_MAX_ORDER];
    INTFLOAT tmp[TNS_MAX_ORDER+1];
    UINTFLOAT *coef = coef_param;

    if(!mmm)
        return;

    for (w = 0; w < ics->num_windows; w++) {
        bottom = ics->num_swb;
        for (filt = 0; filt < tns->n_filt[w]; filt++) {
            top    = bottom;
            bottom = FFMAX(0, top - tns->length[w][filt]);
            order  = tns->order[w][filt];
            if (order == 0)
                continue;

            // tns_decode_coef
            compute_lpc_coefs(tns->AAC_RENAME(coef)[w][filt], 0, order, lpc, 0, 0, 0, NULL);

            start = ics->swb_offset[FFMIN(bottom, mmm)];
            end   = ics->swb_offset[FFMIN(   top, mmm)];
            if ((size = end - start) <= 0)
                continue;
            if (tns->direction[w][filt]) {
                inc = -1;
                start = end - 1;
            } else {
                inc = 1;
            }
            start += w * 128;

            if (decode) {
                // ar filter
                for (m = 0; m < size; m++, start += inc)
                    for (i = 1; i <= FFMIN(m, order); i++)
                        coef[start] -= AAC_MUL26((INTFLOAT)coef[start - i * inc], lpc[i - 1]);
            } else {
                // ma filter
                for (m = 0; m < size; m++, start += inc) {
                    tmp[0] = coef[start];
                    for (i = 1; i <= FFMIN(m, order); i++)
                        coef[start] += AAC_MUL26(tmp[i], lpc[i - 1]);
                    for (i = order; i > 0; i--)
                        tmp[i] = tmp[i - 1];
                }
            }
        }
    }
}

/**
 *  Apply windowing and MDCT to obtain the spectral
 *  coefficient from the predicted sample by LTP.
 */
static inline void AAC_RENAME(windowing_and_mdct_ltp)(AACDecContext *ac,
                                                      INTFLOAT *out, INTFLOAT *in,
                                                      IndividualChannelStream *ics)
{
    const INTFLOAT *lwindow      = ics->use_kb_window[0] ? AAC_RENAME2(aac_kbd_long_1024) : AAC_RENAME2(sine_1024);
    const INTFLOAT *swindow      = ics->use_kb_window[0] ? AAC_RENAME2(aac_kbd_short_128) : AAC_RENAME2(sine_128);
    const INTFLOAT *lwindow_prev = ics->use_kb_window[1] ? AAC_RENAME2(aac_kbd_long_1024) : AAC_RENAME2(sine_1024);
    const INTFLOAT *swindow_prev = ics->use_kb_window[1] ? AAC_RENAME2(aac_kbd_short_128) : AAC_RENAME2(sine_128);

    if (ics->window_sequence[0] != LONG_STOP_SEQUENCE) {
        ac->fdsp->vector_fmul(in, in, lwindow_prev, 1024);
    } else {
        memset(in, 0, 448 * sizeof(*in));
        ac->fdsp->vector_fmul(in + 448, in + 448, swindow_prev, 128);
    }
    if (ics->window_sequence[0] != LONG_START_SEQUENCE) {
        ac->fdsp->vector_fmul_reverse(in + 1024, in + 1024, lwindow, 1024);
    } else {
        ac->fdsp->vector_fmul_reverse(in + 1024 + 448, in + 1024 + 448, swindow, 128);
        memset(in + 1024 + 576, 0, 448 * sizeof(*in));
    }
    ac->mdct_ltp_fn(ac->mdct_ltp, out, in, sizeof(INTFLOAT));
}

/**
 * Apply the long term prediction
 */
static void AAC_RENAME(apply_ltp)(AACDecContext *ac, SingleChannelElement *sce)
{
    const LongTermPrediction *ltp = &sce->ics.ltp;
    const uint16_t *offsets = sce->ics.swb_offset;
    int i, sfb;

    if (sce->ics.window_sequence[0] != EIGHT_SHORT_SEQUENCE) {
        INTFLOAT *predTime = sce->AAC_RENAME(output);
        INTFLOAT *predFreq = ac->AAC_RENAME(buf_mdct);
        int16_t num_samples = 2048;

        if (ltp->lag < 1024)
            num_samples = ltp->lag + 1024;
        for (i = 0; i < num_samples; i++)
            predTime[i] = AAC_MUL30(sce->AAC_RENAME(ltp_state)[i + 2048 - ltp->lag], ltp->AAC_RENAME(coef));
        memset(&predTime[i], 0, (2048 - i) * sizeof(*predTime));

        AAC_RENAME(windowing_and_mdct_ltp)(ac, predFreq, predTime, &sce->ics);

        if (sce->tns.present)
            AAC_RENAME(apply_tns)(predFreq, &sce->tns, &sce->ics, 0);

        for (sfb = 0; sfb < FFMIN(sce->ics.max_sfb, MAX_LTP_LONG_SFB); sfb++)
            if (ltp->used[sfb])
                for (i = offsets[sfb]; i < offsets[sfb + 1]; i++)
                    sce->AAC_RENAME(coeffs)[i] += (UINTFLOAT)predFreq[i];
    }
}

/**
 * Update the LTP buffer for next frame
 */
static void AAC_RENAME(update_ltp)(AACDecContext *ac, SingleChannelElement *sce)
{
    IndividualChannelStream *ics = &sce->ics;
    INTFLOAT *saved     = sce->AAC_RENAME(saved);
    INTFLOAT *saved_ltp = sce->AAC_RENAME(coeffs);
    const INTFLOAT *lwindow = ics->use_kb_window[0] ? AAC_RENAME2(aac_kbd_long_1024) : AAC_RENAME2(sine_1024);
    const INTFLOAT *swindow = ics->use_kb_window[0] ? AAC_RENAME2(aac_kbd_short_128) : AAC_RENAME2(sine_128);
    int i;

    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        memcpy(saved_ltp,       saved, 512 * sizeof(*saved_ltp));
        memset(saved_ltp + 576, 0,     448 * sizeof(*saved_ltp));
        ac->fdsp->vector_fmul_reverse(saved_ltp + 448, ac->AAC_RENAME(buf_mdct) + 960,     &swindow[64],      64);

        for (i = 0; i < 64; i++)
            saved_ltp[i + 512] = AAC_MUL31(ac->AAC_RENAME(buf_mdct)[1023 - i], swindow[63 - i]);
    } else if (1 && ics->window_sequence[0] == LONG_START_SEQUENCE) {
        memcpy(saved_ltp,       ac->AAC_RENAME(buf_mdct) + 512, 448 * sizeof(*saved_ltp));
        memset(saved_ltp + 576, 0,                  448 * sizeof(*saved_ltp));
        ac->fdsp->vector_fmul_reverse(saved_ltp + 448, ac->AAC_RENAME(buf_mdct) + 960,     &swindow[64],      64);

        for (i = 0; i < 64; i++)
            saved_ltp[i + 512] = AAC_MUL31(ac->AAC_RENAME(buf_mdct)[1023 - i], swindow[63 - i]);
    } else if (1) { // LONG_STOP or ONLY_LONG
        ac->fdsp->vector_fmul_reverse(saved_ltp, ac->AAC_RENAME(buf_mdct) + 512,     &lwindow[512],     512);

        for (i = 0; i < 512; i++)
            saved_ltp[i + 512] = AAC_MUL31(ac->AAC_RENAME(buf_mdct)[1023 - i], lwindow[511 - i]);
    }

    memcpy(sce->AAC_RENAME(ltp_state),      sce->AAC_RENAME(ltp_state)+1024,
           1024 * sizeof(*sce->AAC_RENAME(ltp_state)));
    memcpy(sce->AAC_RENAME(ltp_state) + 1024, sce->AAC_RENAME(output),
           1024 * sizeof(*sce->AAC_RENAME(ltp_state)));
    memcpy(sce->AAC_RENAME(ltp_state) + 2048, saved_ltp,
           1024 * sizeof(*sce->AAC_RENAME(ltp_state)));
}

/**
 * Conduct IMDCT and windowing.
 */
static void AAC_RENAME(imdct_and_windowing)(AACDecContext *ac, SingleChannelElement *sce)
{
    IndividualChannelStream *ics = &sce->ics;
    INTFLOAT *in    = sce->AAC_RENAME(coeffs);
    INTFLOAT *out   = sce->AAC_RENAME(output);
    INTFLOAT *saved = sce->AAC_RENAME(saved);
    const INTFLOAT *swindow      = ics->use_kb_window[0] ? AAC_RENAME2(aac_kbd_short_128) : AAC_RENAME2(sine_128);
    const INTFLOAT *lwindow_prev = ics->use_kb_window[1] ? AAC_RENAME2(aac_kbd_long_1024) : AAC_RENAME2(sine_1024);
    const INTFLOAT *swindow_prev = ics->use_kb_window[1] ? AAC_RENAME2(aac_kbd_short_128) : AAC_RENAME2(sine_128);
    INTFLOAT *buf  = ac->AAC_RENAME(buf_mdct);
    INTFLOAT *temp = ac->AAC_RENAME(temp);
    int i;

    // imdct
    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        for (i = 0; i < 1024; i += 128)
            ac->mdct128_fn(ac->mdct128, buf + i, in + i, sizeof(INTFLOAT));
    } else {
        ac->mdct1024_fn(ac->mdct1024, buf, in, sizeof(INTFLOAT));
    }

    /* window overlapping
     * NOTE: To simplify the overlapping code, all 'meaningless' short to long
     * and long to short transitions are considered to be short to short
     * transitions. This leaves just two cases (long to long and short to short)
     * with a little special sauce for EIGHT_SHORT_SEQUENCE.
     */
    if ((ics->window_sequence[1] == ONLY_LONG_SEQUENCE || ics->window_sequence[1] == LONG_STOP_SEQUENCE) &&
            (ics->window_sequence[0] == ONLY_LONG_SEQUENCE || ics->window_sequence[0] == LONG_START_SEQUENCE)) {
        ac->fdsp->vector_fmul_window(    out,               saved,            buf,         lwindow_prev, 512);
    } else {
        memcpy(                         out,               saved,            448 * sizeof(*out));

        if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
            ac->fdsp->vector_fmul_window(out + 448 + 0*128, saved + 448,      buf + 0*128, swindow_prev, 64);
            ac->fdsp->vector_fmul_window(out + 448 + 1*128, buf + 0*128 + 64, buf + 1*128, swindow,      64);
            ac->fdsp->vector_fmul_window(out + 448 + 2*128, buf + 1*128 + 64, buf + 2*128, swindow,      64);
            ac->fdsp->vector_fmul_window(out + 448 + 3*128, buf + 2*128 + 64, buf + 3*128, swindow,      64);
            ac->fdsp->vector_fmul_window(temp,              buf + 3*128 + 64, buf + 4*128, swindow,      64);
            memcpy(                     out + 448 + 4*128, temp, 64 * sizeof(*out));
        } else {
            ac->fdsp->vector_fmul_window(out + 448,         saved + 448,      buf,         swindow_prev, 64);
            memcpy(                     out + 576,         buf + 64,         448 * sizeof(*out));
        }
    }

    // buffer update
    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        memcpy(                     saved,       temp + 64,         64 * sizeof(*saved));
        ac->fdsp->vector_fmul_window(saved + 64,  buf + 4*128 + 64, buf + 5*128, swindow, 64);
        ac->fdsp->vector_fmul_window(saved + 192, buf + 5*128 + 64, buf + 6*128, swindow, 64);
        ac->fdsp->vector_fmul_window(saved + 320, buf + 6*128 + 64, buf + 7*128, swindow, 64);
        memcpy(                     saved + 448, buf + 7*128 + 64,  64 * sizeof(*saved));
    } else if (ics->window_sequence[0] == LONG_START_SEQUENCE) {
        memcpy(                     saved,       buf + 512,        448 * sizeof(*saved));
        memcpy(                     saved + 448, buf + 7*128 + 64,  64 * sizeof(*saved));
    } else { // LONG_STOP or ONLY_LONG
        memcpy(                     saved,       buf + 512,        512 * sizeof(*saved));
    }
}

/**
 * Conduct IMDCT and windowing for 768-point frames.
 */
static void AAC_RENAME(imdct_and_windowing_768)(AACDecContext *ac, SingleChannelElement *sce)
{
    IndividualChannelStream *ics = &sce->ics;
    INTFLOAT *in    = sce->AAC_RENAME(coeffs);
    INTFLOAT *out   = sce->AAC_RENAME(output);
    INTFLOAT *saved = sce->AAC_RENAME(saved);
    const INTFLOAT *swindow      = ics->use_kb_window[0] ? AAC_RENAME(aac_kbd_short_96) : AAC_RENAME(sine_96);
    const INTFLOAT *lwindow_prev = ics->use_kb_window[1] ? AAC_RENAME(aac_kbd_long_768) : AAC_RENAME(sine_768);
    const INTFLOAT *swindow_prev = ics->use_kb_window[1] ? AAC_RENAME(aac_kbd_short_96) : AAC_RENAME(sine_96);
    INTFLOAT *buf  = ac->AAC_RENAME(buf_mdct);
    INTFLOAT *temp = ac->AAC_RENAME(temp);
    int i;

    // imdct
    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        for (i = 0; i < 8; i++)
            ac->mdct96_fn(ac->mdct96, buf + i * 96, in + i * 96, sizeof(INTFLOAT));
    } else {
        ac->mdct768_fn(ac->mdct768, buf, in, sizeof(INTFLOAT));
    }

    /* window overlapping
     * NOTE: To simplify the overlapping code, all 'meaningless' short to long
     * and long to short transitions are considered to be short to short
     * transitions. This leaves just two cases (long to long and short to short)
     * with a little special sauce for EIGHT_SHORT_SEQUENCE.
     */

    if ((ics->window_sequence[1] == ONLY_LONG_SEQUENCE || ics->window_sequence[1] == LONG_STOP_SEQUENCE) &&
        (ics->window_sequence[0] == ONLY_LONG_SEQUENCE || ics->window_sequence[0] == LONG_START_SEQUENCE)) {
        ac->fdsp->vector_fmul_window(    out,               saved,            buf,         lwindow_prev, 384);
    } else {
        memcpy(                          out,               saved,            336 * sizeof(*out));

        if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
            ac->fdsp->vector_fmul_window(out + 336 + 0*96, saved + 336,     buf + 0*96, swindow_prev, 48);
            ac->fdsp->vector_fmul_window(out + 336 + 1*96, buf + 0*96 + 48, buf + 1*96, swindow,      48);
            ac->fdsp->vector_fmul_window(out + 336 + 2*96, buf + 1*96 + 48, buf + 2*96, swindow,      48);
            ac->fdsp->vector_fmul_window(out + 336 + 3*96, buf + 2*96 + 48, buf + 3*96, swindow,      48);
            ac->fdsp->vector_fmul_window(temp,             buf + 3*96 + 48, buf + 4*96, swindow,      48);
            memcpy(                      out + 336 + 4*96, temp, 48 * sizeof(*out));
        } else {
            ac->fdsp->vector_fmul_window(out + 336,        saved + 336,     buf,        swindow_prev, 48);
            memcpy(                      out + 432,        buf + 48,        336 * sizeof(*out));
        }
    }

    // buffer update
    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        memcpy(                      saved,       temp + 48,         48 * sizeof(*saved));
        ac->fdsp->vector_fmul_window(saved + 48,  buf + 4*96 + 48, buf + 5*96, swindow, 48);
        ac->fdsp->vector_fmul_window(saved + 144, buf + 5*96 + 48, buf + 6*96, swindow, 48);
        ac->fdsp->vector_fmul_window(saved + 240, buf + 6*96 + 48, buf + 7*96, swindow, 48);
        memcpy(                      saved + 336, buf + 7*96 + 48,  48 * sizeof(*saved));
    } else if (ics->window_sequence[0] == LONG_START_SEQUENCE) {
        memcpy(                      saved,       buf + 384,       336 * sizeof(*saved));
        memcpy(                      saved + 336, buf + 7*96 + 48,  48 * sizeof(*saved));
    } else { // LONG_STOP or ONLY_LONG
        memcpy(                      saved,       buf + 384,       384 * sizeof(*saved));
    }
}

/**
 * Conduct IMDCT and windowing.
 */
static void AAC_RENAME(imdct_and_windowing_960)(AACDecContext *ac, SingleChannelElement *sce)
{
    IndividualChannelStream *ics = &sce->ics;
    INTFLOAT *in    = sce->AAC_RENAME(coeffs);
    INTFLOAT *out   = sce->AAC_RENAME(output);
    INTFLOAT *saved = sce->AAC_RENAME(saved);
    const INTFLOAT *swindow      = ics->use_kb_window[0] ? AAC_RENAME(aac_kbd_short_120) : AAC_RENAME(sine_120);
    const INTFLOAT *lwindow_prev = ics->use_kb_window[1] ? AAC_RENAME(aac_kbd_long_960) : AAC_RENAME(sine_960);
    const INTFLOAT *swindow_prev = ics->use_kb_window[1] ? AAC_RENAME(aac_kbd_short_120) : AAC_RENAME(sine_120);
    INTFLOAT *buf  = ac->AAC_RENAME(buf_mdct);
    INTFLOAT *temp = ac->AAC_RENAME(temp);
    int i;

    // imdct
    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        for (i = 0; i < 8; i++)
            ac->mdct120_fn(ac->mdct120, buf + i * 120, in + i * 128, sizeof(INTFLOAT));
    } else {
        ac->mdct960_fn(ac->mdct960, buf, in, sizeof(INTFLOAT));
    }

    /* window overlapping
     * NOTE: To simplify the overlapping code, all 'meaningless' short to long
     * and long to short transitions are considered to be short to short
     * transitions. This leaves just two cases (long to long and short to short)
     * with a little special sauce for EIGHT_SHORT_SEQUENCE.
     */

    if ((ics->window_sequence[1] == ONLY_LONG_SEQUENCE || ics->window_sequence[1] == LONG_STOP_SEQUENCE) &&
        (ics->window_sequence[0] == ONLY_LONG_SEQUENCE || ics->window_sequence[0] == LONG_START_SEQUENCE)) {
        ac->fdsp->vector_fmul_window(    out,               saved,            buf,         lwindow_prev, 480);
    } else {
        memcpy(                          out,               saved,            420 * sizeof(*out));

        if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
            ac->fdsp->vector_fmul_window(out + 420 + 0*120, saved + 420,      buf + 0*120, swindow_prev, 60);
            ac->fdsp->vector_fmul_window(out + 420 + 1*120, buf + 0*120 + 60, buf + 1*120, swindow,      60);
            ac->fdsp->vector_fmul_window(out + 420 + 2*120, buf + 1*120 + 60, buf + 2*120, swindow,      60);
            ac->fdsp->vector_fmul_window(out + 420 + 3*120, buf + 2*120 + 60, buf + 3*120, swindow,      60);
            ac->fdsp->vector_fmul_window(temp,              buf + 3*120 + 60, buf + 4*120, swindow,      60);
            memcpy(                      out + 420 + 4*120, temp, 60 * sizeof(*out));
        } else {
            ac->fdsp->vector_fmul_window(out + 420,         saved + 420,      buf,         swindow_prev, 60);
            memcpy(                      out + 540,         buf + 60,         420 * sizeof(*out));
        }
    }

    // buffer update
    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        memcpy(                      saved,       temp + 60,         60 * sizeof(*saved));
        ac->fdsp->vector_fmul_window(saved + 60,  buf + 4*120 + 60, buf + 5*120, swindow, 60);
        ac->fdsp->vector_fmul_window(saved + 180, buf + 5*120 + 60, buf + 6*120, swindow, 60);
        ac->fdsp->vector_fmul_window(saved + 300, buf + 6*120 + 60, buf + 7*120, swindow, 60);
        memcpy(                      saved + 420, buf + 7*120 + 60,  60 * sizeof(*saved));
    } else if (ics->window_sequence[0] == LONG_START_SEQUENCE) {
        memcpy(                      saved,       buf + 480,        420 * sizeof(*saved));
        memcpy(                      saved + 420, buf + 7*120 + 60,  60 * sizeof(*saved));
    } else { // LONG_STOP or ONLY_LONG
        memcpy(                      saved,       buf + 480,        480 * sizeof(*saved));
    }
}

static void AAC_RENAME(imdct_and_windowing_ld)(AACDecContext *ac, SingleChannelElement *sce)
{
    IndividualChannelStream *ics = &sce->ics;
    INTFLOAT *in    = sce->AAC_RENAME(coeffs);
    INTFLOAT *out   = sce->AAC_RENAME(output);
    INTFLOAT *saved = sce->AAC_RENAME(saved);
    INTFLOAT *buf   = ac->AAC_RENAME(buf_mdct);

    // imdct
    ac->mdct512_fn(ac->mdct512, buf, in, sizeof(INTFLOAT));

    // window overlapping
    if (ics->use_kb_window[1]) {
        // AAC LD uses a low overlap sine window instead of a KBD window
        memcpy(out, saved, 192 * sizeof(*out));
        ac->fdsp->vector_fmul_window(out + 192, saved + 192, buf, AAC_RENAME2(sine_128), 64);
        memcpy(                     out + 320, buf + 64, 192 * sizeof(*out));
    } else {
        ac->fdsp->vector_fmul_window(out, saved, buf, AAC_RENAME2(sine_512), 256);
    }

    // buffer update
    memcpy(saved, buf + 256, 256 * sizeof(*saved));
}

static void AAC_RENAME(imdct_and_windowing_eld)(AACDecContext *ac, SingleChannelElement *sce)
{
    UINTFLOAT *in   = sce->AAC_RENAME(coeffs);
    INTFLOAT *out   = sce->AAC_RENAME(output);
    INTFLOAT *saved = sce->AAC_RENAME(saved);
    INTFLOAT *buf   = ac->AAC_RENAME(buf_mdct);
    int i;
    const int n  = ac->oc[1].m4ac.frame_length_short ? 480 : 512;
    const int n2 = n >> 1;
    const int n4 = n >> 2;
    const INTFLOAT *const window = n == 480 ? AAC_RENAME(ff_aac_eld_window_480) :
                                           AAC_RENAME(ff_aac_eld_window_512);

    // Inverse transform, mapped to the conventional IMDCT by
    // Chivukula, R.K.; Reznik, Y.A.; Devarajan, V.,
    // "Efficient algorithms for MPEG-4 AAC-ELD, AAC-LD and AAC-LC filterbanks,"
    // International Conference on Audio, Language and Image Processing, ICALIP 2008.
    // URL: http://ieeexplore.ieee.org/stamp/stamp.jsp?tp=&arnumber=4590245&isnumber=4589950
    for (i = 0; i < n2; i+=2) {
        INTFLOAT temp;
        temp =  in[i    ]; in[i    ] = -in[n - 1 - i]; in[n - 1 - i] = temp;
        temp = -in[i + 1]; in[i + 1] =  in[n - 2 - i]; in[n - 2 - i] = temp;
    }

    if (n == 480)
        ac->mdct480_fn(ac->mdct480, buf, in, sizeof(INTFLOAT));
    else
        ac->mdct512_fn(ac->mdct512, buf, in, sizeof(INTFLOAT));

    for (i = 0; i < n; i+=2) {
        buf[i + 0] = -(UINTFLOAT)(USE_FIXED + 1)*buf[i + 0];
        buf[i + 1] =  (UINTFLOAT)(USE_FIXED + 1)*buf[i + 1];
    }
    // Like with the regular IMDCT at this point we still have the middle half
    // of a transform but with even symmetry on the left and odd symmetry on
    // the right

    // window overlapping
    // The spec says to use samples [0..511] but the reference decoder uses
    // samples [128..639].
    for (i = n4; i < n2; i ++) {
        out[i - n4] = AAC_MUL31(   buf[    n2 - 1 - i] , window[i       - n4]) +
                      AAC_MUL31( saved[        i + n2] , window[i +   n - n4]) +
                      AAC_MUL31(-saved[n + n2 - 1 - i] , window[i + 2*n - n4]) +
                      AAC_MUL31(-saved[  2*n + n2 + i] , window[i + 3*n - n4]);
    }
    for (i = 0; i < n2; i ++) {
        out[n4 + i] = AAC_MUL31(   buf[              i] , window[i + n2       - n4]) +
                      AAC_MUL31(-saved[      n - 1 - i] , window[i + n2 +   n - n4]) +
                      AAC_MUL31(-saved[          n + i] , window[i + n2 + 2*n - n4]) +
                      AAC_MUL31( saved[2*n + n - 1 - i] , window[i + n2 + 3*n - n4]);
    }
    for (i = 0; i < n4; i ++) {
        out[n2 + n4 + i] = AAC_MUL31(   buf[    i + n2] , window[i +   n - n4]) +
                           AAC_MUL31(-saved[n2 - 1 - i] , window[i + 2*n - n4]) +
                           AAC_MUL31(-saved[n + n2 + i] , window[i + 3*n - n4]);
    }

    // buffer update
    memmove(saved + n, saved, 2 * n * sizeof(*saved));
    memcpy( saved,       buf,     n * sizeof(*saved));
}

static void AAC_RENAME(clip_output)(AACDecContext *ac, ChannelElement *che,
                                    int type, int samples)
{
#if USE_FIXED
    /* preparation for resampler */
    for (int j = 0; j < samples; j++){
        che->ch[0].output_fixed[j] = (int32_t)av_clip64((int64_t)che->ch[0].output_fixed[j]*128,
                                                    INT32_MIN, INT32_MAX-0x8000)+0x8000;
        if (type == TYPE_CPE || (type == TYPE_SCE && ac->oc[1].m4ac.ps == 1))
            che->ch[1].output_fixed[j] = (int32_t)av_clip64((int64_t)che->ch[1].output_fixed[j]*128,
                                                        INT32_MIN, INT32_MAX-0x8000)+0x8000;
    }
#endif
}

static inline void reset_all_predictors(PredictorState *ps)
{
    int i;
    for (i = 0; i < MAX_PREDICTORS; i++)
        reset_predict_state(&ps[i]);
}

static inline void reset_predictor_group(PredictorState *ps, int group_num)
{
    int i;
    for (i = group_num - 1; i < MAX_PREDICTORS; i += 30)
        reset_predict_state(&ps[i]);
}

/**
 * Apply AAC-Main style frequency domain prediction.
 */
static void AAC_RENAME(apply_prediction)(AACDecContext *ac, SingleChannelElement *sce)
{
    int sfb, k;

    if (!sce->ics.predictor_initialized) {
        reset_all_predictors(sce->AAC_RENAME(predictor_state));
        sce->ics.predictor_initialized = 1;
    }

    if (sce->ics.window_sequence[0] != EIGHT_SHORT_SEQUENCE) {
        for (sfb = 0;
             sfb < ff_aac_pred_sfb_max[ac->oc[1].m4ac.sampling_index];
             sfb++) {
            for (k = sce->ics.swb_offset[sfb];
                 k < sce->ics.swb_offset[sfb + 1];
                 k++) {
                predict(&sce->AAC_RENAME(predictor_state)[k],
                        &sce->AAC_RENAME(coeffs)[k],
                        sce->ics.predictor_present &&
                        sce->ics.prediction_used[sfb]);
            }
        }
        if (sce->ics.predictor_reset_group)
            reset_predictor_group(sce->AAC_RENAME(predictor_state),
                                  sce->ics.predictor_reset_group);
    } else
        reset_all_predictors(sce->AAC_RENAME(predictor_state));
}

static av_cold void AAC_RENAME(aac_dsp_init)(AACDecDSP *aac_dsp)
{
#define SET(member) aac_dsp->member = AAC_RENAME(member)
    SET(dequant_scalefactors);
    SET(apply_mid_side_stereo);
    SET(apply_intensity_stereo);
    SET(apply_tns);
    SET(apply_ltp);
    SET(update_ltp);

    SET(apply_prediction);

    SET(imdct_and_windowing);
    SET(imdct_and_windowing_768);
    SET(imdct_and_windowing_960);
    SET(imdct_and_windowing_ld);
    SET(imdct_and_windowing_eld);

    SET(apply_dependent_coupling);
    SET(apply_independent_coupling);

    SET(clip_output);
#undef SET
}
