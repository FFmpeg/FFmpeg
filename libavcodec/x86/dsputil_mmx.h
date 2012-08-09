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

#ifndef AVCODEC_X86_DSPUTIL_MMX_H
#define AVCODEC_X86_DSPUTIL_MMX_H

#include <stdint.h>
#include "libavcodec/dsputil.h"
#include "libavutil/x86/asm.h"

typedef struct { uint64_t a, b; } xmm_reg;

extern const uint64_t ff_bone;
extern const uint64_t ff_wtwo;

extern const uint64_t ff_pdw_80000000[2];

extern const xmm_reg  ff_pw_3;
extern const xmm_reg  ff_pw_4;
extern const xmm_reg  ff_pw_5;
extern const xmm_reg  ff_pw_8;
extern const uint64_t ff_pw_15;
extern const xmm_reg  ff_pw_16;
extern const xmm_reg  ff_pw_18;
extern const uint64_t ff_pw_20;
extern const xmm_reg  ff_pw_27;
extern const xmm_reg  ff_pw_28;
extern const xmm_reg  ff_pw_32;
extern const uint64_t ff_pw_42;
extern const uint64_t ff_pw_53;
extern const xmm_reg  ff_pw_63;
extern const xmm_reg  ff_pw_64;
extern const uint64_t ff_pw_96;
extern const uint64_t ff_pw_128;
extern const uint64_t ff_pw_255;

extern const xmm_reg  ff_pb_1;
extern const xmm_reg  ff_pb_3;
extern const uint64_t ff_pb_7;
extern const uint64_t ff_pb_1F;
extern const uint64_t ff_pb_3F;
extern const uint64_t ff_pb_81;
extern const xmm_reg  ff_pb_A1;
extern const xmm_reg  ff_pb_F8;
extern const uint64_t ff_pb_FC;
extern const xmm_reg  ff_pb_FE;

extern const double ff_pd_1[2];
extern const double ff_pd_2[2];

#define SBUTTERFLY(a,b,t,n,m)\
    "mov" #m " " #a ", " #t "         \n\t" /* abcd */\
    "punpckl" #n " " #b ", " #a "     \n\t" /* aebf */\
    "punpckh" #n " " #b ", " #t "     \n\t" /* cgdh */\

#define TRANSPOSE4(a,b,c,d,t)\
    SBUTTERFLY(a,b,t,wd,q) /* a=aebf t=cgdh */\
    SBUTTERFLY(c,d,b,wd,q) /* c=imjn b=kolp */\
    SBUTTERFLY(a,c,d,dq,q) /* a=aeim d=bfjn */\
    SBUTTERFLY(t,b,c,dq,q) /* t=cgko c=dhlp */

#define MOVQ_WONE(regd) \
    __asm__ volatile ( \
    "pcmpeqd %%" #regd ", %%" #regd " \n\t" \
    "psrlw $15, %%" #regd ::)

void ff_dsputilenc_init_mmx(DSPContext* c, AVCodecContext *avctx);
void ff_dsputil_init_pix_mmx(DSPContext* c, AVCodecContext *avctx);

void ff_add_pixels_clamped_mmx(const DCTELEM *block, uint8_t *pixels, int line_size);
void ff_put_pixels_clamped_mmx(const DCTELEM *block, uint8_t *pixels, int line_size);
void ff_put_signed_pixels_clamped_mmx(const DCTELEM *block, uint8_t *pixels, int line_size);

void ff_put_cavs_qpel8_mc00_mmx2(uint8_t *dst, uint8_t *src, int stride);
void ff_avg_cavs_qpel8_mc00_mmx2(uint8_t *dst, uint8_t *src, int stride);
void ff_put_cavs_qpel16_mc00_mmx2(uint8_t *dst, uint8_t *src, int stride);
void ff_avg_cavs_qpel16_mc00_mmx2(uint8_t *dst, uint8_t *src, int stride);

void ff_put_vc1_mspel_mc00_mmx(uint8_t *dst, const uint8_t *src, int stride, int rnd);
void ff_avg_vc1_mspel_mc00_mmx2(uint8_t *dst, const uint8_t *src, int stride, int rnd);

void ff_put_rv40_qpel8_mc33_mmx(uint8_t *block, uint8_t *pixels, int line_size);
void ff_put_rv40_qpel16_mc33_mmx(uint8_t *block, uint8_t *pixels, int line_size);
void ff_avg_rv40_qpel8_mc33_mmx(uint8_t *block, uint8_t *pixels, int line_size);
void ff_avg_rv40_qpel16_mc33_mmx(uint8_t *block, uint8_t *pixels, int line_size);

void ff_mmx_idct(DCTELEM *block);
void ff_mmxext_idct(DCTELEM *block);


void ff_deinterlace_line_mmx(uint8_t *dst,
                             const uint8_t *lum_m4, const uint8_t *lum_m3,
                             const uint8_t *lum_m2, const uint8_t *lum_m1,
                             const uint8_t *lum,
                             int size);

void ff_deinterlace_line_inplace_mmx(const uint8_t *lum_m4,
                                     const uint8_t *lum_m3,
                                     const uint8_t *lum_m2,
                                     const uint8_t *lum_m1,
                                     const uint8_t *lum, int size);

#endif /* AVCODEC_X86_DSPUTIL_MMX_H */
