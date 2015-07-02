/*
 * H.26L/H.264/AVC/JVT/14496-10/... encoder/decoder
 * Copyright (c) 2003-2011 Michael Niedermayer <michaelni@gmx.at>
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
 * H.264 / AVC / MPEG4 part10 DSP functions.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "bit_depth_template.c"

#define op_scale1(x)  block[x] = av_clip_pixel( (block[x]*weight + offset) >> log2_denom )
#define op_scale2(x)  dst[x] = av_clip_pixel( (src[x]*weights + dst[x]*weightd + offset) >> (log2_denom+1))
#define H264_WEIGHT(W) \
static void FUNCC(weight_h264_pixels ## W)(uint8_t *_block, int stride, int height, \
                                           int log2_denom, int weight, int offset) \
{ \
    int y; \
    pixel *block = (pixel*)_block; \
    stride >>= sizeof(pixel)-1; \
    offset = (unsigned)offset << (log2_denom + (BIT_DEPTH-8)); \
    if(log2_denom) offset += 1<<(log2_denom-1); \
    for (y = 0; y < height; y++, block += stride) { \
        op_scale1(0); \
        op_scale1(1); \
        if(W==2) continue; \
        op_scale1(2); \
        op_scale1(3); \
        if(W==4) continue; \
        op_scale1(4); \
        op_scale1(5); \
        op_scale1(6); \
        op_scale1(7); \
        if(W==8) continue; \
        op_scale1(8); \
        op_scale1(9); \
        op_scale1(10); \
        op_scale1(11); \
        op_scale1(12); \
        op_scale1(13); \
        op_scale1(14); \
        op_scale1(15); \
    } \
} \
static void FUNCC(biweight_h264_pixels ## W)(uint8_t *_dst, uint8_t *_src, int stride, int height, \
                                             int log2_denom, int weightd, int weights, int offset) \
{ \
    int y; \
    pixel *dst = (pixel*)_dst; \
    pixel *src = (pixel*)_src; \
    stride >>= sizeof(pixel)-1; \
    offset = (unsigned)offset << (BIT_DEPTH-8); \
    offset = (unsigned)((offset + 1) | 1) << log2_denom; \
    for (y = 0; y < height; y++, dst += stride, src += stride) { \
        op_scale2(0); \
        op_scale2(1); \
        if(W==2) continue; \
        op_scale2(2); \
        op_scale2(3); \
        if(W==4) continue; \
        op_scale2(4); \
        op_scale2(5); \
        op_scale2(6); \
        op_scale2(7); \
        if(W==8) continue; \
        op_scale2(8); \
        op_scale2(9); \
        op_scale2(10); \
        op_scale2(11); \
        op_scale2(12); \
        op_scale2(13); \
        op_scale2(14); \
        op_scale2(15); \
    } \
}

H264_WEIGHT(16)
H264_WEIGHT(8)
H264_WEIGHT(4)
H264_WEIGHT(2)

#undef op_scale1
#undef op_scale2
#undef H264_WEIGHT

static av_always_inline av_flatten void FUNCC(h264_loop_filter_luma)(uint8_t *p_pix, int xstride, int ystride, int inner_iters, int alpha, int beta, int8_t *tc0)
{
    pixel *pix = (pixel*)p_pix;
    int i, d;
    xstride >>= sizeof(pixel)-1;
    ystride >>= sizeof(pixel)-1;
    alpha <<= BIT_DEPTH - 8;
    beta  <<= BIT_DEPTH - 8;
    for( i = 0; i < 4; i++ ) {
        const int tc_orig = tc0[i] * (1 << (BIT_DEPTH - 8));
        if( tc_orig < 0 ) {
            pix += inner_iters*ystride;
            continue;
        }
        for( d = 0; d < inner_iters; d++ ) {
            const int p0 = pix[-1*xstride];
            const int p1 = pix[-2*xstride];
            const int p2 = pix[-3*xstride];
            const int q0 = pix[0];
            const int q1 = pix[1*xstride];
            const int q2 = pix[2*xstride];

            if( FFABS( p0 - q0 ) < alpha &&
                FFABS( p1 - p0 ) < beta &&
                FFABS( q1 - q0 ) < beta ) {

                int tc = tc_orig;
                int i_delta;

                if( FFABS( p2 - p0 ) < beta ) {
                    if(tc_orig)
                    pix[-2*xstride] = p1 + av_clip( (( p2 + ( ( p0 + q0 + 1 ) >> 1 ) ) >> 1) - p1, -tc_orig, tc_orig );
                    tc++;
                }
                if( FFABS( q2 - q0 ) < beta ) {
                    if(tc_orig)
                    pix[   xstride] = q1 + av_clip( (( q2 + ( ( p0 + q0 + 1 ) >> 1 ) ) >> 1) - q1, -tc_orig, tc_orig );
                    tc++;
                }

                i_delta = av_clip( (((q0 - p0 ) * 4) + (p1 - q1) + 4) >> 3, -tc, tc );
                pix[-xstride] = av_clip_pixel( p0 + i_delta );    /* p0' */
                pix[0]        = av_clip_pixel( q0 - i_delta );    /* q0' */
            }
            pix += ystride;
        }
    }
}
static void FUNCC(h264_v_loop_filter_luma)(uint8_t *pix, int stride, int alpha, int beta, int8_t *tc0)
{
    FUNCC(h264_loop_filter_luma)(pix, stride, sizeof(pixel), 4, alpha, beta, tc0);
}
static void FUNCC(h264_h_loop_filter_luma)(uint8_t *pix, int stride, int alpha, int beta, int8_t *tc0)
{
    FUNCC(h264_loop_filter_luma)(pix, sizeof(pixel), stride, 4, alpha, beta, tc0);
}
static void FUNCC(h264_h_loop_filter_luma_mbaff)(uint8_t *pix, int stride, int alpha, int beta, int8_t *tc0)
{
    FUNCC(h264_loop_filter_luma)(pix, sizeof(pixel), stride, 2, alpha, beta, tc0);
}

static av_always_inline av_flatten void FUNCC(h264_loop_filter_luma_intra)(uint8_t *p_pix, int xstride, int ystride, int inner_iters, int alpha, int beta)
{
    pixel *pix = (pixel*)p_pix;
    int d;
    xstride >>= sizeof(pixel)-1;
    ystride >>= sizeof(pixel)-1;
    alpha <<= BIT_DEPTH - 8;
    beta  <<= BIT_DEPTH - 8;
    for( d = 0; d < 4 * inner_iters; d++ ) {
        const int p2 = pix[-3*xstride];
        const int p1 = pix[-2*xstride];
        const int p0 = pix[-1*xstride];

        const int q0 = pix[ 0*xstride];
        const int q1 = pix[ 1*xstride];
        const int q2 = pix[ 2*xstride];

        if( FFABS( p0 - q0 ) < alpha &&
            FFABS( p1 - p0 ) < beta &&
            FFABS( q1 - q0 ) < beta ) {

            if(FFABS( p0 - q0 ) < (( alpha >> 2 ) + 2 )){
                if( FFABS( p2 - p0 ) < beta)
                {
                    const int p3 = pix[-4*xstride];
                    /* p0', p1', p2' */
                    pix[-1*xstride] = ( p2 + 2*p1 + 2*p0 + 2*q0 + q1 + 4 ) >> 3;
                    pix[-2*xstride] = ( p2 + p1 + p0 + q0 + 2 ) >> 2;
                    pix[-3*xstride] = ( 2*p3 + 3*p2 + p1 + p0 + q0 + 4 ) >> 3;
                } else {
                    /* p0' */
                    pix[-1*xstride] = ( 2*p1 + p0 + q1 + 2 ) >> 2;
                }
                if( FFABS( q2 - q0 ) < beta)
                {
                    const int q3 = pix[3*xstride];
                    /* q0', q1', q2' */
                    pix[0*xstride] = ( p1 + 2*p0 + 2*q0 + 2*q1 + q2 + 4 ) >> 3;
                    pix[1*xstride] = ( p0 + q0 + q1 + q2 + 2 ) >> 2;
                    pix[2*xstride] = ( 2*q3 + 3*q2 + q1 + q0 + p0 + 4 ) >> 3;
                } else {
                    /* q0' */
                    pix[0*xstride] = ( 2*q1 + q0 + p1 + 2 ) >> 2;
                }
            }else{
                /* p0', q0' */
                pix[-1*xstride] = ( 2*p1 + p0 + q1 + 2 ) >> 2;
                pix[ 0*xstride] = ( 2*q1 + q0 + p1 + 2 ) >> 2;
            }
        }
        pix += ystride;
    }
}
static void FUNCC(h264_v_loop_filter_luma_intra)(uint8_t *pix, int stride, int alpha, int beta)
{
    FUNCC(h264_loop_filter_luma_intra)(pix, stride, sizeof(pixel), 4, alpha, beta);
}
static void FUNCC(h264_h_loop_filter_luma_intra)(uint8_t *pix, int stride, int alpha, int beta)
{
    FUNCC(h264_loop_filter_luma_intra)(pix, sizeof(pixel), stride, 4, alpha, beta);
}
static void FUNCC(h264_h_loop_filter_luma_mbaff_intra)(uint8_t *pix, int stride, int alpha, int beta)
{
    FUNCC(h264_loop_filter_luma_intra)(pix, sizeof(pixel), stride, 2, alpha, beta);
}

static av_always_inline av_flatten void FUNCC(h264_loop_filter_chroma)(uint8_t *p_pix, int xstride, int ystride, int inner_iters, int alpha, int beta, int8_t *tc0)
{
    pixel *pix = (pixel*)p_pix;
    int i, d;
    alpha <<= BIT_DEPTH - 8;
    beta  <<= BIT_DEPTH - 8;
    xstride >>= sizeof(pixel)-1;
    ystride >>= sizeof(pixel)-1;
    for( i = 0; i < 4; i++ ) {
        const int tc = ((tc0[i] - 1U) << (BIT_DEPTH - 8)) + 1;
        if( tc <= 0 ) {
            pix += inner_iters*ystride;
            continue;
        }
        for( d = 0; d < inner_iters; d++ ) {
            const int p0 = pix[-1*xstride];
            const int p1 = pix[-2*xstride];
            const int q0 = pix[0];
            const int q1 = pix[1*xstride];

            if( FFABS( p0 - q0 ) < alpha &&
                FFABS( p1 - p0 ) < beta &&
                FFABS( q1 - q0 ) < beta ) {

                int delta = av_clip( ((q0 - p0) * 4 + (p1 - q1) + 4) >> 3, -tc, tc );

                pix[-xstride] = av_clip_pixel( p0 + delta );    /* p0' */
                pix[0]        = av_clip_pixel( q0 - delta );    /* q0' */
            }
            pix += ystride;
        }
    }
}
static void FUNCC(h264_v_loop_filter_chroma)(uint8_t *pix, int stride, int alpha, int beta, int8_t *tc0)
{
    FUNCC(h264_loop_filter_chroma)(pix, stride, sizeof(pixel), 2, alpha, beta, tc0);
}
static void FUNCC(h264_h_loop_filter_chroma)(uint8_t *pix, int stride, int alpha, int beta, int8_t *tc0)
{
    FUNCC(h264_loop_filter_chroma)(pix, sizeof(pixel), stride, 2, alpha, beta, tc0);
}
static void FUNCC(h264_h_loop_filter_chroma_mbaff)(uint8_t *pix, int stride, int alpha, int beta, int8_t *tc0)
{
    FUNCC(h264_loop_filter_chroma)(pix, sizeof(pixel), stride, 1, alpha, beta, tc0);
}
static void FUNCC(h264_h_loop_filter_chroma422)(uint8_t *pix, int stride, int alpha, int beta, int8_t *tc0)
{
    FUNCC(h264_loop_filter_chroma)(pix, sizeof(pixel), stride, 4, alpha, beta, tc0);
}
static void FUNCC(h264_h_loop_filter_chroma422_mbaff)(uint8_t *pix, int stride, int alpha, int beta, int8_t *tc0)
{
    FUNCC(h264_loop_filter_chroma)(pix, sizeof(pixel), stride, 2, alpha, beta, tc0);
}

static av_always_inline av_flatten void FUNCC(h264_loop_filter_chroma_intra)(uint8_t *p_pix, int xstride, int ystride, int inner_iters, int alpha, int beta)
{
    pixel *pix = (pixel*)p_pix;
    int d;
    xstride >>= sizeof(pixel)-1;
    ystride >>= sizeof(pixel)-1;
    alpha <<= BIT_DEPTH - 8;
    beta  <<= BIT_DEPTH - 8;
    for( d = 0; d < 4 * inner_iters; d++ ) {
        const int p0 = pix[-1*xstride];
        const int p1 = pix[-2*xstride];
        const int q0 = pix[0];
        const int q1 = pix[1*xstride];

        if( FFABS( p0 - q0 ) < alpha &&
            FFABS( p1 - p0 ) < beta &&
            FFABS( q1 - q0 ) < beta ) {

            pix[-xstride] = ( 2*p1 + p0 + q1 + 2 ) >> 2;   /* p0' */
            pix[0]        = ( 2*q1 + q0 + p1 + 2 ) >> 2;   /* q0' */
        }
        pix += ystride;
    }
}
static void FUNCC(h264_v_loop_filter_chroma_intra)(uint8_t *pix, int stride, int alpha, int beta)
{
    FUNCC(h264_loop_filter_chroma_intra)(pix, stride, sizeof(pixel), 2, alpha, beta);
}
static void FUNCC(h264_h_loop_filter_chroma_intra)(uint8_t *pix, int stride, int alpha, int beta)
{
    FUNCC(h264_loop_filter_chroma_intra)(pix, sizeof(pixel), stride, 2, alpha, beta);
}
static void FUNCC(h264_h_loop_filter_chroma_mbaff_intra)(uint8_t *pix, int stride, int alpha, int beta)
{
    FUNCC(h264_loop_filter_chroma_intra)(pix, sizeof(pixel), stride, 1, alpha, beta);
}
static void FUNCC(h264_h_loop_filter_chroma422_intra)(uint8_t *pix, int stride, int alpha, int beta)
{
    FUNCC(h264_loop_filter_chroma_intra)(pix, sizeof(pixel), stride, 4, alpha, beta);
}
static void FUNCC(h264_h_loop_filter_chroma422_mbaff_intra)(uint8_t *pix, int stride, int alpha, int beta)
{
    FUNCC(h264_loop_filter_chroma_intra)(pix, sizeof(pixel), stride, 2, alpha, beta);
}
