/*
 * DSP utils
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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
 * DSP utils.
 * Note, many functions in here may use MMX which trashes the FPU state, it is
 * absolutely necessary to call emms_c() between DSP & float/double code.
 */

#ifndef AVCODEC_DSPUTIL_H
#define AVCODEC_DSPUTIL_H

#include "avcodec.h"

extern uint32_t ff_square_tab[512];


/* minimum alignment rules ;)
 * If you notice errors in the align stuff, need more alignment for some ASM code
 * for some CPU or need to use a function with less aligned data then send a mail
 * to the ffmpeg-devel mailing list, ...
 *
 * !warning These alignments might not match reality, (missing attribute((align))
 * stuff somewhere possible).
 * I (Michael) did not check them, these are just the alignments which I think
 * could be reached easily ...
 *
 * !future video codecs might need functions with less strict alignment
 */

struct MpegEncContext;
/* Motion estimation:
 * h is limited to { width / 2, width, 2 * width },
 * but never larger than 16 and never smaller than 2.
 * Although currently h < 4 is not used as functions with
 * width < 8 are neither used nor implemented. */
typedef int (*me_cmp_func)(struct MpegEncContext *c,
                           uint8_t *blk1 /* align width (8 or 16) */,
                           uint8_t *blk2 /* align 1 */, int line_size, int h);

/**
 * DSPContext.
 */
typedef struct DSPContext {
    int (*sum_abs_dctelem)(int16_t *block /* align 16 */);

    me_cmp_func sad[6]; /* identical to pix_absAxA except additional void * */
    me_cmp_func sse[6];
    me_cmp_func hadamard8_diff[6];
    me_cmp_func dct_sad[6];
    me_cmp_func quant_psnr[6];
    me_cmp_func bit[6];
    me_cmp_func rd[6];
    me_cmp_func vsad[6];
    me_cmp_func vsse[6];
    me_cmp_func nsse[6];
    me_cmp_func w53[6];
    me_cmp_func w97[6];
    me_cmp_func dct_max[6];
    me_cmp_func dct264_sad[6];

    me_cmp_func me_pre_cmp[6];
    me_cmp_func me_cmp[6];
    me_cmp_func me_sub_cmp[6];
    me_cmp_func mb_cmp[6];
    me_cmp_func ildct_cmp[6]; // only width 16 used
    me_cmp_func frame_skip_cmp[6]; // only width 8 used

    me_cmp_func pix_abs[2][4];
} DSPContext;

void ff_dsputil_static_init(void);
void ff_dsputil_init(DSPContext *p, AVCodecContext *avctx);
void avpriv_dsputil_init(DSPContext* p, AVCodecContext *avctx);
attribute_deprecated void dsputil_init(DSPContext* c, AVCodecContext *avctx);

int ff_check_alignment(void);

void ff_set_cmp(DSPContext *c, me_cmp_func *cmp, int type);

void ff_dsputil_init_alpha(DSPContext* c, AVCodecContext *avctx);
void ff_dsputil_init_arm(DSPContext *c, AVCodecContext *avctx);
void ff_dsputil_init_ppc(DSPContext *c, AVCodecContext *avctx);
void ff_dsputil_init_x86(DSPContext *c, AVCodecContext *avctx);

void ff_dsputil_init_dwt(DSPContext *c);

#endif /* AVCODEC_DSPUTIL_H */
