/*
 * filter to tile a serie of image in a single, bigger, image
 *
 * The parameters are:
 *
 *    xtile:  number of tile on the x axis (5)
 *    ytile:  number of tile on the y axis (5)
 *    xytile: when write the image, it can be different then xtile * ytile
 *            (for example you can write 8 * 7 tile, writing the file every
 *            50 frame, to have one image every 2 seconds @ 25 fps ).
 *    start:  pixel at the start (x/y), default 2
 *    delta:  pixel between 2 tile, (x/y), default 4
 *
 * For example a valid command line is:
 *    ... -vf tile=10:5:-1:4:8 ...
 * that make images of 10 * 5 tiles, with 4 pixel at the beginning and
 * 8 pixel between tiles.
 *
 * The default command is:
 *    ... -vf tile=5:5:25:2:4
 *
 * If you omit a parameter or put a value less then 0, the default is used.
 *    ... -vf tile=10:5::-1:10
 *
 * You can also stop when you're ok
 *    ... -vf tile=10:5
 * (and this is probably the option you will use more often ...)
 *
 * Probably is good to put the scale filter before the tile :-)
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

// strtoi memcpy_pic

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

#include "libvo/fastmemcpy.h"

/* private data */
struct vf_priv_s {
    /* configuration data */
    /* Number on hor/ver tiles */
    int  xtile;
    int  ytile;
    /* When write the whole frame (default = xtile * ytile) */
    int  xytile;
    /* pixel at start / end (default = 4) */
    int  start;
    /* pixel between image (default = 2) */
    int  delta;
//    /* Background color, in destination format */
//    int  bkgSet;

    /* Work data */
    int  frame_cur;
};


static int config(struct vf_instance *vf,
                  int width, int height, int d_width, int d_height,
              unsigned int flags, unsigned int outfmt){

    struct vf_priv_s  *priv;
    int               xw;
    int               yh;

    /* Calculate new destination size */
    priv = vf->priv;
    xw = priv->start * 2 +
        priv->xtile * width +
        (priv->xtile - 1) * priv->delta;
    yh = priv->start * 2 +
        priv->ytile * height +
        (priv->ytile - 1) * priv->delta;

    mp_msg(MSGT_VFILTER,MSGL_V,"vf_tile:config size set to %d * %d\n", xw, yh);

    return vf_next_config(vf, xw, yh, xw, yh, flags, outfmt);
}

/* Filter handler */
static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts)
{
    mp_image_t        *dmpi;
    struct vf_priv_s  *priv;
    int               t;
    int               xw;
    int               yh;
    int               xi;
    int               yi;
    int               by;
    int               dw;

    /* Calculate new size */
    priv = vf->priv;
    xw = priv->start * 2 +
        priv->xtile * mpi->w +
        (priv->xtile - 1) * priv->delta;
    yh = priv->start * 2 +
        priv->ytile * mpi->h+
        (priv->ytile - 1) * priv->delta;

    /* Get the big image! */
    dmpi=vf_get_image(vf->next, mpi->imgfmt,
                      MP_IMGTYPE_STATIC, MP_IMGFLAG_ACCEPT_STRIDE,
                      xw, yh);

    /* bytes x pixel & bytes x line */
    if (mpi->flags & MP_IMGFLAG_PLANAR) {
        by = 1;
        dw = mpi->w;
    }
    else {
        by = (mpi->bpp + 7) / 8;
        dw = mpi->w * by;
    }
    /* Index position */
    t = priv->frame_cur % priv->xytile;
//    if ((t == 0) && (bkg != 0)) {
//        /* First frame, delete the background */
//
//    }

    /* Position of image */
    xi = priv->start + (mpi->w + priv->delta) * (t % priv->xtile);
    yi = priv->start + (mpi->h + priv->delta) * (t / priv->xtile);

    /* Copy first (or only) plane */
    memcpy_pic( dmpi->planes[0] + xi * by + yi * dmpi->stride[0],
                mpi->planes[0],
                dw,
                mpi->h,
                dmpi->stride[0],
                mpi->stride[0]);

    if (mpi->flags & MP_IMGFLAG_PLANAR) {
        /* Copy the other 2 planes */
        memcpy_pic( dmpi->planes[1] + (xi >> mpi->chroma_x_shift) + (yi >> mpi->chroma_y_shift) * dmpi->stride[1],
                    mpi->planes[1],
                    mpi->chroma_width,
                    mpi->chroma_height,
                    dmpi->stride[1],
                    mpi->stride[1]);
         memcpy_pic( dmpi->planes[2] + (xi >> mpi->chroma_x_shift) + (yi >> mpi->chroma_y_shift) * dmpi->stride[2],
                    mpi->planes[2],
                    mpi->chroma_width,
                    mpi->chroma_height,
                    dmpi->stride[2],
                    mpi->stride[2]);
    }

    /* Increment current frame */
    ++priv->frame_cur;

    if (t == priv->xytile - 1)  {
        /* Display the composition */
        dmpi->width  = xw;
        dmpi->height = yh;
        return vf_next_put_image(vf, dmpi, MP_NOPTS_VALUE);
    }
    else {
        /* Skip the frame */
        return 0;
    }
}

static void uninit(struct vf_instance *vf)
{
    /* free local data */
    free(vf->priv);
}

/* rgb/bgr 12...32 supported & some Yxxx */
static int query_format(struct vf_instance *vf, unsigned int fmt)
{
    switch (fmt) {
        /* rgb 12...32 bit */
        case IMGFMT_RGB12:
        case IMGFMT_RGB15:
        case IMGFMT_RGB16:
        case IMGFMT_RGB24:
        case IMGFMT_RGB32:
        /* bgr 12...32 bit */
        case IMGFMT_BGR12:
        case IMGFMT_BGR15:
        case IMGFMT_BGR16:
        case IMGFMT_BGR24:
        case IMGFMT_BGR32:
        /* Various Yxxx Formats */
        case IMGFMT_444P:
        case IMGFMT_422P:
        case IMGFMT_411P:
        case IMGFMT_YUY2:
        case IMGFMT_YV12:
        case IMGFMT_I420:
        case IMGFMT_YVU9:
        case IMGFMT_IF09:
        case IMGFMT_IYUV:
        return vf_next_query_format(vf, fmt);
    }
    return 0;
}

/* Get an integer from the string pointed by s, adjusting s.
 * If the value is less then 0 def_val is used.
 * Return 0 for ok
 *
 * Look below ( in vf_open(...) ) for a use ...
 */
static int parse_int(char **s, int *rt, int def_val)
{

    int     t = 0;

    if (**s) {
        /* Get value (dec, hex or octal) */
        t = strtol( *s, s, 0 );

        /* Use default */
        if (t < 0) {
            t = def_val;
        }

        if (**s == ':') {
            /* Point to next character (problably a digit) */
            ++(*s);
        }
        else if (**s != '\0') {
            /* Error, we got some wrong char */
            return 1;
        }
    }
    else {
        t = def_val;
    }

    *rt = t;
    return 0;

}

/* Main entry funct for the filter */
static int vf_open(vf_instance_t *vf, char *args)
{
    struct vf_priv_s *p;
    int              er;

    vf->put_image    = put_image;
    vf->query_format = query_format;
    vf->config       = config;
    vf->uninit       = uninit;
    vf->default_reqs = VFCAP_ACCEPT_STRIDE;
    /* Private data */
    vf->priv = p = calloc(1, sizeof(struct vf_priv_s));
    if (p == NULL) {
        return 0;
    }

    if (args == NULL) {
        /* Use the default */
        args = "";
    }
    /* Parse all the arguments */
    er =  parse_int( &args, &p->xtile,  5 );
    er |= parse_int( &args, &p->ytile,  5 );
    er |= parse_int( &args, &p->xytile, 0 );
    er |= parse_int( &args, &p->start,  2 );
    er |= parse_int( &args, &p->delta,  4 );
//    er |= parse_int( &args, &p->bkgSet, 0 );

    if (er) {
        mp_msg(MSGT_VFILTER, MSGL_ERR, MSGTR_MPCODECS_ErrorParsingArgument);
        return 0;
    }
    /* Load some default */
    if ((p->xytile <= 0) || (p->xytile > p->xtile * p->ytile)) {
        p->xytile = p->xtile * p->ytile;
    }

    /* Say what happen: use mp_msg(...)? */
    if ( mp_msg_test(MSGT_VFILTER,MSGL_V) ) {
        printf("vf_tile: tiling %d * %d, output every %d frames\n",
               p->xtile,
               p->ytile,
               p->xytile);
        printf("vf_tile: start pixel %d, delta pixel %d\n",
               p->start,
               p->delta);
//      printf("vf_tile: background 0x%x\n",
//             p->bkgSet);
    }
    return 1;
}

const vf_info_t vf_info_tile = {
    "Make a single image tiling x/y images",
    "tile",
    "Daniele Forghieri",
    "",
    vf_open,
    NULL
};
