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

#include "mp_msg.h"
#include "img_format.h"
#include "mp_image.h"
#include "vf.h"


//===========================================================================//

typedef struct FilterParam{
        int radius;
        int power;
}FilterParam;

struct vf_priv_s {
        FilterParam lumaParam;
        FilterParam chromaParam;
};


/***************************************************************************/


static int config(struct vf_instance *vf,
        int width, int height, int d_width, int d_height,
        unsigned int flags, unsigned int outfmt){

        return vf_next_config(vf,width,height,d_width,d_height,flags,outfmt);
}

static inline void blur(uint8_t *dst, uint8_t *src, int w, int radius, int dstStep, int srcStep){
        int x;
        const int length= radius*2 + 1;
        const int inv= ((1<<16) + length/2)/length;

        int sum= 0;

        for(x=0; x<radius; x++){
                sum+= src[x*srcStep]<<1;
        }
        sum+= src[radius*srcStep];

        for(x=0; x<=radius; x++){
                sum+= src[(radius+x)*srcStep] - src[(radius-x)*srcStep];
                dst[x*dstStep]= (sum*inv + (1<<15))>>16;
        }

        for(; x<w-radius; x++){
                sum+= src[(radius+x)*srcStep] - src[(x-radius-1)*srcStep];
                dst[x*dstStep]= (sum*inv + (1<<15))>>16;
        }

        for(; x<w; x++){
                sum+= src[(2*w-radius-x-1)*srcStep] - src[(x-radius-1)*srcStep];
                dst[x*dstStep]= (sum*inv + (1<<15))>>16;
        }
}

static inline void blur2(uint8_t *dst, uint8_t *src, int w, int radius, int power, int dstStep, int srcStep){
        uint8_t temp[2][4096];
        uint8_t *a= temp[0], *b=temp[1];

        if(radius){
                blur(a, src, w, radius, 1, srcStep);
                for(; power>2; power--){
                        uint8_t *c;
                        blur(b, a, w, radius, 1, 1);
                        c=a; a=b; b=c;
                }
                if(power>1)
                        blur(dst, a, w, radius, dstStep, 1);
                else{
                        int i;
                        for(i=0; i<w; i++)
                                dst[i*dstStep]= a[i];
                }
        }else{
                int i;
                for(i=0; i<w; i++)
                        dst[i*dstStep]= src[i*srcStep];
        }
}

static void hBlur(uint8_t *dst, uint8_t *src, int w, int h, int dstStride, int srcStride, int radius, int power){
        int y;

        if(radius==0 && dst==src) return;

        for(y=0; y<h; y++){
                blur2(dst + y*dstStride, src + y*srcStride, w, radius, power, 1, 1);
        }
}

//FIXME optimize (x before y !!!)
static void vBlur(uint8_t *dst, uint8_t *src, int w, int h, int dstStride, int srcStride, int radius, int power){
        int x;

        if(radius==0 && dst==src) return;

        for(x=0; x<w; x++){
                blur2(dst + x, src + x, h, radius, power, dstStride, srcStride);
        }
}

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts){
        int cw= mpi->w >> mpi->chroma_x_shift;
        int ch= mpi->h >> mpi->chroma_y_shift;

        mp_image_t *dmpi=vf_get_image(vf->next,mpi->imgfmt,
                MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE | MP_IMGFLAG_READABLE,
                mpi->w,mpi->h);

        assert(mpi->flags&MP_IMGFLAG_PLANAR);

        hBlur(dmpi->planes[0], mpi->planes[0], mpi->w,mpi->h,
                dmpi->stride[0], mpi->stride[0], vf->priv->lumaParam.radius, vf->priv->lumaParam.power);
        hBlur(dmpi->planes[1], mpi->planes[1], cw,ch,
                dmpi->stride[1], mpi->stride[1], vf->priv->chromaParam.radius, vf->priv->chromaParam.power);
        hBlur(dmpi->planes[2], mpi->planes[2], cw,ch,
                dmpi->stride[2], mpi->stride[2], vf->priv->chromaParam.radius, vf->priv->chromaParam.power);

        vBlur(dmpi->planes[0], dmpi->planes[0], mpi->w,mpi->h,
                dmpi->stride[0], dmpi->stride[0], vf->priv->lumaParam.radius, vf->priv->lumaParam.power);
        vBlur(dmpi->planes[1], dmpi->planes[1], cw,ch,
                dmpi->stride[1], dmpi->stride[1], vf->priv->chromaParam.radius, vf->priv->chromaParam.power);
        vBlur(dmpi->planes[2], dmpi->planes[2], cw,ch,
                dmpi->stride[2], dmpi->stride[2], vf->priv->chromaParam.radius, vf->priv->chromaParam.power);

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
//        vf->get_image=get_image;
        vf->query_format=query_format;
        vf->priv=malloc(sizeof(struct vf_priv_s));
        memset(vf->priv, 0, sizeof(struct vf_priv_s));

        if(args==NULL) return 0;

        e=sscanf(args, "%d:%d:%d:%d",
                &vf->priv->lumaParam.radius,
                &vf->priv->lumaParam.power,
                &vf->priv->chromaParam.radius,
                &vf->priv->chromaParam.power
                );

        if(e==2){
                vf->priv->chromaParam.radius= vf->priv->lumaParam.radius;
                vf->priv->chromaParam.power = vf->priv->lumaParam.power;
        }else if(e!=4)
                return 0;

        if(vf->priv->lumaParam.radius < 0) return 0;
        if(vf->priv->chromaParam.radius < 0) return 0;

        return 1;
}

const vf_info_t vf_info_boxblur = {
    "box blur",
    "boxblur",
    "Michael Niedermayer",
    "",
    vf_open,
    NULL
};

//===========================================================================//
