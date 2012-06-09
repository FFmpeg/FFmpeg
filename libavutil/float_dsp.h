/*
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

#ifndef AVUTIL_FLOAT_DSP_H
#define AVUTIL_FLOAT_DSP_H

typedef struct AVFloatDSPContext {
    /**
     * Calculate the product of two vectors of floats and store the result in
     * a vector of floats.
     *
     * @param dst  output vector
     *             constraints: 32-byte aligned
     * @param src0 first input vector
     *             constraints: 32-byte aligned
     * @param src1 second input vector
     *             constraints: 32-byte aligned
     * @param len  number of elements in the input
     *             constraints: multiple of 16
     */
    void (*vector_fmul)(float *dst, const float *src0, const float *src1,
                        int len);

    /**
     * Multiply a vector of floats by a scalar float and add to
     * destination vector.  Source and destination vectors must
     * overlap exactly or not at all.
     *
     * @param dst result vector
     *            constraints: 32-byte aligned
     * @param src input vector
     *            constraints: 32-byte aligned
     * @param mul scalar value
     * @param len length of vector
     *            constraints: multiple of 16
     */
    void (*vector_fmac_scalar)(float *dst, const float *src, float mul,
                               int len);
} AVFloatDSPContext;

/**
 * Initialize a float DSP context.
 *
 * @param fdsp    float DSP context
 * @param strict  setting to non-zero avoids using functions which may not be IEEE-754 compliant
 */
void avpriv_float_dsp_init(AVFloatDSPContext *fdsp, int strict);


void ff_float_dsp_init_arm(AVFloatDSPContext *fdsp);
void ff_float_dsp_init_ppc(AVFloatDSPContext *fdsp, int strict);
void ff_float_dsp_init_x86(AVFloatDSPContext *fdsp);

#endif /* AVUTIL_FLOAT_DSP_H */
