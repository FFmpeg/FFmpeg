/*
 * Spectral Band Replication definitions and structures
 * Copyright (c) 2008-2009 Robert Swain ( rob opendot cl )
 * Copyright (c) 2010      Alex Converse <alex.converse@gmail.com>
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
 * Spectral Band Replication definitions and structures
 * @author Robert Swain ( rob opendot cl )
 */

#ifndef AVCODEC_SBR_H
#define AVCODEC_SBR_H

#include <stdint.h>
#include "fft.h"
#include "aacps.h"

/**
 * Spectral Band Replication header - spectrum parameters that invoke a reset if they differ from the previous header.
 */
typedef struct {
    uint8_t bs_start_freq;
    uint8_t bs_stop_freq;
    uint8_t bs_xover_band;

    /**
     * @name Variables associated with bs_header_extra_1
     * @{
     */
    uint8_t bs_freq_scale;
    uint8_t bs_alter_scale;
    uint8_t bs_noise_bands;
    /** @} */
} SpectrumParameters;

#define SBR_SYNTHESIS_BUF_SIZE ((1280-128)*2)

/**
 * Spectral Band Replication per channel data
 */
typedef struct {
    /**
     * @name Main bitstream data variables
     * @{
     */
    unsigned           bs_frame_class;
    unsigned           bs_add_harmonic_flag;
    unsigned           bs_num_env;
    uint8_t            bs_freq_res[7];
    unsigned           bs_num_noise;
    uint8_t            bs_df_env[5];
    uint8_t            bs_df_noise[2];
    uint8_t            bs_invf_mode[2][5];
    uint8_t            bs_add_harmonic[48];
    unsigned           bs_amp_res;
    /** @} */

    /**
     * @name State variables
     * @{
     */
    DECLARE_ALIGNED(16, float, synthesis_filterbank_samples)[SBR_SYNTHESIS_BUF_SIZE];
    DECLARE_ALIGNED(16, float, analysis_filterbank_samples) [1312];
    int                synthesis_filterbank_samples_offset;
    ///l_APrev and l_A
    int                e_a[2];
    ///Chirp factors
    float              bw_array[5];
    ///QMF values of the original signal
    float              W[2][32][32][2];
    ///QMF output of the HF adjustor
    float              Y[2][38][64][2];
    float              g_temp[42][48];
    float              q_temp[42][48];
    uint8_t            s_indexmapped[8][48];
    ///Envelope scalefactors
    float              env_facs[6][48];
    ///Noise scalefactors
    float              noise_facs[3][5];
    ///Envelope time borders
    uint8_t            t_env[8];
    ///Envelope time border of the last envelope of the previous frame
    uint8_t            t_env_num_env_old;
    ///Noise time borders
    uint8_t            t_q[3];
    unsigned           f_indexnoise;
    unsigned           f_indexsine;
    /** @} */
} SBRData;

/**
 * Spectral Band Replication
 */
typedef struct {
    int                sample_rate;
    int                start;
    int                reset;
    SpectrumParameters spectrum_params;
    int                bs_amp_res_header;
    /**
     * @name Variables associated with bs_header_extra_2
     * @{
     */
    unsigned           bs_limiter_bands;
    unsigned           bs_limiter_gains;
    unsigned           bs_interpol_freq;
    unsigned           bs_smoothing_mode;
    /** @} */
    unsigned           bs_coupling;
    unsigned           k[5]; ///< k0, k1, k2
    ///kx', and kx respectively, kx is the first QMF subband where SBR is used.
    ///kx' is its value from the previous frame
    unsigned           kx[2];
    ///M' and M respectively, M is the number of QMF subbands that use SBR.
    unsigned           m[2];
    ///The number of frequency bands in f_master
    unsigned           n_master;
    SBRData            data[2];
    PSContext          ps;
    ///N_Low and N_High respectively, the number of frequency bands for low and high resolution
    unsigned           n[2];
    ///Number of noise floor bands
    unsigned           n_q;
    ///Number of limiter bands
    unsigned           n_lim;
    ///The master QMF frequency grouping
    uint16_t           f_master[49];
    ///Frequency borders for low resolution SBR
    uint16_t           f_tablelow[25];
    ///Frequency borders for high resolution SBR
    uint16_t           f_tablehigh[49];
    ///Frequency borders for noise floors
    uint16_t           f_tablenoise[6];
    ///Frequency borders for the limiter
    uint16_t           f_tablelim[29];
    unsigned           num_patches;
    uint8_t            patch_num_subbands[6];
    uint8_t            patch_start_subband[6];
    ///QMF low frequency input to the HF generator
    float              X_low[32][40][2];
    ///QMF output of the HF generator
    float              X_high[64][40][2];
    ///QMF values of the reconstructed signal
    DECLARE_ALIGNED(16, float, X)[2][2][38][64];
    ///Zeroth coefficient used to filter the subband signals
    float              alpha0[64][2];
    ///First coefficient used to filter the subband signals
    float              alpha1[64][2];
    ///Dequantized envelope scalefactors, remapped
    float              e_origmapped[7][48];
    ///Dequantized noise scalefactors, remapped
    float              q_mapped[7][48];
    ///Sinusoidal presence, remapped
    uint8_t            s_mapped[7][48];
    ///Estimated envelope
    float              e_curr[7][48];
    ///Amplitude adjusted noise scalefactors
    float              q_m[7][48];
    ///Sinusoidal levels
    float              s_m[7][48];
    float              gain[7][48];
    DECLARE_ALIGNED(16, float, qmf_filter_scratch)[5][64];
    FFTContext         mdct_ana;
    FFTContext         mdct;
} SpectralBandReplication;

#endif /* AVCODEC_SBR_H */
