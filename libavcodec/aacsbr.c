/*
 * AAC Spectral Band Replication decoding functions
 * Copyright (c) 2008-2009 Robert Swain ( rob opendot cl )
 * Copyright (c) 2009-2010 Alex Converse <alex.converse@gmail.com>
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
 * AAC Spectral Band Replication decoding functions
 * @author Robert Swain ( rob opendot cl )
 */

#include "aac.h"
#include "sbr.h"
#include "aacsbr.h"
#include "aacsbrdata.h"
#include "fft.h"
#include "aacps.h"
#include "sbrdsp.h"
#include "libavutil/internal.h"
#include "libavutil/libm.h"
#include "libavutil/avassert.h"

#include <stdint.h>
#include <float.h>
#include <math.h>

#define ENVELOPE_ADJUSTMENT_OFFSET 2
#define NOISE_FLOOR_OFFSET 6.0f

#if ARCH_MIPS
#include "mips/aacsbr_mips.h"
#endif /* ARCH_MIPS */

/**
 * SBR VLC tables
 */
enum {
    T_HUFFMAN_ENV_1_5DB,
    F_HUFFMAN_ENV_1_5DB,
    T_HUFFMAN_ENV_BAL_1_5DB,
    F_HUFFMAN_ENV_BAL_1_5DB,
    T_HUFFMAN_ENV_3_0DB,
    F_HUFFMAN_ENV_3_0DB,
    T_HUFFMAN_ENV_BAL_3_0DB,
    F_HUFFMAN_ENV_BAL_3_0DB,
    T_HUFFMAN_NOISE_3_0DB,
    T_HUFFMAN_NOISE_BAL_3_0DB,
};

/**
 * bs_frame_class - frame class of current SBR frame (14496-3 sp04 p98)
 */
enum {
    FIXFIX,
    FIXVAR,
    VARFIX,
    VARVAR,
};

enum {
    EXTENSION_ID_PS = 2,
};

static VLC vlc_sbr[10];
static const int8_t vlc_sbr_lav[10] =
    { 60, 60, 24, 24, 31, 31, 12, 12, 31, 12 };

#define SBR_INIT_VLC_STATIC(num, size) \
    INIT_VLC_STATIC(&vlc_sbr[num], 9, sbr_tmp[num].table_size / sbr_tmp[num].elem_size,     \
                    sbr_tmp[num].sbr_bits ,                      1,                      1, \
                    sbr_tmp[num].sbr_codes, sbr_tmp[num].elem_size, sbr_tmp[num].elem_size, \
                    size)

#define SBR_VLC_ROW(name) \
    { name ## _codes, name ## _bits, sizeof(name ## _codes), sizeof(name ## _codes[0]) }

static void aacsbr_func_ptr_init(AACSBRContext *c);

av_cold void ff_aac_sbr_init(void)
{
    int n;
    static const struct {
        const void *sbr_codes, *sbr_bits;
        const unsigned int table_size, elem_size;
    } sbr_tmp[] = {
        SBR_VLC_ROW(t_huffman_env_1_5dB),
        SBR_VLC_ROW(f_huffman_env_1_5dB),
        SBR_VLC_ROW(t_huffman_env_bal_1_5dB),
        SBR_VLC_ROW(f_huffman_env_bal_1_5dB),
        SBR_VLC_ROW(t_huffman_env_3_0dB),
        SBR_VLC_ROW(f_huffman_env_3_0dB),
        SBR_VLC_ROW(t_huffman_env_bal_3_0dB),
        SBR_VLC_ROW(f_huffman_env_bal_3_0dB),
        SBR_VLC_ROW(t_huffman_noise_3_0dB),
        SBR_VLC_ROW(t_huffman_noise_bal_3_0dB),
    };

    // SBR VLC table initialization
    SBR_INIT_VLC_STATIC(0, 1098);
    SBR_INIT_VLC_STATIC(1, 1092);
    SBR_INIT_VLC_STATIC(2, 768);
    SBR_INIT_VLC_STATIC(3, 1026);
    SBR_INIT_VLC_STATIC(4, 1058);
    SBR_INIT_VLC_STATIC(5, 1052);
    SBR_INIT_VLC_STATIC(6, 544);
    SBR_INIT_VLC_STATIC(7, 544);
    SBR_INIT_VLC_STATIC(8, 592);
    SBR_INIT_VLC_STATIC(9, 512);

    for (n = 1; n < 320; n++)
        sbr_qmf_window_us[320 + n] = sbr_qmf_window_us[320 - n];
    sbr_qmf_window_us[384] = -sbr_qmf_window_us[384];
    sbr_qmf_window_us[512] = -sbr_qmf_window_us[512];

    for (n = 0; n < 320; n++)
        sbr_qmf_window_ds[n] = sbr_qmf_window_us[2*n];

    ff_ps_init();
}

/** Places SBR in pure upsampling mode. */
static void sbr_turnoff(SpectralBandReplication *sbr) {
    sbr->start = 0;
    // Init defults used in pure upsampling mode
    sbr->kx[1] = 32; //Typo in spec, kx' inits to 32
    sbr->m[1] = 0;
    // Reset values for first SBR header
    sbr->data[0].e_a[1] = sbr->data[1].e_a[1] = -1;
    memset(&sbr->spectrum_params, -1, sizeof(SpectrumParameters));
}

av_cold void ff_aac_sbr_ctx_init(AACContext *ac, SpectralBandReplication *sbr)
{
    if(sbr->mdct.mdct_bits)
        return;
    sbr->kx[0] = sbr->kx[1];
    sbr_turnoff(sbr);
    sbr->data[0].synthesis_filterbank_samples_offset = SBR_SYNTHESIS_BUF_SIZE - (1280 - 128);
    sbr->data[1].synthesis_filterbank_samples_offset = SBR_SYNTHESIS_BUF_SIZE - (1280 - 128);
    /* SBR requires samples to be scaled to +/-32768.0 to work correctly.
     * mdct scale factors are adjusted to scale up from +/-1.0 at analysis
     * and scale back down at synthesis. */
    ff_mdct_init(&sbr->mdct,     7, 1, 1.0 / (64 * 32768.0));
    ff_mdct_init(&sbr->mdct_ana, 7, 1, -2.0 * 32768.0);
    ff_ps_ctx_init(&sbr->ps);
    ff_sbrdsp_init(&sbr->dsp);
    aacsbr_func_ptr_init(&sbr->c);
}

av_cold void ff_aac_sbr_ctx_close(SpectralBandReplication *sbr)
{
    ff_mdct_end(&sbr->mdct);
    ff_mdct_end(&sbr->mdct_ana);
}

static int qsort_comparison_function_int16(const void *a, const void *b)
{
    return *(const int16_t *)a - *(const int16_t *)b;
}

static inline int in_table_int16(const int16_t *table, int last_el, int16_t needle)
{
    int i;
    for (i = 0; i <= last_el; i++)
        if (table[i] == needle)
            return 1;
    return 0;
}

/// Limiter Frequency Band Table (14496-3 sp04 p198)
static void sbr_make_f_tablelim(SpectralBandReplication *sbr)
{
    int k;
    if (sbr->bs_limiter_bands > 0) {
        static const float bands_warped[3] = { 1.32715174233856803909f,   //2^(0.49/1.2)
                                               1.18509277094158210129f,   //2^(0.49/2)
                                               1.11987160404675912501f }; //2^(0.49/3)
        const float lim_bands_per_octave_warped = bands_warped[sbr->bs_limiter_bands - 1];
        int16_t patch_borders[7];
        uint16_t *in = sbr->f_tablelim + 1, *out = sbr->f_tablelim;

        patch_borders[0] = sbr->kx[1];
        for (k = 1; k <= sbr->num_patches; k++)
            patch_borders[k] = patch_borders[k-1] + sbr->patch_num_subbands[k-1];

        memcpy(sbr->f_tablelim, sbr->f_tablelow,
               (sbr->n[0] + 1) * sizeof(sbr->f_tablelow[0]));
        if (sbr->num_patches > 1)
            memcpy(sbr->f_tablelim + sbr->n[0] + 1, patch_borders + 1,
                   (sbr->num_patches - 1) * sizeof(patch_borders[0]));

        qsort(sbr->f_tablelim, sbr->num_patches + sbr->n[0],
              sizeof(sbr->f_tablelim[0]),
              qsort_comparison_function_int16);

        sbr->n_lim = sbr->n[0] + sbr->num_patches - 1;
        while (out < sbr->f_tablelim + sbr->n_lim) {
            if (*in >= *out * lim_bands_per_octave_warped) {
                *++out = *in++;
            } else if (*in == *out ||
                !in_table_int16(patch_borders, sbr->num_patches, *in)) {
                in++;
                sbr->n_lim--;
            } else if (!in_table_int16(patch_borders, sbr->num_patches, *out)) {
                *out = *in++;
                sbr->n_lim--;
            } else {
                *++out = *in++;
            }
        }
    } else {
        sbr->f_tablelim[0] = sbr->f_tablelow[0];
        sbr->f_tablelim[1] = sbr->f_tablelow[sbr->n[0]];
        sbr->n_lim = 1;
    }
}

static unsigned int read_sbr_header(SpectralBandReplication *sbr, GetBitContext *gb)
{
    unsigned int cnt = get_bits_count(gb);
    uint8_t bs_header_extra_1;
    uint8_t bs_header_extra_2;
    int old_bs_limiter_bands = sbr->bs_limiter_bands;
    SpectrumParameters old_spectrum_params;

    sbr->start = 1;

    // Save last spectrum parameters variables to compare to new ones
    memcpy(&old_spectrum_params, &sbr->spectrum_params, sizeof(SpectrumParameters));

    sbr->bs_amp_res_header              = get_bits1(gb);
    sbr->spectrum_params.bs_start_freq  = get_bits(gb, 4);
    sbr->spectrum_params.bs_stop_freq   = get_bits(gb, 4);
    sbr->spectrum_params.bs_xover_band  = get_bits(gb, 3);
                                          skip_bits(gb, 2); // bs_reserved

    bs_header_extra_1 = get_bits1(gb);
    bs_header_extra_2 = get_bits1(gb);

    if (bs_header_extra_1) {
        sbr->spectrum_params.bs_freq_scale  = get_bits(gb, 2);
        sbr->spectrum_params.bs_alter_scale = get_bits1(gb);
        sbr->spectrum_params.bs_noise_bands = get_bits(gb, 2);
    } else {
        sbr->spectrum_params.bs_freq_scale  = 2;
        sbr->spectrum_params.bs_alter_scale = 1;
        sbr->spectrum_params.bs_noise_bands = 2;
    }

    // Check if spectrum parameters changed
    if (memcmp(&old_spectrum_params, &sbr->spectrum_params, sizeof(SpectrumParameters)))
        sbr->reset = 1;

    if (bs_header_extra_2) {
        sbr->bs_limiter_bands  = get_bits(gb, 2);
        sbr->bs_limiter_gains  = get_bits(gb, 2);
        sbr->bs_interpol_freq  = get_bits1(gb);
        sbr->bs_smoothing_mode = get_bits1(gb);
    } else {
        sbr->bs_limiter_bands  = 2;
        sbr->bs_limiter_gains  = 2;
        sbr->bs_interpol_freq  = 1;
        sbr->bs_smoothing_mode = 1;
    }

    if (sbr->bs_limiter_bands != old_bs_limiter_bands && !sbr->reset)
        sbr_make_f_tablelim(sbr);

    return get_bits_count(gb) - cnt;
}

static int array_min_int16(const int16_t *array, int nel)
{
    int i, min = array[0];
    for (i = 1; i < nel; i++)
        min = FFMIN(array[i], min);
    return min;
}

static void make_bands(int16_t* bands, int start, int stop, int num_bands)
{
    int k, previous, present;
    float base, prod;

    base = powf((float)stop / start, 1.0f / num_bands);
    prod = start;
    previous = start;

    for (k = 0; k < num_bands-1; k++) {
        prod *= base;
        present  = lrintf(prod);
        bands[k] = present - previous;
        previous = present;
    }
    bands[num_bands-1] = stop - previous;
}

static int check_n_master(AVCodecContext *avctx, int n_master, int bs_xover_band)
{
    // Requirements (14496-3 sp04 p205)
    if (n_master <= 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid n_master: %d\n", n_master);
        return -1;
    }
    if (bs_xover_band >= n_master) {
        av_log(avctx, AV_LOG_ERROR,
               "Invalid bitstream, crossover band index beyond array bounds: %d\n",
               bs_xover_band);
        return -1;
    }
    return 0;
}

/// Master Frequency Band Table (14496-3 sp04 p194)
static int sbr_make_f_master(AACContext *ac, SpectralBandReplication *sbr,
                             SpectrumParameters *spectrum)
{
    unsigned int temp, max_qmf_subbands;
    unsigned int start_min, stop_min;
    int k;
    const int8_t *sbr_offset_ptr;
    int16_t stop_dk[13];

    if (sbr->sample_rate < 32000) {
        temp = 3000;
    } else if (sbr->sample_rate < 64000) {
        temp = 4000;
    } else
        temp = 5000;

    switch (sbr->sample_rate) {
    case 16000:
        sbr_offset_ptr = sbr_offset[0];
        break;
    case 22050:
        sbr_offset_ptr = sbr_offset[1];
        break;
    case 24000:
        sbr_offset_ptr = sbr_offset[2];
        break;
    case 32000:
        sbr_offset_ptr = sbr_offset[3];
        break;
    case 44100: case 48000: case 64000:
        sbr_offset_ptr = sbr_offset[4];
        break;
    case 88200: case 96000: case 128000: case 176400: case 192000:
        sbr_offset_ptr = sbr_offset[5];
        break;
    default:
        av_log(ac->avctx, AV_LOG_ERROR,
               "Unsupported sample rate for SBR: %d\n", sbr->sample_rate);
        return -1;
    }

    start_min = ((temp << 7) + (sbr->sample_rate >> 1)) / sbr->sample_rate;
    stop_min  = ((temp << 8) + (sbr->sample_rate >> 1)) / sbr->sample_rate;

    sbr->k[0] = start_min + sbr_offset_ptr[spectrum->bs_start_freq];

    if (spectrum->bs_stop_freq < 14) {
        sbr->k[2] = stop_min;
        make_bands(stop_dk, stop_min, 64, 13);
        qsort(stop_dk, 13, sizeof(stop_dk[0]), qsort_comparison_function_int16);
        for (k = 0; k < spectrum->bs_stop_freq; k++)
            sbr->k[2] += stop_dk[k];
    } else if (spectrum->bs_stop_freq == 14) {
        sbr->k[2] = 2*sbr->k[0];
    } else if (spectrum->bs_stop_freq == 15) {
        sbr->k[2] = 3*sbr->k[0];
    } else {
        av_log(ac->avctx, AV_LOG_ERROR,
               "Invalid bs_stop_freq: %d\n", spectrum->bs_stop_freq);
        return -1;
    }
    sbr->k[2] = FFMIN(64, sbr->k[2]);

    // Requirements (14496-3 sp04 p205)
    if (sbr->sample_rate <= 32000) {
        max_qmf_subbands = 48;
    } else if (sbr->sample_rate == 44100) {
        max_qmf_subbands = 35;
    } else if (sbr->sample_rate >= 48000)
        max_qmf_subbands = 32;
    else
        av_assert0(0);

    if (sbr->k[2] - sbr->k[0] > max_qmf_subbands) {
        av_log(ac->avctx, AV_LOG_ERROR,
               "Invalid bitstream, too many QMF subbands: %d\n", sbr->k[2] - sbr->k[0]);
        return -1;
    }

    if (!spectrum->bs_freq_scale) {
        int dk, k2diff;

        dk = spectrum->bs_alter_scale + 1;
        sbr->n_master = ((sbr->k[2] - sbr->k[0] + (dk&2)) >> dk) << 1;
        if (check_n_master(ac->avctx, sbr->n_master, sbr->spectrum_params.bs_xover_band))
            return -1;

        for (k = 1; k <= sbr->n_master; k++)
            sbr->f_master[k] = dk;

        k2diff = sbr->k[2] - sbr->k[0] - sbr->n_master * dk;
        if (k2diff < 0) {
            sbr->f_master[1]--;
            sbr->f_master[2]-= (k2diff < -1);
        } else if (k2diff) {
            sbr->f_master[sbr->n_master]++;
        }

        sbr->f_master[0] = sbr->k[0];
        for (k = 1; k <= sbr->n_master; k++)
            sbr->f_master[k] += sbr->f_master[k - 1];

    } else {
        int half_bands = 7 - spectrum->bs_freq_scale;      // bs_freq_scale  = {1,2,3}
        int two_regions, num_bands_0;
        int vdk0_max, vdk1_min;
        int16_t vk0[49];

        if (49 * sbr->k[2] > 110 * sbr->k[0]) {
            two_regions = 1;
            sbr->k[1] = 2 * sbr->k[0];
        } else {
            two_regions = 0;
            sbr->k[1] = sbr->k[2];
        }

        num_bands_0 = lrintf(half_bands * log2f(sbr->k[1] / (float)sbr->k[0])) * 2;

        if (num_bands_0 <= 0) { // Requirements (14496-3 sp04 p205)
            av_log(ac->avctx, AV_LOG_ERROR, "Invalid num_bands_0: %d\n", num_bands_0);
            return -1;
        }

        vk0[0] = 0;

        make_bands(vk0+1, sbr->k[0], sbr->k[1], num_bands_0);

        qsort(vk0 + 1, num_bands_0, sizeof(vk0[1]), qsort_comparison_function_int16);
        vdk0_max = vk0[num_bands_0];

        vk0[0] = sbr->k[0];
        for (k = 1; k <= num_bands_0; k++) {
            if (vk0[k] <= 0) { // Requirements (14496-3 sp04 p205)
                av_log(ac->avctx, AV_LOG_ERROR, "Invalid vDk0[%d]: %d\n", k, vk0[k]);
                return -1;
            }
            vk0[k] += vk0[k-1];
        }

        if (two_regions) {
            int16_t vk1[49];
            float invwarp = spectrum->bs_alter_scale ? 0.76923076923076923077f
                                                     : 1.0f; // bs_alter_scale = {0,1}
            int num_bands_1 = lrintf(half_bands * invwarp *
                                     log2f(sbr->k[2] / (float)sbr->k[1])) * 2;

            make_bands(vk1+1, sbr->k[1], sbr->k[2], num_bands_1);

            vdk1_min = array_min_int16(vk1 + 1, num_bands_1);

            if (vdk1_min < vdk0_max) {
                int change;
                qsort(vk1 + 1, num_bands_1, sizeof(vk1[1]), qsort_comparison_function_int16);
                change = FFMIN(vdk0_max - vk1[1], (vk1[num_bands_1] - vk1[1]) >> 1);
                vk1[1]           += change;
                vk1[num_bands_1] -= change;
            }

            qsort(vk1 + 1, num_bands_1, sizeof(vk1[1]), qsort_comparison_function_int16);

            vk1[0] = sbr->k[1];
            for (k = 1; k <= num_bands_1; k++) {
                if (vk1[k] <= 0) { // Requirements (14496-3 sp04 p205)
                    av_log(ac->avctx, AV_LOG_ERROR, "Invalid vDk1[%d]: %d\n", k, vk1[k]);
                    return -1;
                }
                vk1[k] += vk1[k-1];
            }

            sbr->n_master = num_bands_0 + num_bands_1;
            if (check_n_master(ac->avctx, sbr->n_master, sbr->spectrum_params.bs_xover_band))
                return -1;
            memcpy(&sbr->f_master[0],               vk0,
                   (num_bands_0 + 1) * sizeof(sbr->f_master[0]));
            memcpy(&sbr->f_master[num_bands_0 + 1], vk1 + 1,
                    num_bands_1      * sizeof(sbr->f_master[0]));

        } else {
            sbr->n_master = num_bands_0;
            if (check_n_master(ac->avctx, sbr->n_master, sbr->spectrum_params.bs_xover_band))
                return -1;
            memcpy(sbr->f_master, vk0, (num_bands_0 + 1) * sizeof(sbr->f_master[0]));
        }
    }

    return 0;
}

/// High Frequency Generation - Patch Construction (14496-3 sp04 p216 fig. 4.46)
static int sbr_hf_calc_npatches(AACContext *ac, SpectralBandReplication *sbr)
{
    int i, k, sb = 0;
    int msb = sbr->k[0];
    int usb = sbr->kx[1];
    int goal_sb = ((1000 << 11) + (sbr->sample_rate >> 1)) / sbr->sample_rate;

    sbr->num_patches = 0;

    if (goal_sb < sbr->kx[1] + sbr->m[1]) {
        for (k = 0; sbr->f_master[k] < goal_sb; k++) ;
    } else
        k = sbr->n_master;

    do {
        int odd = 0;
        for (i = k; i == k || sb > (sbr->k[0] - 1 + msb - odd); i--) {
            sb = sbr->f_master[i];
            odd = (sb + sbr->k[0]) & 1;
        }

        // Requirements (14496-3 sp04 p205) sets the maximum number of patches to 5.
        // After this check the final number of patches can still be six which is
        // illegal however the Coding Technologies decoder check stream has a final
        // count of 6 patches
        if (sbr->num_patches > 5) {
            av_log(ac->avctx, AV_LOG_ERROR, "Too many patches: %d\n", sbr->num_patches);
            return -1;
        }

        sbr->patch_num_subbands[sbr->num_patches]  = FFMAX(sb - usb, 0);
        sbr->patch_start_subband[sbr->num_patches] = sbr->k[0] - odd - sbr->patch_num_subbands[sbr->num_patches];

        if (sbr->patch_num_subbands[sbr->num_patches] > 0) {
            usb = sb;
            msb = sb;
            sbr->num_patches++;
        } else
            msb = sbr->kx[1];

        if (sbr->f_master[k] - sb < 3)
            k = sbr->n_master;
    } while (sb != sbr->kx[1] + sbr->m[1]);

    if (sbr->num_patches > 1 && sbr->patch_num_subbands[sbr->num_patches-1] < 3)
        sbr->num_patches--;

    return 0;
}

/// Derived Frequency Band Tables (14496-3 sp04 p197)
static int sbr_make_f_derived(AACContext *ac, SpectralBandReplication *sbr)
{
    int k, temp;

    sbr->n[1] = sbr->n_master - sbr->spectrum_params.bs_xover_band;
    sbr->n[0] = (sbr->n[1] + 1) >> 1;

    memcpy(sbr->f_tablehigh, &sbr->f_master[sbr->spectrum_params.bs_xover_band],
           (sbr->n[1] + 1) * sizeof(sbr->f_master[0]));
    sbr->m[1] = sbr->f_tablehigh[sbr->n[1]] - sbr->f_tablehigh[0];
    sbr->kx[1] = sbr->f_tablehigh[0];

    // Requirements (14496-3 sp04 p205)
    if (sbr->kx[1] + sbr->m[1] > 64) {
        av_log(ac->avctx, AV_LOG_ERROR,
               "Stop frequency border too high: %d\n", sbr->kx[1] + sbr->m[1]);
        return -1;
    }
    if (sbr->kx[1] > 32) {
        av_log(ac->avctx, AV_LOG_ERROR, "Start frequency border too high: %d\n", sbr->kx[1]);
        return -1;
    }

    sbr->f_tablelow[0] = sbr->f_tablehigh[0];
    temp = sbr->n[1] & 1;
    for (k = 1; k <= sbr->n[0]; k++)
        sbr->f_tablelow[k] = sbr->f_tablehigh[2 * k - temp];

    sbr->n_q = FFMAX(1, lrintf(sbr->spectrum_params.bs_noise_bands *
                               log2f(sbr->k[2] / (float)sbr->kx[1]))); // 0 <= bs_noise_bands <= 3
    if (sbr->n_q > 5) {
        av_log(ac->avctx, AV_LOG_ERROR, "Too many noise floor scale factors: %d\n", sbr->n_q);
        return -1;
    }

    sbr->f_tablenoise[0] = sbr->f_tablelow[0];
    temp = 0;
    for (k = 1; k <= sbr->n_q; k++) {
        temp += (sbr->n[0] - temp) / (sbr->n_q + 1 - k);
        sbr->f_tablenoise[k] = sbr->f_tablelow[temp];
    }

    if (sbr_hf_calc_npatches(ac, sbr) < 0)
        return -1;

    sbr_make_f_tablelim(sbr);

    sbr->data[0].f_indexnoise = 0;
    sbr->data[1].f_indexnoise = 0;

    return 0;
}

static av_always_inline void get_bits1_vector(GetBitContext *gb, uint8_t *vec,
                                              int elements)
{
    int i;
    for (i = 0; i < elements; i++) {
        vec[i] = get_bits1(gb);
    }
}

/** ceil(log2(index+1)) */
static const int8_t ceil_log2[] = {
    0, 1, 2, 2, 3, 3,
};

static int read_sbr_grid(AACContext *ac, SpectralBandReplication *sbr,
                         GetBitContext *gb, SBRData *ch_data)
{
    int i;
    unsigned bs_pointer = 0;
    // frameLengthFlag ? 15 : 16; 960 sample length frames unsupported; this value is numTimeSlots
    int abs_bord_trail = 16;
    int num_rel_lead, num_rel_trail;
    unsigned bs_num_env_old = ch_data->bs_num_env;

    ch_data->bs_freq_res[0] = ch_data->bs_freq_res[ch_data->bs_num_env];
    ch_data->bs_amp_res = sbr->bs_amp_res_header;
    ch_data->t_env_num_env_old = ch_data->t_env[bs_num_env_old];

    switch (ch_data->bs_frame_class = get_bits(gb, 2)) {
    case FIXFIX:
        ch_data->bs_num_env                 = 1 << get_bits(gb, 2);
        num_rel_lead                        = ch_data->bs_num_env - 1;
        if (ch_data->bs_num_env == 1)
            ch_data->bs_amp_res = 0;

        if (ch_data->bs_num_env > 4) {
            av_log(ac->avctx, AV_LOG_ERROR,
                   "Invalid bitstream, too many SBR envelopes in FIXFIX type SBR frame: %d\n",
                   ch_data->bs_num_env);
            return -1;
        }

        ch_data->t_env[0]                   = 0;
        ch_data->t_env[ch_data->bs_num_env] = abs_bord_trail;

        abs_bord_trail = (abs_bord_trail + (ch_data->bs_num_env >> 1)) /
                   ch_data->bs_num_env;
        for (i = 0; i < num_rel_lead; i++)
            ch_data->t_env[i + 1] = ch_data->t_env[i] + abs_bord_trail;

        ch_data->bs_freq_res[1] = get_bits1(gb);
        for (i = 1; i < ch_data->bs_num_env; i++)
            ch_data->bs_freq_res[i + 1] = ch_data->bs_freq_res[1];
        break;
    case FIXVAR:
        abs_bord_trail                     += get_bits(gb, 2);
        num_rel_trail                       = get_bits(gb, 2);
        ch_data->bs_num_env                 = num_rel_trail + 1;
        ch_data->t_env[0]                   = 0;
        ch_data->t_env[ch_data->bs_num_env] = abs_bord_trail;

        for (i = 0; i < num_rel_trail; i++)
            ch_data->t_env[ch_data->bs_num_env - 1 - i] =
                ch_data->t_env[ch_data->bs_num_env - i] - 2 * get_bits(gb, 2) - 2;

        bs_pointer = get_bits(gb, ceil_log2[ch_data->bs_num_env]);

        for (i = 0; i < ch_data->bs_num_env; i++)
            ch_data->bs_freq_res[ch_data->bs_num_env - i] = get_bits1(gb);
        break;
    case VARFIX:
        ch_data->t_env[0]                   = get_bits(gb, 2);
        num_rel_lead                        = get_bits(gb, 2);
        ch_data->bs_num_env                 = num_rel_lead + 1;
        ch_data->t_env[ch_data->bs_num_env] = abs_bord_trail;

        for (i = 0; i < num_rel_lead; i++)
            ch_data->t_env[i + 1] = ch_data->t_env[i] + 2 * get_bits(gb, 2) + 2;

        bs_pointer = get_bits(gb, ceil_log2[ch_data->bs_num_env]);

        get_bits1_vector(gb, ch_data->bs_freq_res + 1, ch_data->bs_num_env);
        break;
    case VARVAR:
        ch_data->t_env[0]                   = get_bits(gb, 2);
        abs_bord_trail                     += get_bits(gb, 2);
        num_rel_lead                        = get_bits(gb, 2);
        num_rel_trail                       = get_bits(gb, 2);
        ch_data->bs_num_env                 = num_rel_lead + num_rel_trail + 1;

        if (ch_data->bs_num_env > 5) {
            av_log(ac->avctx, AV_LOG_ERROR,
                   "Invalid bitstream, too many SBR envelopes in VARVAR type SBR frame: %d\n",
                   ch_data->bs_num_env);
            return -1;
        }

        ch_data->t_env[ch_data->bs_num_env] = abs_bord_trail;

        for (i = 0; i < num_rel_lead; i++)
            ch_data->t_env[i + 1] = ch_data->t_env[i] + 2 * get_bits(gb, 2) + 2;
        for (i = 0; i < num_rel_trail; i++)
            ch_data->t_env[ch_data->bs_num_env - 1 - i] =
                ch_data->t_env[ch_data->bs_num_env - i] - 2 * get_bits(gb, 2) - 2;

        bs_pointer = get_bits(gb, ceil_log2[ch_data->bs_num_env]);

        get_bits1_vector(gb, ch_data->bs_freq_res + 1, ch_data->bs_num_env);
        break;
    }

    if (bs_pointer > ch_data->bs_num_env + 1) {
        av_log(ac->avctx, AV_LOG_ERROR,
               "Invalid bitstream, bs_pointer points to a middle noise border outside the time borders table: %d\n",
               bs_pointer);
        return -1;
    }

    for (i = 1; i <= ch_data->bs_num_env; i++) {
        if (ch_data->t_env[i-1] > ch_data->t_env[i]) {
            av_log(ac->avctx, AV_LOG_ERROR, "Non monotone time borders\n");
            return -1;
        }
    }

    ch_data->bs_num_noise = (ch_data->bs_num_env > 1) + 1;

    ch_data->t_q[0]                     = ch_data->t_env[0];
    ch_data->t_q[ch_data->bs_num_noise] = ch_data->t_env[ch_data->bs_num_env];
    if (ch_data->bs_num_noise > 1) {
        unsigned int idx;
        if (ch_data->bs_frame_class == FIXFIX) {
            idx = ch_data->bs_num_env >> 1;
        } else if (ch_data->bs_frame_class & 1) { // FIXVAR or VARVAR
            idx = ch_data->bs_num_env - FFMAX((int)bs_pointer - 1, 1);
        } else { // VARFIX
            if (!bs_pointer)
                idx = 1;
            else if (bs_pointer == 1)
                idx = ch_data->bs_num_env - 1;
            else // bs_pointer > 1
                idx = bs_pointer - 1;
        }
        ch_data->t_q[1] = ch_data->t_env[idx];
    }

    ch_data->e_a[0] = -(ch_data->e_a[1] != bs_num_env_old); // l_APrev
    ch_data->e_a[1] = -1;
    if ((ch_data->bs_frame_class & 1) && bs_pointer) { // FIXVAR or VARVAR and bs_pointer != 0
        ch_data->e_a[1] = ch_data->bs_num_env + 1 - bs_pointer;
    } else if ((ch_data->bs_frame_class == 2) && (bs_pointer > 1)) // VARFIX and bs_pointer > 1
        ch_data->e_a[1] = bs_pointer - 1;

    return 0;
}

static void copy_sbr_grid(SBRData *dst, const SBRData *src) {
    //These variables are saved from the previous frame rather than copied
    dst->bs_freq_res[0]    = dst->bs_freq_res[dst->bs_num_env];
    dst->t_env_num_env_old = dst->t_env[dst->bs_num_env];
    dst->e_a[0]            = -(dst->e_a[1] != dst->bs_num_env);

    //These variables are read from the bitstream and therefore copied
    memcpy(dst->bs_freq_res+1, src->bs_freq_res+1, sizeof(dst->bs_freq_res)-sizeof(*dst->bs_freq_res));
    memcpy(dst->t_env,         src->t_env,         sizeof(dst->t_env));
    memcpy(dst->t_q,           src->t_q,           sizeof(dst->t_q));
    dst->bs_num_env        = src->bs_num_env;
    dst->bs_amp_res        = src->bs_amp_res;
    dst->bs_num_noise      = src->bs_num_noise;
    dst->bs_frame_class    = src->bs_frame_class;
    dst->e_a[1]            = src->e_a[1];
}

/// Read how the envelope and noise floor data is delta coded
static void read_sbr_dtdf(SpectralBandReplication *sbr, GetBitContext *gb,
                          SBRData *ch_data)
{
    get_bits1_vector(gb, ch_data->bs_df_env,   ch_data->bs_num_env);
    get_bits1_vector(gb, ch_data->bs_df_noise, ch_data->bs_num_noise);
}

/// Read inverse filtering data
static void read_sbr_invf(SpectralBandReplication *sbr, GetBitContext *gb,
                          SBRData *ch_data)
{
    int i;

    memcpy(ch_data->bs_invf_mode[1], ch_data->bs_invf_mode[0], 5 * sizeof(uint8_t));
    for (i = 0; i < sbr->n_q; i++)
        ch_data->bs_invf_mode[0][i] = get_bits(gb, 2);
}

static void read_sbr_envelope(SpectralBandReplication *sbr, GetBitContext *gb,
                              SBRData *ch_data, int ch)
{
    int bits;
    int i, j, k;
    VLC_TYPE (*t_huff)[2], (*f_huff)[2];
    int t_lav, f_lav;
    const int delta = (ch == 1 && sbr->bs_coupling == 1) + 1;
    const int odd = sbr->n[1] & 1;

    if (sbr->bs_coupling && ch) {
        if (ch_data->bs_amp_res) {
            bits   = 5;
            t_huff = vlc_sbr[T_HUFFMAN_ENV_BAL_3_0DB].table;
            t_lav  = vlc_sbr_lav[T_HUFFMAN_ENV_BAL_3_0DB];
            f_huff = vlc_sbr[F_HUFFMAN_ENV_BAL_3_0DB].table;
            f_lav  = vlc_sbr_lav[F_HUFFMAN_ENV_BAL_3_0DB];
        } else {
            bits   = 6;
            t_huff = vlc_sbr[T_HUFFMAN_ENV_BAL_1_5DB].table;
            t_lav  = vlc_sbr_lav[T_HUFFMAN_ENV_BAL_1_5DB];
            f_huff = vlc_sbr[F_HUFFMAN_ENV_BAL_1_5DB].table;
            f_lav  = vlc_sbr_lav[F_HUFFMAN_ENV_BAL_1_5DB];
        }
    } else {
        if (ch_data->bs_amp_res) {
            bits   = 6;
            t_huff = vlc_sbr[T_HUFFMAN_ENV_3_0DB].table;
            t_lav  = vlc_sbr_lav[T_HUFFMAN_ENV_3_0DB];
            f_huff = vlc_sbr[F_HUFFMAN_ENV_3_0DB].table;
            f_lav  = vlc_sbr_lav[F_HUFFMAN_ENV_3_0DB];
        } else {
            bits   = 7;
            t_huff = vlc_sbr[T_HUFFMAN_ENV_1_5DB].table;
            t_lav  = vlc_sbr_lav[T_HUFFMAN_ENV_1_5DB];
            f_huff = vlc_sbr[F_HUFFMAN_ENV_1_5DB].table;
            f_lav  = vlc_sbr_lav[F_HUFFMAN_ENV_1_5DB];
        }
    }

    for (i = 0; i < ch_data->bs_num_env; i++) {
        if (ch_data->bs_df_env[i]) {
            // bs_freq_res[0] == bs_freq_res[bs_num_env] from prev frame
            if (ch_data->bs_freq_res[i + 1] == ch_data->bs_freq_res[i]) {
                for (j = 0; j < sbr->n[ch_data->bs_freq_res[i + 1]]; j++)
                    ch_data->env_facs[i + 1][j] = ch_data->env_facs[i][j] + delta * (get_vlc2(gb, t_huff, 9, 3) - t_lav);
            } else if (ch_data->bs_freq_res[i + 1]) {
                for (j = 0; j < sbr->n[ch_data->bs_freq_res[i + 1]]; j++) {
                    k = (j + odd) >> 1; // find k such that f_tablelow[k] <= f_tablehigh[j] < f_tablelow[k + 1]
                    ch_data->env_facs[i + 1][j] = ch_data->env_facs[i][k] + delta * (get_vlc2(gb, t_huff, 9, 3) - t_lav);
                }
            } else {
                for (j = 0; j < sbr->n[ch_data->bs_freq_res[i + 1]]; j++) {
                    k = j ? 2*j - odd : 0; // find k such that f_tablehigh[k] == f_tablelow[j]
                    ch_data->env_facs[i + 1][j] = ch_data->env_facs[i][k] + delta * (get_vlc2(gb, t_huff, 9, 3) - t_lav);
                }
            }
        } else {
            ch_data->env_facs[i + 1][0] = delta * get_bits(gb, bits); // bs_env_start_value_balance
            for (j = 1; j < sbr->n[ch_data->bs_freq_res[i + 1]]; j++)
                ch_data->env_facs[i + 1][j] = ch_data->env_facs[i + 1][j - 1] + delta * (get_vlc2(gb, f_huff, 9, 3) - f_lav);
        }
    }

    //assign 0th elements of env_facs from last elements
    memcpy(ch_data->env_facs[0], ch_data->env_facs[ch_data->bs_num_env],
           sizeof(ch_data->env_facs[0]));
}

static void read_sbr_noise(SpectralBandReplication *sbr, GetBitContext *gb,
                           SBRData *ch_data, int ch)
{
    int i, j;
    VLC_TYPE (*t_huff)[2], (*f_huff)[2];
    int t_lav, f_lav;
    int delta = (ch == 1 && sbr->bs_coupling == 1) + 1;

    if (sbr->bs_coupling && ch) {
        t_huff = vlc_sbr[T_HUFFMAN_NOISE_BAL_3_0DB].table;
        t_lav  = vlc_sbr_lav[T_HUFFMAN_NOISE_BAL_3_0DB];
        f_huff = vlc_sbr[F_HUFFMAN_ENV_BAL_3_0DB].table;
        f_lav  = vlc_sbr_lav[F_HUFFMAN_ENV_BAL_3_0DB];
    } else {
        t_huff = vlc_sbr[T_HUFFMAN_NOISE_3_0DB].table;
        t_lav  = vlc_sbr_lav[T_HUFFMAN_NOISE_3_0DB];
        f_huff = vlc_sbr[F_HUFFMAN_ENV_3_0DB].table;
        f_lav  = vlc_sbr_lav[F_HUFFMAN_ENV_3_0DB];
    }

    for (i = 0; i < ch_data->bs_num_noise; i++) {
        if (ch_data->bs_df_noise[i]) {
            for (j = 0; j < sbr->n_q; j++)
                ch_data->noise_facs[i + 1][j] = ch_data->noise_facs[i][j] + delta * (get_vlc2(gb, t_huff, 9, 2) - t_lav);
        } else {
            ch_data->noise_facs[i + 1][0] = delta * get_bits(gb, 5); // bs_noise_start_value_balance or bs_noise_start_value_level
            for (j = 1; j < sbr->n_q; j++)
                ch_data->noise_facs[i + 1][j] = ch_data->noise_facs[i + 1][j - 1] + delta * (get_vlc2(gb, f_huff, 9, 3) - f_lav);
        }
    }

    //assign 0th elements of noise_facs from last elements
    memcpy(ch_data->noise_facs[0], ch_data->noise_facs[ch_data->bs_num_noise],
           sizeof(ch_data->noise_facs[0]));
}

static void read_sbr_extension(AACContext *ac, SpectralBandReplication *sbr,
                               GetBitContext *gb,
                               int bs_extension_id, int *num_bits_left)
{
    switch (bs_extension_id) {
    case EXTENSION_ID_PS:
        if (!ac->oc[1].m4ac.ps) {
            av_log(ac->avctx, AV_LOG_ERROR, "Parametric Stereo signaled to be not-present but was found in the bitstream.\n");
            skip_bits_long(gb, *num_bits_left); // bs_fill_bits
            *num_bits_left = 0;
        } else {
#if 1
            *num_bits_left -= ff_ps_read_data(ac->avctx, gb, &sbr->ps, *num_bits_left);
            ac->avctx->profile = FF_PROFILE_AAC_HE_V2;
#else
            avpriv_report_missing_feature(ac->avctx, "Parametric Stereo");
            skip_bits_long(gb, *num_bits_left); // bs_fill_bits
            *num_bits_left = 0;
#endif
        }
        break;
    default:
        // some files contain 0-padding
        if (bs_extension_id || *num_bits_left > 16 || show_bits(gb, *num_bits_left))
            avpriv_request_sample(ac->avctx, "Reserved SBR extensions");
        skip_bits_long(gb, *num_bits_left); // bs_fill_bits
        *num_bits_left = 0;
        break;
    }
}

static int read_sbr_single_channel_element(AACContext *ac,
                                            SpectralBandReplication *sbr,
                                            GetBitContext *gb)
{
    if (get_bits1(gb)) // bs_data_extra
        skip_bits(gb, 4); // bs_reserved

    if (read_sbr_grid(ac, sbr, gb, &sbr->data[0]))
        return -1;
    read_sbr_dtdf(sbr, gb, &sbr->data[0]);
    read_sbr_invf(sbr, gb, &sbr->data[0]);
    read_sbr_envelope(sbr, gb, &sbr->data[0], 0);
    read_sbr_noise(sbr, gb, &sbr->data[0], 0);

    if ((sbr->data[0].bs_add_harmonic_flag = get_bits1(gb)))
        get_bits1_vector(gb, sbr->data[0].bs_add_harmonic, sbr->n[1]);

    return 0;
}

static int read_sbr_channel_pair_element(AACContext *ac,
                                          SpectralBandReplication *sbr,
                                          GetBitContext *gb)
{
    if (get_bits1(gb))    // bs_data_extra
        skip_bits(gb, 8); // bs_reserved

    if ((sbr->bs_coupling = get_bits1(gb))) {
        if (read_sbr_grid(ac, sbr, gb, &sbr->data[0]))
            return -1;
        copy_sbr_grid(&sbr->data[1], &sbr->data[0]);
        read_sbr_dtdf(sbr, gb, &sbr->data[0]);
        read_sbr_dtdf(sbr, gb, &sbr->data[1]);
        read_sbr_invf(sbr, gb, &sbr->data[0]);
        memcpy(sbr->data[1].bs_invf_mode[1], sbr->data[1].bs_invf_mode[0], sizeof(sbr->data[1].bs_invf_mode[0]));
        memcpy(sbr->data[1].bs_invf_mode[0], sbr->data[0].bs_invf_mode[0], sizeof(sbr->data[1].bs_invf_mode[0]));
        read_sbr_envelope(sbr, gb, &sbr->data[0], 0);
        read_sbr_noise(sbr, gb, &sbr->data[0], 0);
        read_sbr_envelope(sbr, gb, &sbr->data[1], 1);
        read_sbr_noise(sbr, gb, &sbr->data[1], 1);
    } else {
        if (read_sbr_grid(ac, sbr, gb, &sbr->data[0]) ||
            read_sbr_grid(ac, sbr, gb, &sbr->data[1]))
            return -1;
        read_sbr_dtdf(sbr, gb, &sbr->data[0]);
        read_sbr_dtdf(sbr, gb, &sbr->data[1]);
        read_sbr_invf(sbr, gb, &sbr->data[0]);
        read_sbr_invf(sbr, gb, &sbr->data[1]);
        read_sbr_envelope(sbr, gb, &sbr->data[0], 0);
        read_sbr_envelope(sbr, gb, &sbr->data[1], 1);
        read_sbr_noise(sbr, gb, &sbr->data[0], 0);
        read_sbr_noise(sbr, gb, &sbr->data[1], 1);
    }

    if ((sbr->data[0].bs_add_harmonic_flag = get_bits1(gb)))
        get_bits1_vector(gb, sbr->data[0].bs_add_harmonic, sbr->n[1]);
    if ((sbr->data[1].bs_add_harmonic_flag = get_bits1(gb)))
        get_bits1_vector(gb, sbr->data[1].bs_add_harmonic, sbr->n[1]);

    return 0;
}

static unsigned int read_sbr_data(AACContext *ac, SpectralBandReplication *sbr,
                                  GetBitContext *gb, int id_aac)
{
    unsigned int cnt = get_bits_count(gb);

    if (id_aac == TYPE_SCE || id_aac == TYPE_CCE) {
        if (read_sbr_single_channel_element(ac, sbr, gb)) {
            sbr_turnoff(sbr);
            return get_bits_count(gb) - cnt;
        }
    } else if (id_aac == TYPE_CPE) {
        if (read_sbr_channel_pair_element(ac, sbr, gb)) {
            sbr_turnoff(sbr);
            return get_bits_count(gb) - cnt;
        }
    } else {
        av_log(ac->avctx, AV_LOG_ERROR,
            "Invalid bitstream - cannot apply SBR to element type %d\n", id_aac);
        sbr_turnoff(sbr);
        return get_bits_count(gb) - cnt;
    }
    if (get_bits1(gb)) { // bs_extended_data
        int num_bits_left = get_bits(gb, 4); // bs_extension_size
        if (num_bits_left == 15)
            num_bits_left += get_bits(gb, 8); // bs_esc_count

        num_bits_left <<= 3;
        while (num_bits_left > 7) {
            num_bits_left -= 2;
            read_sbr_extension(ac, sbr, gb, get_bits(gb, 2), &num_bits_left); // bs_extension_id
        }
        if (num_bits_left < 0) {
            av_log(ac->avctx, AV_LOG_ERROR, "SBR Extension over read.\n");
        }
        if (num_bits_left > 0)
            skip_bits(gb, num_bits_left);
    }

    return get_bits_count(gb) - cnt;
}

static void sbr_reset(AACContext *ac, SpectralBandReplication *sbr)
{
    int err;
    err = sbr_make_f_master(ac, sbr, &sbr->spectrum_params);
    if (err >= 0)
        err = sbr_make_f_derived(ac, sbr);
    if (err < 0) {
        av_log(ac->avctx, AV_LOG_ERROR,
               "SBR reset failed. Switching SBR to pure upsampling mode.\n");
        sbr_turnoff(sbr);
    }
}

/**
 * Decode Spectral Band Replication extension data; reference: table 4.55.
 *
 * @param   crc flag indicating the presence of CRC checksum
 * @param   cnt length of TYPE_FIL syntactic element in bytes
 *
 * @return  Returns number of bytes consumed from the TYPE_FIL element.
 */
int ff_decode_sbr_extension(AACContext *ac, SpectralBandReplication *sbr,
                            GetBitContext *gb_host, int crc, int cnt, int id_aac)
{
    unsigned int num_sbr_bits = 0, num_align_bits;
    unsigned bytes_read;
    GetBitContext gbc = *gb_host, *gb = &gbc;
    skip_bits_long(gb_host, cnt*8 - 4);

    sbr->reset = 0;

    if (!sbr->sample_rate)
        sbr->sample_rate = 2 * ac->oc[1].m4ac.sample_rate; //TODO use the nominal sample rate for arbitrary sample rate support
    if (!ac->oc[1].m4ac.ext_sample_rate)
        ac->oc[1].m4ac.ext_sample_rate = 2 * ac->oc[1].m4ac.sample_rate;

    if (crc) {
        skip_bits(gb, 10); // bs_sbr_crc_bits; TODO - implement CRC check
        num_sbr_bits += 10;
    }

    //Save some state from the previous frame.
    sbr->kx[0] = sbr->kx[1];
    sbr->m[0] = sbr->m[1];
    sbr->kx_and_m_pushed = 1;

    num_sbr_bits++;
    if (get_bits1(gb)) // bs_header_flag
        num_sbr_bits += read_sbr_header(sbr, gb);

    if (sbr->reset)
        sbr_reset(ac, sbr);

    if (sbr->start)
        num_sbr_bits  += read_sbr_data(ac, sbr, gb, id_aac);

    num_align_bits = ((cnt << 3) - 4 - num_sbr_bits) & 7;
    bytes_read = ((num_sbr_bits + num_align_bits + 4) >> 3);

    if (bytes_read > cnt) {
        av_log(ac->avctx, AV_LOG_ERROR,
               "Expected to read %d SBR bytes actually read %d.\n", cnt, bytes_read);
    }
    return cnt;
}

/// Dequantization and stereo decoding (14496-3 sp04 p203)
static void sbr_dequant(SpectralBandReplication *sbr, int id_aac)
{
    int k, e;
    int ch;

    if (id_aac == TYPE_CPE && sbr->bs_coupling) {
        float alpha      = sbr->data[0].bs_amp_res ?  1.0f :  0.5f;
        float pan_offset = sbr->data[0].bs_amp_res ? 12.0f : 24.0f;
        for (e = 1; e <= sbr->data[0].bs_num_env; e++) {
            for (k = 0; k < sbr->n[sbr->data[0].bs_freq_res[e]]; k++) {
                float temp1 = exp2f(sbr->data[0].env_facs[e][k] * alpha + 7.0f);
                float temp2 = exp2f((pan_offset - sbr->data[1].env_facs[e][k]) * alpha);
                float fac;
                if (temp1 > 1E20) {
                    av_log(NULL, AV_LOG_ERROR, "envelope scalefactor overflow in dequant\n");
                    temp1 = 1;
                }
                fac   = temp1 / (1.0f + temp2);
                sbr->data[0].env_facs[e][k] = fac;
                sbr->data[1].env_facs[e][k] = fac * temp2;
            }
        }
        for (e = 1; e <= sbr->data[0].bs_num_noise; e++) {
            for (k = 0; k < sbr->n_q; k++) {
                float temp1 = exp2f(NOISE_FLOOR_OFFSET - sbr->data[0].noise_facs[e][k] + 1);
                float temp2 = exp2f(12 - sbr->data[1].noise_facs[e][k]);
                float fac;
                if (temp1 > 1E20) {
                    av_log(NULL, AV_LOG_ERROR, "envelope scalefactor overflow in dequant\n");
                    temp1 = 1;
                }
                fac = temp1 / (1.0f + temp2);
                sbr->data[0].noise_facs[e][k] = fac;
                sbr->data[1].noise_facs[e][k] = fac * temp2;
            }
        }
    } else { // SCE or one non-coupled CPE
        for (ch = 0; ch < (id_aac == TYPE_CPE) + 1; ch++) {
            float alpha = sbr->data[ch].bs_amp_res ? 1.0f : 0.5f;
            for (e = 1; e <= sbr->data[ch].bs_num_env; e++)
                for (k = 0; k < sbr->n[sbr->data[ch].bs_freq_res[e]]; k++){
                    sbr->data[ch].env_facs[e][k] =
                        exp2f(alpha * sbr->data[ch].env_facs[e][k] + 6.0f);
                    if (sbr->data[ch].env_facs[e][k] > 1E20) {
                        av_log(NULL, AV_LOG_ERROR, "envelope scalefactor overflow in dequant\n");
                        sbr->data[ch].env_facs[e][k] = 1;
                    }
                }

            for (e = 1; e <= sbr->data[ch].bs_num_noise; e++)
                for (k = 0; k < sbr->n_q; k++)
                    sbr->data[ch].noise_facs[e][k] =
                        exp2f(NOISE_FLOOR_OFFSET - sbr->data[ch].noise_facs[e][k]);
        }
    }
}

/**
 * Analysis QMF Bank (14496-3 sp04 p206)
 *
 * @param   x       pointer to the beginning of the first sample window
 * @param   W       array of complex-valued samples split into subbands
 */
#ifndef sbr_qmf_analysis
static void sbr_qmf_analysis(AVFloatDSPContext *dsp, FFTContext *mdct,
                             SBRDSPContext *sbrdsp, const float *in, float *x,
                             float z[320], float W[2][32][32][2], int buf_idx)
{
    int i;
    memcpy(x    , x+1024, (320-32)*sizeof(x[0]));
    memcpy(x+288, in,         1024*sizeof(x[0]));
    for (i = 0; i < 32; i++) { // numTimeSlots*RATE = 16*2 as 960 sample frames
                               // are not supported
        dsp->vector_fmul_reverse(z, sbr_qmf_window_ds, x, 320);
        sbrdsp->sum64x5(z);
        sbrdsp->qmf_pre_shuffle(z);
        mdct->imdct_half(mdct, z, z+64);
        sbrdsp->qmf_post_shuffle(W[buf_idx][i], z);
        x += 32;
    }
}
#endif

/**
 * Synthesis QMF Bank (14496-3 sp04 p206) and Downsampled Synthesis QMF Bank
 * (14496-3 sp04 p206)
 */
#ifndef sbr_qmf_synthesis
static void sbr_qmf_synthesis(FFTContext *mdct,
                              SBRDSPContext *sbrdsp, AVFloatDSPContext *dsp,
                              float *out, float X[2][38][64],
                              float mdct_buf[2][64],
                              float *v0, int *v_off, const unsigned int div)
{
    int i, n;
    const float *sbr_qmf_window = div ? sbr_qmf_window_ds : sbr_qmf_window_us;
    const int step = 128 >> div;
    float *v;
    for (i = 0; i < 32; i++) {
        if (*v_off < step) {
            int saved_samples = (1280 - 128) >> div;
            memcpy(&v0[SBR_SYNTHESIS_BUF_SIZE - saved_samples], v0, saved_samples * sizeof(float));
            *v_off = SBR_SYNTHESIS_BUF_SIZE - saved_samples - step;
        } else {
            *v_off -= step;
        }
        v = v0 + *v_off;
        if (div) {
            for (n = 0; n < 32; n++) {
                X[0][i][   n] = -X[0][i][n];
                X[0][i][32+n] =  X[1][i][31-n];
            }
            mdct->imdct_half(mdct, mdct_buf[0], X[0][i]);
            sbrdsp->qmf_deint_neg(v, mdct_buf[0]);
        } else {
            sbrdsp->neg_odd_64(X[1][i]);
            mdct->imdct_half(mdct, mdct_buf[0], X[0][i]);
            mdct->imdct_half(mdct, mdct_buf[1], X[1][i]);
            sbrdsp->qmf_deint_bfly(v, mdct_buf[1], mdct_buf[0]);
        }
        dsp->vector_fmul    (out, v                , sbr_qmf_window                       , 64 >> div);
        dsp->vector_fmul_add(out, v + ( 192 >> div), sbr_qmf_window + ( 64 >> div), out   , 64 >> div);
        dsp->vector_fmul_add(out, v + ( 256 >> div), sbr_qmf_window + (128 >> div), out   , 64 >> div);
        dsp->vector_fmul_add(out, v + ( 448 >> div), sbr_qmf_window + (192 >> div), out   , 64 >> div);
        dsp->vector_fmul_add(out, v + ( 512 >> div), sbr_qmf_window + (256 >> div), out   , 64 >> div);
        dsp->vector_fmul_add(out, v + ( 704 >> div), sbr_qmf_window + (320 >> div), out   , 64 >> div);
        dsp->vector_fmul_add(out, v + ( 768 >> div), sbr_qmf_window + (384 >> div), out   , 64 >> div);
        dsp->vector_fmul_add(out, v + ( 960 >> div), sbr_qmf_window + (448 >> div), out   , 64 >> div);
        dsp->vector_fmul_add(out, v + (1024 >> div), sbr_qmf_window + (512 >> div), out   , 64 >> div);
        dsp->vector_fmul_add(out, v + (1216 >> div), sbr_qmf_window + (576 >> div), out   , 64 >> div);
        out += 64 >> div;
    }
}
#endif

/** High Frequency Generation (14496-3 sp04 p214+) and Inverse Filtering
 * (14496-3 sp04 p214)
 * Warning: This routine does not seem numerically stable.
 */
static void sbr_hf_inverse_filter(SBRDSPContext *dsp,
                                  float (*alpha0)[2], float (*alpha1)[2],
                                  const float X_low[32][40][2], int k0)
{
    int k;
    for (k = 0; k < k0; k++) {
        LOCAL_ALIGNED_16(float, phi, [3], [2][2]);
        float dk;

        dsp->autocorrelate(X_low[k], phi);

        dk =  phi[2][1][0] * phi[1][0][0] -
             (phi[1][1][0] * phi[1][1][0] + phi[1][1][1] * phi[1][1][1]) / 1.000001f;

        if (!dk) {
            alpha1[k][0] = 0;
            alpha1[k][1] = 0;
        } else {
            float temp_real, temp_im;
            temp_real = phi[0][0][0] * phi[1][1][0] -
                        phi[0][0][1] * phi[1][1][1] -
                        phi[0][1][0] * phi[1][0][0];
            temp_im   = phi[0][0][0] * phi[1][1][1] +
                        phi[0][0][1] * phi[1][1][0] -
                        phi[0][1][1] * phi[1][0][0];

            alpha1[k][0] = temp_real / dk;
            alpha1[k][1] = temp_im   / dk;
        }

        if (!phi[1][0][0]) {
            alpha0[k][0] = 0;
            alpha0[k][1] = 0;
        } else {
            float temp_real, temp_im;
            temp_real = phi[0][0][0] + alpha1[k][0] * phi[1][1][0] +
                                       alpha1[k][1] * phi[1][1][1];
            temp_im   = phi[0][0][1] + alpha1[k][1] * phi[1][1][0] -
                                       alpha1[k][0] * phi[1][1][1];

            alpha0[k][0] = -temp_real / phi[1][0][0];
            alpha0[k][1] = -temp_im   / phi[1][0][0];
        }

        if (alpha1[k][0] * alpha1[k][0] + alpha1[k][1] * alpha1[k][1] >= 16.0f ||
           alpha0[k][0] * alpha0[k][0] + alpha0[k][1] * alpha0[k][1] >= 16.0f) {
            alpha1[k][0] = 0;
            alpha1[k][1] = 0;
            alpha0[k][0] = 0;
            alpha0[k][1] = 0;
        }
    }
}

/// Chirp Factors (14496-3 sp04 p214)
static void sbr_chirp(SpectralBandReplication *sbr, SBRData *ch_data)
{
    int i;
    float new_bw;
    static const float bw_tab[] = { 0.0f, 0.75f, 0.9f, 0.98f };

    for (i = 0; i < sbr->n_q; i++) {
        if (ch_data->bs_invf_mode[0][i] + ch_data->bs_invf_mode[1][i] == 1) {
            new_bw = 0.6f;
        } else
            new_bw = bw_tab[ch_data->bs_invf_mode[0][i]];

        if (new_bw < ch_data->bw_array[i]) {
            new_bw = 0.75f    * new_bw + 0.25f    * ch_data->bw_array[i];
        } else
            new_bw = 0.90625f * new_bw + 0.09375f * ch_data->bw_array[i];
        ch_data->bw_array[i] = new_bw < 0.015625f ? 0.0f : new_bw;
    }
}

/// Generate the subband filtered lowband
static int sbr_lf_gen(AACContext *ac, SpectralBandReplication *sbr,
                      float X_low[32][40][2], const float W[2][32][32][2],
                      int buf_idx)
{
    int i, k;
    const int t_HFGen = 8;
    const int i_f = 32;
    memset(X_low, 0, 32*sizeof(*X_low));
    for (k = 0; k < sbr->kx[1]; k++) {
        for (i = t_HFGen; i < i_f + t_HFGen; i++) {
            X_low[k][i][0] = W[buf_idx][i - t_HFGen][k][0];
            X_low[k][i][1] = W[buf_idx][i - t_HFGen][k][1];
        }
    }
    buf_idx = 1-buf_idx;
    for (k = 0; k < sbr->kx[0]; k++) {
        for (i = 0; i < t_HFGen; i++) {
            X_low[k][i][0] = W[buf_idx][i + i_f - t_HFGen][k][0];
            X_low[k][i][1] = W[buf_idx][i + i_f - t_HFGen][k][1];
        }
    }
    return 0;
}

/// High Frequency Generator (14496-3 sp04 p215)
static int sbr_hf_gen(AACContext *ac, SpectralBandReplication *sbr,
                      float X_high[64][40][2], const float X_low[32][40][2],
                      const float (*alpha0)[2], const float (*alpha1)[2],
                      const float bw_array[5], const uint8_t *t_env,
                      int bs_num_env)
{
    int j, x;
    int g = 0;
    int k = sbr->kx[1];
    for (j = 0; j < sbr->num_patches; j++) {
        for (x = 0; x < sbr->patch_num_subbands[j]; x++, k++) {
            const int p = sbr->patch_start_subband[j] + x;
            while (g <= sbr->n_q && k >= sbr->f_tablenoise[g])
                g++;
            g--;

            if (g < 0) {
                av_log(ac->avctx, AV_LOG_ERROR,
                       "ERROR : no subband found for frequency %d\n", k);
                return -1;
            }

            sbr->dsp.hf_gen(X_high[k] + ENVELOPE_ADJUSTMENT_OFFSET,
                            X_low[p]  + ENVELOPE_ADJUSTMENT_OFFSET,
                            alpha0[p], alpha1[p], bw_array[g],
                            2 * t_env[0], 2 * t_env[bs_num_env]);
        }
    }
    if (k < sbr->m[1] + sbr->kx[1])
        memset(X_high + k, 0, (sbr->m[1] + sbr->kx[1] - k) * sizeof(*X_high));

    return 0;
}

/// Generate the subband filtered lowband
static int sbr_x_gen(SpectralBandReplication *sbr, float X[2][38][64],
                     const float Y0[38][64][2], const float Y1[38][64][2],
                     const float X_low[32][40][2], int ch)
{
    int k, i;
    const int i_f = 32;
    const int i_Temp = FFMAX(2*sbr->data[ch].t_env_num_env_old - i_f, 0);
    memset(X, 0, 2*sizeof(*X));
    for (k = 0; k < sbr->kx[0]; k++) {
        for (i = 0; i < i_Temp; i++) {
            X[0][i][k] = X_low[k][i + ENVELOPE_ADJUSTMENT_OFFSET][0];
            X[1][i][k] = X_low[k][i + ENVELOPE_ADJUSTMENT_OFFSET][1];
        }
    }
    for (; k < sbr->kx[0] + sbr->m[0]; k++) {
        for (i = 0; i < i_Temp; i++) {
            X[0][i][k] = Y0[i + i_f][k][0];
            X[1][i][k] = Y0[i + i_f][k][1];
        }
    }

    for (k = 0; k < sbr->kx[1]; k++) {
        for (i = i_Temp; i < 38; i++) {
            X[0][i][k] = X_low[k][i + ENVELOPE_ADJUSTMENT_OFFSET][0];
            X[1][i][k] = X_low[k][i + ENVELOPE_ADJUSTMENT_OFFSET][1];
        }
    }
    for (; k < sbr->kx[1] + sbr->m[1]; k++) {
        for (i = i_Temp; i < i_f; i++) {
            X[0][i][k] = Y1[i][k][0];
            X[1][i][k] = Y1[i][k][1];
        }
    }
    return 0;
}

/** High Frequency Adjustment (14496-3 sp04 p217) and Mapping
 * (14496-3 sp04 p217)
 */
static int sbr_mapping(AACContext *ac, SpectralBandReplication *sbr,
                        SBRData *ch_data, int e_a[2])
{
    int e, i, m;

    memset(ch_data->s_indexmapped[1], 0, 7*sizeof(ch_data->s_indexmapped[1]));
    for (e = 0; e < ch_data->bs_num_env; e++) {
        const unsigned int ilim = sbr->n[ch_data->bs_freq_res[e + 1]];
        uint16_t *table = ch_data->bs_freq_res[e + 1] ? sbr->f_tablehigh : sbr->f_tablelow;
        int k;

        if (sbr->kx[1] != table[0]) {
            av_log(ac->avctx, AV_LOG_ERROR, "kx != f_table{high,low}[0]. "
                   "Derived frequency tables were not regenerated.\n");
            sbr_turnoff(sbr);
            return AVERROR_BUG;
        }
        for (i = 0; i < ilim; i++)
            for (m = table[i]; m < table[i + 1]; m++)
                sbr->e_origmapped[e][m - sbr->kx[1]] = ch_data->env_facs[e+1][i];

        // ch_data->bs_num_noise > 1 => 2 noise floors
        k = (ch_data->bs_num_noise > 1) && (ch_data->t_env[e] >= ch_data->t_q[1]);
        for (i = 0; i < sbr->n_q; i++)
            for (m = sbr->f_tablenoise[i]; m < sbr->f_tablenoise[i + 1]; m++)
                sbr->q_mapped[e][m - sbr->kx[1]] = ch_data->noise_facs[k+1][i];

        for (i = 0; i < sbr->n[1]; i++) {
            if (ch_data->bs_add_harmonic_flag) {
                const unsigned int m_midpoint =
                    (sbr->f_tablehigh[i] + sbr->f_tablehigh[i + 1]) >> 1;

                ch_data->s_indexmapped[e + 1][m_midpoint - sbr->kx[1]] = ch_data->bs_add_harmonic[i] *
                    (e >= e_a[1] || (ch_data->s_indexmapped[0][m_midpoint - sbr->kx[1]] == 1));
            }
        }

        for (i = 0; i < ilim; i++) {
            int additional_sinusoid_present = 0;
            for (m = table[i]; m < table[i + 1]; m++) {
                if (ch_data->s_indexmapped[e + 1][m - sbr->kx[1]]) {
                    additional_sinusoid_present = 1;
                    break;
                }
            }
            memset(&sbr->s_mapped[e][table[i] - sbr->kx[1]], additional_sinusoid_present,
                   (table[i + 1] - table[i]) * sizeof(sbr->s_mapped[e][0]));
        }
    }

    memcpy(ch_data->s_indexmapped[0], ch_data->s_indexmapped[ch_data->bs_num_env], sizeof(ch_data->s_indexmapped[0]));
    return 0;
}

/// Estimation of current envelope (14496-3 sp04 p218)
static void sbr_env_estimate(float (*e_curr)[48], float X_high[64][40][2],
                             SpectralBandReplication *sbr, SBRData *ch_data)
{
    int e, m;
    int kx1 = sbr->kx[1];

    if (sbr->bs_interpol_freq) {
        for (e = 0; e < ch_data->bs_num_env; e++) {
            const float recip_env_size = 0.5f / (ch_data->t_env[e + 1] - ch_data->t_env[e]);
            int ilb = ch_data->t_env[e]     * 2 + ENVELOPE_ADJUSTMENT_OFFSET;
            int iub = ch_data->t_env[e + 1] * 2 + ENVELOPE_ADJUSTMENT_OFFSET;

            for (m = 0; m < sbr->m[1]; m++) {
                float sum = sbr->dsp.sum_square(X_high[m+kx1] + ilb, iub - ilb);
                e_curr[e][m] = sum * recip_env_size;
            }
        }
    } else {
        int k, p;

        for (e = 0; e < ch_data->bs_num_env; e++) {
            const int env_size = 2 * (ch_data->t_env[e + 1] - ch_data->t_env[e]);
            int ilb = ch_data->t_env[e]     * 2 + ENVELOPE_ADJUSTMENT_OFFSET;
            int iub = ch_data->t_env[e + 1] * 2 + ENVELOPE_ADJUSTMENT_OFFSET;
            const uint16_t *table = ch_data->bs_freq_res[e + 1] ? sbr->f_tablehigh : sbr->f_tablelow;

            for (p = 0; p < sbr->n[ch_data->bs_freq_res[e + 1]]; p++) {
                float sum = 0.0f;
                const int den = env_size * (table[p + 1] - table[p]);

                for (k = table[p]; k < table[p + 1]; k++) {
                    sum += sbr->dsp.sum_square(X_high[k] + ilb, iub - ilb);
                }
                sum /= den;
                for (k = table[p]; k < table[p + 1]; k++) {
                    e_curr[e][k - kx1] = sum;
                }
            }
        }
    }
}

/**
 * Calculation of levels of additional HF signal components (14496-3 sp04 p219)
 * and Calculation of gain (14496-3 sp04 p219)
 */
static void sbr_gain_calc(AACContext *ac, SpectralBandReplication *sbr,
                          SBRData *ch_data, const int e_a[2])
{
    int e, k, m;
    // max gain limits : -3dB, 0dB, 3dB, inf dB (limiter off)
    static const float limgain[4] = { 0.70795, 1.0, 1.41254, 10000000000 };

    for (e = 0; e < ch_data->bs_num_env; e++) {
        int delta = !((e == e_a[1]) || (e == e_a[0]));
        for (k = 0; k < sbr->n_lim; k++) {
            float gain_boost, gain_max;
            float sum[2] = { 0.0f, 0.0f };
            for (m = sbr->f_tablelim[k] - sbr->kx[1]; m < sbr->f_tablelim[k + 1] - sbr->kx[1]; m++) {
                const float temp = sbr->e_origmapped[e][m] / (1.0f + sbr->q_mapped[e][m]);
                sbr->q_m[e][m] = sqrtf(temp * sbr->q_mapped[e][m]);
                sbr->s_m[e][m] = sqrtf(temp * ch_data->s_indexmapped[e + 1][m]);
                if (!sbr->s_mapped[e][m]) {
                    sbr->gain[e][m] = sqrtf(sbr->e_origmapped[e][m] /
                                            ((1.0f + sbr->e_curr[e][m]) *
                                             (1.0f + sbr->q_mapped[e][m] * delta)));
                } else {
                    sbr->gain[e][m] = sqrtf(sbr->e_origmapped[e][m] * sbr->q_mapped[e][m] /
                                            ((1.0f + sbr->e_curr[e][m]) *
                                             (1.0f + sbr->q_mapped[e][m])));
                }
            }
            for (m = sbr->f_tablelim[k] - sbr->kx[1]; m < sbr->f_tablelim[k + 1] - sbr->kx[1]; m++) {
                sum[0] += sbr->e_origmapped[e][m];
                sum[1] += sbr->e_curr[e][m];
            }
            gain_max = limgain[sbr->bs_limiter_gains] * sqrtf((FLT_EPSILON + sum[0]) / (FLT_EPSILON + sum[1]));
            gain_max = FFMIN(100000.f, gain_max);
            for (m = sbr->f_tablelim[k] - sbr->kx[1]; m < sbr->f_tablelim[k + 1] - sbr->kx[1]; m++) {
                float q_m_max   = sbr->q_m[e][m] * gain_max / sbr->gain[e][m];
                sbr->q_m[e][m]  = FFMIN(sbr->q_m[e][m], q_m_max);
                sbr->gain[e][m] = FFMIN(sbr->gain[e][m], gain_max);
            }
            sum[0] = sum[1] = 0.0f;
            for (m = sbr->f_tablelim[k] - sbr->kx[1]; m < sbr->f_tablelim[k + 1] - sbr->kx[1]; m++) {
                sum[0] += sbr->e_origmapped[e][m];
                sum[1] += sbr->e_curr[e][m] * sbr->gain[e][m] * sbr->gain[e][m]
                          + sbr->s_m[e][m] * sbr->s_m[e][m]
                          + (delta && !sbr->s_m[e][m]) * sbr->q_m[e][m] * sbr->q_m[e][m];
            }
            gain_boost = sqrtf((FLT_EPSILON + sum[0]) / (FLT_EPSILON + sum[1]));
            gain_boost = FFMIN(1.584893192f, gain_boost);
            for (m = sbr->f_tablelim[k] - sbr->kx[1]; m < sbr->f_tablelim[k + 1] - sbr->kx[1]; m++) {
                sbr->gain[e][m] *= gain_boost;
                sbr->q_m[e][m]  *= gain_boost;
                sbr->s_m[e][m]  *= gain_boost;
            }
        }
    }
}

/// Assembling HF Signals (14496-3 sp04 p220)
static void sbr_hf_assemble(float Y1[38][64][2],
                            const float X_high[64][40][2],
                            SpectralBandReplication *sbr, SBRData *ch_data,
                            const int e_a[2])
{
    int e, i, j, m;
    const int h_SL = 4 * !sbr->bs_smoothing_mode;
    const int kx = sbr->kx[1];
    const int m_max = sbr->m[1];
    static const float h_smooth[5] = {
        0.33333333333333,
        0.30150283239582,
        0.21816949906249,
        0.11516383427084,
        0.03183050093751,
    };
    float (*g_temp)[48] = ch_data->g_temp, (*q_temp)[48] = ch_data->q_temp;
    int indexnoise = ch_data->f_indexnoise;
    int indexsine  = ch_data->f_indexsine;

    if (sbr->reset) {
        for (i = 0; i < h_SL; i++) {
            memcpy(g_temp[i + 2*ch_data->t_env[0]], sbr->gain[0], m_max * sizeof(sbr->gain[0][0]));
            memcpy(q_temp[i + 2*ch_data->t_env[0]], sbr->q_m[0],  m_max * sizeof(sbr->q_m[0][0]));
        }
    } else if (h_SL) {
        memcpy(g_temp[2*ch_data->t_env[0]], g_temp[2*ch_data->t_env_num_env_old], 4*sizeof(g_temp[0]));
        memcpy(q_temp[2*ch_data->t_env[0]], q_temp[2*ch_data->t_env_num_env_old], 4*sizeof(q_temp[0]));
    }

    for (e = 0; e < ch_data->bs_num_env; e++) {
        for (i = 2 * ch_data->t_env[e]; i < 2 * ch_data->t_env[e + 1]; i++) {
            memcpy(g_temp[h_SL + i], sbr->gain[e], m_max * sizeof(sbr->gain[0][0]));
            memcpy(q_temp[h_SL + i], sbr->q_m[e],  m_max * sizeof(sbr->q_m[0][0]));
        }
    }

    for (e = 0; e < ch_data->bs_num_env; e++) {
        for (i = 2 * ch_data->t_env[e]; i < 2 * ch_data->t_env[e + 1]; i++) {
            LOCAL_ALIGNED_16(float, g_filt_tab, [48]);
            LOCAL_ALIGNED_16(float, q_filt_tab, [48]);
            float *g_filt, *q_filt;

            if (h_SL && e != e_a[0] && e != e_a[1]) {
                g_filt = g_filt_tab;
                q_filt = q_filt_tab;
                for (m = 0; m < m_max; m++) {
                    const int idx1 = i + h_SL;
                    g_filt[m] = 0.0f;
                    q_filt[m] = 0.0f;
                    for (j = 0; j <= h_SL; j++) {
                        g_filt[m] += g_temp[idx1 - j][m] * h_smooth[j];
                        q_filt[m] += q_temp[idx1 - j][m] * h_smooth[j];
                    }
                }
            } else {
                g_filt = g_temp[i + h_SL];
                q_filt = q_temp[i];
            }

            sbr->dsp.hf_g_filt(Y1[i] + kx, X_high + kx, g_filt, m_max,
                               i + ENVELOPE_ADJUSTMENT_OFFSET);

            if (e != e_a[0] && e != e_a[1]) {
                sbr->dsp.hf_apply_noise[indexsine](Y1[i] + kx, sbr->s_m[e],
                                                   q_filt, indexnoise,
                                                   kx, m_max);
            } else {
                int idx = indexsine&1;
                int A = (1-((indexsine+(kx & 1))&2));
                int B = (A^(-idx)) + idx;
                float *out = &Y1[i][kx][idx];
                float *in  = sbr->s_m[e];
                for (m = 0; m+1 < m_max; m+=2) {
                    out[2*m  ] += in[m  ] * A;
                    out[2*m+2] += in[m+1] * B;
                }
                if(m_max&1)
                    out[2*m  ] += in[m  ] * A;
            }
            indexnoise = (indexnoise + m_max) & 0x1ff;
            indexsine = (indexsine + 1) & 3;
        }
    }
    ch_data->f_indexnoise = indexnoise;
    ch_data->f_indexsine  = indexsine;
}

void ff_sbr_apply(AACContext *ac, SpectralBandReplication *sbr, int id_aac,
                  float* L, float* R)
{
    int downsampled = ac->oc[1].m4ac.ext_sample_rate < sbr->sample_rate;
    int ch;
    int nch = (id_aac == TYPE_CPE) ? 2 : 1;
    int err;

    if (!sbr->kx_and_m_pushed) {
        sbr->kx[0] = sbr->kx[1];
        sbr->m[0] = sbr->m[1];
    } else {
        sbr->kx_and_m_pushed = 0;
    }

    if (sbr->start) {
        sbr_dequant(sbr, id_aac);
    }
    for (ch = 0; ch < nch; ch++) {
        /* decode channel */
        sbr_qmf_analysis(&ac->fdsp, &sbr->mdct_ana, &sbr->dsp, ch ? R : L, sbr->data[ch].analysis_filterbank_samples,
                         (float*)sbr->qmf_filter_scratch,
                         sbr->data[ch].W, sbr->data[ch].Ypos);
        sbr->c.sbr_lf_gen(ac, sbr, sbr->X_low,
                          (const float (*)[32][32][2]) sbr->data[ch].W,
                          sbr->data[ch].Ypos);
        sbr->data[ch].Ypos ^= 1;
        if (sbr->start) {
            sbr->c.sbr_hf_inverse_filter(&sbr->dsp, sbr->alpha0, sbr->alpha1,
                                         (const float (*)[40][2]) sbr->X_low, sbr->k[0]);
            sbr_chirp(sbr, &sbr->data[ch]);
            sbr_hf_gen(ac, sbr, sbr->X_high,
                       (const float (*)[40][2]) sbr->X_low,
                       (const float (*)[2]) sbr->alpha0,
                       (const float (*)[2]) sbr->alpha1,
                       sbr->data[ch].bw_array, sbr->data[ch].t_env,
                       sbr->data[ch].bs_num_env);

            // hf_adj
            err = sbr_mapping(ac, sbr, &sbr->data[ch], sbr->data[ch].e_a);
            if (!err) {
                sbr_env_estimate(sbr->e_curr, sbr->X_high, sbr, &sbr->data[ch]);
                sbr_gain_calc(ac, sbr, &sbr->data[ch], sbr->data[ch].e_a);
                sbr->c.sbr_hf_assemble(sbr->data[ch].Y[sbr->data[ch].Ypos],
                                (const float (*)[40][2]) sbr->X_high,
                                sbr, &sbr->data[ch],
                                sbr->data[ch].e_a);
            }
        }

        /* synthesis */
        sbr->c.sbr_x_gen(sbr, sbr->X[ch],
                  (const float (*)[64][2]) sbr->data[ch].Y[1-sbr->data[ch].Ypos],
                  (const float (*)[64][2]) sbr->data[ch].Y[  sbr->data[ch].Ypos],
                  (const float (*)[40][2]) sbr->X_low, ch);
    }

    if (ac->oc[1].m4ac.ps == 1) {
        if (sbr->ps.start) {
            ff_ps_apply(ac->avctx, &sbr->ps, sbr->X[0], sbr->X[1], sbr->kx[1] + sbr->m[1]);
        } else {
            memcpy(sbr->X[1], sbr->X[0], sizeof(sbr->X[0]));
        }
        nch = 2;
    }

    sbr_qmf_synthesis(&sbr->mdct, &sbr->dsp, &ac->fdsp,
                      L, sbr->X[0], sbr->qmf_filter_scratch,
                      sbr->data[0].synthesis_filterbank_samples,
                      &sbr->data[0].synthesis_filterbank_samples_offset,
                      downsampled);
    if (nch == 2)
        sbr_qmf_synthesis(&sbr->mdct, &sbr->dsp, &ac->fdsp,
                          R, sbr->X[1], sbr->qmf_filter_scratch,
                          sbr->data[1].synthesis_filterbank_samples,
                          &sbr->data[1].synthesis_filterbank_samples_offset,
                          downsampled);
}

static void aacsbr_func_ptr_init(AACSBRContext *c)
{
    c->sbr_lf_gen            = sbr_lf_gen;
    c->sbr_hf_assemble       = sbr_hf_assemble;
    c->sbr_x_gen             = sbr_x_gen;
    c->sbr_hf_inverse_filter = sbr_hf_inverse_filter;

    if(ARCH_MIPS)
        ff_aacsbr_func_ptr_init_mips(c);
}
