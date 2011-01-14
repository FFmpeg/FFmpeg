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
#include "cpudetect.h"

#include "img_format.h"
#include "mp_image.h"
#include "vf.h"

#include "libvo/fastmemcpy.h"

struct vf_priv_s {
        int skipline;
        int scalew;
        int scaleh;
};

static void toright(unsigned char *dst[3], unsigned char *src[3],
                    int dststride[3], int srcstride[3],
                    int w, int h, struct vf_priv_s* p)
{
        int k;

        for (k = 0; k < 3; k++) {
                unsigned char* fromL = src[k];
                unsigned char* fromR = src[k];
                unsigned char* to = dst[k];
                int src = srcstride[k];
                int dst = dststride[k];
                int ss;
                unsigned int dd;
                int i;

                if (k > 0) {
                        i = h / 4 - p->skipline / 2;
                        ss = src * (h / 4 + p->skipline / 2);
                        dd = w / 4;
                } else {
                        i = h / 2 - p->skipline;
                        ss = src * (h / 2 + p->skipline);
                        dd = w / 2;
                }
                fromR += ss;
                for ( ; i > 0; i--) {
                        int j;
                        unsigned char* t = to;
                        unsigned char* sL = fromL;
                        unsigned char* sR = fromR;

                        if (p->scalew == 1) {
                                for (j = dd; j > 0; j--) {
                                        *t++ = (sL[0] + sL[1]) / 2;
                                        sL+=2;
                                }
                                for (j = dd ; j > 0; j--) {
                                        *t++ = (sR[0] + sR[1]) / 2;
                                        sR+=2;
                                }
                        } else {
                                for (j = dd * 2 ; j > 0; j--)
                                        *t++ = *sL++;
                                for (j = dd * 2 ; j > 0; j--)
                                        *t++ = *sR++;
                        }
                        if (p->scaleh == 1) {
                                fast_memcpy(to + dst, to, dst);
                                to += dst;
                        }
                        to += dst;
                        fromL += src;
                        fromR += src;
                }
                //printf("K %d  %d   %d   %d  %d \n", k, w, h,  src, dst);
        }
}

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts)
{
        mp_image_t *dmpi;

        // hope we'll get DR buffer:
        dmpi=vf_get_image(vf->next, IMGFMT_YV12,
                          MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE |
                          (vf->priv->scaleh == 1) ? MP_IMGFLAG_READABLE : 0,
                          mpi->w * vf->priv->scalew,
                          mpi->h / vf->priv->scaleh - vf->priv->skipline);

        toright(dmpi->planes, mpi->planes, dmpi->stride,
                mpi->stride, mpi->w, mpi->h, vf->priv);

        return vf_next_put_image(vf,dmpi, pts);
}

static int config(struct vf_instance *vf,
                  int width, int height, int d_width, int d_height,
                  unsigned int flags, unsigned int outfmt)
{
        /* FIXME - also support UYVY output? */
        return vf_next_config(vf, width * vf->priv->scalew,
                              height / vf->priv->scaleh - vf->priv->skipline, d_width, d_height, flags, IMGFMT_YV12);
}


static int query_format(struct vf_instance *vf, unsigned int fmt)
{
        /* FIXME - really any YUV 4:2:0 input format should work */
        switch (fmt) {
        case IMGFMT_YV12:
        case IMGFMT_IYUV:
        case IMGFMT_I420:
                return vf_next_query_format(vf, IMGFMT_YV12);
        }
        return 0;
}

static void uninit(struct vf_instance *vf)
{
        free(vf->priv);
}

static int vf_open(vf_instance_t *vf, char *args)
{
        vf->config=config;
        vf->query_format=query_format;
        vf->put_image=put_image;
        vf->uninit=uninit;

        vf->priv = calloc(1, sizeof (struct vf_priv_s));
        vf->priv->skipline = 0;
        vf->priv->scalew = 1;
        vf->priv->scaleh = 2;
        if (args) sscanf(args, "%d:%d:%d", &vf->priv->skipline, &vf->priv->scalew, &vf->priv->scaleh);

        return 1;
}

const vf_info_t vf_info_down3dright = {
        "convert stereo movie from top-bottom to left-right field",
        "down3dright",
        "Zdenek Kabelac",
        "",
        vf_open,
        NULL
};
