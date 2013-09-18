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
#include "video.h"
#include "formats.h"
#include "internal.h"
#include "libavutil/avassert.h"
#include "libavutil/pixdesc.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"

#include "libmpcodecs/vf.h"
#include "libmpcodecs/img_format.h"
#include "libmpcodecs/cpudetect.h"
#include "libmpcodecs/av_helpers.h"
#include "libmpcodecs/libvo/fastmemcpy.h"

#include "libswscale/swscale.h"


//FIXME maybe link the orig in
//XXX: identical pix_fmt must be following with each others
static const struct {
    int fmt;
    enum AVPixelFormat pix_fmt;
} conversion_map[] = {
    {IMGFMT_ARGB, AV_PIX_FMT_ARGB},
    {IMGFMT_BGRA, AV_PIX_FMT_BGRA},
    {IMGFMT_BGR24, AV_PIX_FMT_BGR24},
    {IMGFMT_BGR16BE, AV_PIX_FMT_RGB565BE},
    {IMGFMT_BGR16LE, AV_PIX_FMT_RGB565LE},
    {IMGFMT_BGR15BE, AV_PIX_FMT_RGB555BE},
    {IMGFMT_BGR15LE, AV_PIX_FMT_RGB555LE},
    {IMGFMT_BGR12BE, AV_PIX_FMT_RGB444BE},
    {IMGFMT_BGR12LE, AV_PIX_FMT_RGB444LE},
    {IMGFMT_BGR8,  AV_PIX_FMT_RGB8},
    {IMGFMT_BGR4,  AV_PIX_FMT_RGB4},
    {IMGFMT_BGR1,  AV_PIX_FMT_MONOBLACK},
    {IMGFMT_RGB1,  AV_PIX_FMT_MONOBLACK},
    {IMGFMT_RG4B,  AV_PIX_FMT_BGR4_BYTE},
    {IMGFMT_BG4B,  AV_PIX_FMT_RGB4_BYTE},
    {IMGFMT_RGB48LE, AV_PIX_FMT_RGB48LE},
    {IMGFMT_RGB48BE, AV_PIX_FMT_RGB48BE},
    {IMGFMT_ABGR, AV_PIX_FMT_ABGR},
    {IMGFMT_RGBA, AV_PIX_FMT_RGBA},
    {IMGFMT_RGB24, AV_PIX_FMT_RGB24},
    {IMGFMT_RGB16BE, AV_PIX_FMT_BGR565BE},
    {IMGFMT_RGB16LE, AV_PIX_FMT_BGR565LE},
    {IMGFMT_RGB15BE, AV_PIX_FMT_BGR555BE},
    {IMGFMT_RGB15LE, AV_PIX_FMT_BGR555LE},
    {IMGFMT_RGB12BE, AV_PIX_FMT_BGR444BE},
    {IMGFMT_RGB12LE, AV_PIX_FMT_BGR444LE},
    {IMGFMT_RGB8,  AV_PIX_FMT_BGR8},
    {IMGFMT_RGB4,  AV_PIX_FMT_BGR4},
    {IMGFMT_BGR8,  AV_PIX_FMT_PAL8},
    {IMGFMT_YUY2,  AV_PIX_FMT_YUYV422},
    {IMGFMT_UYVY,  AV_PIX_FMT_UYVY422},
    {IMGFMT_NV12,  AV_PIX_FMT_NV12},
    {IMGFMT_NV21,  AV_PIX_FMT_NV21},
    {IMGFMT_Y800,  AV_PIX_FMT_GRAY8},
    {IMGFMT_Y8,    AV_PIX_FMT_GRAY8},
    {IMGFMT_YVU9,  AV_PIX_FMT_YUV410P},
    {IMGFMT_IF09,  AV_PIX_FMT_YUV410P},
    {IMGFMT_YV12,  AV_PIX_FMT_YUV420P},
    {IMGFMT_I420,  AV_PIX_FMT_YUV420P},
    {IMGFMT_IYUV,  AV_PIX_FMT_YUV420P},
    {IMGFMT_411P,  AV_PIX_FMT_YUV411P},
    {IMGFMT_422P,  AV_PIX_FMT_YUV422P},
    {IMGFMT_444P,  AV_PIX_FMT_YUV444P},
    {IMGFMT_440P,  AV_PIX_FMT_YUV440P},

    {IMGFMT_420A,  AV_PIX_FMT_YUVA420P},

    {IMGFMT_420P16_LE,  AV_PIX_FMT_YUV420P16LE},
    {IMGFMT_420P16_BE,  AV_PIX_FMT_YUV420P16BE},
    {IMGFMT_422P16_LE,  AV_PIX_FMT_YUV422P16LE},
    {IMGFMT_422P16_BE,  AV_PIX_FMT_YUV422P16BE},
    {IMGFMT_444P16_LE,  AV_PIX_FMT_YUV444P16LE},
    {IMGFMT_444P16_BE,  AV_PIX_FMT_YUV444P16BE},

    // YUVJ are YUV formats that use the full Y range and not just
    // 16 - 235 (see colorspaces.txt).
    // Currently they are all treated the same way.
    {IMGFMT_YV12,  AV_PIX_FMT_YUVJ420P},
    {IMGFMT_422P,  AV_PIX_FMT_YUVJ422P},
    {IMGFMT_444P,  AV_PIX_FMT_YUVJ444P},
    {IMGFMT_440P,  AV_PIX_FMT_YUVJ440P},

    {IMGFMT_XVMC_MOCO_MPEG2, AV_PIX_FMT_XVMC_MPEG2_MC},
    {IMGFMT_XVMC_IDCT_MPEG2, AV_PIX_FMT_XVMC_MPEG2_IDCT},
    {IMGFMT_VDPAU_MPEG1,     AV_PIX_FMT_VDPAU_MPEG1},
    {IMGFMT_VDPAU_MPEG2,     AV_PIX_FMT_VDPAU_MPEG2},
    {IMGFMT_VDPAU_H264,      AV_PIX_FMT_VDPAU_H264},
    {IMGFMT_VDPAU_WMV3,      AV_PIX_FMT_VDPAU_WMV3},
    {IMGFMT_VDPAU_VC1,       AV_PIX_FMT_VDPAU_VC1},
    {IMGFMT_VDPAU_MPEG4,     AV_PIX_FMT_VDPAU_MPEG4},
    {0, AV_PIX_FMT_NONE}
};

extern const vf_info_t ff_vf_info_eq2;
extern const vf_info_t ff_vf_info_eq;
extern const vf_info_t ff_vf_info_fspp;
extern const vf_info_t ff_vf_info_ilpack;
extern const vf_info_t ff_vf_info_pp7;
extern const vf_info_t ff_vf_info_softpulldown;
extern const vf_info_t ff_vf_info_uspp;


static const vf_info_t* const filters[]={
    &ff_vf_info_eq2,
    &ff_vf_info_eq,
    &ff_vf_info_fspp,
    &ff_vf_info_ilpack,
    &ff_vf_info_pp7,
    &ff_vf_info_softpulldown,
    &ff_vf_info_uspp,

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
tfields
vo
yadif
zrmjpeg
*/

CpuCaps ff_gCpuCaps; //FIXME initialize this so optims work

enum AVPixelFormat ff_mp2ff_pix_fmt(int mp){
    int i;
    for(i=0; conversion_map[i].fmt && mp != conversion_map[i].fmt; i++)
        ;
    return mp == conversion_map[i].fmt ? conversion_map[i].pix_fmt : AV_PIX_FMT_NONE;
}

typedef struct {
    const AVClass *class;
    vf_instance_t vf;
    vf_instance_t next_vf;
    AVFilterContext *avfctx;
    int frame_returned;
    char *filter;
    enum AVPixelFormat in_pix_fmt;
} MPContext;

#define OFFSET(x) offsetof(MPContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption mp_options[] = {
    { "filter", "set MPlayer filter name and parameters", OFFSET(filter), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(mp);

void ff_mp_msg(int mod, int lev, const char *format, ... ){
    va_list va;
    va_start(va, format);
    //FIXME convert lev/mod
    av_vlog(NULL, AV_LOG_DEBUG, format, va);
    va_end(va);
}

int ff_mp_msg_test(int mod, int lev){
    return 123;
}

void ff_init_avcodec(void)
{
    //we maybe should init but its kinda 1. unneeded 2. a bit inpolite from here
}

//Exact copy of vf.c
void ff_vf_clone_mpi_attributes(mp_image_t* dst, mp_image_t* src){
    dst->pict_type= src->pict_type;
    dst->fields = src->fields;
    dst->qscale_type= src->qscale_type;
    if(dst->width == src->width && dst->height == src->height){
        dst->qstride= src->qstride;
        dst->qscale= src->qscale;
    }
}

//Exact copy of vf.c
void ff_vf_next_draw_slice(struct vf_instance *vf,unsigned char** src, int * stride,int w, int h, int x, int y){
    if (vf->next->draw_slice) {
        vf->next->draw_slice(vf->next,src,stride,w,h,x,y);
        return;
    }
    if (!vf->dmpi) {
        ff_mp_msg(MSGT_VFILTER,MSGL_ERR,"draw_slice: dmpi not stored by vf_%s\n", vf->info->name);
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
void ff_vf_mpi_clear(mp_image_t* mpi,int x0,int y0,int w,int h){
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

int ff_vf_next_query_format(struct vf_instance *vf, unsigned int fmt){
    return 1;
}

//used by delogo
unsigned int ff_vf_match_csp(vf_instance_t** vfp,const unsigned int* list,unsigned int preferred){
    return preferred;
}

mp_image_t* ff_vf_get_image(vf_instance_t* vf, unsigned int outfmt, int mp_imgtype, int mp_imgflag, int w, int h){
    MPContext *m= (MPContext*)(((uint8_t*)vf) - offsetof(MPContext, next_vf));
  mp_image_t* mpi=NULL;
  int w2;
  int number = mp_imgtype >> 16;

  av_assert0(vf->next == NULL); // all existing filters call this just on next

  //vf_dint needs these as it calls ff_vf_get_image() before configuring the output
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
    if(!vf->imgctx.export_images[0]) vf->imgctx.export_images[0]=ff_new_mp_image(w2,h);
    mpi=vf->imgctx.export_images[0];
    break;
  case MP_IMGTYPE_STATIC:
    if(!vf->imgctx.static_images[0]) vf->imgctx.static_images[0]=ff_new_mp_image(w2,h);
    mpi=vf->imgctx.static_images[0];
    break;
  case MP_IMGTYPE_TEMP:
    if(!vf->imgctx.temp_images[0]) vf->imgctx.temp_images[0]=ff_new_mp_image(w2,h);
    mpi=vf->imgctx.temp_images[0];
    break;
  case MP_IMGTYPE_IPB:
    if(!(mp_imgflag&MP_IMGFLAG_READABLE)){ // B frame:
      if(!vf->imgctx.temp_images[0]) vf->imgctx.temp_images[0]=ff_new_mp_image(w2,h);
      mpi=vf->imgctx.temp_images[0];
      break;
    }
  case MP_IMGTYPE_IP:
    if(!vf->imgctx.static_images[vf->imgctx.static_idx]) vf->imgctx.static_images[vf->imgctx.static_idx]=ff_new_mp_image(w2,h);
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
    if (!vf->imgctx.numbered_images[number]) vf->imgctx.numbered_images[number] = ff_new_mp_image(w2,h);
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
                ff_mp_msg(MSGT_VFILTER,MSGL_V,"vf.c: have to REALLOCATE buffer memory :(\n");
            }
//      } else {
        } {
            mpi->width=w2; mpi->chroma_width=(w2 + (1<<mpi->chroma_x_shift) - 1)>>mpi->chroma_x_shift;
            mpi->height=h; mpi->chroma_height=(h + (1<<mpi->chroma_y_shift) - 1)>>mpi->chroma_y_shift;
        }
    }
    if(!mpi->bpp) ff_mp_image_setfmt(mpi,outfmt);
    if(!(mpi->flags&MP_IMGFLAG_ALLOCATED) && mpi->type>MP_IMGTYPE_EXPORT){

        av_assert0(!vf->get_image);
        // check libvo first!
        if(vf->get_image) vf->get_image(vf,mpi);

        if(!(mpi->flags&MP_IMGFLAG_DIRECT)){
          // non-direct and not yet allocated image. allocate it!
          if (!mpi->bpp) { // no way we can allocate this
              ff_mp_msg(MSGT_DECVIDEO, MSGL_FATAL,
                     "ff_vf_get_image: Tried to allocate a format that can not be allocated!\n");
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
                  if(!(flags&3)) ff_mp_msg(MSGT_DECVIDEO,MSGL_WARN,"??? ff_vf_get_image{vf->query_format(outfmt)} failed!\n");
//                printf("query -> 0x%X    \n",flags);
                  if(flags&VFCAP_ACCEPT_STRIDE){
#endif
                      mpi->width=w2;
                      mpi->chroma_width=(w2 + (1<<mpi->chroma_x_shift) - 1)>>mpi->chroma_x_shift;
//                  }
              }
          }

          ff_mp_image_alloc_planes(mpi);
//        printf("clearing img!\n");
          ff_vf_mpi_clear(mpi,0,0,mpi->width,mpi->height);
        }
    }
    av_assert0(!vf->start_slice);
    if(mpi->flags&MP_IMGFLAG_DRAW_CALLBACK)
        if(vf->start_slice) vf->start_slice(vf,mpi);
    if(!(mpi->flags&MP_IMGFLAG_TYPE_DISPLAYED)){
            ff_mp_msg(MSGT_DECVIDEO,MSGL_V,"*** [%s] %s%s mp_image_t, %dx%dx%dbpp %s %s, %d bytes\n",
                  "NULL"/*vf->info->name*/,
                  (mpi->type==MP_IMGTYPE_EXPORT)?"Exporting":
                  ((mpi->flags&MP_IMGFLAG_DIRECT)?"Direct Rendering":"Allocating"),
                  (mpi->flags&MP_IMGFLAG_DRAW_CALLBACK)?" (slices)":"",
                  mpi->width,mpi->height,mpi->bpp,
                  (mpi->flags&MP_IMGFLAG_YUV)?"YUV":((mpi->flags&MP_IMGFLAG_SWAPPED)?"BGR":"RGB"),
                  (mpi->flags&MP_IMGFLAG_PLANAR)?"planar":"packed",
                  mpi->bpp*mpi->width*mpi->height/8);
            ff_mp_msg(MSGT_DECVIDEO,MSGL_DBG2,"(imgfmt: %x, planes: %p,%p,%p strides: %d,%d,%d, chroma: %dx%d, shift: h:%d,v:%d)\n",
                mpi->imgfmt, mpi->planes[0], mpi->planes[1], mpi->planes[2],
                mpi->stride[0], mpi->stride[1], mpi->stride[2],
                mpi->chroma_width, mpi->chroma_height, mpi->chroma_x_shift, mpi->chroma_y_shift);
            mpi->flags|=MP_IMGFLAG_TYPE_DISPLAYED;
    }

  mpi->qscale = NULL;
  mpi->usage_count++;
  }
//    printf("\rVF_MPI: %p %p %p %d %d %d    \n",
//      mpi->planes[0],mpi->planes[1],mpi->planes[2],
//      mpi->stride[0],mpi->stride[1],mpi->stride[2]);
  return mpi;
}

int ff_vf_next_put_image(struct vf_instance *vf,mp_image_t *mpi, double pts){
    MPContext *m= (MPContext*)(((uint8_t*)vf) - offsetof(MPContext, vf));
    AVFilterLink *outlink     = m->avfctx->outputs[0];
    AVFrame *picref = av_frame_alloc();
    int i;

    av_assert0(vf->next);

    av_log(m->avfctx, AV_LOG_DEBUG, "ff_vf_next_put_image\n");

    if (!picref)
        goto fail;

    picref->width  = mpi->w;
    picref->height = mpi->h;

    picref->type = AVMEDIA_TYPE_VIDEO;

    for(i=0; conversion_map[i].fmt && mpi->imgfmt != conversion_map[i].fmt; i++);
    picref->format = conversion_map[i].pix_fmt;

    for(i=0; conversion_map[i].fmt && m->in_pix_fmt != conversion_map[i].pix_fmt; i++);
    if (mpi->imgfmt == conversion_map[i].fmt)
        picref->format = conversion_map[i].pix_fmt;

    memcpy(picref->linesize, mpi->stride, FFMIN(sizeof(picref->linesize), sizeof(mpi->stride)));

    for(i=0; i<4 && mpi->stride[i]; i++){
        picref->data[i] = mpi->planes[i];
    }

    if(pts != MP_NOPTS_VALUE)
        picref->pts= pts * av_q2d(outlink->time_base);

    if(1) { // mp buffers are currently unsupported in libavfilter, we thus must copy
        AVFrame *tofree = picref;
        picref = av_frame_clone(picref);
        av_frame_free(&tofree);
    }

    ff_filter_frame(outlink, picref);
    m->frame_returned++;

    return 1;
fail:
    av_frame_free(&picref);
    return 0;
}

int ff_vf_next_config(struct vf_instance *vf,
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
    ff_mp_msg(MSGT_VFILTER,MSGL_V,"REQ: flags=0x%X  req=0x%X  \n",flags,vf->default_reqs);
    miss=vf->default_reqs - (flags&vf->default_reqs);
    if(miss&VFCAP_ACCEPT_STRIDE){
        // vf requires stride support but vf->next doesn't support it!
        // let's insert the 'expand' filter, it does the job for us:
        vf_instance_t* vf2=vf_open_filter(vf->next,"expand",NULL);
        if(!vf2) return 0; // shouldn't happen!
        vf->next=vf2;
    }
    vf->next->w = width; vf->next->h = height;
    return 1;
#endif
}

int ff_vf_next_control(struct vf_instance *vf, int request, void* data){
    MPContext *m= (MPContext*)(((uint8_t*)vf) - offsetof(MPContext, vf));
    av_log(m->avfctx, AV_LOG_DEBUG, "Received control %d\n", request);
    return 0;
}

static int vf_default_query_format(struct vf_instance *vf, unsigned int fmt){
    MPContext *m= (MPContext*)(((uint8_t*)vf) - offsetof(MPContext, vf));
    int i;
    av_log(m->avfctx, AV_LOG_DEBUG, "query %X\n", fmt);

    for(i=0; conversion_map[i].fmt; i++){
        if(fmt==conversion_map[i].fmt)
            return 1; //we suport all
    }
    return 0;
}


static av_cold int init(AVFilterContext *ctx)
{
    MPContext *m = ctx->priv;
    int cpu_flags = av_get_cpu_flags();
    char name[256];
    const char *args;
    int i;

    ff_gCpuCaps.hasMMX      = cpu_flags & AV_CPU_FLAG_MMX;
    ff_gCpuCaps.hasMMX2     = cpu_flags & AV_CPU_FLAG_MMX2;
    ff_gCpuCaps.hasSSE      = cpu_flags & AV_CPU_FLAG_SSE;
    ff_gCpuCaps.hasSSE2     = cpu_flags & AV_CPU_FLAG_SSE2;
    ff_gCpuCaps.hasSSE3     = cpu_flags & AV_CPU_FLAG_SSE3;
    ff_gCpuCaps.hasSSSE3    = cpu_flags & AV_CPU_FLAG_SSSE3;
    ff_gCpuCaps.hasSSE4     = cpu_flags & AV_CPU_FLAG_SSE4;
    ff_gCpuCaps.hasSSE42    = cpu_flags & AV_CPU_FLAG_SSE42;
    ff_gCpuCaps.hasAVX      = cpu_flags & AV_CPU_FLAG_AVX;
    ff_gCpuCaps.has3DNow    = cpu_flags & AV_CPU_FLAG_3DNOW;
    ff_gCpuCaps.has3DNowExt = cpu_flags & AV_CPU_FLAG_3DNOWEXT;

    m->avfctx= ctx;

    args = m->filter;
    if(!args || 1!=sscanf(args, "%255[^:=]", name)){
        av_log(ctx, AV_LOG_ERROR, "Invalid parameter.\n");
        return AVERROR(EINVAL);
    }
    args += strlen(name);
    if (args[0] == '=')
        args++;

    for(i=0; ;i++){
        if(!filters[i] || !strcmp(name, filters[i]->name))
            break;
    }

    if(!filters[i]){
        av_log(ctx, AV_LOG_ERROR, "Unknown filter %s\n", name);
        return AVERROR(EINVAL);
    }

    av_log(ctx, AV_LOG_WARNING,
           "'%s' is a wrapped MPlayer filter (libmpcodecs). This filter may be removed\n"
           "once it has been ported to a native libavfilter.\n", name);

    memset(&m->vf,0,sizeof(m->vf));
    m->vf.info= filters[i];

    m->vf.next        = &m->next_vf;
    m->vf.put_image   = ff_vf_next_put_image;
    m->vf.config      = ff_vf_next_config;
    m->vf.query_format= vf_default_query_format;
    m->vf.control     = ff_vf_next_control;
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
    if(m->vf.info->vf_open(&m->vf, (char*)args)<=0){
        av_log(ctx, AV_LOG_ERROR, "vf_open() of %s with arg=%s failed\n", name, args);
        return -1;
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    MPContext *m = ctx->priv;
    vf_instance_t *vf = &m->vf;

    while(vf){
        vf_instance_t *next = vf->next;
        if(vf->uninit)
            vf->uninit(vf);
        ff_free_mp_image(vf->imgctx.static_images[0]);
        ff_free_mp_image(vf->imgctx.static_images[1]);
        ff_free_mp_image(vf->imgctx.temp_images[0]);
        ff_free_mp_image(vf->imgctx.export_images[0]);
        vf = next;
    }
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *avfmts=NULL;
    MPContext *m = ctx->priv;
    enum AVPixelFormat lastpixfmt = AV_PIX_FMT_NONE;
    int i;

    for(i=0; conversion_map[i].fmt; i++){
        av_log(ctx, AV_LOG_DEBUG, "query: %X\n", conversion_map[i].fmt);
        if(m->vf.query_format(&m->vf, conversion_map[i].fmt)){
            av_log(ctx, AV_LOG_DEBUG, "supported,adding\n");
            if (conversion_map[i].pix_fmt != lastpixfmt) {
                ff_add_format(&avfmts, conversion_map[i].pix_fmt);
                lastpixfmt = conversion_map[i].pix_fmt;
            }
        }
    }

    if (!avfmts)
        return -1;

    //We assume all allowed input formats are also allowed output formats
    ff_set_common_formats(ctx, avfmts);
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
        ret=ff_request_frame(outlink->src->inputs[0]);
        if(ret<0)
            break;
    }

    av_log(m->avfctx, AV_LOG_DEBUG, "mp request_frame ret=%d\n", ret);
    return ret;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *inpic)
{
    MPContext *m = inlink->dst->priv;
    int i;
    double pts= MP_NOPTS_VALUE;
    mp_image_t* mpi = ff_new_mp_image(inpic->width, inpic->height);

    if(inpic->pts != AV_NOPTS_VALUE)
        pts= inpic->pts / av_q2d(inlink->time_base);

    for(i=0; conversion_map[i].fmt && conversion_map[i].pix_fmt != inlink->format; i++);
    ff_mp_image_setfmt(mpi,conversion_map[i].fmt);
    m->in_pix_fmt = inlink->format;

    memcpy(mpi->planes, inpic->data,     FFMIN(sizeof(inpic->data)    , sizeof(mpi->planes)));
    memcpy(mpi->stride, inpic->linesize, FFMIN(sizeof(inpic->linesize), sizeof(mpi->stride)));

    if (inpic->interlaced_frame)
        mpi->fields |= MP_IMGFIELD_INTERLACED;
    if (inpic->top_field_first)
        mpi->fields |= MP_IMGFIELD_TOP_FIRST;
    if (inpic->repeat_pict)
        mpi->fields |= MP_IMGFIELD_REPEAT_FIRST;

    // mpi->flags|=MP_IMGFLAG_ALLOCATED; ?
    mpi->flags |= MP_IMGFLAG_READABLE;
    if(!av_frame_is_writable(inpic))
        mpi->flags |= MP_IMGFLAG_PRESERVE;
    if(m->vf.put_image(&m->vf, mpi, pts) == 0){
        av_log(m->avfctx, AV_LOG_DEBUG, "put_image() says skip\n");
    }else{
        av_frame_free(&inpic);
    }
    ff_free_mp_image(mpi);
    return 0;
}

static const AVFilterPad mp_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_inprops,
    },
    { NULL }
};

static const AVFilterPad mp_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
        .config_props  = config_outprops,
    },
    { NULL }
};

AVFilter avfilter_vf_mp = {
    .name          = "mp",
    .description   = NULL_IF_CONFIG_SMALL("Apply a libmpcodecs filter to the input video."),
    .init          = init,
    .uninit        = uninit,
    .priv_size     = sizeof(MPContext),
    .query_formats = query_formats,
    .inputs        = mp_inputs,
    .outputs       = mp_outputs,
    .priv_class    = &mp_class,
};
