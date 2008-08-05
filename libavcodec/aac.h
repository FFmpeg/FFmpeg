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

#define IVQUANT_SIZE 1024

enum WindowSequence {
    ONLY_LONG_SEQUENCE,
    LONG_START_SEQUENCE,
    EIGHT_SHORT_SEQUENCE,
    LONG_STOP_SEQUENCE,
};

enum ChannelType {
    AAC_CHANNEL_FRONT = 1,
    AAC_CHANNEL_SIDE  = 2,
    AAC_CHANNEL_BACK  = 3,
    AAC_CHANNEL_LFE   = 4,
    AAC_CHANNEL_CC    = 5,
};

/**
 * main AAC context
 */
typedef struct {
    AVCodecContext * avccontext;

    /**
     * @defgroup tables   Computed / set up during initialization.
     * @{
     */
    MDCTContext mdct;
    MDCTContext mdct_small;
    DSPContext dsp;
    /** @} */

    /**
     * @defgroup output   Members used for output interleaving and down-mixing.
     * @{
     */
    float add_bias;                                   ///< offset for dsp.float_to_int16
    float sf_scale;                                   ///< Pre-scale for correct IMDCT and dsp.float_to_int16.
    int sf_offset;                                    ///< offset into pow2sf_tab as appropriate for dsp.float_to_int16
    /** @} */

} AACContext;

#endif /* FFMPEG_AAC_H */
