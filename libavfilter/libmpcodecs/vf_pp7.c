/*
 * Copyright (C) 2005 Michael Niedermayer <michaelni@gmx.at>
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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

#include "config.h"

#include "mp_msg.h"
#include "cpudetect.h"

#if HAVE_MALLOC_H
#include <malloc.h>
#endif

#include "libavutil/mem.h"

#include "img_format.h"
#include "mp_image.h"
#include "vf.h"
#include "libvo/fastmemcpy.h"

#define XMIN(a,b) ((a) < (b) ? (a) : (b))
#define XMAX(a,b) ((a) > (b) ? (a) : (b))

//===========================================================================//
DECLARE_ALIGNED(8, static const uint8_t, dither)[8][8] = {
{  0,  48,  12,  60,   3,  51,  15,  63, },
{ 32,  16,  44,  28,  35,  19,  47,  31, },
{  8,  56,   4,  52,  11,  59,   7,  55, },
{ 40,  24,  36,  20,  43,  27,  39,  23, },
{  2,  50,  14,  62,   1,  49,  13,  61, },
{ 34,  18,  46,  30,  33,  17,  45,  29, },
{ 10,  58,   6,  54,   9,  57,   5,  53, },
{ 42,  26,  38,  22,  41,  25,  37,  21, },
};

struct vf_priv_s {
    int qp;
    int mode;
    int mpeg2;
    int temp_stride;
    uint8_t *src;
};
#if 0
static inline void dct7_c(int16_t *dst, int s0, int s1, int s2, int s3, int step){
    int s, d;
    int dst2[64];
//#define S0 (1024/0.37796447300922719759)
#define C0 ((int)(1024*0.37796447300922719759+0.5)) //sqrt(1/7)
#define C1 ((int)(1024*0.53452248382484879308/6+0.5)) //sqrt(2/7)/6

#define C2 ((int)(1024*0.45221175985034745004/2+0.5))
#define C3 ((int)(1024*0.36264567479870879474/2+0.5))

//0.1962505182412941918 0.0149276808419397944-0.2111781990832339584
#define C4 ((int)(1024*0.1962505182412941918+0.5))
#define C5 ((int)(1024*0.0149276808419397944+0.5))
//#define C6 ((int)(1024*0.2111781990832339584+0.5))
#if 0
    s= s0 + s1 + s2;
    dst[0*step] = ((s + s3)*C0 + 512) >> 10;
    s= (s - 6*s3)*C1 + 512;
    d= (s0-s2)*C4 + (s1-s2)*C5;
    dst[1*step] = (s + 2*d)>>10;
    s -= d;
    d= (s1-s0)*C2 + (s1-s2)*C3;
    dst[2*step] = (s + d)>>10;
    dst[3*step] = (s - d)>>10;
#elif 1
    s = s3+s3;
    s3= s-s0;
    s0= s+s0;
    s = s2+s1;
    s2= s2-s1;
    dst[0*step]= s0 + s;
    dst[2*step]= s0 - s;
    dst[1*step]= 2*s3 +   s2;
    dst[3*step]=   s3 - 2*s2;
#else
    int i,j,n=7;
    for(i=0; i<7; i+=2){
        dst2[i*step/2]= 0;
        for(j=0; j<4; j++)
            dst2[i*step/2] += src[j*step] * cos(i*M_PI/n*(j+0.5)) * sqrt((i?2.0:1.0)/n);
        if(fabs(dst2[i*step/2] - dst[i*step/2]) > 20)
            printf("%d %d %d (%d %d %d %d) -> (%d %d %d %d)\n", i,dst2[i*step/2], dst[i*step/2],src[0*step], src[1*step], src[2*step], src[3*step], dst[0*step], dst[1*step],dst[2*step],dst[3*step]);
    }
#endif
}
#endif

static inline void dctA_c(int16_t *dst, uint8_t *src, int stride){
    int i;

    for(i=0; i<4; i++){
        int s0=  src[0*stride] + src[6*stride];
        int s1=  src[1*stride] + src[5*stride];
        int s2=  src[2*stride] + src[4*stride];
        int s3=  src[3*stride];
        int s= s3+s3;
        s3= s-s0;
        s0= s+s0;
        s = s2+s1;
        s2= s2-s1;
        dst[0]= s0 + s;
        dst[2]= s0 - s;
        dst[1]= 2*s3 +   s2;
        dst[3]=   s3 - 2*s2;
        src++;
        dst+=4;
    }
}

static void dctB_c(int16_t *dst, int16_t *src){
    int i;

    for(i=0; i<4; i++){
        int s0=  src[0*4] + src[6*4];
        int s1=  src[1*4] + src[5*4];
        int s2=  src[2*4] + src[4*4];
        int s3=  src[3*4];
        int s= s3+s3;
        s3= s-s0;
        s0= s+s0;
        s = s2+s1;
        s2= s2-s1;
        dst[0*4]= s0 + s;
        dst[2*4]= s0 - s;
        dst[1*4]= 2*s3 +   s2;
        dst[3*4]=   s3 - 2*s2;
        src++;
        dst++;
    }
}

#if HAVE_MMX_INLINE
static void dctB_mmx(int16_t *dst, int16_t *src){
    __asm__ volatile (
        "movq  (%0), %%mm0      \n\t"
        "movq  1*4*2(%0), %%mm1 \n\t"
        "paddw 6*4*2(%0), %%mm0 \n\t"
        "paddw 5*4*2(%0), %%mm1 \n\t"
        "movq  2*4*2(%0), %%mm2 \n\t"
        "movq  3*4*2(%0), %%mm3 \n\t"
        "paddw 4*4*2(%0), %%mm2 \n\t"
        "paddw %%mm3, %%mm3     \n\t" //s
        "movq %%mm3, %%mm4      \n\t" //s
        "psubw %%mm0, %%mm3     \n\t" //s-s0
        "paddw %%mm0, %%mm4     \n\t" //s+s0
        "movq %%mm2, %%mm0      \n\t" //s2
        "psubw %%mm1, %%mm2     \n\t" //s2-s1
        "paddw %%mm1, %%mm0     \n\t" //s2+s1
        "movq %%mm4, %%mm1      \n\t" //s0'
        "psubw %%mm0, %%mm4     \n\t" //s0'-s'
        "paddw %%mm0, %%mm1     \n\t" //s0'+s'
        "movq %%mm3, %%mm0      \n\t" //s3'
        "psubw %%mm2, %%mm3     \n\t"
        "psubw %%mm2, %%mm3     \n\t"
        "paddw %%mm0, %%mm2     \n\t"
        "paddw %%mm0, %%mm2     \n\t"
        "movq %%mm1, (%1)       \n\t"
        "movq %%mm4, 2*4*2(%1)  \n\t"
        "movq %%mm2, 1*4*2(%1)  \n\t"
        "movq %%mm3, 3*4*2(%1)  \n\t"
        :: "r" (src), "r"(dst)
    );
}
#endif

static void (*dctB)(int16_t *dst, int16_t *src)= dctB_c;

#define N0 4
#define N1 5
#define N2 10
#define SN0 2
#define SN1 2.2360679775
#define SN2 3.16227766017
#define N (1<<16)

static const int factor[16]={
    N/(N0*N0), N/(N0*N1), N/(N0*N0),N/(N0*N2),
    N/(N1*N0), N/(N1*N1), N/(N1*N0),N/(N1*N2),
    N/(N0*N0), N/(N0*N1), N/(N0*N0),N/(N0*N2),
    N/(N2*N0), N/(N2*N1), N/(N2*N0),N/(N2*N2),
};

static const int thres[16]={
    N/(SN0*SN0), N/(SN0*SN2), N/(SN0*SN0),N/(SN0*SN2),
    N/(SN2*SN0), N/(SN2*SN2), N/(SN2*SN0),N/(SN2*SN2),
    N/(SN0*SN0), N/(SN0*SN2), N/(SN0*SN0),N/(SN0*SN2),
    N/(SN2*SN0), N/(SN2*SN2), N/(SN2*SN0),N/(SN2*SN2),
};

static int thres2[99][16];

static void init_thres2(void){
    int qp, i;
    int bias= 0; //FIXME

    for(qp=0; qp<99; qp++){
        for(i=0; i<16; i++){
            thres2[qp][i]= ((i&1)?SN2:SN0) * ((i&4)?SN2:SN0) * XMAX(1,qp) * (1<<2) - 1 - bias;
        }
    }
}

static int hardthresh_c(int16_t *src, int qp){
    int i;
    int a;

    a= src[0] * factor[0];
    for(i=1; i<16; i++){
        unsigned int threshold1= thres2[qp][i];
        unsigned int threshold2= (threshold1<<1);
        int level= src[i];
        if(((unsigned)(level+threshold1))>threshold2){
            a += level * factor[i];
        }
    }
    return (a + (1<<11))>>12;
}

static int mediumthresh_c(int16_t *src, int qp){
    int i;
    int a;

    a= src[0] * factor[0];
    for(i=1; i<16; i++){
        unsigned int threshold1= thres2[qp][i];
        unsigned int threshold2= (threshold1<<1);
        int level= src[i];
        if(((unsigned)(level+threshold1))>threshold2){
            if(((unsigned)(level+2*threshold1))>2*threshold2){
                a += level * factor[i];
            }else{
                if(level>0) a+= 2*(level - (int)threshold1)*factor[i];
                else        a+= 2*(level + (int)threshold1)*factor[i];
            }
        }
    }
    return (a + (1<<11))>>12;
}

static int softthresh_c(int16_t *src, int qp){
    int i;
    int a;

    a= src[0] * factor[0];
    for(i=1; i<16; i++){
        unsigned int threshold1= thres2[qp][i];
        unsigned int threshold2= (threshold1<<1);
        int level= src[i];
        if(((unsigned)(level+threshold1))>threshold2){
            if(level>0) a+= (level - (int)threshold1)*factor[i];
            else        a+= (level + (int)threshold1)*factor[i];
        }
    }
    return (a + (1<<11))>>12;
}

static int (*requantize)(int16_t *src, int qp)= hardthresh_c;

static void filter(struct vf_priv_s *p, uint8_t *dst, uint8_t *src, int dst_stride, int src_stride, int width, int height, uint8_t *qp_store, int qp_stride, int is_luma){
    int x, y;
    const int stride= is_luma ? p->temp_stride : ((width+16+15)&(~15));
    uint8_t  *p_src= p->src + 8*stride;
    int16_t *block= (int16_t *)p->src;
    int16_t *temp= (int16_t *)(p->src + 32);

    if (!src || !dst) return; // HACK avoid crash for Y8 colourspace
    for(y=0; y<height; y++){
        int index= 8 + 8*stride + y*stride;
        fast_memcpy(p_src + index, src + y*src_stride, width);
        for(x=0; x<8; x++){
            p_src[index         - x - 1]= p_src[index +         x    ];
            p_src[index + width + x    ]= p_src[index + width - x - 1];
        }
    }
    for(y=0; y<8; y++){
        fast_memcpy(p_src + (       7-y)*stride, p_src + (       y+8)*stride, stride);
        fast_memcpy(p_src + (height+8+y)*stride, p_src + (height-y+7)*stride, stride);
    }
    //FIXME (try edge emu)

    for(y=0; y<height; y++){
        for(x=-8; x<0; x+=4){
            const int index= x + y*stride + (8-3)*(1+stride) + 8; //FIXME silly offset
            uint8_t *src  = p_src + index;
            int16_t *tp= temp+4*x;

            dctA_c(tp+4*8, src, stride);
        }
        for(x=0; x<width; ){
            const int qps= 3 + is_luma;
            int qp;
            int end= XMIN(x+8, width);

            if(p->qp)
                qp= p->qp;
            else{
                qp= qp_store[ (XMIN(x, width-1)>>qps) + (XMIN(y, height-1)>>qps) * qp_stride];
                qp=norm_qscale(qp, p->mpeg2);
            }
            for(; x<end; x++){
                const int index= x + y*stride + (8-3)*(1+stride) + 8; //FIXME silly offset
                uint8_t *src  = p_src + index;
                int16_t *tp= temp+4*x;
                int v;

                if((x&3)==0)
                    dctA_c(tp+4*8, src, stride);

                dctB(block, tp);

                v= requantize(block, qp);
                v= (v + dither[y&7][x&7])>>6;
                if((unsigned)v > 255)
                    v= (-v)>>31;
                dst[x + y*dst_stride]= v;
            }
        }
    }
}

static int config(struct vf_instance *vf,
    int width, int height, int d_width, int d_height,
    unsigned int flags, unsigned int outfmt){
    int h= (height+16+15)&(~15);

    vf->priv->temp_stride= (width+16+15)&(~15);
    vf->priv->src = av_malloc(vf->priv->temp_stride*(h+8)*sizeof(uint8_t));

    return ff_vf_next_config(vf,width,height,d_width,d_height,flags,outfmt);
}

static void get_image(struct vf_instance *vf, mp_image_t *mpi){
    if(mpi->flags&MP_IMGFLAG_PRESERVE) return; // don't change
    // ok, we can do pp in-place (or pp disabled):
    vf->dmpi=ff_vf_get_image(vf->next,mpi->imgfmt,
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

    if(mpi->flags&MP_IMGFLAG_DIRECT){
        dmpi=vf->dmpi;
    }else{
        // no DR, so get a new image! hope we'll get DR buffer:
        dmpi=ff_vf_get_image(vf->next,mpi->imgfmt,
            MP_IMGTYPE_TEMP,
            MP_IMGFLAG_ACCEPT_STRIDE|MP_IMGFLAG_PREFER_ALIGNED_STRIDE,
            mpi->width,mpi->height);
        ff_vf_clone_mpi_attributes(dmpi, mpi);
    }

    vf->priv->mpeg2= mpi->qscale_type;
    if(mpi->qscale || vf->priv->qp){
        filter(vf->priv, dmpi->planes[0], mpi->planes[0], dmpi->stride[0], mpi->stride[0], mpi->w, mpi->h, mpi->qscale, mpi->qstride, 1);
        filter(vf->priv, dmpi->planes[1], mpi->planes[1], dmpi->stride[1], mpi->stride[1], mpi->w>>mpi->chroma_x_shift, mpi->h>>mpi->chroma_y_shift, mpi->qscale, mpi->qstride, 0);
        filter(vf->priv, dmpi->planes[2], mpi->planes[2], dmpi->stride[2], mpi->stride[2], mpi->w>>mpi->chroma_x_shift, mpi->h>>mpi->chroma_y_shift, mpi->qscale, mpi->qstride, 0);
    }else{
        memcpy_pic(dmpi->planes[0], mpi->planes[0], mpi->w, mpi->h, dmpi->stride[0], mpi->stride[0]);
        memcpy_pic(dmpi->planes[1], mpi->planes[1], mpi->w>>mpi->chroma_x_shift, mpi->h>>mpi->chroma_y_shift, dmpi->stride[1], mpi->stride[1]);
        memcpy_pic(dmpi->planes[2], mpi->planes[2], mpi->w>>mpi->chroma_x_shift, mpi->h>>mpi->chroma_y_shift, dmpi->stride[2], mpi->stride[2]);
    }

#if HAVE_MMX_INLINE
    if(ff_gCpuCaps.hasMMX) __asm__ volatile ("emms\n\t");
#endif
#if HAVE_MMXEXT_INLINE
    if(ff_gCpuCaps.hasMMX2) __asm__ volatile ("sfence\n\t");
#endif

    return ff_vf_next_put_image(vf,dmpi, pts);
}

static void uninit(struct vf_instance *vf){
    if(!vf->priv) return;

    av_free(vf->priv->src);
    vf->priv->src= NULL;

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
        return ff_vf_next_query_format(vf,fmt);
    }
    return 0;
}

static int control(struct vf_instance *vf, int request, void* data){
    return ff_vf_next_control(vf,request,data);
}

static int vf_open(vf_instance_t *vf, char *args){
    vf->config=config;
    vf->put_image=put_image;
    vf->get_image=get_image;
    vf->query_format=query_format;
    vf->uninit=uninit;
    vf->control= control;
    vf->priv=malloc(sizeof(struct vf_priv_s));
    memset(vf->priv, 0, sizeof(struct vf_priv_s));

    if (args) sscanf(args, "%d:%d", &vf->priv->qp, &vf->priv->mode);

    if(vf->priv->qp < 0)
        vf->priv->qp = 0;

    init_thres2();

    switch(vf->priv->mode){
        case 0: requantize= hardthresh_c; break;
        case 1: requantize= softthresh_c; break;
        default:
        case 2: requantize= mediumthresh_c; break;
    }

#if HAVE_MMX_INLINE
    if(ff_gCpuCaps.hasMMX){
        dctB= dctB_mmx;
    }
#endif
#if 0
    if(ff_gCpuCaps.hasMMX){
        switch(vf->priv->mode){
            case 0: requantize= hardthresh_mmx; break;
            case 1: requantize= softthresh_mmx; break;
        }
    }
#endif

    return 1;
}

const vf_info_t ff_vf_info_pp7 = {
    "postprocess 7",
    "pp7",
    "Michael Niedermayer",
    "",
    vf_open,
    NULL
};
