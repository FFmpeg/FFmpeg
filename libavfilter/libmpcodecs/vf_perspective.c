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
#include <math.h>

#include "config.h"
#include "mp_msg.h"

#if HAVE_MALLOC_H
#include <malloc.h>
#endif

#include "libavutil/mem.h"

#include "img_format.h"
#include "mp_image.h"
#include "vf.h"

#define SUB_PIXEL_BITS 8
#define SUB_PIXELS (1<<SUB_PIXEL_BITS)
#define COEFF_BITS 11

//===========================================================================//

struct vf_priv_s {
    double ref[4][2];
    int32_t coeff[1<<SUB_PIXEL_BITS][4];
    int32_t (*pv)[2];
    int pvStride;
    int cubic;
};


/***************************************************************************/

static void initPv(struct vf_priv_s *priv, int W, int H){
    double a,b,c,d,e,f,g,h,D;
    double (*ref)[2]= priv->ref;
    int x,y;

    g= (  (ref[0][0] - ref[1][0] - ref[2][0] + ref[3][0])*(ref[2][1] - ref[3][1])
        - (ref[0][1] - ref[1][1] - ref[2][1] + ref[3][1])*(ref[2][0] - ref[3][0]))*H;
    h= (  (ref[0][1] - ref[1][1] - ref[2][1] + ref[3][1])*(ref[1][0] - ref[3][0])
        - (ref[0][0] - ref[1][0] - ref[2][0] + ref[3][0])*(ref[1][1] - ref[3][1]))*W;
    D=   (ref[1][0] - ref[3][0])*(ref[2][1] - ref[3][1])
       - (ref[2][0] - ref[3][0])*(ref[1][1] - ref[3][1]);

    a= D*(ref[1][0] - ref[0][0])*H + g*ref[1][0];
    b= D*(ref[2][0] - ref[0][0])*W + h*ref[2][0];
    c= D*ref[0][0]*W*H;
    d= D*(ref[1][1] - ref[0][1])*H + g*ref[1][1];
    e= D*(ref[2][1] - ref[0][1])*W + h*ref[2][1];
    f= D*ref[0][1]*W*H;

    for(y=0; y<H; y++){
        for(x=0; x<W; x++){
            int u, v;

            u= (int)floor( SUB_PIXELS*(a*x + b*y + c)/(g*x + h*y + D*W*H) + 0.5);
            v= (int)floor( SUB_PIXELS*(d*x + e*y + f)/(g*x + h*y + D*W*H) + 0.5);

            priv->pv[x + y*W][0]= u;
            priv->pv[x + y*W][1]= v;
        }
    }
}

static double getCoeff(double d){
    double A= -0.60;
    double coeff;

    d= fabs(d);

    // Equation is from VirtualDub
    if(d<1.0)
        coeff = (1.0 - (A+3.0)*d*d + (A+2.0)*d*d*d);
    else if(d<2.0)
        coeff = (-4.0*A + 8.0*A*d - 5.0*A*d*d + A*d*d*d);
    else
        coeff=0.0;

    return coeff;
}

static int config(struct vf_instance *vf,
    int width, int height, int d_width, int d_height,
    unsigned int flags, unsigned int outfmt){
    int i, j;

    vf->priv->pvStride= width;
    vf->priv->pv= av_malloc(width*height*2*sizeof(int32_t));
    initPv(vf->priv, width, height);

    for(i=0; i<SUB_PIXELS; i++){
        double d= i/(double)SUB_PIXELS;
        double temp[4];
        double sum=0;

        for(j=0; j<4; j++)
            temp[j]= getCoeff(j - d - 1);

        for(j=0; j<4; j++)
            sum+= temp[j];

        for(j=0; j<4; j++)
            vf->priv->coeff[i][j]= (int)floor((1<<COEFF_BITS)*temp[j]/sum + 0.5);
    }

    return vf_next_config(vf,width,height,d_width,d_height,flags,outfmt);
}

static void uninit(struct vf_instance *vf){
    if(!vf->priv) return;

    av_free(vf->priv->pv);
    vf->priv->pv= NULL;

    free(vf->priv);
    vf->priv=NULL;
}

static inline void resampleCubic(uint8_t *dst, uint8_t *src, int w, int h, int dstStride, int srcStride, struct vf_priv_s *privParam, int xShift, int yShift){
    int x, y;
    struct vf_priv_s priv= *privParam;

    for(y=0; y<h; y++){
        for(x=0; x<w; x++){
            int u, v, subU, subV, sum, sx, sy;

            sx= x << xShift;
            sy= y << yShift;
            u= priv.pv[sx + sy*priv.pvStride][0]>>xShift;
            v= priv.pv[sx + sy*priv.pvStride][1]>>yShift;
            subU= u & (SUB_PIXELS-1);
            subV= v & (SUB_PIXELS-1);
            u >>= SUB_PIXEL_BITS;
            v >>= SUB_PIXEL_BITS;

            if(u>0 && v>0 && u<w-2 && v<h-2){
                const int index= u + v*srcStride;
                const int a= priv.coeff[subU][0];
                const int b= priv.coeff[subU][1];
                const int c= priv.coeff[subU][2];
                const int d= priv.coeff[subU][3];

                sum=
                 priv.coeff[subV][0]*(  a*src[index - 1 - srcStride] + b*src[index - 0 - srcStride]
                                      + c*src[index + 1 - srcStride] + d*src[index + 2 - srcStride])
                +priv.coeff[subV][1]*(  a*src[index - 1            ] + b*src[index - 0            ]
                                      + c*src[index + 1            ] + d*src[index + 2            ])
                +priv.coeff[subV][2]*(  a*src[index - 1 + srcStride] + b*src[index - 0 + srcStride]
                                      + c*src[index + 1 + srcStride] + d*src[index + 2 + srcStride])
                +priv.coeff[subV][3]*(  a*src[index - 1+2*srcStride] + b*src[index - 0+2*srcStride]
                                      + c*src[index + 1+2*srcStride] + d*src[index + 2+2*srcStride]);
            }else{
                int dx, dy;
                sum=0;

                for(dy=0; dy<4; dy++){
                    int iy= v + dy - 1;
                    if     (iy< 0) iy=0;
                    else if(iy>=h) iy=h-1;
                    for(dx=0; dx<4; dx++){
                        int ix= u + dx - 1;
                        if     (ix< 0) ix=0;
                        else if(ix>=w) ix=w-1;

                        sum+=  priv.coeff[subU][dx]*priv.coeff[subV][dy]
                              *src[ ix + iy*srcStride];
                    }
                }
            }
            sum= (sum + (1<<(COEFF_BITS*2-1)) ) >> (COEFF_BITS*2);
            if(sum&~255){
                if(sum<0) sum=0;
                else      sum=255;
            }
            dst[ x + y*dstStride]= sum;
        }
    }
}

static inline void resampleLinear(uint8_t *dst, uint8_t *src, int w, int h, int dstStride, int srcStride,
                  struct vf_priv_s *privParam, int xShift, int yShift){
    int x, y;
    struct vf_priv_s priv= *privParam;

    for(y=0; y<h; y++){
        for(x=0; x<w; x++){
            int u, v, subU, subV, sum, sx, sy, index, subUI, subVI;

            sx= x << xShift;
            sy= y << yShift;
            u= priv.pv[sx + sy*priv.pvStride][0]>>xShift;
            v= priv.pv[sx + sy*priv.pvStride][1]>>yShift;
            subU= u & (SUB_PIXELS-1);
            subV= v & (SUB_PIXELS-1);
            u >>= SUB_PIXEL_BITS;
            v >>= SUB_PIXEL_BITS;
            index= u + v*srcStride;
            subUI= SUB_PIXELS - subU;
            subVI= SUB_PIXELS - subV;

            if((unsigned)u < (unsigned)(w - 1)){
                if((unsigned)v < (unsigned)(h - 1)){
                    sum= subVI*(subUI*src[index          ] + subU*src[index          +1])
                        +subV *(subUI*src[index+srcStride] + subU*src[index+srcStride+1]);
                    sum= (sum + (1<<(SUB_PIXEL_BITS*2-1)) ) >> (SUB_PIXEL_BITS*2);
                }else{
                    if(v<0) v= 0;
                    else    v= h-1;
                    index= u + v*srcStride;
                    sum= subUI*src[index] + subU*src[index+1];
                    sum= (sum + (1<<(SUB_PIXEL_BITS-1)) ) >> SUB_PIXEL_BITS;
                }
            }else{
                if((unsigned)v < (unsigned)(h - 1)){
                    if(u<0) u= 0;
                    else    u= w-1;
                    index= u + v*srcStride;
                    sum= subVI*src[index] + subV*src[index+srcStride];
                    sum= (sum + (1<<(SUB_PIXEL_BITS-1)) ) >> SUB_PIXEL_BITS;
                }else{
                    if(u<0) u= 0;
                    else    u= w-1;
                    if(v<0) v= 0;
                    else    v= h-1;
                    index= u + v*srcStride;
                    sum= src[index];
                }
            }
            if(sum&~255){
                if(sum<0) sum=0;
                else      sum=255;
            }
            dst[ x + y*dstStride]= sum;
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

    if(vf->priv->cubic){
        resampleCubic(dmpi->planes[0], mpi->planes[0], mpi->w,mpi->h, dmpi->stride[0], mpi->stride[0],
                vf->priv, 0, 0);
        resampleCubic(dmpi->planes[1], mpi->planes[1], cw    , ch   , dmpi->stride[1], mpi->stride[1],
                vf->priv, mpi->chroma_x_shift, mpi->chroma_y_shift);
        resampleCubic(dmpi->planes[2], mpi->planes[2], cw    , ch   , dmpi->stride[2], mpi->stride[2],
                vf->priv, mpi->chroma_x_shift, mpi->chroma_y_shift);
    }else{
        resampleLinear(dmpi->planes[0], mpi->planes[0], mpi->w,mpi->h, dmpi->stride[0], mpi->stride[0],
                vf->priv, 0, 0);
        resampleLinear(dmpi->planes[1], mpi->planes[1], cw    , ch   , dmpi->stride[1], mpi->stride[1],
                vf->priv, mpi->chroma_x_shift, mpi->chroma_y_shift);
        resampleLinear(dmpi->planes[2], mpi->planes[2], cw    , ch   , dmpi->stride[2], mpi->stride[2],
                vf->priv, mpi->chroma_x_shift, mpi->chroma_y_shift);
    }

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
//  vf->get_image=get_image;
    vf->query_format=query_format;
    vf->uninit=uninit;
    vf->priv=malloc(sizeof(struct vf_priv_s));
    memset(vf->priv, 0, sizeof(struct vf_priv_s));

    if(args==NULL) return 0;

    e=sscanf(args, "%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%d",
        &vf->priv->ref[0][0], &vf->priv->ref[0][1],
        &vf->priv->ref[1][0], &vf->priv->ref[1][1],
        &vf->priv->ref[2][0], &vf->priv->ref[2][1],
        &vf->priv->ref[3][0], &vf->priv->ref[3][1],
        &vf->priv->cubic
        );

    if(e!=9)
        return 0;

    return 1;
}

const vf_info_t vf_info_perspective = {
    "perspective correcture",
    "perspective",
    "Michael Niedermayer",
    "",
    vf_open,
    NULL
};

//===========================================================================//
