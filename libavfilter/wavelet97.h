/*
 * Copyright (c) 2007 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2013 Clément Bœsch <u pkh me>
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

#ifndef AVFILTER_WAVELET97_H
#define AVFILTER_WAVELET97_H

/**
 * Undecimated 9/7 wavelet transform
 */

/**
 * Analysis lowpass and highpass filters, both symmetric around tap 0
 */
extern const double ff_wavelet97_coeff[2][5];

/**
 * Undecimated analysis of lines line0..line1-1 along one direction.
 * Line y, sample x is at src[y * ylinesize + x * xlinesize], x < w, w >= 2.
 *
 * @param dst_l    lowpass output, same layout as src
 * @param dst_h    highpass output, same layout as src
 * @param coeff    lowpass and highpass filters, normally ff_wavelet97_coeff
 * @param step     dilation of the filters, 1 << level
 */
void ff_wavelet97_decompose_lines(float *dst_l, float *dst_h, const float *src,
                                  const double coeff[2][5],
                                  int xlinesize, int ylinesize,
                                  int step, int w, int line0, int line1);

/**
 * Inverse of ff_wavelet97_decompose_lines() with ff_wavelet97_coeff.
 */
void ff_wavelet97_compose_lines(float *dst, const float *src_l, const float *src_h,
                                int xlinesize, int ylinesize,
                                int step, int w, int line0, int line1);

/**
 * Decimated analysis of lines line0..line1-1 along one direction.
 * Line y, sample x is at src[y * ystride + x * xstride], x < len, len >= 2.
 * Lowpass coefficients are written to even, highpass ones to odd x of dst.
 *
 * @param coeff    lowpass and highpass filters, normally ff_wavelet97_coeff
 */
void ff_wavelet97_decompose_decimated_lines(float *dst, const float *src,
                                            const double coeff[2][5],
                                            int xstride, int ystride,
                                            int len, int line0, int line1);

/**
 * Inverse of ff_wavelet97_decompose_decimated_lines() with ff_wavelet97_coeff.
 */
void ff_wavelet97_compose_decimated_lines(float *dst, const float *src,
                                          int xstride, int ystride,
                                          int len, int line0, int line1);

/**
 * One level of undecimated 2D analysis, filters are mirrored at the plane borders.
 *
 * @param dst      output bands LL, LH, HL, HH (horizontal/vertical filter)
 * @param src      input plane
 * @param temp     two scratch planes
 * @param coeff    lowpass and highpass filters, normally ff_wavelet97_coeff
 * @param linesize distance between lines in floats, same for all planes
 * @param step     dilation of the filters, 1 << level
 */
void ff_wavelet97_decompose(float *dst[4], const float *src, float *temp[2],
                            const double coeff[2][5],
                            int linesize, int step, int w, int h);

/**
 * One level of 2D synthesis, the inverse of ff_wavelet97_decompose() with
 * ff_wavelet97_coeff.
 */
void ff_wavelet97_compose(float *dst, float *const src[4], float *temp[2],
                          int linesize, int step, int w, int h);

#endif /* AVFILTER_WAVELET97_H */
