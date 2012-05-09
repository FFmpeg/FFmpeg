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

#ifndef AVRESAMPLE_AVRESAMPLE_H
#define AVRESAMPLE_AVRESAMPLE_H

/**
 * @file
 * external API header
 */

#include "libavutil/audioconvert.h"
#include "libavutil/avutil.h"
#include "libavutil/dict.h"
#include "libavutil/log.h"

#include "libavresample/version.h"

#define AVRESAMPLE_MAX_CHANNELS 32

typedef struct AVAudioResampleContext AVAudioResampleContext;

/** Mixing Coefficient Types */
enum AVMixCoeffType {
    AV_MIX_COEFF_TYPE_Q8,   /** 16-bit 8.8 fixed-point                      */
    AV_MIX_COEFF_TYPE_Q15,  /** 32-bit 17.15 fixed-point                    */
    AV_MIX_COEFF_TYPE_FLT,  /** floating-point                              */
    AV_MIX_COEFF_TYPE_NB,   /** Number of coeff types. Not part of ABI      */
};

/**
 * Return the LIBAVRESAMPLE_VERSION_INT constant.
 */
unsigned avresample_version(void);

/**
 * Return the libavresample build-time configuration.
 * @return  configure string
 */
const char *avresample_configuration(void);

/**
 * Return the libavresample license.
 */
const char *avresample_license(void);

/**
 * Get the AVClass for AVAudioResampleContext.
 *
 * Can be used in combination with AV_OPT_SEARCH_FAKE_OBJ for examining options
 * without allocating a context.
 *
 * @see av_opt_find().
 *
 * @return AVClass for AVAudioResampleContext
 */
const AVClass *avresample_get_class(void);

/**
 * Allocate AVAudioResampleContext and set options.
 *
 * @return  allocated audio resample context, or NULL on failure
 */
AVAudioResampleContext *avresample_alloc_context(void);

/**
 * Initialize AVAudioResampleContext.
 *
 * @param avr  audio resample context
 * @return     0 on success, negative AVERROR code on failure
 */
int avresample_open(AVAudioResampleContext *avr);

/**
 * Close AVAudioResampleContext.
 *
 * This closes the context, but it does not change the parameters. The context
 * can be reopened with avresample_open(). It does, however, clear the output
 * FIFO and any remaining leftover samples in the resampling delay buffer. If
 * there was a custom matrix being used, that is also cleared.
 *
 * @see avresample_convert()
 * @see avresample_set_matrix()
 *
 * @param avr  audio resample context
 */
void avresample_close(AVAudioResampleContext *avr);

/**
 * Free AVAudioResampleContext and associated AVOption values.
 *
 * This also calls avresample_close() before freeing.
 *
 * @param avr  audio resample context
 */
void avresample_free(AVAudioResampleContext **avr);

/**
 * Generate a channel mixing matrix.
 *
 * This function is the one used internally by libavresample for building the
 * default mixing matrix. It is made public just as a utility function for
 * building custom matrices.
 *
 * @param in_layout           input channel layout
 * @param out_layout          output channel layout
 * @param center_mix_level    mix level for the center channel
 * @param surround_mix_level  mix level for the surround channel(s)
 * @param lfe_mix_level       mix level for the low-frequency effects channel
 * @param normalize           if 1, coefficients will be normalized to prevent
 *                            overflow. if 0, coefficients will not be
 *                            normalized.
 * @param[out] matrix         mixing coefficients; matrix[i + stride * o] is
 *                            the weight of input channel i in output channel o.
 * @param stride              distance between adjacent input channels in the
 *                            matrix array
 * @return                    0 on success, negative AVERROR code on failure
 */
int avresample_build_matrix(uint64_t in_layout, uint64_t out_layout,
                            double center_mix_level, double surround_mix_level,
                            double lfe_mix_level, int normalize, double *matrix,
                            int stride);

/**
 * Get the current channel mixing matrix.
 *
 * @param avr     audio resample context
 * @param matrix  mixing coefficients; matrix[i + stride * o] is the weight of
 *                input channel i in output channel o.
 * @param stride  distance between adjacent input channels in the matrix array
 * @return        0 on success, negative AVERROR code on failure
 */
int avresample_get_matrix(AVAudioResampleContext *avr, double *matrix,
                          int stride);

/**
 * Set channel mixing matrix.
 *
 * Allows for setting a custom mixing matrix, overriding the default matrix
 * generated internally during avresample_open(). This function can be called
 * anytime on an allocated context, either before or after calling
 * avresample_open(). avresample_convert() always uses the current matrix.
 * Calling avresample_close() on the context will clear the current matrix.
 *
 * @see avresample_close()
 *
 * @param avr     audio resample context
 * @param matrix  mixing coefficients; matrix[i + stride * o] is the weight of
 *                input channel i in output channel o.
 * @param stride  distance between adjacent input channels in the matrix array
 * @return        0 on success, negative AVERROR code on failure
 */
int avresample_set_matrix(AVAudioResampleContext *avr, const double *matrix,
                          int stride);

/**
 * Set compensation for resampling.
 *
 * This can be called anytime after avresample_open(). If resampling was not
 * being done previously, the AVAudioResampleContext is closed and reopened
 * with resampling enabled. In this case, any samples remaining in the output
 * FIFO and the current channel mixing matrix will be restored after reopening
 * the context.
 *
 * @param avr                    audio resample context
 * @param sample_delta           compensation delta, in samples
 * @param compensation_distance  compensation distance, in samples
 * @return                       0 on success, negative AVERROR code on failure
 */
int avresample_set_compensation(AVAudioResampleContext *avr, int sample_delta,
                                int compensation_distance);

/**
 * Convert input samples and write them to the output FIFO.
 *
 * The output data can be NULL or have fewer allocated samples than required.
 * In this case, any remaining samples not written to the output will be added
 * to an internal FIFO buffer, to be returned at the next call to this function
 * or to avresample_read().
 *
 * If converting sample rate, there may be data remaining in the internal
 * resampling delay buffer. avresample_get_delay() tells the number of remaining
 * samples. To get this data as output, call avresample_convert() with NULL
 * input.
 *
 * At the end of the conversion process, there may be data remaining in the
 * internal FIFO buffer. avresample_available() tells the number of remaining
 * samples. To get this data as output, either call avresample_convert() with
 * NULL input or call avresample_read().
 *
 * @see avresample_available()
 * @see avresample_read()
 * @see avresample_get_delay()
 *
 * @param avr             audio resample context
 * @param output          output data pointers
 * @param out_plane_size  output plane size, in bytes.
 *                        This can be 0 if unknown, but that will lead to
 *                        optimized functions not being used directly on the
 *                        output, which could slow down some conversions.
 * @param out_samples     maximum number of samples that the output buffer can hold
 * @param input           input data pointers
 * @param in_plane_size   input plane size, in bytes
 *                        This can be 0 if unknown, but that will lead to
 *                        optimized functions not being used directly on the
 *                        input, which could slow down some conversions.
 * @param in_samples      number of input samples to convert
 * @return                number of samples written to the output buffer,
 *                        not including converted samples added to the internal
 *                        output FIFO
 */
int avresample_convert(AVAudioResampleContext *avr, void **output,
                       int out_plane_size, int out_samples, void **input,
                       int in_plane_size, int in_samples);

/**
 * Return the number of samples currently in the resampling delay buffer.
 *
 * When resampling, there may be a delay between the input and output. Any
 * unconverted samples in each call are stored internally in a delay buffer.
 * This function allows the user to determine the current number of samples in
 * the delay buffer, which can be useful for synchronization.
 *
 * @see avresample_convert()
 *
 * @param avr  audio resample context
 * @return     number of samples currently in the resampling delay buffer
 */
int avresample_get_delay(AVAudioResampleContext *avr);

/**
 * Return the number of available samples in the output FIFO.
 *
 * During conversion, if the user does not specify an output buffer or
 * specifies an output buffer that is smaller than what is needed, remaining
 * samples that are not written to the output are stored to an internal FIFO
 * buffer. The samples in the FIFO can be read with avresample_read() or
 * avresample_convert().
 *
 * @see avresample_read()
 * @see avresample_convert()
 *
 * @param avr  audio resample context
 * @return     number of samples available for reading
 */
int avresample_available(AVAudioResampleContext *avr);

/**
 * Read samples from the output FIFO.
 *
 * During conversion, if the user does not specify an output buffer or
 * specifies an output buffer that is smaller than what is needed, remaining
 * samples that are not written to the output are stored to an internal FIFO
 * buffer. This function can be used to read samples from that internal FIFO.
 *
 * @see avresample_available()
 * @see avresample_convert()
 *
 * @param avr         audio resample context
 * @param output      output data pointers. May be NULL, in which case
 *                    nb_samples of data is discarded from output FIFO.
 * @param nb_samples  number of samples to read from the FIFO
 * @return            the number of samples written to output
 */
int avresample_read(AVAudioResampleContext *avr, void **output, int nb_samples);

#endif /* AVRESAMPLE_AVRESAMPLE_H */
