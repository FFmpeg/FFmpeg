/*
 * Copyright (C) 2004 Michael Niedermayer <michaelni@gmx.at>
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
 */

#include "avcodec.h"
#include "common.h"
#include "dsputil.h"
#include "cabac.h"

#include "mpegvideo.h"

#undef NDEBUG
#include <assert.h>

#define MAX_DECOMPOSITIONS 8
#define MAX_PLANES 4
#define DWTELEM int
#define QROOT 8 
#define LOSSLESS_QLOG -128

static const int8_t quant3[256]={
 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 0,
};
static const int8_t quant3b[256]={
 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
};
static const int8_t quant5[256]={
 0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-1,-1,-1,
};
static const int8_t quant7[256]={
 0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,
-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,
-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,
-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,
-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,
-3,-3,-3,-3,-3,-3,-3,-3,-3,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,
-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-1,-1,
};
static const int8_t quant9[256]={
 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3,
 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,
-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,
-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,
-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,
-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,
-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,
-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-3,-3,-3,-3,
-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-2,-2,-2,-2,-1,-1,
};
static const int8_t quant11[256]={
 0, 1, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4,
 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,
-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,
-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,
-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,
-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,
-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-4,-4,
-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,-4,
-4,-4,-4,-4,-4,-3,-3,-3,-3,-3,-3,-3,-2,-2,-2,-1,
};
static const int8_t quant13[256]={
 0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,
-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,
-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,
-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,
-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-6,-5,
-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,
-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,
-4,-4,-4,-4,-4,-4,-4,-4,-4,-3,-3,-3,-3,-2,-2,-1,
};

#define OBMC_MAX 64
#if 0 //64*cubic
static const uint8_t obmc32[1024]={
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0,
 0, 0, 1, 1, 2, 2, 3, 4, 4, 5, 6, 6, 7, 7, 8, 8, 8, 8, 7, 7, 6, 6, 5, 4, 4, 3, 2, 2, 1, 1, 0, 0,
 0, 0, 1, 2, 2, 3, 4, 6, 7, 8, 9,10,11,12,12,12,12,12,12,11,10, 9, 8, 7, 6, 4, 3, 2, 2, 1, 0, 0,
 0, 1, 1, 2, 3, 5, 6, 8,10,11,13,14,15,16,17,18,18,17,16,15,14,13,11,10, 8, 6, 5, 3, 2, 1, 1, 0,
 0, 1, 1, 3, 4, 6, 8,10,13,15,17,19,20,22,22,23,23,22,22,20,19,17,15,13,10, 8, 6, 4, 3, 1, 1, 0,
 0, 1, 2, 4, 6, 8,10,13,16,19,21,23,25,27,28,29,29,28,27,25,23,21,19,16,13,10, 8, 6, 4, 2, 1, 0,
 0, 1, 2, 4, 7,10,13,16,19,22,25,28,31,33,34,35,35,34,33,31,28,25,22,19,16,13,10, 7, 4, 2, 1, 0,
 0, 1, 3, 5, 8,11,15,19,22,26,30,33,36,38,40,41,41,40,38,36,33,30,26,22,19,15,11, 8, 5, 3, 1, 0,
 0, 1, 3, 6, 9,12,17,21,25,30,34,38,41,44,45,46,46,45,44,41,38,34,30,25,21,17,12, 9, 6, 3, 1, 0,
 0, 1, 3, 6,10,14,19,23,28,33,38,42,45,48,51,52,52,51,48,45,42,38,33,28,23,19,14,10, 6, 3, 1, 0,
 0, 1, 4, 7,11,15,20,25,31,36,41,45,49,52,55,56,56,55,52,49,45,41,36,31,25,20,15,11, 7, 4, 1, 0,
 0, 2, 4, 7,12,16,22,27,33,38,44,48,52,56,58,60,60,58,56,52,48,44,38,33,27,22,16,12, 7, 4, 2, 0,
 0, 1, 4, 8,12,17,22,28,34,40,45,51,55,58,61,62,62,61,58,55,51,45,40,34,28,22,17,12, 8, 4, 1, 0,
 0, 2, 4, 8,12,18,23,29,35,41,46,52,56,60,62,64,64,62,60,56,52,46,41,35,29,23,18,12, 8, 4, 2, 0,
 0, 2, 4, 8,12,18,23,29,35,41,46,52,56,60,62,64,64,62,60,56,52,46,41,35,29,23,18,12, 8, 4, 2, 0,
 0, 1, 4, 8,12,17,22,28,34,40,45,51,55,58,61,62,62,61,58,55,51,45,40,34,28,22,17,12, 8, 4, 1, 0,
 0, 2, 4, 7,12,16,22,27,33,38,44,48,52,56,58,60,60,58,56,52,48,44,38,33,27,22,16,12, 7, 4, 2, 0,
 0, 1, 4, 7,11,15,20,25,31,36,41,45,49,52,55,56,56,55,52,49,45,41,36,31,25,20,15,11, 7, 4, 1, 0,
 0, 1, 3, 6,10,14,19,23,28,33,38,42,45,48,51,52,52,51,48,45,42,38,33,28,23,19,14,10, 6, 3, 1, 0,
 0, 1, 3, 6, 9,12,17,21,25,30,34,38,41,44,45,46,46,45,44,41,38,34,30,25,21,17,12, 9, 6, 3, 1, 0,
 0, 1, 3, 5, 8,11,15,19,22,26,30,33,36,38,40,41,41,40,38,36,33,30,26,22,19,15,11, 8, 5, 3, 1, 0,
 0, 1, 2, 4, 7,10,13,16,19,22,25,28,31,33,34,35,35,34,33,31,28,25,22,19,16,13,10, 7, 4, 2, 1, 0,
 0, 1, 2, 4, 6, 8,10,13,16,19,21,23,25,27,28,29,29,28,27,25,23,21,19,16,13,10, 8, 6, 4, 2, 1, 0,
 0, 1, 1, 3, 4, 6, 8,10,13,15,17,19,20,22,22,23,23,22,22,20,19,17,15,13,10, 8, 6, 4, 3, 1, 1, 0,
 0, 1, 1, 2, 3, 5, 6, 8,10,11,13,14,15,16,17,18,18,17,16,15,14,13,11,10, 8, 6, 5, 3, 2, 1, 1, 0,
 0, 0, 1, 2, 2, 3, 4, 6, 7, 8, 9,10,11,12,12,12,12,12,12,11,10, 9, 8, 7, 6, 4, 3, 2, 2, 1, 0, 0,
 0, 0, 1, 1, 2, 2, 3, 4, 4, 5, 6, 6, 7, 7, 8, 8, 8, 8, 7, 7, 6, 6, 5, 4, 4, 3, 2, 2, 1, 1, 0, 0,
 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0,
 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
//error:0.000022
};
static const uint8_t obmc16[256]={
 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
 0, 1, 1, 2, 4, 5, 5, 6, 6, 5, 5, 4, 2, 1, 1, 0,
 0, 1, 4, 6, 9,11,13,15,15,13,11, 9, 6, 4, 1, 0,
 0, 2, 6,11,15,20,24,26,26,24,20,15,11, 6, 2, 0,
 0, 4, 9,15,23,29,34,38,38,34,29,23,15, 9, 4, 0,
 0, 5,11,20,29,38,45,49,49,45,38,29,20,11, 5, 0,
 1, 5,13,24,34,45,53,57,57,53,45,34,24,13, 5, 1,
 1, 6,15,26,38,49,57,62,62,57,49,38,26,15, 6, 1,
 1, 6,15,26,38,49,57,62,62,57,49,38,26,15, 6, 1,
 1, 5,13,24,34,45,53,57,57,53,45,34,24,13, 5, 1,
 0, 5,11,20,29,38,45,49,49,45,38,29,20,11, 5, 0,
 0, 4, 9,15,23,29,34,38,38,34,29,23,15, 9, 4, 0,
 0, 2, 6,11,15,20,24,26,26,24,20,15,11, 6, 2, 0,
 0, 1, 4, 6, 9,11,13,15,15,13,11, 9, 6, 4, 1, 0,
 0, 1, 1, 2, 4, 5, 5, 6, 6, 5, 5, 4, 2, 1, 1, 0,
 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
//error:0.000033
};
#elif 1 // 64*linear
static const uint8_t obmc32[1024]={
 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
 0, 1, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6, 5, 5, 5, 4, 4, 4, 3, 3, 2, 2, 2, 1, 1, 1, 0,
 0, 1, 2, 2, 3, 3, 4, 5, 5, 6, 7, 7, 8, 8, 9,10,10, 9, 8, 8, 7, 7, 6, 5, 5, 4, 3, 3, 2, 2, 1, 0,
 0, 1, 2, 3, 4, 5, 6, 7, 7, 8, 9,10,11,12,13,14,14,13,12,11,10, 9, 8, 7, 7, 6, 5, 4, 3, 2, 1, 0,
 1, 2, 3, 4, 5, 6, 7, 8,10,11,12,13,14,15,16,17,17,16,15,14,13,12,11,10, 8, 7, 6, 5, 4, 3, 2, 1,
 1, 2, 3, 5, 6, 8, 9,10,12,13,14,16,17,19,20,21,21,20,19,17,16,14,13,12,10, 9, 8, 6, 5, 3, 2, 1,
 1, 2, 4, 6, 7, 9,11,12,14,15,17,19,20,22,24,25,25,24,22,20,19,17,15,14,12,11, 9, 7, 6, 4, 2, 1,
 1, 3, 5, 7, 8,10,12,14,16,18,20,22,23,25,27,29,29,27,25,23,22,20,18,16,14,12,10, 8, 7, 5, 3, 1,
 1, 3, 5, 7,10,12,14,16,18,20,22,24,27,29,31,33,33,31,29,27,24,22,20,18,16,14,12,10, 7, 5, 3, 1,
 1, 4, 6, 8,11,13,15,18,20,23,25,27,30,32,34,37,37,34,32,30,27,25,23,20,18,15,13,11, 8, 6, 4, 1,
 1, 4, 7, 9,12,14,17,20,22,25,28,30,33,35,38,41,41,38,35,33,30,28,25,22,20,17,14,12, 9, 7, 4, 1,
 1, 4, 7,10,13,16,19,22,24,27,30,33,36,39,42,45,45,42,39,36,33,30,27,24,22,19,16,13,10, 7, 4, 1,
 2, 5, 8,11,14,17,20,23,27,30,33,36,39,42,45,48,48,45,42,39,36,33,30,27,23,20,17,14,11, 8, 5, 2,
 2, 5, 8,12,15,19,22,25,29,32,35,39,42,46,49,52,52,49,46,42,39,35,32,29,25,22,19,15,12, 8, 5, 2,
 2, 5, 9,13,16,20,24,27,31,34,38,42,45,49,53,56,56,53,49,45,42,38,34,31,27,24,20,16,13, 9, 5, 2,
 2, 6,10,14,17,21,25,29,33,37,41,45,48,52,56,60,60,56,52,48,45,41,37,33,29,25,21,17,14,10, 6, 2,
 2, 6,10,14,17,21,25,29,33,37,41,45,48,52,56,60,60,56,52,48,45,41,37,33,29,25,21,17,14,10, 6, 2,
 2, 5, 9,13,16,20,24,27,31,34,38,42,45,49,53,56,56,53,49,45,42,38,34,31,27,24,20,16,13, 9, 5, 2,
 2, 5, 8,12,15,19,22,25,29,32,35,39,42,46,49,52,52,49,46,42,39,35,32,29,25,22,19,15,12, 8, 5, 2,
 2, 5, 8,11,14,17,20,23,27,30,33,36,39,42,45,48,48,45,42,39,36,33,30,27,23,20,17,14,11, 8, 5, 2,
 1, 4, 7,10,13,16,19,22,24,27,30,33,36,39,42,45,45,42,39,36,33,30,27,24,22,19,16,13,10, 7, 4, 1,
 1, 4, 7, 9,12,14,17,20,22,25,28,30,33,35,38,41,41,38,35,33,30,28,25,22,20,17,14,12, 9, 7, 4, 1,
 1, 4, 6, 8,11,13,15,18,20,23,25,27,30,32,34,37,37,34,32,30,27,25,23,20,18,15,13,11, 8, 6, 4, 1,
 1, 3, 5, 7,10,12,14,16,18,20,22,24,27,29,31,33,33,31,29,27,24,22,20,18,16,14,12,10, 7, 5, 3, 1,
 1, 3, 5, 7, 8,10,12,14,16,18,20,22,23,25,27,29,29,27,25,23,22,20,18,16,14,12,10, 8, 7, 5, 3, 1,
 1, 2, 4, 6, 7, 9,11,12,14,15,17,19,20,22,24,25,25,24,22,20,19,17,15,14,12,11, 9, 7, 6, 4, 2, 1,
 1, 2, 3, 5, 6, 8, 9,10,12,13,14,16,17,19,20,21,21,20,19,17,16,14,13,12,10, 9, 8, 6, 5, 3, 2, 1,
 1, 2, 3, 4, 5, 6, 7, 8,10,11,12,13,14,15,16,17,17,16,15,14,13,12,11,10, 8, 7, 6, 5, 4, 3, 2, 1,
 0, 1, 2, 3, 4, 5, 6, 7, 7, 8, 9,10,11,12,13,14,14,13,12,11,10, 9, 8, 7, 7, 6, 5, 4, 3, 2, 1, 0,
 0, 1, 2, 2, 3, 3, 4, 5, 5, 6, 7, 7, 8, 8, 9,10,10, 9, 8, 8, 7, 7, 6, 5, 5, 4, 3, 3, 2, 2, 1, 0,
 0, 1, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6, 5, 5, 5, 4, 4, 4, 3, 3, 2, 2, 2, 1, 1, 1, 0,
 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
 //error:0.000020
};
static const uint8_t obmc16[256]={
 0, 1, 1, 2, 2, 3, 3, 4, 4, 3, 3, 2, 2, 1, 1, 0,
 1, 2, 4, 5, 7, 8,10,11,11,10, 8, 7, 5, 4, 2, 1,
 1, 4, 6, 9,11,14,16,19,19,16,14,11, 9, 6, 4, 1,
 2, 5, 9,12,16,19,23,26,26,23,19,16,12, 9, 5, 2,
 2, 7,11,16,20,25,29,34,34,29,25,20,16,11, 7, 2,
 3, 8,14,19,25,30,36,41,41,36,30,25,19,14, 8, 3,
 3,10,16,23,29,36,42,49,49,42,36,29,23,16,10, 3,
 4,11,19,26,34,41,49,56,56,49,41,34,26,19,11, 4,
 4,11,19,26,34,41,49,56,56,49,41,34,26,19,11, 4,
 3,10,16,23,29,36,42,49,49,42,36,29,23,16,10, 3,
 3, 8,14,19,25,30,36,41,41,36,30,25,19,14, 8, 3,
 2, 7,11,16,20,25,29,34,34,29,25,20,16,11, 7, 2,
 2, 5, 9,12,16,19,23,26,26,23,19,16,12, 9, 5, 2,
 1, 4, 6, 9,11,14,16,19,19,16,14,11, 9, 6, 4, 1,
 1, 2, 4, 5, 7, 8,10,11,11,10, 8, 7, 5, 4, 2, 1,
 0, 1, 1, 2, 2, 3, 3, 4, 4, 3, 3, 2, 2, 1, 1, 0,
//error:0.000015
};
#else //64*cos
static const uint8_t obmc32[1024]={
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0,
 0, 0, 1, 1, 1, 2, 2, 3, 4, 5, 5, 6, 7, 7, 7, 7, 7, 7, 7, 7, 6, 5, 5, 4, 3, 2, 2, 1, 1, 1, 0, 0,
 0, 0, 1, 1, 2, 3, 4, 5, 6, 7, 9,10,11,11,12,12,12,12,11,11,10, 9, 7, 6, 5, 4, 3, 2, 1, 1, 0, 0,
 0, 0, 1, 2, 3, 5, 6, 8, 9,11,12,14,15,16,17,17,17,17,16,15,14,12,11, 9, 8, 6, 5, 3, 2, 1, 0, 0,
 0, 1, 1, 2, 4, 6, 8,10,12,15,17,19,20,21,22,23,23,22,21,20,19,17,15,12,10, 8, 6, 4, 2, 1, 1, 0,
 0, 1, 2, 3, 5, 8,10,13,16,19,21,24,26,27,28,29,29,28,27,26,24,21,19,16,13,10, 8, 5, 3, 2, 1, 0,
 0, 1, 2, 4, 6, 9,12,16,19,23,26,29,31,33,34,35,35,34,33,31,29,26,23,19,16,12, 9, 6, 4, 2, 1, 0,
 0, 1, 3, 5, 7,11,15,19,23,26,30,34,37,39,40,41,41,40,39,37,34,30,26,23,19,15,11, 7, 5, 3, 1, 0,
 0, 1, 3, 5, 9,12,17,21,26,30,35,38,42,44,46,47,47,46,44,42,38,35,30,26,21,17,12, 9, 5, 3, 1, 0,
 0, 1, 3, 6, 9,14,19,24,29,34,38,43,46,49,51,52,52,51,49,46,43,38,34,29,24,19,14, 9, 6, 3, 1, 0,
 0, 1, 3, 6,11,15,20,26,31,37,42,46,50,53,56,57,57,56,53,50,46,42,37,31,26,20,15,11, 6, 3, 1, 0,
 0, 1, 3, 7,11,16,21,27,33,39,44,49,53,57,59,60,60,59,57,53,49,44,39,33,27,21,16,11, 7, 3, 1, 0,
 0, 1, 4, 7,12,17,22,28,34,40,46,51,56,59,61,63,63,61,59,56,51,46,40,34,28,22,17,12, 7, 4, 1, 0,
 0, 1, 4, 7,12,17,23,29,35,41,47,52,57,60,63,64,64,63,60,57,52,47,41,35,29,23,17,12, 7, 4, 1, 0,
 0, 1, 4, 7,12,17,23,29,35,41,47,52,57,60,63,64,64,63,60,57,52,47,41,35,29,23,17,12, 7, 4, 1, 0,
 0, 1, 4, 7,12,17,22,28,34,40,46,51,56,59,61,63,63,61,59,56,51,46,40,34,28,22,17,12, 7, 4, 1, 0,
 0, 1, 3, 7,11,16,21,27,33,39,44,49,53,57,59,60,60,59,57,53,49,44,39,33,27,21,16,11, 7, 3, 1, 0,
 0, 1, 3, 6,11,15,20,26,31,37,42,46,50,53,56,57,57,56,53,50,46,42,37,31,26,20,15,11, 6, 3, 1, 0,
 0, 1, 3, 6, 9,14,19,24,29,34,38,43,46,49,51,52,52,51,49,46,43,38,34,29,24,19,14, 9, 6, 3, 1, 0,
 0, 1, 3, 5, 9,12,17,21,26,30,35,38,42,44,46,47,47,46,44,42,38,35,30,26,21,17,12, 9, 5, 3, 1, 0,
 0, 1, 3, 5, 7,11,15,19,23,26,30,34,37,39,40,41,41,40,39,37,34,30,26,23,19,15,11, 7, 5, 3, 1, 0,
 0, 1, 2, 4, 6, 9,12,16,19,23,26,29,31,33,34,35,35,34,33,31,29,26,23,19,16,12, 9, 6, 4, 2, 1, 0,
 0, 1, 2, 3, 5, 8,10,13,16,19,21,24,26,27,28,29,29,28,27,26,24,21,19,16,13,10, 8, 5, 3, 2, 1, 0,
 0, 1, 1, 2, 4, 6, 8,10,12,15,17,19,20,21,22,23,23,22,21,20,19,17,15,12,10, 8, 6, 4, 2, 1, 1, 0,
 0, 0, 1, 2, 3, 5, 6, 8, 9,11,12,14,15,16,17,17,17,17,16,15,14,12,11, 9, 8, 6, 5, 3, 2, 1, 0, 0,
 0, 0, 1, 1, 2, 3, 4, 5, 6, 7, 9,10,11,11,12,12,12,12,11,11,10, 9, 7, 6, 5, 4, 3, 2, 1, 1, 0, 0,
 0, 0, 1, 1, 1, 2, 2, 3, 4, 5, 5, 6, 7, 7, 7, 7, 7, 7, 7, 7, 6, 5, 5, 4, 3, 2, 2, 1, 1, 1, 0, 0,
 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
//error:0.000022
};
static const uint8_t obmc16[256]={
 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
 0, 0, 1, 2, 3, 4, 5, 5, 5, 5, 4, 3, 2, 1, 0, 0,
 0, 1, 3, 6, 8,11,13,14,14,13,11, 8, 6, 3, 1, 0,
 0, 2, 6,10,15,20,24,26,26,24,20,15,10, 6, 2, 0,
 0, 3, 8,16,23,30,35,38,38,35,30,23,16, 8, 3, 0,
 1, 4,11,20,30,39,46,49,49,46,39,30,20,11, 4, 1,
 1, 5,13,24,35,46,54,58,58,54,46,35,24,13, 5, 1,
 0, 5,14,26,38,49,58,63,63,58,49,38,26,14, 5, 0,
 0, 5,14,26,38,49,58,63,63,58,49,38,26,14, 5, 0,
 1, 5,13,24,35,46,54,58,58,54,46,35,24,13, 5, 1,
 1, 4,11,20,30,39,46,49,49,46,39,30,20,11, 4, 1,
 0, 3, 8,16,23,30,35,38,38,35,30,23,16, 8, 3, 0,
 0, 2, 6,10,15,20,24,26,26,24,20,15,10, 6, 2, 0,
 0, 1, 3, 6, 8,11,13,14,14,13,11, 8, 6, 3, 1, 0,
 0, 0, 1, 2, 3, 4, 5, 5, 5, 5, 4, 3, 2, 1, 0, 0,
 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
//error:0.000022
};
#endif

typedef struct SubBand{
    int level;
    int stride;
    int width;
    int height;
    int qlog;                                   ///< log(qscale)/log[2^(1/6)]
    DWTELEM *buf;
    struct SubBand *parent;
    uint8_t state[/*7*2*/ 7 + 512][32];
}SubBand;

typedef struct Plane{
    int width;
    int height;
    SubBand band[MAX_DECOMPOSITIONS][4];
}Plane;

typedef struct SnowContext{
//    MpegEncContext m; // needed for motion estimation, should not be used for anything else, the idea is to make the motion estimation eventually independant of MpegEncContext, so this will be removed then (FIXME/XXX)

    AVCodecContext *avctx;
    CABACContext c;
    DSPContext dsp;
    AVFrame input_picture;
    AVFrame current_picture;
    AVFrame last_picture;
    AVFrame mconly_picture;
//     uint8_t q_context[16];
    uint8_t header_state[32];
    int keyframe;
    int version;
    int spatial_decomposition_type;
    int temporal_decomposition_type;
    int spatial_decomposition_count;
    int temporal_decomposition_count;
    DWTELEM *spatial_dwt_buffer;
    DWTELEM *pred_buffer;
    int colorspace_type;
    int chroma_h_shift;
    int chroma_v_shift;
    int spatial_scalability;
    int qlog;
    int mv_scale;
    int qbias;
#define QBIAS_SHIFT 3
    int b_width; //FIXME remove?
    int b_height; //FIXME remove?
    Plane plane[MAX_PLANES];
    SubBand mb_band;
    SubBand mv_band[2];
    
    uint16_t *mb_type;
    uint8_t *mb_mean;
    uint32_t *dummy;
    int16_t (*motion_val8)[2];
    int16_t (*motion_val16)[2];
    MpegEncContext m; // needed for motion estimation, should not be used for anything else, the idea is to make the motion estimation eventually independant of MpegEncContext, so this will be removed then (FIXME/XXX)
}SnowContext;

#define QEXPSHIFT 7 //FIXME try to change this to 0
static const uint8_t qexp[8]={
    128, 140, 152, 166, 181, 197, 215, 235
//   64,  70,  76,  83,  91,  99, 108, 117
//   32,  35,  38,  41,  45,  49,  54,  59
//   16,  17,  19,  21,  23,  25,  27,  29
//    8,   9,  10,  10,  11,  12,  13,  15
};

static inline int mirror(int v, int m){
    if     (v<0) return -v;
    else if(v>m) return 2*m-v;
    else         return v;
}

static inline void put_symbol(CABACContext *c, uint8_t *state, int v, int is_signed){
    int i;

    if(v){
        const int a= ABS(v);
        const int e= av_log2(a);
#if 1
        const int el= FFMIN(e, 10);   
        put_cabac(c, state+0, 0);

        for(i=0; i<el; i++){
            put_cabac(c, state+1+i, 1);  //1..10
        }
        for(; i<e; i++){
            put_cabac(c, state+1+9, 1);  //1..10
        }
        put_cabac(c, state+1+FFMIN(i,9), 0);

        for(i=e-1; i>=el; i--){
            put_cabac(c, state+22+9, (a>>i)&1); //22..31
        }
        for(; i>=0; i--){
            put_cabac(c, state+22+i, (a>>i)&1); //22..31
        }

        if(is_signed)
            put_cabac(c, state+11 + el, v < 0); //11..21
#else
        
        put_cabac(c, state+0, 0);
        if(e<=9){
            for(i=0; i<e; i++){
                put_cabac(c, state+1+i, 1);  //1..10
            }
            put_cabac(c, state+1+i, 0);

            for(i=e-1; i>=0; i--){
                put_cabac(c, state+22+i, (a>>i)&1); //22..31
            }

            if(is_signed)
                put_cabac(c, state+11 + e, v < 0); //11..21
        }else{
            for(i=0; i<e; i++){
                put_cabac(c, state+1+FFMIN(i,9), 1);  //1..10
            }
            put_cabac(c, state+1+FFMIN(i,9), 0);

            for(i=e-1; i>=0; i--){
                put_cabac(c, state+22+FFMIN(i,9), (a>>i)&1); //22..31
            }

            if(is_signed)
                put_cabac(c, state+11 + FFMIN(e,10), v < 0); //11..21
        }
#endif
    }else{
        put_cabac(c, state+0, 1);
    }
}

static inline int get_symbol(CABACContext *c, uint8_t *state, int is_signed){
    if(get_cabac(c, state+0))
        return 0;
    else{
        int i, e, a, el;
 //FIXME try to merge loops with FFMIN() maybe they are equally fast and they are surly cuter
        for(e=0; e<10; e++){ 
            if(get_cabac(c, state + 1 + e)==0) // 1..10
                break;
        }
        el= e;
 
        if(e==10){
            while(get_cabac(c, state + 1 + 9)) //10
                e++;
        }
        a= 1;
        for(i=e-1; i>=el; i--){
            a += a + get_cabac(c, state+22+9); //31
        }
        for(; i>=0; i--){
            a += a + get_cabac(c, state+22+i); //22..31
        }

        if(is_signed && get_cabac(c, state+11 + el)) //11..21
            return -a;
        else
            return a;
    }
}

static inline void put_symbol2(CABACContext *c, uint8_t *state, int v, int log2){
    int i;
    int r= log2>=0 ? 1<<log2 : 1;

    assert(v>=0);
    assert(log2>=-4);

    while(v >= r){
        put_cabac(c, state+4+log2, 1);
        v -= r;
        log2++;
        if(log2>0) r+=r;
    }
    put_cabac(c, state+4+log2, 0);
    
    for(i=log2-1; i>=0; i--){
        put_cabac(c, state+31-i, (v>>i)&1);
    }
}

static inline int get_symbol2(CABACContext *c, uint8_t *state, int log2){
    int i;
    int r= log2>=0 ? 1<<log2 : 1;
    int v=0;

    assert(log2>=-4);

    while(get_cabac(c, state+4+log2)){
        v+= r;
        log2++;
        if(log2>0) r+=r;
    }
    
    for(i=log2-1; i>=0; i--){
        v+= get_cabac(c, state+31-i)<<i;
    }

    return v;
}

static always_inline void lift(DWTELEM *dst, DWTELEM *src, DWTELEM *ref, int dst_step, int src_step, int ref_step, int width, int mul, int add, int shift, int highpass, int inverse){
    const int mirror_left= !highpass;
    const int mirror_right= (width&1) ^ highpass;
    const int w= (width>>1) - 1 + (highpass & width);
    int i;

#define LIFT(src, ref, inv) ((src) + ((inv) ? - (ref) : + (ref)))
    if(mirror_left){
        dst[0] = LIFT(src[0], ((mul*2*ref[0]+add)>>shift), inverse);
        dst += dst_step;
        src += src_step;
    }
    
    for(i=0; i<w; i++){
        dst[i*dst_step] = LIFT(src[i*src_step], ((mul*(ref[i*ref_step] + ref[(i+1)*ref_step])+add)>>shift), inverse);
    }
    
    if(mirror_right){
        dst[w*dst_step] = LIFT(src[w*src_step], ((mul*2*ref[w*ref_step]+add)>>shift), inverse);
    }
}

static always_inline void lift5(DWTELEM *dst, DWTELEM *src, DWTELEM *ref, int dst_step, int src_step, int ref_step, int width, int mul, int add, int shift, int highpass, int inverse){
    const int mirror_left= !highpass;
    const int mirror_right= (width&1) ^ highpass;
    const int w= (width>>1) - 1 + (highpass & width);
    int i;

    if(mirror_left){
        int r= 3*2*ref[0];
        r += r>>4;
        r += r>>8;
        dst[0] = LIFT(src[0], ((r+add)>>shift), inverse);
        dst += dst_step;
        src += src_step;
    }
    
    for(i=0; i<w; i++){
        int r= 3*(ref[i*ref_step] + ref[(i+1)*ref_step]);
        r += r>>4;
        r += r>>8;
        dst[i*dst_step] = LIFT(src[i*src_step], ((r+add)>>shift), inverse);
    }
    
    if(mirror_right){
        int r= 3*2*ref[w*ref_step];
        r += r>>4;
        r += r>>8;
        dst[w*dst_step] = LIFT(src[w*src_step], ((r+add)>>shift), inverse);
    }
}


static void inplace_lift(int *dst, int width, int *coeffs, int n, int shift, int start, int inverse){
    int x, i;
    
    for(x=start; x<width; x+=2){
        int64_t sum=0;

        for(i=0; i<n; i++){
            int x2= x + 2*i - n + 1;
            if     (x2<     0) x2= -x2;
            else if(x2>=width) x2= 2*width-x2-2;
            sum += coeffs[i]*(int64_t)dst[x2];
        }
        if(inverse) dst[x] -= (sum + (1<<shift)/2)>>shift;
        else        dst[x] += (sum + (1<<shift)/2)>>shift;
    }
}

static void inplace_liftV(int *dst, int width, int height, int stride, int *coeffs, int n, int shift, int start, int inverse){
    int x, y, i;
    for(y=start; y<height; y+=2){
        for(x=0; x<width; x++){
            int64_t sum=0;
    
            for(i=0; i<n; i++){
                int y2= y + 2*i - n + 1;
                if     (y2<      0) y2= -y2;
                else if(y2>=height) y2= 2*height-y2-2;
                sum += coeffs[i]*(int64_t)dst[x + y2*stride];
            }
            if(inverse) dst[x + y*stride] -= (sum + (1<<shift)/2)>>shift;
            else        dst[x + y*stride] += (sum + (1<<shift)/2)>>shift;
        }
    }
}

#define SCALEX 1
#define LX0 0
#define LX1 1

#if 0 // more accurate 9/7
#define N1 2
#define SHIFT1 14
#define COEFFS1 (int[]){-25987,-25987}
#define N2 2
#define SHIFT2 19
#define COEFFS2 (int[]){-27777,-27777}
#define N3 2
#define SHIFT3 15
#define COEFFS3 (int[]){28931,28931}
#define N4 2
#define SHIFT4 15
#define COEFFS4 (int[]){14533,14533}
#elif 1 // 13/7 CRF
#define N1 4
#define SHIFT1 4
#define COEFFS1 (int[]){1,-9,-9,1}
#define N2 4
#define SHIFT2 4
#define COEFFS2 (int[]){-1,5,5,-1}
#define N3 0
#define SHIFT3 1
#define COEFFS3 NULL
#define N4 0
#define SHIFT4 1
#define COEFFS4 NULL
#elif 1 // 3/5
#define LX0 1
#define LX1 0
#define SCALEX 0.5
#define N1 2
#define SHIFT1 1
#define COEFFS1 (int[]){1,1}
#define N2 2
#define SHIFT2 2
#define COEFFS2 (int[]){-1,-1}
#define N3 0
#define SHIFT3 0
#define COEFFS3 NULL
#define N4 0
#define SHIFT4 0
#define COEFFS4 NULL
#elif 1 // 11/5 
#define N1 0
#define SHIFT1 1
#define COEFFS1 NULL
#define N2 2
#define SHIFT2 2
#define COEFFS2 (int[]){-1,-1}
#define N3 2
#define SHIFT3 0
#define COEFFS3 (int[]){-1,-1}
#define N4 4
#define SHIFT4 7
#define COEFFS4 (int[]){-5,29,29,-5}
#define SCALEX 4
#elif 1 // 9/7 CDF
#define N1 2
#define SHIFT1 7
#define COEFFS1 (int[]){-203,-203}
#define N2 2
#define SHIFT2 12
#define COEFFS2 (int[]){-217,-217}
#define N3 2
#define SHIFT3 7
#define COEFFS3 (int[]){113,113}
#define N4 2
#define SHIFT4 9
#define COEFFS4 (int[]){227,227}
#define SCALEX 1
#elif 1 // 7/5 CDF
#define N1 0
#define SHIFT1 1
#define COEFFS1 NULL
#define N2 2
#define SHIFT2 2
#define COEFFS2 (int[]){-1,-1}
#define N3 2
#define SHIFT3 0
#define COEFFS3 (int[]){-1,-1}
#define N4 2
#define SHIFT4 4
#define COEFFS4 (int[]){3,3}
#elif 1 // 9/7 MN
#define N1 4
#define SHIFT1 4
#define COEFFS1 (int[]){1,-9,-9,1}
#define N2 2
#define SHIFT2 2
#define COEFFS2 (int[]){1,1}
#define N3 0
#define SHIFT3 1
#define COEFFS3 NULL
#define N4 0
#define SHIFT4 1
#define COEFFS4 NULL
#else // 13/7 CRF
#define N1 4
#define SHIFT1 4
#define COEFFS1 (int[]){1,-9,-9,1}
#define N2 4
#define SHIFT2 4
#define COEFFS2 (int[]){-1,5,5,-1}
#define N3 0
#define SHIFT3 1
#define COEFFS3 NULL
#define N4 0
#define SHIFT4 1
#define COEFFS4 NULL
#endif
static void horizontal_decomposeX(int *b, int width){
    int temp[width];
    const int width2= width>>1;
    const int w2= (width+1)>>1;
    int A1,A2,A3,A4, x;

    inplace_lift(b, width, COEFFS1, N1, SHIFT1, LX1, 0);
    inplace_lift(b, width, COEFFS2, N2, SHIFT2, LX0, 0);
    inplace_lift(b, width, COEFFS3, N3, SHIFT3, LX1, 0);
    inplace_lift(b, width, COEFFS4, N4, SHIFT4, LX0, 0);
    
    for(x=0; x<width2; x++){
        temp[x   ]= b[2*x    ];
        temp[x+w2]= b[2*x + 1];
    }
    if(width&1)
        temp[x   ]= b[2*x    ];
    memcpy(b, temp, width*sizeof(int));
}

static void horizontal_composeX(int *b, int width){
    int temp[width];
    const int width2= width>>1;
    int A1,A2,A3,A4, x;
    const int w2= (width+1)>>1;

    memcpy(temp, b, width*sizeof(int));
    for(x=0; x<width2; x++){
        b[2*x    ]= temp[x   ];
        b[2*x + 1]= temp[x+w2];
    }
    if(width&1)
        b[2*x    ]= temp[x   ];

    inplace_lift(b, width, COEFFS4, N4, SHIFT4, LX0, 1);
    inplace_lift(b, width, COEFFS3, N3, SHIFT3, LX1, 1);
    inplace_lift(b, width, COEFFS2, N2, SHIFT2, LX0, 1);
    inplace_lift(b, width, COEFFS1, N1, SHIFT1, LX1, 1);
}

static void spatial_decomposeX(int *buffer, int width, int height, int stride){
    int x, y;
  
    for(y=0; y<height; y++){
        for(x=0; x<width; x++){
            buffer[y*stride + x] *= SCALEX;
        }
    }

    for(y=0; y<height; y++){
        horizontal_decomposeX(buffer + y*stride, width);
    }
    
    inplace_liftV(buffer, width, height, stride, COEFFS1, N1, SHIFT1, LX1, 0);
    inplace_liftV(buffer, width, height, stride, COEFFS2, N2, SHIFT2, LX0, 0);
    inplace_liftV(buffer, width, height, stride, COEFFS3, N3, SHIFT3, LX1, 0);
    inplace_liftV(buffer, width, height, stride, COEFFS4, N4, SHIFT4, LX0, 0);    
}

static void spatial_composeX(int *buffer, int width, int height, int stride){
    int x, y;
  
    inplace_liftV(buffer, width, height, stride, COEFFS4, N4, SHIFT4, LX0, 1);
    inplace_liftV(buffer, width, height, stride, COEFFS3, N3, SHIFT3, LX1, 1);
    inplace_liftV(buffer, width, height, stride, COEFFS2, N2, SHIFT2, LX0, 1);
    inplace_liftV(buffer, width, height, stride, COEFFS1, N1, SHIFT1, LX1, 1);

    for(y=0; y<height; y++){
        horizontal_composeX(buffer + y*stride, width);
    }

    for(y=0; y<height; y++){
        for(x=0; x<width; x++){
            buffer[y*stride + x] /= SCALEX;
        }
    }
}

static void horizontal_decompose53i(int *b, int width){
    int temp[width];
    const int width2= width>>1;
    int A1,A2,A3,A4, x;
    const int w2= (width+1)>>1;

    for(x=0; x<width2; x++){
        temp[x   ]= b[2*x    ];
        temp[x+w2]= b[2*x + 1];
    }
    if(width&1)
        temp[x   ]= b[2*x    ];
#if 0
    A2= temp[1       ];
    A4= temp[0       ];
    A1= temp[0+width2];
    A1 -= (A2 + A4)>>1;
    A4 += (A1 + 1)>>1;
    b[0+width2] = A1;
    b[0       ] = A4;
    for(x=1; x+1<width2; x+=2){
        A3= temp[x+width2];
        A4= temp[x+1     ];
        A3 -= (A2 + A4)>>1;
        A2 += (A1 + A3 + 2)>>2;
        b[x+width2] = A3;
        b[x       ] = A2;

        A1= temp[x+1+width2];
        A2= temp[x+2       ];
        A1 -= (A2 + A4)>>1;
        A4 += (A1 + A3 + 2)>>2;
        b[x+1+width2] = A1;
        b[x+1       ] = A4;
    }
    A3= temp[width-1];
    A3 -= A2;
    A2 += (A1 + A3 + 2)>>2;
    b[width -1] = A3;
    b[width2-1] = A2;
#else        
    lift(b+w2, temp+w2, temp, 1, 1, 1, width, -1, 0, 1, 1, 0);
    lift(b   , temp   , b+w2, 1, 1, 1, width,  1, 2, 2, 0, 0);
#endif
}

static void vertical_decompose53iH0(int *b0, int *b1, int *b2, int width){
    int i;
    
    for(i=0; i<width; i++){
        b1[i] -= (b0[i] + b2[i])>>1;
    }
}

static void vertical_decompose53iL0(int *b0, int *b1, int *b2, int width){
    int i;
    
    for(i=0; i<width; i++){
        b1[i] += (b0[i] + b2[i] + 2)>>2;
    }
}

static void spatial_decompose53i(int *buffer, int width, int height, int stride){
    int x, y;
    DWTELEM *b0= buffer + mirror(-2-1, height-1)*stride;
    DWTELEM *b1= buffer + mirror(-2  , height-1)*stride;
  
    for(y=-2; y<height; y+=2){
        DWTELEM *b2= buffer + mirror(y+1, height-1)*stride;
        DWTELEM *b3= buffer + mirror(y+2, height-1)*stride;

{START_TIMER
        if(b1 <= b3)     horizontal_decompose53i(b2, width);
        if(y+2 < height) horizontal_decompose53i(b3, width);
STOP_TIMER("horizontal_decompose53i")}
        
{START_TIMER
        if(b1 <= b3) vertical_decompose53iH0(b1, b2, b3, width);
        if(b0 <= b2) vertical_decompose53iL0(b0, b1, b2, width);
STOP_TIMER("vertical_decompose53i*")}
        
        b0=b2;
        b1=b3;
    }
}

#define lift5 lift
#if 1
#define W_AM 3
#define W_AO 0
#define W_AS 1

#define W_BM 1
#define W_BO 8
#define W_BS 4

#undef lift5
#define W_CM 9999
#define W_CO 2
#define W_CS 2

#define W_DM 15
#define W_DO 16
#define W_DS 5
#elif 0
#define W_AM 55
#define W_AO 16
#define W_AS 5

#define W_BM 3
#define W_BO 32
#define W_BS 6

#define W_CM 127
#define W_CO 64
#define W_CS 7

#define W_DM 7
#define W_DO 8
#define W_DS 4
#elif 0
#define W_AM 97
#define W_AO 32
#define W_AS 6

#define W_BM 63
#define W_BO 512
#define W_BS 10

#define W_CM 13
#define W_CO 8
#define W_CS 4

#define W_DM 15
#define W_DO 16
#define W_DS 5

#else

#define W_AM 203
#define W_AO 64
#define W_AS 7

#define W_BM 217
#define W_BO 2048
#define W_BS 12

#define W_CM 113
#define W_CO 64
#define W_CS 7

#define W_DM 227
#define W_DO 128
#define W_DS 9
#endif
static void horizontal_decompose97i(int *b, int width){
    int temp[width];
    const int w2= (width+1)>>1;

    lift (temp+w2, b    +1, b      , 1, 2, 2, width, -W_AM, W_AO, W_AS, 1, 0);
    lift (temp   , b      , temp+w2, 1, 2, 1, width, -W_BM, W_BO, W_BS, 0, 0);
    lift5(b   +w2, temp+w2, temp   , 1, 1, 1, width,  W_CM, W_CO, W_CS, 1, 0);
    lift (b      , temp   , b   +w2, 1, 1, 1, width,  W_DM, W_DO, W_DS, 0, 0);
}


static void vertical_decompose97iH0(int *b0, int *b1, int *b2, int width){
    int i;
    
    for(i=0; i<width; i++){
        b1[i] -= (W_AM*(b0[i] + b2[i])+W_AO)>>W_AS;
    }
}

static void vertical_decompose97iH1(int *b0, int *b1, int *b2, int width){
    int i;
    
    for(i=0; i<width; i++){
#ifdef lift5
        b1[i] += (W_CM*(b0[i] + b2[i])+W_CO)>>W_CS;
#else
        int r= 3*(b0[i] + b2[i]);
        r+= r>>4;
        r+= r>>8;
        b1[i] += (r+W_CO)>>W_CS;
#endif
    }
}

static void vertical_decompose97iL0(int *b0, int *b1, int *b2, int width){
    int i;
    
    for(i=0; i<width; i++){
        b1[i] -= (W_BM*(b0[i] + b2[i])+W_BO)>>W_BS;
    }
}

static void vertical_decompose97iL1(int *b0, int *b1, int *b2, int width){
    int i;
    
    for(i=0; i<width; i++){
        b1[i] += (W_DM*(b0[i] + b2[i])+W_DO)>>W_DS;
    }
}

static void spatial_decompose97i(int *buffer, int width, int height, int stride){
    int x, y;
    DWTELEM *b0= buffer + mirror(-4-1, height-1)*stride;
    DWTELEM *b1= buffer + mirror(-4  , height-1)*stride;
    DWTELEM *b2= buffer + mirror(-4+1, height-1)*stride;
    DWTELEM *b3= buffer + mirror(-4+2, height-1)*stride;
  
    for(y=-4; y<height; y+=2){
        DWTELEM *b4= buffer + mirror(y+3, height-1)*stride;
        DWTELEM *b5= buffer + mirror(y+4, height-1)*stride;

{START_TIMER
        if(b3 <= b5)     horizontal_decompose97i(b4, width);
        if(y+4 < height) horizontal_decompose97i(b5, width);
if(width>400){
STOP_TIMER("horizontal_decompose97i")
}}
        
{START_TIMER
        if(b3 <= b5) vertical_decompose97iH0(b3, b4, b5, width);
        if(b2 <= b4) vertical_decompose97iL0(b2, b3, b4, width);
        if(b1 <= b3) vertical_decompose97iH1(b1, b2, b3, width);
        if(b0 <= b2) vertical_decompose97iL1(b0, b1, b2, width);

if(width>400){
STOP_TIMER("vertical_decompose97i")
}}
        
        b0=b2;
        b1=b3;
        b2=b4;
        b3=b5;
    }
}

void ff_spatial_dwt(int *buffer, int width, int height, int stride, int type, int decomposition_count){
    int level;
    
    for(level=0; level<decomposition_count; level++){
        switch(type){
        case 0: spatial_decompose97i(buffer, width>>level, height>>level, stride<<level); break;
        case 1: spatial_decompose53i(buffer, width>>level, height>>level, stride<<level); break;
        case 2: spatial_decomposeX  (buffer, width>>level, height>>level, stride<<level); break;
        }
    }
}

static void horizontal_compose53i(int *b, int width){
    int temp[width];
    const int width2= width>>1;
    const int w2= (width+1)>>1;
    int A1,A2,A3,A4, x;

#if 0
    A2= temp[1       ];
    A4= temp[0       ];
    A1= temp[0+width2];
    A1 -= (A2 + A4)>>1;
    A4 += (A1 + 1)>>1;
    b[0+width2] = A1;
    b[0       ] = A4;
    for(x=1; x+1<width2; x+=2){
        A3= temp[x+width2];
        A4= temp[x+1     ];
        A3 -= (A2 + A4)>>1;
        A2 += (A1 + A3 + 2)>>2;
        b[x+width2] = A3;
        b[x       ] = A2;

        A1= temp[x+1+width2];
        A2= temp[x+2       ];
        A1 -= (A2 + A4)>>1;
        A4 += (A1 + A3 + 2)>>2;
        b[x+1+width2] = A1;
        b[x+1       ] = A4;
    }
    A3= temp[width-1];
    A3 -= A2;
    A2 += (A1 + A3 + 2)>>2;
    b[width -1] = A3;
    b[width2-1] = A2;
#else   
    lift(temp   , b   , b+w2, 1, 1, 1, width,  1, 2, 2, 0, 1);
    lift(temp+w2, b+w2, temp, 1, 1, 1, width, -1, 0, 1, 1, 1);
#endif
    for(x=0; x<width2; x++){
        b[2*x    ]= temp[x   ];
        b[2*x + 1]= temp[x+w2];
    }
    if(width&1)
        b[2*x    ]= temp[x   ];
}

static void vertical_compose53iH0(int *b0, int *b1, int *b2, int width){
    int i;
    
    for(i=0; i<width; i++){
        b1[i] += (b0[i] + b2[i])>>1;
    }
}

static void vertical_compose53iL0(int *b0, int *b1, int *b2, int width){
    int i;
    
    for(i=0; i<width; i++){
        b1[i] -= (b0[i] + b2[i] + 2)>>2;
    }
}

static void spatial_compose53i(int *buffer, int width, int height, int stride){
    int x, y;
    DWTELEM *b0= buffer + mirror(-1-1, height-1)*stride;
    DWTELEM *b1= buffer + mirror(-1  , height-1)*stride;
  
    for(y=-1; y<=height; y+=2){
        DWTELEM *b2= buffer + mirror(y+1, height-1)*stride;
        DWTELEM *b3= buffer + mirror(y+2, height-1)*stride;

{START_TIMER
        if(b1 <= b3) vertical_compose53iL0(b1, b2, b3, width);
        if(b0 <= b2) vertical_compose53iH0(b0, b1, b2, width);
STOP_TIMER("vertical_compose53i*")}

{START_TIMER
        if(y-1 >= 0) horizontal_compose53i(b0, width);
        if(b0 <= b2) horizontal_compose53i(b1, width);
STOP_TIMER("horizontal_compose53i")}

        b0=b2;
        b1=b3;
    }
}   

 
static void horizontal_compose97i(int *b, int width){
    int temp[width];
    const int w2= (width+1)>>1;

    lift (temp   , b      , b   +w2, 1, 1, 1, width,  W_DM, W_DO, W_DS, 0, 1);
    lift5(temp+w2, b   +w2, temp   , 1, 1, 1, width,  W_CM, W_CO, W_CS, 1, 1);
    lift (b      , temp   , temp+w2, 2, 1, 1, width, -W_BM, W_BO, W_BS, 0, 1);
    lift (b+1    , temp+w2, b      , 2, 1, 2, width, -W_AM, W_AO, W_AS, 1, 1);
}

static void vertical_compose97iH0(int *b0, int *b1, int *b2, int width){
    int i;
    
    for(i=0; i<width; i++){
        b1[i] += (W_AM*(b0[i] + b2[i])+W_AO)>>W_AS;
    }
}

static void vertical_compose97iH1(int *b0, int *b1, int *b2, int width){
    int i;
    
    for(i=0; i<width; i++){
#ifdef lift5
        b1[i] -= (W_CM*(b0[i] + b2[i])+W_CO)>>W_CS;
#else
        int r= 3*(b0[i] + b2[i]);
        r+= r>>4;
        r+= r>>8;
        b1[i] -= (r+W_CO)>>W_CS;
#endif
    }
}

static void vertical_compose97iL0(int *b0, int *b1, int *b2, int width){
    int i;
    
    for(i=0; i<width; i++){
        b1[i] += (W_BM*(b0[i] + b2[i])+W_BO)>>W_BS;
    }
}

static void vertical_compose97iL1(int *b0, int *b1, int *b2, int width){
    int i;
    
    for(i=0; i<width; i++){
        b1[i] -= (W_DM*(b0[i] + b2[i])+W_DO)>>W_DS;
    }
}

static void spatial_compose97i(int *buffer, int width, int height, int stride){
    int x, y;
    DWTELEM *b0= buffer + mirror(-3-1, height-1)*stride;
    DWTELEM *b1= buffer + mirror(-3  , height-1)*stride;
    DWTELEM *b2= buffer + mirror(-3+1, height-1)*stride;
    DWTELEM *b3= buffer + mirror(-3+2, height-1)*stride;

    for(y=-3; y<=height; y+=2){
        DWTELEM *b4= buffer + mirror(y+3, height-1)*stride;
        DWTELEM *b5= buffer + mirror(y+4, height-1)*stride;

        if(stride == width && y+4 < height && 0){ 
            int x;
            for(x=0; x<width/2; x++)
                b5[x] += 64*2;
            for(; x<width; x++)
                b5[x] += 169*2;
        }
        
{START_TIMER
        if(b3 <= b5) vertical_compose97iL1(b3, b4, b5, width);
        if(b2 <= b4) vertical_compose97iH1(b2, b3, b4, width);
        if(b1 <= b3) vertical_compose97iL0(b1, b2, b3, width);
        if(b0 <= b2) vertical_compose97iH0(b0, b1, b2, width);
if(width>400){
STOP_TIMER("vertical_compose97i")}}

{START_TIMER
        if(y-1>=  0) horizontal_compose97i(b0, width);
        if(b0 <= b2) horizontal_compose97i(b1, width);
if(width>400 && b0 <= b2){
STOP_TIMER("horizontal_compose97i")}}
        
        b0=b2;
        b1=b3;
        b2=b4;
        b3=b5;
    }
}

void ff_spatial_idwt(int *buffer, int width, int height, int stride, int type, int decomposition_count){
    int level;

    for(level=decomposition_count-1; level>=0; level--){
        switch(type){
        case 0: spatial_compose97i(buffer, width>>level, height>>level, stride<<level); break;
        case 1: spatial_compose53i(buffer, width>>level, height>>level, stride<<level); break;
        case 2: spatial_composeX  (buffer, width>>level, height>>level, stride<<level); break;
        }
    }
}

static const int hilbert[16][2]={
    {0,0}, {1,0}, {1,1}, {0,1},
    {0,2}, {0,3}, {1,3}, {1,2},
    {2,2}, {2,3}, {3,3}, {3,2},
    {3,1}, {2,1}, {2,0}, {3,0},
};
#if 0
-o o-
 | |
 o-o
 
-o-o o-o-
   | | 
 o-o o-o
 |     |
 o o-o o
 | | | |
 o-o o-o
 
 0112122312232334122323342334
 0123456789ABCDEF0123456789AB
 RLLRMRRLLRRMRLLMLRRLMLLRRLLM
 
 4  B  F 14 1B
 4 11 15 20 27
 
-o o-o-o o-o-o o-
 | |   | |   | |
 o-o o-o o-o o-o
     |     |
 o-o o-o o-o o-o
 | |   | |   | |
 o o-o-o o-o-o o
 |             |
 o-o o-o-o-o o-o
   | |     | | 
 o-o o-o o-o o-o
 |     | |     |
 o o-o o o o-o o
 | | | | | | | |
 o-o o-o o-o o-o

#endif

#define SVI(a, i, x, y) \
{\
    a[i][0]= x;\
    a[i][1]= y;\
    i++;\
}

static int sig_cmp(const void *a, const void *b){
    const int16_t* da = (const int16_t *) a;
    const int16_t* db = (const int16_t *) b;
    
    if(da[1] != db[1]) return da[1] - db[1];
    else               return da[0] - db[0];
}

static int deint(unsigned int a){
    a &= 0x55555555;         //0 1 2 3 4 5 6 7 8 9 A B C D E F
    a +=     a & 0x11111111; // 01  23  45  67  89  AB  CD  EF
    a +=  3*(a & 0x0F0F0F0F);//   0123    4567    89AB    CDEF
    a += 15*(a & 0x00FF00FF);//       01234567        89ABCDEF
    a +=255*(a & 0x0000FFFF);//               0123456789ABCDEF
    return a>>15;
}

static void encode_subband_z0run(SnowContext *s, SubBand *b, DWTELEM *src, DWTELEM *parent, int stride, int orientation){
    const int level= b->level;
    const int w= b->width;
    const int h= b->height;
    int x, y, pos;

    if(1){
        int run=0;
        int runs[w*h];
        int run_index=0;
        int count=0;
        
        for(pos=0; ; pos++){
            int x= deint(pos   );
            int y= deint(pos>>1);
            int v, p=0, pr=0, pd=0;
            int /*ll=0, */l=0, lt=0, t=0/*, rt=0*/;

            if(x>=w || y>=h){
                if(x>=w && y>=h)
                    break;
                continue;
            }
            count++;
                
            v= src[x + y*stride];

            if(y){
                t= src[x + (y-1)*stride];
                if(x){
                    lt= src[x - 1 + (y-1)*stride];
                }
                if(x + 1 < w){
                    /*rt= src[x + 1 + (y-1)*stride]*/;
                }
            }
            if(x){
                l= src[x - 1 + y*stride];
                /*if(x > 1){
                    if(orientation==1) ll= src[y + (x-2)*stride];
                    else               ll= src[x - 2 + y*stride];
                }*/
            }
            if(parent){
                int px= x>>1;
                int py= y>>1;
                if(px<b->parent->width && py<b->parent->height){
                    p= parent[px + py*2*stride];
                    /*if(px+1<b->parent->width) 
                        pr= parent[px + 1 + py*2*stride];
                    if(py+1<b->parent->height) 
                        pd= parent[px + (py+1)*2*stride];*/
                }
            }
            if(!(/*ll|*/l|lt|t|/*rt|*/p)){
                if(v){
                    runs[run_index++]= run;
                    run=0;
                }else{
                    run++;
                }
            }
        }
        assert(count==w*h);
        runs[run_index++]= run;
        run_index=0;
        run= runs[run_index++];

        put_symbol(&s->c, b->state[1], run, 0);
        
        for(pos=0; ; pos++){
            int x= deint(pos   );
            int y= deint(pos>>1);
            int v, p=0, pr=0, pd=0;
            int /*ll=0, */l=0, lt=0, t=0/*, rt=0*/;

            if(x>=w || y>=h){
                if(x>=w && y>=h)
                    break;
                continue;
            }
            v= src[x + y*stride];

            if(y){
                t= src[x + (y-1)*stride];
                if(x){
                    lt= src[x - 1 + (y-1)*stride];
                }
                if(x + 1 < w){
//                    rt= src[x + 1 + (y-1)*stride];
                }
            }
            if(x){
                l= src[x - 1 + y*stride];
                /*if(x > 1){
                    if(orientation==1) ll= src[y + (x-2)*stride];
                    else               ll= src[x - 2 + y*stride];
                }*/
            }

            if(parent){
                int px= x>>1;
                int py= y>>1;
                if(px<b->parent->width && py<b->parent->height){
                    p= parent[px + py*2*stride];
/*                        if(px+1<b->parent->width) 
                        pr= parent[px + 1 + py*2*stride];
                    if(py+1<b->parent->height) 
                        pd= parent[px + (py+1)*2*stride];*/
                }
            }
            if(/*ll|*/l|lt|t|/*rt|*/p){
                int context= av_log2(/*ABS(ll) + */2*(3*ABS(l) + ABS(lt) + 2*ABS(t) + /*ABS(rt) +*/ ABS(p)));

                put_cabac(&s->c, &b->state[0][context], !!v);
            }else{
                if(!run){
                    run= runs[run_index++];
                    put_symbol(&s->c, b->state[1], run, 0);
                    assert(v);
                }else{
                    run--;
                    assert(!v);
                }
            }
            if(v){
                int context= av_log2(/*ABS(ll) + */3*ABS(l) + ABS(lt) + 2*ABS(t) + /*ABS(rt) +*/ ABS(p));

                put_symbol(&s->c, b->state[context + 2], ABS(v)-1, 0);
                put_cabac(&s->c, &b->state[0][16 + 1 + 3 + quant3b[l&0xFF] + 3*quant3b[t&0xFF]], v<0);
            }
        }
    }
}

static void encode_subband_bp(SnowContext *s, SubBand *b, DWTELEM *src, DWTELEM *parent, int stride, int orientation){
    const int level= b->level;
    const int w= b->width;
    const int h= b->height;
    int x, y;

#if 0
    int plane;
    for(plane=24; plane>=0; plane--){
        int run=0;
        int runs[w*h];
        int run_index=0;
                
        for(y=0; y<h; y++){
            for(x=0; x<w; x++){
                int v, lv, p=0;
                int d=0, r=0, rd=0, ld=0;
                int /*ll=0, */l=0, lt=0, t=0, rt=0;
                v= src[x + y*stride];

                if(y){
                    t= src[x + (y-1)*stride];
                    if(x){
                        lt= src[x - 1 + (y-1)*stride];
                    }
                    if(x + 1 < w){
                        rt= src[x + 1 + (y-1)*stride];
                    }
                }
                if(x){
                    l= src[x - 1 + y*stride];
                    /*if(x > 1){
                        if(orientation==1) ll= src[y + (x-2)*stride];
                        else               ll= src[x - 2 + y*stride];
                    }*/
                }
                if(y+1<h){
                    d= src[x + (y+1)*stride];
                    if(x)         ld= src[x - 1 + (y+1)*stride];
                    if(x + 1 < w) rd= src[x + 1 + (y+1)*stride];
                }
                if(x + 1 < w)
                    r= src[x + 1 + y*stride];
                if(parent){
                    int px= x>>1;
                    int py= y>>1;
                    if(px<b->parent->width && py<b->parent->height) 
                        p= parent[px + py*2*stride];
                }
#define HIDE(c, plane) c= c>=0 ? c&((-1)<<(plane)) : -((-c)&((-1)<<(plane)));
                lv=v;
                HIDE( v, plane)
                HIDE(lv, plane+1)
                HIDE( p, plane)
                HIDE( l, plane)
                HIDE(lt, plane)
                HIDE( t, plane)
                HIDE(rt, plane)
                HIDE( r, plane+1)
                HIDE(rd, plane+1)
                HIDE( d, plane+1)
                HIDE(ld, plane+1)
                if(!(/*ll|*/l|lt|t|rt|r|rd|ld|d|p|lv)){
                    if(v){
                        runs[run_index++]= run;
                        run=0;
                    }else{
                        run++;
                    }
                }
            }
        }
        runs[run_index++]= run;
        run_index=0;
        run= runs[run_index++];

        put_symbol(&s->c, b->state[1], run, 0);
        
        for(y=0; y<h; y++){
            for(x=0; x<w; x++){
                int v, p=0, lv;
                int /*ll=0, */l=0, lt=0, t=0, rt=0;
                int d=0, r=0, rd=0, ld=0;
                v= src[x + y*stride];

                if(y){
                    t= src[x + (y-1)*stride];
                    if(x){
                        lt= src[x - 1 + (y-1)*stride];
                    }
                    if(x + 1 < w){
                        rt= src[x + 1 + (y-1)*stride];
                    }
                }
                if(x){
                    l= src[x - 1 + y*stride];
                    /*if(x > 1){
                        if(orientation==1) ll= src[y + (x-2)*stride];
                        else               ll= src[x - 2 + y*stride];
                    }*/
                }
                if(y+1<h){
                    d= src[x + (y+1)*stride];
                    if(x)         ld= src[x - 1 + (y+1)*stride];
                    if(x + 1 < w) rd= src[x + 1 + (y+1)*stride];
                }
                if(x + 1 < w)
                    r= src[x + 1 + y*stride];

                if(parent){
                    int px= x>>1;
                    int py= y>>1;
                    if(px<b->parent->width && py<b->parent->height) 
                        p= parent[px + py*2*stride];
                }
                lv=v;
                HIDE( v, plane)
                HIDE(lv, plane+1)
                HIDE( p, plane)
                HIDE( l, plane)
                HIDE(lt, plane)
                HIDE( t, plane)
                HIDE(rt, plane)
                HIDE( r, plane+1)
                HIDE(rd, plane+1)
                HIDE( d, plane+1)
                HIDE(ld, plane+1)
                if(/*ll|*/l|lt|t|rt|r|rd|ld|d|p|lv){
                    int context= av_log2(/*ABS(ll) + */3*ABS(l) + ABS(lt) + 2*ABS(t) + ABS(rt) + ABS(p)
                                                      +3*ABS(r) + ABS(rd) + 2*ABS(d) + ABS(ld));

                    if(lv) put_cabac(&s->c, &b->state[99][context + 8*(av_log2(ABS(lv))-plane)], !!(v-lv));
                    else   put_cabac(&s->c, &b->state[ 0][context], !!v);
                }else{
                    assert(!lv);
                    if(!run){
                        run= runs[run_index++];
                        put_symbol(&s->c, b->state[1], run, 0);
                        assert(v);
                    }else{
                        run--;
                        assert(!v);
                    }
                }
                if(v && !lv){
                    int context=    clip(quant3b[l&0xFF] + quant3b[r&0xFF], -1,1)
                                + 3*clip(quant3b[t&0xFF] + quant3b[d&0xFF], -1,1);
                    put_cabac(&s->c, &b->state[0][16 + 1 + 3 + context], v<0);
                }
            }
        }
    }
    return;    
#endif
}

static void encode_subband_X(SnowContext *s, SubBand *b, DWTELEM *src, DWTELEM *parent, int stride, int orientation){
    const int level= b->level;
    const int w= b->width;
    const int h= b->height;
    int x, y;

#if 0
    if(orientation==3 && parent && 0){
        int16_t candidate[w*h][2];
        uint8_t state[w*h];
        int16_t boarder[3][w*h*4][2];
        int16_t significant[w*h][2];
        int candidate_count=0;
        int boarder_count[3]={0,0,0};
        int significant_count=0;
        int rle_pos=0;
        int v, last_v;
        int primary= orientation==1;
        
        memset(candidate, 0, sizeof(candidate));
        memset(state, 0, sizeof(state));
        memset(boarder, 0, sizeof(boarder));
        
        for(y=0; y<h; y++){
            for(x=0; x<w; x++){
                if(parent[(x>>1) + (y>>1)*2*stride])
                    SVI(candidate, candidate_count, x, y)
            }
        }

        for(;;){
            while(candidate_count && !boarder_count[0] && !boarder_count[1] && !boarder_count[2]){
                candidate_count--;
                x= candidate[ candidate_count][0];
                y= candidate[ candidate_count][1];
                if(state[x + y*w])
                    continue;
                state[x + y*w]= 1;
                v= !!src[x + y*stride];
                put_cabac(&s->c, &b->state[0][0], v);
                if(v){
                    SVI(significant, significant_count, x,y)
                    if(x     && !state[x - 1 +  y   *w]) SVI(boarder[0],boarder_count[0],x-1,y  )
                    if(y     && !state[x     + (y-1)*w]) SVI(boarder[1],boarder_count[1],x  ,y-1)
                    if(x+1<w && !state[x + 1 +  y   *w]) SVI(boarder[0],boarder_count[0],x+1,y  )
                    if(y+1<h && !state[x     + (y+1)*w]) SVI(boarder[1],boarder_count[1],x  ,y+1)
                    if(x     && y     && !state[x - 1 + (y-1)*w]) SVI(boarder[2],boarder_count[2],x-1,y-1)
                    if(x     && y+1<h && !state[x - 1 + (y+1)*w]) SVI(boarder[2],boarder_count[2],x-1,y+1)
                    if(x+1<w && y+1<h && !state[x + 1 + (y+1)*w]) SVI(boarder[2],boarder_count[2],x+1,y+1)
                    if(x+1<w && y     && !state[x + 1 + (y-1)*w]) SVI(boarder[2],boarder_count[2],x+1,y-1)
                }
            }
            while(!boarder_count[0] && !boarder_count[1] && !boarder_count[2] && rle_pos < w*h){
                int run=0;
                for(; rle_pos < w*h;){
                    x= rle_pos % w; //FIXME speed
                    y= rle_pos / w;
                    rle_pos++;
                    if(state[x + y*w])
                        continue;
                    state[x + y*w]= 1;
                    v= !!src[x + y*stride];
                    if(v){
                        put_symbol(&s->c, b->state[1], run, 0);
                        SVI(significant, significant_count, x,y)
                        if(x     && !state[x - 1 +  y   *w]) SVI(boarder[0],boarder_count[0],x-1,y  )
                        if(y     && !state[x     + (y-1)*w]) SVI(boarder[1],boarder_count[1],x  ,y-1)
                        if(x+1<w && !state[x + 1 +  y   *w]) SVI(boarder[0],boarder_count[0],x+1,y  )
                        if(y+1<h && !state[x     + (y+1)*w]) SVI(boarder[1],boarder_count[1],x  ,y+1)
                        if(x     && y     && !state[x - 1 + (y-1)*w]) SVI(boarder[2],boarder_count[2],x-1,y-1)
                        if(x     && y+1<h && !state[x - 1 + (y+1)*w]) SVI(boarder[2],boarder_count[2],x-1,y+1)
                        if(x+1<w && y+1<h && !state[x + 1 + (y+1)*w]) SVI(boarder[2],boarder_count[2],x+1,y+1)
                        if(x+1<w && y     && !state[x + 1 + (y-1)*w]) SVI(boarder[2],boarder_count[2],x+1,y-1)
                        break;
//FIXME                note only right & down can be boarders
                    }
                    run++;
                }
            }
            if(!boarder_count[0] && !boarder_count[1] && !boarder_count[2])
                break;
            
            while(boarder_count[0] || boarder_count[1] || boarder_count[2]){
                int index;
                
                if     (boarder_count[  primary]) index=  primary;
                else if(boarder_count[1-primary]) index=1-primary;
                else                              index=2;
                
                boarder_count[index]--;
                x= boarder[index][ boarder_count[index] ][0];
                y= boarder[index][ boarder_count[index] ][1];
                if(state[x + y*w]) //FIXME maybe check earlier
                    continue;
                state[x + y*w]= 1;
                v= !!src[x + y*stride];
                put_cabac(&s->c, &b->state[0][index+1], v);
                if(v){
                    SVI(significant, significant_count, x,y)
                    if(x     && !state[x - 1 +  y   *w]) SVI(boarder[0],boarder_count[0],x-1,y  )
                    if(y     && !state[x     + (y-1)*w]) SVI(boarder[1],boarder_count[1],x  ,y-1)
                    if(x+1<w && !state[x + 1 +  y   *w]) SVI(boarder[0],boarder_count[0],x+1,y  )
                    if(y+1<h && !state[x     + (y+1)*w]) SVI(boarder[1],boarder_count[1],x  ,y+1)
                    if(x     && y     && !state[x - 1 + (y-1)*w]) SVI(boarder[2],boarder_count[2],x-1,y-1)
                    if(x     && y+1<h && !state[x - 1 + (y+1)*w]) SVI(boarder[2],boarder_count[2],x-1,y+1)
                    if(x+1<w && y+1<h && !state[x + 1 + (y+1)*w]) SVI(boarder[2],boarder_count[2],x+1,y+1)
                    if(x+1<w && y     && !state[x + 1 + (y-1)*w]) SVI(boarder[2],boarder_count[2],x+1,y-1)
                }
            }
        }
        //FIXME sort significant coeffs maybe
        if(1){
            qsort(significant, significant_count, sizeof(int16_t[2]), sig_cmp);
        }
        
        last_v=1;
        while(significant_count){
            int context= 3 + quant7[last_v&0xFF]; //use significance of suroundings
            significant_count--;
            x= significant[significant_count][0];//FIXME try opposit direction
            y= significant[significant_count][1];
            v= src[x + y*stride];
            put_symbol(&s->c, b->state[context + 2], v, 1); //FIXME try to avoid first bit, try this with the old code too!!
            last_v= v;
        }
    }
#endif
}

static void encode_subband_c0run(SnowContext *s, SubBand *b, DWTELEM *src, DWTELEM *parent, int stride, int orientation){
    const int level= b->level;
    const int w= b->width;
    const int h= b->height;
    int x, y;

    if(1){
        int run=0;
        int runs[w*h];
        int run_index=0;
                
        for(y=0; y<h; y++){
            for(x=0; x<w; x++){
                int v, p=0;
                int /*ll=0, */l=0, lt=0, t=0, rt=0;
                v= src[x + y*stride];

                if(y){
                    t= src[x + (y-1)*stride];
                    if(x){
                        lt= src[x - 1 + (y-1)*stride];
                    }
                    if(x + 1 < w){
                        rt= src[x + 1 + (y-1)*stride];
                    }
                }
                if(x){
                    l= src[x - 1 + y*stride];
                    /*if(x > 1){
                        if(orientation==1) ll= src[y + (x-2)*stride];
                        else               ll= src[x - 2 + y*stride];
                    }*/
                }
                if(parent){
                    int px= x>>1;
                    int py= y>>1;
                    if(px<b->parent->width && py<b->parent->height) 
                        p= parent[px + py*2*stride];
                }
                if(!(/*ll|*/l|lt|t|rt|p)){
                    if(v){
                        runs[run_index++]= run;
                        run=0;
                    }else{
                        run++;
                    }
                }
            }
        }
        runs[run_index++]= run;
        run_index=0;
        run= runs[run_index++];

        put_symbol2(&s->c, b->state[1], run, 3);
        
        for(y=0; y<h; y++){
            for(x=0; x<w; x++){
                int v, p=0;
                int /*ll=0, */l=0, lt=0, t=0, rt=0;
                v= src[x + y*stride];

                if(y){
                    t= src[x + (y-1)*stride];
                    if(x){
                        lt= src[x - 1 + (y-1)*stride];
                    }
                    if(x + 1 < w){
                        rt= src[x + 1 + (y-1)*stride];
                    }
                }
                if(x){
                    l= src[x - 1 + y*stride];
                    /*if(x > 1){
                        if(orientation==1) ll= src[y + (x-2)*stride];
                        else               ll= src[x - 2 + y*stride];
                    }*/
                }
                if(parent){
                    int px= x>>1;
                    int py= y>>1;
                    if(px<b->parent->width && py<b->parent->height) 
                        p= parent[px + py*2*stride];
                }
                if(/*ll|*/l|lt|t|rt|p){
                    int context= av_log2(/*ABS(ll) + */3*ABS(l) + ABS(lt) + 2*ABS(t) + ABS(rt) + ABS(p));

                    put_cabac(&s->c, &b->state[0][context], !!v);
                }else{
                    if(!run){
                        run= runs[run_index++];

                        put_symbol2(&s->c, b->state[1], run, 3);
                        assert(v);
                    }else{
                        run--;
                        assert(!v);
                    }
                }
                if(v){
                    int context= av_log2(/*ABS(ll) + */3*ABS(l) + ABS(lt) + 2*ABS(t) + ABS(rt) + ABS(p));

                    put_symbol2(&s->c, b->state[context + 2], ABS(v)-1, context-4);
                    put_cabac(&s->c, &b->state[0][16 + 1 + 3 + quant3b[l&0xFF] + 3*quant3b[t&0xFF]], v<0);
                }
            }
        }
    }
}

static void encode_subband(SnowContext *s, SubBand *b, DWTELEM *src, DWTELEM *parent, int stride, int orientation){    
//    encode_subband_qtree(s, b, src, parent, stride, orientation);
//    encode_subband_z0run(s, b, src, parent, stride, orientation);
    encode_subband_c0run(s, b, src, parent, stride, orientation);
//    encode_subband_dzr(s, b, src, parent, stride, orientation);
}

static inline void decode_subband(SnowContext *s, SubBand *b, DWTELEM *src, DWTELEM *parent, int stride, int orientation){
    const int level= b->level;
    const int w= b->width;
    const int h= b->height;
    int x,y;

    START_TIMER
#if 0    
    for(y=0; y<b->height; y++)
        memset(&src[y*stride], 0, b->width*sizeof(DWTELEM));

    int plane;
    for(plane=24; plane>=0; plane--){
        int run;

        run= get_symbol(&s->c, b->state[1], 0);
                
#define HIDE(c, plane) c= c>=0 ? c&((-1)<<(plane)) : -((-c)&((-1)<<(plane)));
        
        for(y=0; y<h; y++){
            for(x=0; x<w; x++){
                int v, p=0, lv;
                int /*ll=0, */l=0, lt=0, t=0, rt=0;
                int d=0, r=0, rd=0, ld=0;
                lv= src[x + y*stride];

                if(y){
                    t= src[x + (y-1)*stride];
                    if(x){
                        lt= src[x - 1 + (y-1)*stride];
                    }
                    if(x + 1 < w){
                        rt= src[x + 1 + (y-1)*stride];
                    }
                }
                if(x){
                    l= src[x - 1 + y*stride];
                    /*if(x > 1){
                        if(orientation==1) ll= src[y + (x-2)*stride];
                        else               ll= src[x - 2 + y*stride];
                    }*/
                }
                if(y+1<h){
                    d= src[x + (y+1)*stride];
                    if(x)         ld= src[x - 1 + (y+1)*stride];
                    if(x + 1 < w) rd= src[x + 1 + (y+1)*stride];
                }
                if(x + 1 < w)
                    r= src[x + 1 + y*stride];

                if(parent){
                    int px= x>>1;
                    int py= y>>1;
                    if(px<b->parent->width && py<b->parent->height) 
                        p= parent[px + py*2*stride];
                }
                HIDE( p, plane)
                if(/*ll|*/l|lt|t|rt|r|rd|ld|d|p|lv){
                    int context= av_log2(/*ABS(ll) + */3*ABS(l) + ABS(lt) + 2*ABS(t) + ABS(rt) + ABS(p)
                                                      +3*ABS(r) + ABS(rd) + 2*ABS(d) + ABS(ld));

                    if(lv){
                        assert(context + 8*av_log2(ABS(lv)) < 512 - 100);
                        if(get_cabac(&s->c, &b->state[99][context + 8*(av_log2(ABS(lv))-plane)])){
                            if(lv<0) v= lv - (1<<plane);
                            else     v= lv + (1<<plane);
                        }else
                            v=lv;
                    }else{
                        v= get_cabac(&s->c, &b->state[ 0][context]) << plane;
                    }
                }else{
                    assert(!lv);
                    if(!run){
                        run= get_symbol(&s->c, b->state[1], 0);
                        v= 1<<plane;
                    }else{
                        run--;
                        v=0;
                    }
                }
                if(v && !lv){
                    int context=    clip(quant3b[l&0xFF] + quant3b[r&0xFF], -1,1)
                                + 3*clip(quant3b[t&0xFF] + quant3b[d&0xFF], -1,1);
                    if(get_cabac(&s->c, &b->state[0][16 + 1 + 3 + context]))
                        v= -v;
                }
                src[x + y*stride]= v;
            }
        }
    }
    return;    
#endif
    if(1){
        int run;
                
        for(y=0; y<b->height; y++)
            memset(&src[y*stride], 0, b->width*sizeof(DWTELEM));

        run= get_symbol2(&s->c, b->state[1], 3);
        for(y=0; y<h; y++){
            for(x=0; x<w; x++){
                int v, p=0;
                int /*ll=0, */l=0, lt=0, t=0, rt=0;

                if(y){
                    t= src[x + (y-1)*stride];
                    if(x){
                        lt= src[x - 1 + (y-1)*stride];
                    }
                    if(x + 1 < w){
                        rt= src[x + 1 + (y-1)*stride];
                    }
                }
                if(x){
                    l= src[x - 1 + y*stride];
                    /*if(x > 1){
                        if(orientation==1) ll= src[y + (x-2)*stride];
                        else               ll= src[x - 2 + y*stride];
                    }*/
                }
                if(parent){
                    int px= x>>1;
                    int py= y>>1;
                    if(px<b->parent->width && py<b->parent->height) 
                        p= parent[px + py*2*stride];
                }
                if(/*ll|*/l|lt|t|rt|p){
                    int context= av_log2(/*ABS(ll) + */3*ABS(l) + ABS(lt) + 2*ABS(t) + ABS(rt) + ABS(p));

                    v=get_cabac(&s->c, &b->state[0][context]);
                }else{
                    if(!run){
                        run= get_symbol2(&s->c, b->state[1], 3);
                        //FIXME optimize this here
                        //FIXME try to store a more naive run
                        v=1;
                    }else{
                        run--;
                        v=0;
                    }
                }
                if(v){
                    int context= av_log2(/*ABS(ll) + */3*ABS(l) + ABS(lt) + 2*ABS(t) + ABS(rt) + ABS(p));
                    v= get_symbol2(&s->c, b->state[context + 2], context-4) + 1;
                    if(get_cabac(&s->c, &b->state[0][16 + 1 + 3 + quant3b[l&0xFF] + 3*quant3b[t&0xFF]]))
                        v= -v;
                    src[x + y*stride]= v;
                }
            }
        }
        if(level+1 == s->spatial_decomposition_count){
            STOP_TIMER("decode_subband")
        }
        
        return;
    }
}

static void reset_contexts(SnowContext *s){
    int plane_index, level, orientation;

    for(plane_index=0; plane_index<2; plane_index++){
        for(level=0; level<s->spatial_decomposition_count; level++){
            for(orientation=level ? 1:0; orientation<4; orientation++){
                memset(s->plane[plane_index].band[level][orientation].state, 0, sizeof(s->plane[plane_index].band[level][orientation].state));
            }
        }
    }
    memset(s->mb_band.state, 0, sizeof(s->mb_band.state));
    memset(s->mv_band[0].state, 0, sizeof(s->mv_band[0].state));
    memset(s->mv_band[1].state, 0, sizeof(s->mv_band[1].state));
    memset(s->header_state, 0, sizeof(s->header_state));
}

static void mc_block(uint8_t *dst, uint8_t *src, uint8_t *tmp, int stride, int b_w, int b_h, int dx, int dy){
    int x, y;

    for(y=0; y < b_h+5; y++){
        for(x=0; x < b_w; x++){
            int a0= src[x     + y*stride];
            int a1= src[x + 1 + y*stride];
            int a2= src[x + 2 + y*stride];
            int a3= src[x + 3 + y*stride];
            int a4= src[x + 4 + y*stride];
            int a5= src[x + 5 + y*stride];
//            int am= 9*(a1+a2) - (a0+a3);
            int am= 20*(a2+a3) - 5*(a1+a4) + (a0+a5);
//            int am= 18*(a2+a3) - 2*(a1+a4);
//             int aL= (-7*a0 + 105*a1 + 35*a2 - 5*a3)>>3;
//             int aR= (-7*a3 + 105*a2 + 35*a1 - 5*a0)>>3;

//            if(b_w==16) am= 8*(a1+a2);

            if(dx<8) tmp[x + y*stride]= (32*a2*( 8-dx) +    am* dx    + 128)>>8;
            else     tmp[x + y*stride]= (   am*(16-dx) + 32*a3*(dx-8) + 128)>>8;

/*            if     (dx< 4) tmp[x + y*stride]= (16*a1*( 4-dx) +    aL* dx     + 32)>>6;
            else if(dx< 8) tmp[x + y*stride]= (   aL*( 8-dx) +    am*(dx- 4) + 32)>>6;
            else if(dx<12) tmp[x + y*stride]= (   am*(12-dx) +    aR*(dx- 8) + 32)>>6;
            else           tmp[x + y*stride]= (   aR*(16-dx) + 16*a2*(dx-12) + 32)>>6;*/
        }
    }
    for(y=0; y < b_h; y++){
        for(x=0; x < b_w; x++){
            int a0= tmp[x +  y     *stride];
            int a1= tmp[x + (y + 1)*stride];
            int a2= tmp[x + (y + 2)*stride];
            int a3= tmp[x + (y + 3)*stride];
            int a4= tmp[x + (y + 4)*stride];
            int a5= tmp[x + (y + 5)*stride];
            int am= 20*(a2+a3) - 5*(a1+a4) + (a0+a5);
//            int am= 18*(a2+a3) - 2*(a1+a4);
/*            int aL= (-7*a0 + 105*a1 + 35*a2 - 5*a3)>>3;
            int aR= (-7*a3 + 105*a2 + 35*a1 - 5*a0)>>3;*/
            
//            if(b_w==16) am= 8*(a1+a2);

            if(dy<8) dst[x + y*stride]= (32*a2*( 8-dy) +    am* dy    + 128)>>8;
            else     dst[x + y*stride]= (   am*(16-dy) + 32*a3*(dy-8) + 128)>>8;

/*            if     (dy< 4) tmp[x + y*stride]= (16*a1*( 4-dy) +    aL* dy     + 32)>>6;
            else if(dy< 8) tmp[x + y*stride]= (   aL*( 8-dy) +    am*(dy- 4) + 32)>>6;
            else if(dy<12) tmp[x + y*stride]= (   am*(12-dy) +    aR*(dy- 8) + 32)>>6;
            else           tmp[x + y*stride]= (   aR*(16-dy) + 16*a2*(dy-12) + 32)>>6;*/
        }
    }
}

#define mcb(dx,dy,b_w)\
static void mc_block ## dx ## dy(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t tmp[stride*(b_w+5)];\
    mc_block(dst, src-2-2*stride, tmp, stride, b_w, b_w, dx, dy);\
}

mcb( 0, 0,16)
mcb( 4, 0,16)
mcb( 8, 0,16)
mcb(12, 0,16)
mcb( 0, 4,16)
mcb( 4, 4,16)
mcb( 8, 4,16)
mcb(12, 4,16)
mcb( 0, 8,16)
mcb( 4, 8,16)
mcb( 8, 8,16)
mcb(12, 8,16)
mcb( 0,12,16)
mcb( 4,12,16)
mcb( 8,12,16)
mcb(12,12,16)

#define mca(dx,dy,b_w)\
static void mc_block_hpel ## dx ## dy(uint8_t *dst, uint8_t *src, int stride, int h){\
    uint8_t tmp[stride*(b_w+5)];\
    assert(h==b_w);\
    mc_block(dst, src-2-2*stride, tmp, stride, b_w, b_w, dx, dy);\
}

mca( 0, 0,16)
mca( 8, 0,16)
mca( 0, 8,16)
mca( 8, 8,16)

static void add_xblock(DWTELEM *dst, uint8_t *src, uint8_t *obmc, int s_x, int s_y, int b_w, int b_h, int mv_x, int mv_y, int w, int h, int dst_stride, int src_stride, int obmc_stride, int mb_type, int add){
    uint8_t tmp[src_stride*(b_h+5)]; //FIXME move to context to gurantee alignment
    int x,y;

    if(s_x<0){
        obmc -= s_x;
        b_w += s_x;
        s_x=0;
    }else if(s_x + b_w > w){
        b_w = w - s_x;
    }
    if(s_y<0){
        obmc -= s_y*obmc_stride;
        b_h += s_y;
        s_y=0;
    }else if(s_y + b_h> h){
        b_h = h - s_y;
    }

    if(b_w<=0 || b_h<=0) return;
    
    dst += s_x + s_y*dst_stride;
    
    if(mb_type==1){
        src += s_x + s_y*src_stride;
        for(y=0; y < b_h; y++){
            for(x=0; x < b_w; x++){
                if(add) dst[x + y*dst_stride] += obmc[x + y*obmc_stride] * 128 * (256/OBMC_MAX);
                else    dst[x + y*dst_stride] -= obmc[x + y*obmc_stride] * 128 * (256/OBMC_MAX);
            }
        }
    }else{
        int dx= mv_x&15;
        int dy= mv_y&15;
//        int dxy= (mv_x&1) + 2*(mv_y&1);

        s_x += (mv_x>>4) - 2;
        s_y += (mv_y>>4) - 2;
        src += s_x + s_y*src_stride;
        //use dsputil
    
        if(   (unsigned)s_x >= w - b_w - 4
           || (unsigned)s_y >= h - b_h - 4){
            ff_emulated_edge_mc(tmp + 32, src, src_stride, b_w+5, b_h+5, s_x, s_y, w, h);
            src= tmp + 32;
        }

        if(mb_type==0){
            mc_block(tmp, src, tmp + 64+8, src_stride, b_w, b_h, dx, dy);
        }else{
            int sum=0;
            for(y=0; y < b_h; y++){
                for(x=0; x < b_w; x++){
                    sum += src[x+  y*src_stride];
                }
            }
            sum= (sum + b_h*b_w/2) / (b_h*b_w);
            for(y=0; y < b_h; y++){
                for(x=0; x < b_w; x++){
                    tmp[x + y*src_stride]= sum; 
                }
            }
        }

        for(y=0; y < b_h; y++){
            for(x=0; x < b_w; x++){
                if(add) dst[x + y*dst_stride] += obmc[x + y*obmc_stride] * tmp[x + y*src_stride] * (256/OBMC_MAX);
                else    dst[x + y*dst_stride] -= obmc[x + y*obmc_stride] * tmp[x + y*src_stride] * (256/OBMC_MAX);
            }
        }
    }
}

static void predict_plane(SnowContext *s, DWTELEM *buf, int plane_index, int add){
    Plane *p= &s->plane[plane_index];
    const int mb_w= s->mb_band.width;
    const int mb_h= s->mb_band.height;
    const int mb_stride= s->mb_band.stride;
    int x, y, mb_x, mb_y;
    int scale      = plane_index ?  s->mv_scale : 2*s->mv_scale;
    int block_w    = plane_index ?  8 : 16;
    uint8_t *obmc  = plane_index ? obmc16 : obmc32;
    int obmc_stride= plane_index ? 16 : 32;
    int ref_stride= s->last_picture.linesize[plane_index];
    uint8_t *ref  = s->last_picture.data[plane_index];
    int w= p->width;
    int h= p->height;
    
if(s->avctx->debug&512){
    for(y=0; y<h; y++){
        for(x=0; x<w; x++){
            if(add) buf[x + y*w]+= 128*256;
            else    buf[x + y*w]-= 128*256;
        }
    }
    
    return;
}
    for(mb_y=-1; mb_y<=mb_h; mb_y++){
        for(mb_x=-1; mb_x<=mb_w; mb_x++){
            int index= clip(mb_x, 0, mb_w-1) + clip(mb_y, 0, mb_h-1)*mb_stride;

            add_xblock(buf, ref, obmc, 
                       block_w*mb_x - block_w/2, 
                       block_w*mb_y - block_w/2,
                       2*block_w, 2*block_w,
                       s->mv_band[0].buf[index]*scale, s->mv_band[1].buf[index]*scale,
                       w, h,
                       w, ref_stride, obmc_stride, 
                       s->mb_band.buf[index], add);

        }
    }
}

static void quantize(SnowContext *s, SubBand *b, DWTELEM *src, int stride, int bias){
    const int level= b->level;
    const int w= b->width;
    const int h= b->height;
    const int qlog= clip(s->qlog + b->qlog, 0, 128);
    const int qmul= qexp[qlog&7]<<(qlog>>3);
    int x,y, thres1, thres2;
    START_TIMER

    assert(QROOT==8);

    if(s->qlog == LOSSLESS_QLOG) return;
 
    bias= bias ? 0 : (3*qmul)>>3;
    thres1= ((qmul - bias)>>QEXPSHIFT) - 1;
    thres2= 2*thres1;
    
    if(!bias){
        for(y=0; y<h; y++){
            for(x=0; x<w; x++){
                int i= src[x + y*stride];
                
                if((unsigned)(i+thres1) > thres2){
                    if(i>=0){
                        i<<= QEXPSHIFT;
                        i/= qmul; //FIXME optimize
                        src[x + y*stride]=  i;
                    }else{
                        i= -i;
                        i<<= QEXPSHIFT;
                        i/= qmul; //FIXME optimize
                        src[x + y*stride]= -i;
                    }
                }else
                    src[x + y*stride]= 0;
            }
        }
    }else{
        for(y=0; y<h; y++){
            for(x=0; x<w; x++){
                int i= src[x + y*stride]; 
                
                if((unsigned)(i+thres1) > thres2){
                    if(i>=0){
                        i<<= QEXPSHIFT;
                        i= (i + bias) / qmul; //FIXME optimize
                        src[x + y*stride]=  i;
                    }else{
                        i= -i;
                        i<<= QEXPSHIFT;
                        i= (i + bias) / qmul; //FIXME optimize
                        src[x + y*stride]= -i;
                    }
                }else
                    src[x + y*stride]= 0;
            }
        }
    }
    if(level+1 == s->spatial_decomposition_count){
//        STOP_TIMER("quantize")
    }
}

static void dequantize(SnowContext *s, SubBand *b, DWTELEM *src, int stride){
    const int level= b->level;
    const int w= b->width;
    const int h= b->height;
    const int qlog= clip(s->qlog + b->qlog, 0, 128);
    const int qmul= qexp[qlog&7]<<(qlog>>3);
    const int qadd= (s->qbias*qmul)>>QBIAS_SHIFT;
    int x,y;
    
    if(s->qlog == LOSSLESS_QLOG) return;
    
    assert(QROOT==8);

    for(y=0; y<h; y++){
        for(x=0; x<w; x++){
            int i= src[x + y*stride];
            if(i<0){
                src[x + y*stride]= -((-i*qmul + qadd)>>(QEXPSHIFT)); //FIXME try different bias
            }else if(i>0){
                src[x + y*stride]=  (( i*qmul + qadd)>>(QEXPSHIFT));
            }
        }
    }
}

static void decorrelate(SnowContext *s, SubBand *b, DWTELEM *src, int stride, int inverse, int use_median){
    const int w= b->width;
    const int h= b->height;
    int x,y;
    
    for(y=h-1; y>=0; y--){
        for(x=w-1; x>=0; x--){
            int i= x + y*stride;
            
            if(x){
                if(use_median){
                    if(y && x+1<w) src[i] -= mid_pred(src[i - 1], src[i - stride], src[i - stride + 1]);
                    else  src[i] -= src[i - 1];
                }else{
                    if(y) src[i] -= mid_pred(src[i - 1], src[i - stride], src[i - 1] + src[i - stride] - src[i - 1 - stride]);
                    else  src[i] -= src[i - 1];
                }
            }else{
                if(y) src[i] -= src[i - stride];
            }
        }
    }
}

static void correlate(SnowContext *s, SubBand *b, DWTELEM *src, int stride, int inverse, int use_median){
    const int w= b->width;
    const int h= b->height;
    int x,y;
    
    for(y=0; y<h; y++){
        for(x=0; x<w; x++){
            int i= x + y*stride;
            
            if(x){
                if(use_median){
                    if(y && x+1<w) src[i] += mid_pred(src[i - 1], src[i - stride], src[i - stride + 1]);
                    else  src[i] += src[i - 1];
                }else{
                    if(y) src[i] += mid_pred(src[i - 1], src[i - stride], src[i - 1] + src[i - stride] - src[i - 1 - stride]);
                    else  src[i] += src[i - 1];
                }
            }else{
                if(y) src[i] += src[i - stride];
            }
        }
    }
}

static void encode_header(SnowContext *s){
    int plane_index, level, orientation;

    put_cabac(&s->c, s->header_state, s->keyframe); // state clearing stuff?
    if(s->keyframe){
        put_symbol(&s->c, s->header_state, s->version, 0);
        put_symbol(&s->c, s->header_state, s->temporal_decomposition_type, 0);
        put_symbol(&s->c, s->header_state, s->temporal_decomposition_count, 0);
        put_symbol(&s->c, s->header_state, s->spatial_decomposition_count, 0);
        put_symbol(&s->c, s->header_state, s->colorspace_type, 0);
        put_symbol(&s->c, s->header_state, s->b_width, 0);
        put_symbol(&s->c, s->header_state, s->b_height, 0);
        put_symbol(&s->c, s->header_state, s->chroma_h_shift, 0);
        put_symbol(&s->c, s->header_state, s->chroma_v_shift, 0);
        put_cabac(&s->c, s->header_state, s->spatial_scalability);
//        put_cabac(&s->c, s->header_state, s->rate_scalability);

        for(plane_index=0; plane_index<2; plane_index++){
            for(level=0; level<s->spatial_decomposition_count; level++){
                for(orientation=level ? 1:0; orientation<4; orientation++){
                    if(orientation==2) continue;
                    put_symbol(&s->c, s->header_state, s->plane[plane_index].band[level][orientation].qlog, 1);
                }
            }
        }
    }
    put_symbol(&s->c, s->header_state, s->spatial_decomposition_type, 0);
    put_symbol(&s->c, s->header_state, s->qlog, 1); 
    put_symbol(&s->c, s->header_state, s->mv_scale, 0); 
    put_symbol(&s->c, s->header_state, s->qbias, 1);
}

static int decode_header(SnowContext *s){
    int plane_index, level, orientation;

    s->keyframe= get_cabac(&s->c, s->header_state);
    if(s->keyframe){
        s->version= get_symbol(&s->c, s->header_state, 0);
        if(s->version>0){
            av_log(s->avctx, AV_LOG_ERROR, "version %d not supported", s->version);
            return -1;
        }
        s->temporal_decomposition_type= get_symbol(&s->c, s->header_state, 0);
        s->temporal_decomposition_count= get_symbol(&s->c, s->header_state, 0);
        s->spatial_decomposition_count= get_symbol(&s->c, s->header_state, 0);
        s->colorspace_type= get_symbol(&s->c, s->header_state, 0);
        s->b_width= get_symbol(&s->c, s->header_state, 0);
        s->b_height= get_symbol(&s->c, s->header_state, 0);
        s->chroma_h_shift= get_symbol(&s->c, s->header_state, 0);
        s->chroma_v_shift= get_symbol(&s->c, s->header_state, 0);
        s->spatial_scalability= get_cabac(&s->c, s->header_state);
//        s->rate_scalability= get_cabac(&s->c, s->header_state);

        for(plane_index=0; plane_index<3; plane_index++){
            for(level=0; level<s->spatial_decomposition_count; level++){
                for(orientation=level ? 1:0; orientation<4; orientation++){
                    int q;
                    if     (plane_index==2) q= s->plane[1].band[level][orientation].qlog;
                    else if(orientation==2) q= s->plane[plane_index].band[level][1].qlog;
                    else                    q= get_symbol(&s->c, s->header_state, 1);
                    s->plane[plane_index].band[level][orientation].qlog= q;
                }
            }
        }
    }
    
    s->spatial_decomposition_type= get_symbol(&s->c, s->header_state, 0);
    if(s->spatial_decomposition_type > 2){
        av_log(s->avctx, AV_LOG_ERROR, "spatial_decomposition_type %d not supported", s->spatial_decomposition_type);
        return -1;
    }
    
    s->qlog= get_symbol(&s->c, s->header_state, 1);
    s->mv_scale= get_symbol(&s->c, s->header_state, 0);
    s->qbias= get_symbol(&s->c, s->header_state, 1);

    return 0;
}

static int common_init(AVCodecContext *avctx){
    SnowContext *s = avctx->priv_data;
    int width, height;
    int level, orientation, plane_index, dec;

    s->avctx= avctx;
        
    dsputil_init(&s->dsp, avctx);

#define mcf(dx,dy)\
    s->dsp.put_qpel_pixels_tab       [0][dy+dx/4]=\
    s->dsp.put_no_rnd_qpel_pixels_tab[0][dy+dx/4]=\
        mc_block ## dx ## dy;

    mcf( 0, 0)
    mcf( 4, 0)
    mcf( 8, 0)
    mcf(12, 0)
    mcf( 0, 4)
    mcf( 4, 4)
    mcf( 8, 4)
    mcf(12, 4)
    mcf( 0, 8)
    mcf( 4, 8)
    mcf( 8, 8)
    mcf(12, 8)
    mcf( 0,12)
    mcf( 4,12)
    mcf( 8,12)
    mcf(12,12)

#define mcfh(dx,dy)\
    s->dsp.put_pixels_tab       [0][dy/4+dx/8]=\
    s->dsp.put_no_rnd_pixels_tab[0][dy/4+dx/8]=\
        mc_block_hpel ## dx ## dy;

    mcfh(0, 0)
    mcfh(8, 0)
    mcfh(0, 8)
    mcfh(8, 8)
        
    dec= s->spatial_decomposition_count= 5;
    s->spatial_decomposition_type= avctx->prediction_method; //FIXME add decorrelator type r transform_type
    
    s->chroma_h_shift= 1; //FIXME XXX
    s->chroma_v_shift= 1;
    
//    dec += FFMAX(s->chroma_h_shift, s->chroma_v_shift);
    
    s->b_width = (s->avctx->width +(1<<dec)-1)>>dec;
    s->b_height= (s->avctx->height+(1<<dec)-1)>>dec;
    
    s->spatial_dwt_buffer= av_mallocz(s->b_width*s->b_height*sizeof(DWTELEM)<<(2*dec));
    s->pred_buffer= av_mallocz(s->b_width*s->b_height*sizeof(DWTELEM)<<(2*dec));
    
    s->mv_scale= (s->avctx->flags & CODEC_FLAG_QPEL) ? 2 : 4;
    
    for(plane_index=0; plane_index<3; plane_index++){    
        int w= s->avctx->width;
        int h= s->avctx->height;

        if(plane_index){
            w>>= s->chroma_h_shift;
            h>>= s->chroma_v_shift;
        }
        s->plane[plane_index].width = w;
        s->plane[plane_index].height= h;
//av_log(NULL, AV_LOG_DEBUG, "%d %d\n", w, h);
        for(level=s->spatial_decomposition_count-1; level>=0; level--){
            for(orientation=level ? 1 : 0; orientation<4; orientation++){
                SubBand *b= &s->plane[plane_index].band[level][orientation];
                
                b->buf= s->spatial_dwt_buffer;
                b->level= level;
                b->stride= s->plane[plane_index].width << (s->spatial_decomposition_count - level);
                b->width = (w + !(orientation&1))>>1;
                b->height= (h + !(orientation>1))>>1;
                
                if(orientation&1) b->buf += (w+1)>>1;
                if(orientation>1) b->buf += b->stride>>1;
                
                if(level)
                    b->parent= &s->plane[plane_index].band[level-1][orientation];
            }
            w= (w+1)>>1;
            h= (h+1)>>1;
        }
    }
    
    //FIXME init_subband() ?
    s->mb_band.stride= s->mv_band[0].stride= s->mv_band[1].stride=
    s->mb_band.width = s->mv_band[0].width = s->mv_band[1].width = (s->avctx->width + 15)>>4;
    s->mb_band.height= s->mv_band[0].height= s->mv_band[1].height= (s->avctx->height+ 15)>>4;
    s->mb_band   .buf= av_mallocz(s->mb_band   .stride * s->mb_band   .height*sizeof(DWTELEM));
    s->mv_band[0].buf= av_mallocz(s->mv_band[0].stride * s->mv_band[0].height*sizeof(DWTELEM));
    s->mv_band[1].buf= av_mallocz(s->mv_band[1].stride * s->mv_band[1].height*sizeof(DWTELEM));

    reset_contexts(s);
/*    
    width= s->width= avctx->width;
    height= s->height= avctx->height;
    
    assert(width && height);
*/
    s->avctx->get_buffer(s->avctx, &s->mconly_picture);
    
    return 0;
}


static void calculate_vissual_weight(SnowContext *s, Plane *p){
    int width = p->width;
    int height= p->height;
    int i, level, orientation, x, y;

    for(level=0; level<s->spatial_decomposition_count; level++){
        for(orientation=level ? 1 : 0; orientation<4; orientation++){
            SubBand *b= &p->band[level][orientation];
            DWTELEM *buf= b->buf;
            int64_t error=0;
            
            memset(s->spatial_dwt_buffer, 0, sizeof(int)*width*height);
            buf[b->width/2 + b->height/2*b->stride]= 256*256;
            ff_spatial_idwt(s->spatial_dwt_buffer, width, height, width, s->spatial_decomposition_type, s->spatial_decomposition_count);
            for(y=0; y<height; y++){
                for(x=0; x<width; x++){
                    int64_t d= s->spatial_dwt_buffer[x + y*width];
                    error += d*d;
                }
            }

            b->qlog= (int)(log(352256.0/sqrt(error)) / log(pow(2.0, 1.0/QROOT))+0.5);
//            av_log(NULL, AV_LOG_DEBUG, "%d %d %d\n", level, orientation, b->qlog/*, sqrt(error)*/);
        }
    }
}

static int encode_init(AVCodecContext *avctx)
{
    SnowContext *s = avctx->priv_data;
    int i;
    int level, orientation, plane_index;

    if(avctx->strict_std_compliance >= 0){
        av_log(avctx, AV_LOG_ERROR, "this codec is under development, files encoded with it wont be decodeable with future versions!!!\n"
               "use vstrict=-1 to use it anyway\n");
        return -1;
    }
 
    common_init(avctx);
 
    s->version=0;
    
    s->m.me.scratchpad= av_mallocz((avctx->width+64)*2*16*2*sizeof(uint8_t));
    s->m.me.map       = av_mallocz(ME_MAP_SIZE*sizeof(uint32_t));
    s->m.me.score_map = av_mallocz(ME_MAP_SIZE*sizeof(uint32_t));
    s->mb_type        = av_mallocz((s->mb_band.width+1)*s->mb_band.height*sizeof(int16_t));
    s->mb_mean        = av_mallocz((s->mb_band.width+1)*s->mb_band.height*sizeof(int8_t ));
    s->dummy          = av_mallocz((s->mb_band.width+1)*s->mb_band.height*sizeof(int32_t));
    h263_encode_init(&s->m); //mv_penalty

    for(plane_index=0; plane_index<3; plane_index++){
        calculate_vissual_weight(s, &s->plane[plane_index]);
    }
    
    
    avctx->coded_frame= &s->current_picture;
    switch(avctx->pix_fmt){
//    case PIX_FMT_YUV444P:
//    case PIX_FMT_YUV422P:
    case PIX_FMT_YUV420P:
    case PIX_FMT_GRAY8:
//    case PIX_FMT_YUV411P:
//    case PIX_FMT_YUV410P:
        s->colorspace_type= 0;
        break;
/*    case PIX_FMT_RGBA32:
        s->colorspace= 1;
        break;*/
    default:
        av_log(avctx, AV_LOG_ERROR, "format not supported\n");
        return -1;
    }
//    avcodec_get_chroma_sub_sample(avctx->pix_fmt, &s->chroma_h_shift, &s->chroma_v_shift);
    s->chroma_h_shift= 1;
    s->chroma_v_shift= 1;
    return 0;
}

static int frame_start(SnowContext *s){
   AVFrame tmp;

   if(s->keyframe)
        reset_contexts(s);
 
    tmp= s->last_picture;
    s->last_picture= s->current_picture;
    s->current_picture= tmp;
    
    s->current_picture.reference= 1;
    if(s->avctx->get_buffer(s->avctx, &s->current_picture) < 0){
        av_log(s->avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    
    return 0;
}

static int encode_frame(AVCodecContext *avctx, unsigned char *buf, int buf_size, void *data){
    SnowContext *s = avctx->priv_data;
    CABACContext * const c= &s->c;
    AVFrame *pict = data;
    const int width= s->avctx->width;
    const int height= s->avctx->height;
    int used_count= 0;
    int log2_threshold, level, orientation, plane_index, i;

    ff_init_cabac_encoder(c, buf, buf_size);
    ff_init_cabac_states(c, ff_h264_lps_range, ff_h264_mps_state, ff_h264_lps_state, 64);
    
    s->input_picture = *pict;

    memset(s->header_state, 0, sizeof(s->header_state));

    s->keyframe=avctx->gop_size==0 || avctx->frame_number % avctx->gop_size == 0;
    pict->pict_type= s->keyframe ? FF_I_TYPE : FF_P_TYPE;
    
    if(pict->quality){
        s->qlog= rint(QROOT*log(pict->quality / (float)FF_QP2LAMBDA)/log(2));
        //<64 >60
        s->qlog += 61;
    }else{
        s->qlog= LOSSLESS_QLOG;
    }

    for(i=0; i<s->mb_band.stride * s->mb_band.height; i++){
        s->mb_band.buf[i]= s->keyframe;
    }
    
    frame_start(s);

    if(pict->pict_type == P_TYPE){
        int block_width = (width +15)>>4;
        int block_height= (height+15)>>4;
        int stride= s->current_picture.linesize[0];
        uint8_t *src_plane= s->input_picture.data[0];
        int src_stride= s->input_picture.linesize[0];
        int x,y;
        
        assert(s->current_picture.data[0]);
        assert(s->last_picture.data[0]);
     
        s->m.avctx= s->avctx;
        s->m.current_picture.data[0]= s->current_picture.data[0];
        s->m.   last_picture.data[0]= s->   last_picture.data[0];
        s->m.    new_picture.data[0]= s->  input_picture.data[0];
        s->m.current_picture_ptr= &s->m.current_picture;
        s->m.   last_picture_ptr= &s->m.   last_picture;
        s->m.linesize=
        s->m.   last_picture.linesize[0]=
        s->m.    new_picture.linesize[0]=
        s->m.current_picture.linesize[0]= stride;
        s->m.width = width;
        s->m.height= height;
        s->m.mb_width = block_width;
        s->m.mb_height= block_height;
        s->m.mb_stride=   s->m.mb_width+1;
        s->m.b8_stride= 2*s->m.mb_width+1;
        s->m.f_code=1;
        s->m.pict_type= pict->pict_type;
        s->m.me_method= s->avctx->me_method;
        s->m.me.scene_change_score=0;
        s->m.flags= s->avctx->flags;
        s->m.quarter_sample= (s->avctx->flags & CODEC_FLAG_QPEL)!=0;
        s->m.out_format= FMT_H263;
        s->m.unrestricted_mv= 1;

        s->m.lambda= pict->quality * 3/2; //FIXME bug somewhere else
        s->m.qscale= (s->m.lambda*139 + FF_LAMBDA_SCALE*64) >> (FF_LAMBDA_SHIFT + 7);
        s->m.lambda2= (s->m.lambda*s->m.lambda + FF_LAMBDA_SCALE/2) >> FF_LAMBDA_SHIFT;

        if(!s->motion_val8){
            s->motion_val8 = av_mallocz(s->m.b8_stride*block_height*2*2*sizeof(int16_t));
            s->motion_val16= av_mallocz(s->m.mb_stride*block_height*2*sizeof(int16_t));
        }
        
        s->m.mb_type= s->mb_type;
        
        //dummies, to avoid segfaults
        s->m.current_picture.mb_mean  = s->mb_mean;
        s->m.current_picture.mb_var   = (int16_t*)s->dummy;
        s->m.current_picture.mc_mb_var= (int16_t*)s->dummy;
        s->m.current_picture.mb_type  = s->dummy;
        
        s->m.current_picture.motion_val[0]= s->motion_val8;
        s->m.p_mv_table= s->motion_val16;
        s->m.dsp= s->dsp; //move
        ff_init_me(&s->m);
    
        
        s->m.me.pre_pass=1;
        s->m.me.dia_size= s->avctx->pre_dia_size;
        s->m.first_slice_line=1;
        for(y= block_height-1; y >= 0; y--) {
            uint8_t src[stride*16];

            s->m.new_picture.data[0]= src - y*16*stride; //ugly
            s->m.mb_y= y;
            for(i=0; i<16 && i + 16*y<height; i++){
                memcpy(&src[i*stride], &src_plane[(i+16*y)*src_stride], width);
                for(x=width; x<16*block_width; x++)
                    src[i*stride+x]= src[i*stride+x-1];
            }
            for(; i<16 && i + 16*y<16*block_height; i++)
                memcpy(&src[i*stride], &src[(i-1)*stride], 16*block_width);

            for(x=block_width-1; x >=0 ;x--) {
                s->m.mb_x= x;
                ff_init_block_index(&s->m);
                ff_update_block_index(&s->m);
                ff_pre_estimate_p_frame_motion(&s->m, x, y);
            }
            s->m.first_slice_line=0;
        }        
        s->m.me.pre_pass=0;
        
        
        s->m.me.dia_size= s->avctx->dia_size;
        s->m.first_slice_line=1;
        for (y = 0; y < block_height; y++) {
            uint8_t src[stride*16];

            s->m.new_picture.data[0]= src - y*16*stride; //ugly
            s->m.mb_y= y;
            
            assert(width <= stride);
            assert(width <= 16*block_width);
    
            for(i=0; i<16 && i + 16*y<height; i++){
                memcpy(&src[i*stride], &src_plane[(i+16*y)*src_stride], width);
                for(x=width; x<16*block_width; x++)
                    src[i*stride+x]= src[i*stride+x-1];
            }
            for(; i<16 && i + 16*y<16*block_height; i++)
                memcpy(&src[i*stride], &src[(i-1)*stride], 16*block_width);
    
            for (x = 0; x < block_width; x++) {
                int mb_xy= x + y*(s->mb_band.stride);
                s->m.mb_x= x;
                ff_init_block_index(&s->m);
                ff_update_block_index(&s->m);
                
                ff_estimate_p_frame_motion(&s->m, x, y);
                
                s->mb_band   .buf[mb_xy]= (s->m.mb_type[x + y*s->m.mb_stride]&CANDIDATE_MB_TYPE_INTER)
                 ? 0 : 2;
                s->mv_band[0].buf[mb_xy]= s->motion_val16[x + y*s->m.mb_stride][0];
                s->mv_band[1].buf[mb_xy]= s->motion_val16[x + y*s->m.mb_stride][1];
                
                if(s->mb_band   .buf[x + y*(s->mb_band.stride)]==2 && 0){
                    int dc0=128, dc1=128, dc, dc2, dir;
                    int offset= (s->avctx->flags & CODEC_FLAG_QPEL) ? 64 : 32;
                    
                    dc       =s->mb_mean[x +  y   *s->m.mb_stride    ];
                    if(x) dc0=s->mb_mean[x +  y   *s->m.mb_stride - 1];
                    if(y) dc1=s->mb_mean[x + (y-1)*s->m.mb_stride    ];
                    dc2= (dc0+dc1)>>1;
#if 0
                    if     (ABS(dc0 - dc) < ABS(dc1 - dc) && ABS(dc0 - dc) < ABS(dc2 - dc))
                        dir= 1;
                    else if(ABS(dc0 - dc) >=ABS(dc1 - dc) && ABS(dc1 - dc) < ABS(dc2 - dc))
                        dir=-1;
                    else
                        dir=0;
#endif                    
                    if(ABS(dc0 - dc) < ABS(dc1 - dc) && x){
                        s->mv_band[0].buf[mb_xy]= s->mv_band[0].buf[x + y*(s->mb_band.stride)-1] - offset;
                        s->mv_band[1].buf[mb_xy]= s->mv_band[1].buf[x + y*(s->mb_band.stride)-1];
                        s->mb_mean[x +  y   *s->m.mb_stride    ]= dc0;
                    }else if(y){
                        s->mv_band[0].buf[mb_xy]= s->mv_band[0].buf[x + (y-1)*(s->mb_band.stride)];
                        s->mv_band[1].buf[mb_xy]= s->mv_band[1].buf[x + (y-1)*(s->mb_band.stride)] - offset;
                        s->mb_mean[x +  y   *s->m.mb_stride    ]= dc1;
                    }
                }
//                s->mb_band   .buf[x + y*(s->mb_band.stride)]=1; //FIXME intra only test
            }
            s->m.first_slice_line=0;
        }
        assert(s->m.pict_type == P_TYPE);
        if(s->m.me.scene_change_score > s->avctx->scenechange_threshold){
            s->m.pict_type= 
            pict->pict_type =I_TYPE;
            for(i=0; i<s->mb_band.stride * s->mb_band.height; i++){
                s->mb_band.buf[i]= 1;
                s->mv_band[0].buf[i]=
                s->mv_band[1].buf[i]= 0;
            }
    //printf("Scene change detected, encoding as I Frame %d %d\n", s->current_picture.mb_var_sum, s->current_picture.mc_mb_var_sum);
        }        
    }
        
    s->m.first_slice_line=1;
    
    s->qbias= pict->pict_type == P_TYPE ? 2 : 0;

    encode_header(s);
    
    decorrelate(s, &s->mb_band   , s->mb_band   .buf, s->mb_band   .stride, 0, 1);
    decorrelate(s, &s->mv_band[0], s->mv_band[0].buf, s->mv_band[0].stride, 0, 1);
    decorrelate(s, &s->mv_band[1], s->mv_band[1].buf, s->mv_band[1].stride, 0 ,1);
    encode_subband(s, &s->mb_band   , s->mb_band   .buf, NULL, s->mb_band   .stride, 0);
    encode_subband(s, &s->mv_band[0], s->mv_band[0].buf, NULL, s->mv_band[0].stride, 0);
    encode_subband(s, &s->mv_band[1], s->mv_band[1].buf, NULL, s->mv_band[1].stride, 0);
    
//FIXME avoid this
    correlate(s, &s->mb_band   , s->mb_band   .buf, s->mb_band   .stride, 1, 1);
    correlate(s, &s->mv_band[0], s->mv_band[0].buf, s->mv_band[0].stride, 1, 1);
    correlate(s, &s->mv_band[1], s->mv_band[1].buf, s->mv_band[1].stride, 1, 1);
    
    for(plane_index=0; plane_index<3; plane_index++){
        Plane *p= &s->plane[plane_index];
        int w= p->width;
        int h= p->height;
        int x, y;
        int bits= put_bits_count(&s->c.pb);

        //FIXME optimize
     if(pict->data[plane_index]) //FIXME gray hack
        for(y=0; y<h; y++){
            for(x=0; x<w; x++){
                s->spatial_dwt_buffer[y*w + x]= pict->data[plane_index][y*pict->linesize[plane_index] + x]<<8;
            }
        }
        predict_plane(s, s->spatial_dwt_buffer, plane_index, 0);
        if(s->qlog == LOSSLESS_QLOG){
            for(y=0; y<h; y++){
                for(x=0; x<w; x++){
                    s->spatial_dwt_buffer[y*w + x]= (s->spatial_dwt_buffer[y*w + x] + 127)>>8;
                }
            }
        }
 
        ff_spatial_dwt(s->spatial_dwt_buffer, w, h, w, s->spatial_decomposition_type, s->spatial_decomposition_count);

        for(level=0; level<s->spatial_decomposition_count; level++){
            for(orientation=level ? 1 : 0; orientation<4; orientation++){
                SubBand *b= &p->band[level][orientation];
                
                quantize(s, b, b->buf, b->stride, s->qbias);
                if(orientation==0)
                    decorrelate(s, b, b->buf, b->stride, pict->pict_type == P_TYPE, 0);
                encode_subband(s, b, b->buf, b->parent ? b->parent->buf : NULL, b->stride, orientation);
                assert(b->parent==NULL || b->parent->stride == b->stride*2);
                if(orientation==0)
                    correlate(s, b, b->buf, b->stride, 1, 0);
            }
        }
//        av_log(NULL, AV_LOG_DEBUG, "plane:%d bits:%d\n", plane_index, put_bits_count(&s->c.pb) - bits);

        for(level=0; level<s->spatial_decomposition_count; level++){
            for(orientation=level ? 1 : 0; orientation<4; orientation++){
                SubBand *b= &p->band[level][orientation];

                dequantize(s, b, b->buf, b->stride);
            }
        }

        ff_spatial_idwt(s->spatial_dwt_buffer, w, h, w, s->spatial_decomposition_type, s->spatial_decomposition_count);
        if(s->qlog == LOSSLESS_QLOG){
            for(y=0; y<h; y++){
                for(x=0; x<w; x++){
                    s->spatial_dwt_buffer[y*w + x]<<=8;
                }
            }
        }
        predict_plane(s, s->spatial_dwt_buffer, plane_index, 1);
        //FIXME optimize
        for(y=0; y<h; y++){
            for(x=0; x<w; x++){
                int v= (s->spatial_dwt_buffer[y*w + x]+128)>>8;
                if(v&(~255)) v= ~(v>>31);
                s->current_picture.data[plane_index][y*s->current_picture.linesize[plane_index] + x]= v;
            }
        }
        if(s->avctx->flags&CODEC_FLAG_PSNR){
            int64_t error= 0;
            
    if(pict->data[plane_index]) //FIXME gray hack
            for(y=0; y<h; y++){
                for(x=0; x<w; x++){
                    int d= s->current_picture.data[plane_index][y*s->current_picture.linesize[plane_index] + x] - pict->data[plane_index][y*pict->linesize[plane_index] + x];
                    error += d*d;
                }
            }
            s->avctx->error[plane_index] += error;
            s->avctx->error[3] += error;
        }
    }

    if(s->last_picture.data[0])
        avctx->release_buffer(avctx, &s->last_picture);

    emms_c();
    
    return put_cabac_terminate(c, 1);
}

static void common_end(SnowContext *s){
    av_freep(&s->spatial_dwt_buffer);
    av_freep(&s->mb_band.buf);
    av_freep(&s->mv_band[0].buf);
    av_freep(&s->mv_band[1].buf);

    av_freep(&s->m.me.scratchpad);    
    av_freep(&s->m.me.map);
    av_freep(&s->m.me.score_map);
    av_freep(&s->mb_type);
    av_freep(&s->mb_mean);
    av_freep(&s->dummy);
    av_freep(&s->motion_val8);
    av_freep(&s->motion_val16);
}

static int encode_end(AVCodecContext *avctx)
{
    SnowContext *s = avctx->priv_data;

    common_end(s);

    return 0;
}

static int decode_init(AVCodecContext *avctx)
{
//    SnowContext *s = avctx->priv_data;

    common_init(avctx);
    
    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size, uint8_t *buf, int buf_size){
    SnowContext *s = avctx->priv_data;
    CABACContext * const c= &s->c;
    const int width= s->avctx->width;
    const int height= s->avctx->height;
    int bytes_read;
    AVFrame *picture = data;
    int log2_threshold, level, orientation, plane_index;
    

    /* no supplementary picture */
    if (buf_size == 0)
        return 0;

    ff_init_cabac_decoder(c, buf, buf_size);
    ff_init_cabac_states(c, ff_h264_lps_range, ff_h264_mps_state, ff_h264_lps_state, 64);

    memset(s->header_state, 0, sizeof(s->header_state));

    s->current_picture.pict_type= FF_I_TYPE; //FIXME I vs. P
    decode_header(s);

    frame_start(s);
    //keyframe flag dupliaction mess FIXME
    if(avctx->debug&FF_DEBUG_PICT_INFO)
        av_log(avctx, AV_LOG_ERROR, "keyframe:%d qlog:%d\n", s->keyframe, s->qlog);
    
    decode_subband(s, &s->mb_band   , s->mb_band   .buf, NULL, s->mb_band   .stride, 0);
    decode_subband(s, &s->mv_band[0], s->mv_band[0].buf, NULL, s->mv_band[0].stride, 0);
    decode_subband(s, &s->mv_band[1], s->mv_band[1].buf, NULL, s->mv_band[1].stride, 0);
    correlate(s, &s->mb_band   , s->mb_band   .buf, s->mb_band   .stride, 1, 1);
    correlate(s, &s->mv_band[0], s->mv_band[0].buf, s->mv_band[0].stride, 1, 1);
    correlate(s, &s->mv_band[1], s->mv_band[1].buf, s->mv_band[1].stride, 1, 1);

    for(plane_index=0; plane_index<3; plane_index++){
        Plane *p= &s->plane[plane_index];
        int w= p->width;
        int h= p->height;
        int x, y;
        
if(s->avctx->debug&2048){
        memset(s->spatial_dwt_buffer, 0, sizeof(DWTELEM)*w*h);
        predict_plane(s, s->spatial_dwt_buffer, plane_index, 1);

        for(y=0; y<h; y++){
            for(x=0; x<w; x++){
                int v= (s->spatial_dwt_buffer[y*w + x]+128)>>8;
                if(v&(~255)) v= ~(v>>31);
                s->mconly_picture.data[plane_index][y*s->mconly_picture.linesize[plane_index] + x]= v;
            }
        }
}
        for(level=0; level<s->spatial_decomposition_count; level++){
            for(orientation=level ? 1 : 0; orientation<4; orientation++){
                SubBand *b= &p->band[level][orientation];

                decode_subband(s, b, b->buf, b->parent ? b->parent->buf : NULL, b->stride, orientation);
                if(orientation==0)
                    correlate(s, b, b->buf, b->stride, 1, 0);
            }
        }
if(!(s->avctx->debug&1024))
        for(level=0; level<s->spatial_decomposition_count; level++){
            for(orientation=level ? 1 : 0; orientation<4; orientation++){
                SubBand *b= &p->band[level][orientation];

                dequantize(s, b, b->buf, b->stride);
            }
        }

        ff_spatial_idwt(s->spatial_dwt_buffer, w, h, w, s->spatial_decomposition_type, s->spatial_decomposition_count);
        if(s->qlog == LOSSLESS_QLOG){
            for(y=0; y<h; y++){
                for(x=0; x<w; x++){
                    s->spatial_dwt_buffer[y*w + x]<<=8;
                }
            }
        }
        predict_plane(s, s->spatial_dwt_buffer, plane_index, 1);

        //FIXME optimize
        for(y=0; y<h; y++){
            for(x=0; x<w; x++){
                int v= (s->spatial_dwt_buffer[y*w + x]+128)>>8;
                if(v&(~255)) v= ~(v>>31);
                s->current_picture.data[plane_index][y*s->current_picture.linesize[plane_index] + x]= v;
            }
        }
    }
            
    emms_c();

    if(s->last_picture.data[0])
        avctx->release_buffer(avctx, &s->last_picture);

if(!(s->avctx->debug&2048))        
    *picture= s->current_picture;
else
    *picture= s->mconly_picture;
    
    *data_size = sizeof(AVFrame);
    
    bytes_read= get_cabac_terminate(c);
    if(bytes_read ==0) av_log(s->avctx, AV_LOG_ERROR, "error at end of frame\n");

    return bytes_read;
}

static int decode_end(AVCodecContext *avctx)
{
    SnowContext *s = avctx->priv_data;

    common_end(s);

    return 0;
}

AVCodec snow_decoder = {
    "snow",
    CODEC_TYPE_VIDEO,
    CODEC_ID_SNOW,
    sizeof(SnowContext),
    decode_init,
    NULL,
    decode_end,
    decode_frame,
    0 /*CODEC_CAP_DR1*/ /*| CODEC_CAP_DRAW_HORIZ_BAND*/,
    NULL
};

AVCodec snow_encoder = {
    "snow",
    CODEC_TYPE_VIDEO,
    CODEC_ID_SNOW,
    sizeof(SnowContext),
    encode_init,
    encode_frame,
    encode_end,
};


#if 0
#undef malloc
#undef free
#undef printf

int main(){
    int width=256;
    int height=256;
    int buffer[2][width*height];
    SnowContext s;
    int i;
    s.spatial_decomposition_count=6;
    s.spatial_decomposition_type=1;
    
    printf("testing 5/3 DWT\n");
    for(i=0; i<width*height; i++)
        buffer[0][i]= buffer[1][i]= random()%54321 - 12345;
    
    ff_spatial_dwt(buffer[0], width, height, width, s->spatial_decomposition_type, s->spatial_decomposition_count);
    ff_spatial_idwt(buffer[0], width, height, width, s->spatial_decomposition_type, s->spatial_decomposition_count);
    
    for(i=0; i<width*height; i++)
        if(buffer[0][i]!= buffer[1][i]) printf("fsck: %d %d %d\n",i, buffer[0][i], buffer[1][i]);

    printf("testing 9/7 DWT\n");
    s.spatial_decomposition_type=0;
    for(i=0; i<width*height; i++)
        buffer[0][i]= buffer[1][i]= random()%54321 - 12345;
    
    ff_spatial_dwt(buffer[0], width, height, width, s->spatial_decomposition_type, s->spatial_decomposition_count);
    ff_spatial_idwt(buffer[0], width, height, width, s->spatial_decomposition_type, s->spatial_decomposition_count);
    
    for(i=0; i<width*height; i++)
        if(buffer[0][i]!= buffer[1][i]) printf("fsck: %d %d %d\n",i, buffer[0][i], buffer[1][i]);
        
    printf("testing AC coder\n");
    memset(s.header_state, 0, sizeof(s.header_state));
    ff_init_cabac_encoder(&s.c, buffer[0], 256*256);
    ff_init_cabac_states(&s.c, ff_h264_lps_range, ff_h264_mps_state, ff_h264_lps_state, 64);
        
    for(i=-256; i<256; i++){
START_TIMER
        put_symbol(&s.c, s.header_state, i*i*i/3*ABS(i), 1);
STOP_TIMER("put_symbol")
    }
    put_cabac_terminate(&s.c, 1);

    memset(s.header_state, 0, sizeof(s.header_state));
    ff_init_cabac_decoder(&s.c, buffer[0], 256*256);
    ff_init_cabac_states(&s.c, ff_h264_lps_range, ff_h264_mps_state, ff_h264_lps_state, 64);
    
    for(i=-256; i<256; i++){
        int j;
START_TIMER
        j= get_symbol(&s.c, s.header_state, 1);
STOP_TIMER("get_symbol")
        if(j!=i*i*i/3*ABS(i)) printf("fsck: %d != %d\n", i, j);
    }
{
int level, orientation, x, y;
int64_t errors[8][4];
int64_t g=0;

    memset(errors, 0, sizeof(errors));
    s.spatial_decomposition_count=3;
    s.spatial_decomposition_type=0;
    for(level=0; level<s.spatial_decomposition_count; level++){
        for(orientation=level ? 1 : 0; orientation<4; orientation++){
            int w= width  >> (s.spatial_decomposition_count-level);
            int h= height >> (s.spatial_decomposition_count-level);
            int stride= width  << (s.spatial_decomposition_count-level);
            DWTELEM *buf= buffer[0];
            int64_t error=0;

            if(orientation&1) buf+=w;
            if(orientation>1) buf+=stride>>1;
            
            memset(buffer[0], 0, sizeof(int)*width*height);
            buf[w/2 + h/2*stride]= 256*256;
            ff_spatial_idwt(buffer[0], width, height, width, s->spatial_decomposition_type, s->spatial_decomposition_count);
            for(y=0; y<height; y++){
                for(x=0; x<width; x++){
                    int64_t d= buffer[0][x + y*width];
                    error += d*d;
                    if(ABS(width/2-x)<9 && ABS(height/2-y)<9 && level==2) printf("%8lld ", d);
                }
                if(ABS(height/2-y)<9 && level==2) printf("\n");
            }
            error= (int)(sqrt(error)+0.5);
            errors[level][orientation]= error;
            if(g) g=ff_gcd(g, error);
            else g= error;
        }
    }
    printf("static int const visual_weight[][4]={\n");
    for(level=0; level<s.spatial_decomposition_count; level++){
        printf("  {");
        for(orientation=0; orientation<4; orientation++){
            printf("%8lld,", errors[level][orientation]/g);
        }
        printf("},\n");
    }
    printf("};\n");
    {
            int level=2;
            int orientation=3;
            int w= width  >> (s.spatial_decomposition_count-level);
            int h= height >> (s.spatial_decomposition_count-level);
            int stride= width  << (s.spatial_decomposition_count-level);
            DWTELEM *buf= buffer[0];
            int64_t error=0;

            buf+=w;
            buf+=stride>>1;
            
            memset(buffer[0], 0, sizeof(int)*width*height);
#if 1
            for(y=0; y<height; y++){
                for(x=0; x<width; x++){
                    int tab[4]={0,2,3,1};
                    buffer[0][x+width*y]= 256*256*tab[(x&1) + 2*(y&1)];
                }
            }
            ff_spatial_dwt(buffer[0], width, height, width, s->spatial_decomposition_type, s->spatial_decomposition_count);
#else
            for(y=0; y<h; y++){
                for(x=0; x<w; x++){
                    buf[x + y*stride  ]=169;
                    buf[x + y*stride-w]=64;
                }
            }
            ff_spatial_idwt(buffer[0], width, height, width, s->spatial_decomposition_type, s->spatial_decomposition_count);
#endif
            for(y=0; y<height; y++){
                for(x=0; x<width; x++){
                    int64_t d= buffer[0][x + y*width];
                    error += d*d;
                    if(ABS(width/2-x)<9 && ABS(height/2-y)<9) printf("%8lld ", d);
                }
                if(ABS(height/2-y)<9) printf("\n");
            }
    }

}
    return 0;
}
#endif

