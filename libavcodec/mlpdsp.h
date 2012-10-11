/*
 * MLP codec common header file
 * Copyright (c) 2007-2008 Ian Caulfield
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

#ifndef AVCODEC_MLPDSP_H
#define AVCODEC_MLPDSP_H

#include <stdint.h>

typedef struct MLPDSPContext {
    void (*mlp_filter_channel)(int32_t *state, const int32_t *coeff,
                               int firorder, int iirorder,
                               unsigned int filter_shift, int32_t mask,
                               int blocksize, int32_t *sample_buffer);
} MLPDSPContext;

void ff_mlpdsp_init(MLPDSPContext *c);
void ff_mlpdsp_init_x86(MLPDSPContext *c);

#endif /* AVCODEC_MLPDSP_H */
