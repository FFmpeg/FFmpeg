/*
 * The simplest AC-3 encoder
 * Copyright (c) 2000 Fabrice Bellard
 * Copyright (c) 2006-2010 Justin Ruggles <justin.ruggles@gmail.com>
 * Copyright (c) 2006-2010 Prakash Punnoor <prakash@punnoor.de>
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

/**
 * @file
 * The simplest AC-3 encoder.
 */

//#define DEBUG
//#define ASSERT_LEVEL 2

#include <stdint.h>

#include "libavutil/audioconvert.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/crc.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "put_bits.h"
#include "dsputil.h"
#include "ac3dsp.h"
#include "ac3.h"
#include "audioconvert.h"
#include "fft.h"


#ifndef CONFIG_AC3ENC_FLOAT
#define CONFIG_AC3ENC_FLOAT 0
#endif


/** Maximum number of exponent groups. +1 for separate DC exponent. */
#define AC3_MAX_EXP_GROUPS 85

#if CONFIG_AC3ENC_FLOAT
#define MAC_COEF(d,a,b) ((d)+=(a)*(b))
typedef float SampleType;
typedef float CoefType;
typedef float CoefSumType;
#else
#define MAC_COEF(d,a,b) MAC64(d,a,b)
typedef int16_t SampleType;
typedef int32_t CoefType;
typedef int64_t CoefSumType;
#endif

typedef struct AC3MDCTContext {
    const SampleType *window;           ///< MDCT window function
    FFTContext fft;                     ///< FFT context for MDCT calculation
} AC3MDCTContext;

/**
 * Encoding Options used by AVOption.
 */
typedef struct AC3EncOptions {
    /* AC-3 metadata options*/
    int dialogue_level;
    int bitstream_mode;
    float center_mix_level;
    float surround_mix_level;
    int dolby_surround_mode;
    int audio_production_info;
    int mixing_level;
    int room_type;
    int copyright;
    int original;
    int extended_bsi_1;
    int preferred_stereo_downmix;
    float ltrt_center_mix_level;
    float ltrt_surround_mix_level;
    float loro_center_mix_level;
    float loro_surround_mix_level;
    int extended_bsi_2;
    int dolby_surround_ex_mode;
    int dolby_headphone_mode;
    int ad_converter_type;

    /* other encoding options */
    int allow_per_frame_metadata;
    int stereo_rematrixing;
    int channel_coupling;
    int cpl_start;
} AC3EncOptions;

/**
 * Data for a single audio block.
 */
typedef struct AC3Block {
    uint8_t  **bap;                             ///< bit allocation pointers (bap)
    CoefType **mdct_coef;                       ///< MDCT coefficients
    int32_t  **fixed_coef;                      ///< fixed-point MDCT coefficients
    uint8_t  **exp;                             ///< original exponents
    uint8_t  **grouped_exp;                     ///< grouped exponents
    int16_t  **psd;                             ///< psd per frequency bin
    int16_t  **band_psd;                        ///< psd per critical band
    int16_t  **mask;                            ///< masking curve
    uint16_t **qmant;                           ///< quantized mantissas
    uint8_t  **cpl_coord_exp;                   ///< coupling coord exponents           (cplcoexp)
    uint8_t  **cpl_coord_mant;                  ///< coupling coord mantissas           (cplcomant)
    uint8_t  coeff_shift[AC3_MAX_CHANNELS];     ///< fixed-point coefficient shift values
    uint8_t  new_rematrixing_strategy;          ///< send new rematrixing flags in this block
    int      num_rematrixing_bands;             ///< number of rematrixing bands
    uint8_t  rematrixing_flags[4];              ///< rematrixing flags
    struct AC3Block *exp_ref_block[AC3_MAX_CHANNELS]; ///< reference blocks for EXP_REUSE
    int      new_cpl_strategy;                  ///< send new coupling strategy
    int      cpl_in_use;                        ///< coupling in use for this block     (cplinu)
    uint8_t  channel_in_cpl[AC3_MAX_CHANNELS];  ///< channel in coupling                (chincpl)
    int      num_cpl_channels;                  ///< number of channels in coupling
    uint8_t  new_cpl_coords;                    ///< send new coupling coordinates      (cplcoe)
    uint8_t  cpl_master_exp[AC3_MAX_CHANNELS];  ///< coupling coord master exponents    (mstrcplco)
    int      new_snr_offsets;                   ///< send new SNR offsets
    int      new_cpl_leak;                      ///< send new coupling leak info
    int      end_freq[AC3_MAX_CHANNELS];        ///< end frequency bin                  (endmant)
} AC3Block;

/**
 * AC-3 encoder private context.
 */
typedef struct AC3EncodeContext {
    AVClass *av_class;                      ///< AVClass used for AVOption
    AC3EncOptions options;                  ///< encoding options
    PutBitContext pb;                       ///< bitstream writer context
    DSPContext dsp;
    AC3DSPContext ac3dsp;                   ///< AC-3 optimized functions
    AC3MDCTContext mdct;                    ///< MDCT context

    AC3Block blocks[AC3_MAX_BLOCKS];        ///< per-block info

    int bitstream_id;                       ///< bitstream id                           (bsid)
    int bitstream_mode;                     ///< bitstream mode                         (bsmod)

    int bit_rate;                           ///< target bit rate, in bits-per-second
    int sample_rate;                        ///< sampling frequency, in Hz

    int frame_size_min;                     ///< minimum frame size in case rounding is necessary
    int frame_size;                         ///< current frame size in bytes
    int frame_size_code;                    ///< frame size code                        (frmsizecod)
    uint16_t crc_inv[2];
    int bits_written;                       ///< bit count    (used to avg. bitrate)
    int samples_written;                    ///< sample count (used to avg. bitrate)

    int fbw_channels;                       ///< number of full-bandwidth channels      (nfchans)
    int channels;                           ///< total number of channels               (nchans)
    int lfe_on;                             ///< indicates if there is an LFE channel   (lfeon)
    int lfe_channel;                        ///< channel index of the LFE channel
    int has_center;                         ///< indicates if there is a center channel
    int has_surround;                       ///< indicates if there are one or more surround channels
    int channel_mode;                       ///< channel mode                           (acmod)
    const uint8_t *channel_map;             ///< channel map used to reorder channels

    int center_mix_level;                   ///< center mix level code
    int surround_mix_level;                 ///< surround mix level code
    int ltrt_center_mix_level;              ///< Lt/Rt center mix level code
    int ltrt_surround_mix_level;            ///< Lt/Rt surround mix level code
    int loro_center_mix_level;              ///< Lo/Ro center mix level code
    int loro_surround_mix_level;            ///< Lo/Ro surround mix level code

    int cutoff;                             ///< user-specified cutoff frequency, in Hz
    int bandwidth_code;                     ///< bandwidth code (0 to 60)               (chbwcod)
    int start_freq[AC3_MAX_CHANNELS];       ///< start frequency bin                    (strtmant)
    int cpl_end_freq;                       ///< coupling channel end frequency bin

    int cpl_on;                             ///< coupling turned on for this frame
    int cpl_enabled;                        ///< coupling enabled for all frames
    int num_cpl_subbands;                   ///< number of coupling subbands            (ncplsubnd)
    int num_cpl_bands;                      ///< number of coupling bands               (ncplbnd)
    uint8_t cpl_band_sizes[AC3_MAX_CPL_BANDS];  ///< number of coeffs in each coupling band

    int rematrixing_enabled;                ///< stereo rematrixing enabled

    /* bitrate allocation control */
    int slow_gain_code;                     ///< slow gain code                         (sgaincod)
    int slow_decay_code;                    ///< slow decay code                        (sdcycod)
    int fast_decay_code;                    ///< fast decay code                        (fdcycod)
    int db_per_bit_code;                    ///< dB/bit code                            (dbpbcod)
    int floor_code;                         ///< floor code                             (floorcod)
    AC3BitAllocParameters bit_alloc;        ///< bit allocation parameters
    int coarse_snr_offset;                  ///< coarse SNR offsets                     (csnroffst)
    int fast_gain_code[AC3_MAX_CHANNELS];   ///< fast gain codes (signal-to-mask ratio) (fgaincod)
    int fine_snr_offset[AC3_MAX_CHANNELS];  ///< fine SNR offsets                       (fsnroffst)
    int frame_bits_fixed;                   ///< number of non-coefficient bits for fixed parameters
    int frame_bits;                         ///< all frame bits except exponents and mantissas
    int exponent_bits;                      ///< number of bits used for exponents

    SampleType **planar_samples;
    uint8_t *bap_buffer;
    uint8_t *bap1_buffer;
    CoefType *mdct_coef_buffer;
    int32_t *fixed_coef_buffer;
    uint8_t *exp_buffer;
    uint8_t *grouped_exp_buffer;
    int16_t *psd_buffer;
    int16_t *band_psd_buffer;
    int16_t *mask_buffer;
    uint16_t *qmant_buffer;
    uint8_t *cpl_coord_exp_buffer;
    uint8_t *cpl_coord_mant_buffer;

    uint8_t exp_strategy[AC3_MAX_CHANNELS][AC3_MAX_BLOCKS]; ///< exponent strategies

    DECLARE_ALIGNED(32, SampleType, windowed_samples)[AC3_WINDOW_SIZE];
} AC3EncodeContext;

typedef struct AC3Mant {
    uint16_t *qmant1_ptr, *qmant2_ptr, *qmant4_ptr; ///< mantissa pointers for bap=1,2,4
    int mant1_cnt, mant2_cnt, mant4_cnt;    ///< mantissa counts for bap=1,2,4
} AC3Mant;

#define CMIXLEV_NUM_OPTIONS 3
static const float cmixlev_options[CMIXLEV_NUM_OPTIONS] = {
    LEVEL_MINUS_3DB, LEVEL_MINUS_4POINT5DB, LEVEL_MINUS_6DB
};

#define SURMIXLEV_NUM_OPTIONS 3
static const float surmixlev_options[SURMIXLEV_NUM_OPTIONS] = {
    LEVEL_MINUS_3DB, LEVEL_MINUS_6DB, LEVEL_ZERO
};

#define EXTMIXLEV_NUM_OPTIONS 8
static const float extmixlev_options[EXTMIXLEV_NUM_OPTIONS] = {
    LEVEL_PLUS_3DB,  LEVEL_PLUS_1POINT5DB,  LEVEL_ONE,       LEVEL_MINUS_4POINT5DB,
    LEVEL_MINUS_3DB, LEVEL_MINUS_4POINT5DB, LEVEL_MINUS_6DB, LEVEL_ZERO
};


#define OFFSET(param) offsetof(AC3EncodeContext, options.param)
#define AC3ENC_PARAM (AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM)

static const AVOption options[] = {
/* Metadata Options */
{"per_frame_metadata", "Allow Changing Metadata Per-Frame", OFFSET(allow_per_frame_metadata), FF_OPT_TYPE_INT, {.dbl = 0 }, 0, 1, AC3ENC_PARAM},
/* downmix levels */
{"center_mixlev", "Center Mix Level", OFFSET(center_mix_level), FF_OPT_TYPE_FLOAT, {.dbl = LEVEL_MINUS_4POINT5DB }, 0.0, 1.0, AC3ENC_PARAM},
{"surround_mixlev", "Surround Mix Level", OFFSET(surround_mix_level), FF_OPT_TYPE_FLOAT, {.dbl = LEVEL_MINUS_6DB }, 0.0, 1.0, AC3ENC_PARAM},
/* audio production information */
{"mixing_level", "Mixing Level", OFFSET(mixing_level), FF_OPT_TYPE_INT, {.dbl = -1 }, -1, 111, AC3ENC_PARAM},
{"room_type", "Room Type", OFFSET(room_type), FF_OPT_TYPE_INT, {.dbl = -1 }, -1, 2, AC3ENC_PARAM, "room_type"},
    {"notindicated", "Not Indicated (default)", 0, FF_OPT_TYPE_CONST, {.dbl = 0 }, INT_MIN, INT_MAX, AC3ENC_PARAM, "room_type"},
    {"large",        "Large Room",              0, FF_OPT_TYPE_CONST, {.dbl = 1 }, INT_MIN, INT_MAX, AC3ENC_PARAM, "room_type"},
    {"small",        "Small Room",              0, FF_OPT_TYPE_CONST, {.dbl = 2 }, INT_MIN, INT_MAX, AC3ENC_PARAM, "room_type"},
/* other metadata options */
{"copyright", "Copyright Bit", OFFSET(copyright), FF_OPT_TYPE_INT, {.dbl = 0 }, 0, 1, AC3ENC_PARAM},
{"dialnorm", "Dialogue Level (dB)", OFFSET(dialogue_level), FF_OPT_TYPE_INT, {.dbl = -31 }, -31, -1, AC3ENC_PARAM},
{"dsur_mode", "Dolby Surround Mode", OFFSET(dolby_surround_mode), FF_OPT_TYPE_INT, {.dbl = 0 }, 0, 2, AC3ENC_PARAM, "dsur_mode"},
    {"notindicated", "Not Indicated (default)",    0, FF_OPT_TYPE_CONST, {.dbl = 0 }, INT_MIN, INT_MAX, AC3ENC_PARAM, "dsur_mode"},
    {"on",           "Dolby Surround Encoded",     0, FF_OPT_TYPE_CONST, {.dbl = 1 }, INT_MIN, INT_MAX, AC3ENC_PARAM, "dsur_mode"},
    {"off",          "Not Dolby Surround Encoded", 0, FF_OPT_TYPE_CONST, {.dbl = 2 }, INT_MIN, INT_MAX, AC3ENC_PARAM, "dsur_mode"},
{"original", "Original Bit Stream", OFFSET(original), FF_OPT_TYPE_INT,   {.dbl = 1 }, 0, 1, AC3ENC_PARAM},
/* extended bitstream information */
{"dmix_mode", "Preferred Stereo Downmix Mode", OFFSET(preferred_stereo_downmix), FF_OPT_TYPE_INT, {.dbl = -1 }, -1, 2, AC3ENC_PARAM, "dmix_mode"},
    {"notindicated", "Not Indicated (default)", 0, FF_OPT_TYPE_CONST, {.dbl = 0 }, INT_MIN, INT_MAX, AC3ENC_PARAM, "dmix_mode"},
    {"ltrt", "Lt/Rt Downmix Preferred",         0, FF_OPT_TYPE_CONST, {.dbl = 1 }, INT_MIN, INT_MAX, AC3ENC_PARAM, "dmix_mode"},
    {"loro", "Lo/Ro Downmix Preferred",         0, FF_OPT_TYPE_CONST, {.dbl = 2 }, INT_MIN, INT_MAX, AC3ENC_PARAM, "dmix_mode"},
{"ltrt_cmixlev", "Lt/Rt Center Mix Level", OFFSET(ltrt_center_mix_level), FF_OPT_TYPE_FLOAT, {.dbl = -1.0 }, -1.0, 2.0, AC3ENC_PARAM},
{"ltrt_surmixlev", "Lt/Rt Surround Mix Level", OFFSET(ltrt_surround_mix_level), FF_OPT_TYPE_FLOAT, {.dbl = -1.0 }, -1.0, 2.0, AC3ENC_PARAM},
{"loro_cmixlev", "Lo/Ro Center Mix Level", OFFSET(loro_center_mix_level), FF_OPT_TYPE_FLOAT, {.dbl = -1.0 }, -1.0, 2.0, AC3ENC_PARAM},
{"loro_surmixlev", "Lo/Ro Surround Mix Level", OFFSET(loro_surround_mix_level), FF_OPT_TYPE_FLOAT, {.dbl = -1.0 }, -1.0, 2.0, AC3ENC_PARAM},
{"dsurex_mode", "Dolby Surround EX Mode", OFFSET(dolby_surround_ex_mode), FF_OPT_TYPE_INT, {.dbl = -1 }, -1, 2, AC3ENC_PARAM, "dsurex_mode"},
    {"notindicated", "Not Indicated (default)",       0, FF_OPT_TYPE_CONST, {.dbl = 0 }, INT_MIN, INT_MAX, AC3ENC_PARAM, "dsurex_mode"},
    {"on",           "Dolby Surround EX Encoded",     0, FF_OPT_TYPE_CONST, {.dbl = 1 }, INT_MIN, INT_MAX, AC3ENC_PARAM, "dsurex_mode"},
    {"off",          "Not Dolby Surround EX Encoded", 0, FF_OPT_TYPE_CONST, {.dbl = 2 }, INT_MIN, INT_MAX, AC3ENC_PARAM, "dsurex_mode"},
{"dheadphone_mode", "Dolby Headphone Mode", OFFSET(dolby_headphone_mode), FF_OPT_TYPE_INT, {.dbl = -1 }, -1, 2, AC3ENC_PARAM, "dheadphone_mode"},
    {"notindicated", "Not Indicated (default)",     0, FF_OPT_TYPE_CONST, {.dbl = 0 }, INT_MIN, INT_MAX, AC3ENC_PARAM, "dheadphone_mode"},
    {"on",           "Dolby Headphone Encoded",     0, FF_OPT_TYPE_CONST, {.dbl = 1 }, INT_MIN, INT_MAX, AC3ENC_PARAM, "dheadphone_mode"},
    {"off",          "Not Dolby Headphone Encoded", 0, FF_OPT_TYPE_CONST, {.dbl = 2 }, INT_MIN, INT_MAX, AC3ENC_PARAM, "dheadphone_mode"},
{"ad_conv_type", "A/D Converter Type", OFFSET(ad_converter_type), FF_OPT_TYPE_INT, {.dbl = -1 }, -1, 1, AC3ENC_PARAM, "ad_conv_type"},
    {"standard", "Standard (default)", 0, FF_OPT_TYPE_CONST, {.dbl = 0 }, INT_MIN, INT_MAX, AC3ENC_PARAM, "ad_conv_type"},
    {"hdcd",     "HDCD",               0, FF_OPT_TYPE_CONST, {.dbl = 1 }, INT_MIN, INT_MAX, AC3ENC_PARAM, "ad_conv_type"},
/* Other Encoding Options */
{"stereo_rematrixing", "Stereo Rematrixing", OFFSET(stereo_rematrixing), FF_OPT_TYPE_INT, {.dbl = 1 }, 0, 1, AC3ENC_PARAM},
#if CONFIG_AC3ENC_FLOAT
{"channel_coupling",   "Channel Coupling",   OFFSET(channel_coupling),   FF_OPT_TYPE_INT, {.dbl = 1 }, 0, 1, AC3ENC_PARAM, "channel_coupling"},
    {"auto", "Selected by the Encoder", 0, FF_OPT_TYPE_CONST, {.dbl = -1 }, INT_MIN, INT_MAX, AC3ENC_PARAM, "channel_coupling"},
{"cpl_start_band", "Coupling Start Band", OFFSET(cpl_start), FF_OPT_TYPE_INT, {.dbl = -1 }, -1, 15, AC3ENC_PARAM, "cpl_start_band"},
    {"auto", "Selected by the Encoder", 0, FF_OPT_TYPE_CONST, {.dbl = -1 }, INT_MIN, INT_MAX, AC3ENC_PARAM, "cpl_start_band"},
#endif
{NULL}
};

#if CONFIG_AC3ENC_FLOAT
static AVClass ac3enc_class = { "AC-3 Encoder", av_default_item_name,
                                options, LIBAVUTIL_VERSION_INT };
#else
static AVClass ac3enc_class = { "Fixed-Point AC-3 Encoder", av_default_item_name,
                                options, LIBAVUTIL_VERSION_INT };
#endif


/* prototypes for functions in ac3enc_fixed.c and ac3enc_float.c */

static av_cold void mdct_end(AC3MDCTContext *mdct);

static av_cold int mdct_init(AVCodecContext *avctx, AC3MDCTContext *mdct,
                             int nbits);

static void apply_window(DSPContext *dsp, SampleType *output, const SampleType *input,
                         const SampleType *window, unsigned int len);

static int normalize_samples(AC3EncodeContext *s);

static void scale_coefficients(AC3EncodeContext *s);


/**
 * LUT for number of exponent groups.
 * exponent_group_tab[coupling][exponent strategy-1][number of coefficients]
 */
static uint8_t exponent_group_tab[2][3][256];


/**
 * List of supported channel layouts.
 */
static const int64_t ac3_channel_layouts[] = {
     AV_CH_LAYOUT_MONO,
     AV_CH_LAYOUT_STEREO,
     AV_CH_LAYOUT_2_1,
     AV_CH_LAYOUT_SURROUND,
     AV_CH_LAYOUT_2_2,
     AV_CH_LAYOUT_QUAD,
     AV_CH_LAYOUT_4POINT0,
     AV_CH_LAYOUT_5POINT0,
     AV_CH_LAYOUT_5POINT0_BACK,
    (AV_CH_LAYOUT_MONO     | AV_CH_LOW_FREQUENCY),
    (AV_CH_LAYOUT_STEREO   | AV_CH_LOW_FREQUENCY),
    (AV_CH_LAYOUT_2_1      | AV_CH_LOW_FREQUENCY),
    (AV_CH_LAYOUT_SURROUND | AV_CH_LOW_FREQUENCY),
    (AV_CH_LAYOUT_2_2      | AV_CH_LOW_FREQUENCY),
    (AV_CH_LAYOUT_QUAD     | AV_CH_LOW_FREQUENCY),
    (AV_CH_LAYOUT_4POINT0  | AV_CH_LOW_FREQUENCY),
     AV_CH_LAYOUT_5POINT1,
     AV_CH_LAYOUT_5POINT1_BACK,
     0
};


/**
 * LUT to select the bandwidth code based on the bit rate, sample rate, and
 * number of full-bandwidth channels.
 * bandwidth_tab[fbw_channels-1][sample rate code][bit rate code]
 */
static const uint8_t ac3_bandwidth_tab[5][3][19] = {
//      32  40  48  56  64  80  96 112 128 160 192 224 256 320 384 448 512 576 640

    { {  0,  0,  0, 12, 16, 32, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48 },
      {  0,  0,  0, 16, 20, 36, 56, 56, 56, 56, 56, 56, 56, 56, 56, 56, 56, 56, 56 },
      {  0,  0,  0, 32, 40, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60 } },

    { {  0,  0,  0,  0,  0,  0,  0, 20, 24, 32, 48, 48, 48, 48, 48, 48, 48, 48, 48 },
      {  0,  0,  0,  0,  0,  0,  4, 24, 28, 36, 56, 56, 56, 56, 56, 56, 56, 56, 56 },
      {  0,  0,  0,  0,  0,  0, 20, 44, 52, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60 } },

    { {  0,  0,  0,  0,  0,  0,  0,  0,  0, 16, 24, 32, 40, 48, 48, 48, 48, 48, 48 },
      {  0,  0,  0,  0,  0,  0,  0,  0,  4, 20, 28, 36, 44, 56, 56, 56, 56, 56, 56 },
      {  0,  0,  0,  0,  0,  0,  0,  0, 20, 40, 48, 60, 60, 60, 60, 60, 60, 60, 60 } },

    { {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 12, 24, 32, 48, 48, 48, 48, 48, 48 },
      {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 16, 28, 36, 56, 56, 56, 56, 56, 56 },
      {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 32, 48, 60, 60, 60, 60, 60, 60, 60 } },

    { {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  8, 20, 32, 40, 48, 48, 48, 48 },
      {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 12, 24, 36, 44, 56, 56, 56, 56 },
      {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 28, 44, 60, 60, 60, 60, 60, 60 } }
};


/**
 * LUT to select the coupling start band based on the bit rate, sample rate, and
 * number of full-bandwidth channels. -1 = coupling off
 * ac3_coupling_start_tab[channel_mode-2][sample rate code][bit rate code]
 *
 * TODO: more testing for optimal parameters.
 *       multi-channel tests at 44.1kHz and 32kHz.
 */
static const int8_t ac3_coupling_start_tab[6][3][19] = {
//      32  40  48  56  64  80  96 112 128 160 192 224 256 320 384 448 512 576 640

    // 2/0
    { {  0,  0,  0,  0,  0,  0,  0,  1,  1,  7,  8, 11, 12, -1, -1, -1, -1, -1, -1 },
      {  0,  0,  0,  0,  0,  0,  1,  3,  5,  7, 10, 12, 13, -1, -1, -1, -1, -1, -1 },
      {  0,  0,  0,  0,  1,  2,  2,  9, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1 } },

    // 3/0
    { {  0,  0,  0,  0,  0,  0,  0,  0,  2,  2,  6,  9, 11, 12, 13, -1, -1, -1, -1 },
      {  0,  0,  0,  0,  0,  0,  0,  0,  2,  2,  6,  9, 11, 12, 13, -1, -1, -1, -1 },
      { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 } },

    // 2/1 - untested
    { {  0,  0,  0,  0,  0,  0,  0,  0,  2,  2,  6,  9, 11, 12, 13, -1, -1, -1, -1 },
      {  0,  0,  0,  0,  0,  0,  0,  0,  2,  2,  6,  9, 11, 12, 13, -1, -1, -1, -1 },
      { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 } },

    // 3/1
    { {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  3,  2, 10, 11, 11, 12, 12, 14, -1 },
      {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  3,  2, 10, 11, 11, 12, 12, 14, -1 },
      { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 } },

    // 2/2 - untested
    { {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  3,  2, 10, 11, 11, 12, 12, 14, -1 },
      {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  3,  2, 10, 11, 11, 12, 12, 14, -1 },
      { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 } },

    // 3/2
    { {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  6,  8, 11, 12, 12, -1, -1 },
      {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  6,  8, 11, 12, 12, -1, -1 },
      { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 } },
};


/**
 * Adjust the frame size to make the average bit rate match the target bit rate.
 * This is only needed for 11025, 22050, and 44100 sample rates.
 */
static void adjust_frame_size(AC3EncodeContext *s)
{
    while (s->bits_written >= s->bit_rate && s->samples_written >= s->sample_rate) {
        s->bits_written    -= s->bit_rate;
        s->samples_written -= s->sample_rate;
    }
    s->frame_size = s->frame_size_min +
                    2 * (s->bits_written * s->sample_rate < s->samples_written * s->bit_rate);
    s->bits_written    += s->frame_size * 8;
    s->samples_written += AC3_FRAME_SIZE;
}


/**
 * Deinterleave input samples.
 * Channels are reordered from Libav's default order to AC-3 order.
 */
static void deinterleave_input_samples(AC3EncodeContext *s,
                                       const SampleType *samples)
{
    int ch, i;

    /* deinterleave and remap input samples */
    for (ch = 0; ch < s->channels; ch++) {
        const SampleType *sptr;
        int sinc;

        /* copy last 256 samples of previous frame to the start of the current frame */
        memcpy(&s->planar_samples[ch][0], &s->planar_samples[ch][AC3_FRAME_SIZE],
               AC3_BLOCK_SIZE * sizeof(s->planar_samples[0][0]));

        /* deinterleave */
        sinc = s->channels;
        sptr = samples + s->channel_map[ch];
        for (i = AC3_BLOCK_SIZE; i < AC3_FRAME_SIZE+AC3_BLOCK_SIZE; i++) {
            s->planar_samples[ch][i] = *sptr;
            sptr += sinc;
        }
    }
}


/**
 * Apply the MDCT to input samples to generate frequency coefficients.
 * This applies the KBD window and normalizes the input to reduce precision
 * loss due to fixed-point calculations.
 */
static void apply_mdct(AC3EncodeContext *s)
{
    int blk, ch;

    for (ch = 0; ch < s->channels; ch++) {
        for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
            AC3Block *block = &s->blocks[blk];
            const SampleType *input_samples = &s->planar_samples[ch][blk * AC3_BLOCK_SIZE];

            apply_window(&s->dsp, s->windowed_samples, input_samples, s->mdct.window, AC3_WINDOW_SIZE);

            block->coeff_shift[ch+1] = normalize_samples(s);

            s->mdct.fft.mdct_calcw(&s->mdct.fft, block->mdct_coef[ch+1],
                                   s->windowed_samples);
        }
    }
}


static void compute_coupling_strategy(AC3EncodeContext *s)
{
    int blk, ch;
    int got_cpl_snr;

    /* set coupling use flags for each block/channel */
    /* TODO: turn coupling on/off and adjust start band based on bit usage */
    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        AC3Block *block = &s->blocks[blk];
        for (ch = 1; ch <= s->fbw_channels; ch++)
            block->channel_in_cpl[ch] = s->cpl_on;
    }

    /* enable coupling for each block if at least 2 channels have coupling
       enabled for that block */
    got_cpl_snr = 0;
    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        AC3Block *block = &s->blocks[blk];
        block->num_cpl_channels = 0;
        for (ch = 1; ch <= s->fbw_channels; ch++)
            block->num_cpl_channels += block->channel_in_cpl[ch];
        block->cpl_in_use = block->num_cpl_channels > 1;
        if (!block->cpl_in_use) {
            block->num_cpl_channels = 0;
            for (ch = 1; ch <= s->fbw_channels; ch++)
                block->channel_in_cpl[ch] = 0;
        }

        block->new_cpl_strategy = !blk;
        if (blk) {
            for (ch = 1; ch <= s->fbw_channels; ch++) {
                if (block->channel_in_cpl[ch] != s->blocks[blk-1].channel_in_cpl[ch]) {
                    block->new_cpl_strategy = 1;
                    break;
                }
            }
        }
        block->new_cpl_leak = block->new_cpl_strategy;

        if (!blk || (block->cpl_in_use && !got_cpl_snr)) {
            block->new_snr_offsets = 1;
            if (block->cpl_in_use)
                got_cpl_snr = 1;
        } else {
            block->new_snr_offsets = 0;
        }
    }

    /* set bandwidth for each channel */
    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        AC3Block *block = &s->blocks[blk];
        for (ch = 1; ch <= s->fbw_channels; ch++) {
            if (block->channel_in_cpl[ch])
                block->end_freq[ch] = s->start_freq[CPL_CH];
            else
                block->end_freq[ch] = s->bandwidth_code * 3 + 73;
        }
    }
}


/**
 * Calculate a single coupling coordinate.
 */
static inline float calc_cpl_coord(float energy_ch, float energy_cpl)
{
    float coord = 0.125;
    if (energy_cpl > 0)
        coord *= sqrtf(energy_ch / energy_cpl);
    return coord;
}


/**
 * Calculate coupling channel and coupling coordinates.
 * TODO: Currently this is only used for the floating-point encoder. I was
 *       able to make it work for the fixed-point encoder, but quality was
 *       generally lower in most cases than not using coupling. If a more
 *       adaptive coupling strategy were to be implemented it might be useful
 *       at that time to use coupling for the fixed-point encoder as well.
 */
static void apply_channel_coupling(AC3EncodeContext *s)
{
#if CONFIG_AC3ENC_FLOAT
    DECLARE_ALIGNED(16, float,   cpl_coords)      [AC3_MAX_BLOCKS][AC3_MAX_CHANNELS][16] = {{{0}}};
    DECLARE_ALIGNED(16, int32_t, fixed_cpl_coords)[AC3_MAX_BLOCKS][AC3_MAX_CHANNELS][16] = {{{0}}};
    int blk, ch, bnd, i, j;
    CoefSumType energy[AC3_MAX_BLOCKS][AC3_MAX_CHANNELS][16] = {{{0}}};
    int num_cpl_coefs = s->num_cpl_subbands * 12;

    /* calculate coupling channel from fbw channels */
    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        AC3Block *block = &s->blocks[blk];
        CoefType *cpl_coef = &block->mdct_coef[CPL_CH][s->start_freq[CPL_CH]];
        if (!block->cpl_in_use)
            continue;
        memset(cpl_coef-1, 0, (num_cpl_coefs+4) * sizeof(*cpl_coef));
        for (ch = 1; ch <= s->fbw_channels; ch++) {
            CoefType *ch_coef = &block->mdct_coef[ch][s->start_freq[CPL_CH]];
            if (!block->channel_in_cpl[ch])
                continue;
            for (i = 0; i < num_cpl_coefs; i++)
                cpl_coef[i] += ch_coef[i];
        }
        /* note: coupling start bin % 4 will always be 1 and num_cpl_coefs
                 will always be a multiple of 12, so we need to subtract 1 from
                 the start and add 4 to the length when using optimized
                 functions which require 16-byte alignment. */

        /* coefficients must be clipped to +/- 1.0 in order to be encoded */
        s->dsp.vector_clipf(cpl_coef-1, cpl_coef-1, -1.0f, 1.0f, num_cpl_coefs+4);

        /* scale coupling coefficients from float to 24-bit fixed-point */
        s->ac3dsp.float_to_fixed24(&block->fixed_coef[CPL_CH][s->start_freq[CPL_CH]-1],
                                   cpl_coef-1, num_cpl_coefs+4);
    }

    /* calculate energy in each band in coupling channel and each fbw channel */
    /* TODO: possibly use SIMD to speed up energy calculation */
    bnd = 0;
    i = s->start_freq[CPL_CH];
    while (i < s->cpl_end_freq) {
        int band_size = s->cpl_band_sizes[bnd];
        for (ch = CPL_CH; ch <= s->fbw_channels; ch++) {
            for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
                AC3Block *block = &s->blocks[blk];
                if (!block->cpl_in_use || (ch > CPL_CH && !block->channel_in_cpl[ch]))
                    continue;
                for (j = 0; j < band_size; j++) {
                    CoefType v = block->mdct_coef[ch][i+j];
                    MAC_COEF(energy[blk][ch][bnd], v, v);
                }
            }
        }
        i += band_size;
        bnd++;
    }

    /* determine which blocks to send new coupling coordinates for */
    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        AC3Block *block  = &s->blocks[blk];
        AC3Block *block0 = blk ? &s->blocks[blk-1] : NULL;
        int new_coords = 0;
        CoefSumType coord_diff[AC3_MAX_CHANNELS] = {0,};

        if (block->cpl_in_use) {
            /* calculate coupling coordinates for all blocks and calculate the
               average difference between coordinates in successive blocks */
            for (ch = 1; ch <= s->fbw_channels; ch++) {
                if (!block->channel_in_cpl[ch])
                    continue;

                for (bnd = 0; bnd < s->num_cpl_bands; bnd++) {
                    cpl_coords[blk][ch][bnd] = calc_cpl_coord(energy[blk][ch][bnd],
                                                              energy[blk][CPL_CH][bnd]);
                    if (blk > 0 && block0->cpl_in_use &&
                        block0->channel_in_cpl[ch]) {
                        coord_diff[ch] += fabs(cpl_coords[blk-1][ch][bnd] -
                                               cpl_coords[blk  ][ch][bnd]);
                    }
                }
                coord_diff[ch] /= s->num_cpl_bands;
            }

            /* send new coordinates if this is the first block, if previous
             * block did not use coupling but this block does, the channels
             * using coupling has changed from the previous block, or the
             * coordinate difference from the last block for any channel is
             * greater than a threshold value. */
            if (blk == 0) {
                new_coords = 1;
            } else if (!block0->cpl_in_use) {
                new_coords = 1;
            } else {
                for (ch = 1; ch <= s->fbw_channels; ch++) {
                    if (block->channel_in_cpl[ch] && !block0->channel_in_cpl[ch]) {
                        new_coords = 1;
                        break;
                    }
                }
                if (!new_coords) {
                    for (ch = 1; ch <= s->fbw_channels; ch++) {
                        if (block->channel_in_cpl[ch] && coord_diff[ch] > 0.04) {
                            new_coords = 1;
                            break;
                        }
                    }
                }
            }
        }
        block->new_cpl_coords = new_coords;
    }

    /* calculate final coupling coordinates, taking into account reusing of
       coordinates in successive blocks */
    for (bnd = 0; bnd < s->num_cpl_bands; bnd++) {
        blk = 0;
        while (blk < AC3_MAX_BLOCKS) {
            int blk1;
            CoefSumType energy_cpl;
            AC3Block *block  = &s->blocks[blk];

            if (!block->cpl_in_use) {
                blk++;
                continue;
            }

            energy_cpl = energy[blk][CPL_CH][bnd];
            blk1 = blk+1;
            while (!s->blocks[blk1].new_cpl_coords && blk1 < AC3_MAX_BLOCKS) {
                if (s->blocks[blk1].cpl_in_use)
                    energy_cpl += energy[blk1][CPL_CH][bnd];
                blk1++;
            }

            for (ch = 1; ch <= s->fbw_channels; ch++) {
                CoefType energy_ch;
                if (!block->channel_in_cpl[ch])
                    continue;
                energy_ch = energy[blk][ch][bnd];
                blk1 = blk+1;
                while (!s->blocks[blk1].new_cpl_coords && blk1 < AC3_MAX_BLOCKS) {
                    if (s->blocks[blk1].cpl_in_use)
                        energy_ch += energy[blk1][ch][bnd];
                    blk1++;
                }
                cpl_coords[blk][ch][bnd] = calc_cpl_coord(energy_ch, energy_cpl);
            }
            blk = blk1;
        }
    }

    /* calculate exponents/mantissas for coupling coordinates */
    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        AC3Block *block = &s->blocks[blk];
        if (!block->cpl_in_use || !block->new_cpl_coords)
            continue;

        s->ac3dsp.float_to_fixed24(fixed_cpl_coords[blk][1],
                                   cpl_coords[blk][1],
                                   s->fbw_channels * 16);
        s->ac3dsp.extract_exponents(block->cpl_coord_exp[1],
                                    fixed_cpl_coords[blk][1],
                                    s->fbw_channels * 16);

        for (ch = 1; ch <= s->fbw_channels; ch++) {
            int bnd, min_exp, max_exp, master_exp;

            /* determine master exponent */
            min_exp = max_exp = block->cpl_coord_exp[ch][0];
            for (bnd = 1; bnd < s->num_cpl_bands; bnd++) {
                int exp = block->cpl_coord_exp[ch][bnd];
                min_exp = FFMIN(exp, min_exp);
                max_exp = FFMAX(exp, max_exp);
            }
            master_exp = ((max_exp - 15) + 2) / 3;
            master_exp = FFMAX(master_exp, 0);
            while (min_exp < master_exp * 3)
                master_exp--;
            for (bnd = 0; bnd < s->num_cpl_bands; bnd++) {
                block->cpl_coord_exp[ch][bnd] = av_clip(block->cpl_coord_exp[ch][bnd] -
                                                        master_exp * 3, 0, 15);
            }
            block->cpl_master_exp[ch] = master_exp;

            /* quantize mantissas */
            for (bnd = 0; bnd < s->num_cpl_bands; bnd++) {
                int cpl_exp  = block->cpl_coord_exp[ch][bnd];
                int cpl_mant = (fixed_cpl_coords[blk][ch][bnd] << (5 + cpl_exp + master_exp * 3)) >> 24;
                if (cpl_exp == 15)
                    cpl_mant >>= 1;
                else
                    cpl_mant -= 16;

                block->cpl_coord_mant[ch][bnd] = cpl_mant;
            }
        }
    }
#endif /* CONFIG_AC3ENC_FLOAT */
}


/**
 * Determine rematrixing flags for each block and band.
 */
static void compute_rematrixing_strategy(AC3EncodeContext *s)
{
    int nb_coefs;
    int blk, bnd, i;
    AC3Block *block, *block0;

    if (s->channel_mode != AC3_CHMODE_STEREO)
        return;

    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        block = &s->blocks[blk];
        block->new_rematrixing_strategy = !blk;

        if (!s->rematrixing_enabled) {
            block0 = block;
            continue;
        }

        block->num_rematrixing_bands = 4;
        if (block->cpl_in_use) {
            block->num_rematrixing_bands -= (s->start_freq[CPL_CH] <= 61);
            block->num_rematrixing_bands -= (s->start_freq[CPL_CH] == 37);
            if (blk && block->num_rematrixing_bands != block0->num_rematrixing_bands)
                block->new_rematrixing_strategy = 1;
        }
        nb_coefs = FFMIN(block->end_freq[1], block->end_freq[2]);

        for (bnd = 0; bnd < block->num_rematrixing_bands; bnd++) {
            /* calculate calculate sum of squared coeffs for one band in one block */
            int start = ff_ac3_rematrix_band_tab[bnd];
            int end   = FFMIN(nb_coefs, ff_ac3_rematrix_band_tab[bnd+1]);
            CoefSumType sum[4] = {0,};
            for (i = start; i < end; i++) {
                CoefType lt = block->mdct_coef[1][i];
                CoefType rt = block->mdct_coef[2][i];
                CoefType md = lt + rt;
                CoefType sd = lt - rt;
                MAC_COEF(sum[0], lt, lt);
                MAC_COEF(sum[1], rt, rt);
                MAC_COEF(sum[2], md, md);
                MAC_COEF(sum[3], sd, sd);
            }

            /* compare sums to determine if rematrixing will be used for this band */
            if (FFMIN(sum[2], sum[3]) < FFMIN(sum[0], sum[1]))
                block->rematrixing_flags[bnd] = 1;
            else
                block->rematrixing_flags[bnd] = 0;

            /* determine if new rematrixing flags will be sent */
            if (blk &&
                block->rematrixing_flags[bnd] != block0->rematrixing_flags[bnd]) {
                block->new_rematrixing_strategy = 1;
            }
        }
        block0 = block;
    }
}


/**
 * Apply stereo rematrixing to coefficients based on rematrixing flags.
 */
static void apply_rematrixing(AC3EncodeContext *s)
{
    int nb_coefs;
    int blk, bnd, i;
    int start, end;
    uint8_t *flags;

    if (!s->rematrixing_enabled)
        return;

    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        AC3Block *block = &s->blocks[blk];
        if (block->new_rematrixing_strategy)
            flags = block->rematrixing_flags;
        nb_coefs = FFMIN(block->end_freq[1], block->end_freq[2]);
        for (bnd = 0; bnd < block->num_rematrixing_bands; bnd++) {
            if (flags[bnd]) {
                start = ff_ac3_rematrix_band_tab[bnd];
                end   = FFMIN(nb_coefs, ff_ac3_rematrix_band_tab[bnd+1]);
                for (i = start; i < end; i++) {
                    int32_t lt = block->fixed_coef[1][i];
                    int32_t rt = block->fixed_coef[2][i];
                    block->fixed_coef[1][i] = (lt + rt) >> 1;
                    block->fixed_coef[2][i] = (lt - rt) >> 1;
                }
            }
        }
    }
}


/**
 * Initialize exponent tables.
 */
static av_cold void exponent_init(AC3EncodeContext *s)
{
    int expstr, i, grpsize;

    for (expstr = EXP_D15-1; expstr <= EXP_D45-1; expstr++) {
        grpsize = 3 << expstr;
        for (i = 12; i < 256; i++) {
            exponent_group_tab[0][expstr][i] = (i + grpsize - 4) / grpsize;
            exponent_group_tab[1][expstr][i] = (i              ) / grpsize;
        }
    }
    /* LFE */
    exponent_group_tab[0][0][7] = 2;
}


/**
 * Extract exponents from the MDCT coefficients.
 * This takes into account the normalization that was done to the input samples
 * by adjusting the exponents by the exponent shift values.
 */
static void extract_exponents(AC3EncodeContext *s)
{
    int blk, ch;

    for (ch = !s->cpl_on; ch <= s->channels; ch++) {
        for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
            AC3Block *block = &s->blocks[blk];
            s->ac3dsp.extract_exponents(block->exp[ch], block->fixed_coef[ch],
                                        AC3_MAX_COEFS);
        }
    }
}


/**
 * Exponent Difference Threshold.
 * New exponents are sent if their SAD exceed this number.
 */
#define EXP_DIFF_THRESHOLD 500


/**
 * Calculate exponent strategies for all channels.
 * Array arrangement is reversed to simplify the per-channel calculation.
 */
static void compute_exp_strategy(AC3EncodeContext *s)
{
    int ch, blk, blk1;

    for (ch = !s->cpl_on; ch <= s->fbw_channels; ch++) {
        uint8_t *exp_strategy = s->exp_strategy[ch];
        uint8_t *exp          = s->blocks[0].exp[ch];
        int exp_diff;

        /* estimate if the exponent variation & decide if they should be
           reused in the next frame */
        exp_strategy[0] = EXP_NEW;
        exp += AC3_MAX_COEFS;
        for (blk = 1; blk < AC3_MAX_BLOCKS; blk++, exp += AC3_MAX_COEFS) {
            if ((ch == CPL_CH && (!s->blocks[blk].cpl_in_use || !s->blocks[blk-1].cpl_in_use)) ||
                (ch  > CPL_CH && (s->blocks[blk].channel_in_cpl[ch] != s->blocks[blk-1].channel_in_cpl[ch]))) {
                exp_strategy[blk] = EXP_NEW;
                continue;
            }
            exp_diff = s->dsp.sad[0](NULL, exp, exp - AC3_MAX_COEFS, 16, 16);
            exp_strategy[blk] = EXP_REUSE;
            if (ch == CPL_CH && exp_diff > (EXP_DIFF_THRESHOLD * (s->blocks[blk].end_freq[ch] - s->start_freq[ch]) / AC3_MAX_COEFS))
                exp_strategy[blk] = EXP_NEW;
            else if (ch > CPL_CH && exp_diff > EXP_DIFF_THRESHOLD)
                exp_strategy[blk] = EXP_NEW;
        }

        /* now select the encoding strategy type : if exponents are often
           recoded, we use a coarse encoding */
        blk = 0;
        while (blk < AC3_MAX_BLOCKS) {
            blk1 = blk + 1;
            while (blk1 < AC3_MAX_BLOCKS && exp_strategy[blk1] == EXP_REUSE)
                blk1++;
            switch (blk1 - blk) {
            case 1:  exp_strategy[blk] = EXP_D45; break;
            case 2:
            case 3:  exp_strategy[blk] = EXP_D25; break;
            default: exp_strategy[blk] = EXP_D15; break;
            }
            blk = blk1;
        }
    }
    if (s->lfe_on) {
        ch = s->lfe_channel;
        s->exp_strategy[ch][0] = EXP_D15;
        for (blk = 1; blk < AC3_MAX_BLOCKS; blk++)
            s->exp_strategy[ch][blk] = EXP_REUSE;
    }
}


/**
 * Update the exponents so that they are the ones the decoder will decode.
 */
static void encode_exponents_blk_ch(uint8_t *exp, int nb_exps, int exp_strategy,
                                    int cpl)
{
    int nb_groups, i, k;

    nb_groups = exponent_group_tab[cpl][exp_strategy-1][nb_exps] * 3;

    /* for each group, compute the minimum exponent */
    switch(exp_strategy) {
    case EXP_D25:
        for (i = 1, k = 1-cpl; i <= nb_groups; i++) {
            uint8_t exp_min = exp[k];
            if (exp[k+1] < exp_min)
                exp_min = exp[k+1];
            exp[i-cpl] = exp_min;
            k += 2;
        }
        break;
    case EXP_D45:
        for (i = 1, k = 1-cpl; i <= nb_groups; i++) {
            uint8_t exp_min = exp[k];
            if (exp[k+1] < exp_min)
                exp_min = exp[k+1];
            if (exp[k+2] < exp_min)
                exp_min = exp[k+2];
            if (exp[k+3] < exp_min)
                exp_min = exp[k+3];
            exp[i-cpl] = exp_min;
            k += 4;
        }
        break;
    }

    /* constraint for DC exponent */
    if (!cpl && exp[0] > 15)
        exp[0] = 15;

    /* decrease the delta between each groups to within 2 so that they can be
       differentially encoded */
    for (i = 1; i <= nb_groups; i++)
        exp[i] = FFMIN(exp[i], exp[i-1] + 2);
    i--;
    while (--i >= 0)
        exp[i] = FFMIN(exp[i], exp[i+1] + 2);

    if (cpl)
        exp[-1] = exp[0] & ~1;

    /* now we have the exponent values the decoder will see */
    switch (exp_strategy) {
    case EXP_D25:
        for (i = nb_groups, k = (nb_groups * 2)-cpl; i > 0; i--) {
            uint8_t exp1 = exp[i-cpl];
            exp[k--] = exp1;
            exp[k--] = exp1;
        }
        break;
    case EXP_D45:
        for (i = nb_groups, k = (nb_groups * 4)-cpl; i > 0; i--) {
            exp[k] = exp[k-1] = exp[k-2] = exp[k-3] = exp[i-cpl];
            k -= 4;
        }
        break;
    }
}


/**
 * Encode exponents from original extracted form to what the decoder will see.
 * This copies and groups exponents based on exponent strategy and reduces
 * deltas between adjacent exponent groups so that they can be differentially
 * encoded.
 */
static void encode_exponents(AC3EncodeContext *s)
{
    int blk, blk1, ch, cpl;
    uint8_t *exp, *exp_strategy;
    int nb_coefs, num_reuse_blocks;

    for (ch = !s->cpl_on; ch <= s->channels; ch++) {
        exp          = s->blocks[0].exp[ch] + s->start_freq[ch];
        exp_strategy = s->exp_strategy[ch];

        cpl = (ch == CPL_CH);
        blk = 0;
        while (blk < AC3_MAX_BLOCKS) {
            AC3Block *block = &s->blocks[blk];
            if (cpl && !block->cpl_in_use) {
                exp += AC3_MAX_COEFS;
                blk++;
                continue;
            }
            nb_coefs = block->end_freq[ch] - s->start_freq[ch];
            blk1 = blk + 1;

            /* count the number of EXP_REUSE blocks after the current block
               and set exponent reference block pointers */
            block->exp_ref_block[ch] = block;
            while (blk1 < AC3_MAX_BLOCKS && exp_strategy[blk1] == EXP_REUSE) {
                s->blocks[blk1].exp_ref_block[ch] = block;
                blk1++;
            }
            num_reuse_blocks = blk1 - blk - 1;

            /* for the EXP_REUSE case we select the min of the exponents */
            s->ac3dsp.ac3_exponent_min(exp-s->start_freq[ch], num_reuse_blocks,
                                       AC3_MAX_COEFS);

            encode_exponents_blk_ch(exp, nb_coefs, exp_strategy[blk], cpl);

            exp += AC3_MAX_COEFS * (num_reuse_blocks + 1);
            blk = blk1;
        }
    }
}


/**
 * Group exponents.
 * 3 delta-encoded exponents are in each 7-bit group. The number of groups
 * varies depending on exponent strategy and bandwidth.
 */
static void group_exponents(AC3EncodeContext *s)
{
    int blk, ch, i, cpl;
    int group_size, nb_groups, bit_count;
    uint8_t *p;
    int delta0, delta1, delta2;
    int exp0, exp1;

    bit_count = 0;
    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        AC3Block *block = &s->blocks[blk];
        for (ch = !block->cpl_in_use; ch <= s->channels; ch++) {
            int exp_strategy = s->exp_strategy[ch][blk];
            if (exp_strategy == EXP_REUSE)
                continue;
            cpl = (ch == CPL_CH);
            group_size = exp_strategy + (exp_strategy == EXP_D45);
            nb_groups = exponent_group_tab[cpl][exp_strategy-1][block->end_freq[ch]-s->start_freq[ch]];
            bit_count += 4 + (nb_groups * 7);
            p = block->exp[ch] + s->start_freq[ch] - cpl;

            /* DC exponent */
            exp1 = *p++;
            block->grouped_exp[ch][0] = exp1;

            /* remaining exponents are delta encoded */
            for (i = 1; i <= nb_groups; i++) {
                /* merge three delta in one code */
                exp0   = exp1;
                exp1   = p[0];
                p     += group_size;
                delta0 = exp1 - exp0 + 2;
                av_assert2(delta0 >= 0 && delta0 <= 4);

                exp0   = exp1;
                exp1   = p[0];
                p     += group_size;
                delta1 = exp1 - exp0 + 2;
                av_assert2(delta1 >= 0 && delta1 <= 4);

                exp0   = exp1;
                exp1   = p[0];
                p     += group_size;
                delta2 = exp1 - exp0 + 2;
                av_assert2(delta2 >= 0 && delta2 <= 4);

                block->grouped_exp[ch][i] = ((delta0 * 5 + delta1) * 5) + delta2;
            }
        }
    }

    s->exponent_bits = bit_count;
}


/**
 * Calculate final exponents from the supplied MDCT coefficients and exponent shift.
 * Extract exponents from MDCT coefficients, calculate exponent strategies,
 * and encode final exponents.
 */
static void process_exponents(AC3EncodeContext *s)
{
    extract_exponents(s);

    compute_exp_strategy(s);

    encode_exponents(s);

    group_exponents(s);

    emms_c();
}


/**
 * Count frame bits that are based solely on fixed parameters.
 * This only has to be run once when the encoder is initialized.
 */
static void count_frame_bits_fixed(AC3EncodeContext *s)
{
    static const int frame_bits_inc[8] = { 0, 0, 2, 2, 2, 4, 2, 4 };
    int blk;
    int frame_bits;

    /* assumptions:
     *   no dynamic range codes
     *   bit allocation parameters do not change between blocks
     *   no delta bit allocation
     *   no skipped data
     *   no auxilliary data
     */

    /* header */
    frame_bits = 65;
    frame_bits += frame_bits_inc[s->channel_mode];

    /* audio blocks */
    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        /* block switch flags */
        frame_bits += s->fbw_channels;

        /* dither flags */
        frame_bits += s->fbw_channels;

        /* dynamic range */
        frame_bits++;

        /* exponent strategy */
        frame_bits += 2 * s->fbw_channels;
        if (s->lfe_on)
            frame_bits++;

        /* bit allocation params */
        frame_bits++;
        if (!blk)
            frame_bits += 2 + 2 + 2 + 2 + 3;

        /* delta bit allocation */
        frame_bits++;

        /* skipped data */
        frame_bits++;
    }

    /* auxiliary data */
    frame_bits++;

    /* CRC */
    frame_bits += 1 + 16;

    s->frame_bits_fixed = frame_bits;
}


/**
 * Initialize bit allocation.
 * Set default parameter codes and calculate parameter values.
 */
static void bit_alloc_init(AC3EncodeContext *s)
{
    int ch;

    /* init default parameters */
    s->slow_decay_code = 2;
    s->fast_decay_code = 1;
    s->slow_gain_code  = 1;
    s->db_per_bit_code = 3;
    s->floor_code      = 7;
    for (ch = 0; ch <= s->channels; ch++)
        s->fast_gain_code[ch] = 4;

    /* initial snr offset */
    s->coarse_snr_offset = 40;

    /* compute real values */
    /* currently none of these values change during encoding, so we can just
       set them once at initialization */
    s->bit_alloc.slow_decay = ff_ac3_slow_decay_tab[s->slow_decay_code] >> s->bit_alloc.sr_shift;
    s->bit_alloc.fast_decay = ff_ac3_fast_decay_tab[s->fast_decay_code] >> s->bit_alloc.sr_shift;
    s->bit_alloc.slow_gain  = ff_ac3_slow_gain_tab[s->slow_gain_code];
    s->bit_alloc.db_per_bit = ff_ac3_db_per_bit_tab[s->db_per_bit_code];
    s->bit_alloc.floor      = ff_ac3_floor_tab[s->floor_code];
    s->bit_alloc.cpl_fast_leak = 0;
    s->bit_alloc.cpl_slow_leak = 0;

    count_frame_bits_fixed(s);
}


/**
 * Count the bits used to encode the frame, minus exponents and mantissas.
 * Bits based on fixed parameters have already been counted, so now we just
 * have to add the bits based on parameters that change during encoding.
 */
static void count_frame_bits(AC3EncodeContext *s)
{
    AC3EncOptions *opt = &s->options;
    int blk, ch;
    int frame_bits = 0;

    /* header */
    if (opt->audio_production_info)
        frame_bits += 7;
    if (s->bitstream_id == 6) {
        if (opt->extended_bsi_1)
            frame_bits += 14;
        if (opt->extended_bsi_2)
            frame_bits += 14;
    }

    /* audio blocks */
    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        AC3Block *block = &s->blocks[blk];

        /* coupling strategy */
        frame_bits++;
        if (block->new_cpl_strategy) {
            frame_bits++;
            if (block->cpl_in_use) {
                frame_bits += s->fbw_channels;
                if (s->channel_mode == AC3_CHMODE_STEREO)
                    frame_bits++;
                frame_bits += 4 + 4;
                frame_bits += s->num_cpl_subbands - 1;
            }
        }

        /* coupling coordinates */
        if (block->cpl_in_use) {
            for (ch = 1; ch <= s->fbw_channels; ch++) {
                if (block->channel_in_cpl[ch]) {
                    frame_bits++;
                    if (block->new_cpl_coords) {
                        frame_bits += 2;
                        frame_bits += (4 + 4) * s->num_cpl_bands;
                    }
                }
            }
        }

        /* stereo rematrixing */
        if (s->channel_mode == AC3_CHMODE_STEREO) {
            frame_bits++;
            if (s->blocks[blk].new_rematrixing_strategy)
                frame_bits += block->num_rematrixing_bands;
        }

        /* bandwidth codes & gain range */
        for (ch = 1; ch <= s->fbw_channels; ch++) {
            if (s->exp_strategy[ch][blk] != EXP_REUSE) {
                if (!block->channel_in_cpl[ch])
                    frame_bits += 6;
                frame_bits += 2;
            }
        }

        /* coupling exponent strategy */
        if (block->cpl_in_use)
            frame_bits += 2;

        /* snr offsets and fast gain codes */
        frame_bits++;
        if (block->new_snr_offsets)
            frame_bits += 6 + (s->channels + block->cpl_in_use) * (4 + 3);

        /* coupling leak info */
        if (block->cpl_in_use) {
            frame_bits++;
            if (block->new_cpl_leak)
                frame_bits += 3 + 3;
        }
    }

    s->frame_bits = s->frame_bits_fixed + frame_bits;
}


/**
 * Finalize the mantissa bit count by adding in the grouped mantissas.
 */
static int compute_mantissa_size_final(int mant_cnt[5])
{
    // bap=1 : 3 mantissas in 5 bits
    int bits = (mant_cnt[1] / 3) * 5;
    // bap=2 : 3 mantissas in 7 bits
    // bap=4 : 2 mantissas in 7 bits
    bits += ((mant_cnt[2] / 3) + (mant_cnt[4] >> 1)) * 7;
    // bap=3 : each mantissa is 3 bits
    bits += mant_cnt[3] * 3;
    return bits;
}


/**
 * Calculate masking curve based on the final exponents.
 * Also calculate the power spectral densities to use in future calculations.
 */
static void bit_alloc_masking(AC3EncodeContext *s)
{
    int blk, ch;

    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        AC3Block *block = &s->blocks[blk];
        for (ch = !block->cpl_in_use; ch <= s->channels; ch++) {
            /* We only need psd and mask for calculating bap.
               Since we currently do not calculate bap when exponent
               strategy is EXP_REUSE we do not need to calculate psd or mask. */
            if (s->exp_strategy[ch][blk] != EXP_REUSE) {
                ff_ac3_bit_alloc_calc_psd(block->exp[ch], s->start_freq[ch],
                                          block->end_freq[ch], block->psd[ch],
                                          block->band_psd[ch]);
                ff_ac3_bit_alloc_calc_mask(&s->bit_alloc, block->band_psd[ch],
                                           s->start_freq[ch], block->end_freq[ch],
                                           ff_ac3_fast_gain_tab[s->fast_gain_code[ch]],
                                           ch == s->lfe_channel,
                                           DBA_NONE, 0, NULL, NULL, NULL,
                                           block->mask[ch]);
            }
        }
    }
}


/**
 * Ensure that bap for each block and channel point to the current bap_buffer.
 * They may have been switched during the bit allocation search.
 */
static void reset_block_bap(AC3EncodeContext *s)
{
    int blk, ch;
    int channels = s->channels + 1;
    if (s->blocks[0].bap[0] == s->bap_buffer)
        return;
    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        for (ch = 0; ch < channels; ch++) {
            s->blocks[blk].bap[ch] = &s->bap_buffer[AC3_MAX_COEFS * (blk * channels + ch)];
        }
    }
}


/**
 * Run the bit allocation with a given SNR offset.
 * This calculates the bit allocation pointers that will be used to determine
 * the quantization of each mantissa.
 * @return the number of bits needed for mantissas if the given SNR offset is
 *         is used.
 */
static int bit_alloc(AC3EncodeContext *s, int snr_offset)
{
    int blk, ch;
    int mantissa_bits;
    int mant_cnt[5];

    snr_offset = (snr_offset - 240) << 2;

    reset_block_bap(s);
    mantissa_bits = 0;
    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        AC3Block *block = &s->blocks[blk];
        AC3Block *ref_block;
        int av_uninit(ch0);
        int got_cpl = !block->cpl_in_use;
        // initialize grouped mantissa counts. these are set so that they are
        // padded to the next whole group size when bits are counted in
        // compute_mantissa_size_final
        mant_cnt[0] = mant_cnt[3] = 0;
        mant_cnt[1] = mant_cnt[2] = 2;
        mant_cnt[4] = 1;
        for (ch = 1; ch <= s->channels; ch++) {
            if (!got_cpl && ch > 1 && block->channel_in_cpl[ch-1]) {
                ch0     = ch - 1;
                ch      = CPL_CH;
                got_cpl = 1;
            }

            /* Currently the only bit allocation parameters which vary across
               blocks within a frame are the exponent values.  We can take
               advantage of that by reusing the bit allocation pointers
               whenever we reuse exponents. */
            ref_block = block->exp_ref_block[ch];
            if (s->exp_strategy[ch][blk] != EXP_REUSE) {
                s->ac3dsp.bit_alloc_calc_bap(ref_block->mask[ch], ref_block->psd[ch],
                                             s->start_freq[ch], block->end_freq[ch],
                                             snr_offset, s->bit_alloc.floor,
                                             ff_ac3_bap_tab, ref_block->bap[ch]);
            }
            mantissa_bits += s->ac3dsp.compute_mantissa_size(mant_cnt,
                                                             ref_block->bap[ch]+s->start_freq[ch],
                                                             block->end_freq[ch]-s->start_freq[ch]);
            if (ch == CPL_CH)
                ch = ch0;
        }
        mantissa_bits += compute_mantissa_size_final(mant_cnt);
    }
    return mantissa_bits;
}


/**
 * Constant bitrate bit allocation search.
 * Find the largest SNR offset that will allow data to fit in the frame.
 */
static int cbr_bit_allocation(AC3EncodeContext *s)
{
    int ch;
    int bits_left;
    int snr_offset, snr_incr;

    bits_left = 8 * s->frame_size - (s->frame_bits + s->exponent_bits);
    if (bits_left < 0)
        return AVERROR(EINVAL);

    snr_offset = s->coarse_snr_offset << 4;

    /* if previous frame SNR offset was 1023, check if current frame can also
       use SNR offset of 1023. if so, skip the search. */
    if ((snr_offset | s->fine_snr_offset[1]) == 1023) {
        if (bit_alloc(s, 1023) <= bits_left)
            return 0;
    }

    while (snr_offset >= 0 &&
           bit_alloc(s, snr_offset) > bits_left) {
        snr_offset -= 64;
    }
    if (snr_offset < 0)
        return AVERROR(EINVAL);

    FFSWAP(uint8_t *, s->bap_buffer, s->bap1_buffer);
    for (snr_incr = 64; snr_incr > 0; snr_incr >>= 2) {
        while (snr_offset + snr_incr <= 1023 &&
               bit_alloc(s, snr_offset + snr_incr) <= bits_left) {
            snr_offset += snr_incr;
            FFSWAP(uint8_t *, s->bap_buffer, s->bap1_buffer);
        }
    }
    FFSWAP(uint8_t *, s->bap_buffer, s->bap1_buffer);
    reset_block_bap(s);

    s->coarse_snr_offset = snr_offset >> 4;
    for (ch = !s->cpl_on; ch <= s->channels; ch++)
        s->fine_snr_offset[ch] = snr_offset & 0xF;

    return 0;
}


/**
 * Downgrade exponent strategies to reduce the bits used by the exponents.
 * This is a fallback for when bit allocation fails with the normal exponent
 * strategies.  Each time this function is run it only downgrades the
 * strategy in 1 channel of 1 block.
 * @return non-zero if downgrade was unsuccessful
 */
static int downgrade_exponents(AC3EncodeContext *s)
{
    int ch, blk;

    for (blk = AC3_MAX_BLOCKS-1; blk >= 0; blk--) {
        for (ch = !s->blocks[blk].cpl_in_use; ch <= s->fbw_channels; ch++) {
            if (s->exp_strategy[ch][blk] == EXP_D15) {
                s->exp_strategy[ch][blk] = EXP_D25;
                return 0;
            }
        }
    }
    for (blk = AC3_MAX_BLOCKS-1; blk >= 0; blk--) {
        for (ch = !s->blocks[blk].cpl_in_use; ch <= s->fbw_channels; ch++) {
            if (s->exp_strategy[ch][blk] == EXP_D25) {
                s->exp_strategy[ch][blk] = EXP_D45;
                return 0;
            }
        }
    }
    /* block 0 cannot reuse exponents, so only downgrade D45 to REUSE if
       the block number > 0 */
    for (blk = AC3_MAX_BLOCKS-1; blk > 0; blk--) {
        for (ch = !s->blocks[blk].cpl_in_use; ch <= s->fbw_channels; ch++) {
            if (s->exp_strategy[ch][blk] > EXP_REUSE) {
                s->exp_strategy[ch][blk] = EXP_REUSE;
                return 0;
            }
        }
    }
    return -1;
}


/**
 * Perform bit allocation search.
 * Finds the SNR offset value that maximizes quality and fits in the specified
 * frame size.  Output is the SNR offset and a set of bit allocation pointers
 * used to quantize the mantissas.
 */
static int compute_bit_allocation(AC3EncodeContext *s)
{
    int ret;

    count_frame_bits(s);

    bit_alloc_masking(s);

    ret = cbr_bit_allocation(s);
    while (ret) {
        /* fallback 1: disable channel coupling */
        if (s->cpl_on) {
            s->cpl_on = 0;
            compute_coupling_strategy(s);
            compute_rematrixing_strategy(s);
            apply_rematrixing(s);
            process_exponents(s);
            ret = compute_bit_allocation(s);
            continue;
        }

        /* fallback 2: downgrade exponents */
        if (!downgrade_exponents(s)) {
            extract_exponents(s);
            encode_exponents(s);
            group_exponents(s);
            ret = compute_bit_allocation(s);
            continue;
        }

        /* fallbacks were not enough... */
        break;
    }

    return ret;
}


/**
 * Symmetric quantization on 'levels' levels.
 */
static inline int sym_quant(int c, int e, int levels)
{
    int v = (((levels * c) >> (24 - e)) + levels) >> 1;
    av_assert2(v >= 0 && v < levels);
    return v;
}


/**
 * Asymmetric quantization on 2^qbits levels.
 */
static inline int asym_quant(int c, int e, int qbits)
{
    int lshift, m, v;

    lshift = e + qbits - 24;
    if (lshift >= 0)
        v = c << lshift;
    else
        v = c >> (-lshift);
    /* rounding */
    v = (v + 1) >> 1;
    m = (1 << (qbits-1));
    if (v >= m)
        v = m - 1;
    av_assert2(v >= -m);
    return v & ((1 << qbits)-1);
}


/**
 * Quantize a set of mantissas for a single channel in a single block.
 */
static void quantize_mantissas_blk_ch(AC3Mant *s, int32_t *fixed_coef,
                                      uint8_t *exp, uint8_t *bap,
                                      uint16_t *qmant, int start_freq,
                                      int end_freq)
{
    int i;

    for (i = start_freq; i < end_freq; i++) {
        int v;
        int c = fixed_coef[i];
        int e = exp[i];
        int b = bap[i];
        switch (b) {
        case 0:
            v = 0;
            break;
        case 1:
            v = sym_quant(c, e, 3);
            switch (s->mant1_cnt) {
            case 0:
                s->qmant1_ptr = &qmant[i];
                v = 9 * v;
                s->mant1_cnt = 1;
                break;
            case 1:
                *s->qmant1_ptr += 3 * v;
                s->mant1_cnt = 2;
                v = 128;
                break;
            default:
                *s->qmant1_ptr += v;
                s->mant1_cnt = 0;
                v = 128;
                break;
            }
            break;
        case 2:
            v = sym_quant(c, e, 5);
            switch (s->mant2_cnt) {
            case 0:
                s->qmant2_ptr = &qmant[i];
                v = 25 * v;
                s->mant2_cnt = 1;
                break;
            case 1:
                *s->qmant2_ptr += 5 * v;
                s->mant2_cnt = 2;
                v = 128;
                break;
            default:
                *s->qmant2_ptr += v;
                s->mant2_cnt = 0;
                v = 128;
                break;
            }
            break;
        case 3:
            v = sym_quant(c, e, 7);
            break;
        case 4:
            v = sym_quant(c, e, 11);
            switch (s->mant4_cnt) {
            case 0:
                s->qmant4_ptr = &qmant[i];
                v = 11 * v;
                s->mant4_cnt = 1;
                break;
            default:
                *s->qmant4_ptr += v;
                s->mant4_cnt = 0;
                v = 128;
                break;
            }
            break;
        case 5:
            v = sym_quant(c, e, 15);
            break;
        case 14:
            v = asym_quant(c, e, 14);
            break;
        case 15:
            v = asym_quant(c, e, 16);
            break;
        default:
            v = asym_quant(c, e, b - 1);
            break;
        }
        qmant[i] = v;
    }
}


/**
 * Quantize mantissas using coefficients, exponents, and bit allocation pointers.
 */
static void quantize_mantissas(AC3EncodeContext *s)
{
    int blk, ch, ch0=0, got_cpl;

    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        AC3Block *block = &s->blocks[blk];
        AC3Block *ref_block;
        AC3Mant m = { 0 };

        got_cpl = !block->cpl_in_use;
        for (ch = 1; ch <= s->channels; ch++) {
            if (!got_cpl && ch > 1 && block->channel_in_cpl[ch-1]) {
                ch0     = ch - 1;
                ch      = CPL_CH;
                got_cpl = 1;
            }
            ref_block = block->exp_ref_block[ch];
            quantize_mantissas_blk_ch(&m, block->fixed_coef[ch],
                                      ref_block->exp[ch],
                                      ref_block->bap[ch], block->qmant[ch],
                                      s->start_freq[ch], block->end_freq[ch]);
            if (ch == CPL_CH)
                ch = ch0;
        }
    }
}


/**
 * Write the AC-3 frame header to the output bitstream.
 */
static void output_frame_header(AC3EncodeContext *s)
{
    AC3EncOptions *opt = &s->options;

    put_bits(&s->pb, 16, 0x0b77);   /* frame header */
    put_bits(&s->pb, 16, 0);        /* crc1: will be filled later */
    put_bits(&s->pb, 2,  s->bit_alloc.sr_code);
    put_bits(&s->pb, 6,  s->frame_size_code + (s->frame_size - s->frame_size_min) / 2);
    put_bits(&s->pb, 5,  s->bitstream_id);
    put_bits(&s->pb, 3,  s->bitstream_mode);
    put_bits(&s->pb, 3,  s->channel_mode);
    if ((s->channel_mode & 0x01) && s->channel_mode != AC3_CHMODE_MONO)
        put_bits(&s->pb, 2, s->center_mix_level);
    if (s->channel_mode & 0x04)
        put_bits(&s->pb, 2, s->surround_mix_level);
    if (s->channel_mode == AC3_CHMODE_STEREO)
        put_bits(&s->pb, 2, opt->dolby_surround_mode);
    put_bits(&s->pb, 1, s->lfe_on); /* LFE */
    put_bits(&s->pb, 5, -opt->dialogue_level);
    put_bits(&s->pb, 1, 0);         /* no compression control word */
    put_bits(&s->pb, 1, 0);         /* no lang code */
    put_bits(&s->pb, 1, opt->audio_production_info);
    if (opt->audio_production_info) {
        put_bits(&s->pb, 5, opt->mixing_level - 80);
        put_bits(&s->pb, 2, opt->room_type);
    }
    put_bits(&s->pb, 1, opt->copyright);
    put_bits(&s->pb, 1, opt->original);
    if (s->bitstream_id == 6) {
        /* alternate bit stream syntax */
        put_bits(&s->pb, 1, opt->extended_bsi_1);
        if (opt->extended_bsi_1) {
            put_bits(&s->pb, 2, opt->preferred_stereo_downmix);
            put_bits(&s->pb, 3, s->ltrt_center_mix_level);
            put_bits(&s->pb, 3, s->ltrt_surround_mix_level);
            put_bits(&s->pb, 3, s->loro_center_mix_level);
            put_bits(&s->pb, 3, s->loro_surround_mix_level);
        }
        put_bits(&s->pb, 1, opt->extended_bsi_2);
        if (opt->extended_bsi_2) {
            put_bits(&s->pb, 2, opt->dolby_surround_ex_mode);
            put_bits(&s->pb, 2, opt->dolby_headphone_mode);
            put_bits(&s->pb, 1, opt->ad_converter_type);
            put_bits(&s->pb, 9, 0);     /* xbsi2 and encinfo : reserved */
        }
    } else {
    put_bits(&s->pb, 1, 0);         /* no time code 1 */
    put_bits(&s->pb, 1, 0);         /* no time code 2 */
    }
    put_bits(&s->pb, 1, 0);         /* no additional bit stream info */
}


/**
 * Write one audio block to the output bitstream.
 */
static void output_audio_block(AC3EncodeContext *s, int blk)
{
    int ch, i, baie, bnd, got_cpl;
    int av_uninit(ch0);
    AC3Block *block = &s->blocks[blk];

    /* block switching */
    for (ch = 0; ch < s->fbw_channels; ch++)
        put_bits(&s->pb, 1, 0);

    /* dither flags */
    for (ch = 0; ch < s->fbw_channels; ch++)
        put_bits(&s->pb, 1, 1);

    /* dynamic range codes */
    put_bits(&s->pb, 1, 0);

    /* channel coupling */
    put_bits(&s->pb, 1, block->new_cpl_strategy);
    if (block->new_cpl_strategy) {
        put_bits(&s->pb, 1, block->cpl_in_use);
        if (block->cpl_in_use) {
            int start_sub, end_sub;
            for (ch = 1; ch <= s->fbw_channels; ch++)
                put_bits(&s->pb, 1, block->channel_in_cpl[ch]);
            if (s->channel_mode == AC3_CHMODE_STEREO)
                put_bits(&s->pb, 1, 0); /* phase flags in use */
            start_sub = (s->start_freq[CPL_CH] - 37) / 12;
            end_sub   = (s->cpl_end_freq       - 37) / 12;
            put_bits(&s->pb, 4, start_sub);
            put_bits(&s->pb, 4, end_sub - 3);
            for (bnd = start_sub+1; bnd < end_sub; bnd++)
                put_bits(&s->pb, 1, ff_eac3_default_cpl_band_struct[bnd]);
        }
    }

    /* coupling coordinates */
    if (block->cpl_in_use) {
        for (ch = 1; ch <= s->fbw_channels; ch++) {
            if (block->channel_in_cpl[ch]) {
                put_bits(&s->pb, 1, block->new_cpl_coords);
                if (block->new_cpl_coords) {
                    put_bits(&s->pb, 2, block->cpl_master_exp[ch]);
                    for (bnd = 0; bnd < s->num_cpl_bands; bnd++) {
                        put_bits(&s->pb, 4, block->cpl_coord_exp [ch][bnd]);
                        put_bits(&s->pb, 4, block->cpl_coord_mant[ch][bnd]);
                    }
                }
            }
        }
    }

    /* stereo rematrixing */
    if (s->channel_mode == AC3_CHMODE_STEREO) {
        put_bits(&s->pb, 1, block->new_rematrixing_strategy);
        if (block->new_rematrixing_strategy) {
            /* rematrixing flags */
            for (bnd = 0; bnd < block->num_rematrixing_bands; bnd++)
                put_bits(&s->pb, 1, block->rematrixing_flags[bnd]);
        }
    }

    /* exponent strategy */
    for (ch = !block->cpl_in_use; ch <= s->fbw_channels; ch++)
        put_bits(&s->pb, 2, s->exp_strategy[ch][blk]);
    if (s->lfe_on)
        put_bits(&s->pb, 1, s->exp_strategy[s->lfe_channel][blk]);

    /* bandwidth */
    for (ch = 1; ch <= s->fbw_channels; ch++) {
        if (s->exp_strategy[ch][blk] != EXP_REUSE && !block->channel_in_cpl[ch])
            put_bits(&s->pb, 6, s->bandwidth_code);
    }

    /* exponents */
    for (ch = !block->cpl_in_use; ch <= s->channels; ch++) {
        int nb_groups;
        int cpl = (ch == CPL_CH);

        if (s->exp_strategy[ch][blk] == EXP_REUSE)
            continue;

        /* DC exponent */
        put_bits(&s->pb, 4, block->grouped_exp[ch][0] >> cpl);

        /* exponent groups */
        nb_groups = exponent_group_tab[cpl][s->exp_strategy[ch][blk]-1][block->end_freq[ch]-s->start_freq[ch]];
        for (i = 1; i <= nb_groups; i++)
            put_bits(&s->pb, 7, block->grouped_exp[ch][i]);

        /* gain range info */
        if (ch != s->lfe_channel && !cpl)
            put_bits(&s->pb, 2, 0);
    }

    /* bit allocation info */
    baie = (blk == 0);
    put_bits(&s->pb, 1, baie);
    if (baie) {
        put_bits(&s->pb, 2, s->slow_decay_code);
        put_bits(&s->pb, 2, s->fast_decay_code);
        put_bits(&s->pb, 2, s->slow_gain_code);
        put_bits(&s->pb, 2, s->db_per_bit_code);
        put_bits(&s->pb, 3, s->floor_code);
    }

    /* snr offset */
    put_bits(&s->pb, 1, block->new_snr_offsets);
    if (block->new_snr_offsets) {
        put_bits(&s->pb, 6, s->coarse_snr_offset);
        for (ch = !block->cpl_in_use; ch <= s->channels; ch++) {
            put_bits(&s->pb, 4, s->fine_snr_offset[ch]);
            put_bits(&s->pb, 3, s->fast_gain_code[ch]);
        }
    }

    /* coupling leak */
    if (block->cpl_in_use) {
        put_bits(&s->pb, 1, block->new_cpl_leak);
        if (block->new_cpl_leak) {
            put_bits(&s->pb, 3, s->bit_alloc.cpl_fast_leak);
            put_bits(&s->pb, 3, s->bit_alloc.cpl_slow_leak);
        }
    }

    put_bits(&s->pb, 1, 0); /* no delta bit allocation */
    put_bits(&s->pb, 1, 0); /* no data to skip */

    /* mantissas */
    got_cpl = !block->cpl_in_use;
    for (ch = 1; ch <= s->channels; ch++) {
        int b, q;
        AC3Block *ref_block;

        if (!got_cpl && ch > 1 && block->channel_in_cpl[ch-1]) {
            ch0     = ch - 1;
            ch      = CPL_CH;
            got_cpl = 1;
        }
        ref_block = block->exp_ref_block[ch];
        for (i = s->start_freq[ch]; i < block->end_freq[ch]; i++) {
            q = block->qmant[ch][i];
            b = ref_block->bap[ch][i];
            switch (b) {
            case 0:                                         break;
            case 1: if (q != 128) put_bits(&s->pb,   5, q); break;
            case 2: if (q != 128) put_bits(&s->pb,   7, q); break;
            case 3:               put_bits(&s->pb,   3, q); break;
            case 4: if (q != 128) put_bits(&s->pb,   7, q); break;
            case 14:              put_bits(&s->pb,  14, q); break;
            case 15:              put_bits(&s->pb,  16, q); break;
            default:              put_bits(&s->pb, b-1, q); break;
            }
        }
        if (ch == CPL_CH)
            ch = ch0;
    }
}


/** CRC-16 Polynomial */
#define CRC16_POLY ((1 << 0) | (1 << 2) | (1 << 15) | (1 << 16))


static unsigned int mul_poly(unsigned int a, unsigned int b, unsigned int poly)
{
    unsigned int c;

    c = 0;
    while (a) {
        if (a & 1)
            c ^= b;
        a = a >> 1;
        b = b << 1;
        if (b & (1 << 16))
            b ^= poly;
    }
    return c;
}


static unsigned int pow_poly(unsigned int a, unsigned int n, unsigned int poly)
{
    unsigned int r;
    r = 1;
    while (n) {
        if (n & 1)
            r = mul_poly(r, a, poly);
        a = mul_poly(a, a, poly);
        n >>= 1;
    }
    return r;
}


/**
 * Fill the end of the frame with 0's and compute the two CRCs.
 */
static void output_frame_end(AC3EncodeContext *s)
{
    const AVCRC *crc_ctx = av_crc_get_table(AV_CRC_16_ANSI);
    int frame_size_58, pad_bytes, crc1, crc2_partial, crc2, crc_inv;
    uint8_t *frame;

    frame_size_58 = ((s->frame_size >> 2) + (s->frame_size >> 4)) << 1;

    /* pad the remainder of the frame with zeros */
    av_assert2(s->frame_size * 8 - put_bits_count(&s->pb) >= 18);
    flush_put_bits(&s->pb);
    frame = s->pb.buf;
    pad_bytes = s->frame_size - (put_bits_ptr(&s->pb) - frame) - 2;
    av_assert2(pad_bytes >= 0);
    if (pad_bytes > 0)
        memset(put_bits_ptr(&s->pb), 0, pad_bytes);

    /* compute crc1 */
    /* this is not so easy because it is at the beginning of the data... */
    crc1    = av_bswap16(av_crc(crc_ctx, 0, frame + 4, frame_size_58 - 4));
    crc_inv = s->crc_inv[s->frame_size > s->frame_size_min];
    crc1    = mul_poly(crc_inv, crc1, CRC16_POLY);
    AV_WB16(frame + 2, crc1);

    /* compute crc2 */
    crc2_partial = av_crc(crc_ctx, 0, frame + frame_size_58,
                          s->frame_size - frame_size_58 - 3);
    crc2 = av_crc(crc_ctx, crc2_partial, frame + s->frame_size - 3, 1);
    /* ensure crc2 does not match sync word by flipping crcrsv bit if needed */
    if (crc2 == 0x770B) {
        frame[s->frame_size - 3] ^= 0x1;
        crc2 = av_crc(crc_ctx, crc2_partial, frame + s->frame_size - 3, 1);
    }
    crc2 = av_bswap16(crc2);
    AV_WB16(frame + s->frame_size - 2, crc2);
}


/**
 * Write the frame to the output bitstream.
 */
static void output_frame(AC3EncodeContext *s, unsigned char *frame)
{
    int blk;

    init_put_bits(&s->pb, frame, AC3_MAX_CODED_FRAME_SIZE);

    output_frame_header(s);

    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++)
        output_audio_block(s, blk);

    output_frame_end(s);
}


static void dprint_options(AVCodecContext *avctx)
{
#ifdef DEBUG
    AC3EncodeContext *s = avctx->priv_data;
    AC3EncOptions *opt = &s->options;
    char strbuf[32];

    switch (s->bitstream_id) {
    case  6:  av_strlcpy(strbuf, "AC-3 (alt syntax)", 32);      break;
    case  8:  av_strlcpy(strbuf, "AC-3 (standard)", 32);        break;
    case  9:  av_strlcpy(strbuf, "AC-3 (dnet half-rate)", 32);  break;
    case 10:  av_strlcpy(strbuf, "AC-3 (dnet quater-rate", 32); break;
    default: snprintf(strbuf, 32, "ERROR");
    }
    av_dlog(avctx, "bitstream_id: %s (%d)\n", strbuf, s->bitstream_id);
    av_dlog(avctx, "sample_fmt: %s\n", av_get_sample_fmt_name(avctx->sample_fmt));
    av_get_channel_layout_string(strbuf, 32, s->channels, avctx->channel_layout);
    av_dlog(avctx, "channel_layout: %s\n", strbuf);
    av_dlog(avctx, "sample_rate: %d\n", s->sample_rate);
    av_dlog(avctx, "bit_rate: %d\n", s->bit_rate);
    if (s->cutoff)
        av_dlog(avctx, "cutoff: %d\n", s->cutoff);

    av_dlog(avctx, "per_frame_metadata: %s\n",
            opt->allow_per_frame_metadata?"on":"off");
    if (s->has_center)
        av_dlog(avctx, "center_mixlev: %0.3f (%d)\n", opt->center_mix_level,
                s->center_mix_level);
    else
        av_dlog(avctx, "center_mixlev: {not written}\n");
    if (s->has_surround)
        av_dlog(avctx, "surround_mixlev: %0.3f (%d)\n", opt->surround_mix_level,
                s->surround_mix_level);
    else
        av_dlog(avctx, "surround_mixlev: {not written}\n");
    if (opt->audio_production_info) {
        av_dlog(avctx, "mixing_level: %ddB\n", opt->mixing_level);
        switch (opt->room_type) {
        case 0:  av_strlcpy(strbuf, "notindicated", 32); break;
        case 1:  av_strlcpy(strbuf, "large", 32);        break;
        case 2:  av_strlcpy(strbuf, "small", 32);        break;
        default: snprintf(strbuf, 32, "ERROR (%d)", opt->room_type);
        }
        av_dlog(avctx, "room_type: %s\n", strbuf);
    } else {
        av_dlog(avctx, "mixing_level: {not written}\n");
        av_dlog(avctx, "room_type: {not written}\n");
    }
    av_dlog(avctx, "copyright: %s\n", opt->copyright?"on":"off");
    av_dlog(avctx, "dialnorm: %ddB\n", opt->dialogue_level);
    if (s->channel_mode == AC3_CHMODE_STEREO) {
        switch (opt->dolby_surround_mode) {
        case 0:  av_strlcpy(strbuf, "notindicated", 32); break;
        case 1:  av_strlcpy(strbuf, "on", 32);           break;
        case 2:  av_strlcpy(strbuf, "off", 32);          break;
        default: snprintf(strbuf, 32, "ERROR (%d)", opt->dolby_surround_mode);
        }
        av_dlog(avctx, "dsur_mode: %s\n", strbuf);
    } else {
        av_dlog(avctx, "dsur_mode: {not written}\n");
    }
    av_dlog(avctx, "original: %s\n", opt->original?"on":"off");

    if (s->bitstream_id == 6) {
        if (opt->extended_bsi_1) {
            switch (opt->preferred_stereo_downmix) {
            case 0:  av_strlcpy(strbuf, "notindicated", 32); break;
            case 1:  av_strlcpy(strbuf, "ltrt", 32);         break;
            case 2:  av_strlcpy(strbuf, "loro", 32);         break;
            default: snprintf(strbuf, 32, "ERROR (%d)", opt->preferred_stereo_downmix);
            }
            av_dlog(avctx, "dmix_mode: %s\n", strbuf);
            av_dlog(avctx, "ltrt_cmixlev: %0.3f (%d)\n",
                    opt->ltrt_center_mix_level, s->ltrt_center_mix_level);
            av_dlog(avctx, "ltrt_surmixlev: %0.3f (%d)\n",
                    opt->ltrt_surround_mix_level, s->ltrt_surround_mix_level);
            av_dlog(avctx, "loro_cmixlev: %0.3f (%d)\n",
                    opt->loro_center_mix_level, s->loro_center_mix_level);
            av_dlog(avctx, "loro_surmixlev: %0.3f (%d)\n",
                    opt->loro_surround_mix_level, s->loro_surround_mix_level);
        } else {
            av_dlog(avctx, "extended bitstream info 1: {not written}\n");
        }
        if (opt->extended_bsi_2) {
            switch (opt->dolby_surround_ex_mode) {
            case 0:  av_strlcpy(strbuf, "notindicated", 32); break;
            case 1:  av_strlcpy(strbuf, "on", 32);           break;
            case 2:  av_strlcpy(strbuf, "off", 32);          break;
            default: snprintf(strbuf, 32, "ERROR (%d)", opt->dolby_surround_ex_mode);
            }
            av_dlog(avctx, "dsurex_mode: %s\n", strbuf);
            switch (opt->dolby_headphone_mode) {
            case 0:  av_strlcpy(strbuf, "notindicated", 32); break;
            case 1:  av_strlcpy(strbuf, "on", 32);           break;
            case 2:  av_strlcpy(strbuf, "off", 32);          break;
            default: snprintf(strbuf, 32, "ERROR (%d)", opt->dolby_headphone_mode);
            }
            av_dlog(avctx, "dheadphone_mode: %s\n", strbuf);

            switch (opt->ad_converter_type) {
            case 0:  av_strlcpy(strbuf, "standard", 32); break;
            case 1:  av_strlcpy(strbuf, "hdcd", 32);     break;
            default: snprintf(strbuf, 32, "ERROR (%d)", opt->ad_converter_type);
            }
            av_dlog(avctx, "ad_conv_type: %s\n", strbuf);
        } else {
            av_dlog(avctx, "extended bitstream info 2: {not written}\n");
        }
    }
#endif
}


#define FLT_OPTION_THRESHOLD 0.01

static int validate_float_option(float v, const float *v_list, int v_list_size)
{
    int i;

    for (i = 0; i < v_list_size; i++) {
        if (v < (v_list[i] + FLT_OPTION_THRESHOLD) &&
            v > (v_list[i] - FLT_OPTION_THRESHOLD))
            break;
    }
    if (i == v_list_size)
        return -1;

    return i;
}


static void validate_mix_level(void *log_ctx, const char *opt_name,
                               float *opt_param, const float *list,
                               int list_size, int default_value, int min_value,
                               int *ctx_param)
{
    int mixlev = validate_float_option(*opt_param, list, list_size);
    if (mixlev < min_value) {
        mixlev = default_value;
        if (*opt_param >= 0.0) {
            av_log(log_ctx, AV_LOG_WARNING, "requested %s is not valid. using "
                   "default value: %0.3f\n", opt_name, list[mixlev]);
        }
    }
    *opt_param = list[mixlev];
    *ctx_param = mixlev;
}


/**
 * Validate metadata options as set by AVOption system.
 * These values can optionally be changed per-frame.
 */
static int validate_metadata(AVCodecContext *avctx)
{
    AC3EncodeContext *s = avctx->priv_data;
    AC3EncOptions *opt = &s->options;

    /* validate mixing levels */
    if (s->has_center) {
        validate_mix_level(avctx, "center_mix_level", &opt->center_mix_level,
                           cmixlev_options, CMIXLEV_NUM_OPTIONS, 1, 0,
                           &s->center_mix_level);
    }
    if (s->has_surround) {
        validate_mix_level(avctx, "surround_mix_level", &opt->surround_mix_level,
                           surmixlev_options, SURMIXLEV_NUM_OPTIONS, 1, 0,
                           &s->surround_mix_level);
    }

    /* set audio production info flag */
    if (opt->mixing_level >= 0 || opt->room_type >= 0) {
        if (opt->mixing_level < 0) {
            av_log(avctx, AV_LOG_ERROR, "mixing_level must be set if "
                   "room_type is set\n");
            return AVERROR(EINVAL);
        }
        if (opt->mixing_level < 80) {
            av_log(avctx, AV_LOG_ERROR, "invalid mixing level. must be between "
                   "80dB and 111dB\n");
            return AVERROR(EINVAL);
        }
        /* default room type */
        if (opt->room_type < 0)
            opt->room_type = 0;
        opt->audio_production_info = 1;
    } else {
        opt->audio_production_info = 0;
    }

    /* set extended bsi 1 flag */
    if ((s->has_center || s->has_surround) &&
        (opt->preferred_stereo_downmix >= 0 ||
         opt->ltrt_center_mix_level   >= 0 ||
         opt->ltrt_surround_mix_level >= 0 ||
         opt->loro_center_mix_level   >= 0 ||
         opt->loro_surround_mix_level >= 0)) {
        /* default preferred stereo downmix */
        if (opt->preferred_stereo_downmix < 0)
            opt->preferred_stereo_downmix = 0;
        /* validate Lt/Rt center mix level */
        validate_mix_level(avctx, "ltrt_center_mix_level",
                           &opt->ltrt_center_mix_level, extmixlev_options,
                           EXTMIXLEV_NUM_OPTIONS, 5, 0,
                           &s->ltrt_center_mix_level);
        /* validate Lt/Rt surround mix level */
        validate_mix_level(avctx, "ltrt_surround_mix_level",
                           &opt->ltrt_surround_mix_level, extmixlev_options,
                           EXTMIXLEV_NUM_OPTIONS, 6, 3,
                           &s->ltrt_surround_mix_level);
        /* validate Lo/Ro center mix level */
        validate_mix_level(avctx, "loro_center_mix_level",
                           &opt->loro_center_mix_level, extmixlev_options,
                           EXTMIXLEV_NUM_OPTIONS, 5, 0,
                           &s->loro_center_mix_level);
        /* validate Lo/Ro surround mix level */
        validate_mix_level(avctx, "loro_surround_mix_level",
                           &opt->loro_surround_mix_level, extmixlev_options,
                           EXTMIXLEV_NUM_OPTIONS, 6, 3,
                           &s->loro_surround_mix_level);
        opt->extended_bsi_1 = 1;
    } else {
        opt->extended_bsi_1 = 0;
    }

    /* set extended bsi 2 flag */
    if (opt->dolby_surround_ex_mode >= 0 ||
        opt->dolby_headphone_mode   >= 0 ||
        opt->ad_converter_type      >= 0) {
        /* default dolby surround ex mode */
        if (opt->dolby_surround_ex_mode < 0)
            opt->dolby_surround_ex_mode = 0;
        /* default dolby headphone mode */
        if (opt->dolby_headphone_mode < 0)
            opt->dolby_headphone_mode = 0;
        /* default A/D converter type */
        if (opt->ad_converter_type < 0)
            opt->ad_converter_type = 0;
        opt->extended_bsi_2 = 1;
    } else {
        opt->extended_bsi_2 = 0;
    }

    /* set bitstream id for alternate bitstream syntax */
    if (opt->extended_bsi_1 || opt->extended_bsi_2) {
        if (s->bitstream_id > 8 && s->bitstream_id < 11) {
            static int warn_once = 1;
            if (warn_once) {
                av_log(avctx, AV_LOG_WARNING, "alternate bitstream syntax is "
                       "not compatible with reduced samplerates. writing of "
                       "extended bitstream information will be disabled.\n");
                warn_once = 0;
            }
        } else {
            s->bitstream_id = 6;
        }
    }

    return 0;
}


/**
 * Encode a single AC-3 frame.
 */
static int ac3_encode_frame(AVCodecContext *avctx, unsigned char *frame,
                            int buf_size, void *data)
{
    AC3EncodeContext *s = avctx->priv_data;
    const SampleType *samples = data;
    int ret;

    if (s->options.allow_per_frame_metadata) {
        ret = validate_metadata(avctx);
        if (ret)
            return ret;
    }

    if (s->bit_alloc.sr_code == 1)
        adjust_frame_size(s);

    deinterleave_input_samples(s, samples);

    apply_mdct(s);

    scale_coefficients(s);

    s->cpl_on = s->cpl_enabled;
    compute_coupling_strategy(s);

    if (s->cpl_on)
        apply_channel_coupling(s);

    compute_rematrixing_strategy(s);

    apply_rematrixing(s);

    process_exponents(s);

    ret = compute_bit_allocation(s);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR, "Bit allocation failed. Try increasing the bitrate.\n");
        return ret;
    }

    quantize_mantissas(s);

    output_frame(s, frame);

    return s->frame_size;
}


/**
 * Finalize encoding and free any memory allocated by the encoder.
 */
static av_cold int ac3_encode_close(AVCodecContext *avctx)
{
    int blk, ch;
    AC3EncodeContext *s = avctx->priv_data;

    for (ch = 0; ch < s->channels; ch++)
        av_freep(&s->planar_samples[ch]);
    av_freep(&s->planar_samples);
    av_freep(&s->bap_buffer);
    av_freep(&s->bap1_buffer);
    av_freep(&s->mdct_coef_buffer);
    av_freep(&s->fixed_coef_buffer);
    av_freep(&s->exp_buffer);
    av_freep(&s->grouped_exp_buffer);
    av_freep(&s->psd_buffer);
    av_freep(&s->band_psd_buffer);
    av_freep(&s->mask_buffer);
    av_freep(&s->qmant_buffer);
    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        AC3Block *block = &s->blocks[blk];
        av_freep(&block->bap);
        av_freep(&block->mdct_coef);
        av_freep(&block->fixed_coef);
        av_freep(&block->exp);
        av_freep(&block->grouped_exp);
        av_freep(&block->psd);
        av_freep(&block->band_psd);
        av_freep(&block->mask);
        av_freep(&block->qmant);
    }

    mdct_end(&s->mdct);

    av_freep(&avctx->coded_frame);
    return 0;
}


/**
 * Set channel information during initialization.
 */
static av_cold int set_channel_info(AC3EncodeContext *s, int channels,
                                    int64_t *channel_layout)
{
    int ch_layout;

    if (channels < 1 || channels > AC3_MAX_CHANNELS)
        return AVERROR(EINVAL);
    if ((uint64_t)*channel_layout > 0x7FF)
        return AVERROR(EINVAL);
    ch_layout = *channel_layout;
    if (!ch_layout)
        ch_layout = avcodec_guess_channel_layout(channels, CODEC_ID_AC3, NULL);

    s->lfe_on       = !!(ch_layout & AV_CH_LOW_FREQUENCY);
    s->channels     = channels;
    s->fbw_channels = channels - s->lfe_on;
    s->lfe_channel  = s->lfe_on ? s->fbw_channels + 1 : -1;
    if (s->lfe_on)
        ch_layout -= AV_CH_LOW_FREQUENCY;

    switch (ch_layout) {
    case AV_CH_LAYOUT_MONO:           s->channel_mode = AC3_CHMODE_MONO;   break;
    case AV_CH_LAYOUT_STEREO:         s->channel_mode = AC3_CHMODE_STEREO; break;
    case AV_CH_LAYOUT_SURROUND:       s->channel_mode = AC3_CHMODE_3F;     break;
    case AV_CH_LAYOUT_2_1:            s->channel_mode = AC3_CHMODE_2F1R;   break;
    case AV_CH_LAYOUT_4POINT0:        s->channel_mode = AC3_CHMODE_3F1R;   break;
    case AV_CH_LAYOUT_QUAD:
    case AV_CH_LAYOUT_2_2:            s->channel_mode = AC3_CHMODE_2F2R;   break;
    case AV_CH_LAYOUT_5POINT0:
    case AV_CH_LAYOUT_5POINT0_BACK:   s->channel_mode = AC3_CHMODE_3F2R;   break;
    default:
        return AVERROR(EINVAL);
    }
    s->has_center   = (s->channel_mode & 0x01) && s->channel_mode != AC3_CHMODE_MONO;
    s->has_surround =  s->channel_mode & 0x04;

    s->channel_map  = ff_ac3_enc_channel_map[s->channel_mode][s->lfe_on];
    *channel_layout = ch_layout;
    if (s->lfe_on)
        *channel_layout |= AV_CH_LOW_FREQUENCY;

    return 0;
}


static av_cold int validate_options(AVCodecContext *avctx, AC3EncodeContext *s)
{
    int i, ret;

    /* validate channel layout */
    if (!avctx->channel_layout) {
        av_log(avctx, AV_LOG_WARNING, "No channel layout specified. The "
                                      "encoder will guess the layout, but it "
                                      "might be incorrect.\n");
    }
    ret = set_channel_info(s, avctx->channels, &avctx->channel_layout);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR, "invalid channel layout\n");
        return ret;
    }

    /* validate sample rate */
    for (i = 0; i < 9; i++) {
        if ((ff_ac3_sample_rate_tab[i / 3] >> (i % 3)) == avctx->sample_rate)
            break;
    }
    if (i == 9) {
        av_log(avctx, AV_LOG_ERROR, "invalid sample rate\n");
        return AVERROR(EINVAL);
    }
    s->sample_rate        = avctx->sample_rate;
    s->bit_alloc.sr_shift = i % 3;
    s->bit_alloc.sr_code  = i / 3;
    s->bitstream_id       = 8 + s->bit_alloc.sr_shift;

    /* validate bit rate */
    for (i = 0; i < 19; i++) {
        if ((ff_ac3_bitrate_tab[i] >> s->bit_alloc.sr_shift)*1000 == avctx->bit_rate)
            break;
    }
    if (i == 19) {
        av_log(avctx, AV_LOG_ERROR, "invalid bit rate\n");
        return AVERROR(EINVAL);
    }
    s->bit_rate        = avctx->bit_rate;
    s->frame_size_code = i << 1;

    /* validate cutoff */
    if (avctx->cutoff < 0) {
        av_log(avctx, AV_LOG_ERROR, "invalid cutoff frequency\n");
        return AVERROR(EINVAL);
    }
    s->cutoff = avctx->cutoff;
    if (s->cutoff > (s->sample_rate >> 1))
        s->cutoff = s->sample_rate >> 1;

    /* validate audio service type / channels combination */
    if ((avctx->audio_service_type == AV_AUDIO_SERVICE_TYPE_KARAOKE &&
         avctx->channels == 1) ||
        ((avctx->audio_service_type == AV_AUDIO_SERVICE_TYPE_COMMENTARY ||
          avctx->audio_service_type == AV_AUDIO_SERVICE_TYPE_EMERGENCY  ||
          avctx->audio_service_type == AV_AUDIO_SERVICE_TYPE_VOICE_OVER)
         && avctx->channels > 1)) {
        av_log(avctx, AV_LOG_ERROR, "invalid audio service type for the "
                                    "specified number of channels\n");
        return AVERROR(EINVAL);
    }

    ret = validate_metadata(avctx);
    if (ret)
        return ret;

    s->rematrixing_enabled = s->options.stereo_rematrixing &&
                             (s->channel_mode == AC3_CHMODE_STEREO);

    s->cpl_enabled = s->options.channel_coupling &&
                     s->channel_mode >= AC3_CHMODE_STEREO &&
                     CONFIG_AC3ENC_FLOAT;

    return 0;
}


/**
 * Set bandwidth for all channels.
 * The user can optionally supply a cutoff frequency. Otherwise an appropriate
 * default value will be used.
 */
static av_cold void set_bandwidth(AC3EncodeContext *s)
{
    int blk, ch;
    int av_uninit(cpl_start);

    if (s->cutoff) {
        /* calculate bandwidth based on user-specified cutoff frequency */
        int fbw_coeffs;
        fbw_coeffs     = s->cutoff * 2 * AC3_MAX_COEFS / s->sample_rate;
        s->bandwidth_code = av_clip((fbw_coeffs - 73) / 3, 0, 60);
    } else {
        /* use default bandwidth setting */
        s->bandwidth_code = ac3_bandwidth_tab[s->fbw_channels-1][s->bit_alloc.sr_code][s->frame_size_code/2];
    }

    /* set number of coefficients for each channel */
    for (ch = 1; ch <= s->fbw_channels; ch++) {
        s->start_freq[ch] = 0;
        for (blk = 0; blk < AC3_MAX_BLOCKS; blk++)
            s->blocks[blk].end_freq[ch] = s->bandwidth_code * 3 + 73;
    }
    /* LFE channel always has 7 coefs */
    if (s->lfe_on) {
        s->start_freq[s->lfe_channel] = 0;
        for (blk = 0; blk < AC3_MAX_BLOCKS; blk++)
            s->blocks[blk].end_freq[ch] = 7;
    }

    /* initialize coupling strategy */
    if (s->cpl_enabled) {
        if (s->options.cpl_start >= 0) {
            cpl_start = s->options.cpl_start;
        } else {
            cpl_start = ac3_coupling_start_tab[s->channel_mode-2][s->bit_alloc.sr_code][s->frame_size_code/2];
            if (cpl_start < 0)
                s->cpl_enabled = 0;
        }
    }
    if (s->cpl_enabled) {
        int i, cpl_start_band, cpl_end_band;
        uint8_t *cpl_band_sizes = s->cpl_band_sizes;

        cpl_end_band   = s->bandwidth_code / 4 + 3;
        cpl_start_band = av_clip(cpl_start, 0, FFMIN(cpl_end_band-1, 15));

        s->num_cpl_subbands = cpl_end_band - cpl_start_band;

        s->num_cpl_bands = 1;
        *cpl_band_sizes  = 12;
        for (i = cpl_start_band + 1; i < cpl_end_band; i++) {
            if (ff_eac3_default_cpl_band_struct[i]) {
                *cpl_band_sizes += 12;
            } else {
                s->num_cpl_bands++;
                cpl_band_sizes++;
                *cpl_band_sizes = 12;
            }
        }

        s->start_freq[CPL_CH] = cpl_start_band * 12 + 37;
        s->cpl_end_freq       = cpl_end_band   * 12 + 37;
        for (blk = 0; blk < AC3_MAX_BLOCKS; blk++)
            s->blocks[blk].end_freq[CPL_CH] = s->cpl_end_freq;
    }
}


static av_cold int allocate_buffers(AVCodecContext *avctx)
{
    int blk, ch;
    AC3EncodeContext *s = avctx->priv_data;
    int channels = s->channels + 1; /* includes coupling channel */

    FF_ALLOC_OR_GOTO(avctx, s->planar_samples, s->channels * sizeof(*s->planar_samples),
                     alloc_fail);
    for (ch = 0; ch < s->channels; ch++) {
        FF_ALLOCZ_OR_GOTO(avctx, s->planar_samples[ch],
                          (AC3_FRAME_SIZE+AC3_BLOCK_SIZE) * sizeof(**s->planar_samples),
                          alloc_fail);
    }
    FF_ALLOC_OR_GOTO(avctx, s->bap_buffer,  AC3_MAX_BLOCKS * channels *
                     AC3_MAX_COEFS * sizeof(*s->bap_buffer),  alloc_fail);
    FF_ALLOC_OR_GOTO(avctx, s->bap1_buffer, AC3_MAX_BLOCKS * channels *
                     AC3_MAX_COEFS * sizeof(*s->bap1_buffer), alloc_fail);
    FF_ALLOCZ_OR_GOTO(avctx, s->mdct_coef_buffer, AC3_MAX_BLOCKS * channels *
                      AC3_MAX_COEFS * sizeof(*s->mdct_coef_buffer), alloc_fail);
    FF_ALLOC_OR_GOTO(avctx, s->exp_buffer, AC3_MAX_BLOCKS * channels *
                     AC3_MAX_COEFS * sizeof(*s->exp_buffer), alloc_fail);
    FF_ALLOC_OR_GOTO(avctx, s->grouped_exp_buffer, AC3_MAX_BLOCKS * channels *
                     128 * sizeof(*s->grouped_exp_buffer), alloc_fail);
    FF_ALLOC_OR_GOTO(avctx, s->psd_buffer, AC3_MAX_BLOCKS * channels *
                     AC3_MAX_COEFS * sizeof(*s->psd_buffer), alloc_fail);
    FF_ALLOC_OR_GOTO(avctx, s->band_psd_buffer, AC3_MAX_BLOCKS * channels *
                     64 * sizeof(*s->band_psd_buffer), alloc_fail);
    FF_ALLOC_OR_GOTO(avctx, s->mask_buffer, AC3_MAX_BLOCKS * channels *
                     64 * sizeof(*s->mask_buffer), alloc_fail);
    FF_ALLOC_OR_GOTO(avctx, s->qmant_buffer, AC3_MAX_BLOCKS * channels *
                     AC3_MAX_COEFS * sizeof(*s->qmant_buffer), alloc_fail);
    if (s->cpl_enabled) {
        FF_ALLOC_OR_GOTO(avctx, s->cpl_coord_exp_buffer, AC3_MAX_BLOCKS * channels *
                         16 * sizeof(*s->cpl_coord_exp_buffer), alloc_fail);
        FF_ALLOC_OR_GOTO(avctx, s->cpl_coord_mant_buffer, AC3_MAX_BLOCKS * channels *
                         16 * sizeof(*s->cpl_coord_mant_buffer), alloc_fail);
    }
    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        AC3Block *block = &s->blocks[blk];
        FF_ALLOC_OR_GOTO(avctx, block->bap, channels * sizeof(*block->bap),
                         alloc_fail);
        FF_ALLOCZ_OR_GOTO(avctx, block->mdct_coef, channels * sizeof(*block->mdct_coef),
                          alloc_fail);
        FF_ALLOCZ_OR_GOTO(avctx, block->exp, channels * sizeof(*block->exp),
                          alloc_fail);
        FF_ALLOCZ_OR_GOTO(avctx, block->grouped_exp, channels * sizeof(*block->grouped_exp),
                          alloc_fail);
        FF_ALLOCZ_OR_GOTO(avctx, block->psd, channels * sizeof(*block->psd),
                          alloc_fail);
        FF_ALLOCZ_OR_GOTO(avctx, block->band_psd, channels * sizeof(*block->band_psd),
                          alloc_fail);
        FF_ALLOCZ_OR_GOTO(avctx, block->mask, channels * sizeof(*block->mask),
                          alloc_fail);
        FF_ALLOCZ_OR_GOTO(avctx, block->qmant, channels * sizeof(*block->qmant),
                          alloc_fail);
        if (s->cpl_enabled) {
            FF_ALLOCZ_OR_GOTO(avctx, block->cpl_coord_exp, channels * sizeof(*block->cpl_coord_exp),
                              alloc_fail);
            FF_ALLOCZ_OR_GOTO(avctx, block->cpl_coord_mant, channels * sizeof(*block->cpl_coord_mant),
                              alloc_fail);
        }

        for (ch = 0; ch < channels; ch++) {
            /* arrangement: block, channel, coeff */
            block->bap[ch]         = &s->bap_buffer        [AC3_MAX_COEFS * (blk * channels + ch)];
            block->grouped_exp[ch] = &s->grouped_exp_buffer[128           * (blk * channels + ch)];
            block->psd[ch]         = &s->psd_buffer        [AC3_MAX_COEFS * (blk * channels + ch)];
            block->band_psd[ch]    = &s->band_psd_buffer   [64            * (blk * channels + ch)];
            block->mask[ch]        = &s->mask_buffer       [64            * (blk * channels + ch)];
            block->qmant[ch]       = &s->qmant_buffer      [AC3_MAX_COEFS * (blk * channels + ch)];
            if (s->cpl_enabled) {
                block->cpl_coord_exp[ch]  = &s->cpl_coord_exp_buffer [16  * (blk * channels + ch)];
                block->cpl_coord_mant[ch] = &s->cpl_coord_mant_buffer[16  * (blk * channels + ch)];
            }

            /* arrangement: channel, block, coeff */
            block->exp[ch]         = &s->exp_buffer        [AC3_MAX_COEFS * (AC3_MAX_BLOCKS * ch + blk)];
            block->mdct_coef[ch]   = &s->mdct_coef_buffer  [AC3_MAX_COEFS * (AC3_MAX_BLOCKS * ch + blk)];
        }
    }

    if (CONFIG_AC3ENC_FLOAT) {
        FF_ALLOCZ_OR_GOTO(avctx, s->fixed_coef_buffer, AC3_MAX_BLOCKS * channels *
                          AC3_MAX_COEFS * sizeof(*s->fixed_coef_buffer), alloc_fail);
        for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
            AC3Block *block = &s->blocks[blk];
            FF_ALLOCZ_OR_GOTO(avctx, block->fixed_coef, channels *
                              sizeof(*block->fixed_coef), alloc_fail);
            for (ch = 0; ch < channels; ch++)
                block->fixed_coef[ch] = &s->fixed_coef_buffer[AC3_MAX_COEFS * (AC3_MAX_BLOCKS * ch + blk)];
        }
    } else {
        for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
            AC3Block *block = &s->blocks[blk];
            FF_ALLOCZ_OR_GOTO(avctx, block->fixed_coef, channels *
                              sizeof(*block->fixed_coef), alloc_fail);
            for (ch = 0; ch < channels; ch++)
                block->fixed_coef[ch] = (int32_t *)block->mdct_coef[ch];
        }
    }

    return 0;
alloc_fail:
    return AVERROR(ENOMEM);
}


/**
 * Initialize the encoder.
 */
static av_cold int ac3_encode_init(AVCodecContext *avctx)
{
    AC3EncodeContext *s = avctx->priv_data;
    int ret, frame_size_58;

    avctx->frame_size = AC3_FRAME_SIZE;

    ff_ac3_common_init();

    ret = validate_options(avctx, s);
    if (ret)
        return ret;

    s->bitstream_mode = avctx->audio_service_type;
    if (s->bitstream_mode == AV_AUDIO_SERVICE_TYPE_KARAOKE)
        s->bitstream_mode = 0x7;

    s->frame_size_min  = 2 * ff_ac3_frame_size_tab[s->frame_size_code][s->bit_alloc.sr_code];
    s->bits_written    = 0;
    s->samples_written = 0;
    s->frame_size      = s->frame_size_min;

    /* calculate crc_inv for both possible frame sizes */
    frame_size_58 = (( s->frame_size    >> 2) + ( s->frame_size    >> 4)) << 1;
    s->crc_inv[0] = pow_poly((CRC16_POLY >> 1), (8 * frame_size_58) - 16, CRC16_POLY);
    if (s->bit_alloc.sr_code == 1) {
        frame_size_58 = (((s->frame_size+2) >> 2) + ((s->frame_size+2) >> 4)) << 1;
        s->crc_inv[1] = pow_poly((CRC16_POLY >> 1), (8 * frame_size_58) - 16, CRC16_POLY);
    }

    set_bandwidth(s);

    exponent_init(s);

    bit_alloc_init(s);

    ret = mdct_init(avctx, &s->mdct, 9);
    if (ret)
        goto init_fail;

    ret = allocate_buffers(avctx);
    if (ret)
        goto init_fail;

    avctx->coded_frame= avcodec_alloc_frame();

    dsputil_init(&s->dsp, avctx);
    ff_ac3dsp_init(&s->ac3dsp, avctx->flags & CODEC_FLAG_BITEXACT);

    dprint_options(avctx);

    return 0;
init_fail:
    ac3_encode_close(avctx);
    return ret;
}
