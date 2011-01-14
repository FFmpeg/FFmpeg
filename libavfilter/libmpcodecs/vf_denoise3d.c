/*
 * Copyright (C) 2003 Daniel Moreno <comac@comac.darktech.org>
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

#define PARAM1_DEFAULT 4.0
#define PARAM2_DEFAULT 3.0
#define PARAM3_DEFAULT 6.0

//===========================================================================//

struct vf_priv_s {
        int Coefs[4][512];
        unsigned char *Line;
        mp_image_t *pmpi;
};


/***************************************************************************/


static int config(struct vf_instance *vf,
        int width, int height, int d_width, int d_height,
        unsigned int flags, unsigned int outfmt){

        free(vf->priv->Line);
        vf->priv->Line = malloc(width);
        vf->priv->pmpi=NULL;
//        vf->default_caps &= !VFCAP_ACCEPT_STRIDE;

        return vf_next_config(vf,width,height,d_width,d_height,flags,outfmt);
}


static void uninit(struct vf_instance *vf)
{
    free(vf->priv->Line);
}

#define LowPass(Prev, Curr, Coef) (Curr + Coef[Prev - Curr])

static void deNoise(unsigned char *Frame,        // mpi->planes[x]
                    unsigned char *FramePrev,    // pmpi->planes[x]
                    unsigned char *FrameDest,    // dmpi->planes[x]
                    unsigned char *LineAnt,      // vf->priv->Line (width bytes)
                    int W, int H, int sStride, int pStride, int dStride,
                    int *Horizontal, int *Vertical, int *Temporal)
{
    int X, Y;
    int sLineOffs = 0, pLineOffs = 0, dLineOffs = 0;
    unsigned char PixelAnt;

    /* First pixel has no left nor top neighbor. Only previous frame */
    LineAnt[0] = PixelAnt = Frame[0];
    FrameDest[0] = LowPass(FramePrev[0], LineAnt[0], Temporal);

    /* Fist line has no top neighbor. Only left one for each pixel and
     * last frame */
    for (X = 1; X < W; X++)
    {
        PixelAnt = LowPass(PixelAnt, Frame[X], Horizontal);
        LineAnt[X] = PixelAnt;
        FrameDest[X] = LowPass(FramePrev[X], LineAnt[X], Temporal);
    }

    for (Y = 1; Y < H; Y++)
    {
        sLineOffs += sStride, pLineOffs += pStride, dLineOffs += dStride;
        /* First pixel on each line doesn't have previous pixel */
        PixelAnt = Frame[sLineOffs];
        LineAnt[0] = LowPass(LineAnt[0], PixelAnt, Vertical);
        FrameDest[dLineOffs] = LowPass(FramePrev[pLineOffs], LineAnt[0], Temporal);

        for (X = 1; X < W; X++)
        {
            /* The rest are normal */
            PixelAnt = LowPass(PixelAnt, Frame[sLineOffs+X], Horizontal);
            LineAnt[X] = LowPass(LineAnt[X], PixelAnt, Vertical);
            FrameDest[dLineOffs+X] = LowPass(FramePrev[pLineOffs+X], LineAnt[X], Temporal);
        }
    }
}



static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts){
        int cw= mpi->w >> mpi->chroma_x_shift;
        int ch= mpi->h >> mpi->chroma_y_shift;
        int W = mpi->w, H = mpi->h;

        mp_image_t *dmpi=vf_get_image(vf->next,mpi->imgfmt,
                MP_IMGTYPE_IP, MP_IMGFLAG_ACCEPT_STRIDE |
                MP_IMGFLAG_PRESERVE | MP_IMGFLAG_READABLE,
                mpi->w,mpi->h);

        if(!dmpi) return 0;
        if (!vf->priv->pmpi) vf->priv->pmpi=mpi;

        deNoise(mpi->planes[0], vf->priv->pmpi->planes[0], dmpi->planes[0],
                vf->priv->Line, W, H,
                mpi->stride[0], vf->priv->pmpi->stride[0], dmpi->stride[0],
                vf->priv->Coefs[0] + 256,
                vf->priv->Coefs[0] + 256,
                vf->priv->Coefs[1] + 256);
        deNoise(mpi->planes[1], vf->priv->pmpi->planes[1], dmpi->planes[1],
                vf->priv->Line, cw, ch,
                mpi->stride[1], vf->priv->pmpi->stride[1], dmpi->stride[1],
                vf->priv->Coefs[2] + 256,
                vf->priv->Coefs[2] + 256,
                vf->priv->Coefs[3] + 256);
        deNoise(mpi->planes[2], vf->priv->pmpi->planes[2], dmpi->planes[2],
                vf->priv->Line, cw, ch,
                mpi->stride[2], vf->priv->pmpi->stride[2], dmpi->stride[2],
                vf->priv->Coefs[2] + 256,
                vf->priv->Coefs[2] + 256,
                vf->priv->Coefs[3] + 256);

        vf->priv->pmpi=dmpi; // save reference image
        return vf_next_put_image(vf,dmpi, pts);
}

//===========================================================================//

static int query_format(struct vf_instance *vf, unsigned int fmt){
        switch(fmt)
        {
        case IMGFMT_YV12:
        case IMGFMT_I420:
        case IMGFMT_IYUV:
        case IMGFMT_YVU9:
        case IMGFMT_444P:
        case IMGFMT_422P:
        case IMGFMT_411P:
                return vf_next_query_format(vf, fmt);
        }
        return 0;
}


#define ABS(A) ( (A) > 0 ? (A) : -(A) )

static void PrecalcCoefs(int *Ct, double Dist25)
{
    int i;
    double Gamma, Simil, C;

    Gamma = log(0.25) / log(1.0 - Dist25/255.0);

    for (i = -256; i <= 255; i++)
    {
        Simil = 1.0 - ABS(i) / 255.0;
//        Ct[256+i] = lround(pow(Simil, Gamma) * (double)i);
        C = pow(Simil, Gamma) * (double)i;
        Ct[256+i] = (C<0) ? (C-0.5) : (C+0.5);
    }
}


static int vf_open(vf_instance_t *vf, char *args){
        double LumSpac, LumTmp, ChromSpac, ChromTmp;
        double Param1, Param2, Param3;

        vf->config=config;
        vf->put_image=put_image;
        vf->query_format=query_format;
        vf->uninit=uninit;
        vf->priv=malloc(sizeof(struct vf_priv_s));
        memset(vf->priv, 0, sizeof(struct vf_priv_s));

        if (args)
        {
            switch(sscanf(args, "%lf:%lf:%lf",
                          &Param1, &Param2, &Param3
                         ))
            {
            case 0:
                LumSpac = PARAM1_DEFAULT;
                LumTmp = PARAM3_DEFAULT;

                ChromSpac = PARAM2_DEFAULT;
                ChromTmp = LumTmp * ChromSpac / LumSpac;
                break;

            case 1:
                LumSpac = Param1;
                LumTmp = PARAM3_DEFAULT * Param1 / PARAM1_DEFAULT;

                ChromSpac = PARAM2_DEFAULT * Param1 / PARAM1_DEFAULT;
                ChromTmp = LumTmp * ChromSpac / LumSpac;
                break;

            case 2:
                LumSpac = Param1;
                LumTmp = PARAM3_DEFAULT * Param1 / PARAM1_DEFAULT;

                ChromSpac = Param2;
                ChromTmp = LumTmp * ChromSpac / LumSpac;
                break;

            case 3:
                LumSpac = Param1;
                LumTmp = Param3;

                ChromSpac = Param2;
                ChromTmp = LumTmp * ChromSpac / LumSpac;
                break;

            default:
                LumSpac = PARAM1_DEFAULT;
                LumTmp = PARAM3_DEFAULT;

                ChromSpac = PARAM2_DEFAULT;
                ChromTmp = LumTmp * ChromSpac / LumSpac;
            }
        }
        else
        {
            LumSpac = PARAM1_DEFAULT;
            LumTmp = PARAM3_DEFAULT;

            ChromSpac = PARAM2_DEFAULT;
            ChromTmp = LumTmp * ChromSpac / LumSpac;
        }

        PrecalcCoefs(vf->priv->Coefs[0], LumSpac);
        PrecalcCoefs(vf->priv->Coefs[1], LumTmp);
        PrecalcCoefs(vf->priv->Coefs[2], ChromSpac);
        PrecalcCoefs(vf->priv->Coefs[3], ChromTmp);

        return 1;
}

const vf_info_t vf_info_denoise3d = {
    "3D Denoiser (variable lowpass filter)",
    "denoise3d",
    "Daniel Moreno",
    "",
    vf_open,
    NULL
};

//===========================================================================//
