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


static void mirror(unsigned char* dst,unsigned char* src,int dststride,int srcstride,int w,int h,int bpp,unsigned int fmt){
    int y;
    for(y=0;y<h;y++){
        int x;
        switch(bpp){
        case 1:
            for(x=0;x<w;x++) dst[x]=src[w-x-1];
            break;
        case 2:
            switch(fmt){
            case IMGFMT_UYVY: {
                // packed YUV is tricky. U,V are 32bpp while Y is 16bpp:
                int w2=w>>1;
                for(x=0;x<w2;x++){
                    // TODO: optimize this...
                    dst[x*4+0]=src[0+(w2-x-1)*4];
                    dst[x*4+1]=src[3+(w2-x-1)*4];
                    dst[x*4+2]=src[2+(w2-x-1)*4];
                    dst[x*4+3]=src[1+(w2-x-1)*4];
                }
                break; }
            case IMGFMT_YUY2:
            case IMGFMT_YVYU: {
                // packed YUV is tricky. U,V are 32bpp while Y is 16bpp:
                int w2=w>>1;
                for(x=0;x<w2;x++){
                    // TODO: optimize this...
                    dst[x*4+0]=src[2+(w2-x-1)*4];
                    dst[x*4+1]=src[1+(w2-x-1)*4];
                    dst[x*4+2]=src[0+(w2-x-1)*4];
                    dst[x*4+3]=src[3+(w2-x-1)*4];
                }
                break; }
            default:
                for(x=0;x<w;x++) *((short*)(dst+x*2))=*((short*)(src+(w-x-1)*2));
            }
            break;
        case 3:
            for(x=0;x<w;x++){
                dst[x*3+0]=src[0+(w-x-1)*3];
                dst[x*3+1]=src[1+(w-x-1)*3];
                dst[x*3+2]=src[2+(w-x-1)*3];
            }
            break;
        case 4:
            for(x=0;x<w;x++) *((int*)(dst+x*4))=*((int*)(src+(w-x-1)*4));
        }
        src+=srcstride;
        dst+=dststride;
    }
}

//===========================================================================//

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts){
    mp_image_t *dmpi;

    // hope we'll get DR buffer:
    dmpi=vf_get_image(vf->next,mpi->imgfmt,
        MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE,
        mpi->w, mpi->h);

    if(mpi->flags&MP_IMGFLAG_PLANAR){
               mirror(dmpi->planes[0],mpi->planes[0],
               dmpi->stride[0],mpi->stride[0],
               dmpi->w,dmpi->h,1,mpi->imgfmt);
               mirror(dmpi->planes[1],mpi->planes[1],
               dmpi->stride[1],mpi->stride[1],
               dmpi->w>>mpi->chroma_x_shift,dmpi->h>>mpi->chroma_y_shift,1,mpi->imgfmt);
               mirror(dmpi->planes[2],mpi->planes[2],
               dmpi->stride[2],mpi->stride[2],
               dmpi->w>>mpi->chroma_x_shift,dmpi->h>>mpi->chroma_y_shift,1,mpi->imgfmt);
    } else {
        mirror(dmpi->planes[0],mpi->planes[0],
               dmpi->stride[0],mpi->stride[0],
               dmpi->w,dmpi->h,dmpi->bpp>>3,mpi->imgfmt);
        dmpi->planes[1]=mpi->planes[1]; // passthrough rgb8 palette
    }

    return vf_next_put_image(vf,dmpi, pts);
}

//===========================================================================//

static int vf_open(vf_instance_t *vf, char *args){
    //vf->config=config;
    vf->put_image=put_image;
    return 1;
}

const vf_info_t vf_info_mirror = {
    "horizontal mirror",
    "mirror",
    "Eyck",
    "",
    vf_open,
    NULL
};

//===========================================================================//
