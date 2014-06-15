/*
 * Copyright (c) 2012 Michael Niedermayer <michaelni@gmx.at>
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

#include "libavutil/x86/asm.h"
#include "libavutil/cpu.h"
#include "libswresample/swresample_internal.h"

DECLARE_ALIGNED(16, const uint64_t, ff_resample_int16_rounder)[2]    = { 0x0000000000004000ULL, 0x0000000000000000ULL};

#define COMMON_CORE_INT16_MMX2 \
    x86_reg len= -2*c->filter_length;\
__asm__ volatile(\
    "movq "MANGLE(ff_resample_int16_rounder)", %%mm0 \n\t"\
    "1:                         \n\t"\
    "movq    (%1, %0), %%mm1    \n\t"\
    "pmaddwd (%2, %0), %%mm1    \n\t"\
    "paddd  %%mm1, %%mm0        \n\t"\
    "add       $8, %0           \n\t"\
    " js 1b                     \n\t"\
    "pshufw $0x0E, %%mm0, %%mm1 \n\t"\
    "paddd %%mm1, %%mm0         \n\t"\
    "psrad    $15, %%mm0        \n\t"\
    "packssdw %%mm0, %%mm0      \n\t"\
    "movd %%mm0, (%3)           \n\t"\
    : "+r" (len)\
    : "r" (((uint8_t*)(src+sample_index))-len),\
      "r" (((uint8_t*)filter)-len),\
      "r" (dst+dst_index)\
      NAMED_CONSTRAINTS_ARRAY_ADD(ff_resample_int16_rounder)\
);

#define LINEAR_CORE_INT16_MMX2 \
    x86_reg len= -2*c->filter_length;\
__asm__ volatile(\
    "pxor          %%mm0, %%mm0 \n\t"\
    "pxor          %%mm2, %%mm2 \n\t"\
    "1:                         \n\t"\
    "movq       (%3, %0), %%mm1 \n\t"\
    "movq          %%mm1, %%mm3 \n\t"\
    "pmaddwd    (%4, %0), %%mm1 \n\t"\
    "pmaddwd    (%5, %0), %%mm3 \n\t"\
    "paddd         %%mm1, %%mm0 \n\t"\
    "paddd         %%mm3, %%mm2 \n\t"\
    "add              $8, %0    \n\t"\
    " js 1b                     \n\t"\
    "pshufw $0x0E, %%mm0, %%mm1 \n\t"\
    "pshufw $0x0E, %%mm2, %%mm3 \n\t"\
    "paddd         %%mm1, %%mm0 \n\t"\
    "paddd         %%mm3, %%mm2 \n\t"\
    "movd          %%mm0, %1    \n\t"\
    "movd          %%mm2, %2    \n\t"\
    : "+r" (len),\
      "=r" (val),\
      "=r" (v2)\
    : "r" (((uint8_t*)(src+sample_index))-len),\
      "r" (((uint8_t*)filter)-len),\
      "r" (((uint8_t*)(filter+c->filter_alloc))-len)\
);

#define COMMON_CORE_INT16_SSE2 \
    x86_reg len= -2*c->filter_length;\
__asm__ volatile(\
    "movdqa "MANGLE(ff_resample_int16_rounder)", %%xmm0 \n\t"\
    "1:                           \n\t"\
    "movdqu  (%1, %0), %%xmm1     \n\t"\
    "pmaddwd (%2, %0), %%xmm1     \n\t"\
    "paddd  %%xmm1, %%xmm0        \n\t"\
    "add       $16, %0            \n\t"\
    " js 1b                       \n\t"\
    "pshufd $0x0E, %%xmm0, %%xmm1 \n\t"\
    "paddd %%xmm1, %%xmm0         \n\t"\
    "pshufd $0x01, %%xmm0, %%xmm1 \n\t"\
    "paddd %%xmm1, %%xmm0         \n\t"\
    "psrad    $15, %%xmm0         \n\t"\
    "packssdw %%xmm0, %%xmm0      \n\t"\
    "movd %%xmm0, (%3)            \n\t"\
    : "+r" (len)\
    : "r" (((uint8_t*)(src+sample_index))-len),\
      "r" (((uint8_t*)filter)-len),\
      "r" (dst+dst_index)\
      NAMED_CONSTRAINTS_ARRAY_ADD(ff_resample_int16_rounder)\
      XMM_CLOBBERS_ONLY("%xmm0", "%xmm1")\
);

#define LINEAR_CORE_INT16_SSE2 \
    x86_reg len= -2*c->filter_length;\
__asm__ volatile(\
    "pxor          %%xmm0, %%xmm0 \n\t"\
    "pxor          %%xmm2, %%xmm2 \n\t"\
    "1:                           \n\t"\
    "movdqu      (%3, %0), %%xmm1 \n\t"\
    "movdqa        %%xmm1, %%xmm3 \n\t"\
    "pmaddwd     (%4, %0), %%xmm1 \n\t"\
    "pmaddwd     (%5, %0), %%xmm3 \n\t"\
    "paddd         %%xmm1, %%xmm0 \n\t"\
    "paddd         %%xmm3, %%xmm2 \n\t"\
    "add              $16, %0     \n\t"\
    " js 1b                       \n\t"\
    "pshufd $0x0E, %%xmm0, %%xmm1 \n\t"\
    "pshufd $0x0E, %%xmm2, %%xmm3 \n\t"\
    "paddd         %%xmm1, %%xmm0 \n\t"\
    "paddd         %%xmm3, %%xmm2 \n\t"\
    "pshufd $0x01, %%xmm0, %%xmm1 \n\t"\
    "pshufd $0x01, %%xmm2, %%xmm3 \n\t"\
    "paddd         %%xmm1, %%xmm0 \n\t"\
    "paddd         %%xmm3, %%xmm2 \n\t"\
    "movd          %%xmm0, %1     \n\t"\
    "movd          %%xmm2, %2     \n\t"\
    : "+r" (len),\
      "=r" (val),\
      "=r" (v2)\
    : "r" (((uint8_t*)(src+sample_index))-len),\
      "r" (((uint8_t*)filter)-len),\
      "r" (((uint8_t*)(filter+c->filter_alloc))-len)\
    XMM_CLOBBERS_ONLY("%xmm0", "%xmm1", "%xmm2", "%xmm3")\
);

#define COMMON_CORE_FLT_SSE \
    x86_reg len= -4*c->filter_length;\
__asm__ volatile(\
    "xorps     %%xmm0, %%xmm0     \n\t"\
    "1:                           \n\t"\
    "movups  (%1, %0), %%xmm1     \n\t"\
    "mulps   (%2, %0), %%xmm1     \n\t"\
    "addps     %%xmm1, %%xmm0     \n\t"\
    "add       $16, %0            \n\t"\
    " js 1b                       \n\t"\
    "movhlps   %%xmm0, %%xmm1     \n\t"\
    "addps     %%xmm1, %%xmm0     \n\t"\
    "movss     %%xmm0, %%xmm1     \n\t"\
    "shufps $1, %%xmm0, %%xmm0    \n\t"\
    "addps     %%xmm1, %%xmm0     \n\t"\
    "movss     %%xmm0, (%3)       \n\t"\
    : "+r" (len)\
    : "r" (((uint8_t*)(src+sample_index))-len),\
      "r" (((uint8_t*)filter)-len),\
      "r" (dst+dst_index)\
    XMM_CLOBBERS_ONLY("%xmm0", "%xmm1")\
);

#define LINEAR_CORE_FLT_SSE \
    x86_reg len= -4*c->filter_length;\
__asm__ volatile(\
    "xorps      %%xmm0, %%xmm0    \n\t"\
    "xorps      %%xmm2, %%xmm2    \n\t"\
    "1:                           \n\t"\
    "movups   (%3, %0), %%xmm1    \n\t"\
    "movaps     %%xmm1, %%xmm3    \n\t"\
    "mulps    (%4, %0), %%xmm1    \n\t"\
    "mulps    (%5, %0), %%xmm3    \n\t"\
    "addps      %%xmm1, %%xmm0    \n\t"\
    "addps      %%xmm3, %%xmm2    \n\t"\
    "add           $16, %0        \n\t"\
    " js 1b                       \n\t"\
    "movhlps    %%xmm0, %%xmm1    \n\t"\
    "movhlps    %%xmm2, %%xmm3    \n\t"\
    "addps      %%xmm1, %%xmm0    \n\t"\
    "addps      %%xmm3, %%xmm2    \n\t"\
    "movss      %%xmm0, %%xmm1    \n\t"\
    "movss      %%xmm2, %%xmm3    \n\t"\
    "shufps $1, %%xmm0, %%xmm0    \n\t"\
    "shufps $1, %%xmm2, %%xmm2    \n\t"\
    "addps      %%xmm1, %%xmm0    \n\t"\
    "addps      %%xmm3, %%xmm2    \n\t"\
    "movss      %%xmm0, %1        \n\t"\
    "movss      %%xmm2, %2        \n\t"\
    : "+r" (len),\
      "=m" (val),\
      "=m" (v2)\
    : "r" (((uint8_t*)(src+sample_index))-len),\
      "r" (((uint8_t*)filter)-len),\
      "r" (((uint8_t*)(filter+c->filter_alloc))-len)\
    XMM_CLOBBERS_ONLY("%xmm0", "%xmm1", "%xmm2", "%xmm3")\
);

#define COMMON_CORE_FLT_AVX \
    x86_reg len= -4*c->filter_length;\
__asm__ volatile(\
    "vxorps     %%ymm0, %%ymm0, %%ymm0    \n\t"\
    "1:                                   \n\t"\
    "vmovups  (%1, %0), %%ymm1            \n\t"\
    "vmulps   (%2, %0), %%ymm1, %%ymm1    \n\t"\
    "vaddps     %%ymm1, %%ymm0, %%ymm0    \n\t"\
    "add           $32, %0                \n\t"\
    " js 1b                               \n\t"\
    "vextractf128   $1, %%ymm0, %%xmm1    \n\t"\
    "vaddps     %%xmm1, %%xmm0, %%xmm0    \n\t"\
    "vmovhlps   %%xmm0, %%xmm1, %%xmm1    \n\t"\
    "vaddps     %%xmm1, %%xmm0, %%xmm0    \n\t"\
    "vshufps $1, %%xmm0, %%xmm0, %%xmm1   \n\t"\
    "vaddss     %%xmm1, %%xmm0, %%xmm0    \n\t"\
    "vmovss     %%xmm0, (%3)              \n\t"\
    : "+r" (len)\
    : "r" (((uint8_t*)(src+sample_index))-len),\
      "r" (((uint8_t*)filter)-len),\
      "r" (dst+dst_index)\
    XMM_CLOBBERS_ONLY("%xmm0", "%xmm1")\
);

#define LINEAR_CORE_FLT_AVX \
    x86_reg len= -4*c->filter_length;\
__asm__ volatile(\
    "vxorps      %%ymm0, %%ymm0, %%ymm0   \n\t"\
    "vxorps      %%ymm2, %%ymm2, %%ymm2   \n\t"\
    "1:                                   \n\t"\
    "vmovups   (%3, %0), %%ymm1           \n\t"\
    "vmulps    (%5, %0), %%ymm1, %%ymm3   \n\t"\
    "vmulps    (%4, %0), %%ymm1, %%ymm1   \n\t"\
    "vaddps      %%ymm1, %%ymm0, %%ymm0   \n\t"\
    "vaddps      %%ymm3, %%ymm2, %%ymm2   \n\t"\
    "add            $32, %0               \n\t"\
    " js 1b                               \n\t"\
    "vextractf128    $1, %%ymm0, %%xmm1   \n\t"\
    "vextractf128    $1, %%ymm2, %%xmm3   \n\t"\
    "vaddps      %%xmm1, %%xmm0, %%xmm0   \n\t"\
    "vaddps      %%xmm3, %%xmm2, %%xmm2   \n\t"\
    "vmovhlps    %%xmm0, %%xmm1, %%xmm1   \n\t"\
    "vmovhlps    %%xmm2, %%xmm3, %%xmm3   \n\t"\
    "vaddps      %%xmm1, %%xmm0, %%xmm0   \n\t"\
    "vaddps      %%xmm3, %%xmm2, %%xmm2   \n\t"\
    "vshufps $1, %%xmm0, %%xmm0, %%xmm1   \n\t"\
    "vshufps $1, %%xmm2, %%xmm2, %%xmm3   \n\t"\
    "vaddss      %%xmm1, %%xmm0, %%xmm0   \n\t"\
    "vaddss      %%xmm3, %%xmm2, %%xmm2   \n\t"\
    "vmovss      %%xmm0, %1               \n\t"\
    "vmovss      %%xmm2, %2               \n\t"\
    : "+r" (len),\
      "=m" (val),\
      "=m" (v2)\
    : "r" (((uint8_t*)(src+sample_index))-len),\
      "r" (((uint8_t*)filter)-len),\
      "r" (((uint8_t*)(filter+c->filter_alloc))-len)\
    XMM_CLOBBERS_ONLY("%xmm0", "%xmm1", "%xmm2", "%xmm3")\
);

#define COMMON_CORE_DBL_SSE2 \
    x86_reg len= -8*c->filter_length;\
__asm__ volatile(\
    "xorpd     %%xmm0, %%xmm0     \n\t"\
    "1:                           \n\t"\
    "movupd  (%1, %0), %%xmm1     \n\t"\
    "mulpd   (%2, %0), %%xmm1     \n\t"\
    "addpd     %%xmm1, %%xmm0     \n\t"\
    "add       $16, %0            \n\t"\
    " js 1b                       \n\t"\
    "movhlps   %%xmm0, %%xmm1     \n\t"\
    "addpd     %%xmm1, %%xmm0     \n\t"\
    "movsd     %%xmm0, (%3)       \n\t"\
    : "+r" (len)\
    : "r" (((uint8_t*)(src+sample_index))-len),\
      "r" (((uint8_t*)filter)-len),\
      "r" (dst+dst_index)\
    XMM_CLOBBERS_ONLY("%xmm0", "%xmm1")\
);

#define LINEAR_CORE_DBL_SSE2 \
    x86_reg len= -8*c->filter_length;\
__asm__ volatile(\
    "xorpd      %%xmm0, %%xmm0    \n\t"\
    "xorpd      %%xmm2, %%xmm2    \n\t"\
    "1:                           \n\t"\
    "movupd   (%3, %0), %%xmm1    \n\t"\
    "movapd     %%xmm1, %%xmm3    \n\t"\
    "mulpd    (%4, %0), %%xmm1    \n\t"\
    "mulpd    (%5, %0), %%xmm3    \n\t"\
    "addpd      %%xmm1, %%xmm0    \n\t"\
    "addpd      %%xmm3, %%xmm2    \n\t"\
    "add           $16, %0        \n\t"\
    " js 1b                       \n\t"\
    "movhlps    %%xmm0, %%xmm1    \n\t"\
    "movhlps    %%xmm2, %%xmm3    \n\t"\
    "addpd      %%xmm1, %%xmm0    \n\t"\
    "addpd      %%xmm3, %%xmm2    \n\t"\
    "movsd      %%xmm0, %1        \n\t"\
    "movsd      %%xmm2, %2        \n\t"\
    : "+r" (len),\
      "=m" (val),\
      "=m" (v2)\
    : "r" (((uint8_t*)(src+sample_index))-len),\
      "r" (((uint8_t*)filter)-len),\
      "r" (((uint8_t*)(filter+c->filter_alloc))-len)\
    XMM_CLOBBERS_ONLY("%xmm0", "%xmm1", "%xmm2", "%xmm3")\
);
