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

//===========================================================================//

struct vf_priv_s {
    unsigned int fmt;
    int w, h;
};

static unsigned int getfmt(unsigned int outfmt){
    switch(outfmt){
    case IMGFMT_RGB12:
    case IMGFMT_RGB15:
    case IMGFMT_RGB16:
    case IMGFMT_RGB24:
    case IMGFMT_RGBA:
    case IMGFMT_ARGB:
    case IMGFMT_BGR12:
    case IMGFMT_BGR15:
    case IMGFMT_BGR16:
    case IMGFMT_BGR24:
    case IMGFMT_BGRA:
    case IMGFMT_ABGR:
        return outfmt;
    }
    return 0;
}

static void put_pixel(uint8_t *buf, int x, int y, int stride, int r, int g, int b, int fmt){
    switch(fmt){
    case IMGFMT_BGR12: ((uint16_t*)(buf + y*stride))[x]=
                           ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
    break;
    case IMGFMT_RGB12: ((uint16_t*)(buf + y*stride))[x]=
                           ((b >> 4) << 8) | ((g >> 4) << 4) | (r >> 4);
    break;
    case IMGFMT_BGR15: ((uint16_t*)(buf + y*stride))[x]= ((r>>3)<<10) | ((g>>3)<<5) | (b>>3);
    break;
    case IMGFMT_RGB15: ((uint16_t*)(buf + y*stride))[x]= ((b>>3)<<10) | ((g>>3)<<5) | (r>>3);
    break;
    case IMGFMT_BGR16: ((uint16_t*)(buf + y*stride))[x]= ((r>>3)<<11) | ((g>>2)<<5) | (b>>3);
    break;
    case IMGFMT_RGB16: ((uint16_t*)(buf + y*stride))[x]= ((b>>3)<<11) | ((g>>2)<<5) | (r>>3);
    break;
    case IMGFMT_RGB24:
        buf[3*x + y*stride + 0]= r;
        buf[3*x + y*stride + 1]= g;
        buf[3*x + y*stride + 2]= b;
    break;
    case IMGFMT_BGR24:
        buf[3*x + y*stride + 0]= b;
        buf[3*x + y*stride + 1]= g;
        buf[3*x + y*stride + 2]= r;
    break;
    case IMGFMT_RGBA:
        buf[4*x + y*stride + 0]= r;
        buf[4*x + y*stride + 1]= g;
        buf[4*x + y*stride + 2]= b;
    break;
    case IMGFMT_BGRA:
        buf[4*x + y*stride + 0]= b;
        buf[4*x + y*stride + 1]= g;
        buf[4*x + y*stride + 2]= r;
    break;
    case IMGFMT_ARGB:
        buf[4*x + y*stride + 1]= r;
        buf[4*x + y*stride + 2]= g;
        buf[4*x + y*stride + 3]= b;
    break;
    case IMGFMT_ABGR:
        buf[4*x + y*stride + 1]= b;
        buf[4*x + y*stride + 2]= g;
        buf[4*x + y*stride + 3]= r;
    break;
    }
}

static int config(struct vf_instance *vf,
        int width, int height, int d_width, int d_height,
        unsigned int flags, unsigned int outfmt){
    if (vf->priv->w > 0) { d_width  = width  = vf->priv->w; }
    if (vf->priv->h > 0) { d_height = height = vf->priv->h; }
    vf->priv->fmt=getfmt(outfmt);
    mp_msg(MSGT_VFILTER,MSGL_V,"rgb test format:%s\n", vo_format_name(outfmt));
    return vf_next_config(vf,width,height,d_width,d_height,flags,vf->priv->fmt);
}

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts){
    mp_image_t *dmpi;
    int x, y;
    int w = vf->priv->w > 0 ? vf->priv->w : mpi->w;
    int h = vf->priv->h > 0 ? vf->priv->h : mpi->h;

    // hope we'll get DR buffer:
    dmpi=vf_get_image(vf->next,vf->priv->fmt,
        MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE,
        w, h);

     for(y=0; y<h; y++){
         for(x=0; x<w; x++){
             int c= 256*x/w;
             int r=0,g=0,b=0;

             if(3*y<h)        r=c;
             else if(3*y<2*h) g=c;
             else                  b=c;

             put_pixel(dmpi->planes[0], x, y, dmpi->stride[0], r, g, b, vf->priv->fmt);
         }
     }

    return vf_next_put_image(vf,dmpi, pts);
}

//===========================================================================//

static int query_format(struct vf_instance *vf, unsigned int outfmt){
    unsigned int fmt=getfmt(outfmt);
    if(!fmt) return 0;
    return vf_next_query_format(vf,fmt) & (~VFCAP_CSP_SUPPORTED_BY_HW);
}

static int vf_open(vf_instance_t *vf, char *args){
    vf->config=config;
    vf->put_image=put_image;
    vf->query_format=query_format;
    vf->priv=malloc(sizeof(struct vf_priv_s));
    vf->priv->w = vf->priv->h = 0;
    if (args)
        sscanf(args, "%d:%d", &vf->priv->w, &vf->priv->h);
    return 1;
}

const vf_info_t vf_info_rgbtest = {
    "rgbtest",
    "rgbtest",
    "Michael Niedermayer",
    "",
    vf_open,
    NULL
};

//===========================================================================//
