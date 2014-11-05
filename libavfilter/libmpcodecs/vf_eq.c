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

#include "libvo/video_out.h"

struct vf_priv_s {
        unsigned char *buf;
        int brightness;
        int contrast;
};

#if HAVE_MMX_INLINE && HAVE_6REGS
static void process_MMX(unsigned char *dest, int dstride, unsigned char *src, int sstride,
                    int w, int h, int brightness, int contrast)
{
        int i;
        int pel;
        int dstep = dstride-w;
        int sstep = sstride-w;
        short brvec[4];
        short contvec[4];

        contrast = ((contrast+100)*256*16)/100;
        brightness = ((brightness+100)*511)/200-128 - contrast/32;

        brvec[0] = brvec[1] = brvec[2] = brvec[3] = brightness;
        contvec[0] = contvec[1] = contvec[2] = contvec[3] = contrast;

        while (h--) {
                __asm__ volatile (
                        "movq (%5), %%mm3 \n\t"
                        "movq (%6), %%mm4 \n\t"
                        "pxor %%mm0, %%mm0 \n\t"
                        "movl %4, %%eax\n\t"
                        ASMALIGN(4)
                        "1: \n\t"
                        "movq (%0), %%mm1 \n\t"
                        "movq (%0), %%mm2 \n\t"
                        "punpcklbw %%mm0, %%mm1 \n\t"
                        "punpckhbw %%mm0, %%mm2 \n\t"
                        "psllw $4, %%mm1 \n\t"
                        "psllw $4, %%mm2 \n\t"
                        "pmulhw %%mm4, %%mm1 \n\t"
                        "pmulhw %%mm4, %%mm2 \n\t"
                        "paddw %%mm3, %%mm1 \n\t"
                        "paddw %%mm3, %%mm2 \n\t"
                        "packuswb %%mm2, %%mm1 \n\t"
                        "add $8, %0 \n\t"
                        "movq %%mm1, (%1) \n\t"
                        "add $8, %1 \n\t"
                        "decl %%eax \n\t"
                        "jnz 1b \n\t"
                        : "=r" (src), "=r" (dest)
                        : "0" (src), "1" (dest), "r" (w>>3), "r" (brvec), "r" (contvec)
                        : "%eax"
                );

                for (i = w&7; i; i--)
                {
                        pel = ((*src++* contrast)>>12) + brightness;
                        if(pel&768) pel = (-pel)>>31;
                        *dest++ = pel;
                }

                src += sstep;
                dest += dstep;
        }
        __asm__ volatile ( "emms \n\t" ::: "memory" );
}
#endif

static void process_C(unsigned char *dest, int dstride, unsigned char *src, int sstride,
                    int w, int h, int brightness, int contrast)
{
        int i;
        int pel;
        int dstep = dstride-w;
        int sstep = sstride-w;

        contrast = ((contrast+100)*256*256)/100;
        brightness = ((brightness+100)*511)/200-128 - contrast/512;

        while (h--) {
                for (i = w; i; i--)
                {
                        pel = ((*src++* contrast)>>16) + brightness;
                        if(pel&768) pel = (-pel)>>31;
                        *dest++ = pel;
                }
                src += sstep;
                dest += dstep;
        }
}

static void (*process)(unsigned char *dest, int dstride, unsigned char *src, int sstride,
                       int w, int h, int brightness, int contrast);

/* FIXME: add packed yuv version of process */

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts)
{
        mp_image_t *dmpi;

        dmpi=ff_vf_get_image(vf->next, mpi->imgfmt,
                          MP_IMGTYPE_EXPORT, 0,
                          mpi->w, mpi->h);

        dmpi->stride[0] = mpi->stride[0];
        dmpi->planes[1] = mpi->planes[1];
        dmpi->planes[2] = mpi->planes[2];
        dmpi->stride[1] = mpi->stride[1];
        dmpi->stride[2] = mpi->stride[2];

        if (!vf->priv->buf) vf->priv->buf = malloc(mpi->stride[0]*mpi->h);

        if ((vf->priv->brightness == 0) && (vf->priv->contrast == 0))
                dmpi->planes[0] = mpi->planes[0];
        else {
                dmpi->planes[0] = vf->priv->buf;
                process(dmpi->planes[0], dmpi->stride[0],
                        mpi->planes[0], mpi->stride[0],
                        mpi->w, mpi->h, vf->priv->brightness,
                        vf->priv->contrast);
        }

        return ff_vf_next_put_image(vf,dmpi, pts);
}

static int control(struct vf_instance *vf, int request, void* data)
{
        vf_equalizer_t *eq;

        switch (request) {
        case VFCTRL_SET_EQUALIZER:
                eq = data;
                if (!strcmp(eq->item,"brightness")) {
                        vf->priv->brightness = eq->value;
                        return CONTROL_TRUE;
                }
                else if (!strcmp(eq->item,"contrast")) {
                        vf->priv->contrast = eq->value;
                        return CONTROL_TRUE;
                }
                break;
        case VFCTRL_GET_EQUALIZER:
                eq = data;
                if (!strcmp(eq->item,"brightness")) {
                        eq->value = vf->priv->brightness;
                        return CONTROL_TRUE;
                }
                else if (!strcmp(eq->item,"contrast")) {
                        eq->value = vf->priv->contrast;
                        return CONTROL_TRUE;
                }
                break;
        }
        return ff_vf_next_control(vf, request, data);
}

static int query_format(struct vf_instance *vf, unsigned int fmt)
{
        switch (fmt) {
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
                return ff_vf_next_query_format(vf, fmt);
        }
        return 0;
}

static void uninit(struct vf_instance *vf)
{
        free(vf->priv->buf);
        free(vf->priv);
}

static int vf_open(vf_instance_t *vf, char *args)
{
        vf->control=control;
        vf->query_format=query_format;
        vf->put_image=put_image;
        vf->uninit=uninit;

    vf->priv = malloc(sizeof(struct vf_priv_s));
    memset(vf->priv, 0, sizeof(struct vf_priv_s));
    if (args) sscanf(args, "%d:%d", &vf->priv->brightness, &vf->priv->contrast);

        process = process_C;
#if HAVE_MMX_INLINE && HAVE_6REGS
        if(ff_gCpuCaps.hasMMX) process = process_MMX;
#endif

        return 1;
}

const vf_info_t ff_vf_info_eq = {
        "soft video equalizer",
        "eq",
        "Richard Felker",
        "",
        vf_open,
};
