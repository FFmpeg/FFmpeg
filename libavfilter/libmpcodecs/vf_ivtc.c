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


struct metrics {
    /* difference: total, even lines, odd lines */
    int d, e, o;
    /* noise: temporal, spacial (current), spacial (past) */
    int t, s, p;
};

struct frameinfo {
    /* peak, relative, mean */
    struct metrics p, r, m;
};

struct vf_priv_s {
    struct frameinfo fi[2];
    mp_image_t *dmpi;
    int first;
    int drop, lastdrop, dropnext;
    int inframes, outframes;
};

enum {
    F_DROP,
    F_MERGE,
    F_NEXT,
    F_SHOW
};

#if HAVE_MMX && HAVE_EBX_AVAILABLE
static void block_diffs_MMX(struct metrics *m, unsigned char *old, unsigned char *new, int os, int ns)
{
    int i;
    short out[24]; // output buffer for the partial metrics from the mmx code

    __asm__ (
        "movl $4, %%ecx \n\t"
        "pxor %%mm4, %%mm4 \n\t" // 4 even difference sums
        "pxor %%mm5, %%mm5 \n\t" // 4 odd difference sums
        "pxor %%mm7, %%mm7 \n\t" // all zeros

        ASMALIGN(4)
        "1: \n\t"

        // Even difference
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

        // Odd difference
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
        "paddw %%mm0, %%mm5 \n\t"
        "paddw %%mm1, %%mm5 \n\t"
        "paddw %%mm2, %%mm5 \n\t"
        "paddw %%mm3, %%mm5 \n\t"

        "decl %%ecx \n\t"
        "jnz 1b \n\t"
        "movq %%mm4, (%%"REG_d") \n\t"
        "movq %%mm5, 8(%%"REG_d") \n\t"
        :
        : "S" (old), "D" (new), "a" (os), "b" (ns), "d" (out)
        : "memory"
        );
    m->e = out[0]+out[1]+out[2]+out[3];
    m->o = out[4]+out[5]+out[6]+out[7];
    m->d = m->e + m->o;

    __asm__ (
        // First loop to measure first four columns
        "movl $4, %%ecx \n\t"
        "pxor %%mm4, %%mm4 \n\t" // Past spacial noise
        "pxor %%mm5, %%mm5 \n\t" // Temporal noise
        "pxor %%mm6, %%mm6 \n\t" // Current spacial noise

        ASMALIGN(4)
        "2: \n\t"

        "movq (%%"REG_S"), %%mm0 \n\t"
        "movq (%%"REG_S",%%"REG_a"), %%mm1 \n\t"
        "add %%"REG_a", %%"REG_S" \n\t"
        "add %%"REG_a", %%"REG_S" \n\t"
        "movq (%%"REG_D"), %%mm2 \n\t"
        "movq (%%"REG_D",%%"REG_b"), %%mm3 \n\t"
        "add %%"REG_b", %%"REG_D" \n\t"
        "add %%"REG_b", %%"REG_D" \n\t"
        "punpcklbw %%mm7, %%mm0 \n\t"
        "punpcklbw %%mm7, %%mm1 \n\t"
        "punpcklbw %%mm7, %%mm2 \n\t"
        "punpcklbw %%mm7, %%mm3 \n\t"
        "paddw %%mm1, %%mm4 \n\t"
        "paddw %%mm1, %%mm5 \n\t"
        "paddw %%mm3, %%mm6 \n\t"
        "psubw %%mm0, %%mm4 \n\t"
        "psubw %%mm2, %%mm5 \n\t"
        "psubw %%mm2, %%mm6 \n\t"

        "decl %%ecx \n\t"
        "jnz 2b \n\t"

        "movq %%mm0, %%mm1 \n\t"
        "movq %%mm0, %%mm2 \n\t"
        "movq %%mm0, %%mm3 \n\t"
        "pcmpgtw %%mm4, %%mm1 \n\t"
        "pcmpgtw %%mm5, %%mm2 \n\t"
        "pcmpgtw %%mm6, %%mm3 \n\t"
        "pxor %%mm1, %%mm4 \n\t"
        "pxor %%mm2, %%mm5 \n\t"
        "pxor %%mm3, %%mm6 \n\t"
        "psubw %%mm1, %%mm4 \n\t"
        "psubw %%mm2, %%mm5 \n\t"
        "psubw %%mm3, %%mm6 \n\t"
        "movq %%mm4, (%%"REG_d") \n\t"
        "movq %%mm5, 16(%%"REG_d") \n\t"
        "movq %%mm6, 32(%%"REG_d") \n\t"

        "mov %%"REG_a", %%"REG_c" \n\t"
        "shl $3, %%"REG_c" \n\t"
        "sub %%"REG_c", %%"REG_S" \n\t"
        "mov %%"REG_b", %%"REG_c" \n\t"
        "shl $3, %%"REG_c" \n\t"
        "sub %%"REG_c", %%"REG_D" \n\t"

        // Second loop for the last four columns
        "movl $4, %%ecx \n\t"
        "pxor %%mm4, %%mm4 \n\t"
        "pxor %%mm5, %%mm5 \n\t"
        "pxor %%mm6, %%mm6 \n\t"

        ASMALIGN(4)
        "3: \n\t"

        "movq (%%"REG_S"), %%mm0 \n\t"
        "movq (%%"REG_S",%%"REG_a"), %%mm1 \n\t"
        "add %%"REG_a", %%"REG_S" \n\t"
        "add %%"REG_a", %%"REG_S" \n\t"
        "movq (%%"REG_D"), %%mm2 \n\t"
        "movq (%%"REG_D",%%"REG_b"), %%mm3 \n\t"
        "add %%"REG_b", %%"REG_D" \n\t"
        "add %%"REG_b", %%"REG_D" \n\t"
        "punpckhbw %%mm7, %%mm0 \n\t"
        "punpckhbw %%mm7, %%mm1 \n\t"
        "punpckhbw %%mm7, %%mm2 \n\t"
        "punpckhbw %%mm7, %%mm3 \n\t"
        "paddw %%mm1, %%mm4 \n\t"
        "paddw %%mm1, %%mm5 \n\t"
        "paddw %%mm3, %%mm6 \n\t"
        "psubw %%mm0, %%mm4 \n\t"
        "psubw %%mm2, %%mm5 \n\t"
        "psubw %%mm2, %%mm6 \n\t"

        "decl %%ecx \n\t"
        "jnz 3b \n\t"

        "movq %%mm0, %%mm1 \n\t"
        "movq %%mm0, %%mm2 \n\t"
        "movq %%mm0, %%mm3 \n\t"
        "pcmpgtw %%mm4, %%mm1 \n\t"
        "pcmpgtw %%mm5, %%mm2 \n\t"
        "pcmpgtw %%mm6, %%mm3 \n\t"
        "pxor %%mm1, %%mm4 \n\t"
        "pxor %%mm2, %%mm5 \n\t"
        "pxor %%mm3, %%mm6 \n\t"
        "psubw %%mm1, %%mm4 \n\t"
        "psubw %%mm2, %%mm5 \n\t"
        "psubw %%mm3, %%mm6 \n\t"
        "movq %%mm4, 8(%%"REG_d") \n\t"
        "movq %%mm5, 24(%%"REG_d") \n\t"
        "movq %%mm6, 40(%%"REG_d") \n\t"

        "emms \n\t"
        :
        : "S" (old), "D" (new), "a" ((long)os), "b" ((long)ns), "d" (out)
        : "memory"
        );
    m->p = m->t = m->s = 0;
    for (i=0; i<8; i++) {
        m->p += out[i];
        m->t += out[8+i];
        m->s += out[16+i];
    }
    //printf("e=%d o=%d d=%d p=%d t=%d s=%d\n", m->e, m->o, m->d, m->p, m->t, m->s);
}
#endif

//#define MAG(a) ((a)*(a))
//#define MAG(a) (abs(a))
#define MAG(a) (((a)^((a)>>31))-((a)>>31))

//#define LOWPASS(s) (((s)[-2] + 4*(s)[-1] + 6*(s)[0] + 4*(s)[1] + (s)[2])>>4)
//#define LOWPASS(s) (((s)[-1] + 2*(s)[0] + (s)[1])>>2)
#define LOWPASS(s) ((s)[0])


static void block_diffs_C(struct metrics *m, unsigned char *old, unsigned char *new, int os, int ns)
{
    int x, y, e=0, o=0, s=0, p=0, t=0;
    unsigned char *oldp, *newp;
    m->s = m->p = m->t = 0;
    for (x = 8; x; x--) {
        oldp = old++;
        newp = new++;
        s = p = t = 0;
        for (y = 4; y; y--) {
            e += MAG(newp[0]-oldp[0]);
            o += MAG(newp[ns]-oldp[os]);
            s += newp[ns]-newp[0];
            p += oldp[os]-oldp[0];
            t += oldp[os]-newp[0];
            oldp += os<<1;
            newp += ns<<1;
        }
        m->s += MAG(s);
        m->p += MAG(p);
        m->t += MAG(t);
    }
    m->e = e;
    m->o = o;
    m->d = e+o;
}

static void (*block_diffs)(struct metrics *, unsigned char *, unsigned char *, int, int);

#define MAXUP(a,b) ((a) = ((a)>(b)) ? (a) : (b))

static void diff_planes(struct frameinfo *fi,
    unsigned char *old, unsigned char *new, int w, int h, int os, int ns)
{
    int x, y;
    struct metrics l;
    struct metrics *peak=&fi->p, *rel=&fi->r, *mean=&fi->m;
    memset(peak, 0, sizeof(struct metrics));
    memset(rel, 0, sizeof(struct metrics));
    memset(mean, 0, sizeof(struct metrics));
    for (y = 0; y < h-7; y += 8) {
        for (x = 8; x < w-8-7; x += 8) {
            block_diffs(&l, old+x+y*os, new+x+y*ns, os, ns);
            mean->d += l.d;
            mean->e += l.e;
            mean->o += l.o;
            mean->s += l.s;
            mean->p += l.p;
            mean->t += l.t;
            MAXUP(peak->d, l.d);
            MAXUP(peak->e, l.e);
            MAXUP(peak->o, l.o);
            MAXUP(peak->s, l.s);
            MAXUP(peak->p, l.p);
            MAXUP(peak->t, l.t);
            MAXUP(rel->e, l.e-l.o);
            MAXUP(rel->o, l.o-l.e);
            MAXUP(rel->s, l.s-l.t);
            MAXUP(rel->p, l.p-l.t);
            MAXUP(rel->t, l.t-l.p);
            MAXUP(rel->d, l.t-l.s); /* hack */
        }
    }
    x = (w/8-2)*(h/8);
    mean->d /= x;
    mean->e /= x;
    mean->o /= x;
    mean->s /= x;
    mean->p /= x;
    mean->t /= x;
}

static void diff_fields(struct frameinfo *fi, mp_image_t *old, mp_image_t *new)
{
    diff_planes(fi, old->planes[0], new->planes[0],
        new->w, new->h, old->stride[0], new->stride[0]);
}

static void stats(struct frameinfo *f)
{
    mp_msg(MSGT_VFILTER, MSGL_V, "       pd=%d re=%d ro=%d rp=%d rt=%d rs=%d rd=%d pp=%d pt=%d ps=%d\r",
        f->p.d, f->r.e, f->r.o, f->r.p, f->r.t, f->r.s, f->r.d, f->p.p, f->p.t, f->p.s);
}

static int foo(struct vf_priv_s *p, mp_image_t *new, mp_image_t *cur)
{
    struct frameinfo *f = p->fi;

    f[0] = f[1];
    diff_fields(&f[1], cur, new);
    stats(&f[1]);

    // Immediately drop this frame if it's already been used.
    if (p->dropnext) {
        p->dropnext = 0;
        return F_DROP;
    }

    // Sometimes a pulldown frame comes all by itself, so both
    // its top and bottom field are duplicates from the adjacent
    // two frames. We can just drop such a frame, but we
    // immediately show the next frame instead to keep the frame
    // drops evenly spaced during normal 3:2 pulldown sequences.
    if ((3*f[1].r.o < f[1].r.e) && (f[1].r.s < f[1].r.d)) {
        p->dropnext = 1;
        return F_NEXT;
    }

    // If none of these conditions hold, we will consider the frame
    // progressive and just show it as-is.
    if (!(  (3*f[0].r.e < f[0].r.o) ||
        ((2*f[0].r.d < f[0].r.s) && (f[0].r.s > 1200)) ||
        ((2*f[1].r.t < f[1].r.p) && (f[1].r.p > 1200))  ))
        return F_SHOW;

    // Otherwise, we have to decide whether to merge or drop.
    // If the noise metric only increases minimally, we're off
    // to a good start...
    if (((2*f[1].r.t < 3*f[1].r.p) && (f[1].r.t < 3600)) ||
        (f[1].r.t < 900) || (f[1].r.d < 900)) {
        // ...and if noise decreases or the duplicate even field
        // is detected, we go ahead with the merge.
        if ((3*f[0].r.e < f[0].r.o) || (2*f[1].r.t < f[1].r.p)) {
            p->dropnext = 1;
            return F_MERGE;
        }
    }
    return F_DROP;
}



static void copy_image(mp_image_t *dmpi, mp_image_t *mpi, int field)
{
    switch (field) {
    case 0:
        my_memcpy_pic(dmpi->planes[0], mpi->planes[0], mpi->w, mpi->h/2,
            dmpi->stride[0]*2, mpi->stride[0]*2);
        if (mpi->flags & MP_IMGFLAG_PLANAR) {
            my_memcpy_pic(dmpi->planes[1], mpi->planes[1],
                mpi->chroma_width, mpi->chroma_height/2,
                dmpi->stride[1]*2, mpi->stride[1]*2);
            my_memcpy_pic(dmpi->planes[2], mpi->planes[2],
                mpi->chroma_width, mpi->chroma_height/2,
                dmpi->stride[2]*2, mpi->stride[2]*2);
        }
        break;
    case 1:
        my_memcpy_pic(dmpi->planes[0]+dmpi->stride[0],
            mpi->planes[0]+mpi->stride[0], mpi->w, mpi->h/2,
            dmpi->stride[0]*2, mpi->stride[0]*2);
        if (mpi->flags & MP_IMGFLAG_PLANAR) {
            my_memcpy_pic(dmpi->planes[1]+dmpi->stride[1],
                mpi->planes[1]+mpi->stride[1],
                mpi->chroma_width, mpi->chroma_height/2,
                dmpi->stride[1]*2, mpi->stride[1]*2);
            my_memcpy_pic(dmpi->planes[2]+dmpi->stride[2],
                mpi->planes[2]+mpi->stride[2],
                mpi->chroma_width, mpi->chroma_height/2,
                dmpi->stride[2]*2, mpi->stride[2]*2);
        }
        break;
    case 2:
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
        break;
    }
}

static int do_put_image(struct vf_instance *vf, mp_image_t *dmpi)
{
    struct vf_priv_s *p = vf->priv;
    int dropflag=0;

    if (!p->dropnext) switch (p->drop) {
    case 0:
        dropflag = 0;
        break;
    case 1:
        dropflag = (++p->lastdrop >= 5);
        break;
    case 2:
        dropflag = (++p->lastdrop >= 5) && (4*p->inframes <= 5*p->outframes);
        break;
    }

    if (dropflag) {
        //mp_msg(MSGT_VFILTER, MSGL_V, "drop! [%d/%d=%g]\n",
        //    p->outframes, p->inframes, (float)p->outframes/p->inframes);
        mp_msg(MSGT_VFILTER, MSGL_V, "!");
        p->lastdrop = 0;
        return 0;
    }

    p->outframes++;
    return vf_next_put_image(vf, dmpi, MP_NOPTS_VALUE);
}

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts)
{
    int ret=0;
    struct vf_priv_s *p = vf->priv;

    p->inframes++;

    if (p->first) { /* hack */
        p->first = 0;
        return 1;
    }

    if (!p->dmpi) p->dmpi = vf_get_image(vf->next, mpi->imgfmt,
        MP_IMGTYPE_STATIC, MP_IMGFLAG_ACCEPT_STRIDE |
        MP_IMGFLAG_PRESERVE | MP_IMGFLAG_READABLE,
        mpi->width, mpi->height);
    /* FIXME -- not correct, off by one frame! */
    p->dmpi->qscale = mpi->qscale;
    p->dmpi->qstride = mpi->qstride;
    p->dmpi->qscale_type = mpi->qscale_type;

    switch (foo(p, mpi, p->dmpi)) {
    case F_DROP:
        copy_image(p->dmpi, mpi, 2);
        ret = 0;
        p->lastdrop = 0;
        mp_msg(MSGT_VFILTER, MSGL_V, "DROP\n");
        break;
    case F_MERGE:
        copy_image(p->dmpi, mpi, 0);
        ret = do_put_image(vf, p->dmpi);
        copy_image(p->dmpi, mpi, 1);
        mp_msg(MSGT_VFILTER, MSGL_V, "MERGE\n");
        p->dmpi = NULL;
        break;
    case F_NEXT:
        copy_image(p->dmpi, mpi, 2);
        ret = do_put_image(vf, p->dmpi);
        mp_msg(MSGT_VFILTER, MSGL_V, "NEXT\n");
        p->dmpi = NULL;
        break;
    case F_SHOW:
        ret = do_put_image(vf, p->dmpi);
        copy_image(p->dmpi, mpi, 2);
        mp_msg(MSGT_VFILTER, MSGL_V, "OK\n");
        p->dmpi = NULL;
        break;
    }
    return ret;
}

static int query_format(struct vf_instance *vf, unsigned int fmt)
{
    switch (fmt) {
    case IMGFMT_YV12:
    case IMGFMT_IYUV:
    case IMGFMT_I420:
        return vf_next_query_format(vf, fmt);
    }
    return 0;
}

static void uninit(struct vf_instance *vf)
{
    free(vf->priv);
}

static int vf_open(vf_instance_t *vf, char *args)
{
    struct vf_priv_s *p;
    vf->put_image = put_image;
    vf->query_format = query_format;
    vf->uninit = uninit;
    vf->default_reqs = VFCAP_ACCEPT_STRIDE;
    vf->priv = p = calloc(1, sizeof(struct vf_priv_s));
    p->drop = 0;
    p->first = 1;
    if (args) sscanf(args, "%d", &p->drop);
    block_diffs = block_diffs_C;
#if HAVE_MMX && HAVE_EBX_AVAILABLE
    if(gCpuCaps.hasMMX) block_diffs = block_diffs_MMX;
#endif
    return 1;
}

const vf_info_t vf_info_ivtc = {
    "inverse telecine, take 2",
    "ivtc",
    "Rich Felker",
    "",
    vf_open,
    NULL
};
