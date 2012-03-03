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
#include <errno.h>

#include "config.h"
#include "mp_msg.h"
#include "cpudetect.h"

#if HAVE_MALLOC_H
#include <malloc.h>
#endif

#include "img_format.h"
#include "mp_image.h"
#include "vf.h"
#include "libpostproc/postprocess.h"

#ifdef CONFIG_FFMPEG_A
#define EMU_OLD
#include "libpostproc/postprocess_internal.h"
#endif

#undef malloc

struct vf_priv_s {
    int pp;
    pp_mode *ppMode[PP_QUALITY_MAX+1];
    void *context;
    unsigned int outfmt;
};

//===========================================================================//

static int config(struct vf_instance *vf,
        int width, int height, int d_width, int d_height,
        unsigned int voflags, unsigned int outfmt){
    int flags=
          (gCpuCaps.hasMMX   ? PP_CPU_CAPS_MMX   : 0)
        | (gCpuCaps.hasMMX2  ? PP_CPU_CAPS_MMX2  : 0)
        | (gCpuCaps.has3DNow ? PP_CPU_CAPS_3DNOW : 0);

    switch(outfmt){
    case IMGFMT_444P: flags|= PP_FORMAT_444; break;
    case IMGFMT_422P: flags|= PP_FORMAT_422; break;
    case IMGFMT_411P: flags|= PP_FORMAT_411; break;
    default:          flags|= PP_FORMAT_420; break;
    }

    if(vf->priv->context) pp_free_context(vf->priv->context);
    vf->priv->context= pp_get_context(width, height, flags);

    return vf_next_config(vf,width,height,d_width,d_height,voflags,outfmt);
}

static void uninit(struct vf_instance *vf){
    int i;
    for(i=0; i<=PP_QUALITY_MAX; i++){
        if(vf->priv->ppMode[i])
            pp_free_mode(vf->priv->ppMode[i]);
    }
    if(vf->priv->context) pp_free_context(vf->priv->context);
    free(vf->priv);
}

static int query_format(struct vf_instance *vf, unsigned int fmt){
    switch(fmt){
    case IMGFMT_YV12:
    case IMGFMT_I420:
    case IMGFMT_IYUV:
    case IMGFMT_444P:
    case IMGFMT_422P:
    case IMGFMT_411P:
        return vf_next_query_format(vf,fmt);
    }
    return 0;
}

static int control(struct vf_instance *vf, int request, void* data){
    switch(request){
    case VFCTRL_QUERY_MAX_PP_LEVEL:
        return PP_QUALITY_MAX;
    case VFCTRL_SET_PP_LEVEL:
        vf->priv->pp= *((unsigned int*)data);
        return CONTROL_TRUE;
    }
    return vf_next_control(vf,request,data);
}

static void get_image(struct vf_instance *vf, mp_image_t *mpi){
    if(vf->priv->pp&0xFFFF) return; // non-local filters enabled
    if((mpi->type==MP_IMGTYPE_IPB || vf->priv->pp) &&
        mpi->flags&MP_IMGFLAG_PRESERVE) return; // don't change
    if(!(mpi->flags&MP_IMGFLAG_ACCEPT_STRIDE) && mpi->imgfmt!=vf->priv->outfmt)
        return; // colorspace differ
    // ok, we can do pp in-place (or pp disabled):
    vf->dmpi=vf_get_image(vf->next,mpi->imgfmt,
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
    if(!(mpi->flags&MP_IMGFLAG_DIRECT)){
        // no DR, so get a new image! hope we'll get DR buffer:
        vf->dmpi=vf_get_image(vf->next,mpi->imgfmt,
            MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE |
            MP_IMGFLAG_PREFER_ALIGNED_STRIDE | MP_IMGFLAG_READABLE,
//            MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE,
//            mpi->w,mpi->h);
            (mpi->width+7)&(~7),(mpi->height+7)&(~7));
        vf->dmpi->w=mpi->w; vf->dmpi->h=mpi->h; // display w;h
    }

    if(vf->priv->pp || !(mpi->flags&MP_IMGFLAG_DIRECT)){
        // do the postprocessing! (or copy if no DR)
        pp_postprocess(mpi->planes           ,mpi->stride,
                    vf->dmpi->planes,vf->dmpi->stride,
                    (mpi->w+7)&(~7),mpi->h,
                    mpi->qscale, mpi->qstride,
                    vf->priv->ppMode[ vf->priv->pp ], vf->priv->context,
#ifdef PP_PICT_TYPE_QP2
                    mpi->pict_type | (mpi->qscale_type ? PP_PICT_TYPE_QP2 : 0));
#else
                    mpi->pict_type);
#endif
    }
    return vf_next_put_image(vf,vf->dmpi, pts);
}

//===========================================================================//

static const unsigned int fmt_list[]={
    IMGFMT_YV12,
    IMGFMT_I420,
    IMGFMT_IYUV,
    IMGFMT_444P,
    IMGFMT_422P,
    IMGFMT_411P,
    0
};

static int vf_open(vf_instance_t *vf, char *args){
    char *endptr, *name;
    int i;
    int hex_mode=0;

    vf->query_format=query_format;
    vf->control=control;
    vf->config=config;
    vf->get_image=get_image;
    vf->put_image=put_image;
    vf->uninit=uninit;
    vf->default_caps=VFCAP_ACCEPT_STRIDE|VFCAP_POSTPROC;
    vf->priv=malloc(sizeof(struct vf_priv_s));
    vf->priv->context=NULL;

    // check csp:
    vf->priv->outfmt=vf_match_csp(&vf->next,fmt_list,IMGFMT_YV12);
    if(!vf->priv->outfmt) return 0; // no csp match :(

    if(args){
        hex_mode= strtol(args, &endptr, 0);
        if(*endptr){
            name= args;
        }else
            name= NULL;
    }else{
        name="de";
    }

#ifdef EMU_OLD
    if(name){
#endif
        for(i=0; i<=PP_QUALITY_MAX; i++){
            vf->priv->ppMode[i]= pp_get_mode_by_name_and_quality(name, i);
            if(vf->priv->ppMode[i]==NULL) return -1;
        }
#ifdef EMU_OLD
    }else{
        /* hex mode for compatibility */
        for(i=0; i<=PP_QUALITY_MAX; i++){
            PPMode *ppMode;

            ppMode = av_malloc(sizeof(PPMode));

            ppMode->lumMode= hex_mode;
            ppMode->chromMode= ((hex_mode&0xFF)>>4) | (hex_mode&0xFFFFFF00);
            ppMode->maxTmpNoise[0]= 700;
            ppMode->maxTmpNoise[1]= 1500;
            ppMode->maxTmpNoise[2]= 3000;
            ppMode->maxAllowedY= 234;
            ppMode->minAllowedY= 16;
            ppMode->baseDcDiff= 256/4;
            ppMode->flatnessThreshold=40;

            vf->priv->ppMode[i]= ppMode;
        }
    }
#endif

    vf->priv->pp=PP_QUALITY_MAX;
    return 1;
}

const vf_info_t vf_info_pp = {
    "postprocessing",
    "pp",
    "A'rpi",
    "",
    vf_open,
    NULL
};

//===========================================================================//
