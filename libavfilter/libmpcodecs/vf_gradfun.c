/*
 * Copyright (C) 2009 Loren Merritt <lorenm@u.washignton.edu>
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

/*
 * Debanding algorithm (from gradfun2db by prunedtree):
 * Boxblur.
 * Foreach pixel, if it's within threshold of the blurred value, make it closer.
 * So now we have a smoothed and higher bitdepth version of all the shallow
 * gradients, while leaving detailed areas untouched.
 * Dither it back to 8bit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "config.h"
#include "cpudetect.h"
#include "img_format.h"
#include "mp_image.h"
#include "vf.h"
#include "libvo/fastmemcpy.h"
#include "libavutil/avutil.h"
#include "libavutil/x86_cpu.h"

struct vf_priv_s {
    int thresh;
    int radius;
    uint16_t *buf;
    void (*filter_line)(uint8_t *dst, uint8_t *src, uint16_t *dc,
                        int width, int thresh, const uint16_t *dithers);
    void (*blur_line)(uint16_t *dc, uint16_t *buf, uint16_t *buf1,
                      uint8_t *src, int sstride, int width);
};

static const uint16_t __attribute__((aligned(16))) pw_7f[8] = {127,127,127,127,127,127,127,127};
static const uint16_t __attribute__((aligned(16))) pw_ff[8] = {255,255,255,255,255,255,255,255};
static const uint16_t __attribute__((aligned(16))) dither[8][8] = {
    {  0, 96, 24,120,  6,102, 30,126 },
    { 64, 32, 88, 56, 70, 38, 94, 62 },
    { 16,112,  8,104, 22,118, 14,110 },
    { 80, 48, 72, 40, 86, 54, 78, 46 },
    {  4,100, 28,124,  2, 98, 26,122 },
    { 68, 36, 92, 60, 66, 34, 90, 58 },
    { 20,116, 12,108, 18,114, 10,106 },
    { 84, 52, 76, 44, 82, 50, 74, 42 },
};

static void filter_line_c(uint8_t *dst, uint8_t *src, uint16_t *dc,
                          int width, int thresh, const uint16_t *dithers)
{
    int x;
    for (x=0; x<width; x++, dc+=x&1) {
        int pix = src[x]<<7;
        int delta = dc[0] - pix;
        int m = abs(delta) * thresh >> 16;
        m = FFMAX(0, 127-m);
        m = m*m*delta >> 14;
        pix += m + dithers[x&7];
        dst[x] = av_clip_uint8(pix>>7);
    }
}

static void blur_line_c(uint16_t *dc, uint16_t *buf, uint16_t *buf1,
                        uint8_t *src, int sstride, int width)
{
    int x, v, old;
    for (x=0; x<width; x++) {
        v = buf1[x] + src[2*x] + src[2*x+1] + src[2*x+sstride] + src[2*x+1+sstride];
        old = buf[x];
        buf[x] = v;
        dc[x] = v - old;
    }
}

#if HAVE_MMX2
static void filter_line_mmx2(uint8_t *dst, uint8_t *src, uint16_t *dc,
                             int width, int thresh, const uint16_t *dithers)
{
    intptr_t x;
    if (width&3) {
        x = width&~3;
        filter_line_c(dst+x, src+x, dc+x/2, width-x, thresh, dithers);
        width = x;
    }
    x = -width;
    __asm__ volatile(
        "movd          %4, %%mm5 \n"
        "pxor       %%mm7, %%mm7 \n"
        "pshufw $0, %%mm5, %%mm5 \n"
        "movq          %6, %%mm6 \n"
        "movq          %5, %%mm4 \n"
        "1: \n"
        "movd     (%2,%0), %%mm0 \n"
        "movd     (%3,%0), %%mm1 \n"
        "punpcklbw  %%mm7, %%mm0 \n"
        "punpcklwd  %%mm1, %%mm1 \n"
        "psllw         $7, %%mm0 \n"
        "pxor       %%mm2, %%mm2 \n"
        "psubw      %%mm0, %%mm1 \n" // delta = dc - pix
        "psubw      %%mm1, %%mm2 \n"
        "pmaxsw     %%mm1, %%mm2 \n"
        "pmulhuw    %%mm5, %%mm2 \n" // m = abs(delta) * thresh >> 16
        "psubw      %%mm6, %%mm2 \n"
        "pminsw     %%mm7, %%mm2 \n" // m = -max(0, 127-m)
        "pmullw     %%mm2, %%mm2 \n"
        "paddw      %%mm4, %%mm0 \n" // pix += dither
        "pmulhw     %%mm2, %%mm1 \n"
        "psllw         $2, %%mm1 \n" // m = m*m*delta >> 14
        "paddw      %%mm1, %%mm0 \n" // pix += m
        "psraw         $7, %%mm0 \n"
        "packuswb   %%mm0, %%mm0 \n"
        "movd       %%mm0, (%1,%0) \n" // dst = clip(pix>>7)
        "add           $4, %0 \n"
        "jl 1b \n"
        "emms \n"
        :"+r"(x)
        :"r"(dst+width), "r"(src+width), "r"(dc+width/2),
         "rm"(thresh), "m"(*dithers), "m"(*pw_7f)
        :"memory"
    );
}
#endif

#if HAVE_SSSE3
static void filter_line_ssse3(uint8_t *dst, uint8_t *src, uint16_t *dc,
                              int width, int thresh, const uint16_t *dithers)
{
    intptr_t x;
    if (width&7) {
        // could be 10% faster if I somehow eliminated this
        x = width&~7;
        filter_line_c(dst+x, src+x, dc+x/2, width-x, thresh, dithers);
        width = x;
    }
    x = -width;
    __asm__ volatile(
        "movd           %4, %%xmm5 \n"
        "pxor       %%xmm7, %%xmm7 \n"
        "pshuflw $0,%%xmm5, %%xmm5 \n"
        "movdqa         %6, %%xmm6 \n"
        "punpcklqdq %%xmm5, %%xmm5 \n"
        "movdqa         %5, %%xmm4 \n"
        "1: \n"
        "movq      (%2,%0), %%xmm0 \n"
        "movq      (%3,%0), %%xmm1 \n"
        "punpcklbw  %%xmm7, %%xmm0 \n"
        "punpcklwd  %%xmm1, %%xmm1 \n"
        "psllw          $7, %%xmm0 \n"
        "psubw      %%xmm0, %%xmm1 \n" // delta = dc - pix
        "pabsw      %%xmm1, %%xmm2 \n"
        "pmulhuw    %%xmm5, %%xmm2 \n" // m = abs(delta) * thresh >> 16
        "psubw      %%xmm6, %%xmm2 \n"
        "pminsw     %%xmm7, %%xmm2 \n" // m = -max(0, 127-m)
        "pmullw     %%xmm2, %%xmm2 \n"
        "psllw          $1, %%xmm2 \n"
        "paddw      %%xmm4, %%xmm0 \n" // pix += dither
        "pmulhrsw   %%xmm2, %%xmm1 \n" // m = m*m*delta >> 14
        "paddw      %%xmm1, %%xmm0 \n" // pix += m
        "psraw          $7, %%xmm0 \n"
        "packuswb   %%xmm0, %%xmm0 \n"
        "movq       %%xmm0, (%1,%0) \n" // dst = clip(pix>>7)
        "add            $8, %0 \n"
        "jl 1b \n"
        :"+&r"(x)
        :"r"(dst+width), "r"(src+width), "r"(dc+width/2),
         "rm"(thresh), "m"(*dithers), "m"(*pw_7f)
        :"memory"
    );
}
#endif // HAVE_SSSE3

#if HAVE_SSE2 && HAVE_6REGS
#define BLURV(load)\
    intptr_t x = -2*width;\
    __asm__ volatile(\
        "movdqa %6, %%xmm7 \n"\
        "1: \n"\
        load"   (%4,%0), %%xmm0 \n"\
        load"   (%5,%0), %%xmm1 \n"\
        "movdqa  %%xmm0, %%xmm2 \n"\
        "movdqa  %%xmm1, %%xmm3 \n"\
        "psrlw       $8, %%xmm0 \n"\
        "psrlw       $8, %%xmm1 \n"\
        "pand    %%xmm7, %%xmm2 \n"\
        "pand    %%xmm7, %%xmm3 \n"\
        "paddw   %%xmm1, %%xmm0 \n"\
        "paddw   %%xmm3, %%xmm2 \n"\
        "paddw   %%xmm2, %%xmm0 \n"\
        "paddw  (%2,%0), %%xmm0 \n"\
        "movdqa (%1,%0), %%xmm1 \n"\
        "movdqa  %%xmm0, (%1,%0) \n"\
        "psubw   %%xmm1, %%xmm0 \n"\
        "movdqa  %%xmm0, (%3,%0) \n"\
        "add        $16, %0 \n"\
        "jl 1b \n"\
        :"+&r"(x)\
        :"r"(buf+width),\
         "r"(buf1+width),\
         "r"(dc+width),\
         "r"(src+width*2),\
         "r"(src+width*2+sstride),\
         "m"(*pw_ff)\
        :"memory"\
    );

static void blur_line_sse2(uint16_t *dc, uint16_t *buf, uint16_t *buf1,
                           uint8_t *src, int sstride, int width)
{
    if (((intptr_t)src|sstride)&15) {
        BLURV("movdqu");
    } else {
        BLURV("movdqa");
    }
}
#endif // HAVE_6REGS && HAVE_SSE2

static void filter(struct vf_priv_s *ctx, uint8_t *dst, uint8_t *src,
                   int width, int height, int dstride, int sstride, int r)
{
    int bstride = ((width+15)&~15)/2;
    int y;
    uint32_t dc_factor = (1<<21)/(r*r);
    uint16_t *dc = ctx->buf+16;
    uint16_t *buf = ctx->buf+bstride+32;
    int thresh = ctx->thresh;

    memset(dc, 0, (bstride+16)*sizeof(*buf));
    for (y=0; y<r; y++)
        ctx->blur_line(dc, buf+y*bstride, buf+(y-1)*bstride, src+2*y*sstride, sstride, width/2);
    for (;;) {
        if (y < height-r) {
            int mod = ((y+r)/2)%r;
            uint16_t *buf0 = buf+mod*bstride;
            uint16_t *buf1 = buf+(mod?mod-1:r-1)*bstride;
            int x, v;
            ctx->blur_line(dc, buf0, buf1, src+(y+r)*sstride, sstride, width/2);
            for (x=v=0; x<r; x++)
                v += dc[x];
            for (; x<width/2; x++) {
                v += dc[x] - dc[x-r];
                dc[x-r] = v * dc_factor >> 16;
            }
            for (; x<(width+r+1)/2; x++)
                dc[x-r] = v * dc_factor >> 16;
            for (x=-r/2; x<0; x++)
                dc[x] = dc[0];
        }
        if (y == r) {
            for (y=0; y<r; y++)
                ctx->filter_line(dst+y*dstride, src+y*sstride, dc-r/2, width, thresh, dither[y&7]);
        }
        ctx->filter_line(dst+y*dstride, src+y*sstride, dc-r/2, width, thresh, dither[y&7]);
        if (++y >= height) break;
        ctx->filter_line(dst+y*dstride, src+y*sstride, dc-r/2, width, thresh, dither[y&7]);
        if (++y >= height) break;
    }
}

static void get_image(struct vf_instance *vf, mp_image_t *mpi)
{
    if (mpi->flags&MP_IMGFLAG_PRESERVE) return; // don't change
    // ok, we can do pp in-place:
    vf->dmpi = vf_get_image(vf->next, mpi->imgfmt,
                            mpi->type, mpi->flags, mpi->width, mpi->height);
    mpi->planes[0] = vf->dmpi->planes[0];
    mpi->stride[0] = vf->dmpi->stride[0];
    mpi->width = vf->dmpi->width;
    if (mpi->flags&MP_IMGFLAG_PLANAR){
        mpi->planes[1] = vf->dmpi->planes[1];
        mpi->planes[2] = vf->dmpi->planes[2];
        mpi->stride[1] = vf->dmpi->stride[1];
        mpi->stride[2] = vf->dmpi->stride[2];
    }
    mpi->flags |= MP_IMGFLAG_DIRECT;
}

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts)
{
    mp_image_t *dmpi = vf->dmpi;
    int p;

    if (!(mpi->flags&MP_IMGFLAG_DIRECT)) {
        // no DR, so get a new image. hope we'll get DR buffer:
        dmpi = vf_get_image(vf->next,mpi->imgfmt, MP_IMGTYPE_TEMP,
                            MP_IMGFLAG_ACCEPT_STRIDE|MP_IMGFLAG_PREFER_ALIGNED_STRIDE,
                            mpi->w, mpi->h);
    }
    vf_clone_mpi_attributes(dmpi, mpi);

    for (p=0; p<mpi->num_planes; p++) {
        int w = mpi->w;
        int h = mpi->h;
        int r = vf->priv->radius;
        if (p) {
            w >>= mpi->chroma_x_shift;
            h >>= mpi->chroma_y_shift;
            r = ((r>>mpi->chroma_x_shift) + (r>>mpi->chroma_y_shift)) / 2;
            r = av_clip((r+1)&~1,4,32);
        }
        if (FFMIN(w,h) > 2*r)
            filter(vf->priv, dmpi->planes[p], mpi->planes[p], w, h,
                             dmpi->stride[p], mpi->stride[p], r);
        else if (dmpi->planes[p] != mpi->planes[p])
            memcpy_pic(dmpi->planes[p], mpi->planes[p], w, h,
                       dmpi->stride[p], mpi->stride[p]);
    }

    return vf_next_put_image(vf, dmpi, pts);
}

static int query_format(struct vf_instance *vf, unsigned int fmt)
{
    switch (fmt){
    case IMGFMT_YVU9:
    case IMGFMT_IF09:
    case IMGFMT_YV12:
    case IMGFMT_I420:
    case IMGFMT_IYUV:
    case IMGFMT_CLPL:
    case IMGFMT_Y800:
    case IMGFMT_Y8:
    case IMGFMT_NV12:
    case IMGFMT_NV21:
    case IMGFMT_444P:
    case IMGFMT_422P:
    case IMGFMT_411P:
    case IMGFMT_HM12:
        return vf_next_query_format(vf,fmt);
    }
    return 0;
}

static int config(struct vf_instance *vf,
                  int width, int height, int d_width, int d_height,
                  unsigned int flags, unsigned int outfmt)
{
    free(vf->priv->buf);
    vf->priv->buf = av_mallocz((((width+15)&~15)*(vf->priv->radius+1)/2+32)*sizeof(uint16_t));
    return vf_next_config(vf,width,height,d_width,d_height,flags,outfmt);
}

static void uninit(struct vf_instance *vf)
{
    if (!vf->priv) return;
    av_free(vf->priv->buf);
    free(vf->priv);
    vf->priv = NULL;
}

static int vf_open(vf_instance_t *vf, char *args)
{
    float thresh = 1.2;
    int radius = 16;

    vf->get_image=get_image;
    vf->put_image=put_image;
    vf->query_format=query_format;
    vf->config=config;
    vf->uninit=uninit;
    vf->priv=malloc(sizeof(struct vf_priv_s));
    memset(vf->priv, 0, sizeof(struct vf_priv_s));

    if (args) sscanf(args, "%f:%d", &thresh, &radius);
    vf->priv->thresh = (1<<15)/av_clipf(thresh,0.51,255);
    vf->priv->radius = av_clip((radius+1)&~1,4,32);

    vf->priv->blur_line = blur_line_c;
    vf->priv->filter_line = filter_line_c;
#if HAVE_SSE2 && HAVE_6REGS
    if (gCpuCaps.hasSSE2)
        vf->priv->blur_line = blur_line_sse2;
#endif
#if HAVE_MMX2
    if (gCpuCaps.hasMMX2)
        vf->priv->filter_line = filter_line_mmx2;
#endif
#if HAVE_SSSE3
    if (gCpuCaps.hasSSSE3)
        vf->priv->filter_line = filter_line_ssse3;
#endif

    return 1;
}

const vf_info_t vf_info_gradfun = {
    "gradient deband",
    "gradfun",
    "Loren Merritt",
    "",
    vf_open,
    NULL
};
