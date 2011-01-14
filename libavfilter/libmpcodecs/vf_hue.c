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
#include <math.h>

#include "config.h"
#include "mp_msg.h"
#include "cpudetect.h"

#include "img_format.h"
#include "mp_image.h"
#include "vf.h"

#include "libvo/video_out.h"

struct vf_priv_s {
        uint8_t *buf[2];
        float hue;
        float saturation;
};

static void process_C(uint8_t *udst, uint8_t *vdst, uint8_t *usrc, uint8_t *vsrc, int dststride, int srcstride,
                    int w, int h, float hue, float sat)
{
        int i;
        const int s= rint(sin(hue) * (1<<16) * sat);
        const int c= rint(cos(hue) * (1<<16) * sat);

        while (h--) {
                for (i = 0; i<w; i++)
                {
                        const int u= usrc[i] - 128;
                        const int v= vsrc[i] - 128;
                        int new_u= (c*u - s*v + (1<<15) + (128<<16))>>16;
                        int new_v= (s*u + c*v + (1<<15) + (128<<16))>>16;
                        if(new_u & 768) new_u= (-new_u)>>31;
                        if(new_v & 768) new_v= (-new_v)>>31;
                        udst[i]= new_u;
                        vdst[i]= new_v;
                }
                usrc += srcstride;
                vsrc += srcstride;
                udst += dststride;
                vdst += dststride;
        }
}

static void (*process)(uint8_t *udst, uint8_t *vdst, uint8_t *usrc, uint8_t *vsrc, int dststride, int srcstride,
                    int w, int h, float hue, float sat);

/* FIXME: add packed yuv version of process */

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts)
{
        mp_image_t *dmpi;

        dmpi=vf_get_image(vf->next, mpi->imgfmt,
                          MP_IMGTYPE_EXPORT, 0,
                          mpi->w, mpi->h);

        dmpi->planes[0] = mpi->planes[0];
        dmpi->stride[0] = mpi->stride[0];
        dmpi->stride[1] = mpi->stride[1];
        dmpi->stride[2] = mpi->stride[2];

        if (!vf->priv->buf[0]){
                vf->priv->buf[0] = malloc(mpi->stride[1]*mpi->h >> mpi->chroma_y_shift);
                vf->priv->buf[1] = malloc(mpi->stride[2]*mpi->h >> mpi->chroma_y_shift);
        }

        if (vf->priv->hue == 0 && vf->priv->saturation == 1){
                dmpi->planes[1] = mpi->planes[1];
                dmpi->planes[2] = mpi->planes[2];
        }else {
                dmpi->planes[1] = vf->priv->buf[0];
                dmpi->planes[2] = vf->priv->buf[1];
                process(dmpi->planes[1], dmpi->planes[2],
                        mpi->planes[1], mpi->planes[2],
                        dmpi->stride[1],mpi->stride[1],
                        mpi->w>> mpi->chroma_x_shift, mpi->h>> mpi->chroma_y_shift,
                        vf->priv->hue, vf->priv->saturation);
        }

        return vf_next_put_image(vf,dmpi, pts);
}

static int control(struct vf_instance *vf, int request, void* data)
{
        vf_equalizer_t *eq;

        switch (request) {
        case VFCTRL_SET_EQUALIZER:
                eq = data;
                if (!strcmp(eq->item,"hue")) {
                        vf->priv->hue = eq->value * M_PI / 100;
                        return CONTROL_TRUE;
                } else if (!strcmp(eq->item,"saturation")) {
                        vf->priv->saturation = (eq->value + 100)/100.0;
                        return CONTROL_TRUE;
                }
                break;
        case VFCTRL_GET_EQUALIZER:
                eq = data;
                if (!strcmp(eq->item,"hue")) {
                        eq->value = rint(vf->priv->hue *100 / M_PI);
                        return CONTROL_TRUE;
                }else if (!strcmp(eq->item,"saturation")) {
                        eq->value = rint(vf->priv->saturation*100 - 100);
                        return CONTROL_TRUE;
                }
                break;
        }
        return vf_next_control(vf, request, data);
}

static int query_format(struct vf_instance *vf, unsigned int fmt)
{
        switch (fmt) {
        case IMGFMT_YVU9:
        case IMGFMT_IF09:
        case IMGFMT_YV12:
        case IMGFMT_I420:
        case IMGFMT_IYUV:
        case IMGFMT_CLPL:
        case IMGFMT_444P:
        case IMGFMT_422P:
        case IMGFMT_411P:
                return vf_next_query_format(vf, fmt);
        }
        return 0;
}

static void uninit(struct vf_instance *vf)
{
        free(vf->priv->buf[0]);
        free(vf->priv->buf[1]);
        free(vf->priv);
}

static int vf_open(vf_instance_t *vf, char *args)
{
        vf->control=control;
        vf->query_format=query_format;
        vf->put_image=put_image;
        vf->uninit=uninit;

    vf->priv = malloc(sizeof(struct vf_priv_s));
    memset(vf->priv, 0, sizeof(struct vf_priv_s));
    sscanf(args, "%f:%f", &vf->priv->hue, &vf->priv->saturation);
        vf->priv->hue *= M_PI / 180.0;

        process = process_C;
        return 1;
}

const vf_info_t vf_info_hue = {
        "hue changer",
        "hue",
        "Michael Niedermayer",
        "",
        vf_open,
};
