/*
 * MLP codec common header file
 * Copyright (c) 2007-2008 Ian Caulfield
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

#ifndef AVCODEC_MLPDSP_H
#define AVCODEC_MLPDSP_H

#include <stdint.h>

void ff_mlp_rematrix_channel(int32_t *samples,
                             const int32_t *coeffs,
                             const uint8_t *bypassed_lsbs,
                             const int8_t *noise_buffer,
                             int index,
                             unsigned int dest_ch,
                             uint16_t blockpos,
                             unsigned int maxchan,
                             int matrix_noise_shift,
                             int access_unit_size_pow2,
                             int32_t mask);

typedef struct MLPDSPContext {
    void (*mlp_filter_channel)(int32_t *state, const int32_t *coeff,
                               int firorder, int iirorder,
                               unsigned int filter_shift, int32_t mask,
                               int blocksize, int32_t *sample_buffer);
    void (*mlp_rematrix_channel)(int32_t *samples,
                                 const int32_t *coeffs,
                                 const uint8_t *bypassed_lsbs,
                                 const int8_t *noise_buffer,
                                 int index,
                                 unsigned int dest_ch,
                                 uint16_t blockpos,
                                 unsigned int maxchan,
                                 int matrix_noise_shift,
                                 int access_unit_size_pow2,
                                 int32_t mask);
} MLPDSPContext;

void ff_mlpdsp_init(MLPDSPContext *c);
void ff_mlpdsp_init_arm(MLPDSPContext *c);
void ff_mlpdsp_init_x86(MLPDSPContext *c);

#endif /* AVCODEC_MLPDSP_H */
