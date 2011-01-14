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
    int field;
};

//===========================================================================//

static int config(struct vf_instance *vf,
        int width, int height, int d_width, int d_height,
        unsigned int flags, unsigned int outfmt){
    return vf_next_config(vf,width,height/2,d_width,d_height,flags,outfmt);
}

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts){
    vf->dmpi=vf_get_image(vf->next,mpi->imgfmt,
        MP_IMGTYPE_EXPORT, MP_IMGFLAG_ACCEPT_STRIDE,
        mpi->width, mpi->height/2);

    // set up mpi as a double-stride image of dmpi:
    vf->dmpi->planes[0]=mpi->planes[0]+mpi->stride[0]*vf->priv->field;
    vf->dmpi->stride[0]=2*mpi->stride[0];
    if(vf->dmpi->flags&MP_IMGFLAG_PLANAR){
        vf->dmpi->planes[1]=mpi->planes[1]+
            mpi->stride[1]*vf->priv->field;
        vf->dmpi->stride[1]=2*mpi->stride[1];
        vf->dmpi->planes[2]=mpi->planes[2]+
            mpi->stride[2]*vf->priv->field;
        vf->dmpi->stride[2]=2*mpi->stride[2];
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
    if (args) sscanf(args, "%d", &vf->priv->field);
    vf->priv->field &= 1;
    return 1;
}

const vf_info_t vf_info_field = {
    "extract single field",
    "field",
    "Rich Felker",
    "",
    vf_open,
    NULL
};

//===========================================================================//
