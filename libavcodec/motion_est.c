/*
 * Motion estimation 
 * Copyright (c) 2000,2001 Fabrice Bellard.
 * Copyright (c) 2002 Michael Niedermayer
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
#include <stdlib.h>
#include <stdio.h>
#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"

#define SQ(a) ((a)*(a))
#define INTER_BIAS	257

#define P_LAST P[0]
#define P_LEFT P[1]
#define P_TOP P[2]
#define P_TOPRIGHT P[3]
#define P_MEDIAN P[4]
#define P_LAST_LEFT P[5]
#define P_LAST_RIGHT P[6]
#define P_LAST_TOP P[7]
#define P_LAST_BOTTOM P[8]
#define P_MV1 P[9]


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

static int pix_dev(UINT8 * pix, int line_size, int mean)
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
                              int xmin, int ymin, int xmax, int ymax, uint8_t *ref_picture)
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
	    d = pix_abs16x16(pix, ref_picture + (y * s->linesize) + x,
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
		d = pix_abs16x16(pix, ref_picture + (y * s->linesize) + x, s->linesize);
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
	    d = pix_abs16x16(pix, ref_picture + (y * s->linesize) + x, s->linesize);
	    if (d < dminx || (d == dminx && (abs(x - xx) + abs(y - yy)) < (abs(mx - xx) + abs(my - yy)))) {
		dminx = d;
		mx = x;
	    }
	}

	x = lastx;
	for (y = y1; y <= y2; y += range) {
	    d = pix_abs16x16(pix, ref_picture + (y * s->linesize) + x, s->linesize);
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
    const int key= ((y)<<ME_MAP_MV_BITS) + (x) + map_generation;\
    const int index= (((y)<<ME_MAP_SHIFT) + (x))&(ME_MAP_SIZE-1);\
    if(map[index]!=key){\
        d = pix_abs16x16(new_pic, old_pic + (x) + (y)*pic_stride, pic_stride);\
        d += (mv_penalty[((x)<<shift)-pred_x] + mv_penalty[((y)<<shift)-pred_y])*quant;\
        COPY3_IF_LT(dmin, d, best[0], x, best[1], y)\
        map[index]= key;\
        score_map[index]= d;\
    }\
}

#define CHECK_MV_DIR(x,y,new_dir)\
{\
    const int key= ((y)<<ME_MAP_MV_BITS) + (x) + map_generation;\
    const int index= (((y)<<ME_MAP_SHIFT) + (x))&(ME_MAP_SIZE-1);\
    if(map[index]!=key){\
        d = pix_abs(new_pic, old_pic + (x) + (y)*pic_stride, pic_stride);\
        d += (mv_penalty[((x)<<shift)-pred_x] + mv_penalty[((y)<<shift)-pred_y])*quant;\
        if(d<dmin){\
            best[0]=x;\
            best[1]=y;\
            dmin=d;\
            next_dir= new_dir;\
        }\
        map[index]= key;\
        score_map[index]= d;\
    }\
}

#define CHECK_MV4(x,y)\
{\
    const int key= ((y)<<ME_MAP_MV_BITS) + (x) + map_generation;\
    const int index= (((y)<<ME_MAP_SHIFT) + (x))&(ME_MAP_SIZE-1);\
    if(map[index]!=key){\
        d = pix_abs8x8(new_pic, old_pic + (x) + (y)*pic_stride, pic_stride);\
        d += (mv_penalty[((x)<<shift)-pred_x] + mv_penalty[((y)<<shift)-pred_y])*quant;\
        COPY3_IF_LT(dmin, d, best[0], x, best[1], y)\
        map[index]= key;\
        score_map[index]= d;\
    }\
}

#define check(x,y,S,v)\
if( (x)<(xmin<<(S)) ) printf("%d %d %d %d %d xmin" #v, xmin, (x), (y), s->mb_x, s->mb_y);\
if( (x)>(xmax<<(S)) ) printf("%d %d %d %d %d xmax" #v, xmax, (x), (y), s->mb_x, s->mb_y);\
if( (y)<(ymin<<(S)) ) printf("%d %d %d %d %d ymin" #v, ymin, (x), (y), s->mb_x, s->mb_y);\
if( (y)>(ymax<<(S)) ) printf("%d %d %d %d %d ymax" #v, ymax, (x), (y), s->mb_x, s->mb_y);\


static inline int small_diamond_search(MpegEncContext * s, int *best, int dmin,
                                       UINT8 *new_pic, UINT8 *old_pic, int pic_stride,
                                       int pred_x, int pred_y, UINT16 *mv_penalty, int quant,
                                       int xmin, int ymin, int xmax, int ymax, int shift,
                                       uint32_t *map, uint16_t *score_map, int map_generation,
                                       op_pixels_abs_func pix_abs)
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

#if 1
#define SNAKE_1 3
#define SNAKE_2 2
#else
#define SNAKE_1 7
#define SNAKE_2 3
#endif
static inline int snake_search(MpegEncContext * s, int *best, int dmin,
                                       UINT8 *new_pic, UINT8 *old_pic, int pic_stride,
                                       int pred_x, int pred_y, UINT16 *mv_penalty, int quant,
                                       int xmin, int ymin, int xmax, int ymax, int shift,
                                       uint32_t *map, uint16_t *score_map,int map_generation,
                                       op_pixels_abs_func pix_abs)
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
            const int key= ((y)<<ME_MAP_MV_BITS) + (x) + map_generation;
            const int index= (((y)<<ME_MAP_SHIFT) + (x))&(ME_MAP_SIZE-1);
            if(map[index]!=key){
                d = pix_abs(new_pic, old_pic + (x) + (y)*pic_stride, pic_stride);
                d += (mv_penalty[((x)<<shift)-pred_x] + mv_penalty[((y)<<shift)-pred_y])*quant;
                map[index]=key;
                score_map[index]=d;
            }else
                d= dmin+1;
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
                if(fails>=SNAKE_1+1) return dmin;
            }else{
                if(dir&1) dir-= c*3;
                else      c= -c;
//                c= -c;
            }
            dir+=c*SNAKE_2;
            fails++;
        }
        dir&=7;
    }
}

static inline int cross_search(MpegEncContext * s, int *best, int dmin,
                                       UINT8 *new_pic, UINT8 *old_pic, int pic_stride,
                                       int pred_x, int pred_y, UINT16 *mv_penalty, int quant,
                                       int xmin, int ymin, int xmax, int ymax, int shift,
                                       uint32_t *map, uint16_t *score_map,int map_generation,
                                       op_pixels_abs_func pix_abs)
{
    static int x_dir[4]= {-1, 0, 1, 0};
    static int y_dir[4]= { 0,-1, 0, 1};
    int improvement[2]={100000, 100000};
    int dirs[2]={2, 3};
    int dir;
    int last_dir= -1;
    
    for(;;){
        dir= dirs[ improvement[0] > improvement[1] ? 0 : 1 ];
        if(improvement[dir&1]==-1) return dmin;
        
        {
            const int x= best[0] + x_dir[dir];
            const int y= best[1] + y_dir[dir];
            const int key= (y<<ME_MAP_MV_BITS) + x + map_generation;
            const int index= ((y<<ME_MAP_SHIFT) + x)&(ME_MAP_SIZE-1);
            int d;
            if(x>=xmin && x<=xmax && y>=ymin && y<=ymax){
                if(map[index]!=key){
                    d = pix_abs(new_pic, old_pic + x + y*pic_stride, pic_stride);
                    d += (mv_penalty[(x<<shift)-pred_x] + mv_penalty[(y<<shift)-pred_y])*quant;
                    map[index]=key;
                    score_map[index]=d;
                    if(d<dmin){
                        improvement[dir&1]= dmin-d;
                        improvement[(dir&1)^1]++;
                        dmin=d;
                        best[0]= x;
                        best[1]= y;
                        last_dir=dir;
                        continue;
                    }
                }else{
                    d= score_map[index];
                }
            }else{
                d= dmin + 1000; //FIXME is this a good idea?
            }
            /* evaluated point was cached or checked and worse */

            if(last_dir==dir){
                improvement[dir&1]= -1;
            }else{
                improvement[dir&1]= d-dmin;
                last_dir= dirs[dir&1]= dir^2;
            }
        }
    }
}

static inline int update_map_generation(MpegEncContext * s)
{
    s->me_map_generation+= 1<<(ME_MAP_MV_BITS*2);
    if(s->me_map_generation==0){
        s->me_map_generation= 1<<(ME_MAP_MV_BITS*2);
        memset(s->me_map, 0, sizeof(uint32_t)*ME_MAP_SIZE);
    }
    return s->me_map_generation;
}

static int epzs_motion_search(MpegEncContext * s,
                             int *mx_ptr, int *my_ptr,
                             int P[10][2], int pred_x, int pred_y,
                             int xmin, int ymin, int xmax, int ymax, uint8_t * ref_picture)
{
    int best[2]={0, 0};
    int d, dmin; 
    UINT8 *new_pic, *old_pic;
    const int pic_stride= s->linesize;
    const int pic_xy= (s->mb_y*pic_stride + s->mb_x)*16;
    UINT16 *mv_penalty= s->mv_penalty[s->f_code] + MAX_MV; // f_code of the prev frame
    int quant= s->qscale; // qscale of the prev frame
    const int shift= 1+s->quarter_sample;
    uint32_t *map= s->me_map;
    uint16_t *score_map= s->me_score_map;
    int map_generation;

    new_pic = s->new_picture[0] + pic_xy;
    old_pic = ref_picture + pic_xy;
    
    map_generation= update_map_generation(s);

    dmin = pix_abs16x16(new_pic, old_pic, pic_stride);
    map[0]= map_generation;
    score_map[0]= dmin;

    /* first line */
    if ((s->mb_y == 0 || s->first_slice_line)) {
        CHECK_MV(P_LEFT[0]>>shift, P_LEFT[1]>>shift)
        CHECK_MV(P_LAST[0]>>shift, P_LAST[1]>>shift)
    }else{
        if(dmin<256 && ( P_LEFT[0]    |P_LEFT[1]
                        |P_TOP[0]     |P_TOP[1]
                        |P_TOPRIGHT[0]|P_TOPRIGHT[1])==0){
            *mx_ptr= 0;
            *my_ptr= 0;
            s->skip_me=1;
            return dmin;
        }
        CHECK_MV(P_MEDIAN[0]>>shift, P_MEDIAN[1]>>shift)
        if(dmin>256*2){
            CHECK_MV(P_LAST[0]    >>shift, P_LAST[1]    >>shift)
            CHECK_MV(P_LEFT[0]    >>shift, P_LEFT[1]    >>shift)
            CHECK_MV(P_TOP[0]     >>shift, P_TOP[1]     >>shift)
            CHECK_MV(P_TOPRIGHT[0]>>shift, P_TOPRIGHT[1]>>shift)
        }
    }
    if(dmin>256*4){
        CHECK_MV(P_LAST_RIGHT[0] >>shift, P_LAST_RIGHT[1] >>shift)
        CHECK_MV(P_LAST_BOTTOM[0]>>shift, P_LAST_BOTTOM[1]>>shift)
    }
#if 0 //doest only slow things down
    if(dmin>512*3){
        int step;
        dmin= score_map[0];
        best[0]= best[1]=0;
        for(step=128; step>0; step>>=1){
            const int step2= step;
            int y;
            for(y=-step2+best[1]; y<=step2+best[1]; y+=step){
                int x;
                if(y<ymin || y>ymax) continue;

                for(x=-step2+best[0]; x<=step2+best[0]; x+=step){
                    if(x<xmin || x>xmax) continue;
                    if(x==best[0] && y==best[1]) continue;
                    CHECK_MV(x,y)
                }
            }
        }
    }
#endif
//check(best[0],best[1],0, b0)
    if(s->me_method==ME_EPZS)
        dmin= small_diamond_search(s, best, dmin, new_pic, old_pic, pic_stride, 
                                   pred_x, pred_y, mv_penalty, quant, xmin, ymin, xmax, ymax, 
                                   shift, map, score_map, map_generation, pix_abs16x16);
    else
        dmin=         cross_search(s, best, dmin, new_pic, old_pic, pic_stride, 
                                   pred_x, pred_y, mv_penalty, quant, xmin, ymin, xmax, ymax, 
                                   shift, map, score_map, map_generation, pix_abs16x16);
//check(best[0],best[1],0, b1)
    *mx_ptr= best[0];
    *my_ptr= best[1];    

//    printf("%d %d %d \n", best[0], best[1], dmin);
    return dmin;
}

static int epzs_motion_search4(MpegEncContext * s, int block,
                             int *mx_ptr, int *my_ptr,
                             int P[10][2], int pred_x, int pred_y,
                             int xmin, int ymin, int xmax, int ymax, uint8_t *ref_picture)
{
    int best[2]={0, 0};
    int d, dmin; 
    UINT8 *new_pic, *old_pic;
    const int pic_stride= s->linesize;
    const int pic_xy= ((s->mb_y*2 + (block>>1))*pic_stride + s->mb_x*2 + (block&1))*8;
    UINT16 *mv_penalty= s->mv_penalty[s->f_code] + MAX_MV; // f_code of the prev frame
    int quant= s->qscale; // qscale of the prev frame
    const int shift= 1+s->quarter_sample;
    uint32_t *map= s->me_map;
    uint16_t *score_map= s->me_score_map;
    int map_generation;

    new_pic = s->new_picture[0] + pic_xy;
    old_pic = ref_picture + pic_xy;

    map_generation= update_map_generation(s);

    dmin = 1000000;
//printf("%d %d %d %d //",xmin, ymin, xmax, ymax); 
    /* first line */
    if ((s->mb_y == 0 || s->first_slice_line) && block<2) {
        CHECK_MV4(P_LEFT[0]>>shift, P_LEFT[1]>>shift)
        CHECK_MV4(P_LAST[0]>>shift, P_LAST[1]>>shift)
        CHECK_MV4(P_MV1[0]>>shift, P_MV1[1]>>shift)
    }else{
        CHECK_MV4(P_MV1[0]>>shift, P_MV1[1]>>shift)
        //FIXME try some early stop
        if(dmin>64*2){
            CHECK_MV4(P_MEDIAN[0]>>shift, P_MEDIAN[1]>>shift)
            CHECK_MV4(P_LEFT[0]>>shift, P_LEFT[1]>>shift)
            CHECK_MV4(P_TOP[0]>>shift, P_TOP[1]>>shift)
            CHECK_MV4(P_TOPRIGHT[0]>>shift, P_TOPRIGHT[1]>>shift)
            CHECK_MV4(P_LAST[0]>>shift, P_LAST[1]>>shift)
        }
    }
    if(dmin>64*4){
        CHECK_MV4(P_LAST_RIGHT[0]>>shift, P_LAST_RIGHT[1]>>shift)
        CHECK_MV4(P_LAST_BOTTOM[0]>>shift, P_LAST_BOTTOM[1]>>shift)
    }

    if(s->me_method==ME_EPZS)
        dmin= small_diamond_search(s, best, dmin, new_pic, old_pic, pic_stride, 
                                   pred_x, pred_y, mv_penalty, quant, xmin, ymin, xmax, ymax, 
                                   shift, map, score_map, map_generation, pix_abs8x8);
    else
        dmin=         cross_search(s, best, dmin, new_pic, old_pic, pic_stride, 
                                   pred_x, pred_y, mv_penalty, quant, xmin, ymin, xmax, ymax, 
                                   shift, map, score_map, map_generation, pix_abs8x8);

    *mx_ptr= best[0];
    *my_ptr= best[1];    

//    printf("%d %d %d \n", best[0], best[1], dmin);
    return dmin;
}

#define CHECK_HALF_MV(suffix, x, y) \
{\
    d= pix_abs_ ## suffix(pix, ptr+((x)>>1), s->linesize);\
    d += (mv_penalty[pen_x + x] + mv_penalty[pen_y + y])*quant;\
    COPY3_IF_LT(dminh, d, dx, x, dy, y)\
}

    
/* The idea would be to make half pel ME after Inter/Intra decision to 
   save time. */
static inline int halfpel_motion_search(MpegEncContext * s,
				  int *mx_ptr, int *my_ptr, int dmin,
				  int xmin, int ymin, int xmax, int ymax,
                                  int pred_x, int pred_y, uint8_t *ref_picture,
                                  op_pixels_abs_func pix_abs_x2, 
                                  op_pixels_abs_func pix_abs_y2, op_pixels_abs_func pix_abs_xy2, int n)
{
    UINT16 *mv_penalty= s->mv_penalty[s->f_code] + MAX_MV; // f_code of the prev frame
    const int quant= s->qscale;
    int mx, my, xx, yy, dminh;
    UINT8 *pix, *ptr;

    if(s->skip_me){
        *mx_ptr = 0;
        *my_ptr = 0;
        return dmin;
    }else

    xx = 16 * s->mb_x + 8*(n&1);
    yy = 16 * s->mb_y + 8*(n>>1);
    pix =  s->new_picture[0] + (yy * s->linesize) + xx;

    mx = *mx_ptr;
    my = *my_ptr;
    ptr = ref_picture + ((yy + my) * s->linesize) + (xx + mx);
    
    dminh = dmin;

    if (mx > xmin && mx < xmax && 
        my > ymin && my < ymax) {
        int dx=0, dy=0;
        int d, pen_x, pen_y; 

        mx<<=1;
        my<<=1;
        
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

static inline int fast_halfpel_motion_search(MpegEncContext * s,
				  int *mx_ptr, int *my_ptr, int dmin,
				  int xmin, int ymin, int xmax, int ymax,
                                  int pred_x, int pred_y, uint8_t *ref_picture,
                                  op_pixels_abs_func pix_abs_x2, 
                                  op_pixels_abs_func pix_abs_y2, op_pixels_abs_func pix_abs_xy2, int n)
{
    UINT16 *mv_penalty= s->mv_penalty[s->f_code] + MAX_MV; // f_code of the prev frame
    uint16_t *score_map= s->me_score_map;
    const int quant= s->qscale;
    int mx, my, xx, yy, dminh;
    UINT8 *pix, *ptr;

    if(s->skip_me){
//    printf("S");
        *mx_ptr = 0;
        *my_ptr = 0;
        return dmin;
    }
//    printf("N");
        
    xx = 16 * s->mb_x + 8*(n&1);
    yy = 16 * s->mb_y + 8*(n>>1);
    pix =  s->new_picture[0] + (yy * s->linesize) + xx;

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
            CHECK_HALF_MV(y2 ,  0, -1)
            if(l<=r){
                CHECK_HALF_MV(xy2, -1, -1)
                if(t+r<=b+l){
                    CHECK_HALF_MV(xy2, +1, -1)
                    ptr+= s->linesize;
                }else{
                    ptr+= s->linesize;
                    CHECK_HALF_MV(xy2, -1, +1)
                }
                CHECK_HALF_MV(x2 , -1,  0)
            }else{
                CHECK_HALF_MV(xy2, +1, -1)
                if(t+l<=b+r){
                    CHECK_HALF_MV(xy2, -1, -1)
                    ptr+= s->linesize;
                }else{
                    ptr+= s->linesize;
                    CHECK_HALF_MV(xy2, +1, +1)
                }
                CHECK_HALF_MV(x2 , +1,  0)
            }
        }else{
            if(l<=r){
                if(t+l<=b+r){
                    CHECK_HALF_MV(xy2, -1, -1)
                    ptr+= s->linesize;
                }else{
                    ptr+= s->linesize;
                    CHECK_HALF_MV(xy2, +1, +1)
                }
                CHECK_HALF_MV(x2 , -1,  0)
                CHECK_HALF_MV(xy2, -1, +1)
            }else{
                if(t+r<=b+l){
                    CHECK_HALF_MV(xy2, +1, -1)
                    ptr+= s->linesize;
                }else{
                    ptr+= s->linesize;
                    CHECK_HALF_MV(xy2, -1, +1)
                }
                CHECK_HALF_MV(x2 , +1,  0)
                CHECK_HALF_MV(xy2, +1, +1)
            }
            CHECK_HALF_MV(y2 ,  0, +1)
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
    const int xy= s->mb_x + 1 + (s->mb_y + 1)*(s->mb_width + 2);
    
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

static inline void get_limits(MpegEncContext *s, int *range, int *xmin, int *ymin, int *xmax, int *ymax, int f_code)
{
    *range = 8 * (1 << (f_code - 1));
    /* XXX: temporary kludge to avoid overflow for msmpeg4 */
    if (s->out_format == FMT_H263 && !s->h263_msmpeg4)
	*range *= 2;

    if (s->unrestricted_mv) {
        *xmin = -16;
        *ymin = -16;
        if (s->h263_plus)
            *range *= 2;
        if(s->avctx==NULL || s->avctx->codec->id!=CODEC_ID_MPEG4){
            *xmax = s->mb_width*16;
            *ymax = s->mb_height*16;
        }else {
            /* XXX: dunno if this is correct but ffmpeg4 decoder wont like it otherwise 
	            (cuz the drawn edge isnt large enough))*/
            *xmax = s->width;
            *ymax = s->height;
        }
    } else {
        *xmin = 0;
        *ymin = 0;
        *xmax = s->mb_width*16 - 16;
        *ymax = s->mb_height*16 - 16;
    }
}

static inline int mv4_search(MpegEncContext *s, int xmin, int ymin, int xmax, int ymax, int mx, int my, int shift)
{
    int block;
    int P[10][2];
    uint8_t *ref_picture= s->last_picture[0];
    int dmin_sum=0;

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
        P_LAST[0] = s->motion_val[mot_xy    ][0];
        P_LAST[1] = s->motion_val[mot_xy    ][1];
        P_LEFT[0] = s->motion_val[mot_xy - 1][0];
        P_LEFT[1] = s->motion_val[mot_xy - 1][1];
        P_LAST_RIGHT[0] = s->motion_val[mot_xy + 1][0];
        P_LAST_RIGHT[1] = s->motion_val[mot_xy + 1][1];
        P_LAST_BOTTOM[0]= s->motion_val[mot_xy + 1*mot_stride][0];
        P_LAST_BOTTOM[1]= s->motion_val[mot_xy + 1*mot_stride][1];

        if(P_LEFT[0]       > (rel_xmax4<<shift)) P_LEFT[0]       = (rel_xmax4<<shift);
        if(P_LAST_RIGHT[0] < (rel_xmin4<<shift)) P_LAST_RIGHT[0] = (rel_xmin4<<shift);
        if(P_LAST_BOTTOM[1]< (rel_ymin4<<shift)) P_LAST_BOTTOM[1]= (rel_ymin4<<shift);

        /* special case for first line */
        if ((s->mb_y == 0 || s->first_slice_line) && block<2) {
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

            if(s->out_format == FMT_H263){
                pred_x4 = P_MEDIAN[0];
                pred_y4 = P_MEDIAN[1];
            }else { /* mpeg1 at least */
                pred_x4= P_LEFT[0];
                pred_y4= P_LEFT[1];
            }
        }
        P_MV1[0]= mx;
        P_MV1[1]= my;

        dmin4 = epzs_motion_search4(s, block, &mx4, &my4, P, pred_x4, pred_y4, rel_xmin4, rel_ymin4, rel_xmax4, rel_ymax4, ref_picture);

        dmin4= fast_halfpel_motion_search(s, &mx4, &my4, dmin4, rel_xmin4, rel_ymin4, rel_xmax4, rel_ymax4, 
                                   pred_x4, pred_y4, ref_picture, pix_abs8x8_x2, 
                                   pix_abs8x8_y2, pix_abs8x8_xy2, block);
 
        s->motion_val[ s->block_index[block] ][0]= mx4;
        s->motion_val[ s->block_index[block] ][1]= my4;
        dmin_sum+= dmin4;
    }
    return dmin_sum;
}

void ff_estimate_p_frame_motion(MpegEncContext * s,
                                int mb_x, int mb_y)
{
    UINT8 *pix, *ppix;
    int sum, varc, vard, mx, my, range, dmin, xx, yy;
    int xmin, ymin, xmax, ymax;
    int rel_xmin, rel_ymin, rel_xmax, rel_ymax;
    int pred_x=0, pred_y=0;
    int P[10][2];
    const int shift= 1+s->quarter_sample;
    int mb_type=0;
    uint8_t *ref_picture= s->last_picture[0];

    get_limits(s, &range, &xmin, &ymin, &xmax, &ymax, s->f_code);
    rel_xmin= xmin - mb_x*16;
    rel_xmax= xmax - mb_x*16;
    rel_ymin= ymin - mb_y*16;
    rel_ymax= ymax - mb_y*16;
    s->skip_me=0;

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

            P_LAST[0]       = s->motion_val[mot_xy    ][0];
            P_LAST[1]       = s->motion_val[mot_xy    ][1];
            P_LEFT[0]       = s->motion_val[mot_xy - 1][0];
            P_LEFT[1]       = s->motion_val[mot_xy - 1][1];
            P_LAST_RIGHT[0] = s->motion_val[mot_xy + 2][0];
            P_LAST_RIGHT[1] = s->motion_val[mot_xy + 2][1];
            P_LAST_BOTTOM[0]= s->motion_val[mot_xy + 2*mot_stride][0];
            P_LAST_BOTTOM[1]= s->motion_val[mot_xy + 2*mot_stride][1];

            if(P_LEFT[0]       > (rel_xmax<<shift)) P_LEFT[0]       = (rel_xmax<<shift);
            if(P_LAST_RIGHT[0] < (rel_xmin<<shift)) P_LAST_RIGHT[0] = (rel_xmin<<shift);
            if(P_LAST_BOTTOM[1]< (rel_ymin<<shift)) P_LAST_BOTTOM[1]= (rel_ymin<<shift);

            /* special case for first line */
            if ((mb_y == 0 || s->first_slice_line)) {
                pred_x= P_LEFT[0];
                pred_y= P_LEFT[1];
            } else {
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
            }
        }
        dmin = epzs_motion_search(s, &mx, &my, P, pred_x, pred_y, rel_xmin, rel_ymin, rel_xmax, rel_ymax, ref_picture);
 
        break;
    }

    /* intra / predictive decision */
    xx = mb_x * 16;
    yy = mb_y * 16;

    pix = s->new_picture[0] + (yy * s->linesize) + xx;
    /* At this point (mx,my) are full-pell and the relative displacement */
    ppix = ref_picture + ((yy+my) * s->linesize) + (xx+mx);
    
    sum = pix_sum(pix, s->linesize);
    
    sum= (sum+8)>>4;
    varc = (pix_norm1(pix, s->linesize) - sum*sum + 500 + 128)>>8;
    vard = (pix_norm(pix, ppix, s->linesize)+128)>>8;
//printf("%d %d %d %X %X %X\n", s->mb_width, mb_x, mb_y,(int)s, (int)s->mb_var, (int)s->mc_mb_var); fflush(stdout);
    s->mb_var   [s->mb_width * mb_y + mb_x] = varc;
    s->mc_mb_var[s->mb_width * mb_y + mb_x] = vard;
    s->mb_var_sum    += varc;
    s->mc_mb_var_sum += vard;
//printf("E%d %d %d %X %X %X\n", s->mb_width, mb_x, mb_y,(int)s, (int)s->mb_var, (int)s->mc_mb_var); fflush(stdout);
    
#if 0
    printf("varc=%4d avg_var=%4d (sum=%4d) vard=%4d mx=%2d my=%2d\n",
	   varc, s->avg_mb_var, sum, vard, mx - xx, my - yy);
#endif
    if(s->flags&CODEC_FLAG_HQ){
        if (vard*2 + 200 > varc)
            mb_type|= MB_TYPE_INTRA;
        if (varc*2 + 200 > vard){
            mb_type|= MB_TYPE_INTER;
            if(s->me_method >= ME_EPZS)
                fast_halfpel_motion_search(s, &mx, &my, dmin, rel_xmin, rel_ymin, rel_xmax, rel_ymax, 
                                           pred_x, pred_y, ref_picture, pix_abs16x16_x2, pix_abs16x16_y2, 
                                           pix_abs16x16_xy2, 0);
            else
                halfpel_motion_search(     s, &mx, &my, dmin, rel_xmin, rel_ymin, rel_xmax, rel_ymax, 
                                           pred_x, pred_y, ref_picture, pix_abs16x16_x2, pix_abs16x16_y2, 
                                           pix_abs16x16_xy2, 0);                                           
        }else{
            mx <<=1;
            my <<=1;
        }
        if((s->flags&CODEC_FLAG_4MV)
           && !s->skip_me && varc>50 && vard>10){
            mv4_search(s, rel_xmin, rel_ymin, rel_xmax, rel_ymax, mx, my, shift);
            mb_type|=MB_TYPE_INTER4V;

            set_p_mv_tables(s, mx, my, 0);
        }else
            set_p_mv_tables(s, mx, my, 1);
    }else{
        if (vard <= 64 || vard < varc) {
            mb_type|= MB_TYPE_INTER;
            if (s->me_method != ME_ZERO) {
                if(s->me_method >= ME_EPZS)
                    dmin= fast_halfpel_motion_search(s, &mx, &my, dmin, rel_xmin, rel_ymin, rel_xmax, rel_ymax, 
                                           pred_x, pred_y, ref_picture, pix_abs16x16_x2, pix_abs16x16_y2, 
                                           pix_abs16x16_xy2, 0);
                else
                    dmin= halfpel_motion_search(s, &mx, &my, dmin, rel_xmin, rel_ymin, rel_xmax, rel_ymax, 
                                           pred_x, pred_y, ref_picture, pix_abs16x16_x2, pix_abs16x16_y2, 
                                           pix_abs16x16_xy2, 0);
                if((s->flags&CODEC_FLAG_4MV)
                   && !s->skip_me && varc>50 && vard>10){
                    int dmin4= mv4_search(s, rel_xmin, rel_ymin, rel_xmax, rel_ymax, mx, my, shift);
                    if(dmin4 + 128 <dmin)
                        mb_type= MB_TYPE_INTER4V;
                }
                set_p_mv_tables(s, mx, my, mb_type!=MB_TYPE_INTER4V);

            } else {
                mx <<=1;
                my <<=1;
            }
#if 0
            if (vard < 10) {
                skip++;
                fprintf(stderr,"\nEarly skip: %d vard: %2d varc: %5d dmin: %d", 
                                skip, vard, varc, dmin);
            }
#endif
        }else{
            mb_type|= MB_TYPE_INTRA;
            mx = 0;
            my = 0;
        }
    }

    s->mb_type[mb_y*s->mb_width + mb_x]= mb_type;
}

int ff_estimate_motion_b(MpegEncContext * s,
                       int mb_x, int mb_y, int16_t (*mv_table)[2], uint8_t *ref_picture, int f_code)
{
    int mx, my, range, dmin;
    int xmin, ymin, xmax, ymax;
    int rel_xmin, rel_ymin, rel_xmax, rel_ymax;
    int pred_x=0, pred_y=0;
    int P[10][2];
    const int shift= 1+s->quarter_sample;
    const int mot_stride = s->mb_width + 2;
    const int mot_xy = (mb_y + 1)*mot_stride + mb_x + 1;
    
    get_limits(s, &range, &xmin, &ymin, &xmax, &ymax, f_code);
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

            P_LAST[0]        = mv_table[mot_xy    ][0];
            P_LAST[1]        = mv_table[mot_xy    ][1];
            P_LEFT[0]        = mv_table[mot_xy - 1][0];
            P_LEFT[1]        = mv_table[mot_xy - 1][1];
            P_LAST_RIGHT[0]  = mv_table[mot_xy + 1][0];
            P_LAST_RIGHT[1]  = mv_table[mot_xy + 1][1];
            P_LAST_BOTTOM[0] = mv_table[mot_xy + mot_stride][0];
            P_LAST_BOTTOM[1] = mv_table[mot_xy + mot_stride][1];

            if(P_LEFT[0]       > (rel_xmax<<shift)) P_LEFT[0]       = (rel_xmax<<shift);
            if(P_LAST_RIGHT[0] < (rel_xmin<<shift)) P_LAST_RIGHT[0] = (rel_xmin<<shift);
            if(P_LAST_BOTTOM[1]< (rel_ymin<<shift)) P_LAST_BOTTOM[1]= (rel_ymin<<shift);

            /* special case for first line */
            if ((mb_y == 0 || s->first_slice_line)) {
            } else {
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
        dmin = epzs_motion_search(s, &mx, &my, P, pred_x, pred_y, rel_xmin, rel_ymin, rel_xmax, rel_ymax, ref_picture);
 
        break;
    }
    
    dmin= fast_halfpel_motion_search(s, &mx, &my, dmin, rel_xmin, rel_ymin, rel_xmax, rel_ymax, 
                                pred_x, pred_y, ref_picture, pix_abs16x16_x2, pix_abs16x16_y2, 
                                pix_abs16x16_xy2, 0);
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
    //FIXME direct mode penalty
    UINT16 *mv_penalty= s->mv_penalty[s->f_code] + MAX_MV; // f_code of the prev frame
    uint8_t *dest_y = s->me_scratchpad;
    uint8_t *ptr;
    int dxy;
    int src_x, src_y;
    int fbmin;

    fbmin = (mv_penalty[motion_fx-pred_fx] + mv_penalty[motion_fy-pred_fy])*s->qscale;

    dxy = ((motion_fy & 1) << 1) | (motion_fx & 1);
    src_x = mb_x * 16 + (motion_fx >> 1);
    src_y = mb_y * 16 + (motion_fy >> 1);
            
    ptr = s->last_picture[0] + (src_y * s->linesize) + src_x;
    put_pixels_tab[dxy](dest_y    , ptr    , s->linesize, 16);
    put_pixels_tab[dxy](dest_y + 8, ptr + 8, s->linesize, 16);
    
    fbmin += (mv_penalty[motion_bx-pred_bx] + mv_penalty[motion_by-pred_by])*s->qscale;

    dxy = ((motion_by & 1) << 1) | (motion_bx & 1);
    src_x = mb_x * 16 + (motion_bx >> 1);
    src_y = mb_y * 16 + (motion_by >> 1);
            
    ptr = s->next_picture[0] + (src_y * s->linesize) + src_x;
    avg_pixels_tab[dxy](dest_y    , ptr    , s->linesize, 16);
    avg_pixels_tab[dxy](dest_y + 8, ptr + 8, s->linesize, 16);
    
    fbmin += pix_abs16x16(s->new_picture[0] + mb_x*16 + mb_y*16*s->linesize, dest_y, s->linesize);
    return fbmin;
}

/* refine the bidir vectors in hq mode and return the score in both lq & hq mode*/
static inline int bidir_refine(MpegEncContext * s,
                                  int mb_x, int mb_y)
{
    const int mot_stride = s->mb_width + 2;
    const int xy = (mb_y + 1)*mot_stride + mb_x + 1;
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
    const int mot_stride = s->mb_width + 2;
    const int mot_xy = (mb_y + 1)*mot_stride + mb_x + 1;
    int dmin, dmin2;
    int motion_fx, motion_fy, motion_bx, motion_by, motion_bx0, motion_by0;
    int motion_dx, motion_dy;
    const int motion_px= s->p_mv_table[mot_xy][0];
    const int motion_py= s->p_mv_table[mot_xy][1];
    const int time_pp= s->pp_time;
    const int time_bp= s->bp_time;
    const int time_pb= time_pp - time_bp;
    int bx, by;
    int mx, my, mx2, my2;
    uint8_t *ref_picture= s->me_scratchpad - (mb_x + 1 + (mb_y + 1)*s->linesize)*16;
    int16_t (*mv_table)[2]= s->b_direct_mv_table;
    uint16_t *mv_penalty= s->mv_penalty[s->f_code] + MAX_MV; // f_code of the prev frame

    /* thanks to iso-mpeg the rounding is different for the zero vector, so we need to handle that ... */
    motion_fx= (motion_px*time_pb)/time_pp;
    motion_fy= (motion_py*time_pb)/time_pp;
    motion_bx0= (-motion_px*time_bp)/time_pp;
    motion_by0= (-motion_py*time_bp)/time_pp;
    motion_dx= motion_dy=0;
    dmin2= check_bidir_mv(s, mb_x, mb_y, 
                          motion_fx, motion_fy,
                          motion_bx0, motion_by0,
                          motion_fx, motion_fy,
                          motion_bx0, motion_by0) - s->qscale;

    motion_bx= motion_fx - motion_px;
    motion_by= motion_fy - motion_py;
    for(by=-1; by<2; by++){
        for(bx=-1; bx<2; bx++){
            uint8_t *dest_y = s->me_scratchpad + (by+1)*s->linesize*16 + (bx+1)*16;
            uint8_t *ptr;
            int dxy;
            int src_x, src_y;
            const int width= s->width;
            const int height= s->height;

            dxy = ((motion_fy & 1) << 1) | (motion_fx & 1);
            src_x = (mb_x + bx) * 16 + (motion_fx >> 1);
            src_y = (mb_y + by) * 16 + (motion_fy >> 1);
            src_x = clip(src_x, -16, width);
            if (src_x == width) dxy &= ~1;
            src_y = clip(src_y, -16, height);
            if (src_y == height) dxy &= ~2;

            ptr = s->last_picture[0] + (src_y * s->linesize) + src_x;
            put_pixels_tab[dxy](dest_y    , ptr    , s->linesize, 16);
            put_pixels_tab[dxy](dest_y + 8, ptr + 8, s->linesize, 16);

            dxy = ((motion_by & 1) << 1) | (motion_bx & 1);
            src_x = (mb_x + bx) * 16 + (motion_bx >> 1);
            src_y = (mb_y + by) * 16 + (motion_by >> 1);
            src_x = clip(src_x, -16, width);
            if (src_x == width) dxy &= ~1;
            src_y = clip(src_y, -16, height);
            if (src_y == height) dxy &= ~2;

            avg_pixels_tab[dxy](dest_y    , ptr    , s->linesize, 16);
            avg_pixels_tab[dxy](dest_y + 8, ptr + 8, s->linesize, 16);
        }
    }

    P_LAST[0]        = mv_table[mot_xy    ][0];
    P_LAST[1]        = mv_table[mot_xy    ][1];
    P_LEFT[0]        = mv_table[mot_xy - 1][0];
    P_LEFT[1]        = mv_table[mot_xy - 1][1];
    P_LAST_RIGHT[0]  = mv_table[mot_xy + 1][0];
    P_LAST_RIGHT[1]  = mv_table[mot_xy + 1][1];
    P_LAST_BOTTOM[0] = mv_table[mot_xy + mot_stride][0];
    P_LAST_BOTTOM[1] = mv_table[mot_xy + mot_stride][1];
/*
    if(P_LEFT[0]       > (rel_xmax<<shift)) P_LEFT[0]       = (rel_xmax<<shift);
    if(P_LAST_RIGHT[0] < (rel_xmin<<shift)) P_LAST_RIGHT[0] = (rel_xmin<<shift);
    if(P_LAST_BOTTOM[1]< (rel_ymin<<shift)) P_LAST_BOTTOM[1]= (rel_ymin<<shift);
*/
    /* special case for first line */
    if ((mb_y == 0 || s->first_slice_line)) {
    } else {
        P_TOP[0] = mv_table[mot_xy - mot_stride             ][0];
        P_TOP[1] = mv_table[mot_xy - mot_stride             ][1];
        P_TOPRIGHT[0] = mv_table[mot_xy - mot_stride + 1         ][0];
        P_TOPRIGHT[1] = mv_table[mot_xy - mot_stride + 1         ][1];
    
        P_MEDIAN[0]= mid_pred(P_LEFT[0], P_TOP[0], P_TOPRIGHT[0]);
        P_MEDIAN[1]= mid_pred(P_LEFT[1], P_TOP[1], P_TOPRIGHT[1]);
    }
    dmin = epzs_motion_search(s, &mx, &my, P, 0, 0, -16, -16, 15, 15, ref_picture);
    if(mx==0 && my==0) dmin=99999999; // not representable, due to rounding stuff
    if(dmin2<dmin){ 
        dmin= dmin2;
        mx=0;
        my=0;
    }
#if 1
    mx2= mx= mx*2; 
    my2= my= my*2;
    for(by=-1; by<2; by++){
        if(my2+by < -32) continue;
        for(bx=-1; bx<2; bx++){
            if(bx==0 && by==0) continue;
            if(mx2+bx < -32) continue;
            dmin2= check_bidir_mv(s, mb_x, mb_y, 
                          mx2+bx+motion_fx, my2+by+motion_fy,
                          mx2+bx+motion_bx, my2+by+motion_by,
                          mx2+bx+motion_fx, my2+by+motion_fy,
                          motion_bx, motion_by) - s->qscale;
            
            if(dmin2<dmin){
                dmin=dmin2;
                mx= mx2 + bx;
                my= my2 + by;
            }
        }
    }
#else
    mx*=2; my*=2;
#endif
    if(mx==0 && my==0){
        motion_bx= motion_bx0;
        motion_by= motion_by0;
    }

    s->b_direct_mv_table[mot_xy][0]= mx;
    s->b_direct_mv_table[mot_xy][1]= my;
    s->b_direct_forw_mv_table[mot_xy][0]= motion_fx + mx;
    s->b_direct_forw_mv_table[mot_xy][1]= motion_fy + my;
    s->b_direct_back_mv_table[mot_xy][0]= motion_bx + mx;
    s->b_direct_back_mv_table[mot_xy][1]= motion_by + my;
    return dmin;
}

void ff_estimate_b_frame_motion(MpegEncContext * s,
                             int mb_x, int mb_y)
{
    const int quant= s->qscale;
    int fmin, bmin, dmin, fbmin;
    int type=0;
    
    dmin= direct_search(s, mb_x, mb_y);

    fmin= ff_estimate_motion_b(s, mb_x, mb_y, s->b_forw_mv_table, s->last_picture[0], s->f_code);
    bmin= ff_estimate_motion_b(s, mb_x, mb_y, s->b_back_mv_table, s->next_picture[0], s->b_code) - quant;
//printf(" %d %d ", s->b_forw_mv_table[xy][0], s->b_forw_mv_table[xy][1]);

    fbmin= bidir_refine(s, mb_x, mb_y);

    if(s->flags&CODEC_FLAG_HQ){
        type= MB_TYPE_FORWARD | MB_TYPE_BACKWARD | MB_TYPE_BIDIR | MB_TYPE_DIRECT;
    }else{
        int score= dmin;
        type=MB_TYPE_DIRECT;
        
        if(fmin<score){
            score=fmin;
            type= MB_TYPE_FORWARD; 
        }
        if(bmin<score){
            score=bmin;
            type= MB_TYPE_BACKWARD; 
        }
        if(fbmin<score){
            score=fbmin;
            type= MB_TYPE_BIDIR;
        }
        s->mc_mb_var_sum += score;
        s->mc_mb_var[mb_y*s->mb_width + mb_x] = score;
    }
/*
{
static int count=0;
static int sum=0;
if(type==MB_TYPE_DIRECT){
  int diff= ABS(s->b_forw_mv_table)
}
}*/

    s->mb_type[mb_y*s->mb_width + mb_x]= type;
/*    if(mb_y==0 && mb_x==0) printf("\n");
    if(mb_x==0) printf("\n");
    printf("%d", av_log2(type));
*/
}

/* find best f_code for ME which do unlimited searches */
int ff_get_best_fcode(MpegEncContext * s, int16_t (*mv_table)[2], int type)
{
    if(s->me_method>=ME_EPZS){
        int score[8];
        int i, y;
        UINT8 * fcode_tab= s->fcode_tab;
        int best_fcode=-1;
        int best_score=-10000000;

        for(i=0; i<8; i++) score[i]= s->mb_num*(8-i); //FIXME *2 and all other too so its the same but nicer

        for(y=0; y<s->mb_height; y++){
            int x;
            int xy= (y+1)* (s->mb_width+2) + 1;
            i= y*s->mb_width;
            for(x=0; x<s->mb_width; x++){
                if(s->mb_type[i] & type){
                    int fcode= MAX(fcode_tab[mv_table[xy][0] + MAX_MV],
                                   fcode_tab[mv_table[xy][1] + MAX_MV]);
                    int j;
                    
                    for(j=0; j<fcode && j<8; j++){
                        if(s->pict_type==B_TYPE || s->mc_mb_var[i] < s->mb_var[i])
                            score[j]-= 170;
                    }
                }
                i++;
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
    int y;
    UINT8 * fcode_tab= s->fcode_tab;
//int clip=0;
//int noclip=0;
    /* clip / convert to intra 16x16 type MVs */
    for(y=0; y<s->mb_height; y++){
        int x;
        int xy= (y+1)* (s->mb_width+2)+1;
        int i= y*s->mb_width;
        for(x=0; x<s->mb_width; x++){
            if(s->mb_type[i]&MB_TYPE_INTER){
                if(   fcode_tab[s->p_mv_table[xy][0] + MAX_MV] > f_code
                   || fcode_tab[s->p_mv_table[xy][0] + MAX_MV] == 0
                   || fcode_tab[s->p_mv_table[xy][1] + MAX_MV] > f_code
                   || fcode_tab[s->p_mv_table[xy][1] + MAX_MV] == 0 ){
                    s->mb_type[i] &= ~MB_TYPE_INTER;
                    s->mb_type[i] |= MB_TYPE_INTRA;
                    s->p_mv_table[xy][0] = 0;
                    s->p_mv_table[xy][1] = 0;
//clip++;
                }
//else
//  noclip++;
            }
            xy++;
            i++;
        }
    }
//printf("%d no:%d %d//\n", clip, noclip, f_code);
    if(s->flags&CODEC_FLAG_4MV){
        const int wrap= 2+ s->mb_width*2;

        /* clip / convert to intra 8x8 type MVs */
        for(y=0; y<s->mb_height; y++){
            int xy= (y*2 + 1)*wrap + 1;
            int i= y*s->mb_width;
            int x;

            for(x=0; x<s->mb_width; x++){
                if(s->mb_type[i]&MB_TYPE_INTER4V){
                    int block;
                    for(block=0; block<4; block++){
                        int off= (block& 1) + (block>>1)*wrap;
                        int mx= s->motion_val[ xy + off ][0];
                        int my= s->motion_val[ xy + off ][1];

                        if(   fcode_tab[mx + MAX_MV] > f_code
                           || fcode_tab[mx + MAX_MV] == 0
                           || fcode_tab[my + MAX_MV] > f_code
                           || fcode_tab[my + MAX_MV] == 0 ){
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
    UINT8 * fcode_tab= s->fcode_tab;

    /* clip / convert to intra 16x16 type MVs */
    for(y=0; y<s->mb_height; y++){
        int x;
        int xy= (y+1)* (s->mb_width+2)+1;
        int i= y*s->mb_width;
        for(x=0; x<s->mb_width; x++){
            if(s->mb_type[i]&type){
                if(   fcode_tab[mv_table[xy][0] + MAX_MV] > f_code
                   || fcode_tab[mv_table[xy][0] + MAX_MV] == 0
                   || fcode_tab[mv_table[xy][1] + MAX_MV] > f_code
                   || fcode_tab[mv_table[xy][1] + MAX_MV] == 0 ){
                    if(s->mb_type[i]&(~type)) s->mb_type[i] &= ~type;
                    else{
                        mv_table[xy][0] = 0;
                        mv_table[xy][1] = 0;
                        //this is certainly bad FIXME            
                    }
                }
            }
            xy++;
            i++;
        }
    }
}
