/*
 * Common code between the AC-3 and E-AC-3 decoders
 * Copyright (c) 2007 Bartlomiej Wolowiec <bartek.wolowiec@gmail.com>
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
 * @file libavcodec/ac3.h
 * Common code between the AC-3 and E-AC-3 decoders.
 *
 * Summary of MDCT Coefficient Grouping:
 * The individual MDCT coefficient indices are often referred to in the
 * (E-)AC-3 specification as frequency bins.  These bins are grouped together
 * into subbands of 12 coefficients each.  The subbands are grouped together
 * into bands as defined in the bitstream by the band structures, which
 * determine the number of bands and the size of each band.  The full spectrum
 * of 256 frequency bins is divided into 1 DC bin + 21 subbands = 253 bins.
 * This system of grouping coefficients is used for channel bandwidth, stereo
 * rematrixing, channel coupling, enhanced coupling, and spectral extension.
 *
 * +-+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+-+
 * |1|  |12|  |  [12|12|12|12]  |  |  |  |  |  |  |  |  |  |  |  |  |3|
 * +-+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+-+
 * ~~~  ~~~~     ~~~~~~~~~~~~~                                      ~~~
 *  |     |            |                                             |
 *  |     |            |                    3 unused frequency bins--+
 *  |     |            |
 *  |     |            +--1 band containing 4 subbands
 *  |     |
 *  |     +--1 subband of 12 frequency bins
 *  |
 *  +--DC frequency bin
 */

#ifndef AVCODEC_AC3DEC_H
#define AVCODEC_AC3DEC_H

#include "libavutil/lfg.h"
#include "ac3.h"
#include "get_bits.h"
#include "dsputil.h"
#include "fft.h"

/* override ac3.h to include coupling channel */
#undef AC3_MAX_CHANNELS
#define AC3_MAX_CHANNELS 7
#define CPL_CH 0

#define AC3_OUTPUT_LFEON  8

#define AC3_MAX_COEFS   256
#define AC3_BLOCK_SIZE  256
#define MAX_BLOCKS        6
#define SPX_MAX_BANDS    17

typedef struct {
    AVCodecContext *avctx;                  ///< parent context
    GetBitContext gbc;                      ///< bitstream reader
    uint8_t *input_buffer;                  ///< temp buffer to prevent overread

///@defgroup bsi bit stream information
///@{
    int frame_type;                         ///< frame type                             (strmtyp)
    int substreamid;                        ///< substream identification
    int frame_size;                         ///< current frame size, in bytes
    int bit_rate;                           ///< stream bit rate, in bits-per-second
    int sample_rate;                        ///< sample frequency, in Hz
    int num_blocks;                         ///< number of audio blocks
    int channel_mode;                       ///< channel mode                           (acmod)
    int channel_layout;                     ///< channel layout
    int lfe_on;                             ///< lfe channel in use
    int channel_map;                        ///< custom channel map
    int center_mix_level;                   ///< Center mix level index
    int surround_mix_level;                 ///< Surround mix level index
    int eac3;                               ///< indicates if current frame is E-AC-3
///@}

///@defgroup audfrm frame syntax parameters
    int snr_offset_strategy;                ///< SNR offset strategy                    (snroffststr)
    int block_switch_syntax;                ///< block switch syntax enabled            (blkswe)
    int dither_flag_syntax;                 ///< dither flag syntax enabled             (dithflage)
    int bit_allocation_syntax;              ///< bit allocation model syntax enabled    (bamode)
    int fast_gain_syntax;                   ///< fast gain codes enabled                (frmfgaincode)
    int dba_syntax;                         ///< delta bit allocation syntax enabled    (dbaflde)
    int skip_syntax;                        ///< skip field syntax enabled              (skipflde)
 ///@}

///@defgroup cpl standard coupling
    int cpl_in_use[MAX_BLOCKS];             ///< coupling in use                        (cplinu)
    int cpl_strategy_exists[MAX_BLOCKS];    ///< coupling strategy exists               (cplstre)
    int channel_in_cpl[AC3_MAX_CHANNELS];   ///< channel in coupling                    (chincpl)
    int phase_flags_in_use;                 ///< phase flags in use                     (phsflginu)
    int phase_flags[18];                    ///< phase flags                            (phsflg)
    int num_cpl_bands;                      ///< number of coupling bands               (ncplbnd)
    uint8_t cpl_band_sizes[18];             ///< number of coeffs in each coupling band
    int firstchincpl;                       ///< first channel in coupling
    int first_cpl_coords[AC3_MAX_CHANNELS]; ///< first coupling coordinates states      (firstcplcos)
    int cpl_coords[AC3_MAX_CHANNELS][18];   ///< coupling coordinates                   (cplco)
///@}

///@defgroup spx spectral extension
///@{
    int spx_in_use;                             ///< spectral extension in use              (spxinu)
    uint8_t channel_uses_spx[AC3_MAX_CHANNELS]; ///< channel uses spectral extension        (chinspx)
    int8_t spx_atten_code[AC3_MAX_CHANNELS];    ///< spx attenuation code                   (spxattencod)
    int spx_src_start_freq;                     ///< spx start frequency bin
    int spx_dst_end_freq;                       ///< spx end frequency bin
    int spx_dst_start_freq;                     ///< spx starting frequency bin for copying (copystartmant)
                                                ///< the copy region ends at the start of the spx region.
    int num_spx_bands;                          ///< number of spx bands                    (nspxbnds)
    uint8_t spx_band_sizes[SPX_MAX_BANDS];      ///< number of bins in each spx band
    uint8_t first_spx_coords[AC3_MAX_CHANNELS]; ///< first spx coordinates states           (firstspxcos)
    float spx_noise_blend[AC3_MAX_CHANNELS][SPX_MAX_BANDS]; ///< spx noise blending factor  (nblendfact)
    float spx_signal_blend[AC3_MAX_CHANNELS][SPX_MAX_BANDS];///< spx signal blending factor (sblendfact)
///@}

///@defgroup aht adaptive hybrid transform
    int channel_uses_aht[AC3_MAX_CHANNELS];                         ///< channel AHT in use (chahtinu)
    int pre_mantissa[AC3_MAX_CHANNELS][AC3_MAX_COEFS][MAX_BLOCKS];  ///< pre-IDCT mantissas
///@}

///@defgroup channel channel
    int fbw_channels;                           ///< number of full-bandwidth channels
    int channels;                               ///< number of total channels
    int lfe_ch;                                 ///< index of LFE channel
    float downmix_coeffs[AC3_MAX_CHANNELS][2];  ///< stereo downmix coefficients
    int downmixed;                              ///< indicates if coeffs are currently downmixed
    int output_mode;                            ///< output channel configuration
    int out_channels;                           ///< number of output channels
///@}

///@defgroup dynrng dynamic range
    float dynamic_range[2];                 ///< dynamic range
///@}

///@defgroup bandwidth bandwidth
    int start_freq[AC3_MAX_CHANNELS];       ///< start frequency bin                    (strtmant)
    int end_freq[AC3_MAX_CHANNELS];         ///< end frequency bin                      (endmant)
///@}

///@defgroup rematrixing rematrixing
    int num_rematrixing_bands;              ///< number of rematrixing bands            (nrematbnd)
    int rematrixing_flags[4];               ///< rematrixing flags                      (rematflg)
///@}

///@defgroup exponents exponents
    int num_exp_groups[AC3_MAX_CHANNELS];           ///< Number of exponent groups      (nexpgrp)
    int8_t dexps[AC3_MAX_CHANNELS][AC3_MAX_COEFS];  ///< decoded exponents
    int exp_strategy[MAX_BLOCKS][AC3_MAX_CHANNELS]; ///< exponent strategies            (expstr)
///@}

///@defgroup bitalloc bit allocation
    AC3BitAllocParameters bit_alloc_params;         ///< bit allocation parameters
    int first_cpl_leak;                             ///< first coupling leak state      (firstcplleak)
    int snr_offset[AC3_MAX_CHANNELS];               ///< signal-to-noise ratio offsets  (snroffst)
    int fast_gain[AC3_MAX_CHANNELS];                ///< fast gain values/SMR's         (fgain)
    uint8_t bap[AC3_MAX_CHANNELS][AC3_MAX_COEFS];   ///< bit allocation pointers
    int16_t psd[AC3_MAX_CHANNELS][AC3_MAX_COEFS];   ///< scaled exponents
    int16_t band_psd[AC3_MAX_CHANNELS][50];         ///< interpolated exponents
    int16_t mask[AC3_MAX_CHANNELS][50];             ///< masking curve values
    int dba_mode[AC3_MAX_CHANNELS];                 ///< delta bit allocation mode
    int dba_nsegs[AC3_MAX_CHANNELS];                ///< number of delta segments
    uint8_t dba_offsets[AC3_MAX_CHANNELS][8];       ///< delta segment offsets
    uint8_t dba_lengths[AC3_MAX_CHANNELS][8];       ///< delta segment lengths
    uint8_t dba_values[AC3_MAX_CHANNELS][8];        ///< delta values for each segment
///@}

///@defgroup dithering zero-mantissa dithering
    int dither_flag[AC3_MAX_CHANNELS];      ///< dither flags                           (dithflg)
    AVLFG dith_state;                       ///< for dither generation
///@}

///@defgroup imdct IMDCT
    int block_switch[AC3_MAX_CHANNELS];     ///< block switch flags                     (blksw)
    FFTContext imdct_512;                   ///< for 512 sample IMDCT
    FFTContext imdct_256;                   ///< for 256 sample IMDCT
///@}

///@defgroup opt optimization
    DSPContext dsp;                         ///< for optimization
    float add_bias;                         ///< offset for float_to_int16 conversion
    float mul_bias;                         ///< scaling for float_to_int16 conversion
///@}

///@defgroup arrays aligned arrays
    DECLARE_ALIGNED(16, int,   fixed_coeffs)[AC3_MAX_CHANNELS][AC3_MAX_COEFS];       ///> fixed-point transform coefficients
    DECLARE_ALIGNED(16, float, transform_coeffs)[AC3_MAX_CHANNELS][AC3_MAX_COEFS];   ///< transform coefficients
    DECLARE_ALIGNED(16, float, delay)[AC3_MAX_CHANNELS][AC3_BLOCK_SIZE];             ///< delay - added to the next block
    DECLARE_ALIGNED(16, float, window)[AC3_BLOCK_SIZE];                              ///< window coefficients
    DECLARE_ALIGNED(16, float, tmp_output)[AC3_BLOCK_SIZE];                          ///< temporary storage for output before windowing
    DECLARE_ALIGNED(16, float, output)[AC3_MAX_CHANNELS][AC3_BLOCK_SIZE];            ///< output after imdct transform and windowing
///@}
} AC3DecodeContext;

/**
 * Parse the E-AC-3 frame header.
 * This parses both the bit stream info and audio frame header.
 */
int ff_eac3_parse_header(AC3DecodeContext *s);

/**
 * Decode mantissas in a single channel for the entire frame.
 * This is used when AHT mode is enabled.
 */
void ff_eac3_decode_transform_coeffs_aht_ch(AC3DecodeContext *s, int ch);

void ff_ac3_downmix_c(float (*samples)[256], float (*matrix)[2],
                      int out_ch, int in_ch, int len);

/**
 * Apply spectral extension to each channel by copying lower frequency
 * coefficients to higher frequency bins and applying side information to
 * approximate the original high frequency signal.
 */
void ff_eac3_apply_spectral_extension(AC3DecodeContext *s);

#endif /* AVCODEC_AC3DEC_H */
