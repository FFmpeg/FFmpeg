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
 * @file aac.h
 * AAC definitions and structures
 * @author Oded Shimon  ( ods15 ods15 dyndns org )
 * @author Maxim Gavrilov ( maxim.gavrilov gmail com )
 */

#ifndef FFMPEG_AAC_H
#define FFMPEG_AAC_H

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

#define IVQUANT_SIZE 1024

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

typedef struct {
    int num_pulse;
    int start;
    int offset[4];
    int amp[4];
} Pulse;

/**
 * coupling parameters
 */
typedef struct {

/**
 * main AAC context
 */
typedef struct {
    AVCodecContext * avccontext;

    MPEG4AudioConfig m4ac;

    int is_saved;                 ///< Set if elements have stored overlap from previous frame.
    DynamicRangeControl che_drc;

    enum ChannelPosition che_pos[4][MAX_ELEM_ID]; /**< channel element channel mapping with the
                                                   *   first index as the first 4 raw data block types
                                                   */

    /**
     * @defgroup tables   Computed / set up during initialization.
     * @{
     */
    MDCTContext mdct;
    MDCTContext mdct_small;
    DSPContext dsp;
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

} AACContext;

#endif /* FFMPEG_AAC_H */
