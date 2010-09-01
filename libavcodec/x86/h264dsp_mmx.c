/*
 * Copyright (c) 2004-2005 Michael Niedermayer, Loren Merritt
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

#include "libavutil/x86_cpu.h"
#include "libavcodec/h264dsp.h"
#include "dsputil_mmx.h"

DECLARE_ALIGNED(8, static const uint64_t, ff_pb_3_1  ) = 0x0103010301030103ULL;
DECLARE_ALIGNED(8, static const uint64_t, ff_pb_7_3  ) = 0x0307030703070307ULL;

/***********************************/
/* IDCT */

#define SUMSUB_BADC( a, b, c, d ) \
    "paddw "#b", "#a" \n\t"\
    "paddw "#d", "#c" \n\t"\
    "paddw "#b", "#b" \n\t"\
    "paddw "#d", "#d" \n\t"\
    "psubw "#a", "#b" \n\t"\
    "psubw "#c", "#d" \n\t"

#define SUMSUBD2_AB( a, b, t ) \
    "movq  "#b", "#t" \n\t"\
    "psraw  $1 , "#b" \n\t"\
    "paddw "#a", "#b" \n\t"\
    "psraw  $1 , "#a" \n\t"\
    "psubw "#t", "#a" \n\t"

#define IDCT4_1D( s02, s13, d02, d13, t ) \
    SUMSUB_BA  ( s02, d02 )\
    SUMSUBD2_AB( s13, d13, t )\
    SUMSUB_BADC( d13, s02, s13, d02 )

#define STORE_DIFF_4P( p, t, z ) \
    "psraw      $6,     "#p" \n\t"\
    "movd       (%0),   "#t" \n\t"\
    "punpcklbw "#z",    "#t" \n\t"\
    "paddsw    "#t",    "#p" \n\t"\
    "packuswb  "#z",    "#p" \n\t"\
    "movd      "#p",    (%0) \n\t"

static void ff_h264_idct_add_mmx(uint8_t *dst, int16_t *block, int stride)
{
    /* Load dct coeffs */
    __asm__ volatile(
        "movq   (%0), %%mm0 \n\t"
        "movq  8(%0), %%mm1 \n\t"
        "movq 16(%0), %%mm2 \n\t"
        "movq 24(%0), %%mm3 \n\t"
    :: "r"(block) );

    __asm__ volatile(
        /* mm1=s02+s13  mm2=s02-s13  mm4=d02+d13  mm0=d02-d13 */
        IDCT4_1D( %%mm2, %%mm1, %%mm0, %%mm3, %%mm4 )

        "movq      %0,    %%mm6 \n\t"
        /* in: 1,4,0,2  out: 1,2,3,0 */
        TRANSPOSE4( %%mm3, %%mm1, %%mm0, %%mm2, %%mm4 )

        "paddw     %%mm6, %%mm3 \n\t"

        /* mm2=s02+s13  mm3=s02-s13  mm4=d02+d13  mm1=d02-d13 */
        IDCT4_1D( %%mm4, %%mm2, %%mm3, %%mm0, %%mm1 )

        "pxor %%mm7, %%mm7    \n\t"
    :: "m"(ff_pw_32));

    __asm__ volatile(
    STORE_DIFF_4P( %%mm0, %%mm1, %%mm7)
        "add %1, %0             \n\t"
    STORE_DIFF_4P( %%mm2, %%mm1, %%mm7)
        "add %1, %0             \n\t"
    STORE_DIFF_4P( %%mm3, %%mm1, %%mm7)
        "add %1, %0             \n\t"
    STORE_DIFF_4P( %%mm4, %%mm1, %%mm7)
        : "+r"(dst)
        : "r" ((x86_reg)stride)
    );
}

static inline void h264_idct8_1d(int16_t *block)
{
    __asm__ volatile(
        "movq 112(%0), %%mm7  \n\t"
        "movq  80(%0), %%mm0  \n\t"
        "movq  48(%0), %%mm3  \n\t"
        "movq  16(%0), %%mm5  \n\t"

        "movq   %%mm0, %%mm4  \n\t"
        "movq   %%mm5, %%mm1  \n\t"
        "psraw  $1,    %%mm4  \n\t"
        "psraw  $1,    %%mm1  \n\t"
        "paddw  %%mm0, %%mm4  \n\t"
        "paddw  %%mm5, %%mm1  \n\t"
        "paddw  %%mm7, %%mm4  \n\t"
        "paddw  %%mm0, %%mm1  \n\t"
        "psubw  %%mm5, %%mm4  \n\t"
        "paddw  %%mm3, %%mm1  \n\t"

        "psubw  %%mm3, %%mm5  \n\t"
        "psubw  %%mm3, %%mm0  \n\t"
        "paddw  %%mm7, %%mm5  \n\t"
        "psubw  %%mm7, %%mm0  \n\t"
        "psraw  $1,    %%mm3  \n\t"
        "psraw  $1,    %%mm7  \n\t"
        "psubw  %%mm3, %%mm5  \n\t"
        "psubw  %%mm7, %%mm0  \n\t"

        "movq   %%mm4, %%mm3  \n\t"
        "movq   %%mm1, %%mm7  \n\t"
        "psraw  $2,    %%mm1  \n\t"
        "psraw  $2,    %%mm3  \n\t"
        "paddw  %%mm5, %%mm3  \n\t"
        "psraw  $2,    %%mm5  \n\t"
        "paddw  %%mm0, %%mm1  \n\t"
        "psraw  $2,    %%mm0  \n\t"
        "psubw  %%mm4, %%mm5  \n\t"
        "psubw  %%mm0, %%mm7  \n\t"

        "movq  32(%0), %%mm2  \n\t"
        "movq  96(%0), %%mm6  \n\t"
        "movq   %%mm2, %%mm4  \n\t"
        "movq   %%mm6, %%mm0  \n\t"
        "psraw  $1,    %%mm4  \n\t"
        "psraw  $1,    %%mm6  \n\t"
        "psubw  %%mm0, %%mm4  \n\t"
        "paddw  %%mm2, %%mm6  \n\t"

        "movq    (%0), %%mm2  \n\t"
        "movq  64(%0), %%mm0  \n\t"
        SUMSUB_BA( %%mm0, %%mm2 )
        SUMSUB_BA( %%mm6, %%mm0 )
        SUMSUB_BA( %%mm4, %%mm2 )
        SUMSUB_BA( %%mm7, %%mm6 )
        SUMSUB_BA( %%mm5, %%mm4 )
        SUMSUB_BA( %%mm3, %%mm2 )
        SUMSUB_BA( %%mm1, %%mm0 )
        :: "r"(block)
    );
}

static void ff_h264_idct8_add_mmx(uint8_t *dst, int16_t *block, int stride)
{
    int i;
    DECLARE_ALIGNED(8, int16_t, b2)[64];

    block[0] += 32;

    for(i=0; i<2; i++){
        DECLARE_ALIGNED(8, uint64_t, tmp);

        h264_idct8_1d(block+4*i);

        __asm__ volatile(
            "movq   %%mm7,    %0   \n\t"
            TRANSPOSE4( %%mm0, %%mm2, %%mm4, %%mm6, %%mm7 )
            "movq   %%mm0,  8(%1)  \n\t"
            "movq   %%mm6, 24(%1)  \n\t"
            "movq   %%mm7, 40(%1)  \n\t"
            "movq   %%mm4, 56(%1)  \n\t"
            "movq    %0,    %%mm7  \n\t"
            TRANSPOSE4( %%mm7, %%mm5, %%mm3, %%mm1, %%mm0 )
            "movq   %%mm7,   (%1)  \n\t"
            "movq   %%mm1, 16(%1)  \n\t"
            "movq   %%mm0, 32(%1)  \n\t"
            "movq   %%mm3, 48(%1)  \n\t"
            : "=m"(tmp)
            : "r"(b2+32*i)
            : "memory"
        );
    }

    for(i=0; i<2; i++){
        h264_idct8_1d(b2+4*i);

        __asm__ volatile(
            "psraw     $6, %%mm7  \n\t"
            "psraw     $6, %%mm6  \n\t"
            "psraw     $6, %%mm5  \n\t"
            "psraw     $6, %%mm4  \n\t"
            "psraw     $6, %%mm3  \n\t"
            "psraw     $6, %%mm2  \n\t"
            "psraw     $6, %%mm1  \n\t"
            "psraw     $6, %%mm0  \n\t"

            "movq   %%mm7,    (%0)  \n\t"
            "movq   %%mm5,  16(%0)  \n\t"
            "movq   %%mm3,  32(%0)  \n\t"
            "movq   %%mm1,  48(%0)  \n\t"
            "movq   %%mm0,  64(%0)  \n\t"
            "movq   %%mm2,  80(%0)  \n\t"
            "movq   %%mm4,  96(%0)  \n\t"
            "movq   %%mm6, 112(%0)  \n\t"
            :: "r"(b2+4*i)
            : "memory"
        );
    }

    ff_add_pixels_clamped_mmx(b2, dst, stride);
}

#define STORE_DIFF_8P( p, d, t, z )\
        "movq       "#d", "#t" \n"\
        "psraw       $6,  "#p" \n"\
        "punpcklbw  "#z", "#t" \n"\
        "paddsw     "#t", "#p" \n"\
        "packuswb   "#p", "#p" \n"\
        "movq       "#p", "#d" \n"

#define H264_IDCT8_1D_SSE2(a,b,c,d,e,f,g,h)\
        "movdqa     "#c", "#a" \n"\
        "movdqa     "#g", "#e" \n"\
        "psraw       $1,  "#c" \n"\
        "psraw       $1,  "#g" \n"\
        "psubw      "#e", "#c" \n"\
        "paddw      "#a", "#g" \n"\
        "movdqa     "#b", "#e" \n"\
        "psraw       $1,  "#e" \n"\
        "paddw      "#b", "#e" \n"\
        "paddw      "#d", "#e" \n"\
        "paddw      "#f", "#e" \n"\
        "movdqa     "#f", "#a" \n"\
        "psraw       $1,  "#a" \n"\
        "paddw      "#f", "#a" \n"\
        "paddw      "#h", "#a" \n"\
        "psubw      "#b", "#a" \n"\
        "psubw      "#d", "#b" \n"\
        "psubw      "#d", "#f" \n"\
        "paddw      "#h", "#b" \n"\
        "psubw      "#h", "#f" \n"\
        "psraw       $1,  "#d" \n"\
        "psraw       $1,  "#h" \n"\
        "psubw      "#d", "#b" \n"\
        "psubw      "#h", "#f" \n"\
        "movdqa     "#e", "#d" \n"\
        "movdqa     "#a", "#h" \n"\
        "psraw       $2,  "#d" \n"\
        "psraw       $2,  "#h" \n"\
        "paddw      "#f", "#d" \n"\
        "paddw      "#b", "#h" \n"\
        "psraw       $2,  "#f" \n"\
        "psraw       $2,  "#b" \n"\
        "psubw      "#f", "#e" \n"\
        "psubw      "#a", "#b" \n"\
        "movdqa 0x00(%1), "#a" \n"\
        "movdqa 0x40(%1), "#f" \n"\
        SUMSUB_BA(f, a)\
        SUMSUB_BA(g, f)\
        SUMSUB_BA(c, a)\
        SUMSUB_BA(e, g)\
        SUMSUB_BA(b, c)\
        SUMSUB_BA(h, a)\
        SUMSUB_BA(d, f)

static void ff_h264_idct8_add_sse2(uint8_t *dst, int16_t *block, int stride)
{
    __asm__ volatile(
        "movdqa   0x10(%1), %%xmm1 \n"
        "movdqa   0x20(%1), %%xmm2 \n"
        "movdqa   0x30(%1), %%xmm3 \n"
        "movdqa   0x50(%1), %%xmm5 \n"
        "movdqa   0x60(%1), %%xmm6 \n"
        "movdqa   0x70(%1), %%xmm7 \n"
        H264_IDCT8_1D_SSE2(%%xmm0, %%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm6, %%xmm7)
        TRANSPOSE8(%%xmm4, %%xmm1, %%xmm7, %%xmm3, %%xmm5, %%xmm0, %%xmm2, %%xmm6, (%1))
        "paddw          %4, %%xmm4 \n"
        "movdqa     %%xmm4, 0x00(%1) \n"
        "movdqa     %%xmm2, 0x40(%1) \n"
        H264_IDCT8_1D_SSE2(%%xmm4, %%xmm0, %%xmm6, %%xmm3, %%xmm2, %%xmm5, %%xmm7, %%xmm1)
        "movdqa     %%xmm6, 0x60(%1) \n"
        "movdqa     %%xmm7, 0x70(%1) \n"
        "pxor       %%xmm7, %%xmm7 \n"
        STORE_DIFF_8P(%%xmm2, (%0),      %%xmm6, %%xmm7)
        STORE_DIFF_8P(%%xmm0, (%0,%2),   %%xmm6, %%xmm7)
        STORE_DIFF_8P(%%xmm1, (%0,%2,2), %%xmm6, %%xmm7)
        STORE_DIFF_8P(%%xmm3, (%0,%3),   %%xmm6, %%xmm7)
        "lea     (%0,%2,4), %0 \n"
        STORE_DIFF_8P(%%xmm5, (%0),      %%xmm6, %%xmm7)
        STORE_DIFF_8P(%%xmm4, (%0,%2),   %%xmm6, %%xmm7)
        "movdqa   0x60(%1), %%xmm0 \n"
        "movdqa   0x70(%1), %%xmm1 \n"
        STORE_DIFF_8P(%%xmm0, (%0,%2,2), %%xmm6, %%xmm7)
        STORE_DIFF_8P(%%xmm1, (%0,%3),   %%xmm6, %%xmm7)
        :"+r"(dst)
        :"r"(block), "r"((x86_reg)stride), "r"((x86_reg)3L*stride), "m"(ff_pw_32)
    );
}

static void ff_h264_idct_dc_add_mmx2(uint8_t *dst, int16_t *block, int stride)
{
    int dc = (block[0] + 32) >> 6;
    __asm__ volatile(
        "movd          %0, %%mm0 \n\t"
        "pshufw $0, %%mm0, %%mm0 \n\t"
        "pxor       %%mm1, %%mm1 \n\t"
        "psubw      %%mm0, %%mm1 \n\t"
        "packuswb   %%mm0, %%mm0 \n\t"
        "packuswb   %%mm1, %%mm1 \n\t"
        ::"r"(dc)
    );
    __asm__ volatile(
        "movd          %0, %%mm2 \n\t"
        "movd          %1, %%mm3 \n\t"
        "movd          %2, %%mm4 \n\t"
        "movd          %3, %%mm5 \n\t"
        "paddusb    %%mm0, %%mm2 \n\t"
        "paddusb    %%mm0, %%mm3 \n\t"
        "paddusb    %%mm0, %%mm4 \n\t"
        "paddusb    %%mm0, %%mm5 \n\t"
        "psubusb    %%mm1, %%mm2 \n\t"
        "psubusb    %%mm1, %%mm3 \n\t"
        "psubusb    %%mm1, %%mm4 \n\t"
        "psubusb    %%mm1, %%mm5 \n\t"
        "movd       %%mm2, %0    \n\t"
        "movd       %%mm3, %1    \n\t"
        "movd       %%mm4, %2    \n\t"
        "movd       %%mm5, %3    \n\t"
        :"+m"(*(uint32_t*)(dst+0*stride)),
         "+m"(*(uint32_t*)(dst+1*stride)),
         "+m"(*(uint32_t*)(dst+2*stride)),
         "+m"(*(uint32_t*)(dst+3*stride))
    );
}

static void ff_h264_idct8_dc_add_mmx2(uint8_t *dst, int16_t *block, int stride)
{
    int dc = (block[0] + 32) >> 6;
    int y;
    __asm__ volatile(
        "movd          %0, %%mm0 \n\t"
        "pshufw $0, %%mm0, %%mm0 \n\t"
        "pxor       %%mm1, %%mm1 \n\t"
        "psubw      %%mm0, %%mm1 \n\t"
        "packuswb   %%mm0, %%mm0 \n\t"
        "packuswb   %%mm1, %%mm1 \n\t"
        ::"r"(dc)
    );
    for(y=2; y--; dst += 4*stride){
    __asm__ volatile(
        "movq          %0, %%mm2 \n\t"
        "movq          %1, %%mm3 \n\t"
        "movq          %2, %%mm4 \n\t"
        "movq          %3, %%mm5 \n\t"
        "paddusb    %%mm0, %%mm2 \n\t"
        "paddusb    %%mm0, %%mm3 \n\t"
        "paddusb    %%mm0, %%mm4 \n\t"
        "paddusb    %%mm0, %%mm5 \n\t"
        "psubusb    %%mm1, %%mm2 \n\t"
        "psubusb    %%mm1, %%mm3 \n\t"
        "psubusb    %%mm1, %%mm4 \n\t"
        "psubusb    %%mm1, %%mm5 \n\t"
        "movq       %%mm2, %0    \n\t"
        "movq       %%mm3, %1    \n\t"
        "movq       %%mm4, %2    \n\t"
        "movq       %%mm5, %3    \n\t"
        :"+m"(*(uint64_t*)(dst+0*stride)),
         "+m"(*(uint64_t*)(dst+1*stride)),
         "+m"(*(uint64_t*)(dst+2*stride)),
         "+m"(*(uint64_t*)(dst+3*stride))
    );
    }
}

//FIXME this table is a duplicate from h264data.h, and will be removed once the tables from, h264 have been split
static const uint8_t scan8[16 + 2*4]={
 4+1*8, 5+1*8, 4+2*8, 5+2*8,
 6+1*8, 7+1*8, 6+2*8, 7+2*8,
 4+3*8, 5+3*8, 4+4*8, 5+4*8,
 6+3*8, 7+3*8, 6+4*8, 7+4*8,
 1+1*8, 2+1*8,
 1+2*8, 2+2*8,
 1+4*8, 2+4*8,
 1+5*8, 2+5*8,
};

static void ff_h264_idct_add16_mmx(uint8_t *dst, const int *block_offset, DCTELEM *block, int stride, const uint8_t nnzc[6*8]){
    int i;
    for(i=0; i<16; i++){
        if(nnzc[ scan8[i] ])
            ff_h264_idct_add_mmx(dst + block_offset[i], block + i*16, stride);
    }
}

static void ff_h264_idct8_add4_mmx(uint8_t *dst, const int *block_offset, DCTELEM *block, int stride, const uint8_t nnzc[6*8]){
    int i;
    for(i=0; i<16; i+=4){
        if(nnzc[ scan8[i] ])
            ff_h264_idct8_add_mmx(dst + block_offset[i], block + i*16, stride);
    }
}


static void ff_h264_idct_add16_mmx2(uint8_t *dst, const int *block_offset, DCTELEM *block, int stride, const uint8_t nnzc[6*8]){
    int i;
    for(i=0; i<16; i++){
        int nnz = nnzc[ scan8[i] ];
        if(nnz){
            if(nnz==1 && block[i*16]) ff_h264_idct_dc_add_mmx2(dst + block_offset[i], block + i*16, stride);
            else                      ff_h264_idct_add_mmx    (dst + block_offset[i], block + i*16, stride);
        }
    }
}

static void ff_h264_idct_add16intra_mmx(uint8_t *dst, const int *block_offset, DCTELEM *block, int stride, const uint8_t nnzc[6*8]){
    int i;
    for(i=0; i<16; i++){
        if(nnzc[ scan8[i] ] || block[i*16])
            ff_h264_idct_add_mmx(dst + block_offset[i], block + i*16, stride);
    }
}

static void ff_h264_idct_add16intra_mmx2(uint8_t *dst, const int *block_offset, DCTELEM *block, int stride, const uint8_t nnzc[6*8]){
    int i;
    for(i=0; i<16; i++){
        if(nnzc[ scan8[i] ]) ff_h264_idct_add_mmx    (dst + block_offset[i], block + i*16, stride);
        else if(block[i*16]) ff_h264_idct_dc_add_mmx2(dst + block_offset[i], block + i*16, stride);
    }
}

static void ff_h264_idct8_add4_mmx2(uint8_t *dst, const int *block_offset, DCTELEM *block, int stride, const uint8_t nnzc[6*8]){
    int i;
    for(i=0; i<16; i+=4){
        int nnz = nnzc[ scan8[i] ];
        if(nnz){
            if(nnz==1 && block[i*16]) ff_h264_idct8_dc_add_mmx2(dst + block_offset[i], block + i*16, stride);
            else                      ff_h264_idct8_add_mmx    (dst + block_offset[i], block + i*16, stride);
        }
    }
}

static void ff_h264_idct8_add4_sse2(uint8_t *dst, const int *block_offset, DCTELEM *block, int stride, const uint8_t nnzc[6*8]){
    int i;
    for(i=0; i<16; i+=4){
        int nnz = nnzc[ scan8[i] ];
        if(nnz){
            if(nnz==1 && block[i*16]) ff_h264_idct8_dc_add_mmx2(dst + block_offset[i], block + i*16, stride);
            else                      ff_h264_idct8_add_sse2   (dst + block_offset[i], block + i*16, stride);
        }
    }
}

static void ff_h264_idct_add8_mmx(uint8_t **dest, const int *block_offset, DCTELEM *block, int stride, const uint8_t nnzc[6*8]){
    int i;
    for(i=16; i<16+8; i++){
        if(nnzc[ scan8[i] ] || block[i*16])
            ff_h264_idct_add_mmx    (dest[(i&4)>>2] + block_offset[i], block + i*16, stride);
    }
}

static void ff_h264_idct_add8_mmx2(uint8_t **dest, const int *block_offset, DCTELEM *block, int stride, const uint8_t nnzc[6*8]){
    int i;
    for(i=16; i<16+8; i++){
        if(nnzc[ scan8[i] ])
            ff_h264_idct_add_mmx    (dest[(i&4)>>2] + block_offset[i], block + i*16, stride);
        else if(block[i*16])
            ff_h264_idct_dc_add_mmx2(dest[(i&4)>>2] + block_offset[i], block + i*16, stride);
    }
}

#if CONFIG_GPL && HAVE_YASM
static void ff_h264_idct_dc_add8_mmx2(uint8_t *dst, int16_t *block, int stride)
{
    __asm__ volatile(
        "movd             %0, %%mm0 \n\t"   //  0 0 X D
        "punpcklwd        %1, %%mm0 \n\t"   //  x X d D
        "paddsw           %2, %%mm0 \n\t"
        "psraw            $6, %%mm0 \n\t"
        "punpcklwd     %%mm0, %%mm0 \n\t"   //  d d D D
        "pxor          %%mm1, %%mm1 \n\t"   //  0 0 0 0
        "psubw         %%mm0, %%mm1 \n\t"   // -d-d-D-D
        "packuswb      %%mm1, %%mm0 \n\t"   // -d-d-D-D d d D D
        "pshufw $0xFA, %%mm0, %%mm1 \n\t"   // -d-d-d-d-D-D-D-D
        "punpcklwd     %%mm0, %%mm0 \n\t"   //  d d d d D D D D
        ::"m"(block[ 0]),
          "m"(block[16]),
          "m"(ff_pw_32)
    );
    __asm__ volatile(
        "movq          %0, %%mm2 \n\t"
        "movq          %1, %%mm3 \n\t"
        "movq          %2, %%mm4 \n\t"
        "movq          %3, %%mm5 \n\t"
        "paddusb    %%mm0, %%mm2 \n\t"
        "paddusb    %%mm0, %%mm3 \n\t"
        "paddusb    %%mm0, %%mm4 \n\t"
        "paddusb    %%mm0, %%mm5 \n\t"
        "psubusb    %%mm1, %%mm2 \n\t"
        "psubusb    %%mm1, %%mm3 \n\t"
        "psubusb    %%mm1, %%mm4 \n\t"
        "psubusb    %%mm1, %%mm5 \n\t"
        "movq       %%mm2, %0    \n\t"
        "movq       %%mm3, %1    \n\t"
        "movq       %%mm4, %2    \n\t"
        "movq       %%mm5, %3    \n\t"
        :"+m"(*(uint64_t*)(dst+0*stride)),
         "+m"(*(uint64_t*)(dst+1*stride)),
         "+m"(*(uint64_t*)(dst+2*stride)),
         "+m"(*(uint64_t*)(dst+3*stride))
    );
}

extern void ff_x264_add8x4_idct_sse2(uint8_t *dst, int16_t *block, int stride);

static void ff_h264_idct_add16_sse2(uint8_t *dst, const int *block_offset, DCTELEM *block, int stride, const uint8_t nnzc[6*8]){
    int i;
    for(i=0; i<16; i+=2)
        if(nnzc[ scan8[i+0] ]|nnzc[ scan8[i+1] ])
            ff_x264_add8x4_idct_sse2 (dst + block_offset[i], block + i*16, stride);
}

static void ff_h264_idct_add16intra_sse2(uint8_t *dst, const int *block_offset, DCTELEM *block, int stride, const uint8_t nnzc[6*8]){
    int i;
    for(i=0; i<16; i+=2){
        if(nnzc[ scan8[i+0] ]|nnzc[ scan8[i+1] ])
            ff_x264_add8x4_idct_sse2 (dst + block_offset[i], block + i*16, stride);
        else if(block[i*16]|block[i*16+16])
            ff_h264_idct_dc_add8_mmx2(dst + block_offset[i], block + i*16, stride);
    }
}

static void ff_h264_idct_add8_sse2(uint8_t **dest, const int *block_offset, DCTELEM *block, int stride, const uint8_t nnzc[6*8]){
    int i;
    for(i=16; i<16+8; i+=2){
        if(nnzc[ scan8[i+0] ]|nnzc[ scan8[i+1] ])
            ff_x264_add8x4_idct_sse2 (dest[(i&4)>>2] + block_offset[i], block + i*16, stride);
        else if(block[i*16]|block[i*16+16])
            ff_h264_idct_dc_add8_mmx2(dest[(i&4)>>2] + block_offset[i], block + i*16, stride);
    }
}
#endif

/***********************************/
/* deblocking */

// out: o = |x-y|>a
// clobbers: t
#define DIFF_GT_MMX(x,y,a,o,t)\
    "movq     "#y", "#t"  \n\t"\
    "movq     "#x", "#o"  \n\t"\
    "psubusb  "#x", "#t"  \n\t"\
    "psubusb  "#y", "#o"  \n\t"\
    "por      "#t", "#o"  \n\t"\
    "psubusb  "#a", "#o"  \n\t"

// out: o = |x-y|>a
// clobbers: t
#define DIFF_GT2_MMX(x,y,a,o,t)\
    "movq     "#y", "#t"  \n\t"\
    "movq     "#x", "#o"  \n\t"\
    "psubusb  "#x", "#t"  \n\t"\
    "psubusb  "#y", "#o"  \n\t"\
    "psubusb  "#a", "#t"  \n\t"\
    "psubusb  "#a", "#o"  \n\t"\
    "pcmpeqb  "#t", "#o"  \n\t"\

// in: mm0=p1 mm1=p0 mm2=q0 mm3=q1
// out: mm5=beta-1, mm7=mask
// clobbers: mm4,mm6
#define H264_DEBLOCK_MASK(alpha1, beta1) \
    "pshufw $0, "#alpha1", %%mm4 \n\t"\
    "pshufw $0, "#beta1 ", %%mm5 \n\t"\
    "packuswb  %%mm4, %%mm4      \n\t"\
    "packuswb  %%mm5, %%mm5      \n\t"\
    DIFF_GT_MMX(%%mm1, %%mm2, %%mm4, %%mm7, %%mm6) /* |p0-q0| > alpha-1 */\
    DIFF_GT_MMX(%%mm0, %%mm1, %%mm5, %%mm4, %%mm6) /* |p1-p0| > beta-1 */\
    "por       %%mm4, %%mm7      \n\t"\
    DIFF_GT_MMX(%%mm3, %%mm2, %%mm5, %%mm4, %%mm6) /* |q1-q0| > beta-1 */\
    "por       %%mm4, %%mm7      \n\t"\
    "pxor      %%mm6, %%mm6      \n\t"\
    "pcmpeqb   %%mm6, %%mm7      \n\t"

// in: mm0=p1 mm1=p0 mm2=q0 mm3=q1 mm7=(tc&mask)
// out: mm1=p0' mm2=q0'
// clobbers: mm0,3-6
#define H264_DEBLOCK_P0_Q0(pb_01, pb_3f)\
        "movq    %%mm1              , %%mm5 \n\t"\
        "pxor    %%mm2              , %%mm5 \n\t" /* p0^q0*/\
        "pand    "#pb_01"           , %%mm5 \n\t" /* (p0^q0)&1*/\
        "pcmpeqb %%mm4              , %%mm4 \n\t"\
        "pxor    %%mm4              , %%mm3 \n\t"\
        "pavgb   %%mm0              , %%mm3 \n\t" /* (p1 - q1 + 256)>>1*/\
        "pavgb   "MANGLE(ff_pb_3)"  , %%mm3 \n\t" /*(((p1 - q1 + 256)>>1)+4)>>1 = 64+2+(p1-q1)>>2*/\
        "pxor    %%mm1              , %%mm4 \n\t"\
        "pavgb   %%mm2              , %%mm4 \n\t" /* (q0 - p0 + 256)>>1*/\
        "pavgb   %%mm5              , %%mm3 \n\t"\
        "paddusb %%mm4              , %%mm3 \n\t" /* d+128+33*/\
        "movq    "MANGLE(ff_pb_A1)" , %%mm6 \n\t"\
        "psubusb %%mm3              , %%mm6 \n\t"\
        "psubusb "MANGLE(ff_pb_A1)" , %%mm3 \n\t"\
        "pminub  %%mm7              , %%mm6 \n\t"\
        "pminub  %%mm7              , %%mm3 \n\t"\
        "psubusb %%mm6              , %%mm1 \n\t"\
        "psubusb %%mm3              , %%mm2 \n\t"\
        "paddusb %%mm3              , %%mm1 \n\t"\
        "paddusb %%mm6              , %%mm2 \n\t"

// in: mm0=p1 mm1=p0 mm2=q0 mm3=q1 mm7=(tc&mask) %8=ff_bone
// out: (q1addr) = av_clip( (q2+((p0+q0+1)>>1))>>1, q1-tc0, q1+tc0 )
// clobbers: q2, tmp, tc0
#define H264_DEBLOCK_Q1(p1, q2, q2addr, q1addr, tc0, tmp)\
        "movq     %%mm1,  "#tmp"   \n\t"\
        "pavgb    %%mm2,  "#tmp"   \n\t"\
        "pavgb    "#tmp", "#q2"    \n\t" /* avg(p2,avg(p0,q0)) */\
        "pxor   "q2addr", "#tmp"   \n\t"\
        "pand     %9,     "#tmp"   \n\t" /* (p2^avg(p0,q0))&1 */\
        "psubusb  "#tmp", "#q2"    \n\t" /* (p2+((p0+q0+1)>>1))>>1 */\
        "movq     "#p1",  "#tmp"   \n\t"\
        "psubusb  "#tc0", "#tmp"   \n\t"\
        "paddusb  "#p1",  "#tc0"   \n\t"\
        "pmaxub   "#tmp", "#q2"    \n\t"\
        "pminub   "#tc0", "#q2"    \n\t"\
        "movq     "#q2",  "q1addr" \n\t"

static inline void h264_loop_filter_luma_mmx2(uint8_t *pix, int stride, int alpha1, int beta1, int8_t *tc0)
{
    DECLARE_ALIGNED(8, uint64_t, tmp0)[2];

    __asm__ volatile(
        "movq    (%2,%4), %%mm0    \n\t" //p1
        "movq    (%2,%4,2), %%mm1  \n\t" //p0
        "movq    (%3),    %%mm2    \n\t" //q0
        "movq    (%3,%4), %%mm3    \n\t" //q1
        H264_DEBLOCK_MASK(%7, %8)

        "movd      %6,    %%mm4    \n\t"
        "punpcklbw %%mm4, %%mm4    \n\t"
        "punpcklwd %%mm4, %%mm4    \n\t"
        "pcmpeqb   %%mm3, %%mm3    \n\t"
        "movq      %%mm4, %%mm6    \n\t"
        "pcmpgtb   %%mm3, %%mm4    \n\t"
        "movq      %%mm6, %1       \n\t"
        "pand      %%mm4, %%mm7    \n\t"
        "movq      %%mm7, %0       \n\t"

        /* filter p1 */
        "movq     (%2),   %%mm3    \n\t" //p2
        DIFF_GT2_MMX(%%mm1, %%mm3, %%mm5, %%mm6, %%mm4) // |p2-p0|>beta-1
        "pand     %%mm7,  %%mm6    \n\t" // mask & |p2-p0|<beta
        "pand     %1,     %%mm7    \n\t" // mask & tc0
        "movq     %%mm7,  %%mm4    \n\t"
        "psubb    %%mm6,  %%mm7    \n\t"
        "pand     %%mm4,  %%mm6    \n\t" // mask & |p2-p0|<beta & tc0
        H264_DEBLOCK_Q1(%%mm0, %%mm3, "(%2)", "(%2,%4)", %%mm6, %%mm4)

        /* filter q1 */
        "movq    (%3,%4,2), %%mm4  \n\t" //q2
        DIFF_GT2_MMX(%%mm2, %%mm4, %%mm5, %%mm6, %%mm3) // |q2-q0|>beta-1
        "pand     %0,     %%mm6    \n\t"
        "movq     %1,     %%mm5    \n\t" // can be merged with the and below but is slower then
        "pand     %%mm6,  %%mm5    \n\t"
        "psubb    %%mm6,  %%mm7    \n\t"
        "movq    (%3,%4), %%mm3    \n\t"
        H264_DEBLOCK_Q1(%%mm3, %%mm4, "(%3,%4,2)", "(%3,%4)", %%mm5, %%mm6)

        /* filter p0, q0 */
        H264_DEBLOCK_P0_Q0(%9, unused)
        "movq      %%mm1, (%2,%4,2) \n\t"
        "movq      %%mm2, (%3)      \n\t"

        : "=m"(tmp0[0]), "=m"(tmp0[1])
        : "r"(pix-3*stride), "r"(pix), "r"((x86_reg)stride),
          "m"(*tmp0/*unused*/), "m"(*(uint32_t*)tc0), "m"(alpha1), "m"(beta1),
          "m"(ff_bone)
    );
}

static void h264_v_loop_filter_luma_mmx2(uint8_t *pix, int stride, int alpha, int beta, int8_t *tc0)
{
    if((tc0[0] & tc0[1]) >= 0)
        h264_loop_filter_luma_mmx2(pix, stride, alpha-1, beta-1, tc0);
    if((tc0[2] & tc0[3]) >= 0)
        h264_loop_filter_luma_mmx2(pix+8, stride, alpha-1, beta-1, tc0+2);
}
static void h264_h_loop_filter_luma_mmx2(uint8_t *pix, int stride, int alpha, int beta, int8_t *tc0)
{
    //FIXME: could cut some load/stores by merging transpose with filter
    // also, it only needs to transpose 6x8
    DECLARE_ALIGNED(8, uint8_t, trans)[8*8];
    int i;
    for(i=0; i<2; i++, pix+=8*stride, tc0+=2) {
        if((tc0[0] & tc0[1]) < 0)
            continue;
        transpose4x4(trans,       pix-4,          8, stride);
        transpose4x4(trans  +4*8, pix,            8, stride);
        transpose4x4(trans+4,     pix-4+4*stride, 8, stride);
        transpose4x4(trans+4+4*8, pix  +4*stride, 8, stride);
        h264_loop_filter_luma_mmx2(trans+4*8, 8, alpha-1, beta-1, tc0);
        transpose4x4(pix-2,          trans  +2*8, stride, 8);
        transpose4x4(pix-2+4*stride, trans+4+2*8, stride, 8);
    }
}

static inline void h264_loop_filter_chroma_mmx2(uint8_t *pix, int stride, int alpha1, int beta1, int8_t *tc0)
{
    __asm__ volatile(
        "movq    (%0),    %%mm0     \n\t" //p1
        "movq    (%0,%2), %%mm1     \n\t" //p0
        "movq    (%1),    %%mm2     \n\t" //q0
        "movq    (%1,%2), %%mm3     \n\t" //q1
        H264_DEBLOCK_MASK(%4, %5)
        "movd      %3,    %%mm6     \n\t"
        "punpcklbw %%mm6, %%mm6     \n\t"
        "pand      %%mm6, %%mm7     \n\t" // mm7 = tc&mask
        H264_DEBLOCK_P0_Q0(%6, %7)
        "movq      %%mm1, (%0,%2)   \n\t"
        "movq      %%mm2, (%1)      \n\t"

        :: "r"(pix-2*stride), "r"(pix), "r"((x86_reg)stride),
           "r"(*(uint32_t*)tc0),
           "m"(alpha1), "m"(beta1), "m"(ff_bone), "m"(ff_pb_3F)
    );
}

static void h264_v_loop_filter_chroma_mmx2(uint8_t *pix, int stride, int alpha, int beta, int8_t *tc0)
{
    h264_loop_filter_chroma_mmx2(pix, stride, alpha-1, beta-1, tc0);
}

static void h264_h_loop_filter_chroma_mmx2(uint8_t *pix, int stride, int alpha, int beta, int8_t *tc0)
{
    //FIXME: could cut some load/stores by merging transpose with filter
    DECLARE_ALIGNED(8, uint8_t, trans)[8*4];
    transpose4x4(trans, pix-2, 8, stride);
    transpose4x4(trans+4, pix-2+4*stride, 8, stride);
    h264_loop_filter_chroma_mmx2(trans+2*8, 8, alpha-1, beta-1, tc0);
    transpose4x4(pix-2, trans, stride, 8);
    transpose4x4(pix-2+4*stride, trans+4, stride, 8);
}

// p0 = (p0 + q1 + 2*p1 + 2) >> 2
#define H264_FILTER_CHROMA4(p0, p1, q1, one) \
    "movq    "#p0", %%mm4  \n\t"\
    "pxor    "#q1", %%mm4  \n\t"\
    "pand   "#one", %%mm4  \n\t" /* mm4 = (p0^q1)&1 */\
    "pavgb   "#q1", "#p0"  \n\t"\
    "psubusb %%mm4, "#p0"  \n\t"\
    "pavgb   "#p1", "#p0"  \n\t" /* dst = avg(p1, avg(p0,q1) - ((p0^q1)&1)) */\

static inline void h264_loop_filter_chroma_intra_mmx2(uint8_t *pix, int stride, int alpha1, int beta1)
{
    __asm__ volatile(
        "movq    (%0),    %%mm0     \n\t"
        "movq    (%0,%2), %%mm1     \n\t"
        "movq    (%1),    %%mm2     \n\t"
        "movq    (%1,%2), %%mm3     \n\t"
        H264_DEBLOCK_MASK(%3, %4)
        "movq    %%mm1,   %%mm5     \n\t"
        "movq    %%mm2,   %%mm6     \n\t"
        H264_FILTER_CHROMA4(%%mm1, %%mm0, %%mm3, %5) //p0'
        H264_FILTER_CHROMA4(%%mm2, %%mm3, %%mm0, %5) //q0'
        "psubb   %%mm5,   %%mm1     \n\t"
        "psubb   %%mm6,   %%mm2     \n\t"
        "pand    %%mm7,   %%mm1     \n\t"
        "pand    %%mm7,   %%mm2     \n\t"
        "paddb   %%mm5,   %%mm1     \n\t"
        "paddb   %%mm6,   %%mm2     \n\t"
        "movq    %%mm1,   (%0,%2)   \n\t"
        "movq    %%mm2,   (%1)      \n\t"
        :: "r"(pix-2*stride), "r"(pix), "r"((x86_reg)stride),
           "m"(alpha1), "m"(beta1), "m"(ff_bone)
    );
}

static void h264_v_loop_filter_chroma_intra_mmx2(uint8_t *pix, int stride, int alpha, int beta)
{
    h264_loop_filter_chroma_intra_mmx2(pix, stride, alpha-1, beta-1);
}

static void h264_h_loop_filter_chroma_intra_mmx2(uint8_t *pix, int stride, int alpha, int beta)
{
    //FIXME: could cut some load/stores by merging transpose with filter
    DECLARE_ALIGNED(8, uint8_t, trans)[8*4];
    transpose4x4(trans, pix-2, 8, stride);
    transpose4x4(trans+4, pix-2+4*stride, 8, stride);
    h264_loop_filter_chroma_intra_mmx2(trans+2*8, 8, alpha-1, beta-1);
    transpose4x4(pix-2, trans, stride, 8);
    transpose4x4(pix-2+4*stride, trans+4, stride, 8);
}

static void h264_loop_filter_strength_mmx2( int16_t bS[2][4][4], uint8_t nnz[40], int8_t ref[2][40], int16_t mv[2][40][2],
                                            int bidir, int edges, int step, int mask_mv0, int mask_mv1, int field ) {
    int dir;
    __asm__ volatile(
        "movq %0, %%mm7 \n"
        "movq %1, %%mm6 \n"
        ::"m"(ff_pb_1), "m"(ff_pb_3)
    );
    if(field)
        __asm__ volatile(
            "movq %0, %%mm6 \n"
            ::"m"(ff_pb_3_1)
        );
    __asm__ volatile(
        "movq  %%mm6, %%mm5 \n"
        "paddb %%mm5, %%mm5 \n"
    :);

    // could do a special case for dir==0 && edges==1, but it only reduces the
    // average filter time by 1.2%
    for( dir=1; dir>=0; dir-- ) {
        const x86_reg d_idx = dir ? -8 : -1;
        const int mask_mv = dir ? mask_mv1 : mask_mv0;
        DECLARE_ALIGNED(8, const uint64_t, mask_dir) = dir ? 0 : 0xffffffffffffffffULL;
        int b_idx, edge;
        for( b_idx=12, edge=0; edge<edges; edge+=step, b_idx+=8*step ) {
            __asm__ volatile(
                "pand %0, %%mm0 \n\t"
                ::"m"(mask_dir)
            );
            if(!(mask_mv & edge)) {
                if(bidir) {
                    __asm__ volatile(
                        "movd         (%1,%0), %%mm2 \n"
                        "punpckldq  40(%1,%0), %%mm2 \n" // { ref0[bn], ref1[bn] }
                        "pshufw $0x44,   (%1), %%mm0 \n" // { ref0[b], ref0[b] }
                        "pshufw $0x44, 40(%1), %%mm1 \n" // { ref1[b], ref1[b] }
                        "pshufw $0x4E, %%mm2, %%mm3 \n"
                        "psubb         %%mm2, %%mm0 \n" // { ref0[b]!=ref0[bn], ref0[b]!=ref1[bn] }
                        "psubb         %%mm3, %%mm1 \n" // { ref1[b]!=ref1[bn], ref1[b]!=ref0[bn] }
                        "1: \n"
                        "por           %%mm1, %%mm0 \n"
                        "movq      (%2,%0,4), %%mm1 \n"
                        "movq     8(%2,%0,4), %%mm2 \n"
                        "movq          %%mm1, %%mm3 \n"
                        "movq          %%mm2, %%mm4 \n"
                        "psubw          (%2), %%mm1 \n"
                        "psubw         8(%2), %%mm2 \n"
                        "psubw       160(%2), %%mm3 \n"
                        "psubw       168(%2), %%mm4 \n"
                        "packsswb      %%mm2, %%mm1 \n"
                        "packsswb      %%mm4, %%mm3 \n"
                        "paddb         %%mm6, %%mm1 \n"
                        "paddb         %%mm6, %%mm3 \n"
                        "psubusb       %%mm5, %%mm1 \n" // abs(mv[b] - mv[bn]) >= limit
                        "psubusb       %%mm5, %%mm3 \n"
                        "packsswb      %%mm3, %%mm1 \n"
                        "add $40, %0 \n"
                        "cmp $40, %0 \n"
                        "jl 1b \n"
                        "sub $80, %0 \n"
                        "pshufw $0x4E, %%mm1, %%mm1 \n"
                        "por           %%mm1, %%mm0 \n"
                        "pshufw $0x4E, %%mm0, %%mm1 \n"
                        "pminub        %%mm1, %%mm0 \n"
                        ::"r"(d_idx),
                          "r"(ref[0]+b_idx),
                          "r"(mv[0]+b_idx)
                    );
                } else {
                    __asm__ volatile(
                        "movd        (%1), %%mm0 \n"
                        "psubb    (%1,%0), %%mm0 \n" // ref[b] != ref[bn]
                        "movq        (%2), %%mm1 \n"
                        "movq       8(%2), %%mm2 \n"
                        "psubw  (%2,%0,4), %%mm1 \n"
                        "psubw 8(%2,%0,4), %%mm2 \n"
                        "packsswb   %%mm2, %%mm1 \n"
                        "paddb      %%mm6, %%mm1 \n"
                        "psubusb    %%mm5, %%mm1 \n" // abs(mv[b] - mv[bn]) >= limit
                        "packsswb   %%mm1, %%mm1 \n"
                        "por        %%mm1, %%mm0 \n"
                        ::"r"(d_idx),
                          "r"(ref[0]+b_idx),
                          "r"(mv[0]+b_idx)
                    );
                }
            }
            __asm__ volatile(
                "movd %0, %%mm1 \n"
                "por  %1, %%mm1 \n" // nnz[b] || nnz[bn]
                ::"m"(nnz[b_idx]),
                  "m"(nnz[b_idx+d_idx])
            );
            __asm__ volatile(
                "pminub    %%mm7, %%mm1 \n"
                "pminub    %%mm7, %%mm0 \n"
                "psllw        $1, %%mm1 \n"
                "pxor      %%mm2, %%mm2 \n"
                "pmaxub    %%mm0, %%mm1 \n"
                "punpcklbw %%mm2, %%mm1 \n"
                "movq      %%mm1, %0    \n"
                :"=m"(*bS[dir][edge])
                ::"memory"
            );
        }
        edges = 4;
        step = 1;
    }
    __asm__ volatile(
        "movq   (%0), %%mm0 \n\t"
        "movq  8(%0), %%mm1 \n\t"
        "movq 16(%0), %%mm2 \n\t"
        "movq 24(%0), %%mm3 \n\t"
        TRANSPOSE4(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4)
        "movq %%mm0,   (%0) \n\t"
        "movq %%mm3,  8(%0) \n\t"
        "movq %%mm4, 16(%0) \n\t"
        "movq %%mm2, 24(%0) \n\t"
        ::"r"(bS[0])
        :"memory"
    );
}

/***********************************/
/* weighted prediction */

#define H264_WEIGHT(W, H, OPT) \
void ff_h264_weight_ ## W ## x ## H ## _ ## OPT(uint8_t *dst, \
    int stride, int log2_denom, int weight, int offset);

#define H264_BIWEIGHT(W, H, OPT) \
void ff_h264_biweight_ ## W ## x ## H ## _ ## OPT(uint8_t *dst, \
    uint8_t *src, int stride, int log2_denom, int weightd, \
    int weights, int offset);

#define H264_BIWEIGHT_MMX(W,H) \
H264_WEIGHT  (W, H, mmx2) \
H264_BIWEIGHT(W, H, mmx2)

#define H264_BIWEIGHT_MMX_SSE(W,H) \
H264_BIWEIGHT_MMX(W, H) \
H264_WEIGHT      (W, H, sse2) \
H264_BIWEIGHT    (W, H, sse2) \
H264_BIWEIGHT    (W, H, ssse3)

H264_BIWEIGHT_MMX_SSE(16, 16)
H264_BIWEIGHT_MMX_SSE(16,  8)
H264_BIWEIGHT_MMX_SSE( 8, 16)
H264_BIWEIGHT_MMX_SSE( 8,  8)
H264_BIWEIGHT_MMX_SSE( 8,  4)
H264_BIWEIGHT_MMX    ( 4,  8)
H264_BIWEIGHT_MMX    ( 4,  4)
H264_BIWEIGHT_MMX    ( 4,  2)

void ff_x264_deblock_v_luma_sse2(uint8_t *pix, int stride, int alpha, int beta, int8_t *tc0);
void ff_x264_deblock_h_luma_sse2(uint8_t *pix, int stride, int alpha, int beta, int8_t *tc0);
void ff_x264_deblock_h_luma_intra_mmxext(uint8_t *pix, int stride, int alpha, int beta);
void ff_x264_deblock_v_luma_intra_sse2(uint8_t *pix, int stride, int alpha, int beta);
void ff_x264_deblock_h_luma_intra_sse2(uint8_t *pix, int stride, int alpha, int beta);

#if HAVE_YASM && ARCH_X86_32
void ff_x264_deblock_v8_luma_intra_mmxext(uint8_t *pix, int stride, int alpha, int beta);
static void ff_x264_deblock_v_luma_intra_mmxext(uint8_t *pix, int stride, int alpha, int beta)
{
    ff_x264_deblock_v8_luma_intra_mmxext(pix+0, stride, alpha, beta);
    ff_x264_deblock_v8_luma_intra_mmxext(pix+8, stride, alpha, beta);
}
#endif

void ff_h264dsp_init_x86(H264DSPContext *c)
{
    int mm_flags = mm_support();

    if (mm_flags & FF_MM_MMX) {
        c->h264_idct_dc_add=
        c->h264_idct_add= ff_h264_idct_add_mmx;
        c->h264_idct8_dc_add=
        c->h264_idct8_add= ff_h264_idct8_add_mmx;

        c->h264_idct_add16     = ff_h264_idct_add16_mmx;
        c->h264_idct8_add4     = ff_h264_idct8_add4_mmx;
        c->h264_idct_add8      = ff_h264_idct_add8_mmx;
        c->h264_idct_add16intra= ff_h264_idct_add16intra_mmx;

        if (mm_flags & FF_MM_MMX2) {
            c->h264_idct_dc_add= ff_h264_idct_dc_add_mmx2;
            c->h264_idct8_dc_add= ff_h264_idct8_dc_add_mmx2;
            c->h264_idct_add16     = ff_h264_idct_add16_mmx2;
            c->h264_idct8_add4     = ff_h264_idct8_add4_mmx2;
            c->h264_idct_add8      = ff_h264_idct_add8_mmx2;
            c->h264_idct_add16intra= ff_h264_idct_add16intra_mmx2;

            c->h264_v_loop_filter_luma= h264_v_loop_filter_luma_mmx2;
            c->h264_h_loop_filter_luma= h264_h_loop_filter_luma_mmx2;
            c->h264_v_loop_filter_chroma= h264_v_loop_filter_chroma_mmx2;
            c->h264_h_loop_filter_chroma= h264_h_loop_filter_chroma_mmx2;
            c->h264_v_loop_filter_chroma_intra= h264_v_loop_filter_chroma_intra_mmx2;
            c->h264_h_loop_filter_chroma_intra= h264_h_loop_filter_chroma_intra_mmx2;
            c->h264_loop_filter_strength= h264_loop_filter_strength_mmx2;
        }
        if(mm_flags & FF_MM_SSE2){
            c->h264_idct8_add = ff_h264_idct8_add_sse2;
            c->h264_idct8_add4= ff_h264_idct8_add4_sse2;
        }

#if HAVE_YASM
        if (mm_flags & FF_MM_MMX2){
#if ARCH_X86_32
            c->h264_v_loop_filter_luma_intra = ff_x264_deblock_v_luma_intra_mmxext;
            c->h264_h_loop_filter_luma_intra = ff_x264_deblock_h_luma_intra_mmxext;
#endif
            c->weight_h264_pixels_tab[0]= ff_h264_weight_16x16_mmx2;
            c->weight_h264_pixels_tab[1]= ff_h264_weight_16x8_mmx2;
            c->weight_h264_pixels_tab[2]= ff_h264_weight_8x16_mmx2;
            c->weight_h264_pixels_tab[3]= ff_h264_weight_8x8_mmx2;
            c->weight_h264_pixels_tab[4]= ff_h264_weight_8x4_mmx2;
            c->weight_h264_pixels_tab[5]= ff_h264_weight_4x8_mmx2;
            c->weight_h264_pixels_tab[6]= ff_h264_weight_4x4_mmx2;
            c->weight_h264_pixels_tab[7]= ff_h264_weight_4x2_mmx2;

            c->biweight_h264_pixels_tab[0]= ff_h264_biweight_16x16_mmx2;
            c->biweight_h264_pixels_tab[1]= ff_h264_biweight_16x8_mmx2;
            c->biweight_h264_pixels_tab[2]= ff_h264_biweight_8x16_mmx2;
            c->biweight_h264_pixels_tab[3]= ff_h264_biweight_8x8_mmx2;
            c->biweight_h264_pixels_tab[4]= ff_h264_biweight_8x4_mmx2;
            c->biweight_h264_pixels_tab[5]= ff_h264_biweight_4x8_mmx2;
            c->biweight_h264_pixels_tab[6]= ff_h264_biweight_4x4_mmx2;
            c->biweight_h264_pixels_tab[7]= ff_h264_biweight_4x2_mmx2;

            if( mm_flags&FF_MM_SSE2 ){
                c->weight_h264_pixels_tab[0]= ff_h264_weight_16x16_sse2;
                c->weight_h264_pixels_tab[1]= ff_h264_weight_16x8_sse2;
                c->weight_h264_pixels_tab[2]= ff_h264_weight_8x16_sse2;
                c->weight_h264_pixels_tab[3]= ff_h264_weight_8x8_sse2;
                c->weight_h264_pixels_tab[4]= ff_h264_weight_8x4_sse2;

                c->biweight_h264_pixels_tab[0]= ff_h264_biweight_16x16_sse2;
                c->biweight_h264_pixels_tab[1]= ff_h264_biweight_16x8_sse2;
                c->biweight_h264_pixels_tab[2]= ff_h264_biweight_8x16_sse2;
                c->biweight_h264_pixels_tab[3]= ff_h264_biweight_8x8_sse2;
                c->biweight_h264_pixels_tab[4]= ff_h264_biweight_8x4_sse2;

#if ARCH_X86_64 || !defined(__ICC) || __ICC > 1110
                c->h264_v_loop_filter_luma = ff_x264_deblock_v_luma_sse2;
                c->h264_h_loop_filter_luma = ff_x264_deblock_h_luma_sse2;
                c->h264_v_loop_filter_luma_intra = ff_x264_deblock_v_luma_intra_sse2;
                c->h264_h_loop_filter_luma_intra = ff_x264_deblock_h_luma_intra_sse2;
#endif
#if CONFIG_GPL
                c->h264_idct_add16 = ff_h264_idct_add16_sse2;
                c->h264_idct_add8  = ff_h264_idct_add8_sse2;
                c->h264_idct_add16intra = ff_h264_idct_add16intra_sse2;
#endif
            }
            if ( mm_flags&FF_MM_SSSE3 ){
                c->biweight_h264_pixels_tab[0]= ff_h264_biweight_16x16_ssse3;
                c->biweight_h264_pixels_tab[1]= ff_h264_biweight_16x8_ssse3;
                c->biweight_h264_pixels_tab[2]= ff_h264_biweight_8x16_ssse3;
                c->biweight_h264_pixels_tab[3]= ff_h264_biweight_8x8_ssse3;
                c->biweight_h264_pixels_tab[4]= ff_h264_biweight_8x4_ssse3;
            }
        }
#endif
    }
}
