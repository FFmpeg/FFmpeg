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

#include "config.h"
#include "mp_msg.h"
#include "cpudetect.h"

#include "img_format.h"
#include "mp_image.h"
#include "vf.h"

#include "libvo/fastmemcpy.h"


struct vf_priv_s {
    int hi, lo;
    float frac;
    int max, last, cnt;
};

#if HAVE_MMX && HAVE_EBX_AVAILABLE
static int diff_MMX(unsigned char *old, unsigned char *new, int os, int ns)
{
    volatile short out[4];
    __asm__ (
        "movl $8, %%ecx \n\t"
        "pxor %%mm4, %%mm4 \n\t"
        "pxor %%mm7, %%mm7 \n\t"

        ASMALIGN(4)
        "1: \n\t"

        "movq (%%"REG_S"), %%mm0 \n\t"
        "movq (%%"REG_S"), %%mm2 \n\t"
        "add %%"REG_a", %%"REG_S" \n\t"
        "movq (%%"REG_D"), %%mm1 \n\t"
        "add %%"REG_b", %%"REG_D" \n\t"
        "psubusb %%mm1, %%mm2 \n\t"
        "psubusb %%mm0, %%mm1 \n\t"
        "movq %%mm2, %%mm0 \n\t"
        "movq %%mm1, %%mm3 \n\t"
        "punpcklbw %%mm7, %%mm0 \n\t"
        "punpcklbw %%mm7, %%mm1 \n\t"
        "punpckhbw %%mm7, %%mm2 \n\t"
        "punpckhbw %%mm7, %%mm3 \n\t"
        "paddw %%mm0, %%mm4 \n\t"
        "paddw %%mm1, %%mm4 \n\t"
        "paddw %%mm2, %%mm4 \n\t"
        "paddw %%mm3, %%mm4 \n\t"

        "decl %%ecx \n\t"
        "jnz 1b \n\t"
        "movq %%mm4, (%%"REG_d") \n\t"
        "emms \n\t"
        :
        : "S" (old), "D" (new), "a" ((long)os), "b" ((long)ns), "d" (out)
        : "%ecx", "memory"
        );
    return out[0]+out[1]+out[2]+out[3];
}
#endif

static int diff_C(unsigned char *old, unsigned char *new, int os, int ns)
{
    int x, y, d=0;
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            d += abs(new[x] - old[x]);
        }
        new += ns;
        old += os;
    }
    return d;
}

static int (*diff)(unsigned char *, unsigned char *, int, int);

static int diff_to_drop_plane(int hi, int lo, float frac, unsigned char *old, unsigned char *new, int w, int h, int os, int ns)
{
    int x, y;
    int d, c=0;
    int t = (w/16)*(h/16)*frac;
    for (y = 0; y < h-7; y += 4) {
        for (x = 8; x < w-7; x += 4) {
            d = diff(old+x+y*os, new+x+y*ns, os, ns);
            if (d > hi) return 0;
            if (d > lo) {
                c++;
                if (c > t) return 0;
            }
        }
    }
    return 1;
}

static int diff_to_drop(int hi, int lo, float frac, mp_image_t *old, mp_image_t *new)
{
    if (new->flags & MP_IMGFLAG_PLANAR) {
        return diff_to_drop_plane(hi,lo,frac, old->planes[0], new->planes[0],
            new->w, new->h, old->stride[0], new->stride[0])
            && diff_to_drop_plane(hi,lo,frac, old->planes[1], new->planes[1],
            new->chroma_width, new->chroma_height,
            old->stride[1], new->stride[1])
            && diff_to_drop_plane(hi,lo,frac, old->planes[2], new->planes[2],
            new->chroma_width, new->chroma_height,
            old->stride[2], new->stride[2]);
    }
    return diff_to_drop_plane(hi,lo,frac, old->planes[0], new->planes[0],
        new->w*(new->bpp/8), new->h, old->stride[0], new->stride[0]);
}

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts)
{
    mp_image_t *dmpi;

    dmpi = vf_get_image(vf->next, mpi->imgfmt,
        MP_IMGTYPE_STATIC, MP_IMGFLAG_ACCEPT_STRIDE |
        MP_IMGFLAG_PRESERVE | MP_IMGFLAG_READABLE,
        mpi->width, mpi->height);
    dmpi->qscale = mpi->qscale;
    dmpi->qstride = mpi->qstride;
    dmpi->qscale_type = mpi->qscale_type;

    if (diff_to_drop(vf->priv->hi, vf->priv->lo, vf->priv->frac, dmpi, mpi)) {
        if (vf->priv->max == 0)
            return 0;
        else if ((vf->priv->max > 0) && (vf->priv->cnt++ < vf->priv->max))
            return 0;
        else if ((vf->priv->max < 0) && (vf->priv->last+1 >= -vf->priv->max))
            return vf->priv->last=0;
    }
    vf->priv->last++;
    vf->priv->cnt=0;

    memcpy_pic(dmpi->planes[0], mpi->planes[0], mpi->w, mpi->h,
        dmpi->stride[0], mpi->stride[0]);
    if (mpi->flags & MP_IMGFLAG_PLANAR) {
        memcpy_pic(dmpi->planes[1], mpi->planes[1],
            mpi->chroma_width, mpi->chroma_height,
            dmpi->stride[1], mpi->stride[1]);
        memcpy_pic(dmpi->planes[2], mpi->planes[2],
            mpi->chroma_width, mpi->chroma_height,
            dmpi->stride[2], mpi->stride[2]);
    }
    return vf_next_put_image(vf, dmpi, pts);
}

static void uninit(struct vf_instance *vf)
{
    free(vf->priv);
}

static int vf_open(vf_instance_t *vf, char *args)
{
    struct vf_priv_s *p;
    vf->put_image = put_image;
    vf->uninit = uninit;
    vf->default_reqs = VFCAP_ACCEPT_STRIDE;
    vf->priv = p = calloc(1, sizeof(struct vf_priv_s));
    p->max = 0;
    p->hi = 64*12;
    p->lo = 64*5;
    p->frac = 0.33;
    if (args) sscanf(args, "%d:%d:%d:%f", &p->max, &p->hi, &p->lo, &p->frac);
    diff = diff_C;
#if HAVE_MMX && HAVE_EBX_AVAILABLE
    if(gCpuCaps.hasMMX) diff = diff_MMX;
#endif
    return 1;
}

const vf_info_t vf_info_decimate = {
    "near-duplicate frame remover",
    "decimate",
    "Rich Felker",
    "",
    vf_open,
    NULL
};
