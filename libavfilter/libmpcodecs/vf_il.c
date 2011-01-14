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
#include "libvo/fastmemcpy.h"


//===========================================================================//

typedef struct FilterParam{
    int interleave;
    int swap;
}FilterParam;

struct vf_priv_s {
    FilterParam lumaParam;
    FilterParam chromaParam;
};

/***************************************************************************/

static void interleave(uint8_t *dst, uint8_t *src, int w, int h, int dstStride, int srcStride, int interleave, int swap){
    const int a= swap;
    const int b= 1-a;
    const int m= h>>1;
    int y;

    switch(interleave){
    case -1:
        for(y=0; y < m; y++){
            fast_memcpy(dst + dstStride* y     , src + srcStride*(y*2 + a), w);
            fast_memcpy(dst + dstStride*(y + m), src + srcStride*(y*2 + b), w);
        }
        break;
    case 0:
        for(y=0; y < m; y++){
            fast_memcpy(dst + dstStride* y*2   , src + srcStride*(y*2 + a), w);
            fast_memcpy(dst + dstStride*(y*2+1), src + srcStride*(y*2 + b), w);
        }
        break;
    case 1:
        for(y=0; y < m; y++){
            fast_memcpy(dst + dstStride*(y*2+a), src + srcStride* y     , w);
            fast_memcpy(dst + dstStride*(y*2+b), src + srcStride*(y + m), w);
        }
        break;
    }
}

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts){
    int w;
    FilterParam *luma  = &vf->priv->lumaParam;
    FilterParam *chroma= &vf->priv->chromaParam;

    mp_image_t *dmpi=vf_get_image(vf->next,mpi->imgfmt,
        MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE,
        mpi->w,mpi->h);

    if(mpi->flags&MP_IMGFLAG_PLANAR)
        w= mpi->w;
    else
        w= mpi->w * mpi->bpp/8;

    interleave(dmpi->planes[0], mpi->planes[0],
        w, mpi->h, dmpi->stride[0], mpi->stride[0], luma->interleave, luma->swap);

    if(mpi->flags&MP_IMGFLAG_PLANAR){
        int cw= mpi->w >> mpi->chroma_x_shift;
        int ch= mpi->h >> mpi->chroma_y_shift;

        interleave(dmpi->planes[1], mpi->planes[1], cw,ch,
            dmpi->stride[1], mpi->stride[1], chroma->interleave, luma->swap);
        interleave(dmpi->planes[2], mpi->planes[2], cw,ch,
            dmpi->stride[2], mpi->stride[2], chroma->interleave, luma->swap);
    }

    return vf_next_put_image(vf,dmpi, pts);
}

//===========================================================================//

static void parse(FilterParam *fp, char* args){
    char *pos;
    char *max= strchr(args, ':');

    if(!max) max= args + strlen(args);

    pos= strchr(args, 's');
    if(pos && pos<max) fp->swap=1;
    pos= strchr(args, 'i');
    if(pos && pos<max) fp->interleave=1;
    pos= strchr(args, 'd');
    if(pos && pos<max) fp->interleave=-1;
}

static int vf_open(vf_instance_t *vf, char *args){

    vf->put_image=put_image;
//    vf->get_image=get_image;
    vf->priv=malloc(sizeof(struct vf_priv_s));
    memset(vf->priv, 0, sizeof(struct vf_priv_s));

    if(args)
    {
        char *arg2= strchr(args,':');
        if(arg2) parse(&vf->priv->chromaParam, arg2+1);
        parse(&vf->priv->lumaParam, args);
    }

    return 1;
}

const vf_info_t vf_info_il = {
    "(de)interleave",
    "il",
    "Michael Niedermayer",
    "",
    vf_open,
    NULL
};

//===========================================================================//
