/*
 * VP9 compatible video decoder
 *
 * Copyright (C) 2013 Ronald S. Bultje <rsbultje gmail com>
 * Copyright (C) 2013 Clément Bœsch <u pkh me>
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

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "vp9dsp.h"

av_cold void ff_vp9dsp_init(VP9DSPContext *dsp, int bpp)
{
    if (bpp == 8) {
        ff_vp9dsp_init_8(dsp);
    } else if (bpp == 10) {
        ff_vp9dsp_init_10(dsp);
    } else {
        av_assert0(bpp == 12);
        ff_vp9dsp_init_12(dsp);
    }

    if (ARCH_X86) ff_vp9dsp_init_x86(dsp, bpp);
    if (ARCH_MIPS) ff_vp9dsp_init_mips(dsp, bpp);
}
