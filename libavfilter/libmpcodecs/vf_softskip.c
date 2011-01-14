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

struct vf_priv_s {
    int skipflag;
};

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts)
{
    mp_image_t *dmpi;

    if (vf->priv->skipflag)
        return vf->priv->skipflag = 0;

    dmpi = vf_get_image(vf->next, mpi->imgfmt,
        MP_IMGTYPE_EXPORT, 0, mpi->width, mpi->height);
    vf_clone_mpi_attributes(dmpi, mpi);

    dmpi->planes[0] = mpi->planes[0];
    dmpi->stride[0] = mpi->stride[0];
    if (dmpi->flags&MP_IMGFLAG_PLANAR) {
        dmpi->planes[1] = mpi->planes[1];
        dmpi->stride[1] = mpi->stride[1];
        dmpi->planes[2] = mpi->planes[2];
        dmpi->stride[2] = mpi->stride[2];
    }

    return vf_next_put_image(vf, dmpi, pts);
}

static int control(struct vf_instance *vf, int request, void* data)
{
    switch (request) {
    case VFCTRL_SKIP_NEXT_FRAME:
        vf->priv->skipflag = 1;
        return CONTROL_TRUE;
    }
    return vf_next_control(vf, request, data);
}

#if 0
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
#endif

static void uninit(struct vf_instance *vf)
{
    free(vf->priv);
}

static int vf_open(vf_instance_t *vf, char *args)
{
    vf->put_image = put_image;
    vf->control = control;
    vf->uninit = uninit;
    vf->priv = calloc(1, sizeof(struct vf_priv_s));
    return 1;
}

const vf_info_t vf_info_softskip = {
    "soft (post-filter) frame skipping for encoding",
    "softskip",
    "Rich Felker",
    "",
    vf_open,
    NULL
};
