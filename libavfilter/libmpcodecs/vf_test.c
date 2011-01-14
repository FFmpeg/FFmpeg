/*
 * Copyright (C) 2002 Michael Niedermayer <michaelni@gmx.at>
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

#include "config.h"
#include "mp_msg.h"
#include "help_mp.h"

#include "img_format.h"
#include "mp_image.h"
#include "vf.h"

//===========================================================================//

#include <inttypes.h>
#include <math.h>

#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define ABS(a,b) ((a) > 0 ? (a) : -(a))

#define WIDTH 512
#define HEIGHT 512

struct vf_priv_s {
    int frame_num;
};

static int config(struct vf_instance *vf,
        int width, int height, int d_width, int d_height,
        unsigned int flags, unsigned int outfmt){

    if(vf_next_query_format(vf,IMGFMT_YV12)<=0){
        mp_msg(MSGT_VFILTER, MSGL_WARN, MSGTR_MPCODECS_WarnNextFilterDoesntSupport, "YV12");
        return 0;
    }

    //hmm whats the meaning of these ... ;)
    d_width= width= WIDTH;
    d_height= height= HEIGHT;

    return vf_next_config(vf,width,height,d_width,d_height,flags,IMGFMT_YV12);
}

static double c[64];

static void initIdct(void)
{
        int i;

        for (i=0; i<8; i++)
        {
                double s= i==0 ? sqrt(0.125) : 0.5;
                int j;

                for(j=0; j<8; j++)
                        c[i*8+j]= s*cos((3.141592654/8.0)*i*(j+0.5));
        }
}


static void idct(uint8_t *dst, int dstStride, int src[64])
{
        int i, j, k;
        double tmp[64];

        for(i=0; i<8; i++)
        {
                for(j=0; j<8; j++)
                {
                        double sum= 0.0;

                        for(k=0; k<8; k++)
                                sum+= c[k*8+j]*src[8*i+k];

                        tmp[8*i+j]= sum;
                }
        }

        for(j=0; j<8; j++)
        {
                for(i=0; i<8; i++)
                {
                        int v;
                        double sum= 0.0;

                        for(k=0; k<8; k++)
                                sum+= c[k*8+i]*tmp[8*k+j];

                        v= (int)floor(sum+0.5);
                        if(v<0) v=0;
                        else if(v>255) v=255;

                        dst[dstStride*i + j] = v;
                }
        }
}

static void drawDc(uint8_t *dst, int stride, int color, int w, int h)
{
        int y;
        for(y=0; y<h; y++)
        {
                int x;
                for(x=0; x<w; x++)
                {
                        dst[x + y*stride]= color;
                }
        }
}

static void drawBasis(uint8_t *dst, int stride, int amp, int freq, int dc)
{
        int src[64];

        memset(src, 0, 64*sizeof(int));
        src[0]= dc;
        if(amp) src[freq]= amp;
        idct(dst, stride, src);
}

static void drawCbp(uint8_t *dst[3], int stride[3], int cbp, int amp, int dc)
{
        if(cbp&1) drawBasis(dst[0]              , stride[0], amp, 1, dc);
        if(cbp&2) drawBasis(dst[0]+8            , stride[0], amp, 1, dc);
        if(cbp&4) drawBasis(dst[0]+  8*stride[0], stride[0], amp, 1, dc);
        if(cbp&8) drawBasis(dst[0]+8+8*stride[0], stride[0], amp, 1, dc);
        if(cbp&16)drawBasis(dst[1]              , stride[1], amp, 1, dc);
        if(cbp&32)drawBasis(dst[2]              , stride[2], amp, 1, dc);
}

static void dc1Test(uint8_t *dst, int stride, int w, int h, int off)
{
        const int step= MAX(256/(w*h/256), 1);
        int y;
        int color=off;
        for(y=0; y<h; y+=16)
        {
                int x;
                for(x=0; x<w; x+=16)
                {
                        drawDc(dst + x + y*stride, stride, color, 8, 8);
                        color+=step;
                }
        }
}

static void freq1Test(uint8_t *dst, int stride, int off)
{
        int y;
        int freq=0;
        for(y=0; y<8*16; y+=16)
        {
                int x;
                for(x=0; x<8*16; x+=16)
                {
                        drawBasis(dst + x + y*stride, stride, 4*(96+off), freq, 128*8);
                        freq++;
                }
        }
}

static void amp1Test(uint8_t *dst, int stride, int off)
{
        int y;
        int amp=off;
        for(y=0; y<16*16; y+=16)
        {
                int x;
                for(x=0; x<16*16; x+=16)
                {
                        drawBasis(dst + x + y*stride, stride, 4*(amp), 1, 128*8);
                        amp++;
                }
        }
}

static void cbp1Test(uint8_t *dst[3], int stride[3], int off)
{
        int y;
        int cbp=0;
        for(y=0; y<16*8; y+=16)
        {
                int x;
                for(x=0; x<16*8; x+=16)
                {
                        uint8_t *dst1[3];
                        dst1[0]= dst[0] + x*2 + y*2*stride[0];
                        dst1[1]= dst[1] + x + y*stride[1];
                        dst1[2]= dst[2] + x + y*stride[2];

                        drawCbp(dst1, stride, cbp, (64+off)*4, 128*8);
                        cbp++;
                }
        }
}

static void mv1Test(uint8_t *dst, int stride, int off)
{
        int y;
        for(y=0; y<16*16; y++)
        {
                int x;
                if(y&16) continue;
                for(x=0; x<16*16; x++)
                {
                        dst[x + y*stride]= x + off*8/(y/32+1);
                }
        }
}

static void ring1Test(uint8_t *dst, int stride, int off)
{
        int y;
        int color=0;
        for(y=off; y<16*16; y+=16)
        {
                int x;
                for(x=off; x<16*16; x+=16)
                {
                        drawDc(dst + x + y*stride, stride, ((x+y)&16) ? color : -color, 16, 16);
//                        dst[x + y*stride]= 255 + (off&1);
                        color++;
                }
        }
}

static void ring2Test(uint8_t *dst, int stride, int off)
{
        int y;
        for(y=0; y<16*16; y++)
        {
                int x;
                for(x=0; x<16*16; x++)
                {
                        double d= sqrt((x-8*16)*(x-8*16) + (y-8*16)*(y-8*16));
                        double r= d/20 - (int)(d/20);
                        if(r<off/30.0)
                        {
                                dst[x + y*stride]= 255;
                                dst[x + y*stride+256]= 0;
                        }
                        else{
                                dst[x + y*stride]= x;
                                dst[x + y*stride+256]= x;
                        }
                }
        }
}

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts){
    mp_image_t *dmpi;
    int frame= vf->priv->frame_num;

    // hope we'll get DR buffer:
    dmpi=vf_get_image(vf->next,IMGFMT_YV12,
        MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE,
        WIDTH, HEIGHT);

    // clean
    memset(dmpi->planes[0], 0, dmpi->stride[0]*dmpi->h);
    memset(dmpi->planes[1], 128, dmpi->stride[1]*dmpi->h>>dmpi->chroma_y_shift);
    memset(dmpi->planes[2], 128, dmpi->stride[2]*dmpi->h>>dmpi->chroma_y_shift);

    if(frame%30)
    {
        switch(frame/30)
        {
        case 0:   dc1Test(dmpi->planes[0], dmpi->stride[0], 256, 256, frame%30); break;
        case 1:   dc1Test(dmpi->planes[1], dmpi->stride[1], 256, 256, frame%30); break;
        case 2: freq1Test(dmpi->planes[0], dmpi->stride[0], frame%30); break;
        case 3: freq1Test(dmpi->planes[1], dmpi->stride[1], frame%30); break;
        case 4:  amp1Test(dmpi->planes[0], dmpi->stride[0], frame%30); break;
        case 5:  amp1Test(dmpi->planes[1], dmpi->stride[1], frame%30); break;
        case 6:  cbp1Test(dmpi->planes   , dmpi->stride   , frame%30); break;
        case 7:   mv1Test(dmpi->planes[0], dmpi->stride[0], frame%30); break;
        case 8: ring1Test(dmpi->planes[0], dmpi->stride[0], frame%30); break;
        case 9: ring2Test(dmpi->planes[0], dmpi->stride[0], frame%30); break;
        }
    }

    frame++;
    vf->priv->frame_num= frame;
    return vf_next_put_image(vf,dmpi, pts);
}

//===========================================================================//

static int query_format(struct vf_instance *vf, unsigned int fmt){
    return vf_next_query_format(vf,IMGFMT_YV12) & (~VFCAP_CSP_SUPPORTED_BY_HW);
}

static int vf_open(vf_instance_t *vf, char *args){
    vf->config=config;
    vf->put_image=put_image;
    vf->query_format=query_format;
    vf->priv=malloc(sizeof(struct vf_priv_s));
    vf->priv->frame_num= args ? atoi(args) : 0;
    initIdct();
    return 1;
}

const vf_info_t vf_info_test = {
    "test pattern generator",
    "test",
    "Michael Niedermayer",
    "",
    vf_open,
    NULL
};

//===========================================================================//
