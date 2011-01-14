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
    int w, h;
    int method; // aspect method, 0 -> downscale, 1-> upscale. +2 -> original aspect.
    int round;
    float aspect;
};

static int config(struct vf_instance *vf,
    int width, int height, int d_width, int d_height,
    unsigned int flags, unsigned int outfmt)
{
    if (vf->priv->aspect < 0.001) { // did the user input aspect or w,h params
        if (vf->priv->w == 0) vf->priv->w = d_width;
        if (vf->priv->h == 0) vf->priv->h = d_height;
        if (vf->priv->w == -1) vf->priv->w = width;
        if (vf->priv->h == -1) vf->priv->h = height;
        if (vf->priv->w == -2) vf->priv->w = vf->priv->h * (double)d_width / d_height;
        if (vf->priv->w == -3) vf->priv->w = vf->priv->h * (double)width / height;
        if (vf->priv->h == -2) vf->priv->h = vf->priv->w * (double)d_height / d_width;
        if (vf->priv->h == -3) vf->priv->h = vf->priv->w * (double)height / width;
        if (vf->priv->method > -1) {
            double aspect = (vf->priv->method & 2) ? ((double)height / width) : ((double)d_height / d_width);
            if ((vf->priv->h > vf->priv->w * aspect) ^ (vf->priv->method & 1)) {
                vf->priv->h = vf->priv->w * aspect;
            } else {
                vf->priv->w = vf->priv->h / aspect;
            }
        }
        if (vf->priv->round > 1) { // round up
            vf->priv->w += (vf->priv->round - 1 - (vf->priv->w - 1) % vf->priv->round);
            vf->priv->h += (vf->priv->round - 1 - (vf->priv->h - 1) % vf->priv->round);
        }
        d_width = vf->priv->w;
        d_height = vf->priv->h;
    } else {
        if (vf->priv->aspect * height > width) {
            d_width = height * vf->priv->aspect + .5;
            d_height = height;
        } else {
            d_height = width / vf->priv->aspect + .5;
            d_width = width;
        }
    }
    return vf_next_config(vf, width, height, d_width, d_height, flags, outfmt);
}

static void uninit(vf_instance_t *vf) {
    free(vf->priv);
    vf->priv = NULL;
}

static int vf_open(vf_instance_t *vf, char *args)
{
    vf->config = config;
    vf->draw_slice = vf_next_draw_slice;
    vf->uninit = uninit;
    //vf->default_caps = 0;
    vf->priv = calloc(sizeof(struct vf_priv_s), 1);
    vf->priv->aspect = 0.;
    vf->priv->w = -1;
    vf->priv->h = -1;
    vf->priv->method = -1;
    vf->priv->round = 1;
    if (args) {
        if (strchr(args, '/')) {
            int w, h;
            sscanf(args, "%d/%d", &w, &h);
            vf->priv->aspect = (float)w/h;
        } else if (strchr(args, '.')) {
            sscanf(args, "%f", &vf->priv->aspect);
        } else {
            sscanf(args, "%d:%d:%d:%d", &vf->priv->w, &vf->priv->h, &vf->priv->method, &vf->priv->round);
        }
    }
    if ((vf->priv->aspect < 0.) || (vf->priv->w < -3) || (vf->priv->h < -3) ||
            ((vf->priv->w < -1) && (vf->priv->h < -1)) ||
            (vf->priv->method < -1) || (vf->priv->method > 3) ||
            (vf->priv->round < 0)) {
        mp_msg(MSGT_VFILTER, MSGL_ERR, "[dsize] Illegal value(s): aspect: %f w: %d h: %d aspect_method: %d round: %d\n", vf->priv->aspect, vf->priv->w, vf->priv->h, vf->priv->method, vf->priv->round);
        free(vf->priv); vf->priv = NULL;
        return -1;
    }
    return 1;
}

const vf_info_t vf_info_dsize = {
    "reset displaysize/aspect",
    "dsize",
    "Rich Felker",
    "",
    vf_open,
    NULL
};
