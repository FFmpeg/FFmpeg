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

#include "mp_image.h"
#include "img_format.h"
#include "vf.h"

struct vf_priv_s {
  float sense; // first parameter
  float level; // second parameter
  unsigned int imgfmt;
  int diff;
  uint32_t max;
//  int dfr;
//  int rdfr;
  int was_dint;
  mp_image_t *pmpi; // previous mpi
};

#define MAXROWSIZE 1200

static int config (struct vf_instance *vf,
        int width, int height, int d_width, int d_height,
        unsigned int flags, unsigned int outfmt)
{
    int rowsize;

    vf->priv->pmpi = vf_get_image (vf->next, outfmt, MP_IMGTYPE_TEMP,
                                   0, width, height);
    if (!(vf->priv->pmpi->flags & MP_IMGFLAG_PLANAR) &&
        outfmt != IMGFMT_RGB32 && outfmt != IMGFMT_BGR32 &&
        outfmt != IMGFMT_RGB24 && outfmt != IMGFMT_BGR24 &&
        outfmt != IMGFMT_RGB16 && outfmt != IMGFMT_BGR16)
    {
      mp_msg (MSGT_VFILTER, MSGL_WARN, "Drop-interlaced filter doesn't support this outfmt :(\n");
      return 0;
    }
    vf->priv->imgfmt = outfmt;
    // recalculate internal values
    rowsize = vf->priv->pmpi->width;
    if (rowsize > MAXROWSIZE) rowsize = MAXROWSIZE;
    vf->priv->max = vf->priv->level * vf->priv->pmpi->height * rowsize / 2;
    if (vf->priv->pmpi->flags & MP_IMGFLAG_PLANAR) // planar YUV
      vf->priv->diff = vf->priv->sense * 256;
    else
      vf->priv->diff = vf->priv->sense * (1 << (vf->priv->pmpi->bpp/3));
    if (vf->priv->diff < 0) vf->priv->diff = 0;
    if (!(vf->priv->pmpi->flags & MP_IMGFLAG_PLANAR) &&
        vf->priv->pmpi->bpp < 24 && vf->priv->diff > 31)
      vf->priv->diff = 31;
    mp_msg (MSGT_VFILTER, MSGL_INFO, "Drop-interlaced: %dx%d diff %d / level %u\n",
           vf->priv->pmpi->width, vf->priv->pmpi->height,
           vf->priv->diff, (unsigned int)vf->priv->max);
//    vf->priv->rdfr = vf->priv->dfr = 0;
    vf->priv->was_dint = 0;
    return vf_next_config(vf,width,height,d_width,d_height,flags,outfmt);
}

static int put_image (struct vf_instance *vf, mp_image_t *mpi, double pts)
{
    int8_t rrow0[MAXROWSIZE];
    int8_t rrow1[MAXROWSIZE];
    int8_t rrow2[MAXROWSIZE];
    int8_t *row0 = rrow0, *row1 = rrow1, *row2 = rrow2/*, *row3 = rrow3*/;
    int rowsize = mpi->width;
    uint32_t nok = 0, max = vf->priv->max;
    int diff = vf->priv->diff;
    int i, j;
    register int n1, n2;
    unsigned char *cur0, *prv0;
    register unsigned char *cur, *prv;

    if (rowsize > MAXROWSIZE) rowsize = MAXROWSIZE;
    // check if nothing to do
    if (mpi->imgfmt == vf->priv->imgfmt)
    {
      cur0 = mpi->planes[0] + mpi->stride[0];
      prv0 = mpi->planes[0];
      for (j = 1; j < mpi->height && nok <= max; j++)
      {
        cur = cur0;
        prv = prv0;
        // analyse row (row0)
        if (mpi->flags & MP_IMGFLAG_PLANAR) // planar YUV - check luminance
          for (i = 0; i < rowsize; i++)
          {
            if (cur[0] - prv[0] > diff)
              row0[i] = 1;
            else if (cur[0] - prv[0] < -diff)
              row0[i] = -1;
            else
              row0[i] = 0;
            cur++;
            prv++;
            // check if row0 is 1 but row1 is 0, and row2 is 1 or row2 is 0
            // but row3 is 1 so it's interlaced ptr (nok++)
            if (j > 2 && row0[i] > 0 && (row1[i] < 0 || (!row1[i] && row2[i] < 0)) &&
                (++nok) > max)
              break;
          }
        else if (mpi->bpp < 24) // RGB/BGR 16 - check all colors
          for (i = 0; i < rowsize; i++)
          {
            n1 = cur[0] + (cur[1]<<8);
            n2 = prv[0] + (prv[1]<<8);
            if ((n1&0x1f) - (n2&0x1f) > diff ||
                ((n1>>5)&0x3f) - ((n2>>5)&0x3f) > diff ||
                ((n1>>11)&0x1f) - ((n2>>11)&0x1f) > diff)
              row0[i] = 1;
            else if ((n1&0x1f) - (n2&0x1f) < -diff ||
                     ((n1>>5)&0x3f) - ((n2>>5)&0x3f) < -diff ||
                     ((n1>>11)&0x1f) - ((n2>>11)&0x1f) < -diff)
              row0[i] = -1;
            else
              row0[i] = 0;
            cur += 2;
            prv += 2;
            // check if row0 is 1 but row1 is 0, and row2 is 1 or row2 is 0
            // but row3 is 1 so it's interlaced ptr (nok++)
            if (j > 2 && row0[i] > 0 && (row1[i] < 0 || (!row1[i] && row2[i] < 0)) &&
                (++nok) > max)
              break;
          }
        else // RGB/BGR 24/32
          for (i = 0; i < rowsize; i++)
          {
            if (cur[0] - prv[0] > diff ||
                cur[1] - prv[1] > diff ||
                cur[2] - prv[2] > diff)
              row0[i] = 1;
            else if (prv[0] - cur[0] > diff ||
                     prv[1] - cur[1] > diff ||
                     prv[2] - cur[2] > diff)
              row0[i] = -1;
            else
              row0[i] = 0;
            cur += mpi->bpp/8;
            prv += mpi->bpp/8;
            // check if row0 is 1 but row1 is 0, and row2 is 1 or row2 is 0
            // but row3 is 1 so it's interlaced ptr (nok++)
            if (j > 2 && row0[i] > 0 && (row1[i] < 0 || (!row1[i] && row2[i] < 0)) &&
                (++nok) > max)
              break;
          }
        cur0 += mpi->stride[0];
        prv0 += mpi->stride[0];
        // rotate rows
        cur = row2;
        row2 = row1;
        row1 = row0;
        row0 = cur;
      }
    }
    // check if number of interlaced is above of max
    if (nok > max)
    {
//    vf->priv->dfr++;
      if (vf->priv->was_dint < 1) // can skip at most one frame!
      {
        vf->priv->was_dint++;
//      vf->priv->rdfr++;
//      mp_msg (MSGT_VFILTER, MSGL_INFO, "DI:%d/%d ", vf->priv->rdfr, vf->priv->dfr);
        return 0;
      }
    }
    vf->priv->was_dint = 0;
//    mp_msg (MSGT_VFILTER, MSGL_INFO, "DI:%d/%d ", vf->priv->rdfr, vf->priv->dfr);
    return vf_next_put_image (vf, mpi, pts);
}

static int vf_open(vf_instance_t *vf, char *args){
    vf->config = config;
    vf->put_image = put_image;
//  vf->default_reqs=VFCAP_ACCEPT_STRIDE;
    vf->priv = malloc (sizeof(struct vf_priv_s));
    vf->priv->sense = 0.1;
    vf->priv->level = 0.15;
    vf->priv->pmpi = NULL;
    if (args)
      sscanf (args, "%f:%f", &vf->priv->sense, &vf->priv->level);
    return 1;
}

const vf_info_t vf_info_dint = {
    "drop interlaced frames",
    "dint",
    "A.G.",
    "",
    vf_open,
    NULL
};
