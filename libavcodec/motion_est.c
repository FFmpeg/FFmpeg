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
 */
#include <stdlib.h>
#include <stdio.h>
#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"

static void halfpel_motion_search(MpegEncContext * s,
				  int *mx_ptr, int *my_ptr, int dmin,
				  int xmin, int ymin, int xmax, int ymax);

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

/* The idea would be to make half pel ME after Inter/Intra decision to 
   save time. */
static void halfpel_motion_search(MpegEncContext * s,
				  int *mx_ptr, int *my_ptr, int dmin,
				  int xmin, int ymin, int xmax, int ymax)
{
    int mx, my, mx1, my1, d, xx, yy, dminh;
    UINT8 *pix;

    mx = *mx_ptr << 1;
    my = *my_ptr << 1;

    xx = 16 * s->mb_x;
    yy = 16 * s->mb_y;

    dminh = dmin;

    /* Half pixel search */
    mx1 = mx;
    my1 = my;

    pix = s->new_picture[0] + (yy * s->linesize) + xx;

    if ((mx > (xmin << 1)) && mx < (xmax << 1) && 
        (my > (ymin << 1)) && my < (ymax << 1)) {
	    int dx, dy, px, py;
	    UINT8 *ptr;
        for (dy = -1; dy <= 1; dy++) {
            for (dx = -1; dx <= 1; dx++) {
                if (dx != 0 || dy != 0) {
                    px = mx1 + dx;
                    py = my1 + dy;
                    ptr = s->last_picture[0] + ((py >> 1) * s->linesize) + (px >> 1);
                    switch (((py & 1) << 1) | (px & 1)) {
                    default:
                    case 0:
                        d = pix_abs16x16(pix, ptr, s->linesize, 16);
                        break;
                    case 1:
                        d = pix_abs16x16_x2(pix, ptr, s->linesize, 16);
                        break;
                    case 2:
                        d = pix_abs16x16_y2(pix, ptr, s->linesize, 16);
                        break;
                    case 3:
                        d = pix_abs16x16_xy2(pix, ptr, s->linesize, 16);
                        break;
                    }
                    if (d < dminh) {
                        dminh = d;
                        mx = px;
                        my = py;
                    }
                }
            }
        }
    }

    *mx_ptr = mx - (xx << 1);
    *my_ptr = my - (yy << 1);
    //fprintf(stderr,"half  - MX: %d\tMY: %d\n",*mx_ptr ,*my_ptr);
}

#ifndef CONFIG_TEST_MV_ENCODE

int estimate_motion(MpegEncContext * s,
		    int mb_x, int mb_y,
		    int *mx_ptr, int *my_ptr)
{
    UINT8 *pix, *ppix;
    int sum, varc, vard, mx, my, range, dmin, xx, yy;
    int xmin, ymin, xmax, ymax;

    range = 8 * (1 << (s->f_code - 1));
    /* XXX: temporary kludge to avoid overflow for msmpeg4 */
    if (s->out_format == FMT_H263 && !s->h263_msmpeg4)
	range = range * 2;

    if (s->unrestricted_mv) {
        xmin = -16;
        ymin = -16;
        xmax = s->width;
        ymax = s->height;
    } else {
        xmin = 0;
        ymin = 0;
        xmax = s->width - 16;
        ymax = s->height - 16;
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
    }
    emms_c();

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
#if 0
    printf("varc=%d (sum=%d) vard=%d mx=%d my=%d\n",
	   varc, sum, vard, mx - xx, my - yy);
#endif
    if (vard <= 64 || vard < varc) {
        if (s->full_search != ME_ZERO) {
            halfpel_motion_search(s, &mx, &my, dmin, xmin, ymin, xmax, ymax);
        } else {
            mx -= 16 * s->mb_x;
            my -= 16 * s->mb_y;
        }
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
