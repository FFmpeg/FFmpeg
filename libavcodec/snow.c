/*
 * Copyright (C) 2004 Michael Niedermayer <michaelni@gmx.at>
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

#include "avcodec.h"
#include "dsputil.h"
#include "snow.h"

#include "rangecoder.h"

#include "mpegvideo.h"

#undef NDEBUG
#include <assert.h>

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

#if 0 //64*cubic
static const uint8_t obmc32[1024]={
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  4,  4,  4,  4,  4,  4,  4,  4,  8,  8,  8,  8,  8,  8,  4,  4,  4,  4,  4,  4,  4,  4,  0,  0,  0,  0,  0,
  0,  0,  0,  4,  4,  4,  4,  8,  8, 12, 12, 12, 16, 16, 16, 16, 16, 16, 16, 16, 12, 12, 12,  8,  8,  4,  4,  4,  4,  0,  0,  0,
  0,  0,  4,  4,  8,  8, 12, 16, 16, 20, 24, 24, 28, 28, 32, 32, 32, 32, 28, 28, 24, 24, 20, 16, 16, 12,  8,  8,  4,  4,  0,  0,
  0,  0,  4,  8,  8, 12, 16, 24, 28, 32, 36, 40, 44, 48, 48, 48, 48, 48, 48, 44, 40, 36, 32, 28, 24, 16, 12,  8,  8,  4,  0,  0,
  0,  4,  4,  8, 12, 20, 24, 32, 40, 44, 52, 56, 60, 64, 68, 72, 72, 68, 64, 60, 56, 52, 44, 40, 32, 24, 20, 12,  8,  4,  4,  0,
  0,  4,  4, 12, 16, 24, 32, 40, 52, 60, 68, 76, 80, 88, 88, 92, 92, 88, 88, 80, 76, 68, 60, 52, 40, 32, 24, 16, 12,  4,  4,  0,
  0,  4,  8, 16, 24, 32, 40, 52, 64, 76, 84, 92,100,108,112,116,116,112,108,100, 92, 84, 76, 64, 52, 40, 32, 24, 16,  8,  4,  0,
  0,  4,  8, 16, 28, 40, 52, 64, 76, 88,100,112,124,132,136,140,140,136,132,124,112,100, 88, 76, 64, 52, 40, 28, 16,  8,  4,  0,
  0,  4, 12, 20, 32, 44, 60, 76, 88,104,120,132,144,152,160,164,164,160,152,144,132,120,104, 88, 76, 60, 44, 32, 20, 12,  4,  0,
  0,  4, 12, 24, 36, 48, 68, 84,100,120,136,152,164,176,180,184,184,180,176,164,152,136,120,100, 84, 68, 48, 36, 24, 12,  4,  0,
  0,  4, 12, 24, 40, 56, 76, 92,112,132,152,168,180,192,204,208,208,204,192,180,168,152,132,112, 92, 76, 56, 40, 24, 12,  4,  0,
  0,  4, 16, 28, 44, 60, 80,100,124,144,164,180,196,208,220,224,224,220,208,196,180,164,144,124,100, 80, 60, 44, 28, 16,  4,  0,
  0,  8, 16, 28, 48, 64, 88,108,132,152,176,192,208,224,232,240,240,232,224,208,192,176,152,132,108, 88, 64, 48, 28, 16,  8,  0,
  0,  4, 16, 32, 48, 68, 88,112,136,160,180,204,220,232,244,248,248,244,232,220,204,180,160,136,112, 88, 68, 48, 32, 16,  4,  0,
  1,  8, 16, 32, 48, 72, 92,116,140,164,184,208,224,240,248,255,255,248,240,224,208,184,164,140,116, 92, 72, 48, 32, 16,  8,  1,
  1,  8, 16, 32, 48, 72, 92,116,140,164,184,208,224,240,248,255,255,248,240,224,208,184,164,140,116, 92, 72, 48, 32, 16,  8,  1,
  0,  4, 16, 32, 48, 68, 88,112,136,160,180,204,220,232,244,248,248,244,232,220,204,180,160,136,112, 88, 68, 48, 32, 16,  4,  0,
  0,  8, 16, 28, 48, 64, 88,108,132,152,176,192,208,224,232,240,240,232,224,208,192,176,152,132,108, 88, 64, 48, 28, 16,  8,  0,
  0,  4, 16, 28, 44, 60, 80,100,124,144,164,180,196,208,220,224,224,220,208,196,180,164,144,124,100, 80, 60, 44, 28, 16,  4,  0,
  0,  4, 12, 24, 40, 56, 76, 92,112,132,152,168,180,192,204,208,208,204,192,180,168,152,132,112, 92, 76, 56, 40, 24, 12,  4,  0,
  0,  4, 12, 24, 36, 48, 68, 84,100,120,136,152,164,176,180,184,184,180,176,164,152,136,120,100, 84, 68, 48, 36, 24, 12,  4,  0,
  0,  4, 12, 20, 32, 44, 60, 76, 88,104,120,132,144,152,160,164,164,160,152,144,132,120,104, 88, 76, 60, 44, 32, 20, 12,  4,  0,
  0,  4,  8, 16, 28, 40, 52, 64, 76, 88,100,112,124,132,136,140,140,136,132,124,112,100, 88, 76, 64, 52, 40, 28, 16,  8,  4,  0,
  0,  4,  8, 16, 24, 32, 40, 52, 64, 76, 84, 92,100,108,112,116,116,112,108,100, 92, 84, 76, 64, 52, 40, 32, 24, 16,  8,  4,  0,
  0,  4,  4, 12, 16, 24, 32, 40, 52, 60, 68, 76, 80, 88, 88, 92, 92, 88, 88, 80, 76, 68, 60, 52, 40, 32, 24, 16, 12,  4,  4,  0,
  0,  4,  4,  8, 12, 20, 24, 32, 40, 44, 52, 56, 60, 64, 68, 72, 72, 68, 64, 60, 56, 52, 44, 40, 32, 24, 20, 12,  8,  4,  4,  0,
  0,  0,  4,  8,  8, 12, 16, 24, 28, 32, 36, 40, 44, 48, 48, 48, 48, 48, 48, 44, 40, 36, 32, 28, 24, 16, 12,  8,  8,  4,  0,  0,
  0,  0,  4,  4,  8,  8, 12, 16, 16, 20, 24, 24, 28, 28, 32, 32, 32, 32, 28, 28, 24, 24, 20, 16, 16, 12,  8,  8,  4,  4,  0,  0,
  0,  0,  0,  4,  4,  4,  4,  8,  8, 12, 12, 12, 16, 16, 16, 16, 16, 16, 16, 16, 12, 12, 12,  8,  8,  4,  4,  4,  4,  0,  0,  0,
  0,  0,  0,  0,  0,  4,  4,  4,  4,  4,  4,  4,  4,  8,  8,  8,  8,  8,  8,  4,  4,  4,  4,  4,  4,  4,  4,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
//error:0.000022
};
static const uint8_t obmc16[256]={
  0,  0,  0,  0,  0,  0,  4,  4,  4,  4,  0,  0,  0,  0,  0,  0,
  0,  4,  4,  8, 16, 20, 20, 24, 24, 20, 20, 16,  8,  4,  4,  0,
  0,  4, 16, 24, 36, 44, 52, 60, 60, 52, 44, 36, 24, 16,  4,  0,
  0,  8, 24, 44, 60, 80, 96,104,104, 96, 80, 60, 44, 24,  8,  0,
  0, 16, 36, 60, 92,116,136,152,152,136,116, 92, 60, 36, 16,  0,
  0, 20, 44, 80,116,152,180,196,196,180,152,116, 80, 44, 20,  0,
  4, 20, 52, 96,136,180,212,228,228,212,180,136, 96, 52, 20,  4,
  4, 24, 60,104,152,196,228,248,248,228,196,152,104, 60, 24,  4,
  4, 24, 60,104,152,196,228,248,248,228,196,152,104, 60, 24,  4,
  4, 20, 52, 96,136,180,212,228,228,212,180,136, 96, 52, 20,  4,
  0, 20, 44, 80,116,152,180,196,196,180,152,116, 80, 44, 20,  0,
  0, 16, 36, 60, 92,116,136,152,152,136,116, 92, 60, 36, 16,  0,
  0,  8, 24, 44, 60, 80, 96,104,104, 96, 80, 60, 44, 24,  8,  0,
  0,  4, 16, 24, 36, 44, 52, 60, 60, 52, 44, 36, 24, 16,  4,  0,
  0,  4,  4,  8, 16, 20, 20, 24, 24, 20, 20, 16,  8,  4,  4,  0,
  0,  0,  0,  0,  0,  0,  4,  4,  4,  4,  0,  0,  0,  0,  0,  0,
//error:0.000033
};
#elif 1 // 64*linear
static const uint8_t obmc32[1024]={
  0,  0,  0,  0,  4,  4,  4,  4,  4,  4,  4,  4,  8,  8,  8,  8,  8,  8,  8,  8,  4,  4,  4,  4,  4,  4,  4,  4,  0,  0,  0,  0,
  0,  4,  4,  4,  8,  8,  8, 12, 12, 16, 16, 16, 20, 20, 20, 24, 24, 20, 20, 20, 16, 16, 16, 12, 12,  8,  8,  8,  4,  4,  4,  0,
  0,  4,  8,  8, 12, 12, 16, 20, 20, 24, 28, 28, 32, 32, 36, 40, 40, 36, 32, 32, 28, 28, 24, 20, 20, 16, 12, 12,  8,  8,  4,  0,
  0,  4,  8, 12, 16, 20, 24, 28, 28, 32, 36, 40, 44, 48, 52, 56, 56, 52, 48, 44, 40, 36, 32, 28, 28, 24, 20, 16, 12,  8,  4,  0,
  4,  8, 12, 16, 20, 24, 28, 32, 40, 44, 48, 52, 56, 60, 64, 68, 68, 64, 60, 56, 52, 48, 44, 40, 32, 28, 24, 20, 16, 12,  8,  4,
  4,  8, 12, 20, 24, 32, 36, 40, 48, 52, 56, 64, 68, 76, 80, 84, 84, 80, 76, 68, 64, 56, 52, 48, 40, 36, 32, 24, 20, 12,  8,  4,
  4,  8, 16, 24, 28, 36, 44, 48, 56, 60, 68, 76, 80, 88, 96,100,100, 96, 88, 80, 76, 68, 60, 56, 48, 44, 36, 28, 24, 16,  8,  4,
  4, 12, 20, 28, 32, 40, 48, 56, 64, 72, 80, 88, 92,100,108,116,116,108,100, 92, 88, 80, 72, 64, 56, 48, 40, 32, 28, 20, 12,  4,
  4, 12, 20, 28, 40, 48, 56, 64, 72, 80, 88, 96,108,116,124,132,132,124,116,108, 96, 88, 80, 72, 64, 56, 48, 40, 28, 20, 12,  4,
  4, 16, 24, 32, 44, 52, 60, 72, 80, 92,100,108,120,128,136,148,148,136,128,120,108,100, 92, 80, 72, 60, 52, 44, 32, 24, 16,  4,
  4, 16, 28, 36, 48, 56, 68, 80, 88,100,112,120,132,140,152,164,164,152,140,132,120,112,100, 88, 80, 68, 56, 48, 36, 28, 16,  4,
  4, 16, 28, 40, 52, 64, 76, 88, 96,108,120,132,144,156,168,180,180,168,156,144,132,120,108, 96, 88, 76, 64, 52, 40, 28, 16,  4,
  8, 20, 32, 44, 56, 68, 80, 92,108,120,132,144,156,168,180,192,192,180,168,156,144,132,120,108, 92, 80, 68, 56, 44, 32, 20,  8,
  8, 20, 32, 48, 60, 76, 88,100,116,128,140,156,168,184,196,208,208,196,184,168,156,140,128,116,100, 88, 76, 60, 48, 32, 20,  8,
  8, 20, 36, 52, 64, 80, 96,108,124,136,152,168,180,196,212,224,224,212,196,180,168,152,136,124,108, 96, 80, 64, 52, 36, 20,  8,
  8, 24, 40, 56, 68, 84,100,116,132,148,164,180,192,208,224,240,240,224,208,192,180,164,148,132,116,100, 84, 68, 56, 40, 24,  8,
  8, 24, 40, 56, 68, 84,100,116,132,148,164,180,192,208,224,240,240,224,208,192,180,164,148,132,116,100, 84, 68, 56, 40, 24,  8,
  8, 20, 36, 52, 64, 80, 96,108,124,136,152,168,180,196,212,224,224,212,196,180,168,152,136,124,108, 96, 80, 64, 52, 36, 20,  8,
  8, 20, 32, 48, 60, 76, 88,100,116,128,140,156,168,184,196,208,208,196,184,168,156,140,128,116,100, 88, 76, 60, 48, 32, 20,  8,
  8, 20, 32, 44, 56, 68, 80, 92,108,120,132,144,156,168,180,192,192,180,168,156,144,132,120,108, 92, 80, 68, 56, 44, 32, 20,  8,
  4, 16, 28, 40, 52, 64, 76, 88, 96,108,120,132,144,156,168,180,180,168,156,144,132,120,108, 96, 88, 76, 64, 52, 40, 28, 16,  4,
  4, 16, 28, 36, 48, 56, 68, 80, 88,100,112,120,132,140,152,164,164,152,140,132,120,112,100, 88, 80, 68, 56, 48, 36, 28, 16,  4,
  4, 16, 24, 32, 44, 52, 60, 72, 80, 92,100,108,120,128,136,148,148,136,128,120,108,100, 92, 80, 72, 60, 52, 44, 32, 24, 16,  4,
  4, 12, 20, 28, 40, 48, 56, 64, 72, 80, 88, 96,108,116,124,132,132,124,116,108, 96, 88, 80, 72, 64, 56, 48, 40, 28, 20, 12,  4,
  4, 12, 20, 28, 32, 40, 48, 56, 64, 72, 80, 88, 92,100,108,116,116,108,100, 92, 88, 80, 72, 64, 56, 48, 40, 32, 28, 20, 12,  4,
  4,  8, 16, 24, 28, 36, 44, 48, 56, 60, 68, 76, 80, 88, 96,100,100, 96, 88, 80, 76, 68, 60, 56, 48, 44, 36, 28, 24, 16,  8,  4,
  4,  8, 12, 20, 24, 32, 36, 40, 48, 52, 56, 64, 68, 76, 80, 84, 84, 80, 76, 68, 64, 56, 52, 48, 40, 36, 32, 24, 20, 12,  8,  4,
  4,  8, 12, 16, 20, 24, 28, 32, 40, 44, 48, 52, 56, 60, 64, 68, 68, 64, 60, 56, 52, 48, 44, 40, 32, 28, 24, 20, 16, 12,  8,  4,
  0,  4,  8, 12, 16, 20, 24, 28, 28, 32, 36, 40, 44, 48, 52, 56, 56, 52, 48, 44, 40, 36, 32, 28, 28, 24, 20, 16, 12,  8,  4,  0,
  0,  4,  8,  8, 12, 12, 16, 20, 20, 24, 28, 28, 32, 32, 36, 40, 40, 36, 32, 32, 28, 28, 24, 20, 20, 16, 12, 12,  8,  8,  4,  0,
  0,  4,  4,  4,  8,  8,  8, 12, 12, 16, 16, 16, 20, 20, 20, 24, 24, 20, 20, 20, 16, 16, 16, 12, 12,  8,  8,  8,  4,  4,  4,  0,
  0,  0,  0,  0,  4,  4,  4,  4,  4,  4,  4,  4,  8,  8,  8,  8,  8,  8,  8,  8,  4,  4,  4,  4,  4,  4,  4,  4,  0,  0,  0,  0,
 //error:0.000020
};
static const uint8_t obmc16[256]={
  0,  4,  4,  8,  8, 12, 12, 16, 16, 12, 12,  8,  8,  4,  4,  0,
  4,  8, 16, 20, 28, 32, 40, 44, 44, 40, 32, 28, 20, 16,  8,  4,
  4, 16, 24, 36, 44, 56, 64, 76, 76, 64, 56, 44, 36, 24, 16,  4,
  8, 20, 36, 48, 64, 76, 92,104,104, 92, 76, 64, 48, 36, 20,  8,
  8, 28, 44, 64, 80,100,116,136,136,116,100, 80, 64, 44, 28,  8,
 12, 32, 56, 76,100,120,144,164,164,144,120,100, 76, 56, 32, 12,
 12, 40, 64, 92,116,144,168,196,196,168,144,116, 92, 64, 40, 12,
 16, 44, 76,104,136,164,196,224,224,196,164,136,104, 76, 44, 16,
 16, 44, 76,104,136,164,196,224,224,196,164,136,104, 76, 44, 16,
 12, 40, 64, 92,116,144,168,196,196,168,144,116, 92, 64, 40, 12,
 12, 32, 56, 76,100,120,144,164,164,144,120,100, 76, 56, 32, 12,
  8, 28, 44, 64, 80,100,116,136,136,116,100, 80, 64, 44, 28,  8,
  8, 20, 36, 48, 64, 76, 92,104,104, 92, 76, 64, 48, 36, 20,  8,
  4, 16, 24, 36, 44, 56, 64, 76, 76, 64, 56, 44, 36, 24, 16,  4,
  4,  8, 16, 20, 28, 32, 40, 44, 44, 40, 32, 28, 20, 16,  8,  4,
  0,  4,  4,  8,  8, 12, 12, 16, 16, 12, 12,  8,  8,  4,  4,  0,
//error:0.000015
};
#else //64*cos
static const uint8_t obmc32[1024]={
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  4,  4,  4,  4,  4,  4,  4,  4,  8,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  4,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  4,  4,  4,  4,  8,  8, 12, 12, 12, 12, 16, 16, 16, 16, 16, 16, 12, 12, 12, 12,  8,  8,  4,  4,  4,  4,  0,  0,  0,
  0,  0,  4,  4,  4,  8,  8, 12, 16, 20, 20, 24, 28, 28, 28, 28, 28, 28, 28, 28, 24, 20, 20, 16, 12,  8,  8,  4,  4,  4,  0,  0,
  0,  0,  4,  4,  8, 12, 16, 20, 24, 28, 36, 40, 44, 44, 48, 48, 48, 48, 44, 44, 40, 36, 28, 24, 20, 16, 12,  8,  4,  4,  0,  0,
  0,  0,  4,  8, 12, 20, 24, 32, 36, 44, 48, 56, 60, 64, 68, 68, 68, 68, 64, 60, 56, 48, 44, 36, 32, 24, 20, 12,  8,  4,  0,  0,
  0,  4,  4,  8, 16, 24, 32, 40, 48, 60, 68, 76, 80, 84, 88, 92, 92, 88, 84, 80, 76, 68, 60, 48, 40, 32, 24, 16,  8,  4,  4,  0,
  0,  4,  8, 12, 20, 32, 40, 52, 64, 76, 84, 96,104,108,112,116,116,112,108,104, 96, 84, 76, 64, 52, 40, 32, 20, 12,  8,  4,  0,
  0,  4,  8, 16, 24, 36, 48, 64, 76, 92,104,116,124,132,136,140,140,136,132,124,116,104, 92, 76, 64, 48, 36, 24, 16,  8,  4,  0,
  0,  4, 12, 20, 28, 44, 60, 76, 92,104,120,136,148,156,160,164,164,160,156,148,136,120,104, 92, 76, 60, 44, 28, 20, 12,  4,  0,
  0,  4, 12, 20, 36, 48, 68, 84,104,120,140,152,168,176,184,188,188,184,176,168,152,140,120,104, 84, 68, 48, 36, 20, 12,  4,  0,
  0,  4, 12, 24, 36, 56, 76, 96,116,136,152,172,184,196,204,208,208,204,196,184,172,152,136,116, 96, 76, 56, 36, 24, 12,  4,  0,
  0,  4, 12, 24, 44, 60, 80,104,124,148,168,184,200,212,224,228,228,224,212,200,184,168,148,124,104, 80, 60, 44, 24, 12,  4,  0,
  0,  4, 12, 28, 44, 64, 84,108,132,156,176,196,212,228,236,240,240,236,228,212,196,176,156,132,108, 84, 64, 44, 28, 12,  4,  0,
  0,  4, 16, 28, 48, 68, 88,112,136,160,184,204,224,236,244,252,252,244,236,224,204,184,160,136,112, 88, 68, 48, 28, 16,  4,  0,
  1,  4, 16, 28, 48, 68, 92,116,140,164,188,208,228,240,252,255,255,252,240,228,208,188,164,140,116, 92, 68, 48, 28, 16,  4,  1,
  1,  4, 16, 28, 48, 68, 92,116,140,164,188,208,228,240,252,255,255,252,240,228,208,188,164,140,116, 92, 68, 48, 28, 16,  4,  1,
  0,  4, 16, 28, 48, 68, 88,112,136,160,184,204,224,236,244,252,252,244,236,224,204,184,160,136,112, 88, 68, 48, 28, 16,  4,  0,
  0,  4, 12, 28, 44, 64, 84,108,132,156,176,196,212,228,236,240,240,236,228,212,196,176,156,132,108, 84, 64, 44, 28, 12,  4,  0,
  0,  4, 12, 24, 44, 60, 80,104,124,148,168,184,200,212,224,228,228,224,212,200,184,168,148,124,104, 80, 60, 44, 24, 12,  4,  0,
  0,  4, 12, 24, 36, 56, 76, 96,116,136,152,172,184,196,204,208,208,204,196,184,172,152,136,116, 96, 76, 56, 36, 24, 12,  4,  0,
  0,  4, 12, 20, 36, 48, 68, 84,104,120,140,152,168,176,184,188,188,184,176,168,152,140,120,104, 84, 68, 48, 36, 20, 12,  4,  0,
  0,  4, 12, 20, 28, 44, 60, 76, 92,104,120,136,148,156,160,164,164,160,156,148,136,120,104, 92, 76, 60, 44, 28, 20, 12,  4,  0,
  0,  4,  8, 16, 24, 36, 48, 64, 76, 92,104,116,124,132,136,140,140,136,132,124,116,104, 92, 76, 64, 48, 36, 24, 16,  8,  4,  0,
  0,  4,  8, 12, 20, 32, 40, 52, 64, 76, 84, 96,104,108,112,116,116,112,108,104, 96, 84, 76, 64, 52, 40, 32, 20, 12,  8,  4,  0,
  0,  4,  4,  8, 16, 24, 32, 40, 48, 60, 68, 76, 80, 84, 88, 92, 92, 88, 84, 80, 76, 68, 60, 48, 40, 32, 24, 16,  8,  4,  4,  0,
  0,  0,  4,  8, 12, 20, 24, 32, 36, 44, 48, 56, 60, 64, 68, 68, 68, 68, 64, 60, 56, 48, 44, 36, 32, 24, 20, 12,  8,  4,  0,  0,
  0,  0,  4,  4,  8, 12, 16, 20, 24, 28, 36, 40, 44, 44, 48, 48, 48, 48, 44, 44, 40, 36, 28, 24, 20, 16, 12,  8,  4,  4,  0,  0,
  0,  0,  4,  4,  4,  8,  8, 12, 16, 20, 20, 24, 28, 28, 28, 28, 28, 28, 28, 28, 24, 20, 20, 16, 12,  8,  8,  4,  4,  4,  0,  0,
  0,  0,  0,  4,  4,  4,  4,  8,  8, 12, 12, 12, 12, 16, 16, 16, 16, 16, 16, 12, 12, 12, 12,  8,  8,  4,  4,  4,  4,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  4,  4,  4,  4,  4,  4,  4,  4,  8,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  4,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
//error:0.000022
};
static const uint8_t obmc16[256]={
  0,  0,  0,  0,  0,  4,  4,  4,  4,  4,  4,  0,  0,  0,  0,  0,
  0,  0,  4,  8, 12, 16, 20, 20, 20, 20, 16, 12,  8,  4,  0,  0,
  0,  4, 12, 24, 32, 44, 52, 56, 56, 52, 44, 32, 24, 12,  4,  0,
  0,  8, 24, 40, 60, 80, 96,104,104, 96, 80, 60, 40, 24,  8,  0,
  0, 12, 32, 64, 92,120,140,152,152,140,120, 92, 64, 32, 12,  0,
  4, 16, 44, 80,120,156,184,196,196,184,156,120, 80, 44, 16,  4,
  4, 20, 52, 96,140,184,216,232,232,216,184,140, 96, 52, 20,  4,
  0, 20, 56,104,152,196,232,252,252,232,196,152,104, 56, 20,  0,
  0, 20, 56,104,152,196,232,252,252,232,196,152,104, 56, 20,  0,
  4, 20, 52, 96,140,184,216,232,232,216,184,140, 96, 52, 20,  4,
  4, 16, 44, 80,120,156,184,196,196,184,156,120, 80, 44, 16,  4,
  0, 12, 32, 64, 92,120,140,152,152,140,120, 92, 64, 32, 12,  0,
  0,  8, 24, 40, 60, 80, 96,104,104, 96, 80, 60, 40, 24,  8,  0,
  0,  4, 12, 24, 32, 44, 52, 56, 56, 52, 44, 32, 24, 12,  4,  0,
  0,  0,  4,  8, 12, 16, 20, 20, 20, 20, 16, 12,  8,  4,  0,  0,
  0,  0,  0,  0,  0,  4,  4,  4,  4,  4,  4,  0,  0,  0,  0,  0,
//error:0.000022
};
#endif /* 0 */

//linear *64
static const uint8_t obmc8[64]={
  4, 12, 20, 28, 28, 20, 12,  4,
 12, 36, 60, 84, 84, 60, 36, 12,
 20, 60,100,140,140,100, 60, 20,
 28, 84,140,196,196,140, 84, 28,
 28, 84,140,196,196,140, 84, 28,
 20, 60,100,140,140,100, 60, 20,
 12, 36, 60, 84, 84, 60, 36, 12,
  4, 12, 20, 28, 28, 20, 12,  4,
//error:0.000000
};

//linear *64
static const uint8_t obmc4[16]={
 16, 48, 48, 16,
 48,144,144, 48,
 48,144,144, 48,
 16, 48, 48, 16,
//error:0.000000
};

static const uint8_t *obmc_tab[4]={
    obmc32, obmc16, obmc8, obmc4
};

static int scale_mv_ref[MAX_REF_FRAMES][MAX_REF_FRAMES];

typedef struct BlockNode{
    int16_t mx;
    int16_t my;
    uint8_t ref;
    uint8_t color[3];
    uint8_t type;
//#define TYPE_SPLIT    1
#define BLOCK_INTRA   1
#define BLOCK_OPT     2
//#define TYPE_NOCOLOR  4
    uint8_t level; //FIXME merge into type?
}BlockNode;

static const BlockNode null_block= { //FIXME add border maybe
    .color= {128,128,128},
    .mx= 0,
    .my= 0,
    .ref= 0,
    .type= 0,
    .level= 0,
};

#define LOG2_MB_SIZE 4
#define MB_SIZE (1<<LOG2_MB_SIZE)
#define ENCODER_EXTRA_BITS 4
#define HTAPS_MAX 8

typedef struct x_and_coeff{
    int16_t x;
    uint16_t coeff;
} x_and_coeff;

typedef struct SubBand{
    int level;
    int stride;
    int width;
    int height;
    int qlog;        ///< log(qscale)/log[2^(1/6)]
    DWTELEM *buf;
    IDWTELEM *ibuf;
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

    int htaps;
    int8_t hcoeff[HTAPS_MAX/2];
    int diag_mc;
    int fast_mc;

    int last_htaps;
    int8_t last_hcoeff[HTAPS_MAX/2];
    int last_diag_mc;
}Plane;

typedef struct SnowContext{
//    MpegEncContext m; // needed for motion estimation, should not be used for anything else, the idea is to eventually make the motion estimation independent of MpegEncContext, so this will be removed then (FIXME/XXX)

    AVCodecContext *avctx;
    RangeCoder c;
    DSPContext dsp;
    AVFrame new_picture;
    AVFrame input_picture;              ///< new_picture with the internal linesizes
    AVFrame current_picture;
    AVFrame last_picture[MAX_REF_FRAMES];
    uint8_t *halfpel_plane[MAX_REF_FRAMES][4][4];
    AVFrame mconly_picture;
//     uint8_t q_context[16];
    uint8_t header_state[32];
    uint8_t block_state[128 + 32*128];
    int keyframe;
    int always_reset;
    int version;
    int spatial_decomposition_type;
    int last_spatial_decomposition_type;
    int temporal_decomposition_type;
    int spatial_decomposition_count;
    int last_spatial_decomposition_count;
    int temporal_decomposition_count;
    int max_ref_frames;
    int ref_frames;
    int16_t (*ref_mvs[MAX_REF_FRAMES])[2];
    uint32_t *ref_scores[MAX_REF_FRAMES];
    DWTELEM *spatial_dwt_buffer;
    IDWTELEM *spatial_idwt_buffer;
    int colorspace_type;
    int chroma_h_shift;
    int chroma_v_shift;
    int spatial_scalability;
    int qlog;
    int last_qlog;
    int lambda;
    int lambda2;
    int pass1_rc;
    int mv_scale;
    int last_mv_scale;
    int qbias;
    int last_qbias;
#define QBIAS_SHIFT 3
    int b_width;
    int b_height;
    int block_max_depth;
    int last_block_max_depth;
    Plane plane[MAX_PLANES];
    BlockNode *block;
#define ME_CACHE_SIZE 1024
    int me_cache[ME_CACHE_SIZE];
    int me_cache_generation;
    slice_buffer sb;

    MpegEncContext m; // needed for motion estimation, should not be used for anything else, the idea is to eventually make the motion estimation independent of MpegEncContext, so this will be removed then (FIXME/XXX)
}SnowContext;

typedef struct {
    IDWTELEM *b0;
    IDWTELEM *b1;
    IDWTELEM *b2;
    IDWTELEM *b3;
    int y;
} dwt_compose_t;

#define slice_buffer_get_line(slice_buf, line_num) ((slice_buf)->line[line_num] ? (slice_buf)->line[line_num] : slice_buffer_load_line((slice_buf), (line_num)))
//#define slice_buffer_get_line(slice_buf, line_num) (slice_buffer_load_line((slice_buf), (line_num)))

static void iterative_me(SnowContext *s);

static void slice_buffer_init(slice_buffer * buf, int line_count, int max_allocated_lines, int line_width, IDWTELEM * base_buffer)
{
    int i;

    buf->base_buffer = base_buffer;
    buf->line_count = line_count;
    buf->line_width = line_width;
    buf->data_count = max_allocated_lines;
    buf->line = av_mallocz (sizeof(IDWTELEM *) * line_count);
    buf->data_stack = av_malloc (sizeof(IDWTELEM *) * max_allocated_lines);

    for(i = 0; i < max_allocated_lines; i++){
        buf->data_stack[i] = av_malloc (sizeof(IDWTELEM) * line_width);
    }

    buf->data_stack_top = max_allocated_lines - 1;
}

static IDWTELEM * slice_buffer_load_line(slice_buffer * buf, int line)
{
    int offset;
    IDWTELEM * buffer;

    assert(buf->data_stack_top >= 0);
//  assert(!buf->line[line]);
    if (buf->line[line])
        return buf->line[line];

    offset = buf->line_width * line;
    buffer = buf->data_stack[buf->data_stack_top];
    buf->data_stack_top--;
    buf->line[line] = buffer;

    return buffer;
}

static void slice_buffer_release(slice_buffer * buf, int line)
{
    int offset;
    IDWTELEM * buffer;

    assert(line >= 0 && line < buf->line_count);
    assert(buf->line[line]);

    offset = buf->line_width * line;
    buffer = buf->line[line];
    buf->data_stack_top++;
    buf->data_stack[buf->data_stack_top] = buffer;
    buf->line[line] = NULL;
}

static void slice_buffer_flush(slice_buffer * buf)
{
    int i;
    for(i = 0; i < buf->line_count; i++){
        if (buf->line[i])
            slice_buffer_release(buf, i);
    }
}

static void slice_buffer_destroy(slice_buffer * buf)
{
    int i;
    slice_buffer_flush(buf);

    for(i = buf->data_count - 1; i >= 0; i--){
        av_freep(&buf->data_stack[i]);
    }
    av_freep(&buf->data_stack);
    av_freep(&buf->line);
}

#ifdef __sgi
// Avoid a name clash on SGI IRIX
#undef qexp
#endif
#define QEXPSHIFT (7-FRAC_BITS+8) //FIXME try to change this to 0
static uint8_t qexp[QROOT];

static inline int mirror(int v, int m){
    while((unsigned)v > (unsigned)m){
        v=-v;
        if(v<0) v+= 2*m;
    }
    return v;
}

static inline void put_symbol(RangeCoder *c, uint8_t *state, int v, int is_signed){
    int i;

    if(v){
        const int a= FFABS(v);
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
#endif /* 1 */
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

static av_always_inline void
lift(DWTELEM *dst, DWTELEM *src, DWTELEM *ref,
     int dst_step, int src_step, int ref_step,
     int width, int mul, int add, int shift,
     int highpass, int inverse){
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
        dst[i*dst_step] =
            LIFT(src[i*src_step],
                 ((mul*(ref[i*ref_step] + ref[(i+1)*ref_step])+add)>>shift),
                 inverse);
    }

    if(mirror_right){
        dst[w*dst_step] =
            LIFT(src[w*src_step],
                 ((mul*2*ref[w*ref_step]+add)>>shift),
                 inverse);
    }
}

static av_always_inline void
inv_lift(IDWTELEM *dst, IDWTELEM *src, IDWTELEM *ref,
         int dst_step, int src_step, int ref_step,
         int width, int mul, int add, int shift,
         int highpass, int inverse){
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
        dst[i*dst_step] =
            LIFT(src[i*src_step],
                 ((mul*(ref[i*ref_step] + ref[(i+1)*ref_step])+add)>>shift),
                 inverse);
    }

    if(mirror_right){
        dst[w*dst_step] =
            LIFT(src[w*src_step],
                 ((mul*2*ref[w*ref_step]+add)>>shift),
                 inverse);
    }
}

#ifndef liftS
static av_always_inline void
liftS(DWTELEM *dst, DWTELEM *src, DWTELEM *ref,
      int dst_step, int src_step, int ref_step,
      int width, int mul, int add, int shift,
      int highpass, int inverse){
    const int mirror_left= !highpass;
    const int mirror_right= (width&1) ^ highpass;
    const int w= (width>>1) - 1 + (highpass & width);
    int i;

    assert(shift == 4);
#define LIFTS(src, ref, inv) \
        ((inv) ? \
            (src) + (((ref) + 4*(src))>>shift): \
            -((-16*(src) + (ref) + add/4 + 1 + (5<<25))/(5*4) - (1<<23)))
    if(mirror_left){
        dst[0] = LIFTS(src[0], mul*2*ref[0]+add, inverse);
        dst += dst_step;
        src += src_step;
    }

    for(i=0; i<w; i++){
        dst[i*dst_step] =
            LIFTS(src[i*src_step],
                  mul*(ref[i*ref_step] + ref[(i+1)*ref_step])+add,
                  inverse);
    }

    if(mirror_right){
        dst[w*dst_step] =
            LIFTS(src[w*src_step], mul*2*ref[w*ref_step]+add, inverse);
    }
}
static av_always_inline void
inv_liftS(IDWTELEM *dst, IDWTELEM *src, IDWTELEM *ref,
          int dst_step, int src_step, int ref_step,
          int width, int mul, int add, int shift,
          int highpass, int inverse){
    const int mirror_left= !highpass;
    const int mirror_right= (width&1) ^ highpass;
    const int w= (width>>1) - 1 + (highpass & width);
    int i;

    assert(shift == 4);
#define LIFTS(src, ref, inv) \
    ((inv) ? \
        (src) + (((ref) + 4*(src))>>shift): \
        -((-16*(src) + (ref) + add/4 + 1 + (5<<25))/(5*4) - (1<<23)))
    if(mirror_left){
        dst[0] = LIFTS(src[0], mul*2*ref[0]+add, inverse);
        dst += dst_step;
        src += src_step;
    }

    for(i=0; i<w; i++){
        dst[i*dst_step] =
            LIFTS(src[i*src_step],
                  mul*(ref[i*ref_step] + ref[(i+1)*ref_step])+add,
                  inverse);
    }

    if(mirror_right){
        dst[w*dst_step] =
            LIFTS(src[w*src_step], mul*2*ref[w*ref_step]+add, inverse);
    }
}
#endif /* ! liftS */

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
#endif /* 0 */
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

        if(y+1<(unsigned)height) horizontal_decompose53i(b2, width);
        if(y+2<(unsigned)height) horizontal_decompose53i(b3, width);

        if(y+1<(unsigned)height) vertical_decompose53iH0(b1, b2, b3, width);
        if(y+0<(unsigned)height) vertical_decompose53iL0(b0, b1, b2, width);

        b0=b2;
        b1=b3;
    }
}

static void horizontal_decompose97i(DWTELEM *b, int width){
    DWTELEM temp[width];
    const int w2= (width+1)>>1;

    lift (temp+w2, b    +1, b      , 1, 2, 2, width,  W_AM, W_AO, W_AS, 1, 1);
    liftS(temp   , b      , temp+w2, 1, 2, 1, width,  W_BM, W_BO, W_BS, 0, 0);
    lift (b   +w2, temp+w2, temp   , 1, 1, 1, width,  W_CM, W_CO, W_CS, 1, 0);
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
        b1[i] += (W_CM*(b0[i] + b2[i])+W_CO)>>W_CS;
    }
}

static void vertical_decompose97iL0(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, int width){
    int i;

    for(i=0; i<width; i++){
#ifdef liftS
        b1[i] -= (W_BM*(b0[i] + b2[i])+W_BO)>>W_BS;
#else
        b1[i] = (16*4*b1[i] - 4*(b0[i] + b2[i]) + W_BO*5 + (5<<27)) / (5*16) - (1<<23);
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

        if(y+3<(unsigned)height) horizontal_decompose97i(b4, width);
        if(y+4<(unsigned)height) horizontal_decompose97i(b5, width);

        if(y+3<(unsigned)height) vertical_decompose97iH0(b3, b4, b5, width);
        if(y+2<(unsigned)height) vertical_decompose97iL0(b2, b3, b4, width);
        if(y+1<(unsigned)height) vertical_decompose97iH1(b1, b2, b3, width);
        if(y+0<(unsigned)height) vertical_decompose97iL1(b0, b1, b2, width);

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
        case DWT_97: spatial_decompose97i(buffer, width>>level, height>>level, stride<<level); break;
        case DWT_53: spatial_decompose53i(buffer, width>>level, height>>level, stride<<level); break;
        }
    }
}

static void horizontal_compose53i(IDWTELEM *b, int width){
    IDWTELEM temp[width];
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
    inv_lift(temp   , b   , b+w2, 1, 1, 1, width,  1, 2, 2, 0, 1);
    inv_lift(temp+w2, b+w2, temp, 1, 1, 1, width, -1, 0, 1, 1, 1);
#endif /* 0 */
    for(x=0; x<width2; x++){
        b[2*x    ]= temp[x   ];
        b[2*x + 1]= temp[x+w2];
    }
    if(width&1)
        b[2*x    ]= temp[x   ];
}

static void vertical_compose53iH0(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, int width){
    int i;

    for(i=0; i<width; i++){
        b1[i] += (b0[i] + b2[i])>>1;
    }
}

static void vertical_compose53iL0(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, int width){
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

static void spatial_compose53i_init(dwt_compose_t *cs, IDWTELEM *buffer, int height, int stride){
    cs->b0 = buffer + mirror(-1-1, height-1)*stride;
    cs->b1 = buffer + mirror(-1  , height-1)*stride;
    cs->y = -1;
}

static void spatial_compose53i_dy_buffered(dwt_compose_t *cs, slice_buffer * sb, int width, int height, int stride_line){
    int y= cs->y;

    IDWTELEM *b0= cs->b0;
    IDWTELEM *b1= cs->b1;
    IDWTELEM *b2= slice_buffer_get_line(sb, mirror(y+1, height-1) * stride_line);
    IDWTELEM *b3= slice_buffer_get_line(sb, mirror(y+2, height-1) * stride_line);

        if(y+1<(unsigned)height) vertical_compose53iL0(b1, b2, b3, width);
        if(y+0<(unsigned)height) vertical_compose53iH0(b0, b1, b2, width);

        if(y-1<(unsigned)height) horizontal_compose53i(b0, width);
        if(y+0<(unsigned)height) horizontal_compose53i(b1, width);

    cs->b0 = b2;
    cs->b1 = b3;
    cs->y += 2;
}

static void spatial_compose53i_dy(dwt_compose_t *cs, IDWTELEM *buffer, int width, int height, int stride){
    int y= cs->y;
    IDWTELEM *b0= cs->b0;
    IDWTELEM *b1= cs->b1;
    IDWTELEM *b2= buffer + mirror(y+1, height-1)*stride;
    IDWTELEM *b3= buffer + mirror(y+2, height-1)*stride;

        if(y+1<(unsigned)height) vertical_compose53iL0(b1, b2, b3, width);
        if(y+0<(unsigned)height) vertical_compose53iH0(b0, b1, b2, width);

        if(y-1<(unsigned)height) horizontal_compose53i(b0, width);
        if(y+0<(unsigned)height) horizontal_compose53i(b1, width);

    cs->b0 = b2;
    cs->b1 = b3;
    cs->y += 2;
}

static void av_unused spatial_compose53i(IDWTELEM *buffer, int width, int height, int stride){
    dwt_compose_t cs;
    spatial_compose53i_init(&cs, buffer, height, stride);
    while(cs.y <= height)
        spatial_compose53i_dy(&cs, buffer, width, height, stride);
}


void ff_snow_horizontal_compose97i(IDWTELEM *b, int width){
    IDWTELEM temp[width];
    const int w2= (width+1)>>1;

    inv_lift (temp   , b      , b   +w2, 1, 1, 1, width,  W_DM, W_DO, W_DS, 0, 1);
    inv_lift (temp+w2, b   +w2, temp   , 1, 1, 1, width,  W_CM, W_CO, W_CS, 1, 1);
    inv_liftS(b      , temp   , temp+w2, 2, 1, 1, width,  W_BM, W_BO, W_BS, 0, 1);
    inv_lift (b+1    , temp+w2, b      , 2, 1, 2, width,  W_AM, W_AO, W_AS, 1, 0);
}

static void vertical_compose97iH0(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, int width){
    int i;

    for(i=0; i<width; i++){
        b1[i] += (W_AM*(b0[i] + b2[i])+W_AO)>>W_AS;
    }
}

static void vertical_compose97iH1(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, int width){
    int i;

    for(i=0; i<width; i++){
        b1[i] -= (W_CM*(b0[i] + b2[i])+W_CO)>>W_CS;
    }
}

static void vertical_compose97iL0(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, int width){
    int i;

    for(i=0; i<width; i++){
#ifdef liftS
        b1[i] += (W_BM*(b0[i] + b2[i])+W_BO)>>W_BS;
#else
        b1[i] += (W_BM*(b0[i] + b2[i])+4*b1[i]+W_BO)>>W_BS;
#endif
    }
}

static void vertical_compose97iL1(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, int width){
    int i;

    for(i=0; i<width; i++){
        b1[i] -= (W_DM*(b0[i] + b2[i])+W_DO)>>W_DS;
    }
}

void ff_snow_vertical_compose97i(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, IDWTELEM *b3, IDWTELEM *b4, IDWTELEM *b5, int width){
    int i;

    for(i=0; i<width; i++){
        b4[i] -= (W_DM*(b3[i] + b5[i])+W_DO)>>W_DS;
        b3[i] -= (W_CM*(b2[i] + b4[i])+W_CO)>>W_CS;
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

static void spatial_compose97i_init(dwt_compose_t *cs, IDWTELEM *buffer, int height, int stride){
    cs->b0 = buffer + mirror(-3-1, height-1)*stride;
    cs->b1 = buffer + mirror(-3  , height-1)*stride;
    cs->b2 = buffer + mirror(-3+1, height-1)*stride;
    cs->b3 = buffer + mirror(-3+2, height-1)*stride;
    cs->y = -3;
}

static void spatial_compose97i_dy_buffered(DSPContext *dsp, dwt_compose_t *cs, slice_buffer * sb, int width, int height, int stride_line){
    int y = cs->y;

    IDWTELEM *b0= cs->b0;
    IDWTELEM *b1= cs->b1;
    IDWTELEM *b2= cs->b2;
    IDWTELEM *b3= cs->b3;
    IDWTELEM *b4= slice_buffer_get_line(sb, mirror(y + 3, height - 1) * stride_line);
    IDWTELEM *b5= slice_buffer_get_line(sb, mirror(y + 4, height - 1) * stride_line);

    if(y>0 && y+4<height){
        dsp->vertical_compose97i(b0, b1, b2, b3, b4, b5, width);
    }else{
        if(y+3<(unsigned)height) vertical_compose97iL1(b3, b4, b5, width);
        if(y+2<(unsigned)height) vertical_compose97iH1(b2, b3, b4, width);
        if(y+1<(unsigned)height) vertical_compose97iL0(b1, b2, b3, width);
        if(y+0<(unsigned)height) vertical_compose97iH0(b0, b1, b2, width);
    }

        if(y-1<(unsigned)height) dsp->horizontal_compose97i(b0, width);
        if(y+0<(unsigned)height) dsp->horizontal_compose97i(b1, width);

    cs->b0=b2;
    cs->b1=b3;
    cs->b2=b4;
    cs->b3=b5;
    cs->y += 2;
}

static void spatial_compose97i_dy(dwt_compose_t *cs, IDWTELEM *buffer, int width, int height, int stride){
    int y = cs->y;
    IDWTELEM *b0= cs->b0;
    IDWTELEM *b1= cs->b1;
    IDWTELEM *b2= cs->b2;
    IDWTELEM *b3= cs->b3;
    IDWTELEM *b4= buffer + mirror(y+3, height-1)*stride;
    IDWTELEM *b5= buffer + mirror(y+4, height-1)*stride;

        if(y+3<(unsigned)height) vertical_compose97iL1(b3, b4, b5, width);
        if(y+2<(unsigned)height) vertical_compose97iH1(b2, b3, b4, width);
        if(y+1<(unsigned)height) vertical_compose97iL0(b1, b2, b3, width);
        if(y+0<(unsigned)height) vertical_compose97iH0(b0, b1, b2, width);

        if(y-1<(unsigned)height) ff_snow_horizontal_compose97i(b0, width);
        if(y+0<(unsigned)height) ff_snow_horizontal_compose97i(b1, width);

    cs->b0=b2;
    cs->b1=b3;
    cs->b2=b4;
    cs->b3=b5;
    cs->y += 2;
}

static void av_unused spatial_compose97i(IDWTELEM *buffer, int width, int height, int stride){
    dwt_compose_t cs;
    spatial_compose97i_init(&cs, buffer, height, stride);
    while(cs.y <= height)
        spatial_compose97i_dy(&cs, buffer, width, height, stride);
}

static void ff_spatial_idwt_buffered_init(dwt_compose_t *cs, slice_buffer * sb, int width, int height, int stride_line, int type, int decomposition_count){
    int level;
    for(level=decomposition_count-1; level>=0; level--){
        switch(type){
        case DWT_97: spatial_compose97i_buffered_init(cs+level, sb, height>>level, stride_line<<level); break;
        case DWT_53: spatial_compose53i_buffered_init(cs+level, sb, height>>level, stride_line<<level); break;
        }
    }
}

static void ff_spatial_idwt_init(dwt_compose_t *cs, IDWTELEM *buffer, int width, int height, int stride, int type, int decomposition_count){
    int level;
    for(level=decomposition_count-1; level>=0; level--){
        switch(type){
        case DWT_97: spatial_compose97i_init(cs+level, buffer, height>>level, stride<<level); break;
        case DWT_53: spatial_compose53i_init(cs+level, buffer, height>>level, stride<<level); break;
        }
    }
}

static void ff_spatial_idwt_slice(dwt_compose_t *cs, IDWTELEM *buffer, int width, int height, int stride, int type, int decomposition_count, int y){
    const int support = type==1 ? 3 : 5;
    int level;
    if(type==2) return;

    for(level=decomposition_count-1; level>=0; level--){
        while(cs[level].y <= FFMIN((y>>level)+support, height>>level)){
            switch(type){
            case DWT_97: spatial_compose97i_dy(cs+level, buffer, width>>level, height>>level, stride<<level);
                break;
            case DWT_53: spatial_compose53i_dy(cs+level, buffer, width>>level, height>>level, stride<<level);
                break;
            }
        }
    }
}

static void ff_spatial_idwt_buffered_slice(DSPContext *dsp, dwt_compose_t *cs, slice_buffer * slice_buf, int width, int height, int stride_line, int type, int decomposition_count, int y){
    const int support = type==1 ? 3 : 5;
    int level;
    if(type==2) return;

    for(level=decomposition_count-1; level>=0; level--){
        while(cs[level].y <= FFMIN((y>>level)+support, height>>level)){
            switch(type){
            case DWT_97: spatial_compose97i_dy_buffered(dsp, cs+level, slice_buf, width>>level, height>>level, stride_line<<level);
                break;
            case DWT_53: spatial_compose53i_dy_buffered(cs+level, slice_buf, width>>level, height>>level, stride_line<<level);
                break;
            }
        }
    }
}

static void ff_spatial_idwt(IDWTELEM *buffer, int width, int height, int stride, int type, int decomposition_count){
        dwt_compose_t cs[MAX_DECOMPOSITIONS];
        int y;
        ff_spatial_idwt_init(cs, buffer, width, height, stride, type, decomposition_count);
        for(y=0; y<height; y+=4)
            ff_spatial_idwt_slice(cs, buffer, width, height, stride, type, decomposition_count, y);
}

static int encode_subband_c0run(SnowContext *s, SubBand *b, IDWTELEM *src, IDWTELEM *parent, int stride, int orientation){
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
                    int context= av_log2(/*FFABS(ll) + */3*FFABS(l) + FFABS(lt) + 2*FFABS(t) + FFABS(rt) + FFABS(p));

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
                    int context= av_log2(/*FFABS(ll) + */3*FFABS(l) + FFABS(lt) + 2*FFABS(t) + FFABS(rt) + FFABS(p));
                    int l2= 2*FFABS(l) + (l<0);
                    int t2= 2*FFABS(t) + (t<0);

                    put_symbol2(&s->c, b->state[context + 2], FFABS(v)-1, context-4);
                    put_rac(&s->c, &b->state[0][16 + 1 + 3 + quant3bA[l2&0xFF] + 3*quant3bA[t2&0xFF]], v<0);
                }
            }
        }
    }
    return 0;
}

static int encode_subband(SnowContext *s, SubBand *b, IDWTELEM *src, IDWTELEM *parent, int stride, int orientation){
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
                    int context= av_log2(/*FFABS(ll) + */3*(l>>1) + (lt>>1) + (t&~1) + (rt>>1) + (p>>1));

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
    const int qlog= av_clip(s->qlog + b->qlog, 0, QROOT*16);
    int qmul= qexp[qlog&(QROOT-1)]<<(qlog>>QSHIFT);
    int qadd= (s->qbias*qmul)>>QBIAS_SHIFT;
    int new_index = 0;

    if(b->ibuf == s->spatial_idwt_buffer || s->qlog == LOSSLESS_QLOG){
        qadd= 0;
        qmul= 1<<QEXPSHIFT;
    }

    /* If we are on the second or later slice, restore our index. */
    if (start_y != 0)
        new_index = save_state[0];


    for(y=start_y; y<h; y++){
        int x = 0;
        int v;
        IDWTELEM * line = slice_buffer_get_line(sb, y * b->stride_line + b->buf_y_offset) + b->buf_x_offset;
        memset(line, 0, b->width*sizeof(IDWTELEM));
        v = b->x_coeff[new_index].coeff;
        x = b->x_coeff[new_index++].x;
        while(x < w){
            register int t= ( (v>>1)*qmul + qadd)>>QEXPSHIFT;
            register int u= -(v&1);
            line[x] = (t^u) - u;

            v = b->x_coeff[new_index].coeff;
            x = b->x_coeff[new_index++].x;
        }
    }

    /* Save our variables for the next slice. */
    save_state[0] = new_index;

    return;
}

static void reset_contexts(SnowContext *s){ //FIXME better initial contexts
    int plane_index, level, orientation;

    for(plane_index=0; plane_index<3; plane_index++){
        for(level=0; level<MAX_DECOMPOSITIONS; level++){
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
    uint32_t *sq = ff_squareTbl + 256;

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

static inline void set_blocks(SnowContext *s, int level, int x, int y, int l, int cb, int cr, int mx, int my, int ref, int type){
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
    block.ref= ref;
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

static inline void pred_mv(SnowContext *s, int *mx, int *my, int ref,
                           const BlockNode *left, const BlockNode *top, const BlockNode *tr){
    if(s->ref_frames == 1){
        *mx = mid_pred(left->mx, top->mx, tr->mx);
        *my = mid_pred(left->my, top->my, tr->my);
    }else{
        const int *scale = scale_mv_ref[ref];
        *mx = mid_pred((left->mx * scale[left->ref] + 128) >>8,
                       (top ->mx * scale[top ->ref] + 128) >>8,
                       (tr  ->mx * scale[tr  ->ref] + 128) >>8);
        *my = mid_pred((left->my * scale[left->ref] + 128) >>8,
                       (top ->my * scale[top ->ref] + 128) >>8,
                       (tr  ->my * scale[tr  ->ref] + 128) >>8);
    }
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
    int score, score2, iscore, i_len, p_len, block_s, sum, base_bits;
    const int w= s->b_width  << s->block_max_depth;
    const int h= s->b_height << s->block_max_depth;
    const int rem_depth= s->block_max_depth - level;
    const int index= (x + y*w) << rem_depth;
    const int block_w= 1<<(LOG2_MB_SIZE - level);
    int trx= (x+1)<<rem_depth;
    int try= (y+1)<<rem_depth;
    const BlockNode *left  = x ? &s->block[index-1] : &null_block;
    const BlockNode *top   = y ? &s->block[index-w] : &null_block;
    const BlockNode *right = trx<w ? &s->block[index+1] : &null_block;
    const BlockNode *bottom= try<h ? &s->block[index+w] : &null_block;
    const BlockNode *tl    = y && x ? &s->block[index-w-1] : left;
    const BlockNode *tr    = y && trx<w && ((x&1)==0 || level==0) ? &s->block[index-w+(1<<rem_depth)] : tl; //FIXME use lt
    int pl = left->color[0];
    int pcb= left->color[1];
    int pcr= left->color[2];
    int pmx, pmy;
    int mx=0, my=0;
    int l,cr,cb;
    const int stride= s->current_picture.linesize[0];
    const int uvstride= s->current_picture.linesize[1];
    uint8_t *current_data[3]= { s->input_picture.data[0] + (x + y*  stride)*block_w,
                                s->input_picture.data[1] + (x + y*uvstride)*block_w/2,
                                s->input_picture.data[2] + (x + y*uvstride)*block_w/2};
    int P[10][2];
    int16_t last_mv[3][2];
    int qpel= !!(s->avctx->flags & CODEC_FLAG_QPEL); //unused
    const int shift= 1+qpel;
    MotionEstContext *c= &s->m.me;
    int ref_context= av_log2(2*left->ref) + av_log2(2*top->ref);
    int mx_context= av_log2(2*FFABS(left->mx - top->mx));
    int my_context= av_log2(2*FFABS(left->my - top->my));
    int s_context= 2*left->level + 2*top->level + tl->level + tr->level;
    int ref, best_ref, ref_score, ref_mx, ref_my;

    assert(sizeof(s->block_state) >= 256);
    if(s->keyframe){
        set_blocks(s, level, x, y, pl, pcb, pcr, 0, 0, 0, BLOCK_INTRA);
        return 0;
    }

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
    c->skip= 0;

    assert(c->  stride ==   stride);
    assert(c->uvstride == uvstride);

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

    score= INT_MAX;
    best_ref= 0;
    for(ref=0; ref<s->ref_frames; ref++){
        init_ref(c, current_data, s->last_picture[ref].data, NULL, block_w*x, block_w*y, 0);

        ref_score= ff_epzs_motion_search(&s->m, &ref_mx, &ref_my, P, 0, /*ref_index*/ 0, last_mv,
                                         (1<<16)>>shift, level-LOG2_MB_SIZE+4, block_w);

        assert(ref_mx >= c->xmin);
        assert(ref_mx <= c->xmax);
        assert(ref_my >= c->ymin);
        assert(ref_my <= c->ymax);

        ref_score= c->sub_motion_search(&s->m, &ref_mx, &ref_my, ref_score, 0, 0, level-LOG2_MB_SIZE+4, block_w);
        ref_score= ff_get_mb_score(&s->m, ref_mx, ref_my, 0, 0, level-LOG2_MB_SIZE+4, block_w, 0);
        ref_score+= 2*av_log2(2*ref)*c->penalty_factor;
        if(s->ref_mvs[ref]){
            s->ref_mvs[ref][index][0]= ref_mx;
            s->ref_mvs[ref][index][1]= ref_my;
            s->ref_scores[ref][index]= ref_score;
        }
        if(score > ref_score){
            score= ref_score;
            best_ref= ref;
            mx= ref_mx;
            my= ref_my;
        }
    }
    //FIXME if mb_cmp != SSE then intra cannot be compared currently and mb_penalty vs. lambda2

  //  subpel search
    base_bits= get_rac_count(&s->c) - 8*(s->c.bytestream - s->c.bytestream_start);
    pc= s->c;
    pc.bytestream_start=
    pc.bytestream= p_buffer; //FIXME end/start? and at the other stoo
    memcpy(p_state, s->block_state, sizeof(s->block_state));

    if(level!=s->block_max_depth)
        put_rac(&pc, &p_state[4 + s_context], 1);
    put_rac(&pc, &p_state[1 + left->type + top->type], 0);
    if(s->ref_frames > 1)
        put_symbol(&pc, &p_state[128 + 1024 + 32*ref_context], best_ref, 0);
    pred_mv(s, &pmx, &pmy, best_ref, left, top, tr);
    put_symbol(&pc, &p_state[128 + 32*(mx_context + 16*!!best_ref)], mx - pmx, 1);
    put_symbol(&pc, &p_state[128 + 32*(my_context + 16*!!best_ref)], my - pmy, 1);
    p_len= pc.bytestream - pc.bytestream_start;
    score += (s->lambda2*(get_rac_count(&pc)-base_bits))>>FF_LAMBDA_SHIFT;

    block_s= block_w*block_w;
    sum = pix_sum(current_data[0], stride, block_w);
    l= (sum + block_s/2)/block_s;
    iscore = pix_norm1(current_data[0], stride, block_w) - 2*l*sum + l*l*block_s;

    block_s= block_w*block_w>>2;
    sum = pix_sum(current_data[1], uvstride, block_w>>1);
    cb= (sum + block_s/2)/block_s;
//    iscore += pix_norm1(&current_mb[1][0], uvstride, block_w>>1) - 2*cb*sum + cb*cb*block_s;
    sum = pix_sum(current_data[2], uvstride, block_w>>1);
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
    iscore += (s->lambda2*(get_rac_count(&ic)-base_bits))>>FF_LAMBDA_SHIFT;

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
        pred_mv(s, &pmx, &pmy, 0, left, top, tr);
        memcpy(pbbak, i_buffer, i_len);
        s->c= ic;
        s->c.bytestream_start= pbbak_start;
        s->c.bytestream= pbbak + i_len;
        set_blocks(s, level, x, y, l, cb, cr, pmx, pmy, 0, BLOCK_INTRA);
        memcpy(s->block_state, i_state, sizeof(s->block_state));
        return iscore;
    }else{
        memcpy(pbbak, p_buffer, p_len);
        s->c= pc;
        s->c.bytestream_start= pbbak_start;
        s->c.bytestream= pbbak + p_len;
        set_blocks(s, level, x, y, pl, pcb, pcr, mx, my, best_ref, 0);
        memcpy(s->block_state, p_state, sizeof(s->block_state));
        return score;
    }
}

static av_always_inline int same_block(BlockNode *a, BlockNode *b){
    if((a->type&BLOCK_INTRA) && (b->type&BLOCK_INTRA)){
        return !((a->color[0] - b->color[0]) | (a->color[1] - b->color[1]) | (a->color[2] - b->color[2]));
    }else{
        return !((a->mx - b->mx) | (a->my - b->my) | (a->ref - b->ref) | ((a->type ^ b->type)&BLOCK_INTRA));
    }
}

static void encode_q_branch2(SnowContext *s, int level, int x, int y){
    const int w= s->b_width  << s->block_max_depth;
    const int rem_depth= s->block_max_depth - level;
    const int index= (x + y*w) << rem_depth;
    int trx= (x+1)<<rem_depth;
    BlockNode *b= &s->block[index];
    const BlockNode *left  = x ? &s->block[index-1] : &null_block;
    const BlockNode *top   = y ? &s->block[index-w] : &null_block;
    const BlockNode *tl    = y && x ? &s->block[index-w-1] : left;
    const BlockNode *tr    = y && trx<w && ((x&1)==0 || level==0) ? &s->block[index-w+(1<<rem_depth)] : tl; //FIXME use lt
    int pl = left->color[0];
    int pcb= left->color[1];
    int pcr= left->color[2];
    int pmx, pmy;
    int ref_context= av_log2(2*left->ref) + av_log2(2*top->ref);
    int mx_context= av_log2(2*FFABS(left->mx - top->mx)) + 16*!!b->ref;
    int my_context= av_log2(2*FFABS(left->my - top->my)) + 16*!!b->ref;
    int s_context= 2*left->level + 2*top->level + tl->level + tr->level;

    if(s->keyframe){
        set_blocks(s, level, x, y, pl, pcb, pcr, 0, 0, 0, BLOCK_INTRA);
        return;
    }

    if(level!=s->block_max_depth){
        if(same_block(b,b+1) && same_block(b,b+w) && same_block(b,b+w+1)){
            put_rac(&s->c, &s->block_state[4 + s_context], 1);
        }else{
            put_rac(&s->c, &s->block_state[4 + s_context], 0);
            encode_q_branch2(s, level+1, 2*x+0, 2*y+0);
            encode_q_branch2(s, level+1, 2*x+1, 2*y+0);
            encode_q_branch2(s, level+1, 2*x+0, 2*y+1);
            encode_q_branch2(s, level+1, 2*x+1, 2*y+1);
            return;
        }
    }
    if(b->type & BLOCK_INTRA){
        pred_mv(s, &pmx, &pmy, 0, left, top, tr);
        put_rac(&s->c, &s->block_state[1 + (left->type&1) + (top->type&1)], 1);
        put_symbol(&s->c, &s->block_state[32], b->color[0]-pl , 1);
        put_symbol(&s->c, &s->block_state[64], b->color[1]-pcb, 1);
        put_symbol(&s->c, &s->block_state[96], b->color[2]-pcr, 1);
        set_blocks(s, level, x, y, b->color[0], b->color[1], b->color[2], pmx, pmy, 0, BLOCK_INTRA);
    }else{
        pred_mv(s, &pmx, &pmy, b->ref, left, top, tr);
        put_rac(&s->c, &s->block_state[1 + (left->type&1) + (top->type&1)], 0);
        if(s->ref_frames > 1)
            put_symbol(&s->c, &s->block_state[128 + 1024 + 32*ref_context], b->ref, 0);
        put_symbol(&s->c, &s->block_state[128 + 32*mx_context], b->mx - pmx, 1);
        put_symbol(&s->c, &s->block_state[128 + 32*my_context], b->my - pmy, 1);
        set_blocks(s, level, x, y, pl, pcb, pcr, b->mx, b->my, b->ref, 0);
    }
}

static void decode_q_branch(SnowContext *s, int level, int x, int y){
    const int w= s->b_width << s->block_max_depth;
    const int rem_depth= s->block_max_depth - level;
    const int index= (x + y*w) << rem_depth;
    int trx= (x+1)<<rem_depth;
    const BlockNode *left  = x ? &s->block[index-1] : &null_block;
    const BlockNode *top   = y ? &s->block[index-w] : &null_block;
    const BlockNode *tl    = y && x ? &s->block[index-w-1] : left;
    const BlockNode *tr    = y && trx<w && ((x&1)==0 || level==0) ? &s->block[index-w+(1<<rem_depth)] : tl; //FIXME use lt
    int s_context= 2*left->level + 2*top->level + tl->level + tr->level;

    if(s->keyframe){
        set_blocks(s, level, x, y, null_block.color[0], null_block.color[1], null_block.color[2], null_block.mx, null_block.my, null_block.ref, BLOCK_INTRA);
        return;
    }

    if(level==s->block_max_depth || get_rac(&s->c, &s->block_state[4 + s_context])){
        int type, mx, my;
        int l = left->color[0];
        int cb= left->color[1];
        int cr= left->color[2];
        int ref = 0;
        int ref_context= av_log2(2*left->ref) + av_log2(2*top->ref);
        int mx_context= av_log2(2*FFABS(left->mx - top->mx)) + 0*av_log2(2*FFABS(tr->mx - top->mx));
        int my_context= av_log2(2*FFABS(left->my - top->my)) + 0*av_log2(2*FFABS(tr->my - top->my));

        type= get_rac(&s->c, &s->block_state[1 + left->type + top->type]) ? BLOCK_INTRA : 0;

        if(type){
            pred_mv(s, &mx, &my, 0, left, top, tr);
            l += get_symbol(&s->c, &s->block_state[32], 1);
            cb+= get_symbol(&s->c, &s->block_state[64], 1);
            cr+= get_symbol(&s->c, &s->block_state[96], 1);
        }else{
            if(s->ref_frames > 1)
                ref= get_symbol(&s->c, &s->block_state[128 + 1024 + 32*ref_context], 0);
            pred_mv(s, &mx, &my, ref, left, top, tr);
            mx+= get_symbol(&s->c, &s->block_state[128 + 32*(mx_context + 16*!!ref)], 1);
            my+= get_symbol(&s->c, &s->block_state[128 + 32*(my_context + 16*!!ref)], 1);
        }
        set_blocks(s, level, x, y, l, cb, cr, mx, my, ref, type);
    }else{
        decode_q_branch(s, level+1, 2*x+0, 2*y+0);
        decode_q_branch(s, level+1, 2*x+1, 2*y+0);
        decode_q_branch(s, level+1, 2*x+0, 2*y+1);
        decode_q_branch(s, level+1, 2*x+1, 2*y+1);
    }
}

static void encode_blocks(SnowContext *s, int search){
    int x, y;
    int w= s->b_width;
    int h= s->b_height;

    if(s->avctx->me_method == ME_ITER && !s->keyframe && search)
        iterative_me(s);

    for(y=0; y<h; y++){
        if(s->c.bytestream_end - s->c.bytestream < w*MB_SIZE*MB_SIZE*3){ //FIXME nicer limit
            av_log(s->avctx, AV_LOG_ERROR, "encoded frame too large\n");
            return;
        }
        for(x=0; x<w; x++){
            if(s->avctx->me_method == ME_ITER || !search)
                encode_q_branch2(s, 0, x, y);
            else
                encode_q_branch (s, 0, x, y);
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

static void mc_block(Plane *p, uint8_t *dst, const uint8_t *src, uint8_t *tmp, int stride, int b_w, int b_h, int dx, int dy){
    const static uint8_t weight[64]={
    8,7,6,5,4,3,2,1,
    7,7,0,0,0,0,0,1,
    6,0,6,0,0,0,2,0,
    5,0,0,5,0,3,0,0,
    4,0,0,0,4,0,0,0,
    3,0,0,5,0,3,0,0,
    2,0,6,0,0,0,2,0,
    1,7,0,0,0,0,0,1,
    };

    const static uint8_t brane[256]={
    0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x11,0x12,0x12,0x12,0x12,0x12,0x12,0x12,
    0x04,0x05,0xcc,0xcc,0xcc,0xcc,0xcc,0x41,0x15,0x16,0xcc,0xcc,0xcc,0xcc,0xcc,0x52,
    0x04,0xcc,0x05,0xcc,0xcc,0xcc,0x41,0xcc,0x15,0xcc,0x16,0xcc,0xcc,0xcc,0x52,0xcc,
    0x04,0xcc,0xcc,0x05,0xcc,0x41,0xcc,0xcc,0x15,0xcc,0xcc,0x16,0xcc,0x52,0xcc,0xcc,
    0x04,0xcc,0xcc,0xcc,0x41,0xcc,0xcc,0xcc,0x15,0xcc,0xcc,0xcc,0x16,0xcc,0xcc,0xcc,
    0x04,0xcc,0xcc,0x41,0xcc,0x05,0xcc,0xcc,0x15,0xcc,0xcc,0x52,0xcc,0x16,0xcc,0xcc,
    0x04,0xcc,0x41,0xcc,0xcc,0xcc,0x05,0xcc,0x15,0xcc,0x52,0xcc,0xcc,0xcc,0x16,0xcc,
    0x04,0x41,0xcc,0xcc,0xcc,0xcc,0xcc,0x05,0x15,0x52,0xcc,0xcc,0xcc,0xcc,0xcc,0x16,
    0x44,0x45,0x45,0x45,0x45,0x45,0x45,0x45,0x55,0x56,0x56,0x56,0x56,0x56,0x56,0x56,
    0x48,0x49,0xcc,0xcc,0xcc,0xcc,0xcc,0x85,0x59,0x5A,0xcc,0xcc,0xcc,0xcc,0xcc,0x96,
    0x48,0xcc,0x49,0xcc,0xcc,0xcc,0x85,0xcc,0x59,0xcc,0x5A,0xcc,0xcc,0xcc,0x96,0xcc,
    0x48,0xcc,0xcc,0x49,0xcc,0x85,0xcc,0xcc,0x59,0xcc,0xcc,0x5A,0xcc,0x96,0xcc,0xcc,
    0x48,0xcc,0xcc,0xcc,0x49,0xcc,0xcc,0xcc,0x59,0xcc,0xcc,0xcc,0x96,0xcc,0xcc,0xcc,
    0x48,0xcc,0xcc,0x85,0xcc,0x49,0xcc,0xcc,0x59,0xcc,0xcc,0x96,0xcc,0x5A,0xcc,0xcc,
    0x48,0xcc,0x85,0xcc,0xcc,0xcc,0x49,0xcc,0x59,0xcc,0x96,0xcc,0xcc,0xcc,0x5A,0xcc,
    0x48,0x85,0xcc,0xcc,0xcc,0xcc,0xcc,0x49,0x59,0x96,0xcc,0xcc,0xcc,0xcc,0xcc,0x5A,
    };

    const static uint8_t needs[16]={
    0,1,0,0,
    2,4,2,0,
    0,1,0,0,
    15
    };

    int x, y, b, r, l;
    int16_t tmpIt   [64*(32+HTAPS_MAX)];
    uint8_t tmp2t[3][stride*(32+HTAPS_MAX)];
    int16_t *tmpI= tmpIt;
    uint8_t *tmp2= tmp2t[0];
    const uint8_t *hpel[11];
    assert(dx<16 && dy<16);
    r= brane[dx + 16*dy]&15;
    l= brane[dx + 16*dy]>>4;

    b= needs[l] | needs[r];
    if(p && !p->diag_mc)
        b= 15;

    if(b&5){
        for(y=0; y < b_h+HTAPS_MAX-1; y++){
            for(x=0; x < b_w; x++){
                int a_1=src[x + HTAPS_MAX/2-4];
                int a0= src[x + HTAPS_MAX/2-3];
                int a1= src[x + HTAPS_MAX/2-2];
                int a2= src[x + HTAPS_MAX/2-1];
                int a3= src[x + HTAPS_MAX/2+0];
                int a4= src[x + HTAPS_MAX/2+1];
                int a5= src[x + HTAPS_MAX/2+2];
                int a6= src[x + HTAPS_MAX/2+3];
                int am=0;
                if(!p || p->fast_mc){
                    am= 20*(a2+a3) - 5*(a1+a4) + (a0+a5);
                    tmpI[x]= am;
                    am= (am+16)>>5;
                }else{
                    am= p->hcoeff[0]*(a2+a3) + p->hcoeff[1]*(a1+a4) + p->hcoeff[2]*(a0+a5) + p->hcoeff[3]*(a_1+a6);
                    tmpI[x]= am;
                    am= (am+32)>>6;
                }

                if(am&(~255)) am= ~(am>>31);
                tmp2[x]= am;
            }
            tmpI+= 64;
            tmp2+= stride;
            src += stride;
        }
        src -= stride*y;
    }
    src += HTAPS_MAX/2 - 1;
    tmp2= tmp2t[1];

    if(b&2){
        for(y=0; y < b_h; y++){
            for(x=0; x < b_w+1; x++){
                int a_1=src[x + (HTAPS_MAX/2-4)*stride];
                int a0= src[x + (HTAPS_MAX/2-3)*stride];
                int a1= src[x + (HTAPS_MAX/2-2)*stride];
                int a2= src[x + (HTAPS_MAX/2-1)*stride];
                int a3= src[x + (HTAPS_MAX/2+0)*stride];
                int a4= src[x + (HTAPS_MAX/2+1)*stride];
                int a5= src[x + (HTAPS_MAX/2+2)*stride];
                int a6= src[x + (HTAPS_MAX/2+3)*stride];
                int am=0;
                if(!p || p->fast_mc)
                    am= (20*(a2+a3) - 5*(a1+a4) + (a0+a5) + 16)>>5;
                else
                    am= (p->hcoeff[0]*(a2+a3) + p->hcoeff[1]*(a1+a4) + p->hcoeff[2]*(a0+a5) + p->hcoeff[3]*(a_1+a6) + 32)>>6;

                if(am&(~255)) am= ~(am>>31);
                tmp2[x]= am;
            }
            src += stride;
            tmp2+= stride;
        }
        src -= stride*y;
    }
    src += stride*(HTAPS_MAX/2 - 1);
    tmp2= tmp2t[2];
    tmpI= tmpIt;
    if(b&4){
        for(y=0; y < b_h; y++){
            for(x=0; x < b_w; x++){
                int a_1=tmpI[x + (HTAPS_MAX/2-4)*64];
                int a0= tmpI[x + (HTAPS_MAX/2-3)*64];
                int a1= tmpI[x + (HTAPS_MAX/2-2)*64];
                int a2= tmpI[x + (HTAPS_MAX/2-1)*64];
                int a3= tmpI[x + (HTAPS_MAX/2+0)*64];
                int a4= tmpI[x + (HTAPS_MAX/2+1)*64];
                int a5= tmpI[x + (HTAPS_MAX/2+2)*64];
                int a6= tmpI[x + (HTAPS_MAX/2+3)*64];
                int am=0;
                if(!p || p->fast_mc)
                    am= (20*(a2+a3) - 5*(a1+a4) + (a0+a5) + 512)>>10;
                else
                    am= (p->hcoeff[0]*(a2+a3) + p->hcoeff[1]*(a1+a4) + p->hcoeff[2]*(a0+a5) + p->hcoeff[3]*(a_1+a6) + 2048)>>12;
                if(am&(~255)) am= ~(am>>31);
                tmp2[x]= am;
            }
            tmpI+= 64;
            tmp2+= stride;
        }
    }

    hpel[ 0]= src;
    hpel[ 1]= tmp2t[0] + stride*(HTAPS_MAX/2-1);
    hpel[ 2]= src + 1;

    hpel[ 4]= tmp2t[1];
    hpel[ 5]= tmp2t[2];
    hpel[ 6]= tmp2t[1] + 1;

    hpel[ 8]= src + stride;
    hpel[ 9]= hpel[1] + stride;
    hpel[10]= hpel[8] + 1;

    if(b==15){
        const uint8_t *src1= hpel[dx/8 + dy/8*4  ];
        const uint8_t *src2= hpel[dx/8 + dy/8*4+1];
        const uint8_t *src3= hpel[dx/8 + dy/8*4+4];
        const uint8_t *src4= hpel[dx/8 + dy/8*4+5];
        dx&=7;
        dy&=7;
        for(y=0; y < b_h; y++){
            for(x=0; x < b_w; x++){
                dst[x]= ((8-dx)*(8-dy)*src1[x] + dx*(8-dy)*src2[x]+
                         (8-dx)*   dy *src3[x] + dx*   dy *src4[x]+32)>>6;
            }
            src1+=stride;
            src2+=stride;
            src3+=stride;
            src4+=stride;
            dst +=stride;
        }
    }else{
        const uint8_t *src1= hpel[l];
        const uint8_t *src2= hpel[r];
        int a= weight[((dx&7) + (8*(dy&7)))];
        int b= 8-a;
        for(y=0; y < b_h; y++){
            for(x=0; x < b_w; x++){
                dst[x]= (a*src1[x] + b*src2[x] + 4)>>3;
            }
            src1+=stride;
            src2+=stride;
            dst +=stride;
        }
    }
}

#define mca(dx,dy,b_w)\
static void mc_block_hpel ## dx ## dy ## b_w(uint8_t *dst, const uint8_t *src, int stride, int h){\
    uint8_t tmp[stride*(b_w+HTAPS_MAX-1)];\
    assert(h==b_w);\
    mc_block(NULL, dst, src-(HTAPS_MAX/2-1)-(HTAPS_MAX/2-1)*stride, tmp, stride, b_w, b_w, dx, dy);\
}

mca( 0, 0,16)
mca( 8, 0,16)
mca( 0, 8,16)
mca( 8, 8,16)
mca( 0, 0,8)
mca( 8, 0,8)
mca( 0, 8,8)
mca( 8, 8,8)

static void pred_block(SnowContext *s, uint8_t *dst, uint8_t *tmp, int stride, int sx, int sy, int b_w, int b_h, BlockNode *block, int plane_index, int w, int h){
    if(block->type & BLOCK_INTRA){
        int x, y;
        const int color = block->color[plane_index];
        const int color4= color*0x01010101;
        if(b_w==32){
            for(y=0; y < b_h; y++){
                *(uint32_t*)&dst[0 + y*stride]= color4;
                *(uint32_t*)&dst[4 + y*stride]= color4;
                *(uint32_t*)&dst[8 + y*stride]= color4;
                *(uint32_t*)&dst[12+ y*stride]= color4;
                *(uint32_t*)&dst[16+ y*stride]= color4;
                *(uint32_t*)&dst[20+ y*stride]= color4;
                *(uint32_t*)&dst[24+ y*stride]= color4;
                *(uint32_t*)&dst[28+ y*stride]= color4;
            }
        }else if(b_w==16){
            for(y=0; y < b_h; y++){
                *(uint32_t*)&dst[0 + y*stride]= color4;
                *(uint32_t*)&dst[4 + y*stride]= color4;
                *(uint32_t*)&dst[8 + y*stride]= color4;
                *(uint32_t*)&dst[12+ y*stride]= color4;
            }
        }else if(b_w==8){
            for(y=0; y < b_h; y++){
                *(uint32_t*)&dst[0 + y*stride]= color4;
                *(uint32_t*)&dst[4 + y*stride]= color4;
            }
        }else if(b_w==4){
            for(y=0; y < b_h; y++){
                *(uint32_t*)&dst[0 + y*stride]= color4;
            }
        }else{
            for(y=0; y < b_h; y++){
                for(x=0; x < b_w; x++){
                    dst[x + y*stride]= color;
                }
            }
        }
    }else{
        uint8_t *src= s->last_picture[block->ref].data[plane_index];
        const int scale= plane_index ?  s->mv_scale : 2*s->mv_scale;
        int mx= block->mx*scale;
        int my= block->my*scale;
        const int dx= mx&15;
        const int dy= my&15;
        const int tab_index= 3 - (b_w>>2) + (b_w>>4);
        sx += (mx>>4) - (HTAPS_MAX/2-1);
        sy += (my>>4) - (HTAPS_MAX/2-1);
        src += sx + sy*stride;
        if(   (unsigned)sx >= w - b_w - (HTAPS_MAX-2)
           || (unsigned)sy >= h - b_h - (HTAPS_MAX-2)){
            ff_emulated_edge_mc(tmp + MB_SIZE, src, stride, b_w+HTAPS_MAX-1, b_h+HTAPS_MAX-1, sx, sy, w, h);
            src= tmp + MB_SIZE;
        }
//        assert(b_w == b_h || 2*b_w == b_h || b_w == 2*b_h);
//        assert(!(b_w&(b_w-1)));
        assert(b_w>1 && b_h>1);
        assert((tab_index>=0 && tab_index<4) || b_w==32);
        if((dx&3) || (dy&3) || !(b_w == b_h || 2*b_w == b_h || b_w == 2*b_h) || (b_w&(b_w-1)) || !s->plane[plane_index].fast_mc )
            mc_block(&s->plane[plane_index], dst, src, tmp, stride, b_w, b_h, dx, dy);
        else if(b_w==32){
            int y;
            for(y=0; y<b_h; y+=16){
                s->dsp.put_h264_qpel_pixels_tab[0][dy+(dx>>2)](dst + y*stride, src + 3 + (y+3)*stride,stride);
                s->dsp.put_h264_qpel_pixels_tab[0][dy+(dx>>2)](dst + 16 + y*stride, src + 19 + (y+3)*stride,stride);
            }
        }else if(b_w==b_h)
            s->dsp.put_h264_qpel_pixels_tab[tab_index  ][dy+(dx>>2)](dst,src + 3 + 3*stride,stride);
        else if(b_w==2*b_h){
            s->dsp.put_h264_qpel_pixels_tab[tab_index+1][dy+(dx>>2)](dst    ,src + 3       + 3*stride,stride);
            s->dsp.put_h264_qpel_pixels_tab[tab_index+1][dy+(dx>>2)](dst+b_h,src + 3 + b_h + 3*stride,stride);
        }else{
            assert(2*b_w==b_h);
            s->dsp.put_h264_qpel_pixels_tab[tab_index  ][dy+(dx>>2)](dst           ,src + 3 + 3*stride           ,stride);
            s->dsp.put_h264_qpel_pixels_tab[tab_index  ][dy+(dx>>2)](dst+b_w*stride,src + 3 + 3*stride+b_w*stride,stride);
        }
    }
}

void ff_snow_inner_add_yblock(const uint8_t *obmc, const int obmc_stride, uint8_t * * block, int b_w, int b_h,
                              int src_x, int src_y, int src_stride, slice_buffer * sb, int add, uint8_t * dst8){
    int y, x;
    IDWTELEM * dst;
    for(y=0; y<b_h; y++){
        //FIXME ugly misuse of obmc_stride
        const uint8_t *obmc1= obmc + y*obmc_stride;
        const uint8_t *obmc2= obmc1+ (obmc_stride>>1);
        const uint8_t *obmc3= obmc1+ obmc_stride*(obmc_stride>>1);
        const uint8_t *obmc4= obmc3+ (obmc_stride>>1);
        dst = slice_buffer_get_line(sb, src_y + y);
        for(x=0; x<b_w; x++){
            int v=   obmc1[x] * block[3][x + y*src_stride]
                    +obmc2[x] * block[2][x + y*src_stride]
                    +obmc3[x] * block[1][x + y*src_stride]
                    +obmc4[x] * block[0][x + y*src_stride];

            v <<= 8 - LOG2_OBMC_MAX;
            if(FRAC_BITS != 8){
                v >>= 8 - FRAC_BITS;
            }
            if(add){
                v += dst[x + src_x];
                v = (v + (1<<(FRAC_BITS-1))) >> FRAC_BITS;
                if(v&(~255)) v= ~(v>>31);
                dst8[x + y*src_stride] = v;
            }else{
                dst[x + src_x] -= v;
            }
        }
    }
}

//FIXME name cleanup (b_w, block_w, b_width stuff)
static av_always_inline void add_yblock(SnowContext *s, int sliced, slice_buffer *sb, IDWTELEM *dst, uint8_t *dst8, const uint8_t *obmc, int src_x, int src_y, int b_w, int b_h, int w, int h, int dst_stride, int src_stride, int obmc_stride, int b_x, int b_y, int add, int offset_dst, int plane_index){
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

    if(src_x<0){ //FIXME merge with prev & always round internal width up to *16
        obmc -= src_x;
        b_w += src_x;
        if(!sliced && !offset_dst)
            dst -= src_x;
        src_x=0;
    }else if(src_x + b_w > w){
        b_w = w - src_x;
    }
    if(src_y<0){
        obmc -= src_y*obmc_stride;
        b_h += src_y;
        if(!sliced && !offset_dst)
            dst -= src_y*dst_stride;
        src_y=0;
    }else if(src_y + b_h> h){
        b_h = h - src_y;
    }

    if(b_w<=0 || b_h<=0) return;

    assert(src_stride > 2*MB_SIZE + 5);

    if(!sliced && offset_dst)
        dst += src_x + src_y*dst_stride;
    dst8+= src_x + src_y*src_stride;
//    src += src_x + src_y*src_stride;

    ptmp= tmp + 3*tmp_step;
    block[0]= ptmp;
    ptmp+=tmp_step;
    pred_block(s, block[0], tmp, src_stride, src_x, src_y, b_w, b_h, lt, plane_index, w, h);

    if(same_block(lt, rt)){
        block[1]= block[0];
    }else{
        block[1]= ptmp;
        ptmp+=tmp_step;
        pred_block(s, block[1], tmp, src_stride, src_x, src_y, b_w, b_h, rt, plane_index, w, h);
    }

    if(same_block(lt, lb)){
        block[2]= block[0];
    }else if(same_block(rt, lb)){
        block[2]= block[1];
    }else{
        block[2]= ptmp;
        ptmp+=tmp_step;
        pred_block(s, block[2], tmp, src_stride, src_x, src_y, b_w, b_h, lb, plane_index, w, h);
    }

    if(same_block(lt, rb) ){
        block[3]= block[0];
    }else if(same_block(rt, rb)){
        block[3]= block[1];
    }else if(same_block(lb, rb)){
        block[3]= block[2];
    }else{
        block[3]= ptmp;
        pred_block(s, block[3], tmp, src_stride, src_x, src_y, b_w, b_h, rb, plane_index, w, h);
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
    if(sliced){
        s->dsp.inner_add_yblock(obmc, obmc_stride, block, b_w, b_h, src_x,src_y, src_stride, sb, add, dst8);
    }else{
        for(y=0; y<b_h; y++){
            //FIXME ugly misuse of obmc_stride
            const uint8_t *obmc1= obmc + y*obmc_stride;
            const uint8_t *obmc2= obmc1+ (obmc_stride>>1);
            const uint8_t *obmc3= obmc1+ obmc_stride*(obmc_stride>>1);
            const uint8_t *obmc4= obmc3+ (obmc_stride>>1);
            for(x=0; x<b_w; x++){
                int v=   obmc1[x] * block[3][x + y*src_stride]
                        +obmc2[x] * block[2][x + y*src_stride]
                        +obmc3[x] * block[1][x + y*src_stride]
                        +obmc4[x] * block[0][x + y*src_stride];

                v <<= 8 - LOG2_OBMC_MAX;
                if(FRAC_BITS != 8){
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
    }
#endif /* 0 */
}

static av_always_inline void predict_slice_buffered(SnowContext *s, slice_buffer * sb, IDWTELEM * old_buffer, int plane_index, int add, int mb_y){
    Plane *p= &s->plane[plane_index];
    const int mb_w= s->b_width  << s->block_max_depth;
    const int mb_h= s->b_height << s->block_max_depth;
    int x, y, mb_x;
    int block_size = MB_SIZE >> s->block_max_depth;
    int block_w    = plane_index ? block_size/2 : block_size;
    const uint8_t *obmc  = plane_index ? obmc_tab[s->block_max_depth+1] : obmc_tab[s->block_max_depth];
    int obmc_stride= plane_index ? block_size : 2*block_size;
    int ref_stride= s->current_picture.linesize[plane_index];
    uint8_t *dst8= s->current_picture.data[plane_index];
    int w= p->width;
    int h= p->height;

    if(s->keyframe || (s->avctx->debug&512)){
        if(mb_y==mb_h)
            return;

        if(add){
            for(y=block_w*mb_y; y<FFMIN(h,block_w*(mb_y+1)); y++){
//                DWTELEM * line = slice_buffer_get_line(sb, y);
                IDWTELEM * line = sb->line[y];
                for(x=0; x<w; x++){
//                    int v= buf[x + y*w] + (128<<FRAC_BITS) + (1<<(FRAC_BITS-1));
                    int v= line[x] + (128<<FRAC_BITS) + (1<<(FRAC_BITS-1));
                    v >>= FRAC_BITS;
                    if(v&(~255)) v= ~(v>>31);
                    dst8[x + y*ref_stride]= v;
                }
            }
        }else{
            for(y=block_w*mb_y; y<FFMIN(h,block_w*(mb_y+1)); y++){
//                DWTELEM * line = slice_buffer_get_line(sb, y);
                IDWTELEM * line = sb->line[y];
                for(x=0; x<w; x++){
                    line[x] -= 128 << FRAC_BITS;
//                    buf[x + y*w]-= 128<<FRAC_BITS;
                }
            }
        }

        return;
    }

    for(mb_x=0; mb_x<=mb_w; mb_x++){
        add_yblock(s, 1, sb, old_buffer, dst8, obmc,
                   block_w*mb_x - block_w/2,
                   block_w*mb_y - block_w/2,
                   block_w, block_w,
                   w, h,
                   w, ref_stride, obmc_stride,
                   mb_x - 1, mb_y - 1,
                   add, 0, plane_index);
    }
}

static av_always_inline void predict_slice(SnowContext *s, IDWTELEM *buf, int plane_index, int add, int mb_y){
    Plane *p= &s->plane[plane_index];
    const int mb_w= s->b_width  << s->block_max_depth;
    const int mb_h= s->b_height << s->block_max_depth;
    int x, y, mb_x;
    int block_size = MB_SIZE >> s->block_max_depth;
    int block_w    = plane_index ? block_size/2 : block_size;
    const uint8_t *obmc  = plane_index ? obmc_tab[s->block_max_depth+1] : obmc_tab[s->block_max_depth];
    const int obmc_stride= plane_index ? block_size : 2*block_size;
    int ref_stride= s->current_picture.linesize[plane_index];
    uint8_t *dst8= s->current_picture.data[plane_index];
    int w= p->width;
    int h= p->height;

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
        add_yblock(s, 0, NULL, buf, dst8, obmc,
                   block_w*mb_x - block_w/2,
                   block_w*mb_y - block_w/2,
                   block_w, block_w,
                   w, h,
                   w, ref_stride, obmc_stride,
                   mb_x - 1, mb_y - 1,
                   add, 1, plane_index);
    }
}

static av_always_inline void predict_plane(SnowContext *s, IDWTELEM *buf, int plane_index, int add){
    const int mb_h= s->b_height << s->block_max_depth;
    int mb_y;
    for(mb_y=0; mb_y<=mb_h; mb_y++)
        predict_slice(s, buf, plane_index, add, mb_y);
}

static int get_dc(SnowContext *s, int mb_x, int mb_y, int plane_index){
    int i, x2, y2;
    Plane *p= &s->plane[plane_index];
    const int block_size = MB_SIZE >> s->block_max_depth;
    const int block_w    = plane_index ? block_size/2 : block_size;
    const uint8_t *obmc  = plane_index ? obmc_tab[s->block_max_depth+1] : obmc_tab[s->block_max_depth];
    const int obmc_stride= plane_index ? block_size : 2*block_size;
    const int ref_stride= s->current_picture.linesize[plane_index];
    uint8_t *src= s-> input_picture.data[plane_index];
    IDWTELEM *dst= (IDWTELEM*)s->m.obmc_scratchpad + plane_index*block_size*block_size*4; //FIXME change to unsigned
    const int b_stride = s->b_width << s->block_max_depth;
    const int w= p->width;
    const int h= p->height;
    int index= mb_x + mb_y*b_stride;
    BlockNode *b= &s->block[index];
    BlockNode backup= *b;
    int ab=0;
    int aa=0;

    b->type|= BLOCK_INTRA;
    b->color[plane_index]= 0;
    memset(dst, 0, obmc_stride*obmc_stride*sizeof(IDWTELEM));

    for(i=0; i<4; i++){
        int mb_x2= mb_x + (i &1) - 1;
        int mb_y2= mb_y + (i>>1) - 1;
        int x= block_w*mb_x2 + block_w/2;
        int y= block_w*mb_y2 + block_w/2;

        add_yblock(s, 0, NULL, dst + ((i&1)+(i>>1)*obmc_stride)*block_w, NULL, obmc,
                    x, y, block_w, block_w, w, h, obmc_stride, ref_stride, obmc_stride, mb_x2, mb_y2, 0, 0, plane_index);

        for(y2= FFMAX(y, 0); y2<FFMIN(h, y+block_w); y2++){
            for(x2= FFMAX(x, 0); x2<FFMIN(w, x+block_w); x2++){
                int index= x2-(block_w*mb_x - block_w/2) + (y2-(block_w*mb_y - block_w/2))*obmc_stride;
                int obmc_v= obmc[index];
                int d;
                if(y<0) obmc_v += obmc[index + block_w*obmc_stride];
                if(x<0) obmc_v += obmc[index + block_w];
                if(y+block_w>h) obmc_v += obmc[index - block_w*obmc_stride];
                if(x+block_w>w) obmc_v += obmc[index - block_w];
                //FIXME precalculate this or simplify it somehow else

                d = -dst[index] + (1<<(FRAC_BITS-1));
                dst[index] = d;
                ab += (src[x2 + y2*ref_stride] - (d>>FRAC_BITS)) * obmc_v;
                aa += obmc_v * obmc_v; //FIXME precalculate this
            }
        }
    }
    *b= backup;

    return av_clip(((ab<<LOG2_OBMC_MAX) + aa/2)/aa, 0, 255); //FIXME we should not need clipping
}

static inline int get_block_bits(SnowContext *s, int x, int y, int w){
    const int b_stride = s->b_width << s->block_max_depth;
    const int b_height = s->b_height<< s->block_max_depth;
    int index= x + y*b_stride;
    const BlockNode *b     = &s->block[index];
    const BlockNode *left  = x ? &s->block[index-1] : &null_block;
    const BlockNode *top   = y ? &s->block[index-b_stride] : &null_block;
    const BlockNode *tl    = y && x ? &s->block[index-b_stride-1] : left;
    const BlockNode *tr    = y && x+w<b_stride ? &s->block[index-b_stride+w] : tl;
    int dmx, dmy;
//  int mx_context= av_log2(2*FFABS(left->mx - top->mx));
//  int my_context= av_log2(2*FFABS(left->my - top->my));

    if(x<0 || x>=b_stride || y>=b_height)
        return 0;
/*
1            0      0
01X          1-2    1
001XX        3-6    2-3
0001XXX      7-14   4-7
00001XXXX   15-30   8-15
*/
//FIXME try accurate rate
//FIXME intra and inter predictors if surrounding blocks are not the same type
    if(b->type & BLOCK_INTRA){
        return 3+2*( av_log2(2*FFABS(left->color[0] - b->color[0]))
                   + av_log2(2*FFABS(left->color[1] - b->color[1]))
                   + av_log2(2*FFABS(left->color[2] - b->color[2])));
    }else{
        pred_mv(s, &dmx, &dmy, b->ref, left, top, tr);
        dmx-= b->mx;
        dmy-= b->my;
        return 2*(1 + av_log2(2*FFABS(dmx)) //FIXME kill the 2* can be merged in lambda
                    + av_log2(2*FFABS(dmy))
                    + av_log2(2*b->ref));
    }
}

static int get_block_rd(SnowContext *s, int mb_x, int mb_y, int plane_index, const uint8_t *obmc_edged){
    Plane *p= &s->plane[plane_index];
    const int block_size = MB_SIZE >> s->block_max_depth;
    const int block_w    = plane_index ? block_size/2 : block_size;
    const int obmc_stride= plane_index ? block_size : 2*block_size;
    const int ref_stride= s->current_picture.linesize[plane_index];
    uint8_t *dst= s->current_picture.data[plane_index];
    uint8_t *src= s->  input_picture.data[plane_index];
    IDWTELEM *pred= (IDWTELEM*)s->m.obmc_scratchpad + plane_index*block_size*block_size*4;
    uint8_t cur[ref_stride*2*MB_SIZE]; //FIXME alignment
    uint8_t tmp[ref_stride*(2*MB_SIZE+HTAPS_MAX-1)];
    const int b_stride = s->b_width << s->block_max_depth;
    const int b_height = s->b_height<< s->block_max_depth;
    const int w= p->width;
    const int h= p->height;
    int distortion;
    int rate= 0;
    const int penalty_factor= get_penalty_factor(s->lambda, s->lambda2, s->avctx->me_cmp);
    int sx= block_w*mb_x - block_w/2;
    int sy= block_w*mb_y - block_w/2;
    int x0= FFMAX(0,-sx);
    int y0= FFMAX(0,-sy);
    int x1= FFMIN(block_w*2, w-sx);
    int y1= FFMIN(block_w*2, h-sy);
    int i,x,y;

    pred_block(s, cur, tmp, ref_stride, sx, sy, block_w*2, block_w*2, &s->block[mb_x + mb_y*b_stride], plane_index, w, h);

    for(y=y0; y<y1; y++){
        const uint8_t *obmc1= obmc_edged + y*obmc_stride;
        const IDWTELEM *pred1 = pred + y*obmc_stride;
        uint8_t *cur1 = cur + y*ref_stride;
        uint8_t *dst1 = dst + sx + (sy+y)*ref_stride;
        for(x=x0; x<x1; x++){
#if FRAC_BITS >= LOG2_OBMC_MAX
            int v = (cur1[x] * obmc1[x]) << (FRAC_BITS - LOG2_OBMC_MAX);
#else
            int v = (cur1[x] * obmc1[x] + (1<<(LOG2_OBMC_MAX - FRAC_BITS-1))) >> (LOG2_OBMC_MAX - FRAC_BITS);
#endif
            v = (v + pred1[x]) >> FRAC_BITS;
            if(v&(~255)) v= ~(v>>31);
            dst1[x] = v;
        }
    }

    /* copy the regions where obmc[] = (uint8_t)256 */
    if(LOG2_OBMC_MAX == 8
        && (mb_x == 0 || mb_x == b_stride-1)
        && (mb_y == 0 || mb_y == b_height-1)){
        if(mb_x == 0)
            x1 = block_w;
        else
            x0 = block_w;
        if(mb_y == 0)
            y1 = block_w;
        else
            y0 = block_w;
        for(y=y0; y<y1; y++)
            memcpy(dst + sx+x0 + (sy+y)*ref_stride, cur + x0 + y*ref_stride, x1-x0);
    }

    if(block_w==16){
        /* FIXME rearrange dsputil to fit 32x32 cmp functions */
        /* FIXME check alignment of the cmp wavelet vs the encoding wavelet */
        /* FIXME cmps overlap but do not cover the wavelet's whole support.
         * So improving the score of one block is not strictly guaranteed
         * to improve the score of the whole frame, thus iterative motion
         * estimation does not always converge. */
        if(s->avctx->me_cmp == FF_CMP_W97)
            distortion = w97_32_c(&s->m, src + sx + sy*ref_stride, dst + sx + sy*ref_stride, ref_stride, 32);
        else if(s->avctx->me_cmp == FF_CMP_W53)
            distortion = w53_32_c(&s->m, src + sx + sy*ref_stride, dst + sx + sy*ref_stride, ref_stride, 32);
        else{
            distortion = 0;
            for(i=0; i<4; i++){
                int off = sx+16*(i&1) + (sy+16*(i>>1))*ref_stride;
                distortion += s->dsp.me_cmp[0](&s->m, src + off, dst + off, ref_stride, 16);
            }
        }
    }else{
        assert(block_w==8);
        distortion = s->dsp.me_cmp[0](&s->m, src + sx + sy*ref_stride, dst + sx + sy*ref_stride, ref_stride, block_w*2);
    }

    if(plane_index==0){
        for(i=0; i<4; i++){
/* ..RRr
 * .RXx.
 * rxx..
 */
            rate += get_block_bits(s, mb_x + (i&1) - (i>>1), mb_y + (i>>1), 1);
        }
        if(mb_x == b_stride-2)
            rate += get_block_bits(s, mb_x + 1, mb_y + 1, 1);
    }
    return distortion + rate*penalty_factor;
}

static int get_4block_rd(SnowContext *s, int mb_x, int mb_y, int plane_index){
    int i, y2;
    Plane *p= &s->plane[plane_index];
    const int block_size = MB_SIZE >> s->block_max_depth;
    const int block_w    = plane_index ? block_size/2 : block_size;
    const uint8_t *obmc  = plane_index ? obmc_tab[s->block_max_depth+1] : obmc_tab[s->block_max_depth];
    const int obmc_stride= plane_index ? block_size : 2*block_size;
    const int ref_stride= s->current_picture.linesize[plane_index];
    uint8_t *dst= s->current_picture.data[plane_index];
    uint8_t *src= s-> input_picture.data[plane_index];
    //FIXME zero_dst is const but add_yblock changes dst if add is 0 (this is never the case for dst=zero_dst
    // const has only been removed from zero_dst to suppress a warning
    static IDWTELEM zero_dst[4096]; //FIXME
    const int b_stride = s->b_width << s->block_max_depth;
    const int w= p->width;
    const int h= p->height;
    int distortion= 0;
    int rate= 0;
    const int penalty_factor= get_penalty_factor(s->lambda, s->lambda2, s->avctx->me_cmp);

    for(i=0; i<9; i++){
        int mb_x2= mb_x + (i%3) - 1;
        int mb_y2= mb_y + (i/3) - 1;
        int x= block_w*mb_x2 + block_w/2;
        int y= block_w*mb_y2 + block_w/2;

        add_yblock(s, 0, NULL, zero_dst, dst, obmc,
                   x, y, block_w, block_w, w, h, /*dst_stride*/0, ref_stride, obmc_stride, mb_x2, mb_y2, 1, 1, plane_index);

        //FIXME find a cleaner/simpler way to skip the outside stuff
        for(y2= y; y2<0; y2++)
            memcpy(dst + x + y2*ref_stride, src + x + y2*ref_stride, block_w);
        for(y2= h; y2<y+block_w; y2++)
            memcpy(dst + x + y2*ref_stride, src + x + y2*ref_stride, block_w);
        if(x<0){
            for(y2= y; y2<y+block_w; y2++)
                memcpy(dst + x + y2*ref_stride, src + x + y2*ref_stride, -x);
        }
        if(x+block_w > w){
            for(y2= y; y2<y+block_w; y2++)
                memcpy(dst + w + y2*ref_stride, src + w + y2*ref_stride, x+block_w - w);
        }

        assert(block_w== 8 || block_w==16);
        distortion += s->dsp.me_cmp[block_w==8](&s->m, src + x + y*ref_stride, dst + x + y*ref_stride, ref_stride, block_w);
    }

    if(plane_index==0){
        BlockNode *b= &s->block[mb_x+mb_y*b_stride];
        int merged= same_block(b,b+1) && same_block(b,b+b_stride) && same_block(b,b+b_stride+1);

/* ..RRRr
 * .RXXx.
 * .RXXx.
 * rxxx.
 */
        if(merged)
            rate = get_block_bits(s, mb_x, mb_y, 2);
        for(i=merged?4:0; i<9; i++){
            static const int dxy[9][2] = {{0,0},{1,0},{0,1},{1,1},{2,0},{2,1},{-1,2},{0,2},{1,2}};
            rate += get_block_bits(s, mb_x + dxy[i][0], mb_y + dxy[i][1], 1);
        }
    }
    return distortion + rate*penalty_factor;
}

static av_always_inline int check_block(SnowContext *s, int mb_x, int mb_y, int p[3], int intra, const uint8_t *obmc_edged, int *best_rd){
    const int b_stride= s->b_width << s->block_max_depth;
    BlockNode *block= &s->block[mb_x + mb_y * b_stride];
    BlockNode backup= *block;
    int rd, index, value;

    assert(mb_x>=0 && mb_y>=0);
    assert(mb_x<b_stride);

    if(intra){
        block->color[0] = p[0];
        block->color[1] = p[1];
        block->color[2] = p[2];
        block->type |= BLOCK_INTRA;
    }else{
        index= (p[0] + 31*p[1]) & (ME_CACHE_SIZE-1);
        value= s->me_cache_generation + (p[0]>>10) + (p[1]<<6) + (block->ref<<12);
        if(s->me_cache[index] == value)
            return 0;
        s->me_cache[index]= value;

        block->mx= p[0];
        block->my= p[1];
        block->type &= ~BLOCK_INTRA;
    }

    rd= get_block_rd(s, mb_x, mb_y, 0, obmc_edged);

//FIXME chroma
    if(rd < *best_rd){
        *best_rd= rd;
        return 1;
    }else{
        *block= backup;
        return 0;
    }
}

/* special case for int[2] args we discard afterwards,
 * fixes compilation problem with gcc 2.95 */
static av_always_inline int check_block_inter(SnowContext *s, int mb_x, int mb_y, int p0, int p1, const uint8_t *obmc_edged, int *best_rd){
    int p[2] = {p0, p1};
    return check_block(s, mb_x, mb_y, p, 0, obmc_edged, best_rd);
}

static av_always_inline int check_4block_inter(SnowContext *s, int mb_x, int mb_y, int p0, int p1, int ref, int *best_rd){
    const int b_stride= s->b_width << s->block_max_depth;
    BlockNode *block= &s->block[mb_x + mb_y * b_stride];
    BlockNode backup[4]= {block[0], block[1], block[b_stride], block[b_stride+1]};
    int rd, index, value;

    assert(mb_x>=0 && mb_y>=0);
    assert(mb_x<b_stride);
    assert(((mb_x|mb_y)&1) == 0);

    index= (p0 + 31*p1) & (ME_CACHE_SIZE-1);
    value= s->me_cache_generation + (p0>>10) + (p1<<6) + (block->ref<<12);
    if(s->me_cache[index] == value)
        return 0;
    s->me_cache[index]= value;

    block->mx= p0;
    block->my= p1;
    block->ref= ref;
    block->type &= ~BLOCK_INTRA;
    block[1]= block[b_stride]= block[b_stride+1]= *block;

    rd= get_4block_rd(s, mb_x, mb_y, 0);

//FIXME chroma
    if(rd < *best_rd){
        *best_rd= rd;
        return 1;
    }else{
        block[0]= backup[0];
        block[1]= backup[1];
        block[b_stride]= backup[2];
        block[b_stride+1]= backup[3];
        return 0;
    }
}

static void iterative_me(SnowContext *s){
    int pass, mb_x, mb_y;
    const int b_width = s->b_width  << s->block_max_depth;
    const int b_height= s->b_height << s->block_max_depth;
    const int b_stride= b_width;
    int color[3];

    {
        RangeCoder r = s->c;
        uint8_t state[sizeof(s->block_state)];
        memcpy(state, s->block_state, sizeof(s->block_state));
        for(mb_y= 0; mb_y<s->b_height; mb_y++)
            for(mb_x= 0; mb_x<s->b_width; mb_x++)
                encode_q_branch(s, 0, mb_x, mb_y);
        s->c = r;
        memcpy(s->block_state, state, sizeof(s->block_state));
    }

    for(pass=0; pass<25; pass++){
        int change= 0;

        for(mb_y= 0; mb_y<b_height; mb_y++){
            for(mb_x= 0; mb_x<b_width; mb_x++){
                int dia_change, i, j, ref;
                int best_rd= INT_MAX, ref_rd;
                BlockNode backup, ref_b;
                const int index= mb_x + mb_y * b_stride;
                BlockNode *block= &s->block[index];
                BlockNode *tb =                   mb_y            ? &s->block[index-b_stride  ] : NULL;
                BlockNode *lb = mb_x                              ? &s->block[index         -1] : NULL;
                BlockNode *rb = mb_x+1<b_width                    ? &s->block[index         +1] : NULL;
                BlockNode *bb =                   mb_y+1<b_height ? &s->block[index+b_stride  ] : NULL;
                BlockNode *tlb= mb_x           && mb_y            ? &s->block[index-b_stride-1] : NULL;
                BlockNode *trb= mb_x+1<b_width && mb_y            ? &s->block[index-b_stride+1] : NULL;
                BlockNode *blb= mb_x           && mb_y+1<b_height ? &s->block[index+b_stride-1] : NULL;
                BlockNode *brb= mb_x+1<b_width && mb_y+1<b_height ? &s->block[index+b_stride+1] : NULL;
                const int b_w= (MB_SIZE >> s->block_max_depth);
                uint8_t obmc_edged[b_w*2][b_w*2];

                if(pass && (block->type & BLOCK_OPT))
                    continue;
                block->type |= BLOCK_OPT;

                backup= *block;

                if(!s->me_cache_generation)
                    memset(s->me_cache, 0, sizeof(s->me_cache));
                s->me_cache_generation += 1<<22;

                //FIXME precalculate
                {
                    int x, y;
                    memcpy(obmc_edged, obmc_tab[s->block_max_depth], b_w*b_w*4);
                    if(mb_x==0)
                        for(y=0; y<b_w*2; y++)
                            memset(obmc_edged[y], obmc_edged[y][0] + obmc_edged[y][b_w-1], b_w);
                    if(mb_x==b_stride-1)
                        for(y=0; y<b_w*2; y++)
                            memset(obmc_edged[y]+b_w, obmc_edged[y][b_w] + obmc_edged[y][b_w*2-1], b_w);
                    if(mb_y==0){
                        for(x=0; x<b_w*2; x++)
                            obmc_edged[0][x] += obmc_edged[b_w-1][x];
                        for(y=1; y<b_w; y++)
                            memcpy(obmc_edged[y], obmc_edged[0], b_w*2);
                    }
                    if(mb_y==b_height-1){
                        for(x=0; x<b_w*2; x++)
                            obmc_edged[b_w*2-1][x] += obmc_edged[b_w][x];
                        for(y=b_w; y<b_w*2-1; y++)
                            memcpy(obmc_edged[y], obmc_edged[b_w*2-1], b_w*2);
                    }
                }

                //skip stuff outside the picture
                if(mb_x==0 || mb_y==0 || mb_x==b_width-1 || mb_y==b_height-1){
                    uint8_t *src= s->  input_picture.data[0];
                    uint8_t *dst= s->current_picture.data[0];
                    const int stride= s->current_picture.linesize[0];
                    const int block_w= MB_SIZE >> s->block_max_depth;
                    const int sx= block_w*mb_x - block_w/2;
                    const int sy= block_w*mb_y - block_w/2;
                    const int w= s->plane[0].width;
                    const int h= s->plane[0].height;
                    int y;

                    for(y=sy; y<0; y++)
                        memcpy(dst + sx + y*stride, src + sx + y*stride, block_w*2);
                    for(y=h; y<sy+block_w*2; y++)
                        memcpy(dst + sx + y*stride, src + sx + y*stride, block_w*2);
                    if(sx<0){
                        for(y=sy; y<sy+block_w*2; y++)
                            memcpy(dst + sx + y*stride, src + sx + y*stride, -sx);
                    }
                    if(sx+block_w*2 > w){
                        for(y=sy; y<sy+block_w*2; y++)
                            memcpy(dst + w + y*stride, src + w + y*stride, sx+block_w*2 - w);
                    }
                }

                // intra(black) = neighbors' contribution to the current block
                for(i=0; i<3; i++)
                    color[i]= get_dc(s, mb_x, mb_y, i);

                // get previous score (cannot be cached due to OBMC)
                if(pass > 0 && (block->type&BLOCK_INTRA)){
                    int color0[3]= {block->color[0], block->color[1], block->color[2]};
                    check_block(s, mb_x, mb_y, color0, 1, *obmc_edged, &best_rd);
                }else
                    check_block_inter(s, mb_x, mb_y, block->mx, block->my, *obmc_edged, &best_rd);

                ref_b= *block;
                ref_rd= best_rd;
                for(ref=0; ref < s->ref_frames; ref++){
                    int16_t (*mvr)[2]= &s->ref_mvs[ref][index];
                    if(s->ref_scores[ref][index] > s->ref_scores[ref_b.ref][index]*3/2) //FIXME tune threshold
                        continue;
                    block->ref= ref;
                    best_rd= INT_MAX;

                    check_block_inter(s, mb_x, mb_y, mvr[0][0], mvr[0][1], *obmc_edged, &best_rd);
                    check_block_inter(s, mb_x, mb_y, 0, 0, *obmc_edged, &best_rd);
                    if(tb)
                        check_block_inter(s, mb_x, mb_y, mvr[-b_stride][0], mvr[-b_stride][1], *obmc_edged, &best_rd);
                    if(lb)
                        check_block_inter(s, mb_x, mb_y, mvr[-1][0], mvr[-1][1], *obmc_edged, &best_rd);
                    if(rb)
                        check_block_inter(s, mb_x, mb_y, mvr[1][0], mvr[1][1], *obmc_edged, &best_rd);
                    if(bb)
                        check_block_inter(s, mb_x, mb_y, mvr[b_stride][0], mvr[b_stride][1], *obmc_edged, &best_rd);

                    /* fullpel ME */
                    //FIXME avoid subpel interpolation / round to nearest integer
                    do{
                        dia_change=0;
                        for(i=0; i<FFMAX(s->avctx->dia_size, 1); i++){
                            for(j=0; j<i; j++){
                                dia_change |= check_block_inter(s, mb_x, mb_y, block->mx+4*(i-j), block->my+(4*j), *obmc_edged, &best_rd);
                                dia_change |= check_block_inter(s, mb_x, mb_y, block->mx-4*(i-j), block->my-(4*j), *obmc_edged, &best_rd);
                                dia_change |= check_block_inter(s, mb_x, mb_y, block->mx+4*(i-j), block->my-(4*j), *obmc_edged, &best_rd);
                                dia_change |= check_block_inter(s, mb_x, mb_y, block->mx-4*(i-j), block->my+(4*j), *obmc_edged, &best_rd);
                            }
                        }
                    }while(dia_change);
                    /* subpel ME */
                    do{
                        static const int square[8][2]= {{+1, 0},{-1, 0},{ 0,+1},{ 0,-1},{+1,+1},{-1,-1},{+1,-1},{-1,+1},};
                        dia_change=0;
                        for(i=0; i<8; i++)
                            dia_change |= check_block_inter(s, mb_x, mb_y, block->mx+square[i][0], block->my+square[i][1], *obmc_edged, &best_rd);
                    }while(dia_change);
                    //FIXME or try the standard 2 pass qpel or similar

                    mvr[0][0]= block->mx;
                    mvr[0][1]= block->my;
                    if(ref_rd > best_rd){
                        ref_rd= best_rd;
                        ref_b= *block;
                    }
                }
                best_rd= ref_rd;
                *block= ref_b;
#if 1
                check_block(s, mb_x, mb_y, color, 1, *obmc_edged, &best_rd);
                //FIXME RD style color selection
#endif
                if(!same_block(block, &backup)){
                    if(tb ) tb ->type &= ~BLOCK_OPT;
                    if(lb ) lb ->type &= ~BLOCK_OPT;
                    if(rb ) rb ->type &= ~BLOCK_OPT;
                    if(bb ) bb ->type &= ~BLOCK_OPT;
                    if(tlb) tlb->type &= ~BLOCK_OPT;
                    if(trb) trb->type &= ~BLOCK_OPT;
                    if(blb) blb->type &= ~BLOCK_OPT;
                    if(brb) brb->type &= ~BLOCK_OPT;
                    change ++;
                }
            }
        }
        av_log(NULL, AV_LOG_ERROR, "pass:%d changed:%d\n", pass, change);
        if(!change)
            break;
    }

    if(s->block_max_depth == 1){
        int change= 0;
        for(mb_y= 0; mb_y<b_height; mb_y+=2){
            for(mb_x= 0; mb_x<b_width; mb_x+=2){
                int i;
                int best_rd, init_rd;
                const int index= mb_x + mb_y * b_stride;
                BlockNode *b[4];

                b[0]= &s->block[index];
                b[1]= b[0]+1;
                b[2]= b[0]+b_stride;
                b[3]= b[2]+1;
                if(same_block(b[0], b[1]) &&
                   same_block(b[0], b[2]) &&
                   same_block(b[0], b[3]))
                    continue;

                if(!s->me_cache_generation)
                    memset(s->me_cache, 0, sizeof(s->me_cache));
                s->me_cache_generation += 1<<22;

                init_rd= best_rd= get_4block_rd(s, mb_x, mb_y, 0);

                //FIXME more multiref search?
                check_4block_inter(s, mb_x, mb_y,
                                   (b[0]->mx + b[1]->mx + b[2]->mx + b[3]->mx + 2) >> 2,
                                   (b[0]->my + b[1]->my + b[2]->my + b[3]->my + 2) >> 2, 0, &best_rd);

                for(i=0; i<4; i++)
                    if(!(b[i]->type&BLOCK_INTRA))
                        check_4block_inter(s, mb_x, mb_y, b[i]->mx, b[i]->my, b[i]->ref, &best_rd);

                if(init_rd != best_rd)
                    change++;
            }
        }
        av_log(NULL, AV_LOG_ERROR, "pass:4mv changed:%d\n", change*4);
    }
}

static void quantize(SnowContext *s, SubBand *b, IDWTELEM *dst, DWTELEM *src, int stride, int bias){
    const int w= b->width;
    const int h= b->height;
    const int qlog= av_clip(s->qlog + b->qlog, 0, QROOT*16);
    const int qmul= qexp[qlog&(QROOT-1)]<<((qlog>>QSHIFT) + ENCODER_EXTRA_BITS);
    int x,y, thres1, thres2;

    if(s->qlog == LOSSLESS_QLOG){
        for(y=0; y<h; y++)
            for(x=0; x<w; x++)
                dst[x + y*stride]= src[x + y*stride];
        return;
    }

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
                        dst[x + y*stride]=  i;
                    }else{
                        i= -i;
                        i<<= QEXPSHIFT;
                        i/= qmul; //FIXME optimize
                        dst[x + y*stride]= -i;
                    }
                }else
                    dst[x + y*stride]= 0;
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
                        dst[x + y*stride]=  i;
                    }else{
                        i= -i;
                        i<<= QEXPSHIFT;
                        i= (i + bias) / qmul; //FIXME optimize
                        dst[x + y*stride]= -i;
                    }
                }else
                    dst[x + y*stride]= 0;
            }
        }
    }
}

static void dequantize_slice_buffered(SnowContext *s, slice_buffer * sb, SubBand *b, IDWTELEM *src, int stride, int start_y, int end_y){
    const int w= b->width;
    const int qlog= av_clip(s->qlog + b->qlog, 0, QROOT*16);
    const int qmul= qexp[qlog&(QROOT-1)]<<(qlog>>QSHIFT);
    const int qadd= (s->qbias*qmul)>>QBIAS_SHIFT;
    int x,y;

    if(s->qlog == LOSSLESS_QLOG) return;

    for(y=start_y; y<end_y; y++){
//        DWTELEM * line = slice_buffer_get_line_from_address(sb, src + (y * stride));
        IDWTELEM * line = slice_buffer_get_line(sb, (y * b->stride_line) + b->buf_y_offset) + b->buf_x_offset;
        for(x=0; x<w; x++){
            int i= line[x];
            if(i<0){
                line[x]= -((-i*qmul + qadd)>>(QEXPSHIFT)); //FIXME try different bias
            }else if(i>0){
                line[x]=  (( i*qmul + qadd)>>(QEXPSHIFT));
            }
        }
    }
}

static void dequantize(SnowContext *s, SubBand *b, IDWTELEM *src, int stride){
    const int w= b->width;
    const int h= b->height;
    const int qlog= av_clip(s->qlog + b->qlog, 0, QROOT*16);
    const int qmul= qexp[qlog&(QROOT-1)]<<(qlog>>QSHIFT);
    const int qadd= (s->qbias*qmul)>>QBIAS_SHIFT;
    int x,y;

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
}

static void decorrelate(SnowContext *s, SubBand *b, IDWTELEM *src, int stride, int inverse, int use_median){
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

static void correlate_slice_buffered(SnowContext *s, slice_buffer * sb, SubBand *b, IDWTELEM *src, int stride, int inverse, int use_median, int start_y, int end_y){
    const int w= b->width;
    int x,y;

    IDWTELEM * line=0; // silence silly "could be used without having been initialized" warning
    IDWTELEM * prev;

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
}

static void correlate(SnowContext *s, SubBand *b, IDWTELEM *src, int stride, int inverse, int use_median){
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

static void encode_qlogs(SnowContext *s){
    int plane_index, level, orientation;

    for(plane_index=0; plane_index<2; plane_index++){
        for(level=0; level<s->spatial_decomposition_count; level++){
            for(orientation=level ? 1:0; orientation<4; orientation++){
                if(orientation==2) continue;
                put_symbol(&s->c, s->header_state, s->plane[plane_index].band[level][orientation].qlog, 1);
            }
        }
    }
}

static void encode_header(SnowContext *s){
    int plane_index, i;
    uint8_t kstate[32];

    memset(kstate, MID_STATE, sizeof(kstate));

    put_rac(&s->c, kstate, s->keyframe);
    if(s->keyframe || s->always_reset){
        reset_contexts(s);
        s->last_spatial_decomposition_type=
        s->last_qlog=
        s->last_qbias=
        s->last_mv_scale=
        s->last_block_max_depth= 0;
        for(plane_index=0; plane_index<2; plane_index++){
            Plane *p= &s->plane[plane_index];
            p->last_htaps=0;
            p->last_diag_mc=0;
            memset(p->last_hcoeff, 0, sizeof(p->last_hcoeff));
        }
    }
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
        put_symbol(&s->c, s->header_state, s->max_ref_frames-1, 0);

        encode_qlogs(s);
    }

    if(!s->keyframe){
        int update_mc=0;
        for(plane_index=0; plane_index<2; plane_index++){
            Plane *p= &s->plane[plane_index];
            update_mc |= p->last_htaps   != p->htaps;
            update_mc |= p->last_diag_mc != p->diag_mc;
            update_mc |= !!memcmp(p->last_hcoeff, p->hcoeff, sizeof(p->hcoeff));
        }
        put_rac(&s->c, s->header_state, update_mc);
        if(update_mc){
            for(plane_index=0; plane_index<2; plane_index++){
                Plane *p= &s->plane[plane_index];
                put_rac(&s->c, s->header_state, p->diag_mc);
                put_symbol(&s->c, s->header_state, p->htaps/2-1, 0);
                for(i= p->htaps/2; i; i--)
                    put_symbol(&s->c, s->header_state, FFABS(p->hcoeff[i]), 0);
            }
        }
        if(s->last_spatial_decomposition_count != s->spatial_decomposition_count){
            put_rac(&s->c, s->header_state, 1);
            put_symbol(&s->c, s->header_state, s->spatial_decomposition_count, 0);
            encode_qlogs(s);
        }else
            put_rac(&s->c, s->header_state, 0);
    }

    put_symbol(&s->c, s->header_state, s->spatial_decomposition_type - s->last_spatial_decomposition_type, 1);
    put_symbol(&s->c, s->header_state, s->qlog            - s->last_qlog    , 1);
    put_symbol(&s->c, s->header_state, s->mv_scale        - s->last_mv_scale, 1);
    put_symbol(&s->c, s->header_state, s->qbias           - s->last_qbias   , 1);
    put_symbol(&s->c, s->header_state, s->block_max_depth - s->last_block_max_depth, 1);

}

static void update_last_header_values(SnowContext *s){
    int plane_index;

    if(!s->keyframe){
        for(plane_index=0; plane_index<2; plane_index++){
            Plane *p= &s->plane[plane_index];
            p->last_diag_mc= p->diag_mc;
            p->last_htaps  = p->htaps;
            memcpy(p->last_hcoeff, p->hcoeff, sizeof(p->hcoeff));
        }
    }

    s->last_spatial_decomposition_type  = s->spatial_decomposition_type;
    s->last_qlog                        = s->qlog;
    s->last_qbias                       = s->qbias;
    s->last_mv_scale                    = s->mv_scale;
    s->last_block_max_depth             = s->block_max_depth;
    s->last_spatial_decomposition_count = s->spatial_decomposition_count;
}

static void decode_qlogs(SnowContext *s){
    int plane_index, level, orientation;

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

static int decode_header(SnowContext *s){
    int plane_index;
    uint8_t kstate[32];

    memset(kstate, MID_STATE, sizeof(kstate));

    s->keyframe= get_rac(&s->c, kstate);
    if(s->keyframe || s->always_reset){
        reset_contexts(s);
        s->spatial_decomposition_type=
        s->qlog=
        s->qbias=
        s->mv_scale=
        s->block_max_depth= 0;
    }
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
        s->max_ref_frames= get_symbol(&s->c, s->header_state, 0)+1;

        decode_qlogs(s);
    }

    if(!s->keyframe){
        if(get_rac(&s->c, s->header_state)){
            for(plane_index=0; plane_index<2; plane_index++){
                int htaps, i, sum=0;
                Plane *p= &s->plane[plane_index];
                p->diag_mc= get_rac(&s->c, s->header_state);
                htaps= get_symbol(&s->c, s->header_state, 0)*2 + 2;
                if((unsigned)htaps > HTAPS_MAX || htaps==0)
                    return -1;
                p->htaps= htaps;
                for(i= htaps/2; i; i--){
                    p->hcoeff[i]= get_symbol(&s->c, s->header_state, 0) * (1-2*(i&1));
                    sum += p->hcoeff[i];
                }
                p->hcoeff[0]= 32-sum;
            }
            s->plane[2].diag_mc= s->plane[1].diag_mc;
            s->plane[2].htaps  = s->plane[1].htaps;
            memcpy(s->plane[2].hcoeff, s->plane[1].hcoeff, sizeof(s->plane[1].hcoeff));
        }
        if(get_rac(&s->c, s->header_state)){
            s->spatial_decomposition_count= get_symbol(&s->c, s->header_state, 0);
            decode_qlogs(s);
        }
    }

    s->spatial_decomposition_type+= get_symbol(&s->c, s->header_state, 1);
    if(s->spatial_decomposition_type > 1){
        av_log(s->avctx, AV_LOG_ERROR, "spatial_decomposition_type %d not supported", s->spatial_decomposition_type);
        return -1;
    }

    s->qlog           += get_symbol(&s->c, s->header_state, 1);
    s->mv_scale       += get_symbol(&s->c, s->header_state, 1);
    s->qbias          += get_symbol(&s->c, s->header_state, 1);
    s->block_max_depth+= get_symbol(&s->c, s->header_state, 1);
    if(s->block_max_depth > 1 || s->block_max_depth < 0){
        av_log(s->avctx, AV_LOG_ERROR, "block_max_depth= %d is too large", s->block_max_depth);
        s->block_max_depth= 0;
        return -1;
    }

    return 0;
}

static void init_qexp(void){
    int i;
    double v=128;

    for(i=0; i<QROOT; i++){
        qexp[i]= lrintf(v);
        v *= pow(2, 1.0 / QROOT);
    }
}

static av_cold int common_init(AVCodecContext *avctx){
    SnowContext *s = avctx->priv_data;
    int width, height;
    int i, j;

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

//    dec += FFMAX(s->chroma_h_shift, s->chroma_v_shift);

    width= s->avctx->width;
    height= s->avctx->height;

    s->spatial_idwt_buffer= av_mallocz(width*height*sizeof(IDWTELEM));
    s->spatial_dwt_buffer= av_mallocz(width*height*sizeof(DWTELEM)); //FIXME this does not belong here

    for(i=0; i<MAX_REF_FRAMES; i++)
        for(j=0; j<MAX_REF_FRAMES; j++)
            scale_mv_ref[i][j] = 256*(i+1)/(j+1);

    s->avctx->get_buffer(s->avctx, &s->mconly_picture);

    return 0;
}

static int common_init_after_header(AVCodecContext *avctx){
    SnowContext *s = avctx->priv_data;
    int plane_index, level, orientation;

    for(plane_index=0; plane_index<3; plane_index++){
        int w= s->avctx->width;
        int h= s->avctx->height;

        if(plane_index){
            w>>= s->chroma_h_shift;
            h>>= s->chroma_v_shift;
        }
        s->plane[plane_index].width = w;
        s->plane[plane_index].height= h;

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
                b->ibuf= s->spatial_idwt_buffer + (b->buf - s->spatial_dwt_buffer);

                if(level)
                    b->parent= &s->plane[plane_index].band[level-1][orientation];
                //FIXME avoid this realloc
                av_freep(&b->x_coeff);
                b->x_coeff=av_mallocz(((b->width+1) * b->height+1)*sizeof(x_and_coeff));
            }
            w= (w+1)>>1;
            h= (h+1)>>1;
        }
    }

    return 0;
}

static int qscale2qlog(int qscale){
    return rint(QROOT*log(qscale / (float)FF_QP2LAMBDA)/log(2))
           + 61*QROOT/8; //<64 >60
}

static int ratecontrol_1pass(SnowContext *s, AVFrame *pict)
{
    /* Estimate the frame's complexity as a sum of weighted dwt coefficients.
     * FIXME we know exact mv bits at this point,
     * but ratecontrol isn't set up to include them. */
    uint32_t coef_sum= 0;
    int level, orientation, delta_qlog;

    for(level=0; level<s->spatial_decomposition_count; level++){
        for(orientation=level ? 1 : 0; orientation<4; orientation++){
            SubBand *b= &s->plane[0].band[level][orientation];
            IDWTELEM *buf= b->ibuf;
            const int w= b->width;
            const int h= b->height;
            const int stride= b->stride;
            const int qlog= av_clip(2*QROOT + b->qlog, 0, QROOT*16);
            const int qmul= qexp[qlog&(QROOT-1)]<<(qlog>>QSHIFT);
            const int qdiv= (1<<16)/qmul;
            int x, y;
            //FIXME this is ugly
            for(y=0; y<h; y++)
                for(x=0; x<w; x++)
                    buf[x+y*stride]= b->buf[x+y*stride];
            if(orientation==0)
                decorrelate(s, b, buf, stride, 1, 0);
            for(y=0; y<h; y++)
                for(x=0; x<w; x++)
                    coef_sum+= abs(buf[x+y*stride]) * qdiv >> 16;
        }
    }

    /* ugly, ratecontrol just takes a sqrt again */
    coef_sum = (uint64_t)coef_sum * coef_sum >> 16;
    assert(coef_sum < INT_MAX);

    if(pict->pict_type == FF_I_TYPE){
        s->m.current_picture.mb_var_sum= coef_sum;
        s->m.current_picture.mc_mb_var_sum= 0;
    }else{
        s->m.current_picture.mc_mb_var_sum= coef_sum;
        s->m.current_picture.mb_var_sum= 0;
    }

    pict->quality= ff_rate_estimate_qscale(&s->m, 1);
    if (pict->quality < 0)
        return INT_MIN;
    s->lambda= pict->quality * 3/2;
    delta_qlog= qscale2qlog(pict->quality) - s->qlog;
    s->qlog+= delta_qlog;
    return delta_qlog;
}

static void calculate_visual_weight(SnowContext *s, Plane *p){
    int width = p->width;
    int height= p->height;
    int level, orientation, x, y;

    for(level=0; level<s->spatial_decomposition_count; level++){
        for(orientation=level ? 1 : 0; orientation<4; orientation++){
            SubBand *b= &p->band[level][orientation];
            IDWTELEM *ibuf= b->ibuf;
            int64_t error=0;

            memset(s->spatial_idwt_buffer, 0, sizeof(*s->spatial_idwt_buffer)*width*height);
            ibuf[b->width/2 + b->height/2*b->stride]= 256*16;
            ff_spatial_idwt(s->spatial_idwt_buffer, width, height, width, s->spatial_decomposition_type, s->spatial_decomposition_count);
            for(y=0; y<height; y++){
                for(x=0; x<width; x++){
                    int64_t d= s->spatial_idwt_buffer[x + y*width]*16;
                    error += d*d;
                }
            }

            b->qlog= (int)(log(352256.0/sqrt(error)) / log(pow(2.0, 1.0/QROOT))+0.5);
        }
    }
}

#define QUANTIZE2 0

#if QUANTIZE2==1
#define Q2_STEP 8

static void find_sse(SnowContext *s, Plane *p, int *score, int score_stride, IDWTELEM *r0, IDWTELEM *r1, int level, int orientation){
    SubBand *b= &p->band[level][orientation];
    int x, y;
    int xo=0;
    int yo=0;
    int step= 1 << (s->spatial_decomposition_count - level);

    if(orientation&1)
        xo= step>>1;
    if(orientation&2)
        yo= step>>1;

    //FIXME bias for nonzero ?
    //FIXME optimize
    memset(score, 0, sizeof(*score)*score_stride*((p->height + Q2_STEP-1)/Q2_STEP));
    for(y=0; y<p->height; y++){
        for(x=0; x<p->width; x++){
            int sx= (x-xo + step/2) / step / Q2_STEP;
            int sy= (y-yo + step/2) / step / Q2_STEP;
            int v= r0[x + y*p->width] - r1[x + y*p->width];
            assert(sx>=0 && sy>=0 && sx < score_stride);
            v= ((v+8)>>4)<<4;
            score[sx + sy*score_stride] += v*v;
            assert(score[sx + sy*score_stride] >= 0);
        }
    }
}

static void dequantize_all(SnowContext *s, Plane *p, IDWTELEM *buffer, int width, int height){
    int level, orientation;

    for(level=0; level<s->spatial_decomposition_count; level++){
        for(orientation=level ? 1 : 0; orientation<4; orientation++){
            SubBand *b= &p->band[level][orientation];
            IDWTELEM *dst= buffer + (b->ibuf - s->spatial_idwt_buffer);

            dequantize(s, b, dst, b->stride);
        }
    }
}

static void dwt_quantize(SnowContext *s, Plane *p, DWTELEM *buffer, int width, int height, int stride, int type){
    int level, orientation, ys, xs, x, y, pass;
    IDWTELEM best_dequant[height * stride];
    IDWTELEM idwt2_buffer[height * stride];
    const int score_stride= (width + 10)/Q2_STEP;
    int best_score[(width + 10)/Q2_STEP * (height + 10)/Q2_STEP]; //FIXME size
    int score[(width + 10)/Q2_STEP * (height + 10)/Q2_STEP]; //FIXME size
    int threshold= (s->m.lambda * s->m.lambda) >> 6;

    //FIXME pass the copy cleanly ?

//    memcpy(dwt_buffer, buffer, height * stride * sizeof(DWTELEM));
    ff_spatial_dwt(buffer, width, height, stride, type, s->spatial_decomposition_count);

    for(level=0; level<s->spatial_decomposition_count; level++){
        for(orientation=level ? 1 : 0; orientation<4; orientation++){
            SubBand *b= &p->band[level][orientation];
            IDWTELEM *dst= best_dequant + (b->ibuf - s->spatial_idwt_buffer);
             DWTELEM *src=       buffer + (b-> buf - s->spatial_dwt_buffer);
            assert(src == b->buf); // code does not depend on this but it is true currently

            quantize(s, b, dst, src, b->stride, s->qbias);
        }
    }
    for(pass=0; pass<1; pass++){
        if(s->qbias == 0) //keyframe
            continue;
        for(level=0; level<s->spatial_decomposition_count; level++){
            for(orientation=level ? 1 : 0; orientation<4; orientation++){
                SubBand *b= &p->band[level][orientation];
                IDWTELEM *dst= idwt2_buffer + (b->ibuf - s->spatial_idwt_buffer);
                IDWTELEM *best_dst= best_dequant + (b->ibuf - s->spatial_idwt_buffer);

                for(ys= 0; ys<Q2_STEP; ys++){
                    for(xs= 0; xs<Q2_STEP; xs++){
                        memcpy(idwt2_buffer, best_dequant, height * stride * sizeof(IDWTELEM));
                        dequantize_all(s, p, idwt2_buffer, width, height);
                        ff_spatial_idwt(idwt2_buffer, width, height, stride, type, s->spatial_decomposition_count);
                        find_sse(s, p, best_score, score_stride, idwt2_buffer, s->spatial_idwt_buffer, level, orientation);
                        memcpy(idwt2_buffer, best_dequant, height * stride * sizeof(IDWTELEM));
                        for(y=ys; y<b->height; y+= Q2_STEP){
                            for(x=xs; x<b->width; x+= Q2_STEP){
                                if(dst[x + y*b->stride]<0) dst[x + y*b->stride]++;
                                if(dst[x + y*b->stride]>0) dst[x + y*b->stride]--;
                                //FIXME try more than just --
                            }
                        }
                        dequantize_all(s, p, idwt2_buffer, width, height);
                        ff_spatial_idwt(idwt2_buffer, width, height, stride, type, s->spatial_decomposition_count);
                        find_sse(s, p, score, score_stride, idwt2_buffer, s->spatial_idwt_buffer, level, orientation);
                        for(y=ys; y<b->height; y+= Q2_STEP){
                            for(x=xs; x<b->width; x+= Q2_STEP){
                                int score_idx= x/Q2_STEP + (y/Q2_STEP)*score_stride;
                                if(score[score_idx] <= best_score[score_idx] + threshold){
                                    best_score[score_idx]= score[score_idx];
                                    if(best_dst[x + y*b->stride]<0) best_dst[x + y*b->stride]++;
                                    if(best_dst[x + y*b->stride]>0) best_dst[x + y*b->stride]--;
                                    //FIXME copy instead
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    memcpy(s->spatial_idwt_buffer, best_dequant, height * stride * sizeof(IDWTELEM)); //FIXME work with that directly instead of copy at the end
}

#endif /* QUANTIZE2==1 */

static av_cold int encode_init(AVCodecContext *avctx)
{
    SnowContext *s = avctx->priv_data;
    int plane_index;

    if(avctx->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL){
        av_log(avctx, AV_LOG_ERROR, "This codec is under development, files encoded with it may not be decodable with future versions!!!\n"
               "Use vstrict=-2 / -strict -2 to use it anyway.\n");
        return -1;
    }

    if(avctx->prediction_method == DWT_97
       && (avctx->flags & CODEC_FLAG_QSCALE)
       && avctx->global_quality == 0){
        av_log(avctx, AV_LOG_ERROR, "The 9/7 wavelet is incompatible with lossless mode.\n");
        return -1;
    }

    s->spatial_decomposition_type= avctx->prediction_method; //FIXME add decorrelator type r transform_type

    s->chroma_h_shift= 1; //FIXME XXX
    s->chroma_v_shift= 1;

    s->mv_scale       = (avctx->flags & CODEC_FLAG_QPEL) ? 2 : 4;
    s->block_max_depth= (avctx->flags & CODEC_FLAG_4MV ) ? 1 : 0;

    for(plane_index=0; plane_index<3; plane_index++){
        s->plane[plane_index].diag_mc= 1;
        s->plane[plane_index].htaps= 6;
        s->plane[plane_index].hcoeff[0]=  40;
        s->plane[plane_index].hcoeff[1]= -10;
        s->plane[plane_index].hcoeff[2]=   2;
        s->plane[plane_index].fast_mc= 1;
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
    s->m.obmc_scratchpad= av_mallocz(MB_SIZE*MB_SIZE*12*sizeof(uint32_t));
    h263_encode_init(&s->m); //mv_penalty

    s->max_ref_frames = FFMAX(FFMIN(avctx->refs, MAX_REF_FRAMES), 1);

    if(avctx->flags&CODEC_FLAG_PASS1){
        if(!avctx->stats_out)
            avctx->stats_out = av_mallocz(256);
    }
    if((avctx->flags&CODEC_FLAG_PASS2) || !(avctx->flags&CODEC_FLAG_QSCALE)){
        if(ff_rate_control_init(&s->m) < 0)
            return -1;
    }
    s->pass1_rc= !(avctx->flags & (CODEC_FLAG_QSCALE|CODEC_FLAG_PASS2));

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
/*    case PIX_FMT_RGB32:
        s->colorspace= 1;
        break;*/
    default:
        av_log(avctx, AV_LOG_ERROR, "pixel format not supported\n");
        return -1;
    }
//    avcodec_get_chroma_sub_sample(avctx->pix_fmt, &s->chroma_h_shift, &s->chroma_v_shift);
    s->chroma_h_shift= 1;
    s->chroma_v_shift= 1;

    ff_set_cmp(&s->dsp, s->dsp.me_cmp, s->avctx->me_cmp);
    ff_set_cmp(&s->dsp, s->dsp.me_sub_cmp, s->avctx->me_sub_cmp);

    s->avctx->get_buffer(s->avctx, &s->input_picture);

    if(s->avctx->me_method == ME_ITER){
        int i;
        int size= s->b_width * s->b_height << 2*s->block_max_depth;
        for(i=0; i<s->max_ref_frames; i++){
            s->ref_mvs[i]= av_mallocz(size*sizeof(int16_t[2]));
            s->ref_scores[i]= av_mallocz(size*sizeof(uint32_t));
        }
    }

    return 0;
}

#define USE_HALFPEL_PLANE 0

static void halfpel_interpol(SnowContext *s, uint8_t *halfpel[4][4], AVFrame *frame){
    int p,x,y;

    assert(!(s->avctx->flags & CODEC_FLAG_EMU_EDGE));

    for(p=0; p<3; p++){
        int is_chroma= !!p;
        int w= s->avctx->width  >>is_chroma;
        int h= s->avctx->height >>is_chroma;
        int ls= frame->linesize[p];
        uint8_t *src= frame->data[p];

        halfpel[1][p]= (uint8_t*)av_malloc(ls * (h+2*EDGE_WIDTH)) + EDGE_WIDTH*(1+ls);
        halfpel[2][p]= (uint8_t*)av_malloc(ls * (h+2*EDGE_WIDTH)) + EDGE_WIDTH*(1+ls);
        halfpel[3][p]= (uint8_t*)av_malloc(ls * (h+2*EDGE_WIDTH)) + EDGE_WIDTH*(1+ls);

        halfpel[0][p]= src;
        for(y=0; y<h; y++){
            for(x=0; x<w; x++){
                int i= y*ls + x;

                halfpel[1][p][i]= (20*(src[i] + src[i+1]) - 5*(src[i-1] + src[i+2]) + (src[i-2] + src[i+3]) + 16 )>>5;
            }
        }
        for(y=0; y<h; y++){
            for(x=0; x<w; x++){
                int i= y*ls + x;

                halfpel[2][p][i]= (20*(src[i] + src[i+ls]) - 5*(src[i-ls] + src[i+2*ls]) + (src[i-2*ls] + src[i+3*ls]) + 16 )>>5;
            }
        }
        src= halfpel[1][p];
        for(y=0; y<h; y++){
            for(x=0; x<w; x++){
                int i= y*ls + x;

                halfpel[3][p][i]= (20*(src[i] + src[i+ls]) - 5*(src[i-ls] + src[i+2*ls]) + (src[i-2*ls] + src[i+3*ls]) + 16 )>>5;
            }
        }

//FIXME border!
    }
}

static int frame_start(SnowContext *s){
   AVFrame tmp;
   int w= s->avctx->width; //FIXME round up to x16 ?
   int h= s->avctx->height;

    if(s->current_picture.data[0]){
        s->dsp.draw_edges(s->current_picture.data[0], s->current_picture.linesize[0], w   , h   , EDGE_WIDTH  );
        s->dsp.draw_edges(s->current_picture.data[1], s->current_picture.linesize[1], w>>1, h>>1, EDGE_WIDTH/2);
        s->dsp.draw_edges(s->current_picture.data[2], s->current_picture.linesize[2], w>>1, h>>1, EDGE_WIDTH/2);
    }

    tmp= s->last_picture[s->max_ref_frames-1];
    memmove(s->last_picture+1, s->last_picture, (s->max_ref_frames-1)*sizeof(AVFrame));
    memmove(s->halfpel_plane+1, s->halfpel_plane, (s->max_ref_frames-1)*sizeof(void*)*4*4);
    if(USE_HALFPEL_PLANE && s->current_picture.data[0])
        halfpel_interpol(s, s->halfpel_plane[0], &s->current_picture);
    s->last_picture[0]= s->current_picture;
    s->current_picture= tmp;

    if(s->keyframe){
        s->ref_frames= 0;
    }else{
        int i;
        for(i=0; i<s->max_ref_frames && s->last_picture[i].data[0]; i++)
            if(i && s->last_picture[i-1].key_frame)
                break;
        s->ref_frames= i;
    }

    s->current_picture.reference= 1;
    if(s->avctx->get_buffer(s->avctx, &s->current_picture) < 0){
        av_log(s->avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    s->current_picture.key_frame= s->keyframe;

    return 0;
}

static int encode_frame(AVCodecContext *avctx, unsigned char *buf, int buf_size, void *data){
    SnowContext *s = avctx->priv_data;
    RangeCoder * const c= &s->c;
    AVFrame *pict = data;
    const int width= s->avctx->width;
    const int height= s->avctx->height;
    int level, orientation, plane_index, i, y;
    uint8_t rc_header_bak[sizeof(s->header_state)];
    uint8_t rc_block_bak[sizeof(s->block_state)];

    ff_init_range_encoder(c, buf, buf_size);
    ff_build_rac_states(c, 0.05*(1LL<<32), 256-8);

    for(i=0; i<3; i++){
        int shift= !!i;
        for(y=0; y<(height>>shift); y++)
            memcpy(&s->input_picture.data[i][y * s->input_picture.linesize[i]],
                   &pict->data[i][y * pict->linesize[i]],
                   width>>shift);
    }
    s->new_picture = *pict;

    s->m.picture_number= avctx->frame_number;
    if(avctx->flags&CODEC_FLAG_PASS2){
        s->m.pict_type =
        pict->pict_type= s->m.rc_context.entry[avctx->frame_number].new_pict_type;
        s->keyframe= pict->pict_type==FF_I_TYPE;
        if(!(avctx->flags&CODEC_FLAG_QSCALE)) {
            pict->quality= ff_rate_estimate_qscale(&s->m, 0);
            if (pict->quality < 0)
                return -1;
        }
    }else{
        s->keyframe= avctx->gop_size==0 || avctx->frame_number % avctx->gop_size == 0;
        s->m.pict_type=
        pict->pict_type= s->keyframe ? FF_I_TYPE : FF_P_TYPE;
    }

    if(s->pass1_rc && avctx->frame_number == 0)
        pict->quality= 2*FF_QP2LAMBDA;
    if(pict->quality){
        s->qlog= qscale2qlog(pict->quality);
        s->lambda = pict->quality * 3/2;
    }
    if(s->qlog < 0 || (!pict->quality && (avctx->flags & CODEC_FLAG_QSCALE))){
        s->qlog= LOSSLESS_QLOG;
        s->lambda = 0;
    }//else keep previous frame's qlog until after motion estimation

    frame_start(s);

    s->m.current_picture_ptr= &s->m.current_picture;
    if(pict->pict_type == FF_P_TYPE){
        int block_width = (width +15)>>4;
        int block_height= (height+15)>>4;
        int stride= s->current_picture.linesize[0];

        assert(s->current_picture.data[0]);
        assert(s->last_picture[0].data[0]);

        s->m.avctx= s->avctx;
        s->m.current_picture.data[0]= s->current_picture.data[0];
        s->m.   last_picture.data[0]= s->last_picture[0].data[0];
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

        s->m.lambda = s->lambda;
        s->m.qscale= (s->m.lambda*139 + FF_LAMBDA_SCALE*64) >> (FF_LAMBDA_SHIFT + 7);
        s->lambda2= s->m.lambda2= (s->m.lambda*s->m.lambda + FF_LAMBDA_SCALE/2) >> FF_LAMBDA_SHIFT;

        s->m.dsp= s->dsp; //move
        ff_init_me(&s->m);
        s->dsp= s->m.dsp;
    }

    if(s->pass1_rc){
        memcpy(rc_header_bak, s->header_state, sizeof(s->header_state));
        memcpy(rc_block_bak, s->block_state, sizeof(s->block_state));
    }

redo_frame:

    if(pict->pict_type == FF_I_TYPE)
        s->spatial_decomposition_count= 5;
    else
        s->spatial_decomposition_count= 5;

    s->m.pict_type = pict->pict_type;
    s->qbias= pict->pict_type == FF_P_TYPE ? 2 : 0;

    common_init_after_header(avctx);

    if(s->last_spatial_decomposition_count != s->spatial_decomposition_count){
        for(plane_index=0; plane_index<3; plane_index++){
            calculate_visual_weight(s, &s->plane[plane_index]);
        }
    }

    encode_header(s);
    s->m.misc_bits = 8*(s->c.bytestream - s->c.bytestream_start);
    encode_blocks(s, 1);
    s->m.mv_bits = 8*(s->c.bytestream - s->c.bytestream_start) - s->m.misc_bits;

    for(plane_index=0; plane_index<3; plane_index++){
        Plane *p= &s->plane[plane_index];
        int w= p->width;
        int h= p->height;
        int x, y;
//        int bits= put_bits_count(&s->c.pb);

        if(!(avctx->flags2 & CODEC_FLAG2_MEMC_ONLY)){
            //FIXME optimize
            if(pict->data[plane_index]) //FIXME gray hack
                for(y=0; y<h; y++){
                    for(x=0; x<w; x++){
                        s->spatial_idwt_buffer[y*w + x]= pict->data[plane_index][y*pict->linesize[plane_index] + x]<<FRAC_BITS;
                    }
                }
            predict_plane(s, s->spatial_idwt_buffer, plane_index, 0);

            if(   plane_index==0
               && pict->pict_type == FF_P_TYPE
               && !(avctx->flags&CODEC_FLAG_PASS2)
               && s->m.me.scene_change_score > s->avctx->scenechange_threshold){
                ff_init_range_encoder(c, buf, buf_size);
                ff_build_rac_states(c, 0.05*(1LL<<32), 256-8);
                pict->pict_type= FF_I_TYPE;
                s->keyframe=1;
                s->current_picture.key_frame=1;
                goto redo_frame;
            }

            if(s->qlog == LOSSLESS_QLOG){
                for(y=0; y<h; y++){
                    for(x=0; x<w; x++){
                        s->spatial_dwt_buffer[y*w + x]= (s->spatial_idwt_buffer[y*w + x] + (1<<(FRAC_BITS-1))-1)>>FRAC_BITS;
                    }
                }
            }else{
                for(y=0; y<h; y++){
                    for(x=0; x<w; x++){
                        s->spatial_dwt_buffer[y*w + x]=s->spatial_idwt_buffer[y*w + x]<<ENCODER_EXTRA_BITS;
                    }
                }
            }

            /*  if(QUANTIZE2)
                dwt_quantize(s, p, s->spatial_dwt_buffer, w, h, w, s->spatial_decomposition_type);
            else*/
                ff_spatial_dwt(s->spatial_dwt_buffer, w, h, w, s->spatial_decomposition_type, s->spatial_decomposition_count);

            if(s->pass1_rc && plane_index==0){
                int delta_qlog = ratecontrol_1pass(s, pict);
                if (delta_qlog <= INT_MIN)
                    return -1;
                if(delta_qlog){
                    //reordering qlog in the bitstream would eliminate this reset
                    ff_init_range_encoder(c, buf, buf_size);
                    memcpy(s->header_state, rc_header_bak, sizeof(s->header_state));
                    memcpy(s->block_state, rc_block_bak, sizeof(s->block_state));
                    encode_header(s);
                    encode_blocks(s, 0);
                }
            }

            for(level=0; level<s->spatial_decomposition_count; level++){
                for(orientation=level ? 1 : 0; orientation<4; orientation++){
                    SubBand *b= &p->band[level][orientation];

                    if(!QUANTIZE2)
                        quantize(s, b, b->ibuf, b->buf, b->stride, s->qbias);
                    if(orientation==0)
                        decorrelate(s, b, b->ibuf, b->stride, pict->pict_type == FF_P_TYPE, 0);
                    encode_subband(s, b, b->ibuf, b->parent ? b->parent->ibuf : NULL, b->stride, orientation);
                    assert(b->parent==NULL || b->parent->stride == b->stride*2);
                    if(orientation==0)
                        correlate(s, b, b->ibuf, b->stride, 1, 0);
                }
            }

            for(level=0; level<s->spatial_decomposition_count; level++){
                for(orientation=level ? 1 : 0; orientation<4; orientation++){
                    SubBand *b= &p->band[level][orientation];

                    dequantize(s, b, b->ibuf, b->stride);
                }
            }

            ff_spatial_idwt(s->spatial_idwt_buffer, w, h, w, s->spatial_decomposition_type, s->spatial_decomposition_count);
            if(s->qlog == LOSSLESS_QLOG){
                for(y=0; y<h; y++){
                    for(x=0; x<w; x++){
                        s->spatial_idwt_buffer[y*w + x]<<=FRAC_BITS;
                    }
                }
            }
            predict_plane(s, s->spatial_idwt_buffer, plane_index, 1);
        }else{
            //ME/MC only
            if(pict->pict_type == FF_I_TYPE){
                for(y=0; y<h; y++){
                    for(x=0; x<w; x++){
                        s->current_picture.data[plane_index][y*s->current_picture.linesize[plane_index] + x]=
                            pict->data[plane_index][y*pict->linesize[plane_index] + x];
                    }
                }
            }else{
                memset(s->spatial_idwt_buffer, 0, sizeof(IDWTELEM)*w*h);
                predict_plane(s, s->spatial_idwt_buffer, plane_index, 1);
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
            s->current_picture.error[plane_index] = error;
        }

    }

    update_last_header_values(s);

    if(s->last_picture[s->max_ref_frames-1].data[0]){
        avctx->release_buffer(avctx, &s->last_picture[s->max_ref_frames-1]);
        for(i=0; i<9; i++)
            if(s->halfpel_plane[s->max_ref_frames-1][1+i/3][i%3])
                av_free(s->halfpel_plane[s->max_ref_frames-1][1+i/3][i%3] - EDGE_WIDTH*(1+s->current_picture.linesize[i%3]));
    }

    s->current_picture.coded_picture_number = avctx->frame_number;
    s->current_picture.pict_type = pict->pict_type;
    s->current_picture.quality = pict->quality;
    s->m.frame_bits = 8*(s->c.bytestream - s->c.bytestream_start);
    s->m.p_tex_bits = s->m.frame_bits - s->m.misc_bits - s->m.mv_bits;
    s->m.current_picture.display_picture_number =
    s->m.current_picture.coded_picture_number = avctx->frame_number;
    s->m.current_picture.quality = pict->quality;
    s->m.total_bits += 8*(s->c.bytestream - s->c.bytestream_start);
    if(s->pass1_rc)
        if (ff_rate_estimate_qscale(&s->m, 0) < 0)
            return -1;
    if(avctx->flags&CODEC_FLAG_PASS1)
        ff_write_pass1_stats(&s->m);
    s->m.last_pict_type = s->m.pict_type;
    avctx->frame_bits = s->m.frame_bits;
    avctx->mv_bits = s->m.mv_bits;
    avctx->misc_bits = s->m.misc_bits;
    avctx->p_tex_bits = s->m.p_tex_bits;

    emms_c();

    return ff_rac_terminate(c);
}

static av_cold void common_end(SnowContext *s){
    int plane_index, level, orientation, i;

    av_freep(&s->spatial_dwt_buffer);
    av_freep(&s->spatial_idwt_buffer);

    av_freep(&s->m.me.scratchpad);
    av_freep(&s->m.me.map);
    av_freep(&s->m.me.score_map);
    av_freep(&s->m.obmc_scratchpad);

    av_freep(&s->block);

    for(i=0; i<MAX_REF_FRAMES; i++){
        av_freep(&s->ref_mvs[i]);
        av_freep(&s->ref_scores[i]);
        if(s->last_picture[i].data[0])
            s->avctx->release_buffer(s->avctx, &s->last_picture[i]);
    }

    for(plane_index=0; plane_index<3; plane_index++){
        for(level=s->spatial_decomposition_count-1; level>=0; level--){
            for(orientation=level ? 1 : 0; orientation<4; orientation++){
                SubBand *b= &s->plane[plane_index].band[level][orientation];

                av_freep(&b->x_coeff);
            }
        }
    }
}

static av_cold int encode_end(AVCodecContext *avctx)
{
    SnowContext *s = avctx->priv_data;

    common_end(s);
    av_free(avctx->stats_out);

    return 0;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    avctx->pix_fmt= PIX_FMT_YUV420P;

    common_init(avctx);

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size, const uint8_t *buf, int buf_size){
    SnowContext *s = avctx->priv_data;
    RangeCoder * const c= &s->c;
    int bytes_read;
    AVFrame *picture = data;
    int level, orientation, plane_index, i;

    ff_init_range_decoder(c, buf, buf_size);
    ff_build_rac_states(c, 0.05*(1LL<<32), 256-8);

    s->current_picture.pict_type= FF_I_TYPE; //FIXME I vs. P
    if(decode_header(s)<0)
        return -1;
    common_init_after_header(avctx);

    // realloc slice buffer for the case that spatial_decomposition_count changed
    slice_buffer_destroy(&s->sb);
    slice_buffer_init(&s->sb, s->plane[0].height, (MB_SIZE >> s->block_max_depth) + s->spatial_decomposition_count * 8 + 1, s->plane[0].width, s->spatial_idwt_buffer);

    for(plane_index=0; plane_index<3; plane_index++){
        Plane *p= &s->plane[plane_index];
        p->fast_mc= p->diag_mc && p->htaps==6 && p->hcoeff[0]==40
                                              && p->hcoeff[1]==-10
                                              && p->hcoeff[2]==2;
    }

    if(!s->block) alloc_blocks(s);

    frame_start(s);
    //keyframe flag duplication mess FIXME
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
            predict_plane(s, s->spatial_idwt_buffer, plane_index, 1);

            for(y=0; y<h; y++){
                for(x=0; x<w; x++){
                    int v= s->current_picture.data[plane_index][y*s->current_picture.linesize[plane_index] + x];
                    s->mconly_picture.data[plane_index][y*s->mconly_picture.linesize[plane_index] + x]= v;
                }
            }
        }

        {
        for(level=0; level<s->spatial_decomposition_count; level++){
            for(orientation=level ? 1 : 0; orientation<4; orientation++){
                SubBand *b= &p->band[level][orientation];
                unpack_coeffs(s, b, b->parent, orientation);
            }
        }
        }

        {
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

            for(level=0; level<s->spatial_decomposition_count; level++){
                for(orientation=level ? 1 : 0; orientation<4; orientation++){
                    SubBand *b= &p->band[level][orientation];
                    int start_y;
                    int end_y;
                    int our_mb_start = mb_y;
                    int our_mb_end = (mb_y + 1);
                    const int extra= 3;
                    start_y = (mb_y ? ((block_w * our_mb_start) >> (s->spatial_decomposition_count - level)) + s->spatial_decomposition_count - level + extra: 0);
                    end_y = (((block_w * our_mb_end) >> (s->spatial_decomposition_count - level)) + s->spatial_decomposition_count - level + extra);
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
                            correlate_slice_buffered(s, &s->sb, correlate_band, correlate_band->ibuf, correlate_band->stride, 1, 0, correlate_start_y, correlate_end_y);
                            dequantize_slice_buffered(s, &s->sb, correlate_band, correlate_band->ibuf, correlate_band->stride, start_y, end_y);
                        }
                        else
                            decode_subband_slice_buffered(s, b, &s->sb, start_y, end_y, decode_state[level][orientation]);
                    }
                }
            }

            for(; yd<slice_h; yd+=4){
                ff_spatial_idwt_buffered_slice(&s->dsp, cs, &s->sb, w, h, 1, s->spatial_decomposition_type, s->spatial_decomposition_count, yd);
            }

            if(s->qlog == LOSSLESS_QLOG){
                for(; yq<slice_h && yq<h; yq++){
                    IDWTELEM * line = slice_buffer_get_line(&s->sb, yq);
                    for(x=0; x<w; x++){
                        line[x] <<= FRAC_BITS;
                    }
                }
            }

            predict_slice_buffered(s, &s->sb, s->spatial_idwt_buffer, plane_index, 1, mb_y);

            y = FFMIN(p->height, slice_starty);
            end_y = FFMIN(p->height, slice_h);
            while(y < end_y)
                slice_buffer_release(&s->sb, y++);
        }

        slice_buffer_flush(&s->sb);
        }

    }

    emms_c();

    if(s->last_picture[s->max_ref_frames-1].data[0]){
        avctx->release_buffer(avctx, &s->last_picture[s->max_ref_frames-1]);
        for(i=0; i<9; i++)
            if(s->halfpel_plane[s->max_ref_frames-1][1+i/3][i%3])
                av_free(s->halfpel_plane[s->max_ref_frames-1][1+i/3][i%3] - EDGE_WIDTH*(1+s->current_picture.linesize[i%3]));
    }

    if(!(s->avctx->debug&2048))
        *picture= s->current_picture;
    else
        *picture= s->mconly_picture;

    *data_size = sizeof(AVFrame);

    bytes_read= c->bytestream - c->bytestream_start;
    if(bytes_read ==0) av_log(s->avctx, AV_LOG_ERROR, "error at end of frame\n"); //FIXME

    return bytes_read;
}

static av_cold int decode_end(AVCodecContext *avctx)
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

#ifdef CONFIG_SNOW_ENCODER
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


#ifdef TEST
#undef malloc
#undef free
#undef printf
#undef random

int main(void){
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

    ff_spatial_dwt(buffer[0], width, height, width, s.spatial_decomposition_type, s.spatial_decomposition_count);
    ff_spatial_idwt(buffer[0], width, height, width, s.spatial_decomposition_type, s.spatial_decomposition_count);

    for(i=0; i<width*height; i++)
        if(buffer[0][i]!= buffer[1][i]) printf("fsck: %d %d %d\n",i, buffer[0][i], buffer[1][i]);

    printf("testing 9/7 DWT\n");
    s.spatial_decomposition_type=0;
    for(i=0; i<width*height; i++)
        buffer[0][i]= buffer[1][i]= random()%54321 - 12345;

    ff_spatial_dwt(buffer[0], width, height, width, s.spatial_decomposition_type, s.spatial_decomposition_count);
    ff_spatial_idwt(buffer[0], width, height, width, s.spatial_decomposition_type, s.spatial_decomposition_count);

    for(i=0; i<width*height; i++)
        if(FFABS(buffer[0][i] - buffer[1][i])>20) printf("fsck: %d %d %d\n",i, buffer[0][i], buffer[1][i]);

#if 0
    printf("testing AC coder\n");
    memset(s.header_state, 0, sizeof(s.header_state));
    ff_init_range_encoder(&s.c, buffer[0], 256*256);
    ff_init_cabac_states(&s.c, ff_h264_lps_range, ff_h264_mps_state, ff_h264_lps_state, 64);

    for(i=-256; i<256; i++){
        put_symbol(&s.c, s.header_state, i*i*i/3*FFABS(i), 1);
    }
    ff_rac_terminate(&s.c);

    memset(s.header_state, 0, sizeof(s.header_state));
    ff_init_range_decoder(&s.c, buffer[0], 256*256);
    ff_init_cabac_states(&s.c, ff_h264_lps_range, ff_h264_mps_state, ff_h264_lps_state, 64);

    for(i=-256; i<256; i++){
        int j;
        j= get_symbol(&s.c, s.header_state, 1);
        if(j!=i*i*i/3*FFABS(i)) printf("fsck: %d != %d\n", i, j);
    }
#endif
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
                ff_spatial_idwt(buffer[0], width, height, width, s.spatial_decomposition_type, s.spatial_decomposition_count);
                for(y=0; y<height; y++){
                    for(x=0; x<width; x++){
                        int64_t d= buffer[0][x + y*width];
                        error += d*d;
                        if(FFABS(width/2-x)<9 && FFABS(height/2-y)<9 && level==2) printf("%8"PRId64" ", d);
                    }
                    if(FFABS(height/2-y)<9 && level==2) printf("\n");
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
                printf("%8"PRId64",", errors[level][orientation]/g);
            }
            printf("},\n");
        }
        printf("};\n");
        {
            int level=2;
            int w= width  >> (s.spatial_decomposition_count-level);
            //int h= height >> (s.spatial_decomposition_count-level);
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
            ff_spatial_dwt(buffer[0], width, height, width, s.spatial_decomposition_type, s.spatial_decomposition_count);
#else
            for(y=0; y<h; y++){
                for(x=0; x<w; x++){
                    buf[x + y*stride  ]=169;
                    buf[x + y*stride-w]=64;
                }
            }
            ff_spatial_idwt(buffer[0], width, height, width, s.spatial_decomposition_type, s.spatial_decomposition_count);
#endif
            for(y=0; y<height; y++){
                for(x=0; x<width; x++){
                    int64_t d= buffer[0][x + y*width];
                    error += d*d;
                    if(FFABS(width/2-x)<9 && FFABS(height/2-y)<9) printf("%8"PRId64" ", d);
                }
                if(FFABS(height/2-y)<9) printf("\n");
            }
        }

    }
    return 0;
}
#endif /* TEST */
