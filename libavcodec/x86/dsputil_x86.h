/*
 * MMX optimized DSP utils
 * Copyright (c) 2007  Aurelien Jacobs <aurel@gnuage.org>
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

#ifndef AVCODEC_X86_DSPUTIL_X86_H
#define AVCODEC_X86_DSPUTIL_X86_H

#include <stdint.h>

#include "libavcodec/avcodec.h"
#include "libavcodec/dsputil.h"

void ff_dsputil_init_pix_mmx(DSPContext *c, AVCodecContext *avctx);


void ff_mmx_idct(int16_t *block);
void ff_mmxext_idct(int16_t *block);
#endif /* AVCODEC_X86_DSPUTIL_X86_H */
