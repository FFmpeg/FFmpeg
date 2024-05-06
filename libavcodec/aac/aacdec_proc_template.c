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

/**
 * linear congruential pseudorandom number generator
 *
 * @param   previous_val    pointer to the current state of the generator
 *
 * @return  Returns a 32-bit pseudorandom integer
 */
static av_always_inline int lcg_random(unsigned previous_val)
{
    union { unsigned u; int s; } v = { previous_val * 1664525u + 1013904223 };
    return v.s;
}

/**
 * Decode spectral data; reference: table 4.50.
 * Dequantize and scale spectral data; reference: 4.6.3.3.
 *
 * @param   coef            array of dequantized, scaled spectral data
 * @param   sf              array of scalefactors or intensity stereo positions
 * @param   pulse_present   set if pulses are present
 * @param   pulse           pointer to pulse data struct
 * @param   band_type       array of the used band type
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int AAC_RENAME(decode_spectrum_and_dequant)(AACDecContext *ac,
                                                   GetBitContext *gb,
                                                   const Pulse *pulse,
                                                   SingleChannelElement *sce)
{
    int i, k, g, idx = 0;
    INTFLOAT *coef = sce->AAC_RENAME(coeffs);
    IndividualChannelStream *ics = &sce->ics;
    const int c = 1024 / ics->num_windows;
    const uint16_t *offsets = ics->swb_offset;
    const INTFLOAT *sf = sce->AAC_RENAME(sf);
    const enum BandType *band_type = sce->band_type;
    INTFLOAT *coef_base = coef;

    for (g = 0; g < ics->num_windows; g++)
        memset(coef + g * 128 + offsets[ics->max_sfb], 0,
               sizeof(INTFLOAT) * (c - offsets[ics->max_sfb]));

    for (g = 0; g < ics->num_window_groups; g++) {
        unsigned g_len = ics->group_len[g];

        for (i = 0; i < ics->max_sfb; i++, idx++) {
            const unsigned cbt_m1 = band_type[idx] - 1;
            INTFLOAT *cfo = coef + offsets[i];
            int off_len = offsets[i + 1] - offsets[i];
            int group;

            if (cbt_m1 >= INTENSITY_BT2 - 1) {
                for (group = 0; group < (AAC_SIGNE)g_len; group++, cfo+=128) {
                    memset(cfo, 0, off_len * sizeof(*cfo));
                }
            } else if (cbt_m1 == NOISE_BT - 1) {
                for (group = 0; group < (AAC_SIGNE)g_len; group++, cfo+=128) {
                    INTFLOAT band_energy;
#if USE_FIXED
                    for (k = 0; k < off_len; k++) {
                        ac->random_state  = lcg_random(ac->random_state);
                        cfo[k] = ac->random_state >> 3;
                    }

                    band_energy = ac->fdsp->scalarproduct_fixed(cfo, cfo, off_len);
                    band_energy = fixed_sqrt(band_energy, 31);
                    noise_scale(cfo, sf[idx], band_energy, off_len);
#else
                    float scale;

                    for (k = 0; k < off_len; k++) {
                        ac->random_state  = lcg_random(ac->random_state);
                        cfo[k] = ac->random_state;
                    }

                    band_energy = ac->fdsp->scalarproduct_float(cfo, cfo, off_len);
                    scale = sf[idx] / sqrtf(band_energy);
                    ac->fdsp->vector_fmul_scalar(cfo, cfo, scale, off_len);
#endif /* USE_FIXED */
                }
            } else {
#if !USE_FIXED
                const float *vq = ff_aac_codebook_vector_vals[cbt_m1];
#endif /* !USE_FIXED */
                const VLCElem *vlc_tab = ff_vlc_spectral[cbt_m1];
                OPEN_READER(re, gb);

                switch (cbt_m1 >> 1) {
                case 0:
                    for (group = 0; group < (AAC_SIGNE)g_len; group++, cfo+=128) {
                        INTFLOAT *cf = cfo;
                        int len = off_len;

                        do {
                            int code;
                            unsigned cb_idx;

                            UPDATE_CACHE(re, gb);
                            GET_VLC(code, re, gb, vlc_tab, 8, 2);
                            cb_idx = code;
#if USE_FIXED
                            cf = DEC_SQUAD(cf, cb_idx);
#else
                            cf = VMUL4(cf, vq, cb_idx, sf + idx);
#endif /* USE_FIXED */
                        } while (len -= 4);
                    }
                    break;

                case 1:
                    for (group = 0; group < (AAC_SIGNE)g_len; group++, cfo+=128) {
                        INTFLOAT *cf = cfo;
                        int len = off_len;

                        do {
                            int code;
                            unsigned nnz;
                            unsigned cb_idx;
                            uint32_t bits;

                            UPDATE_CACHE(re, gb);
                            GET_VLC(code, re, gb, vlc_tab, 8, 2);
                            cb_idx = code;
                            nnz = cb_idx >> 8 & 15;
                            bits = nnz ? GET_CACHE(re, gb) : 0;
                            LAST_SKIP_BITS(re, gb, nnz);
#if USE_FIXED
                            cf = DEC_UQUAD(cf, cb_idx, bits);
#else
                            cf = VMUL4S(cf, vq, cb_idx, bits, sf + idx);
#endif /* USE_FIXED */
                        } while (len -= 4);
                    }
                    break;

                case 2:
                    for (group = 0; group < (AAC_SIGNE)g_len; group++, cfo+=128) {
                        INTFLOAT *cf = cfo;
                        int len = off_len;

                        do {
                            int code;
                            unsigned cb_idx;

                            UPDATE_CACHE(re, gb);
                            GET_VLC(code, re, gb, vlc_tab, 8, 2);
                            cb_idx = code;
#if USE_FIXED
                            cf = DEC_SPAIR(cf, cb_idx);
#else
                            cf = VMUL2(cf, vq, cb_idx, sf + idx);
#endif /* USE_FIXED */
                        } while (len -= 2);
                    }
                    break;

                case 3:
                case 4:
                    for (group = 0; group < (AAC_SIGNE)g_len; group++, cfo+=128) {
                        INTFLOAT *cf = cfo;
                        int len = off_len;

                        do {
                            int code;
                            unsigned nnz;
                            unsigned cb_idx;
                            unsigned sign;

                            UPDATE_CACHE(re, gb);
                            GET_VLC(code, re, gb, vlc_tab, 8, 2);
                            cb_idx = code;
                            nnz = cb_idx >> 8 & 15;
                            sign = nnz ? SHOW_UBITS(re, gb, nnz) << (cb_idx >> 12) : 0;
                            LAST_SKIP_BITS(re, gb, nnz);
#if USE_FIXED
                            cf = DEC_UPAIR(cf, cb_idx, sign);
#else
                            cf = VMUL2S(cf, vq, cb_idx, sign, sf + idx);
#endif /* USE_FIXED */
                        } while (len -= 2);
                    }
                    break;

                default:
                    for (group = 0; group < (AAC_SIGNE)g_len; group++, cfo+=128) {
#if USE_FIXED
                        int *icf = cfo;
                        int v;
#else
                        float *cf = cfo;
                        uint32_t *icf = (uint32_t *) cf;
#endif /* USE_FIXED */
                        int len = off_len;

                        do {
                            int code;
                            unsigned nzt, nnz;
                            unsigned cb_idx;
                            uint32_t bits;
                            int j;

                            UPDATE_CACHE(re, gb);
                            GET_VLC(code, re, gb, vlc_tab, 8, 2);
                            cb_idx = code;

                            if (cb_idx == 0x0000) {
                                *icf++ = 0;
                                *icf++ = 0;
                                continue;
                            }

                            nnz = cb_idx >> 12;
                            nzt = cb_idx >> 8;
                            bits = SHOW_UBITS(re, gb, nnz) << (32-nnz);
                            LAST_SKIP_BITS(re, gb, nnz);

                            for (j = 0; j < 2; j++) {
                                if (nzt & 1<<j) {
                                    uint32_t b;
                                    int n;
                                    /* The total length of escape_sequence must be < 22 bits according
                                       to the specification (i.e. max is 111111110xxxxxxxxxxxx). */
                                    UPDATE_CACHE(re, gb);
                                    b = GET_CACHE(re, gb);
                                    b = 31 - av_log2(~b);

                                    if (b > 8) {
                                        av_log(ac->avctx, AV_LOG_ERROR, "error in spectral data, ESC overflow\n");
                                        return AVERROR_INVALIDDATA;
                                    }

                                    SKIP_BITS(re, gb, b + 1);
                                    b += 4;
                                    n = (1 << b) + SHOW_UBITS(re, gb, b);
                                    LAST_SKIP_BITS(re, gb, b);
#if USE_FIXED
                                    v = n;
                                    if (bits & 1U<<31)
                                        v = -v;
                                    *icf++ = v;
#else
                                    *icf++ = ff_cbrt_tab[n] | (bits & 1U<<31);
#endif /* USE_FIXED */
                                    bits <<= 1;
                                } else {
#if USE_FIXED
                                    v = cb_idx & 15;
                                    if (bits & 1U<<31)
                                        v = -v;
                                    *icf++ = v;
#else
                                    unsigned v = ((const uint32_t*)vq)[cb_idx & 15];
                                    *icf++ = (bits & 1U<<31) | v;
#endif /* USE_FIXED */
                                    bits <<= !!v;
                                }
                                cb_idx >>= 4;
                            }
                        } while (len -= 2);
#if !USE_FIXED
                        ac->fdsp->vector_fmul_scalar(cfo, cfo, sf[idx], off_len);
#endif /* !USE_FIXED */
                    }
                }

                CLOSE_READER(re, gb);
            }
        }
        coef += g_len << 7;
    }

    if (pulse) {
        idx = 0;
        for (i = 0; i < pulse->num_pulse; i++) {
            INTFLOAT co = coef_base[ pulse->pos[i] ];
            while (offsets[idx + 1] <= pulse->pos[i])
                idx++;
            if (band_type[idx] != NOISE_BT && sf[idx]) {
                INTFLOAT ico = -pulse->amp[i];
#if USE_FIXED
                if (co) {
                    ico = co + (co > 0 ? -ico : ico);
                }
                coef_base[ pulse->pos[i] ] = ico;
#else
                if (co) {
                    co /= sf[idx];
                    ico = co / sqrtf(sqrtf(fabsf(co))) + (co > 0 ? -ico : ico);
                }
                coef_base[ pulse->pos[i] ] = cbrtf(fabsf(ico)) * ico * sf[idx];
#endif /* USE_FIXED */
            }
        }
    }
#if USE_FIXED
    coef = coef_base;
    idx = 0;
    for (g = 0; g < ics->num_window_groups; g++) {
        unsigned g_len = ics->group_len[g];

        for (i = 0; i < ics->max_sfb; i++, idx++) {
            const unsigned cbt_m1 = band_type[idx] - 1;
            int *cfo = coef + offsets[i];
            int off_len = offsets[i + 1] - offsets[i];
            int group;

            if (cbt_m1 < NOISE_BT - 1) {
                for (group = 0; group < (int)g_len; group++, cfo+=128) {
                    vector_pow43(cfo, off_len);
                    subband_scale(cfo, cfo, sf[idx], 34, off_len, ac->avctx);
                }
            }
        }
        coef += g_len << 7;
    }
#endif /* USE_FIXED */
    return 0;
}

/**
 * Decode coupling_channel_element; reference: table 4.8.
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int AAC_RENAME(decode_cce)(AACDecContext *ac, GetBitContext *gb, ChannelElement *che)
{
    int num_gain = 0;
    int c, g, sfb, ret;
    int sign;
    INTFLOAT scale;
    SingleChannelElement *sce = &che->ch[0];
    ChannelCoupling     *coup = &che->coup;

    coup->coupling_point = 2 * get_bits1(gb);
    coup->num_coupled = get_bits(gb, 3);
    for (c = 0; c <= coup->num_coupled; c++) {
        num_gain++;
        coup->type[c] = get_bits1(gb) ? TYPE_CPE : TYPE_SCE;
        coup->id_select[c] = get_bits(gb, 4);
        if (coup->type[c] == TYPE_CPE) {
            coup->ch_select[c] = get_bits(gb, 2);
            if (coup->ch_select[c] == 3)
                num_gain++;
        } else
            coup->ch_select[c] = 2;
    }
    coup->coupling_point += get_bits1(gb) || (coup->coupling_point >> 1);

    sign  = get_bits(gb, 1);
#if USE_FIXED
    scale = get_bits(gb, 2);
#else
    scale = cce_scale[get_bits(gb, 2)];
#endif

    if ((ret = ff_aac_decode_ics(ac, sce, gb, 0, 0)))
        return ret;

    for (c = 0; c < num_gain; c++) {
        int idx  = 0;
        int cge  = 1;
        int gain = 0;
        INTFLOAT gain_cache = FIXR10(1.);
        if (c) {
            cge = coup->coupling_point == AFTER_IMDCT ? 1 : get_bits1(gb);
            gain = cge ? get_vlc2(gb, ff_vlc_scalefactors, 7, 3) - 60: 0;
            gain_cache = GET_GAIN(scale, gain);
#if USE_FIXED
            if ((abs(gain_cache)-1024) >> 3 > 30)
                return AVERROR(ERANGE);
#endif
        }
        if (coup->coupling_point == AFTER_IMDCT) {
            coup->gain[c][0] = gain_cache;
        } else {
            for (g = 0; g < sce->ics.num_window_groups; g++) {
                for (sfb = 0; sfb < sce->ics.max_sfb; sfb++, idx++) {
                    if (sce->band_type[idx] != ZERO_BT) {
                        if (!cge) {
                            int t = get_vlc2(gb, ff_vlc_scalefactors, 7, 3) - 60;
                            if (t) {
                                int s = 1;
                                t = gain += t;
                                if (sign) {
                                    s  -= 2 * (t & 0x1);
                                    t >>= 1;
                                }
                                gain_cache = GET_GAIN(scale, t) * s;
#if USE_FIXED
                                if ((abs(gain_cache)-1024) >> 3 > 30)
                                    return AVERROR(ERANGE);
#endif
                            }
                        }
                        coup->gain[c][idx] = gain_cache;
                    }
                }
            }
        }
    }
    return 0;
}

static av_cold void AAC_RENAME(aac_proc_init)(AACDecProc *aac_proc)
{
#define SET(member) aac_proc->member = AAC_RENAME(member)
    SET(decode_spectrum_and_dequant);
    SET(decode_cce);
#undef SET
#define SET(member) aac_proc->member = AV_JOIN(ff_aac_, AAC_RENAME(member));
    SET(sbr_ctx_alloc_init);
    SET(sbr_decode_extension);
    SET(sbr_apply);
    SET(sbr_ctx_close);
#undef SET
}
