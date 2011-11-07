/*
 * Copyright (c) 2011 Mans Rullgard
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

#include "config.h"
#include "mpegaudiodsp.h"
#include "dct.h"
#include "dct32.h"

void ff_mpadsp_init(MPADSPContext *s)
{
    DCTContext dct;

    ff_dct_init(&dct, 5, DCT_II);

    s->apply_window_float = ff_mpadsp_apply_window_float;
    s->apply_window_fixed = ff_mpadsp_apply_window_fixed;

    s->dct32_float = dct.dct32;
    s->dct32_fixed = ff_dct32_fixed;

    s->imdct36_float = ff_imdct36_float;
    s->imdct36_fixed = ff_imdct36_fixed;

    if (ARCH_ARM)     ff_mpadsp_init_arm(s);
    if (HAVE_MMX)     ff_mpadsp_init_mmx(s);
    if (HAVE_ALTIVEC) ff_mpadsp_init_altivec(s);
}
