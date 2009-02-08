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

#include "dsputil_mmx.h"

DECLARE_ALIGNED_8 (static const uint64_t, ff_pb_3_1  ) = 0x0103010301030103ULL;
DECLARE_ALIGNED_8 (static const uint64_t, ff_pb_7_3  ) = 0x0307030703070307ULL;

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
    int16_t __attribute__ ((aligned(8))) b2[64];

    block[0] += 32;

    for(i=0; i<2; i++){
        DECLARE_ALIGNED_8(uint64_t, tmp);

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

    add_pixels_clamped_mmx(b2, dst, stride);
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
        "pand     %8,     "#tmp"   \n\t" /* (p2^avg(p0,q0))&1 */\
        "psubusb  "#tmp", "#q2"    \n\t" /* (p2+((p0+q0+1)>>1))>>1 */\
        "movq     "#p1",  "#tmp"   \n\t"\
        "psubusb  "#tc0", "#tmp"   \n\t"\
        "paddusb  "#p1",  "#tc0"   \n\t"\
        "pmaxub   "#tmp", "#q2"    \n\t"\
        "pminub   "#tc0", "#q2"    \n\t"\
        "movq     "#q2",  "q1addr" \n\t"

static inline void h264_loop_filter_luma_mmx2(uint8_t *pix, int stride, int alpha1, int beta1, int8_t *tc0)
{
    DECLARE_ALIGNED_8(uint64_t, tmp0[2]);

    __asm__ volatile(
        "movq    (%1,%3), %%mm0    \n\t" //p1
        "movq    (%1,%3,2), %%mm1  \n\t" //p0
        "movq    (%2),    %%mm2    \n\t" //q0
        "movq    (%2,%3), %%mm3    \n\t" //q1
        H264_DEBLOCK_MASK(%6, %7)

        "movd      %5,    %%mm4    \n\t"
        "punpcklbw %%mm4, %%mm4    \n\t"
        "punpcklwd %%mm4, %%mm4    \n\t"
        "pcmpeqb   %%mm3, %%mm3    \n\t"
        "movq      %%mm4, %%mm6    \n\t"
        "pcmpgtb   %%mm3, %%mm4    \n\t"
        "movq      %%mm6, 8+%0     \n\t"
        "pand      %%mm4, %%mm7    \n\t"
        "movq      %%mm7, %0       \n\t"

        /* filter p1 */
        "movq     (%1),   %%mm3    \n\t" //p2
        DIFF_GT2_MMX(%%mm1, %%mm3, %%mm5, %%mm6, %%mm4) // |p2-p0|>beta-1
        "pand     %%mm7,  %%mm6    \n\t" // mask & |p2-p0|<beta
        "pand     8+%0,   %%mm7    \n\t" // mask & tc0
        "movq     %%mm7,  %%mm4    \n\t"
        "psubb    %%mm6,  %%mm7    \n\t"
        "pand     %%mm4,  %%mm6    \n\t" // mask & |p2-p0|<beta & tc0
        H264_DEBLOCK_Q1(%%mm0, %%mm3, "(%1)", "(%1,%3)", %%mm6, %%mm4)

        /* filter q1 */
        "movq    (%2,%3,2), %%mm4  \n\t" //q2
        DIFF_GT2_MMX(%%mm2, %%mm4, %%mm5, %%mm6, %%mm3) // |q2-q0|>beta-1
        "pand     %0,     %%mm6    \n\t"
        "movq     8+%0,   %%mm5    \n\t" // can be merged with the and below but is slower then
        "pand     %%mm6,  %%mm5    \n\t"
        "psubb    %%mm6,  %%mm7    \n\t"
        "movq    (%2,%3), %%mm3    \n\t"
        H264_DEBLOCK_Q1(%%mm3, %%mm4, "(%2,%3,2)", "(%2,%3)", %%mm5, %%mm6)

        /* filter p0, q0 */
        H264_DEBLOCK_P0_Q0(%8, unused)
        "movq      %%mm1, (%1,%3,2) \n\t"
        "movq      %%mm2, (%2)      \n\t"

        : "=m"(*tmp0)
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
    DECLARE_ALIGNED_8(uint8_t, trans[8*8]);
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
    DECLARE_ALIGNED_8(uint8_t, trans[8*4]);
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
    DECLARE_ALIGNED_8(uint8_t, trans[8*4]);
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
        "pxor %%mm7, %%mm7 \n\t"
        "movq %0, %%mm6 \n\t"
        "movq %1, %%mm5 \n\t"
        "movq %2, %%mm4 \n\t"
        ::"m"(ff_pb_1), "m"(ff_pb_3), "m"(ff_pb_7)
    );
    if(field)
        __asm__ volatile(
            "movq %0, %%mm5 \n\t"
            "movq %1, %%mm4 \n\t"
            ::"m"(ff_pb_3_1), "m"(ff_pb_7_3)
        );

    // could do a special case for dir==0 && edges==1, but it only reduces the
    // average filter time by 1.2%
    for( dir=1; dir>=0; dir-- ) {
        const int d_idx = dir ? -8 : -1;
        const int mask_mv = dir ? mask_mv1 : mask_mv0;
        DECLARE_ALIGNED_8(const uint64_t, mask_dir) = dir ? 0 : 0xffffffffffffffffULL;
        int b_idx, edge, l;
        for( b_idx=12, edge=0; edge<edges; edge+=step, b_idx+=8*step ) {
            __asm__ volatile(
                "pand %0, %%mm0 \n\t"
                ::"m"(mask_dir)
            );
            if(!(mask_mv & edge)) {
                __asm__ volatile("pxor %%mm0, %%mm0 \n\t":);
                for( l = bidir; l >= 0; l-- ) {
                    __asm__ volatile(
                        "movd %0, %%mm1 \n\t"
                        "punpckldq %1, %%mm1 \n\t"
                        "movq %%mm1, %%mm2 \n\t"
                        "psrlw $7, %%mm2 \n\t"
                        "pand %%mm6, %%mm2 \n\t"
                        "por %%mm2, %%mm1 \n\t" // ref_cache with -2 mapped to -1
                        "punpckldq %%mm1, %%mm2 \n\t"
                        "pcmpeqb %%mm2, %%mm1 \n\t"
                        "paddb %%mm6, %%mm1 \n\t"
                        "punpckhbw %%mm7, %%mm1 \n\t" // ref[b] != ref[bn]
                        "por %%mm1, %%mm0 \n\t"

                        "movq %2, %%mm1 \n\t"
                        "movq %3, %%mm2 \n\t"
                        "psubw %4, %%mm1 \n\t"
                        "psubw %5, %%mm2 \n\t"
                        "packsswb %%mm2, %%mm1 \n\t"
                        "paddb %%mm5, %%mm1 \n\t"
                        "pminub %%mm4, %%mm1 \n\t"
                        "pcmpeqb %%mm4, %%mm1 \n\t" // abs(mv[b] - mv[bn]) >= limit
                        "por %%mm1, %%mm0 \n\t"
                        ::"m"(ref[l][b_idx]),
                          "m"(ref[l][b_idx+d_idx]),
                          "m"(mv[l][b_idx][0]),
                          "m"(mv[l][b_idx+2][0]),
                          "m"(mv[l][b_idx+d_idx][0]),
                          "m"(mv[l][b_idx+d_idx+2][0])
                    );
                }
            }
            __asm__ volatile(
                "movd %0, %%mm1 \n\t"
                "por  %1, %%mm1 \n\t"
                "punpcklbw %%mm7, %%mm1 \n\t"
                "pcmpgtw %%mm7, %%mm1 \n\t" // nnz[b] || nnz[bn]
                ::"m"(nnz[b_idx]),
                  "m"(nnz[b_idx+d_idx])
            );
            __asm__ volatile(
                "pcmpeqw %%mm7, %%mm0 \n\t"
                "pcmpeqw %%mm7, %%mm0 \n\t"
                "psrlw $15, %%mm0 \n\t" // nonzero -> 1
                "psrlw $14, %%mm1 \n\t"
                "movq %%mm0, %%mm2 \n\t"
                "por %%mm1, %%mm2 \n\t"
                "psrlw $1, %%mm1 \n\t"
                "pandn %%mm2, %%mm1 \n\t"
                "movq %%mm1, %0 \n\t"
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
/* motion compensation */

#define QPEL_H264V_MM(A,B,C,D,E,F,OP,T,Z,d,q)\
        "mov"#q" "#C", "#T"         \n\t"\
        "mov"#d" (%0), "#F"         \n\t"\
        "paddw "#D", "#T"           \n\t"\
        "psllw $2, "#T"             \n\t"\
        "psubw "#B", "#T"           \n\t"\
        "psubw "#E", "#T"           \n\t"\
        "punpcklbw "#Z", "#F"       \n\t"\
        "pmullw %4, "#T"            \n\t"\
        "paddw %5, "#A"             \n\t"\
        "add %2, %0                 \n\t"\
        "paddw "#F", "#A"           \n\t"\
        "paddw "#A", "#T"           \n\t"\
        "psraw $5, "#T"             \n\t"\
        "packuswb "#T", "#T"        \n\t"\
        OP(T, (%1), A, d)\
        "add %3, %1                 \n\t"

#define QPEL_H264HV_MM(A,B,C,D,E,F,OF,T,Z,d,q)\
        "mov"#q" "#C", "#T"         \n\t"\
        "mov"#d" (%0), "#F"         \n\t"\
        "paddw "#D", "#T"           \n\t"\
        "psllw $2, "#T"             \n\t"\
        "paddw %4, "#A"             \n\t"\
        "psubw "#B", "#T"           \n\t"\
        "psubw "#E", "#T"           \n\t"\
        "punpcklbw "#Z", "#F"       \n\t"\
        "pmullw %3, "#T"            \n\t"\
        "paddw "#F", "#A"           \n\t"\
        "add %2, %0                 \n\t"\
        "paddw "#A", "#T"           \n\t"\
        "mov"#q" "#T", "#OF"(%1)    \n\t"

#define QPEL_H264V(A,B,C,D,E,F,OP) QPEL_H264V_MM(A,B,C,D,E,F,OP,%%mm6,%%mm7,d,q)
#define QPEL_H264HV(A,B,C,D,E,F,OF) QPEL_H264HV_MM(A,B,C,D,E,F,OF,%%mm6,%%mm7,d,q)
#define QPEL_H264V_XMM(A,B,C,D,E,F,OP) QPEL_H264V_MM(A,B,C,D,E,F,OP,%%xmm6,%%xmm7,q,dqa)
#define QPEL_H264HV_XMM(A,B,C,D,E,F,OF) QPEL_H264HV_MM(A,B,C,D,E,F,OF,%%xmm6,%%xmm7,q,dqa)


#define QPEL_H264(OPNAME, OP, MMX)\
static av_noinline void OPNAME ## h264_qpel4_h_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    int h=4;\
\
    __asm__ volatile(\
        "pxor %%mm7, %%mm7          \n\t"\
        "movq %5, %%mm4             \n\t"\
        "movq %6, %%mm5             \n\t"\
        "1:                         \n\t"\
        "movd  -1(%0), %%mm1        \n\t"\
        "movd    (%0), %%mm2        \n\t"\
        "movd   1(%0), %%mm3        \n\t"\
        "movd   2(%0), %%mm0        \n\t"\
        "punpcklbw %%mm7, %%mm1     \n\t"\
        "punpcklbw %%mm7, %%mm2     \n\t"\
        "punpcklbw %%mm7, %%mm3     \n\t"\
        "punpcklbw %%mm7, %%mm0     \n\t"\
        "paddw %%mm0, %%mm1         \n\t"\
        "paddw %%mm3, %%mm2         \n\t"\
        "movd  -2(%0), %%mm0        \n\t"\
        "movd   3(%0), %%mm3        \n\t"\
        "punpcklbw %%mm7, %%mm0     \n\t"\
        "punpcklbw %%mm7, %%mm3     \n\t"\
        "paddw %%mm3, %%mm0         \n\t"\
        "psllw $2, %%mm2            \n\t"\
        "psubw %%mm1, %%mm2         \n\t"\
        "pmullw %%mm4, %%mm2        \n\t"\
        "paddw %%mm5, %%mm0         \n\t"\
        "paddw %%mm2, %%mm0         \n\t"\
        "psraw $5, %%mm0            \n\t"\
        "packuswb %%mm0, %%mm0      \n\t"\
        OP(%%mm0, (%1),%%mm6, d)\
        "add %3, %0                 \n\t"\
        "add %4, %1                 \n\t"\
        "decl %2                    \n\t"\
        " jnz 1b                    \n\t"\
        : "+a"(src), "+c"(dst), "+g"(h)\
        : "d"((x86_reg)srcStride), "S"((x86_reg)dstStride), "m"(ff_pw_5), "m"(ff_pw_16)\
        : "memory"\
    );\
}\
static av_noinline void OPNAME ## h264_qpel4_h_lowpass_l2_ ## MMX(uint8_t *dst, uint8_t *src, uint8_t *src2, int dstStride, int src2Stride){\
    int h=4;\
    __asm__ volatile(\
        "pxor %%mm7, %%mm7          \n\t"\
        "movq %0, %%mm4             \n\t"\
        "movq %1, %%mm5             \n\t"\
        :: "m"(ff_pw_5), "m"(ff_pw_16)\
    );\
    do{\
    __asm__ volatile(\
        "movd  -1(%0), %%mm1        \n\t"\
        "movd    (%0), %%mm2        \n\t"\
        "movd   1(%0), %%mm3        \n\t"\
        "movd   2(%0), %%mm0        \n\t"\
        "punpcklbw %%mm7, %%mm1     \n\t"\
        "punpcklbw %%mm7, %%mm2     \n\t"\
        "punpcklbw %%mm7, %%mm3     \n\t"\
        "punpcklbw %%mm7, %%mm0     \n\t"\
        "paddw %%mm0, %%mm1         \n\t"\
        "paddw %%mm3, %%mm2         \n\t"\
        "movd  -2(%0), %%mm0        \n\t"\
        "movd   3(%0), %%mm3        \n\t"\
        "punpcklbw %%mm7, %%mm0     \n\t"\
        "punpcklbw %%mm7, %%mm3     \n\t"\
        "paddw %%mm3, %%mm0         \n\t"\
        "psllw $2, %%mm2            \n\t"\
        "psubw %%mm1, %%mm2         \n\t"\
        "pmullw %%mm4, %%mm2        \n\t"\
        "paddw %%mm5, %%mm0         \n\t"\
        "paddw %%mm2, %%mm0         \n\t"\
        "movd   (%2), %%mm3         \n\t"\
        "psraw $5, %%mm0            \n\t"\
        "packuswb %%mm0, %%mm0      \n\t"\
        PAVGB" %%mm3, %%mm0         \n\t"\
        OP(%%mm0, (%1),%%mm6, d)\
        "add %4, %0                 \n\t"\
        "add %4, %1                 \n\t"\
        "add %3, %2                 \n\t"\
        : "+a"(src), "+c"(dst), "+d"(src2)\
        : "D"((x86_reg)src2Stride), "S"((x86_reg)dstStride)\
        : "memory"\
    );\
    }while(--h);\
}\
static av_noinline void OPNAME ## h264_qpel4_v_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    src -= 2*srcStride;\
    __asm__ volatile(\
        "pxor %%mm7, %%mm7          \n\t"\
        "movd (%0), %%mm0           \n\t"\
        "add %2, %0                 \n\t"\
        "movd (%0), %%mm1           \n\t"\
        "add %2, %0                 \n\t"\
        "movd (%0), %%mm2           \n\t"\
        "add %2, %0                 \n\t"\
        "movd (%0), %%mm3           \n\t"\
        "add %2, %0                 \n\t"\
        "movd (%0), %%mm4           \n\t"\
        "add %2, %0                 \n\t"\
        "punpcklbw %%mm7, %%mm0     \n\t"\
        "punpcklbw %%mm7, %%mm1     \n\t"\
        "punpcklbw %%mm7, %%mm2     \n\t"\
        "punpcklbw %%mm7, %%mm3     \n\t"\
        "punpcklbw %%mm7, %%mm4     \n\t"\
        QPEL_H264V(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, OP)\
        QPEL_H264V(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, OP)\
        QPEL_H264V(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, OP)\
        QPEL_H264V(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, OP)\
         \
        : "+a"(src), "+c"(dst)\
        : "S"((x86_reg)srcStride), "D"((x86_reg)dstStride), "m"(ff_pw_5), "m"(ff_pw_16)\
        : "memory"\
    );\
}\
static av_noinline void OPNAME ## h264_qpel4_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, uint8_t *src, int dstStride, int tmpStride, int srcStride){\
    int h=4;\
    int w=3;\
    src -= 2*srcStride+2;\
    while(w--){\
        __asm__ volatile(\
            "pxor %%mm7, %%mm7      \n\t"\
            "movd (%0), %%mm0       \n\t"\
            "add %2, %0             \n\t"\
            "movd (%0), %%mm1       \n\t"\
            "add %2, %0             \n\t"\
            "movd (%0), %%mm2       \n\t"\
            "add %2, %0             \n\t"\
            "movd (%0), %%mm3       \n\t"\
            "add %2, %0             \n\t"\
            "movd (%0), %%mm4       \n\t"\
            "add %2, %0             \n\t"\
            "punpcklbw %%mm7, %%mm0 \n\t"\
            "punpcklbw %%mm7, %%mm1 \n\t"\
            "punpcklbw %%mm7, %%mm2 \n\t"\
            "punpcklbw %%mm7, %%mm3 \n\t"\
            "punpcklbw %%mm7, %%mm4 \n\t"\
            QPEL_H264HV(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, 0*8*3)\
            QPEL_H264HV(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, 1*8*3)\
            QPEL_H264HV(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, 2*8*3)\
            QPEL_H264HV(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, 3*8*3)\
             \
            : "+a"(src)\
            : "c"(tmp), "S"((x86_reg)srcStride), "m"(ff_pw_5), "m"(ff_pw_16)\
            : "memory"\
        );\
        tmp += 4;\
        src += 4 - 9*srcStride;\
    }\
    tmp -= 3*4;\
    __asm__ volatile(\
        "1:                         \n\t"\
        "movq     (%0), %%mm0       \n\t"\
        "paddw  10(%0), %%mm0       \n\t"\
        "movq    2(%0), %%mm1       \n\t"\
        "paddw   8(%0), %%mm1       \n\t"\
        "movq    4(%0), %%mm2       \n\t"\
        "paddw   6(%0), %%mm2       \n\t"\
        "psubw %%mm1, %%mm0         \n\t"/*a-b   (abccba)*/\
        "psraw $2, %%mm0            \n\t"/*(a-b)/4 */\
        "psubw %%mm1, %%mm0         \n\t"/*(a-b)/4-b */\
        "paddsw %%mm2, %%mm0        \n\t"\
        "psraw $2, %%mm0            \n\t"/*((a-b)/4-b+c)/4 */\
        "paddw %%mm2, %%mm0         \n\t"/*(a-5*b+20*c)/16 */\
        "psraw $6, %%mm0            \n\t"\
        "packuswb %%mm0, %%mm0      \n\t"\
        OP(%%mm0, (%1),%%mm7, d)\
        "add $24, %0                \n\t"\
        "add %3, %1                 \n\t"\
        "decl %2                    \n\t"\
        " jnz 1b                    \n\t"\
        : "+a"(tmp), "+c"(dst), "+g"(h)\
        : "S"((x86_reg)dstStride)\
        : "memory"\
    );\
}\
\
static av_noinline void OPNAME ## h264_qpel8_h_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    int h=8;\
    __asm__ volatile(\
        "pxor %%mm7, %%mm7          \n\t"\
        "movq %5, %%mm6             \n\t"\
        "1:                         \n\t"\
        "movq    (%0), %%mm0        \n\t"\
        "movq   1(%0), %%mm2        \n\t"\
        "movq %%mm0, %%mm1          \n\t"\
        "movq %%mm2, %%mm3          \n\t"\
        "punpcklbw %%mm7, %%mm0     \n\t"\
        "punpckhbw %%mm7, %%mm1     \n\t"\
        "punpcklbw %%mm7, %%mm2     \n\t"\
        "punpckhbw %%mm7, %%mm3     \n\t"\
        "paddw %%mm2, %%mm0         \n\t"\
        "paddw %%mm3, %%mm1         \n\t"\
        "psllw $2, %%mm0            \n\t"\
        "psllw $2, %%mm1            \n\t"\
        "movq   -1(%0), %%mm2       \n\t"\
        "movq    2(%0), %%mm4       \n\t"\
        "movq %%mm2, %%mm3          \n\t"\
        "movq %%mm4, %%mm5          \n\t"\
        "punpcklbw %%mm7, %%mm2     \n\t"\
        "punpckhbw %%mm7, %%mm3     \n\t"\
        "punpcklbw %%mm7, %%mm4     \n\t"\
        "punpckhbw %%mm7, %%mm5     \n\t"\
        "paddw %%mm4, %%mm2         \n\t"\
        "paddw %%mm3, %%mm5         \n\t"\
        "psubw %%mm2, %%mm0         \n\t"\
        "psubw %%mm5, %%mm1         \n\t"\
        "pmullw %%mm6, %%mm0        \n\t"\
        "pmullw %%mm6, %%mm1        \n\t"\
        "movd   -2(%0), %%mm2       \n\t"\
        "movd    7(%0), %%mm5       \n\t"\
        "punpcklbw %%mm7, %%mm2     \n\t"\
        "punpcklbw %%mm7, %%mm5     \n\t"\
        "paddw %%mm3, %%mm2         \n\t"\
        "paddw %%mm5, %%mm4         \n\t"\
        "movq %6, %%mm5             \n\t"\
        "paddw %%mm5, %%mm2         \n\t"\
        "paddw %%mm5, %%mm4         \n\t"\
        "paddw %%mm2, %%mm0         \n\t"\
        "paddw %%mm4, %%mm1         \n\t"\
        "psraw $5, %%mm0            \n\t"\
        "psraw $5, %%mm1            \n\t"\
        "packuswb %%mm1, %%mm0      \n\t"\
        OP(%%mm0, (%1),%%mm5, q)\
        "add %3, %0                 \n\t"\
        "add %4, %1                 \n\t"\
        "decl %2                    \n\t"\
        " jnz 1b                    \n\t"\
        : "+a"(src), "+c"(dst), "+g"(h)\
        : "d"((x86_reg)srcStride), "S"((x86_reg)dstStride), "m"(ff_pw_5), "m"(ff_pw_16)\
        : "memory"\
    );\
}\
\
static av_noinline void OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(uint8_t *dst, uint8_t *src, uint8_t *src2, int dstStride, int src2Stride){\
    int h=8;\
    __asm__ volatile(\
        "pxor %%mm7, %%mm7          \n\t"\
        "movq %0, %%mm6             \n\t"\
        :: "m"(ff_pw_5)\
    );\
    do{\
    __asm__ volatile(\
        "movq    (%0), %%mm0        \n\t"\
        "movq   1(%0), %%mm2        \n\t"\
        "movq %%mm0, %%mm1          \n\t"\
        "movq %%mm2, %%mm3          \n\t"\
        "punpcklbw %%mm7, %%mm0     \n\t"\
        "punpckhbw %%mm7, %%mm1     \n\t"\
        "punpcklbw %%mm7, %%mm2     \n\t"\
        "punpckhbw %%mm7, %%mm3     \n\t"\
        "paddw %%mm2, %%mm0         \n\t"\
        "paddw %%mm3, %%mm1         \n\t"\
        "psllw $2, %%mm0            \n\t"\
        "psllw $2, %%mm1            \n\t"\
        "movq   -1(%0), %%mm2       \n\t"\
        "movq    2(%0), %%mm4       \n\t"\
        "movq %%mm2, %%mm3          \n\t"\
        "movq %%mm4, %%mm5          \n\t"\
        "punpcklbw %%mm7, %%mm2     \n\t"\
        "punpckhbw %%mm7, %%mm3     \n\t"\
        "punpcklbw %%mm7, %%mm4     \n\t"\
        "punpckhbw %%mm7, %%mm5     \n\t"\
        "paddw %%mm4, %%mm2         \n\t"\
        "paddw %%mm3, %%mm5         \n\t"\
        "psubw %%mm2, %%mm0         \n\t"\
        "psubw %%mm5, %%mm1         \n\t"\
        "pmullw %%mm6, %%mm0        \n\t"\
        "pmullw %%mm6, %%mm1        \n\t"\
        "movd   -2(%0), %%mm2       \n\t"\
        "movd    7(%0), %%mm5       \n\t"\
        "punpcklbw %%mm7, %%mm2     \n\t"\
        "punpcklbw %%mm7, %%mm5     \n\t"\
        "paddw %%mm3, %%mm2         \n\t"\
        "paddw %%mm5, %%mm4         \n\t"\
        "movq %5, %%mm5             \n\t"\
        "paddw %%mm5, %%mm2         \n\t"\
        "paddw %%mm5, %%mm4         \n\t"\
        "paddw %%mm2, %%mm0         \n\t"\
        "paddw %%mm4, %%mm1         \n\t"\
        "psraw $5, %%mm0            \n\t"\
        "psraw $5, %%mm1            \n\t"\
        "movq (%2), %%mm4           \n\t"\
        "packuswb %%mm1, %%mm0      \n\t"\
        PAVGB" %%mm4, %%mm0         \n\t"\
        OP(%%mm0, (%1),%%mm5, q)\
        "add %4, %0                 \n\t"\
        "add %4, %1                 \n\t"\
        "add %3, %2                 \n\t"\
        : "+a"(src), "+c"(dst), "+d"(src2)\
        : "D"((x86_reg)src2Stride), "S"((x86_reg)dstStride),\
          "m"(ff_pw_16)\
        : "memory"\
    );\
    }while(--h);\
}\
\
static av_noinline void OPNAME ## h264_qpel8or16_v_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride, int h){\
    int w= 2;\
    src -= 2*srcStride;\
    \
    while(w--){\
      __asm__ volatile(\
        "pxor %%mm7, %%mm7          \n\t"\
        "movd (%0), %%mm0           \n\t"\
        "add %2, %0                 \n\t"\
        "movd (%0), %%mm1           \n\t"\
        "add %2, %0                 \n\t"\
        "movd (%0), %%mm2           \n\t"\
        "add %2, %0                 \n\t"\
        "movd (%0), %%mm3           \n\t"\
        "add %2, %0                 \n\t"\
        "movd (%0), %%mm4           \n\t"\
        "add %2, %0                 \n\t"\
        "punpcklbw %%mm7, %%mm0     \n\t"\
        "punpcklbw %%mm7, %%mm1     \n\t"\
        "punpcklbw %%mm7, %%mm2     \n\t"\
        "punpcklbw %%mm7, %%mm3     \n\t"\
        "punpcklbw %%mm7, %%mm4     \n\t"\
        QPEL_H264V(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, OP)\
        QPEL_H264V(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, OP)\
        QPEL_H264V(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, OP)\
        QPEL_H264V(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, OP)\
        QPEL_H264V(%%mm4, %%mm5, %%mm0, %%mm1, %%mm2, %%mm3, OP)\
        QPEL_H264V(%%mm5, %%mm0, %%mm1, %%mm2, %%mm3, %%mm4, OP)\
        QPEL_H264V(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, OP)\
        QPEL_H264V(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, OP)\
         \
        : "+a"(src), "+c"(dst)\
        : "S"((x86_reg)srcStride), "D"((x86_reg)dstStride), "m"(ff_pw_5), "m"(ff_pw_16)\
        : "memory"\
     );\
     if(h==16){\
        __asm__ volatile(\
            QPEL_H264V(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, OP)\
            QPEL_H264V(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, OP)\
            QPEL_H264V(%%mm4, %%mm5, %%mm0, %%mm1, %%mm2, %%mm3, OP)\
            QPEL_H264V(%%mm5, %%mm0, %%mm1, %%mm2, %%mm3, %%mm4, OP)\
            QPEL_H264V(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, OP)\
            QPEL_H264V(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, OP)\
            QPEL_H264V(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, OP)\
            QPEL_H264V(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, OP)\
            \
           : "+a"(src), "+c"(dst)\
           : "S"((x86_reg)srcStride), "D"((x86_reg)dstStride), "m"(ff_pw_5), "m"(ff_pw_16)\
           : "memory"\
        );\
     }\
     src += 4-(h+5)*srcStride;\
     dst += 4-h*dstStride;\
   }\
}\
static av_always_inline void OPNAME ## h264_qpel8or16_hv1_lowpass_ ## MMX(int16_t *tmp, uint8_t *src, int tmpStride, int srcStride, int size){\
    int w = (size+8)>>2;\
    src -= 2*srcStride+2;\
    while(w--){\
        __asm__ volatile(\
            "pxor %%mm7, %%mm7      \n\t"\
            "movd (%0), %%mm0       \n\t"\
            "add %2, %0             \n\t"\
            "movd (%0), %%mm1       \n\t"\
            "add %2, %0             \n\t"\
            "movd (%0), %%mm2       \n\t"\
            "add %2, %0             \n\t"\
            "movd (%0), %%mm3       \n\t"\
            "add %2, %0             \n\t"\
            "movd (%0), %%mm4       \n\t"\
            "add %2, %0             \n\t"\
            "punpcklbw %%mm7, %%mm0 \n\t"\
            "punpcklbw %%mm7, %%mm1 \n\t"\
            "punpcklbw %%mm7, %%mm2 \n\t"\
            "punpcklbw %%mm7, %%mm3 \n\t"\
            "punpcklbw %%mm7, %%mm4 \n\t"\
            QPEL_H264HV(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, 0*48)\
            QPEL_H264HV(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, 1*48)\
            QPEL_H264HV(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, 2*48)\
            QPEL_H264HV(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, 3*48)\
            QPEL_H264HV(%%mm4, %%mm5, %%mm0, %%mm1, %%mm2, %%mm3, 4*48)\
            QPEL_H264HV(%%mm5, %%mm0, %%mm1, %%mm2, %%mm3, %%mm4, 5*48)\
            QPEL_H264HV(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, 6*48)\
            QPEL_H264HV(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, 7*48)\
            : "+a"(src)\
            : "c"(tmp), "S"((x86_reg)srcStride), "m"(ff_pw_5), "m"(ff_pw_16)\
            : "memory"\
        );\
        if(size==16){\
            __asm__ volatile(\
                QPEL_H264HV(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1,  8*48)\
                QPEL_H264HV(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2,  9*48)\
                QPEL_H264HV(%%mm4, %%mm5, %%mm0, %%mm1, %%mm2, %%mm3, 10*48)\
                QPEL_H264HV(%%mm5, %%mm0, %%mm1, %%mm2, %%mm3, %%mm4, 11*48)\
                QPEL_H264HV(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, 12*48)\
                QPEL_H264HV(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, 13*48)\
                QPEL_H264HV(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, 14*48)\
                QPEL_H264HV(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, 15*48)\
                : "+a"(src)\
                : "c"(tmp), "S"((x86_reg)srcStride), "m"(ff_pw_5), "m"(ff_pw_16)\
                : "memory"\
            );\
        }\
        tmp += 4;\
        src += 4 - (size+5)*srcStride;\
    }\
}\
static av_always_inline void OPNAME ## h264_qpel8or16_hv2_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, int dstStride, int tmpStride, int size){\
    int w = size>>4;\
    do{\
    int h = size;\
    __asm__ volatile(\
        "1:                         \n\t"\
        "movq     (%0), %%mm0       \n\t"\
        "movq    8(%0), %%mm3       \n\t"\
        "movq    2(%0), %%mm1       \n\t"\
        "movq   10(%0), %%mm4       \n\t"\
        "paddw   %%mm4, %%mm0       \n\t"\
        "paddw   %%mm3, %%mm1       \n\t"\
        "paddw  18(%0), %%mm3       \n\t"\
        "paddw  16(%0), %%mm4       \n\t"\
        "movq    4(%0), %%mm2       \n\t"\
        "movq   12(%0), %%mm5       \n\t"\
        "paddw   6(%0), %%mm2       \n\t"\
        "paddw  14(%0), %%mm5       \n\t"\
        "psubw %%mm1, %%mm0         \n\t"\
        "psubw %%mm4, %%mm3         \n\t"\
        "psraw $2, %%mm0            \n\t"\
        "psraw $2, %%mm3            \n\t"\
        "psubw %%mm1, %%mm0         \n\t"\
        "psubw %%mm4, %%mm3         \n\t"\
        "paddsw %%mm2, %%mm0        \n\t"\
        "paddsw %%mm5, %%mm3        \n\t"\
        "psraw $2, %%mm0            \n\t"\
        "psraw $2, %%mm3            \n\t"\
        "paddw %%mm2, %%mm0         \n\t"\
        "paddw %%mm5, %%mm3         \n\t"\
        "psraw $6, %%mm0            \n\t"\
        "psraw $6, %%mm3            \n\t"\
        "packuswb %%mm3, %%mm0      \n\t"\
        OP(%%mm0, (%1),%%mm7, q)\
        "add $48, %0                \n\t"\
        "add %3, %1                 \n\t"\
        "decl %2                    \n\t"\
        " jnz 1b                    \n\t"\
        : "+a"(tmp), "+c"(dst), "+g"(h)\
        : "S"((x86_reg)dstStride)\
        : "memory"\
    );\
    tmp += 8 - size*24;\
    dst += 8 - size*dstStride;\
    }while(w--);\
}\
\
static void OPNAME ## h264_qpel8_v_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    OPNAME ## h264_qpel8or16_v_lowpass_ ## MMX(dst  , src  , dstStride, srcStride, 8);\
}\
static av_noinline void OPNAME ## h264_qpel16_v_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    OPNAME ## h264_qpel8or16_v_lowpass_ ## MMX(dst  , src  , dstStride, srcStride, 16);\
    OPNAME ## h264_qpel8or16_v_lowpass_ ## MMX(dst+8, src+8, dstStride, srcStride, 16);\
}\
\
static void OPNAME ## h264_qpel16_h_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst  , src  , dstStride, srcStride);\
    OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst+8, src+8, dstStride, srcStride);\
    src += 8*srcStride;\
    dst += 8*dstStride;\
    OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst  , src  , dstStride, srcStride);\
    OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst+8, src+8, dstStride, srcStride);\
}\
\
static av_noinline void OPNAME ## h264_qpel16_h_lowpass_l2_ ## MMX(uint8_t *dst, uint8_t *src, uint8_t *src2, int dstStride, int src2Stride){\
    OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(dst  , src  , src2  , dstStride, src2Stride);\
    OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(dst+8, src+8, src2+8, dstStride, src2Stride);\
    src += 8*dstStride;\
    dst += 8*dstStride;\
    src2 += 8*src2Stride;\
    OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(dst  , src  , src2  , dstStride, src2Stride);\
    OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(dst+8, src+8, src2+8, dstStride, src2Stride);\
}\
\
static av_noinline void OPNAME ## h264_qpel8or16_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, uint8_t *src, int dstStride, int tmpStride, int srcStride, int size){\
          put_h264_qpel8or16_hv1_lowpass_ ## MMX(tmp, src, tmpStride, srcStride, size);\
    OPNAME ## h264_qpel8or16_hv2_lowpass_ ## MMX(dst, tmp, dstStride, tmpStride, size);\
}\
static void OPNAME ## h264_qpel8_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, uint8_t *src, int dstStride, int tmpStride, int srcStride){\
    OPNAME ## h264_qpel8or16_hv_lowpass_ ## MMX(dst  , tmp  , src  , dstStride, tmpStride, srcStride, 8);\
}\
\
static void OPNAME ## h264_qpel16_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, uint8_t *src, int dstStride, int tmpStride, int srcStride){\
    OPNAME ## h264_qpel8or16_hv_lowpass_ ## MMX(dst  , tmp  , src  , dstStride, tmpStride, srcStride, 16);\
}\
\
static av_noinline void OPNAME ## pixels4_l2_shift5_ ## MMX(uint8_t *dst, int16_t *src16, uint8_t *src8, int dstStride, int src8Stride, int h)\
{\
    __asm__ volatile(\
        "movq      (%1), %%mm0          \n\t"\
        "movq    24(%1), %%mm1          \n\t"\
        "psraw      $5,  %%mm0          \n\t"\
        "psraw      $5,  %%mm1          \n\t"\
        "packuswb %%mm0, %%mm0          \n\t"\
        "packuswb %%mm1, %%mm1          \n\t"\
        PAVGB"     (%0), %%mm0          \n\t"\
        PAVGB"  (%0,%3), %%mm1          \n\t"\
        OP(%%mm0, (%2),    %%mm4, d)\
        OP(%%mm1, (%2,%4), %%mm5, d)\
        "lea  (%0,%3,2), %0             \n\t"\
        "lea  (%2,%4,2), %2             \n\t"\
        "movq    48(%1), %%mm0          \n\t"\
        "movq    72(%1), %%mm1          \n\t"\
        "psraw      $5,  %%mm0          \n\t"\
        "psraw      $5,  %%mm1          \n\t"\
        "packuswb %%mm0, %%mm0          \n\t"\
        "packuswb %%mm1, %%mm1          \n\t"\
        PAVGB"     (%0), %%mm0          \n\t"\
        PAVGB"  (%0,%3), %%mm1          \n\t"\
        OP(%%mm0, (%2),    %%mm4, d)\
        OP(%%mm1, (%2,%4), %%mm5, d)\
        :"+a"(src8), "+c"(src16), "+d"(dst)\
        :"S"((x86_reg)src8Stride), "D"((x86_reg)dstStride)\
        :"memory");\
}\
static av_noinline void OPNAME ## pixels8_l2_shift5_ ## MMX(uint8_t *dst, int16_t *src16, uint8_t *src8, int dstStride, int src8Stride, int h)\
{\
    do{\
    __asm__ volatile(\
        "movq      (%1), %%mm0          \n\t"\
        "movq     8(%1), %%mm1          \n\t"\
        "movq    48(%1), %%mm2          \n\t"\
        "movq  8+48(%1), %%mm3          \n\t"\
        "psraw      $5,  %%mm0          \n\t"\
        "psraw      $5,  %%mm1          \n\t"\
        "psraw      $5,  %%mm2          \n\t"\
        "psraw      $5,  %%mm3          \n\t"\
        "packuswb %%mm1, %%mm0          \n\t"\
        "packuswb %%mm3, %%mm2          \n\t"\
        PAVGB"     (%0), %%mm0          \n\t"\
        PAVGB"  (%0,%3), %%mm2          \n\t"\
        OP(%%mm0, (%2), %%mm5, q)\
        OP(%%mm2, (%2,%4), %%mm5, q)\
        ::"a"(src8), "c"(src16), "d"(dst),\
          "r"((x86_reg)src8Stride), "r"((x86_reg)dstStride)\
        :"memory");\
        src8 += 2L*src8Stride;\
        src16 += 48;\
        dst += 2L*dstStride;\
    }while(h-=2);\
}\
static void OPNAME ## pixels16_l2_shift5_ ## MMX(uint8_t *dst, int16_t *src16, uint8_t *src8, int dstStride, int src8Stride, int h)\
{\
    OPNAME ## pixels8_l2_shift5_ ## MMX(dst  , src16  , src8  , dstStride, src8Stride, h);\
    OPNAME ## pixels8_l2_shift5_ ## MMX(dst+8, src16+8, src8+8, dstStride, src8Stride, h);\
}\


#if ARCH_X86_64
#define QPEL_H264_H16_XMM(OPNAME, OP, MMX)\
static av_noinline void OPNAME ## h264_qpel16_h_lowpass_l2_ ## MMX(uint8_t *dst, uint8_t *src, uint8_t *src2, int dstStride, int src2Stride){\
    int h=16;\
    __asm__ volatile(\
        "pxor %%xmm15, %%xmm15      \n\t"\
        "movdqa %6, %%xmm14         \n\t"\
        "movdqa %7, %%xmm13         \n\t"\
        "1:                         \n\t"\
        "lddqu    3(%0), %%xmm1     \n\t"\
        "lddqu   -5(%0), %%xmm7     \n\t"\
        "movdqa  %%xmm1, %%xmm0     \n\t"\
        "punpckhbw %%xmm15, %%xmm1  \n\t"\
        "punpcklbw %%xmm15, %%xmm0  \n\t"\
        "punpcklbw %%xmm15, %%xmm7  \n\t"\
        "movdqa  %%xmm1, %%xmm2     \n\t"\
        "movdqa  %%xmm0, %%xmm6     \n\t"\
        "movdqa  %%xmm1, %%xmm3     \n\t"\
        "movdqa  %%xmm0, %%xmm8     \n\t"\
        "movdqa  %%xmm1, %%xmm4     \n\t"\
        "movdqa  %%xmm0, %%xmm9     \n\t"\
        "movdqa  %%xmm1, %%xmm5     \n\t"\
        "movdqa  %%xmm0, %%xmm10    \n\t"\
        "palignr $6, %%xmm0, %%xmm5 \n\t"\
        "palignr $6, %%xmm7, %%xmm10\n\t"\
        "palignr $8, %%xmm0, %%xmm4 \n\t"\
        "palignr $8, %%xmm7, %%xmm9 \n\t"\
        "palignr $10,%%xmm0, %%xmm3 \n\t"\
        "palignr $10,%%xmm7, %%xmm8 \n\t"\
        "paddw   %%xmm1, %%xmm5     \n\t"\
        "paddw   %%xmm0, %%xmm10    \n\t"\
        "palignr $12,%%xmm0, %%xmm2 \n\t"\
        "palignr $12,%%xmm7, %%xmm6 \n\t"\
        "palignr $14,%%xmm0, %%xmm1 \n\t"\
        "palignr $14,%%xmm7, %%xmm0 \n\t"\
        "paddw   %%xmm3, %%xmm2     \n\t"\
        "paddw   %%xmm8, %%xmm6     \n\t"\
        "paddw   %%xmm4, %%xmm1     \n\t"\
        "paddw   %%xmm9, %%xmm0     \n\t"\
        "psllw   $2,     %%xmm2     \n\t"\
        "psllw   $2,     %%xmm6     \n\t"\
        "psubw   %%xmm1, %%xmm2     \n\t"\
        "psubw   %%xmm0, %%xmm6     \n\t"\
        "paddw   %%xmm13,%%xmm5     \n\t"\
        "paddw   %%xmm13,%%xmm10    \n\t"\
        "pmullw  %%xmm14,%%xmm2     \n\t"\
        "pmullw  %%xmm14,%%xmm6     \n\t"\
        "lddqu   (%2),   %%xmm3     \n\t"\
        "paddw   %%xmm5, %%xmm2     \n\t"\
        "paddw   %%xmm10,%%xmm6     \n\t"\
        "psraw   $5,     %%xmm2     \n\t"\
        "psraw   $5,     %%xmm6     \n\t"\
        "packuswb %%xmm2,%%xmm6     \n\t"\
        "pavgb   %%xmm3, %%xmm6     \n\t"\
        OP(%%xmm6, (%1), %%xmm4, dqa)\
        "add %5, %0                 \n\t"\
        "add %5, %1                 \n\t"\
        "add %4, %2                 \n\t"\
        "decl %3                    \n\t"\
        "jg 1b                      \n\t"\
        : "+a"(src), "+c"(dst), "+d"(src2), "+g"(h)\
        : "D"((x86_reg)src2Stride), "S"((x86_reg)dstStride),\
          "m"(ff_pw_5), "m"(ff_pw_16)\
        : "memory"\
    );\
}
#else // ARCH_X86_64
#define QPEL_H264_H16_XMM(OPNAME, OP, MMX)\
static av_noinline void OPNAME ## h264_qpel16_h_lowpass_l2_ ## MMX(uint8_t *dst, uint8_t *src, uint8_t *src2, int dstStride, int src2Stride){\
    OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(dst  , src  , src2  , dstStride, src2Stride);\
    OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(dst+8, src+8, src2+8, dstStride, src2Stride);\
    src += 8*dstStride;\
    dst += 8*dstStride;\
    src2 += 8*src2Stride;\
    OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(dst  , src  , src2  , dstStride, src2Stride);\
    OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(dst+8, src+8, src2+8, dstStride, src2Stride);\
}
#endif // ARCH_X86_64

#define QPEL_H264_H_XMM(OPNAME, OP, MMX)\
static av_noinline void OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(uint8_t *dst, uint8_t *src, uint8_t *src2, int dstStride, int src2Stride){\
    int h=8;\
    __asm__ volatile(\
        "pxor %%xmm7, %%xmm7        \n\t"\
        "movdqa %0, %%xmm6          \n\t"\
        :: "m"(ff_pw_5)\
    );\
    do{\
    __asm__ volatile(\
        "lddqu   -5(%0), %%xmm1     \n\t"\
        "movdqa  %%xmm1, %%xmm0     \n\t"\
        "punpckhbw %%xmm7, %%xmm1   \n\t"\
        "punpcklbw %%xmm7, %%xmm0   \n\t"\
        "movdqa  %%xmm1, %%xmm2     \n\t"\
        "movdqa  %%xmm1, %%xmm3     \n\t"\
        "movdqa  %%xmm1, %%xmm4     \n\t"\
        "movdqa  %%xmm1, %%xmm5     \n\t"\
        "palignr $6, %%xmm0, %%xmm5 \n\t"\
        "palignr $8, %%xmm0, %%xmm4 \n\t"\
        "palignr $10,%%xmm0, %%xmm3 \n\t"\
        "paddw   %%xmm1, %%xmm5     \n\t"\
        "palignr $12,%%xmm0, %%xmm2 \n\t"\
        "palignr $14,%%xmm0, %%xmm1 \n\t"\
        "paddw   %%xmm3, %%xmm2     \n\t"\
        "paddw   %%xmm4, %%xmm1     \n\t"\
        "psllw   $2,     %%xmm2     \n\t"\
        "movq    (%2),   %%xmm3     \n\t"\
        "psubw   %%xmm1, %%xmm2     \n\t"\
        "paddw   %5,     %%xmm5     \n\t"\
        "pmullw  %%xmm6, %%xmm2     \n\t"\
        "paddw   %%xmm5, %%xmm2     \n\t"\
        "psraw   $5,     %%xmm2     \n\t"\
        "packuswb %%xmm2, %%xmm2    \n\t"\
        "pavgb   %%xmm3, %%xmm2     \n\t"\
        OP(%%xmm2, (%1), %%xmm4, q)\
        "add %4, %0                 \n\t"\
        "add %4, %1                 \n\t"\
        "add %3, %2                 \n\t"\
        : "+a"(src), "+c"(dst), "+d"(src2)\
        : "D"((x86_reg)src2Stride), "S"((x86_reg)dstStride),\
          "m"(ff_pw_16)\
        : "memory"\
    );\
    }while(--h);\
}\
QPEL_H264_H16_XMM(OPNAME, OP, MMX)\
\
static av_noinline void OPNAME ## h264_qpel8_h_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    int h=8;\
    __asm__ volatile(\
        "pxor %%xmm7, %%xmm7        \n\t"\
        "movdqa %5, %%xmm6          \n\t"\
        "1:                         \n\t"\
        "lddqu   -5(%0), %%xmm1     \n\t"\
        "movdqa  %%xmm1, %%xmm0     \n\t"\
        "punpckhbw %%xmm7, %%xmm1   \n\t"\
        "punpcklbw %%xmm7, %%xmm0   \n\t"\
        "movdqa  %%xmm1, %%xmm2     \n\t"\
        "movdqa  %%xmm1, %%xmm3     \n\t"\
        "movdqa  %%xmm1, %%xmm4     \n\t"\
        "movdqa  %%xmm1, %%xmm5     \n\t"\
        "palignr $6, %%xmm0, %%xmm5 \n\t"\
        "palignr $8, %%xmm0, %%xmm4 \n\t"\
        "palignr $10,%%xmm0, %%xmm3 \n\t"\
        "paddw   %%xmm1, %%xmm5     \n\t"\
        "palignr $12,%%xmm0, %%xmm2 \n\t"\
        "palignr $14,%%xmm0, %%xmm1 \n\t"\
        "paddw   %%xmm3, %%xmm2     \n\t"\
        "paddw   %%xmm4, %%xmm1     \n\t"\
        "psllw   $2,     %%xmm2     \n\t"\
        "psubw   %%xmm1, %%xmm2     \n\t"\
        "paddw   %6,     %%xmm5     \n\t"\
        "pmullw  %%xmm6, %%xmm2     \n\t"\
        "paddw   %%xmm5, %%xmm2     \n\t"\
        "psraw   $5,     %%xmm2     \n\t"\
        "packuswb %%xmm2, %%xmm2    \n\t"\
        OP(%%xmm2, (%1), %%xmm4, q)\
        "add %3, %0                 \n\t"\
        "add %4, %1                 \n\t"\
        "decl %2                    \n\t"\
        " jnz 1b                    \n\t"\
        : "+a"(src), "+c"(dst), "+g"(h)\
        : "D"((x86_reg)srcStride), "S"((x86_reg)dstStride),\
          "m"(ff_pw_5), "m"(ff_pw_16)\
        : "memory"\
    );\
}\
static void OPNAME ## h264_qpel16_h_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst  , src  , dstStride, srcStride);\
    OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst+8, src+8, dstStride, srcStride);\
    src += 8*srcStride;\
    dst += 8*dstStride;\
    OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst  , src  , dstStride, srcStride);\
    OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst+8, src+8, dstStride, srcStride);\
}\

#define QPEL_H264_V_XMM(OPNAME, OP, MMX)\
static av_noinline void OPNAME ## h264_qpel8or16_v_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride, int h){\
    src -= 2*srcStride;\
    \
    __asm__ volatile(\
        "pxor %%xmm7, %%xmm7        \n\t"\
        "movq (%0), %%xmm0          \n\t"\
        "add %2, %0                 \n\t"\
        "movq (%0), %%xmm1          \n\t"\
        "add %2, %0                 \n\t"\
        "movq (%0), %%xmm2          \n\t"\
        "add %2, %0                 \n\t"\
        "movq (%0), %%xmm3          \n\t"\
        "add %2, %0                 \n\t"\
        "movq (%0), %%xmm4          \n\t"\
        "add %2, %0                 \n\t"\
        "punpcklbw %%xmm7, %%xmm0   \n\t"\
        "punpcklbw %%xmm7, %%xmm1   \n\t"\
        "punpcklbw %%xmm7, %%xmm2   \n\t"\
        "punpcklbw %%xmm7, %%xmm3   \n\t"\
        "punpcklbw %%xmm7, %%xmm4   \n\t"\
        QPEL_H264V_XMM(%%xmm0, %%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, OP)\
        QPEL_H264V_XMM(%%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm0, OP)\
        QPEL_H264V_XMM(%%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm0, %%xmm1, OP)\
        QPEL_H264V_XMM(%%xmm3, %%xmm4, %%xmm5, %%xmm0, %%xmm1, %%xmm2, OP)\
        QPEL_H264V_XMM(%%xmm4, %%xmm5, %%xmm0, %%xmm1, %%xmm2, %%xmm3, OP)\
        QPEL_H264V_XMM(%%xmm5, %%xmm0, %%xmm1, %%xmm2, %%xmm3, %%xmm4, OP)\
        QPEL_H264V_XMM(%%xmm0, %%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, OP)\
        QPEL_H264V_XMM(%%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm0, OP)\
         \
        : "+a"(src), "+c"(dst)\
        : "S"((x86_reg)srcStride), "D"((x86_reg)dstStride), "m"(ff_pw_5), "m"(ff_pw_16)\
        : "memory"\
    );\
    if(h==16){\
        __asm__ volatile(\
            QPEL_H264V_XMM(%%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm0, %%xmm1, OP)\
            QPEL_H264V_XMM(%%xmm3, %%xmm4, %%xmm5, %%xmm0, %%xmm1, %%xmm2, OP)\
            QPEL_H264V_XMM(%%xmm4, %%xmm5, %%xmm0, %%xmm1, %%xmm2, %%xmm3, OP)\
            QPEL_H264V_XMM(%%xmm5, %%xmm0, %%xmm1, %%xmm2, %%xmm3, %%xmm4, OP)\
            QPEL_H264V_XMM(%%xmm0, %%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, OP)\
            QPEL_H264V_XMM(%%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm0, OP)\
            QPEL_H264V_XMM(%%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm0, %%xmm1, OP)\
            QPEL_H264V_XMM(%%xmm3, %%xmm4, %%xmm5, %%xmm0, %%xmm1, %%xmm2, OP)\
            \
            : "+a"(src), "+c"(dst)\
            : "S"((x86_reg)srcStride), "D"((x86_reg)dstStride), "m"(ff_pw_5), "m"(ff_pw_16)\
            : "memory"\
        );\
    }\
}\
static void OPNAME ## h264_qpel8_v_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    OPNAME ## h264_qpel8or16_v_lowpass_ ## MMX(dst  , src  , dstStride, srcStride, 8);\
}\
static av_noinline void OPNAME ## h264_qpel16_v_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    OPNAME ## h264_qpel8or16_v_lowpass_ ## MMX(dst  , src  , dstStride, srcStride, 16);\
    OPNAME ## h264_qpel8or16_v_lowpass_ ## MMX(dst+8, src+8, dstStride, srcStride, 16);\
}

static av_always_inline void put_h264_qpel8or16_hv1_lowpass_sse2(int16_t *tmp, uint8_t *src, int tmpStride, int srcStride, int size){
    int w = (size+8)>>3;
    src -= 2*srcStride+2;
    while(w--){
        __asm__ volatile(
            "pxor %%xmm7, %%xmm7        \n\t"
            "movq (%0), %%xmm0          \n\t"
            "add %2, %0                 \n\t"
            "movq (%0), %%xmm1          \n\t"
            "add %2, %0                 \n\t"
            "movq (%0), %%xmm2          \n\t"
            "add %2, %0                 \n\t"
            "movq (%0), %%xmm3          \n\t"
            "add %2, %0                 \n\t"
            "movq (%0), %%xmm4          \n\t"
            "add %2, %0                 \n\t"
            "punpcklbw %%xmm7, %%xmm0   \n\t"
            "punpcklbw %%xmm7, %%xmm1   \n\t"
            "punpcklbw %%xmm7, %%xmm2   \n\t"
            "punpcklbw %%xmm7, %%xmm3   \n\t"
            "punpcklbw %%xmm7, %%xmm4   \n\t"
            QPEL_H264HV_XMM(%%xmm0, %%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, 0*48)
            QPEL_H264HV_XMM(%%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm0, 1*48)
            QPEL_H264HV_XMM(%%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm0, %%xmm1, 2*48)
            QPEL_H264HV_XMM(%%xmm3, %%xmm4, %%xmm5, %%xmm0, %%xmm1, %%xmm2, 3*48)
            QPEL_H264HV_XMM(%%xmm4, %%xmm5, %%xmm0, %%xmm1, %%xmm2, %%xmm3, 4*48)
            QPEL_H264HV_XMM(%%xmm5, %%xmm0, %%xmm1, %%xmm2, %%xmm3, %%xmm4, 5*48)
            QPEL_H264HV_XMM(%%xmm0, %%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, 6*48)
            QPEL_H264HV_XMM(%%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm0, 7*48)
            : "+a"(src)
            : "c"(tmp), "S"((x86_reg)srcStride), "m"(ff_pw_5), "m"(ff_pw_16)
            : "memory"
        );
        if(size==16){
            __asm__ volatile(
                QPEL_H264HV_XMM(%%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm0, %%xmm1,  8*48)
                QPEL_H264HV_XMM(%%xmm3, %%xmm4, %%xmm5, %%xmm0, %%xmm1, %%xmm2,  9*48)
                QPEL_H264HV_XMM(%%xmm4, %%xmm5, %%xmm0, %%xmm1, %%xmm2, %%xmm3, 10*48)
                QPEL_H264HV_XMM(%%xmm5, %%xmm0, %%xmm1, %%xmm2, %%xmm3, %%xmm4, 11*48)
                QPEL_H264HV_XMM(%%xmm0, %%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, 12*48)
                QPEL_H264HV_XMM(%%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm0, 13*48)
                QPEL_H264HV_XMM(%%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm0, %%xmm1, 14*48)
                QPEL_H264HV_XMM(%%xmm3, %%xmm4, %%xmm5, %%xmm0, %%xmm1, %%xmm2, 15*48)
                : "+a"(src)
                : "c"(tmp), "S"((x86_reg)srcStride), "m"(ff_pw_5), "m"(ff_pw_16)
                : "memory"
            );
        }
        tmp += 8;
        src += 8 - (size+5)*srcStride;
    }
}

#define QPEL_H264_HV2_XMM(OPNAME, OP, MMX)\
static av_always_inline void OPNAME ## h264_qpel8or16_hv2_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, int dstStride, int tmpStride, int size){\
    int h = size;\
    if(size == 16){\
        __asm__ volatile(\
            "1:                         \n\t"\
            "movdqa 32(%0), %%xmm4      \n\t"\
            "movdqa 16(%0), %%xmm5      \n\t"\
            "movdqa   (%0), %%xmm7      \n\t"\
            "movdqa %%xmm4, %%xmm3      \n\t"\
            "movdqa %%xmm4, %%xmm2      \n\t"\
            "movdqa %%xmm4, %%xmm1      \n\t"\
            "movdqa %%xmm4, %%xmm0      \n\t"\
            "palignr $10, %%xmm5, %%xmm0 \n\t"\
            "palignr  $8, %%xmm5, %%xmm1 \n\t"\
            "palignr  $6, %%xmm5, %%xmm2 \n\t"\
            "palignr  $4, %%xmm5, %%xmm3 \n\t"\
            "palignr  $2, %%xmm5, %%xmm4 \n\t"\
            "paddw  %%xmm5, %%xmm0      \n\t"\
            "paddw  %%xmm4, %%xmm1      \n\t"\
            "paddw  %%xmm3, %%xmm2      \n\t"\
            "movdqa %%xmm5, %%xmm6      \n\t"\
            "movdqa %%xmm5, %%xmm4      \n\t"\
            "movdqa %%xmm5, %%xmm3      \n\t"\
            "palignr  $8, %%xmm7, %%xmm4 \n\t"\
            "palignr  $2, %%xmm7, %%xmm6 \n\t"\
            "palignr $10, %%xmm7, %%xmm3 \n\t"\
            "paddw  %%xmm6, %%xmm4      \n\t"\
            "movdqa %%xmm5, %%xmm6      \n\t"\
            "palignr  $6, %%xmm7, %%xmm5 \n\t"\
            "palignr  $4, %%xmm7, %%xmm6 \n\t"\
            "paddw  %%xmm7, %%xmm3      \n\t"\
            "paddw  %%xmm6, %%xmm5      \n\t"\
            \
            "psubw  %%xmm1, %%xmm0      \n\t"\
            "psubw  %%xmm4, %%xmm3      \n\t"\
            "psraw      $2, %%xmm0      \n\t"\
            "psraw      $2, %%xmm3      \n\t"\
            "psubw  %%xmm1, %%xmm0      \n\t"\
            "psubw  %%xmm4, %%xmm3      \n\t"\
            "paddw  %%xmm2, %%xmm0      \n\t"\
            "paddw  %%xmm5, %%xmm3      \n\t"\
            "psraw      $2, %%xmm0      \n\t"\
            "psraw      $2, %%xmm3      \n\t"\
            "paddw  %%xmm2, %%xmm0      \n\t"\
            "paddw  %%xmm5, %%xmm3      \n\t"\
            "psraw      $6, %%xmm0      \n\t"\
            "psraw      $6, %%xmm3      \n\t"\
            "packuswb %%xmm0, %%xmm3    \n\t"\
            OP(%%xmm3, (%1), %%xmm7, dqa)\
            "add $48, %0                \n\t"\
            "add %3, %1                 \n\t"\
            "decl %2                    \n\t"\
            " jnz 1b                    \n\t"\
            : "+a"(tmp), "+c"(dst), "+g"(h)\
            : "S"((x86_reg)dstStride)\
            : "memory"\
        );\
    }else{\
        __asm__ volatile(\
            "1:                         \n\t"\
            "movdqa 16(%0), %%xmm1      \n\t"\
            "movdqa   (%0), %%xmm0      \n\t"\
            "movdqa %%xmm1, %%xmm2      \n\t"\
            "movdqa %%xmm1, %%xmm3      \n\t"\
            "movdqa %%xmm1, %%xmm4      \n\t"\
            "movdqa %%xmm1, %%xmm5      \n\t"\
            "palignr $10, %%xmm0, %%xmm5 \n\t"\
            "palignr  $8, %%xmm0, %%xmm4 \n\t"\
            "palignr  $6, %%xmm0, %%xmm3 \n\t"\
            "palignr  $4, %%xmm0, %%xmm2 \n\t"\
            "palignr  $2, %%xmm0, %%xmm1 \n\t"\
            "paddw  %%xmm5, %%xmm0      \n\t"\
            "paddw  %%xmm4, %%xmm1      \n\t"\
            "paddw  %%xmm3, %%xmm2      \n\t"\
            "psubw  %%xmm1, %%xmm0      \n\t"\
            "psraw      $2, %%xmm0      \n\t"\
            "psubw  %%xmm1, %%xmm0      \n\t"\
            "paddw  %%xmm2, %%xmm0      \n\t"\
            "psraw      $2, %%xmm0      \n\t"\
            "paddw  %%xmm2, %%xmm0      \n\t"\
            "psraw      $6, %%xmm0      \n\t"\
            "packuswb %%xmm0, %%xmm0    \n\t"\
            OP(%%xmm0, (%1), %%xmm7, q)\
            "add $48, %0                \n\t"\
            "add %3, %1                 \n\t"\
            "decl %2                    \n\t"\
            " jnz 1b                    \n\t"\
            : "+a"(tmp), "+c"(dst), "+g"(h)\
            : "S"((x86_reg)dstStride)\
            : "memory"\
        );\
    }\
}

#define QPEL_H264_HV_XMM(OPNAME, OP, MMX)\
static av_noinline void OPNAME ## h264_qpel8or16_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, uint8_t *src, int dstStride, int tmpStride, int srcStride, int size){\
          put_h264_qpel8or16_hv1_lowpass_sse2(tmp, src, tmpStride, srcStride, size);\
    OPNAME ## h264_qpel8or16_hv2_lowpass_ ## MMX(dst, tmp, dstStride, tmpStride, size);\
}\
static void OPNAME ## h264_qpel8_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, uint8_t *src, int dstStride, int tmpStride, int srcStride){\
    OPNAME ## h264_qpel8or16_hv_lowpass_ ## MMX(dst, tmp, src, dstStride, tmpStride, srcStride, 8);\
}\
static void OPNAME ## h264_qpel16_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, uint8_t *src, int dstStride, int tmpStride, int srcStride){\
    OPNAME ## h264_qpel8or16_hv_lowpass_ ## MMX(dst, tmp, src, dstStride, tmpStride, srcStride, 16);\
}\

#define put_pixels8_l2_sse2 put_pixels8_l2_mmx2
#define avg_pixels8_l2_sse2 avg_pixels8_l2_mmx2
#define put_pixels16_l2_sse2 put_pixels16_l2_mmx2
#define avg_pixels16_l2_sse2 avg_pixels16_l2_mmx2
#define put_pixels8_l2_ssse3 put_pixels8_l2_mmx2
#define avg_pixels8_l2_ssse3 avg_pixels8_l2_mmx2
#define put_pixels16_l2_ssse3 put_pixels16_l2_mmx2
#define avg_pixels16_l2_ssse3 avg_pixels16_l2_mmx2

#define put_pixels8_l2_shift5_sse2 put_pixels8_l2_shift5_mmx2
#define avg_pixels8_l2_shift5_sse2 avg_pixels8_l2_shift5_mmx2
#define put_pixels16_l2_shift5_sse2 put_pixels16_l2_shift5_mmx2
#define avg_pixels16_l2_shift5_sse2 avg_pixels16_l2_shift5_mmx2
#define put_pixels8_l2_shift5_ssse3 put_pixels8_l2_shift5_mmx2
#define avg_pixels8_l2_shift5_ssse3 avg_pixels8_l2_shift5_mmx2
#define put_pixels16_l2_shift5_ssse3 put_pixels16_l2_shift5_mmx2
#define avg_pixels16_l2_shift5_ssse3 avg_pixels16_l2_shift5_mmx2

#define put_h264_qpel8_h_lowpass_l2_sse2 put_h264_qpel8_h_lowpass_l2_mmx2
#define avg_h264_qpel8_h_lowpass_l2_sse2 avg_h264_qpel8_h_lowpass_l2_mmx2
#define put_h264_qpel16_h_lowpass_l2_sse2 put_h264_qpel16_h_lowpass_l2_mmx2
#define avg_h264_qpel16_h_lowpass_l2_sse2 avg_h264_qpel16_h_lowpass_l2_mmx2

#define put_h264_qpel8_v_lowpass_ssse3 put_h264_qpel8_v_lowpass_sse2
#define avg_h264_qpel8_v_lowpass_ssse3 avg_h264_qpel8_v_lowpass_sse2
#define put_h264_qpel16_v_lowpass_ssse3 put_h264_qpel16_v_lowpass_sse2
#define avg_h264_qpel16_v_lowpass_ssse3 avg_h264_qpel16_v_lowpass_sse2

#define put_h264_qpel8or16_hv2_lowpass_sse2 put_h264_qpel8or16_hv2_lowpass_mmx2
#define avg_h264_qpel8or16_hv2_lowpass_sse2 avg_h264_qpel8or16_hv2_lowpass_mmx2

#define H264_MC(OPNAME, SIZE, MMX, ALIGN) \
H264_MC_C(OPNAME, SIZE, MMX, ALIGN)\
H264_MC_V(OPNAME, SIZE, MMX, ALIGN)\
H264_MC_H(OPNAME, SIZE, MMX, ALIGN)\
H264_MC_HV(OPNAME, SIZE, MMX, ALIGN)\

static void put_h264_qpel16_mc00_sse2 (uint8_t *dst, uint8_t *src, int stride){
    put_pixels16_sse2(dst, src, stride, 16);
}
static void avg_h264_qpel16_mc00_sse2 (uint8_t *dst, uint8_t *src, int stride){
    avg_pixels16_sse2(dst, src, stride, 16);
}
#define put_h264_qpel8_mc00_sse2 put_h264_qpel8_mc00_mmx2
#define avg_h264_qpel8_mc00_sse2 avg_h264_qpel8_mc00_mmx2

#define H264_MC_C(OPNAME, SIZE, MMX, ALIGN) \
static void OPNAME ## h264_qpel ## SIZE ## _mc00_ ## MMX (uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## pixels ## SIZE ## _ ## MMX(dst, src, stride, SIZE);\
}\

#define H264_MC_H(OPNAME, SIZE, MMX, ALIGN) \
static void OPNAME ## h264_qpel ## SIZE ## _mc10_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src, src, stride, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc20_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## h264_qpel ## SIZE ## _h_lowpass_ ## MMX(dst, src, stride, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc30_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src, src+1, stride, stride);\
}\

#define H264_MC_V(OPNAME, SIZE, MMX, ALIGN) \
static void OPNAME ## h264_qpel ## SIZE ## _mc01_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(ALIGN, uint8_t, temp[SIZE*SIZE]);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(temp, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## MMX(dst, src, temp, stride, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc02_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## h264_qpel ## SIZE ## _v_lowpass_ ## MMX(dst, src, stride, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc03_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(ALIGN, uint8_t, temp[SIZE*SIZE]);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(temp, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## MMX(dst, src+stride, temp, stride, stride, SIZE);\
}\

#define H264_MC_HV(OPNAME, SIZE, MMX, ALIGN) \
static void OPNAME ## h264_qpel ## SIZE ## _mc11_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(ALIGN, uint8_t, temp[SIZE*SIZE]);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(temp, src, SIZE, stride);\
    OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src, temp, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc31_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(ALIGN, uint8_t, temp[SIZE*SIZE]);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(temp, src+1, SIZE, stride);\
    OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src, temp, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc13_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(ALIGN, uint8_t, temp[SIZE*SIZE]);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(temp, src, SIZE, stride);\
    OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src+stride, temp, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc33_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(ALIGN, uint8_t, temp[SIZE*SIZE]);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(temp, src+1, SIZE, stride);\
    OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src+stride, temp, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc22_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(ALIGN, uint16_t, temp[SIZE*(SIZE<8?12:24)]);\
    OPNAME ## h264_qpel ## SIZE ## _hv_lowpass_ ## MMX(dst, temp, src, stride, SIZE, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc21_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(ALIGN, uint8_t, temp[SIZE*(SIZE<8?12:24)*2 + SIZE*SIZE]);\
    uint8_t * const halfHV= temp;\
    int16_t * const halfV= (int16_t*)(temp + SIZE*SIZE);\
    assert(((int)temp & 7) == 0);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## MMX(halfHV, halfV, src, SIZE, SIZE, stride);\
    OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src, halfHV, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc23_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(ALIGN, uint8_t, temp[SIZE*(SIZE<8?12:24)*2 + SIZE*SIZE]);\
    uint8_t * const halfHV= temp;\
    int16_t * const halfV= (int16_t*)(temp + SIZE*SIZE);\
    assert(((int)temp & 7) == 0);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## MMX(halfHV, halfV, src, SIZE, SIZE, stride);\
    OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src+stride, halfHV, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc12_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(ALIGN, uint8_t, temp[SIZE*(SIZE<8?12:24)*2 + SIZE*SIZE]);\
    uint8_t * const halfHV= temp;\
    int16_t * const halfV= (int16_t*)(temp + SIZE*SIZE);\
    assert(((int)temp & 7) == 0);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## MMX(halfHV, halfV, src, SIZE, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_shift5_ ## MMX(dst, halfV+2, halfHV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc32_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(ALIGN, uint8_t, temp[SIZE*(SIZE<8?12:24)*2 + SIZE*SIZE]);\
    uint8_t * const halfHV= temp;\
    int16_t * const halfV= (int16_t*)(temp + SIZE*SIZE);\
    assert(((int)temp & 7) == 0);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## MMX(halfHV, halfV, src, SIZE, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_shift5_ ## MMX(dst, halfV+3, halfHV, stride, SIZE, SIZE);\
}\

#define H264_MC_4816(MMX)\
H264_MC(put_, 4, MMX, 8)\
H264_MC(put_, 8, MMX, 8)\
H264_MC(put_, 16,MMX, 8)\
H264_MC(avg_, 4, MMX, 8)\
H264_MC(avg_, 8, MMX, 8)\
H264_MC(avg_, 16,MMX, 8)\

#define H264_MC_816(QPEL, XMM)\
QPEL(put_, 8, XMM, 16)\
QPEL(put_, 16,XMM, 16)\
QPEL(avg_, 8, XMM, 16)\
QPEL(avg_, 16,XMM, 16)\


#define AVG_3DNOW_OP(a,b,temp, size) \
"mov" #size " " #b ", " #temp "   \n\t"\
"pavgusb " #temp ", " #a "        \n\t"\
"mov" #size " " #a ", " #b "      \n\t"
#define AVG_MMX2_OP(a,b,temp, size) \
"mov" #size " " #b ", " #temp "   \n\t"\
"pavgb " #temp ", " #a "          \n\t"\
"mov" #size " " #a ", " #b "      \n\t"

#define PAVGB "pavgusb"
QPEL_H264(put_,       PUT_OP, 3dnow)
QPEL_H264(avg_, AVG_3DNOW_OP, 3dnow)
#undef PAVGB
#define PAVGB "pavgb"
QPEL_H264(put_,       PUT_OP, mmx2)
QPEL_H264(avg_,  AVG_MMX2_OP, mmx2)
QPEL_H264_V_XMM(put_,       PUT_OP, sse2)
QPEL_H264_V_XMM(avg_,  AVG_MMX2_OP, sse2)
QPEL_H264_HV_XMM(put_,       PUT_OP, sse2)
QPEL_H264_HV_XMM(avg_,  AVG_MMX2_OP, sse2)
#if HAVE_SSSE3
QPEL_H264_H_XMM(put_,       PUT_OP, ssse3)
QPEL_H264_H_XMM(avg_,  AVG_MMX2_OP, ssse3)
QPEL_H264_HV2_XMM(put_,       PUT_OP, ssse3)
QPEL_H264_HV2_XMM(avg_,  AVG_MMX2_OP, ssse3)
QPEL_H264_HV_XMM(put_,       PUT_OP, ssse3)
QPEL_H264_HV_XMM(avg_,  AVG_MMX2_OP, ssse3)
#endif
#undef PAVGB

H264_MC_4816(3dnow)
H264_MC_4816(mmx2)
H264_MC_816(H264_MC_V, sse2)
H264_MC_816(H264_MC_HV, sse2)
#if HAVE_SSSE3
H264_MC_816(H264_MC_H, ssse3)
H264_MC_816(H264_MC_HV, ssse3)
#endif

/* rnd interleaved with rnd div 8, use p+1 to access rnd div 8 */
DECLARE_ALIGNED_8(static const uint64_t, h264_rnd_reg[4]) = {
    0x0020002000200020ULL, 0x0004000400040004ULL, 0x001C001C001C001CULL, 0x0003000300030003ULL
};

#define H264_CHROMA_OP(S,D)
#define H264_CHROMA_OP4(S,D,T)
#define H264_CHROMA_MC8_TMPL put_h264_chroma_generic_mc8_mmx
#define H264_CHROMA_MC4_TMPL put_h264_chroma_generic_mc4_mmx
#define H264_CHROMA_MC2_TMPL put_h264_chroma_mc2_mmx2
#define H264_CHROMA_MC8_MV0 put_pixels8_mmx
#include "dsputil_h264_template_mmx.c"

static void put_h264_chroma_mc8_mmx_rnd(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y)
{
    put_h264_chroma_generic_mc8_mmx(dst, src, stride, h, x, y, h264_rnd_reg);
}
static void put_h264_chroma_mc8_mmx_nornd(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y)
{
    put_h264_chroma_generic_mc8_mmx(dst, src, stride, h, x, y, h264_rnd_reg+2);
}
static void put_h264_chroma_mc4_mmx(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y)
{
    put_h264_chroma_generic_mc4_mmx(dst, src, stride, h, x, y, h264_rnd_reg);
}

#undef H264_CHROMA_OP
#undef H264_CHROMA_OP4
#undef H264_CHROMA_MC8_TMPL
#undef H264_CHROMA_MC4_TMPL
#undef H264_CHROMA_MC2_TMPL
#undef H264_CHROMA_MC8_MV0

#define H264_CHROMA_OP(S,D) "pavgb " #S ", " #D " \n\t"
#define H264_CHROMA_OP4(S,D,T) "movd  " #S ", " #T " \n\t"\
                               "pavgb " #T ", " #D " \n\t"
#define H264_CHROMA_MC8_TMPL avg_h264_chroma_generic_mc8_mmx2
#define H264_CHROMA_MC4_TMPL avg_h264_chroma_generic_mc4_mmx2
#define H264_CHROMA_MC2_TMPL avg_h264_chroma_mc2_mmx2
#define H264_CHROMA_MC8_MV0 avg_pixels8_mmx2
#include "dsputil_h264_template_mmx.c"
static void avg_h264_chroma_mc8_mmx2_rnd(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y)
{
    avg_h264_chroma_generic_mc8_mmx2(dst, src, stride, h, x, y, h264_rnd_reg);
}
static void avg_h264_chroma_mc4_mmx2(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y)
{
    avg_h264_chroma_generic_mc4_mmx2(dst, src, stride, h, x, y, h264_rnd_reg);
}
#undef H264_CHROMA_OP
#undef H264_CHROMA_OP4
#undef H264_CHROMA_MC8_TMPL
#undef H264_CHROMA_MC4_TMPL
#undef H264_CHROMA_MC2_TMPL
#undef H264_CHROMA_MC8_MV0

#define H264_CHROMA_OP(S,D) "pavgusb " #S ", " #D " \n\t"
#define H264_CHROMA_OP4(S,D,T) "movd " #S ", " #T " \n\t"\
                               "pavgusb " #T ", " #D " \n\t"
#define H264_CHROMA_MC8_TMPL avg_h264_chroma_generic_mc8_3dnow
#define H264_CHROMA_MC4_TMPL avg_h264_chroma_generic_mc4_3dnow
#define H264_CHROMA_MC8_MV0 avg_pixels8_3dnow
#include "dsputil_h264_template_mmx.c"
static void avg_h264_chroma_mc8_3dnow_rnd(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y)
{
    avg_h264_chroma_generic_mc8_3dnow(dst, src, stride, h, x, y, h264_rnd_reg);
}
static void avg_h264_chroma_mc4_3dnow(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y)
{
    avg_h264_chroma_generic_mc4_3dnow(dst, src, stride, h, x, y, h264_rnd_reg);
}
#undef H264_CHROMA_OP
#undef H264_CHROMA_OP4
#undef H264_CHROMA_MC8_TMPL
#undef H264_CHROMA_MC4_TMPL
#undef H264_CHROMA_MC8_MV0

#if HAVE_SSSE3
#define AVG_OP(X)
#undef H264_CHROMA_MC8_TMPL
#undef H264_CHROMA_MC4_TMPL
#define H264_CHROMA_MC8_TMPL put_h264_chroma_mc8_ssse3
#define H264_CHROMA_MC4_TMPL put_h264_chroma_mc4_ssse3
#define H264_CHROMA_MC8_MV0 put_pixels8_mmx
#include "dsputil_h264_template_ssse3.c"
static void put_h264_chroma_mc8_ssse3_rnd(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y)
{
    put_h264_chroma_mc8_ssse3(dst, src, stride, h, x, y, 1);
}
static void put_h264_chroma_mc8_ssse3_nornd(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y)
{
    put_h264_chroma_mc8_ssse3(dst, src, stride, h, x, y, 0);
}

#undef AVG_OP
#undef H264_CHROMA_MC8_TMPL
#undef H264_CHROMA_MC4_TMPL
#undef H264_CHROMA_MC8_MV0
#define AVG_OP(X) X
#define H264_CHROMA_MC8_TMPL avg_h264_chroma_mc8_ssse3
#define H264_CHROMA_MC4_TMPL avg_h264_chroma_mc4_ssse3
#define H264_CHROMA_MC8_MV0 avg_pixels8_mmx2
#include "dsputil_h264_template_ssse3.c"
static void avg_h264_chroma_mc8_ssse3_rnd(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y)
{
    avg_h264_chroma_mc8_ssse3(dst, src, stride, h, x, y, 1);
}
#undef AVG_OP
#undef H264_CHROMA_MC8_TMPL
#undef H264_CHROMA_MC4_TMPL
#undef H264_CHROMA_MC8_MV0
#endif

/***********************************/
/* weighted prediction */

static inline void ff_h264_weight_WxH_mmx2(uint8_t *dst, int stride, int log2_denom, int weight, int offset, int w, int h)
{
    int x, y;
    offset <<= log2_denom;
    offset += (1 << log2_denom) >> 1;
    __asm__ volatile(
        "movd    %0, %%mm4        \n\t"
        "movd    %1, %%mm5        \n\t"
        "movd    %2, %%mm6        \n\t"
        "pshufw  $0, %%mm4, %%mm4 \n\t"
        "pshufw  $0, %%mm5, %%mm5 \n\t"
        "pxor    %%mm7, %%mm7     \n\t"
        :: "g"(weight), "g"(offset), "g"(log2_denom)
    );
    for(y=0; y<h; y+=2){
        for(x=0; x<w; x+=4){
            __asm__ volatile(
                "movd      %0,    %%mm0 \n\t"
                "movd      %1,    %%mm1 \n\t"
                "punpcklbw %%mm7, %%mm0 \n\t"
                "punpcklbw %%mm7, %%mm1 \n\t"
                "pmullw    %%mm4, %%mm0 \n\t"
                "pmullw    %%mm4, %%mm1 \n\t"
                "paddsw    %%mm5, %%mm0 \n\t"
                "paddsw    %%mm5, %%mm1 \n\t"
                "psraw     %%mm6, %%mm0 \n\t"
                "psraw     %%mm6, %%mm1 \n\t"
                "packuswb  %%mm7, %%mm0 \n\t"
                "packuswb  %%mm7, %%mm1 \n\t"
                "movd      %%mm0, %0    \n\t"
                "movd      %%mm1, %1    \n\t"
                : "+m"(*(uint32_t*)(dst+x)),
                  "+m"(*(uint32_t*)(dst+x+stride))
            );
        }
        dst += 2*stride;
    }
}

static inline void ff_h264_biweight_WxH_mmx2(uint8_t *dst, uint8_t *src, int stride, int log2_denom, int weightd, int weights, int offset, int w, int h)
{
    int x, y;
    offset = ((offset + 1) | 1) << log2_denom;
    __asm__ volatile(
        "movd    %0, %%mm3        \n\t"
        "movd    %1, %%mm4        \n\t"
        "movd    %2, %%mm5        \n\t"
        "movd    %3, %%mm6        \n\t"
        "pshufw  $0, %%mm3, %%mm3 \n\t"
        "pshufw  $0, %%mm4, %%mm4 \n\t"
        "pshufw  $0, %%mm5, %%mm5 \n\t"
        "pxor    %%mm7, %%mm7     \n\t"
        :: "g"(weightd), "g"(weights), "g"(offset), "g"(log2_denom+1)
    );
    for(y=0; y<h; y++){
        for(x=0; x<w; x+=4){
            __asm__ volatile(
                "movd      %0,    %%mm0 \n\t"
                "movd      %1,    %%mm1 \n\t"
                "punpcklbw %%mm7, %%mm0 \n\t"
                "punpcklbw %%mm7, %%mm1 \n\t"
                "pmullw    %%mm3, %%mm0 \n\t"
                "pmullw    %%mm4, %%mm1 \n\t"
                "paddsw    %%mm1, %%mm0 \n\t"
                "paddsw    %%mm5, %%mm0 \n\t"
                "psraw     %%mm6, %%mm0 \n\t"
                "packuswb  %%mm0, %%mm0 \n\t"
                "movd      %%mm0, %0    \n\t"
                : "+m"(*(uint32_t*)(dst+x))
                :  "m"(*(uint32_t*)(src+x))
            );
        }
        src += stride;
        dst += stride;
    }
}

#define H264_WEIGHT(W,H) \
static void ff_h264_biweight_ ## W ## x ## H ## _mmx2(uint8_t *dst, uint8_t *src, int stride, int log2_denom, int weightd, int weights, int offset){ \
    ff_h264_biweight_WxH_mmx2(dst, src, stride, log2_denom, weightd, weights, offset, W, H); \
} \
static void ff_h264_weight_ ## W ## x ## H ## _mmx2(uint8_t *dst, int stride, int log2_denom, int weight, int offset){ \
    ff_h264_weight_WxH_mmx2(dst, stride, log2_denom, weight, offset, W, H); \
}

H264_WEIGHT(16,16)
H264_WEIGHT(16, 8)
H264_WEIGHT( 8,16)
H264_WEIGHT( 8, 8)
H264_WEIGHT( 8, 4)
H264_WEIGHT( 4, 8)
H264_WEIGHT( 4, 4)
H264_WEIGHT( 4, 2)

