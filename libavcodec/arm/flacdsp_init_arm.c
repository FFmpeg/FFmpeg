/*
 * Copyright (c) 2012 Mans Rullgard <mans@mansr.com>
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

#include "libavcodec/flacdsp.h"
#include "config.h"

void ff_flac_lpc_16_arm(int32_t *samples, const int coeffs[32], int order,
                        int qlevel, int len);

av_cold void ff_flacdsp_init_arm(FLACDSPContext *c, enum AVSampleFormat fmt, int channels,
                                 int bps)
{
    if (bps <= 16 && CONFIG_FLAC_DECODER)
        c->lpc = ff_flac_lpc_16_arm;
}
