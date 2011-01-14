/*
 * Copyright (C) 2003 Michael Zucchi <notzed@ximian.com>
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

#include "config.h"
#include "mp_msg.h"

#include "img_format.h"
#include "mp_image.h"
#include "vf.h"

#include "libvo/fastmemcpy.h"

struct vf_priv_s {
    int mode;
    int frame;
    mp_image_t *dmpi;
};

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts)
{
    int ret = 0;
    mp_image_t *dmpi;

    switch (vf->priv->mode) {
    case 0:
        dmpi = vf->priv->dmpi;
        if (dmpi == NULL) {
            dmpi = vf_get_image(vf->next, mpi->imgfmt,
                        MP_IMGTYPE_STATIC, MP_IMGFLAG_ACCEPT_STRIDE |
                        MP_IMGFLAG_PRESERVE,
                        mpi->width, mpi->height*2);

            vf->priv->dmpi = dmpi;

            memcpy_pic(dmpi->planes[0], mpi->planes[0], mpi->w, mpi->h,
                   dmpi->stride[0]*2, mpi->stride[0]);
            if (mpi->flags & MP_IMGFLAG_PLANAR) {
                memcpy_pic(dmpi->planes[1], mpi->planes[1],
                       mpi->chroma_width, mpi->chroma_height,
                       dmpi->stride[1]*2, mpi->stride[1]);
                memcpy_pic(dmpi->planes[2], mpi->planes[2],
                       mpi->chroma_width, mpi->chroma_height,
                       dmpi->stride[2]*2, mpi->stride[2]);
            }
        } else {
            vf->priv->dmpi = NULL;

            memcpy_pic(dmpi->planes[0]+dmpi->stride[0], mpi->planes[0], mpi->w, mpi->h,
                   dmpi->stride[0]*2, mpi->stride[0]);
            if (mpi->flags & MP_IMGFLAG_PLANAR) {
                memcpy_pic(dmpi->planes[1]+dmpi->stride[1], mpi->planes[1],
                       mpi->chroma_width, mpi->chroma_height,
                       dmpi->stride[1]*2, mpi->stride[1]);
                memcpy_pic(dmpi->planes[2]+dmpi->stride[2], mpi->planes[2],
                       mpi->chroma_width, mpi->chroma_height,
                       dmpi->stride[2]*2, mpi->stride[2]);
            }
            ret = vf_next_put_image(vf, dmpi, MP_NOPTS_VALUE);
        }
        break;
    case 1:
        if (vf->priv->frame & 1)
            ret = vf_next_put_image(vf, mpi, MP_NOPTS_VALUE);
        break;
    case 2:
        if ((vf->priv->frame & 1) == 0)
            ret = vf_next_put_image(vf, mpi, MP_NOPTS_VALUE);
        break;
    case 3:
        dmpi = vf_get_image(vf->next, mpi->imgfmt,
                    MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE,
                    mpi->width, mpi->height*2);
        /* fixme, just clear alternate lines */
        vf_mpi_clear(dmpi, 0, 0, dmpi->w, dmpi->h);
        if ((vf->priv->frame & 1) == 0) {
            memcpy_pic(dmpi->planes[0], mpi->planes[0], mpi->w, mpi->h,
                   dmpi->stride[0]*2, mpi->stride[0]);
            if (mpi->flags & MP_IMGFLAG_PLANAR) {
                memcpy_pic(dmpi->planes[1], mpi->planes[1],
                       mpi->chroma_width, mpi->chroma_height,
                       dmpi->stride[1]*2, mpi->stride[1]);
                memcpy_pic(dmpi->planes[2], mpi->planes[2],
                       mpi->chroma_width, mpi->chroma_height,
                       dmpi->stride[2]*2, mpi->stride[2]);
            }
        } else {
            memcpy_pic(dmpi->planes[0]+dmpi->stride[0], mpi->planes[0], mpi->w, mpi->h,
                   dmpi->stride[0]*2, mpi->stride[0]);
            if (mpi->flags & MP_IMGFLAG_PLANAR) {
                memcpy_pic(dmpi->planes[1]+dmpi->stride[1], mpi->planes[1],
                       mpi->chroma_width, mpi->chroma_height,
                       dmpi->stride[1]*2, mpi->stride[1]);
                memcpy_pic(dmpi->planes[2]+dmpi->stride[2], mpi->planes[2],
                       mpi->chroma_width, mpi->chroma_height,
                       dmpi->stride[2]*2, mpi->stride[2]);
            }
        }
        ret = vf_next_put_image(vf, dmpi, MP_NOPTS_VALUE);
        break;
    case 4:
        // Interleave even lines (only) from Frame 'i' with odd
        // lines (only) from Frame 'i+1', halving the Frame
        // rate and preserving image height.

        dmpi = vf->priv->dmpi;

        // @@ Need help:  Should I set dmpi->fields to indicate
        // that the (new) frame will be interlaced!?  E.g. ...
        // dmpi->fields |= MP_IMGFIELD_INTERLACED;
        // dmpi->fields |= MP_IMGFIELD_TOP_FIRST;
        // etc.

        if (dmpi == NULL) {
            dmpi = vf_get_image(vf->next, mpi->imgfmt,
                        MP_IMGTYPE_STATIC, MP_IMGFLAG_ACCEPT_STRIDE |
                        MP_IMGFLAG_PRESERVE,
                        mpi->width, mpi->height);

            vf->priv->dmpi = dmpi;

            my_memcpy_pic(dmpi->planes[0], mpi->planes[0], mpi->w, mpi->h/2,
                      dmpi->stride[0]*2, mpi->stride[0]*2);
            if (mpi->flags & MP_IMGFLAG_PLANAR) {
                my_memcpy_pic(dmpi->planes[1], mpi->planes[1],
                          mpi->chroma_width, mpi->chroma_height/2,
                          dmpi->stride[1]*2, mpi->stride[1]*2);
                my_memcpy_pic(dmpi->planes[2], mpi->planes[2],
                          mpi->chroma_width, mpi->chroma_height/2,
                          dmpi->stride[2]*2, mpi->stride[2]*2);
            }
        } else {
            vf->priv->dmpi = NULL;

            my_memcpy_pic(dmpi->planes[0]+dmpi->stride[0],
                      mpi->planes[0]+mpi->stride[0],
                      mpi->w, mpi->h/2,
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
            ret = vf_next_put_image(vf, dmpi, MP_NOPTS_VALUE);
        }
        break;
    }

    vf->priv->frame++;

    return ret;
}

static int query_format(struct vf_instance *vf, unsigned int fmt)
{
    /* FIXME - figure out which other formats work */
    switch (fmt) {
    case IMGFMT_YV12:
    case IMGFMT_IYUV:
    case IMGFMT_I420:
        return vf_next_query_format(vf, fmt);
    }
    return 0;
}

static int config(struct vf_instance *vf,
    int width, int height, int d_width, int d_height,
    unsigned int flags, unsigned int outfmt)
{
    switch (vf->priv->mode) {
    case 0:
    case 3:
        return vf_next_config(vf,width,height*2,d_width,d_height*2,flags,outfmt);
    case 1:            /* odd frames */
    case 2:            /* even frames */
    case 4:            /* alternate frame (height-preserving) interlacing */
        return vf_next_config(vf,width,height,d_width,d_height,flags,outfmt);
    }
    return 0;
}

static void uninit(struct vf_instance *vf)
{
    free(vf->priv);
}

static int vf_open(vf_instance_t *vf, char *args)
{
    struct vf_priv_s *p;
    vf->config = config;
    vf->put_image = put_image;
    vf->query_format = query_format;
    vf->uninit = uninit;
    vf->default_reqs = VFCAP_ACCEPT_STRIDE;
    vf->priv = p = calloc(1, sizeof(struct vf_priv_s));
    vf->priv->mode = 0;
    if (args)
      sscanf(args, "%d", &vf->priv->mode);
    vf->priv->frame = 0;
    return 1;
}

const vf_info_t vf_info_tinterlace = {
    "temporal field interlacing",
    "tinterlace",
    "Michael Zucchi",
    "",
    vf_open,
    NULL
};
