/*
 * H.26L/H.264/AVC/JVT/14496-10/... loop filter
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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
 * H.264 / AVC / MPEG4 part10 loop filter.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "internal.h"
#include "avcodec.h"
#include "mpegvideo.h"
#include "h264.h"
#include "mathops.h"
#include "rectangle.h"

/* Deblocking filter (p153) */
static const uint8_t alpha_table[52*3] = {
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  4,  4,  5,  6,
     7,  8,  9, 10, 12, 13, 15, 17, 20, 22,
    25, 28, 32, 36, 40, 45, 50, 56, 63, 71,
    80, 90,101,113,127,144,162,182,203,226,
   255,255,
   255,255,255,255,255,255,255,255,255,255,255,255,255,
   255,255,255,255,255,255,255,255,255,255,255,255,255,
   255,255,255,255,255,255,255,255,255,255,255,255,255,
   255,255,255,255,255,255,255,255,255,255,255,255,255,
};
static const uint8_t beta_table[52*3] = {
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  2,  2,  2,  3,
     3,  3,  3,  4,  4,  4,  6,  6,  7,  7,
     8,  8,  9,  9, 10, 10, 11, 11, 12, 12,
    13, 13, 14, 14, 15, 15, 16, 16, 17, 17,
    18, 18,
    18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
};
static const uint8_t tc0_table[52*3][4] = {
    {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 },
    {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 },
    {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 },
    {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 },
    {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 },
    {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 },
    {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 },
    {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 },
    {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 },
    {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 },
    {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 },
    {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 0 }, {-1, 0, 0, 1 },
    {-1, 0, 0, 1 }, {-1, 0, 0, 1 }, {-1, 0, 0, 1 }, {-1, 0, 1, 1 }, {-1, 0, 1, 1 }, {-1, 1, 1, 1 },
    {-1, 1, 1, 1 }, {-1, 1, 1, 1 }, {-1, 1, 1, 1 }, {-1, 1, 1, 2 }, {-1, 1, 1, 2 }, {-1, 1, 1, 2 },
    {-1, 1, 1, 2 }, {-1, 1, 2, 3 }, {-1, 1, 2, 3 }, {-1, 2, 2, 3 }, {-1, 2, 2, 4 }, {-1, 2, 3, 4 },
    {-1, 2, 3, 4 }, {-1, 3, 3, 5 }, {-1, 3, 4, 6 }, {-1, 3, 4, 6 }, {-1, 4, 5, 7 }, {-1, 4, 5, 8 },
    {-1, 4, 6, 9 }, {-1, 5, 7,10 }, {-1, 6, 8,11 }, {-1, 6, 8,13 }, {-1, 7,10,14 }, {-1, 8,11,16 },
    {-1, 9,12,18 }, {-1,10,13,20 }, {-1,11,15,23 }, {-1,13,17,25 },
    {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 },
    {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 },
    {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 },
    {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 },
    {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 },
    {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 },
    {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 },
    {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 },
    {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 }, {-1,13,17,25 },
};

/* intra: 0 if this loopfilter call is guaranteed to be inter (bS < 4), 1 if it might be intra (bS == 4) */
static av_always_inline void filter_mb_edgev(uint8_t *pix, int stride,
                                             const int16_t bS[4],
                                             unsigned int qp, int a, int b,
                                             H264Context *h, int intra)
{
    const unsigned int index_a = qp + a;
    const int alpha = alpha_table[index_a];
    const int beta  = beta_table[qp + b];
    if (alpha ==0 || beta == 0) return;

    if( bS[0] < 4 || !intra ) {
        int8_t tc[4];
        tc[0] = tc0_table[index_a][bS[0]];
        tc[1] = tc0_table[index_a][bS[1]];
        tc[2] = tc0_table[index_a][bS[2]];
        tc[3] = tc0_table[index_a][bS[3]];
        h->h264dsp.h264_h_loop_filter_luma(pix, stride, alpha, beta, tc);
    } else {
        h->h264dsp.h264_h_loop_filter_luma_intra(pix, stride, alpha, beta);
    }
}

static av_always_inline void filter_mb_edgecv(uint8_t *pix, int stride,
                                              const int16_t bS[4],
                                              unsigned int qp, int a, int b,
                                              H264Context *h, int intra)
{
    const unsigned int index_a = qp + a;
    const int alpha = alpha_table[index_a];
    const int beta  = beta_table[qp + b];
    if (alpha ==0 || beta == 0) return;

    if( bS[0] < 4 || !intra ) {
        int8_t tc[4];
        tc[0] = tc0_table[index_a][bS[0]]+1;
        tc[1] = tc0_table[index_a][bS[1]]+1;
        tc[2] = tc0_table[index_a][bS[2]]+1;
        tc[3] = tc0_table[index_a][bS[3]]+1;
        h->h264dsp.h264_h_loop_filter_chroma(pix, stride, alpha, beta, tc);
    } else {
        h->h264dsp.h264_h_loop_filter_chroma_intra(pix, stride, alpha, beta);
    }
}

static av_always_inline void filter_mb_mbaff_edgev(H264Context *h, uint8_t *pix,
                                                   int stride,
                                                   const int16_t bS[7], int bsi,
                                                   int qp, int a, int b,
                                                   int intra)
{
    const unsigned int index_a = qp + a;
    const int alpha = alpha_table[index_a];
    const int beta  = beta_table[qp + b];
    if (alpha ==0 || beta == 0) return;

    if( bS[0] < 4 || !intra ) {
        int8_t tc[4];
        tc[0] = tc0_table[index_a][bS[0*bsi]];
        tc[1] = tc0_table[index_a][bS[1*bsi]];
        tc[2] = tc0_table[index_a][bS[2*bsi]];
        tc[3] = tc0_table[index_a][bS[3*bsi]];
        h->h264dsp.h264_h_loop_filter_luma_mbaff(pix, stride, alpha, beta, tc);
    } else {
        h->h264dsp.h264_h_loop_filter_luma_mbaff_intra(pix, stride, alpha, beta);
    }
}

static av_always_inline void filter_mb_mbaff_edgecv(H264Context *h,
                                                    uint8_t *pix, int stride,
                                                    const int16_t bS[7],
                                                    int bsi, int qp, int a,
                                                    int b, int intra)
{
    const unsigned int index_a = qp + a;
    const int alpha = alpha_table[index_a];
    const int beta  = beta_table[qp + b];
    if (alpha ==0 || beta == 0) return;

    if( bS[0] < 4 || !intra ) {
        int8_t tc[4];
        tc[0] = tc0_table[index_a][bS[0*bsi]] + 1;
        tc[1] = tc0_table[index_a][bS[1*bsi]] + 1;
        tc[2] = tc0_table[index_a][bS[2*bsi]] + 1;
        tc[3] = tc0_table[index_a][bS[3*bsi]] + 1;
        h->h264dsp.h264_h_loop_filter_chroma_mbaff(pix, stride, alpha, beta, tc);
    } else {
        h->h264dsp.h264_h_loop_filter_chroma_mbaff_intra(pix, stride, alpha, beta);
    }
}

static av_always_inline void filter_mb_edgeh(uint8_t *pix, int stride,
                                             const int16_t bS[4],
                                             unsigned int qp, int a, int b,
                                             H264Context *h, int intra)
{
    const unsigned int index_a = qp + a;
    const int alpha = alpha_table[index_a];
    const int beta  = beta_table[qp + b];
    if (alpha ==0 || beta == 0) return;

    if( bS[0] < 4 || !intra ) {
        int8_t tc[4];
        tc[0] = tc0_table[index_a][bS[0]];
        tc[1] = tc0_table[index_a][bS[1]];
        tc[2] = tc0_table[index_a][bS[2]];
        tc[3] = tc0_table[index_a][bS[3]];
        h->h264dsp.h264_v_loop_filter_luma(pix, stride, alpha, beta, tc);
    } else {
        h->h264dsp.h264_v_loop_filter_luma_intra(pix, stride, alpha, beta);
    }
}

static av_always_inline void filter_mb_edgech(uint8_t *pix, int stride,
                                              const int16_t bS[4],
                                              unsigned int qp, int a, int b,
                                              H264Context *h, int intra)
{
    const unsigned int index_a = qp + a;
    const int alpha = alpha_table[index_a];
    const int beta  = beta_table[qp + b];
    if (alpha ==0 || beta == 0) return;

    if( bS[0] < 4 || !intra ) {
        int8_t tc[4];
        tc[0] = tc0_table[index_a][bS[0]]+1;
        tc[1] = tc0_table[index_a][bS[1]]+1;
        tc[2] = tc0_table[index_a][bS[2]]+1;
        tc[3] = tc0_table[index_a][bS[3]]+1;
        h->h264dsp.h264_v_loop_filter_chroma(pix, stride, alpha, beta, tc);
    } else {
        h->h264dsp.h264_v_loop_filter_chroma_intra(pix, stride, alpha, beta);
    }
}

static av_always_inline void h264_filter_mb_fast_internal(H264Context *h,
                                                          int mb_x, int mb_y,
                                                          uint8_t *img_y,
                                                          uint8_t *img_cb,
                                                          uint8_t *img_cr,
                                                          unsigned int linesize,
                                                          unsigned int uvlinesize,
                                                          int pixel_shift)
{
    int chroma = CHROMA(h) && !(CONFIG_GRAY && (h->flags&CODEC_FLAG_GRAY));
    int chroma444 = CHROMA444(h);
    int chroma422 = CHROMA422(h);

    int mb_xy = h->mb_xy;
    int left_type= h->left_type[LTOP];
    int top_type= h->top_type;

    int qp_bd_offset = 6 * (h->sps.bit_depth_luma - 8);
    int a = 52 + h->slice_alpha_c0_offset - qp_bd_offset;
    int b = 52 + h->slice_beta_offset - qp_bd_offset;

    int mb_type = h->cur_pic.mb_type[mb_xy];
    int qp      = h->cur_pic.qscale_table[mb_xy];
    int qp0     = h->cur_pic.qscale_table[mb_xy - 1];
    int qp1     = h->cur_pic.qscale_table[h->top_mb_xy];
    int qpc = get_chroma_qp( h, 0, qp );
    int qpc0 = get_chroma_qp( h, 0, qp0 );
    int qpc1 = get_chroma_qp( h, 0, qp1 );
    qp0 = (qp + qp0 + 1) >> 1;
    qp1 = (qp + qp1 + 1) >> 1;
    qpc0 = (qpc + qpc0 + 1) >> 1;
    qpc1 = (qpc + qpc1 + 1) >> 1;

    if( IS_INTRA(mb_type) ) {
        static const int16_t bS4[4] = {4,4,4,4};
        static const int16_t bS3[4] = {3,3,3,3};
        const int16_t *bSH = FIELD_PICTURE(h) ? bS3 : bS4;
        if(left_type)
            filter_mb_edgev( &img_y[4*0<<pixel_shift], linesize, bS4, qp0, a, b, h, 1);
        if( IS_8x8DCT(mb_type) ) {
            filter_mb_edgev( &img_y[4*2<<pixel_shift], linesize, bS3, qp, a, b, h, 0);
            if(top_type){
                filter_mb_edgeh( &img_y[4*0*linesize], linesize, bSH, qp1, a, b, h, 1);
            }
            filter_mb_edgeh( &img_y[4*2*linesize], linesize, bS3, qp, a, b, h, 0);
        } else {
            filter_mb_edgev( &img_y[4*1<<pixel_shift], linesize, bS3, qp, a, b, h, 0);
            filter_mb_edgev( &img_y[4*2<<pixel_shift], linesize, bS3, qp, a, b, h, 0);
            filter_mb_edgev( &img_y[4*3<<pixel_shift], linesize, bS3, qp, a, b, h, 0);
            if(top_type){
                filter_mb_edgeh( &img_y[4*0*linesize], linesize, bSH, qp1, a, b, h, 1);
            }
            filter_mb_edgeh( &img_y[4*1*linesize], linesize, bS3, qp, a, b, h, 0);
            filter_mb_edgeh( &img_y[4*2*linesize], linesize, bS3, qp, a, b, h, 0);
            filter_mb_edgeh( &img_y[4*3*linesize], linesize, bS3, qp, a, b, h, 0);
        }
        if(chroma){
            if(chroma444){
                if(left_type){
                    filter_mb_edgev( &img_cb[4*0<<pixel_shift], linesize, bS4, qpc0, a, b, h, 1);
                    filter_mb_edgev( &img_cr[4*0<<pixel_shift], linesize, bS4, qpc0, a, b, h, 1);
                }
                if( IS_8x8DCT(mb_type) ) {
                    filter_mb_edgev( &img_cb[4*2<<pixel_shift], linesize, bS3, qpc, a, b, h, 0);
                    filter_mb_edgev( &img_cr[4*2<<pixel_shift], linesize, bS3, qpc, a, b, h, 0);
                    if(top_type){
                        filter_mb_edgeh( &img_cb[4*0*linesize], linesize, bSH, qpc1, a, b, h, 1 );
                        filter_mb_edgeh( &img_cr[4*0*linesize], linesize, bSH, qpc1, a, b, h, 1 );
                    }
                    filter_mb_edgeh( &img_cb[4*2*linesize], linesize, bS3, qpc, a, b, h, 0);
                    filter_mb_edgeh( &img_cr[4*2*linesize], linesize, bS3, qpc, a, b, h, 0);
                } else {
                    filter_mb_edgev( &img_cb[4*1<<pixel_shift], linesize, bS3, qpc, a, b, h, 0);
                    filter_mb_edgev( &img_cr[4*1<<pixel_shift], linesize, bS3, qpc, a, b, h, 0);
                    filter_mb_edgev( &img_cb[4*2<<pixel_shift], linesize, bS3, qpc, a, b, h, 0);
                    filter_mb_edgev( &img_cr[4*2<<pixel_shift], linesize, bS3, qpc, a, b, h, 0);
                    filter_mb_edgev( &img_cb[4*3<<pixel_shift], linesize, bS3, qpc, a, b, h, 0);
                    filter_mb_edgev( &img_cr[4*3<<pixel_shift], linesize, bS3, qpc, a, b, h, 0);
                    if(top_type){
                        filter_mb_edgeh( &img_cb[4*0*linesize], linesize, bSH, qpc1, a, b, h, 1);
                        filter_mb_edgeh( &img_cr[4*0*linesize], linesize, bSH, qpc1, a, b, h, 1);
                    }
                    filter_mb_edgeh( &img_cb[4*1*linesize], linesize, bS3, qpc, a, b, h, 0);
                    filter_mb_edgeh( &img_cr[4*1*linesize], linesize, bS3, qpc, a, b, h, 0);
                    filter_mb_edgeh( &img_cb[4*2*linesize], linesize, bS3, qpc, a, b, h, 0);
                    filter_mb_edgeh( &img_cr[4*2*linesize], linesize, bS3, qpc, a, b, h, 0);
                    filter_mb_edgeh( &img_cb[4*3*linesize], linesize, bS3, qpc, a, b, h, 0);
                    filter_mb_edgeh( &img_cr[4*3*linesize], linesize, bS3, qpc, a, b, h, 0);
                }
            }else if(chroma422){
                if(left_type){
                    filter_mb_edgecv(&img_cb[2*0<<pixel_shift], uvlinesize, bS4, qpc0, a, b, h, 1);
                    filter_mb_edgecv(&img_cr[2*0<<pixel_shift], uvlinesize, bS4, qpc0, a, b, h, 1);
                }
                filter_mb_edgecv(&img_cb[2*2<<pixel_shift], uvlinesize, bS3, qpc, a, b, h, 0);
                filter_mb_edgecv(&img_cr[2*2<<pixel_shift], uvlinesize, bS3, qpc, a, b, h, 0);
                if(top_type){
                    filter_mb_edgech(&img_cb[4*0*uvlinesize], uvlinesize, bSH, qpc1, a, b, h, 1);
                    filter_mb_edgech(&img_cr[4*0*uvlinesize], uvlinesize, bSH, qpc1, a, b, h, 1);
                }
                filter_mb_edgech(&img_cb[4*1*uvlinesize], uvlinesize, bS3, qpc, a, b, h, 0);
                filter_mb_edgech(&img_cr[4*1*uvlinesize], uvlinesize, bS3, qpc, a, b, h, 0);
                filter_mb_edgech(&img_cb[4*2*uvlinesize], uvlinesize, bS3, qpc, a, b, h, 0);
                filter_mb_edgech(&img_cr[4*2*uvlinesize], uvlinesize, bS3, qpc, a, b, h, 0);
                filter_mb_edgech(&img_cb[4*3*uvlinesize], uvlinesize, bS3, qpc, a, b, h, 0);
                filter_mb_edgech(&img_cr[4*3*uvlinesize], uvlinesize, bS3, qpc, a, b, h, 0);
            }else{
                if(left_type){
                    filter_mb_edgecv( &img_cb[2*0<<pixel_shift], uvlinesize, bS4, qpc0, a, b, h, 1);
                    filter_mb_edgecv( &img_cr[2*0<<pixel_shift], uvlinesize, bS4, qpc0, a, b, h, 1);
                }
                filter_mb_edgecv( &img_cb[2*2<<pixel_shift], uvlinesize, bS3, qpc, a, b, h, 0);
                filter_mb_edgecv( &img_cr[2*2<<pixel_shift], uvlinesize, bS3, qpc, a, b, h, 0);
                if(top_type){
                    filter_mb_edgech( &img_cb[2*0*uvlinesize], uvlinesize, bSH, qpc1, a, b, h, 1);
                    filter_mb_edgech( &img_cr[2*0*uvlinesize], uvlinesize, bSH, qpc1, a, b, h, 1);
                }
                filter_mb_edgech( &img_cb[2*2*uvlinesize], uvlinesize, bS3, qpc, a, b, h, 0);
                filter_mb_edgech( &img_cr[2*2*uvlinesize], uvlinesize, bS3, qpc, a, b, h, 0);
            }
        }
        return;
    } else {
        LOCAL_ALIGNED_8(int16_t, bS, [2], [4][4]);
        int edges;
        if( IS_8x8DCT(mb_type) && (h->cbp&7) == 7 && !chroma444 ) {
            edges = 4;
            AV_WN64A(bS[0][0], 0x0002000200020002ULL);
            AV_WN64A(bS[0][2], 0x0002000200020002ULL);
            AV_WN64A(bS[1][0], 0x0002000200020002ULL);
            AV_WN64A(bS[1][2], 0x0002000200020002ULL);
        } else {
            int mask_edge1 = (3*(((5*mb_type)>>5)&1)) | (mb_type>>4); //(mb_type & (MB_TYPE_16x16 | MB_TYPE_8x16)) ? 3 : (mb_type & MB_TYPE_16x8) ? 1 : 0;
            int mask_edge0 = 3*((mask_edge1>>1) & ((5*left_type)>>5)&1); // (mb_type & (MB_TYPE_16x16 | MB_TYPE_8x16)) && (h->left_type[LTOP] & (MB_TYPE_16x16 | MB_TYPE_8x16)) ? 3 : 0;
            int step =  1+(mb_type>>24); //IS_8x8DCT(mb_type) ? 2 : 1;
            edges = 4 - 3*((mb_type>>3) & !(h->cbp & 15)); //(mb_type & MB_TYPE_16x16) && !(h->cbp & 15) ? 1 : 4;
            h->h264dsp.h264_loop_filter_strength( bS, h->non_zero_count_cache, h->ref_cache, h->mv_cache,
                                              h->list_count==2, edges, step, mask_edge0, mask_edge1, FIELD_PICTURE(h));
        }
        if( IS_INTRA(left_type) )
            AV_WN64A(bS[0][0], 0x0004000400040004ULL);
        if( IS_INTRA(top_type) )
            AV_WN64A(bS[1][0], FIELD_PICTURE(h) ? 0x0003000300030003ULL : 0x0004000400040004ULL);

#define FILTER(hv,dir,edge,intra)\
        if(AV_RN64A(bS[dir][edge])) {                                   \
            filter_mb_edge##hv( &img_y[4*edge*(dir?linesize:1<<pixel_shift)], linesize, bS[dir][edge], edge ? qp : qp##dir, a, b, h, intra );\
            if(chroma){\
                if(chroma444){\
                    filter_mb_edge##hv( &img_cb[4*edge*(dir?linesize:1<<pixel_shift)], linesize, bS[dir][edge], edge ? qpc : qpc##dir, a, b, h, intra );\
                    filter_mb_edge##hv( &img_cr[4*edge*(dir?linesize:1<<pixel_shift)], linesize, bS[dir][edge], edge ? qpc : qpc##dir, a, b, h, intra );\
                } else if(!(edge&1)) {\
                    filter_mb_edgec##hv( &img_cb[2*edge*(dir?uvlinesize:1<<pixel_shift)], uvlinesize, bS[dir][edge], edge ? qpc : qpc##dir, a, b, h, intra );\
                    filter_mb_edgec##hv( &img_cr[2*edge*(dir?uvlinesize:1<<pixel_shift)], uvlinesize, bS[dir][edge], edge ? qpc : qpc##dir, a, b, h, intra );\
                }\
            }\
        }
        if(left_type)
            FILTER(v,0,0,1);
        if( edges == 1 ) {
            if(top_type)
                FILTER(h,1,0,1);
        } else if( IS_8x8DCT(mb_type) ) {
            FILTER(v,0,2,0);
            if(top_type)
                FILTER(h,1,0,1);
            FILTER(h,1,2,0);
        } else {
            FILTER(v,0,1,0);
            FILTER(v,0,2,0);
            FILTER(v,0,3,0);
            if(top_type)
                FILTER(h,1,0,1);
            FILTER(h,1,1,0);
            FILTER(h,1,2,0);
            FILTER(h,1,3,0);
        }
#undef FILTER
    }
}

void ff_h264_filter_mb_fast( H264Context *h, int mb_x, int mb_y, uint8_t *img_y, uint8_t *img_cb, uint8_t *img_cr, unsigned int linesize, unsigned int uvlinesize) {
    av_assert2(!FRAME_MBAFF(h));
    if(!h->h264dsp.h264_loop_filter_strength || h->pps.chroma_qp_diff) {
        ff_h264_filter_mb(h, mb_x, mb_y, img_y, img_cb, img_cr, linesize, uvlinesize);
        return;
    }

#if CONFIG_SMALL
    h264_filter_mb_fast_internal(h, mb_x, mb_y, img_y, img_cb, img_cr, linesize, uvlinesize, h->pixel_shift);
#else
    if(h->pixel_shift){
        h264_filter_mb_fast_internal(h, mb_x, mb_y, img_y, img_cb, img_cr, linesize, uvlinesize, 1);
    }else{
        h264_filter_mb_fast_internal(h, mb_x, mb_y, img_y, img_cb, img_cr, linesize, uvlinesize, 0);
    }
#endif
}

static int check_mv(H264Context *h, long b_idx, long bn_idx, int mvy_limit){
    int v;

    v= h->ref_cache[0][b_idx] != h->ref_cache[0][bn_idx];
    if(!v && h->ref_cache[0][b_idx]!=-1)
        v= h->mv_cache[0][b_idx][0] - h->mv_cache[0][bn_idx][0] + 3 >= 7U |
           FFABS( h->mv_cache[0][b_idx][1] - h->mv_cache[0][bn_idx][1] ) >= mvy_limit;

    if(h->list_count==2){
        if(!v)
            v = h->ref_cache[1][b_idx] != h->ref_cache[1][bn_idx] |
                h->mv_cache[1][b_idx][0] - h->mv_cache[1][bn_idx][0] + 3 >= 7U |
                FFABS( h->mv_cache[1][b_idx][1] - h->mv_cache[1][bn_idx][1] ) >= mvy_limit;

        if(v){
            if(h->ref_cache[0][b_idx] != h->ref_cache[1][bn_idx] |
               h->ref_cache[1][b_idx] != h->ref_cache[0][bn_idx])
                return 1;
            return
                h->mv_cache[0][b_idx][0] - h->mv_cache[1][bn_idx][0] + 3 >= 7U |
                FFABS( h->mv_cache[0][b_idx][1] - h->mv_cache[1][bn_idx][1] ) >= mvy_limit |
                h->mv_cache[1][b_idx][0] - h->mv_cache[0][bn_idx][0] + 3 >= 7U |
                FFABS( h->mv_cache[1][b_idx][1] - h->mv_cache[0][bn_idx][1] ) >= mvy_limit;
        }
    }

    return v;
}

static av_always_inline void filter_mb_dir(H264Context *h, int mb_x, int mb_y, uint8_t *img_y, uint8_t *img_cb, uint8_t *img_cr, unsigned int linesize, unsigned int uvlinesize, int mb_xy, int mb_type, int mvy_limit, int first_vertical_edge_done, int a, int b, int chroma, int dir) {
    int edge;
    int chroma_qp_avg[2];
    int chroma444 = CHROMA444(h);
    int chroma422 = CHROMA422(h);
    const int mbm_xy = dir == 0 ? mb_xy -1 : h->top_mb_xy;
    const int mbm_type = dir == 0 ? h->left_type[LTOP] : h->top_type;

    // how often to recheck mv-based bS when iterating between edges
    static const uint8_t mask_edge_tab[2][8]={{0,3,3,3,1,1,1,1},
                                              {0,3,1,1,3,3,3,3}};
    const int mask_edge = mask_edge_tab[dir][(mb_type>>3)&7];
    const int edges = mask_edge== 3 && !(h->cbp&15) ? 1 : 4;

    // how often to recheck mv-based bS when iterating along each edge
    const int mask_par0 = mb_type & (MB_TYPE_16x16 | (MB_TYPE_8x16 >> dir));

    if(mbm_type && !first_vertical_edge_done){

        if (FRAME_MBAFF(h) && (dir == 1) && ((mb_y&1) == 0)
            && IS_INTERLACED(mbm_type&~mb_type)
            ) {
            // This is a special case in the norm where the filtering must
            // be done twice (one each of the field) even if we are in a
            // frame macroblock.
            //
            unsigned int tmp_linesize   = 2 *   linesize;
            unsigned int tmp_uvlinesize = 2 * uvlinesize;
            int mbn_xy = mb_xy - 2 * h->mb_stride;
            int j;

            for(j=0; j<2; j++, mbn_xy += h->mb_stride){
                DECLARE_ALIGNED(8, int16_t, bS)[4];
                int qp;
                if (IS_INTRA(mb_type | h->cur_pic.mb_type[mbn_xy])) {
                    AV_WN64A(bS, 0x0003000300030003ULL);
                } else {
                    if (!CABAC(h) && IS_8x8DCT(h->cur_pic.mb_type[mbn_xy])) {
                        bS[0]= 1+((h->cbp_table[mbn_xy] & 0x4000)||h->non_zero_count_cache[scan8[0]+0]);
                        bS[1]= 1+((h->cbp_table[mbn_xy] & 0x4000)||h->non_zero_count_cache[scan8[0]+1]);
                        bS[2]= 1+((h->cbp_table[mbn_xy] & 0x8000)||h->non_zero_count_cache[scan8[0]+2]);
                        bS[3]= 1+((h->cbp_table[mbn_xy] & 0x8000)||h->non_zero_count_cache[scan8[0]+3]);
                    }else{
                    const uint8_t *mbn_nnz = h->non_zero_count[mbn_xy] + 3*4;
                    int i;
                    for( i = 0; i < 4; i++ ) {
                        bS[i] = 1 + !!(h->non_zero_count_cache[scan8[0]+i] | mbn_nnz[i]);
                    }
                    }
                }
                // Do not use s->qscale as luma quantizer because it has not the same
                // value in IPCM macroblocks.
                qp = (h->cur_pic.qscale_table[mb_xy] + h->cur_pic.qscale_table[mbn_xy] + 1) >> 1;
                tprintf(h->avctx, "filter mb:%d/%d dir:%d edge:%d, QPy:%d ls:%d uvls:%d", mb_x, mb_y, dir, edge, qp, tmp_linesize, tmp_uvlinesize);
                { int i; for (i = 0; i < 4; i++) tprintf(h->avctx, " bS[%d]:%d", i, bS[i]); tprintf(h->avctx, "\n"); }
                filter_mb_edgeh( &img_y[j*linesize], tmp_linesize, bS, qp, a, b, h, 0 );
                chroma_qp_avg[0] = (h->chroma_qp[0] + get_chroma_qp(h, 0, h->cur_pic.qscale_table[mbn_xy]) + 1) >> 1;
                chroma_qp_avg[1] = (h->chroma_qp[1] + get_chroma_qp(h, 1, h->cur_pic.qscale_table[mbn_xy]) + 1) >> 1;
                if (chroma) {
                    if (chroma444) {
                        filter_mb_edgeh (&img_cb[j*uvlinesize], tmp_uvlinesize, bS, chroma_qp_avg[0], a, b, h, 0);
                        filter_mb_edgeh (&img_cr[j*uvlinesize], tmp_uvlinesize, bS, chroma_qp_avg[1], a, b, h, 0);
                    } else {
                        filter_mb_edgech(&img_cb[j*uvlinesize], tmp_uvlinesize, bS, chroma_qp_avg[0], a, b, h, 0);
                        filter_mb_edgech(&img_cr[j*uvlinesize], tmp_uvlinesize, bS, chroma_qp_avg[1], a, b, h, 0);
                    }
                }
            }
        }else{
            DECLARE_ALIGNED(8, int16_t, bS)[4];
            int qp;

            if( IS_INTRA(mb_type|mbm_type)) {
                AV_WN64A(bS, 0x0003000300030003ULL);
                if (   (!IS_INTERLACED(mb_type|mbm_type))
                    || ((FRAME_MBAFF(h) || (h->picture_structure != PICT_FRAME)) && (dir == 0))
                )
                    AV_WN64A(bS, 0x0004000400040004ULL);
            } else {
                int i;
                int mv_done;

                if( dir && FRAME_MBAFF(h) && IS_INTERLACED(mb_type ^ mbm_type)) {
                    AV_WN64A(bS, 0x0001000100010001ULL);
                    mv_done = 1;
                }
                else if( mask_par0 && ((mbm_type & (MB_TYPE_16x16 | (MB_TYPE_8x16 >> dir)))) ) {
                    int b_idx= 8 + 4;
                    int bn_idx= b_idx - (dir ? 8:1);

                    bS[0] = bS[1] = bS[2] = bS[3] = check_mv(h, 8 + 4, bn_idx, mvy_limit);
                    mv_done = 1;
                }
                else
                    mv_done = 0;

                for( i = 0; i < 4; i++ ) {
                    int x = dir == 0 ? 0 : i;
                    int y = dir == 0 ? i    : 0;
                    int b_idx= 8 + 4 + x + 8*y;
                    int bn_idx= b_idx - (dir ? 8:1);

                    if( h->non_zero_count_cache[b_idx] |
                        h->non_zero_count_cache[bn_idx] ) {
                        bS[i] = 2;
                    }
                    else if(!mv_done)
                    {
                        bS[i] = check_mv(h, b_idx, bn_idx, mvy_limit);
                    }
                }
            }

            /* Filter edge */
            // Do not use s->qscale as luma quantizer because it has not the same
            // value in IPCM macroblocks.
            if(bS[0]+bS[1]+bS[2]+bS[3]){
                qp = (h->cur_pic.qscale_table[mb_xy] + h->cur_pic.qscale_table[mbm_xy] + 1) >> 1;
                //tprintf(h->avctx, "filter mb:%d/%d dir:%d edge:%d, QPy:%d, QPc:%d, QPcn:%d\n", mb_x, mb_y, dir, edge, qp, h->chroma_qp[0], h->cur_pic.qscale_table[mbn_xy]);
                tprintf(h->avctx, "filter mb:%d/%d dir:%d edge:%d, QPy:%d ls:%d uvls:%d", mb_x, mb_y, dir, edge, qp, linesize, uvlinesize);
                //{ int i; for (i = 0; i < 4; i++) tprintf(h->avctx, " bS[%d]:%d", i, bS[i]); tprintf(h->avctx, "\n"); }
                chroma_qp_avg[0] = (h->chroma_qp[0] + get_chroma_qp(h, 0, h->cur_pic.qscale_table[mbm_xy]) + 1) >> 1;
                chroma_qp_avg[1] = (h->chroma_qp[1] + get_chroma_qp(h, 1, h->cur_pic.qscale_table[mbm_xy]) + 1) >> 1;
                if( dir == 0 ) {
                    filter_mb_edgev( &img_y[0], linesize, bS, qp, a, b, h, 1 );
                    if (chroma) {
                        if (chroma444) {
                            filter_mb_edgev ( &img_cb[0], uvlinesize, bS, chroma_qp_avg[0], a, b, h, 1);
                            filter_mb_edgev ( &img_cr[0], uvlinesize, bS, chroma_qp_avg[1], a, b, h, 1);
                        } else {
                            filter_mb_edgecv( &img_cb[0], uvlinesize, bS, chroma_qp_avg[0], a, b, h, 1);
                            filter_mb_edgecv( &img_cr[0], uvlinesize, bS, chroma_qp_avg[1], a, b, h, 1);
                        }
                    }
                } else {
                    filter_mb_edgeh( &img_y[0], linesize, bS, qp, a, b, h, 1 );
                    if (chroma) {
                        if (chroma444) {
                            filter_mb_edgeh ( &img_cb[0], uvlinesize, bS, chroma_qp_avg[0], a, b, h, 1);
                            filter_mb_edgeh ( &img_cr[0], uvlinesize, bS, chroma_qp_avg[1], a, b, h, 1);
                        } else {
                            filter_mb_edgech( &img_cb[0], uvlinesize, bS, chroma_qp_avg[0], a, b, h, 1);
                            filter_mb_edgech( &img_cr[0], uvlinesize, bS, chroma_qp_avg[1], a, b, h, 1);
                        }
                    }
                }
            }
        }
    }

    /* Calculate bS */
    for( edge = 1; edge < edges; edge++ ) {
        DECLARE_ALIGNED(8, int16_t, bS)[4];
        int qp;
        const int deblock_edge = !IS_8x8DCT(mb_type & (edge<<24)); // (edge&1) && IS_8x8DCT(mb_type)

        if (!deblock_edge && (!chroma422 || dir == 0))
            continue;

        if( IS_INTRA(mb_type)) {
            AV_WN64A(bS, 0x0003000300030003ULL);
        } else {
            int i;
            int mv_done;

            if( edge & mask_edge ) {
                AV_ZERO64(bS);
                mv_done = 1;
            }
            else if( mask_par0 ) {
                int b_idx= 8 + 4 + edge * (dir ? 8:1);
                int bn_idx= b_idx - (dir ? 8:1);

                bS[0] = bS[1] = bS[2] = bS[3] = check_mv(h, b_idx, bn_idx, mvy_limit);
                mv_done = 1;
            }
            else
                mv_done = 0;

            for( i = 0; i < 4; i++ ) {
                int x = dir == 0 ? edge : i;
                int y = dir == 0 ? i    : edge;
                int b_idx= 8 + 4 + x + 8*y;
                int bn_idx= b_idx - (dir ? 8:1);

                if( h->non_zero_count_cache[b_idx] |
                    h->non_zero_count_cache[bn_idx] ) {
                    bS[i] = 2;
                }
                else if(!mv_done)
                {
                    bS[i] = check_mv(h, b_idx, bn_idx, mvy_limit);
                }
            }

            if(bS[0]+bS[1]+bS[2]+bS[3] == 0)
                continue;
        }

        /* Filter edge */
        // Do not use s->qscale as luma quantizer because it has not the same
        // value in IPCM macroblocks.
        qp = h->cur_pic.qscale_table[mb_xy];
        //tprintf(h->avctx, "filter mb:%d/%d dir:%d edge:%d, QPy:%d, QPc:%d, QPcn:%d\n", mb_x, mb_y, dir, edge, qp, h->chroma_qp[0], h->cur_pic.qscale_table[mbn_xy]);
        tprintf(h->avctx, "filter mb:%d/%d dir:%d edge:%d, QPy:%d ls:%d uvls:%d", mb_x, mb_y, dir, edge, qp, linesize, uvlinesize);
        //{ int i; for (i = 0; i < 4; i++) tprintf(h->avctx, " bS[%d]:%d", i, bS[i]); tprintf(h->avctx, "\n"); }
        if( dir == 0 ) {
            filter_mb_edgev( &img_y[4*edge << h->pixel_shift], linesize, bS, qp, a, b, h, 0 );
            if (chroma) {
                if (chroma444) {
                    filter_mb_edgev ( &img_cb[4*edge << h->pixel_shift], uvlinesize, bS, h->chroma_qp[0], a, b, h, 0);
                    filter_mb_edgev ( &img_cr[4*edge << h->pixel_shift], uvlinesize, bS, h->chroma_qp[1], a, b, h, 0);
                } else if( (edge&1) == 0 ) {
                    filter_mb_edgecv( &img_cb[2*edge << h->pixel_shift], uvlinesize, bS, h->chroma_qp[0], a, b, h, 0);
                    filter_mb_edgecv( &img_cr[2*edge << h->pixel_shift], uvlinesize, bS, h->chroma_qp[1], a, b, h, 0);
                }
            }
        } else {
            if (chroma422) {
                if (deblock_edge)
                    filter_mb_edgeh(&img_y[4*edge*linesize], linesize, bS, qp, a, b, h, 0);
                if (chroma) {
                    filter_mb_edgech(&img_cb[4*edge*uvlinesize], uvlinesize, bS, h->chroma_qp[0], a, b, h, 0);
                    filter_mb_edgech(&img_cr[4*edge*uvlinesize], uvlinesize, bS, h->chroma_qp[1], a, b, h, 0);
                }
            } else {
                filter_mb_edgeh(&img_y[4*edge*linesize], linesize, bS, qp, a, b, h, 0);
                if (chroma) {
                    if (chroma444) {
                        filter_mb_edgeh (&img_cb[4*edge*uvlinesize], uvlinesize, bS, h->chroma_qp[0], a, b, h, 0);
                        filter_mb_edgeh (&img_cr[4*edge*uvlinesize], uvlinesize, bS, h->chroma_qp[1], a, b, h, 0);
                    } else if ((edge&1) == 0) {
                        filter_mb_edgech(&img_cb[2*edge*uvlinesize], uvlinesize, bS, h->chroma_qp[0], a, b, h, 0);
                        filter_mb_edgech(&img_cr[2*edge*uvlinesize], uvlinesize, bS, h->chroma_qp[1], a, b, h, 0);
                    }
                }
            }
        }
    }
}

void ff_h264_filter_mb( H264Context *h, int mb_x, int mb_y, uint8_t *img_y, uint8_t *img_cb, uint8_t *img_cr, unsigned int linesize, unsigned int uvlinesize) {
    const int mb_xy= mb_x + mb_y*h->mb_stride;
    const int mb_type = h->cur_pic.mb_type[mb_xy];
    const int mvy_limit = IS_INTERLACED(mb_type) ? 2 : 4;
    int first_vertical_edge_done = 0;
    av_unused int dir;
    int chroma = CHROMA(h) && !(CONFIG_GRAY && (h->flags&CODEC_FLAG_GRAY));
    int qp_bd_offset = 6 * (h->sps.bit_depth_luma - 8);
    int a = 52 + h->slice_alpha_c0_offset - qp_bd_offset;
    int b = 52 + h->slice_beta_offset - qp_bd_offset;

    if (FRAME_MBAFF(h)
            // and current and left pair do not have the same interlaced type
            && IS_INTERLACED(mb_type^h->left_type[LTOP])
            // and left mb is in available to us
            && h->left_type[LTOP]) {
        /* First vertical edge is different in MBAFF frames
         * There are 8 different bS to compute and 2 different Qp
         */
        DECLARE_ALIGNED(8, int16_t, bS)[8];
        int qp[2];
        int bqp[2];
        int rqp[2];
        int mb_qp, mbn0_qp, mbn1_qp;
        int i;
        first_vertical_edge_done = 1;

        if( IS_INTRA(mb_type) ) {
            AV_WN64A(&bS[0], 0x0004000400040004ULL);
            AV_WN64A(&bS[4], 0x0004000400040004ULL);
        } else {
            static const uint8_t offset[2][2][8]={
                {
                    {3+4*0, 3+4*0, 3+4*0, 3+4*0, 3+4*1, 3+4*1, 3+4*1, 3+4*1},
                    {3+4*2, 3+4*2, 3+4*2, 3+4*2, 3+4*3, 3+4*3, 3+4*3, 3+4*3},
                },{
                    {3+4*0, 3+4*1, 3+4*2, 3+4*3, 3+4*0, 3+4*1, 3+4*2, 3+4*3},
                    {3+4*0, 3+4*1, 3+4*2, 3+4*3, 3+4*0, 3+4*1, 3+4*2, 3+4*3},
                }
            };
            const uint8_t *off= offset[MB_FIELD(h)][mb_y&1];
            for( i = 0; i < 8; i++ ) {
                int j= MB_FIELD(h) ? i>>2 : i&1;
                int mbn_xy = h->left_mb_xy[LEFT(j)];
                int mbn_type= h->left_type[LEFT(j)];

                if( IS_INTRA( mbn_type ) )
                    bS[i] = 4;
                else{
                    bS[i] = 1 + !!(h->non_zero_count_cache[12+8*(i>>1)] |
                         ((!h->pps.cabac && IS_8x8DCT(mbn_type)) ?
                            (h->cbp_table[mbn_xy] & (((MB_FIELD(h) ? (i&2) : (mb_y&1)) ? 8 : 2) << 12))
                                                                       :
                            h->non_zero_count[mbn_xy][ off[i] ]));
                }
            }
        }

        mb_qp   = h->cur_pic.qscale_table[mb_xy];
        mbn0_qp = h->cur_pic.qscale_table[h->left_mb_xy[0]];
        mbn1_qp = h->cur_pic.qscale_table[h->left_mb_xy[1]];
        qp[0] = ( mb_qp + mbn0_qp + 1 ) >> 1;
        bqp[0] = ( get_chroma_qp( h, 0, mb_qp ) +
                   get_chroma_qp( h, 0, mbn0_qp ) + 1 ) >> 1;
        rqp[0] = ( get_chroma_qp( h, 1, mb_qp ) +
                   get_chroma_qp( h, 1, mbn0_qp ) + 1 ) >> 1;
        qp[1] = ( mb_qp + mbn1_qp + 1 ) >> 1;
        bqp[1] = ( get_chroma_qp( h, 0, mb_qp ) +
                   get_chroma_qp( h, 0, mbn1_qp ) + 1 ) >> 1;
        rqp[1] = ( get_chroma_qp( h, 1, mb_qp ) +
                   get_chroma_qp( h, 1, mbn1_qp ) + 1 ) >> 1;

        /* Filter edge */
        tprintf(h->avctx, "filter mb:%d/%d MBAFF, QPy:%d/%d, QPb:%d/%d QPr:%d/%d ls:%d uvls:%d", mb_x, mb_y, qp[0], qp[1], bqp[0], bqp[1], rqp[0], rqp[1], linesize, uvlinesize);
        { int i; for (i = 0; i < 8; i++) tprintf(h->avctx, " bS[%d]:%d", i, bS[i]); tprintf(h->avctx, "\n"); }
        if (MB_FIELD(h)) {
            filter_mb_mbaff_edgev ( h, img_y                ,   linesize, bS  , 1, qp [0], a, b, 1 );
            filter_mb_mbaff_edgev ( h, img_y  + 8*  linesize,   linesize, bS+4, 1, qp [1], a, b, 1 );
            if (chroma){
                if (CHROMA444(h)) {
                    filter_mb_mbaff_edgev ( h, img_cb,                uvlinesize, bS  , 1, bqp[0], a, b, 1 );
                    filter_mb_mbaff_edgev ( h, img_cb + 8*uvlinesize, uvlinesize, bS+4, 1, bqp[1], a, b, 1 );
                    filter_mb_mbaff_edgev ( h, img_cr,                uvlinesize, bS  , 1, rqp[0], a, b, 1 );
                    filter_mb_mbaff_edgev ( h, img_cr + 8*uvlinesize, uvlinesize, bS+4, 1, rqp[1], a, b, 1 );
                } else if (CHROMA422(h)) {
                    filter_mb_mbaff_edgecv(h, img_cb,                uvlinesize, bS  , 1, bqp[0], a, b, 1);
                    filter_mb_mbaff_edgecv(h, img_cb + 8*uvlinesize, uvlinesize, bS+4, 1, bqp[1], a, b, 1);
                    filter_mb_mbaff_edgecv(h, img_cr,                uvlinesize, bS  , 1, rqp[0], a, b, 1);
                    filter_mb_mbaff_edgecv(h, img_cr + 8*uvlinesize, uvlinesize, bS+4, 1, rqp[1], a, b, 1);
                }else{
                    filter_mb_mbaff_edgecv( h, img_cb,                uvlinesize, bS  , 1, bqp[0], a, b, 1 );
                    filter_mb_mbaff_edgecv( h, img_cb + 4*uvlinesize, uvlinesize, bS+4, 1, bqp[1], a, b, 1 );
                    filter_mb_mbaff_edgecv( h, img_cr,                uvlinesize, bS  , 1, rqp[0], a, b, 1 );
                    filter_mb_mbaff_edgecv( h, img_cr + 4*uvlinesize, uvlinesize, bS+4, 1, rqp[1], a, b, 1 );
                }
            }
        }else{
            filter_mb_mbaff_edgev ( h, img_y              , 2*  linesize, bS  , 2, qp [0], a, b, 1 );
            filter_mb_mbaff_edgev ( h, img_y  +   linesize, 2*  linesize, bS+1, 2, qp [1], a, b, 1 );
            if (chroma){
                if (CHROMA444(h)) {
                    filter_mb_mbaff_edgev ( h, img_cb,              2*uvlinesize, bS  , 2, bqp[0], a, b, 1 );
                    filter_mb_mbaff_edgev ( h, img_cb + uvlinesize, 2*uvlinesize, bS+1, 2, bqp[1], a, b, 1 );
                    filter_mb_mbaff_edgev ( h, img_cr,              2*uvlinesize, bS  , 2, rqp[0], a, b, 1 );
                    filter_mb_mbaff_edgev ( h, img_cr + uvlinesize, 2*uvlinesize, bS+1, 2, rqp[1], a, b, 1 );
                }else{
                    filter_mb_mbaff_edgecv( h, img_cb,              2*uvlinesize, bS  , 2, bqp[0], a, b, 1 );
                    filter_mb_mbaff_edgecv( h, img_cb + uvlinesize, 2*uvlinesize, bS+1, 2, bqp[1], a, b, 1 );
                    filter_mb_mbaff_edgecv( h, img_cr,              2*uvlinesize, bS  , 2, rqp[0], a, b, 1 );
                    filter_mb_mbaff_edgecv( h, img_cr + uvlinesize, 2*uvlinesize, bS+1, 2, rqp[1], a, b, 1 );
                }
            }
        }
    }

#if CONFIG_SMALL
    for( dir = 0; dir < 2; dir++ )
        filter_mb_dir(h, mb_x, mb_y, img_y, img_cb, img_cr, linesize, uvlinesize, mb_xy, mb_type, mvy_limit, dir ? 0 : first_vertical_edge_done, a, b, chroma, dir);
#else
    filter_mb_dir(h, mb_x, mb_y, img_y, img_cb, img_cr, linesize, uvlinesize, mb_xy, mb_type, mvy_limit, first_vertical_edge_done, a, b, chroma, 0);
    filter_mb_dir(h, mb_x, mb_y, img_y, img_cb, img_cr, linesize, uvlinesize, mb_xy, mb_type, mvy_limit, 0,                        a, b, chroma, 1);
#endif
}
