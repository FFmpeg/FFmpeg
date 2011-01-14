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
    int csp;
};

//===========================================================================//

static int config(struct vf_instance *vf,
        int width, int height, int d_width, int d_height,
        unsigned int flags, unsigned int outfmt){
    return vf_next_config(vf, width, height, d_width, d_height, flags, outfmt);
}

static inline int clamp_y(int x){
    return (x > 235) ? 235 : (x < 16) ? 16 : x;
}

static inline int clamp_c(int x){
    return (x > 240) ? 240 : (x < 16) ? 16 : x;
}

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts){
    int i,j;
    uint8_t *y_in, *cb_in, *cr_in;
    uint8_t *y_out, *cb_out, *cr_out;

    vf->dmpi=vf_get_image(vf->next,mpi->imgfmt,
        MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE,
        mpi->width, mpi->height);

    y_in = mpi->planes[0];
    cb_in = mpi->planes[1];
    cr_in = mpi->planes[2];

    y_out = vf->dmpi->planes[0];
    cb_out = vf->dmpi->planes[1];
    cr_out = vf->dmpi->planes[2];

    for (i = 0; i < mpi->height; i++)
        for (j = 0; j < mpi->width; j++)
            y_out[i*vf->dmpi->stride[0]+j] = clamp_y(y_in[i*mpi->stride[0]+j]);

    for (i = 0; i < mpi->chroma_height; i++)
        for (j = 0; j < mpi->chroma_width; j++)
        {
            cb_out[i*vf->dmpi->stride[1]+j] = clamp_c(cb_in[i*mpi->stride[1]+j]);
            cr_out[i*vf->dmpi->stride[2]+j] = clamp_c(cr_in[i*mpi->stride[2]+j]);
        }

    return vf_next_put_image(vf,vf->dmpi, pts);
}

//===========================================================================//

/*
static void uninit(struct vf_instance *vf){
        free(vf->priv);
}
*/

static int query_format(struct vf_instance *vf, unsigned int fmt){
    switch(fmt){
        case IMGFMT_YV12:
        case IMGFMT_I420:
        case IMGFMT_IYUV:
            return 1;
    }
    return 0;
}

static int vf_open(vf_instance_t *vf, char *args){
    vf->config=config;
    vf->put_image=put_image;
//    vf->uninit=uninit;
    vf->query_format=query_format;
//    vf->priv=calloc(1, sizeof(struct vf_priv_s));
//    if (args)
//        vf->priv->csp = atoi(args);
    return 1;
}

const vf_info_t vf_info_yuvcsp = {
    "yuv colorspace converter",
    "yuvcsp",
    "Alex Beregszaszi",
    "",
    vf_open,
    NULL
};

//===========================================================================//
