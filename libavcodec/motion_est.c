/*
 * Motion estimation 
 * Copyright (c) 2000,2001 Gerard Lantau.
 * 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * new Motion Estimation (X1/EPZS) by Michael Niedermayer <michaelni@gmx.at>
 */
#include <stdlib.h>
#include <stdio.h>
#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"

static void halfpel_motion_search(MpegEncContext * s,
				  int *mx_ptr, int *my_ptr, int dmin,
				  int xmin, int ymin, int xmax, int ymax,
                                  int pred_x, int pred_y);

/* config it to test motion vector encoding (send random vectors) */
//#define CONFIG_TEST_MV_ENCODE

static int pix_sum(UINT8 * pix, int line_size)
{
    int s, i, j;

    s = 0;
    for (i = 0; i < 16; i++) {
	for (j = 0; j < 16; j += 8) {
	    s += pix[0];
	    s += pix[1];
	    s += pix[2];
	    s += pix[3];
	    s += pix[4];
	    s += pix[5];
	    s += pix[6];
	    s += pix[7];
	    pix += 8;
	}
	pix += line_size - 16;
    }
    return s;
}

static int pix_norm1(UINT8 * pix, int line_size)
{
    int s, i, j;
    UINT32 *sq = squareTbl + 256;

    s = 0;
    for (i = 0; i < 16; i++) {
	for (j = 0; j < 16; j += 8) {
	    s += sq[pix[0]];
	    s += sq[pix[1]];
	    s += sq[pix[2]];
	    s += sq[pix[3]];
	    s += sq[pix[4]];
	    s += sq[pix[5]];
	    s += sq[pix[6]];
	    s += sq[pix[7]];
	    pix += 8;
	}
	pix += line_size - 16;
    }
    return s;
}

static int pix_norm(UINT8 * pix1, UINT8 * pix2, int line_size)
{
    int s, i, j;
    UINT32 *sq = squareTbl + 256;

    s = 0;
    for (i = 0; i < 16; i++) {
	for (j = 0; j < 16; j += 8) {
	    s += sq[pix1[0] - pix2[0]];
	    s += sq[pix1[1] - pix2[1]];
	    s += sq[pix1[2] - pix2[2]];
	    s += sq[pix1[3] - pix2[3]];
	    s += sq[pix1[4] - pix2[4]];
	    s += sq[pix1[5] - pix2[5]];
	    s += sq[pix1[6] - pix2[6]];
	    s += sq[pix1[7] - pix2[7]];
	    pix1 += 8;
	    pix2 += 8;
	}
	pix1 += line_size - 16;
	pix2 += line_size - 16;
    }
    return s;
}

static void no_motion_search(MpegEncContext * s,
			     int *mx_ptr, int *my_ptr)
{
    *mx_ptr = 16 * s->mb_x;
    *my_ptr = 16 * s->mb_y;
}

static int full_motion_search(MpegEncContext * s,
                              int *mx_ptr, int *my_ptr, int range,
                              int xmin, int ymin, int xmax, int ymax)
{
    int x1, y1, x2, y2, xx, yy, x, y;
    int mx, my, dmin, d;
    UINT8 *pix;

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
    pix = s->new_picture[0] + (yy * s->linesize) + xx;
    dmin = 0x7fffffff;
    mx = 0;
    my = 0;
    for (y = y1; y <= y2; y++) {
	for (x = x1; x <= x2; x++) {
	    d = pix_abs16x16(pix, s->last_picture[0] + (y * s->linesize) + x,
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
                             int xmin, int ymin, int xmax, int ymax)
{
    int x1, y1, x2, y2, xx, yy, x, y;
    int mx, my, dmin, d;
    UINT8 *pix;

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

    pix = s->new_picture[0] + (yy * s->linesize) + xx;
    dmin = 0x7fffffff;
    mx = 0;
    my = 0;

    do {
	for (y = y1; y <= y2; y += range) {
	    for (x = x1; x <= x2; x += range) {
		d = pix_abs16x16(pix, s->last_picture[0] + (y * s->linesize) + x, s->linesize, 16);
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
                               int xmin, int ymin, int xmax, int ymax)
{
    int x1, y1, x2, y2, xx, yy, x, y, lastx, d;
    int mx, my, dminx, dminy;
    UINT8 *pix;

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

    pix = s->new_picture[0] + (yy * s->linesize) + xx;
    mx = 0;
    my = 0;

    x = xx;
    y = yy;
    do {
        dminx = 0x7fffffff;
        dminy = 0x7fffffff;

	lastx = x;
	for (x = x1; x <= x2; x += range) {
	    d = pix_abs16x16(pix, s->last_picture[0] + (y * s->linesize) + x, s->linesize, 16);
	    if (d < dminx || (d == dminx && (abs(x - xx) + abs(y - yy)) < (abs(mx - xx) + abs(my - yy)))) {
		dminx = d;
		mx = x;
	    }
	}

	x = lastx;
	for (y = y1; y <= y2; y += range) {
	    d = pix_abs16x16(pix, s->last_picture[0] + (y * s->linesize) + x, s->linesize, 16);
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

#define CHECK_MV(x,y)\
{\
    d = pix_abs16x16(new_pic, old_pic + (x) + (y)*pic_stride, pic_stride, 16);\
    d += (mv_penalty[((x)<<shift)-pred_x] + mv_penalty[((y)<<shift)-pred_y])*quant;\
    if(d<dmin){\
        best[0]=x;\
        best[1]=y;\
        dmin=d;\
    }\
}

#define CHECK_MV_DIR(x,y,new_dir)\
{\
    d = pix_abs16x16(new_pic, old_pic + (x) + (y)*pic_stride, pic_stride, 16);\
    d += (mv_penalty[((x)<<shift)-pred_x] + mv_penalty[((y)<<shift)-pred_y])*quant;\
    if(d<dmin){\
        best[0]=x;\
        best[1]=y;\
        dmin=d;\
        next_dir= new_dir;\
    }\
}

#define check(x,y,S,v)\
if( (x)<(xmin<<(S)) ) printf("%d %d %d %d xmin" #v, (x), (y), s->mb_x, s->mb_y);\
if( (x)>(xmax<<(S)) ) printf("%d %d %d %d xmax" #v, (x), (y), s->mb_x, s->mb_y);\
if( (y)<(ymin<<(S)) ) printf("%d %d %d %d ymin" #v, (x), (y), s->mb_x, s->mb_y);\
if( (y)>(ymax<<(S)) ) printf("%d %d %d %d ymax" #v, (x), (y), s->mb_x, s->mb_y);\


static inline int small_diamond_search(MpegEncContext * s, int *best, int dmin,
                                       UINT8 *new_pic, UINT8 *old_pic, int pic_stride,
                                       int pred_x, int pred_y, UINT16 *mv_penalty, int quant,
                                       int xmin, int ymin, int xmax, int ymax, int shift)
{
    int next_dir=-1;

    for(;;){
        int d;
        const int dir= next_dir;
        const int x= best[0];
        const int y= best[1];
        next_dir=-1;

//printf("%d", dir);
        if(dir!=2 && x>xmin) CHECK_MV_DIR(x-1, y  , 0)
        if(dir!=3 && y>ymin) CHECK_MV_DIR(x  , y-1, 1)
        if(dir!=0 && x<xmax) CHECK_MV_DIR(x+1, y  , 2)
        if(dir!=1 && y<ymax) CHECK_MV_DIR(x  , y+1, 3)

        if(next_dir==-1){
            return dmin;
        }
    }

/*    for(;;){
        int d;
        const int x= best[0];
        const int y= best[1];
        const int last_min=dmin;
        if(x>xmin) CHECK_MV(x-1, y  )
        if(y>xmin) CHECK_MV(x  , y-1)
        if(x<xmax) CHECK_MV(x+1, y  )
        if(y<xmax) CHECK_MV(x  , y+1)
        if(x>xmin && y>ymin) CHECK_MV(x-1, y-1)
        if(x>xmin && y<ymax) CHECK_MV(x-1, y+1)
        if(x<xmax && y>ymin) CHECK_MV(x+1, y-1)
        if(x<xmax && y<ymax) CHECK_MV(x+1, y+1)
        if(x-1>xmin) CHECK_MV(x-2, y  )
        if(y-1>xmin) CHECK_MV(x  , y-2)
        if(x+1<xmax) CHECK_MV(x+2, y  )
        if(y+1<xmax) CHECK_MV(x  , y+2)
        if(x-1>xmin && y-1>ymin) CHECK_MV(x-2, y-2)
        if(x-1>xmin && y+1<ymax) CHECK_MV(x-2, y+2)
        if(x+1<xmax && y-1>ymin) CHECK_MV(x+2, y-2)
        if(x+1<xmax && y+1<ymax) CHECK_MV(x+2, y+2)
        if(dmin==last_min) return dmin;
    }
    */
}

static inline int snake_search(MpegEncContext * s, int *best, int dmin,
                                       UINT8 *new_pic, UINT8 *old_pic, int pic_stride,
                                       int pred_x, int pred_y, UINT16 *mv_penalty, int quant,
                                       int xmin, int ymin, int xmax, int ymax, int shift)
{
    int dir=0;
    int c=1;
    static int x_dir[8]= {1,1,0,-1,-1,-1, 0, 1};
    static int y_dir[8]= {0,1,1, 1, 0,-1,-1,-1};
    int fails=0;
    int last_d[2]={dmin, dmin};

/*static int good=0;
static int bad=0;
static int point=0;

point++;
if(256*256*256*64%point==0)
{
    printf("%d %d %d\n", good, bad, point);
}*/

    for(;;){
        int x= best[0];
        int y= best[1];
        int d;
        x+=x_dir[dir];
        y+=y_dir[dir];
        if(x>=xmin && x<=xmax && y>=ymin && y<=ymax){
            d = pix_abs16x16(new_pic, old_pic + (x) + (y)*pic_stride, pic_stride, 16);
            d += (mv_penalty[((x)<<shift)-pred_x] + mv_penalty[((y)<<shift)-pred_y])*quant;
        }else{
            d = dmin + 10000; //FIXME smarter boundary handling
        }
        if(d<dmin){
            best[0]=x;
            best[1]=y;
            dmin=d;

            if(last_d[1] - last_d[0] > last_d[0] - d) c= -c;
            dir+=c;

            fails=0;
//good++;
            last_d[1]=last_d[0];
            last_d[0]=d;
        }else{
//bad++;
            if(fails){
                if(fails>=3) return dmin;
            }else{
                c= -c;
            }
            dir+=c*2;
            fails++;
        }
        dir&=7;
    }
}

static int epzs_motion_search(MpegEncContext * s,
                             int *mx_ptr, int *my_ptr,
                             int P[5][2], int pred_x, int pred_y,
                             int xmin, int ymin, int xmax, int ymax)
{
    int best[2]={0, 0};
    int d, dmin; 
    UINT8 *new_pic, *old_pic;
    const int pic_stride= s->linesize;
    const int pic_xy= (s->mb_y*pic_stride + s->mb_x)*16;
    UINT16 *mv_penalty= s->mv_penalty[s->f_code] + MAX_MV; // f_code of the prev frame
    int quant= s->qscale; // qscale of the prev frame
    const int shift= 1+s->quarter_sample;

    new_pic = s->new_picture[0] + pic_xy;
    old_pic = s->last_picture[0] + pic_xy;
   
    dmin = pix_abs16x16(new_pic, old_pic, pic_stride, 16);
    if(dmin<Z_THRESHOLD){
        *mx_ptr= 0;
        *my_ptr= 0;
//printf("Z");
        return dmin;
    }

    /* first line */
    if ((s->mb_y == 0 || s->first_slice_line || s->first_gob_line)) {
        CHECK_MV(P[1][0]>>shift, P[1][1]>>shift)
    }else{
        CHECK_MV(P[4][0]>>shift, P[4][1]>>shift)
        if(dmin<Z_THRESHOLD){
            *mx_ptr= P[4][0]>>shift;
            *my_ptr= P[4][1]>>shift;
//printf("M\n");
            return dmin;
        }
        CHECK_MV(P[1][0]>>shift, P[1][1]>>shift)
        CHECK_MV(P[2][0]>>shift, P[2][1]>>shift)
        CHECK_MV(P[3][0]>>shift, P[3][1]>>shift)
    }
    CHECK_MV(P[0][0]>>shift, P[0][1]>>shift)

//check(best[0],best[1],0, b0)
    if(s->full_search==ME_EPZS)
        dmin= small_diamond_search(s, best, dmin, new_pic, old_pic, pic_stride, 
                                   pred_x, pred_y, mv_penalty, quant, xmin, ymin, xmax, ymax, shift);
    else
        dmin=         snake_search(s, best, dmin, new_pic, old_pic, pic_stride, 
                                   pred_x, pred_y, mv_penalty, quant, xmin, ymin, xmax, ymax, shift);
//check(best[0],best[1],0, b1)
    *mx_ptr= best[0];
    *my_ptr= best[1];    

//    printf("%d %d %d \n", best[0], best[1], dmin);
    return dmin;
}

#define CHECK_HALF_MV(suffix, x, y) \
    d= pix_abs16x16_ ## suffix(pix, ptr+((x)>>1), s->linesize, 16);\
    d += (mv_penalty[pen_x + x] + mv_penalty[pen_y + y])*quant;\
    if(d<dminh){\
        dminh= d;\
        mx= mx1 + x;\
        my= my1 + y;\
    }

/* The idea would be to make half pel ME after Inter/Intra decision to 
   save time. */
static inline void halfpel_motion_search(MpegEncContext * s,
				  int *mx_ptr, int *my_ptr, int dmin,
				  int xmin, int ymin, int xmax, int ymax,
                                  int pred_x, int pred_y)
{
    UINT16 *mv_penalty= s->mv_penalty[s->f_code] + MAX_MV; // f_code of the prev frame
    const int quant= s->qscale;
    int pen_x, pen_y;
    int mx, my, mx1, my1, d, xx, yy, dminh;
    UINT8 *pix, *ptr;

    
    mx = *mx_ptr;
    my = *my_ptr;
    ptr = s->last_picture[0] + (my * s->linesize) + mx;

    xx = 16 * s->mb_x;
    yy = 16 * s->mb_y;
    pix =  s->new_picture[0] + (yy * s->linesize) + xx;
    
    dminh = dmin;

    if (mx > xmin && mx < xmax && 
        my > ymin && my < ymax) {

        mx= mx1= 2*(mx - xx);
        my= my1= 2*(my - yy);
        if(dmin < Z_THRESHOLD && mx==0 && my==0){
            *mx_ptr = 0;
            *my_ptr = 0;
            return;
        }
        
        pen_x= pred_x + mx;
        pen_y= pred_y + my;

        ptr-= s->linesize;
        CHECK_HALF_MV(xy2, -1, -1)
        CHECK_HALF_MV(y2 ,  0, -1)
        CHECK_HALF_MV(xy2, +1, -1)
        
        ptr+= s->linesize;
        CHECK_HALF_MV(x2 , -1,  0)
        CHECK_HALF_MV(x2 , +1,  0)
        CHECK_HALF_MV(xy2, -1, +1)
        CHECK_HALF_MV(y2 ,  0, +1)
        CHECK_HALF_MV(xy2, +1, +1)
    }else{
        mx= 2*(mx - xx);
        my= 2*(my - yy);
    }

    *mx_ptr = mx;
    *my_ptr = my;
}

#ifndef CONFIG_TEST_MV_ENCODE

int estimate_motion(MpegEncContext * s,
		    int mb_x, int mb_y,
		    int *mx_ptr, int *my_ptr)
{
    UINT8 *pix, *ppix;
    int sum, varc, vard, mx, my, range, dmin, xx, yy;
    int xmin, ymin, xmax, ymax;
    int rel_xmin, rel_ymin, rel_xmax, rel_ymax;
    int pred_x=0, pred_y=0;
    int P[5][2];
    const int shift= 1+s->quarter_sample;
    
    range = 8 * (1 << (s->f_code - 1));
    /* XXX: temporary kludge to avoid overflow for msmpeg4 */
    if (s->out_format == FMT_H263 && !s->h263_msmpeg4)
	range = range * 2;

    if (s->unrestricted_mv) {
        xmin = -16;
        ymin = -16;
        if (s->h263_plus)
            range *= 2;
        if(s->avctx==NULL || s->avctx->codec->id!=CODEC_ID_MPEG4){
            xmax = s->mb_width*16;
            ymax = s->mb_height*16;
        }else {
            /* XXX: dunno if this is correct but ffmpeg4 decoder wont like it otherwise 
	            (cuz the drawn edge isnt large enough))*/
            xmax = s->width;
            ymax = s->height;
        }
    } else {
        xmin = 0;
        ymin = 0;
        xmax = s->mb_width*16 - 16;
        ymax = s->mb_height*16 - 16;
    }
    switch(s->full_search) {
    case ME_ZERO:
    default:
	no_motion_search(s, &mx, &my);
        dmin = 0;
        break;
    case ME_FULL:
	dmin = full_motion_search(s, &mx, &my, range, xmin, ymin, xmax, ymax);
        break;
    case ME_LOG:
	dmin = log_motion_search(s, &mx, &my, range / 2, xmin, ymin, xmax, ymax);
        break;
    case ME_PHODS:
	dmin = phods_motion_search(s, &mx, &my, range / 2, xmin, ymin, xmax, ymax);
        break;
    case ME_X1: // just reserving some space for experiments ...
    case ME_EPZS:
        rel_xmin= xmin - s->mb_x*16;
        rel_xmax= xmax - s->mb_x*16;
        rel_ymin= ymin - s->mb_y*16;
        rel_ymax= ymax - s->mb_y*16;
        if(s->out_format == FMT_H263){
            static const int off[4]= {2, 1, 1, -1};
            const int mot_stride = s->mb_width*2 + 2;
            const int mot_xy = (s->mb_y*2 + 1)*mot_stride + s->mb_x*2 + 1;
         
            P[0][0] = s->motion_val[mot_xy    ][0];
            P[0][1] = s->motion_val[mot_xy    ][1];
            P[1][0] = s->motion_val[mot_xy - 1][0];
            P[1][1] = s->motion_val[mot_xy - 1][1];
            if(P[1][0] > (rel_xmax<<shift)) P[1][0]= (rel_xmax<<shift);

            /* special case for first line */
            if ((s->mb_y == 0 || s->first_slice_line || s->first_gob_line)) {
                pred_x = P[1][0];
                pred_y = P[1][1];
            } else {
                P[2][0] = s->motion_val[mot_xy - mot_stride             ][0];
                P[2][1] = s->motion_val[mot_xy - mot_stride             ][1];
                P[3][0] = s->motion_val[mot_xy - mot_stride + off[0]    ][0];
                P[3][1] = s->motion_val[mot_xy - mot_stride + off[0]    ][1];
                if(P[2][1] > (rel_ymax<<shift)) P[2][1]= (rel_ymax<<shift);
                if(P[3][0] < (rel_xmin<<shift)) P[3][0]= (rel_xmin<<shift);
                if(P[3][1] > (rel_ymax<<shift)) P[3][1]= (rel_ymax<<shift);
        
                P[4][0]= pred_x = mid_pred(P[1][0], P[2][0], P[3][0]);
                P[4][1]= pred_y = mid_pred(P[1][1], P[2][1], P[3][1]);
            }
        }else {
            const int xy= s->mb_y*s->mb_width + s->mb_x;
            pred_x= s->last_mv[0][0][0];
            pred_y= s->last_mv[0][0][1];

            P[0][0]= s->mv_table[0][xy  ];
            P[0][1]= s->mv_table[1][xy  ];
            if(s->mb_x == 0){
                P[1][0]= 0;
                P[1][1]= 0;
            }else{
                P[1][0]= s->mv_table[0][xy-1];
                P[1][1]= s->mv_table[1][xy-1];
                if(P[1][0] > (rel_xmax<<shift)) P[1][0]= (rel_xmax<<shift);
            }
    
            if (!(s->mb_y == 0 || s->first_slice_line || s->first_gob_line)) {
                P[2][0] = s->mv_table[0][xy - s->mb_width];
                P[2][1] = s->mv_table[1][xy - s->mb_width];
                P[3][0] = s->mv_table[0][xy - s->mb_width+1];
                P[3][1] = s->mv_table[1][xy - s->mb_width+1];
                if(P[2][1] > (rel_ymax<<shift)) P[2][1]= (rel_ymax<<shift);
                if(P[3][0] > (rel_xmax<<shift)) P[3][0]= (rel_xmax<<shift);
                if(P[3][0] < (rel_xmin<<shift)) P[3][0]= (rel_xmin<<shift);
                if(P[3][1] > (rel_ymax<<shift)) P[3][1]= (rel_ymax<<shift);
        
                P[4][0]= mid_pred(P[1][0], P[2][0], P[3][0]);
                P[4][1]= mid_pred(P[1][1], P[2][1], P[3][1]);
            }
        }
        dmin = epzs_motion_search(s, &mx, &my, P, pred_x, pred_y, rel_xmin, rel_ymin, rel_xmax, rel_ymax);
 
        mx+= s->mb_x*16;
        my+= s->mb_y*16;
        break;
    }

    /* intra / predictive decision */
    xx = mb_x * 16;
    yy = mb_y * 16;

    pix = s->new_picture[0] + (yy * s->linesize) + xx;
    /* At this point (mx,my) are full-pell and the absolute displacement */
    ppix = s->last_picture[0] + (my * s->linesize) + mx;

    sum = pix_sum(pix, s->linesize);
    varc = pix_norm1(pix, s->linesize);
    vard = pix_norm(pix, ppix, s->linesize);

    vard = vard >> 8;
    sum = sum >> 8;
    varc = (varc >> 8) - (sum * sum);
    s->mb_var[s->mb_width * mb_y + mb_x] = varc;
    s->avg_mb_var += varc;
    s->mc_mb_var += vard;
     
#if 0
    printf("varc=%4d avg_var=%4d (sum=%4d) vard=%4d mx=%2d my=%2d\n",
	   varc, s->avg_mb_var, sum, vard, mx - xx, my - yy);
#endif
    if (vard <= 64 || vard < varc) {
        if (s->full_search != ME_ZERO) {
            halfpel_motion_search(s, &mx, &my, dmin, xmin, ymin, xmax, ymax, pred_x, pred_y);
        } else {
            mx -= 16 * s->mb_x;
            my -= 16 * s->mb_y;
        }
//        check(mx + 32*s->mb_x, my + 32*s->mb_y, 1, end)
        
	*mx_ptr = mx;
	*my_ptr = my;
	return 0;
    } else {
	*mx_ptr = 0;
	*my_ptr = 0;
	return 1;
    }
}

#else

/* test version which generates valid random vectors */
int estimate_motion(MpegEncContext * s,
		    int mb_x, int mb_y,
		    int *mx_ptr, int *my_ptr)
{
    int xx, yy, x1, y1, x2, y2, range;

    if ((random() % 10) >= 5) {
	range = 8 * (1 << (s->f_code - 1));
	if (s->out_format == FMT_H263 && !s->h263_msmpeg4)
	    range = range * 2;

	xx = 16 * s->mb_x;
	yy = 16 * s->mb_y;
	x1 = xx - range;
	if (x1 < 0)
	    x1 = 0;
	x2 = xx + range - 1;
	if (x2 > (s->width - 16))
	    x2 = s->width - 16;
	y1 = yy - range;
	if (y1 < 0)
	    y1 = 0;
	y2 = yy + range - 1;
	if (y2 > (s->height - 16))
	    y2 = s->height - 16;

	*mx_ptr = (random() % (2 * (x2 - x1 + 1))) + 2 * (x1 - xx);
	*my_ptr = (random() % (2 * (y2 - y1 + 1))) + 2 * (y1 - yy);
	return 0;
    } else {
	*mx_ptr = 0;
	*my_ptr = 0;
	return 1;
    }
}

#endif
