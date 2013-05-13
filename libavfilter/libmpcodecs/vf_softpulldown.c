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

#include "img_format.h"
#include "mp_image.h"
#include "vf.h"

#include "libvo/fastmemcpy.h"

struct vf_priv_s {
    int state;
    long long in;
    long long out;
};

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts)
{
    mp_image_t *dmpi;
    int ret = 0;
    int flags = mpi->fields;
    int state = vf->priv->state;

    dmpi = ff_vf_get_image(vf->next, mpi->imgfmt,
                        MP_IMGTYPE_STATIC, MP_IMGFLAG_ACCEPT_STRIDE |
                        MP_IMGFLAG_PRESERVE, mpi->width, mpi->height);

    vf->priv->in++;

    if ((state == 0 &&
         !(flags & MP_IMGFIELD_TOP_FIRST)) ||
        (state == 1 &&
         flags & MP_IMGFIELD_TOP_FIRST)) {
        ff_mp_msg(MSGT_VFILTER, MSGL_WARN,
               "softpulldown: Unexpected field flags: state=%d top_field_first=%d repeat_first_field=%d\n",
               state,
               (flags & MP_IMGFIELD_TOP_FIRST) != 0,
               (flags & MP_IMGFIELD_REPEAT_FIRST) != 0);
        state ^= 1;
    }

    if (state == 0) {
        ret = ff_vf_next_put_image(vf, mpi, MP_NOPTS_VALUE);
        vf->priv->out++;
        if (flags & MP_IMGFIELD_REPEAT_FIRST) {
            my_memcpy_pic(dmpi->planes[0],
                       mpi->planes[0], mpi->w, mpi->h/2,
                       dmpi->stride[0]*2, mpi->stride[0]*2);
            if (mpi->flags & MP_IMGFLAG_PLANAR) {
                my_memcpy_pic(dmpi->planes[1],
                              mpi->planes[1],
                              mpi->chroma_width,
                              mpi->chroma_height/2,
                              dmpi->stride[1]*2,
                              mpi->stride[1]*2);
                my_memcpy_pic(dmpi->planes[2],
                              mpi->planes[2],
                              mpi->chroma_width,
                              mpi->chroma_height/2,
                              dmpi->stride[2]*2,
                              mpi->stride[2]*2);
            }
            state=1;
        }
    } else {
        my_memcpy_pic(dmpi->planes[0]+dmpi->stride[0],
                      mpi->planes[0]+mpi->stride[0], mpi->w, mpi->h/2,
                      dmpi->stride[0]*2, mpi->stride[0]*2);
        if (mpi->flags & MP_IMGFLAG_PLANAR) {
            my_memcpy_pic(dmpi->planes[1]+dmpi->stride[1],
                          mpi->planes[1]+mpi->stride[1],
                          mpi->chroma_width, mpi->chroma_height/2,
                          dmpi->stride[1]*2, mpi->stride[1]*2);
            my_memcpy_pic(dmpi->planes[2]+dmpi->stride[2],
                          mpi->planes[2]+mpi->stride[2],
                          mpi->chroma_width, mpi->chroma_height/2,
                          dmpi->stride[2]*2, mpi->stride[2]*2);
        }
        ret = ff_vf_next_put_image(vf, dmpi, MP_NOPTS_VALUE);
        vf->priv->out++;
        if (flags & MP_IMGFIELD_REPEAT_FIRST) {
            ret |= ff_vf_next_put_image(vf, mpi, MP_NOPTS_VALUE);
            vf->priv->out++;
            state=0;
        } else {
            my_memcpy_pic(dmpi->planes[0],
                          mpi->planes[0], mpi->w, mpi->h/2,
                          dmpi->stride[0]*2, mpi->stride[0]*2);
            if (mpi->flags & MP_IMGFLAG_PLANAR) {
                my_memcpy_pic(dmpi->planes[1],
                              mpi->planes[1],
                              mpi->chroma_width,
                              mpi->chroma_height/2,
                              dmpi->stride[1]*2,
                              mpi->stride[1]*2);
                my_memcpy_pic(dmpi->planes[2],
                              mpi->planes[2],
                              mpi->chroma_width,
                              mpi->chroma_height/2,
                              dmpi->stride[2]*2,
                              mpi->stride[2]*2);
            }
        }
    }

    vf->priv->state = state;

    return ret;
}

static int config(struct vf_instance *vf,
    int width, int height, int d_width, int d_height,
    unsigned int flags, unsigned int outfmt)
{
    return ff_vf_next_config(vf,width,height,d_width,d_height,flags,outfmt);
}

static void uninit(struct vf_instance *vf)
{
    ff_mp_msg(MSGT_VFILTER, MSGL_INFO, "softpulldown: %lld frames in, %lld frames out\n", vf->priv->in, vf->priv->out);
    free(vf->priv);
}

static int vf_open(vf_instance_t *vf, char *args)
{
    vf->config = config;
    vf->put_image = put_image;
    vf->uninit = uninit;
    vf->default_reqs = VFCAP_ACCEPT_STRIDE;
    vf->priv = calloc(1, sizeof(struct vf_priv_s));
    vf->priv->state = 0;
    return 1;
}

const vf_info_t ff_vf_info_softpulldown = {
    "mpeg2 soft 3:2 pulldown",
    "softpulldown",
    "Tobias Diedrich <ranma+mplayer@tdiedrich.de>",
    "",
    vf_open,
    NULL
};
