/*
 * Copyright (C) 2001-2003 Michael Niedermayer (michaelni@gmx.at)
 *
 * AltiVec optimizations (C) 2004 Romain Dolbeau <romain@dolbeau.org>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * postprocessing.
 */

/*
                        C       MMX     MMX2    AltiVec
isVertDC                Ec      Ec              Ec
isVertMinMaxOk          Ec      Ec              Ec
doVertLowPass           E               e       Ec
doVertDefFilter         Ec      Ec      e       Ec
isHorizDC               Ec      Ec              Ec
isHorizMinMaxOk         a       E               Ec
doHorizLowPass          E               e       Ec
doHorizDefFilter        Ec      Ec      e       Ec
do_a_deblock            Ec      E       Ec
deRing                  E               e       Ecp
Vertical RKAlgo1        E               a
Horizontal RKAlgo1                      a
Vertical X1#            a               E
Horizontal X1#          a               E
LinIpolDeinterlace      e               E
CubicIpolDeinterlace    a               e
LinBlendDeinterlace     e               E
MedianDeinterlace#      E       Ec      Ec
TempDeNoiser#           E               e       Ec

# more or less selfinvented filters so the exactness is not too meaningful
E = Exact implementation
e = almost exact implementation (slightly different rounding,...)
a = alternative / approximate impl
c = checked against the other implementations (-vo md5)
p = partially optimized, still some work to do
*/

/*
TODO:
reduce the time wasted on the mem transfer
unroll stuff if instructions depend too much on the prior one
move YScale thing to the end instead of fixing QP
write a faster and higher quality deblocking filter :)
make the mainloop more flexible (variable number of blocks at once
        (the if/else stuff per block is slowing things down)
compare the quality & speed of all filters
split this huge file
optimize c versions
try to unroll inner for(x=0 ... loop to avoid these damn if(x ... checks
...
*/

//Changelog: use git log

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "libavutil/common.h"
#include "libavutil/cpu.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
//#undef HAVE_MMXEXT_INLINE
//#undef HAVE_MMX_INLINE
//#undef ARCH_X86
//#define DEBUG_BRIGHTNESS
#include "postprocess.h"
#include "postprocess_internal.h"
#include "libavutil/avstring.h"

#define GET_MODE_BUFFER_SIZE 500
#define OPTIONS_ARRAY_SIZE 10
#define BLOCK_SIZE 8
#define TEMP_STRIDE 8
//#define NUM_BLOCKS_AT_ONCE 16 //not used yet

#if ARCH_X86 && HAVE_INLINE_ASM
DECLARE_ASM_CONST(8, uint64_t, w05)= 0x0005000500050005LL;
DECLARE_ASM_CONST(8, uint64_t, w04)= 0x0004000400040004LL;
DECLARE_ASM_CONST(8, uint64_t, w20)= 0x0020002000200020LL;
DECLARE_ASM_CONST(8, uint64_t, b00)= 0x0000000000000000LL;
DECLARE_ASM_CONST(8, uint64_t, b01)= 0x0101010101010101LL;
DECLARE_ASM_CONST(8, uint64_t, b02)= 0x0202020202020202LL;
DECLARE_ASM_CONST(8, uint64_t, b08)= 0x0808080808080808LL;
DECLARE_ASM_CONST(8, uint64_t, b80)= 0x8080808080808080LL;
#endif

DECLARE_ASM_CONST(8, int, deringThreshold)= 20;


static const struct PPFilter filters[]=
{
    {"hb", "hdeblock",              1, 1, 3, H_DEBLOCK},
    {"vb", "vdeblock",              1, 2, 4, V_DEBLOCK},
/*  {"hr", "rkhdeblock",            1, 1, 3, H_RK1_FILTER},
    {"vr", "rkvdeblock",            1, 2, 4, V_RK1_FILTER},*/
    {"h1", "x1hdeblock",            1, 1, 3, H_X1_FILTER},
    {"v1", "x1vdeblock",            1, 2, 4, V_X1_FILTER},
    {"ha", "ahdeblock",             1, 1, 3, H_A_DEBLOCK},
    {"va", "avdeblock",             1, 2, 4, V_A_DEBLOCK},
    {"dr", "dering",                1, 5, 6, DERING},
    {"al", "autolevels",            0, 1, 2, LEVEL_FIX},
    {"lb", "linblenddeint",         1, 1, 4, LINEAR_BLEND_DEINT_FILTER},
    {"li", "linipoldeint",          1, 1, 4, LINEAR_IPOL_DEINT_FILTER},
    {"ci", "cubicipoldeint",        1, 1, 4, CUBIC_IPOL_DEINT_FILTER},
    {"md", "mediandeint",           1, 1, 4, MEDIAN_DEINT_FILTER},
    {"fd", "ffmpegdeint",           1, 1, 4, FFMPEG_DEINT_FILTER},
    {"l5", "lowpass5",              1, 1, 4, LOWPASS5_DEINT_FILTER},
    {"tn", "tmpnoise",              1, 7, 8, TEMP_NOISE_FILTER},
    {"fq", "forcequant",            1, 0, 0, FORCE_QUANT},
    {"be", "bitexact",              1, 0, 0, BITEXACT},
    {"vi", "visualize",             1, 0, 0, VISUALIZE},
    {NULL, NULL,0,0,0,0} //End Marker
};

static const char * const replaceTable[]=
{
    "default",      "hb:a,vb:a,dr:a",
    "de",           "hb:a,vb:a,dr:a",
    "fast",         "h1:a,v1:a,dr:a",
    "fa",           "h1:a,v1:a,dr:a",
    "ac",           "ha:a:128:7,va:a,dr:a",
    NULL //End Marker
};

/* The horizontal functions exist only in C because the MMX
 * code is faster with vertical filters and transposing. */

/**
 * Check if the given 8x8 Block is mostly "flat"
 */
static inline int isHorizDC_C(const uint8_t src[], int stride, const PPContext *c)
{
    int numEq= 0;
    int y;
    const int dcOffset= ((c->nonBQP*c->ppMode.baseDcDiff)>>8) + 1;
    const int dcThreshold= dcOffset*2 + 1;

    for(y=0; y<BLOCK_SIZE; y++){
        numEq += ((unsigned)(src[0] - src[1] + dcOffset)) < dcThreshold;
        numEq += ((unsigned)(src[1] - src[2] + dcOffset)) < dcThreshold;
        numEq += ((unsigned)(src[2] - src[3] + dcOffset)) < dcThreshold;
        numEq += ((unsigned)(src[3] - src[4] + dcOffset)) < dcThreshold;
        numEq += ((unsigned)(src[4] - src[5] + dcOffset)) < dcThreshold;
        numEq += ((unsigned)(src[5] - src[6] + dcOffset)) < dcThreshold;
        numEq += ((unsigned)(src[6] - src[7] + dcOffset)) < dcThreshold;
        src+= stride;
    }
    return numEq > c->ppMode.flatnessThreshold;
}

/**
 * Check if the middle 8x8 Block in the given 8x16 block is flat
 */
static inline int isVertDC_C(const uint8_t src[], int stride, const PPContext *c)
{
    int numEq= 0;
    int y;
    const int dcOffset= ((c->nonBQP*c->ppMode.baseDcDiff)>>8) + 1;
    const int dcThreshold= dcOffset*2 + 1;

    src+= stride*4; // src points to begin of the 8x8 Block
    for(y=0; y<BLOCK_SIZE-1; y++){
        numEq += ((unsigned)(src[0] - src[0+stride] + dcOffset)) < dcThreshold;
        numEq += ((unsigned)(src[1] - src[1+stride] + dcOffset)) < dcThreshold;
        numEq += ((unsigned)(src[2] - src[2+stride] + dcOffset)) < dcThreshold;
        numEq += ((unsigned)(src[3] - src[3+stride] + dcOffset)) < dcThreshold;
        numEq += ((unsigned)(src[4] - src[4+stride] + dcOffset)) < dcThreshold;
        numEq += ((unsigned)(src[5] - src[5+stride] + dcOffset)) < dcThreshold;
        numEq += ((unsigned)(src[6] - src[6+stride] + dcOffset)) < dcThreshold;
        numEq += ((unsigned)(src[7] - src[7+stride] + dcOffset)) < dcThreshold;
        src+= stride;
    }
    return numEq > c->ppMode.flatnessThreshold;
}

static inline int isHorizMinMaxOk_C(const uint8_t src[], int stride, int QP)
{
    int i;
    for(i=0; i<2; i++){
        if((unsigned)(src[0] - src[5] + 2*QP) > 4*QP) return 0;
        src += stride;
        if((unsigned)(src[2] - src[7] + 2*QP) > 4*QP) return 0;
        src += stride;
        if((unsigned)(src[4] - src[1] + 2*QP) > 4*QP) return 0;
        src += stride;
        if((unsigned)(src[6] - src[3] + 2*QP) > 4*QP) return 0;
        src += stride;
    }
    return 1;
}

static inline int isVertMinMaxOk_C(const uint8_t src[], int stride, int QP)
{
    int x;
    src+= stride*4;
    for(x=0; x<BLOCK_SIZE; x+=4){
        if((unsigned)(src[  x + 0*stride] - src[  x + 5*stride] + 2*QP) > 4*QP) return 0;
        if((unsigned)(src[1+x + 2*stride] - src[1+x + 7*stride] + 2*QP) > 4*QP) return 0;
        if((unsigned)(src[2+x + 4*stride] - src[2+x + 1*stride] + 2*QP) > 4*QP) return 0;
        if((unsigned)(src[3+x + 6*stride] - src[3+x + 3*stride] + 2*QP) > 4*QP) return 0;
    }
    return 1;
}

static inline int horizClassify_C(const uint8_t src[], int stride, const PPContext *c)
{
    if( isHorizDC_C(src, stride, c) ){
        return isHorizMinMaxOk_C(src, stride, c->QP);
    }else{
        return 2;
    }
}

static inline int vertClassify_C(const uint8_t src[], int stride, const PPContext *c)
{
    if( isVertDC_C(src, stride, c) ){
        return isVertMinMaxOk_C(src, stride, c->QP);
    }else{
        return 2;
    }
}

static inline void doHorizDefFilter_C(uint8_t dst[], int stride, const PPContext *c)
{
    int y;
    for(y=0; y<BLOCK_SIZE; y++){
        const int middleEnergy= 5*(dst[4] - dst[3]) + 2*(dst[2] - dst[5]);

        if(FFABS(middleEnergy) < 8*c->QP){
            const int q=(dst[3] - dst[4])/2;
            const int leftEnergy=  5*(dst[2] - dst[1]) + 2*(dst[0] - dst[3]);
            const int rightEnergy= 5*(dst[6] - dst[5]) + 2*(dst[4] - dst[7]);

            int d= FFABS(middleEnergy) - FFMIN( FFABS(leftEnergy), FFABS(rightEnergy) );
            d= FFMAX(d, 0);

            d= (5*d + 32) >> 6;
            d*= FFSIGN(-middleEnergy);

            if(q>0)
            {
                d = FFMAX(d, 0);
                d = FFMIN(d, q);
            }
            else
            {
                d = FFMIN(d, 0);
                d = FFMAX(d, q);
            }

            dst[3]-= d;
            dst[4]+= d;
        }
        dst+= stride;
    }
}

/**
 * Do a horizontal low pass filter on the 10x8 block (dst points to middle 8x8 Block)
 * using the 9-Tap Filter (1,1,2,2,4,2,2,1,1)/16 (C version)
 */
static inline void doHorizLowPass_C(uint8_t dst[], int stride, const PPContext *c)
{
    int y;
    for(y=0; y<BLOCK_SIZE; y++){
        const int first= FFABS(dst[-1] - dst[0]) < c->QP ? dst[-1] : dst[0];
        const int last= FFABS(dst[8] - dst[7]) < c->QP ? dst[8] : dst[7];

        int sums[10];
        sums[0] = 4*first + dst[0] + dst[1] + dst[2] + 4;
        sums[1] = sums[0] - first  + dst[3];
        sums[2] = sums[1] - first  + dst[4];
        sums[3] = sums[2] - first  + dst[5];
        sums[4] = sums[3] - first  + dst[6];
        sums[5] = sums[4] - dst[0] + dst[7];
        sums[6] = sums[5] - dst[1] + last;
        sums[7] = sums[6] - dst[2] + last;
        sums[8] = sums[7] - dst[3] + last;
        sums[9] = sums[8] - dst[4] + last;

        dst[0]= (sums[0] + sums[2] + 2*dst[0])>>4;
        dst[1]= (sums[1] + sums[3] + 2*dst[1])>>4;
        dst[2]= (sums[2] + sums[4] + 2*dst[2])>>4;
        dst[3]= (sums[3] + sums[5] + 2*dst[3])>>4;
        dst[4]= (sums[4] + sums[6] + 2*dst[4])>>4;
        dst[5]= (sums[5] + sums[7] + 2*dst[5])>>4;
        dst[6]= (sums[6] + sums[8] + 2*dst[6])>>4;
        dst[7]= (sums[7] + sums[9] + 2*dst[7])>>4;

        dst+= stride;
    }
}

/**
 * Experimental Filter 1 (Horizontal)
 * will not damage linear gradients
 * Flat blocks should look like they were passed through the (1,1,2,2,4,2,2,1,1) 9-Tap filter
 * can only smooth blocks at the expected locations (it cannot smooth them if they did move)
 * MMX2 version does correct clipping C version does not
 * not identical with the vertical one
 */
static inline void horizX1Filter(uint8_t *src, int stride, int QP)
{
    int y;
    static uint64_t lut[256];
    if(!lut[255])
    {
        int i;
        for(i=0; i<256; i++)
        {
            int v= i < 128 ? 2*i : 2*(i-256);
/*
//Simulate 112242211 9-Tap filter
            uint64_t a= (v/16)  & 0xFF;
            uint64_t b= (v/8)   & 0xFF;
            uint64_t c= (v/4)   & 0xFF;
            uint64_t d= (3*v/8) & 0xFF;
*/
//Simulate piecewise linear interpolation
            uint64_t a= (v/16)   & 0xFF;
            uint64_t b= (v*3/16) & 0xFF;
            uint64_t c= (v*5/16) & 0xFF;
            uint64_t d= (7*v/16) & 0xFF;
            uint64_t A= (0x100 - a)&0xFF;
            uint64_t B= (0x100 - b)&0xFF;
            uint64_t C= (0x100 - c)&0xFF;
            uint64_t D= (0x100 - c)&0xFF;

            lut[i]   = (a<<56) | (b<<48) | (c<<40) | (d<<32) |
                       (D<<24) | (C<<16) | (B<<8)  | (A);
            //lut[i] = (v<<32) | (v<<24);
        }
    }

    for(y=0; y<BLOCK_SIZE; y++){
        int a= src[1] - src[2];
        int b= src[3] - src[4];
        int c= src[5] - src[6];

        int d= FFMAX(FFABS(b) - (FFABS(a) + FFABS(c))/2, 0);

        if(d < QP){
            int v = d * FFSIGN(-b);

            src[1] +=v/8;
            src[2] +=v/4;
            src[3] +=3*v/8;
            src[4] -=3*v/8;
            src[5] -=v/4;
            src[6] -=v/8;
        }
        src+=stride;
    }
}

/**
 * accurate deblock filter
 */
static av_always_inline void do_a_deblock_C(uint8_t *src, int step,
                                            int stride, const PPContext *c, int mode)
{
    int y;
    const int QP= c->QP;
    const int dcOffset= ((c->nonBQP*c->ppMode.baseDcDiff)>>8) + 1;
    const int dcThreshold= dcOffset*2 + 1;

    src+= step*4; // src points to begin of the 8x8 Block
    for(y=0; y<8; y++){
        int numEq= 0;

        numEq += ((unsigned)(src[-1*step] - src[0*step] + dcOffset)) < dcThreshold;
        numEq += ((unsigned)(src[ 0*step] - src[1*step] + dcOffset)) < dcThreshold;
        numEq += ((unsigned)(src[ 1*step] - src[2*step] + dcOffset)) < dcThreshold;
        numEq += ((unsigned)(src[ 2*step] - src[3*step] + dcOffset)) < dcThreshold;
        numEq += ((unsigned)(src[ 3*step] - src[4*step] + dcOffset)) < dcThreshold;
        numEq += ((unsigned)(src[ 4*step] - src[5*step] + dcOffset)) < dcThreshold;
        numEq += ((unsigned)(src[ 5*step] - src[6*step] + dcOffset)) < dcThreshold;
        numEq += ((unsigned)(src[ 6*step] - src[7*step] + dcOffset)) < dcThreshold;
        numEq += ((unsigned)(src[ 7*step] - src[8*step] + dcOffset)) < dcThreshold;
        if(numEq > c->ppMode.flatnessThreshold){
            int min, max, x;

            if(src[0] > src[step]){
                max= src[0];
                min= src[step];
            }else{
                max= src[step];
                min= src[0];
            }
            for(x=2; x<8; x+=2){
                if(src[x*step] > src[(x+1)*step]){
                        if(src[x    *step] > max) max= src[ x   *step];
                        if(src[(x+1)*step] < min) min= src[(x+1)*step];
                }else{
                        if(src[(x+1)*step] > max) max= src[(x+1)*step];
                        if(src[ x   *step] < min) min= src[ x   *step];
                }
            }
            if(max-min < 2*QP){
                const int first= FFABS(src[-1*step] - src[0]) < QP ? src[-1*step] : src[0];
                const int last= FFABS(src[8*step] - src[7*step]) < QP ? src[8*step] : src[7*step];

                int sums[10];
                sums[0] = 4*first + src[0*step] + src[1*step] + src[2*step] + 4;
                sums[1] = sums[0] - first       + src[3*step];
                sums[2] = sums[1] - first       + src[4*step];
                sums[3] = sums[2] - first       + src[5*step];
                sums[4] = sums[3] - first       + src[6*step];
                sums[5] = sums[4] - src[0*step] + src[7*step];
                sums[6] = sums[5] - src[1*step] + last;
                sums[7] = sums[6] - src[2*step] + last;
                sums[8] = sums[7] - src[3*step] + last;
                sums[9] = sums[8] - src[4*step] + last;

                if (mode & VISUALIZE) {
                    src[0*step] =
                    src[1*step] =
                    src[2*step] =
                    src[3*step] =
                    src[4*step] =
                    src[5*step] =
                    src[6*step] =
                    src[7*step] = 128;
                }
                src[0*step]= (sums[0] + sums[2] + 2*src[0*step])>>4;
                src[1*step]= (sums[1] + sums[3] + 2*src[1*step])>>4;
                src[2*step]= (sums[2] + sums[4] + 2*src[2*step])>>4;
                src[3*step]= (sums[3] + sums[5] + 2*src[3*step])>>4;
                src[4*step]= (sums[4] + sums[6] + 2*src[4*step])>>4;
                src[5*step]= (sums[5] + sums[7] + 2*src[5*step])>>4;
                src[6*step]= (sums[6] + sums[8] + 2*src[6*step])>>4;
                src[7*step]= (sums[7] + sums[9] + 2*src[7*step])>>4;
            }
        }else{
            const int middleEnergy= 5*(src[4*step] - src[3*step]) + 2*(src[2*step] - src[5*step]);

            if(FFABS(middleEnergy) < 8*QP){
                const int q=(src[3*step] - src[4*step])/2;
                const int leftEnergy=  5*(src[2*step] - src[1*step]) + 2*(src[0*step] - src[3*step]);
                const int rightEnergy= 5*(src[6*step] - src[5*step]) + 2*(src[4*step] - src[7*step]);

                int d= FFABS(middleEnergy) - FFMIN( FFABS(leftEnergy), FFABS(rightEnergy) );
                d= FFMAX(d, 0);

                d= (5*d + 32) >> 6;
                d*= FFSIGN(-middleEnergy);

                if(q>0){
                    d = FFMAX(d, 0);
                    d = FFMIN(d, q);
                }else{
                    d = FFMIN(d, 0);
                    d = FFMAX(d, q);
                }

                if ((mode & VISUALIZE) && d) {
                    d= (d < 0) ? 32 : -32;
                    src[3*step]= av_clip_uint8(src[3*step] - d);
                    src[4*step]= av_clip_uint8(src[4*step] + d);
                    d = 0;
                }

                src[3*step]-= d;
                src[4*step]+= d;
            }
        }

        src += stride;
    }
}

//Note: we have C and SSE2 version (which uses MMX(EXT) when advantageous)
//Plain C versions
//we always compile C for testing which needs bitexactness
#define TEMPLATE_PP_C 1
#include "postprocess_template.c"

#if HAVE_ALTIVEC
#include "libavutil/ppc/util_altivec.h"

#   define TEMPLATE_PP_ALTIVEC 1
#   include "postprocess_altivec_template.c"
#   include "postprocess_template.c"
#endif

#if ARCH_X86 && HAVE_INLINE_ASM
#    if CONFIG_RUNTIME_CPUDETECT
#        define TEMPLATE_PP_SSE2 1
#        include "postprocess_template.c"
#    else
#        if HAVE_SSE2_INLINE
#            define TEMPLATE_PP_SSE2 1
#            include "postprocess_template.c"
#        endif
#    endif
#endif

typedef void (*pp_fn)(const uint8_t src[], int srcStride, uint8_t dst[], int dstStride, int width, int height,
                      const int8_t QPs[], int QPStride, int isColor, PPContext *c2);

static inline void postProcess(const uint8_t src[], int srcStride, uint8_t dst[], int dstStride, int width, int height,
        const int8_t QPs[], int QPStride, int isColor, pp_mode *vm, pp_context *vc)
{
    pp_fn pp = postProcess_C;
    PPContext *c= (PPContext *)vc;
    PPMode *ppMode= (PPMode *)vm;
    c->ppMode= *ppMode; //FIXME

    if (!(ppMode->lumMode & BITEXACT)) {
#if CONFIG_RUNTIME_CPUDETECT
#if ARCH_X86 && HAVE_INLINE_ASM
        // ordered per speed fastest first
        if      (c->cpuCaps & AV_CPU_FLAG_SSE2)     pp = postProcess_SSE2;
#elif HAVE_ALTIVEC
        if      (c->cpuCaps & AV_CPU_FLAG_ALTIVEC)  pp = postProcess_altivec;
#endif
#else /* CONFIG_RUNTIME_CPUDETECT */
#if     HAVE_SSE2_INLINE
        pp = postProcess_SSE2;
#elif HAVE_ALTIVEC
        pp = postProcess_altivec;
#endif
#endif /* !CONFIG_RUNTIME_CPUDETECT */
    }

    pp(src, srcStride, dst, dstStride, width, height, QPs, QPStride, isColor, c);
}

/* -pp Command line Help
*/
const char pp_help[] =
"Available postprocessing filters:\n"
"Filters                        Options\n"
"short  long name       short   long option     Description\n"
"*      *               a       autoq           CPU power dependent enabler\n"
"                       c       chrom           chrominance filtering enabled\n"
"                       y       nochrom         chrominance filtering disabled\n"
"                       n       noluma          luma filtering disabled\n"
"hb     hdeblock        (2 threshold)           horizontal deblocking filter\n"
"       1. difference factor: default=32, higher -> more deblocking\n"
"       2. flatness threshold: default=39, lower -> more deblocking\n"
"                       the h & v deblocking filters share these\n"
"                       so you can't set different thresholds for h / v\n"
"vb     vdeblock        (2 threshold)           vertical deblocking filter\n"
"ha     hadeblock       (2 threshold)           horizontal deblocking filter\n"
"va     vadeblock       (2 threshold)           vertical deblocking filter\n"
"h1     x1hdeblock                              experimental h deblock filter 1\n"
"v1     x1vdeblock                              experimental v deblock filter 1\n"
"dr     dering                                  deringing filter\n"
"al     autolevels                              automatic brightness / contrast\n"
"                       f        fullyrange     stretch luminance to (0..255)\n"
"lb     linblenddeint                           linear blend deinterlacer\n"
"li     linipoldeint                            linear interpolating deinterlace\n"
"ci     cubicipoldeint                          cubic interpolating deinterlacer\n"
"md     mediandeint                             median deinterlacer\n"
"fd     ffmpegdeint                             ffmpeg deinterlacer\n"
"l5     lowpass5                                FIR lowpass deinterlacer\n"
"de     default                                 hb:a,vb:a,dr:a\n"
"fa     fast                                    h1:a,v1:a,dr:a\n"
"ac                                             ha:a:128:7,va:a,dr:a\n"
"tn     tmpnoise        (3 threshold)           temporal noise reducer\n"
"                     1. <= 2. <= 3.            larger -> stronger filtering\n"
"fq     forceQuant      <quantizer>             force quantizer\n"
"Usage:\n"
"<filterName>[:<option>[:<option>...]][[,|/][-]<filterName>[:<option>...]]...\n"
"long form example:\n"
"vdeblock:autoq/hdeblock:autoq/linblenddeint    default,-vdeblock\n"
"short form example:\n"
"vb:a/hb:a/lb                                   de,-vb\n"
"more examples:\n"
"tn:64:128:256\n"
"\n"
;

pp_mode *pp_get_mode_by_name_and_quality(const char *name, int quality)
{
    char temp[GET_MODE_BUFFER_SIZE];
    char *p= temp;
    static const char filterDelimiters[] = ",/";
    static const char optionDelimiters[] = ":|";
    struct PPMode *ppMode;
    char *filterToken;

    if (!name)  {
        av_log(NULL, AV_LOG_ERROR, "pp: Missing argument\n");
        return NULL;
    }

    if (!strcmp(name, "help")) {
        const char *p;
        for (p = pp_help; strchr(p, '\n'); p = strchr(p, '\n') + 1) {
            av_strlcpy(temp, p, FFMIN(sizeof(temp), strchr(p, '\n') - p + 2));
            av_log(NULL, AV_LOG_INFO, "%s", temp);
        }
        return NULL;
    }

    ppMode= av_malloc(sizeof(PPMode));
    if (!ppMode)
        return NULL;

    ppMode->lumMode= 0;
    ppMode->chromMode= 0;
    ppMode->maxTmpNoise[0]= 700;
    ppMode->maxTmpNoise[1]= 1500;
    ppMode->maxTmpNoise[2]= 3000;
    ppMode->maxAllowedY= 234;
    ppMode->minAllowedY= 16;
    ppMode->baseDcDiff= 256/8;
    ppMode->flatnessThreshold= 56-16-1;
    ppMode->maxClippedThreshold= (AVRational){1,100};
    ppMode->error=0;

    memset(temp, 0, GET_MODE_BUFFER_SIZE);
    av_strlcpy(temp, name, GET_MODE_BUFFER_SIZE - 1);

    av_log(NULL, AV_LOG_DEBUG, "pp: %s\n", name);

    for(;;){
        const char *filterName;
        int q= 1000000; //PP_QUALITY_MAX;
        int chrom=-1;
        int luma=-1;
        const char *option;
        const char *options[OPTIONS_ARRAY_SIZE];
        int i;
        int filterNameOk=0;
        int numOfUnknownOptions=0;
        int enable=1; //does the user want us to enabled or disabled the filter
        char *tokstate;

        filterToken= av_strtok(p, filterDelimiters, &tokstate);
        if(!filterToken) break;
        p+= strlen(filterToken) + 1; // p points to next filterToken
        filterName= av_strtok(filterToken, optionDelimiters, &tokstate);
        if (!filterName) {
            ppMode->error++;
            break;
        }
        av_log(NULL, AV_LOG_DEBUG, "pp: %s::%s\n", filterToken, filterName);

        if(*filterName == '-'){
            enable=0;
            filterName++;
        }

        for(;;){ //for all options
            option= av_strtok(NULL, optionDelimiters, &tokstate);
            if(!option) break;

            av_log(NULL, AV_LOG_DEBUG, "pp: option: %s\n", option);
            if(!strcmp("autoq", option) || !strcmp("a", option)) q= quality;
            else if(!strcmp("nochrom", option) || !strcmp("y", option)) chrom=0;
            else if(!strcmp("chrom", option) || !strcmp("c", option)) chrom=1;
            else if(!strcmp("noluma", option) || !strcmp("n", option)) luma=0;
            else{
                options[numOfUnknownOptions] = option;
                numOfUnknownOptions++;
            }
            if(numOfUnknownOptions >= OPTIONS_ARRAY_SIZE-1) break;
        }
        options[numOfUnknownOptions] = NULL;

        /* replace stuff from the replace Table */
        for(i=0; replaceTable[2*i]; i++){
            if(!strcmp(replaceTable[2*i], filterName)){
                size_t newlen = strlen(replaceTable[2*i + 1]);
                int plen;
                int spaceLeft;

                p--, *p=',';

                plen= strlen(p);
                spaceLeft= p - temp + plen;
                if(spaceLeft + newlen  >= GET_MODE_BUFFER_SIZE - 1){
                    ppMode->error++;
                    break;
                }
                memmove(p + newlen, p, plen+1);
                memcpy(p, replaceTable[2*i + 1], newlen);
                filterNameOk=1;
            }
        }

        for(i=0; filters[i].shortName; i++){
            if(   !strcmp(filters[i].longName, filterName)
               || !strcmp(filters[i].shortName, filterName)){
                ppMode->lumMode &= ~filters[i].mask;
                ppMode->chromMode &= ~filters[i].mask;

                filterNameOk=1;
                if(!enable) break; // user wants to disable it

                if(q >= filters[i].minLumQuality && luma)
                    ppMode->lumMode|= filters[i].mask;
                if(chrom==1 || (chrom==-1 && filters[i].chromDefault))
                    if(q >= filters[i].minChromQuality)
                            ppMode->chromMode|= filters[i].mask;

                if(filters[i].mask == LEVEL_FIX){
                    int o;
                    ppMode->minAllowedY= 16;
                    ppMode->maxAllowedY= 234;
                    for(o=0; options[o]; o++){
                        if(  !strcmp(options[o],"fullyrange")
                           ||!strcmp(options[o],"f")){
                            ppMode->minAllowedY= 0;
                            ppMode->maxAllowedY= 255;
                            numOfUnknownOptions--;
                        }
                    }
                }
                else if(filters[i].mask == TEMP_NOISE_FILTER)
                {
                    int o;
                    int numOfNoises=0;

                    for(o=0; options[o]; o++){
                        char *tail;
                        ppMode->maxTmpNoise[numOfNoises]=
                            strtol(options[o], &tail, 0);
                        if(tail!=options[o]){
                            numOfNoises++;
                            numOfUnknownOptions--;
                            if(numOfNoises >= 3) break;
                        }
                    }
                }
                else if(filters[i].mask == V_DEBLOCK   || filters[i].mask == H_DEBLOCK
                     || filters[i].mask == V_A_DEBLOCK || filters[i].mask == H_A_DEBLOCK){
                    int o;

                    for(o=0; options[o] && o<2; o++){
                        char *tail;
                        int val= strtol(options[o], &tail, 0);
                        if(tail==options[o]) break;

                        numOfUnknownOptions--;
                        if(o==0) ppMode->baseDcDiff= val;
                        else ppMode->flatnessThreshold= val;
                    }
                }
                else if(filters[i].mask == FORCE_QUANT){
                    int o;
                    ppMode->forcedQuant= 15;

                    for(o=0; options[o] && o<1; o++){
                        char *tail;
                        int val= strtol(options[o], &tail, 0);
                        if(tail==options[o]) break;

                        numOfUnknownOptions--;
                        ppMode->forcedQuant= val;
                    }
                }
            }
        }
        if(!filterNameOk) ppMode->error++;
        ppMode->error += numOfUnknownOptions;
    }

    av_log(NULL, AV_LOG_DEBUG, "pp: lumMode=%X, chromMode=%X\n", ppMode->lumMode, ppMode->chromMode);
    if(ppMode->error){
        av_log(NULL, AV_LOG_ERROR, "%d errors in postprocess string \"%s\"\n", ppMode->error, name);
        av_free(ppMode);
        return NULL;
    }
    return ppMode;
}

void pp_free_mode(pp_mode *mode){
    av_free(mode);
}

static void reallocAlign(void **p, int size){
    av_free(*p);
    *p= av_mallocz(size);
}

static void reallocBuffers(PPContext *c, int width, int height, int stride, int qpStride){
    int mbWidth = (width+15)>>4;
    int mbHeight= (height+15)>>4;
    int i;

    c->stride= stride;
    c->qpStride= qpStride;

    reallocAlign((void **)&c->tempDst, stride*24+32);
    reallocAlign((void **)&c->tempSrc, stride*24);
    reallocAlign((void **)&c->tempBlocks, 2*16*8);
    reallocAlign((void **)&c->yHistogram, 256*sizeof(uint64_t));
    for(i=0; i<256; i++)
            c->yHistogram[i]= width*height/64*15/256;

    for(i=0; i<3; i++){
        //Note: The +17*1024 is just there so I do not have to worry about r/w over the end.
        reallocAlign((void **)&c->tempBlurred[i], stride*mbHeight*16 + 17*1024);
        reallocAlign((void **)&c->tempBlurredPast[i], 256*((height+7)&(~7))/2 + 17*1024);//FIXME size
    }

    reallocAlign((void **)&c->deintTemp, 2*width+32);
    reallocAlign((void **)&c->nonBQPTable, qpStride*mbHeight*sizeof(int8_t));
    reallocAlign((void **)&c->stdQPTable, qpStride*mbHeight*sizeof(int8_t));
    reallocAlign((void **)&c->forcedQPTable, mbWidth*sizeof(int8_t));
}

static const char * context_to_name(void * ptr) {
    return "postproc";
}

static const AVClass av_codec_context_class = { "Postproc", context_to_name, NULL };

av_cold pp_context *pp_get_context(int width, int height, int cpuCaps){
    PPContext *c= av_mallocz(sizeof(PPContext));
    int stride= FFALIGN(width, 16);  //assumed / will realloc if needed
    int qpStride= (width+15)/16 + 2; //assumed / will realloc if needed

    if (!c)
        return NULL;

    c->av_class = &av_codec_context_class;
    if(cpuCaps&PP_FORMAT){
        c->hChromaSubSample= cpuCaps&0x3;
        c->vChromaSubSample= (cpuCaps>>4)&0x3;
    }else{
        c->hChromaSubSample= 1;
        c->vChromaSubSample= 1;
    }
    if (cpuCaps & PP_CPU_CAPS_AUTO) {
        c->cpuCaps = av_get_cpu_flags();
    } else {
        c->cpuCaps = 0;
        if (cpuCaps & PP_CPU_CAPS_ALTIVEC)  c->cpuCaps |= AV_CPU_FLAG_ALTIVEC;
    }

    reallocBuffers(c, width, height, stride, qpStride);

    c->frameNum=-1;

    return c;
}

av_cold void pp_free_context(void *vc){
    PPContext *c = (PPContext*)vc;
    int i;

    for(i=0; i<FF_ARRAY_ELEMS(c->tempBlurred); i++)
        av_free(c->tempBlurred[i]);
    for(i=0; i<FF_ARRAY_ELEMS(c->tempBlurredPast); i++)
        av_free(c->tempBlurredPast[i]);

    av_free(c->tempBlocks);
    av_free(c->yHistogram);
    av_free(c->tempDst);
    av_free(c->tempSrc);
    av_free(c->deintTemp);
    av_free(c->stdQPTable);
    av_free(c->nonBQPTable);
    av_free(c->forcedQPTable);

    memset(c, 0, sizeof(PPContext));

    av_free(c);
}

void  pp_postprocess(const uint8_t * src[3], const int srcStride[3],
                     uint8_t * dst[3], const int dstStride[3],
                     int width, int height,
                     const int8_t *QP_store,  int QPStride,
                     pp_mode *vm,  void *vc, int pict_type)
{
    int mbWidth = (width+15)>>4;
    int mbHeight= (height+15)>>4;
    PPMode *mode = vm;
    PPContext *c = vc;
    int minStride= FFMAX(FFABS(srcStride[0]), FFABS(dstStride[0]));
    int absQPStride = FFABS(QPStride);

    // c->stride and c->QPStride are always positive
    if(c->stride < minStride || c->qpStride < absQPStride)
        reallocBuffers(c, width, height,
                       FFMAX(minStride, c->stride),
                       FFMAX(c->qpStride, absQPStride));

    if(!QP_store || (mode->lumMode & FORCE_QUANT)){
        int i;
        QP_store= c->forcedQPTable;
        absQPStride = QPStride = 0;
        if(mode->lumMode & FORCE_QUANT)
            for(i=0; i<mbWidth; i++) c->forcedQPTable[i]= mode->forcedQuant;
        else
            for(i=0; i<mbWidth; i++) c->forcedQPTable[i]= 1;
    }

    if(pict_type & PP_PICT_TYPE_QP2){
        int i;
        const int count= FFMAX(mbHeight * absQPStride, mbWidth);
        for(i=0; i<(count>>2); i++){
            AV_WN32(c->stdQPTable + (i<<2), AV_RN32(QP_store + (i<<2)) >> 1 & 0x7F7F7F7F);
        }
        for(i<<=2; i<count; i++){
            c->stdQPTable[i] = QP_store[i]>>1;
        }
        QP_store= c->stdQPTable;
        QPStride= absQPStride;
    }

    if(0){
        int x,y;
        for(y=0; y<mbHeight; y++){
            for(x=0; x<mbWidth; x++){
                av_log(c, AV_LOG_INFO, "%2d ", QP_store[x + y*QPStride]);
            }
            av_log(c, AV_LOG_INFO, "\n");
        }
        av_log(c, AV_LOG_INFO, "\n");
    }

    if((pict_type&7)!=3){
        if (QPStride >= 0){
            int i;
            const int count= FFMAX(mbHeight * QPStride, mbWidth);
            for(i=0; i<(count>>2); i++){
                AV_WN32(c->nonBQPTable + (i<<2), AV_RN32(QP_store + (i<<2)) & 0x3F3F3F3F);
            }
            for(i<<=2; i<count; i++){
                c->nonBQPTable[i] = QP_store[i] & 0x3F;
            }
        } else {
            int i,j;
            for(i=0; i<mbHeight; i++) {
                for(j=0; j<absQPStride; j++) {
                    c->nonBQPTable[i*absQPStride+j] = QP_store[i*QPStride+j] & 0x3F;
                }
            }
        }
    }

    av_log(c, AV_LOG_DEBUG, "using npp filters 0x%X/0x%X\n",
           mode->lumMode, mode->chromMode);

    postProcess(src[0], srcStride[0], dst[0], dstStride[0],
                width, height, QP_store, QPStride, 0, mode, c);

    if (!(src[1] && src[2] && dst[1] && dst[2]))
        return;

    width  = (width )>>c->hChromaSubSample;
    height = (height)>>c->vChromaSubSample;

    if(mode->chromMode){
        postProcess(src[1], srcStride[1], dst[1], dstStride[1],
                    width, height, QP_store, QPStride, 1, mode, c);
        postProcess(src[2], srcStride[2], dst[2], dstStride[2],
                    width, height, QP_store, QPStride, 2, mode, c);
    }
    else if(srcStride[1] == dstStride[1] && srcStride[2] == dstStride[2]){
        linecpy(dst[1], src[1], height, srcStride[1]);
        linecpy(dst[2], src[2], height, srcStride[2]);
    }else{
        int y;
        for(y=0; y<height; y++){
            memcpy(&(dst[1][y*dstStride[1]]), &(src[1][y*srcStride[1]]), width);
            memcpy(&(dst[2][y*dstStride[2]]), &(src[2][y*srcStride[2]]), width);
        }
    }
}
