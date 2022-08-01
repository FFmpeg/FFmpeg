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

#include "config.h"
#include "flacencdsp.h"

#define SAMPLE_SIZE 16
#include "flacdsp_lpc_template.c"

#undef  SAMPLE_SIZE
#define SAMPLE_SIZE 32
#include "flacdsp_lpc_template.c"


av_cold void ff_flacencdsp_init(FLACEncDSPContext *c)
{
    c->lpc16_encode = flac_lpc_encode_c_16;
    c->lpc32_encode = flac_lpc_encode_c_32;

#if ARCH_X86
    ff_flacencdsp_init_x86(c);
#endif
}
