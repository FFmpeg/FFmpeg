/*
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

#include "config.h"
#include "mp_msg.h"

#include "img_format.h"
#include "mp_image.h"
#include "vf.h"

struct vf_priv_s {
    int direction;
};

static void rotate(unsigned char* dst,unsigned char* src,int dststride,int srcstride,int w,int h,int bpp,int dir){
    int y;
    if(dir&1){
        src+=srcstride*(w-1);
        srcstride*=-1;
    }
    if(dir&2){
        dst+=dststride*(h-1);
        dststride*=-1;
    }

    for(y=0;y<h;y++){
        int x;
        switch(bpp){
        case 1:
            for(x=0;x<w;x++) dst[x]=src[y+x*srcstride];
            break;
        case 2:
            for(x=0;x<w;x++) *((short*)(dst+x*2))=*((short*)(src+y*2+x*srcstride));
            break;
        case 3:
            for(x=0;x<w;x++){
                dst[x*3+0]=src[0+y*3+x*srcstride];
                dst[x*3+1]=src[1+y*3+x*srcstride];
                dst[x*3+2]=src[2+y*3+x*srcstride];
            }
            break;
        case 4:
            for(x=0;x<w;x++) *((int*)(dst+x*4))=*((int*)(src+y*4+x*srcstride));
        }
        dst+=dststride;
    }
}

//===========================================================================//

static int config(struct vf_instance *vf,
        int width, int height, int d_width, int d_height,
        unsigned int flags, unsigned int outfmt){
    if (vf->priv->direction & 4) {
        if (width<height) vf->priv->direction&=3;
    }
    if (vf->priv->direction & 4){
        vf->put_image=vf_next_put_image; // passthru mode!
        if (vf->next->draw_slice) vf->draw_slice=vf_next_draw_slice;
/* FIXME: this should be in an other procedure in vf.c; that should always check
     whether the filter after the passthrough one still (not)supports slices */
        return vf_next_config(vf,width,height,d_width,d_height,flags,outfmt);
    }
    return vf_next_config(vf,height,width,d_height,d_width,flags,outfmt);
}

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts){
    mp_image_t *dmpi;

    // hope we'll get DR buffer:
    dmpi=vf_get_image(vf->next,mpi->imgfmt,
        MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE,
        mpi->h, mpi->w);

    if(mpi->flags&MP_IMGFLAG_PLANAR){
        rotate(dmpi->planes[0],mpi->planes[0],
               dmpi->stride[0],mpi->stride[0],
               dmpi->w,dmpi->h,1,vf->priv->direction);
        rotate(dmpi->planes[1],mpi->planes[1],
               dmpi->stride[1],mpi->stride[1],
               dmpi->w>>mpi->chroma_x_shift,dmpi->h>>mpi->chroma_y_shift,1,vf->priv->direction);
        rotate(dmpi->planes[2],mpi->planes[2],
               dmpi->stride[2],mpi->stride[2],
               dmpi->w>>mpi->chroma_x_shift,dmpi->h>>mpi->chroma_y_shift,1,vf->priv->direction);
    } else {
        rotate(dmpi->planes[0],mpi->planes[0],
               dmpi->stride[0],mpi->stride[0],
               dmpi->w,dmpi->h,dmpi->bpp>>3,vf->priv->direction);
        dmpi->planes[1] = mpi->planes[1]; // passthrough rgb8 palette
    }

    return vf_next_put_image(vf,dmpi, pts);
}

//===========================================================================//

static int query_format(struct vf_instance *vf, unsigned int fmt){
    if(IMGFMT_IS_RGB(fmt) || IMGFMT_IS_BGR(fmt)) return vf_next_query_format(vf, fmt);
    // we can support only symmetric (chroma_x_shift==chroma_y_shift) YUV formats:
    switch(fmt) {
        case IMGFMT_YV12:
        case IMGFMT_I420:
        case IMGFMT_IYUV:
        case IMGFMT_YVU9:
//        case IMGFMT_IF09:
        case IMGFMT_Y8:
        case IMGFMT_Y800:
        case IMGFMT_444P:
            return vf_next_query_format(vf, fmt);
    }
    return 0;
}

static int vf_open(vf_instance_t *vf, char *args){
    vf->config=config;
    vf->put_image=put_image;
    vf->query_format=query_format;
    vf->priv=malloc(sizeof(struct vf_priv_s));
    vf->priv->direction=args?atoi(args):0;
    return 1;
}

const vf_info_t vf_info_rotate = {
    "rotate",
    "rotate",
    "A'rpi",
    "",
    vf_open,
    NULL
};

//===========================================================================//
