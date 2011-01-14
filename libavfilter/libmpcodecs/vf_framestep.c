/*
 * filter to ouput only 1 every n frame, or only the I (key)frame
 *
 * The parameters are:
 *
 *    [I] | [i]num
 *
 * if you call the filter with I (uppercase) as the parameter
 *    ... -vf framestep=I ...
 * then ONLY the keyframes are outputted.
 * For DVD it means, generally, one every 15 frames (IBBPBBPBBPBBPBB), for avi it means
 * every scene change or every keyint value (see -lavcopts).
 *
 * if you call the filter with the i (lowercase)
 *    ... -vf framestep=i ...
 * then a I! followed by a cr is printed when a key frame (eg Intra frame) is
 * found, leaving the current line of mplayer/mencoder, where you got the
 * time, in seconds, and frame of the key. Use this information to split the
 * AVI.
 *
 * After the i or alone you can put a positive number and only one frame every
 * x (the number you set) is passed on the filter chain, limiting the output
 * of the frame.
 *
 * Example
 *    ... -vf framestep=i20 ...
 * Dump one every 20 frames, printing on the console when a I-Frame is encounter.
 *
 *    ... -vf framestep=25
 * Dump one every 25 frames.
 *
 * If you call the filter without parameter it does nothing (except using memory
 * and resource of your system,. of course).
 *
 * This filter doesn' t work like the option -sstep seconds.
 *
 * The -sstep seek to the new position, without decoding all frames but,
 * expecially on avi file coded whith mpeg4 (lavc or xvid or divx), the
 * seek is not always too much precise.
 *
 * This filter simply discard the unwanted frames, so you are very precise in
 * counting the frame but sometime you use a lot of CPU for nothing.
 *
 * As usual it depends on what you're doing.
 *
 * copyright (c) 2003 Daniele Forghieri ( guru@digitalfantasy.it )
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "mp_msg.h"
#include "help_mp.h"
#include "cpudetect.h"

#include "img_format.h"
#include "mp_image.h"
#include "vf.h"

/* Uncomment if you want to print some info on the format */
// #define DUMP_FORMAT_DATA

/* Private data */
struct vf_priv_s {
    /* Current frame */
    int  frame_cur;
    /* Frame output step, 0 = all */
    int  frame_step;
    /* Only I-Frame (2), print on I-Frame (1) */
    int  dump_iframe;
};

/* Filter handler */
static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts)
{
    mp_image_t        *dmpi;
    struct vf_priv_s  *priv;
    int               skip;

    priv = vf->priv;

    /* Print the 'I' if is a intra frame. The \n advance the current line so you got the
     * current file time (in second) and the frame number on the console ;-)
     */
    if (priv->dump_iframe) {
        if (mpi->pict_type == 1) {
            mp_msg(MSGT_VFILTER, MSGL_INFO, "I!\n");
        }
    }

    /* decide if frame must be shown */
    if (priv->dump_iframe == 2) {
        /* Only key frame */
        skip = mpi->pict_type == 1 ? 0 : 1;
    }
    else {
        /* Only 1 every frame_step */
        skip = 0;
        if ((priv->frame_step != 0) && ((priv->frame_cur % priv->frame_step) != 0)) {
            skip = 1;
        }
    }
    /* Increment current frame */
    ++priv->frame_cur;

    if (skip == 0) {
        /* Get image, export type (we don't modify tghe image) */
        dmpi=vf_get_image(vf->next, mpi->imgfmt,
                      MP_IMGTYPE_EXPORT, 0,
                      mpi->w, mpi->h);
        /* Copy only the pointer ( MP_IMGTYPE_EXPORT ! ) */
        dmpi->planes[0] = mpi->planes[0];
        dmpi->planes[1] = mpi->planes[1];
        dmpi->planes[2] = mpi->planes[2];

        dmpi->stride[0] = mpi->stride[0];
        dmpi->stride[1] = mpi->stride[1];
        dmpi->stride[2] = mpi->stride[2];

        dmpi->width     = mpi->width;
        dmpi->height    = mpi->height;

        /* Chain to next filter / output ... */
        return vf_next_put_image(vf, dmpi, pts);
    }

    /* Skip the frame */
    return 0;
}

static void uninit(struct vf_instance *vf)
{
    /* Free private data */
    free(vf->priv);
}

/* Main entry funct for the filter */
static int vf_open(vf_instance_t *vf, char *args)
{
        struct vf_priv_s *p;

        vf->put_image = put_image;
        vf->uninit = uninit;
        vf->default_reqs = VFCAP_ACCEPT_STRIDE;
        vf->priv = p = calloc(1, sizeof(struct vf_priv_s));
        if (p == NULL) {
            return 0;
        }

        if (args != NULL) {
#ifdef DUMP_FORMAT_DATA
            if (*args == 'd') {
                p->dump_iframe = 3;
            }
            else
#endif
            if (*args == 'I') {
                /* Dump only KEY (ie INTRA) frame */
                p->dump_iframe = 2;
            }
            else {
                if (*args == 'i') {
                    /* Print a 'I!' when a i-frame is encounter */
                    p->dump_iframe = 1;
                    ++args;
                }

                if (*args != '\0') {
                    p->frame_step = atoi(args);
                    if (p->frame_step <= 0) {
                        mp_msg(MSGT_VFILTER, MSGL_WARN, MSGTR_MPCODECS_ErrorParsingArgument);
                        return 0;
                    }
                }
            }
        }
        return 1;
}

const vf_info_t vf_info_framestep = {
    "Dump one every n / key frames",
    "framestep",
    "Daniele Forghieri",
    "",
    vf_open,
    NULL
};
