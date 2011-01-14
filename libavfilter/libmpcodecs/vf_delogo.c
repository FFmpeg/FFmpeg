/*
 * Copyright (C) 2002 Jindrich Makovicka <makovick@gmail.com>
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

/* A very simple tv station logo remover */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

#include "mp_msg.h"
#include "cpudetect.h"
#include "img_format.h"
#include "mp_image.h"
#include "vf.h"
#include "libvo/fastmemcpy.h"

//===========================================================================//

struct vf_priv_s {
    unsigned int outfmt;
    int xoff, yoff, lw, lh, band, show;
};

#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MAX(a,b) (((a) > (b)) ? (a) : (b))

static void delogo(uint8_t *dst, uint8_t *src, int dstStride, int srcStride, int width, int height,
                   int logo_x, int logo_y, int logo_w, int logo_h, int band, int show, int direct) {
    int y, x;
    int interp, dist;
    uint8_t *xdst, *xsrc;

    uint8_t *topleft, *botleft, *topright;
    int xclipl, xclipr, yclipt, yclipb;
    int logo_x1, logo_x2, logo_y1, logo_y2;

    xclipl = MAX(-logo_x, 0);
    xclipr = MAX(logo_x+logo_w-width, 0);
    yclipt = MAX(-logo_y, 0);
    yclipb = MAX(logo_y+logo_h-height, 0);

    logo_x1 = logo_x + xclipl;
    logo_x2 = logo_x + logo_w - xclipr;
    logo_y1 = logo_y + yclipt;
    logo_y2 = logo_y + logo_h - yclipb;

    topleft = src+logo_y1*srcStride+logo_x1;
    topright = src+logo_y1*srcStride+logo_x2-1;
    botleft = src+(logo_y2-1)*srcStride+logo_x1;

    if (!direct) memcpy_pic(dst, src, width, height, dstStride, srcStride);

    dst += (logo_y1+1)*dstStride;
    src += (logo_y1+1)*srcStride;

    for(y = logo_y1+1; y < logo_y2-1; y++)
    {
        for (x = logo_x1+1, xdst = dst+logo_x1+1, xsrc = src+logo_x1+1; x < logo_x2-1; x++, xdst++, xsrc++) {
            interp = ((topleft[srcStride*(y-logo_y-yclipt)]
                       + topleft[srcStride*(y-logo_y-1-yclipt)]
                       + topleft[srcStride*(y-logo_y+1-yclipt)])*(logo_w-(x-logo_x))/logo_w
                      + (topright[srcStride*(y-logo_y-yclipt)]
                         + topright[srcStride*(y-logo_y-1-yclipt)]
                         + topright[srcStride*(y-logo_y+1-yclipt)])*(x-logo_x)/logo_w
                      + (topleft[x-logo_x-xclipl]
                         + topleft[x-logo_x-1-xclipl]
                         + topleft[x-logo_x+1-xclipl])*(logo_h-(y-logo_y))/logo_h
                      + (botleft[x-logo_x-xclipl]
                         + botleft[x-logo_x-1-xclipl]
                         + botleft[x-logo_x+1-xclipl])*(y-logo_y)/logo_h
                )/6;
/*                interp = (topleft[srcStride*(y-logo_y)]*(logo_w-(x-logo_x))/logo_w
                          + topright[srcStride*(y-logo_y)]*(x-logo_x)/logo_w
                          + topleft[x-logo_x]*(logo_h-(y-logo_y))/logo_h
                          + botleft[x-logo_x]*(y-logo_y)/logo_h
                          )/2;*/
            if (y >= logo_y+band && y < logo_y+logo_h-band && x >= logo_x+band && x < logo_x+logo_w-band) {
                    *xdst = interp;
            } else {
                dist = 0;
                if (x < logo_x+band) dist = MAX(dist, logo_x-x+band);
                else if (x >= logo_x+logo_w-band) dist = MAX(dist, x-(logo_x+logo_w-1-band));
                if (y < logo_y+band) dist = MAX(dist, logo_y-y+band);
                else if (y >= logo_y+logo_h-band) dist = MAX(dist, y-(logo_y+logo_h-1-band));
                *xdst = (*xsrc*dist + interp*(band-dist))/band;
                if (show && (dist == band-1)) *xdst = 0;
            }
        }

        dst+= dstStride;
        src+= srcStride;
    }
}

static int config(struct vf_instance *vf,
                  int width, int height, int d_width, int d_height,
                  unsigned int flags, unsigned int outfmt){

    return vf_next_config(vf,width,height,d_width,d_height,flags,outfmt);
}


static void get_image(struct vf_instance *vf, mp_image_t *mpi){
    if(mpi->flags&MP_IMGFLAG_PRESERVE) return; // don't change
    if(mpi->imgfmt!=vf->priv->outfmt) return; // colorspace differ
    // ok, we can do pp in-place (or pp disabled):
    vf->dmpi=vf_get_image(vf->next,mpi->imgfmt,
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
        vf->dmpi=vf_get_image(vf->next,vf->priv->outfmt,
                              MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE,
                              mpi->w,mpi->h);
    }
    dmpi= vf->dmpi;

    delogo(dmpi->planes[0], mpi->planes[0], dmpi->stride[0], mpi->stride[0], mpi->w, mpi->h,
           vf->priv->xoff, vf->priv->yoff, vf->priv->lw, vf->priv->lh, vf->priv->band, vf->priv->show,
           mpi->flags&MP_IMGFLAG_DIRECT);
    delogo(dmpi->planes[1], mpi->planes[1], dmpi->stride[1], mpi->stride[1], mpi->w/2, mpi->h/2,
           vf->priv->xoff/2, vf->priv->yoff/2, vf->priv->lw/2, vf->priv->lh/2, vf->priv->band/2, vf->priv->show,
           mpi->flags&MP_IMGFLAG_DIRECT);
    delogo(dmpi->planes[2], mpi->planes[2], dmpi->stride[2], mpi->stride[2], mpi->w/2, mpi->h/2,
           vf->priv->xoff/2, vf->priv->yoff/2, vf->priv->lw/2, vf->priv->lh/2, vf->priv->band/2, vf->priv->show,
           mpi->flags&MP_IMGFLAG_DIRECT);

    vf_clone_mpi_attributes(dmpi, mpi);

    return vf_next_put_image(vf,dmpi, pts);
}

static void uninit(struct vf_instance *vf){
    if(!vf->priv) return;

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
        return vf_next_query_format(vf,vf->priv->outfmt);
    }
    return 0;
}

static const unsigned int fmt_list[]={
    IMGFMT_YV12,
    IMGFMT_I420,
    IMGFMT_IYUV,
    0
};

static int vf_open(vf_instance_t *vf, char *args){
    int res=0;
    vf->config=config;
    vf->put_image=put_image;
    vf->get_image=get_image;
    vf->query_format=query_format;
    vf->uninit=uninit;

    vf->priv=malloc(sizeof(struct vf_priv_s));
    memset(vf->priv, 0, sizeof(struct vf_priv_s));

    if (args) res = sscanf(args, "%d:%d:%d:%d:%d",
                          &vf->priv->xoff, &vf->priv->yoff,
                          &vf->priv->lw, &vf->priv->lh,
                          &vf->priv->band);

    if (res != 5) {
       mp_msg(MSGT_VFILTER, MSGL_ERR, "deLogo: syntax is \"delogo=xoff:yoff:width:height:band\"\n");
       uninit(vf);
       return 0;
    }

    mp_msg(MSGT_VFILTER, MSGL_V, "delogo: %d x %d, %d x %d, band = %d\n",
           vf->priv->xoff, vf->priv->yoff,
           vf->priv->lw, vf->priv->lh,
           vf->priv->band);

    vf->priv->show = 0;

    if (vf->priv->band < 0) {
        vf->priv->band = 4;
        vf->priv->show = 1;
    }


    vf->priv->lw += vf->priv->band*2;
    vf->priv->lh += vf->priv->band*2;
    vf->priv->xoff -= vf->priv->band;
    vf->priv->yoff -= vf->priv->band;

    // check csp:
    vf->priv->outfmt=vf_match_csp(&vf->next,fmt_list,IMGFMT_YV12);
    if(!vf->priv->outfmt)
    {
        uninit(vf);
        return 0; // no csp match :(
    }

    return 1;
}

const vf_info_t vf_info_delogo = {
    "simple logo remover",
    "delogo",
    "Jindrich Makovicka, Alex Beregszaszi",
    "",
    vf_open,
};

//===========================================================================//
