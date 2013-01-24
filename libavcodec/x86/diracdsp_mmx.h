/*
 * Copyright (c) 2010 David Conrad
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

#ifndef AVCODEC_X86_DIRACDSP_H
#define AVCODEC_X86_DIRACDSP_H

#include "libavcodec/diracdsp.h"

void ff_diracdsp_init_mmx(DiracDSPContext* c);

DECL_DIRAC_PIXOP(put, mmx);
DECL_DIRAC_PIXOP(avg, mmx);
DECL_DIRAC_PIXOP(avg, mmxext);

void ff_put_dirac_pixels16_sse2(uint8_t *dst, const uint8_t *src[5], int stride, int h);
void ff_avg_dirac_pixels16_sse2(uint8_t *dst, const uint8_t *src[5], int stride, int h);
void ff_put_dirac_pixels32_sse2(uint8_t *dst, const uint8_t *src[5], int stride, int h);
void ff_avg_dirac_pixels32_sse2(uint8_t *dst, const uint8_t *src[5], int stride, int h);

void ff_add_rect_clamped_mmx(uint8_t *, const uint16_t *, int, const int16_t *, int, int, int);
void ff_add_rect_clamped_sse2(uint8_t *, const uint16_t *, int, const int16_t *, int, int, int);

void ff_add_dirac_obmc8_mmx(uint16_t *dst, const uint8_t *src, int stride, const uint8_t *obmc_weight, int yblen);
void ff_add_dirac_obmc16_mmx(uint16_t *dst, const uint8_t *src, int stride, const uint8_t *obmc_weight, int yblen);
void ff_add_dirac_obmc32_mmx(uint16_t *dst, const uint8_t *src, int stride, const uint8_t *obmc_weight, int yblen);

void ff_add_dirac_obmc16_sse2(uint16_t *dst, const uint8_t *src, int stride, const uint8_t *obmc_weight, int yblen);
void ff_add_dirac_obmc32_sse2(uint16_t *dst, const uint8_t *src, int stride, const uint8_t *obmc_weight, int yblen);

#endif
