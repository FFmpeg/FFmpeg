/*
 * Motion estimation 
 * Copyright (c) 2000,2001 Fabrice Bellard.
 * Copyright (c) 2002-2004 Michael Niedermayer
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
                                  int pred_x, int pred_y, uint8_t *src_data[3],
                                  uint8_t *ref_data[6], int stride, int uvstride,
                                  int size, int h, uint8_t * const mv_penalty);

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
d = cmp(s, src_y, (ref_y) + (x) + (y)*(stride), stride, h);

#define CMP_HPEL(d, dx, dy, x, y, size)\
{\
    const int dxy= (dx) + 2*(dy);\
    hpel_put[0][dxy](s->me.scratchpad, (ref_y) + (x) + (y)*(stride), stride, h);\
    d = cmp_sub(s, s->me.scratchpad, src_y, stride, h);\
}


#define CMP_QPEL(d, dx, dy, x, y, size)\
{\
    const int dxy= (dx) + 4*(dy);\
    qpel_put[0][dxy](s->me.scratchpad, (ref_y) + (x) + (y)*(stride), stride);\
    d = cmp_sub(s, s->me.scratchpad, src_y, stride, h);\
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
d = cmp(s, src_y, (ref_y) + (x) + (y)*(stride), stride, h);\
if(chroma_cmp){\
    int dxy= ((x)&1) + 2*((y)&1);\
    int c= ((x)>>1) + ((y)>>1)*uvstride;\
\
    chroma_hpel_put[0][dxy](s->me.scratchpad, ref_u + c, uvstride, h>>1);\
    d += chroma_cmp(s, s->me.scratchpad, src_u, uvstride, h>>1);\
    chroma_hpel_put[0][dxy](s->me.scratchpad, ref_v + c, uvstride, h>>1);\
    d += chroma_cmp(s, s->me.scratchpad, src_v, uvstride, h>>1);\
}

#define CMP_HPEL(d, dx, dy, x, y, size)\
{\
    const int dxy= (dx) + 2*(dy);\
    hpel_put[0][dxy](s->me.scratchpad, (ref_y) + (x) + (y)*(stride), stride, h);\
    d = cmp_sub(s, s->me.scratchpad, src_y, stride, h);\
    if(chroma_cmp_sub){\
        int cxy= (dxy) | ((x)&1) | (2*((y)&1));\
        int c= ((x)>>1) + ((y)>>1)*uvstride;\
        chroma_hpel_put[0][cxy](s->me.scratchpad, ref_u + c, uvstride, h>>1);\
        d += chroma_cmp_sub(s, s->me.scratchpad, src_u, uvstride, h>>1);\
        chroma_hpel_put[0][cxy](s->me.scratchpad, ref_v + c, uvstride, h>>1);\
        d += chroma_cmp_sub(s, s->me.scratchpad, src_v, uvstride, h>>1);\
    }\
}

#define CMP_QPEL(d, dx, dy, x, y, size)\
{\
    const int dxy= (dx) + 4*(dy);\
    qpel_put[0][dxy](s->me.scratchpad, (ref_y) + (x) + (y)*(stride), stride);\
    d = cmp_sub(s, s->me.scratchpad, src_y, stride, h);\
    if(chroma_cmp_sub){\
        int cxy, c;\
        int cx= (4*(x) + (dx))/2;\
        int cy= (4*(y) + (dy))/2;\
        cx= (cx>>1)|(cx&1);\
        cy= (cy>>1)|(cy&1);\
        cxy= (cx&1) + 2*(cy&1);\
        c= ((cx)>>1) + ((cy)>>1)*uvstride;\
        chroma_hpel_put[0][cxy](s->me.scratchpad, ref_u + c, uvstride, h>>1);\
        d += chroma_cmp_sub(s, s->me.scratchpad, src_u, uvstride, h>>1);\
        chroma_hpel_put[0][cxy](s->me.scratchpad, ref_v + c, uvstride, h>>1);\
        d += chroma_cmp_sub(s, s->me.scratchpad, src_v, uvstride, h>>1);\
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
            hpel_avg[1][bxy](dst, (ref_data[3]) + (bx>>1) + (by>>1)*(stride), stride, 8);\
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
        hpel_avg[0][bxy](s->me.scratchpad, (ref_data[3]) + (bx>>1) + (by>>1)*(stride), stride, 16);\
    }\
    d = cmp_func(s, s->me.scratchpad, src_y, stride, 16);\
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
            qpel_avg[1][bxy](dst, (ref_data[3]) + (bx>>2) + (by>>2)*(stride), stride);\
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
        qpel_avg[1][bxy](s->me.scratchpad               , (ref_data[3]) + (bx>>2) + (by>>2)*(stride)               , stride);\
        qpel_avg[1][bxy](s->me.scratchpad + 8           , (ref_data[3]) + (bx>>2) + (by>>2)*(stride) + 8           , stride);\
        qpel_avg[1][bxy](s->me.scratchpad     + 8*stride, (ref_data[3]) + (bx>>2) + (by>>2)*(stride)     + 8*stride, stride);\
        qpel_avg[1][bxy](s->me.scratchpad + 8 + 8*stride, (ref_data[3]) + (bx>>2) + (by>>2)*(stride) + 8 + 8*stride, stride);\
    }\
    d = cmp_func(s, s->me.scratchpad, src_y, stride, 16);\
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
    ff_set_cmp(&s->dsp, s->dsp.me_pre_cmp, s->avctx->me_pre_cmp);
    ff_set_cmp(&s->dsp, s->dsp.me_cmp, s->avctx->me_cmp);
    ff_set_cmp(&s->dsp, s->dsp.me_sub_cmp, s->avctx->me_sub_cmp);
    ff_set_cmp(&s->dsp, s->dsp.mb_cmp, s->avctx->mb_cmp);

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
            s->me.sub_motion_search= sad_hpel_motion_search; // 2050 vs. 2450 cycles
        else
            s->me.sub_motion_search= simple_hpel_motion_search;
    }

    if(s->avctx->me_cmp&FF_CMP_CHROMA){
        s->me.motion_search[0]= simple_chroma_epzs_motion_search;
        s->me.motion_search[1]= simple_chroma_epzs_motion_search4;
        s->me.motion_search[4]= simple_chroma_epzs_motion_search2;
    }else{
        s->me.motion_search[0]= simple_epzs_motion_search;
        s->me.motion_search[1]= simple_epzs_motion_search4;
        s->me.motion_search[4]= simple_epzs_motion_search2;
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
	    d = s->dsp.pix_abs[0][0](NULL, pix, ref_picture + (y * s->linesize) + x,
			     s->linesize, 16);
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
		d = s->dsp.pix_abs[0][0](NULL, pix, ref_picture + (y * s->linesize) + x, s->linesize, 16);
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
	    d = s->dsp.pix_abs[0][0](NULL, pix, ref_picture + (y * s->linesize) + x, s->linesize, 16);
	    if (d < dminx || (d == dminx && (abs(x - xx) + abs(y - yy)) < (abs(mx - xx) + abs(my - yy)))) {
		dminx = d;
		mx = x;
	    }
	}

	x = lastx;
	for (y = y1; y <= y2; y += range) {
	    d = s->dsp.pix_abs[0][0](NULL, pix, ref_picture + (y * s->linesize) + x, s->linesize, 16);
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
    d= s->dsp.pix_abs[size][(x?1:0)+(y?2:0)](NULL, pix, ptr+((x)>>1), stride, h);\
    d += (mv_penalty[pen_x + x] + mv_penalty[pen_y + y])*penalty_factor;\
    COPY3_IF_LT(dminh, d, dx, x, dy, y)\
}

static inline int sad_hpel_motion_search(MpegEncContext * s,
				  int *mx_ptr, int *my_ptr, int dmin,
                                  int pred_x, int pred_y, uint8_t *src_data[3],
                                  uint8_t *ref_data[6], int stride, int uvstride,                                  
                                  int size, int h, uint8_t * const mv_penalty)
{
    uint32_t *score_map= s->me.score_map;
    const int penalty_factor= s->me.sub_penalty_factor;
    int mx, my, dminh;
    uint8_t *pix, *ptr;
    const int xmin= s->me.xmin;
    const int ymin= s->me.ymin;
    const int xmax= s->me.xmax;
    const int ymax= s->me.ymax;

    if(s->me.skip){
//    printf("S");
        *mx_ptr = 0;
        *my_ptr = 0;
        return dmin;
    }
//    printf("N");
        
    pix = src_data[0];

    mx = *mx_ptr;
    my = *my_ptr;
    ptr = ref_data[0] + (my * stride) + mx;
    
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

        ptr-= stride;
        if(t<=b){
            CHECK_SAD_HALF_MV(y2 , 0, -1)
            if(l<=r){
                CHECK_SAD_HALF_MV(xy2, -1, -1)
                if(t+r<=b+l){
                    CHECK_SAD_HALF_MV(xy2, +1, -1)
                    ptr+= stride;
                }else{
                    ptr+= stride;
                    CHECK_SAD_HALF_MV(xy2, -1, +1)
                }
                CHECK_SAD_HALF_MV(x2 , -1,  0)
            }else{
                CHECK_SAD_HALF_MV(xy2, +1, -1)
                if(t+l<=b+r){
                    CHECK_SAD_HALF_MV(xy2, -1, -1)
                    ptr+= stride;
                }else{
                    ptr+= stride;
                    CHECK_SAD_HALF_MV(xy2, +1, +1)
                }
                CHECK_SAD_HALF_MV(x2 , +1,  0)
            }
        }else{
            if(l<=r){
                if(t+l<=b+r){
                    CHECK_SAD_HALF_MV(xy2, -1, -1)
                    ptr+= stride;
                }else{
                    ptr+= stride;
                    CHECK_SAD_HALF_MV(xy2, +1, +1)
                }
                CHECK_SAD_HALF_MV(x2 , -1,  0)
                CHECK_SAD_HALF_MV(xy2, -1, +1)
            }else{
                if(t+r<=b+l){
                    CHECK_SAD_HALF_MV(xy2, +1, -1)
                    ptr+= stride;
                }else{
                    ptr+= stride;
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

        s->current_picture.motion_val[0][mot_xy  ][0]= mx;
        s->current_picture.motion_val[0][mot_xy  ][1]= my;
        s->current_picture.motion_val[0][mot_xy+1][0]= mx;
        s->current_picture.motion_val[0][mot_xy+1][1]= my;

        mot_xy += s->b8_stride;
        s->current_picture.motion_val[0][mot_xy  ][0]= mx;
        s->current_picture.motion_val[0][mot_xy  ][1]= my;
        s->current_picture.motion_val[0][mot_xy+1][0]= mx;
        s->current_picture.motion_val[0][mot_xy+1][1]= my;
    }
}

/**
 * get fullpel ME search limits.
 */
static inline void get_limits(MpegEncContext *s, int x, int y)
{
/*
    if(s->avctx->me_range) s->me.range= s->avctx->me_range >> 1;
    else                   s->me.range= 16;
*/
    if (s->unrestricted_mv) {
        s->me.xmin = - x - 16;
        s->me.ymin = - y - 16;
        s->me.xmax = - x + s->mb_width *16;
        s->me.ymax = - y + s->mb_height*16;
    } else {
        s->me.xmin = - x;
        s->me.ymin = - y;
        s->me.xmax = - x + s->mb_width *16 - 16;
        s->me.ymax = - y + s->mb_height*16 - 16;
    }
}

static inline int h263_mv4_search(MpegEncContext *s, int mx, int my, int shift)
{
    const int size= 1;
    const int h=8;
    int block;
    int P[10][2];
    int dmin_sum=0, mx4_sum=0, my4_sum=0;
    uint8_t * const mv_penalty= s->me.mv_penalty[s->f_code] + MAX_MV;
    int same=1;
    const int stride= s->linesize;
    const int uvstride= s->uvlinesize;

    for(block=0; block<4; block++){
        int mx4, my4;
        int pred_x4, pred_y4;
        int dmin4;
        static const int off[4]= {2, 1, 1, -1};
        const int mot_stride = s->b8_stride;
        const int mot_xy = s->block_index[block];
        const int block_x= (block&1);
        const int block_y= (block>>1);
        uint8_t *src_data[3]= {
            s->new_picture.data[0] + 8*(2*s->mb_x + block_x) + stride  *8*(2*s->mb_y + block_y), //FIXME chroma?
            s->new_picture.data[1] + 4*(2*s->mb_x + block_x) + uvstride*4*(2*s->mb_y + block_y),
            s->new_picture.data[2] + 4*(2*s->mb_x + block_x) + uvstride*4*(2*s->mb_y + block_y)
        };
        uint8_t *ref_data[3]= {
            s->last_picture.data[0] + 8*(2*s->mb_x + block_x) + stride  *8*(2*s->mb_y + block_y), //FIXME chroma?
            s->last_picture.data[1] + 4*(2*s->mb_x + block_x) + uvstride*4*(2*s->mb_y + block_y),
            s->last_picture.data[2] + 4*(2*s->mb_x + block_x) + uvstride*4*(2*s->mb_y + block_y)
        };

        P_LEFT[0] = s->current_picture.motion_val[0][mot_xy - 1][0];
        P_LEFT[1] = s->current_picture.motion_val[0][mot_xy - 1][1];

        if(P_LEFT[0]       > (s->me.xmax<<shift)) P_LEFT[0]       = (s->me.xmax<<shift);

        /* special case for first line */
        if (s->first_slice_line && block<2) {
            pred_x4= P_LEFT[0];
            pred_y4= P_LEFT[1];
        } else {
            P_TOP[0]      = s->current_picture.motion_val[0][mot_xy - mot_stride             ][0];
            P_TOP[1]      = s->current_picture.motion_val[0][mot_xy - mot_stride             ][1];
            P_TOPRIGHT[0] = s->current_picture.motion_val[0][mot_xy - mot_stride + off[block]][0];
            P_TOPRIGHT[1] = s->current_picture.motion_val[0][mot_xy - mot_stride + off[block]][1];
            if(P_TOP[1]      > (s->me.ymax<<shift)) P_TOP[1]     = (s->me.ymax<<shift);
            if(P_TOPRIGHT[0] < (s->me.xmin<<shift)) P_TOPRIGHT[0]= (s->me.xmin<<shift);
            if(P_TOPRIGHT[0] > (s->me.xmax<<shift)) P_TOPRIGHT[0]= (s->me.xmax<<shift);
            if(P_TOPRIGHT[1] > (s->me.ymax<<shift)) P_TOPRIGHT[1]= (s->me.ymax<<shift);
    
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

        dmin4 = s->me.motion_search[1](s, &mx4, &my4, P, pred_x4, pred_y4, 
                                       src_data, ref_data, stride, uvstride, s->p_mv_table, (1<<16)>>shift, mv_penalty);

        dmin4= s->me.sub_motion_search(s, &mx4, &my4, dmin4, 
					  pred_x4, pred_y4, src_data, ref_data, stride, uvstride, size, h, mv_penalty);
        
        if(s->dsp.me_sub_cmp[0] != s->dsp.mb_cmp[0]
           && s->avctx->mb_decision == FF_MB_DECISION_SIMPLE){
            int dxy;
            const int offset= ((block&1) + (block>>1)*stride)*8;
            uint8_t *dest_y = s->me.scratchpad + offset;
            if(s->quarter_sample){
                uint8_t *ref= ref_data[0] + (mx4>>2) + (my4>>2)*stride;
                dxy = ((my4 & 3) << 2) | (mx4 & 3);

                if(s->no_rounding)
                    s->dsp.put_no_rnd_qpel_pixels_tab[1][dxy](dest_y   , ref    , stride);
                else
                    s->dsp.put_qpel_pixels_tab       [1][dxy](dest_y   , ref    , stride);
            }else{
                uint8_t *ref= ref_data[0] + (mx4>>1) + (my4>>1)*stride;
                dxy = ((my4 & 1) << 1) | (mx4 & 1);

                if(s->no_rounding)
                    s->dsp.put_no_rnd_pixels_tab[1][dxy](dest_y    , ref    , stride, h);
                else
                    s->dsp.put_pixels_tab       [1][dxy](dest_y    , ref    , stride, h);
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
            
        s->current_picture.motion_val[0][ s->block_index[block] ][0]= mx4;
        s->current_picture.motion_val[0][ s->block_index[block] ][1]= my4;

        if(mx4 != mx || my4 != my) same=0;
    }
    
    if(same)
        return INT_MAX;
    
    if(s->dsp.me_sub_cmp[0] != s->dsp.mb_cmp[0]){
        dmin_sum += s->dsp.mb_cmp[0](s, s->new_picture.data[0] + s->mb_x*16 + s->mb_y*16*stride, s->me.scratchpad, stride, 16);
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

        dmin_sum += s->dsp.mb_cmp[1](s, s->new_picture.data[1] + s->mb_x*8 + s->mb_y*8*s->uvlinesize, s->me.scratchpad  , s->uvlinesize, 8);
        dmin_sum += s->dsp.mb_cmp[1](s, s->new_picture.data[2] + s->mb_x*8 + s->mb_y*8*s->uvlinesize, s->me.scratchpad+8, s->uvlinesize, 8);
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

static int interlaced_search(MpegEncContext *s, uint8_t *frame_src_data[3], uint8_t *frame_ref_data[3], 
                             int16_t (*mv_tables[2][2])[2], uint8_t *field_select_tables[2], int f_code, int mx, int my)
{
    const int size=0;
    const int h=8;
    int block;
    int P[10][2];
    uint8_t * const mv_penalty= s->me.mv_penalty[f_code] + MAX_MV;
    int same=1;
    const int stride= 2*s->linesize;
    const int uvstride= 2*s->uvlinesize;
    int dmin_sum= 0;
    const int mot_stride= s->mb_stride;
    const int xy= s->mb_x + s->mb_y*mot_stride;
    
    s->me.ymin>>=1;
    s->me.ymax>>=1;
    
    for(block=0; block<2; block++){
        int field_select;
        int best_dmin= INT_MAX;
        int best_field= -1;

        uint8_t *src_data[3]= {
            frame_src_data[0] + s->  linesize*block,
            frame_src_data[1] + s->uvlinesize*block,
            frame_src_data[2] + s->uvlinesize*block
        };

        for(field_select=0; field_select<2; field_select++){
            int dmin, mx_i, my_i, pred_x, pred_y;
            uint8_t *ref_data[3]= {
                frame_ref_data[0] + s->  linesize*field_select,
                frame_ref_data[1] + s->uvlinesize*field_select,
                frame_ref_data[2] + s->uvlinesize*field_select
            };
            int16_t (*mv_table)[2]= mv_tables[block][field_select];
            
            P_LEFT[0] = mv_table[xy - 1][0];
            P_LEFT[1] = mv_table[xy - 1][1];
            if(P_LEFT[0]       > (s->me.xmax<<1)) P_LEFT[0]       = (s->me.xmax<<1);
            
            pred_x= P_LEFT[0];
            pred_y= P_LEFT[1];
            
            if(!s->first_slice_line){
                P_TOP[0]      = mv_table[xy - mot_stride][0];
                P_TOP[1]      = mv_table[xy - mot_stride][1];
                P_TOPRIGHT[0] = mv_table[xy - mot_stride + 1][0];
                P_TOPRIGHT[1] = mv_table[xy - mot_stride + 1][1];
                if(P_TOP[1]      > (s->me.ymax<<1)) P_TOP[1]     = (s->me.ymax<<1);
                if(P_TOPRIGHT[0] < (s->me.xmin<<1)) P_TOPRIGHT[0]= (s->me.xmin<<1);
                if(P_TOPRIGHT[0] > (s->me.xmax<<1)) P_TOPRIGHT[0]= (s->me.xmax<<1);
                if(P_TOPRIGHT[1] > (s->me.ymax<<1)) P_TOPRIGHT[1]= (s->me.ymax<<1);
    
                P_MEDIAN[0]= mid_pred(P_LEFT[0], P_TOP[0], P_TOPRIGHT[0]);
                P_MEDIAN[1]= mid_pred(P_LEFT[1], P_TOP[1], P_TOPRIGHT[1]);
            }
            P_MV1[0]= mx; //FIXME not correct if block != field_select
            P_MV1[1]= my / 2;
            
            dmin = s->me.motion_search[4](s, &mx_i, &my_i, P, pred_x, pred_y, 
                                           src_data, ref_data, stride, uvstride, mv_table, (1<<16)>>1, mv_penalty);

            dmin= s->me.sub_motion_search(s, &mx_i, &my_i, dmin, 
                                           pred_x, pred_y, src_data, ref_data, stride, uvstride, size, h, mv_penalty);
            
            mv_table[xy][0]= mx_i;
            mv_table[xy][1]= my_i;
            
            if(s->dsp.me_sub_cmp[0] != s->dsp.mb_cmp[0]
               && s->avctx->mb_decision == FF_MB_DECISION_SIMPLE){
                int dxy;

                //FIXME chroma ME
                uint8_t *ref= ref_data[0] + (mx_i>>1) + (my_i>>1)*stride;
                dxy = ((my_i & 1) << 1) | (mx_i & 1);

                if(s->no_rounding){
                    s->dsp.put_no_rnd_pixels_tab[size][dxy](s->me.scratchpad, ref    , stride, h);
                }else{
                    s->dsp.put_pixels_tab       [size][dxy](s->me.scratchpad, ref    , stride, h);
                }
                dmin= s->dsp.mb_cmp[size](s, src_data[0], s->me.scratchpad, stride, h);
                dmin+= (mv_penalty[mx_i-pred_x] + mv_penalty[my_i-pred_y] + 1)*s->me.mb_penalty_factor;
            }else
                dmin+= s->me.mb_penalty_factor; //field_select bits
                
            dmin += field_select != block; //slightly prefer same field
            
            if(dmin < best_dmin){
                best_dmin= dmin;
                best_field= field_select;
            }
        }
        {
            int16_t (*mv_table)[2]= mv_tables[block][best_field];

            if(mv_table[xy][0] != mx) same=0; //FIXME check if these checks work and are any good at all
            if(mv_table[xy][1]&1) same=0;
            if(mv_table[xy][1]*2 != my) same=0; 
            if(best_field != block) same=0;
        }

        field_select_tables[block][xy]= best_field;
        dmin_sum += best_dmin;
    }
    
    s->me.ymin<<=1;
    s->me.ymax<<=1;

    if(same)
        return INT_MAX;
    
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
    int sum, varc, vard, mx, my, dmin, xx, yy;
    int pred_x=0, pred_y=0;
    int P[10][2];
    const int shift= 1+s->quarter_sample;
    int mb_type=0;
    uint8_t *ref_picture= s->last_picture.data[0];
    Picture * const pic= &s->current_picture;
    uint8_t * const mv_penalty= s->me.mv_penalty[s->f_code] + MAX_MV;
    const int stride= s->linesize;
    const int uvstride= s->uvlinesize;
    uint8_t *src_data[3]= {
        s->new_picture.data[0] + 16*(mb_x + stride*mb_y),
        s->new_picture.data[1] + 8*(mb_x + uvstride*mb_y),
        s->new_picture.data[2] + 8*(mb_x + uvstride*mb_y)
    };
    uint8_t *ref_data[3]= {
        s->last_picture.data[0] + 16*(mb_x + stride*mb_y),
        s->last_picture.data[1] + 8*(mb_x + uvstride*mb_y),
        s->last_picture.data[2] + 8*(mb_x + uvstride*mb_y)
    };

    assert(s->quarter_sample==0 || s->quarter_sample==1);

    s->me.penalty_factor    = get_penalty_factor(s, s->avctx->me_cmp);
    s->me.sub_penalty_factor= get_penalty_factor(s, s->avctx->me_sub_cmp);
    s->me.mb_penalty_factor = get_penalty_factor(s, s->avctx->mb_cmp);

    get_limits(s, 16*mb_x, 16*mb_y);
    s->me.skip=0;

    switch(s->me_method) {
    case ME_ZERO:
    default:
	no_motion_search(s, &mx, &my);
        mx-= mb_x*16;
        my-= mb_y*16;
        dmin = 0;
        break;
#if 0
    case ME_FULL:
	dmin = full_motion_search(s, &mx, &my, range, ref_picture);
        mx-= mb_x*16;
        my-= mb_y*16;
        break;
    case ME_LOG:
	dmin = log_motion_search(s, &mx, &my, range / 2, ref_picture);
        mx-= mb_x*16;
        my-= mb_y*16;
        break;
    case ME_PHODS:
	dmin = phods_motion_search(s, &mx, &my, range / 2, ref_picture);
        mx-= mb_x*16;
        my-= mb_y*16;
        break;
#endif
    case ME_X1:
    case ME_EPZS:
       {
            const int mot_stride = s->b8_stride;
            const int mot_xy = s->block_index[0];

            P_LEFT[0]       = s->current_picture.motion_val[0][mot_xy - 1][0];
            P_LEFT[1]       = s->current_picture.motion_val[0][mot_xy - 1][1];

            if(P_LEFT[0]       > (s->me.xmax<<shift)) P_LEFT[0]       = (s->me.xmax<<shift);

            if(!s->first_slice_line) {
                P_TOP[0]      = s->current_picture.motion_val[0][mot_xy - mot_stride    ][0];
                P_TOP[1]      = s->current_picture.motion_val[0][mot_xy - mot_stride    ][1];
                P_TOPRIGHT[0] = s->current_picture.motion_val[0][mot_xy - mot_stride + 2][0];
                P_TOPRIGHT[1] = s->current_picture.motion_val[0][mot_xy - mot_stride + 2][1];
                if(P_TOP[1]      > (s->me.ymax<<shift)) P_TOP[1]     = (s->me.ymax<<shift);
                if(P_TOPRIGHT[0] < (s->me.xmin<<shift)) P_TOPRIGHT[0]= (s->me.xmin<<shift);
                if(P_TOPRIGHT[1] > (s->me.ymax<<shift)) P_TOPRIGHT[1]= (s->me.ymax<<shift);
        
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
        dmin = s->me.motion_search[0](s, &mx, &my, P, pred_x, pred_y, 
                                      src_data, ref_data, stride, uvstride, s->p_mv_table, (1<<16)>>shift, mv_penalty);
 
        break;
    }

    /* intra / predictive decision */
    xx = mb_x * 16;
    yy = mb_y * 16;

    pix = src_data[0];
    /* At this point (mx,my) are full-pell and the relative displacement */
    ppix = ref_data[0] + (my * s->linesize) + mx;
    
    sum = s->dsp.pix_sum(pix, s->linesize);
    
    varc = (s->dsp.pix_norm1(pix, s->linesize) - (((unsigned)(sum*sum))>>8) + 500 + 128)>>8;
    vard = (s->dsp.sse[0](NULL, pix, ppix, s->linesize, 16)+128)>>8;

//printf("%d %d %d %X %X %X\n", s->mb_width, mb_x, mb_y,(int)s, (int)s->mb_var, (int)s->mc_mb_var); fflush(stdout);
    pic->mb_var   [s->mb_stride * mb_y + mb_x] = varc;
    pic->mc_mb_var[s->mb_stride * mb_y + mb_x] = vard;
    pic->mb_mean  [s->mb_stride * mb_y + mb_x] = (sum+128)>>8;
//    pic->mb_cmp_score[s->mb_stride * mb_y + mb_x] = dmin; 
    s->mb_var_sum_temp    += varc;
    s->mc_mb_var_sum_temp += vard;
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
            mb_type|= CANDIDATE_MB_TYPE_INTRA;
        if (varc*2 + 200 > vard){
            mb_type|= CANDIDATE_MB_TYPE_INTER;
            s->me.sub_motion_search(s, &mx, &my, dmin,
				   pred_x, pred_y, src_data, ref_data, stride, uvstride, 0, 16, mv_penalty);
            if(s->flags&CODEC_FLAG_MV0)
                if(mx || my)
                    mb_type |= CANDIDATE_MB_TYPE_SKIPED; //FIXME check difference
        }else{
            mx <<=shift;
            my <<=shift;
        }
        if((s->flags&CODEC_FLAG_4MV)
           && !s->me.skip && varc>50 && vard>10){
            if(h263_mv4_search(s, mx, my, shift) < INT_MAX)
                mb_type|=CANDIDATE_MB_TYPE_INTER4V;

            set_p_mv_tables(s, mx, my, 0);
        }else
            set_p_mv_tables(s, mx, my, 1);
        if((s->flags&CODEC_FLAG_INTERLACED_ME)
           && !s->me.skip){ //FIXME varc/d checks
            if(interlaced_search(s, src_data, ref_data, s->p_field_mv_table, s->p_field_select_table, s->f_code, mx, my) < INT_MAX)
                mb_type |= CANDIDATE_MB_TYPE_INTER_I;
        }
    }else{
        int intra_score, i;
        mb_type= CANDIDATE_MB_TYPE_INTER;

        dmin= s->me.sub_motion_search(s, &mx, &my, dmin,
                                    pred_x, pred_y, src_data, ref_data, stride, uvstride, 0, 16, mv_penalty);
        if(s->avctx->me_sub_cmp != s->avctx->mb_cmp && !s->me.skip)
            dmin= s->me.get_mb_score(s, mx, my, pred_x, pred_y, src_data, ref_data, stride, uvstride, mv_penalty);

        if((s->flags&CODEC_FLAG_4MV)
           && !s->me.skip && varc>50 && vard>10){
            int dmin4= h263_mv4_search(s, mx, my, shift);
            if(dmin4 < dmin){
                mb_type= CANDIDATE_MB_TYPE_INTER4V;
                dmin=dmin4;
            }
        }
        if((s->flags&CODEC_FLAG_INTERLACED_ME)
           && !s->me.skip){ //FIXME varc/d checks
            int dmin_i= interlaced_search(s, src_data, ref_data, s->p_field_mv_table, s->p_field_select_table, s->f_code, mx, my);
            if(dmin_i < dmin){
                mb_type = CANDIDATE_MB_TYPE_INTER_I;
                dmin= dmin_i;
            }
        }
                
//        pic->mb_cmp_score[s->mb_stride * mb_y + mb_x] = dmin; 
        set_p_mv_tables(s, mx, my, mb_type!=CANDIDATE_MB_TYPE_INTER4V);

        /* get intra luma score */
        if((s->avctx->mb_cmp&0xFF)==FF_CMP_SSE){
            intra_score= (varc<<8) - 500; //FIXME dont scale it down so we dont have to fix it
        }else{
            int mean= (sum+128)>>8;
            mean*= 0x01010101;
            
            for(i=0; i<16; i++){
                *(uint32_t*)(&s->me.scratchpad[i*s->linesize+ 0]) = mean;
                *(uint32_t*)(&s->me.scratchpad[i*s->linesize+ 4]) = mean;
                *(uint32_t*)(&s->me.scratchpad[i*s->linesize+ 8]) = mean;
                *(uint32_t*)(&s->me.scratchpad[i*s->linesize+12]) = mean;
            }

            intra_score= s->dsp.mb_cmp[0](s, s->me.scratchpad, pix, s->linesize, 16);
        }
#if 0 //FIXME
        /* get chroma score */
        if(s->avctx->mb_cmp&FF_CMP_CHROMA){
            for(i=1; i<3; i++){
                uint8_t *dest_c;
                int mean;
                
                if(s->out_format == FMT_H263){
                    mean= (s->dc_val[i][mb_x + mb_y*s->b8_stride] + 4)>>3; //FIXME not exact but simple ;)
                }else{
                    mean= (s->last_dc[i] + 4)>>3;
                }
                dest_c = s->new_picture.data[i] + (mb_y * 8  * (s->uvlinesize)) + mb_x * 8;
                
                mean*= 0x01010101;
                for(i=0; i<8; i++){
                    *(uint32_t*)(&s->me.scratchpad[i*s->uvlinesize+ 0]) = mean;
                    *(uint32_t*)(&s->me.scratchpad[i*s->uvlinesize+ 4]) = mean;
                }
                
                intra_score+= s->dsp.mb_cmp[1](s, s->me.scratchpad, dest_c, s->uvlinesize);
            }                
        }
#endif
        intra_score += s->me.mb_penalty_factor*16;
        
        if(intra_score < dmin){
            mb_type= CANDIDATE_MB_TYPE_INTRA;
            s->current_picture.mb_type[mb_y*s->mb_stride + mb_x]= CANDIDATE_MB_TYPE_INTRA; //FIXME cleanup
        }else
            s->current_picture.mb_type[mb_y*s->mb_stride + mb_x]= 0;
        
        if (vard <= 64 || vard < varc) { //FIXME
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
    int mx, my, dmin;
    int pred_x=0, pred_y=0;
    int P[10][2];
    const int shift= 1+s->quarter_sample;
    uint8_t * const mv_penalty= s->me.mv_penalty[s->f_code] + MAX_MV;
    const int xy= mb_x + mb_y*s->mb_stride;
    const int stride= s->linesize;
    const int uvstride= s->uvlinesize;
    uint8_t *src_data[3]= {
        s->new_picture.data[0] + 16*(mb_x + stride*mb_y),
        s->new_picture.data[1] + 8*(mb_x + uvstride*mb_y),
        s->new_picture.data[2] + 8*(mb_x + uvstride*mb_y)
    };
    uint8_t *ref_data[3]= {
        s->last_picture.data[0] + 16*(mb_x + stride*mb_y),
        s->last_picture.data[1] + 8*(mb_x + uvstride*mb_y),
        s->last_picture.data[2] + 8*(mb_x + uvstride*mb_y)
    };
    
    assert(s->quarter_sample==0 || s->quarter_sample==1);

    s->me.pre_penalty_factor    = get_penalty_factor(s, s->avctx->me_pre_cmp);

    get_limits(s, 16*mb_x, 16*mb_y);
    s->me.skip=0;

    P_LEFT[0]       = s->p_mv_table[xy + 1][0];
    P_LEFT[1]       = s->p_mv_table[xy + 1][1];

    if(P_LEFT[0]       < (s->me.xmin<<shift)) P_LEFT[0]       = (s->me.xmin<<shift);

    /* special case for first line */
    if (s->first_slice_line) {
        pred_x= P_LEFT[0];
        pred_y= P_LEFT[1];
        P_TOP[0]= P_TOPRIGHT[0]= P_MEDIAN[0]=
        P_TOP[1]= P_TOPRIGHT[1]= P_MEDIAN[1]= 0; //FIXME 
    } else {
        P_TOP[0]      = s->p_mv_table[xy + s->mb_stride    ][0];
        P_TOP[1]      = s->p_mv_table[xy + s->mb_stride    ][1];
        P_TOPRIGHT[0] = s->p_mv_table[xy + s->mb_stride - 1][0];
        P_TOPRIGHT[1] = s->p_mv_table[xy + s->mb_stride - 1][1];
        if(P_TOP[1]      < (s->me.ymin<<shift)) P_TOP[1]     = (s->me.ymin<<shift);
        if(P_TOPRIGHT[0] > (s->me.xmax<<shift)) P_TOPRIGHT[0]= (s->me.xmax<<shift);
        if(P_TOPRIGHT[1] < (s->me.ymin<<shift)) P_TOPRIGHT[1]= (s->me.ymin<<shift);
    
        P_MEDIAN[0]= mid_pred(P_LEFT[0], P_TOP[0], P_TOPRIGHT[0]);
        P_MEDIAN[1]= mid_pred(P_LEFT[1], P_TOP[1], P_TOPRIGHT[1]);

        pred_x = P_MEDIAN[0];
        pred_y = P_MEDIAN[1];
    }
    dmin = s->me.pre_motion_search(s, &mx, &my, P, pred_x, pred_y, 
                                   src_data, ref_data, stride, uvstride, s->p_mv_table, (1<<16)>>shift, mv_penalty);

    s->p_mv_table[xy][0] = mx<<shift;
    s->p_mv_table[xy][1] = my<<shift;
    
    return dmin;
}

static int ff_estimate_motion_b(MpegEncContext * s,
                       int mb_x, int mb_y, int16_t (*mv_table)[2], uint8_t *src_data[3],
                       uint8_t *ref_data[3], int stride, int uvstride, int f_code)
{
    int mx, my, dmin;
    int pred_x=0, pred_y=0;
    int P[10][2];
    const int shift= 1+s->quarter_sample;
    const int mot_stride = s->mb_stride;
    const int mot_xy = mb_y*mot_stride + mb_x;
    uint8_t * const ref_picture= ref_data[0] - 16*s->mb_x - 16*s->mb_y*s->linesize; //FIXME ugly
    uint8_t * const mv_penalty= s->me.mv_penalty[f_code] + MAX_MV;
    int mv_scale;
        
    s->me.penalty_factor    = get_penalty_factor(s, s->avctx->me_cmp);
    s->me.sub_penalty_factor= get_penalty_factor(s, s->avctx->me_sub_cmp);
    s->me.mb_penalty_factor = get_penalty_factor(s, s->avctx->mb_cmp);

    get_limits(s, 16*mb_x, 16*mb_y);

    switch(s->me_method) {
    case ME_ZERO:
    default:
	no_motion_search(s, &mx, &my);
        dmin = 0;
        mx-= mb_x*16;
        my-= mb_y*16;
        break;
#if 0
    case ME_FULL:
	dmin = full_motion_search(s, &mx, &my, range, ref_picture);
        mx-= mb_x*16;
        my-= mb_y*16;
        break;
    case ME_LOG:
	dmin = log_motion_search(s, &mx, &my, range / 2, ref_picture);
        mx-= mb_x*16;
        my-= mb_y*16;
        break;
    case ME_PHODS:
	dmin = phods_motion_search(s, &mx, &my, range / 2, ref_picture);
        mx-= mb_x*16;
        my-= mb_y*16;
        break;
#endif
    case ME_X1:
    case ME_EPZS:
       {
            P_LEFT[0]        = mv_table[mot_xy - 1][0];
            P_LEFT[1]        = mv_table[mot_xy - 1][1];

            if(P_LEFT[0]       > (s->me.xmax<<shift)) P_LEFT[0]       = (s->me.xmax<<shift);

            /* special case for first line */
            if (!s->first_slice_line) {
                P_TOP[0] = mv_table[mot_xy - mot_stride             ][0];
                P_TOP[1] = mv_table[mot_xy - mot_stride             ][1];
                P_TOPRIGHT[0] = mv_table[mot_xy - mot_stride + 1         ][0];
                P_TOPRIGHT[1] = mv_table[mot_xy - mot_stride + 1         ][1];
                if(P_TOP[1] > (s->me.ymax<<shift)) P_TOP[1]= (s->me.ymax<<shift);
                if(P_TOPRIGHT[0] < (s->me.xmin<<shift)) P_TOPRIGHT[0]= (s->me.xmin<<shift);
                if(P_TOPRIGHT[1] > (s->me.ymax<<shift)) P_TOPRIGHT[1]= (s->me.ymax<<shift);
        
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
        
        dmin = s->me.motion_search[0](s, &mx, &my, P, pred_x, pred_y, 
                                      src_data, ref_data, stride, uvstride, s->p_mv_table, mv_scale, mv_penalty);
 
        break;
    }
    
    dmin= s->me.sub_motion_search(s, &mx, &my, dmin,
				   pred_x, pred_y, src_data, ref_data, stride, uvstride, 0, 16, mv_penalty);
                                   
    if(s->avctx->me_sub_cmp != s->avctx->mb_cmp && !s->me.skip)
        dmin= s->me.get_mb_score(s, mx, my, pred_x, pred_y, src_data, ref_data, stride, uvstride, mv_penalty);

//printf("%d %d %d %d//", s->mb_x, s->mb_y, mx, my);
//    s->mb_type[mb_y*s->mb_width + mb_x]= mb_type;
    mv_table[mot_xy][0]= mx;
    mv_table[mot_xy][1]= my;

    return dmin;
}

static inline int check_bidir_mv(MpegEncContext * s, uint8_t *src_data[3], uint8_t *ref_data[6],
                   int stride, int uvstride,
                   int motion_fx, int motion_fy,
                   int motion_bx, int motion_by,
                   int pred_fx, int pred_fy,
                   int pred_bx, int pred_by,
                   int size, int h)
{
    //FIXME optimize?
    //FIXME move into template?
    //FIXME better f_code prediction (max mv & distance)
    //FIXME pointers
    uint8_t * const mv_penalty= s->me.mv_penalty[s->f_code] + MAX_MV; // f_code of the prev frame
    uint8_t *dest_y = s->me.scratchpad;
    uint8_t *ptr;
    int dxy;
    int src_x, src_y;
    int fbmin;

    if(s->quarter_sample){
        dxy = ((motion_fy & 3) << 2) | (motion_fx & 3);
        src_x = motion_fx >> 2;
        src_y = motion_fy >> 2;

        ptr = ref_data[0] + (src_y * stride) + src_x;
        s->dsp.put_qpel_pixels_tab[0][dxy](dest_y    , ptr    , stride);

        dxy = ((motion_by & 3) << 2) | (motion_bx & 3);
        src_x = motion_bx >> 2;
        src_y = motion_by >> 2;
    
        ptr = ref_data[3] + (src_y * stride) + src_x;
        s->dsp.avg_qpel_pixels_tab[size][dxy](dest_y    , ptr    , stride);
    }else{
        dxy = ((motion_fy & 1) << 1) | (motion_fx & 1);
        src_x = motion_fx >> 1;
        src_y = motion_fy >> 1;

        ptr = ref_data[0] + (src_y * stride) + src_x;
        s->dsp.put_pixels_tab[size][dxy](dest_y    , ptr    , stride, h);

        dxy = ((motion_by & 1) << 1) | (motion_bx & 1);
        src_x = motion_bx >> 1;
        src_y = motion_by >> 1;
    
        ptr = ref_data[3] + (src_y * stride) + src_x;
        s->dsp.avg_pixels_tab[size][dxy](dest_y    , ptr    , stride, h);
    }

    fbmin = (mv_penalty[motion_fx-pred_fx] + mv_penalty[motion_fy-pred_fy])*s->me.mb_penalty_factor
           +(mv_penalty[motion_bx-pred_bx] + mv_penalty[motion_by-pred_by])*s->me.mb_penalty_factor
           + s->dsp.mb_cmp[size](s, src_data[0], dest_y, stride, h); //FIXME new_pic
           
    if(s->avctx->mb_cmp&FF_CMP_CHROMA){
    }
    //FIXME CHROMA !!!
           
    return fbmin;
}

/* refine the bidir vectors in hq mode and return the score in both lq & hq mode*/
static inline int bidir_refine(MpegEncContext * s, uint8_t *src_data[3], uint8_t *ref_data[6],
                                  int stride, int uvstride,
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
    
    fbmin= check_bidir_mv(s, src_data, ref_data, stride, uvstride,
                          motion_fx, motion_fy,
                          motion_bx, motion_by,
                          pred_fx, pred_fy,
                          pred_bx, pred_by,
                          0, 16);

   return fbmin;
}

static inline int direct_search(MpegEncContext * s, uint8_t *src_data[3], uint8_t *ref_data[6],
                                int stride, int uvstride,
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
    
        s->me.co_located_mv[i][0]= s->next_picture.motion_val[0][index][0];
        s->me.co_located_mv[i][1]= s->next_picture.motion_val[0][index][1];
        s->me.direct_basis_mv[i][0]= s->me.co_located_mv[i][0]*time_pb/time_pp + ((i& 1)<<(shift+3));
        s->me.direct_basis_mv[i][1]= s->me.co_located_mv[i][1]*time_pb/time_pp + ((i>>1)<<(shift+3));
//        s->me.direct_basis_mv[1][i][0]= s->me.co_located_mv[i][0]*(time_pb - time_pp)/time_pp + ((i &1)<<(shift+3);
//        s->me.direct_basis_mv[1][i][1]= s->me.co_located_mv[i][1]*(time_pb - time_pp)/time_pp + ((i>>1)<<(shift+3);

        max= FFMAX(s->me.direct_basis_mv[i][0], s->me.direct_basis_mv[i][0] - s->me.co_located_mv[i][0])>>shift;
        min= FFMIN(s->me.direct_basis_mv[i][0], s->me.direct_basis_mv[i][0] - s->me.co_located_mv[i][0])>>shift;
        max+= 16*mb_x + 1; // +-1 is for the simpler rounding
        min+= 16*mb_x - 1;
        xmax= FFMIN(xmax, s->width - max);
        xmin= FFMAX(xmin, - 16     - min);

        max= FFMAX(s->me.direct_basis_mv[i][1], s->me.direct_basis_mv[i][1] - s->me.co_located_mv[i][1])>>shift;
        min= FFMIN(s->me.direct_basis_mv[i][1], s->me.direct_basis_mv[i][1] - s->me.co_located_mv[i][1])>>shift;
        max+= 16*mb_y + 1; // +-1 is for the simpler rounding
        min+= 16*mb_y - 1;
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
    
    s->me.xmin= xmin;
    s->me.ymin= ymin;
    s->me.xmax= xmax;
    s->me.ymax= ymax;

    P_LEFT[0]        = clip(mv_table[mot_xy - 1][0], xmin<<shift, xmax<<shift);
    P_LEFT[1]        = clip(mv_table[mot_xy - 1][1], ymin<<shift, ymax<<shift);

    /* special case for first line */
    if (!s->first_slice_line) { //FIXME maybe allow this over thread boundary as its cliped
        P_TOP[0]      = clip(mv_table[mot_xy - mot_stride             ][0], xmin<<shift, xmax<<shift);
        P_TOP[1]      = clip(mv_table[mot_xy - mot_stride             ][1], ymin<<shift, ymax<<shift);
        P_TOPRIGHT[0] = clip(mv_table[mot_xy - mot_stride + 1         ][0], xmin<<shift, xmax<<shift);
        P_TOPRIGHT[1] = clip(mv_table[mot_xy - mot_stride + 1         ][1], ymin<<shift, ymax<<shift);
    
        P_MEDIAN[0]= mid_pred(P_LEFT[0], P_TOP[0], P_TOPRIGHT[0]);
        P_MEDIAN[1]= mid_pred(P_LEFT[1], P_TOP[1], P_TOPRIGHT[1]);
    }
 
    //FIXME direct_search  ptr in context!!! (needed for chroma anyway or this will get messy)   
    if(s->flags&CODEC_FLAG_QPEL){
        dmin = simple_direct_qpel_epzs_motion_search(s, &mx, &my, P, 0, 0, 
                                                     src_data, ref_data, stride, uvstride, mv_table, 1<<14, mv_penalty);
        dmin = simple_direct_qpel_qpel_motion_search(s, &mx, &my, dmin,
                                                0, 0, src_data, ref_data, stride, uvstride, 0, 16, mv_penalty);
        
        if(s->avctx->me_sub_cmp != s->avctx->mb_cmp && !s->me.skip)
            dmin= simple_direct_qpel_qpel_get_mb_score(s, mx, my, 0, 0, src_data, ref_data, stride, uvstride, mv_penalty);
    }else{
        dmin = simple_direct_hpel_epzs_motion_search(s, &mx, &my, P, 0, 0, 
                                                     src_data, ref_data, stride, uvstride, mv_table, 1<<15, mv_penalty);
        dmin = simple_direct_hpel_hpel_motion_search(s, &mx, &my, dmin,
                                                0, 0, src_data, ref_data, stride, uvstride, 0, 16, mv_penalty);

        if(s->avctx->me_sub_cmp != s->avctx->mb_cmp && !s->me.skip)
            dmin= simple_direct_hpel_hpel_get_mb_score(s, mx, my, 0, 0, src_data, ref_data, stride, uvstride, mv_penalty);
    }
    
    get_limits(s, 16*mb_x, 16*mb_y); //restore s->me.?min/max, maybe not needed

    s->b_direct_mv_table[mot_xy][0]= mx;
    s->b_direct_mv_table[mot_xy][1]= my;
    return dmin;
}

void ff_estimate_b_frame_motion(MpegEncContext * s,
                             int mb_x, int mb_y)
{
    const int penalty_factor= s->me.mb_penalty_factor;
    int fmin, bmin, dmin, fbmin, bimin, fimin;
    int type=0;
    const int stride= s->linesize;
    const int uvstride= s->uvlinesize;
    uint8_t *src_data[3]= {
        s->new_picture.data[0] + 16*(s->mb_x + stride*s->mb_y),
        s->new_picture.data[1] + 8*(s->mb_x + uvstride*s->mb_y),
        s->new_picture.data[2] + 8*(s->mb_x + uvstride*s->mb_y)
    };
    uint8_t *ref_data[6]= {
        s->last_picture.data[0] + 16*(s->mb_x + stride*s->mb_y),
        s->last_picture.data[1] + 8*(s->mb_x + uvstride*s->mb_y),
        s->last_picture.data[2] + 8*(s->mb_x + uvstride*s->mb_y),
        s->next_picture.data[0] + 16*(s->mb_x + stride*s->mb_y),
        s->next_picture.data[1] + 8*(s->mb_x + uvstride*s->mb_y),
        s->next_picture.data[2] + 8*(s->mb_x + uvstride*s->mb_y)
    };
    
    s->me.skip=0;
    if (s->codec_id == CODEC_ID_MPEG4)
        dmin= direct_search(s, src_data, ref_data, stride, uvstride, mb_x, mb_y);
    else
        dmin= INT_MAX;
//FIXME penalty stuff for non mpeg4
    s->me.skip=0;
    fmin= ff_estimate_motion_b(s, mb_x, mb_y, s->b_forw_mv_table, src_data, 
                               ref_data, stride, uvstride, s->f_code) + 3*penalty_factor;
    
    s->me.skip=0;
    bmin= ff_estimate_motion_b(s, mb_x, mb_y, s->b_back_mv_table, src_data, 
                               ref_data+3, stride, uvstride, s->b_code) + 2*penalty_factor;
//printf(" %d %d ", s->b_forw_mv_table[xy][0], s->b_forw_mv_table[xy][1]);

    s->me.skip=0;
    fbmin= bidir_refine(s, src_data, ref_data, stride, uvstride, mb_x, mb_y) + penalty_factor;
//printf("%d %d %d %d\n", dmin, fmin, bmin, fbmin);
    
    if(s->flags & CODEC_FLAG_INTERLACED_ME){
        const int xy = mb_y*s->mb_stride + mb_x;

//FIXME mb type penalty
        s->me.skip=0;
        fimin= interlaced_search(s, src_data, ref_data  , 
                                 s->b_field_mv_table[0], s->b_field_select_table[0], s->f_code,
                                 s->b_forw_mv_table[xy][0], s->b_forw_mv_table[xy][1]);
        bimin= interlaced_search(s, src_data, ref_data+3, 
                                 s->b_field_mv_table[1], s->b_field_select_table[1], s->b_code,
                                 s->b_back_mv_table[xy][0], s->b_back_mv_table[xy][1]);
    }else
        fimin= bimin= INT_MAX;

    {
        int score= fmin;
        type = CANDIDATE_MB_TYPE_FORWARD;
        
        if (dmin <= score){
            score = dmin;
            type = CANDIDATE_MB_TYPE_DIRECT;
        }
        if(bmin<score){
            score=bmin;
            type= CANDIDATE_MB_TYPE_BACKWARD; 
        }
        if(fbmin<score){
            score=fbmin;
            type= CANDIDATE_MB_TYPE_BIDIR;
        }
        if(fimin<score){
            score=fimin;
            type= CANDIDATE_MB_TYPE_FORWARD_I;
        }
        if(bimin<score){
            score=bimin;
            type= CANDIDATE_MB_TYPE_BACKWARD_I;
        }
        
        score= ((unsigned)(score*score + 128*256))>>16;
        s->mc_mb_var_sum_temp += score;
        s->current_picture.mc_mb_var[mb_y*s->mb_stride + mb_x] = score; //FIXME use SSE
    }

    if(s->avctx->mb_decision > FF_MB_DECISION_SIMPLE){
        type= CANDIDATE_MB_TYPE_FORWARD | CANDIDATE_MB_TYPE_BACKWARD | CANDIDATE_MB_TYPE_BIDIR | CANDIDATE_MB_TYPE_DIRECT;
        if(fimin < INT_MAX)
            type |= CANDIDATE_MB_TYPE_FORWARD_I;
        if(bimin < INT_MAX)
            type |= CANDIDATE_MB_TYPE_BACKWARD_I;
        if(fimin < INT_MAX && bimin < INT_MAX){
            type |= CANDIDATE_MB_TYPE_BIDIR_I;
        }
         //FIXME something smarter
        if(dmin>256*256*16) type&= ~CANDIDATE_MB_TYPE_DIRECT; //dont try direct mode if its invalid for this MB
#if 0        
        if(s->out_format == FMT_MPEG1)
            type |= CANDIDATE_MB_TYPE_INTRA;
#endif
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
    
//printf("%d no:%d %d//\n", clip, noclip, f_code);
    if(s->flags&CODEC_FLAG_4MV){
        const int wrap= s->b8_stride;

        /* clip / convert to intra 8x8 type MVs */
        for(y=0; y<s->mb_height; y++){
            int xy= y*2*wrap;
            int i= y*s->mb_stride;
            int x;

            for(x=0; x<s->mb_width; x++){
                if(s->mb_type[i]&CANDIDATE_MB_TYPE_INTER4V){
                    int block;
                    for(block=0; block<4; block++){
                        int off= (block& 1) + (block>>1)*wrap;
                        int mx= s->current_picture.motion_val[0][ xy + off ][0];
                        int my= s->current_picture.motion_val[0][ xy + off ][1];

                        if(   mx >=range || mx <-range
                           || my >=range || my <-range){
                            s->mb_type[i] &= ~CANDIDATE_MB_TYPE_INTER4V;
                            s->mb_type[i] |= CANDIDATE_MB_TYPE_INTRA;
                            s->current_picture.mb_type[i]= CANDIDATE_MB_TYPE_INTRA;
                        }
                    }
                }
                xy+=2;
                i++;
            }
        }
    }
}

/**
 *
 * @param truncate 1 for truncation, 0 for using intra
 */
void ff_fix_long_mvs(MpegEncContext * s, uint8_t *field_select_table, int field_select, 
                     int16_t (*mv_table)[2], int f_code, int type, int truncate)
{
    int y, h_range, v_range;

    // RAL: 8 in MPEG-1, 16 in MPEG-4
    int range = (((s->out_format == FMT_MPEG1) ? 8 : 16) << f_code);

    if(s->msmpeg4_version) range= 16;
    if(s->avctx->me_range && range > s->avctx->me_range) range= s->avctx->me_range;

    h_range= range;
    v_range= field_select_table ? range>>1 : range;

    /* clip / convert to intra 16x16 type MVs */
    for(y=0; y<s->mb_height; y++){
        int x;
        int xy= y*s->mb_stride;
        for(x=0; x<s->mb_width; x++){
            if (s->mb_type[xy] & type){    // RAL: "type" test added...
                if(field_select_table==NULL || field_select_table[xy] == field_select){
                    if(   mv_table[xy][0] >=h_range || mv_table[xy][0] <-h_range
                       || mv_table[xy][1] >=v_range || mv_table[xy][1] <-v_range){

                        if(truncate){
                            if     (mv_table[xy][0] > h_range-1) mv_table[xy][0]=  h_range-1;
                            else if(mv_table[xy][0] < -h_range ) mv_table[xy][0]= -h_range;
                            if     (mv_table[xy][1] > v_range-1) mv_table[xy][1]=  v_range-1;
                            else if(mv_table[xy][1] < -v_range ) mv_table[xy][1]= -v_range;
                        }else{
                            s->mb_type[xy] &= ~type;
                            s->mb_type[xy] |= CANDIDATE_MB_TYPE_INTRA;
                            mv_table[xy][0]=
                            mv_table[xy][1]= 0;
                        }
                    }
                }
            }
            xy++;
        }
    }
}
