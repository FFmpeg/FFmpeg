/*
 * Copyright (C) 2005 Michael Niedermayer <michaelni@gmx.at>
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
#include <assert.h>

#include "config.h"

#include "mp_msg.h"
#include "cpudetect.h"

#include "libavutil/mem.h"
#include "libavcodec/avcodec.h"

#include "img_format.h"
#include "mp_image.h"
#include "vf.h"
#include "av_helpers.h"
#include "libvo/fastmemcpy.h"

#define XMIN(a,b) ((a) < (b) ? (a) : (b))

#define BLOCK 16

//===========================================================================//
DECLARE_ALIGNED(8, static const uint8_t, dither)[8][8] = {
{  0*4,  48*4,  12*4,  60*4,   3*4,  51*4,  15*4,  63*4, },
{ 32*4,  16*4,  44*4,  28*4,  35*4,  19*4,  47*4,  31*4, },
{  8*4,  56*4,   4*4,  52*4,  11*4,  59*4,   7*4,  55*4, },
{ 40*4,  24*4,  36*4,  20*4,  43*4,  27*4,  39*4,  23*4, },
{  2*4,  50*4,  14*4,  62*4,   1*4,  49*4,  13*4,  61*4, },
{ 34*4,  18*4,  46*4,  30*4,  33*4,  17*4,  45*4,  29*4, },
{ 10*4,  58*4,   6*4,  54*4,   9*4,  57*4,   5*4,  53*4, },
{ 42*4,  26*4,  38*4,  22*4,  41*4,  25*4,  37*4,  21*4, },
};

static const uint8_t offset[511][2]= {
{ 0, 0},
{ 0, 0}, { 8, 8},
{ 0, 0}, { 4, 4}, {12, 8}, { 8,12},
{ 0, 0}, {10, 2}, { 4, 4}, {14, 6}, { 8, 8}, { 2,10}, {12,12}, { 6,14},

{ 0, 0}, {10, 2}, { 4, 4}, {14, 6}, { 8, 8}, { 2,10}, {12,12}, { 6,14},
{ 5, 1}, {15, 3}, { 9, 5}, { 3, 7}, {13, 9}, { 7,11}, { 1,13}, {11,15},

{ 0, 0}, { 8, 0}, { 0, 8}, { 8, 8}, { 5, 1}, {13, 1}, { 5, 9}, {13, 9},
{ 2, 2}, {10, 2}, { 2,10}, {10,10}, { 7, 3}, {15, 3}, { 7,11}, {15,11},
{ 4, 4}, {12, 4}, { 4,12}, {12,12}, { 1, 5}, { 9, 5}, { 1,13}, { 9,13},
{ 6, 6}, {14, 6}, { 6,14}, {14,14}, { 3, 7}, {11, 7}, { 3,15}, {11,15},

{ 0, 0}, { 8, 0}, { 0, 8}, { 8, 8}, { 4, 0}, {12, 0}, { 4, 8}, {12, 8},
{ 1, 1}, { 9, 1}, { 1, 9}, { 9, 9}, { 5, 1}, {13, 1}, { 5, 9}, {13, 9},
{ 3, 2}, {11, 2}, { 3,10}, {11,10}, { 7, 2}, {15, 2}, { 7,10}, {15,10},
{ 2, 3}, {10, 3}, { 2,11}, {10,11}, { 6, 3}, {14, 3}, { 6,11}, {14,11},
{ 0, 4}, { 8, 4}, { 0,12}, { 8,12}, { 4, 4}, {12, 4}, { 4,12}, {12,12},
{ 1, 5}, { 9, 5}, { 1,13}, { 9,13}, { 5, 5}, {13, 5}, { 5,13}, {13,13},
{ 3, 6}, {11, 6}, { 3,14}, {11,14}, { 7, 6}, {15, 6}, { 7,14}, {15,14},
{ 2, 7}, {10, 7}, { 2,15}, {10,15}, { 6, 7}, {14, 7}, { 6,15}, {14,15},

{ 0, 0}, { 8, 0}, { 0, 8}, { 8, 8}, { 0, 2}, { 8, 2}, { 0,10}, { 8,10},
{ 0, 4}, { 8, 4}, { 0,12}, { 8,12}, { 0, 6}, { 8, 6}, { 0,14}, { 8,14},
{ 1, 1}, { 9, 1}, { 1, 9}, { 9, 9}, { 1, 3}, { 9, 3}, { 1,11}, { 9,11},
{ 1, 5}, { 9, 5}, { 1,13}, { 9,13}, { 1, 7}, { 9, 7}, { 1,15}, { 9,15},
{ 2, 0}, {10, 0}, { 2, 8}, {10, 8}, { 2, 2}, {10, 2}, { 2,10}, {10,10},
{ 2, 4}, {10, 4}, { 2,12}, {10,12}, { 2, 6}, {10, 6}, { 2,14}, {10,14},
{ 3, 1}, {11, 1}, { 3, 9}, {11, 9}, { 3, 3}, {11, 3}, { 3,11}, {11,11},
{ 3, 5}, {11, 5}, { 3,13}, {11,13}, { 3, 7}, {11, 7}, { 3,15}, {11,15},
{ 4, 0}, {12, 0}, { 4, 8}, {12, 8}, { 4, 2}, {12, 2}, { 4,10}, {12,10},
{ 4, 4}, {12, 4}, { 4,12}, {12,12}, { 4, 6}, {12, 6}, { 4,14}, {12,14},
{ 5, 1}, {13, 1}, { 5, 9}, {13, 9}, { 5, 3}, {13, 3}, { 5,11}, {13,11},
{ 5, 5}, {13, 5}, { 5,13}, {13,13}, { 5, 7}, {13, 7}, { 5,15}, {13,15},
{ 6, 0}, {14, 0}, { 6, 8}, {14, 8}, { 6, 2}, {14, 2}, { 6,10}, {14,10},
{ 6, 4}, {14, 4}, { 6,12}, {14,12}, { 6, 6}, {14, 6}, { 6,14}, {14,14},
{ 7, 1}, {15, 1}, { 7, 9}, {15, 9}, { 7, 3}, {15, 3}, { 7,11}, {15,11},
{ 7, 5}, {15, 5}, { 7,13}, {15,13}, { 7, 7}, {15, 7}, { 7,15}, {15,15},

{ 0, 0}, { 8, 0}, { 0, 8}, { 8, 8}, { 4, 4}, {12, 4}, { 4,12}, {12,12}, { 0, 4}, { 8, 4}, { 0,12}, { 8,12}, { 4, 0}, {12, 0}, { 4, 8}, {12, 8}, { 2, 2}, {10, 2}, { 2,10}, {10,10}, { 6, 6}, {14, 6}, { 6,14}, {14,14}, { 2, 6}, {10, 6}, { 2,14}, {10,14}, { 6, 2}, {14, 2}, { 6,10}, {14,10}, { 0, 2}, { 8, 2}, { 0,10}, { 8,10}, { 4, 6}, {12, 6}, { 4,14}, {12,14}, { 0, 6}, { 8, 6}, { 0,14}, { 8,14}, { 4, 2}, {12, 2}, { 4,10}, {12,10}, { 2, 0}, {10, 0}, { 2, 8}, {10, 8}, { 6, 4}, {14, 4}, { 6,12}, {14,12}, { 2, 4}, {10, 4}, { 2,12}, {10,12}, { 6, 0}, {14, 0}, { 6, 8}, {14, 8}, { 1, 1}, { 9, 1}, { 1, 9}, { 9, 9}, { 5, 5}, {13, 5}, { 5,13}, {13,13}, { 1, 5}, { 9, 5}, { 1,13}, { 9,13}, { 5, 1}, {13, 1}, { 5, 9}, {13, 9}, { 3, 3}, {11, 3}, { 3,11}, {11,11}, { 7, 7}, {15, 7}, { 7,15}, {15,15}, { 3, 7}, {11, 7}, { 3,15}, {11,15}, { 7, 3}, {15, 3}, { 7,11}, {15,11}, { 1, 3}, { 9, 3}, { 1,11}, { 9,11}, { 5, 7}, {13, 7}, { 5,15}, {13,15}, { 1, 7}, { 9, 7}, { 1,15}, { 9,15}, { 5, 3}, {13, 3}, { 5,11}, {13,11}, { 3, 1}, {11, 1}
, { 3, 9}, {11, 9}, { 7, 5}, {15, 5}, { 7,13}, {15,13}, { 3, 5}, {11, 5}, { 3,13}, {11,13}, { 7, 1}, {15, 1}, { 7, 9}, {15, 9}, { 0, 1}, { 8, 1}, { 0, 9}, { 8, 9}, { 4, 5}, {12, 5}, { 4,13}, {12,13}, { 0, 5}, { 8, 5}, { 0,13}, { 8,13}, { 4, 1}, {12, 1}, { 4, 9}, {12, 9}, { 2, 3}, {10, 3}, { 2,11}, {10,11}, { 6, 7}, {14, 7}, { 6,15}, {14,15}, { 2, 7}, {10, 7}, { 2,15}, {10,15}, { 6, 3}, {14, 3}, { 6,11}, {14,11}, { 0, 3}, { 8, 3}, { 0,11}, { 8,11}, { 4, 7}, {12, 7}, { 4,15}, {12,15}, { 0, 7}, { 8, 7}, { 0,15}, { 8,15}, { 4, 3}, {12, 3}, { 4,11}, {12,11}, { 2, 1}, {10, 1}, { 2, 9}, {10, 9}, { 6, 5}, {14, 5}, { 6,13}, {14,13}, { 2, 5}, {10, 5}, { 2,13}, {10,13}, { 6, 1}, {14, 1}, { 6, 9}, {14, 9}, { 1, 0}, { 9, 0}, { 1, 8}, { 9, 8}, { 5, 4}, {13, 4}, { 5,12}, {13,12}, { 1, 4}, { 9, 4}, { 1,12}, { 9,12}, { 5, 0}, {13, 0}, { 5, 8}, {13, 8}, { 3, 2}, {11, 2}, { 3,10}, {11,10}, { 7, 6}, {15, 6}, { 7,14}, {15,14}, { 3, 6}, {11, 6}, { 3,14}, {11,14}, { 7, 2}, {15, 2}, { 7,10}, {15,10}, { 1, 2}, { 9, 2}, { 1,10}, { 9,
10}, { 5, 6}, {13, 6}, { 5,14}, {13,14}, { 1, 6}, { 9, 6}, { 1,14}, { 9,14}, { 5, 2}, {13, 2}, { 5,10}, {13,10}, { 3, 0}, {11, 0}, { 3, 8}, {11, 8}, { 7, 4}, {15, 4}, { 7,12}, {15,12}, { 3, 4}, {11, 4}, { 3,12}, {11,12}, { 7, 0}, {15, 0}, { 7, 8}, {15, 8},
};

struct vf_priv_s {
    int log2_count;
    int qp;
    int mode;
    int mpeg2;
    int temp_stride[3];
    uint8_t *src[3];
    int16_t *temp[3];
    int outbuf_size;
    uint8_t *outbuf;
    AVCodecContext *avctx_enc[BLOCK*BLOCK];
    AVFrame *frame;
    AVFrame *frame_dec;
};

static void store_slice_c(uint8_t *dst, int16_t *src, int dst_stride, int src_stride, int width, int height, int log2_scale){
        int y, x;

#define STORE(pos) \
        temp= ((src[x + y*src_stride + pos]<<log2_scale) + d[pos])>>8;\
        if(temp & 0x100) temp= ~(temp>>31);\
        dst[x + y*dst_stride + pos]= temp;

        for(y=0; y<height; y++){
                const uint8_t *d= dither[y&7];
                for(x=0; x<width; x+=8){
                        int temp;
                        STORE(0);
                        STORE(1);
                        STORE(2);
                        STORE(3);
                        STORE(4);
                        STORE(5);
                        STORE(6);
                        STORE(7);
                }
        }
}

static void filter(struct vf_priv_s *p, uint8_t *dst[3], uint8_t *src[3], int dst_stride[3], int src_stride[3], int width, int height, uint8_t *qp_store, int qp_stride){
    int x, y, i, j;
    const int count= 1<<p->log2_count;

    for(i=0; i<3; i++){
        int is_chroma= !!i;
        int w= width >>is_chroma;
        int h= height>>is_chroma;
        int stride= p->temp_stride[i];
        int block= BLOCK>>is_chroma;

        if (!src[i] || !dst[i])
            continue; // HACK avoid crash for Y8 colourspace
        for(y=0; y<h; y++){
            int index= block + block*stride + y*stride;
            fast_memcpy(p->src[i] + index, src[i] + y*src_stride[i], w);
            for(x=0; x<block; x++){
                p->src[i][index     - x - 1]= p->src[i][index +     x    ];
                p->src[i][index + w + x    ]= p->src[i][index + w - x - 1];
            }
        }
        for(y=0; y<block; y++){
            fast_memcpy(p->src[i] + (  block-1-y)*stride, p->src[i] + (  y+block  )*stride, stride);
            fast_memcpy(p->src[i] + (h+block  +y)*stride, p->src[i] + (h-y+block-1)*stride, stride);
        }

        p->frame->linesize[i]= stride;
        memset(p->temp[i], 0, (h+2*block)*stride*sizeof(int16_t));
    }

    if(p->qp)
        p->frame->quality= p->qp * FF_QP2LAMBDA;
    else
        p->frame->quality= norm_qscale(qp_store[0], p->mpeg2) * FF_QP2LAMBDA;
//    init per MB qscale stuff FIXME

    for(i=0; i<count; i++){
        const int x1= offset[i+count-1][0];
        const int y1= offset[i+count-1][1];
        int offset;
        p->frame->data[0]= p->src[0] + x1 + y1 * p->frame->linesize[0];
        p->frame->data[1]= p->src[1] + x1/2 + y1/2 * p->frame->linesize[1];
        p->frame->data[2]= p->src[2] + x1/2 + y1/2 * p->frame->linesize[2];

        avcodec_encode_video(p->avctx_enc[i], p->outbuf, p->outbuf_size, p->frame);
        p->frame_dec = p->avctx_enc[i]->coded_frame;

        offset= (BLOCK-x1) + (BLOCK-y1)*p->frame_dec->linesize[0];
        //FIXME optimize
        for(y=0; y<height; y++){
            for(x=0; x<width; x++){
                p->temp[0][ x + y*p->temp_stride[0] ] += p->frame_dec->data[0][ x + y*p->frame_dec->linesize[0] + offset ];
            }
        }
        offset= (BLOCK/2-x1/2) + (BLOCK/2-y1/2)*p->frame_dec->linesize[1];
        for(y=0; y<height/2; y++){
            for(x=0; x<width/2; x++){
                p->temp[1][ x + y*p->temp_stride[1] ] += p->frame_dec->data[1][ x + y*p->frame_dec->linesize[1] + offset ];
                p->temp[2][ x + y*p->temp_stride[2] ] += p->frame_dec->data[2][ x + y*p->frame_dec->linesize[2] + offset ];
            }
        }
    }

    for(j=0; j<3; j++){
        int is_chroma= !!j;
        if (!dst[j])
            continue; // HACK avoid crash for Y8 colourspace
        store_slice_c(dst[j], p->temp[j], dst_stride[j], p->temp_stride[j], width>>is_chroma, height>>is_chroma, 8-p->log2_count);
    }
}

static int config(struct vf_instance *vf,
        int width, int height, int d_width, int d_height,
        unsigned int flags, unsigned int outfmt){
        int i;
        AVCodec *enc= avcodec_find_encoder(AV_CODEC_ID_SNOW);

        for(i=0; i<3; i++){
            int is_chroma= !!i;
            int w= ((width  + 4*BLOCK-1) & (~(2*BLOCK-1)))>>is_chroma;
            int h= ((height + 4*BLOCK-1) & (~(2*BLOCK-1)))>>is_chroma;

            vf->priv->temp_stride[i]= w;
            vf->priv->temp[i]= malloc(vf->priv->temp_stride[i]*h*sizeof(int16_t));
            vf->priv->src [i]= malloc(vf->priv->temp_stride[i]*h*sizeof(uint8_t));
        }
        for(i=0; i< (1<<vf->priv->log2_count); i++){
            AVCodecContext *avctx_enc;
            AVDictionary *opts = NULL;

            avctx_enc=
            vf->priv->avctx_enc[i]= avcodec_alloc_context3(NULL);
            avctx_enc->width = width + BLOCK;
            avctx_enc->height = height + BLOCK;
            avctx_enc->time_base= (AVRational){1,25};  // meaningless
            avctx_enc->gop_size = 300;
            avctx_enc->max_b_frames= 0;
            avctx_enc->pix_fmt = AV_PIX_FMT_YUV420P;
            avctx_enc->flags = CODEC_FLAG_QSCALE | CODEC_FLAG_LOW_DELAY;
            avctx_enc->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
            avctx_enc->global_quality= 123;
            av_dict_set(&opts, "no_bitstream", "1", 0);
            if (avcodec_open2(avctx_enc, enc, &opts) < 0)
                return 0;
            av_dict_free(&opts);
            assert(avctx_enc->codec);
        }
        vf->priv->frame= av_frame_alloc();
        vf->priv->frame_dec= av_frame_alloc();

        vf->priv->outbuf_size= (width + BLOCK)*(height + BLOCK)*10;
        vf->priv->outbuf= malloc(vf->priv->outbuf_size);

        return ff_vf_next_config(vf,width,height,d_width,d_height,flags,outfmt);
}

static void get_image(struct vf_instance *vf, mp_image_t *mpi){
    if(mpi->flags&MP_IMGFLAG_PRESERVE) return; // don't change
    // ok, we can do pp in-place (or pp disabled):
    vf->dmpi=ff_vf_get_image(vf->next,mpi->imgfmt,
        mpi->type, mpi->flags | MP_IMGFLAG_READABLE, mpi->width, mpi->height);
    mpi->planes[0]=vf->dmpi->planes[0];
    mpi->stride[0]=vf->dmpi->stride[0];
    mpi->width=vf->dmpi->width;
    if(mpi->flags&MP_IMGFLAG_PLANAR){
        mpi->planes[1]=vf->dmpi->planes[1];
        mpi->planes[2]=vf->dmpi->planes[2];
        mpi->stride[1]=vf->dmpi->stride[1];
        mpi->stride[2]=vf->dmpi->stride[2];
    }
    mpi->flags|=MP_IMGFLAG_DIRECT;
}

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts){
    mp_image_t *dmpi;

    if(!(mpi->flags&MP_IMGFLAG_DIRECT)){
        // no DR, so get a new image! hope we'll get DR buffer:
        dmpi=ff_vf_get_image(vf->next,mpi->imgfmt,
            MP_IMGTYPE_TEMP,
            MP_IMGFLAG_ACCEPT_STRIDE|MP_IMGFLAG_PREFER_ALIGNED_STRIDE,
            mpi->width,mpi->height);
        ff_vf_clone_mpi_attributes(dmpi, mpi);
    }else{
        dmpi=vf->dmpi;
    }

    vf->priv->mpeg2= mpi->qscale_type;
    if(vf->priv->log2_count || !(mpi->flags&MP_IMGFLAG_DIRECT)){
        if(mpi->qscale || vf->priv->qp){
            filter(vf->priv, dmpi->planes, mpi->planes, dmpi->stride, mpi->stride, mpi->w, mpi->h, mpi->qscale, mpi->qstride);
        }else{
            memcpy_pic(dmpi->planes[0], mpi->planes[0], mpi->w, mpi->h, dmpi->stride[0], mpi->stride[0]);
            memcpy_pic(dmpi->planes[1], mpi->planes[1], mpi->w>>mpi->chroma_x_shift, mpi->h>>mpi->chroma_y_shift, dmpi->stride[1], mpi->stride[1]);
            memcpy_pic(dmpi->planes[2], mpi->planes[2], mpi->w>>mpi->chroma_x_shift, mpi->h>>mpi->chroma_y_shift, dmpi->stride[2], mpi->stride[2]);
        }
    }

#if HAVE_MMX
    if(ff_gCpuCaps.hasMMX) __asm__ volatile ("emms\n\t");
#endif
#if HAVE_MMX2
    if(ff_gCpuCaps.hasMMX2) __asm__ volatile ("sfence\n\t");
#endif

    return ff_vf_next_put_image(vf,dmpi, pts);
}

static void uninit(struct vf_instance *vf){
    int i;
    if(!vf->priv) return;

    for(i=0; i<3; i++){
        free(vf->priv->temp[i]);
        vf->priv->temp[i]= NULL;
        free(vf->priv->src[i]);
        vf->priv->src[i]= NULL;
    }
    for(i=0; i<BLOCK*BLOCK; i++){
        av_freep(&vf->priv->avctx_enc[i]);
    }

    free(vf->priv);
    vf->priv=NULL;
}

//===========================================================================//
static int query_format(struct vf_instance *vf, unsigned int fmt){
    switch(fmt){
        case IMGFMT_YV12:
        case IMGFMT_I420:
        case IMGFMT_IYUV:
        case IMGFMT_Y800:
        case IMGFMT_Y8:
            return ff_vf_next_query_format(vf,fmt);
    }
    return 0;
}

static int control(struct vf_instance *vf, int request, void* data){
    switch(request){
    case VFCTRL_QUERY_MAX_PP_LEVEL:
        return 8;
    case VFCTRL_SET_PP_LEVEL:
        vf->priv->log2_count= *((unsigned int*)data);
        //FIXME we have to realloc a few things here
        return CONTROL_TRUE;
    }
    return ff_vf_next_control(vf,request,data);
}

static int vf_open(vf_instance_t *vf, char *args){

    int log2c=-1;

    vf->config=config;
    vf->put_image=put_image;
    vf->get_image=get_image;
    vf->query_format=query_format;
    vf->uninit=uninit;
    vf->control= control;
    vf->priv=malloc(sizeof(struct vf_priv_s));
    memset(vf->priv, 0, sizeof(struct vf_priv_s));

    ff_init_avcodec();

    vf->priv->log2_count= 4;

    if (args) sscanf(args, "%d:%d:%d", &log2c, &vf->priv->qp, &vf->priv->mode);

    if( log2c >=0 && log2c <=8 )
        vf->priv->log2_count = log2c;

    if(vf->priv->qp < 0)
        vf->priv->qp = 0;

// #if HAVE_MMX
//     if(ff_gCpuCaps.hasMMX){
//         store_slice= store_slice_mmx;
//     }
// #endif

    return 1;
}

const vf_info_t ff_vf_info_uspp = {
    "ultra simple/slow postprocess",
    "uspp",
    "Michael Niedermayer",
    "",
    vf_open,
    NULL
};
