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

#include "libvo/fastmemcpy.h"

//===========================================================================//

static int config(struct vf_instance *vf,
        int width, int height, int d_width, int d_height,
        unsigned int flags, unsigned int outfmt){

    if(vf_next_query_format(vf,IMGFMT_YV12)<=0){
        mp_msg(MSGT_VFILTER, MSGL_WARN, MSGTR_MPCODECS_WarnNextFilterDoesntSupport, "YVU9");
        return 0;
    }

    return vf_next_config(vf,width,height,d_width,d_height,flags,IMGFMT_YV12);
}

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts){
    mp_image_t *dmpi;
    int y,w,h;

    // hope we'll get DR buffer:
    dmpi=vf_get_image(vf->next,IMGFMT_YV12,
        MP_IMGTYPE_TEMP, 0/*MP_IMGFLAG_ACCEPT_STRIDE*/,
        mpi->w, mpi->h);

    for(y=0;y<mpi->h;y++)
        fast_memcpy(dmpi->planes[0]+dmpi->stride[0]*y,
               mpi->planes[0]+mpi->stride[0]*y,
               mpi->w);

    w=mpi->w/4; h=mpi->h/2;
    for(y=0;y<h;y++){
        unsigned char* s=mpi->planes[1]+mpi->stride[1]*(y>>1);
        unsigned char* d=dmpi->planes[1]+dmpi->stride[1]*y;
        int x;
        for(x=0;x<w;x++) d[2*x]=d[2*x+1]=s[x];
    }
    for(y=0;y<h;y++){
        unsigned char* s=mpi->planes[2]+mpi->stride[2]*(y>>1);
        unsigned char* d=dmpi->planes[2]+dmpi->stride[2]*y;
        int x;
        for(x=0;x<w;x++) d[2*x]=d[2*x+1]=s[x];
    }

    vf_clone_mpi_attributes(dmpi, mpi);

    return vf_next_put_image(vf,dmpi, pts);
}

//===========================================================================//

static int query_format(struct vf_instance *vf, unsigned int fmt){
    if (fmt == IMGFMT_YVU9 || fmt == IMGFMT_IF09)
        return vf_next_query_format(vf,IMGFMT_YV12) & (~VFCAP_CSP_SUPPORTED_BY_HW);
    return 0;
}

static int vf_open(vf_instance_t *vf, char *args){
    vf->config=config;
    vf->put_image=put_image;
    vf->query_format=query_format;
    return 1;
}

const vf_info_t vf_info_yvu9 = {
    "fast YVU9->YV12 conversion",
    "yvu9",
    "alex",
    "",
    vf_open,
    NULL
};

//===========================================================================//
