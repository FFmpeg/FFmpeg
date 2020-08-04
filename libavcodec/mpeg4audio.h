/*
 * MPEG-4 Audio common header
 * Copyright (c) 2008 Baptiste Coudurier <baptiste.coudurier@free.fr>
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

#ifndef AVCODEC_MPEG4AUDIO_H
#define AVCODEC_MPEG4AUDIO_H

#include <stdint.h>

#include "libavutil/attributes.h"

#include "get_bits.h"
#include "internal.h"
#include "put_bits.h"

typedef struct MPEG4AudioConfig {
    int object_type;
    int sampling_index;
    int sample_rate;
    int chan_config;
    int sbr; ///< -1 implicit, 1 presence
    int ext_object_type;
    int ext_sampling_index;
    int ext_sample_rate;
    int ext_chan_config;
    int channels;
    int ps;  ///< -1 implicit, 1 presence
    int frame_length_short;
} MPEG4AudioConfig;

extern av_export_avcodec const int avpriv_mpeg4audio_sample_rates[16];
extern const uint8_t ff_mpeg4audio_channels[14];

/**
 * Parse MPEG-4 systems extradata from a potentially unaligned GetBitContext to retrieve audio configuration.
 * @param[in] c        MPEG4AudioConfig structure to fill.
 * @param[in] gb       Extradata from container.
 * @param[in] sync_extension look for a sync extension after config if true.
 * @param[in] logctx opaque struct starting with an AVClass element, used for logging.
 * @return negative AVERROR code on error, on success AudioSpecificConfig bit index in extradata.
 */
int ff_mpeg4audio_get_config_gb(MPEG4AudioConfig *c, GetBitContext *gb,
                                int sync_extension, void *logctx);

#if LIBAVCODEC_VERSION_MAJOR < 59
/**
 * Parse MPEG-4 systems extradata from a raw buffer to retrieve audio configuration.
 * @param[in] c        MPEG4AudioConfig structure to fill.
 * @param[in] buf      Extradata from container.
 * @param[in] bit_size Extradata size in bits.
 * @param[in] sync_extension look for a sync extension after config if true.
 * @return negative AVERROR code on error, on success AudioSpecificConfig bit index in extradata.
 */
int avpriv_mpeg4audio_get_config(MPEG4AudioConfig *c, const uint8_t *buf,
                                 int bit_size, int sync_extension);
#endif

/**
 * Parse MPEG-4 systems extradata from a raw buffer to retrieve audio configuration.
 * @param[in] c        MPEG4AudioConfig structure to fill.
 * @param[in] buf      Extradata from container.
 * @param[in] size     Extradata size in bytes.
 * @param[in] sync_extension look for a sync extension after config if true.
 * @param[in] logctx opaque struct starting with an AVClass element, used for logging.
 * @return negative AVERROR code on error, AudioSpecificConfig bit index in extradata on success.
 */
int avpriv_mpeg4audio_get_config2(MPEG4AudioConfig *c, const uint8_t *buf,
                                  int size, int sync_extension, void *logctx);

enum AudioObjectType {
    AOT_NULL,
                               // Support?                Name
    AOT_AAC_MAIN,              ///< Y                       Main
    AOT_AAC_LC,                ///< Y                       Low Complexity
    AOT_AAC_SSR,               ///< N (code in SoC repo)    Scalable Sample Rate
    AOT_AAC_LTP,               ///< Y                       Long Term Prediction
    AOT_SBR,                   ///< Y                       Spectral Band Replication
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
    AOT_PS,                    ///< N                       Parametric Stereo
    AOT_SURROUND,              ///< N                       MPEG Surround
    AOT_ESCAPE,                ///< Y                       Escape Value
    AOT_L1,                    ///< Y                       Layer 1
    AOT_L2,                    ///< Y                       Layer 2
    AOT_L3,                    ///< Y                       Layer 3
    AOT_DST,                   ///< N                       Direct Stream Transfer
    AOT_ALS,                   ///< Y                       Audio LosslesS
    AOT_SLS,                   ///< N                       Scalable LosslesS
    AOT_SLS_NON_CORE,          ///< N                       Scalable LosslesS (non core)
    AOT_ER_AAC_ELD,            ///< N                       Error Resilient Enhanced Low Delay
    AOT_SMR_SIMPLE,            ///< N                       Symbolic Music Representation Simple
    AOT_SMR_MAIN,              ///< N                       Symbolic Music Representation Main
    AOT_USAC_NOSBR,            ///< N                       Unified Speech and Audio Coding (no SBR)
    AOT_SAOC,                  ///< N                       Spatial Audio Object Coding
    AOT_LD_SURROUND,           ///< N                       Low Delay MPEG Surround
    AOT_USAC,                  ///< N                       Unified Speech and Audio Coding
};

#define MAX_PCE_SIZE 320 ///<Maximum size of a PCE including the 3-bit ID_PCE
                         ///<marker and the comment

static av_always_inline unsigned int ff_pce_copy_bits(PutBitContext *pb,
                                                      GetBitContext *gb,
                                                      int bits)
{
    unsigned int el = get_bits(gb, bits);
    put_bits(pb, bits, el);
    return el;
}

static inline int ff_copy_pce_data(PutBitContext *pb, GetBitContext *gb)
{
    int five_bit_ch, four_bit_ch, comment_size, bits;
    int offset = put_bits_count(pb);

    ff_pce_copy_bits(pb, gb, 10);               // Tag, Object Type, Frequency
    five_bit_ch  = ff_pce_copy_bits(pb, gb, 4); // Front
    five_bit_ch += ff_pce_copy_bits(pb, gb, 4); // Side
    five_bit_ch += ff_pce_copy_bits(pb, gb, 4); // Back
    four_bit_ch  = ff_pce_copy_bits(pb, gb, 2); // LFE
    four_bit_ch += ff_pce_copy_bits(pb, gb, 3); // Data
    five_bit_ch += ff_pce_copy_bits(pb, gb, 4); // Coupling
    if (ff_pce_copy_bits(pb, gb, 1))            // Mono Mixdown
        ff_pce_copy_bits(pb, gb, 4);
    if (ff_pce_copy_bits(pb, gb, 1))            // Stereo Mixdown
        ff_pce_copy_bits(pb, gb, 4);
    if (ff_pce_copy_bits(pb, gb, 1))            // Matrix Mixdown
        ff_pce_copy_bits(pb, gb, 3);
    for (bits = five_bit_ch*5+four_bit_ch*4; bits > 16; bits -= 16)
        ff_pce_copy_bits(pb, gb, 16);
    if (bits)
        ff_pce_copy_bits(pb, gb, bits);
    avpriv_align_put_bits(pb);
    align_get_bits(gb);
    comment_size = ff_pce_copy_bits(pb, gb, 8);
    for (; comment_size > 0; comment_size--)
        ff_pce_copy_bits(pb, gb, 8);

    return put_bits_count(pb) - offset;
}

#endif /* AVCODEC_MPEG4AUDIO_H */
