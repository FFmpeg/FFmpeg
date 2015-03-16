/*
 * DCA XLL extension
 *
 * Copyright (C) 2012 Paul B Mahol
 * Copyright (C) 2014 Niels Möller
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"

#include "avcodec.h"
#include "dca.h"
#include "dcadata.h"
#include "get_bits.h"
#include "unary.h"

/* Sign as bit 0 */
static inline int get_bits_sm(GetBitContext *s, unsigned n)
{
    int x = get_bits(s, n);
    if (x & 1)
        return -(x >> 1) - 1;
    else
        return x >> 1;
}

/* Return -1 on error. */
static int32_t get_dmix_coeff(DCAContext *s, int inverse)
{
    unsigned code = get_bits(&s->gb, 9);
    int32_t sign = (int32_t) (code >> 8) - 1;
    unsigned idx = code & 0xff;
    int inv_offset = FF_DCA_DMIXTABLE_SIZE -FF_DCA_INV_DMIXTABLE_SIZE;
    if (idx >= FF_DCA_DMIXTABLE_SIZE) {
        av_log(s->avctx, AV_LOG_ERROR,
               "XLL: Invalid channel set downmix code %x\n", code);
        return -1;
    } else if (!inverse) {
        return (ff_dca_dmixtable[idx] ^ sign) - sign;
    } else if (idx < inv_offset) {
        av_log(s->avctx, AV_LOG_ERROR,
               "XLL: Invalid channel set inverse downmix code %x\n", code);
        return -1;
    } else {
        return (ff_dca_inv_dmixtable[idx - inv_offset] ^ sign) - sign;
    }
}

static int32_t dca_get_dmix_coeff(DCAContext *s)
{
    return get_dmix_coeff(s, 0);
}

static int32_t dca_get_inv_dmix_coeff(DCAContext *s)
{
    return get_dmix_coeff(s, 1);
}

/* parse XLL header */
int ff_dca_xll_decode_header(DCAContext *s)
{
    int hdr_pos, hdr_size;
    av_unused int version, frame_size;
    int i, chset_index;

    /* get bit position of sync header */
    hdr_pos    = get_bits_count(&s->gb) - 32;

    version    = get_bits(&s->gb, 4) + 1;
    hdr_size   = get_bits(&s->gb, 8) + 1;

    frame_size = get_bits_long(&s->gb, get_bits(&s->gb, 5) + 1) + 1;

    s->xll_channels          =
    s->xll_residual_channels = 0;
    s->xll_nch_sets          = get_bits(&s->gb, 4) + 1;
    s->xll_segments          = 1 << get_bits(&s->gb, 4);
    s->xll_log_smpl_in_seg   = get_bits(&s->gb, 4);
    s->xll_smpl_in_seg       = 1 << s->xll_log_smpl_in_seg;
    s->xll_bits4seg_size     = get_bits(&s->gb, 5) + 1;
    s->xll_banddata_crc      = get_bits(&s->gb, 2);
    s->xll_scalable_lsb      = get_bits1(&s->gb);
    s->xll_bits4ch_mask      = get_bits(&s->gb, 5) + 1;

    if (s->xll_scalable_lsb) {
        s->xll_fixed_lsb_width = get_bits(&s->gb, 4);
        if (s->xll_fixed_lsb_width)
            av_log(s->avctx, AV_LOG_WARNING,
                   "XLL: fixed lsb width = %d, non-zero not supported.\n",
                   s->xll_fixed_lsb_width);
    }
    /* skip to the end of the common header */
    i = get_bits_count(&s->gb);
    if (hdr_pos + hdr_size * 8 > i)
        skip_bits_long(&s->gb, hdr_pos + hdr_size * 8 - i);

    for (chset_index = 0; chset_index < s->xll_nch_sets; chset_index++) {
        XllChSetSubHeader *chset = &s->xll_chsets[chset_index];
        hdr_pos  = get_bits_count(&s->gb);
        hdr_size = get_bits(&s->gb, 10) + 1;

        chset->channels           = get_bits(&s->gb, 4) + 1;
        chset->residual_encode    = get_bits(&s->gb, chset->channels);
        chset->bit_resolution     = get_bits(&s->gb, 5) + 1;
        chset->bit_width          = get_bits(&s->gb, 5) + 1;
        chset->sampling_frequency = ff_dca_sampling_freqs[get_bits(&s->gb, 4)];
        chset->samp_freq_interp   = get_bits(&s->gb, 2);
        chset->replacement_set    = get_bits(&s->gb, 2);
        if (chset->replacement_set)
            chset->active_replace_set = get_bits(&s->gb, 1);

        if (s->one2one_map_chtospkr) {
            chset->primary_ch_set              = get_bits(&s->gb, 1);
            chset->downmix_coeff_code_embedded = get_bits(&s->gb, 1);
            if (chset->downmix_coeff_code_embedded) {
                chset->downmix_embedded = get_bits(&s->gb, 1);
                if (chset->primary_ch_set) {
                    chset->downmix_type = get_bits(&s->gb, 3);
                    if (chset->downmix_type > 6) {
                        av_log(s->avctx, AV_LOG_ERROR,
                               "XLL: Invalid channel set downmix type\n");
                        return AVERROR_INVALIDDATA;
                    }
                }
            }
            chset->hier_chset = get_bits(&s->gb, 1);

            if (chset->downmix_coeff_code_embedded) {
                /* nDownmixCoeffs is specified as N * M. For a primary
                 * channel set, it appears that N = number of
                 * channels, and M is the number of downmix channels.
                 *
                 * For a non-primary channel set, N is specified as
                 * number of channels + 1, and M is derived from the
                 * channel set hierarchy, and at least in simple cases
                 * M is the number of channels in preceding channel
                 * sets. */
                if (chset->primary_ch_set) {
                    static const char dmix_table[7] = { 1, 2, 2, 3, 3, 4, 4 };
                    chset->downmix_ncoeffs = chset->channels * dmix_table[chset->downmix_type];
                } else
                    chset->downmix_ncoeffs = (chset->channels + 1) * s->xll_channels;

                if (chset->downmix_ncoeffs > DCA_XLL_DMIX_NCOEFFS_MAX) {
                    avpriv_request_sample(s->avctx,
                                          "XLL: More than %d downmix coefficients",
                                          DCA_XLL_DMIX_NCOEFFS_MAX);
                    return AVERROR_PATCHWELCOME;
                } else if (chset->primary_ch_set) {
                    for (i = 0; i < chset->downmix_ncoeffs; i++)
                        if ((chset->downmix_coeffs[i] = dca_get_dmix_coeff(s)) == -1)
                            return AVERROR_INVALIDDATA;
                } else {
                    unsigned c, r;
                    for (c = 0, i = 0; c < s->xll_channels; c++, i += chset->channels + 1) {
                        if ((chset->downmix_coeffs[i] = dca_get_inv_dmix_coeff(s)) == -1)
                            return AVERROR_INVALIDDATA;
                        for (r = 1; r <= chset->channels; r++) {
                            int32_t coeff = dca_get_dmix_coeff(s);
                            if (coeff == -1)
                                return AVERROR_INVALIDDATA;
                            chset->downmix_coeffs[i + r] =
                                (chset->downmix_coeffs[i] * (int64_t) coeff + (1 << 15)) >> 16;
                        }
                    }
                }
            }
            chset->ch_mask_enabled = get_bits(&s->gb, 1);
            if (chset->ch_mask_enabled)
                chset->ch_mask = get_bits(&s->gb, s->xll_bits4ch_mask);
            else
                /* Skip speaker configuration bits */
                skip_bits_long(&s->gb, 25 * chset->channels);
        } else {
            chset->primary_ch_set              = 1;
            chset->downmix_coeff_code_embedded = 0;
            /* Spec: NumChHierChSet = 0, NumDwnMixCodeCoeffs = 0, whatever that means. */
            chset->mapping_coeffs_present = get_bits(&s->gb, 1);
            if (chset->mapping_coeffs_present) {
                avpriv_report_missing_feature(s->avctx, "XLL: mapping coefficients");
                return AVERROR_PATCHWELCOME;
            }
        }
        if (chset->sampling_frequency > 96000)
            chset->num_freq_bands = 2 * (1 + get_bits(&s->gb, 1));
        else
            chset->num_freq_bands = 1;

        if (chset->num_freq_bands > 1) {
            avpriv_report_missing_feature(s->avctx, "XLL: num_freq_bands > 1");
            return AVERROR_PATCHWELCOME;
        }

        if (get_bits(&s->gb, 1)) { /* pw_ch_decor_enabled */
            int bits = av_ceil_log2(chset->channels);
            for (i = 0; i < chset->channels; i++) {
                unsigned j = get_bits(&s->gb, bits);
                if (j >= chset->channels) {
                    av_log(s->avctx, AV_LOG_ERROR,
                           "Original channel order value %u too large, only %d channels.\n",
                           j, chset->channels);
                    return AVERROR_INVALIDDATA;
                }
                chset->orig_chan_order[0][i]     = j;
                chset->orig_chan_order_inv[0][j] = i;
            }
            for (i = 0; i < chset->channels / 2; i++) {
                if (get_bits(&s->gb, 1)) /* bChPFlag */
                    chset->pw_ch_pairs_coeffs[0][i] = get_bits_sm(&s->gb, 7);
                else
                    chset->pw_ch_pairs_coeffs[0][i] = 0;
            }
        } else {
            for (i = 0; i < chset->channels; i++)
                chset->orig_chan_order[0][i]     =
                chset->orig_chan_order_inv[0][i] = i;
            for (i = 0; i < chset->channels / 2; i++)
                chset->pw_ch_pairs_coeffs[0][i] = 0;
        }
        /* Adaptive prediction order */
        chset->adapt_order_max[0] = 0;
        for (i = 0; i < chset->channels; i++) {
            chset->adapt_order[0][i] = get_bits(&s->gb, 4);
            if (chset->adapt_order_max[0] < chset->adapt_order[0][i])
                chset->adapt_order_max[0] = chset->adapt_order[0][i];
        }
        /* Fixed prediction order, used in case the adaptive order
         * above is zero */
        for (i = 0; i < chset->channels; i++)
            chset->fixed_order[0][i] =
                chset->adapt_order[0][i] ? 0 : get_bits(&s->gb, 2);

        for (i = 0; i < chset->channels; i++) {
            unsigned j;
            for (j = 0; j < chset->adapt_order[0][i]; j++)
                chset->lpc_refl_coeffs_q_ind[0][i][j] = get_bits(&s->gb, 8);
        }

        if (s->xll_scalable_lsb) {
            chset->lsb_fsize[0] = get_bits(&s->gb, s->xll_bits4seg_size);

            for (i = 0; i < chset->channels; i++)
                chset->scalable_lsbs[0][i] = get_bits(&s->gb, 4);
            for (i = 0; i < chset->channels; i++)
                chset->bit_width_adj_per_ch[0][i] = get_bits(&s->gb, 4);
        } else {
            memset(chset->scalable_lsbs[0], 0,
                   chset->channels * sizeof(chset->scalable_lsbs[0][0]));
            memset(chset->bit_width_adj_per_ch[0], 0,
                   chset->channels * sizeof(chset->bit_width_adj_per_ch[0][0]));
        }

        s->xll_channels          += chset->channels;
        s->xll_residual_channels += chset->channels -
                                    av_popcount(chset->residual_encode);

        /* FIXME: Parse header data for extra frequency bands. */

        /* Skip to end of channel set sub header. */
        i = get_bits_count(&s->gb);
        if (hdr_pos + 8 * hdr_size < i) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "chset header too large, %d bits, should be <= %d bits\n",
                   i - hdr_pos, 8 * hdr_size);
            return AVERROR_INVALIDDATA;
        }
        if (hdr_pos + 8 * hdr_size > i)
            skip_bits_long(&s->gb, hdr_pos + 8 * hdr_size - i);
    }
    return 0;
}

/* parse XLL navigation table */
int ff_dca_xll_decode_navi(DCAContext *s, int asset_end)
{
    int nbands, band, chset, seg, data_start;

    /* FIXME: Supports only a single frequency band */
    nbands = 1;

    for (band = 0; band < nbands; band++) {
        s->xll_navi.band_size[band] = 0;
        for (seg = 0; seg < s->xll_segments; seg++) {
            /* Note: The spec, ETSI TS 102 114 V1.4.1 (2012-09), says
             * we should read a base value for segment_size from the
             * stream, before reading the sizes of the channel sets.
             * But that's apparently incorrect. */
            s->xll_navi.segment_size[band][seg] = 0;

            for (chset = 0; chset < s->xll_nch_sets; chset++)
                if (band < s->xll_chsets[chset].num_freq_bands) {
                    s->xll_navi.chset_size[band][seg][chset] =
                        get_bits(&s->gb, s->xll_bits4seg_size) + 1;
                    s->xll_navi.segment_size[band][seg] +=
                        s->xll_navi.chset_size[band][seg][chset];
                }
            s->xll_navi.band_size[band] += s->xll_navi.segment_size[band][seg];
        }
    }
    /* Align to 8 bits and skip 16-bit CRC. */
    skip_bits_long(&s->gb, 16 + ((-get_bits_count(&s->gb)) & 7));

    data_start = get_bits_count(&s->gb);
    if (data_start + 8 * s->xll_navi.band_size[0] > asset_end) {
        av_log(s->avctx, AV_LOG_ERROR,
               "XLL: Data in NAVI table exceeds containing asset\n"
               "start: %d (bit), size %u (bytes), end %d (bit), error %u\n",
               data_start, s->xll_navi.band_size[0], asset_end,
               data_start + 8 * s->xll_navi.band_size[0] - asset_end);
        return AVERROR_INVALIDDATA;
    }
    init_get_bits(&s->xll_navi.gb, s->gb.buffer + data_start / 8,
                  8 * s->xll_navi.band_size[0]);
    return 0;
}

static void dca_xll_inv_adapt_pred(int *samples, int nsamples, unsigned order,
                                   const int *prev, const uint8_t *q_ind)
{
    static const uint16_t table[0x81] = {
            0,  3070,  5110,  7140,  9156, 11154, 13132, 15085,
        17010, 18904, 20764, 22588, 24373, 26117, 27818, 29474,
        31085, 32648, 34164, 35631, 37049, 38418, 39738, 41008,
        42230, 43404, 44530, 45609, 46642, 47630, 48575, 49477,
        50337, 51157, 51937, 52681, 53387, 54059, 54697, 55302,
        55876, 56421, 56937, 57426, 57888, 58326, 58741, 59132,
        59502, 59852, 60182, 60494, 60789, 61066, 61328, 61576,
        61809, 62029, 62236, 62431, 62615, 62788, 62951, 63105,
        63250, 63386, 63514, 63635, 63749, 63855, 63956, 64051,
        64140, 64224, 64302, 64376, 64446, 64512, 64573, 64631,
        64686, 64737, 64785, 64830, 64873, 64913, 64950, 64986,
        65019, 65050, 65079, 65107, 65133, 65157, 65180, 65202,
        65222, 65241, 65259, 65275, 65291, 65306, 65320, 65333,
        65345, 65357, 65368, 65378, 65387, 65396, 65405, 65413,
        65420, 65427, 65434, 65440, 65446, 65451, 65456, 65461,
        65466, 65470, 65474, 65478, 65481, 65485, 65488, 65491,
        65535, /* Final value is for the -128 corner case, see below. */
    };
    int c[DCA_XLL_AORDER_MAX];
    int64_t s;
    unsigned i, j;

    for (i = 0; i < order; i++) {
        if (q_ind[i] & 1)
            /* The index value 0xff corresponds to a lookup of entry 0x80 in
             * the table, and no value is provided in the specification. */
            c[i] = -table[(q_ind[i] >> 1) + 1];
        else
            c[i] = table[q_ind[i] >> 1];
    }
    /* The description in the spec is a bit convoluted. We can convert
     * the reflected values to direct values in place, using a
     * sequence of reflections operating on two values. */
    for (i = 1; i < order; i++) {
        /* i = 1: scale c[0]
         * i = 2: reflect c[0] <-> c[1]
         * i = 3: scale c[1], reflect c[0] <-> c[2]
         * i = 4: reflect c[0] <-> c[3] reflect c[1] <-> c[2]
         * ... */
        if (i & 1)
            c[i / 2] += ((int64_t) c[i] * c[i / 2] + 0x8000) >> 16;
        for (j = 0; j < i / 2; j++) {
            int r0 = c[j];
            int r1 = c[i - j - 1];
            c[j]         += ((int64_t) c[i] * r1 + 0x8000) >> 16;
            c[i - j - 1] += ((int64_t) c[i] * r0 + 0x8000) >> 16;
        }
    }
    /* Apply predictor. */
    /* NOTE: Processing samples in this order means that the
     * predictor is applied to the newly reconstructed samples. */
    if (prev) {
        for (i = 0; i < order; i++) {
            for (j = s = 0; j < i; j++)
                s += (int64_t) c[j] * samples[i - 1 - j];
            for (; j < order; j++)
                s += (int64_t) c[j] * prev[DCA_XLL_AORDER_MAX + i - 1 - j];

            samples[i] -= av_clip((s + 0x8000) >> 16, -0x1000000, 0xffffff);
        }
    }
    for (i = order; i < nsamples; i++) {
        for (j = s = 0; j < order; j++)
            s += (int64_t) c[j] * samples[i - 1 - j];

        /* NOTE: Equations seem to imply addition, while the
         * pseudocode seems to use subtraction.*/
        samples[i] -= av_clip((s + 0x8000) >> 16, -0x1000000, 0xffffff);
    }
}

int ff_dca_xll_decode_audio(DCAContext *s, AVFrame *frame)
{
    /* FIXME: Decodes only the first frequency band. */
    int seg, chset_i;

    /* Coding parameters for each channel set. */
    struct coding_params {
        int seg_type;
        int rice_code_flag[16];
        int pancAuxABIT[16];
        int pancABIT0[16];  /* Not sure what this is */
        int pancABIT[16];   /* Not sure what this is */
        int nSamplPart0[16];
    } param_state[16];

    GetBitContext *gb = &s->xll_navi.gb;
    int *history;

    /* Layout: First the sample buffer for one segment per channel,
     * followed by history buffers of DCA_XLL_AORDER_MAX samples for
     * each channel. */
    av_fast_malloc(&s->xll_sample_buf, &s->xll_sample_buf_size,
                   (s->xll_smpl_in_seg + DCA_XLL_AORDER_MAX) *
                   s->xll_channels * sizeof(*s->xll_sample_buf));
    if (!s->xll_sample_buf)
        return AVERROR(ENOMEM);

    history = s->xll_sample_buf + s->xll_smpl_in_seg * s->xll_channels;

    for (seg = 0; seg < s->xll_segments; seg++) {
        unsigned in_channel;

        for (chset_i = in_channel = 0; chset_i < s->xll_nch_sets; chset_i++) {
            /* The spec isn't very explicit, but I think the NAVI sizes are in bytes. */
            int end_pos = get_bits_count(gb) +
                          8 * s->xll_navi.chset_size[0][seg][chset_i];
            int i, j;
            struct coding_params *params = &param_state[chset_i];
            /* I think this flag means that we should keep seg_type and
             * other parameters from the previous segment. */
            int use_seg_state_code_param;
            XllChSetSubHeader *chset = &s->xll_chsets[chset_i];
            if (in_channel >= s->avctx->channels)
                /* FIXME: Could go directly to next segment */
                goto next_chset;

            if (s->avctx->sample_rate != chset->sampling_frequency) {
                av_log(s->avctx, AV_LOG_WARNING,
                       "XLL: unexpected chset sample rate %d, expected %d\n",
                       chset->sampling_frequency, s->avctx->sample_rate);
                goto next_chset;
            }
            if (seg != 0)
                use_seg_state_code_param = get_bits(gb, 1);
            else
                use_seg_state_code_param = 0;

            if (!use_seg_state_code_param) {
                int num_param_sets, i;
                unsigned bits4ABIT;

                params->seg_type = get_bits(gb, 1);
                num_param_sets   = params->seg_type ? 1 : chset->channels;

                if (chset->bit_width > 16) {
                    bits4ABIT = 5;
                } else {
                    if (chset->bit_width > 8)
                        bits4ABIT = 4;
                    else
                        bits4ABIT = 3;
                    if (s->xll_nch_sets > 1)
                        bits4ABIT++;
                }

                for (i = 0; i < num_param_sets; i++) {
                    params->rice_code_flag[i] = get_bits(gb, 1);
                    if (!params->seg_type && params->rice_code_flag[i] && get_bits(gb, 1))
                        params->pancAuxABIT[i] = get_bits(gb, bits4ABIT) + 1;
                    else
                        params->pancAuxABIT[i] = 0;
                }

                for (i = 0; i < num_param_sets; i++) {
                    if (!seg) {
                        /* Parameters for part 1 */
                        params->pancABIT0[i] = get_bits(gb, bits4ABIT);
                        if (params->rice_code_flag[i] == 0 && params->pancABIT0[i] > 0)
                            /* For linear code */
                            params->pancABIT0[i]++;

                        /* NOTE: In the spec, not indexed by band??? */
                        if (params->seg_type == 0)
                            params->nSamplPart0[i] = chset->adapt_order[0][i];
                        else
                            params->nSamplPart0[i] = chset->adapt_order_max[0];
                    } else
                        params->nSamplPart0[i] = 0;

                    /* Parameters for part 2 */
                    params->pancABIT[i] = get_bits(gb, bits4ABIT);
                    if (params->rice_code_flag[i] == 0 && params->pancABIT[i] > 0)
                        /* For linear code */
                        params->pancABIT[i]++;
                }
            }
            for (i = 0; i < chset->channels; i++) {
                int param_index = params->seg_type ? 0 : i;
                int part0       = params->nSamplPart0[param_index];
                int bits        = part0 ? params->pancABIT0[param_index] : 0;
                int *sample_buf = s->xll_sample_buf +
                                  (in_channel + i) * s->xll_smpl_in_seg;

                if (!params->rice_code_flag[param_index]) {
                    /* Linear code */
                    if (bits)
                        for (j = 0; j < part0; j++)
                            sample_buf[j] = get_bits_sm(gb, bits);
                    else
                        memset(sample_buf, 0, part0 * sizeof(sample_buf[0]));

                    /* Second part */
                    bits = params->pancABIT[param_index];
                    if (bits)
                        for (j = part0; j < s->xll_smpl_in_seg; j++)
                            sample_buf[j] = get_bits_sm(gb, bits);
                    else
                        memset(sample_buf + part0, 0,
                               (s->xll_smpl_in_seg - part0) * sizeof(sample_buf[0]));
                } else {
                    int aux_bits = params->pancAuxABIT[param_index];

                    for (j = 0; j < part0; j++) {
                        /* FIXME: Is this identical to Golomb code? */
                        int t = get_unary(gb, 1, 33) << bits;
                        /* FIXME: Could move this test outside of the loop, for efficiency. */
                        if (bits)
                            t |= get_bits(gb, bits);
                        sample_buf[j] = (t & 1) ? -(t >> 1) - 1 : (t >> 1);
                    }

                    /* Second part */
                    bits = params->pancABIT[param_index];

                    /* Follow the spec's suggestion of using the
                     * buffer also to store the hybrid-rice flags. */
                    memset(sample_buf + part0, 0,
                           (s->xll_smpl_in_seg - part0) * sizeof(sample_buf[0]));

                    if (aux_bits > 0) {
                        /* For hybrid rice encoding, some samples are linearly
                         * coded. According to the spec, "nBits4SamplLoci" bits
                         * are used for each index, but this value is not
                         * defined. I guess we should use log2(xll_smpl_in_seg)
                         * bits. */
                        int count = get_bits(gb, s->xll_log_smpl_in_seg);
                        av_log(s->avctx, AV_LOG_DEBUG, "aux count %d (bits %d)\n",
                               count, s->xll_log_smpl_in_seg);

                        for (j = 0; j < count; j++)
                            sample_buf[get_bits(gb, s->xll_log_smpl_in_seg)] = 1;
                    }
                    for (j = part0; j < s->xll_smpl_in_seg; j++) {
                        if (!sample_buf[j]) {
                            int t = get_unary(gb, 1, 33);
                            if (bits)
                                t = (t << bits) | get_bits(gb, bits);
                            sample_buf[j] = (t & 1) ? -(t >> 1) - 1 : (t >> 1);
                        } else
                            sample_buf[j] = get_bits_sm(gb, aux_bits);
                    }
                }
            }

            for (i = 0; i < chset->channels; i++) {
                unsigned adapt_order = chset->adapt_order[0][i];
                int *sample_buf = s->xll_sample_buf +
                                  (in_channel + i) * s->xll_smpl_in_seg;
                int *prev = history + (in_channel + i) * DCA_XLL_AORDER_MAX;

                if (!adapt_order) {
                    unsigned order;
                    for (order = chset->fixed_order[0][i]; order > 0; order--) {
                        unsigned j;
                        for (j = 1; j < s->xll_smpl_in_seg; j++)
                            sample_buf[j] += sample_buf[j - 1];
                    }
                } else
                    /* Inverse adaptive prediction, in place. */
                    dca_xll_inv_adapt_pred(sample_buf, s->xll_smpl_in_seg,
                                           adapt_order, seg ? prev : NULL,
                                           chset->lpc_refl_coeffs_q_ind[0][i]);
                memcpy(prev, sample_buf + s->xll_smpl_in_seg - DCA_XLL_AORDER_MAX,
                       DCA_XLL_AORDER_MAX * sizeof(*prev));
            }
            for (i = 1; i < chset->channels; i += 2) {
                int coeff = chset->pw_ch_pairs_coeffs[0][i / 2];
                if (coeff != 0) {
                    int *sample_buf = s->xll_sample_buf +
                                      (in_channel + i) * s->xll_smpl_in_seg;
                    int *prev = sample_buf - s->xll_smpl_in_seg;
                    unsigned j;
                    for (j = 0; j < s->xll_smpl_in_seg; j++)
                        /* Shift is unspecified, but should apparently be 3. */
                        sample_buf[j] += ((int64_t) coeff * prev[j] + 4) >> 3;
                }
            }

            if (s->xll_scalable_lsb) {
                int lsb_start = end_pos - 8 * chset->lsb_fsize[0] -
                                8 * (s->xll_banddata_crc & 2);
                int done;
                i = get_bits_count(gb);
                if (i > lsb_start) {
                    av_log(s->avctx, AV_LOG_ERROR,
                           "chset data lsb exceeds NAVI size, end_pos %d, lsb_start %d, pos %d\n",
                           end_pos, lsb_start, i);
                    return AVERROR_INVALIDDATA;
                }
                if (i < lsb_start)
                    skip_bits_long(gb, lsb_start - i);

                for (i = done = 0; i < chset->channels; i++) {
                    int bits = chset->scalable_lsbs[0][i];
                    if (bits > 0) {
                        /* The channel reordering is conceptually done
                         * before adding the lsb:s, so we need to do
                         * the inverse permutation here. */
                        unsigned pi = chset->orig_chan_order_inv[0][i];
                        int *sample_buf = s->xll_sample_buf +
                                          (in_channel + pi) * s->xll_smpl_in_seg;
                        int adj = chset->bit_width_adj_per_ch[0][i];
                        int msb_shift = bits;
                        unsigned j;

                        if (adj > 0)
                            msb_shift += adj - 1;

                        for (j = 0; j < s->xll_smpl_in_seg; j++)
                            sample_buf[j] = (sample_buf[j] << msb_shift) +
                                            (get_bits(gb, bits) << adj);

                        done += bits * s->xll_smpl_in_seg;
                    }
                }
                if (done > 8 * chset->lsb_fsize[0]) {
                    av_log(s->avctx, AV_LOG_ERROR,
                           "chset lsb exceeds lsb_size\n");
                    return AVERROR_INVALIDDATA;
                }
            }

            /* Store output. */
            for (i = 0; i < chset->channels; i++) {
                int *sample_buf = s->xll_sample_buf +
                                  (in_channel + i) * s->xll_smpl_in_seg;
                int shift = 1 - chset->bit_resolution;
                int out_channel = chset->orig_chan_order[0][i];
                float *out;

                /* XLL uses the channel order C, L, R, and we want L,
                 * R, C. FIXME: Generalize. */
                if (chset->ch_mask_enabled &&
                    (chset->ch_mask & 7) == 7 && out_channel < 3)
                    out_channel = out_channel ? out_channel - 1 : 2;

                out_channel += in_channel;
                if (out_channel >= s->avctx->channels)
                    continue;

                out  = (float *) frame->extended_data[out_channel];
                out += seg * s->xll_smpl_in_seg;

                /* NOTE: A one bit means residual encoding is *not* used. */
                if ((chset->residual_encode >> i) & 1) {
                    /* Replace channel samples.
                     * FIXME: Most likely not the right thing to do. */
                    for (j = 0; j < s->xll_smpl_in_seg; j++)
                        out[j] = ldexpf(sample_buf[j], shift);
                } else {
                    /* Add residual signal to core channel */
                    for (j = 0; j < s->xll_smpl_in_seg; j++)
                        out[j] += ldexpf(sample_buf[j], shift);
                }
            }

            if (chset->downmix_coeff_code_embedded &&
                !chset->primary_ch_set && chset->hier_chset) {
                /* Undo hierarchical downmix of earlier channels. */
                unsigned mix_channel;
                for (mix_channel = 0; mix_channel < in_channel; mix_channel++) {
                    float *mix_buf;
                    const int *col;
                    float coeff;
                    unsigned row;
                    /* Similar channel reorder C, L, R vs L, R, C reorder. */
                    if (chset->ch_mask_enabled &&
                        (chset->ch_mask & 7) == 7 && mix_channel < 3)
                        mix_buf = (float *) frame->extended_data[mix_channel ? mix_channel - 1 : 2];
                    else
                        mix_buf = (float *) frame->extended_data[mix_channel];

                    mix_buf += seg * s->xll_smpl_in_seg;
                    col = &chset->downmix_coeffs[mix_channel * (chset->channels + 1)];

                    /* Scale */
                    coeff = ldexpf(col[0], -16);
                    for (j = 0; j < s->xll_smpl_in_seg; j++)
                        mix_buf[j] *= coeff;

                    for (row = 0;
                         row < chset->channels && in_channel + row < s->avctx->channels;
                         row++)
                        if (col[row + 1]) {
                            const float *new_channel =
                                (const float *) frame->extended_data[in_channel + row];
                            new_channel += seg * s->xll_smpl_in_seg;
                            coeff        = ldexpf(col[row + 1], -15);
                            for (j = 0; j < s->xll_smpl_in_seg; j++)
                                mix_buf[j] -= coeff * new_channel[j];
                        }
                }
            }

next_chset:
            in_channel += chset->channels;
            /* Skip to next channel set using the NAVI info. */
            i = get_bits_count(gb);
            if (i > end_pos) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "chset data exceeds NAVI size\n");
                return AVERROR_INVALIDDATA;
            }
            if (i < end_pos)
                skip_bits_long(gb, end_pos - i);
        }
    }
    return 0;
}
