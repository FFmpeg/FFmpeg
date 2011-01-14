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
        int Coefs[4][512*16];
        unsigned int *Line;
        unsigned short *Frame[3];
};


/***************************************************************************/

static void uninit(struct vf_instance *vf)
{
        free(vf->priv->Line);
        free(vf->priv->Frame[0]);
        free(vf->priv->Frame[1]);
        free(vf->priv->Frame[2]);

        vf->priv->Line     = NULL;
        vf->priv->Frame[0] = NULL;
        vf->priv->Frame[1] = NULL;
        vf->priv->Frame[2] = NULL;
}

static int config(struct vf_instance *vf,
        int width, int height, int d_width, int d_height,
        unsigned int flags, unsigned int outfmt){

        uninit(vf);
        vf->priv->Line = malloc(width*sizeof(int));

        return vf_next_config(vf,width,height,d_width,d_height,flags,outfmt);
}

static inline unsigned int LowPassMul(unsigned int PrevMul, unsigned int CurrMul, int* Coef){
//    int dMul= (PrevMul&0xFFFFFF)-(CurrMul&0xFFFFFF);
    int dMul= PrevMul-CurrMul;
    unsigned int d=((dMul+0x10007FF)>>12);
    return CurrMul + Coef[d];
}

static void deNoiseTemporal(
                    unsigned char *Frame,        // mpi->planes[x]
                    unsigned char *FrameDest,    // dmpi->planes[x]
                    unsigned short *FrameAnt,
                    int W, int H, int sStride, int dStride,
                    int *Temporal)
{
    long X, Y;
    unsigned int PixelDst;

    for (Y = 0; Y < H; Y++){
        for (X = 0; X < W; X++){
            PixelDst = LowPassMul(FrameAnt[X]<<8, Frame[X]<<16, Temporal);
            FrameAnt[X] = ((PixelDst+0x1000007F)>>8);
            FrameDest[X]= ((PixelDst+0x10007FFF)>>16);
        }
        Frame += sStride;
        FrameDest += dStride;
        FrameAnt += W;
    }
}

static void deNoiseSpacial(
                    unsigned char *Frame,        // mpi->planes[x]
                    unsigned char *FrameDest,    // dmpi->planes[x]
                    unsigned int *LineAnt,       // vf->priv->Line (width bytes)
                    int W, int H, int sStride, int dStride,
                    int *Horizontal, int *Vertical)
{
    long X, Y;
    long sLineOffs = 0, dLineOffs = 0;
    unsigned int PixelAnt;
    unsigned int PixelDst;

    /* First pixel has no left nor top neighbor. */
    PixelDst = LineAnt[0] = PixelAnt = Frame[0]<<16;
    FrameDest[0]= ((PixelDst+0x10007FFF)>>16);

    /* First line has no top neighbor, only left. */
    for (X = 1; X < W; X++){
        PixelDst = LineAnt[X] = LowPassMul(PixelAnt, Frame[X]<<16, Horizontal);
        FrameDest[X]= ((PixelDst+0x10007FFF)>>16);
    }

    for (Y = 1; Y < H; Y++){
        unsigned int PixelAnt;
        sLineOffs += sStride, dLineOffs += dStride;
        /* First pixel on each line doesn't have previous pixel */
        PixelAnt = Frame[sLineOffs]<<16;
        PixelDst = LineAnt[0] = LowPassMul(LineAnt[0], PixelAnt, Vertical);
        FrameDest[dLineOffs]= ((PixelDst+0x10007FFF)>>16);

        for (X = 1; X < W; X++){
            unsigned int PixelDst;
            /* The rest are normal */
            PixelAnt = LowPassMul(PixelAnt, Frame[sLineOffs+X]<<16, Horizontal);
            PixelDst = LineAnt[X] = LowPassMul(LineAnt[X], PixelAnt, Vertical);
            FrameDest[dLineOffs+X]= ((PixelDst+0x10007FFF)>>16);
        }
    }
}

static void deNoise(unsigned char *Frame,        // mpi->planes[x]
                    unsigned char *FrameDest,    // dmpi->planes[x]
                    unsigned int *LineAnt,      // vf->priv->Line (width bytes)
                    unsigned short **FrameAntPtr,
                    int W, int H, int sStride, int dStride,
                    int *Horizontal, int *Vertical, int *Temporal)
{
    long X, Y;
    long sLineOffs = 0, dLineOffs = 0;
    unsigned int PixelAnt;
    unsigned int PixelDst;
    unsigned short* FrameAnt=(*FrameAntPtr);

    if(!FrameAnt){
        (*FrameAntPtr)=FrameAnt=malloc(W*H*sizeof(unsigned short));
        for (Y = 0; Y < H; Y++){
            unsigned short* dst=&FrameAnt[Y*W];
            unsigned char* src=Frame+Y*sStride;
            for (X = 0; X < W; X++) dst[X]=src[X]<<8;
        }
    }

    if(!Horizontal[0] && !Vertical[0]){
        deNoiseTemporal(Frame, FrameDest, FrameAnt,
                        W, H, sStride, dStride, Temporal);
        return;
    }
    if(!Temporal[0]){
        deNoiseSpacial(Frame, FrameDest, LineAnt,
                       W, H, sStride, dStride, Horizontal, Vertical);
        return;
    }

    /* First pixel has no left nor top neighbor. Only previous frame */
    LineAnt[0] = PixelAnt = Frame[0]<<16;
    PixelDst = LowPassMul(FrameAnt[0]<<8, PixelAnt, Temporal);
    FrameAnt[0] = ((PixelDst+0x1000007F)>>8);
    FrameDest[0]= ((PixelDst+0x10007FFF)>>16);

    /* First line has no top neighbor. Only left one for each pixel and
     * last frame */
    for (X = 1; X < W; X++){
        LineAnt[X] = PixelAnt = LowPassMul(PixelAnt, Frame[X]<<16, Horizontal);
        PixelDst = LowPassMul(FrameAnt[X]<<8, PixelAnt, Temporal);
        FrameAnt[X] = ((PixelDst+0x1000007F)>>8);
        FrameDest[X]= ((PixelDst+0x10007FFF)>>16);
    }

    for (Y = 1; Y < H; Y++){
        unsigned int PixelAnt;
        unsigned short* LinePrev=&FrameAnt[Y*W];
        sLineOffs += sStride, dLineOffs += dStride;
        /* First pixel on each line doesn't have previous pixel */
        PixelAnt = Frame[sLineOffs]<<16;
        LineAnt[0] = LowPassMul(LineAnt[0], PixelAnt, Vertical);
        PixelDst = LowPassMul(LinePrev[0]<<8, LineAnt[0], Temporal);
        LinePrev[0] = ((PixelDst+0x1000007F)>>8);
        FrameDest[dLineOffs]= ((PixelDst+0x10007FFF)>>16);

        for (X = 1; X < W; X++){
            unsigned int PixelDst;
            /* The rest are normal */
            PixelAnt = LowPassMul(PixelAnt, Frame[sLineOffs+X]<<16, Horizontal);
            LineAnt[X] = LowPassMul(LineAnt[X], PixelAnt, Vertical);
            PixelDst = LowPassMul(LinePrev[X]<<8, LineAnt[X], Temporal);
            LinePrev[X] = ((PixelDst+0x1000007F)>>8);
            FrameDest[dLineOffs+X]= ((PixelDst+0x10007FFF)>>16);
        }
    }
}


static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts){
        int cw= mpi->w >> mpi->chroma_x_shift;
        int ch= mpi->h >> mpi->chroma_y_shift;
        int W = mpi->w, H = mpi->h;

        mp_image_t *dmpi=vf_get_image(vf->next,mpi->imgfmt,
                MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE,
                mpi->w,mpi->h);

        if(!dmpi) return 0;

        deNoise(mpi->planes[0], dmpi->planes[0],
                vf->priv->Line, &vf->priv->Frame[0], W, H,
                mpi->stride[0], dmpi->stride[0],
                vf->priv->Coefs[0],
                vf->priv->Coefs[0],
                vf->priv->Coefs[1]);
        deNoise(mpi->planes[1], dmpi->planes[1],
                vf->priv->Line, &vf->priv->Frame[1], cw, ch,
                mpi->stride[1], dmpi->stride[1],
                vf->priv->Coefs[2],
                vf->priv->Coefs[2],
                vf->priv->Coefs[3]);
        deNoise(mpi->planes[2], dmpi->planes[2],
                vf->priv->Line, &vf->priv->Frame[2], cw, ch,
                mpi->stride[2], dmpi->stride[2],
                vf->priv->Coefs[2],
                vf->priv->Coefs[2],
                vf->priv->Coefs[3]);

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

    Gamma = log(0.25) / log(1.0 - Dist25/255.0 - 0.00001);

    for (i = -255*16; i <= 255*16; i++)
    {
        Simil = 1.0 - ABS(i) / (16*255.0);
        C = pow(Simil, Gamma) * 65536.0 * (double)i / 16.0;
        Ct[16*256+i] = (C<0) ? (C-0.5) : (C+0.5);
    }

    Ct[0] = (Dist25 != 0);
}


static int vf_open(vf_instance_t *vf, char *args){
        double LumSpac, LumTmp, ChromSpac, ChromTmp;
        double Param1, Param2, Param3, Param4;

        vf->config=config;
        vf->put_image=put_image;
        vf->query_format=query_format;
        vf->uninit=uninit;
        vf->priv=malloc(sizeof(struct vf_priv_s));
        memset(vf->priv, 0, sizeof(struct vf_priv_s));

        if (args)
        {
            switch(sscanf(args, "%lf:%lf:%lf:%lf",
                          &Param1, &Param2, &Param3, &Param4
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

            case 4:
                LumSpac = Param1;
                LumTmp = Param3;

                ChromSpac = Param2;
                ChromTmp = Param4;
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

const vf_info_t vf_info_hqdn3d = {
    "High Quality 3D Denoiser",
    "hqdn3d",
    "Daniel Moreno & A'rpi",
    "",
    vf_open,
    NULL
};

//===========================================================================//
