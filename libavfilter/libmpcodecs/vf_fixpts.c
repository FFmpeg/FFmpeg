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
#include "help_mp.h"

#include "img_format.h"
#include "mp_image.h"
#include "vf.h"

struct vf_priv_s {
    double current;
    double step;
    int autostart;
    int autostep;
    unsigned have_step:1;
    unsigned print:1;
};

static int put_image(vf_instance_t *vf, mp_image_t *src, double pts)
{
    struct vf_priv_s *p = vf->priv;

    if (p->print) {
        if (pts == MP_NOPTS_VALUE)
            mp_msg(MSGT_VFILTER, MSGL_INFO, "PTS: undef\n");
        else
            mp_msg(MSGT_VFILTER, MSGL_INFO, "PTS: %f\n", pts);
    }
    if (pts != MP_NOPTS_VALUE && p->autostart != 0) {
        p->current = pts;
        if (p->autostart > 0)
            p->autostart--;
    } else if (pts != MP_NOPTS_VALUE && p->autostep > 0) {
        p->step = pts - p->current;
        p->current = pts;
        p->autostep--;
        p->have_step = 1;
    } else if (p->have_step) {
        p->current += p->step;
        pts = p->current;
    } else {
        pts = MP_NOPTS_VALUE;
    }
    return vf_next_put_image(vf, src, pts);
}

static void uninit(vf_instance_t *vf)
{
    free(vf->priv);
}

static int parse_args(struct vf_priv_s *p, const char *args)
{
    int pos;
    double num, denom = 1;
    int iarg;

    while (*args != 0) {
        pos = 0;
        if (sscanf(args, "print%n", &pos) == 0 && pos > 0) {
            p->print = 1;
        } else if (sscanf(args, "fps=%lf%n/%lf%n", &num, &pos, &denom, &pos) >=
                   1 && pos > 0) {
            p->step = denom / num;
            p->have_step = 1;
        } else if (sscanf(args, "start=%lf%n", &num, &pos) >= 1 && pos > 0) {
            p->current = num;
        } else if (sscanf(args, "autostart=%d%n", &iarg, &pos) == 1 && pos > 0) {
            p->autostart = iarg;
        } else if (sscanf(args, "autofps=%d%n", &iarg, &pos) == 1 && pos > 0) {
            p->autostep = iarg;
        } else {
            mp_msg(MSGT_VFILTER, MSGL_FATAL,
                   "fixpts: unknown suboption: %s\n", args);
            return 0;
        }
        args += pos;
        if (*args == ':')
            args++;
    }
    return 1;
}

static int open(vf_instance_t *vf, char *args)
{
    struct vf_priv_s *p;
    struct vf_priv_s ptmp = {
        .current = 0,
        .step = 0,
        .autostart = 0,
        .autostep = 0,
        .have_step = 0,
        .print = 0,
    };

    if (!parse_args(&ptmp, args == NULL ? "" : args))
        return 0;

    vf->put_image = put_image;
    vf->uninit = uninit;
    vf->priv = p = malloc(sizeof(struct vf_priv_s));
    *p = ptmp;
    p->current = -p->step;

    return 1;
}

const vf_info_t vf_info_fixpts = {
    "Fix presentation timestamps",
    "fixpts",
    "Nicolas George",
    "",
    &open,
    NULL
};
