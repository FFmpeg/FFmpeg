/*
 * BlackFin DSPUTILS COMMON OPTIMIZATIONS HEADER
 *
 * Copyright (C) 2007 Marc Hoffman <mmh@pleasantst.com>
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


#ifndef AVCODEC_BFIN_DSPUTIL_BFIN_H
#define AVCODEC_BFIN_DSPUTIL_BFIN_H

#include "config.h"
#include "libavcodec/dsputil.h"

#if defined(__FDPIC__) && CONFIG_SRAM
#define attribute_l1_text  __attribute__ ((l1_text))
#define attribute_l1_data_b __attribute__((l1_data_B))
#else
#define attribute_l1_text
#define attribute_l1_data_b
#endif

void ff_bfin_idct (DCTELEM *block) attribute_l1_text;
void ff_bfin_fdct (DCTELEM *block) attribute_l1_text;
void ff_bfin_vp3_idct (DCTELEM *block);
void ff_bfin_vp3_idct_put (uint8_t *dest, int line_size, DCTELEM *block);
void ff_bfin_vp3_idct_add (uint8_t *dest, int line_size, DCTELEM *block);
void ff_bfin_add_pixels_clamped (const DCTELEM *block, uint8_t *dest, int line_size) attribute_l1_text;
void ff_bfin_put_pixels_clamped (const DCTELEM *block, uint8_t *dest, int line_size) attribute_l1_text;
void ff_bfin_diff_pixels (DCTELEM *block, const uint8_t *s1, const uint8_t *s2, int stride)  attribute_l1_text;
void ff_bfin_get_pixels  (DCTELEM *restrict block, const uint8_t *pixels, int line_size) attribute_l1_text;
int  ff_bfin_pix_norm1  (uint8_t * pix, int line_size) attribute_l1_text;
int  ff_bfin_z_sad8x8   (uint8_t *blk1, uint8_t *blk2, int dsz, int line_size, int h) attribute_l1_text;
int  ff_bfin_z_sad16x16 (uint8_t *blk1, uint8_t *blk2, int dsz, int line_size, int h) attribute_l1_text;

void ff_bfin_z_put_pixels16_xy2     (uint8_t *block, const uint8_t *s0, int dest_size, int line_size, int h) attribute_l1_text;
void ff_bfin_z_put_pixels8_xy2      (uint8_t *block, const uint8_t *s0, int dest_size, int line_size, int h) attribute_l1_text;
void ff_bfin_put_pixels16_xy2_nornd (uint8_t *block, const uint8_t *s0, int line_size, int h) attribute_l1_text;
void ff_bfin_put_pixels8_xy2_nornd  (uint8_t *block, const uint8_t *s0, int line_size, int h) attribute_l1_text;


int  ff_bfin_pix_sum (uint8_t *p, int stride) attribute_l1_text;

void ff_bfin_put_pixels8uc        (uint8_t *block, const uint8_t *s0, const uint8_t *s1, int dest_size, int line_size, int h) attribute_l1_text;
void ff_bfin_put_pixels16uc       (uint8_t *block, const uint8_t *s0, const uint8_t *s1, int dest_size, int line_size, int h) attribute_l1_text;
void ff_bfin_put_pixels8uc_nornd  (uint8_t *block, const uint8_t *s0, const uint8_t *s1, int line_size, int h) attribute_l1_text;
void ff_bfin_put_pixels16uc_nornd (uint8_t *block, const uint8_t *s0, const uint8_t *s1, int line_size, int h) attribute_l1_text;

int ff_bfin_sse4  (void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h) attribute_l1_text;
int ff_bfin_sse8  (void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h) attribute_l1_text;
int ff_bfin_sse16 (void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h) attribute_l1_text;


#ifdef BFIN_PROFILE

static double Telem[16];
static char  *TelemNames[16];
static int    TelemCnt;

#define PROF(lab,e) { int xx_e = e; char*xx_lab = lab; uint64_t xx_t0 = read_time();
#define EPROF()       xx_t0 = read_time()-xx_t0; Telem[xx_e] = Telem[xx_e] + xx_t0; TelemNames[xx_e] = xx_lab; }

static void prof_report (void)
{
    int i;
    double s = 0;
    for (i=0;i<16;i++) {
        double v;
        if (TelemNames[i]) {
            v = Telem[i]/TelemCnt;
            av_log (NULL,AV_LOG_DEBUG,"%-20s: %12.4f\t%12.4f\n", TelemNames[i],v,v/64);
            s = s + Telem[i];
        }
    }
    av_log (NULL,AV_LOG_DEBUG,"%-20s: %12.4f\t%12.4f\n%20.4f\t%d\n",
            "total",s/TelemCnt,s/TelemCnt/64,s,TelemCnt);
}

static void bfprof (void)
{
    static int init;
    if (!init) atexit (prof_report);
    init=1;
    TelemCnt++;
}

#else
#define PROF(a,b)
#define EPROF()
#define bfprof()
#endif

#endif /* AVCODEC_BFIN_DSPUTIL_BFIN_H */
