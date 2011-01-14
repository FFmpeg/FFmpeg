/*
 * Copyright (C) 2003 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*
 * This implementation is based on an algorithm described in
 * "Aria Nosratinia Embedded Post-Processing for
 * Enhancement of Compressed Images (1999)"
 * (http://citeseer.nj.nec.com/nosratinia99embedded.html)
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

#include "config.h"

#include "mp_msg.h"
#include "cpudetect.h"

#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/dsputil.h"

#undef fprintf
#undef free
#undef malloc

#include "img_format.h"
#include "mp_image.h"
#include "vf.h"
#include "vd_ffmpeg.h"
#include "libvo/fastmemcpy.h"

#define XMIN(a,b) ((a) < (b) ? (a) : (b))

//===========================================================================//
static const uint8_t  __attribute__((aligned(8))) dither[8][8]={
{  0,  48,  12,  60,   3,  51,  15,  63, },
{ 32,  16,  44,  28,  35,  19,  47,  31, },
{  8,  56,   4,  52,  11,  59,   7,  55, },
{ 40,  24,  36,  20,  43,  27,  39,  23, },
{  2,  50,  14,  62,   1,  49,  13,  61, },
{ 34,  18,  46,  30,  33,  17,  45,  29, },
{ 10,  58,   6,  54,   9,  57,   5,  53, },
{ 42,  26,  38,  22,  41,  25,  37,  21, },
};

static const uint8_t offset[127][2]= {
{0,0},
{0,0}, {4,4},
{0,0}, {2,2}, {6,4}, {4,6},
{0,0}, {5,1}, {2,2}, {7,3}, {4,4}, {1,5}, {6,6}, {3,7},

{0,0}, {4,0}, {1,1}, {5,1}, {3,2}, {7,2}, {2,3}, {6,3},
{0,4}, {4,4}, {1,5}, {5,5}, {3,6}, {7,6}, {2,7}, {6,7},

{0,0}, {0,2}, {0,4}, {0,6}, {1,1}, {1,3}, {1,5}, {1,7},
{2,0}, {2,2}, {2,4}, {2,6}, {3,1}, {3,3}, {3,5}, {3,7},
{4,0}, {4,2}, {4,4}, {4,6}, {5,1}, {5,3}, {5,5}, {5,7},
{6,0}, {6,2}, {6,4}, {6,6}, {7,1}, {7,3}, {7,5}, {7,7},

{0,0}, {4,4}, {0,4}, {4,0}, {2,2}, {6,6}, {2,6}, {6,2},
{0,2}, {4,6}, {0,6}, {4,2}, {2,0}, {6,4}, {2,4}, {6,0},
{1,1}, {5,5}, {1,5}, {5,1}, {3,3}, {7,7}, {3,7}, {7,3},
{1,3}, {5,7}, {1,7}, {5,3}, {3,1}, {7,5}, {3,5}, {7,1},
{0,1}, {4,5}, {0,5}, {4,1}, {2,3}, {6,7}, {2,7}, {6,3},
{0,3}, {4,7}, {0,7}, {4,3}, {2,1}, {6,5}, {2,5}, {6,1},
{1,0}, {5,4}, {1,4}, {5,0}, {3,2}, {7,6}, {3,6}, {7,2},
{1,2}, {5,6}, {1,6}, {5,2}, {3,0}, {7,4}, {3,4}, {7,0},
};

struct vf_priv_s {
        int log2_count;
        int qp;
        int mode;
        int mpeg2;
        int temp_stride;
        uint8_t *src;
        int16_t *temp;
        AVCodecContext *avctx;
        DSPContext dsp;
        char *non_b_qp;
};

#define SHIFT 22

static void hardthresh_c(DCTELEM dst[64], DCTELEM src[64], int qp, uint8_t *permutation){
        int i;
        int bias= 0; //FIXME
        unsigned int threshold1, threshold2;

        threshold1= qp*((1<<4) - bias) - 1;
        threshold2= (threshold1<<1);

        memset(dst, 0, 64*sizeof(DCTELEM));
        dst[0]= (src[0] + 4)>>3;

        for(i=1; i<64; i++){
                int level= src[i];
                if(((unsigned)(level+threshold1))>threshold2){
                        const int j= permutation[i];
                        dst[j]= (level + 4)>>3;
                }
        }
}

static void softthresh_c(DCTELEM dst[64], DCTELEM src[64], int qp, uint8_t *permutation){
        int i;
        int bias= 0; //FIXME
        unsigned int threshold1, threshold2;

        threshold1= qp*((1<<4) - bias) - 1;
        threshold2= (threshold1<<1);

        memset(dst, 0, 64*sizeof(DCTELEM));
        dst[0]= (src[0] + 4)>>3;

        for(i=1; i<64; i++){
                int level= src[i];
                if(((unsigned)(level+threshold1))>threshold2){
                        const int j= permutation[i];
                        if(level>0)
                                dst[j]= (level - threshold1 + 4)>>3;
                        else
                                dst[j]= (level + threshold1 + 4)>>3;
                }
        }
}

#if HAVE_MMX
static void hardthresh_mmx(DCTELEM dst[64], DCTELEM src[64], int qp, uint8_t *permutation){
        int bias= 0; //FIXME
        unsigned int threshold1;

        threshold1= qp*((1<<4) - bias) - 1;

        __asm__ volatile(
#define REQUANT_CORE(dst0, dst1, dst2, dst3, src0, src1, src2, src3) \
                "movq " #src0 ", %%mm0        \n\t"\
                "movq " #src1 ", %%mm1        \n\t"\
                "movq " #src2 ", %%mm2        \n\t"\
                "movq " #src3 ", %%mm3        \n\t"\
                "psubw %%mm4, %%mm0        \n\t"\
                "psubw %%mm4, %%mm1        \n\t"\
                "psubw %%mm4, %%mm2        \n\t"\
                "psubw %%mm4, %%mm3        \n\t"\
                "paddusw %%mm5, %%mm0        \n\t"\
                "paddusw %%mm5, %%mm1        \n\t"\
                "paddusw %%mm5, %%mm2        \n\t"\
                "paddusw %%mm5, %%mm3        \n\t"\
                "paddw %%mm6, %%mm0        \n\t"\
                "paddw %%mm6, %%mm1        \n\t"\
                "paddw %%mm6, %%mm2        \n\t"\
                "paddw %%mm6, %%mm3        \n\t"\
                "psubusw %%mm6, %%mm0        \n\t"\
                "psubusw %%mm6, %%mm1        \n\t"\
                "psubusw %%mm6, %%mm2        \n\t"\
                "psubusw %%mm6, %%mm3        \n\t"\
                "psraw $3, %%mm0        \n\t"\
                "psraw $3, %%mm1        \n\t"\
                "psraw $3, %%mm2        \n\t"\
                "psraw $3, %%mm3        \n\t"\
\
                "movq %%mm0, %%mm7        \n\t"\
                "punpcklwd %%mm2, %%mm0        \n\t" /*A*/\
                "punpckhwd %%mm2, %%mm7        \n\t" /*C*/\
                "movq %%mm1, %%mm2        \n\t"\
                "punpcklwd %%mm3, %%mm1        \n\t" /*B*/\
                "punpckhwd %%mm3, %%mm2        \n\t" /*D*/\
                "movq %%mm0, %%mm3        \n\t"\
                "punpcklwd %%mm1, %%mm0        \n\t" /*A*/\
                "punpckhwd %%mm7, %%mm3        \n\t" /*C*/\
                "punpcklwd %%mm2, %%mm7        \n\t" /*B*/\
                "punpckhwd %%mm2, %%mm1        \n\t" /*D*/\
\
                "movq %%mm0, " #dst0 "        \n\t"\
                "movq %%mm7, " #dst1 "        \n\t"\
                "movq %%mm3, " #dst2 "        \n\t"\
                "movq %%mm1, " #dst3 "        \n\t"

                "movd %2, %%mm4                \n\t"
                "movd %3, %%mm5                \n\t"
                "movd %4, %%mm6                \n\t"
                "packssdw %%mm4, %%mm4        \n\t"
                "packssdw %%mm5, %%mm5        \n\t"
                "packssdw %%mm6, %%mm6        \n\t"
                "packssdw %%mm4, %%mm4        \n\t"
                "packssdw %%mm5, %%mm5        \n\t"
                "packssdw %%mm6, %%mm6        \n\t"
                REQUANT_CORE(  (%1),  8(%1), 16(%1), 24(%1),  (%0), 8(%0), 64(%0), 72(%0))
                REQUANT_CORE(32(%1), 40(%1), 48(%1), 56(%1),16(%0),24(%0), 48(%0), 56(%0))
                REQUANT_CORE(64(%1), 72(%1), 80(%1), 88(%1),32(%0),40(%0), 96(%0),104(%0))
                REQUANT_CORE(96(%1),104(%1),112(%1),120(%1),80(%0),88(%0),112(%0),120(%0))
                : : "r" (src), "r" (dst), "g" (threshold1+1), "g" (threshold1+5), "g" (threshold1-4) //FIXME maybe more accurate then needed?
        );
        dst[0]= (src[0] + 4)>>3;
}

static void softthresh_mmx(DCTELEM dst[64], DCTELEM src[64], int qp, uint8_t *permutation){
        int bias= 0; //FIXME
        unsigned int threshold1;

        threshold1= qp*((1<<4) - bias) - 1;

        __asm__ volatile(
#undef REQUANT_CORE
#define REQUANT_CORE(dst0, dst1, dst2, dst3, src0, src1, src2, src3) \
                "movq " #src0 ", %%mm0        \n\t"\
                "movq " #src1 ", %%mm1        \n\t"\
                "pxor %%mm6, %%mm6        \n\t"\
                "pxor %%mm7, %%mm7        \n\t"\
                "pcmpgtw %%mm0, %%mm6        \n\t"\
                "pcmpgtw %%mm1, %%mm7        \n\t"\
                "pxor %%mm6, %%mm0        \n\t"\
                "pxor %%mm7, %%mm1        \n\t"\
                "psubusw %%mm4, %%mm0        \n\t"\
                "psubusw %%mm4, %%mm1        \n\t"\
                "pxor %%mm6, %%mm0        \n\t"\
                "pxor %%mm7, %%mm1        \n\t"\
                "movq " #src2 ", %%mm2        \n\t"\
                "movq " #src3 ", %%mm3        \n\t"\
                "pxor %%mm6, %%mm6        \n\t"\
                "pxor %%mm7, %%mm7        \n\t"\
                "pcmpgtw %%mm2, %%mm6        \n\t"\
                "pcmpgtw %%mm3, %%mm7        \n\t"\
                "pxor %%mm6, %%mm2        \n\t"\
                "pxor %%mm7, %%mm3        \n\t"\
                "psubusw %%mm4, %%mm2        \n\t"\
                "psubusw %%mm4, %%mm3        \n\t"\
                "pxor %%mm6, %%mm2        \n\t"\
                "pxor %%mm7, %%mm3        \n\t"\
\
                "paddsw %%mm5, %%mm0        \n\t"\
                "paddsw %%mm5, %%mm1        \n\t"\
                "paddsw %%mm5, %%mm2        \n\t"\
                "paddsw %%mm5, %%mm3        \n\t"\
                "psraw $3, %%mm0        \n\t"\
                "psraw $3, %%mm1        \n\t"\
                "psraw $3, %%mm2        \n\t"\
                "psraw $3, %%mm3        \n\t"\
\
                "movq %%mm0, %%mm7        \n\t"\
                "punpcklwd %%mm2, %%mm0        \n\t" /*A*/\
                "punpckhwd %%mm2, %%mm7        \n\t" /*C*/\
                "movq %%mm1, %%mm2        \n\t"\
                "punpcklwd %%mm3, %%mm1        \n\t" /*B*/\
                "punpckhwd %%mm3, %%mm2        \n\t" /*D*/\
                "movq %%mm0, %%mm3        \n\t"\
                "punpcklwd %%mm1, %%mm0        \n\t" /*A*/\
                "punpckhwd %%mm7, %%mm3        \n\t" /*C*/\
                "punpcklwd %%mm2, %%mm7        \n\t" /*B*/\
                "punpckhwd %%mm2, %%mm1        \n\t" /*D*/\
\
                "movq %%mm0, " #dst0 "        \n\t"\
                "movq %%mm7, " #dst1 "        \n\t"\
                "movq %%mm3, " #dst2 "        \n\t"\
                "movq %%mm1, " #dst3 "        \n\t"

                "movd %2, %%mm4                \n\t"
                "movd %3, %%mm5                \n\t"
                "packssdw %%mm4, %%mm4        \n\t"
                "packssdw %%mm5, %%mm5        \n\t"
                "packssdw %%mm4, %%mm4        \n\t"
                "packssdw %%mm5, %%mm5        \n\t"
                REQUANT_CORE(  (%1),  8(%1), 16(%1), 24(%1),  (%0), 8(%0), 64(%0), 72(%0))
                REQUANT_CORE(32(%1), 40(%1), 48(%1), 56(%1),16(%0),24(%0), 48(%0), 56(%0))
                REQUANT_CORE(64(%1), 72(%1), 80(%1), 88(%1),32(%0),40(%0), 96(%0),104(%0))
                REQUANT_CORE(96(%1),104(%1),112(%1),120(%1),80(%0),88(%0),112(%0),120(%0))
                : : "r" (src), "r" (dst), "g" (threshold1), "rm" (4) //FIXME maybe more accurate then needed?
        );

        dst[0]= (src[0] + 4)>>3;
}
#endif

static inline void add_block(int16_t *dst, int stride, DCTELEM block[64]){
        int y;

        for(y=0; y<8; y++){
                *(uint32_t*)&dst[0 + y*stride]+= *(uint32_t*)&block[0 + y*8];
                *(uint32_t*)&dst[2 + y*stride]+= *(uint32_t*)&block[2 + y*8];
                *(uint32_t*)&dst[4 + y*stride]+= *(uint32_t*)&block[4 + y*8];
                *(uint32_t*)&dst[6 + y*stride]+= *(uint32_t*)&block[6 + y*8];
        }
}

static void store_slice_c(uint8_t *dst, int16_t *src, int dst_stride, int src_stride, int width, int height, int log2_scale){
        int y, x;

#define STORE(pos) \
        temp= ((src[x + y*src_stride + pos]<<log2_scale) + d[pos])>>6;\
        if(temp & 0x100) temp= ~(temp>>31);\
        dst[x + y*dst_stride + pos]= temp;

        for(y=0; y<height; y++){
                const uint8_t *d= dither[y];
                for(x=0; x<width; x+=8){
                        int temp;
                        STORE(0);
                        STORE(1);
                        STORE(2);
                        STORE(3);
                        STORE(4);
                        STORE(5);
                        STORE(6);
                        STORE(7);
                }
        }
}

#if HAVE_MMX
static void store_slice_mmx(uint8_t *dst, int16_t *src, int dst_stride, int src_stride, int width, int height, int log2_scale){
        int y;

        for(y=0; y<height; y++){
                uint8_t *dst1= dst;
                int16_t *src1= src;
                __asm__ volatile(
                        "movq (%3), %%mm3        \n\t"
                        "movq (%3), %%mm4        \n\t"
                        "movd %4, %%mm2                \n\t"
                        "pxor %%mm0, %%mm0        \n\t"
                        "punpcklbw %%mm0, %%mm3        \n\t"
                        "punpckhbw %%mm0, %%mm4        \n\t"
                        "psraw %%mm2, %%mm3        \n\t"
                        "psraw %%mm2, %%mm4        \n\t"
                        "movd %5, %%mm2                \n\t"
                        "1:                        \n\t"
                        "movq (%0), %%mm0        \n\t"
                        "movq 8(%0), %%mm1        \n\t"
                        "paddw %%mm3, %%mm0        \n\t"
                        "paddw %%mm4, %%mm1        \n\t"
                        "psraw %%mm2, %%mm0        \n\t"
                        "psraw %%mm2, %%mm1        \n\t"
                        "packuswb %%mm1, %%mm0        \n\t"
                        "movq %%mm0, (%1)         \n\t"
                        "add $16, %0                \n\t"
                        "add $8, %1                \n\t"
                        "cmp %2, %1                \n\t"
                        " jb 1b                        \n\t"
                        : "+r" (src1), "+r"(dst1)
                        : "r"(dst + width), "r"(dither[y]), "g"(log2_scale), "g"(6-log2_scale)
                );
                src += src_stride;
                dst += dst_stride;
        }
//        if(width != mmxw)
//                store_slice_c(dst + mmxw, src + mmxw, dst_stride, src_stride, width - mmxw, log2_scale);
}
#endif

static void (*store_slice)(uint8_t *dst, int16_t *src, int dst_stride, int src_stride, int width, int height, int log2_scale)= store_slice_c;

static void (*requantize)(DCTELEM dst[64], DCTELEM src[64], int qp, uint8_t *permutation)= hardthresh_c;

static void filter(struct vf_priv_s *p, uint8_t *dst, uint8_t *src, int dst_stride, int src_stride, int width, int height, uint8_t *qp_store, int qp_stride, int is_luma){
        int x, y, i;
        const int count= 1<<p->log2_count;
        const int stride= is_luma ? p->temp_stride : ((width+16+15)&(~15));
        uint64_t __attribute__((aligned(16))) block_align[32];
        DCTELEM *block = (DCTELEM *)block_align;
        DCTELEM *block2= (DCTELEM *)(block_align+16);

        if (!src || !dst) return; // HACK avoid crash for Y8 colourspace
        for(y=0; y<height; y++){
                int index= 8 + 8*stride + y*stride;
                fast_memcpy(p->src + index, src + y*src_stride, width);
                for(x=0; x<8; x++){
                        p->src[index         - x - 1]= p->src[index +         x    ];
                        p->src[index + width + x    ]= p->src[index + width - x - 1];
                }
        }
        for(y=0; y<8; y++){
                fast_memcpy(p->src + (      7-y)*stride, p->src + (      y+8)*stride, stride);
                fast_memcpy(p->src + (height+8+y)*stride, p->src + (height-y+7)*stride, stride);
        }
        //FIXME (try edge emu)

        for(y=0; y<height+8; y+=8){
                memset(p->temp + (8+y)*stride, 0, 8*stride*sizeof(int16_t));
                for(x=0; x<width+8; x+=8){
                        const int qps= 3 + is_luma;
                        int qp;

                        if(p->qp)
                                qp= p->qp;
                        else{
                                qp= qp_store[ (XMIN(x, width-1)>>qps) + (XMIN(y, height-1)>>qps) * qp_stride];
                                qp = FFMAX(1, norm_qscale(qp, p->mpeg2));
                        }
                        for(i=0; i<count; i++){
                                const int x1= x + offset[i+count-1][0];
                                const int y1= y + offset[i+count-1][1];
                                const int index= x1 + y1*stride;
                                p->dsp.get_pixels(block, p->src + index, stride);
                                p->dsp.fdct(block);
                                requantize(block2, block, qp, p->dsp.idct_permutation);
                                p->dsp.idct(block2);
                                add_block(p->temp + index, stride, block2);
                        }
                }
                if(y)
                        store_slice(dst + (y-8)*dst_stride, p->temp + 8 + y*stride, dst_stride, stride, width, XMIN(8, height+8-y), 6-p->log2_count);
        }
#if 0
        for(y=0; y<height; y++){
                for(x=0; x<width; x++){
                        if((((x>>6) ^ (y>>6)) & 1) == 0)
                                dst[x + y*dst_stride]= p->src[8 + 8*stride  + x + y*stride];
                        if((x&63) == 0 || (y&63)==0)
                                dst[x + y*dst_stride] += 128;
                }
        }
#endif
        //FIXME reorder for better caching
}

static int config(struct vf_instance *vf,
        int width, int height, int d_width, int d_height,
        unsigned int flags, unsigned int outfmt){
        int h= (height+16+15)&(~15);

        vf->priv->temp_stride= (width+16+15)&(~15);
        vf->priv->temp= malloc(vf->priv->temp_stride*h*sizeof(int16_t));
        vf->priv->src = malloc(vf->priv->temp_stride*h*sizeof(uint8_t));

        return vf_next_config(vf,width,height,d_width,d_height,flags,outfmt);
}

static void get_image(struct vf_instance *vf, mp_image_t *mpi){
    if(mpi->flags&MP_IMGFLAG_PRESERVE) return; // don't change
    // ok, we can do pp in-place (or pp disabled):
    vf->dmpi=vf_get_image(vf->next,mpi->imgfmt,
        mpi->type, mpi->flags | MP_IMGFLAG_READABLE, mpi->width, mpi->height);
    mpi->planes[0]=vf->dmpi->planes[0];
    mpi->stride[0]=vf->dmpi->stride[0];
    mpi->width=vf->dmpi->width;
    if(mpi->flags&MP_IMGFLAG_PLANAR){
        mpi->planes[1]=vf->dmpi->planes[1];
        mpi->planes[2]=vf->dmpi->planes[2];
        mpi->stride[1]=vf->dmpi->stride[1];
        mpi->stride[2]=vf->dmpi->stride[2];
    }
    mpi->flags|=MP_IMGFLAG_DIRECT;
}

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts){
        mp_image_t *dmpi;

        if(!(mpi->flags&MP_IMGFLAG_DIRECT)){
                // no DR, so get a new image! hope we'll get DR buffer:
                dmpi=vf_get_image(vf->next,mpi->imgfmt,
                    MP_IMGTYPE_TEMP,
                    MP_IMGFLAG_ACCEPT_STRIDE|MP_IMGFLAG_PREFER_ALIGNED_STRIDE,
                    mpi->width,mpi->height);
                vf_clone_mpi_attributes(dmpi, mpi);
        }else{
           dmpi=vf->dmpi;
        }

        vf->priv->mpeg2= mpi->qscale_type;
        if(mpi->pict_type != 3 && mpi->qscale && !vf->priv->qp){
            int w = mpi->qstride;
            int h = (mpi->h + 15) >> 4;
            if (!w) {
                w = (mpi->w + 15) >> 4;
                h = 1;
            }
            if(!vf->priv->non_b_qp)
                vf->priv->non_b_qp= malloc(w*h);
            fast_memcpy(vf->priv->non_b_qp, mpi->qscale, w*h);
        }
        if(vf->priv->log2_count || !(mpi->flags&MP_IMGFLAG_DIRECT)){
            char *qp_tab= vf->priv->non_b_qp;
            if((vf->priv->mode&4) || !qp_tab)
                qp_tab= mpi->qscale;

            if(qp_tab || vf->priv->qp){
                filter(vf->priv, dmpi->planes[0], mpi->planes[0], dmpi->stride[0], mpi->stride[0], mpi->w, mpi->h, qp_tab, mpi->qstride, 1);
                filter(vf->priv, dmpi->planes[1], mpi->planes[1], dmpi->stride[1], mpi->stride[1], mpi->w>>mpi->chroma_x_shift, mpi->h>>mpi->chroma_y_shift, qp_tab, mpi->qstride, 0);
                filter(vf->priv, dmpi->planes[2], mpi->planes[2], dmpi->stride[2], mpi->stride[2], mpi->w>>mpi->chroma_x_shift, mpi->h>>mpi->chroma_y_shift, qp_tab, mpi->qstride, 0);
            }else{
                memcpy_pic(dmpi->planes[0], mpi->planes[0], mpi->w, mpi->h, dmpi->stride[0], mpi->stride[0]);
                memcpy_pic(dmpi->planes[1], mpi->planes[1], mpi->w>>mpi->chroma_x_shift, mpi->h>>mpi->chroma_y_shift, dmpi->stride[1], mpi->stride[1]);
                memcpy_pic(dmpi->planes[2], mpi->planes[2], mpi->w>>mpi->chroma_x_shift, mpi->h>>mpi->chroma_y_shift, dmpi->stride[2], mpi->stride[2]);
            }
        }

#if HAVE_MMX
        if(gCpuCaps.hasMMX) __asm__ volatile ("emms\n\t");
#endif
#if HAVE_MMX2
        if(gCpuCaps.hasMMX2) __asm__ volatile ("sfence\n\t");
#endif

        return vf_next_put_image(vf,dmpi, pts);
}

static void uninit(struct vf_instance *vf){
        if(!vf->priv) return;

        free(vf->priv->temp);
        vf->priv->temp= NULL;
        free(vf->priv->src);
        vf->priv->src= NULL;
        free(vf->priv->avctx);
        vf->priv->avctx= NULL;
        free(vf->priv->non_b_qp);
        vf->priv->non_b_qp= NULL;

        free(vf->priv);
        vf->priv=NULL;
}

//===========================================================================//
static int query_format(struct vf_instance *vf, unsigned int fmt){
    switch(fmt){
        case IMGFMT_YVU9:
        case IMGFMT_IF09:
        case IMGFMT_YV12:
        case IMGFMT_I420:
        case IMGFMT_IYUV:
        case IMGFMT_CLPL:
        case IMGFMT_Y800:
        case IMGFMT_Y8:
        case IMGFMT_444P:
        case IMGFMT_422P:
        case IMGFMT_411P:
            return vf_next_query_format(vf,fmt);
    }
    return 0;
}

static int control(struct vf_instance *vf, int request, void* data){
    switch(request){
    case VFCTRL_QUERY_MAX_PP_LEVEL:
        return 6;
    case VFCTRL_SET_PP_LEVEL:
        vf->priv->log2_count= *((unsigned int*)data);
        return CONTROL_TRUE;
    }
    return vf_next_control(vf,request,data);
}

static int vf_open(vf_instance_t *vf, char *args){

    int log2c=-1;

    vf->config=config;
    vf->put_image=put_image;
    vf->get_image=get_image;
    vf->query_format=query_format;
    vf->uninit=uninit;
    vf->control= control;
    vf->priv=malloc(sizeof(struct vf_priv_s));
    memset(vf->priv, 0, sizeof(struct vf_priv_s));

    init_avcodec();

    vf->priv->avctx= avcodec_alloc_context();
    dsputil_init(&vf->priv->dsp, vf->priv->avctx);

    vf->priv->log2_count= 3;

    if (args) sscanf(args, "%d:%d:%d", &log2c, &vf->priv->qp, &vf->priv->mode);

    if( log2c >=0 && log2c <=6 )
        vf->priv->log2_count = log2c;

    if(vf->priv->qp < 0)
        vf->priv->qp = 0;

    switch(vf->priv->mode&3){
        default:
        case 0: requantize= hardthresh_c; break;
        case 1: requantize= softthresh_c; break;
    }

#if HAVE_MMX
    if(gCpuCaps.hasMMX){
        store_slice= store_slice_mmx;
        switch(vf->priv->mode&3){
            case 0: requantize= hardthresh_mmx; break;
            case 1: requantize= softthresh_mmx; break;
        }
    }
#endif

    return 1;
}

const vf_info_t vf_info_spp = {
    "simple postprocess",
    "spp",
    "Michael Niedermayer",
    "",
    vf_open,
    NULL
};
