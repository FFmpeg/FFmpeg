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

#ifndef AVRESAMPLE_AUDIO_CONVERT_H
#define AVRESAMPLE_AUDIO_CONVERT_H

#include "libavutil/samplefmt.h"
#include "avresample.h"
#include "internal.h"
#include "audio_data.h"

/**
 * Set conversion function if the parameters match.
 *
 * This compares the parameters of the conversion function to the parameters
 * in the AudioConvert context. If the parameters do not match, no changes are
 * made to the active functions. If the parameters do match and the alignment
 * is not constrained, the function is set as the generic conversion function.
 * If the parameters match and the alignment is constrained, the function is
 * set as the optimized conversion function.
 *
 * @param ac             AudioConvert context
 * @param out_fmt        output sample format
 * @param in_fmt         input sample format
 * @param channels       number of channels, or 0 for any number of channels
 * @param ptr_align      buffer pointer alignment, in bytes
 * @param samples_align  buffer size alignment, in samples
 * @param descr          function type description (e.g. "C" or "SSE")
 * @param conv           conversion function pointer
 */
void ff_audio_convert_set_func(AudioConvert *ac, enum AVSampleFormat out_fmt,
                               enum AVSampleFormat in_fmt, int channels,
                               int ptr_align, int samples_align,
                               const char *descr, void *conv);

/**
 * Allocate and initialize AudioConvert context for sample format conversion.
 *
 * @param avr         AVAudioResampleContext
 * @param out_fmt     output sample format
 * @param in_fmt      input sample format
 * @param channels    number of channels
 * @param sample_rate sample rate (used for dithering)
 * @param apply_map   apply channel map during conversion
 * @return            newly-allocated AudioConvert context
 */
AudioConvert *ff_audio_convert_alloc(AVAudioResampleContext *avr,
                                     enum AVSampleFormat out_fmt,
                                     enum AVSampleFormat in_fmt,
                                     int channels, int sample_rate,
                                     int apply_map);

/**
 * Free AudioConvert.
 *
 * The AudioConvert must have been previously allocated with ff_audio_convert_alloc().
 *
 * @param ac  AudioConvert struct
 */
void ff_audio_convert_free(AudioConvert **ac);

/**
 * Convert audio data from one sample format to another.
 *
 * For each call, the alignment of the input and output AudioData buffers are
 * examined to determine whether to use the generic or optimized conversion
 * function (when available).
 *
 * The number of samples to convert is determined by in->nb_samples. The output
 * buffer must be large enough to handle this many samples. out->nb_samples is
 * set by this function before a successful return.
 *
 * @param ac     AudioConvert context
 * @param out    output audio data
 * @param in     input audio data
 * @return       0 on success, negative AVERROR code on failure
 */
int ff_audio_convert(AudioConvert *ac, AudioData *out, AudioData *in);

/* arch-specific initialization functions */

void ff_audio_convert_init_arm(AudioConvert *ac);
void ff_audio_convert_init_x86(AudioConvert *ac);

#endif /* AVRESAMPLE_AUDIO_CONVERT_H */
