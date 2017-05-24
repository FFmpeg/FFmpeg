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
 * @file
 * AAC definitions and structures
 * @author Oded Shimon  ( ods15 ods15 dyndns org )
 * @author Maxim Gavrilov ( maxim.gavrilov gmail com )
 */

#ifndef AVCODEC_AAC_H
#define AVCODEC_AAC_H


#include "aac_defines.h"
#include "libavutil/float_dsp.h"
#include "libavutil/fixed_dsp.h"
#include "avcodec.h"
#if !USE_FIXED
#include "mdct15.h"
#endif
#include "fft.h"
#include "mpeg4audio.h"
#include "sbr.h"

#include <stdint.h>

#define MAX_CHANNELS 64
#define MAX_ELEM_ID 16

#define TNS_MAX_ORDER 20
#define MAX_LTP_LONG_SFB 40

#define CLIP_AVOIDANCE_FACTOR 0.95f

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
    RESERVED_BT    = 12,    ///< Band types following are encoded differently from others.
    NOISE_BT       = 13,    ///< Spectral data are scaled white noise not coded in the bitstream.
    INTENSITY_BT2  = 14,    ///< Scalefactor data are intensity stereo positions (out of phase).
    INTENSITY_BT   = 15,    ///< Scalefactor data are intensity stereo positions (in phase).
};

#define IS_CODEBOOK_UNSIGNED(x) (((x) - 1) & 10)

enum ChannelPosition {
    AAC_CHANNEL_OFF   = 0,
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
 * Output configuration status
 */
enum OCStatus {
    OC_NONE,        ///< Output unconfigured
    OC_TRIAL_PCE,   ///< Output configuration under trial specified by an inband PCE
    OC_TRIAL_FRAME, ///< Output configuration under trial specified by a frame header
    OC_GLOBAL_HDR,  ///< Output configuration set in a global header but not yet locked
    OC_LOCKED,      ///< Output configuration locked in place
};

typedef struct OutputConfiguration {
    MPEG4AudioConfig m4ac;
    uint8_t layout_map[MAX_ELEM_ID*4][3];
    int layout_map_tags;
    int channels;
    uint64_t channel_layout;
    enum OCStatus status;
} OutputConfiguration;

/**
 * Predictor State
 */
typedef struct PredictorState {
    AAC_FLOAT cor0;
    AAC_FLOAT cor1;
    AAC_FLOAT var0;
    AAC_FLOAT var1;
    AAC_FLOAT r0;
    AAC_FLOAT r1;
    AAC_FLOAT k1;
    AAC_FLOAT x_est;
} PredictorState;

#define MAX_PREDICTORS 672

#define SCALE_DIV_512    36    ///< scalefactor difference that corresponds to scale difference in 512 times
#define SCALE_ONE_POS   140    ///< scalefactor index that corresponds to scale=1.0
#define SCALE_MAX_POS   255    ///< scalefactor index maximum value
#define SCALE_MAX_DIFF   60    ///< maximum scalefactor difference allowed by standard
#define SCALE_DIFF_ZERO  60    ///< codebook index corresponding to zero scalefactor indices difference

#define POW_SF2_ZERO    200    ///< ff_aac_pow2sf_tab index corresponding to pow(2, 0);

#define NOISE_PRE       256    ///< preamble for NOISE_BT, put in bitstream with the first noise band
#define NOISE_PRE_BITS    9    ///< length of preamble
#define NOISE_OFFSET     90    ///< subtracted from global gain, used as offset for the preamble

/**
 * Long Term Prediction
 */
typedef struct LongTermPrediction {
    int8_t present;
    int16_t lag;
    int coef_idx;
    INTFLOAT coef;
    int8_t used[MAX_LTP_LONG_SFB];
} LongTermPrediction;

/**
 * Individual Channel Stream
 */
typedef struct IndividualChannelStream {
    uint8_t max_sfb;            ///< number of scalefactor bands per group
    enum WindowSequence window_sequence[2];
    uint8_t use_kb_window[2];   ///< If set, use Kaiser-Bessel window, otherwise use a sine window.
    int num_window_groups;
    uint8_t group_len[8];
    LongTermPrediction ltp;
    const uint16_t *swb_offset; ///< table of offsets to the lowest spectral coefficient of a scalefactor band, sfb, for a particular window
    const uint8_t *swb_sizes;   ///< table of scalefactor band sizes for a particular window
    int num_swb;                ///< number of scalefactor window bands
    int num_windows;
    int tns_max_bands;
    int predictor_present;
    int predictor_initialized;
    int predictor_reset_group;
    int predictor_reset_count[31];  ///< used by encoder to count prediction resets
    uint8_t prediction_used[41];
    uint8_t window_clipping[8]; ///< set if a certain window is near clipping
    float clip_avoidance_factor; ///< set if any window is near clipping to the necessary atennuation factor to avoid it
} IndividualChannelStream;

/**
 * Temporal Noise Shaping
 */
typedef struct TemporalNoiseShaping {
    int present;
    int n_filt[8];
    int length[8][4];
    int direction[8][4];
    int order[8][4];
    int coef_idx[8][4][TNS_MAX_ORDER];
    INTFLOAT coef[8][4][TNS_MAX_ORDER];
} TemporalNoiseShaping;

/**
 * Dynamic Range Control - decoded from the bitstream but not processed further.
 */
typedef struct DynamicRangeControl {
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

typedef struct Pulse {
    int num_pulse;
    int start;
    int pos[4];
    int amp[4];
} Pulse;

/**
 * coupling parameters
 */
typedef struct ChannelCoupling {
    enum CouplingPoint coupling_point;  ///< The point during decoding at which coupling is applied.
    int num_coupled;       ///< number of target elements
    enum RawDataBlockType type[8];   ///< Type of channel element to be coupled - SCE or CPE.
    int id_select[8];      ///< element id
    int ch_select[8];      /**< [0] shared list of gains; [1] list of gains for right channel;
                            *   [2] list of gains for left channel; [3] lists of gains for both channels
                            */
    INTFLOAT gain[16][120];
} ChannelCoupling;

/**
 * Single Channel Element - used for both SCE and LFE elements.
 */
typedef struct SingleChannelElement {
    IndividualChannelStream ics;
    TemporalNoiseShaping tns;
    Pulse pulse;
    enum BandType band_type[128];                   ///< band types
    enum BandType band_alt[128];                    ///< alternative band type (used by encoder)
    int band_type_run_end[120];                     ///< band type run end points
    INTFLOAT sf[120];                               ///< scalefactors
    int sf_idx[128];                                ///< scalefactor indices (used by encoder)
    uint8_t zeroes[128];                            ///< band is not coded (used by encoder)
    uint8_t can_pns[128];                           ///< band is allowed to PNS (informative)
    float  is_ener[128];                            ///< Intensity stereo pos (used by encoder)
    float pns_ener[128];                            ///< Noise energy values (used by encoder)
    DECLARE_ALIGNED(32, INTFLOAT, pcoeffs)[1024];   ///< coefficients for IMDCT, pristine
    DECLARE_ALIGNED(32, INTFLOAT, coeffs)[1024];    ///< coefficients for IMDCT, maybe processed
    DECLARE_ALIGNED(32, INTFLOAT, saved)[1536];     ///< overlap
    DECLARE_ALIGNED(32, INTFLOAT, ret_buf)[2048];   ///< PCM output buffer
    DECLARE_ALIGNED(16, INTFLOAT, ltp_state)[3072]; ///< time signal for LTP
    DECLARE_ALIGNED(32, AAC_FLOAT, lcoeffs)[1024];  ///< MDCT of LTP coefficients (used by encoder)
    DECLARE_ALIGNED(32, AAC_FLOAT, prcoeffs)[1024]; ///< Main prediction coefs (used by encoder)
    PredictorState predictor_state[MAX_PREDICTORS];
    INTFLOAT *ret;                                  ///< PCM output
} SingleChannelElement;

/**
 * channel element - generic struct for SCE/CPE/CCE/LFE
 */
typedef struct ChannelElement {
    int present;
    // CPE specific
    int common_window;        ///< Set if channels share a common 'IndividualChannelStream' in bitstream.
    int     ms_mode;          ///< Signals mid/side stereo flags coding mode (used by encoder)
    uint8_t is_mode;          ///< Set if any bands have been encoded using intensity stereo (used by encoder)
    uint8_t ms_mask[128];     ///< Set if mid/side stereo is used for each scalefactor window band
    uint8_t is_mask[128];     ///< Set if intensity stereo is used (used by encoder)
    // shared
    SingleChannelElement ch[2];
    // CCE specific
    ChannelCoupling coup;
    SpectralBandReplication sbr;
} ChannelElement;

/**
 * main AAC context
 */
struct AACContext {
    AVClass        *class;
    AVCodecContext *avctx;
    AVFrame *frame;

    int is_saved;                 ///< Set if elements have stored overlap from previous frame.
    DynamicRangeControl che_drc;

    /**
     * @name Channel element related data
     * @{
     */
    ChannelElement          *che[4][MAX_ELEM_ID];
    ChannelElement  *tag_che_map[4][MAX_ELEM_ID];
    int tags_mapped;
    int warned_remapping_once;
    /** @} */

    /**
     * @name temporary aligned temporary buffers
     * (We do not want to have these on the stack.)
     * @{
     */
    DECLARE_ALIGNED(32, INTFLOAT, buf_mdct)[1024];
    /** @} */

    /**
     * @name Computed / set up during initialization
     * @{
     */
    FFTContext mdct;
    FFTContext mdct_small;
    FFTContext mdct_ld;
    FFTContext mdct_ltp;
#if USE_FIXED
    AVFixedDSPContext *fdsp;
#else
    MDCT15Context *mdct480;
    AVFloatDSPContext *fdsp;
#endif /* USE_FIXED */
    int random_state;
    /** @} */

    /**
     * @name Members used for output
     * @{
     */
    SingleChannelElement *output_element[MAX_CHANNELS]; ///< Points to each SingleChannelElement
    /** @} */


    /**
     * @name Japanese DTV specific extension
     * @{
     */
    int force_dmono_mode;///< 0->not dmono, 1->use first channel, 2->use second channel
    int dmono_mode;      ///< 0->not dmono, 1->use first channel, 2->use second channel
    /** @} */

    DECLARE_ALIGNED(32, INTFLOAT, temp)[128];

    OutputConfiguration oc[2];
    int warned_num_aac_frames;

    /* aacdec functions pointers */
    void (*imdct_and_windowing)(AACContext *ac, SingleChannelElement *sce);
    void (*apply_ltp)(AACContext *ac, SingleChannelElement *sce);
    void (*apply_tns)(INTFLOAT coef[1024], TemporalNoiseShaping *tns,
                      IndividualChannelStream *ics, int decode);
    void (*windowing_and_mdct_ltp)(AACContext *ac, INTFLOAT *out,
                                   INTFLOAT *in, IndividualChannelStream *ics);
    void (*update_ltp)(AACContext *ac, SingleChannelElement *sce);
    void (*vector_pow43)(int *coefs, int len);
    void (*subband_scale)(int *dst, int *src, int scale, int offset, int len);

};

void ff_aacdec_init_mips(AACContext *c);

#endif /* AVCODEC_AAC_H */
