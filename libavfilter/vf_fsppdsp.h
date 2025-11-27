/*
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (C) 2005 Nikolaj Poroshin <porosh3@psu.ru>
 * Copyright (c) 2014 Arwa Arif <arwaarif1994@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef AVFILTER_FSPPDSP_H
#define AVFILTER_FSPPDSP_H

#include <stddef.h>
#include <stdint.h>

#include "config.h"

#include "libavutil/attributes_internal.h"

typedef struct FSPPDSPContext {
    void (*store_slice)(uint8_t *restrict dst, int16_t *restrict src /* align 16 */,
                        ptrdiff_t dst_stride, ptrdiff_t src_stride,
                        ptrdiff_t width, ptrdiff_t height, ptrdiff_t log2_scale);

    void (*store_slice2)(uint8_t *restrict dst, int16_t *restrict src /* align 16 */,
                         ptrdiff_t dst_stride, ptrdiff_t src_stride,
                         ptrdiff_t width, ptrdiff_t height, ptrdiff_t log2_scale);

    void (*mul_thrmat)(const int16_t *restrict thr_adr_noq /* align 16 */,
                       int16_t *restrict thr_adr /* align 16 */, int q);

    void (*column_fidct)(const int16_t *restrict thr_adr, const int16_t *restrict data,
                         int16_t *restrict output, int cnt);

    void (*row_idct)(const int16_t *restrict workspace, int16_t *restrict output_adr,
                     ptrdiff_t output_stride, int cnt);

    void (*row_fdct)(int16_t *restrict data, const uint8_t *restrict pixels,
                     ptrdiff_t line_size, int cnt);
} FSPPDSPContext;

FF_VISIBILITY_PUSH_HIDDEN
extern const uint8_t ff_fspp_dither[8][8];

void ff_store_slice_c(uint8_t *restrict dst, int16_t *restrict src,
                      ptrdiff_t dst_stride, ptrdiff_t src_stride,
                      ptrdiff_t width, ptrdiff_t height, ptrdiff_t log2_scale);
void ff_store_slice2_c(uint8_t *restrict dst, int16_t *restrict src,
                       ptrdiff_t dst_stride, ptrdiff_t src_stride,
                       ptrdiff_t width, ptrdiff_t height, ptrdiff_t log2_scale);
void ff_mul_thrmat_c(const int16_t *restrict thr_adr_noq, int16_t *restrict thr_adr, int q);
void ff_column_fidct_c(const int16_t *restrict thr_adr, const int16_t *restrict data,
                       int16_t *restrict output, int cnt);
void ff_row_idct_c(const int16_t *restrict workspace, int16_t *restrict output_adr,
                   ptrdiff_t output_stride, int cnt);
void ff_row_fdct_c(int16_t *restrict data, const uint8_t *restrict pixels,
                   ptrdiff_t line_size, int cnt);

void ff_fsppdsp_init_x86(FSPPDSPContext *fspp);
FF_VISIBILITY_POP_HIDDEN

static inline void ff_fsppdsp_init(FSPPDSPContext *fspp)
{
    fspp->store_slice  = ff_store_slice_c;
    fspp->store_slice2 = ff_store_slice2_c;
    fspp->mul_thrmat   = ff_mul_thrmat_c;
    fspp->column_fidct = ff_column_fidct_c;
    fspp->row_idct     = ff_row_idct_c;
    fspp->row_fdct     = ff_row_fdct_c;

#if ARCH_X86 && HAVE_X86ASM
    ff_fsppdsp_init_x86(fspp);
#endif
}

#endif /* AVFILTER_FSPPDSP_H */
