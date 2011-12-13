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

void ff_rv34_inv_transform_neon(DCTELEM *block);
void ff_rv34_inv_transform_noround_neon(DCTELEM *block);
void ff_rv34_dequant4x4_neon(DCTELEM *block, int Qdc, int Q);

void ff_rv34dsp_init_neon(RV34DSPContext *c, DSPContext* dsp)
{
    c->rv34_inv_transform_tab[0] = ff_rv34_inv_transform_neon;
    c->rv34_inv_transform_tab[1] = ff_rv34_inv_transform_noround_neon;

    c->rv34_dequant4x4 = ff_rv34_dequant4x4_neon;
}
