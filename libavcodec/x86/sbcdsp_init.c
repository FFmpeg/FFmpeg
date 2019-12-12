/*
 * Bluetooth low-complexity, subband codec (SBC)
 *
 * Copyright (C) 2017  Aurelien Jacobs <aurel@gnuage.org>
 * Copyright (C) 2008-2010  Nokia Corporation
 * Copyright (C) 2004-2010  Marcel Holtmann <marcel@holtmann.org>
 * Copyright (C) 2004-2005  Henryk Ploetz <henryk@ploetzli.ch>
 * Copyright (C) 2005-2006  Brad Midgley <bmidgley@xmission.com>
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

/**
 * @file
 * SBC MMX optimization for some basic "building bricks"
 */

#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/sbcdsp.h"

void ff_sbc_analyze_4_mmx(const int16_t *in, int32_t *out, const int16_t *consts);
void ff_sbc_analyze_8_mmx(const int16_t *in, int32_t *out, const int16_t *consts);
void ff_sbc_calc_scalefactors_mmx(int32_t sb_sample_f[16][2][8],
                                  uint32_t scale_factor[2][8],
                                  int blocks, int channels, int subbands);

av_cold void ff_sbcdsp_init_x86(SBCDSPContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_MMX(cpu_flags)) {
        s->sbc_analyze_4 = ff_sbc_analyze_4_mmx;
        s->sbc_analyze_8 = ff_sbc_analyze_8_mmx;
        s->sbc_calc_scalefactors = ff_sbc_calc_scalefactors_mmx;
    }
}
