/*
 * Copyright (c) 2012 Justin Ruggles <justin.ruggles@gmail.com>
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

#ifndef AVRESAMPLE_AUDIO_MIX_H
#define AVRESAMPLE_AUDIO_MIX_H

#include <stdint.h>

#include "libavutil/samplefmt.h"
#include "avresample.h"
#include "audio_data.h"

typedef void (mix_func)(uint8_t **src, void **matrix, int len, int out_ch,
                        int in_ch);

typedef struct AudioMix {
    AVAudioResampleContext *avr;
    enum AVSampleFormat fmt;
    enum AVMixCoeffType coeff_type;
    uint64_t in_layout;
    uint64_t out_layout;
    int in_channels;
    int out_channels;

    int ptr_align;
    int samples_align;
    int has_optimized_func;
    const char *func_descr;
    const char *func_descr_generic;
    mix_func *mix;
    mix_func *mix_generic;

    int16_t *matrix_q8[AVRESAMPLE_MAX_CHANNELS];
    int32_t *matrix_q15[AVRESAMPLE_MAX_CHANNELS];
    float   *matrix_flt[AVRESAMPLE_MAX_CHANNELS];
    void   **matrix;
} AudioMix;

/**
 * Set mixing function if the parameters match.
 *
 * This compares the parameters of the mixing function to the parameters in the
 * AudioMix context. If the parameters do not match, no changes are made to the
 * active functions. If the parameters do match and the alignment is not
 * constrained, the function is set as the generic mixing function. If the
 * parameters match and the alignment is constrained, the function is set as
 * the optimized mixing function.
 *
 * @param am             AudioMix context
 * @param fmt            input/output sample format
 * @param coeff_type     mixing coefficient type
 * @param in_channels    number of input channels, or 0 for any number of channels
 * @param out_channels   number of output channels, or 0 for any number of channels
 * @param ptr_align      buffer pointer alignment, in bytes
 * @param sample_align   buffer size alignment, in samples
 * @param descr          function type description (e.g. "C" or "SSE")
 * @param mix_func       mixing function pointer
 */
void ff_audio_mix_set_func(AudioMix *am, enum AVSampleFormat fmt,
                           enum AVMixCoeffType coeff_type, int in_channels,
                           int out_channels, int ptr_align, int samples_align,
                           const char *descr, void *mix_func);

/**
 * Initialize the AudioMix context in the AVAudioResampleContext.
 *
 * The parameters in the AVAudioResampleContext are used to initialize the
 * AudioMix context and set the mixing matrix.
 *
 * @param avr  AVAudioResampleContext
 * @return     0 on success, negative AVERROR code on failure
 */
int ff_audio_mix_init(AVAudioResampleContext *avr);

/**
 * Close an AudioMix context.
 *
 * This clears and frees the mixing matrix arrays.
 */
void ff_audio_mix_close(AudioMix *am);

/**
 * Apply channel mixing to audio data using the current mixing matrix.
 */
int ff_audio_mix(AudioMix *am, AudioData *src);

/* arch-specific initialization functions */

void ff_audio_mix_init_x86(AudioMix *am);

#endif /* AVRESAMPLE_AUDIO_MIX_H */
