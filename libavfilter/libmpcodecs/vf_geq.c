/*
 * Copyright (C) 2006 Michael Niedermayer <michaelni@gmx.at>
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
#include <math.h>
#include <inttypes.h>

#include "config.h"

#include "mp_msg.h"
#include "cpudetect.h"

#include "img_format.h"
#include "mp_image.h"
#include "vf.h"

#include "libavcodec/avcodec.h"
#include "libavutil/eval.h"

struct vf_priv_s {
    AVExpr * e[3];
    int framenum;
    mp_image_t *mpi;
};

static int config(struct vf_instance *vf,
        int width, int height, int d_width, int d_height,
        unsigned int flags, unsigned int outfmt){
    return vf_next_config(vf,width,height,d_width,d_height,flags,outfmt);
}

static inline double getpix(struct vf_instance *vf, double x, double y, int plane){
    int xi, yi;
    mp_image_t *mpi= vf->priv->mpi;
    int stride= mpi->stride[plane];
    uint8_t *src=  mpi->planes[plane];
    xi=x= FFMIN(FFMAX(x, 0), (mpi->w >> (plane ? mpi->chroma_x_shift : 0))-1);
    yi=y= FFMIN(FFMAX(y, 0), (mpi->h >> (plane ? mpi->chroma_y_shift : 0))-1);

    x-=xi;
    y-=yi;

    return
     (1-y)*((1-x)*src[xi +  yi    * stride] + x*src[xi + 1 +  yi    * stride])
    +   y *((1-x)*src[xi + (yi+1) * stride] + x*src[xi + 1 + (yi+1) * stride]);
}

//FIXME cubic interpolate
//FIXME keep the last few frames
static double lum(void *vf, double x, double y){
    return getpix(vf, x, y, 0);
}

static double cb(void *vf, double x, double y){
    return getpix(vf, x, y, 1);
}

static double cr(void *vf, double x, double y){
    return getpix(vf, x, y, 2);
}

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts){
    mp_image_t *dmpi;
    int x,y, plane;

    if(!(mpi->flags&MP_IMGFLAG_DIRECT)){
        // no DR, so get a new image! hope we'll get DR buffer:
        vf->dmpi=vf_get_image(vf->next,mpi->imgfmt, MP_IMGTYPE_TEMP,
                              MP_IMGFLAG_ACCEPT_STRIDE|MP_IMGFLAG_PREFER_ALIGNED_STRIDE,
                              mpi->w,mpi->h);
    }

    dmpi= vf->dmpi;
    vf->priv->mpi= mpi;

    vf_clone_mpi_attributes(dmpi, mpi);

    for(plane=0; plane<3; plane++){
        int w= mpi->w >> (plane ? mpi->chroma_x_shift : 0);
        int h= mpi->h >> (plane ? mpi->chroma_y_shift : 0);
        uint8_t *dst  = dmpi->planes[plane];
        int dst_stride= dmpi->stride[plane];
        double const_values[]={
            M_PI,
            M_E,
            0,
            0,
            w,
            h,
            vf->priv->framenum,
            w/(double)mpi->w,
            h/(double)mpi->h,
            0
        };
        if (!vf->priv->e[plane]) continue;
        for(y=0; y<h; y++){
            const_values[3]=y;
            for(x=0; x<w; x++){
                const_values[2]=x;
                dst[x + y * dst_stride] = av_expr_eval(vf->priv->e[plane],
                                                       const_values, vf);
            }
        }
    }

    vf->priv->framenum++;

    return vf_next_put_image(vf,dmpi, pts);
}

static void uninit(struct vf_instance *vf){
    av_free(vf->priv);
    vf->priv=NULL;
}

//===========================================================================//
static int vf_open(vf_instance_t *vf, char *args){
    char eq[3][2000] = { { 0 }, { 0 }, { 0 } };
    int plane, res;

    vf->config=config;
    vf->put_image=put_image;
//    vf->get_image=get_image;
    vf->uninit=uninit;
    vf->priv=av_malloc(sizeof(struct vf_priv_s));
    memset(vf->priv, 0, sizeof(struct vf_priv_s));

    if (args) sscanf(args, "%1999[^:]:%1999[^:]:%1999[^:]", eq[0], eq[1], eq[2]);

    if (!eq[1][0]) strncpy(eq[1], eq[0], sizeof(eq[0])-1);
    if (!eq[2][0]) strncpy(eq[2], eq[1], sizeof(eq[0])-1);

    for(plane=0; plane<3; plane++){
        static const char *const_names[]={
            "PI",
            "E",
            "X",
            "Y",
            "W",
            "H",
            "N",
            "SW",
            "SH",
            NULL
        };
        static const char *func2_names[]={
            "lum",
            "cb",
            "cr",
            "p",
            NULL
        };
        double (*func2[])(void *, double, double)={
            lum,
            cb,
            cr,
            plane==0 ? lum : (plane==1 ? cb : cr),
            NULL
        };
        res = av_expr_parse(&vf->priv->e[plane], eq[plane], const_names, NULL, NULL, func2_names, func2, 0, NULL);

        if (res < 0) {
            mp_msg(MSGT_VFILTER, MSGL_ERR, "geq: error loading equation `%s'\n", eq[plane]);
            return 0;
        }
    }

    return 1;
}

const vf_info_t vf_info_geq = {
    "generic equation filter",
    "geq",
    "Michael Niedermayer",
    "",
    vf_open,
    NULL
};
