/*
 * Unquantize functions for mpegvideo
 * Copyright (c) 2000,2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * 4MV & hq & B-frame encoding stuff by Michael Niedermayer <michaelni@gmx.at>
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

#ifndef AVCODEC_MPEGVIDEO_UNQUANTIZE_H
#define AVCODEC_MPEGVIDEO_UNQUANTIZE_H

#include <stdint.h>

#include "config.h"

typedef struct MpegEncContext MpegEncContext;

typedef struct MPVUnquantDSPContext {
    void (*dct_unquantize_mpeg1_intra)(struct MpegEncContext *s,
                           int16_t *block/*align 16*/, int n, int qscale);
    void (*dct_unquantize_mpeg1_inter)(struct MpegEncContext *s,
                           int16_t *block/*align 16*/, int n, int qscale);
    void (*dct_unquantize_mpeg2_intra)(struct MpegEncContext *s,
                           int16_t *block/*align 16*/, int n, int qscale);
    void (*dct_unquantize_mpeg2_inter)(struct MpegEncContext *s,
                           int16_t *block/*align 16*/, int n, int qscale);
    void (*dct_unquantize_h263_intra)(struct MpegEncContext *s,
                           int16_t *block/*align 16*/, int n, int qscale);
    void (*dct_unquantize_h263_inter)(struct MpegEncContext *s,
                           int16_t *block/*align 16*/, int n, int qscale);
} MPVUnquantDSPContext;

#if !ARCH_MIPS
#define ff_mpv_unquantize_init(s, bitexact, q_scale_type) ff_mpv_unquantize_init(s, bitexact)
#endif

void ff_mpv_unquantize_init(MPVUnquantDSPContext *s,
                            int bitexact, int q_scale_type);
void ff_mpv_unquantize_init_arm (MPVUnquantDSPContext *s, int bitexact);
void ff_mpv_unquantize_init_neon(MPVUnquantDSPContext *s, int bitexact);
void ff_mpv_unquantize_init_ppc (MPVUnquantDSPContext *s, int bitexact);
void ff_mpv_unquantize_init_x86 (MPVUnquantDSPContext *s, int bitexact);
void ff_mpv_unquantize_init_mips(MPVUnquantDSPContext *s, int bitexact,
                                 int q_scale_type);

#endif /* AVCODEC_MPEGVIDEO_UNQUANTIZE_H */
