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

typedef struct {
    int object_type;
    int sampling_index;
    int sample_rate;
    int chan_config;
    int sbr; //< -1 implicit, 1 presence
    int ext_object_type;
    int ext_sampling_index;
    int ext_sample_rate;
} MPEG4AudioConfig;

extern const int ff_mpeg4audio_sample_rates[16];
extern const uint8_t ff_mpeg4audio_channels[8];
/**
 * Parse MPEG-4 systems extradata to retrieve audio configuration.
 * @param[in] c        MPEG4AudioConfig structure to fill.
 * @param[in] buf      Extradata from container.
 * @param[in] buf_size Extradata size.
 * @return On error -1 is returned, on success AudioSpecificConfig bit index in extradata.
 */
int ff_mpeg4audio_get_config(MPEG4AudioConfig *c, const uint8_t *buf, int buf_size);

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
    AOT_PS,                    ///< N                       Parametric Stereo
    AOT_SURROUND,              ///< N                       MPEG Surround
    AOT_ESCAPE,                ///< N                       Escape Value
    AOT_L1,                    ///< Y                       Layer 1
    AOT_L2,                    ///< Y                       Layer 2
    AOT_L3,                    ///< Y                       Layer 3
    AOT_DST,                   ///< N                       Direct Stream Transfer
    AOT_ALS,                   ///< N                       Audio LosslesS
    AOT_SLS,                   ///< N                       Scalable LosslesS
    AOT_SLS_NON_CORE,          ///< N                       Scalable LosslesS (non core)
    AOT_ER_AAC_ELD,            ///< N                       Error Resilient Enhanced Low Delay
    AOT_SMR_SIMPLE,            ///< N                       Symbolic Music Representation Simple
    AOT_SMR_MAIN,              ///< N                       Symbolic Music Representation Main
};

#endif /* AVCODEC_MPEG4AUDIO_H */
