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
#include <assert.h>

#include "config.h"
#include "mp_msg.h"

#if HAVE_MALLOC_H
#include <malloc.h>
#endif

#include "libavutil/avutil.h"
#include "img_format.h"
#include "mp_image.h"
#include "vf.h"
#include "libswscale/swscale.h"
#include "vf_scale.h"


//===========================================================================//

typedef struct FilterParam{
    float radius;
    float preFilterRadius;
    float strength;
    float quality;
    struct SwsContext *preFilterContext;
    uint8_t *preFilterBuf;
    int preFilterStride;
    int distWidth;
    int distStride;
    int *distCoeff;
    int colorDiffCoeff[512];
}FilterParam;

struct vf_priv_s {
    FilterParam luma;
    FilterParam chroma;
};


/***************************************************************************/

//FIXME stupid code duplication
static void getSubSampleFactors(int *h, int *v, int format){
    switch(format){
    default:
        assert(0);
    case IMGFMT_YV12:
    case IMGFMT_I420:
        *h=1;
        *v=1;
        break;
    case IMGFMT_YVU9:
        *h=2;
        *v=2;
        break;
    case IMGFMT_444P:
        *h=0;
        *v=0;
        break;
    case IMGFMT_422P:
        *h=1;
        *v=0;
        break;
    case IMGFMT_411P:
        *h=2;
        *v=0;
        break;
    }
}

static int allocStuff(FilterParam *f, int width, int height){
    int stride= (width+7)&~7;
    SwsVector *vec;
    SwsFilter swsF;
    int i,x,y;
    f->preFilterBuf= av_malloc(stride*height);
    f->preFilterStride= stride;

    vec = sws_getGaussianVec(f->preFilterRadius, f->quality);
    swsF.lumH= swsF.lumV= vec;
    swsF.chrH= swsF.chrV= NULL;
    f->preFilterContext= sws_getContext(
        width, height, PIX_FMT_GRAY8, width, height, PIX_FMT_GRAY8, SWS_POINT, &swsF, NULL, NULL);

    sws_freeVec(vec);
    vec = sws_getGaussianVec(f->strength, 5.0);
    for(i=0; i<512; i++){
        double d;
        int index= i-256 + vec->length/2;

        if(index<0 || index>=vec->length)     d= 0.0;
        else                    d= vec->coeff[index];

        f->colorDiffCoeff[i]= (int)(d/vec->coeff[vec->length/2]*(1<<12) + 0.5);
    }
    sws_freeVec(vec);
    vec = sws_getGaussianVec(f->radius, f->quality);
    f->distWidth= vec->length;
    f->distStride= (vec->length+7)&~7;
    f->distCoeff= av_malloc(f->distWidth*f->distStride*sizeof(int32_t));

    for(y=0; y<vec->length; y++){
        for(x=0; x<vec->length; x++){
            double d= vec->coeff[x] * vec->coeff[y];

            f->distCoeff[x + y*f->distStride]= (int)(d*(1<<10) + 0.5);
//            if(y==vec->length/2)
//                printf("%6d ", f->distCoeff[x + y*f->distStride]);
        }
    }
    sws_freeVec(vec);

    return 0;
}

static int config(struct vf_instance *vf,
    int width, int height, int d_width, int d_height,
    unsigned int flags, unsigned int outfmt){

    int sw, sh;
//__asm__ volatile("emms\n\t");
    allocStuff(&vf->priv->luma, width, height);

    getSubSampleFactors(&sw, &sh, outfmt);
    allocStuff(&vf->priv->chroma, width>>sw, height>>sh);

    return vf_next_config(vf,width,height,d_width,d_height,flags,outfmt);
}

static void freeBuffers(FilterParam *f){
    if(f->preFilterContext) sws_freeContext(f->preFilterContext);
    f->preFilterContext=NULL;

    av_free(f->preFilterBuf);
    f->preFilterBuf=NULL;

    av_free(f->distCoeff);
    f->distCoeff=NULL;
}

static void uninit(struct vf_instance *vf){
    if(!vf->priv) return;

    freeBuffers(&vf->priv->luma);
    freeBuffers(&vf->priv->chroma);

    free(vf->priv);
    vf->priv=NULL;
}

static inline void blur(uint8_t *dst, uint8_t *src, int w, int h, int dstStride, int srcStride, FilterParam *fp){
    int x, y;
    FilterParam f= *fp;
    const int radius= f.distWidth/2;
    const uint8_t* const srcArray[MP_MAX_PLANES] = {src};
    uint8_t *dstArray[MP_MAX_PLANES]= {f.preFilterBuf};
    int srcStrideArray[MP_MAX_PLANES]= {srcStride};
    int dstStrideArray[MP_MAX_PLANES]= {f.preFilterStride};

//    f.preFilterContext->swScale(f.preFilterContext, srcArray, srcStrideArray, 0, h, dstArray, dstStrideArray);
    sws_scale(f.preFilterContext, srcArray, srcStrideArray, 0, h, dstArray, dstStrideArray);

    for(y=0; y<h; y++){
        for(x=0; x<w; x++){
            int sum=0;
            int div=0;
            int dy;
            const int preVal= f.preFilterBuf[x + y*f.preFilterStride];
#if 0
            const int srcVal= src[x + y*srcStride];
if((x/32)&1){
    dst[x + y*dstStride]= srcVal;
    if(y%32==0) dst[x + y*dstStride]= 0;
    continue;
}
#endif
            if(x >= radius && x < w - radius){
                for(dy=0; dy<radius*2+1; dy++){
                    int dx;
                    int iy= y+dy - radius;
                    if     (iy<0)  iy=  -iy;
                    else if(iy>=h) iy= h+h-iy-1;

                    for(dx=0; dx<radius*2+1; dx++){
                        const int ix= x+dx - radius;
                        int factor;

                        factor= f.colorDiffCoeff[256+preVal - f.preFilterBuf[ix + iy*f.preFilterStride] ]
                            *f.distCoeff[dx + dy*f.distStride];
                        sum+= src[ix + iy*srcStride] *factor;
                        div+= factor;
                    }
                }
            }else{
                for(dy=0; dy<radius*2+1; dy++){
                    int dx;
                    int iy= y+dy - radius;
                    if     (iy<0)  iy=  -iy;
                    else if(iy>=h) iy= h+h-iy-1;

                    for(dx=0; dx<radius*2+1; dx++){
                        int ix= x+dx - radius;
                        int factor;
                        if     (ix<0)  ix=  -ix;
                        else if(ix>=w) ix= w+w-ix-1;

                        factor= f.colorDiffCoeff[256+preVal - f.preFilterBuf[ix + iy*f.preFilterStride] ]
                            *f.distCoeff[dx + dy*f.distStride];
                        sum+= src[ix + iy*srcStride] *factor;
                        div+= factor;
                    }
                }
            }
            dst[x + y*dstStride]= (sum + div/2)/div;
        }
    }
}

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts){
    int cw= mpi->w >> mpi->chroma_x_shift;
    int ch= mpi->h >> mpi->chroma_y_shift;

    mp_image_t *dmpi=vf_get_image(vf->next,mpi->imgfmt,
        MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE,
        mpi->w,mpi->h);

    assert(mpi->flags&MP_IMGFLAG_PLANAR);

    blur(dmpi->planes[0], mpi->planes[0], mpi->w,mpi->h, dmpi->stride[0], mpi->stride[0], &vf->priv->luma);
    blur(dmpi->planes[1], mpi->planes[1], cw    , ch   , dmpi->stride[1], mpi->stride[1], &vf->priv->chroma);
    blur(dmpi->planes[2], mpi->planes[2], cw    , ch   , dmpi->stride[2], mpi->stride[2], &vf->priv->chroma);

    return vf_next_put_image(vf,dmpi, pts);
}

//===========================================================================//

static int query_format(struct vf_instance *vf, unsigned int fmt){
    switch(fmt)
    {
    case IMGFMT_YV12:
    case IMGFMT_I420:
    case IMGFMT_IYUV:
    case IMGFMT_YVU9:
    case IMGFMT_444P:
    case IMGFMT_422P:
    case IMGFMT_411P:
        return vf_next_query_format(vf, fmt);
    }
    return 0;
}

static int vf_open(vf_instance_t *vf, char *args){
    int e;

    vf->config=config;
    vf->put_image=put_image;
//    vf->get_image=get_image;
    vf->query_format=query_format;
    vf->uninit=uninit;
    vf->priv=malloc(sizeof(struct vf_priv_s));
    memset(vf->priv, 0, sizeof(struct vf_priv_s));

    if(args==NULL) return 0;

    e=sscanf(args, "%f:%f:%f:%f:%f:%f",
        &vf->priv->luma.radius,
        &vf->priv->luma.preFilterRadius,
        &vf->priv->luma.strength,
        &vf->priv->chroma.radius,
        &vf->priv->chroma.preFilterRadius,
        &vf->priv->chroma.strength
        );

    vf->priv->luma.quality = vf->priv->chroma.quality= 3.0;

    if(e==3){
        vf->priv->chroma.radius= vf->priv->luma.radius;
        vf->priv->chroma.preFilterRadius = vf->priv->luma.preFilterRadius;
        vf->priv->chroma.strength= vf->priv->luma.strength;
    }else if(e!=6)
        return 0;

//    if(vf->priv->luma.radius < 0) return 0;
//    if(vf->priv->chroma.radius < 0) return 0;

    return 1;
}

const vf_info_t vf_info_sab = {
    "shape adaptive blur",
    "sab",
    "Michael Niedermayer",
    "",
    vf_open,
    NULL
};

//===========================================================================//
