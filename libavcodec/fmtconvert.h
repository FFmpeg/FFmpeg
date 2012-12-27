/*
 * Format Conversion Utils
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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

#ifndef AVCODEC_FMTCONVERT_H
#define AVCODEC_FMTCONVERT_H

#include "avcodec.h"

typedef struct FmtConvertContext {
    /**
     * Convert an array of int32_t to float and multiply by a float value.
     * @param dst destination array of float.
     *            constraints: 16-byte aligned
     * @param src source array of int32_t.
     *            constraints: 16-byte aligned
     * @param len number of elements to convert.
     *            constraints: multiple of 8
     */
    void (*int32_to_float_fmul_scalar)(float *dst, const int32_t *src, float mul, int len);

    /**
     * Convert an array of float to an array of int16_t.
     *
     * Convert floats from in the range [-32768.0,32767.0] to ints
     * without rescaling
     *
     * @param dst destination array of int16_t.
     *            constraints: 16-byte aligned
     * @param src source array of float.
     *            constraints: 16-byte aligned
     * @param len number of elements to convert.
     *            constraints: multiple of 8
     */
    void (*float_to_int16)(int16_t *dst, const float *src, long len);

    /**
     * Convert multiple arrays of float to an interleaved array of int16_t.
     *
     * Convert floats from in the range [-32768.0,32767.0] to ints
     * without rescaling
     *
     * @param dst destination array of interleaved int16_t.
     *            constraints: 16-byte aligned
     * @param src source array of float arrays, one for each channel.
     *            constraints: 16-byte aligned
     * @param len number of elements to convert.
     *            constraints: multiple of 8
     * @param channels number of channels
     */
    void (*float_to_int16_interleave)(int16_t *dst, const float **src,
                                      long len, int channels);

    /**
     * Convert multiple arrays of float to an array of interleaved float.
     *
     * @param dst destination array of interleaved float.
     *            constraints: 16-byte aligned
     * @param src source array of float arrays, one for each channel.
     *            constraints: 16-byte aligned
     * @param len number of elements to convert.
     *            constraints: multiple of 8
     * @param channels number of channels
     */
    void (*float_interleave)(float *dst, const float **src, unsigned int len,
                             int channels);
} FmtConvertContext;

void ff_float_interleave_c(float *dst, const float **src, unsigned int len,
                           int channels);

void ff_fmt_convert_init(FmtConvertContext *c, AVCodecContext *avctx);

void ff_fmt_convert_init_arm(FmtConvertContext *c, AVCodecContext *avctx);
void ff_fmt_convert_init_ppc(FmtConvertContext *c, AVCodecContext *avctx);
void ff_fmt_convert_init_x86(FmtConvertContext *c, AVCodecContext *avctx);
void ff_fmt_convert_init_mips(FmtConvertContext *c);

/* ffdshow custom code */
void float_interleave(float *dst, const float **src, long len, int channels);
void float_interleave_noscale(float *dst, const float **src, long len, int channels);

#endif /* AVCODEC_FMTCONVERT_H */
