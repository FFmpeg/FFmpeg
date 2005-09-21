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

#include "rangecoder.h"
#define MID_STATE 128

#include "mpegvideo.h"

#undef NDEBUG
#include <assert.h>

#define MAX_DECOMPOSITIONS 8
#define MAX_PLANES 4
#define DWTELEM int
#define QSHIFT 5
#define QROOT (1<<QSHIFT)
#define LOSSLESS_QLOG -128
#define FRAC_BITS 8

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
static const int8_t quant3bA[256]={
 0, 0, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1,
 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1,
 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1,
 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1,
 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1,
 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1,
 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1,
 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1,
 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1,
 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1,
 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1,
 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1,
 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1,
 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1,
 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1,
 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1, 1,-1,
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

#define LOG2_OBMC_MAX 6
#define OBMC_MAX (1<<(LOG2_OBMC_MAX))
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

//linear *64
static const uint8_t obmc8[64]={
 1, 3, 5, 7, 7, 5, 3, 1,
 3, 9,15,21,21,15, 9, 3,
 5,15,25,35,35,25,15, 5,
 7,21,35,49,49,35,21, 7,
 7,21,35,49,49,35,21, 7,
 5,15,25,35,35,25,15, 5,
 3, 9,15,21,21,15, 9, 3,
 1, 3, 5, 7, 7, 5, 3, 1,
//error:0.000000
};

//linear *64
static const uint8_t obmc4[16]={
 4,12,12, 4,
12,36,36,12,
12,36,36,12,
 4,12,12, 4,
//error:0.000000
};

static const uint8_t *obmc_tab[4]={
    obmc32, obmc16, obmc8, obmc4
};

typedef struct BlockNode{
    int16_t mx;
    int16_t my;
    uint8_t color[3];
    uint8_t type;
//#define TYPE_SPLIT    1
#define BLOCK_INTRA   1
//#define TYPE_NOCOLOR  4
    uint8_t level; //FIXME merge into type?
}BlockNode;

#define LOG2_MB_SIZE 4
#define MB_SIZE (1<<LOG2_MB_SIZE)

typedef struct x_and_coeff{
    int16_t x;
    uint16_t coeff;
} x_and_coeff;

typedef struct SubBand{
    int level;
    int stride;
    int width;
    int height;
    int qlog;                                   ///< log(qscale)/log[2^(1/6)]
    DWTELEM *buf;
    int buf_x_offset;
    int buf_y_offset;
    int stride_line; ///< Stride measured in lines, not pixels.
    x_and_coeff * x_coeff;
    struct SubBand *parent;
    uint8_t state[/*7*2*/ 7 + 512][32];
}SubBand;

typedef struct Plane{
    int width;
    int height;
    SubBand band[MAX_DECOMPOSITIONS][4];
}Plane;

/** Used to minimize the amount of memory used in order to optimize cache performance. **/
typedef struct {
    DWTELEM * * line; ///< For use by idwt and predict_slices.
    DWTELEM * * data_stack; ///< Used for internal purposes.
    int data_stack_top;
    int line_count;
    int line_width;
    int data_count;
    DWTELEM * base_buffer; ///< Buffer that this structure is caching.
} slice_buffer;

typedef struct SnowContext{
//    MpegEncContext m; // needed for motion estimation, should not be used for anything else, the idea is to make the motion estimation eventually independant of MpegEncContext, so this will be removed then (FIXME/XXX)

    AVCodecContext *avctx;
    RangeCoder c;
    DSPContext dsp;
    AVFrame input_picture;
    AVFrame current_picture;
    AVFrame last_picture;
    AVFrame mconly_picture;
//     uint8_t q_context[16];
    uint8_t header_state[32];
    uint8_t block_state[128 + 32*128];
    int keyframe;
    int always_reset;
    int version;
    int spatial_decomposition_type;
    int temporal_decomposition_type;
    int spatial_decomposition_count;
    int temporal_decomposition_count;
    DWTELEM *spatial_dwt_buffer;
    int colorspace_type;
    int chroma_h_shift;
    int chroma_v_shift;
    int spatial_scalability;
    int qlog;
    int lambda;
    int lambda2;
    int mv_scale;
    int qbias;
#define QBIAS_SHIFT 3
    int b_width;
    int b_height;
    int block_max_depth;
    Plane plane[MAX_PLANES];
    BlockNode *block;
    slice_buffer sb;

    MpegEncContext m; // needed for motion estimation, should not be used for anything else, the idea is to make the motion estimation eventually independant of MpegEncContext, so this will be removed then (FIXME/XXX)
}SnowContext;

typedef struct {
    DWTELEM *b0;
    DWTELEM *b1;
    DWTELEM *b2;
    DWTELEM *b3;
    int y;
} dwt_compose_t;

#define slice_buffer_get_line(slice_buf, line_num) ((slice_buf)->line[line_num] ? (slice_buf)->line[line_num] : slice_buffer_load_line((slice_buf), (line_num)))
//#define slice_buffer_get_line(slice_buf, line_num) (slice_buffer_load_line((slice_buf), (line_num)))

static void slice_buffer_init(slice_buffer * buf, int line_count, int max_allocated_lines, int line_width, DWTELEM * base_buffer)
{
    int i;
  
    buf->base_buffer = base_buffer;
    buf->line_count = line_count;
    buf->line_width = line_width;
    buf->data_count = max_allocated_lines;
    buf->line = (DWTELEM * *) av_mallocz (sizeof(DWTELEM *) * line_count);
    buf->data_stack = (DWTELEM * *) av_malloc (sizeof(DWTELEM *) * max_allocated_lines);
  
    for (i = 0; i < max_allocated_lines; i++)
    {
      buf->data_stack[i] = (DWTELEM *) av_malloc (sizeof(DWTELEM) * line_width);
    }
    
    buf->data_stack_top = max_allocated_lines - 1;
}

static DWTELEM * slice_buffer_load_line(slice_buffer * buf, int line)
{
    int offset;
    DWTELEM * buffer;
  
//  av_log(NULL, AV_LOG_DEBUG, "Cache hit: %d\n", line);  
  
    assert(buf->data_stack_top >= 0);
//  assert(!buf->line[line]);
    if (buf->line[line])
        return buf->line[line];
    
    offset = buf->line_width * line;
    buffer = buf->data_stack[buf->data_stack_top];
    buf->data_stack_top--;
    buf->line[line] = buffer;
  
//  av_log(NULL, AV_LOG_DEBUG, "slice_buffer_load_line: line: %d remaining: %d\n", line, buf->data_stack_top + 1);
  
    return buffer;
}

static void slice_buffer_release(slice_buffer * buf, int line)
{
    int offset;
    DWTELEM * buffer;

    assert(line >= 0 && line < buf->line_count);
    assert(buf->line[line]);

    offset = buf->line_width * line;
    buffer = buf->line[line];
    buf->data_stack_top++;
    buf->data_stack[buf->data_stack_top] = buffer;
    buf->line[line] = NULL;
  
//  av_log(NULL, AV_LOG_DEBUG, "slice_buffer_release: line: %d remaining: %d\n", line, buf->data_stack_top + 1);
}

static void slice_buffer_flush(slice_buffer * buf)
{
    int i;
    for (i = 0; i < buf->line_count; i++)
    {
        if (buf->line[i])
        {
//      av_log(NULL, AV_LOG_DEBUG, "slice_buffer_flush: line: %d \n", i);
            slice_buffer_release(buf, i);
        }
    }
}

static void slice_buffer_destroy(slice_buffer * buf)
{
    int i;
    slice_buffer_flush(buf);
  
    for (i = buf->data_count - 1; i >= 0; i--)
    {
        assert(buf->data_stack[i]);
        av_free(buf->data_stack[i]);
    }
    assert(buf->data_stack);
    av_free(buf->data_stack);
    assert(buf->line);
    av_free(buf->line);
}

#ifdef	__sgi
// Avoid a name clash on SGI IRIX
#undef	qexp
#endif
#define QEXPSHIFT (7-FRAC_BITS+8) //FIXME try to change this to 0
static uint8_t qexp[QROOT];

static inline int mirror(int v, int m){
    if     (v<0) return -v;
    else if(v>m) return 2*m-v;
    else         return v;
}

static inline void put_symbol(RangeCoder *c, uint8_t *state, int v, int is_signed){
    int i;

    if(v){
        const int a= ABS(v);
        const int e= av_log2(a);
#if 1
        const int el= FFMIN(e, 10);   
        put_rac(c, state+0, 0);

        for(i=0; i<el; i++){
            put_rac(c, state+1+i, 1);  //1..10
        }
        for(; i<e; i++){
            put_rac(c, state+1+9, 1);  //1..10
        }
        put_rac(c, state+1+FFMIN(i,9), 0);

        for(i=e-1; i>=el; i--){
            put_rac(c, state+22+9, (a>>i)&1); //22..31
        }
        for(; i>=0; i--){
            put_rac(c, state+22+i, (a>>i)&1); //22..31
        }

        if(is_signed)
            put_rac(c, state+11 + el, v < 0); //11..21
#else
        
        put_rac(c, state+0, 0);
        if(e<=9){
            for(i=0; i<e; i++){
                put_rac(c, state+1+i, 1);  //1..10
            }
            put_rac(c, state+1+i, 0);

            for(i=e-1; i>=0; i--){
                put_rac(c, state+22+i, (a>>i)&1); //22..31
            }

            if(is_signed)
                put_rac(c, state+11 + e, v < 0); //11..21
        }else{
            for(i=0; i<e; i++){
                put_rac(c, state+1+FFMIN(i,9), 1);  //1..10
            }
            put_rac(c, state+1+FFMIN(i,9), 0);

            for(i=e-1; i>=0; i--){
                put_rac(c, state+22+FFMIN(i,9), (a>>i)&1); //22..31
            }

            if(is_signed)
                put_rac(c, state+11 + FFMIN(e,10), v < 0); //11..21
        }
#endif
    }else{
        put_rac(c, state+0, 1);
    }
}

static inline int get_symbol(RangeCoder *c, uint8_t *state, int is_signed){
    if(get_rac(c, state+0))
        return 0;
    else{
        int i, e, a;
        e= 0;
        while(get_rac(c, state+1 + FFMIN(e,9))){ //1..10
            e++;
        }

        a= 1;
        for(i=e-1; i>=0; i--){
            a += a + get_rac(c, state+22 + FFMIN(i,9)); //22..31
        }

        if(is_signed && get_rac(c, state+11 + FFMIN(e,10))) //11..21
            return -a;
        else
            return a;
    }
}

static inline void put_symbol2(RangeCoder *c, uint8_t *state, int v, int log2){
    int i;
    int r= log2>=0 ? 1<<log2 : 1;

    assert(v>=0);
    assert(log2>=-4);

    while(v >= r){
        put_rac(c, state+4+log2, 1);
        v -= r;
        log2++;
        if(log2>0) r+=r;
    }
    put_rac(c, state+4+log2, 0);
    
    for(i=log2-1; i>=0; i--){
        put_rac(c, state+31-i, (v>>i)&1);
    }
}

static inline int get_symbol2(RangeCoder *c, uint8_t *state, int log2){
    int i;
    int r= log2>=0 ? 1<<log2 : 1;
    int v=0;

    assert(log2>=-4);

    while(get_rac(c, state+4+log2)){
        v+= r;
        log2++;
        if(log2>0) r+=r;
    }
    
    for(i=log2-1; i>=0; i--){
        v+= get_rac(c, state+31-i)<<i;
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

static always_inline void liftS(DWTELEM *dst, DWTELEM *src, DWTELEM *ref, int dst_step, int src_step, int ref_step, int width, int mul, int add, int shift, int highpass, int inverse){
    const int mirror_left= !highpass;
    const int mirror_right= (width&1) ^ highpass;
    const int w= (width>>1) - 1 + (highpass & width);
    int i;

    assert(shift == 4);
#define LIFTS(src, ref, inv) ((inv) ? (src) - (((ref) - 4*(src))>>shift): (16*4*(src) + 4*(ref) + 8 + (5<<27))/(5*16) - (1<<23))
    if(mirror_left){
        dst[0] = LIFTS(src[0], mul*2*ref[0]+add, inverse);
        dst += dst_step;
        src += src_step;
    }
    
    for(i=0; i<w; i++){
        dst[i*dst_step] = LIFTS(src[i*src_step], mul*(ref[i*ref_step] + ref[(i+1)*ref_step])+add, inverse);
    }
    
    if(mirror_right){
        dst[w*dst_step] = LIFTS(src[w*src_step], mul*2*ref[w*ref_step]+add, inverse);
    }
}


static void inplace_lift(DWTELEM *dst, int width, int *coeffs, int n, int shift, int start, int inverse){
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

static void inplace_liftV(DWTELEM *dst, int width, int height, int stride, int *coeffs, int n, int shift, int start, int inverse){
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
static void horizontal_decomposeX(DWTELEM *b, int width){
    DWTELEM temp[width];
    const int width2= width>>1;
    const int w2= (width+1)>>1;
    int x;

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

static void horizontal_composeX(DWTELEM *b, int width){
    DWTELEM temp[width];
    const int width2= width>>1;
    int x;
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

static void spatial_decomposeX(DWTELEM *buffer, int width, int height, int stride){
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

static void spatial_composeX(DWTELEM *buffer, int width, int height, int stride){
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

static void horizontal_decompose53i(DWTELEM *b, int width){
    DWTELEM temp[width];
    const int width2= width>>1;
    int x;
    const int w2= (width+1)>>1;

    for(x=0; x<width2; x++){
        temp[x   ]= b[2*x    ];
        temp[x+w2]= b[2*x + 1];
    }
    if(width&1)
        temp[x   ]= b[2*x    ];
#if 0
    {
    int A1,A2,A3,A4;
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
    }
#else        
    lift(b+w2, temp+w2, temp, 1, 1, 1, width, -1, 0, 1, 1, 0);
    lift(b   , temp   , b+w2, 1, 1, 1, width,  1, 2, 2, 0, 0);
#endif
}

static void vertical_decompose53iH0(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, int width){
    int i;
    
    for(i=0; i<width; i++){
        b1[i] -= (b0[i] + b2[i])>>1;
    }
}

static void vertical_decompose53iL0(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, int width){
    int i;
    
    for(i=0; i<width; i++){
        b1[i] += (b0[i] + b2[i] + 2)>>2;
    }
}

static void spatial_decompose53i(DWTELEM *buffer, int width, int height, int stride){
    int y;
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

#define liftS lift
#define lift5 lift
#if 1
#define W_AM 3
#define W_AO 0
#define W_AS 1

#undef liftS
#define W_BM 1
#define W_BO 8
#define W_BS 4

#define W_CM 1
#define W_CO 0
#define W_CS 0

#define W_DM 3
#define W_DO 4
#define W_DS 3
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
static void horizontal_decompose97i(DWTELEM *b, int width){
    DWTELEM temp[width];
    const int w2= (width+1)>>1;

    lift (temp+w2, b    +1, b      , 1, 2, 2, width, -W_AM, W_AO, W_AS, 1, 0);
    liftS(temp   , b      , temp+w2, 1, 2, 1, width, -W_BM, W_BO, W_BS, 0, 0);
    lift5(b   +w2, temp+w2, temp   , 1, 1, 1, width,  W_CM, W_CO, W_CS, 1, 0);
    lift (b      , temp   , b   +w2, 1, 1, 1, width,  W_DM, W_DO, W_DS, 0, 0);
}


static void vertical_decompose97iH0(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, int width){
    int i;
    
    for(i=0; i<width; i++){
        b1[i] -= (W_AM*(b0[i] + b2[i])+W_AO)>>W_AS;
    }
}

static void vertical_decompose97iH1(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, int width){
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

static void vertical_decompose97iL0(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, int width){
    int i;
    
    for(i=0; i<width; i++){
#ifdef liftS
        b1[i] -= (W_BM*(b0[i] + b2[i])+W_BO)>>W_BS;
#else
        b1[i] = (16*4*b1[i] - 4*(b0[i] + b2[i]) + 8*5 + (5<<27)) / (5*16) - (1<<23);
#endif
    }
}

static void vertical_decompose97iL1(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, int width){
    int i;
    
    for(i=0; i<width; i++){
        b1[i] += (W_DM*(b0[i] + b2[i])+W_DO)>>W_DS;
    }
}

static void spatial_decompose97i(DWTELEM *buffer, int width, int height, int stride){
    int y;
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

void ff_spatial_dwt(DWTELEM *buffer, int width, int height, int stride, int type, int decomposition_count){
    int level;
    
    for(level=0; level<decomposition_count; level++){
        switch(type){
        case 0: spatial_decompose97i(buffer, width>>level, height>>level, stride<<level); break;
        case 1: spatial_decompose53i(buffer, width>>level, height>>level, stride<<level); break;
        case 2: spatial_decomposeX  (buffer, width>>level, height>>level, stride<<level); break;
        }
    }
}

static void horizontal_compose53i(DWTELEM *b, int width){
    DWTELEM temp[width];
    const int width2= width>>1;
    const int w2= (width+1)>>1;
    int x;

#if 0
    int A1,A2,A3,A4;
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

static void vertical_compose53iH0(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, int width){
    int i;
    
    for(i=0; i<width; i++){
        b1[i] += (b0[i] + b2[i])>>1;
    }
}

static void vertical_compose53iL0(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, int width){
    int i;
    
    for(i=0; i<width; i++){
        b1[i] -= (b0[i] + b2[i] + 2)>>2;
    }
}

static void spatial_compose53i_buffered_init(dwt_compose_t *cs, slice_buffer * sb, int height, int stride_line){
    cs->b0 = slice_buffer_get_line(sb, mirror(-1-1, height-1) * stride_line);
    cs->b1 = slice_buffer_get_line(sb, mirror(-1  , height-1) * stride_line);
    cs->y = -1;
}

static void spatial_compose53i_init(dwt_compose_t *cs, DWTELEM *buffer, int height, int stride){
    cs->b0 = buffer + mirror(-1-1, height-1)*stride;
    cs->b1 = buffer + mirror(-1  , height-1)*stride;
    cs->y = -1;
}

static void spatial_compose53i_dy_buffered(dwt_compose_t *cs, slice_buffer * sb, int width, int height, int stride_line){
    int y= cs->y;
    int mirror0 = mirror(y-1, height-1);
    int mirror1 = mirror(y  , height-1);
    int mirror2 = mirror(y+1, height-1);
    int mirror3 = mirror(y+2, height-1);
    
    DWTELEM *b0= cs->b0;
    DWTELEM *b1= cs->b1;
    DWTELEM *b2= slice_buffer_get_line(sb, mirror2 * stride_line);
    DWTELEM *b3= slice_buffer_get_line(sb, mirror3 * stride_line);

{START_TIMER
        if(mirror1 <= mirror3) vertical_compose53iL0(b1, b2, b3, width);
        if(mirror0 <= mirror2) vertical_compose53iH0(b0, b1, b2, width);
STOP_TIMER("vertical_compose53i*")}

{START_TIMER
        if(y-1 >= 0) horizontal_compose53i(b0, width);
        if(mirror0 <= mirror2) horizontal_compose53i(b1, width);
STOP_TIMER("horizontal_compose53i")}

    cs->b0 = b2;
    cs->b1 = b3;
    cs->y += 2;
}

static void spatial_compose53i_dy(dwt_compose_t *cs, DWTELEM *buffer, int width, int height, int stride){
    int y= cs->y;
    DWTELEM *b0= cs->b0;
    DWTELEM *b1= cs->b1;
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

    cs->b0 = b2;
    cs->b1 = b3;
    cs->y += 2;
}

static void spatial_compose53i(DWTELEM *buffer, int width, int height, int stride){
    dwt_compose_t cs;
    spatial_compose53i_init(&cs, buffer, height, stride);
    while(cs.y <= height)
        spatial_compose53i_dy(&cs, buffer, width, height, stride);
}   

 
static void horizontal_compose97i(DWTELEM *b, int width){
    DWTELEM temp[width];
    const int w2= (width+1)>>1;

    lift (temp   , b      , b   +w2, 1, 1, 1, width,  W_DM, W_DO, W_DS, 0, 1);
    lift5(temp+w2, b   +w2, temp   , 1, 1, 1, width,  W_CM, W_CO, W_CS, 1, 1);
    liftS(b      , temp   , temp+w2, 2, 1, 1, width, -W_BM, W_BO, W_BS, 0, 1);
    lift (b+1    , temp+w2, b      , 2, 1, 2, width, -W_AM, W_AO, W_AS, 1, 1);
}

static void vertical_compose97iH0(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, int width){
    int i;
    
    for(i=0; i<width; i++){
        b1[i] += (W_AM*(b0[i] + b2[i])+W_AO)>>W_AS;
    }
}

static void vertical_compose97iH1(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, int width){
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

static void vertical_compose97iL0(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, int width){
    int i;
    
    for(i=0; i<width; i++){
#ifdef liftS
        b1[i] += (W_BM*(b0[i] + b2[i])+W_BO)>>W_BS;
#else
        b1[i] += (W_BM*(b0[i] + b2[i])+4*b1[i]+W_BO)>>W_BS;
#endif
    }
}

static void vertical_compose97iL1(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, int width){
    int i;
    
    for(i=0; i<width; i++){
        b1[i] -= (W_DM*(b0[i] + b2[i])+W_DO)>>W_DS;
    }
}

static void vertical_compose97i(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, DWTELEM *b3, DWTELEM *b4, DWTELEM *b5, int width){
    int i;
    
    for(i=0; i<width; i++){
#ifndef lift5
        int r;
#endif
        b4[i] -= (W_DM*(b3[i] + b5[i])+W_DO)>>W_DS;
#ifdef lift5
        b3[i] -= (W_CM*(b2[i] + b4[i])+W_CO)>>W_CS;
#else
        r= 3*(b2[i] + b4[i]);
        r+= r>>4;
        r+= r>>8;
        b3[i] -= (r+W_CO)>>W_CS;
#endif
#ifdef liftS
        b2[i] += (W_BM*(b1[i] + b3[i])+W_BO)>>W_BS;
#else
        b2[i] += (W_BM*(b1[i] + b3[i])+4*b2[i]+W_BO)>>W_BS;
#endif
        b1[i] += (W_AM*(b0[i] + b2[i])+W_AO)>>W_AS;
    }
}

static void spatial_compose97i_buffered_init(dwt_compose_t *cs, slice_buffer * sb, int height, int stride_line){
    cs->b0 = slice_buffer_get_line(sb, mirror(-3-1, height-1) * stride_line);
    cs->b1 = slice_buffer_get_line(sb, mirror(-3  , height-1) * stride_line);
    cs->b2 = slice_buffer_get_line(sb, mirror(-3+1, height-1) * stride_line);
    cs->b3 = slice_buffer_get_line(sb, mirror(-3+2, height-1) * stride_line);
    cs->y = -3;
}

static void spatial_compose97i_init(dwt_compose_t *cs, DWTELEM *buffer, int height, int stride){
    cs->b0 = buffer + mirror(-3-1, height-1)*stride;
    cs->b1 = buffer + mirror(-3  , height-1)*stride;
    cs->b2 = buffer + mirror(-3+1, height-1)*stride;
    cs->b3 = buffer + mirror(-3+2, height-1)*stride;
    cs->y = -3;
}

static void spatial_compose97i_dy_buffered(dwt_compose_t *cs, slice_buffer * sb, int width, int height, int stride_line){
    int y = cs->y;
    
    int mirror0 = mirror(y - 1, height - 1);
    int mirror1 = mirror(y + 0, height - 1);
    int mirror2 = mirror(y + 1, height - 1);
    int mirror3 = mirror(y + 2, height - 1);
    int mirror4 = mirror(y + 3, height - 1);
    int mirror5 = mirror(y + 4, height - 1);
    DWTELEM *b0= cs->b0;
    DWTELEM *b1= cs->b1;
    DWTELEM *b2= cs->b2;
    DWTELEM *b3= cs->b3;
    DWTELEM *b4= slice_buffer_get_line(sb, mirror4 * stride_line);
    DWTELEM *b5= slice_buffer_get_line(sb, mirror5 * stride_line);
        
{START_TIMER
    if(y>0 && y+4<height){
        vertical_compose97i(b0, b1, b2, b3, b4, b5, width);
    }else{
        if(mirror3 <= mirror5) vertical_compose97iL1(b3, b4, b5, width);
        if(mirror2 <= mirror4) vertical_compose97iH1(b2, b3, b4, width);
        if(mirror1 <= mirror3) vertical_compose97iL0(b1, b2, b3, width);
        if(mirror0 <= mirror2) vertical_compose97iH0(b0, b1, b2, width);
    }
if(width>400){
STOP_TIMER("vertical_compose97i")}}

{START_TIMER
        if(y-1>=  0) horizontal_compose97i(b0, width);
        if(mirror0 <= mirror2) horizontal_compose97i(b1, width);
if(width>400 && mirror0 <= mirror2){
STOP_TIMER("horizontal_compose97i")}}

    cs->b0=b2;
    cs->b1=b3;
    cs->b2=b4;
    cs->b3=b5;
    cs->y += 2;
}

static void spatial_compose97i_dy(dwt_compose_t *cs, DWTELEM *buffer, int width, int height, int stride){
    int y = cs->y;
    DWTELEM *b0= cs->b0;
    DWTELEM *b1= cs->b1;
    DWTELEM *b2= cs->b2;
    DWTELEM *b3= cs->b3;
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

    cs->b0=b2;
    cs->b1=b3;
    cs->b2=b4;
    cs->b3=b5;
    cs->y += 2;
}

static void spatial_compose97i(DWTELEM *buffer, int width, int height, int stride){
    dwt_compose_t cs;
    spatial_compose97i_init(&cs, buffer, height, stride);
    while(cs.y <= height)
        spatial_compose97i_dy(&cs, buffer, width, height, stride);
}

void ff_spatial_idwt_buffered_init(dwt_compose_t *cs, slice_buffer * sb, int width, int height, int stride_line, int type, int decomposition_count){
    int level;
    for(level=decomposition_count-1; level>=0; level--){
        switch(type){
        case 0: spatial_compose97i_buffered_init(cs+level, sb, height>>level, stride_line<<level); break;
        case 1: spatial_compose53i_buffered_init(cs+level, sb, height>>level, stride_line<<level); break;
        /* not slicified yet */
        case 2: /*spatial_composeX(buffer, width>>level, height>>level, stride<<level); break;*/
          av_log(NULL, AV_LOG_ERROR, "spatial_composeX neither buffered nor slicified yet.\n"); break;
        }
    }
}

void ff_spatial_idwt_init(dwt_compose_t *cs, DWTELEM *buffer, int width, int height, int stride, int type, int decomposition_count){
    int level;
    for(level=decomposition_count-1; level>=0; level--){
        switch(type){
        case 0: spatial_compose97i_init(cs+level, buffer, height>>level, stride<<level); break;
        case 1: spatial_compose53i_init(cs+level, buffer, height>>level, stride<<level); break;
        /* not slicified yet */
        case 2: spatial_composeX(buffer, width>>level, height>>level, stride<<level); break;
        }
    }
}

void ff_spatial_idwt_slice(dwt_compose_t *cs, DWTELEM *buffer, int width, int height, int stride, int type, int decomposition_count, int y){
    const int support = type==1 ? 3 : 5;
    int level;
    if(type==2) return;

    for(level=decomposition_count-1; level>=0; level--){
        while(cs[level].y <= FFMIN((y>>level)+support, height>>level)){
            switch(type){
            case 0: spatial_compose97i_dy(cs+level, buffer, width>>level, height>>level, stride<<level);
                    break;
            case 1: spatial_compose53i_dy(cs+level, buffer, width>>level, height>>level, stride<<level);
                    break;
            case 2: break;
            }
        }
    }
}

void ff_spatial_idwt_buffered_slice(dwt_compose_t *cs, slice_buffer * slice_buf, int width, int height, int stride_line, int type, int decomposition_count, int y){
    const int support = type==1 ? 3 : 5;
    int level;
    if(type==2) return;

    for(level=decomposition_count-1; level>=0; level--){
        while(cs[level].y <= FFMIN((y>>level)+support, height>>level)){
            switch(type){
            case 0: spatial_compose97i_dy_buffered(cs+level, slice_buf, width>>level, height>>level, stride_line<<level);
                    break;
            case 1: spatial_compose53i_dy_buffered(cs+level, slice_buf, width>>level, height>>level, stride_line<<level);
                    break;
            case 2: break;
            }
        }
    }
}

void ff_spatial_idwt(DWTELEM *buffer, int width, int height, int stride, int type, int decomposition_count){
    if(type==2){
        int level;
        for(level=decomposition_count-1; level>=0; level--)
            spatial_composeX  (buffer, width>>level, height>>level, stride<<level);
    }else{
        dwt_compose_t cs[MAX_DECOMPOSITIONS];
        int y;
        ff_spatial_idwt_init(cs, buffer, width, height, stride, type, decomposition_count);
        for(y=0; y<height; y+=4)
            ff_spatial_idwt_slice(cs, buffer, width, height, stride, type, decomposition_count, y);
    }
}

static int encode_subband_c0run(SnowContext *s, SubBand *b, DWTELEM *src, DWTELEM *parent, int stride, int orientation){
    const int w= b->width;
    const int h= b->height;
    int x, y;

    if(1){
        int run=0;
        int runs[w*h];
        int run_index=0;
        int max_index;
                
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
        max_index= run_index;
        runs[run_index++]= run;
        run_index=0;
        run= runs[run_index++];

        put_symbol2(&s->c, b->state[30], max_index, 0);
        if(run_index <= max_index)
            put_symbol2(&s->c, b->state[1], run, 3);
        
        for(y=0; y<h; y++){
            if(s->c.bytestream_end - s->c.bytestream < w*40){
                av_log(s->avctx, AV_LOG_ERROR, "encoded frame too large\n");
                return -1;
            }
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

                    put_rac(&s->c, &b->state[0][context], !!v);
                }else{
                    if(!run){
                        run= runs[run_index++];

                        if(run_index <= max_index)
                            put_symbol2(&s->c, b->state[1], run, 3);
                        assert(v);
                    }else{
                        run--;
                        assert(!v);
                    }
                }
                if(v){
                    int context= av_log2(/*ABS(ll) + */3*ABS(l) + ABS(lt) + 2*ABS(t) + ABS(rt) + ABS(p));
                    int l2= 2*ABS(l) + (l<0);
                    int t2= 2*ABS(t) + (t<0);

                    put_symbol2(&s->c, b->state[context + 2], ABS(v)-1, context-4);
                    put_rac(&s->c, &b->state[0][16 + 1 + 3 + quant3bA[l2&0xFF] + 3*quant3bA[t2&0xFF]], v<0);
                }
            }
        }
    }
    return 0;
}

static int encode_subband(SnowContext *s, SubBand *b, DWTELEM *src, DWTELEM *parent, int stride, int orientation){    
//    encode_subband_qtree(s, b, src, parent, stride, orientation);
//    encode_subband_z0run(s, b, src, parent, stride, orientation);
    return encode_subband_c0run(s, b, src, parent, stride, orientation);
//    encode_subband_dzr(s, b, src, parent, stride, orientation);
}

static inline void unpack_coeffs(SnowContext *s, SubBand *b, SubBand * parent, int orientation){
    const int w= b->width;
    const int h= b->height;
    int x,y;
    
    if(1){
        int run, runs;
        x_and_coeff *xc= b->x_coeff;
        x_and_coeff *prev_xc= NULL;
        x_and_coeff *prev2_xc= xc;
        x_and_coeff *parent_xc= parent ? parent->x_coeff : NULL;
        x_and_coeff *prev_parent_xc= parent_xc;

        runs= get_symbol2(&s->c, b->state[30], 0);
        if(runs-- > 0) run= get_symbol2(&s->c, b->state[1], 3);
        else           run= INT_MAX;

        for(y=0; y<h; y++){
            int v=0;
            int lt=0, t=0, rt=0;

            if(y && prev_xc->x == 0){
                rt= prev_xc->coeff;
            }
            for(x=0; x<w; x++){
                int p=0;
                const int l= v;
                
                lt= t; t= rt;

                if(y){
                    if(prev_xc->x <= x)
                        prev_xc++;
                    if(prev_xc->x == x + 1)
                        rt= prev_xc->coeff;
                    else
                        rt=0;
                }
                if(parent_xc){
                    if(x>>1 > parent_xc->x){
                        parent_xc++;
                    }
                    if(x>>1 == parent_xc->x){
                        p= parent_xc->coeff;
                    }
                }
                if(/*ll|*/l|lt|t|rt|p){
                    int context= av_log2(/*ABS(ll) + */3*(l>>1) + (lt>>1) + (t&~1) + (rt>>1) + (p>>1));

                    v=get_rac(&s->c, &b->state[0][context]);
                    if(v){
                        v= 2*(get_symbol2(&s->c, b->state[context + 2], context-4) + 1);
                        v+=get_rac(&s->c, &b->state[0][16 + 1 + 3 + quant3bA[l&0xFF] + 3*quant3bA[t&0xFF]]);
                        
                        xc->x=x;
                        (xc++)->coeff= v;
                    }
                }else{
                    if(!run){
                        if(runs-- > 0) run= get_symbol2(&s->c, b->state[1], 3);
                        else           run= INT_MAX;
                        v= 2*(get_symbol2(&s->c, b->state[0 + 2], 0-4) + 1);
                        v+=get_rac(&s->c, &b->state[0][16 + 1 + 3]);
                        
                        xc->x=x;
                        (xc++)->coeff= v;
                    }else{
                        int max_run;
                        run--;
                        v=0;

                        if(y) max_run= FFMIN(run, prev_xc->x - x - 2);
                        else  max_run= FFMIN(run, w-x-1);
                        if(parent_xc)
                            max_run= FFMIN(max_run, 2*parent_xc->x - x - 1);
                        x+= max_run;
                        run-= max_run;
                    }
                }
            }
            (xc++)->x= w+1; //end marker
            prev_xc= prev2_xc;
            prev2_xc= xc;
            
            if(parent_xc){
                if(y&1){
                    while(parent_xc->x != parent->width+1)
                        parent_xc++;
                    parent_xc++;
                    prev_parent_xc= parent_xc;
                }else{
                    parent_xc= prev_parent_xc;
                }
            }
        }

        (xc++)->x= w+1; //end marker
    }
}

static inline void decode_subband_slice_buffered(SnowContext *s, SubBand *b, slice_buffer * sb, int start_y, int h, int save_state[1]){
    const int w= b->width;
    int y;
    const int qlog= clip(s->qlog + b->qlog, 0, QROOT*16);
    int qmul= qexp[qlog&(QROOT-1)]<<(qlog>>QSHIFT);
    int qadd= (s->qbias*qmul)>>QBIAS_SHIFT;
    int new_index = 0;
    
    START_TIMER

    if(b->buf == s->spatial_dwt_buffer || s->qlog == LOSSLESS_QLOG){
        qadd= 0;
        qmul= 1<<QEXPSHIFT;
    }

    /* If we are on the second or later slice, restore our index. */
    if (start_y != 0)
        new_index = save_state[0];

        
    for(y=start_y; y<h; y++){
        int x = 0;
        int v;
        DWTELEM * line = slice_buffer_get_line(sb, y * b->stride_line + b->buf_y_offset) + b->buf_x_offset;
        memset(line, 0, b->width*sizeof(DWTELEM));
        v = b->x_coeff[new_index].coeff;
        x = b->x_coeff[new_index++].x;
        while(x < w)
        {
            register int t= ( (v>>1)*qmul + qadd)>>QEXPSHIFT;
            register int u= -(v&1);
            line[x] = (t^u) - u;

            v = b->x_coeff[new_index].coeff;
            x = b->x_coeff[new_index++].x;
        }
    }
    if(w > 200 && start_y != 0/*level+1 == s->spatial_decomposition_count*/){
        STOP_TIMER("decode_subband")
    }
        
    /* Save our variables for the next slice. */
    save_state[0] = new_index;
        
    return;
}

static void reset_contexts(SnowContext *s){
    int plane_index, level, orientation;

    for(plane_index=0; plane_index<3; plane_index++){
        for(level=0; level<s->spatial_decomposition_count; level++){
            for(orientation=level ? 1:0; orientation<4; orientation++){
                memset(s->plane[plane_index].band[level][orientation].state, MID_STATE, sizeof(s->plane[plane_index].band[level][orientation].state));
            }
        }
    }
    memset(s->header_state, MID_STATE, sizeof(s->header_state));
    memset(s->block_state, MID_STATE, sizeof(s->block_state));
}

static int alloc_blocks(SnowContext *s){
    int w= -((-s->avctx->width )>>LOG2_MB_SIZE);
    int h= -((-s->avctx->height)>>LOG2_MB_SIZE);
    
    s->b_width = w;
    s->b_height= h;
    
    s->block= av_mallocz(w * h * sizeof(BlockNode) << (s->block_max_depth*2));
    return 0;
}

static inline void copy_rac_state(RangeCoder *d, RangeCoder *s){
    uint8_t *bytestream= d->bytestream;
    uint8_t *bytestream_start= d->bytestream_start;
    *d= *s;
    d->bytestream= bytestream;
    d->bytestream_start= bytestream_start;
}

//near copy & paste from dsputil, FIXME
static int pix_sum(uint8_t * pix, int line_size, int w)
{
    int s, i, j;

    s = 0;
    for (i = 0; i < w; i++) {
        for (j = 0; j < w; j++) {
            s += pix[0];
            pix ++;
        }
        pix += line_size - w;
    }
    return s;
}

//near copy & paste from dsputil, FIXME
static int pix_norm1(uint8_t * pix, int line_size, int w)
{
    int s, i, j;
    uint32_t *sq = squareTbl + 256;

    s = 0;
    for (i = 0; i < w; i++) {
        for (j = 0; j < w; j ++) {
            s += sq[pix[0]];
            pix ++;
        }
        pix += line_size - w;
    }
    return s;
}

static inline void set_blocks(SnowContext *s, int level, int x, int y, int l, int cb, int cr, int mx, int my, int type){
    const int w= s->b_width << s->block_max_depth;
    const int rem_depth= s->block_max_depth - level;
    const int index= (x + y*w) << rem_depth;
    const int block_w= 1<<rem_depth;
    BlockNode block;
    int i,j;
    
    block.color[0]= l;
    block.color[1]= cb;
    block.color[2]= cr;
    block.mx= mx;
    block.my= my;
    block.type= type;
    block.level= level;

    for(j=0; j<block_w; j++){
        for(i=0; i<block_w; i++){
            s->block[index + i + j*w]= block;
        }
    }
}

static inline void init_ref(MotionEstContext *c, uint8_t *src[3], uint8_t *ref[3], uint8_t *ref2[3], int x, int y, int ref_index){
    const int offset[3]= {
          y*c->  stride + x,
        ((y*c->uvstride + x)>>1),
        ((y*c->uvstride + x)>>1),
    };
    int i;
    for(i=0; i<3; i++){
        c->src[0][i]= src [i];
        c->ref[0][i]= ref [i] + offset[i];
    }
    assert(!ref_index);
}

//FIXME copy&paste
#define P_LEFT P[1]
#define P_TOP P[2]
#define P_TOPRIGHT P[3]
#define P_MEDIAN P[4]
#define P_MV1 P[9]
#define FLAG_QPEL   1 //must be 1

static int encode_q_branch(SnowContext *s, int level, int x, int y){
    uint8_t p_buffer[1024];
    uint8_t i_buffer[1024];
    uint8_t p_state[sizeof(s->block_state)];
    uint8_t i_state[sizeof(s->block_state)];
    RangeCoder pc, ic;
    uint8_t *pbbak= s->c.bytestream;
    uint8_t *pbbak_start= s->c.bytestream_start;
    int score, score2, iscore, i_len, p_len, block_s, sum;
    const int w= s->b_width  << s->block_max_depth;
    const int h= s->b_height << s->block_max_depth;
    const int rem_depth= s->block_max_depth - level;
    const int index= (x + y*w) << rem_depth;
    const int block_w= 1<<(LOG2_MB_SIZE - level);
    static BlockNode null_block= { //FIXME add border maybe
        .color= {128,128,128},
        .mx= 0,
        .my= 0,
        .type= 0,
        .level= 0,
    };
    int trx= (x+1)<<rem_depth;
    int try= (y+1)<<rem_depth;
    BlockNode *left  = x ? &s->block[index-1] : &null_block;
    BlockNode *top   = y ? &s->block[index-w] : &null_block;
    BlockNode *right = trx<w ? &s->block[index+1] : &null_block;
    BlockNode *bottom= try<h ? &s->block[index+w] : &null_block;
    BlockNode *tl    = y && x ? &s->block[index-w-1] : left;
    BlockNode *tr    = y && trx<w && ((x&1)==0 || level==0) ? &s->block[index-w+(1<<rem_depth)] : tl; //FIXME use lt
    int pl = left->color[0];
    int pcb= left->color[1];
    int pcr= left->color[2];
    int pmx= mid_pred(left->mx, top->mx, tr->mx);
    int pmy= mid_pred(left->my, top->my, tr->my);
    int mx=0, my=0;
    int l,cr,cb, i;
    const int stride= s->current_picture.linesize[0];
    const int uvstride= s->current_picture.linesize[1];
    const int instride= s->input_picture.linesize[0];
    const int uvinstride= s->input_picture.linesize[1];
    uint8_t *new_l = s->input_picture.data[0] + (x + y*  instride)*block_w;
    uint8_t *new_cb= s->input_picture.data[1] + (x + y*uvinstride)*block_w/2;
    uint8_t *new_cr= s->input_picture.data[2] + (x + y*uvinstride)*block_w/2;
    uint8_t current_mb[3][stride*block_w];
    uint8_t *current_data[3]= {&current_mb[0][0], &current_mb[1][0], &current_mb[2][0]};
    int P[10][2];
    int16_t last_mv[3][2];
    int qpel= !!(s->avctx->flags & CODEC_FLAG_QPEL); //unused
    const int shift= 1+qpel;
    MotionEstContext *c= &s->m.me;
    int mx_context= av_log2(2*ABS(left->mx - top->mx));
    int my_context= av_log2(2*ABS(left->my - top->my));
    int s_context= 2*left->level + 2*top->level + tl->level + tr->level;

    assert(sizeof(s->block_state) >= 256);
    if(s->keyframe){
        set_blocks(s, level, x, y, pl, pcb, pcr, pmx, pmy, BLOCK_INTRA);
        return 0;
    }

    //FIXME optimize
    for(i=0; i<block_w; i++)
        memcpy(&current_mb[0][0] +   stride*i, new_l  +   instride*i, block_w);
    for(i=0; i<block_w>>1; i++)
        memcpy(&current_mb[1][0] + uvstride*i, new_cb + uvinstride*i, block_w>>1);
    for(i=0; i<block_w>>1; i++)
        memcpy(&current_mb[2][0] + uvstride*i, new_cr + uvinstride*i, block_w>>1);

//    clip predictors / edge ?

    P_LEFT[0]= left->mx;
    P_LEFT[1]= left->my;
    P_TOP [0]= top->mx;
    P_TOP [1]= top->my;
    P_TOPRIGHT[0]= tr->mx;
    P_TOPRIGHT[1]= tr->my;
    
    last_mv[0][0]= s->block[index].mx;
    last_mv[0][1]= s->block[index].my;
    last_mv[1][0]= right->mx;
    last_mv[1][1]= right->my;
    last_mv[2][0]= bottom->mx;
    last_mv[2][1]= bottom->my;
    
    s->m.mb_stride=2;
    s->m.mb_x= 
    s->m.mb_y= 0;
    s->m.me.skip= 0;

    init_ref(c, current_data, s->last_picture.data, NULL, block_w*x, block_w*y, 0);
    
    assert(s->m.me.  stride ==   stride);
    assert(s->m.me.uvstride == uvstride);
    
    c->penalty_factor    = get_penalty_factor(s->lambda, s->lambda2, c->avctx->me_cmp);
    c->sub_penalty_factor= get_penalty_factor(s->lambda, s->lambda2, c->avctx->me_sub_cmp);
    c->mb_penalty_factor = get_penalty_factor(s->lambda, s->lambda2, c->avctx->mb_cmp);
    c->current_mv_penalty= c->mv_penalty[s->m.f_code=1] + MAX_MV;
    
    c->xmin = - x*block_w - 16+2;
    c->ymin = - y*block_w - 16+2;
    c->xmax = - (x+1)*block_w + (w<<(LOG2_MB_SIZE - s->block_max_depth)) + 16-2;
    c->ymax = - (y+1)*block_w + (h<<(LOG2_MB_SIZE - s->block_max_depth)) + 16-2;

    if(P_LEFT[0]     > (c->xmax<<shift)) P_LEFT[0]    = (c->xmax<<shift);
    if(P_LEFT[1]     > (c->ymax<<shift)) P_LEFT[1]    = (c->ymax<<shift); 
    if(P_TOP[0]      > (c->xmax<<shift)) P_TOP[0]     = (c->xmax<<shift);
    if(P_TOP[1]      > (c->ymax<<shift)) P_TOP[1]     = (c->ymax<<shift);
    if(P_TOPRIGHT[0] < (c->xmin<<shift)) P_TOPRIGHT[0]= (c->xmin<<shift);
    if(P_TOPRIGHT[0] > (c->xmax<<shift)) P_TOPRIGHT[0]= (c->xmax<<shift); //due to pmx no clip
    if(P_TOPRIGHT[1] > (c->ymax<<shift)) P_TOPRIGHT[1]= (c->ymax<<shift);

    P_MEDIAN[0]= mid_pred(P_LEFT[0], P_TOP[0], P_TOPRIGHT[0]);
    P_MEDIAN[1]= mid_pred(P_LEFT[1], P_TOP[1], P_TOPRIGHT[1]);

    if (!y) {
        c->pred_x= P_LEFT[0];
        c->pred_y= P_LEFT[1];
    } else {
        c->pred_x = P_MEDIAN[0];
        c->pred_y = P_MEDIAN[1];
    }

    score= ff_epzs_motion_search(&s->m, &mx, &my, P, 0, /*ref_index*/ 0, last_mv, 
                             (1<<16)>>shift, level-LOG2_MB_SIZE+4, block_w);

    assert(mx >= c->xmin);
    assert(mx <= c->xmax);
    assert(my >= c->ymin);
    assert(my <= c->ymax);
    
    score= s->m.me.sub_motion_search(&s->m, &mx, &my, score, 0, 0, level-LOG2_MB_SIZE+4, block_w);
    score= ff_get_mb_score(&s->m, mx, my, 0, 0, level-LOG2_MB_SIZE+4, block_w, 0);
    //FIXME if mb_cmp != SSE then intra cant be compared currently and mb_penalty vs. lambda2
                             
  //  subpel search
    pc= s->c;
    pc.bytestream_start=
    pc.bytestream= p_buffer; //FIXME end/start? and at the other stoo
    memcpy(p_state, s->block_state, sizeof(s->block_state));

    if(level!=s->block_max_depth)
        put_rac(&pc, &p_state[4 + s_context], 1);
    put_rac(&pc, &p_state[1 + left->type + top->type], 0);
    put_symbol(&pc, &p_state[128 + 32*mx_context], mx - pmx, 1);
    put_symbol(&pc, &p_state[128 + 32*my_context], my - pmy, 1);
    p_len= pc.bytestream - pc.bytestream_start;
    score += (s->lambda2*(p_len*8
              + (pc.outstanding_count - s->c.outstanding_count)*8
              + (-av_log2(pc.range)    + av_log2(s->c.range))
             ))>>FF_LAMBDA_SHIFT;

    block_s= block_w*block_w;
    sum = pix_sum(&current_mb[0][0], stride, block_w);
    l= (sum + block_s/2)/block_s;
    iscore = pix_norm1(&current_mb[0][0], stride, block_w) - 2*l*sum + l*l*block_s;
    
    block_s= block_w*block_w>>2;
    sum = pix_sum(&current_mb[1][0], uvstride, block_w>>1);
    cb= (sum + block_s/2)/block_s;
//    iscore += pix_norm1(&current_mb[1][0], uvstride, block_w>>1) - 2*cb*sum + cb*cb*block_s;
    sum = pix_sum(&current_mb[2][0], uvstride, block_w>>1);
    cr= (sum + block_s/2)/block_s;
//    iscore += pix_norm1(&current_mb[2][0], uvstride, block_w>>1) - 2*cr*sum + cr*cr*block_s;

    ic= s->c;
    ic.bytestream_start=
    ic.bytestream= i_buffer; //FIXME end/start? and at the other stoo
    memcpy(i_state, s->block_state, sizeof(s->block_state));
    if(level!=s->block_max_depth)
        put_rac(&ic, &i_state[4 + s_context], 1);
    put_rac(&ic, &i_state[1 + left->type + top->type], 1);
    put_symbol(&ic, &i_state[32],  l-pl , 1);
    put_symbol(&ic, &i_state[64], cb-pcb, 1);
    put_symbol(&ic, &i_state[96], cr-pcr, 1);
    i_len= ic.bytestream - ic.bytestream_start;
    iscore += (s->lambda2*(i_len*8
              + (ic.outstanding_count - s->c.outstanding_count)*8
              + (-av_log2(ic.range)    + av_log2(s->c.range))
             ))>>FF_LAMBDA_SHIFT;

//    assert(score==256*256*256*64-1);
    assert(iscore < 255*255*256 + s->lambda2*10);
    assert(iscore >= 0);
    assert(l>=0 && l<=255);
    assert(pl>=0 && pl<=255);

    if(level==0){
        int varc= iscore >> 8;
        int vard= score >> 8;
        if (vard <= 64 || vard < varc)
            c->scene_change_score+= ff_sqrt(vard) - ff_sqrt(varc);
        else
            c->scene_change_score+= s->m.qscale;
    }
        
    if(level!=s->block_max_depth){
        put_rac(&s->c, &s->block_state[4 + s_context], 0);
        score2 = encode_q_branch(s, level+1, 2*x+0, 2*y+0);
        score2+= encode_q_branch(s, level+1, 2*x+1, 2*y+0);
        score2+= encode_q_branch(s, level+1, 2*x+0, 2*y+1);
        score2+= encode_q_branch(s, level+1, 2*x+1, 2*y+1);
        score2+= s->lambda2>>FF_LAMBDA_SHIFT; //FIXME exact split overhead
    
        if(score2 < score && score2 < iscore)
            return score2;
    }
    
    if(iscore < score){
        memcpy(pbbak, i_buffer, i_len);
        s->c= ic;
        s->c.bytestream_start= pbbak_start;
        s->c.bytestream= pbbak + i_len;
        set_blocks(s, level, x, y, l, cb, cr, pmx, pmy, BLOCK_INTRA);
        memcpy(s->block_state, i_state, sizeof(s->block_state));
        return iscore;
    }else{
        memcpy(pbbak, p_buffer, p_len);
        s->c= pc;
        s->c.bytestream_start= pbbak_start;
        s->c.bytestream= pbbak + p_len;
        set_blocks(s, level, x, y, pl, pcb, pcr, mx, my, 0);
        memcpy(s->block_state, p_state, sizeof(s->block_state));
        return score;
    }
}

static void decode_q_branch(SnowContext *s, int level, int x, int y){
    const int w= s->b_width << s->block_max_depth;
    const int rem_depth= s->block_max_depth - level;
    const int index= (x + y*w) << rem_depth;
    static BlockNode null_block= { //FIXME add border maybe
        .color= {128,128,128},
        .mx= 0,
        .my= 0,
        .type= 0,
        .level= 0,
    };
    int trx= (x+1)<<rem_depth;
    BlockNode *left  = x ? &s->block[index-1] : &null_block;
    BlockNode *top   = y ? &s->block[index-w] : &null_block;
    BlockNode *tl    = y && x ? &s->block[index-w-1] : left;
    BlockNode *tr    = y && trx<w && ((x&1)==0 || level==0) ? &s->block[index-w+(1<<rem_depth)] : tl; //FIXME use lt
    int s_context= 2*left->level + 2*top->level + tl->level + tr->level;
    
    if(s->keyframe){
        set_blocks(s, level, x, y, null_block.color[0], null_block.color[1], null_block.color[2], null_block.mx, null_block.my, BLOCK_INTRA);
        return;
    }

    if(level==s->block_max_depth || get_rac(&s->c, &s->block_state[4 + s_context])){
        int type;
        int l = left->color[0];
        int cb= left->color[1];
        int cr= left->color[2];
        int mx= mid_pred(left->mx, top->mx, tr->mx);
        int my= mid_pred(left->my, top->my, tr->my);
        int mx_context= av_log2(2*ABS(left->mx - top->mx)) + 0*av_log2(2*ABS(tr->mx - top->mx));
        int my_context= av_log2(2*ABS(left->my - top->my)) + 0*av_log2(2*ABS(tr->my - top->my));
        
        type= get_rac(&s->c, &s->block_state[1 + left->type + top->type]) ? BLOCK_INTRA : 0;

        if(type){
            l += get_symbol(&s->c, &s->block_state[32], 1);
            cb+= get_symbol(&s->c, &s->block_state[64], 1);
            cr+= get_symbol(&s->c, &s->block_state[96], 1);
        }else{
            mx+= get_symbol(&s->c, &s->block_state[128 + 32*mx_context], 1);
            my+= get_symbol(&s->c, &s->block_state[128 + 32*my_context], 1);
        }
        set_blocks(s, level, x, y, l, cb, cr, mx, my, type);
    }else{
        decode_q_branch(s, level+1, 2*x+0, 2*y+0);
        decode_q_branch(s, level+1, 2*x+1, 2*y+0);
        decode_q_branch(s, level+1, 2*x+0, 2*y+1);
        decode_q_branch(s, level+1, 2*x+1, 2*y+1);
    }
}

static void encode_blocks(SnowContext *s){
    int x, y;
    int w= s->b_width;
    int h= s->b_height;

    for(y=0; y<h; y++){
        if(s->c.bytestream_end - s->c.bytestream < w*MB_SIZE*MB_SIZE*3){ //FIXME nicer limit
            av_log(s->avctx, AV_LOG_ERROR, "encoded frame too large\n");
            return;
        }
        for(x=0; x<w; x++){
            encode_q_branch(s, 0, x, y);
        }
    }
}

static void decode_blocks(SnowContext *s){
    int x, y;
    int w= s->b_width;
    int h= s->b_height;

    for(y=0; y<h; y++){
        for(x=0; x<w; x++){
            decode_q_branch(s, 0, x, y);
        }
    }
}

static void mc_block(uint8_t *dst, uint8_t *src, uint8_t *tmp, int stride, int b_w, int b_h, int dx, int dy){
    int x, y;
START_TIMER
    for(y=0; y < b_h+5; y++){
        for(x=0; x < b_w; x++){
            int a0= src[x    ];
            int a1= src[x + 1];
            int a2= src[x + 2];
            int a3= src[x + 3];
            int a4= src[x + 4];
            int a5= src[x + 5];
//            int am= 9*(a1+a2) - (a0+a3);
            int am= 20*(a2+a3) - 5*(a1+a4) + (a0+a5);
//            int am= 18*(a2+a3) - 2*(a1+a4);
//             int aL= (-7*a0 + 105*a1 + 35*a2 - 5*a3)>>3;
//             int aR= (-7*a3 + 105*a2 + 35*a1 - 5*a0)>>3;

//            if(b_w==16) am= 8*(a1+a2);

            if(dx<8) am = (32*a2*( 8-dx) +    am* dx    + 128)>>8;
            else     am = (   am*(16-dx) + 32*a3*(dx-8) + 128)>>8;
            
            /* FIXME Try increasing tmp buffer to 16 bits and not clipping here. Should give marginally better results. - Robert*/
            if(am&(~255)) am= ~(am>>31);
            
            tmp[x] = am;

/*            if     (dx< 4) tmp[x + y*stride]= (16*a1*( 4-dx) +    aL* dx     + 32)>>6;
            else if(dx< 8) tmp[x + y*stride]= (   aL*( 8-dx) +    am*(dx- 4) + 32)>>6;
            else if(dx<12) tmp[x + y*stride]= (   am*(12-dx) +    aR*(dx- 8) + 32)>>6;
            else           tmp[x + y*stride]= (   aR*(16-dx) + 16*a2*(dx-12) + 32)>>6;*/
        }
        tmp += stride;
        src += stride;
    }
    tmp -= (b_h+5)*stride;
    
    for(y=0; y < b_h; y++){
        for(x=0; x < b_w; x++){
            int a0= tmp[x + 0*stride];
            int a1= tmp[x + 1*stride];
            int a2= tmp[x + 2*stride];
            int a3= tmp[x + 3*stride];
            int a4= tmp[x + 4*stride];
            int a5= tmp[x + 5*stride];
            int am= 20*(a2+a3) - 5*(a1+a4) + (a0+a5);
//            int am= 18*(a2+a3) - 2*(a1+a4);
/*            int aL= (-7*a0 + 105*a1 + 35*a2 - 5*a3)>>3;
            int aR= (-7*a3 + 105*a2 + 35*a1 - 5*a0)>>3;*/
            
//            if(b_w==16) am= 8*(a1+a2);

            if(dy<8) am =  (32*a2*( 8-dy) +    am* dy    + 128)>>8;
            else     am = (   am*(16-dy) + 32*a3*(dy-8) + 128)>>8;

            if(am&(~255)) am= ~(am>>31);
            
            dst[x] = am;
/*            if     (dy< 4) tmp[x + y*stride]= (16*a1*( 4-dy) +    aL* dy     + 32)>>6;
            else if(dy< 8) tmp[x + y*stride]= (   aL*( 8-dy) +    am*(dy- 4) + 32)>>6;
            else if(dy<12) tmp[x + y*stride]= (   am*(12-dy) +    aR*(dy- 8) + 32)>>6;
            else           tmp[x + y*stride]= (   aR*(16-dy) + 16*a2*(dy-12) + 32)>>6;*/
        }
        dst += stride;
        tmp += stride;
    }
STOP_TIMER("mc_block")
}

#define mca(dx,dy,b_w)\
static void mc_block_hpel ## dx ## dy ## b_w(uint8_t *dst, uint8_t *src, int stride, int h){\
    uint8_t tmp[stride*(b_w+5)];\
    assert(h==b_w);\
    mc_block(dst, src-2-2*stride, tmp, stride, b_w, b_w, dx, dy);\
}

mca( 0, 0,16)
mca( 8, 0,16)
mca( 0, 8,16)
mca( 8, 8,16)
mca( 0, 0,8)
mca( 8, 0,8)
mca( 0, 8,8)
mca( 8, 8,8)

static void pred_block(SnowContext *s, uint8_t *dst, uint8_t *src, uint8_t *tmp, int stride, int sx, int sy, int b_w, int b_h, BlockNode *block, int plane_index, int w, int h){
    if(block->type){
        int x, y;
        const int color= block->color[plane_index];
        for(y=0; y < b_h; y++){
            for(x=0; x < b_w; x++){
                dst[x + y*stride]= color;
            }
        }
    }else{
        const int scale= plane_index ?  s->mv_scale : 2*s->mv_scale;
        int mx= block->mx*scale;
        int my= block->my*scale;
        const int dx= mx&15;
        const int dy= my&15;
        sx += (mx>>4) - 2;
        sy += (my>>4) - 2;
        src += sx + sy*stride;
        if(   (unsigned)sx >= w - b_w - 4
           || (unsigned)sy >= h - b_h - 4){
            ff_emulated_edge_mc(tmp + MB_SIZE, src, stride, b_w+5, b_h+5, sx, sy, w, h);
            src= tmp + MB_SIZE;
        }
        if((dx&3) || (dy&3) || b_w!=b_h || (b_w!=4 && b_w!=8 && b_w!=16))
            mc_block(dst, src, tmp, stride, b_w, b_h, dx, dy);
        else
            s->dsp.put_h264_qpel_pixels_tab[2-(b_w>>3)][dy+(dx>>2)](dst,src + 2 + 2*stride,stride);
    }
}

static always_inline int same_block(BlockNode *a, BlockNode *b){
    return !((a->mx - b->mx) | (a->my - b->my) | a->type | b->type);
}

//FIXME name clenup (b_w, block_w, b_width stuff)
static always_inline void add_yblock_buffered(SnowContext *s, slice_buffer * sb, DWTELEM *old_dst, uint8_t *dst8, uint8_t *src, uint8_t *obmc, int src_x, int src_y, int b_w, int b_h, int w, int h, int dst_stride, int src_stride, int obmc_stride, int b_x, int b_y, int add, int plane_index){
    DWTELEM * dst = NULL;
    const int b_width = s->b_width  << s->block_max_depth;
    const int b_height= s->b_height << s->block_max_depth;
    const int b_stride= b_width;
    BlockNode *lt= &s->block[b_x + b_y*b_stride];
    BlockNode *rt= lt+1;
    BlockNode *lb= lt+b_stride;
    BlockNode *rb= lb+1;
    uint8_t *block[4]; 
    int tmp_step= src_stride >= 7*MB_SIZE ? MB_SIZE : MB_SIZE*src_stride;
    uint8_t tmp[src_stride*7*MB_SIZE]; //FIXME align
    uint8_t *ptmp;
    int x,y;

    if(b_x<0){
        lt= rt;
        lb= rb;
    }else if(b_x + 1 >= b_width){
        rt= lt;
        rb= lb;
    }
    if(b_y<0){
        lt= lb;
        rt= rb;
    }else if(b_y + 1 >= b_height){
        lb= lt;
        rb= rt;
    }
        
    if(src_x<0){ //FIXME merge with prev & always round internal width upto *16
        obmc -= src_x;
        b_w += src_x;
        src_x=0;
    }else if(src_x + b_w > w){
        b_w = w - src_x;
    }
    if(src_y<0){
        obmc -= src_y*obmc_stride;
        b_h += src_y;
        src_y=0;
    }else if(src_y + b_h> h){
        b_h = h - src_y;
    }
    
    if(b_w<=0 || b_h<=0) return;

assert(src_stride > 2*MB_SIZE + 5);
//    old_dst += src_x + src_y*dst_stride;
    dst8+= src_x + src_y*src_stride;
//    src += src_x + src_y*src_stride;

    ptmp= tmp + 3*tmp_step;
    block[0]= ptmp;
    ptmp+=tmp_step;
    pred_block(s, block[0], src, tmp, src_stride, src_x, src_y, b_w, b_h, lt, plane_index, w, h);    

    if(same_block(lt, rt)){
        block[1]= block[0];
    }else{
        block[1]= ptmp;
        ptmp+=tmp_step;
        pred_block(s, block[1], src, tmp, src_stride, src_x, src_y, b_w, b_h, rt, plane_index, w, h);
    }
        
    if(same_block(lt, lb)){
        block[2]= block[0];
    }else if(same_block(rt, lb)){
        block[2]= block[1];
    }else{
        block[2]= ptmp;
        ptmp+=tmp_step;
        pred_block(s, block[2], src, tmp, src_stride, src_x, src_y, b_w, b_h, lb, plane_index, w, h);
    }

    if(same_block(lt, rb) ){
        block[3]= block[0];
    }else if(same_block(rt, rb)){
        block[3]= block[1];
    }else if(same_block(lb, rb)){
        block[3]= block[2];
    }else{
        block[3]= ptmp;
        pred_block(s, block[3], src, tmp, src_stride, src_x, src_y, b_w, b_h, rb, plane_index, w, h);
    }
#if 0
    for(y=0; y<b_h; y++){
        for(x=0; x<b_w; x++){
            int v=   obmc [x + y*obmc_stride] * block[3][x + y*src_stride] * (256/OBMC_MAX);
            if(add) dst[x + y*dst_stride] += v;
            else    dst[x + y*dst_stride] -= v;
        }
    }
    for(y=0; y<b_h; y++){
        uint8_t *obmc2= obmc + (obmc_stride>>1);
        for(x=0; x<b_w; x++){
            int v=   obmc2[x + y*obmc_stride] * block[2][x + y*src_stride] * (256/OBMC_MAX);
            if(add) dst[x + y*dst_stride] += v;
            else    dst[x + y*dst_stride] -= v;
        }
    }
    for(y=0; y<b_h; y++){
        uint8_t *obmc3= obmc + obmc_stride*(obmc_stride>>1);
        for(x=0; x<b_w; x++){
            int v=   obmc3[x + y*obmc_stride] * block[1][x + y*src_stride] * (256/OBMC_MAX);
            if(add) dst[x + y*dst_stride] += v;
            else    dst[x + y*dst_stride] -= v;
        }
    }
    for(y=0; y<b_h; y++){
        uint8_t *obmc3= obmc + obmc_stride*(obmc_stride>>1);
        uint8_t *obmc4= obmc3+ (obmc_stride>>1);
        for(x=0; x<b_w; x++){
            int v=   obmc4[x + y*obmc_stride] * block[0][x + y*src_stride] * (256/OBMC_MAX);
            if(add) dst[x + y*dst_stride] += v;
            else    dst[x + y*dst_stride] -= v;
        }
    }
#else
{

    START_TIMER
    
    for(y=0; y<b_h; y++){
        //FIXME ugly missue of obmc_stride
        uint8_t *obmc1= obmc + y*obmc_stride;
        uint8_t *obmc2= obmc1+ (obmc_stride>>1);
        uint8_t *obmc3= obmc1+ obmc_stride*(obmc_stride>>1);
        uint8_t *obmc4= obmc3+ (obmc_stride>>1);
        dst = slice_buffer_get_line(sb, src_y + y);
        for(x=0; x<b_w; x++){
            int v=   obmc1[x] * block[3][x + y*src_stride]
                    +obmc2[x] * block[2][x + y*src_stride]
                    +obmc3[x] * block[1][x + y*src_stride]
                    +obmc4[x] * block[0][x + y*src_stride];

            v <<= 8 - LOG2_OBMC_MAX;
            if(FRAC_BITS != 8){
                v += 1<<(7 - FRAC_BITS);
                v >>= 8 - FRAC_BITS;
            }
            if(add){
//                v += old_dst[x + y*dst_stride];
                v += dst[x + src_x];
                v = (v + (1<<(FRAC_BITS-1))) >> FRAC_BITS;
                if(v&(~255)) v= ~(v>>31);
                dst8[x + y*src_stride] = v;
            }else{
//                old_dst[x + y*dst_stride] -= v;
                dst[x + src_x] -= v;
            }
        }
    }
        STOP_TIMER("Inner add y block")
}
#endif
}

//FIXME name clenup (b_w, block_w, b_width stuff)
static always_inline void add_yblock(SnowContext *s, DWTELEM *dst, uint8_t *dst8, uint8_t *src, uint8_t *obmc, int src_x, int src_y, int b_w, int b_h, int w, int h, int dst_stride, int src_stride, int obmc_stride, int b_x, int b_y, int add, int plane_index){
    const int b_width = s->b_width  << s->block_max_depth;
    const int b_height= s->b_height << s->block_max_depth;
    const int b_stride= b_width;
    BlockNode *lt= &s->block[b_x + b_y*b_stride];
    BlockNode *rt= lt+1;
    BlockNode *lb= lt+b_stride;
    BlockNode *rb= lb+1;
    uint8_t *block[4]; 
    int tmp_step= src_stride >= 7*MB_SIZE ? MB_SIZE : MB_SIZE*src_stride;
    uint8_t tmp[src_stride*7*MB_SIZE]; //FIXME align
    uint8_t *ptmp;
    int x,y;

    if(b_x<0){
        lt= rt;
        lb= rb;
    }else if(b_x + 1 >= b_width){
        rt= lt;
        rb= lb;
    }
    if(b_y<0){
        lt= lb;
        rt= rb;
    }else if(b_y + 1 >= b_height){
        lb= lt;
        rb= rt;
    }
        
    if(src_x<0){ //FIXME merge with prev & always round internal width upto *16
        obmc -= src_x;
        b_w += src_x;
        src_x=0;
    }else if(src_x + b_w > w){
        b_w = w - src_x;
    }
    if(src_y<0){
        obmc -= src_y*obmc_stride;
        b_h += src_y;
        src_y=0;
    }else if(src_y + b_h> h){
        b_h = h - src_y;
    }
    
    if(b_w<=0 || b_h<=0) return;

assert(src_stride > 2*MB_SIZE + 5);
    dst += src_x + src_y*dst_stride;
    dst8+= src_x + src_y*src_stride;
//    src += src_x + src_y*src_stride;

    ptmp= tmp + 3*tmp_step;
    block[0]= ptmp;
    ptmp+=tmp_step;
    pred_block(s, block[0], src, tmp, src_stride, src_x, src_y, b_w, b_h, lt, plane_index, w, h);    

    if(same_block(lt, rt)){
        block[1]= block[0];
    }else{
        block[1]= ptmp;
        ptmp+=tmp_step;
        pred_block(s, block[1], src, tmp, src_stride, src_x, src_y, b_w, b_h, rt, plane_index, w, h);
    }
        
    if(same_block(lt, lb)){
        block[2]= block[0];
    }else if(same_block(rt, lb)){
        block[2]= block[1];
    }else{
        block[2]= ptmp;
        ptmp+=tmp_step;
        pred_block(s, block[2], src, tmp, src_stride, src_x, src_y, b_w, b_h, lb, plane_index, w, h);
    }

    if(same_block(lt, rb) ){
        block[3]= block[0];
    }else if(same_block(rt, rb)){
        block[3]= block[1];
    }else if(same_block(lb, rb)){
        block[3]= block[2];
    }else{
        block[3]= ptmp;
        pred_block(s, block[3], src, tmp, src_stride, src_x, src_y, b_w, b_h, rb, plane_index, w, h);
    }
#if 0
    for(y=0; y<b_h; y++){
        for(x=0; x<b_w; x++){
            int v=   obmc [x + y*obmc_stride] * block[3][x + y*src_stride] * (256/OBMC_MAX);
            if(add) dst[x + y*dst_stride] += v;
            else    dst[x + y*dst_stride] -= v;
        }
    }
    for(y=0; y<b_h; y++){
        uint8_t *obmc2= obmc + (obmc_stride>>1);
        for(x=0; x<b_w; x++){
            int v=   obmc2[x + y*obmc_stride] * block[2][x + y*src_stride] * (256/OBMC_MAX);
            if(add) dst[x + y*dst_stride] += v;
            else    dst[x + y*dst_stride] -= v;
        }
    }
    for(y=0; y<b_h; y++){
        uint8_t *obmc3= obmc + obmc_stride*(obmc_stride>>1);
        for(x=0; x<b_w; x++){
            int v=   obmc3[x + y*obmc_stride] * block[1][x + y*src_stride] * (256/OBMC_MAX);
            if(add) dst[x + y*dst_stride] += v;
            else    dst[x + y*dst_stride] -= v;
        }
    }
    for(y=0; y<b_h; y++){
        uint8_t *obmc3= obmc + obmc_stride*(obmc_stride>>1);
        uint8_t *obmc4= obmc3+ (obmc_stride>>1);
        for(x=0; x<b_w; x++){
            int v=   obmc4[x + y*obmc_stride] * block[0][x + y*src_stride] * (256/OBMC_MAX);
            if(add) dst[x + y*dst_stride] += v;
            else    dst[x + y*dst_stride] -= v;
        }
    }
#else
    for(y=0; y<b_h; y++){
        //FIXME ugly missue of obmc_stride
        uint8_t *obmc1= obmc + y*obmc_stride;
        uint8_t *obmc2= obmc1+ (obmc_stride>>1);
        uint8_t *obmc3= obmc1+ obmc_stride*(obmc_stride>>1);
        uint8_t *obmc4= obmc3+ (obmc_stride>>1);
        for(x=0; x<b_w; x++){
            int v=   obmc1[x] * block[3][x + y*src_stride]
                    +obmc2[x] * block[2][x + y*src_stride]
                    +obmc3[x] * block[1][x + y*src_stride]
                    +obmc4[x] * block[0][x + y*src_stride];
            
            v <<= 8 - LOG2_OBMC_MAX;
            if(FRAC_BITS != 8){
                v += 1<<(7 - FRAC_BITS);
                v >>= 8 - FRAC_BITS;
            }
            if(add){
                v += dst[x + y*dst_stride];
                v = (v + (1<<(FRAC_BITS-1))) >> FRAC_BITS;
                if(v&(~255)) v= ~(v>>31);
                dst8[x + y*src_stride] = v;
            }else{
                dst[x + y*dst_stride] -= v;
            }
        }
    }
#endif
}

static always_inline void predict_slice_buffered(SnowContext *s, slice_buffer * sb, DWTELEM * old_buffer, int plane_index, int add, int mb_y){
    Plane *p= &s->plane[plane_index];
    const int mb_w= s->b_width  << s->block_max_depth;
    const int mb_h= s->b_height << s->block_max_depth;
    int x, y, mb_x;
    int block_size = MB_SIZE >> s->block_max_depth;
    int block_w    = plane_index ? block_size/2 : block_size;
    const uint8_t *obmc  = plane_index ? obmc_tab[s->block_max_depth+1] : obmc_tab[s->block_max_depth];
    int obmc_stride= plane_index ? block_size : 2*block_size;
    int ref_stride= s->current_picture.linesize[plane_index];
    uint8_t *ref  = s->last_picture.data[plane_index];
    uint8_t *dst8= s->current_picture.data[plane_index];
    int w= p->width;
    int h= p->height;
    START_TIMER
    
    if(s->keyframe || (s->avctx->debug&512)){
        if(mb_y==mb_h)
            return;

        if(add){
            for(y=block_w*mb_y; y<FFMIN(h,block_w*(mb_y+1)); y++)
            {
//                DWTELEM * line = slice_buffer_get_line(sb, y);
                DWTELEM * line = sb->line[y];
                for(x=0; x<w; x++)
                {
//                    int v= buf[x + y*w] + (128<<FRAC_BITS) + (1<<(FRAC_BITS-1));
                    int v= line[x] + (128<<FRAC_BITS) + (1<<(FRAC_BITS-1));
                    v >>= FRAC_BITS;
                    if(v&(~255)) v= ~(v>>31);
                    dst8[x + y*ref_stride]= v;
                }
            }
        }else{
            for(y=block_w*mb_y; y<FFMIN(h,block_w*(mb_y+1)); y++)
            {
//                DWTELEM * line = slice_buffer_get_line(sb, y);
                DWTELEM * line = sb->line[y];
                for(x=0; x<w; x++)
                {
                    line[x] -= 128 << FRAC_BITS;
//                    buf[x + y*w]-= 128<<FRAC_BITS;
                }
            }
        }

        return;
    }
    
        for(mb_x=0; mb_x<=mb_w; mb_x++){
            START_TIMER

            add_yblock_buffered(s, sb, old_buffer, dst8, ref, obmc, 
                       block_w*mb_x - block_w/2,
                       block_w*mb_y - block_w/2,
                       block_w, block_w,
                       w, h,
                       w, ref_stride, obmc_stride,
                       mb_x - 1, mb_y - 1,
                       add, plane_index);
            
            STOP_TIMER("add_yblock")
        }
    
    STOP_TIMER("predict_slice")
}

static always_inline void predict_slice(SnowContext *s, DWTELEM *buf, int plane_index, int add, int mb_y){
    Plane *p= &s->plane[plane_index];
    const int mb_w= s->b_width  << s->block_max_depth;
    const int mb_h= s->b_height << s->block_max_depth;
    int x, y, mb_x;
    int block_size = MB_SIZE >> s->block_max_depth;
    int block_w    = plane_index ? block_size/2 : block_size;
    const uint8_t *obmc  = plane_index ? obmc_tab[s->block_max_depth+1] : obmc_tab[s->block_max_depth];
    int obmc_stride= plane_index ? block_size : 2*block_size;
    int ref_stride= s->current_picture.linesize[plane_index];
    uint8_t *ref  = s->last_picture.data[plane_index];
    uint8_t *dst8= s->current_picture.data[plane_index];
    int w= p->width;
    int h= p->height;
    START_TIMER
    
    if(s->keyframe || (s->avctx->debug&512)){
        if(mb_y==mb_h)
            return;

        if(add){
            for(y=block_w*mb_y; y<FFMIN(h,block_w*(mb_y+1)); y++){
                for(x=0; x<w; x++){
                    int v= buf[x + y*w] + (128<<FRAC_BITS) + (1<<(FRAC_BITS-1));
                    v >>= FRAC_BITS;
                    if(v&(~255)) v= ~(v>>31);
                    dst8[x + y*ref_stride]= v;
                }
            }
        }else{
            for(y=block_w*mb_y; y<FFMIN(h,block_w*(mb_y+1)); y++){
                for(x=0; x<w; x++){
                    buf[x + y*w]-= 128<<FRAC_BITS;
                }
            }
        }

        return;
    }
    
        for(mb_x=0; mb_x<=mb_w; mb_x++){
            START_TIMER

            add_yblock(s, buf, dst8, ref, obmc, 
                       block_w*mb_x - block_w/2,
                       block_w*mb_y - block_w/2,
                       block_w, block_w,
                       w, h,
                       w, ref_stride, obmc_stride,
                       mb_x - 1, mb_y - 1,
                       add, plane_index);
            
            STOP_TIMER("add_yblock")
        }
    
    STOP_TIMER("predict_slice")
}

static always_inline void predict_plane(SnowContext *s, DWTELEM *buf, int plane_index, int add){
    const int mb_h= s->b_height << s->block_max_depth;
    int mb_y;
    for(mb_y=0; mb_y<=mb_h; mb_y++)
        predict_slice(s, buf, plane_index, add, mb_y);
}

static void quantize(SnowContext *s, SubBand *b, DWTELEM *src, int stride, int bias){
    const int level= b->level;
    const int w= b->width;
    const int h= b->height;
    const int qlog= clip(s->qlog + b->qlog, 0, QROOT*16);
    const int qmul= qexp[qlog&(QROOT-1)]<<(qlog>>QSHIFT);
    int x,y, thres1, thres2;
//    START_TIMER

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

static void dequantize_slice_buffered(SnowContext *s, slice_buffer * sb, SubBand *b, DWTELEM *src, int stride, int start_y, int end_y){
    const int w= b->width;
    const int qlog= clip(s->qlog + b->qlog, 0, QROOT*16);
    const int qmul= qexp[qlog&(QROOT-1)]<<(qlog>>QSHIFT);
    const int qadd= (s->qbias*qmul)>>QBIAS_SHIFT;
    int x,y;
    START_TIMER
    
    if(s->qlog == LOSSLESS_QLOG) return;
    
    for(y=start_y; y<end_y; y++){
//        DWTELEM * line = slice_buffer_get_line_from_address(sb, src + (y * stride));
        DWTELEM * line = slice_buffer_get_line(sb, (y * b->stride_line) + b->buf_y_offset) + b->buf_x_offset;
        for(x=0; x<w; x++){
            int i= line[x];
            if(i<0){
                line[x]= -((-i*qmul + qadd)>>(QEXPSHIFT)); //FIXME try different bias
            }else if(i>0){
                line[x]=  (( i*qmul + qadd)>>(QEXPSHIFT));
            }
        }
    }
    if(w > 200 /*level+1 == s->spatial_decomposition_count*/){
        STOP_TIMER("dquant")
    }
}

static void dequantize(SnowContext *s, SubBand *b, DWTELEM *src, int stride){
    const int w= b->width;
    const int h= b->height;
    const int qlog= clip(s->qlog + b->qlog, 0, QROOT*16);
    const int qmul= qexp[qlog&(QROOT-1)]<<(qlog>>QSHIFT);
    const int qadd= (s->qbias*qmul)>>QBIAS_SHIFT;
    int x,y;
    START_TIMER
    
    if(s->qlog == LOSSLESS_QLOG) return;
    
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
    if(w > 200 /*level+1 == s->spatial_decomposition_count*/){
        STOP_TIMER("dquant")
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

static void correlate_slice_buffered(SnowContext *s, slice_buffer * sb, SubBand *b, DWTELEM *src, int stride, int inverse, int use_median, int start_y, int end_y){
    const int w= b->width;
    int x,y;
    
//    START_TIMER
    
    DWTELEM * line;
    DWTELEM * prev;
    
    if (start_y != 0)
        line = slice_buffer_get_line(sb, ((start_y - 1) * b->stride_line) + b->buf_y_offset) + b->buf_x_offset;
    
    for(y=start_y; y<end_y; y++){
        prev = line;
//        line = slice_buffer_get_line_from_address(sb, src + (y * stride));
        line = slice_buffer_get_line(sb, (y * b->stride_line) + b->buf_y_offset) + b->buf_x_offset;
        for(x=0; x<w; x++){
            if(x){
                if(use_median){
                    if(y && x+1<w) line[x] += mid_pred(line[x - 1], prev[x], prev[x + 1]);
                    else  line[x] += line[x - 1];
                }else{
                    if(y) line[x] += mid_pred(line[x - 1], prev[x], line[x - 1] + prev[x] - prev[x - 1]);
                    else  line[x] += line[x - 1];
                }
            }else{
                if(y) line[x] += prev[x];
            }
        }
    }
    
//    STOP_TIMER("correlate")
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
    uint8_t kstate[32]; 
    
    memset(kstate, MID_STATE, sizeof(kstate));   

    put_rac(&s->c, kstate, s->keyframe);
    if(s->keyframe || s->always_reset)
        reset_contexts(s);
    if(s->keyframe){
        put_symbol(&s->c, s->header_state, s->version, 0);
        put_rac(&s->c, s->header_state, s->always_reset);
        put_symbol(&s->c, s->header_state, s->temporal_decomposition_type, 0);
        put_symbol(&s->c, s->header_state, s->temporal_decomposition_count, 0);
        put_symbol(&s->c, s->header_state, s->spatial_decomposition_count, 0);
        put_symbol(&s->c, s->header_state, s->colorspace_type, 0);
        put_symbol(&s->c, s->header_state, s->chroma_h_shift, 0);
        put_symbol(&s->c, s->header_state, s->chroma_v_shift, 0);
        put_rac(&s->c, s->header_state, s->spatial_scalability);
//        put_rac(&s->c, s->header_state, s->rate_scalability);

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
    put_symbol(&s->c, s->header_state, s->block_max_depth, 0);
}

static int decode_header(SnowContext *s){
    int plane_index, level, orientation;
    uint8_t kstate[32];

    memset(kstate, MID_STATE, sizeof(kstate));   

    s->keyframe= get_rac(&s->c, kstate);
    if(s->keyframe || s->always_reset)
        reset_contexts(s);
    if(s->keyframe){
        s->version= get_symbol(&s->c, s->header_state, 0);
        if(s->version>0){
            av_log(s->avctx, AV_LOG_ERROR, "version %d not supported", s->version);
            return -1;
        }
        s->always_reset= get_rac(&s->c, s->header_state);
        s->temporal_decomposition_type= get_symbol(&s->c, s->header_state, 0);
        s->temporal_decomposition_count= get_symbol(&s->c, s->header_state, 0);
        s->spatial_decomposition_count= get_symbol(&s->c, s->header_state, 0);
        s->colorspace_type= get_symbol(&s->c, s->header_state, 0);
        s->chroma_h_shift= get_symbol(&s->c, s->header_state, 0);
        s->chroma_v_shift= get_symbol(&s->c, s->header_state, 0);
        s->spatial_scalability= get_rac(&s->c, s->header_state);
//        s->rate_scalability= get_rac(&s->c, s->header_state);

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
    s->block_max_depth= get_symbol(&s->c, s->header_state, 0);

    return 0;
}

static void init_qexp(){
    int i;
    double v=128;

    for(i=0; i<QROOT; i++){
        qexp[i]= lrintf(v);
        v *= pow(2, 1.0 / QROOT); 
    }
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
        s->dsp.put_h264_qpel_pixels_tab[0][dy+dx/4];\
    s->dsp.put_qpel_pixels_tab       [1][dy+dx/4]=\
    s->dsp.put_no_rnd_qpel_pixels_tab[1][dy+dx/4]=\
        s->dsp.put_h264_qpel_pixels_tab[1][dy+dx/4];

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
        mc_block_hpel ## dx ## dy ## 16;\
    s->dsp.put_pixels_tab       [1][dy/4+dx/8]=\
    s->dsp.put_no_rnd_pixels_tab[1][dy/4+dx/8]=\
        mc_block_hpel ## dx ## dy ## 8;

    mcfh(0, 0)
    mcfh(8, 0)
    mcfh(0, 8)
    mcfh(8, 8)

    if(!qexp[0])
        init_qexp();

    dec= s->spatial_decomposition_count= 5;
    s->spatial_decomposition_type= avctx->prediction_method; //FIXME add decorrelator type r transform_type
    
    s->chroma_h_shift= 1; //FIXME XXX
    s->chroma_v_shift= 1;
    
//    dec += FFMAX(s->chroma_h_shift, s->chroma_v_shift);
    
    width= s->avctx->width;
    height= s->avctx->height;

    s->spatial_dwt_buffer= av_mallocz(width*height*sizeof(DWTELEM));
    
    s->mv_scale= (s->avctx->flags & CODEC_FLAG_QPEL) ? 2 : 4;
    s->block_max_depth= (s->avctx->flags & CODEC_FLAG_4MV) ? 1 : 0;
    
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
                
                b->stride_line = 1 << (s->spatial_decomposition_count - level);
                b->buf_x_offset = 0;
                b->buf_y_offset = 0;
                
                if(orientation&1){
                    b->buf += (w+1)>>1;
                    b->buf_x_offset = (w+1)>>1;
                }
                if(orientation>1){
                    b->buf += b->stride>>1;
                    b->buf_y_offset = b->stride_line >> 1;
                }
                
                if(level)
                    b->parent= &s->plane[plane_index].band[level-1][orientation];
                b->x_coeff=av_mallocz(((b->width+1) * b->height+1)*sizeof(x_and_coeff));
            }
            w= (w+1)>>1;
            h= (h+1)>>1;
        }
    }
    
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
    int level, orientation, x, y;

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
    int plane_index;

    if(avctx->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL){
        av_log(avctx, AV_LOG_ERROR, "this codec is under development, files encoded with it may not be decodable with future versions!!!\n"
               "use vstrict=-2 / -strict -2 to use it anyway\n");
        return -1;
    }
 
    common_init(avctx);
    alloc_blocks(s);
 
    s->version=0;
    
    s->m.avctx   = avctx;
    s->m.flags   = avctx->flags;
    s->m.bit_rate= avctx->bit_rate;

    s->m.me.scratchpad= av_mallocz((avctx->width+64)*2*16*2*sizeof(uint8_t));
    s->m.me.map       = av_mallocz(ME_MAP_SIZE*sizeof(uint32_t));
    s->m.me.score_map = av_mallocz(ME_MAP_SIZE*sizeof(uint32_t));
    h263_encode_init(&s->m); //mv_penalty

    if(avctx->flags&CODEC_FLAG_PASS1){
        if(!avctx->stats_out)
            avctx->stats_out = av_mallocz(256);
    }
    if(avctx->flags&CODEC_FLAG_PASS2){
        if(ff_rate_control_init(&s->m) < 0)
            return -1;
    }

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
   int w= s->avctx->width; //FIXME round up to x16 ?
   int h= s->avctx->height;

    if(s->current_picture.data[0]){
        draw_edges(s->current_picture.data[0], s->current_picture.linesize[0], w   , h   , EDGE_WIDTH  );
        draw_edges(s->current_picture.data[1], s->current_picture.linesize[1], w>>1, h>>1, EDGE_WIDTH/2);
        draw_edges(s->current_picture.data[2], s->current_picture.linesize[2], w>>1, h>>1, EDGE_WIDTH/2);
    }

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
    RangeCoder * const c= &s->c;
    AVFrame *pict = data;
    const int width= s->avctx->width;
    const int height= s->avctx->height;
    int level, orientation, plane_index;

    ff_init_range_encoder(c, buf, buf_size);
    ff_build_rac_states(c, 0.05*(1LL<<32), 256-8);
    
    s->input_picture = *pict;

    if(avctx->flags&CODEC_FLAG_PASS2){
        s->m.pict_type =
        pict->pict_type= s->m.rc_context.entry[avctx->frame_number].new_pict_type;
        s->keyframe= pict->pict_type==FF_I_TYPE;
        s->m.picture_number= avctx->frame_number;
        pict->quality= ff_rate_estimate_qscale(&s->m);
    }else{
        s->keyframe= avctx->gop_size==0 || avctx->frame_number % avctx->gop_size == 0;
        pict->pict_type= s->keyframe ? FF_I_TYPE : FF_P_TYPE;
    }
    
    if(pict->quality){
        s->qlog= rint(QROOT*log(pict->quality / (float)FF_QP2LAMBDA)/log(2));
        //<64 >60
        s->qlog += 61*QROOT/8;
    }else{
        s->qlog= LOSSLESS_QLOG;
    }

    frame_start(s);
    s->current_picture.key_frame= s->keyframe;

    s->m.current_picture_ptr= &s->m.current_picture;
    if(pict->pict_type == P_TYPE){
        int block_width = (width +15)>>4;
        int block_height= (height+15)>>4;
        int stride= s->current_picture.linesize[0];
        
        assert(s->current_picture.data[0]);
        assert(s->last_picture.data[0]);
     
        s->m.avctx= s->avctx;
        s->m.current_picture.data[0]= s->current_picture.data[0];
        s->m.   last_picture.data[0]= s->   last_picture.data[0];
        s->m.    new_picture.data[0]= s->  input_picture.data[0];
        s->m.   last_picture_ptr= &s->m.   last_picture;
        s->m.linesize=
        s->m.   last_picture.linesize[0]=
        s->m.    new_picture.linesize[0]=
        s->m.current_picture.linesize[0]= stride;
        s->m.uvlinesize= s->current_picture.linesize[1];
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

        s->lambda = s->m.lambda= pict->quality * 3/2; //FIXME bug somewhere else
        s->m.qscale= (s->m.lambda*139 + FF_LAMBDA_SCALE*64) >> (FF_LAMBDA_SHIFT + 7);
        s->lambda2= s->m.lambda2= (s->m.lambda*s->m.lambda + FF_LAMBDA_SCALE/2) >> FF_LAMBDA_SHIFT;

        s->m.dsp= s->dsp; //move
        ff_init_me(&s->m);
    }
    
redo_frame:
        
    s->qbias= pict->pict_type == P_TYPE ? 2 : 0;

    encode_header(s);
    s->m.misc_bits = 8*(s->c.bytestream - s->c.bytestream_start);
    encode_blocks(s);
    s->m.mv_bits = 8*(s->c.bytestream - s->c.bytestream_start) - s->m.misc_bits;
      
    for(plane_index=0; plane_index<3; plane_index++){
        Plane *p= &s->plane[plane_index];
        int w= p->width;
        int h= p->height;
        int x, y;
//        int bits= put_bits_count(&s->c.pb);

        //FIXME optimize
     if(pict->data[plane_index]) //FIXME gray hack
        for(y=0; y<h; y++){
            for(x=0; x<w; x++){
                s->spatial_dwt_buffer[y*w + x]= pict->data[plane_index][y*pict->linesize[plane_index] + x]<<FRAC_BITS;
            }
        }
        predict_plane(s, s->spatial_dwt_buffer, plane_index, 0);
        
        if(   plane_index==0 
           && pict->pict_type == P_TYPE 
           && s->m.me.scene_change_score > s->avctx->scenechange_threshold){
            ff_init_range_encoder(c, buf, buf_size);
            ff_build_rac_states(c, 0.05*(1LL<<32), 256-8);
            pict->pict_type= FF_I_TYPE;
            s->keyframe=1;
            reset_contexts(s);
            goto redo_frame;
        }
        
        if(s->qlog == LOSSLESS_QLOG){
            for(y=0; y<h; y++){
                for(x=0; x<w; x++){
                    s->spatial_dwt_buffer[y*w + x]= (s->spatial_dwt_buffer[y*w + x] + (1<<(FRAC_BITS-1))-1)>>FRAC_BITS;
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
                    s->spatial_dwt_buffer[y*w + x]<<=FRAC_BITS;
                }
            }
        }
{START_TIMER
        predict_plane(s, s->spatial_dwt_buffer, plane_index, 1);
STOP_TIMER("pred-conv")}
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
            s->current_picture.error[plane_index] = error;
        }
    }

    if(s->last_picture.data[0])
        avctx->release_buffer(avctx, &s->last_picture);

    s->current_picture.coded_picture_number = avctx->frame_number;
    s->current_picture.pict_type = pict->pict_type;
    s->current_picture.quality = pict->quality;
    if(avctx->flags&CODEC_FLAG_PASS1){
        s->m.p_tex_bits = 8*(s->c.bytestream - s->c.bytestream_start) - s->m.misc_bits - s->m.mv_bits;
        s->m.current_picture.display_picture_number =
        s->m.current_picture.coded_picture_number = avctx->frame_number;
        s->m.pict_type = pict->pict_type;
        s->m.current_picture.quality = pict->quality;
        ff_write_pass1_stats(&s->m);
    }
    if(avctx->flags&CODEC_FLAG_PASS2){
        s->m.total_bits += 8*(s->c.bytestream - s->c.bytestream_start);
    }

    emms_c();
    
    return ff_rac_terminate(c);
}

static void common_end(SnowContext *s){
    int plane_index, level, orientation;

    av_freep(&s->spatial_dwt_buffer);

    av_freep(&s->m.me.scratchpad);    
    av_freep(&s->m.me.map);
    av_freep(&s->m.me.score_map);
 
    av_freep(&s->block);

    for(plane_index=0; plane_index<3; plane_index++){    
        for(level=s->spatial_decomposition_count-1; level>=0; level--){
            for(orientation=level ? 1 : 0; orientation<4; orientation++){
                SubBand *b= &s->plane[plane_index].band[level][orientation];
                
                av_freep(&b->x_coeff);
            }
        }
    }
}

static int encode_end(AVCodecContext *avctx)
{
    SnowContext *s = avctx->priv_data;

    common_end(s);
    av_free(avctx->stats_out);

    return 0;
}

static int decode_init(AVCodecContext *avctx)
{
    SnowContext *s = avctx->priv_data;
    int block_size;
    
    avctx->pix_fmt= PIX_FMT_YUV420P;

    common_init(avctx);
    
    block_size = MB_SIZE >> s->block_max_depth;
    slice_buffer_init(&s->sb, s->plane[0].height, (block_size) + (s->spatial_decomposition_count * (s->spatial_decomposition_count + 2)) + 1, s->plane[0].width, s->spatial_dwt_buffer);
    
    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size, uint8_t *buf, int buf_size){
    SnowContext *s = avctx->priv_data;
    RangeCoder * const c= &s->c;
    int bytes_read;
    AVFrame *picture = data;
    int level, orientation, plane_index;

    ff_init_range_decoder(c, buf, buf_size);
    ff_build_rac_states(c, 0.05*(1LL<<32), 256-8);

    s->current_picture.pict_type= FF_I_TYPE; //FIXME I vs. P
    decode_header(s);
    if(!s->block) alloc_blocks(s);

    frame_start(s);
    //keyframe flag dupliaction mess FIXME
    if(avctx->debug&FF_DEBUG_PICT_INFO)
        av_log(avctx, AV_LOG_ERROR, "keyframe:%d qlog:%d\n", s->keyframe, s->qlog);
    
    decode_blocks(s);

    for(plane_index=0; plane_index<3; plane_index++){
        Plane *p= &s->plane[plane_index];
        int w= p->width;
        int h= p->height;
        int x, y;
        int decode_state[MAX_DECOMPOSITIONS][4][1]; /* Stored state info for unpack_coeffs. 1 variable per instance. */
        
if(s->avctx->debug&2048){
        memset(s->spatial_dwt_buffer, 0, sizeof(DWTELEM)*w*h);
        predict_plane(s, s->spatial_dwt_buffer, plane_index, 1);

        for(y=0; y<h; y++){
            for(x=0; x<w; x++){
                int v= s->current_picture.data[plane_index][y*s->current_picture.linesize[plane_index] + x];
                s->mconly_picture.data[plane_index][y*s->mconly_picture.linesize[plane_index] + x]= v;
            }
        }
}

{   START_TIMER
    for(level=0; level<s->spatial_decomposition_count; level++){
        for(orientation=level ? 1 : 0; orientation<4; orientation++){
            SubBand *b= &p->band[level][orientation];
            unpack_coeffs(s, b, b->parent, orientation);
        }
    }
    STOP_TIMER("unpack coeffs");
}

{START_TIMER
    const int mb_h= s->b_height << s->block_max_depth;
    const int block_size = MB_SIZE >> s->block_max_depth;
    const int block_w    = plane_index ? block_size/2 : block_size;
    int mb_y;
    dwt_compose_t cs[MAX_DECOMPOSITIONS];
    int yd=0, yq=0;
    int y;
    int end_y;

    ff_spatial_idwt_buffered_init(cs, &s->sb, w, h, 1, s->spatial_decomposition_type, s->spatial_decomposition_count);
    for(mb_y=0; mb_y<=mb_h; mb_y++){
        
        int slice_starty = block_w*mb_y;
        int slice_h = block_w*(mb_y+1);
        if (!(s->keyframe || s->avctx->debug&512)){
            slice_starty = FFMAX(0, slice_starty - (block_w >> 1));
            slice_h -= (block_w >> 1);
        }

        {        
        START_TIMER
        for(level=0; level<s->spatial_decomposition_count; level++){
            for(orientation=level ? 1 : 0; orientation<4; orientation++){
                SubBand *b= &p->band[level][orientation];
                int start_y;
                int end_y;
                int our_mb_start = mb_y;
                int our_mb_end = (mb_y + 1);
                start_y = (mb_y ? ((block_w * our_mb_start) >> (s->spatial_decomposition_count - level)) + s->spatial_decomposition_count - level + 2: 0);
                end_y = (((block_w * our_mb_end) >> (s->spatial_decomposition_count - level)) + s->spatial_decomposition_count - level + 2);
                if (!(s->keyframe || s->avctx->debug&512)){
                    start_y = FFMAX(0, start_y - (block_w >> (1+s->spatial_decomposition_count - level)));
                    end_y = FFMAX(0, end_y - (block_w >> (1+s->spatial_decomposition_count - level)));
                }
                start_y = FFMIN(b->height, start_y);
                end_y = FFMIN(b->height, end_y);
                
                if (start_y != end_y){
                    if (orientation == 0){
                        SubBand * correlate_band = &p->band[0][0];
                        int correlate_end_y = FFMIN(b->height, end_y + 1);
                        int correlate_start_y = FFMIN(b->height, (start_y ? start_y + 1 : 0));
                        decode_subband_slice_buffered(s, correlate_band, &s->sb, correlate_start_y, correlate_end_y, decode_state[0][0]);
                        correlate_slice_buffered(s, &s->sb, correlate_band, correlate_band->buf, correlate_band->stride, 1, 0, correlate_start_y, correlate_end_y);
                        dequantize_slice_buffered(s, &s->sb, correlate_band, correlate_band->buf, correlate_band->stride, start_y, end_y);
                    }
                    else
                        decode_subband_slice_buffered(s, b, &s->sb, start_y, end_y, decode_state[level][orientation]);
                }
            }
        }
        STOP_TIMER("decode_subband_slice");
        }
        
{   START_TIMER
        for(; yd<slice_h; yd+=4){
            ff_spatial_idwt_buffered_slice(cs, &s->sb, w, h, 1, s->spatial_decomposition_type, s->spatial_decomposition_count, yd);
        }
    STOP_TIMER("idwt slice");}

        
        if(s->qlog == LOSSLESS_QLOG){
            for(; yq<slice_h && yq<h; yq++){
                DWTELEM * line = slice_buffer_get_line(&s->sb, yq);
                for(x=0; x<w; x++){
                    line[x] <<= FRAC_BITS;
                }
            }
        }

        predict_slice_buffered(s, &s->sb, s->spatial_dwt_buffer, plane_index, 1, mb_y);
        
        y = FFMIN(p->height, slice_starty);
        end_y = FFMIN(p->height, slice_h);
        while(y < end_y)
            slice_buffer_release(&s->sb, y++);
    }
    
    slice_buffer_flush(&s->sb);
    
STOP_TIMER("idwt + predict_slices")}
    }
            
    emms_c();

    if(s->last_picture.data[0])
        avctx->release_buffer(avctx, &s->last_picture);

if(!(s->avctx->debug&2048))        
    *picture= s->current_picture;
else
    *picture= s->mconly_picture;
    
    *data_size = sizeof(AVFrame);
    
    bytes_read= c->bytestream - c->bytestream_start;
    if(bytes_read ==0) av_log(s->avctx, AV_LOG_ERROR, "error at end of frame\n"); //FIXME

    return bytes_read;
}

static int decode_end(AVCodecContext *avctx)
{
    SnowContext *s = avctx->priv_data;

    slice_buffer_destroy(&s->sb);
    
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

#ifdef CONFIG_ENCODERS
AVCodec snow_encoder = {
    "snow",
    CODEC_TYPE_VIDEO,
    CODEC_ID_SNOW,
    sizeof(SnowContext),
    encode_init,
    encode_frame,
    encode_end,
};
#endif


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
    ff_init_range_encoder(&s.c, buffer[0], 256*256);
    ff_init_cabac_states(&s.c, ff_h264_lps_range, ff_h264_mps_state, ff_h264_lps_state, 64);
        
    for(i=-256; i<256; i++){
START_TIMER
        put_symbol(&s.c, s.header_state, i*i*i/3*ABS(i), 1);
STOP_TIMER("put_symbol")
    }
    ff_rac_terminate(&s.c);

    memset(s.header_state, 0, sizeof(s.header_state));
    ff_init_range_decoder(&s.c, buffer[0], 256*256);
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

