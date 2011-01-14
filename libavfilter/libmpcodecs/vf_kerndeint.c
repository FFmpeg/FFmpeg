/*
 * Original AVISynth Filter Copyright (C) 2003 Donald A. Graft
 *  Adapted to MPlayer by Tobias Diedrich
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
#include <inttypes.h>
#include <math.h>

#include "mp_msg.h"
#include "img_format.h"
#include "mp_image.h"
#include "vf.h"
#include "libvo/fastmemcpy.h"

//===========================================================================//

struct vf_priv_s {
    int    frame;
    int    map;
    int    order;
    int    thresh;
    int    sharp;
    int    twoway;
    int    do_deinterlace;
};


/***************************************************************************/


static int config(struct vf_instance *vf,
    int width, int height, int d_width, int d_height,
    unsigned int flags, unsigned int outfmt){

    return vf_next_config(vf,width,height,d_width,d_height,flags,outfmt);
}


static void uninit(struct vf_instance *vf)
{
    free(vf->priv);
}

static inline int IsRGB(mp_image_t *mpi)
{
    return mpi->imgfmt == IMGFMT_RGB;
}

static inline int IsYUY2(mp_image_t *mpi)
{
    return mpi->imgfmt == IMGFMT_YUY2;
}

#define PLANAR_Y 0
#define PLANAR_U 1
#define PLANAR_V 2

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts){
    int cw= mpi->w >> mpi->chroma_x_shift;
    int ch= mpi->h >> mpi->chroma_y_shift;
    int W = mpi->w, H = mpi->h;
    const unsigned char *prvp, *prvpp, *prvpn, *prvpnn, *prvppp, *prvp4p, *prvp4n;
    const unsigned char *srcp_saved;
    const unsigned char *srcp, *srcpp, *srcpn, *srcpnn, *srcppp, *srcp3p, *srcp3n, *srcp4p, *srcp4n;
    unsigned char *dstp, *dstp_saved;
    int src_pitch;
    int psrc_pitch;
    int dst_pitch;
    int x, y, z;
    int n = vf->priv->frame++;
    int val, hi, lo, w, h;
    double valf;
    int plane;
    int threshold = vf->priv->thresh;
    int order = vf->priv->order;
    int map = vf->priv->map;
    int sharp = vf->priv->sharp;
    int twoway = vf->priv->twoway;
    mp_image_t *dmpi, *pmpi;

    if(!vf->priv->do_deinterlace)
        return vf_next_put_image(vf, mpi, pts);

    dmpi=vf_get_image(vf->next,mpi->imgfmt,
        MP_IMGTYPE_IP, MP_IMGFLAG_ACCEPT_STRIDE,
        mpi->w,mpi->h);
    pmpi=vf_get_image(vf->next,mpi->imgfmt,
        MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE,
        mpi->w,mpi->h);
    if(!dmpi) return 0;

    for (z=0; z<mpi->num_planes; z++) {
        if (z == 0) plane = PLANAR_Y;
        else if (z == 1) plane = PLANAR_U;
        else plane = PLANAR_V;

        h = plane == PLANAR_Y ? H : ch;
        w = plane == PLANAR_Y ? W : cw;

        srcp = srcp_saved = mpi->planes[z];
        src_pitch = mpi->stride[z];
        psrc_pitch = pmpi->stride[z];
        dstp = dstp_saved = dmpi->planes[z];
        dst_pitch = dmpi->stride[z];
        srcp = srcp_saved + (1-order) * src_pitch;
        dstp = dstp_saved + (1-order) * dst_pitch;

        for (y=0; y<h; y+=2) {
            fast_memcpy(dstp, srcp, w);
            srcp += 2*src_pitch;
            dstp += 2*dst_pitch;
        }

        // Copy through the lines that will be missed below.
        fast_memcpy(dstp_saved + order*dst_pitch, srcp_saved + (1-order)*src_pitch, w);
        fast_memcpy(dstp_saved + (2+order)*dst_pitch, srcp_saved + (3-order)*src_pitch, w);
        fast_memcpy(dstp_saved + (h-2+order)*dst_pitch, srcp_saved + (h-1-order)*src_pitch, w);
        fast_memcpy(dstp_saved + (h-4+order)*dst_pitch, srcp_saved + (h-3-order)*src_pitch, w);
        /* For the other field choose adaptively between using the previous field
           or the interpolant from the current field. */

        prvp = pmpi->planes[z] + 5*psrc_pitch - (1-order)*psrc_pitch;
        prvpp = prvp - psrc_pitch;
        prvppp = prvp - 2*psrc_pitch;
        prvp4p = prvp - 4*psrc_pitch;
        prvpn = prvp + psrc_pitch;
        prvpnn = prvp + 2*psrc_pitch;
        prvp4n = prvp + 4*psrc_pitch;
        srcp = srcp_saved + 5*src_pitch - (1-order)*src_pitch;
        srcpp = srcp - src_pitch;
        srcppp = srcp - 2*src_pitch;
        srcp3p = srcp - 3*src_pitch;
        srcp4p = srcp - 4*src_pitch;
        srcpn = srcp + src_pitch;
        srcpnn = srcp + 2*src_pitch;
        srcp3n = srcp + 3*src_pitch;
        srcp4n = srcp + 4*src_pitch;
        dstp =  dstp_saved  + 5*dst_pitch - (1-order)*dst_pitch;
        for (y = 5 - (1-order); y <= h - 5 - (1-order); y+=2)
        {
            for (x = 0; x < w; x++)
            {
                if ((threshold == 0) || (n == 0) ||
                    (abs((int)prvp[x] - (int)srcp[x]) > threshold) ||
                    (abs((int)prvpp[x] - (int)srcpp[x]) > threshold) ||
                    (abs((int)prvpn[x] - (int)srcpn[x]) > threshold))
                {
                    if (map == 1)
                    {
                        int g = x & ~3;
                        if (IsRGB(mpi) == 1)
                        {
                            dstp[g++] = 255;
                            dstp[g++] = 255;
                            dstp[g++] = 255;
                            dstp[g] = 255;
                            x = g;
                        }
                        else if (IsYUY2(mpi) == 1)
                        {
                            dstp[g++] = 235;
                            dstp[g++] = 128;
                            dstp[g++] = 235;
                            dstp[g] = 128;
                            x = g;
                        }
                        else
                        {
                            if (plane == PLANAR_Y) dstp[x] = 235;
                            else dstp[x] = 128;
                        }
                    }
                    else
                    {
                        if (IsRGB(mpi))
                        {
                            hi = 255;
                            lo = 0;
                        }
                        else if (IsYUY2(mpi))
                        {
                            hi = (x & 1) ? 240 : 235;
                            lo = 16;
                        }
                        else
                        {
                            hi = (plane == PLANAR_Y) ? 235 : 240;
                            lo = 16;
                        }

                        if (sharp == 1)
                        {
                            if (twoway == 1)
                                valf = + 0.526*((int)srcpp[x] + (int)srcpn[x])
                                   + 0.170*((int)srcp[x] + (int)prvp[x])
                                   - 0.116*((int)srcppp[x] + (int)srcpnn[x] + (int)prvppp[x] + (int)prvpnn[x])
                                   - 0.026*((int)srcp3p[x] + (int)srcp3n[x])
                                   + 0.031*((int)srcp4p[x] + (int)srcp4n[x] + (int)prvp4p[x] + (int)prvp4n[x]);
                            else
                                valf = + 0.526*((int)srcpp[x] + (int)srcpn[x])
                                   + 0.170*((int)prvp[x])
                                   - 0.116*((int)prvppp[x] + (int)prvpnn[x])
                                   - 0.026*((int)srcp3p[x] + (int)srcp3n[x])
                                   + 0.031*((int)prvp4p[x] + (int)prvp4p[x]);
                            if (valf > hi) valf = hi;
                            else if (valf < lo) valf = lo;
                            dstp[x] = (int) valf;
                        }
                        else
                        {
                            if (twoway == 1)
                                val = (8*((int)srcpp[x] + (int)srcpn[x]) + 2*((int)srcp[x] + (int)prvp[x]) -
                                    (int)(srcppp[x]) - (int)(srcpnn[x]) -
                                    (int)(prvppp[x]) - (int)(prvpnn[x])) >> 4;
                            else
                                val = (8*((int)srcpp[x] + (int)srcpn[x]) + 2*((int)prvp[x]) -
                                    (int)(prvppp[x]) - (int)(prvpnn[x])) >> 4;
                            if (val > hi) val = hi;
                            else if (val < lo) val = lo;
                            dstp[x] = (int) val;
                        }
                    }
                }
                else
                {
                    dstp[x] = srcp[x];
                }
            }
            prvp  += 2*psrc_pitch;
            prvpp  += 2*psrc_pitch;
            prvppp  += 2*psrc_pitch;
            prvpn  += 2*psrc_pitch;
            prvpnn  += 2*psrc_pitch;
            prvp4p  += 2*psrc_pitch;
            prvp4n  += 2*psrc_pitch;
            srcp  += 2*src_pitch;
            srcpp += 2*src_pitch;
            srcppp += 2*src_pitch;
            srcp3p += 2*src_pitch;
            srcp4p += 2*src_pitch;
            srcpn += 2*src_pitch;
            srcpnn += 2*src_pitch;
            srcp3n += 2*src_pitch;
            srcp4n += 2*src_pitch;
            dstp  += 2*dst_pitch;
        }

        srcp = mpi->planes[z];
        dstp = pmpi->planes[z];
        for (y=0; y<h; y++) {
            fast_memcpy(dstp, srcp, w);
            srcp += src_pitch;
            dstp += psrc_pitch;
        }
    }

    return vf_next_put_image(vf,dmpi, pts);
}

//===========================================================================//

static int query_format(struct vf_instance *vf, unsigned int fmt){
        switch(fmt)
    {
    case IMGFMT_YV12:
    case IMGFMT_RGB:
    case IMGFMT_YUY2:
        return vf_next_query_format(vf, fmt);
    }
    return 0;
}

static int control(struct vf_instance *vf, int request, void* data){
    switch (request)
    {
    case VFCTRL_GET_DEINTERLACE:
        *(int*)data = vf->priv->do_deinterlace;
        return CONTROL_OK;
    case VFCTRL_SET_DEINTERLACE:
        vf->priv->do_deinterlace = *(int*)data;
        return CONTROL_OK;
    }
    return vf_next_control (vf, request, data);
}

static int vf_open(vf_instance_t *vf, char *args){

    vf->control=control;
    vf->config=config;
    vf->put_image=put_image;
        vf->query_format=query_format;
        vf->uninit=uninit;
    vf->priv=malloc(sizeof(struct vf_priv_s));
        memset(vf->priv, 0, sizeof(struct vf_priv_s));

    vf->priv->frame = 0;

    vf->priv->map = 0;
    vf->priv->order = 0;
    vf->priv->thresh = 10;
    vf->priv->sharp = 0;
    vf->priv->twoway = 0;
    vf->priv->do_deinterlace=1;

        if (args)
        {
            sscanf(args, "%d:%d:%d:%d:%d",
        &vf->priv->thresh, &vf->priv->map,
        &vf->priv->order, &vf->priv->sharp,
        &vf->priv->twoway);
        }
    if (vf->priv->order > 1) vf->priv->order = 1;

    return 1;
}

const vf_info_t vf_info_kerndeint = {
    "Kernel Deinterlacer",
    "kerndeint",
    "Donald Graft",
    "",
    vf_open,
    NULL
};

//===========================================================================//
