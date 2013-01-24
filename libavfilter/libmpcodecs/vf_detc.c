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

#include "img_format.h"
#include "mp_image.h"
#include "vf.h"

#include "libvo/fastmemcpy.h"

struct metrics {
        int even;
        int odd;
        int noise;
        int temp;
};

struct vf_priv_s {
        int frame;
        int drop, lastdrop;
        struct metrics pm;
        int thres[5];
        int inframes, outframes;
        int mode;
        int (*analyze)(struct vf_priv_s *, mp_image_t *, mp_image_t *);
        int needread;
};

#define COMPE(a,b,e) (abs((a)-(b)) < (((a)+(b))>>(e)))
#define COMPARABLE(a,b) COMPE((a),(b),2)
#define VERYCLOSE(a,b) COMPE((a),(b),3)

#define OUTER_TC_NBHD(s) ( \
 COMPARABLE((s)[-1].m.even,(s)[-1].m.odd) && \
 COMPARABLE((s)[1].m.even,(s)[0].m.odd) && \
 COMPARABLE((s)[2].m.even,(s)[1].m.odd) && \
 COMPARABLE((s)[-1].m.noise,(s)[0].m.temp) && \
 COMPARABLE((s)[2].m.noise,(s)[2].m.temp) )

#define INNER_TC_NBHD(s,l,h) ( \
 COMPARABLE((s)[0].m.even,(l)) && \
 COMPARABLE((s)[2].m.odd,(l)) && ( \
 COMPARABLE((s)[0].m.noise,(h)) || \
 COMPARABLE((s)[1].m.noise,(h)) ) )

enum {
        TC_DROP,
        TC_PROG,
        TC_IL1,
        TC_IL2
};

static void block_diffs(struct metrics *m, unsigned char *old, unsigned char *new, int os, int ns)
{
        int x, y, even=0, odd=0, noise, temp;
        unsigned char *oldp, *newp;
        m->noise = m->temp = 0;
        for (x = 8; x; x--) {
                oldp = old++;
                newp = new++;
                noise = temp = 0;
                for (y = 4; y; y--) {
                        even += abs(newp[0]-oldp[0]);
                        odd += abs(newp[ns]-oldp[os]);
                        noise += newp[ns]-newp[0];
                        temp += oldp[os]-newp[0];
                        oldp += os<<1;
                        newp += ns<<1;
                }
                m->noise += abs(noise);
                m->temp += abs(temp);
        }
        m->even = even;
        m->odd = odd;
}

static void diff_planes(struct metrics *m, unsigned char *old, unsigned char *new, int w, int h, int os, int ns)
{
        int x, y, me=0, mo=0, mn=0, mt=0;
        struct metrics l;
        for (y = 0; y < h-7; y += 8) {
                for (x = 0; x < w-7; x += 8) {
                        block_diffs(&l, old+x+y*os, new+x+y*ns, os, ns);
                        if (l.even > me) me = l.even;
                        if (l.odd > mo) mo = l.odd;
                        if (l.noise > mn) mn = l.noise;
                        if (l.temp > mt) mt = l.temp;
                }
        }
        m->even = me;
        m->odd = mo;
        m->noise = mn;
        m->temp = mt;
}

static void diff_fields(struct metrics *metr, mp_image_t *old, mp_image_t *new)
{
        struct metrics m, mu, mv;
        diff_planes(&m, old->planes[0], new->planes[0],
                new->w, new->h, old->stride[0], new->stride[0]);
        if (new->flags & MP_IMGFLAG_PLANAR) {
                diff_planes(&mu, old->planes[1], new->planes[1],
                        new->chroma_width, new->chroma_height,
                        old->stride[1], new->stride[1]);
                diff_planes(&mv, old->planes[2], new->planes[2],
                        new->chroma_width, new->chroma_height,
                        old->stride[2], new->stride[2]);
                if (mu.even > m.even) m.even = mu.even;
                if (mu.odd > m.odd) m.odd = mu.odd;
                if (mu.noise > m.noise) m.noise = mu.noise;
                if (mu.temp > m.temp) m.temp = mu.temp;
                if (mv.even > m.even) m.even = mv.even;
                if (mv.odd > m.odd) m.odd = mv.odd;
                if (mv.noise > m.noise) m.noise = mv.noise;
                if (mv.temp > m.temp) m.temp = mv.temp;
        }
        *metr = m;
}

static void status(int f, struct metrics *m)
{
        ff_mp_msg(MSGT_VFILTER, MSGL_V, "frame %d: e=%d o=%d n=%d t=%d\n",
                f, m->even, m->odd, m->noise, m->temp);
}

static int analyze_fixed_pattern(struct vf_priv_s *p, mp_image_t *new, mp_image_t *old)
{
        if (p->frame >= 0) p->frame = (p->frame+1)%5;
        ff_mp_msg(MSGT_VFILTER, MSGL_V, "frame %d\n", p->frame);
        switch (p->frame) {
        case -1: case 0: case 1: case 2:
                return TC_PROG;
        case 3:
                return TC_IL1;
        case 4:
                return TC_IL2;
        }
        return 0;
}

static int analyze_aggressive(struct vf_priv_s *p, mp_image_t *new, mp_image_t *old)
{
        struct metrics m, pm;

        if (p->frame >= 0) p->frame = (p->frame+1)%5;

        diff_fields(&m, old, new);

        status(p->frame, &m);

        pm = p->pm;
        p->pm = m;

        if (p->frame == 4) {
                /* We need to break at scene changes, but is this a valid test? */
                if ((m.even > p->thres[2]) && (m.odd > p->thres[2]) && (m.temp > p->thres[3])
                        && (m.temp > 5*pm.temp) && (m.temp*2 > m.noise)) {
                        ff_mp_msg(MSGT_VFILTER, MSGL_V, "scene change breaking telecine!\n");
                        p->frame = -1;
                        return TC_DROP;
                }
                /* Thres. is to compensate for quantization errors when noise is low */
                if (m.noise - m.temp > -p->thres[4]) {
                        if (COMPARABLE(m.even, pm.odd)) {
                                //ff_mp_msg(MSGT_VFILTER, MSGL_V, "confirmed field match!\n");
                                return TC_IL2;
                        } else if ((m.even < p->thres[0]) && (m.odd < p->thres[0]) && VERYCLOSE(m.even, m.odd)
                                && VERYCLOSE(m.noise,m.temp) && VERYCLOSE(m.noise,pm.noise)) {
                                ff_mp_msg(MSGT_VFILTER, MSGL_V, "interlaced frame appears in duplicate!!!\n");
                                p->pm = pm; /* hack :) */
                                p->frame = 3;
                                return TC_IL1;
                        }
                } else {
                        ff_mp_msg(MSGT_VFILTER, MSGL_V, "mismatched telecine fields!\n");
                        p->frame = -1;
                }
        }

        if (2*m.even*m.temp < m.odd*m.noise) {
                ff_mp_msg(MSGT_VFILTER, MSGL_V, "caught telecine sync!\n");
                p->frame = 3;
                return TC_IL1;
        }

        if (p->frame < 3) {
                if (m.noise > p->thres[3]) {
                        if (m.noise > 2*m.temp) {
                                ff_mp_msg(MSGT_VFILTER, MSGL_V, "merging fields out of sequence!\n");
                                return TC_IL2;
                        }
                        if ((m.noise > 2*pm.noise) && (m.even > p->thres[2]) && (m.odd > p->thres[2])) {
                                ff_mp_msg(MSGT_VFILTER, MSGL_V, "dropping horrible interlaced frame!\n");
                                return TC_DROP;
                        }
                }
        }

        switch (p->frame) {
        case -1:
                if (4*m.noise > 5*m.temp) {
                        ff_mp_msg(MSGT_VFILTER, MSGL_V, "merging fields out of sequence!\n");
                        return TC_IL2;
                }
        case 0:
        case 1:
        case 2:
                return TC_PROG;
        case 3:
                if ((m.even > p->thres[1]) && (m.even > m.odd) && (m.temp > m.noise)) {
                        ff_mp_msg(MSGT_VFILTER, MSGL_V, "lost telecine tracking!\n");
                        p->frame = -1;
                        return TC_PROG;
                }
                return TC_IL1;
        case 4:
                return TC_IL2;
        }
        return 0;
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
        int dropflag;

        switch (p->drop) {
        default:
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
                ff_mp_msg(MSGT_VFILTER, MSGL_V, "drop! [%d/%d=%g]\n",
                        p->outframes, p->inframes, (float)p->outframes/p->inframes);
                p->lastdrop = 0;
                return 0;
        }

        p->outframes++;
        return ff_vf_next_put_image(vf, dmpi, MP_NOPTS_VALUE);
}

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts)
{
        int ret=0;
        mp_image_t *dmpi;
        struct vf_priv_s *p = vf->priv;

        p->inframes++;

        if (p->needread) dmpi = ff_vf_get_image(vf->next, mpi->imgfmt,
                MP_IMGTYPE_STATIC, MP_IMGFLAG_ACCEPT_STRIDE |
                MP_IMGFLAG_PRESERVE | MP_IMGFLAG_READABLE,
                mpi->width, mpi->height);
        /* FIXME: is there a good way to get rid of static type? */
        else dmpi = ff_vf_get_image(vf->next, mpi->imgfmt,
                MP_IMGTYPE_STATIC, MP_IMGFLAG_ACCEPT_STRIDE |
                MP_IMGFLAG_PRESERVE, mpi->width, mpi->height);

        switch (p->analyze(p, mpi, dmpi)) {
        case TC_DROP:
                /* Don't copy anything unless we'll need to read it. */
                if (p->needread) copy_image(dmpi, mpi, 2);
                p->lastdrop = 0;
                break;
        case TC_PROG:
                /* Copy and display the whole frame. */
                copy_image(dmpi, mpi, 2);
                ret = do_put_image(vf, dmpi);
                break;
        case TC_IL1:
                /* Only copy bottom field unless we need to read. */
                if (p->needread) copy_image(dmpi, mpi, 2);
                else copy_image(dmpi, mpi, 1);
                p->lastdrop = 0;
                break;
        case TC_IL2:
                /* Copy top field and show frame, then copy bottom if needed. */
                copy_image(dmpi, mpi, 0);
                ret = do_put_image(vf, dmpi);
                if (p->needread) copy_image(dmpi, mpi, 1);
                break;
        }
        return ret;
}

static int query_format(struct vf_instance *vf, unsigned int fmt)
{
        /* FIXME - figure out which other formats work */
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
        return ff_vf_next_config(vf,width,height,d_width,d_height,flags,outfmt);
}

static void uninit(struct vf_instance *vf)
{
        free(vf->priv);
}

static struct {
        const char *name;
        int (*func)(struct vf_priv_s *p, mp_image_t *new, mp_image_t *old);
        int needread;
} anal_funcs[] = {
        { "fixed", analyze_fixed_pattern, 0 },
        { "aggressive", analyze_aggressive, 1 },
        { NULL, NULL, 0 }
};

#define STARTVARS if (0)
#define GETVAR(str, name, out, func) \
 else if (!strncmp((str), name "=", sizeof(name))) \
 (out) = (func)((str) + sizeof(name))

static void parse_var(struct vf_priv_s *p, char *var)
{
        STARTVARS;
        GETVAR(var, "dr", p->drop, atoi);
        GETVAR(var, "t0", p->thres[0], atoi);
        GETVAR(var, "t1", p->thres[1], atoi);
        GETVAR(var, "t2", p->thres[2], atoi);
        GETVAR(var, "t3", p->thres[3], atoi);
        GETVAR(var, "t4", p->thres[4], atoi);
        GETVAR(var, "fr", p->frame, atoi);
        GETVAR(var, "am", p->mode, atoi);
}

static void parse_args(struct vf_priv_s *p, char *args)
{
        char *next, *orig;
        for (args=orig=strdup(args); args; args=next) {
                next = strchr(args, ':');
                if (next) *next++ = 0;
                parse_var(p, args);
        }
        free(orig);
}

static int vf_open(vf_instance_t *vf, char *args)
{
        struct vf_priv_s *p;
        vf->config = config;
        vf->put_image = put_image;
        vf->query_format = query_format;
        vf->uninit = uninit;
        vf->default_reqs = VFCAP_ACCEPT_STRIDE;
        vf->priv = p = calloc(1, sizeof(struct vf_priv_s));
        p->frame = -1;
        p->thres[0] = 440;
        p->thres[1] = 720;
        p->thres[2] = 2500;
        p->thres[3] = 2500;
        p->thres[4] = 800;
        p->drop = 0;
        p->mode = 1;
        if (args) parse_args(p, args);
        p->analyze = anal_funcs[p->mode].func;
        p->needread = anal_funcs[p->mode].needread;
        return 1;
}

const vf_info_t ff_vf_info_detc = {
    "de-telecine filter",
    "detc",
    "Rich Felker",
    "",
    vf_open,
    NULL
};
