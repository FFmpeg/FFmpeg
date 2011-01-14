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

#include "config.h"
#include "mp_msg.h"

#include "mp_image.h"
#include "vf.h"

struct vf_priv_s {
    int interleave;
    int height;
    int width;
    int stridefactor;
};

//===========================================================================//

static int config(struct vf_instance *vf,
        int width, int height, int d_width, int d_height,
        unsigned int flags, unsigned int outfmt){
        int pixel_stride= (width+15)&~15; //FIXME this is ust a guess ... especially for non planar its somewhat bad one

#if 0
    if(mpi->flags&MP_IMGFLAG_PLANAR)
        pixel_stride= mpi->stride[0];
    else
        pixel_stride= 8*mpi->stride[0] / mpi->bpp;

#endif

    if(vf->priv->interleave){
        vf->priv->height= 2*height;
        vf->priv->width= width - (pixel_stride/2);
        vf->priv->stridefactor=1;
    }else{
        vf->priv->height= height/2;
        vf->priv->width= width + pixel_stride;
        vf->priv->stridefactor=4;
    }
//printf("hX %d %d %d\n", vf->priv->width,vf->priv->height,vf->priv->stridefactor);

    return vf_next_config(vf, vf->priv->width, vf->priv->height,
        (d_width*vf->priv->stridefactor)>>1, 2*d_height/vf->priv->stridefactor, flags, outfmt);
}

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts){
    if(mpi->flags&MP_IMGFLAG_DIRECT){
        // we've used DR, so we're ready...
        return vf_next_put_image(vf,(mp_image_t*)mpi->priv, pts);
    }

    vf->dmpi=vf_get_image(vf->next,mpi->imgfmt,
        MP_IMGTYPE_EXPORT, MP_IMGFLAG_ACCEPT_STRIDE,
        vf->priv->width, vf->priv->height);

    // set up mpi as a double-stride image of dmpi:
    vf->dmpi->planes[0]=mpi->planes[0];
    vf->dmpi->stride[0]=(mpi->stride[0]*vf->priv->stridefactor)>>1;
    if(vf->dmpi->flags&MP_IMGFLAG_PLANAR){
        vf->dmpi->planes[1]=mpi->planes[1];
        vf->dmpi->stride[1]=(mpi->stride[1]*vf->priv->stridefactor)>>1;
        vf->dmpi->planes[2]=mpi->planes[2];
        vf->dmpi->stride[2]=(mpi->stride[2]*vf->priv->stridefactor)>>1;
    } else
        vf->dmpi->planes[1]=mpi->planes[1]; // passthru bgr8 palette!!!

    return vf_next_put_image(vf,vf->dmpi, pts);
}

//===========================================================================//

static void uninit(struct vf_instance *vf)
{
        free(vf->priv);
}

static int vf_open(vf_instance_t *vf, char *args){
    vf->config=config;
    vf->put_image=put_image;
    vf->uninit=uninit;
    vf->default_reqs=VFCAP_ACCEPT_STRIDE;
    vf->priv=calloc(1, sizeof(struct vf_priv_s));
    vf->priv->interleave= args && (*args == 'i');
    return 1;
}

const vf_info_t vf_info_fil = {
    "fast (de)interleaver",
    "fil",
    "Michael Niedermayer",
    "",
    vf_open,
    NULL
};

//===========================================================================//
