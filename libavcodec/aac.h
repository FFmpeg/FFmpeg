/*
 * AAC definitions and structures
 * Copyright (c) 2005-2006 Oded Shimon ( ods15 ods15 dyndns org )
 * Copyright (c) 2006-2007 Maxim Gavrilov ( maxim.gavrilov gmail com )
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
 * @file libavcodec/aac.h
 * AAC definitions and structures
 * @author Oded Shimon  ( ods15 ods15 dyndns org )
 * @author Maxim Gavrilov ( maxim.gavrilov gmail com )
 */

#ifndef AVCODEC_AAC_H
#define AVCODEC_AAC_H

#include "avcodec.h"
#include "dsputil.h"
#include "mpeg4audio.h"

#include <stdint.h>

#define AAC_INIT_VLC_STATIC(num, size) \
    INIT_VLC_STATIC(&vlc_spectral[num], 6, ff_aac_spectral_sizes[num], \
         ff_aac_spectral_bits[num], sizeof( ff_aac_spectral_bits[num][0]), sizeof( ff_aac_spectral_bits[num][0]), \
        ff_aac_spectral_codes[num], sizeof(ff_aac_spectral_codes[num][0]), sizeof(ff_aac_spectral_codes[num][0]), \
        size);

#define MAX_CHANNELS 64
#define MAX_ELEM_ID 16

#define TNS_MAX_ORDER 20

enum AudioObjectType {
    AOT_NULL,
                               // Support?                Name
    AOT_AAC_MAIN,              ///< Y                       Main
    AOT_AAC_LC,                ///< Y                       Low Complexity
    AOT_AAC_SSR,               ///< N (code in SoC repo)    Scalable Sample Rate
    AOT_AAC_LTP,               ///< N (code in SoC repo)    Long Term Prediction
    AOT_SBR,                   ///< N (in progress)         Spectral Band Replication
    AOT_AAC_SCALABLE,          ///< N                       Scalable
    AOT_TWINVQ,                ///< N                       Twin Vector Quantizer
    AOT_CELP,                  ///< N                       Code Excited Linear Prediction
    AOT_HVXC,                  ///< N                       Harmonic Vector eXcitation Coding
    AOT_TTSI             = 12, ///< N                       Text-To-Speech Interface
    AOT_MAINSYNTH,             ///< N                       Main Synthesis
    AOT_WAVESYNTH,             ///< N                       Wavetable Synthesis
    AOT_MIDI,                  ///< N                       General MIDI
    AOT_SAFX,                  ///< N                       Algorithmic Synthesis and Audio Effects
    AOT_ER_AAC_LC,             ///< N                       Error Resilient Low Complexity
    AOT_ER_AAC_LTP       = 19, ///< N                       Error Resilient Long Term Prediction
    AOT_ER_AAC_SCALABLE,       ///< N                       Error Resilient Scalable
    AOT_ER_TWINVQ,             ///< N                       Error Resilient Twin Vector Quantizer
    AOT_ER_BSAC,               ///< N                       Error Resilient Bit-Sliced Arithmetic Coding
    AOT_ER_AAC_LD,             ///< N                       Error Resilient Low Delay
    AOT_ER_CELP,               ///< N                       Error Resilient Code Excited Linear Prediction
    AOT_ER_HVXC,               ///< N                       Error Resilient Harmonic Vector eXcitation Coding
    AOT_ER_HILN,               ///< N                       Error Resilient Harmonic and Individual Lines plus Noise
    AOT_ER_PARAM,              ///< N                       Error Resilient Parametric
    AOT_SSC,                   ///< N                       SinuSoidal Coding
};

enum RawDataBlockType {
    TYPE_SCE,
    TYPE_CPE,
    TYPE_CCE,
    TYPE_LFE,
    TYPE_DSE,
    TYPE_PCE,
    TYPE_FIL,
    TYPE_END,
};

enum ExtensionPayloadID {
    EXT_FILL,
    EXT_FILL_DATA,
    EXT_DATA_ELEMENT,
    EXT_DYNAMIC_RANGE = 0xb,
    EXT_SBR_DATA      = 0xd,
    EXT_SBR_DATA_CRC  = 0xe,
};

enum WindowSequence {
    ONLY_LONG_SEQUENCE,
    LONG_START_SEQUENCE,
    EIGHT_SHORT_SEQUENCE,
    LONG_STOP_SEQUENCE,
};

enum BandType {
    ZERO_BT        = 0,     ///< Scalefactors and spectral data are all zero.
    FIRST_PAIR_BT  = 5,     ///< This and later band types encode two values (rather than four) with one code word.
    ESC_BT         = 11,    ///< Spectral data are coded with an escape sequence.
    NOISE_BT       = 13,    ///< Spectral data are scaled white noise not coded in the bitstream.
    INTENSITY_BT2  = 14,    ///< Scalefactor data are intensity stereo positions.
    INTENSITY_BT   = 15,    ///< Scalefactor data are intensity stereo positions.
};

#define IS_CODEBOOK_UNSIGNED(x) ((x - 1) & 10)

enum ChannelPosition {
    AAC_CHANNEL_FRONT = 1,
    AAC_CHANNEL_SIDE  = 2,
    AAC_CHANNEL_BACK  = 3,
    AAC_CHANNEL_LFE   = 4,
    AAC_CHANNEL_CC    = 5,
};

/**
 * The point during decoding at which channel coupling is applied.
 */
enum CouplingPoint {
    BEFORE_TNS,
    BETWEEN_TNS_AND_IMDCT,
    AFTER_IMDCT = 3,
};

/**
 * Predictor State
 */
typedef struct {
    float cor0;
    float cor1;
    float var0;
    float var1;
    float r0;
    float r1;
} PredictorState;

#define MAX_PREDICTORS 672

/**
 * Individual Channel Stream
 */
typedef struct {
    uint8_t max_sfb;            ///< number of scalefactor bands per group
    enum WindowSequence window_sequence[2];
    uint8_t use_kb_window[2];   ///< If set, use Kaiser-Bessel window, otherwise use a sinus window.
    int num_window_groups;
    uint8_t group_len[8];
    const uint16_t *swb_offset; ///< table of offsets to the lowest spectral coefficient of a scalefactor band, sfb, for a particular window
    int num_swb;                ///< number of scalefactor window bands
    int num_windows;
    int tns_max_bands;
    int predictor_present;
    int predictor_initialized;
    int predictor_reset_group;
    uint8_t prediction_used[41];
} IndividualChannelStream;

/**
 * Temporal Noise Shaping
 */
typedef struct {
    int present;
    int n_filt[8];
    int length[8][4];
    int direction[8][4];
    int order[8][4];
    float coef[8][4][TNS_MAX_ORDER];
} TemporalNoiseShaping;

/**
 * Dynamic Range Control - decoded from the bitstream but not processed further.
 */
typedef struct {
    int pce_instance_tag;                           ///< Indicates with which program the DRC info is associated.
    int dyn_rng_sgn[17];                            ///< DRC sign information; 0 - positive, 1 - negative
    int dyn_rng_ctl[17];                            ///< DRC magnitude information
    int exclude_mask[MAX_CHANNELS];                 ///< Channels to be excluded from DRC processing.
    int band_incr;                                  ///< Number of DRC bands greater than 1 having DRC info.
    int interpolation_scheme;                       ///< Indicates the interpolation scheme used in the SBR QMF domain.
    int band_top[17];                               ///< Indicates the top of the i-th DRC band in units of 4 spectral lines.
    int prog_ref_level;                             /**< A reference level for the long-term program audio level for all
                                                     *   channels combined.
                                                     */
} DynamicRangeControl;

typedef struct {
    int num_pulse;
    int pos[4];
    int amp[4];
} Pulse;

/**
 * coupling parameters
 */
typedef struct {
    enum CouplingPoint coupling_point;  ///< The point during decoding at which coupling is applied.
    int num_coupled;       ///< number of target elements
    enum RawDataBlockType type[8];   ///< Type of channel element to be coupled - SCE or CPE.
    int id_select[8];      ///< element id
    int ch_select[8];      /**< [0] shared list of gains; [1] list of gains for right channel;
                            *   [2] list of gains for left channel; [3] lists of gains for both channels
                            */
    float gain[16][120];
} ChannelCoupling;

/**
 * Single Channel Element - used for both SCE and LFE elements.
 */
typedef struct {
    IndividualChannelStream ics;
    TemporalNoiseShaping tns;
    enum BandType band_type[120];             ///< band types
    int band_type_run_end[120];               ///< band type run end points
    float sf[120];                            ///< scalefactors
    DECLARE_ALIGNED_16(float, coeffs[1024]);  ///< coefficients for IMDCT
    DECLARE_ALIGNED_16(float, saved[512]);    ///< overlap
    DECLARE_ALIGNED_16(float, ret[1024]);     ///< PCM output
    PredictorState predictor_state[MAX_PREDICTORS];
} SingleChannelElement;

/**
 * channel element - generic struct for SCE/CPE/CCE/LFE
 */
typedef struct {
    // CPE specific
    uint8_t ms_mask[120];     ///< Set if mid/side stereo is used for each scalefactor window band
    // shared
    SingleChannelElement ch[2];
    // CCE specific
    ChannelCoupling coup;
} ChannelElement;

/**
 * main AAC context
 */
typedef struct {
    AVCodecContext * avccontext;

    MPEG4AudioConfig m4ac;

    int is_saved;                 ///< Set if elements have stored overlap from previous frame.
    DynamicRangeControl che_drc;

    /**
     * @defgroup elements Channel element related data.
     * @{
     */
    enum ChannelPosition che_pos[4][MAX_ELEM_ID]; /**< channel element channel mapping with the
                                                   *   first index as the first 4 raw data block types
                                                   */
    ChannelElement * che[4][MAX_ELEM_ID];
    /** @} */

    /**
     * @defgroup temporary aligned temporary buffers (We do not want to have these on the stack.)
     * @{
     */
    DECLARE_ALIGNED_16(float, buf_mdct[1024]);
    /** @} */

    /**
     * @defgroup tables   Computed / set up during initialization.
     * @{
     */
    MDCTContext mdct;
    MDCTContext mdct_small;
    DSPContext dsp;
    int random_state;
    /** @} */

    /**
     * @defgroup output   Members used for output interleaving.
     * @{
     */
    float *output_data[MAX_CHANNELS];                 ///< Points to each element's 'ret' buffer (PCM output).
    float add_bias;                                   ///< offset for dsp.float_to_int16
    float sf_scale;                                   ///< Pre-scale for correct IMDCT and dsp.float_to_int16.
    int sf_offset;                                    ///< offset into pow2sf_tab as appropriate for dsp.float_to_int16
    /** @} */

    DECLARE_ALIGNED(16, float, temp[128]);
} AACContext;

#endif /* AVCODEC_AAC_H */
