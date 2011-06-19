/*
 * Copyright (c) 2011 Michael Niedermayer
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Parts of this file have been stolen from mplayer
 */

/**
 * @file
 */

#include "avfilter.h"
#include "libavutil/avassert.h"
#include "libavutil/pixdesc.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"

#include "libmpcodecs/vf.h"
#include "libmpcodecs/img_format.h"
#include "libmpcodecs/cpudetect.h"
#include "libmpcodecs/vd_ffmpeg.h"
#include "libmpcodecs/vf_scale.h"
#include "libmpcodecs/libvo/fastmemcpy.h"

#include "libswscale/swscale.h"


//FIXME maybe link the orig in
static const struct {
    int fmt;
    enum PixelFormat pix_fmt;
} conversion_map[] = {
    {IMGFMT_ARGB, PIX_FMT_ARGB},
    {IMGFMT_BGRA, PIX_FMT_BGRA},
    {IMGFMT_BGR24, PIX_FMT_BGR24},
    {IMGFMT_BGR16BE, PIX_FMT_RGB565BE},
    {IMGFMT_BGR16LE, PIX_FMT_RGB565LE},
    {IMGFMT_BGR15BE, PIX_FMT_RGB555BE},
    {IMGFMT_BGR15LE, PIX_FMT_RGB555LE},
    {IMGFMT_BGR12BE, PIX_FMT_RGB444BE},
    {IMGFMT_BGR12LE, PIX_FMT_RGB444LE},
    {IMGFMT_BGR8,  PIX_FMT_RGB8},
    {IMGFMT_BGR4,  PIX_FMT_RGB4},
    {IMGFMT_BGR1,  PIX_FMT_MONOBLACK},
    {IMGFMT_RGB1,  PIX_FMT_MONOBLACK},
    {IMGFMT_RG4B,  PIX_FMT_BGR4_BYTE},
    {IMGFMT_BG4B,  PIX_FMT_RGB4_BYTE},
    {IMGFMT_RGB48LE, PIX_FMT_RGB48LE},
    {IMGFMT_RGB48BE, PIX_FMT_RGB48BE},
    {IMGFMT_ABGR, PIX_FMT_ABGR},
    {IMGFMT_RGBA, PIX_FMT_RGBA},
    {IMGFMT_RGB24, PIX_FMT_RGB24},
    {IMGFMT_RGB16BE, PIX_FMT_BGR565BE},
    {IMGFMT_RGB16LE, PIX_FMT_BGR565LE},
    {IMGFMT_RGB15BE, PIX_FMT_BGR555BE},
    {IMGFMT_RGB15LE, PIX_FMT_BGR555LE},
    {IMGFMT_RGB12BE, PIX_FMT_BGR444BE},
    {IMGFMT_RGB12LE, PIX_FMT_BGR444LE},
    {IMGFMT_RGB8,  PIX_FMT_BGR8},
    {IMGFMT_RGB4,  PIX_FMT_BGR4},
    {IMGFMT_BGR8,  PIX_FMT_PAL8},
    {IMGFMT_YUY2,  PIX_FMT_YUYV422},
    {IMGFMT_UYVY,  PIX_FMT_UYVY422},
    {IMGFMT_NV12,  PIX_FMT_NV12},
    {IMGFMT_NV21,  PIX_FMT_NV21},
    {IMGFMT_Y800,  PIX_FMT_GRAY8},
    {IMGFMT_Y8,    PIX_FMT_GRAY8},
    {IMGFMT_YVU9,  PIX_FMT_YUV410P},
    {IMGFMT_IF09,  PIX_FMT_YUV410P},
    {IMGFMT_YV12,  PIX_FMT_YUV420P},
    {IMGFMT_I420,  PIX_FMT_YUV420P},
    {IMGFMT_IYUV,  PIX_FMT_YUV420P},
    {IMGFMT_411P,  PIX_FMT_YUV411P},
    {IMGFMT_422P,  PIX_FMT_YUV422P},
    {IMGFMT_444P,  PIX_FMT_YUV444P},
    {IMGFMT_440P,  PIX_FMT_YUV440P},

    {IMGFMT_420A,  PIX_FMT_YUVA420P},

    {IMGFMT_420P16_LE,  PIX_FMT_YUV420P16LE},
    {IMGFMT_420P16_BE,  PIX_FMT_YUV420P16BE},
    {IMGFMT_422P16_LE,  PIX_FMT_YUV422P16LE},
    {IMGFMT_422P16_BE,  PIX_FMT_YUV422P16BE},
    {IMGFMT_444P16_LE,  PIX_FMT_YUV444P16LE},
    {IMGFMT_444P16_BE,  PIX_FMT_YUV444P16BE},

    // YUVJ are YUV formats that use the full Y range and not just
    // 16 - 235 (see colorspaces.txt).
    // Currently they are all treated the same way.
    {IMGFMT_YV12,  PIX_FMT_YUVJ420P},
    {IMGFMT_422P,  PIX_FMT_YUVJ422P},
    {IMGFMT_444P,  PIX_FMT_YUVJ444P},
    {IMGFMT_440P,  PIX_FMT_YUVJ440P},

    {IMGFMT_XVMC_MOCO_MPEG2, PIX_FMT_XVMC_MPEG2_MC},
    {IMGFMT_XVMC_IDCT_MPEG2, PIX_FMT_XVMC_MPEG2_IDCT},
    {IMGFMT_VDPAU_MPEG1,     PIX_FMT_VDPAU_MPEG1},
    {IMGFMT_VDPAU_MPEG2,     PIX_FMT_VDPAU_MPEG2},
    {IMGFMT_VDPAU_H264,      PIX_FMT_VDPAU_H264},
    {IMGFMT_VDPAU_WMV3,      PIX_FMT_VDPAU_WMV3},
    {IMGFMT_VDPAU_VC1,       PIX_FMT_VDPAU_VC1},
    {IMGFMT_VDPAU_MPEG4,     PIX_FMT_VDPAU_MPEG4},
    {0, PIX_FMT_NONE}
};

//copied from vf.c
extern const vf_info_t vf_info_vo;
extern const vf_info_t vf_info_rectangle;
extern const vf_info_t vf_info_bmovl;
extern const vf_info_t vf_info_crop;
extern const vf_info_t vf_info_expand;
extern const vf_info_t vf_info_pp;
extern const vf_info_t vf_info_scale;
extern const vf_info_t vf_info_format;
extern const vf_info_t vf_info_noformat;
extern const vf_info_t vf_info_flip;
extern const vf_info_t vf_info_rotate;
extern const vf_info_t vf_info_mirror;
extern const vf_info_t vf_info_palette;
extern const vf_info_t vf_info_lavc;
extern const vf_info_t vf_info_zrmjpeg;
extern const vf_info_t vf_info_dvbscale;
extern const vf_info_t vf_info_cropdetect;
extern const vf_info_t vf_info_test;
extern const vf_info_t vf_info_noise;
extern const vf_info_t vf_info_yvu9;
extern const vf_info_t vf_info_lavcdeint;
extern const vf_info_t vf_info_eq;
extern const vf_info_t vf_info_eq2;
extern const vf_info_t vf_info_gradfun;
extern const vf_info_t vf_info_halfpack;
extern const vf_info_t vf_info_dint;
extern const vf_info_t vf_info_1bpp;
extern const vf_info_t vf_info_2xsai;
extern const vf_info_t vf_info_unsharp;
extern const vf_info_t vf_info_swapuv;
extern const vf_info_t vf_info_il;
extern const vf_info_t vf_info_fil;
extern const vf_info_t vf_info_boxblur;
extern const vf_info_t vf_info_sab;
extern const vf_info_t vf_info_smartblur;
extern const vf_info_t vf_info_perspective;
extern const vf_info_t vf_info_down3dright;
extern const vf_info_t vf_info_field;
extern const vf_info_t vf_info_denoise3d;
extern const vf_info_t vf_info_hqdn3d;
extern const vf_info_t vf_info_detc;
extern const vf_info_t vf_info_telecine;
extern const vf_info_t vf_info_tinterlace;
extern const vf_info_t vf_info_tfields;
extern const vf_info_t vf_info_ivtc;
extern const vf_info_t vf_info_ilpack;
extern const vf_info_t vf_info_dsize;
extern const vf_info_t vf_info_decimate;
extern const vf_info_t vf_info_softpulldown;
extern const vf_info_t vf_info_pullup;
extern const vf_info_t vf_info_filmdint;
extern const vf_info_t vf_info_framestep;
extern const vf_info_t vf_info_tile;
extern const vf_info_t vf_info_delogo;
extern const vf_info_t vf_info_remove_logo;
extern const vf_info_t vf_info_hue;
extern const vf_info_t vf_info_spp;
extern const vf_info_t vf_info_uspp;
extern const vf_info_t vf_info_fspp;
extern const vf_info_t vf_info_pp7;
extern const vf_info_t vf_info_yuvcsp;
extern const vf_info_t vf_info_kerndeint;
extern const vf_info_t vf_info_rgbtest;
extern const vf_info_t vf_info_qp;
extern const vf_info_t vf_info_phase;
extern const vf_info_t vf_info_divtc;
extern const vf_info_t vf_info_harddup;
extern const vf_info_t vf_info_softskip;
extern const vf_info_t vf_info_screenshot;
extern const vf_info_t vf_info_ass;
extern const vf_info_t vf_info_mcdeint;
extern const vf_info_t vf_info_yadif;
extern const vf_info_t vf_info_blackframe;
extern const vf_info_t vf_info_geq;
extern const vf_info_t vf_info_ow;
extern const vf_info_t vf_info_fixpts;
extern const vf_info_t vf_info_stereo3d;


static const vf_info_t* const filters[]={
    &vf_info_2xsai,
    &vf_info_blackframe,
    &vf_info_boxblur,
    &vf_info_cropdetect,
    &vf_info_decimate,
    &vf_info_delogo,
    &vf_info_denoise3d,
    &vf_info_detc,
    &vf_info_dint,
    &vf_info_divtc,
    &vf_info_down3dright,
    &vf_info_dsize,
    &vf_info_eq2,
    &vf_info_eq,
    &vf_info_field,
    &vf_info_fil,
//    &vf_info_filmdint, cmmx.h vd.h ‘opt_screen_size_x’
    &vf_info_fixpts,
    &vf_info_framestep,
    &vf_info_fspp,
    &vf_info_geq,
    &vf_info_gradfun,
    &vf_info_harddup,
    &vf_info_hqdn3d,
    &vf_info_hue,
    &vf_info_il,
    &vf_info_ilpack,
    &vf_info_ivtc,
    &vf_info_kerndeint,
    &vf_info_mcdeint,
    &vf_info_mirror,
    &vf_info_noise,
    &vf_info_ow,
    &vf_info_palette,
    &vf_info_perspective,
    &vf_info_phase,
    &vf_info_pp7,
    &vf_info_pullup,
    &vf_info_qp,
    &vf_info_rectangle,
    &vf_info_remove_logo,
    &vf_info_rgbtest,
    &vf_info_rotate,
    &vf_info_sab,
    &vf_info_screenshot,
    &vf_info_smartblur,
    &vf_info_softpulldown,
    &vf_info_softskip,
    &vf_info_spp,
    &vf_info_swapuv,
    &vf_info_telecine,
    &vf_info_test,
    &vf_info_tile,
    &vf_info_tinterlace,
    &vf_info_unsharp,
    &vf_info_uspp,
    &vf_info_yuvcsp,
    &vf_info_yvu9,

    NULL
};

/*
Unsupported filters
1bpp
ass
bmovl
crop
dvbscale
flip
expand
format
halfpack
lavc
lavcdeint
noformat
pp
scale
stereo3d
tfields
vo
yadif
zrmjpeg
*/

CpuCaps gCpuCaps; //FIXME initialize this so optims work


static void sws_getFlagsAndFilterFromCmdLine(int *flags, SwsFilter **srcFilterParam, SwsFilter **dstFilterParam)
{
        static int firstTime=1;
        *flags=0;

#if ARCH_X86
        if(gCpuCaps.hasMMX)
                __asm__ volatile("emms\n\t"::: "memory"); //FIXME this should not be required but it IS (even for non-MMX versions)
#endif
        if(firstTime)
        {
                firstTime=0;
                *flags= SWS_PRINT_INFO;
        }
        else if( mp_msg_test(MSGT_VFILTER,MSGL_DBG2) ) *flags= SWS_PRINT_INFO;

        switch(SWS_BILINEAR)
        {
                case 0: *flags|= SWS_FAST_BILINEAR; break;
                case 1: *flags|= SWS_BILINEAR; break;
                case 2: *flags|= SWS_BICUBIC; break;
                case 3: *flags|= SWS_X; break;
                case 4: *flags|= SWS_POINT; break;
                case 5: *flags|= SWS_AREA; break;
                case 6: *flags|= SWS_BICUBLIN; break;
                case 7: *flags|= SWS_GAUSS; break;
                case 8: *flags|= SWS_SINC; break;
                case 9: *flags|= SWS_LANCZOS; break;
                case 10:*flags|= SWS_SPLINE; break;
                default:*flags|= SWS_BILINEAR; break;
        }

        *srcFilterParam= NULL;
        *dstFilterParam= NULL;
}

//exact copy from vf_scale.c
// will use sws_flags & src_filter (from cmd line)
struct SwsContext *sws_getContextFromCmdLine(int srcW, int srcH, int srcFormat, int dstW, int dstH, int dstFormat)
{
        int flags, i;
        SwsFilter *dstFilterParam, *srcFilterParam;
        enum PixelFormat dfmt, sfmt;

        for(i=0; conversion_map[i].fmt && dstFormat != conversion_map[i].fmt; i++);
        dfmt= conversion_map[i].pix_fmt;
        for(i=0; conversion_map[i].fmt && srcFormat != conversion_map[i].fmt; i++);
        sfmt= conversion_map[i].pix_fmt;

        if (srcFormat == IMGFMT_RGB8 || srcFormat == IMGFMT_BGR8) sfmt = PIX_FMT_PAL8;
        sws_getFlagsAndFilterFromCmdLine(&flags, &srcFilterParam, &dstFilterParam);

        return sws_getContext(srcW, srcH, sfmt, dstW, dstH, dfmt, flags , srcFilterParam, dstFilterParam, NULL);
}

typedef struct {
    vf_instance_t vf;
    vf_instance_t next_vf;
    AVFilterContext *avfctx;
    int frame_returned;
} MPContext;

void mp_msg(int mod, int lev, const char *format, ... ){
    va_list va;
    va_start(va, format);
    //FIXME convert lev/mod
    av_vlog(NULL, AV_LOG_DEBUG, format, va);
    va_end(va);
}

int mp_msg_test(int mod, int lev){
    return 123;
}

void init_avcodec(void)
{
    //we maybe should init but its kinda 1. unneeded 2. a bit inpolite from here
}

//Exact copy of vf.c
void vf_clone_mpi_attributes(mp_image_t* dst, mp_image_t* src){
    dst->pict_type= src->pict_type;
    dst->fields = src->fields;
    dst->qscale_type= src->qscale_type;
    if(dst->width == src->width && dst->height == src->height){
        dst->qstride= src->qstride;
        dst->qscale= src->qscale;
    }
}

//Exact copy of vf.c
void vf_next_draw_slice(struct vf_instance *vf,unsigned char** src, int * stride,int w, int h, int x, int y){
    if (vf->next->draw_slice) {
        vf->next->draw_slice(vf->next,src,stride,w,h,x,y);
        return;
    }
    if (!vf->dmpi) {
        mp_msg(MSGT_VFILTER,MSGL_ERR,"draw_slice: dmpi not stored by vf_%s\n", vf->info->name);
        return;
    }
    if (!(vf->dmpi->flags & MP_IMGFLAG_PLANAR)) {
        memcpy_pic(vf->dmpi->planes[0]+y*vf->dmpi->stride[0]+vf->dmpi->bpp/8*x,
            src[0], vf->dmpi->bpp/8*w, h, vf->dmpi->stride[0], stride[0]);
        return;
    }
    memcpy_pic(vf->dmpi->planes[0]+y*vf->dmpi->stride[0]+x, src[0],
        w, h, vf->dmpi->stride[0], stride[0]);
    memcpy_pic(vf->dmpi->planes[1]+(y>>vf->dmpi->chroma_y_shift)*vf->dmpi->stride[1]+(x>>vf->dmpi->chroma_x_shift),
        src[1], w>>vf->dmpi->chroma_x_shift, h>>vf->dmpi->chroma_y_shift, vf->dmpi->stride[1], stride[1]);
    memcpy_pic(vf->dmpi->planes[2]+(y>>vf->dmpi->chroma_y_shift)*vf->dmpi->stride[2]+(x>>vf->dmpi->chroma_x_shift),
        src[2], w>>vf->dmpi->chroma_x_shift, h>>vf->dmpi->chroma_y_shift, vf->dmpi->stride[2], stride[2]);
}

//Exact copy of vf.c
void vf_mpi_clear(mp_image_t* mpi,int x0,int y0,int w,int h){
    int y;
    if(mpi->flags&MP_IMGFLAG_PLANAR){
        y0&=~1;h+=h&1;
        if(x0==0 && w==mpi->width){
            // full width clear:
            memset(mpi->planes[0]+mpi->stride[0]*y0,0,mpi->stride[0]*h);
            memset(mpi->planes[1]+mpi->stride[1]*(y0>>mpi->chroma_y_shift),128,mpi->stride[1]*(h>>mpi->chroma_y_shift));
            memset(mpi->planes[2]+mpi->stride[2]*(y0>>mpi->chroma_y_shift),128,mpi->stride[2]*(h>>mpi->chroma_y_shift));
        } else
        for(y=y0;y<y0+h;y+=2){
            memset(mpi->planes[0]+x0+mpi->stride[0]*y,0,w);
            memset(mpi->planes[0]+x0+mpi->stride[0]*(y+1),0,w);
            memset(mpi->planes[1]+(x0>>mpi->chroma_x_shift)+mpi->stride[1]*(y>>mpi->chroma_y_shift),128,(w>>mpi->chroma_x_shift));
            memset(mpi->planes[2]+(x0>>mpi->chroma_x_shift)+mpi->stride[2]*(y>>mpi->chroma_y_shift),128,(w>>mpi->chroma_x_shift));
        }
        return;
    }
    // packed:
    for(y=y0;y<y0+h;y++){
        unsigned char* dst=mpi->planes[0]+mpi->stride[0]*y+(mpi->bpp>>3)*x0;
        if(mpi->flags&MP_IMGFLAG_YUV){
            unsigned int* p=(unsigned int*) dst;
            int size=(mpi->bpp>>3)*w/4;
            int i;
#if HAVE_BIGENDIAN
#define CLEAR_PACKEDYUV_PATTERN 0x00800080
#define CLEAR_PACKEDYUV_PATTERN_SWAPPED 0x80008000
#else
#define CLEAR_PACKEDYUV_PATTERN 0x80008000
#define CLEAR_PACKEDYUV_PATTERN_SWAPPED 0x00800080
#endif
            if(mpi->flags&MP_IMGFLAG_SWAPPED){
                for(i=0;i<size-3;i+=4) p[i]=p[i+1]=p[i+2]=p[i+3]=CLEAR_PACKEDYUV_PATTERN_SWAPPED;
                for(;i<size;i++) p[i]=CLEAR_PACKEDYUV_PATTERN_SWAPPED;
            } else {
                for(i=0;i<size-3;i+=4) p[i]=p[i+1]=p[i+2]=p[i+3]=CLEAR_PACKEDYUV_PATTERN;
                for(;i<size;i++) p[i]=CLEAR_PACKEDYUV_PATTERN;
            }
        } else
            memset(dst,0,(mpi->bpp>>3)*w);
    }
}

int vf_next_query_format(struct vf_instance *vf, unsigned int fmt){
    return 1;
}

//used by delogo
unsigned int vf_match_csp(vf_instance_t** vfp,const unsigned int* list,unsigned int preferred){
    return preferred;
}

mp_image_t* vf_get_image(vf_instance_t* vf, unsigned int outfmt, int mp_imgtype, int mp_imgflag, int w, int h){
    MPContext *m= ((uint8_t*)vf) - offsetof(MPContext, next_vf);
  mp_image_t* mpi=NULL;
  int w2;
  int number = mp_imgtype >> 16;

  av_assert0(vf->next == NULL); // all existing filters call this just on next

  //vf_dint needs these as it calls vf_get_image() before configuring the output
  if(vf->w==0 && w>0) vf->w=w;
  if(vf->h==0 && h>0) vf->h=h;

  av_assert0(w == -1 || w >= vf->w);
  av_assert0(h == -1 || h >= vf->h);
  av_assert0(vf->w > 0);
  av_assert0(vf->h > 0);

  av_log(m->avfctx, AV_LOG_DEBUG, "get_image: %d:%d, vf: %d:%d\n", w,h,vf->w,vf->h);

  if (w == -1) w = vf->w;
  if (h == -1) h = vf->h;

  w2=(mp_imgflag&MP_IMGFLAG_ACCEPT_ALIGNED_STRIDE)?((w+15)&(~15)):w;

  // Note: we should call libvo first to check if it supports direct rendering
  // and if not, then fallback to software buffers:
  switch(mp_imgtype & 0xff){
  case MP_IMGTYPE_EXPORT:
    if(!vf->imgctx.export_images[0]) vf->imgctx.export_images[0]=new_mp_image(w2,h);
    mpi=vf->imgctx.export_images[0];
    break;
  case MP_IMGTYPE_STATIC:
    if(!vf->imgctx.static_images[0]) vf->imgctx.static_images[0]=new_mp_image(w2,h);
    mpi=vf->imgctx.static_images[0];
    break;
  case MP_IMGTYPE_TEMP:
    if(!vf->imgctx.temp_images[0]) vf->imgctx.temp_images[0]=new_mp_image(w2,h);
    mpi=vf->imgctx.temp_images[0];
    break;
  case MP_IMGTYPE_IPB:
    if(!(mp_imgflag&MP_IMGFLAG_READABLE)){ // B frame:
      if(!vf->imgctx.temp_images[0]) vf->imgctx.temp_images[0]=new_mp_image(w2,h);
      mpi=vf->imgctx.temp_images[0];
      break;
    }
  case MP_IMGTYPE_IP:
    if(!vf->imgctx.static_images[vf->imgctx.static_idx]) vf->imgctx.static_images[vf->imgctx.static_idx]=new_mp_image(w2,h);
    mpi=vf->imgctx.static_images[vf->imgctx.static_idx];
    vf->imgctx.static_idx^=1;
    break;
  case MP_IMGTYPE_NUMBERED:
    if (number == -1) {
      int i;
      for (i = 0; i < NUM_NUMBERED_MPI; i++)
        if (!vf->imgctx.numbered_images[i] || !vf->imgctx.numbered_images[i]->usage_count)
          break;
      number = i;
    }
    if (number < 0 || number >= NUM_NUMBERED_MPI) return NULL;
    if (!vf->imgctx.numbered_images[number]) vf->imgctx.numbered_images[number] = new_mp_image(w2,h);
    mpi = vf->imgctx.numbered_images[number];
    mpi->number = number;
    break;
  }
  if(mpi){
    mpi->type=mp_imgtype;
    mpi->w=vf->w; mpi->h=vf->h;
    // keep buffer allocation status & color flags only:
//    mpi->flags&=~(MP_IMGFLAG_PRESERVE|MP_IMGFLAG_READABLE|MP_IMGFLAG_DIRECT);
    mpi->flags&=MP_IMGFLAG_ALLOCATED|MP_IMGFLAG_TYPE_DISPLAYED|MP_IMGFLAGMASK_COLORS;
    // accept restrictions, draw_slice and palette flags only:
    mpi->flags|=mp_imgflag&(MP_IMGFLAGMASK_RESTRICTIONS|MP_IMGFLAG_DRAW_CALLBACK|MP_IMGFLAG_RGB_PALETTE);
    if(!vf->draw_slice) mpi->flags&=~MP_IMGFLAG_DRAW_CALLBACK;
    if(mpi->width!=w2 || mpi->height!=h){
//      printf("vf.c: MPI parameters changed!  %dx%d -> %dx%d   \n", mpi->width,mpi->height,w2,h);
        if(mpi->flags&MP_IMGFLAG_ALLOCATED){
            if(mpi->width<w2 || mpi->height<h){
                // need to re-allocate buffer memory:
                av_free(mpi->planes[0]);
                mpi->flags&=~MP_IMGFLAG_ALLOCATED;
                mp_msg(MSGT_VFILTER,MSGL_V,"vf.c: have to REALLOCATE buffer memory :(\n");
            }
//      } else {
        } {
            mpi->width=w2; mpi->chroma_width=(w2 + (1<<mpi->chroma_x_shift) - 1)>>mpi->chroma_x_shift;
            mpi->height=h; mpi->chroma_height=(h + (1<<mpi->chroma_y_shift) - 1)>>mpi->chroma_y_shift;
        }
    }
    if(!mpi->bpp) mp_image_setfmt(mpi,outfmt);
    if(!(mpi->flags&MP_IMGFLAG_ALLOCATED) && mpi->type>MP_IMGTYPE_EXPORT){

        av_assert0(!vf->get_image);
        // check libvo first!
        if(vf->get_image) vf->get_image(vf,mpi);

        if(!(mpi->flags&MP_IMGFLAG_DIRECT)){
          // non-direct and not yet allocated image. allocate it!
          if (!mpi->bpp) { // no way we can allocate this
              mp_msg(MSGT_DECVIDEO, MSGL_FATAL,
                     "vf_get_image: Tried to allocate a format that can not be allocated!\n");
              return NULL;
          }

          // check if codec prefer aligned stride:
          if(mp_imgflag&MP_IMGFLAG_PREFER_ALIGNED_STRIDE){
              int align=(mpi->flags&MP_IMGFLAG_PLANAR &&
                         mpi->flags&MP_IMGFLAG_YUV) ?
                         (8<<mpi->chroma_x_shift)-1 : 15; // -- maybe FIXME
              w2=((w+align)&(~align));
              if(mpi->width!=w2){
#if 0
                  // we have to change width... check if we CAN co it:
                  int flags=vf->query_format(vf,outfmt); // should not fail
                  if(!(flags&3)) mp_msg(MSGT_DECVIDEO,MSGL_WARN,"??? vf_get_image{vf->query_format(outfmt)} failed!\n");
//                printf("query -> 0x%X    \n",flags);
                  if(flags&VFCAP_ACCEPT_STRIDE){
#endif
                      mpi->width=w2;
                      mpi->chroma_width=(w2 + (1<<mpi->chroma_x_shift) - 1)>>mpi->chroma_x_shift;
//                  }
              }
          }

          mp_image_alloc_planes(mpi);
//        printf("clearing img!\n");
          vf_mpi_clear(mpi,0,0,mpi->width,mpi->height);
        }
    }
    av_assert0(!vf->start_slice);
    if(mpi->flags&MP_IMGFLAG_DRAW_CALLBACK)
        if(vf->start_slice) vf->start_slice(vf,mpi);
    if(!(mpi->flags&MP_IMGFLAG_TYPE_DISPLAYED)){
            mp_msg(MSGT_DECVIDEO,MSGL_V,"*** [%s] %s%s mp_image_t, %dx%dx%dbpp %s %s, %d bytes\n",
                  "NULL"/*vf->info->name*/,
                  (mpi->type==MP_IMGTYPE_EXPORT)?"Exporting":
                  ((mpi->flags&MP_IMGFLAG_DIRECT)?"Direct Rendering":"Allocating"),
                  (mpi->flags&MP_IMGFLAG_DRAW_CALLBACK)?" (slices)":"",
                  mpi->width,mpi->height,mpi->bpp,
                  (mpi->flags&MP_IMGFLAG_YUV)?"YUV":((mpi->flags&MP_IMGFLAG_SWAPPED)?"BGR":"RGB"),
                  (mpi->flags&MP_IMGFLAG_PLANAR)?"planar":"packed",
                  mpi->bpp*mpi->width*mpi->height/8);
            mp_msg(MSGT_DECVIDEO,MSGL_DBG2,"(imgfmt: %x, planes: %p,%p,%p strides: %d,%d,%d, chroma: %dx%d, shift: h:%d,v:%d)\n",
                mpi->imgfmt, mpi->planes[0], mpi->planes[1], mpi->planes[2],
                mpi->stride[0], mpi->stride[1], mpi->stride[2],
                mpi->chroma_width, mpi->chroma_height, mpi->chroma_x_shift, mpi->chroma_y_shift);
            mpi->flags|=MP_IMGFLAG_TYPE_DISPLAYED;
    }

  mpi->qscale = NULL;
  }
  mpi->usage_count++;
//    printf("\rVF_MPI: %p %p %p %d %d %d    \n",
//      mpi->planes[0],mpi->planes[1],mpi->planes[2],
//      mpi->stride[0],mpi->stride[1],mpi->stride[2]);
  return mpi;
}


int vf_next_put_image(struct vf_instance *vf,mp_image_t *mpi, double pts){
    MPContext *m= (void*)vf;
    AVFilterLink *outlink     = m->avfctx->outputs[0];
    AVFilterBuffer    *pic    = av_mallocz(sizeof(AVFilterBuffer));
    AVFilterBufferRef *picref = av_mallocz(sizeof(AVFilterBufferRef));
    int i;

    av_assert0(vf->next);

    av_log(m->avfctx, AV_LOG_DEBUG, "vf_next_put_image\n");

    if (!pic || !picref)
        goto fail;

    picref->buf = pic;
    picref->buf->please_use_av_free= av_free;
    if (!(picref->video = av_mallocz(sizeof(AVFilterBufferRefVideoProps))))
        goto fail;

    pic->w = picref->video->w = mpi->w;
    pic->h = picref->video->h = mpi->h;

    /* make sure the buffer gets read permission or it's useless for output */
    picref->perms = AV_PERM_READ | AV_PERM_REUSE2;
//    av_assert0(mpi->flags&MP_IMGFLAG_READABLE);
    if(!(mpi->flags&MP_IMGFLAG_PRESERVE))
        picref->perms |= AV_PERM_WRITE;

    pic->refcount = 1;
    picref->type = AVMEDIA_TYPE_VIDEO;

    for(i=0; conversion_map[i].fmt && mpi->imgfmt != conversion_map[i].fmt; i++);
    pic->format = picref->format = conversion_map[i].pix_fmt;

    memcpy(pic->data,        mpi->planes,   FFMIN(sizeof(pic->data)    , sizeof(mpi->planes)));
    memcpy(pic->linesize,    mpi->stride,   FFMIN(sizeof(pic->linesize), sizeof(mpi->stride)));
    memcpy(picref->data,     pic->data,     sizeof(picref->data));
    memcpy(picref->linesize, pic->linesize, sizeof(picref->linesize));

    if(pts != MP_NOPTS_VALUE)
        picref->pts= pts * av_q2d(outlink->time_base);

    avfilter_start_frame(outlink, avfilter_ref_buffer(picref, ~0));
    avfilter_draw_slice(outlink, 0, picref->video->h, 1);
    avfilter_end_frame(outlink);
    avfilter_unref_buffer(picref);
    m->frame_returned++;

    return 1;
fail:
    if (picref && picref->video)
        av_free(picref->video);
    av_free(picref);
    av_free(pic);
    return 0;
}

int vf_next_config(struct vf_instance *vf,
        int width, int height, int d_width, int d_height,
        unsigned int voflags, unsigned int outfmt){

    av_assert0(width>0 && height>0);
    vf->next->w = width; vf->next->h = height;

    return 1;
#if 0
    int flags=vf->next->query_format(vf->next,outfmt);
    if(!flags){
        // hmm. colorspace mismatch!!!
        //this is fatal for us ATM
        return 0;
    }
    mp_msg(MSGT_VFILTER,MSGL_V,"REQ: flags=0x%X  req=0x%X  \n",flags,vf->default_reqs);
    miss=vf->default_reqs - (flags&vf->default_reqs);
    if(miss&VFCAP_ACCEPT_STRIDE){
        // vf requires stride support but vf->next doesn't support it!
        // let's insert the 'expand' filter, it does the job for us:
        vf_instance_t* vf2=vf_open_filter(vf->next,"expand",NULL);
        if(!vf2) return 0; // shouldn't happen!
        vf->next=vf2;
    }
    vf->next->w = width; vf->next->h = height;
#endif
    return 1;
}

int vf_next_control(struct vf_instance *vf, int request, void* data){
    MPContext *m= (void*)vf;
    av_log(m->avfctx, AV_LOG_DEBUG, "Received control %d\n", request);
    return 0;
}

static int vf_default_query_format(struct vf_instance *vf, unsigned int fmt){
    MPContext *m= (void*)vf;
    int i;
    av_log(m->avfctx, AV_LOG_DEBUG, "query %X\n", fmt);

    for(i=0; conversion_map[i].fmt; i++){
        if(fmt==conversion_map[i].fmt)
            return 1; //we suport all
    }
    return 0;
}


static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    MPContext *m = ctx->priv;
    char name[256];
    int i;

    av_log(ctx, AV_LOG_WARNING,
"This is a unholy filter, it will be purified by the ffmpeg exorcist team\n"
"which will change its syntax from dark -vf mp to light -vf.\n"
"Thou shalst not make spells or scripts that depend on it\n");

    m->avfctx= ctx;

    if(!args || 1!=sscanf(args, "%255[^:=]", name)){
        av_log(ctx, AV_LOG_ERROR, "Invalid parameter.\n");
        return AVERROR(EINVAL);
    }
    args+= strlen(name)+1;

    for(i=0; ;i++){
        if(!filters[i] || !strcmp(name, filters[i]->name))
            break;
    }

    if(!filters[i]){
        av_log(ctx, AV_LOG_ERROR, "Unknown filter %s\n", name);
        return AVERROR(EINVAL);
    }

    memset(&m->vf,0,sizeof(m->vf));
    m->vf.info= filters[i];

    m->vf.next        = &m->next_vf;
    m->vf.put_image   = vf_next_put_image;
    m->vf.config      = vf_next_config;
    m->vf.query_format= vf_default_query_format;
    m->vf.control     = vf_next_control;
    m->vf.default_caps=VFCAP_ACCEPT_STRIDE;
    m->vf.default_reqs=0;
    if(m->vf.info->opts)
        av_log(ctx, AV_LOG_ERROR, "opts / m_struct_set is unsupported\n");
#if 0
    if(vf->info->opts) { // vf_vo get some special argument
      const m_struct_t* st = vf->info->opts;
      void* vf_priv = m_struct_alloc(st);
      int n;
      for(n = 0 ; args && args[2*n] ; n++)
        m_struct_set(st,vf_priv,args[2*n],args[2*n+1]);
      vf->priv = vf_priv;
      args = NULL;
    } else // Otherwise we should have the '_oldargs_'
      if(args && !strcmp(args[0],"_oldargs_"))
        args = (char**)args[1];
      else
        args = NULL;
#endif
    if(m->vf.info->vf_open(&m->vf, args)<=0){
        av_log(ctx, AV_LOG_ERROR, "vf_open() of %s with arg=%s failed\n", name, args);
        return -1;
    }

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *avfmts=NULL;
    MPContext *m = ctx->priv;
    int i;

    for(i=0; conversion_map[i].fmt; i++){
        av_log(ctx, AV_LOG_DEBUG, "query: %X\n", conversion_map[i].fmt);
        if(m->vf.query_format(&m->vf, conversion_map[i].fmt)){
            av_log(ctx, AV_LOG_DEBUG, "supported,adding\n");
            avfilter_add_format(&avfmts, conversion_map[i].pix_fmt);
        }
    }

    //We assume all allowed input formats are also allowed output formats
    avfilter_set_common_pixel_formats(ctx, avfmts);
    return 0;
}

static int config_inprops(AVFilterLink *inlink)
{
    MPContext *m = inlink->dst->priv;
    int i;
    for(i=0; conversion_map[i].fmt && conversion_map[i].pix_fmt != inlink->format; i++);

    av_assert0(conversion_map[i].fmt && inlink->w && inlink->h);

    m->vf.fmt.have_configured = 1;
    m->vf.fmt.orig_height     = inlink->h;
    m->vf.fmt.orig_width      = inlink->w;
    m->vf.fmt.orig_fmt        = conversion_map[i].fmt;

    if(m->vf.config(&m->vf, inlink->w, inlink->h, inlink->w, inlink->h, 0, conversion_map[i].fmt)<=0)
        return -1;

    return 0;
}

static int config_outprops(AVFilterLink *outlink)
{
    MPContext *m = outlink->src->priv;

    outlink->w = m->next_vf.w;
    outlink->h = m->next_vf.h;

    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    MPContext *m = outlink->src->priv;
    int ret;

    av_log(m->avfctx, AV_LOG_DEBUG, "mp request_frame\n");

    for(m->frame_returned=0; !m->frame_returned;){
        ret=avfilter_request_frame(outlink->src->inputs[0]);
        if(ret<0)
            break;
    }

    av_log(m->avfctx, AV_LOG_DEBUG, "mp request_frame ret=%d\n", ret);
    return ret;
}

static void start_frame(AVFilterLink *inlink, AVFilterBufferRef *picref)
{
}

static void null_draw_slice(AVFilterLink *link, int y, int h, int slice_dir)
{
}

static void end_frame(AVFilterLink *inlink)
{
    MPContext *m = inlink->dst->priv;
    AVFilterBufferRef *inpic  = inlink->cur_buf;
    int i;
    double pts= MP_NOPTS_VALUE;
    mp_image_t* mpi = new_mp_image(inpic->video->w, inpic->video->h);

    if(inpic->pts != AV_NOPTS_VALUE)
        pts= inpic->pts / av_q2d(inlink->time_base);

    for(i=0; conversion_map[i].fmt && conversion_map[i].pix_fmt != inlink->format; i++);
    mp_image_setfmt(mpi,conversion_map[i].fmt);

    memcpy(mpi->planes, inpic->data,     FFMIN(sizeof(inpic->data)    , sizeof(mpi->planes)));
    memcpy(mpi->stride, inpic->linesize, FFMIN(sizeof(inpic->linesize), sizeof(mpi->stride)));

    //FIXME pass interleced & tff flags around

    // mpi->flags|=MP_IMGFLAG_ALLOCATED; ?
    mpi->flags |= MP_IMGFLAG_READABLE;
    if(!(inpic->perms & AV_PERM_WRITE))
        mpi->flags |= MP_IMGFLAG_PRESERVE;
    if(m->vf.put_image(&m->vf, mpi, pts) == 0){
        av_log(m->avfctx, AV_LOG_DEBUG, "put_image() says skip\n");
    }
    free_mp_image(mpi);

    avfilter_unref_buffer(inpic);
}

AVFilter avfilter_vf_mp = {
    .name      = "mp",
    .description = NULL_IF_CONFIG_SMALL("libmpcodecs wrapper."),
    .init = init,
    .priv_size = sizeof(MPContext),
    .query_formats = query_formats,

    .inputs    = (AVFilterPad[]) {{ .name            = "default",
                                    .type            = AVMEDIA_TYPE_VIDEO,
                                    .start_frame     = start_frame,
                                    .draw_slice      = null_draw_slice,
                                    .end_frame       = end_frame,
                                    .config_props    = config_inprops,
                                    .min_perms       = AV_PERM_READ, },
                                  { .name = NULL}},
    .outputs   = (AVFilterPad[]) {{ .name            = "default",
                                    .type            = AVMEDIA_TYPE_VIDEO,
                                    .request_frame   = request_frame,
                                    .config_props    = config_outprops, },
                                  { .name = NULL}},
};
