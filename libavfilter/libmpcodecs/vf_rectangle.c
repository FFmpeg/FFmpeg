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
#include "mp_image.h"
#include "mp_msg.h"
#include "vf.h"

#include "libvo/fastmemcpy.h"
#include "libavutil/common.h"

struct vf_priv_s {
    int x, y, w, h;
};

static int
config(struct vf_instance *vf,
       int width, int height, int d_width, int d_height,
       unsigned int flags, unsigned int outfmt)
{
    if (vf->priv->w < 0 || width < vf->priv->w)
        vf->priv->w = width;
    if (vf->priv->h < 0 || height < vf->priv->h)
        vf->priv->h = height;
    if (vf->priv->x < 0)
        vf->priv->x = (width - vf->priv->w) / 2;
    if (vf->priv->y < 0)
        vf->priv->y = (height - vf->priv->h) / 2;
    if (vf->priv->w + vf->priv->x > width
        || vf->priv->h + vf->priv->y > height) {
        mp_msg(MSGT_VFILTER,MSGL_WARN,"rectangle: bad position/width/height - rectangle area is out of the original!\n");
        return 0;
    }
    return vf_next_config(vf, width, height, d_width, d_height, flags, outfmt);
}

static int
control(struct vf_instance *vf, int request, void *data)
{
    const int *const tmp = data;
    switch(request){
    case VFCTRL_CHANGE_RECTANGLE:
        switch (tmp[0]){
        case 0:
            vf->priv->w += tmp[1];
            return 1;
            break;
        case 1:
            vf->priv->h += tmp[1];
            return 1;
            break;
        case 2:
            vf->priv->x += tmp[1];
            return 1;
            break;
        case 3:
            vf->priv->y += tmp[1];
            return 1;
            break;
        default:
            mp_msg(MSGT_VFILTER,MSGL_FATAL,"Unknown param %d \n", tmp[0]);
            return 0;
        }
    }
    return vf_next_control(vf, request, data);
    return 0;
}
static int
put_image(struct vf_instance *vf, mp_image_t* mpi, double pts){
    mp_image_t* dmpi;
    unsigned int bpp = mpi->bpp / 8;
    int x, y, w, h;
    dmpi = vf_get_image(vf->next, mpi->imgfmt, MP_IMGTYPE_TEMP,
                        MP_IMGFLAG_ACCEPT_STRIDE | MP_IMGFLAG_PREFER_ALIGNED_STRIDE,
                        mpi->w, mpi->h);

    memcpy_pic(dmpi->planes[0],mpi->planes[0],mpi->w*bpp, mpi->h,
               dmpi->stride[0],mpi->stride[0]);
    if(mpi->flags&MP_IMGFLAG_PLANAR && mpi->flags&MP_IMGFLAG_YUV){
        memcpy_pic(dmpi->planes[1],mpi->planes[1],
                   mpi->w>>mpi->chroma_x_shift, mpi->h>>mpi->chroma_y_shift,
                   dmpi->stride[1],mpi->stride[1]);
        memcpy_pic(dmpi->planes[2],mpi->planes[2],
                   mpi->w>>mpi->chroma_x_shift, mpi->h>>mpi->chroma_y_shift,
                   dmpi->stride[2],mpi->stride[2]);
    }

    /* Draw the rectangle */

    mp_msg(MSGT_VFILTER,MSGL_INFO, "rectangle: -vf rectangle=%d:%d:%d:%d \n", vf->priv->w, vf->priv->h, vf->priv->x, vf->priv->y);

    x = FFMIN(vf->priv->x, dmpi->width);
    x = FFMAX(x, 0);

    w = vf->priv->x + vf->priv->w - 1 - x;
    w = FFMIN(w, dmpi->width - x);
    w = FFMAX(w, 0);

    y = FFMIN(vf->priv->y, dmpi->height);
    y = FFMAX(y, 0);

    h = vf->priv->y + vf->priv->h - 1 - y;
    h = FFMIN(h, dmpi->height - y);
    h = FFMAX(h, 0);

    if (0 <= vf->priv->y && vf->priv->y <= dmpi->height) {
        unsigned char *p = dmpi->planes[0] + y * dmpi->stride[0] + x * bpp;
        unsigned int count = w * bpp;
        while (count--)
            p[count] = 0xff - p[count];
    }
    if (h != 1 && vf->priv->y + vf->priv->h - 1 <= mpi->height) {
        unsigned char *p = dmpi->planes[0] + (vf->priv->y + vf->priv->h - 1) * dmpi->stride[0] + x * bpp;
        unsigned int count = w * bpp;
        while (count--)
            p[count] = 0xff - p[count];
    }
    if (0 <= vf->priv->x  && vf->priv->x <= dmpi->width) {
        unsigned char *p = dmpi->planes[0] + y * dmpi->stride[0] + x * bpp;
        unsigned int count = h;
        while (count--) {
            unsigned int i = bpp;
            while (i--)
                p[i] = 0xff - p[i];
            p += dmpi->stride[0];
        }
    }
    if (w != 1 && vf->priv->x + vf->priv->w - 1 <= mpi->width) {
        unsigned char *p = dmpi->planes[0] + y * dmpi->stride[0] + (vf->priv->x + vf->priv->w - 1) * bpp;
        unsigned int count = h;
        while (count--) {
            unsigned int i = bpp;
            while (i--)
                p[i] = 0xff - p[i];
            p += dmpi->stride[0];
        }
    }
    return vf_next_put_image(vf, dmpi, pts);
}

static int
vf_open(vf_instance_t *vf, char *args) {
    vf->config = config;
    vf->control = control;
    vf->put_image = put_image;
    vf->priv = malloc(sizeof(struct vf_priv_s));
    vf->priv->x = -1;
    vf->priv->y = -1;
    vf->priv->w = -1;
    vf->priv->h = -1;
    if (args)
        sscanf(args, "%d:%d:%d:%d",
               &vf->priv->w, &vf->priv->h, &vf->priv->x, &vf->priv->y);
    return 1;
}

const vf_info_t vf_info_rectangle = {
    "draw rectangle",
    "rectangle",
    "Kim Minh Kaplan",
    "",
    vf_open,
    NULL
};
