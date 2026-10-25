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

#ifndef AVCODEC_APV_DSP_H
#define AVCODEC_APV_DSP_H

#include <stddef.h>
#include <stdint.h>


/**
 * Size of the buffer holding constants for decode_transquant(), which
 * must be large enough for the layout used by any implementation.
 * The buffer must be 32-byte aligned.
 */
#define APV_DSP_CONSTANTS_SIZE 512

typedef struct APVDSPContext {
    /**
     * Fill the constants buffer used by decode_transquant() for all
     * blocks with the same quantisation parameters.  The layout of the
     * buffer is private to the implementation.
     */
    void (*prepare_constants)(void *constants,
                              const int16_t *qmatrix,
                              int bit_depth,
                              int qp_shift);

    /**
     * Run transquant for a single block.  The constants argument must
     * have been prepared by a call to prepare_constants() matching the
     * implementation being used.
     */
    void (*decode_transquant)(void *output,
                              ptrdiff_t pitch,
                              const int16_t *input,
                              const void *constants);
} APVDSPContext;

void ff_apv_dsp_init(APVDSPContext *dsp);

void ff_apv_prepare_constants_c(void *constants,
                                const int16_t *qmatrix,
                                int bit_depth,
                                int qp_shift);

void ff_apv_dsp_init_x86_64(APVDSPContext *dsp);

void ff_apv_dsp_init_aarch64(APVDSPContext *dsp);

#endif /* AVCODEC_APV_DSP_H */
