/*
 * Copyright (C) 2007 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @todo try to change to int
 * @todo try lifting based implementation
 * @todo optimize optimize optimize
 * @todo hard tresholding
 * @todo use QP to decide filter strength
 * @todo wavelet normalization / least squares optimal signal vs. noise thresholds
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

#include "mp_msg.h"
#include "img_format.h"
#include "mp_image.h"
#include "vf.h"

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
//FIXME the above is duplicated in many filters

struct vf_priv_s {
    float strength[2];
    float delta;
    int mode;
    int depth;
    float *plane[16][4];
    int stride;
};

#define S 1.41421356237 //sqrt(2)

static const double coeff[2][5]={
    {
         0.6029490182363579  *S,
         0.2668641184428723  *S,
        -0.07822326652898785 *S,
        -0.01686411844287495 *S,
         0.02674875741080976 *S
    },{
         1.115087052456994   /S,
        -0.5912717631142470  /S,
        -0.05754352622849957 /S,
         0.09127176311424948 /S
    }
};

static const double icoeff[2][5]={
    {
         1.115087052456994   /S,
         0.5912717631142470  /S,
        -0.05754352622849957 /S,
        -0.09127176311424948 /S
    },{
         0.6029490182363579  *S,
        -0.2668641184428723  *S,
        -0.07822326652898785 *S,
         0.01686411844287495 *S,
         0.02674875741080976 *S
    }
};
#undef S

static inline int mirror(int x, int w){
    while((unsigned)x > (unsigned)w){
        x=-x;
        if(x<0) x+= 2*w;
    }
    return x;
}

static inline void decompose(float *dstL, float *dstH, float *src, int stride, int w){
    int x, i;
    for(x=0; x<w; x++){
        double sumL= src[x*stride] * coeff[0][0];
        double sumH= src[x*stride] * coeff[1][0];
        for(i=1; i<=4; i++){
            double s= (src[mirror(x-i, w-1)*stride] + src[mirror(x+i, w-1)*stride]);

            sumL+= coeff[0][i]*s;
            sumH+= coeff[1][i]*s;
        }
        dstL[x*stride]= sumL;
        dstH[x*stride]= sumH;
    }
}

static inline void compose(float *dst, float *srcL, float *srcH, int stride, int w){
    int x, i;
    for(x=0; x<w; x++){
        double sumL= srcL[x*stride] * icoeff[0][0];
        double sumH= srcH[x*stride] * icoeff[1][0];
        for(i=1; i<=4; i++){
            int x0= mirror(x-i, w-1)*stride;
            int x1= mirror(x+i, w-1)*stride;

            sumL+= icoeff[0][i]*(srcL[x0] + srcL[x1]);
            sumH+= icoeff[1][i]*(srcH[x0] + srcH[x1]);
        }
        dst[x*stride]= (sumL + sumH)*0.5;
    }
}

static inline void decompose2D(float *dstL, float *dstH, float *src, int xstride, int ystride, int step, int w, int h){
    int y, x;
    for(y=0; y<h; y++)
        for(x=0; x<step; x++)
            decompose(dstL + ystride*y + xstride*x, dstH + ystride*y + xstride*x, src + ystride*y + xstride*x, step*xstride, (w-x+step-1)/step);
}

static inline void compose2D(float *dst, float *srcL, float *srcH, int xstride, int ystride, int step, int w, int h){
    int y, x;
    for(y=0; y<h; y++)
        for(x=0; x<step; x++)
            compose(dst + ystride*y + xstride*x, srcL + ystride*y + xstride*x, srcH + ystride*y + xstride*x, step*xstride, (w-x+step-1)/step);
}

static void decompose2D2(float *dst[4], float *src, float *temp[2], int stride, int step, int w, int h){
    decompose2D(temp[0], temp[1], src    , 1, stride, step  , w, h);
    decompose2D( dst[0],  dst[1], temp[0], stride, 1, step  , h, w);
    decompose2D( dst[2],  dst[3], temp[1], stride, 1, step  , h, w);
}

static void compose2D2(float *dst, float *src[4], float *temp[2], int stride, int step, int w, int h){
    compose2D(temp[0],  src[0],  src[1], stride, 1, step  , h, w);
    compose2D(temp[1],  src[2],  src[3], stride, 1, step  , h, w);
    compose2D(dst    , temp[0], temp[1], 1, stride, step  , w, h);
}

static void filter(struct vf_priv_s *p, uint8_t *dst, uint8_t *src, int dst_stride, int src_stride, int width, int height, int is_luma){
    int x,y, i, j;
//    double sum=0;
    double s= p->strength[!is_luma];
    int depth= p->depth;

    while(1<<depth > width || 1<<depth > height)
        depth--;

    for(y=0; y<height; y++)
        for(x=0; x<width; x++)
            p->plane[0][0][x + y*p->stride]= src[x + y*src_stride];

    for(i=0; i<depth; i++){
        decompose2D2(p->plane[i+1], p->plane[i][0], p->plane[0]+1,p->stride, 1<<i, width, height);
    }
    for(i=0; i<depth; i++){
        for(j=1; j<4; j++){
            for(y=0; y<height; y++){
                for(x=0; x<width; x++){
                    double v= p->plane[i+1][j][x + y*p->stride];
                    if     (v> s) v-=s;
                    else if(v<-s) v+=s;
                    else          v =0;
                    p->plane[i+1][j][x + y*p->stride]= v;
                }
            }
        }
    }
    for(i=depth-1; i>=0; i--){
        compose2D2(p->plane[i][0], p->plane[i+1], p->plane[0]+1, p->stride, 1<<i, width, height);
    }

    for(y=0; y<height; y++)
        for(x=0; x<width; x++){
            i= p->plane[0][0][x + y*p->stride] + dither[x&7][y&7]*(1.0/64) + 1.0/128; //yes the rounding is insane but optimal :)
//            double e= i - src[x + y*src_stride];
//            sum += e*e;
            if((unsigned)i > 255U) i= ~(i>>31);
            dst[x + y*dst_stride]= i;
        }

//    printf("%f\n", sum/height/width);
}

static int config(struct vf_instance *vf, int width, int height, int d_width, int d_height, unsigned int flags, unsigned int outfmt){
    int h= (height+15)&(~15);
    int i,j;

    vf->priv->stride= (width+15)&(~15);
    for(j=0; j<4; j++){
        for(i=0; i<=vf->priv->depth; i++)
            vf->priv->plane[i][j]= malloc(vf->priv->stride*h*sizeof(vf->priv->plane[0][0][0]));
    }

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

    filter(vf->priv, dmpi->planes[0], mpi->planes[0], dmpi->stride[0], mpi->stride[0], mpi->w, mpi->h, 1);
    filter(vf->priv, dmpi->planes[1], mpi->planes[1], dmpi->stride[1], mpi->stride[1], mpi->w>>mpi->chroma_x_shift, mpi->h>>mpi->chroma_y_shift, 0);
    filter(vf->priv, dmpi->planes[2], mpi->planes[2], dmpi->stride[2], mpi->stride[2], mpi->w>>mpi->chroma_x_shift, mpi->h>>mpi->chroma_y_shift, 0);

    return vf_next_put_image(vf,dmpi, pts);
}

static void uninit(struct vf_instance *vf){
    int i,j;
    if(!vf->priv) return;

    for(j=0; j<4; j++){
        for(i=0; i<16; i++){
            free(vf->priv->plane[i][j]);
            vf->priv->plane[i][j]= NULL;
        }
    }

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


static int vf_open(vf_instance_t *vf, char *args){
    vf->config=config;
    vf->put_image=put_image;
    vf->get_image=get_image;
    vf->query_format=query_format;
    vf->uninit=uninit;
    vf->priv=malloc(sizeof(struct vf_priv_s));
    memset(vf->priv, 0, sizeof(struct vf_priv_s));

    vf->priv->depth= 8;
    vf->priv->strength[0]= 1.0;
    vf->priv->strength[1]= 1.0;
    vf->priv->delta= 1.0;

    if (args) sscanf(args, "%d:%f:%f:%d:%f", &vf->priv->depth,
                     &vf->priv->strength[0],
                     &vf->priv->strength[1],
                     &vf->priv->mode,
                     &vf->priv->delta);

    return 1;
}

const vf_info_t vf_info_ow = {
    "overcomplete wavelet denoiser",
    "ow",
    "Michael Niedermayer",
    "",
    vf_open,
    NULL
};
