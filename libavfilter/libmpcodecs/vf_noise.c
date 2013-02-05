/*
 * Copyright (C) 2002 Michael Niedermayer <michaelni@gmx.at>
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

#include "img_format.h"
#include "mp_image.h"
#include "vf.h"
#include "libvo/fastmemcpy.h"
#include "libavutil/mem.h"
#include "libavutil/x86/asm.h"

#define MAX_NOISE 4096
#define MAX_SHIFT 1024
#define MAX_RES (MAX_NOISE-MAX_SHIFT)

//===========================================================================//

static inline void lineNoise_C(uint8_t *dst, uint8_t *src, int8_t *noise, int len, int shift);
static inline void lineNoiseAvg_C(uint8_t *dst, uint8_t *src, int len, int8_t **shift);

static void (*lineNoise)(uint8_t *dst, uint8_t *src, int8_t *noise, int len, int shift)= lineNoise_C;
static void (*lineNoiseAvg)(uint8_t *dst, uint8_t *src, int len, int8_t **shift)= lineNoiseAvg_C;

typedef struct FilterParam{
        int strength;
        int uniform;
        int temporal;
        int quality;
        int averaged;
        int pattern;
        int shiftptr;
        int8_t *noise;
        int8_t *prev_shift[MAX_RES][3];
}FilterParam;

struct vf_priv_s {
        FilterParam lumaParam;
        FilterParam chromaParam;
        unsigned int outfmt;
};

static int nonTempRandShift_init;
static int nonTempRandShift[MAX_RES];

static int patt[4] = {
    -1,0,1,0
};

#define RAND_N(range) ((int) ((double)range*rand()/(RAND_MAX+1.0)))
static int8_t *initNoise(FilterParam *fp){
        int strength= fp->strength;
        int uniform= fp->uniform;
        int averaged= fp->averaged;
        int pattern= fp->pattern;
        int8_t *noise= av_malloc(MAX_NOISE*sizeof(int8_t));
        int i, j;

        srand(123457);

        for(i=0,j=0; i<MAX_NOISE; i++,j++)
        {
                if(uniform) {
                        if (averaged) {
                                    if (pattern) {
                                        noise[i]= (RAND_N(strength) - strength/2)/6
                                                +patt[j%4]*strength*0.25/3;
                                } else {
                                        noise[i]= (RAND_N(strength) - strength/2)/3;
                                    }
                        } else {
                                    if (pattern) {
                                    noise[i]= (RAND_N(strength) - strength/2)/2
                                            + patt[j%4]*strength*0.25;
                                } else {
                                        noise[i]= RAND_N(strength) - strength/2;
                                    }
                        }
                    } else {
                        double x1, x2, w, y1;
                        do {
                                x1 = 2.0 * rand()/(float)RAND_MAX - 1.0;
                                x2 = 2.0 * rand()/(float)RAND_MAX - 1.0;
                                w = x1 * x1 + x2 * x2;
                        } while ( w >= 1.0 );

                        w = sqrt( (-2.0 * log( w ) ) / w );
                        y1= x1 * w;
                        y1*= strength / sqrt(3.0);
                        if (pattern) {
                            y1 /= 2;
                            y1 += patt[j%4]*strength*0.35;
                        }
                        if     (y1<-128) y1=-128;
                        else if(y1> 127) y1= 127;
                        if (averaged) y1 /= 3.0;
                        noise[i]= (int)y1;
                }
                if (RAND_N(6) == 0) j--;
        }


        for (i = 0; i < MAX_RES; i++)
            for (j = 0; j < 3; j++)
                fp->prev_shift[i][j] = noise + (rand()&(MAX_SHIFT-1));

        if(!nonTempRandShift_init){
                for(i=0; i<MAX_RES; i++){
                        nonTempRandShift[i]= rand()&(MAX_SHIFT-1);
                }
                nonTempRandShift_init = 1;
        }

        fp->noise= noise;
        fp->shiftptr= 0;
        return noise;
}

/***************************************************************************/

#if HAVE_MMX
static inline void lineNoise_MMX(uint8_t *dst, uint8_t *src, int8_t *noise, int len, int shift){
        x86_reg mmx_len= len&(~7);
        noise+=shift;

        __asm__ volatile(
                "mov %3, %%"REG_a"                \n\t"
                "pcmpeqb %%mm7, %%mm7                \n\t"
                "psllw $15, %%mm7                \n\t"
                "packsswb %%mm7, %%mm7                \n\t"
                ASMALIGN(4)
                "1:                                \n\t"
                "movq (%0, %%"REG_a"), %%mm0        \n\t"
                "movq (%1, %%"REG_a"), %%mm1        \n\t"
                "pxor %%mm7, %%mm0                \n\t"
                "paddsb %%mm1, %%mm0                \n\t"
                "pxor %%mm7, %%mm0                \n\t"
                "movq %%mm0, (%2, %%"REG_a")        \n\t"
                "add $8, %%"REG_a"                \n\t"
                " js 1b                                \n\t"
                :: "r" (src+mmx_len), "r" (noise+mmx_len), "r" (dst+mmx_len), "g" (-mmx_len)
                : "%"REG_a
        );
        if(mmx_len!=len)
                lineNoise_C(dst+mmx_len, src+mmx_len, noise+mmx_len, len-mmx_len, 0);
}
#endif

//duplicate of previous except movntq
#if HAVE_MMX2
static inline void lineNoise_MMX2(uint8_t *dst, uint8_t *src, int8_t *noise, int len, int shift){
        x86_reg mmx_len= len&(~7);
        noise+=shift;

        __asm__ volatile(
                "mov %3, %%"REG_a"                \n\t"
                "pcmpeqb %%mm7, %%mm7                \n\t"
                "psllw $15, %%mm7                \n\t"
                "packsswb %%mm7, %%mm7                \n\t"
                ASMALIGN(4)
                "1:                                \n\t"
                "movq (%0, %%"REG_a"), %%mm0        \n\t"
                "movq (%1, %%"REG_a"), %%mm1        \n\t"
                "pxor %%mm7, %%mm0                \n\t"
                "paddsb %%mm1, %%mm0                \n\t"
                "pxor %%mm7, %%mm0                \n\t"
                "movntq %%mm0, (%2, %%"REG_a")        \n\t"
                "add $8, %%"REG_a"                \n\t"
                " js 1b                                \n\t"
                :: "r" (src+mmx_len), "r" (noise+mmx_len), "r" (dst+mmx_len), "g" (-mmx_len)
                : "%"REG_a
        );
        if(mmx_len!=len)
                lineNoise_C(dst+mmx_len, src+mmx_len, noise+mmx_len, len-mmx_len, 0);
}
#endif

static inline void lineNoise_C(uint8_t *dst, uint8_t *src, int8_t *noise, int len, int shift){
        int i;
        noise+= shift;
        for(i=0; i<len; i++)
        {
                int v= src[i]+ noise[i];
                if(v>255)         dst[i]=255; //FIXME optimize
                else if(v<0)         dst[i]=0;
                else                dst[i]=v;
        }
}

/***************************************************************************/

#if HAVE_MMX
static inline void lineNoiseAvg_MMX(uint8_t *dst, uint8_t *src, int len, int8_t **shift){
        x86_reg mmx_len= len&(~7);

        __asm__ volatile(
                "mov %5, %%"REG_a"                \n\t"
                ASMALIGN(4)
                "1:                                \n\t"
                "movq (%1, %%"REG_a"), %%mm1        \n\t"
                "movq (%0, %%"REG_a"), %%mm0        \n\t"
                "paddb (%2, %%"REG_a"), %%mm1        \n\t"
                "paddb (%3, %%"REG_a"), %%mm1        \n\t"
                "movq %%mm0, %%mm2                \n\t"
                "movq %%mm1, %%mm3                \n\t"
                "punpcklbw %%mm0, %%mm0                \n\t"
                "punpckhbw %%mm2, %%mm2                \n\t"
                "punpcklbw %%mm1, %%mm1                \n\t"
                "punpckhbw %%mm3, %%mm3                \n\t"
                "pmulhw %%mm0, %%mm1                \n\t"
                "pmulhw %%mm2, %%mm3                \n\t"
                "paddw %%mm1, %%mm1                \n\t"
                "paddw %%mm3, %%mm3                \n\t"
                "paddw %%mm0, %%mm1                \n\t"
                "paddw %%mm2, %%mm3                \n\t"
                "psrlw $8, %%mm1                \n\t"
                "psrlw $8, %%mm3                \n\t"
                "packuswb %%mm3, %%mm1                \n\t"
                "movq %%mm1, (%4, %%"REG_a")        \n\t"
                "add $8, %%"REG_a"                \n\t"
                " js 1b                                \n\t"
                :: "r" (src+mmx_len), "r" (shift[0]+mmx_len), "r" (shift[1]+mmx_len), "r" (shift[2]+mmx_len),
                   "r" (dst+mmx_len), "g" (-mmx_len)
                : "%"REG_a
        );

        if(mmx_len!=len){
                int8_t *shift2[3]={shift[0]+mmx_len, shift[1]+mmx_len, shift[2]+mmx_len};
                lineNoiseAvg_C(dst+mmx_len, src+mmx_len, len-mmx_len, shift2);
        }
}
#endif

static inline void lineNoiseAvg_C(uint8_t *dst, uint8_t *src, int len, int8_t **shift){
        int i;
        int8_t *src2= (int8_t*)src;

        for(i=0; i<len; i++)
        {
            const int n= shift[0][i] + shift[1][i] + shift[2][i];
            dst[i]= src2[i]+((n*src2[i])>>7);
        }
}

/***************************************************************************/

static void noise(uint8_t *dst, uint8_t *src, int dstStride, int srcStride, int width, int height, FilterParam *fp){
        int8_t *noise= fp->noise;
        int y;
        int shift=0;

        if(!noise)
        {
                if(src==dst) return;

                if(dstStride==srcStride) fast_memcpy(dst, src, srcStride*height);
                else
                {
                        for(y=0; y<height; y++)
                        {
                                fast_memcpy(dst, src, width);
                                dst+= dstStride;
                                src+= srcStride;
                        }
                }
                return;
        }

        for(y=0; y<height; y++)
        {
                if(fp->temporal)        shift=  rand()&(MAX_SHIFT  -1);
                else                        shift= nonTempRandShift[y];

                if(fp->quality==0) shift&= ~7;
                if (fp->averaged) {
                    lineNoiseAvg(dst, src, width, fp->prev_shift[y]);
                    fp->prev_shift[y][fp->shiftptr] = noise + shift;
                } else {
                    lineNoise(dst, src, noise, width, shift);
                }
                dst+= dstStride;
                src+= srcStride;
        }
        fp->shiftptr++;
        if (fp->shiftptr == 3) fp->shiftptr = 0;
}

static int config(struct vf_instance *vf,
        int width, int height, int d_width, int d_height,
        unsigned int flags, unsigned int outfmt){

        return ff_vf_next_config(vf,width,height,d_width,d_height,flags,outfmt);
}

static void get_image(struct vf_instance *vf, mp_image_t *mpi){
    if(mpi->flags&MP_IMGFLAG_PRESERVE) return; // don't change
    if(mpi->imgfmt!=vf->priv->outfmt) return; // colorspace differ
    // ok, we can do pp in-place (or pp disabled):
    vf->dmpi=ff_vf_get_image(vf->next,mpi->imgfmt,
        mpi->type, mpi->flags, mpi->w, mpi->h);
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
                vf->dmpi=ff_vf_get_image(vf->next,vf->priv->outfmt,
                MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE,
                mpi->w,mpi->h);
//printf("nodr\n");
        }
//else printf("dr\n");
        dmpi= vf->dmpi;

        noise(dmpi->planes[0], mpi->planes[0], dmpi->stride[0], mpi->stride[0], mpi->w, mpi->h, &vf->priv->lumaParam);
        noise(dmpi->planes[1], mpi->planes[1], dmpi->stride[1], mpi->stride[1], mpi->w/2, mpi->h/2, &vf->priv->chromaParam);
        noise(dmpi->planes[2], mpi->planes[2], dmpi->stride[2], mpi->stride[2], mpi->w/2, mpi->h/2, &vf->priv->chromaParam);

        ff_vf_clone_mpi_attributes(dmpi, mpi);

#if HAVE_MMX
        if(ff_gCpuCaps.hasMMX) __asm__ volatile ("emms\n\t");
#endif
#if HAVE_MMX2
        if(ff_gCpuCaps.hasMMX2) __asm__ volatile ("sfence\n\t");
#endif

        return ff_vf_next_put_image(vf,dmpi, pts);
}

static void uninit(struct vf_instance *vf){
        if(!vf->priv) return;

        av_free(vf->priv->chromaParam.noise);
        vf->priv->chromaParam.noise= NULL;

        av_free(vf->priv->lumaParam.noise);
        vf->priv->lumaParam.noise= NULL;

        free(vf->priv);
        vf->priv=NULL;
}

//===========================================================================//

static int query_format(struct vf_instance *vf, unsigned int fmt){
        switch(fmt)
        {
        case IMGFMT_YV12:
        case IMGFMT_I420:
        case IMGFMT_IYUV:
                return ff_vf_next_query_format(vf,vf->priv->outfmt);
        }
        return 0;
}

static void parse(FilterParam *fp, char* args){
        char *pos;
        char *max= strchr(args, ':');

        if(!max) max= args + strlen(args);

        fp->strength= atoi(args);
        pos= strchr(args, 'u');
        if(pos && pos<max) fp->uniform=1;
        pos= strchr(args, 't');
        if(pos && pos<max) fp->temporal=1;
        pos= strchr(args, 'h');
        if(pos && pos<max) fp->quality=1;
        pos= strchr(args, 'p');
        if(pos && pos<max) fp->pattern=1;
        pos= strchr(args, 'a');
        if(pos && pos<max) {
            fp->temporal=1;
            fp->averaged=1;
        }

        if(fp->strength) initNoise(fp);
}

static const unsigned int fmt_list[]={
    IMGFMT_YV12,
    IMGFMT_I420,
    IMGFMT_IYUV,
    0
};

static int vf_open(vf_instance_t *vf, char *args){
    vf->config=config;
    vf->put_image=put_image;
    vf->get_image=get_image;
    vf->query_format=query_format;
    vf->uninit=uninit;
    vf->priv=malloc(sizeof(struct vf_priv_s));
    memset(vf->priv, 0, sizeof(struct vf_priv_s));
    if(args)
    {
        char *arg2= strchr(args,':');
        if(arg2) parse(&vf->priv->chromaParam, arg2+1);
        parse(&vf->priv->lumaParam, args);
    }

    // check csp:
    vf->priv->outfmt=ff_vf_match_csp(&vf->next,fmt_list,IMGFMT_YV12);
    if(!vf->priv->outfmt)
    {
        uninit(vf);
        return 0; // no csp match :(
    }


#if HAVE_MMX
    if(ff_gCpuCaps.hasMMX){
        lineNoise= lineNoise_MMX;
        lineNoiseAvg= lineNoiseAvg_MMX;
    }
#endif
#if HAVE_MMX2
    if(ff_gCpuCaps.hasMMX2) lineNoise= lineNoise_MMX2;
//    if(ff_gCpuCaps.hasMMX) lineNoiseAvg= lineNoiseAvg_MMX2;
#endif

    return 1;
}

const vf_info_t ff_vf_info_noise = {
    "noise generator",
    "noise",
    "Michael Niedermayer",
    "",
    vf_open,
    NULL
};

//===========================================================================//
