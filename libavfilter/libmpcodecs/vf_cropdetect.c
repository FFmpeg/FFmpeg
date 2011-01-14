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
#include "help_mp.h"

#include "img_format.h"
#include "mp_image.h"
#include "vf.h"

struct vf_priv_s {
    int x1,y1,x2,y2;
    int limit;
    int round;
    int reset_count;
    int fno;
};

static int checkline(unsigned char* src,int stride,int len,int bpp){
    int total=0;
    int div=len;
    switch(bpp){
    case 1:
        while(--len>=0){
            total+=src[0]; src+=stride;
        }
        break;
    case 3:
    case 4:
        while(--len>=0){
            total+=src[0]+src[1]+src[2]; src+=stride;
        }
        div*=3;
        break;
    }
    total/=div;
//    printf("total=%d\n",total);
    return total;
}

//===========================================================================//

static int config(struct vf_instance *vf,
        int width, int height, int d_width, int d_height,
        unsigned int flags, unsigned int outfmt){
    vf->priv->x1=width - 1;
    vf->priv->y1=height - 1;
    vf->priv->x2=0;
    vf->priv->y2=0;
    vf->priv->fno=-2;
    return vf_next_config(vf,width,height,d_width,d_height,flags,outfmt);
}

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts){
    mp_image_t *dmpi;
    int bpp=mpi->bpp/8;
    int w,h,x,y,shrink_by;

    // hope we'll get DR buffer:
    dmpi=vf_get_image(vf->next,mpi->imgfmt,
        MP_IMGTYPE_EXPORT, 0,
        mpi->w, mpi->h);

    dmpi->planes[0]=mpi->planes[0];
    dmpi->planes[1]=mpi->planes[1];
    dmpi->planes[2]=mpi->planes[2];
    dmpi->stride[0]=mpi->stride[0];
    dmpi->stride[1]=mpi->stride[1];
    dmpi->stride[2]=mpi->stride[2];
    dmpi->width=mpi->width;
    dmpi->height=mpi->height;

if(++vf->priv->fno>0){        // ignore first 2 frames - they may be empty

    // Reset the crop area every reset_count frames, if reset_count is > 0
    if(vf->priv->reset_count > 0 && vf->priv->fno > vf->priv->reset_count){
        vf->priv->x1=mpi->w-1;
        vf->priv->y1=mpi->h-1;
        vf->priv->x2=0;
        vf->priv->y2=0;
        vf->priv->fno=1;
    }

    for(y=0;y<vf->priv->y1;y++){
        if(checkline(mpi->planes[0]+mpi->stride[0]*y,bpp,mpi->w,bpp)>vf->priv->limit){
            vf->priv->y1=y;
            break;
        }
    }

    for(y=mpi->h-1;y>vf->priv->y2;y--){
        if(checkline(mpi->planes[0]+mpi->stride[0]*y,bpp,mpi->w,bpp)>vf->priv->limit){
            vf->priv->y2=y;
            break;
        }
    }

    for(y=0;y<vf->priv->x1;y++){
        if(checkline(mpi->planes[0]+bpp*y,mpi->stride[0],mpi->h,bpp)>vf->priv->limit){
            vf->priv->x1=y;
            break;
        }
    }

    for(y=mpi->w-1;y>vf->priv->x2;y--){
        if(checkline(mpi->planes[0]+bpp*y,mpi->stride[0],mpi->h,bpp)>vf->priv->limit){
            vf->priv->x2=y;
            break;
        }
    }

    // round x and y (up), important for yuv colorspaces
    // make sure they stay rounded!
    x=(vf->priv->x1+1)&(~1);
    y=(vf->priv->y1+1)&(~1);

    w = vf->priv->x2 - x + 1;
    h = vf->priv->y2 - y + 1;

    // w and h must be divisible by 2 as well because of yuv
    // colorspace problems.
    if (vf->priv->round <= 1)
      vf->priv->round = 16;
    if (vf->priv->round % 2)
      vf->priv->round *= 2;

    shrink_by = w % vf->priv->round;
    w -= shrink_by;
    x += (shrink_by / 2 + 1) & ~1;

    shrink_by = h % vf->priv->round;
    h -= shrink_by;
    y += (shrink_by / 2 + 1) & ~1;

    mp_msg(MSGT_VFILTER, MSGL_INFO, MSGTR_MPCODECS_CropArea,
        vf->priv->x1,vf->priv->x2,
        vf->priv->y1,vf->priv->y2,
        w,h,x,y);


}

    return vf_next_put_image(vf,dmpi, pts);
}

static int query_format(struct vf_instance *vf, unsigned int fmt) {
  switch(fmt) {
    // the default limit value works only right with YV12 right now.
    case IMGFMT_YV12:
      return vf_next_query_format(vf, fmt);
  }
  return 0;
}
//===========================================================================//

static int vf_open(vf_instance_t *vf, char *args){
    vf->config=config;
    vf->put_image=put_image;
    vf->query_format=query_format;
    vf->priv=malloc(sizeof(struct vf_priv_s));
    vf->priv->limit=24; // should be option
    vf->priv->round = 0;
    vf->priv->reset_count = 0;
    if(args) sscanf(args, "%d:%d:%d",
    &vf->priv->limit,
    &vf->priv->round,
    &vf->priv->reset_count);
    return 1;
}

const vf_info_t vf_info_cropdetect = {
    "autodetect crop size",
    "cropdetect",
    "A'rpi",
    "",
    vf_open,
    NULL
};

//===========================================================================//
