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

#include "img_format.h"
#include "mp_image.h"
#include "vf.h"

//===========================================================================//

/* FIXME: these all belong in the context, not as globals! */

static uint32_t colorMask = 0xF7DEF7DE;
static uint32_t lowPixelMask = 0x08210821;
static uint32_t qcolorMask = 0xE79CE79C;
static uint32_t qlowpixelMask = 0x18631863;
static uint32_t redblueMask = 0xF81F;
static uint32_t greenMask = 0x7E0;
static int PixelsPerMask = 2;

#define makecol(r,g,b) (r+(g<<8)+(b<<16))
#define makecol_depth(d,r,g,b) (r+(g<<8)+(b<<16))

static int Init_2xSaI(int d)
{

        int minr = 0, ming = 0, minb = 0;
        int i;

//        if (d != 15 && d != 16 && d != 24 && d != 32)
//                return -1;

        /* Get lowest color bit */
        for (i = 0; i < 255; i++) {
                if (!minr)
                        minr = makecol(i, 0, 0);
                if (!ming)
                        ming = makecol(0, i, 0);
                if (!minb)
                        minb = makecol(0, 0, i);
        }

        colorMask = (makecol_depth(d, 255, 0, 0) - minr) | (makecol_depth(d, 0, 255, 0) - ming) | (makecol_depth(d, 0, 0, 255) - minb);
        lowPixelMask = minr | ming | minb;
        qcolorMask = (makecol_depth(d, 255, 0, 0) - 3 * minr) | (makecol_depth(d, 0, 255, 0) - 3 * ming) | (makecol_depth(d, 0, 0, 255) - 3 * minb);
        qlowpixelMask = (minr * 3) | (ming * 3) | (minb * 3);
        redblueMask = makecol_depth(d, 255, 0, 255);
        greenMask = makecol_depth(d, 0, 255, 0);

        PixelsPerMask = (d <= 16) ? 2 : 1;

        if (PixelsPerMask == 2) {
                colorMask |= (colorMask << 16);
                qcolorMask |= (qcolorMask << 16);
                lowPixelMask |= (lowPixelMask << 16);
                qlowpixelMask |= (qlowpixelMask << 16);
        }

//        TRACE("Color Mask:       0x%lX\n", colorMask);
//        TRACE("Low Pixel Mask:   0x%lX\n", lowPixelMask);
//        TRACE("QColor Mask:      0x%lX\n", qcolorMask);
//        TRACE("QLow Pixel Mask:  0x%lX\n", qlowpixelMask);

        return 0;
}


#define GET_RESULT(A, B, C, D) ((A != C || A != D) - (B != C || B != D))

#define INTERPOLATE(A, B) (((A & colorMask) >> 1) + ((B & colorMask) >> 1) + (A & B & lowPixelMask))

#define Q_INTERPOLATE(A, B, C, D) ((A & qcolorMask) >> 2) + ((B & qcolorMask) >> 2) + ((C & qcolorMask) >> 2) + ((D & qcolorMask) >> 2) \
        + ((((A & qlowpixelMask) + (B & qlowpixelMask) + (C & qlowpixelMask) + (D & qlowpixelMask)) >> 2) & qlowpixelMask)


static void Super2xSaI_ex(uint8_t *src, uint32_t src_pitch,
                          uint8_t *dst, uint32_t dst_pitch,
                          uint32_t width, uint32_t height, int sbpp)
{

        unsigned int x, y;
        uint32_t color[16];
        unsigned char *src_line[4];

        /* Point to the first 3 lines. */
        src_line[0] = src;
        src_line[1] = src;
        src_line[2] = src + src_pitch;
        src_line[3] = src + src_pitch * 2;

        x = 0, y = 0;

        if (PixelsPerMask == 2) {
                unsigned short *sbp;
                sbp = (unsigned short*)src_line[0];
                color[0] = *sbp;       color[1] = color[0];   color[2] = color[0];    color[3] = color[0];
                color[4] = color[0];   color[5] = color[0];   color[6] = *(sbp + 1);  color[7] = *(sbp + 2);
                sbp = (unsigned short*)src_line[2];
                color[8] = *sbp;     color[9] = color[8];     color[10] = *(sbp + 1); color[11] = *(sbp + 2);
                sbp = (unsigned short*)src_line[3];
                color[12] = *sbp;    color[13] = color[12];   color[14] = *(sbp + 1); color[15] = *(sbp + 2);
        }
        else {
                uint32_t *lbp;
                lbp = (uint32_t*)src_line[0];
                color[0] = *lbp;       color[1] = color[0];   color[2] = color[0];    color[3] = color[0];
                color[4] = color[0];   color[5] = color[0];   color[6] = *(lbp + 1);  color[7] = *(lbp + 2);
                lbp = (uint32_t*)src_line[2];
                color[8] = *lbp;     color[9] = color[8];     color[10] = *(lbp + 1); color[11] = *(lbp + 2);
                lbp = (uint32_t*)src_line[3];
                color[12] = *lbp;    color[13] = color[12];   color[14] = *(lbp + 1); color[15] = *(lbp + 2);
        }

        for (y = 0; y < height; y++) {
                unsigned char *dst_line[2];

                dst_line[0] = dst + dst_pitch*2*y;
                dst_line[1] = dst + dst_pitch*(2*y+1);

                /* Todo: x = width - 2, x = width - 1 */

                for (x = 0; x < width; x++) {
                        uint32_t product1a, product1b, product2a, product2b;

//---------------------------------------  B0 B1 B2 B3    0  1  2  3
//                                         4  5* 6  S2 -> 4  5* 6  7
//                                         1  2  3  S1    8  9 10 11
//                                         A0 A1 A2 A3   12 13 14 15
//--------------------------------------
                        if (color[9] == color[6] && color[5] != color[10]) {
                                product2b = color[9];
                                product1b = product2b;
                        }
                        else if (color[5] == color[10] && color[9] != color[6]) {
                                product2b = color[5];
                                product1b = product2b;
                        }
                        else if (color[5] == color[10] && color[9] == color[6]) {
                                int r = 0;

                                r += GET_RESULT(color[6], color[5], color[8], color[13]);
                                r += GET_RESULT(color[6], color[5], color[4], color[1]);
                                r += GET_RESULT(color[6], color[5], color[14], color[11]);
                                r += GET_RESULT(color[6], color[5], color[2], color[7]);

                                if (r > 0)
                                        product1b = color[6];
                                else if (r < 0)
                                        product1b = color[5];
                                else
                                        product1b = INTERPOLATE(color[5], color[6]);

                                product2b = product1b;

                        }
                        else {
                                if (color[6] == color[10] && color[10] == color[13] && color[9] != color[14] && color[10] != color[12])
                                        product2b = Q_INTERPOLATE(color[10], color[10], color[10], color[9]);
                                else if (color[5] == color[9] && color[9] == color[14] && color[13] != color[10] && color[9] != color[15])
                                        product2b = Q_INTERPOLATE(color[9], color[9], color[9], color[10]);
                                else
                                        product2b = INTERPOLATE(color[9], color[10]);

                                if (color[6] == color[10] && color[6] == color[1] && color[5] != color[2] && color[6] != color[0])
                                        product1b = Q_INTERPOLATE(color[6], color[6], color[6], color[5]);
                                else if (color[5] == color[9] && color[5] == color[2] && color[1] != color[6] && color[5] != color[3])
                                        product1b = Q_INTERPOLATE(color[6], color[5], color[5], color[5]);
                                else
                                        product1b = INTERPOLATE(color[5], color[6]);
                        }

                        if (color[5] == color[10] && color[9] != color[6] && color[4] == color[5] && color[5] != color[14])
                                product2a = INTERPOLATE(color[9], color[5]);
                        else if (color[5] == color[8] && color[6] == color[5] && color[4] != color[9] && color[5] != color[12])
                                product2a = INTERPOLATE(color[9], color[5]);
                        else
                                product2a = color[9];

                        if (color[9] == color[6] && color[5] != color[10] && color[8] == color[9] && color[9] != color[2])
                                product1a = INTERPOLATE(color[9], color[5]);
                        else if (color[4] == color[9] && color[10] == color[9] && color[8] != color[5] && color[9] != color[0])
                                product1a = INTERPOLATE(color[9], color[5]);
                        else
                                product1a = color[5];

                        if (PixelsPerMask == 2) {
                                *((uint32_t *) (&dst_line[0][x * 4])) = product1a | (product1b << 16);
                                *((uint32_t *) (&dst_line[1][x * 4])) = product2a | (product2b << 16);
                        }
                        else {
                                *((uint32_t *) (&dst_line[0][x * 8])) = product1a;
                                *((uint32_t *) (&dst_line[0][x * 8 + 4])) = product1b;
                                *((uint32_t *) (&dst_line[1][x * 8])) = product2a;
                                *((uint32_t *) (&dst_line[1][x * 8 + 4])) = product2b;
                        }

                        /* Move color matrix forward */
                        color[0] = color[1]; color[4] = color[5]; color[8] = color[9];   color[12] = color[13];
                        color[1] = color[2]; color[5] = color[6]; color[9] = color[10];  color[13] = color[14];
                        color[2] = color[3]; color[6] = color[7]; color[10] = color[11]; color[14] = color[15];

                        if (x < width - 3) {
                                x += 3;
                                if (PixelsPerMask == 2) {
                                        color[3] = *(((unsigned short*)src_line[0]) + x);
                                        color[7] = *(((unsigned short*)src_line[1]) + x);
                                        color[11] = *(((unsigned short*)src_line[2]) + x);
                                        color[15] = *(((unsigned short*)src_line[3]) + x);
                                }
                                else {
                                        color[3] = *(((uint32_t*)src_line[0]) + x);
                                        color[7] = *(((uint32_t*)src_line[1]) + x);
                                        color[11] = *(((uint32_t*)src_line[2]) + x);
                                        color[15] = *(((uint32_t*)src_line[3]) + x);
                                }
                                x -= 3;
                        }
                }

                /* We're done with one line, so we shift the source lines up */
                src_line[0] = src_line[1];
                src_line[1] = src_line[2];
                src_line[2] = src_line[3];

                /* Read next line */
                if (y + 3 >= height)
                        src_line[3] = src_line[2];
                else
                        src_line[3] = src_line[2] + src_pitch;

                /* Then shift the color matrix up */
                if (PixelsPerMask == 2) {
                        unsigned short *sbp;
                        sbp = (unsigned short*)src_line[0];
                        color[0] = *sbp;     color[1] = color[0];    color[2] = *(sbp + 1);  color[3] = *(sbp + 2);
                        sbp = (unsigned short*)src_line[1];
                        color[4] = *sbp;     color[5] = color[4];    color[6] = *(sbp + 1);  color[7] = *(sbp + 2);
                        sbp = (unsigned short*)src_line[2];
                        color[8] = *sbp;     color[9] = color[9];    color[10] = *(sbp + 1); color[11] = *(sbp + 2);
                        sbp = (unsigned short*)src_line[3];
                        color[12] = *sbp;    color[13] = color[12];  color[14] = *(sbp + 1); color[15] = *(sbp + 2);
                }
                else {
                        uint32_t *lbp;
                        lbp = (uint32_t*)src_line[0];
                        color[0] = *lbp;     color[1] = color[0];    color[2] = *(lbp + 1);  color[3] = *(lbp + 2);
                        lbp = (uint32_t*)src_line[1];
                        color[4] = *lbp;     color[5] = color[4];    color[6] = *(lbp + 1);  color[7] = *(lbp + 2);
                        lbp = (uint32_t*)src_line[2];
                        color[8] = *lbp;     color[9] = color[9];    color[10] = *(lbp + 1); color[11] = *(lbp + 2);
                        lbp = (uint32_t*)src_line[3];
                        color[12] = *lbp;    color[13] = color[12];  color[14] = *(lbp + 1); color[15] = *(lbp + 2);
                }

        } // y loop

}


//===========================================================================//

static int config(struct vf_instance *vf,
        int width, int height, int d_width, int d_height,
        unsigned int flags, unsigned int outfmt){

    Init_2xSaI(outfmt&255);

    return vf_next_config(vf,2*width,2*height,2*d_width,2*d_height,flags,outfmt);
}

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts){
    mp_image_t *dmpi;

    // hope we'll get DR buffer:
    dmpi=vf_get_image(vf->next,mpi->imgfmt,
        MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE,
        2*mpi->w, 2*mpi->h);

    Super2xSaI_ex(mpi->planes[0], mpi->stride[0],
                  dmpi->planes[0], dmpi->stride[0],
                  mpi->w, mpi->h, mpi->bpp/8);

    return vf_next_put_image(vf,dmpi, pts);
}

//===========================================================================//

static int query_format(struct vf_instance *vf, unsigned int fmt){
    switch(fmt){
//    case IMGFMT_BGR15:
//    case IMGFMT_BGR16:
    case IMGFMT_BGR32:
        return vf_next_query_format(vf,fmt);
    }
    return 0;
}

static int vf_open(vf_instance_t *vf, char *args){
    vf->config=config;
    vf->put_image=put_image;
    vf->query_format=query_format;
    return 1;
}

const vf_info_t vf_info_2xsai = {
    "2xSai BGR bitmap 2x scaler",
    "2xsai",
    "A'rpi",
    "http://elektron.its.tudelft.nl/~dalikifa/",
    vf_open,
    NULL
};

//===========================================================================//
