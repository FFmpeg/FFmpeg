/*
 * Motion estimation 
 * Copyright (c) 2000,2001 Fabrice Bellard.
 * Copyright (c) 2002-2003 Michael Niedermayer
 * 
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * new Motion Estimation (X1/EPZS) by Michael Niedermayer <michaelni@gmx.at>
 */
 
/**
 * @file motion_est.c
 * Motion estimation.
 */
 
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"

//#undef NDEBUG
//#include <assert.h>

#define SQ(a) ((a)*(a))

#define P_LEFT P[1]
#define P_TOP P[2]
#define P_TOPRIGHT P[3]
#define P_MEDIAN P[4]
#define P_MV1 P[9]

static inline int sad_hpel_motion_search(MpegEncContext * s,
				  int *mx_ptr, int *my_ptr, int dmin,
				  int xmin, int ymin, int xmax, int ymax,
                                  int pred_x, int pred_y, Picture *picture,
                                  int n, int size, uint8_t * const mv_penalty);

static inline int update_map_generation(MpegEncContext * s)
{
    s->me.map_generation+= 1<<(ME_MAP_MV_BITS*2);
    if(s->me.map_generation==0){
        s->me.map_generation= 1<<(ME_MAP_MV_BITS*2);
        memset(s->me.map, 0, sizeof(uint32_t)*ME_MAP_SIZE);
    }
    return s->me.map_generation;
}

/* shape adaptive search stuff */
typedef struct Minima{
    int height;
    int x, y;
    int checked;
}Minima;

static int minima_cmp(const void *a, const void *b){
    const Minima *da = (const Minima *) a;
    const Minima *db = (const Minima *) b;
    
    return da->height - db->height;
}
                                  
/* SIMPLE */
#define RENAME(a) simple_ ## a

#define CMP(d, x, y, size)\
d = cmp(s, src_y, (ref_y) + (x) + (y)*(stride), stride);

#define CMP_HPEL(d, dx, dy, x, y, size)\
{\
    const int dxy= (dx) + 2*(dy);\
    hpel_put[0][dxy](s->me.scratchpad, (ref_y) + (x) + (y)*(stride), stride, (16>>size));\
    d = cmp_sub(s, s->me.scratchpad, src_y, stride);\
}

#define CMP_QPEL(d, dx, dy, x, y, size)\
{\
    const int dxy= (dx) + 4*(dy);\
    qpel_put[0][dxy](s->me.scratchpad, (ref_y) + (x) + (y)*(stride), stride);\
    d = cmp_sub(s, s->me.scratchpad, src_y, stride);\
}

#include "motion_est_template.c"
#undef RENAME
#undef CMP
#undef CMP_HPEL
#undef CMP_QPEL
#undef INIT

/* SIMPLE CHROMA */
#define RENAME(a) simple_chroma_ ## a

#define CMP(d, x, y, size)\
d = cmp(s, src_y, (ref_y) + (x) + (y)*(stride), stride);\
if(chroma_cmp){\
    int dxy= ((x)&1) + 2*((y)&1);\
    int c= ((x)>>1) + ((y)>>1)*uvstride;\
\
    chroma_hpel_put[0][dxy](s->me.scratchpad, ref_u + c, uvstride, 8);\
    d += chroma_cmp(s, s->me.scratchpad, src_u, uvstride);\
    chroma_hpel_put[0][dxy](s->me.scratchpad, ref_v + c, uvstride, 8);\
    d += chroma_cmp(s, s->me.scratchpad, src_v, uvstride);\
}

#define CMP_HPEL(d, dx, dy, x, y, size)\
{\
    const int dxy= (dx) + 2*(dy);\
    hpel_put[0][dxy](s->me.scratchpad, (ref_y) + (x) + (y)*(stride), stride, (16>>size));\
    d = cmp_sub(s, s->me.scratchpad, src_y, stride);\
    if(chroma_cmp_sub){\
        int cxy= (dxy) | ((x)&1) | (2*((y)&1));\
        int c= ((x)>>1) + ((y)>>1)*uvstride;\
        chroma_hpel_put[0][cxy](s->me.scratchpad, ref_u + c, uvstride, 8);\
        d += chroma_cmp_sub(s, s->me.scratchpad, src_u, uvstride);\
        chroma_hpel_put[0][cxy](s->me.scratchpad, ref_v + c, uvstride, 8);\
        d += chroma_cmp_sub(s, s->me.scratchpad, src_v, uvstride);\
    }\
}

#define CMP_QPEL(d, dx, dy, x, y, size)\
{\
    const int dxy= (dx) + 4*(dy);\
    qpel_put[0][dxy](s->me.scratchpad, (ref_y) + (x) + (y)*(stride), stride);\
    d = cmp_sub(s, s->me.scratchpad, src_y, stride);\
    if(chroma_cmp_sub){\
        int cxy, c;\
        int cx= (4*(x) + (dx))/2;\
        int cy= (4*(y) + (dy))/2;\
        cx= (cx>>1)|(cx&1);\
        cy= (cy>>1)|(cy&1);\
        cxy= (cx&1) + 2*(cy&1);\
        c= ((cx)>>1) + ((cy)>>1)*uvstride;\
        chroma_hpel_put[0][cxy](s->me.scratchpad, ref_u + c, uvstride, 8);\
        d += chroma_cmp_sub(s, s->me.scratchpad, src_u, uvstride);\
        chroma_hpel_put[0][cxy](s->me.scratchpad, ref_v + c, uvstride, 8);\
        d += chroma_cmp_sub(s, s->me.scratchpad, src_v, uvstride);\
    }\
}

#include "motion_est_template.c"
#undef RENAME
#undef CMP
#undef CMP_HPEL
#undef CMP_QPEL
#undef INIT

/* SIMPLE DIRECT HPEL */
#define RENAME(a) simple_direct_hpel_ ## a
//FIXME precalc divisions stuff

#define CMP_DIRECT(d, dx, dy, x, y, size, cmp_func)\
if((x) >= xmin && 2*(x) + (dx) <= 2*xmax && (y) >= ymin && 2*(y) + (dy) <= 2*ymax){\
    const int hx= 2*(x) + (dx);\
    const int hy= 2*(y) + (dy);\
    if(s->mv_type==MV_TYPE_8X8){\
        int i;\
        for(i=0; i<4; i++){\
            int fx = s->me.direct_basis_mv[i][0] + hx;\
            int fy = s->me.direct_basis_mv[i][1] + hy;\
            int bx = hx ? fx - s->me.co_located_mv[i][0] : s->me.co_located_mv[i][0]*(time_pb - time_pp)/time_pp + (i &1)*16;\
            int by = hy ? fy - s->me.co_located_mv[i][1] : s->me.co_located_mv[i][1]*(time_pb - time_pp)/time_pp + (i>>1)*16;\
            int fxy= (fx&1) + 2*(fy&1);\
            int bxy= (bx&1) + 2*(by&1);\
\
            uint8_t *dst= s->me.scratchpad + 8*(i&1) + 8*stride*(i>>1);\
            hpel_put[1][fxy](dst, (ref_y ) + (fx>>1) + (fy>>1)*(stride), stride, 8);\
            hpel_avg[1][bxy](dst, (ref2_y) + (bx>>1) + (by>>1)*(stride), stride, 8);\
        }\
    }else{\
        int fx = s->me.direct_basis_mv[0][0] + hx;\
        int fy = s->me.direct_basis_mv[0][1] + hy;\
        int bx = hx ? fx - s->me.co_located_mv[0][0] : (s->me.co_located_mv[0][0]*(time_pb - time_pp)/time_pp);\
        int by = hy ? fy - s->me.co_located_mv[0][1] : (s->me.co_located_mv[0][1]*(time_pb - time_pp)/time_pp);\
        int fxy= (fx&1) + 2*(fy&1);\
        int bxy= (bx&1) + 2*(by&1);\
        \
        assert((fx>>1) + 16*s->mb_x >= -16);\
        assert((fy>>1) + 16*s->mb_y >= -16);\
        assert((fx>>1) + 16*s->mb_x <= s->width);\
        assert((fy>>1) + 16*s->mb_y <= s->height);\
        assert((bx>>1) + 16*s->mb_x >= -16);\
        assert((by>>1) + 16*s->mb_y >= -16);\
        assert((bx>>1) + 16*s->mb_x <= s->width);\
        assert((by>>1) + 16*s->mb_y <= s->height);\
\
        hpel_put[0][fxy](s->me.scratchpad, (ref_y ) + (fx>>1) + (fy>>1)*(stride), stride, 16);\
        hpel_avg[0][bxy](s->me.scratchpad, (ref2_y) + (bx>>1) + (by>>1)*(stride), stride, 16);\
    }\
    d = cmp_func(s, s->me.scratchpad, src_y, stride);\
}else\
    d= 256*256*256*32;


#define CMP_HPEL(d, dx, dy, x, y, size)\
    CMP_DIRECT(d, dx, dy, x, y, size, cmp_sub)

#define CMP(d, x, y, size)\
    CMP_DIRECT(d, 0, 0, x, y, size, cmp)
    
#include "motion_est_template.c"
#undef RENAME
#undef CMP
#undef CMP_HPEL
#undef CMP_QPEL
#undef INIT
#undef CMP_DIRECT

/* SIMPLE DIRECT QPEL */
#define RENAME(a) simple_direct_qpel_ ## a

#define CMP_DIRECT(d, dx, dy, x, y, size, cmp_func)\
if((x) >= xmin && 4*(x) + (dx) <= 4*xmax && (y) >= ymin && 4*(y) + (dy) <= 4*ymax){\
    const int qx= 4*(x) + (dx);\
    const int qy= 4*(y) + (dy);\
    if(s->mv_type==MV_TYPE_8X8){\
        int i;\
        for(i=0; i<4; i++){\
            int fx = s->me.direct_basis_mv[i][0] + qx;\
            int fy = s->me.direct_basis_mv[i][1] + qy;\
            int bx = qx ? fx - s->me.co_located_mv[i][0] : s->me.co_located_mv[i][0]*(time_pb - time_pp)/time_pp + (i &1)*16;\
            int by = qy ? fy - s->me.co_located_mv[i][1] : s->me.co_located_mv[i][1]*(time_pb - time_pp)/time_pp + (i>>1)*16;\
            int fxy= (fx&3) + 4*(fy&3);\
            int bxy= (bx&3) + 4*(by&3);\
\
            uint8_t *dst= s->me.scratchpad + 8*(i&1) + 8*stride*(i>>1);\
            qpel_put[1][fxy](dst, (ref_y ) + (fx>>2) + (fy>>2)*(stride), stride);\
            qpel_avg[1][bxy](dst, (ref2_y) + (bx>>2) + (by>>2)*(stride), stride);\
        }\
    }else{\
        int fx = s->me.direct_basis_mv[0][0] + qx;\
        int fy = s->me.direct_basis_mv[0][1] + qy;\
        int bx = qx ? fx - s->me.co_located_mv[0][0] : s->me.co_located_mv[0][0]*(time_pb - time_pp)/time_pp;\
        int by = qy ? fy - s->me.co_located_mv[0][1] : s->me.co_located_mv[0][1]*(time_pb - time_pp)/time_pp;\
        int fxy= (fx&3) + 4*(fy&3);\
        int bxy= (bx&3) + 4*(by&3);\
\
        qpel_put[1][fxy](s->me.scratchpad               , (ref_y ) + (fx>>2) + (fy>>2)*(stride)               , stride);\
        qpel_put[1][fxy](s->me.scratchpad + 8           , (ref_y ) + (fx>>2) + (fy>>2)*(stride) + 8           , stride);\
        qpel_put[1][fxy](s->me.scratchpad     + 8*stride, (ref_y ) + (fx>>2) + (fy>>2)*(stride)     + 8*stride, stride);\
        qpel_put[1][fxy](s->me.scratchpad + 8 + 8*stride, (ref_y ) + (fx>>2) + (fy>>2)*(stride) + 8 + 8*stride, stride);\
        qpel_avg[1][bxy](s->me.scratchpad               , (ref2_y) + (bx>>2) + (by>>2)*(stride)               , stride);\
        qpel_avg[1][bxy](s->me.scratchpad + 8           , (ref2_y) + (bx>>2) + (by>>2)*(stride) + 8           , stride);\
        qpel_avg[1][bxy](s->me.scratchpad     + 8*stride, (ref2_y) + (bx>>2) + (by>>2)*(stride)     + 8*stride, stride);\
        qpel_avg[1][bxy](s->me.scratchpad + 8 + 8*stride, (ref2_y) + (bx>>2) + (by>>2)*(stride) + 8 + 8*stride, stride);\
    }\
    d = cmp_func(s, s->me.scratchpad, src_y, stride);\
}else\
    d= 256*256*256*32;


#define CMP_QPEL(d, dx, dy, x, y, size)\
    CMP_DIRECT(d, dx, dy, x, y, size, cmp_sub)

#define CMP(d, x, y, size)\
    CMP_DIRECT(d, 0, 0, x, y, size, cmp)

#include "motion_est_template.c"
#undef RENAME
#undef CMP
#undef CMP_HPEL
#undef CMP_QPEL
#undef INIT
#undef CMP__DIRECT


static int zero_cmp(void *s, uint8_t *a, uint8_t *b, int stride){
    return 0;
}

static void set_cmp(MpegEncContext *s, me_cmp_func *cmp, int type){
    DSPContext* c= &s->dsp;
    int i;
    
    memset(cmp, 0, sizeof(void*)*11);

    switch(type&0xFF){
    case FF_CMP_SAD:
        cmp[0]= c->sad[0];
        cmp[1]= c->sad[1];
        break;
    case FF_CMP_SATD:
        cmp[0]= c->hadamard8_diff[0];
        cmp[1]= c->hadamard8_diff[1];
        break;
    case FF_CMP_SSE:
        cmp[0]= c->sse[0];
        cmp[1]= c->sse[1];
        break;
    case FF_CMP_DCT:
        cmp[0]= c->dct_sad[0];
        cmp[1]= c->dct_sad[1];
        break;
    case FF_CMP_PSNR:
        cmp[0]= c->quant_psnr[0];
        cmp[1]= c->quant_psnr[1];
        break;
    case FF_CMP_BIT:
        cmp[0]= c->bit[0];
        cmp[1]= c->bit[1];
        break;
    case FF_CMP_RD:
        cmp[0]= c->rd[0];
        cmp[1]= c->rd[1];
        break;
    case FF_CMP_ZERO:
        for(i=0; i<7; i++){
            cmp[i]= zero_cmp;
        }
        break;
    default:
        fprintf(stderr,"internal error in cmp function selection\n");
    }
}

static inline int get_penalty_factor(MpegEncContext *s, int type){
    switch(type&0xFF){
    default:
    case FF_CMP_SAD:
        return s->qscale*2;
    case FF_CMP_DCT:
        return s->qscale*3;
    case FF_CMP_SATD:
        return s->qscale*6;
    case FF_CMP_SSE:
        return s->qscale*s->qscale*2;
    case FF_CMP_BIT:
        return 1;
    case FF_CMP_RD:
    case FF_CMP_PSNR:
        return (s->qscale*s->qscale*185 + 64)>>7;
    }
}

void ff_init_me(MpegEncContext *s){
    set_cmp(s, s->dsp.me_pre_cmp, s->avctx->me_pre_cmp);
    set_cmp(s, s->dsp.me_cmp, s->avctx->me_cmp);
    set_cmp(s, s->dsp.me_sub_cmp, s->avctx->me_sub_cmp);
    set_cmp(s, s->dsp.mb_cmp, s->avctx->mb_cmp);

    if(s->flags&CODEC_FLAG_QPEL){
        if(s->avctx->me_sub_cmp&FF_CMP_CHROMA)
            s->me.sub_motion_search= simple_chroma_qpel_motion_search;
        else
            s->me.sub_motion_search= simple_qpel_motion_search;
    }else{
        if(s->avctx->me_sub_cmp&FF_CMP_CHROMA)
            s->me.sub_motion_search= simple_chroma_hpel_motion_search;
        else if(   s->avctx->me_sub_cmp == FF_CMP_SAD 
                && s->avctx->    me_cmp == FF_CMP_SAD 
                && s->avctx->    mb_cmp == FF_CMP_SAD)
            s->me.sub_motion_search= sad_hpel_motion_search;
        else
            s->me.sub_motion_search= simple_hpel_motion_search;
    }

    if(s->avctx->me_cmp&FF_CMP_CHROMA){
        s->me.motion_search[0]= simple_chroma_epzs_motion_search;
        s->me.motion_search[1]= simple_chroma_epzs_motion_search4;
    }else{
        s->me.motion_search[0]= simple_epzs_motion_search;
        s->me.motion_search[1]= simple_epzs_motion_search4;
    }
    
    if(s->avctx->me_pre_cmp&FF_CMP_CHROMA){
        s->me.pre_motion_search= simple_chroma_epzs_motion_search;
    }else{
        s->me.pre_motion_search= simple_epzs_motion_search;
    }
    
    if(s->flags&CODEC_FLAG_QPEL){
        if(s->avctx->mb_cmp&FF_CMP_CHROMA)
            s->me.get_mb_score= simple_chroma_qpel_get_mb_score;
        else
            s->me.get_mb_score= simple_qpel_get_mb_score;
    }else{
        if(s->avctx->mb_cmp&FF_CMP_CHROMA)
            s->me.get_mb_score= simple_chroma_hpel_get_mb_score;
        else
            s->me.get_mb_score= simple_hpel_get_mb_score;
    }
}
      
#if 0
static int pix_dev(uint8_t * pix, int line_size, int mean)
{
    int s, i, j;

    s = 0;
    for (i = 0; i < 16; i++) {
	for (j = 0; j < 16; j += 8) {
	    s += ABS(pix[0]-mean);
	    s += ABS(pix[1]-mean);
	    s += ABS(pix[2]-mean);
	    s += ABS(pix[3]-mean);
	    s += ABS(pix[4]-mean);
	    s += ABS(pix[5]-mean);
	    s += ABS(pix[6]-mean);
	    s += ABS(pix[7]-mean);
	    pix += 8;
	}
	pix += line_size - 16;
    }
    return s;
}
#endif

static inline void no_motion_search(MpegEncContext * s,
				    int *mx_ptr, int *my_ptr)
{
    *mx_ptr = 16 * s->mb_x;
    *my_ptr = 16 * s->mb_y;
}

static int full_motion_search(MpegEncContext * s,
                              int *mx_ptr, int *my_ptr, int range,
                              int xmin, int ymin, int xmax, int ymax, uint8_t *ref_picture)
{
    int x1, y1, x2, y2, xx, yy, x, y;
    int mx, my, dmin, d;
    uint8_t *pix;

    xx = 16 * s->mb_x;
    yy = 16 * s->mb_y;
    x1 = xx - range + 1;	/* we loose one pixel to avoid boundary pb with half pixel pred */
    if (x1 < xmin)
	x1 = xmin;
    x2 = xx + range - 1;
    if (x2 > xmax)
	x2 = xmax;
    y1 = yy - range + 1;
    if (y1 < ymin)
	y1 = ymin;
    y2 = yy + range - 1;
    if (y2 > ymax)
	y2 = ymax;
    pix = s->new_picture.data[0] + (yy * s->linesize) + xx;
    dmin = 0x7fffffff;
    mx = 0;
    my = 0;
    for (y = y1; y <= y2; y++) {
	for (x = x1; x <= x2; x++) {
	    d = s->dsp.pix_abs16x16(pix, ref_picture + (y * s->linesize) + x,
			     s->linesize);
	    if (d < dmin ||
		(d == dmin &&
		 (abs(x - xx) + abs(y - yy)) <
		 (abs(mx - xx) + abs(my - yy)))) {
		dmin = d;
		mx = x;
		my = y;
	    }
	}
    }

    *mx_ptr = mx;
    *my_ptr = my;

#if 0
    if (*mx_ptr < -(2 * range) || *mx_ptr >= (2 * range) ||
	*my_ptr < -(2 * range) || *my_ptr >= (2 * range)) {
	fprintf(stderr, "error %d %d\n", *mx_ptr, *my_ptr);
    }
#endif
    return dmin;
}


static int log_motion_search(MpegEncContext * s,
                             int *mx_ptr, int *my_ptr, int range,
                             int xmin, int ymin, int xmax, int ymax, uint8_t *ref_picture)
{
    int x1, y1, x2, y2, xx, yy, x, y;
    int mx, my, dmin, d;
    uint8_t *pix;

    xx = s->mb_x << 4;
    yy = s->mb_y << 4;

    /* Left limit */
    x1 = xx - range;
    if (x1 < xmin)
	x1 = xmin;

    /* Right limit */
    x2 = xx + range;
    if (x2 > xmax)
	x2 = xmax;

    /* Upper limit */
    y1 = yy - range;
    if (y1 < ymin)
	y1 = ymin;

    /* Lower limit */
    y2 = yy + range;
    if (y2 > ymax)
	y2 = ymax;

    pix = s->new_picture.data[0] + (yy * s->linesize) + xx;
    dmin = 0x7fffffff;
    mx = 0;
    my = 0;

    do {
	for (y = y1; y <= y2; y += range) {
	    for (x = x1; x <= x2; x += range) {
		d = s->dsp.pix_abs16x16(pix, ref_picture + (y * s->linesize) + x, s->linesize);
		if (d < dmin || (d == dmin && (abs(x - xx) + abs(y - yy)) < (abs(mx - xx) + abs(my - yy)))) {
		    dmin = d;
		    mx = x;
		    my = y;
		}
	    }
	}

	range = range >> 1;

	x1 = mx - range;
	if (x1 < xmin)
	    x1 = xmin;

	x2 = mx + range;
	if (x2 > xmax)
	    x2 = xmax;

	y1 = my - range;
	if (y1 < ymin)
	    y1 = ymin;

	y2 = my + range;
	if (y2 > ymax)
	    y2 = ymax;

    } while (range >= 1);

#ifdef DEBUG
    fprintf(stderr, "log       - MX: %d\tMY: %d\n", mx, my);
#endif
    *mx_ptr = mx;
    *my_ptr = my;
    return dmin;
}

static int phods_motion_search(MpegEncContext * s,
                               int *mx_ptr, int *my_ptr, int range,
                               int xmin, int ymin, int xmax, int ymax, uint8_t *ref_picture)
{
    int x1, y1, x2, y2, xx, yy, x, y, lastx, d;
    int mx, my, dminx, dminy;
    uint8_t *pix;

    xx = s->mb_x << 4;
    yy = s->mb_y << 4;

    /* Left limit */
    x1 = xx - range;
    if (x1 < xmin)
	x1 = xmin;

    /* Right limit */
    x2 = xx + range;
    if (x2 > xmax)
	x2 = xmax;

    /* Upper limit */
    y1 = yy - range;
    if (y1 < ymin)
	y1 = ymin;

    /* Lower limit */
    y2 = yy + range;
    if (y2 > ymax)
	y2 = ymax;

    pix = s->new_picture.data[0] + (yy * s->linesize) + xx;
    mx = 0;
    my = 0;

    x = xx;
    y = yy;
    do {
        dminx = 0x7fffffff;
        dminy = 0x7fffffff;

	lastx = x;
	for (x = x1; x <= x2; x += range) {
	    d = s->dsp.pix_abs16x16(pix, ref_picture + (y * s->linesize) + x, s->linesize);
	    if (d < dminx || (d == dminx && (abs(x - xx) + abs(y - yy)) < (abs(mx - xx) + abs(my - yy)))) {
		dminx = d;
		mx = x;
	    }
	}

	x = lastx;
	for (y = y1; y <= y2; y += range) {
	    d = s->dsp.pix_abs16x16(pix, ref_picture + (y * s->linesize) + x, s->linesize);
	    if (d < dminy || (d == dminy && (abs(x - xx) + abs(y - yy)) < (abs(mx - xx) + abs(my - yy)))) {
		dminy = d;
		my = y;
	    }
	}

	range = range >> 1;

	x = mx;
	y = my;
	x1 = mx - range;
	if (x1 < xmin)
	    x1 = xmin;

	x2 = mx + range;
	if (x2 > xmax)
	    x2 = xmax;

	y1 = my - range;
	if (y1 < ymin)
	    y1 = ymin;

	y2 = my + range;
	if (y2 > ymax)
	    y2 = ymax;

    } while (range >= 1);

#ifdef DEBUG
    fprintf(stderr, "phods     - MX: %d\tMY: %d\n", mx, my);
#endif

    /* half pixel search */
    *mx_ptr = mx;
    *my_ptr = my;
    return dminy;
}


#define Z_THRESHOLD 256

#define CHECK_SAD_HALF_MV(suffix, x, y) \
{\
    d= pix_abs_ ## suffix(pix, ptr+((x)>>1), s->linesize);\
    d += (mv_penalty[pen_x + x] + mv_penalty[pen_y + y])*penalty_factor;\
    COPY3_IF_LT(dminh, d, dx, x, dy, y)\
}

static inline int sad_hpel_motion_search(MpegEncContext * s,
				  int *mx_ptr, int *my_ptr, int dmin,
				  int xmin, int ymin, int xmax, int ymax,
                                  int pred_x, int pred_y, Picture *picture,
                                  int n, int size, uint8_t * const mv_penalty)
{
    uint8_t *ref_picture= picture->data[0];
    uint32_t *score_map= s->me.score_map;
    const int penalty_factor= s->me.sub_penalty_factor;
    int mx, my, xx, yy, dminh;
    uint8_t *pix, *ptr;
    op_pixels_abs_func pix_abs_x2;
    op_pixels_abs_func pix_abs_y2;
    op_pixels_abs_func pix_abs_xy2;
    
    if(size==0){
        pix_abs_x2 = s->dsp.pix_abs16x16_x2;
        pix_abs_y2 = s->dsp.pix_abs16x16_y2;
        pix_abs_xy2= s->dsp.pix_abs16x16_xy2;
    }else{
        pix_abs_x2 = s->dsp.pix_abs8x8_x2;
        pix_abs_y2 = s->dsp.pix_abs8x8_y2;
        pix_abs_xy2= s->dsp.pix_abs8x8_xy2;
    }

    if(s->me.skip){
//    printf("S");
        *mx_ptr = 0;
        *my_ptr = 0;
        return dmin;
    }
//    printf("N");
        
    xx = 16 * s->mb_x + 8*(n&1);
    yy = 16 * s->mb_y + 8*(n>>1);
    pix =  s->new_picture.data[0] + (yy * s->linesize) + xx;

    mx = *mx_ptr;
    my = *my_ptr;
    ptr = ref_picture + ((yy + my) * s->linesize) + (xx + mx);
    
    dminh = dmin;

    if (mx > xmin && mx < xmax && 
        my > ymin && my < ymax) {
        int dx=0, dy=0;
        int d, pen_x, pen_y; 
        const int index= (my<<ME_MAP_SHIFT) + mx;
        const int t= score_map[(index-(1<<ME_MAP_SHIFT))&(ME_MAP_SIZE-1)];
        const int l= score_map[(index- 1               )&(ME_MAP_SIZE-1)];
        const int r= score_map[(index+ 1               )&(ME_MAP_SIZE-1)];
        const int b= score_map[(index+(1<<ME_MAP_SHIFT))&(ME_MAP_SIZE-1)];
        mx<<=1;
        my<<=1;

        
        pen_x= pred_x + mx;
        pen_y= pred_y + my;

        ptr-= s->linesize;
        if(t<=b){
            CHECK_SAD_HALF_MV(y2 , 0, -1)
            if(l<=r){
                CHECK_SAD_HALF_MV(xy2, -1, -1)
                if(t+r<=b+l){
                    CHECK_SAD_HALF_MV(xy2, +1, -1)
                    ptr+= s->linesize;
                }else{
                    ptr+= s->linesize;
                    CHECK_SAD_HALF_MV(xy2, -1, +1)
                }
                CHECK_SAD_HALF_MV(x2 , -1,  0)
            }else{
                CHECK_SAD_HALF_MV(xy2, +1, -1)
                if(t+l<=b+r){
                    CHECK_SAD_HALF_MV(xy2, -1, -1)
                    ptr+= s->linesize;
                }else{
                    ptr+= s->linesize;
                    CHECK_SAD_HALF_MV(xy2, +1, +1)
                }
                CHECK_SAD_HALF_MV(x2 , +1,  0)
            }
        }else{
            if(l<=r){
                if(t+l<=b+r){
                    CHECK_SAD_HALF_MV(xy2, -1, -1)
                    ptr+= s->linesize;
                }else{
                    ptr+= s->linesize;
                    CHECK_SAD_HALF_MV(xy2, +1, +1)
                }
                CHECK_SAD_HALF_MV(x2 , -1,  0)
                CHECK_SAD_HALF_MV(xy2, -1, +1)
            }else{
                if(t+r<=b+l){
                    CHECK_SAD_HALF_MV(xy2, +1, -1)
                    ptr+= s->linesize;
                }else{
                    ptr+= s->linesize;
                    CHECK_SAD_HALF_MV(xy2, -1, +1)
                }
                CHECK_SAD_HALF_MV(x2 , +1,  0)
                CHECK_SAD_HALF_MV(xy2, +1, +1)
            }
            CHECK_SAD_HALF_MV(y2 ,  0, +1)
        }
        mx+=dx;
        my+=dy;

    }else{
        mx<<=1;
        my<<=1;
    }

    *mx_ptr = mx;
    *my_ptr = my;
    return dminh;
}

static inline void set_p_mv_tables(MpegEncContext * s, int mx, int my, int mv4)
{
    const int xy= s->mb_x + s->mb_y*s->mb_stride;
    
    s->p_mv_table[xy][0] = mx;
    s->p_mv_table[xy][1] = my;

    /* has allready been set to the 4 MV if 4MV is done */
    if(mv4){
        int mot_xy= s->block_index[0];

        s->motion_val[mot_xy  ][0]= mx;
        s->motion_val[mot_xy  ][1]= my;
        s->motion_val[mot_xy+1][0]= mx;
        s->motion_val[mot_xy+1][1]= my;

        mot_xy += s->block_wrap[0];
        s->motion_val[mot_xy  ][0]= mx;
        s->motion_val[mot_xy  ][1]= my;
        s->motion_val[mot_xy+1][0]= mx;
        s->motion_val[mot_xy+1][1]= my;
    }
}

/**
 * get fullpel ME search limits.
 * @param range the approximate search range for the old ME code, unused for EPZS and newer
 */
static inline void get_limits(MpegEncContext *s, int *range, int *xmin, int *ymin, int *xmax, int *ymax)
{
    if(s->avctx->me_range) *range= s->avctx->me_range >> 1;
    else                   *range= 16;

    if (s->unrestricted_mv) {
        *xmin = -16;
        *ymin = -16;
        *xmax = s->mb_width*16;
        *ymax = s->mb_height*16;
    } else {
        *xmin = 0;
        *ymin = 0;
        *xmax = s->mb_width*16 - 16;
        *ymax = s->mb_height*16 - 16;
    }
    
    //FIXME try to limit x/y min/max if me_range is set
}

static inline int h263_mv4_search(MpegEncContext *s, int xmin, int ymin, int xmax, int ymax, int mx, int my, int shift)
{
    int block;
    int P[10][2];
    int dmin_sum=0, mx4_sum=0, my4_sum=0;
    uint8_t * const mv_penalty= s->me.mv_penalty[s->f_code] + MAX_MV;

    for(block=0; block<4; block++){
        int mx4, my4;
        int pred_x4, pred_y4;
        int dmin4;
        static const int off[4]= {2, 1, 1, -1};
        const int mot_stride = s->block_wrap[0];
        const int mot_xy = s->block_index[block];
//        const int block_x= (block&1);
//        const int block_y= (block>>1);
#if 1 // this saves us a bit of cliping work and shouldnt affect compression in a negative way
        const int rel_xmin4= xmin;
        const int rel_xmax4= xmax;
        const int rel_ymin4= ymin;
        const int rel_ymax4= ymax;
#else
        const int rel_xmin4= xmin - block_x*8;
        const int rel_xmax4= xmax - block_x*8 + 8;
        const int rel_ymin4= ymin - block_y*8;
        const int rel_ymax4= ymax - block_y*8 + 8;
#endif
        P_LEFT[0] = s->motion_val[mot_xy - 1][0];
        P_LEFT[1] = s->motion_val[mot_xy - 1][1];

        if(P_LEFT[0]       > (rel_xmax4<<shift)) P_LEFT[0]       = (rel_xmax4<<shift);

        /* special case for first line */
        if (s->mb_y == 0 && block<2) {
            pred_x4= P_LEFT[0];
            pred_y4= P_LEFT[1];
        } else {
            P_TOP[0]      = s->motion_val[mot_xy - mot_stride             ][0];
            P_TOP[1]      = s->motion_val[mot_xy - mot_stride             ][1];
            P_TOPRIGHT[0] = s->motion_val[mot_xy - mot_stride + off[block]][0];
            P_TOPRIGHT[1] = s->motion_val[mot_xy - mot_stride + off[block]][1];
            if(P_TOP[1]      > (rel_ymax4<<shift)) P_TOP[1]     = (rel_ymax4<<shift);
            if(P_TOPRIGHT[0] < (rel_xmin4<<shift)) P_TOPRIGHT[0]= (rel_xmin4<<shift);
            if(P_TOPRIGHT[0] > (rel_xmax4<<shift)) P_TOPRIGHT[0]= (rel_xmax4<<shift);
            if(P_TOPRIGHT[1] > (rel_ymax4<<shift)) P_TOPRIGHT[1]= (rel_ymax4<<shift);
    
            P_MEDIAN[0]= mid_pred(P_LEFT[0], P_TOP[0], P_TOPRIGHT[0]);
            P_MEDIAN[1]= mid_pred(P_LEFT[1], P_TOP[1], P_TOPRIGHT[1]);

//            if(s->out_format == FMT_H263){
                pred_x4 = P_MEDIAN[0];
                pred_y4 = P_MEDIAN[1];
#if 0
            }else { /* mpeg1 at least */
                pred_x4= P_LEFT[0];
                pred_y4= P_LEFT[1];
            }
#endif
        }
        P_MV1[0]= mx;
        P_MV1[1]= my;

        dmin4 = s->me.motion_search[1](s, block, &mx4, &my4, P, pred_x4, pred_y4, rel_xmin4, rel_ymin4, rel_xmax4, rel_ymax4, 
                                       &s->last_picture, s->p_mv_table, (1<<16)>>shift, mv_penalty);

        dmin4= s->me.sub_motion_search(s, &mx4, &my4, dmin4, rel_xmin4, rel_ymin4, rel_xmax4, rel_ymax4, 
					  pred_x4, pred_y4, &s->last_picture, block, 1, mv_penalty);
        
        if(s->dsp.me_sub_cmp[0] != s->dsp.mb_cmp[0]){
            int dxy;
            const int offset= ((block&1) + (block>>1)*s->linesize)*8;
            uint8_t *dest_y = s->me.scratchpad + offset;

            if(s->quarter_sample){
                uint8_t *ref= s->last_picture.data[0] + (s->mb_x*16 + (mx4>>2)) + (s->mb_y*16 + (my4>>2))*s->linesize + offset;
                dxy = ((my4 & 3) << 2) | (mx4 & 3);

                if(s->no_rounding)
                    s->dsp.put_no_rnd_qpel_pixels_tab[1][dxy](dest_y   , ref    , s->linesize);
                else
                    s->dsp.put_qpel_pixels_tab       [1][dxy](dest_y   , ref    , s->linesize);
            }else{
                uint8_t *ref= s->last_picture.data[0] + (s->mb_x*16 + (mx4>>1)) + (s->mb_y*16 + (my4>>1))*s->linesize + offset;
                dxy = ((my4 & 1) << 1) | (mx4 & 1);

                if(s->no_rounding)
                    s->dsp.put_no_rnd_pixels_tab[1][dxy](dest_y    , ref    , s->linesize, 8);
                else
                    s->dsp.put_pixels_tab       [1][dxy](dest_y    , ref    , s->linesize, 8);
            }
            dmin_sum+= (mv_penalty[mx4-pred_x4] + mv_penalty[my4-pred_y4])*s->me.mb_penalty_factor;
        }else
            dmin_sum+= dmin4;

        if(s->quarter_sample){
            mx4_sum+= mx4/2;
            my4_sum+= my4/2;
        }else{
            mx4_sum+= mx4;
            my4_sum+= my4;
        }
            
        s->motion_val[ s->block_index[block] ][0]= mx4;
        s->motion_val[ s->block_index[block] ][1]= my4;
    }
    
    if(s->dsp.me_sub_cmp[0] != s->dsp.mb_cmp[0]){
        dmin_sum += s->dsp.mb_cmp[0](s, s->new_picture.data[0] + s->mb_x*16 + s->mb_y*16*s->linesize, s->me.scratchpad, s->linesize);
    }
    
    if(s->avctx->mb_cmp&FF_CMP_CHROMA){
        int dxy;
        int mx, my;
        int offset;

        mx= ff_h263_round_chroma(mx4_sum);
        my= ff_h263_round_chroma(my4_sum);
        dxy = ((my & 1) << 1) | (mx & 1);
        
        offset= (s->mb_x*8 + (mx>>1)) + (s->mb_y*8 + (my>>1))*s->uvlinesize;
       
        if(s->no_rounding){
            s->dsp.put_no_rnd_pixels_tab[1][dxy](s->me.scratchpad    , s->last_picture.data[1] + offset, s->uvlinesize, 8);
            s->dsp.put_no_rnd_pixels_tab[1][dxy](s->me.scratchpad+8  , s->last_picture.data[2] + offset, s->uvlinesize, 8);
        }else{
            s->dsp.put_pixels_tab       [1][dxy](s->me.scratchpad    , s->last_picture.data[1] + offset, s->uvlinesize, 8);
            s->dsp.put_pixels_tab       [1][dxy](s->me.scratchpad+8  , s->last_picture.data[2] + offset, s->uvlinesize, 8);
        }

        dmin_sum += s->dsp.mb_cmp[1](s, s->new_picture.data[1] + s->mb_x*8 + s->mb_y*8*s->uvlinesize, s->me.scratchpad  , s->uvlinesize);
        dmin_sum += s->dsp.mb_cmp[1](s, s->new_picture.data[2] + s->mb_x*8 + s->mb_y*8*s->uvlinesize, s->me.scratchpad+8, s->uvlinesize);
    }

    switch(s->avctx->mb_cmp&0xFF){
    /*case FF_CMP_SSE:
        return dmin_sum+ 32*s->qscale*s->qscale;*/
    case FF_CMP_RD:
        return dmin_sum;
    default:
        return dmin_sum+ 11*s->me.mb_penalty_factor;
    }
}

void ff_estimate_p_frame_motion(MpegEncContext * s,
                                int mb_x, int mb_y)
{
    uint8_t *pix, *ppix;
    int sum, varc, vard, mx, my, range, dmin, xx, yy;
    int xmin, ymin, xmax, ymax;
    int rel_xmin, rel_ymin, rel_xmax, rel_ymax;
    int pred_x=0, pred_y=0;
    int P[10][2];
    const int shift= 1+s->quarter_sample;
    int mb_type=0;
    uint8_t *ref_picture= s->last_picture.data[0];
    Picture * const pic= &s->current_picture;
    uint8_t * const mv_penalty= s->me.mv_penalty[s->f_code] + MAX_MV;
    
    assert(s->quarter_sample==0 || s->quarter_sample==1);

    s->me.penalty_factor    = get_penalty_factor(s, s->avctx->me_cmp);
    s->me.sub_penalty_factor= get_penalty_factor(s, s->avctx->me_sub_cmp);
    s->me.mb_penalty_factor = get_penalty_factor(s, s->avctx->mb_cmp);

    get_limits(s, &range, &xmin, &ymin, &xmax, &ymax);
    rel_xmin= xmin - mb_x*16;
    rel_xmax= xmax - mb_x*16;
    rel_ymin= ymin - mb_y*16;
    rel_ymax= ymax - mb_y*16;
    s->me.skip=0;

    switch(s->me_method) {
    case ME_ZERO:
    default:
	no_motion_search(s, &mx, &my);
        mx-= mb_x*16;
        my-= mb_y*16;
        dmin = 0;
        break;
    case ME_FULL:
	dmin = full_motion_search(s, &mx, &my, range, xmin, ymin, xmax, ymax, ref_picture);
        mx-= mb_x*16;
        my-= mb_y*16;
        break;
    case ME_LOG:
	dmin = log_motion_search(s, &mx, &my, range / 2, xmin, ymin, xmax, ymax, ref_picture);
        mx-= mb_x*16;
        my-= mb_y*16;
        break;
    case ME_PHODS:
	dmin = phods_motion_search(s, &mx, &my, range / 2, xmin, ymin, xmax, ymax, ref_picture);
        mx-= mb_x*16;
        my-= mb_y*16;
        break;
    case ME_X1:
    case ME_EPZS:
       {
            const int mot_stride = s->block_wrap[0];
            const int mot_xy = s->block_index[0];

            P_LEFT[0]       = s->motion_val[mot_xy - 1][0];
            P_LEFT[1]       = s->motion_val[mot_xy - 1][1];

            if(P_LEFT[0]       > (rel_xmax<<shift)) P_LEFT[0]       = (rel_xmax<<shift);

            if(mb_y) {
                P_TOP[0]      = s->motion_val[mot_xy - mot_stride    ][0];
                P_TOP[1]      = s->motion_val[mot_xy - mot_stride    ][1];
                P_TOPRIGHT[0] = s->motion_val[mot_xy - mot_stride + 2][0];
                P_TOPRIGHT[1] = s->motion_val[mot_xy - mot_stride + 2][1];
                if(P_TOP[1]      > (rel_ymax<<shift)) P_TOP[1]     = (rel_ymax<<shift);
                if(P_TOPRIGHT[0] < (rel_xmin<<shift)) P_TOPRIGHT[0]= (rel_xmin<<shift);
                if(P_TOPRIGHT[1] > (rel_ymax<<shift)) P_TOPRIGHT[1]= (rel_ymax<<shift);
        
                P_MEDIAN[0]= mid_pred(P_LEFT[0], P_TOP[0], P_TOPRIGHT[0]);
                P_MEDIAN[1]= mid_pred(P_LEFT[1], P_TOP[1], P_TOPRIGHT[1]);

                if(s->out_format == FMT_H263){
                    pred_x = P_MEDIAN[0];
                    pred_y = P_MEDIAN[1];
                }else { /* mpeg1 at least */
                    pred_x= P_LEFT[0];
                    pred_y= P_LEFT[1];
                }
            }else{
                pred_x= P_LEFT[0];
                pred_y= P_LEFT[1];
            }

        }
        dmin = s->me.motion_search[0](s, 0, &mx, &my, P, pred_x, pred_y, rel_xmin, rel_ymin, rel_xmax, rel_ymax, 
                                      &s->last_picture, s->p_mv_table, (1<<16)>>shift, mv_penalty);
 
        break;
    }

    /* intra / predictive decision */
    xx = mb_x * 16;
    yy = mb_y * 16;

    pix = s->new_picture.data[0] + (yy * s->linesize) + xx;
    /* At this point (mx,my) are full-pell and the relative displacement */
    ppix = ref_picture + ((yy+my) * s->linesize) + (xx+mx);
    
    sum = s->dsp.pix_sum(pix, s->linesize);
    
    varc = (s->dsp.pix_norm1(pix, s->linesize) - (((unsigned)(sum*sum))>>8) + 500 + 128)>>8;
    vard = (s->dsp.sse[0](NULL, pix, ppix, s->linesize)+128)>>8;

//printf("%d %d %d %X %X %X\n", s->mb_width, mb_x, mb_y,(int)s, (int)s->mb_var, (int)s->mc_mb_var); fflush(stdout);
    pic->mb_var   [s->mb_stride * mb_y + mb_x] = varc;
    pic->mc_mb_var[s->mb_stride * mb_y + mb_x] = vard;
    pic->mb_mean  [s->mb_stride * mb_y + mb_x] = (sum+128)>>8;
//    pic->mb_cmp_score[s->mb_stride * mb_y + mb_x] = dmin; 
    pic->mb_var_sum    += varc;
    pic->mc_mb_var_sum += vard;
//printf("E%d %d %d %X %X %X\n", s->mb_width, mb_x, mb_y,(int)s, (int)s->mb_var, (int)s->mc_mb_var); fflush(stdout);
    
#if 0
    printf("varc=%4d avg_var=%4d (sum=%4d) vard=%4d mx=%2d my=%2d\n",
	   varc, s->avg_mb_var, sum, vard, mx - xx, my - yy);
#endif
    if(s->avctx->mb_decision > FF_MB_DECISION_SIMPLE){
        if (vard <= 64 || vard < varc)
            s->scene_change_score+= ff_sqrt(vard) - ff_sqrt(varc);
        else
            s->scene_change_score+= s->qscale;

        if (vard*2 + 200 > varc)
            mb_type|= MB_TYPE_INTRA;
        if (varc*2 + 200 > vard){
            mb_type|= MB_TYPE_INTER;
            s->me.sub_motion_search(s, &mx, &my, dmin, rel_xmin, rel_ymin, rel_xmax, rel_ymax,
				   pred_x, pred_y, &s->last_picture, 0, 0, mv_penalty);
        }else{
            mx <<=shift;
            my <<=shift;
        }
        if((s->flags&CODEC_FLAG_4MV)
           && !s->me.skip && varc>50 && vard>10){
            h263_mv4_search(s, rel_xmin, rel_ymin, rel_xmax, rel_ymax, mx, my, shift);
            mb_type|=MB_TYPE_INTER4V;

            set_p_mv_tables(s, mx, my, 0);
        }else
            set_p_mv_tables(s, mx, my, 1);
    }else{
        mb_type= MB_TYPE_INTER;

        dmin= s->me.sub_motion_search(s, &mx, &my, dmin, rel_xmin, rel_ymin, rel_xmax, rel_ymax,
                                    pred_x, pred_y, &s->last_picture, 0, 0, mv_penalty);
        
        if(s->avctx->me_sub_cmp != s->avctx->mb_cmp && !s->me.skip)
            dmin= s->me.get_mb_score(s, mx, my, pred_x, pred_y, &s->last_picture, mv_penalty);

        if((s->flags&CODEC_FLAG_4MV)
           && !s->me.skip && varc>50 && vard>10){
            int dmin4= h263_mv4_search(s, rel_xmin, rel_ymin, rel_xmax, rel_ymax, mx, my, shift);
            if(dmin4 < dmin){
                mb_type= MB_TYPE_INTER4V;
                dmin=dmin4;
            }
        }
        pic->mb_cmp_score[s->mb_stride * mb_y + mb_x] = dmin; 
        set_p_mv_tables(s, mx, my, mb_type!=MB_TYPE_INTER4V);
        
        if (vard <= 64 || vard < varc) {
            s->scene_change_score+= ff_sqrt(vard) - ff_sqrt(varc);
        }else{
            s->scene_change_score+= s->qscale;
        }
    }

    s->mb_type[mb_y*s->mb_stride + mb_x]= mb_type;
}

int ff_pre_estimate_p_frame_motion(MpegEncContext * s,
                                    int mb_x, int mb_y)
{
    int mx, my, range, dmin;
    int xmin, ymin, xmax, ymax;
    int rel_xmin, rel_ymin, rel_xmax, rel_ymax;
    int pred_x=0, pred_y=0;
    int P[10][2];
    const int shift= 1+s->quarter_sample;
    uint8_t * const mv_penalty= s->me.mv_penalty[s->f_code] + MAX_MV;
    const int xy= mb_x + mb_y*s->mb_stride;
    
    assert(s->quarter_sample==0 || s->quarter_sample==1);

    s->me.pre_penalty_factor    = get_penalty_factor(s, s->avctx->me_pre_cmp);

    get_limits(s, &range, &xmin, &ymin, &xmax, &ymax);
    rel_xmin= xmin - mb_x*16;
    rel_xmax= xmax - mb_x*16;
    rel_ymin= ymin - mb_y*16;
    rel_ymax= ymax - mb_y*16;
    s->me.skip=0;

    P_LEFT[0]       = s->p_mv_table[xy + 1][0];
    P_LEFT[1]       = s->p_mv_table[xy + 1][1];

    if(P_LEFT[0]       < (rel_xmin<<shift)) P_LEFT[0]       = (rel_xmin<<shift);

    /* special case for first line */
    if (mb_y == s->mb_height-1) {
        pred_x= P_LEFT[0];
        pred_y= P_LEFT[1];
        P_TOP[0]= P_TOPRIGHT[0]= P_MEDIAN[0]=
        P_TOP[1]= P_TOPRIGHT[1]= P_MEDIAN[1]= 0; //FIXME 
    } else {
        P_TOP[0]      = s->p_mv_table[xy + s->mb_stride    ][0];
        P_TOP[1]      = s->p_mv_table[xy + s->mb_stride    ][1];
        P_TOPRIGHT[0] = s->p_mv_table[xy + s->mb_stride - 1][0];
        P_TOPRIGHT[1] = s->p_mv_table[xy + s->mb_stride - 1][1];
        if(P_TOP[1]      < (rel_ymin<<shift)) P_TOP[1]     = (rel_ymin<<shift);
        if(P_TOPRIGHT[0] > (rel_xmax<<shift)) P_TOPRIGHT[0]= (rel_xmax<<shift);
        if(P_TOPRIGHT[1] < (rel_ymin<<shift)) P_TOPRIGHT[1]= (rel_ymin<<shift);
    
        P_MEDIAN[0]= mid_pred(P_LEFT[0], P_TOP[0], P_TOPRIGHT[0]);
        P_MEDIAN[1]= mid_pred(P_LEFT[1], P_TOP[1], P_TOPRIGHT[1]);

        pred_x = P_MEDIAN[0];
        pred_y = P_MEDIAN[1];
    }
    dmin = s->me.pre_motion_search(s, 0, &mx, &my, P, pred_x, pred_y, rel_xmin, rel_ymin, rel_xmax, rel_ymax, 
                                   &s->last_picture, s->p_mv_table, (1<<16)>>shift, mv_penalty);

    s->p_mv_table[xy][0] = mx<<shift;
    s->p_mv_table[xy][1] = my<<shift;
    
    return dmin;
}

static int ff_estimate_motion_b(MpegEncContext * s,
                       int mb_x, int mb_y, int16_t (*mv_table)[2], Picture *picture, int f_code)
{
    int mx, my, range, dmin;
    int xmin, ymin, xmax, ymax;
    int rel_xmin, rel_ymin, rel_xmax, rel_ymax;
    int pred_x=0, pred_y=0;
    int P[10][2];
    const int shift= 1+s->quarter_sample;
    const int mot_stride = s->mb_stride;
    const int mot_xy = mb_y*mot_stride + mb_x;
    uint8_t * const ref_picture= picture->data[0];
    uint8_t * const mv_penalty= s->me.mv_penalty[f_code] + MAX_MV;
    int mv_scale;
        
    s->me.penalty_factor    = get_penalty_factor(s, s->avctx->me_cmp);
    s->me.sub_penalty_factor= get_penalty_factor(s, s->avctx->me_sub_cmp);
    s->me.mb_penalty_factor = get_penalty_factor(s, s->avctx->mb_cmp);

    get_limits(s, &range, &xmin, &ymin, &xmax, &ymax);
    rel_xmin= xmin - mb_x*16;
    rel_xmax= xmax - mb_x*16;
    rel_ymin= ymin - mb_y*16;
    rel_ymax= ymax - mb_y*16;

    switch(s->me_method) {
    case ME_ZERO:
    default:
	no_motion_search(s, &mx, &my);
        dmin = 0;
        mx-= mb_x*16;
        my-= mb_y*16;
        break;
    case ME_FULL:
	dmin = full_motion_search(s, &mx, &my, range, xmin, ymin, xmax, ymax, ref_picture);
        mx-= mb_x*16;
        my-= mb_y*16;
        break;
    case ME_LOG:
	dmin = log_motion_search(s, &mx, &my, range / 2, xmin, ymin, xmax, ymax, ref_picture);
        mx-= mb_x*16;
        my-= mb_y*16;
        break;
    case ME_PHODS:
	dmin = phods_motion_search(s, &mx, &my, range / 2, xmin, ymin, xmax, ymax, ref_picture);
        mx-= mb_x*16;
        my-= mb_y*16;
        break;
    case ME_X1:
    case ME_EPZS:
       {
            P_LEFT[0]        = mv_table[mot_xy - 1][0];
            P_LEFT[1]        = mv_table[mot_xy - 1][1];

            if(P_LEFT[0]       > (rel_xmax<<shift)) P_LEFT[0]       = (rel_xmax<<shift);

            /* special case for first line */
            if (mb_y) {
                P_TOP[0] = mv_table[mot_xy - mot_stride             ][0];
                P_TOP[1] = mv_table[mot_xy - mot_stride             ][1];
                P_TOPRIGHT[0] = mv_table[mot_xy - mot_stride + 1         ][0];
                P_TOPRIGHT[1] = mv_table[mot_xy - mot_stride + 1         ][1];
                if(P_TOP[1] > (rel_ymax<<shift)) P_TOP[1]= (rel_ymax<<shift);
                if(P_TOPRIGHT[0] < (rel_xmin<<shift)) P_TOPRIGHT[0]= (rel_xmin<<shift);
                if(P_TOPRIGHT[1] > (rel_ymax<<shift)) P_TOPRIGHT[1]= (rel_ymax<<shift);
        
                P_MEDIAN[0]= mid_pred(P_LEFT[0], P_TOP[0], P_TOPRIGHT[0]);
                P_MEDIAN[1]= mid_pred(P_LEFT[1], P_TOP[1], P_TOPRIGHT[1]);
            }
            pred_x= P_LEFT[0];
            pred_y= P_LEFT[1];
        }
        
        if(mv_table == s->b_forw_mv_table){
            mv_scale= (s->pb_time<<16) / (s->pp_time<<shift);
        }else{
            mv_scale= ((s->pb_time - s->pp_time)<<16) / (s->pp_time<<shift);
        }
        
        dmin = s->me.motion_search[0](s, 0, &mx, &my, P, pred_x, pred_y, rel_xmin, rel_ymin, rel_xmax, rel_ymax, 
                                      picture, s->p_mv_table, mv_scale, mv_penalty);
 
        break;
    }
    
    dmin= s->me.sub_motion_search(s, &mx, &my, dmin, rel_xmin, rel_ymin, rel_xmax, rel_ymax,
				   pred_x, pred_y, picture, 0, 0, mv_penalty);
                                   
    if(s->avctx->me_sub_cmp != s->avctx->mb_cmp && !s->me.skip)
        dmin= s->me.get_mb_score(s, mx, my, pred_x, pred_y, picture, mv_penalty);

//printf("%d %d %d %d//", s->mb_x, s->mb_y, mx, my);
//    s->mb_type[mb_y*s->mb_width + mb_x]= mb_type;
    mv_table[mot_xy][0]= mx;
    mv_table[mot_xy][1]= my;

    return dmin;
}

static inline int check_bidir_mv(MpegEncContext * s,
                   int mb_x, int mb_y,
                   int motion_fx, int motion_fy,
                   int motion_bx, int motion_by,
                   int pred_fx, int pred_fy,
                   int pred_bx, int pred_by)
{
    //FIXME optimize?
    //FIXME move into template?
    //FIXME better f_code prediction (max mv & distance)
    uint8_t * const mv_penalty= s->me.mv_penalty[s->f_code] + MAX_MV; // f_code of the prev frame
    uint8_t *dest_y = s->me.scratchpad;
    uint8_t *ptr;
    int dxy;
    int src_x, src_y;
    int fbmin;

    if(s->quarter_sample){
        dxy = ((motion_fy & 3) << 2) | (motion_fx & 3);
        src_x = mb_x * 16 + (motion_fx >> 2);
        src_y = mb_y * 16 + (motion_fy >> 2);
        assert(src_x >=-16 && src_x<=s->h_edge_pos);
        assert(src_y >=-16 && src_y<=s->v_edge_pos);

        ptr = s->last_picture.data[0] + (src_y * s->linesize) + src_x;
        s->dsp.put_qpel_pixels_tab[0][dxy](dest_y    , ptr    , s->linesize);

        dxy = ((motion_by & 3) << 2) | (motion_bx & 3);
        src_x = mb_x * 16 + (motion_bx >> 2);
        src_y = mb_y * 16 + (motion_by >> 2);
        assert(src_x >=-16 && src_x<=s->h_edge_pos);
        assert(src_y >=-16 && src_y<=s->v_edge_pos);
    
        ptr = s->next_picture.data[0] + (src_y * s->linesize) + src_x;
        s->dsp.avg_qpel_pixels_tab[0][dxy](dest_y    , ptr    , s->linesize);
    }else{
        dxy = ((motion_fy & 1) << 1) | (motion_fx & 1);
        src_x = mb_x * 16 + (motion_fx >> 1);
        src_y = mb_y * 16 + (motion_fy >> 1);
        assert(src_x >=-16 && src_x<=s->h_edge_pos);
        assert(src_y >=-16 && src_y<=s->v_edge_pos);

        ptr = s->last_picture.data[0] + (src_y * s->linesize) + src_x;
        s->dsp.put_pixels_tab[0][dxy](dest_y    , ptr    , s->linesize, 16);

        dxy = ((motion_by & 1) << 1) | (motion_bx & 1);
        src_x = mb_x * 16 + (motion_bx >> 1);
        src_y = mb_y * 16 + (motion_by >> 1);
        assert(src_x >=-16 && src_x<=s->h_edge_pos);
        assert(src_y >=-16 && src_y<=s->v_edge_pos);
    
        ptr = s->next_picture.data[0] + (src_y * s->linesize) + src_x;
        s->dsp.avg_pixels_tab[0][dxy](dest_y    , ptr    , s->linesize, 16);
    }

    fbmin = (mv_penalty[motion_fx-pred_fx] + mv_penalty[motion_fy-pred_fy])*s->me.mb_penalty_factor
           +(mv_penalty[motion_bx-pred_bx] + mv_penalty[motion_by-pred_by])*s->me.mb_penalty_factor
           + s->dsp.mb_cmp[0](s, s->new_picture.data[0] + mb_x*16 + mb_y*16*s->linesize, dest_y, s->linesize);
           
    if(s->avctx->mb_cmp&FF_CMP_CHROMA){
    }
    //FIXME CHROMA !!!
           
    return fbmin;
}

/* refine the bidir vectors in hq mode and return the score in both lq & hq mode*/
static inline int bidir_refine(MpegEncContext * s,
                                  int mb_x, int mb_y)
{
    const int mot_stride = s->mb_stride;
    const int xy = mb_y *mot_stride + mb_x;
    int fbmin;
    int pred_fx= s->b_bidir_forw_mv_table[xy-1][0];
    int pred_fy= s->b_bidir_forw_mv_table[xy-1][1];
    int pred_bx= s->b_bidir_back_mv_table[xy-1][0];
    int pred_by= s->b_bidir_back_mv_table[xy-1][1];
    int motion_fx= s->b_bidir_forw_mv_table[xy][0]= s->b_forw_mv_table[xy][0];
    int motion_fy= s->b_bidir_forw_mv_table[xy][1]= s->b_forw_mv_table[xy][1];
    int motion_bx= s->b_bidir_back_mv_table[xy][0]= s->b_back_mv_table[xy][0];
    int motion_by= s->b_bidir_back_mv_table[xy][1]= s->b_back_mv_table[xy][1];

    //FIXME do refinement and add flag
    
    fbmin= check_bidir_mv(s, mb_x, mb_y, 
                          motion_fx, motion_fy,
                          motion_bx, motion_by,
                          pred_fx, pred_fy,
                          pred_bx, pred_by);

   return fbmin;
}

static inline int direct_search(MpegEncContext * s,
                                int mb_x, int mb_y)
{
    int P[10][2];
    const int mot_stride = s->mb_stride;
    const int mot_xy = mb_y*mot_stride + mb_x;
    const int shift= 1+s->quarter_sample;
    int dmin, i;
    const int time_pp= s->pp_time;
    const int time_pb= s->pb_time;
    int mx, my, xmin, xmax, ymin, ymax;
    int16_t (*mv_table)[2]= s->b_direct_mv_table;
    uint8_t * const mv_penalty= s->me.mv_penalty[1] + MAX_MV;
    
    ymin= xmin=(-32)>>shift;
    ymax= xmax=   31>>shift;

    if(IS_8X8(s->next_picture.mb_type[mot_xy])){
        s->mv_type= MV_TYPE_8X8;
    }else{
        s->mv_type= MV_TYPE_16X16;
    }

    for(i=0; i<4; i++){
        int index= s->block_index[i];
        int min, max;
    
        s->me.co_located_mv[i][0]= s->motion_val[index][0];
        s->me.co_located_mv[i][1]= s->motion_val[index][1];
        s->me.direct_basis_mv[i][0]= s->me.co_located_mv[i][0]*time_pb/time_pp + ((i& 1)<<(shift+3));
        s->me.direct_basis_mv[i][1]= s->me.co_located_mv[i][1]*time_pb/time_pp + ((i>>1)<<(shift+3));
//        s->me.direct_basis_mv[1][i][0]= s->me.co_located_mv[i][0]*(time_pb - time_pp)/time_pp + ((i &1)<<(shift+3);
//        s->me.direct_basis_mv[1][i][1]= s->me.co_located_mv[i][1]*(time_pb - time_pp)/time_pp + ((i>>1)<<(shift+3);

        max= FFMAX(s->me.direct_basis_mv[i][0], s->me.direct_basis_mv[i][0] - s->me.co_located_mv[i][0])>>shift;
        min= FFMIN(s->me.direct_basis_mv[i][0], s->me.direct_basis_mv[i][0] - s->me.co_located_mv[i][0])>>shift;
        max+= (2*mb_x + (i& 1))*8 + 1; // +-1 is for the simpler rounding
        min+= (2*mb_x + (i& 1))*8 - 1;
        xmax= FFMIN(xmax, s->width - max);
        xmin= FFMAX(xmin, - 16     - min);

        max= FFMAX(s->me.direct_basis_mv[i][1], s->me.direct_basis_mv[i][1] - s->me.co_located_mv[i][1])>>shift;
        min= FFMIN(s->me.direct_basis_mv[i][1], s->me.direct_basis_mv[i][1] - s->me.co_located_mv[i][1])>>shift;
        max+= (2*mb_y + (i>>1))*8 + 1; // +-1 is for the simpler rounding
        min+= (2*mb_y + (i>>1))*8 - 1;
        ymax= FFMIN(ymax, s->height - max);
        ymin= FFMAX(ymin, - 16      - min);
        
        if(s->mv_type == MV_TYPE_16X16) break;
    }
    
    assert(xmax <= 15 && ymax <= 15 && xmin >= -16 && ymin >= -16);
    
    if(xmax < 0 || xmin >0 || ymax < 0 || ymin > 0){
        s->b_direct_mv_table[mot_xy][0]= 0;
        s->b_direct_mv_table[mot_xy][1]= 0;

        return 256*256*256*64;
    }

    P_LEFT[0]        = clip(mv_table[mot_xy - 1][0], xmin<<shift, xmax<<shift);
    P_LEFT[1]        = clip(mv_table[mot_xy - 1][1], ymin<<shift, ymax<<shift);

    /* special case for first line */
    if (mb_y) {
        P_TOP[0]      = clip(mv_table[mot_xy - mot_stride             ][0], xmin<<shift, xmax<<shift);
        P_TOP[1]      = clip(mv_table[mot_xy - mot_stride             ][1], ymin<<shift, ymax<<shift);
        P_TOPRIGHT[0] = clip(mv_table[mot_xy - mot_stride + 1         ][0], xmin<<shift, xmax<<shift);
        P_TOPRIGHT[1] = clip(mv_table[mot_xy - mot_stride + 1         ][1], ymin<<shift, ymax<<shift);
    
        P_MEDIAN[0]= mid_pred(P_LEFT[0], P_TOP[0], P_TOPRIGHT[0]);
        P_MEDIAN[1]= mid_pred(P_LEFT[1], P_TOP[1], P_TOPRIGHT[1]);
    }
 
    //FIXME direct_search  ptr in context!!! (needed for chroma anyway or this will get messy)   
    if(s->flags&CODEC_FLAG_QPEL){
        dmin = simple_direct_qpel_epzs_motion_search(s, 0, &mx, &my, P, 0, 0, xmin, ymin, xmax, ymax, 
                                                     &s->last_picture, mv_table, 1<<14, mv_penalty);
        dmin = simple_direct_qpel_qpel_motion_search(s, &mx, &my, dmin, xmin, ymin, xmax, ymax,
                                                0, 0, &s->last_picture, 0, 0, mv_penalty);
        
        if(s->avctx->me_sub_cmp != s->avctx->mb_cmp && !s->me.skip)
            dmin= simple_direct_qpel_qpel_get_mb_score(s, mx, my, 0, 0, &s->last_picture, mv_penalty);
    }else{
        dmin = simple_direct_hpel_epzs_motion_search(s, 0, &mx, &my, P, 0, 0, xmin, ymin, xmax, ymax, 
                                                     &s->last_picture, mv_table, 1<<15, mv_penalty);
        dmin = simple_direct_hpel_hpel_motion_search(s, &mx, &my, dmin, xmin, ymin, xmax, ymax,
                                                0, 0, &s->last_picture, 0, 0, mv_penalty);

        if(s->avctx->me_sub_cmp != s->avctx->mb_cmp && !s->me.skip)
            dmin= simple_direct_hpel_hpel_get_mb_score(s, mx, my, 0, 0, &s->last_picture, mv_penalty);
    }

    s->b_direct_mv_table[mot_xy][0]= mx;
    s->b_direct_mv_table[mot_xy][1]= my;
    return dmin;
}

void ff_estimate_b_frame_motion(MpegEncContext * s,
                             int mb_x, int mb_y)
{
    const int penalty_factor= s->me.mb_penalty_factor;
    int fmin, bmin, dmin, fbmin;
    int type=0;
    
    s->me.skip=0;
    if (s->codec_id == CODEC_ID_MPEG4)
        dmin= direct_search(s, mb_x, mb_y);
    else
        dmin= INT_MAX;

    s->me.skip=0;
    fmin= ff_estimate_motion_b(s, mb_x, mb_y, s->b_forw_mv_table, &s->last_picture, s->f_code) + 3*penalty_factor;
    
    s->me.skip=0;
    bmin= ff_estimate_motion_b(s, mb_x, mb_y, s->b_back_mv_table, &s->next_picture, s->b_code) + 2*penalty_factor;
//printf(" %d %d ", s->b_forw_mv_table[xy][0], s->b_forw_mv_table[xy][1]);

    s->me.skip=0;
    fbmin= bidir_refine(s, mb_x, mb_y) + penalty_factor;
//printf("%d %d %d %d\n", dmin, fmin, bmin, fbmin);
    {
        int score= fmin;
        type = MB_TYPE_FORWARD;
        
        if (dmin <= score){
            score = dmin;
            type = MB_TYPE_DIRECT;
        }
        if(bmin<score){
            score=bmin;
            type= MB_TYPE_BACKWARD; 
        }
        if(fbmin<score){
            score=fbmin;
            type= MB_TYPE_BIDIR;
        }
        
        score= ((unsigned)(score*score + 128*256))>>16;
        s->current_picture.mc_mb_var_sum += score;
        s->current_picture.mc_mb_var[mb_y*s->mb_stride + mb_x] = score; //FIXME use SSE
    }

    if(s->avctx->mb_decision > FF_MB_DECISION_SIMPLE){
        type= MB_TYPE_FORWARD | MB_TYPE_BACKWARD | MB_TYPE_BIDIR | MB_TYPE_DIRECT; //FIXME something smarter
        if(dmin>256*256*16) type&= ~MB_TYPE_DIRECT; //dont try direct mode if its invalid for this MB
    }

    s->mb_type[mb_y*s->mb_stride + mb_x]= type;
}

/* find best f_code for ME which do unlimited searches */
int ff_get_best_fcode(MpegEncContext * s, int16_t (*mv_table)[2], int type)
{
    if(s->me_method>=ME_EPZS){
        int score[8];
        int i, y;
        uint8_t * fcode_tab= s->fcode_tab;
        int best_fcode=-1;
        int best_score=-10000000;

        for(i=0; i<8; i++) score[i]= s->mb_num*(8-i);

        for(y=0; y<s->mb_height; y++){
            int x;
            int xy= y*s->mb_stride;
            for(x=0; x<s->mb_width; x++){
                if(s->mb_type[xy] & type){
                    int fcode= FFMAX(fcode_tab[mv_table[xy][0] + MAX_MV],
                                     fcode_tab[mv_table[xy][1] + MAX_MV]);
                    int j;
                    
                    for(j=0; j<fcode && j<8; j++){
                        if(s->pict_type==B_TYPE || s->current_picture.mc_mb_var[xy] < s->current_picture.mb_var[xy])
                            score[j]-= 170;
                    }
                }
                xy++;
            }
        }
        
        for(i=1; i<8; i++){
            if(score[i] > best_score){
                best_score= score[i];
                best_fcode= i;
            }
//            printf("%d %d\n", i, score[i]);
        }

//    printf("fcode: %d type: %d\n", i, s->pict_type);
        return best_fcode;
/*        for(i=0; i<=MAX_FCODE; i++){
            printf("%d ", mv_num[i]);
        }
        printf("\n");*/
    }else{
        return 1;
    }
}

void ff_fix_long_p_mvs(MpegEncContext * s)
{
    const int f_code= s->f_code;
    int y, range;
    assert(s->pict_type==P_TYPE);

    range = (((s->out_format == FMT_MPEG1) ? 8 : 16) << f_code);
    
    if(s->msmpeg4_version) range= 16;
    
    if(s->avctx->me_range && range > s->avctx->me_range) range= s->avctx->me_range;
    
    /* clip / convert to intra 16x16 type MVs */
    for(y=0; y<s->mb_height; y++){
        int x;
        int xy= y*s->mb_stride;
        for(x=0; x<s->mb_width; x++){
            if(s->mb_type[xy]&MB_TYPE_INTER){
                if(   s->p_mv_table[xy][0] >=range || s->p_mv_table[xy][0] <-range
                   || s->p_mv_table[xy][1] >=range || s->p_mv_table[xy][1] <-range){
                    s->mb_type[xy] &= ~MB_TYPE_INTER;
                    s->mb_type[xy] |= MB_TYPE_INTRA;
                    s->p_mv_table[xy][0] = 0;
                    s->p_mv_table[xy][1] = 0;
                }
            }
            xy++;
        }
    }
//printf("%d no:%d %d//\n", clip, noclip, f_code);
    if(s->flags&CODEC_FLAG_4MV){
        const int wrap= 2+ s->mb_width*2;

        /* clip / convert to intra 8x8 type MVs */
        for(y=0; y<s->mb_height; y++){
            int xy= (y*2 + 1)*wrap + 1;
            int i= y*s->mb_stride;
            int x;

            for(x=0; x<s->mb_width; x++){
                if(s->mb_type[i]&MB_TYPE_INTER4V){
                    int block;
                    for(block=0; block<4; block++){
                        int off= (block& 1) + (block>>1)*wrap;
                        int mx= s->motion_val[ xy + off ][0];
                        int my= s->motion_val[ xy + off ][1];

                        if(   mx >=range || mx <-range
                           || my >=range || my <-range){
                            s->mb_type[i] &= ~MB_TYPE_INTER4V;
                            s->mb_type[i] |= MB_TYPE_INTRA;
                        }
                    }
                }
                xy+=2;
                i++;
            }
        }
    }
}

void ff_fix_long_b_mvs(MpegEncContext * s, int16_t (*mv_table)[2], int f_code, int type)
{
    int y;

    // RAL: 8 in MPEG-1, 16 in MPEG-4
    int range = (((s->out_format == FMT_MPEG1) ? 8 : 16) << f_code);
    
    if(s->avctx->me_range && range > s->avctx->me_range) range= s->avctx->me_range;

    /* clip / convert to intra 16x16 type MVs */
    for(y=0; y<s->mb_height; y++){
        int x;
        int xy= y*s->mb_stride;
        for(x=0; x<s->mb_width; x++){
            if (s->mb_type[xy] & type){    // RAL: "type" test added...
                if(   mv_table[xy][0] >=range || mv_table[xy][0] <-range
                   || mv_table[xy][1] >=range || mv_table[xy][1] <-range){

                    if(s->codec_id == CODEC_ID_MPEG1VIDEO && 0){
                    }else{
                        if     (mv_table[xy][0] > range-1) mv_table[xy][0]=  range-1;
                        else if(mv_table[xy][0] < -range ) mv_table[xy][0]= -range;
                        if     (mv_table[xy][1] > range-1) mv_table[xy][1]=  range-1;
                        else if(mv_table[xy][1] < -range ) mv_table[xy][1]= -range;
                    }
                }
            }
            xy++;
        }
    }
}
