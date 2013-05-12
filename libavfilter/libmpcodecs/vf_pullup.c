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

#include "pullup.h"

#undef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))

struct vf_priv_s {
    struct pullup_context *ctx;
    int init;
    int fakecount;
    char *qbuf;
};

static void init_pullup(struct vf_instance *vf, mp_image_t *mpi)
{
    struct pullup_context *c = vf->priv->ctx;

    if (mpi->flags & MP_IMGFLAG_PLANAR) {
        c->format = PULLUP_FMT_Y;
        c->nplanes = 4;
        ff_pullup_preinit_context(c);
        c->bpp[0] = c->bpp[1] = c->bpp[2] = 8;
        c->w[0] = mpi->w;
        c->h[0] = mpi->h;
        c->w[1] = c->w[2] = mpi->chroma_width;
        c->h[1] = c->h[2] = mpi->chroma_height;
        c->w[3] = ((mpi->w+15)/16) * ((mpi->h+15)/16);
        c->h[3] = 2;
        c->stride[0] = mpi->width;
        c->stride[1] = c->stride[2] = mpi->chroma_width;
        c->stride[3] = c->w[3];
        c->background[1] = c->background[2] = 128;
    }

    if (ff_gCpuCaps.hasMMX) c->cpu |= PULLUP_CPU_MMX;
    if (ff_gCpuCaps.hasMMX2) c->cpu |= PULLUP_CPU_MMX2;
    if (ff_gCpuCaps.has3DNow) c->cpu |= PULLUP_CPU_3DNOW;
    if (ff_gCpuCaps.has3DNowExt) c->cpu |= PULLUP_CPU_3DNOWEXT;
    if (ff_gCpuCaps.hasSSE) c->cpu |= PULLUP_CPU_SSE;
    if (ff_gCpuCaps.hasSSE2) c->cpu |= PULLUP_CPU_SSE2;

    ff_pullup_init_context(c);

    vf->priv->init = 1;
    vf->priv->qbuf = malloc(c->w[3]);
}


#if 0
static void get_image(struct vf_instance *vf, mp_image_t *mpi)
{
    struct pullup_context *c = vf->priv->ctx;
    struct pullup_buffer *b;

    if (mpi->type == MP_IMGTYPE_STATIC) return;

    if (!vf->priv->init) init_pullup(vf, mpi);

    b = ff_pullup_get_buffer(c, 2);
    if (!b) return; /* shouldn't happen... */

    mpi->priv = b;

    mpi->planes[0] = b->planes[0];
    mpi->planes[1] = b->planes[1];
    mpi->planes[2] = b->planes[2];
    mpi->stride[0] = c->stride[0];
    mpi->stride[1] = c->stride[1];
    mpi->stride[2] = c->stride[2];

    mpi->flags |= MP_IMGFLAG_DIRECT;
    mpi->flags &= ~MP_IMGFLAG_DRAW_CALLBACK;
}
#endif

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts)
{
    struct pullup_context *c = vf->priv->ctx;
    struct pullup_buffer *b;
    struct pullup_frame *f;
    mp_image_t *dmpi;
    int ret;
    int p;
    int i;

    if (!vf->priv->init) init_pullup(vf, mpi);

    if (mpi->flags & MP_IMGFLAG_DIRECT) {
        b = mpi->priv;
        mpi->priv = 0;
    } else {
        b = ff_pullup_get_buffer(c, 2);
        if (!b) {
            ff_mp_msg(MSGT_VFILTER,MSGL_ERR,"Could not get buffer from pullup!\n");
            f = ff_pullup_get_frame(c);
            ff_pullup_release_frame(f);
            return 0;
        }
        memcpy_pic(b->planes[0], mpi->planes[0], mpi->w, mpi->h,
            c->stride[0], mpi->stride[0]);
        if (mpi->flags & MP_IMGFLAG_PLANAR) {
            memcpy_pic(b->planes[1], mpi->planes[1],
                mpi->chroma_width, mpi->chroma_height,
                c->stride[1], mpi->stride[1]);
            memcpy_pic(b->planes[2], mpi->planes[2],
                mpi->chroma_width, mpi->chroma_height,
                c->stride[2], mpi->stride[2]);
        }
    }
    if (mpi->qscale) {
        fast_memcpy(b->planes[3], mpi->qscale, c->w[3]);
        fast_memcpy(b->planes[3]+c->w[3], mpi->qscale, c->w[3]);
    }

    p = mpi->fields & MP_IMGFIELD_TOP_FIRST ? 0 :
        (mpi->fields & MP_IMGFIELD_ORDERED ? 1 : 0);
    ff_pullup_submit_field(c, b, p);
    ff_pullup_submit_field(c, b, p^1);
    if (mpi->fields & MP_IMGFIELD_REPEAT_FIRST)
        ff_pullup_submit_field(c, b, p);

    ff_pullup_release_buffer(b, 2);

    f = ff_pullup_get_frame(c);

    /* Fake yes for first few frames (buffer depth) to keep from
     * breaking A/V sync with G1's bad architecture... */
    if (!f) return vf->priv->fakecount ? (--vf->priv->fakecount,1) : 0;

    if (f->length < 2) {
        ff_pullup_release_frame(f);
        f = ff_pullup_get_frame(c);
        if (!f) return 0;
        if (f->length < 2) {
            ff_pullup_release_frame(f);
            if (!(mpi->fields & MP_IMGFIELD_REPEAT_FIRST))
                return 0;
            f = ff_pullup_get_frame(c);
            if (!f) return 0;
            if (f->length < 2) {
                ff_pullup_release_frame(f);
                return 0;
            }
        }
    }

#if 0
    /* Average qscale tables from both frames. */
    if (mpi->qscale) {
        for (i=0; i<c->w[3]; i++) {
            vf->priv->qbuf[i] = (f->ofields[0]->planes[3][i]
                + f->ofields[1]->planes[3][i+c->w[3]])>>1;
        }
    }
#else
    /* Take worst of qscale tables from both frames. */
    if (mpi->qscale) {
        for (i=0; i<c->w[3]; i++) {
            vf->priv->qbuf[i] = MAX(f->ofields[0]->planes[3][i], f->ofields[1]->planes[3][i+c->w[3]]);
        }
    }
#endif

    /* If the frame isn't already exportable... */
    while (!f->buffer) {
        dmpi = ff_vf_get_image(vf->next, mpi->imgfmt,
            MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE,
            mpi->width, mpi->height);
        /* FIXME: Is it ok to discard dmpi if it's not direct? */
        if (!(dmpi->flags & MP_IMGFLAG_DIRECT)) {
            ff_pullup_pack_frame(c, f);
            break;
        }
        /* Direct render fields into output buffer */
        my_memcpy_pic(dmpi->planes[0], f->ofields[0]->planes[0],
            mpi->w, mpi->h/2, dmpi->stride[0]*2, c->stride[0]*2);
        my_memcpy_pic(dmpi->planes[0] + dmpi->stride[0],
            f->ofields[1]->planes[0] + c->stride[0],
            mpi->w, mpi->h/2, dmpi->stride[0]*2, c->stride[0]*2);
        if (mpi->flags & MP_IMGFLAG_PLANAR) {
            my_memcpy_pic(dmpi->planes[1], f->ofields[0]->planes[1],
                mpi->chroma_width, mpi->chroma_height/2,
                dmpi->stride[1]*2, c->stride[1]*2);
            my_memcpy_pic(dmpi->planes[1] + dmpi->stride[1],
                f->ofields[1]->planes[1] + c->stride[1],
                mpi->chroma_width, mpi->chroma_height/2,
                dmpi->stride[1]*2, c->stride[1]*2);
            my_memcpy_pic(dmpi->planes[2], f->ofields[0]->planes[2],
                mpi->chroma_width, mpi->chroma_height/2,
                dmpi->stride[2]*2, c->stride[2]*2);
            my_memcpy_pic(dmpi->planes[2] + dmpi->stride[2],
                f->ofields[1]->planes[2] + c->stride[2],
                mpi->chroma_width, mpi->chroma_height/2,
                dmpi->stride[2]*2, c->stride[2]*2);
        }
        ff_pullup_release_frame(f);
        if (mpi->qscale) {
            dmpi->qscale = vf->priv->qbuf;
            dmpi->qstride = mpi->qstride;
            dmpi->qscale_type = mpi->qscale_type;
        }
        return ff_vf_next_put_image(vf, dmpi, MP_NOPTS_VALUE);
    }
    dmpi = ff_vf_get_image(vf->next, mpi->imgfmt,
        MP_IMGTYPE_EXPORT, MP_IMGFLAG_ACCEPT_STRIDE,
        mpi->width, mpi->height);

    dmpi->planes[0] = f->buffer->planes[0];
    dmpi->planes[1] = f->buffer->planes[1];
    dmpi->planes[2] = f->buffer->planes[2];

    dmpi->stride[0] = c->stride[0];
    dmpi->stride[1] = c->stride[1];
    dmpi->stride[2] = c->stride[2];

    if (mpi->qscale) {
        dmpi->qscale = vf->priv->qbuf;
        dmpi->qstride = mpi->qstride;
        dmpi->qscale_type = mpi->qscale_type;
    }
    ret = ff_vf_next_put_image(vf, dmpi, MP_NOPTS_VALUE);
    ff_pullup_release_frame(f);
    return ret;
}

static int query_format(struct vf_instance *vf, unsigned int fmt)
{
    /* FIXME - support more formats */
    switch (fmt) {
    case IMGFMT_YV12:
    case IMGFMT_IYUV:
    case IMGFMT_I420:
        return ff_vf_next_query_format(vf, fmt);
    }
    return 0;
}

static int config(struct vf_instance *vf,
    int width, int height, int d_width, int d_height,
    unsigned int flags, unsigned int outfmt)
{
    if (height&3) {
        ff_mp_msg(MSGT_VFILTER, MSGL_ERR, "height must be divisible by four\n");
        return 0;
    }
    return ff_vf_next_config(vf, width, height, d_width, d_height, flags, outfmt);
}

static void uninit(struct vf_instance *vf)
{
    ff_pullup_free_context(vf->priv->ctx);
    free(vf->priv);
}

static int vf_open(vf_instance_t *vf, char *args)
{
    struct vf_priv_s *p;
    struct pullup_context *c;
    //vf->get_image = get_image;
    vf->put_image = put_image;
    vf->config = config;
    vf->query_format = query_format;
    vf->uninit = uninit;
    vf->default_reqs = VFCAP_ACCEPT_STRIDE;
    vf->priv = p = calloc(1, sizeof(struct vf_priv_s));
    p->ctx = c = ff_pullup_alloc_context();
    p->fakecount = 1;
    c->junk_left = c->junk_right = 1;
    c->junk_top = c->junk_bottom = 4;
    c->strict_breaks = 0;
    c->metric_plane = 0;
    if (args) {
        sscanf(args, "%d:%d:%d:%d:%d:%d", &c->junk_left, &c->junk_right, &c->junk_top, &c->junk_bottom, &c->strict_breaks, &c->metric_plane);
    }
    return 1;
}

const vf_info_t ff_vf_info_pullup = {
    "pullup (from field sequence to frames)",
    "pullup",
    "Rich Felker",
    "",
    vf_open,
    NULL
};
