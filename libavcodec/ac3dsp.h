/*
 * AC-3 DSP utils
 * Copyright (c) 2011 Justin Ruggles
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

#ifndef AVCODEC_AC3DSP_H
#define AVCODEC_AC3DSP_H

#include <stdint.h>

typedef struct AC3DSPContext {
    /**
     * Set each encoded exponent in a block to the minimum of itself and the
     * exponents in the same frequency bin of up to 5 following blocks.
     * @param exp   pointer to the start of the current block of exponents.
     *              constraints: align 16
     * @param num_reuse_blocks  number of blocks that will reuse exponents from the current block.
     *                          constraints: range 0 to 5
     * @param nb_coefs  number of frequency coefficients.
     */
    void (*ac3_exponent_min)(uint8_t *exp, int num_reuse_blocks, int nb_coefs);
} AC3DSPContext;

void ff_ac3dsp_init    (AC3DSPContext *c);
void ff_ac3dsp_init_x86(AC3DSPContext *c);

#endif /* AVCODEC_AC3DSP_H */
