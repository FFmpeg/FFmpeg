/*
 * Copyright (c) 2011 Janne Grunau <janne-libav@jannau.net>
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

#include <stdint.h>

#include "libavcodec/avcodec.h"
#include "libavcodec/rv34dsp.h"

void ff_rv34_inv_transform_noround_neon(DCTELEM *block);

void ff_rv34_inv_transform_noround_dc_neon(DCTELEM *block);

void ff_rv34_idct_add_neon(uint8_t *dst, ptrdiff_t stride, DCTELEM *block);
void ff_rv34_idct_dc_add_neon(uint8_t *dst, ptrdiff_t stride, int dc);

void ff_rv34dsp_init_neon(RV34DSPContext *c, DSPContext* dsp)
{
    c->rv34_inv_transform    = ff_rv34_inv_transform_noround_neon;
    c->rv34_inv_transform_dc = ff_rv34_inv_transform_noround_dc_neon;

    c->rv34_idct_add    = ff_rv34_idct_add_neon;
    c->rv34_idct_dc_add = ff_rv34_idct_dc_add_neon;
}
