/*
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

#ifndef AVUTIL_TX_H
#define AVUTIL_TX_H

#include <stdint.h>
#include <stddef.h>

typedef struct AVTXContext AVTXContext;

typedef struct AVComplexFloat {
    float re, im;
} AVComplexFloat;

enum AVTXType {
    /**
     * Standard complex to complex FFT with sample data type AVComplexFloat.
     * Scaling currently unsupported
     */
    AV_TX_FLOAT_FFT = 0,
    /**
     * Standard MDCT with sample data type of float and a scale type of
     * float. Length is the frame size, not the window size (which is 2x frame)
     */
    AV_TX_FLOAT_MDCT = 1,
};

/**
 * Function pointer to a function to perform the transform.
 *
 * @note Using a different context than the one allocated during av_tx_init()
 * is not allowed.
 *
 * @param s the transform context
 * @param out the output array
 * @param in the input array
 * @param stride the input or output stride (depending on transform direction)
 * in bytes, currently implemented for all MDCT transforms
 */
typedef void (*av_tx_fn)(AVTXContext *s, void *out, void *in, ptrdiff_t stride);

/**
 * Initialize a transform context with the given configuration
 * Currently power of two lengths from 4 to 131072 are supported, along with
 * any length decomposable to a power of two and either 3, 5 or 15.
 *
 * @param ctx the context to allocate, will be NULL on error
 * @param tx pointer to the transform function pointer to set
 * @param type type the type of transform
 * @param inv whether to do an inverse or a forward transform
 * @param len the size of the transform in samples
 * @param scale pointer to the value to scale the output if supported by type
 * @param flags currently unused
 *
 * @return 0 on success, negative error code on failure
 */
int av_tx_init(AVTXContext **ctx, av_tx_fn *tx, enum AVTXType type,
               int inv, int len, const void *scale, uint64_t flags);

/**
 * Frees a context and sets ctx to NULL, does nothing when ctx == NULL
 */
void av_tx_uninit(AVTXContext **ctx);

#endif /* AVUTIL_TX_H */
