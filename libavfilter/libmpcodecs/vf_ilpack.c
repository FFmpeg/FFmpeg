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
#include "libavutil/attributes.h"
#include "libavutil/x86/asm.h"

typedef void (pack_func_t)(unsigned char *dst, unsigned char *y,
    unsigned char *u, unsigned char *v, int w, int us, int vs);

struct vf_priv_s {
    int mode;
    pack_func_t *pack[2];
};

static void pack_nn_C(unsigned char *dst, unsigned char *y,
    unsigned char *u, unsigned char *v, int w,
    int av_unused us, int av_unused vs)
{
    int j;
    for (j = w/2; j; j--) {
        *dst++ = *y++;
        *dst++ = *u++;
        *dst++ = *y++;
        *dst++ = *v++;
    }
}

static void pack_li_0_C(unsigned char *dst, unsigned char *y,
    unsigned char *u, unsigned char *v, int w, int us, int vs)
{
    int j;
    for (j = w/2; j; j--) {
        *dst++ = *y++;
        *dst++ = (u[us+us] + 7*u[0])>>3;
        *dst++ = *y++;
        *dst++ = (v[vs+vs] + 7*v[0])>>3;
        u++; v++;
    }
}

static void pack_li_1_C(unsigned char *dst, unsigned char *y,
    unsigned char *u, unsigned char *v, int w, int us, int vs)
{
    int j;
    for (j = w/2; j; j--) {
        *dst++ = *y++;
        *dst++ = (3*u[us+us] + 5*u[0])>>3;
        *dst++ = *y++;
        *dst++ = (3*v[vs+vs] + 5*v[0])>>3;
        u++; v++;
    }
}

#if HAVE_MMX
static void pack_nn_MMX(unsigned char *dst, unsigned char *y,
    unsigned char *u, unsigned char *v, int w,
    int av_unused us, int av_unused vs)
{
    __asm__ volatile (""
        ASMALIGN(4)
        "1: \n\t"
        "movq (%0), %%mm1 \n\t"
        "movq (%0), %%mm2 \n\t"
        "movq (%1), %%mm4 \n\t"
        "movq (%2), %%mm6 \n\t"
        "punpcklbw %%mm6, %%mm4 \n\t"
        "punpcklbw %%mm4, %%mm1 \n\t"
        "punpckhbw %%mm4, %%mm2 \n\t"

        "add $8, %0 \n\t"
        "add $4, %1 \n\t"
        "add $4, %2 \n\t"
        "movq %%mm1, (%3) \n\t"
        "movq %%mm2, 8(%3) \n\t"
        "add $16, %3 \n\t"
        "decl %4 \n\t"
        "jnz 1b \n\t"
        "emms \n\t"
        :
        : "r" (y), "r" (u), "r" (v), "r" (dst), "r" (w/8)
        : "memory"
        );
    pack_nn_C(dst, y, u, v, (w&7), 0, 0);
}

#if HAVE_EBX_AVAILABLE
static void pack_li_0_MMX(unsigned char *dst, unsigned char *y,
    unsigned char *u, unsigned char *v, int w, int us, int vs)
{
    __asm__ volatile (""
        "push %%"REG_BP" \n\t"
#if ARCH_X86_64
        "mov %6, %%"REG_BP" \n\t"
#else
        "movl 4(%%"REG_d"), %%"REG_BP" \n\t"
        "movl (%%"REG_d"), %%"REG_d" \n\t"
#endif
        "pxor %%mm0, %%mm0 \n\t"

        ASMALIGN(4)
        ".Lli0: \n\t"
        "movq (%%"REG_S"), %%mm1 \n\t"
        "movq (%%"REG_S"), %%mm2 \n\t"

        "movq (%%"REG_a",%%"REG_d",2), %%mm4 \n\t"
        "movq (%%"REG_b",%%"REG_BP",2), %%mm6 \n\t"
        "punpcklbw %%mm0, %%mm4 \n\t"
        "punpcklbw %%mm0, %%mm6 \n\t"
        "movq (%%"REG_a"), %%mm3 \n\t"
        "movq (%%"REG_b"), %%mm5 \n\t"
        "punpcklbw %%mm0, %%mm3 \n\t"
        "punpcklbw %%mm0, %%mm5 \n\t"
        "paddw %%mm3, %%mm4 \n\t"
        "paddw %%mm5, %%mm6 \n\t"
        "paddw %%mm3, %%mm4 \n\t"
        "paddw %%mm5, %%mm6 \n\t"
        "paddw %%mm3, %%mm4 \n\t"
        "paddw %%mm5, %%mm6 \n\t"
        "paddw %%mm3, %%mm4 \n\t"
        "paddw %%mm5, %%mm6 \n\t"
        "paddw %%mm3, %%mm4 \n\t"
        "paddw %%mm5, %%mm6 \n\t"
        "paddw %%mm3, %%mm4 \n\t"
        "paddw %%mm5, %%mm6 \n\t"
        "paddw %%mm3, %%mm4 \n\t"
        "paddw %%mm5, %%mm6 \n\t"
        "psrlw $3, %%mm4 \n\t"
        "psrlw $3, %%mm6 \n\t"
        "packuswb %%mm4, %%mm4 \n\t"
        "packuswb %%mm6, %%mm6 \n\t"
        "punpcklbw %%mm6, %%mm4 \n\t"
        "punpcklbw %%mm4, %%mm1 \n\t"
        "punpckhbw %%mm4, %%mm2 \n\t"

        "movq %%mm1, (%%"REG_D") \n\t"
        "movq %%mm2, 8(%%"REG_D") \n\t"

        "movq 8(%%"REG_S"), %%mm1 \n\t"
        "movq 8(%%"REG_S"), %%mm2 \n\t"

        "movq (%%"REG_a",%%"REG_d",2), %%mm4 \n\t"
        "movq (%%"REG_b",%%"REG_BP",2), %%mm6 \n\t"
        "punpckhbw %%mm0, %%mm4 \n\t"
        "punpckhbw %%mm0, %%mm6 \n\t"
        "movq (%%"REG_a"), %%mm3 \n\t"
        "movq (%%"REG_b"), %%mm5 \n\t"
        "punpckhbw %%mm0, %%mm3 \n\t"
        "punpckhbw %%mm0, %%mm5 \n\t"
        "paddw %%mm3, %%mm4 \n\t"
        "paddw %%mm5, %%mm6 \n\t"
        "paddw %%mm3, %%mm4 \n\t"
        "paddw %%mm5, %%mm6 \n\t"
        "paddw %%mm3, %%mm4 \n\t"
        "paddw %%mm5, %%mm6 \n\t"
        "paddw %%mm3, %%mm4 \n\t"
        "paddw %%mm5, %%mm6 \n\t"
        "paddw %%mm3, %%mm4 \n\t"
        "paddw %%mm5, %%mm6 \n\t"
        "paddw %%mm3, %%mm4 \n\t"
        "paddw %%mm5, %%mm6 \n\t"
        "paddw %%mm3, %%mm4 \n\t"
        "paddw %%mm5, %%mm6 \n\t"
        "psrlw $3, %%mm4 \n\t"
        "psrlw $3, %%mm6 \n\t"
        "packuswb %%mm4, %%mm4 \n\t"
        "packuswb %%mm6, %%mm6 \n\t"
        "punpcklbw %%mm6, %%mm4 \n\t"
        "punpcklbw %%mm4, %%mm1 \n\t"
        "punpckhbw %%mm4, %%mm2 \n\t"

        "add $16, %%"REG_S" \n\t"
        "add $8, %%"REG_a" \n\t"
        "add $8, %%"REG_b" \n\t"

        "movq %%mm1, 16(%%"REG_D") \n\t"
        "movq %%mm2, 24(%%"REG_D") \n\t"
        "add $32, %%"REG_D" \n\t"

        "decl %%ecx \n\t"
        "jnz .Lli0 \n\t"
        "emms \n\t"
        "pop %%"REG_BP" \n\t"
        :
        : "S" (y), "D" (dst), "a" (u), "b" (v), "c" (w/16),
#if ARCH_X86_64
        "d" ((x86_reg)us), "r" ((x86_reg)vs)
#else
        "d" (&us)
#endif
        : "memory"
        );
    pack_li_0_C(dst, y, u, v, (w&15), us, vs);
}

static void pack_li_1_MMX(unsigned char *dst, unsigned char *y,
    unsigned char *u, unsigned char *v, int w, int us, int vs)
{
    __asm__ volatile (""
        "push %%"REG_BP" \n\t"
#if ARCH_X86_64
        "mov %6, %%"REG_BP" \n\t"
#else
        "movl 4(%%"REG_d"), %%"REG_BP" \n\t"
        "movl (%%"REG_d"), %%"REG_d" \n\t"
#endif
        "pxor %%mm0, %%mm0 \n\t"

        ASMALIGN(4)
        ".Lli1: \n\t"
        "movq (%%"REG_S"), %%mm1 \n\t"
        "movq (%%"REG_S"), %%mm2 \n\t"

        "movq (%%"REG_a",%%"REG_d",2), %%mm4 \n\t"
        "movq (%%"REG_b",%%"REG_BP",2), %%mm6 \n\t"
        "punpcklbw %%mm0, %%mm4 \n\t"
        "punpcklbw %%mm0, %%mm6 \n\t"
        "movq (%%"REG_a"), %%mm3 \n\t"
        "movq (%%"REG_b"), %%mm5 \n\t"
        "punpcklbw %%mm0, %%mm3 \n\t"
        "punpcklbw %%mm0, %%mm5 \n\t"
        "movq %%mm4, %%mm7 \n\t"
        "paddw %%mm4, %%mm4 \n\t"
        "paddw %%mm7, %%mm4 \n\t"
        "movq %%mm6, %%mm7 \n\t"
        "paddw %%mm6, %%mm6 \n\t"
        "paddw %%mm7, %%mm6 \n\t"
        "paddw %%mm3, %%mm4 \n\t"
        "paddw %%mm5, %%mm6 \n\t"
        "paddw %%mm3, %%mm4 \n\t"
        "paddw %%mm5, %%mm6 \n\t"
        "paddw %%mm3, %%mm4 \n\t"
        "paddw %%mm5, %%mm6 \n\t"
        "paddw %%mm3, %%mm4 \n\t"
        "paddw %%mm5, %%mm6 \n\t"
        "paddw %%mm3, %%mm4 \n\t"
        "paddw %%mm5, %%mm6 \n\t"
        "psrlw $3, %%mm4 \n\t"
        "psrlw $3, %%mm6 \n\t"
        "packuswb %%mm4, %%mm4 \n\t"
        "packuswb %%mm6, %%mm6 \n\t"
        "punpcklbw %%mm6, %%mm4 \n\t"
        "punpcklbw %%mm4, %%mm1 \n\t"
        "punpckhbw %%mm4, %%mm2 \n\t"

        "movq %%mm1, (%%"REG_D") \n\t"
        "movq %%mm2, 8(%%"REG_D") \n\t"

        "movq 8(%%"REG_S"), %%mm1 \n\t"
        "movq 8(%%"REG_S"), %%mm2 \n\t"

        "movq (%%"REG_a",%%"REG_d",2), %%mm4 \n\t"
        "movq (%%"REG_b",%%"REG_BP",2), %%mm6 \n\t"
        "punpckhbw %%mm0, %%mm4 \n\t"
        "punpckhbw %%mm0, %%mm6 \n\t"
        "movq (%%"REG_a"), %%mm3 \n\t"
        "movq (%%"REG_b"), %%mm5 \n\t"
        "punpckhbw %%mm0, %%mm3 \n\t"
        "punpckhbw %%mm0, %%mm5 \n\t"
        "movq %%mm4, %%mm7 \n\t"
        "paddw %%mm4, %%mm4 \n\t"
        "paddw %%mm7, %%mm4 \n\t"
        "movq %%mm6, %%mm7 \n\t"
        "paddw %%mm6, %%mm6 \n\t"
        "paddw %%mm7, %%mm6 \n\t"
        "paddw %%mm3, %%mm4 \n\t"
        "paddw %%mm5, %%mm6 \n\t"
        "paddw %%mm3, %%mm4 \n\t"
        "paddw %%mm5, %%mm6 \n\t"
        "paddw %%mm3, %%mm4 \n\t"
        "paddw %%mm5, %%mm6 \n\t"
        "paddw %%mm3, %%mm4 \n\t"
        "paddw %%mm5, %%mm6 \n\t"
        "paddw %%mm3, %%mm4 \n\t"
        "paddw %%mm5, %%mm6 \n\t"
        "psrlw $3, %%mm4 \n\t"
        "psrlw $3, %%mm6 \n\t"
        "packuswb %%mm4, %%mm4 \n\t"
        "packuswb %%mm6, %%mm6 \n\t"
        "punpcklbw %%mm6, %%mm4 \n\t"
        "punpcklbw %%mm4, %%mm1 \n\t"
        "punpckhbw %%mm4, %%mm2 \n\t"

        "add $16, %%"REG_S" \n\t"
        "add $8, %%"REG_a" \n\t"
        "add $8, %%"REG_b" \n\t"

        "movq %%mm1, 16(%%"REG_D") \n\t"
        "movq %%mm2, 24(%%"REG_D") \n\t"
        "add $32, %%"REG_D" \n\t"

        "decl %%ecx \n\t"
        "jnz .Lli1 \n\t"
        "emms \n\t"
        "pop %%"REG_BP" \n\t"
        :
        : "S" (y), "D" (dst), "a" (u), "b" (v), "c" (w/16),
#if ARCH_X86_64
        "d" ((x86_reg)us), "r" ((x86_reg)vs)
#else
        "d" (&us)
#endif
        : "memory"
        );
    pack_li_1_C(dst, y, u, v, (w&15), us, vs);
}
#endif /* HAVE_EBX_AVAILABLE */
#endif

static pack_func_t *pack_nn;
static pack_func_t *pack_li_0;
static pack_func_t *pack_li_1;

static void ilpack(unsigned char *dst, unsigned char *src[3],
    int dststride, int srcstride[3], int w, int h, pack_func_t *pack[2])
{
    int i;
    unsigned char *y, *u, *v;
    int ys = srcstride[0], us = srcstride[1], vs = srcstride[2];
    int a, b;

    y = src[0];
    u = src[1];
    v = src[2];

    pack_nn(dst, y, u, v, w, 0, 0);
    y += ys; dst += dststride;
    pack_nn(dst, y, u+us, v+vs, w, 0, 0);
    y += ys; dst += dststride;
    for (i=2; i<h-2; i++) {
        a = (i&2) ? 1 : -1;
        b = (i&1) ^ ((i&2)>>1);
        pack[b](dst, y, u, v, w, us*a, vs*a);
        y += ys;
        if ((i&3) == 1) {
            u -= us;
            v -= vs;
        } else {
            u += us;
            v += vs;
        }
        dst += dststride;
    }
    pack_nn(dst, y, u, v, w, 0, 0);
    y += ys; dst += dststride; u += us; v += vs;
    pack_nn(dst, y, u, v, w, 0, 0);
}


static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts)
{
    mp_image_t *dmpi;

    // hope we'll get DR buffer:
    dmpi=ff_vf_get_image(vf->next, IMGFMT_YUY2,
              MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE,
              mpi->w, mpi->h);

    ilpack(dmpi->planes[0], mpi->planes, dmpi->stride[0], mpi->stride, mpi->w, mpi->h, vf->priv->pack);

    return ff_vf_next_put_image(vf,dmpi, pts);
}

static int config(struct vf_instance *vf,
          int width, int height, int d_width, int d_height,
          unsigned int flags, unsigned int outfmt)
{
    /* FIXME - also support UYVY output? */
    return ff_vf_next_config(vf, width, height, d_width, d_height, flags, IMGFMT_YUY2);
}


static int query_format(struct vf_instance *vf, unsigned int fmt)
{
    /* FIXME - really any YUV 4:2:0 input format should work */
    switch (fmt) {
    case IMGFMT_YV12:
    case IMGFMT_IYUV:
    case IMGFMT_I420:
        return ff_vf_next_query_format(vf,IMGFMT_YUY2);
    }
    return 0;
}

static int vf_open(vf_instance_t *vf, char *args)
{
    vf->config=config;
    vf->query_format=query_format;
    vf->put_image=put_image;
    vf->priv = calloc(1, sizeof(struct vf_priv_s));
    vf->priv->mode = 1;
    if (args) sscanf(args, "%d", &vf->priv->mode);

    pack_nn = pack_nn_C;
    pack_li_0 = pack_li_0_C;
    pack_li_1 = pack_li_1_C;
#if HAVE_MMX
    if(ff_gCpuCaps.hasMMX) {
        pack_nn = pack_nn_MMX;
#if HAVE_EBX_AVAILABLE
        pack_li_0 = pack_li_0_MMX;
        pack_li_1 = pack_li_1_MMX;
#endif
    }
#endif

    switch(vf->priv->mode) {
    case 0:
        vf->priv->pack[0] = vf->priv->pack[1] = pack_nn;
        break;
    default:
        ff_mp_msg(MSGT_VFILTER, MSGL_WARN,
            "ilpack: unknown mode %d (fallback to linear)\n",
            vf->priv->mode);
        /* Fallthrough */
    case 1:
        vf->priv->pack[0] = pack_li_0;
        vf->priv->pack[1] = pack_li_1;
        break;
    }

    return 1;
}

const vf_info_t ff_vf_info_ilpack = {
    "4:2:0 planar -> 4:2:2 packed reinterlacer",
    "ilpack",
    "Richard Felker",
    "",
    vf_open,
    NULL
};
