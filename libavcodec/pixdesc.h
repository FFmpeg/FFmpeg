/*
 * pixel format descriptor
 * Copyright (c) 2009 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <inttypes.h>

#include "libavutil/intreadwrite.h"

typedef struct AVComponentDescriptor{
    uint16_t plane        :2;            ///< which of the 4 planes contains the component
    uint16_t step_minus1  :3;            ///< number of bytes between 2 horizontally consecutive pixels minus 1
    uint16_t offset_plus1 :3;            ///< number of bytes before the component of the first pixel plus 1
    uint16_t shift        :3;            ///< number of lsb that must be shifted away to get the value
    uint16_t depth_minus1 :4;            ///< number of bits in the component minus 1
}AVComponentDescriptor;

/**
 * Descriptor that unambiguously describes how the bits of a pixel are
 * stored in the up to 4 data planes of an image. It also stores the
 * subsampling factors and number of components.
 *
 * @note This is separate of the colorspace (RGB, YCbCr, YPbPr, JPEG-style YUV
 *       and all the YUV variants) AVPixFmtDescriptor just stores how values
 *       are stored not what these values represent.
 */
typedef struct AVPixFmtDescriptor{
    uint8_t nb_channels;        ///< The number of components each pixel has, (1-4)

    /**
     * Amount to shift the luma width right to find the chroma width.
     * For YV12 this is 1 for example.
     * chroma_width = -((-luma_width) >> log2_chroma_w)
     * The note above is needed to ensure rounding up.
     */
    uint8_t log2_chroma_w;      ///< chroma_width = -((-luma_width )>>log2_chroma_w)

    /**
     * Amount to shift the luma height right to find the chroma height.
     * For YV12 this is 1 for example.
     * chroma_height= -((-luma_height) >> log2_chroma_h)
     * The note above is needed to ensure rounding up.
     */
    uint8_t log2_chroma_h;
    uint8_t flags;
    AVComponentDescriptor comp[4]; ///< parameters that describe how pixels are packed
}AVPixFmtDescriptor;

#define PIX_FMT_BE        1 ///< big-endian
#define PIX_FMT_PAL       2 ///< Pixel format has a palette i data[1], values are indexes in this palette.
#define PIX_FMT_BITSTREAM 4 ///< All values of a component are bit-wise packed end to end.


static inline void read_line(uint16_t *dst, const uint8_t *data[4], const int linesize[4], AVPixFmtDescriptor *desc, int x, int y, int c, int w)
{
    AVComponentDescriptor comp= desc->comp[c];
    int plane= comp.plane;
    int depth= comp.depth_minus1+1;
    int mask = (1<<depth)-1;
    int shift= comp.shift;
    int step = comp.step_minus1+1;
    int flags= desc->flags;
    const uint8_t *p= data[plane]+y*linesize[plane] + x * step + comp.offset_plus1 - 1;

    //FIXME initial x in case of PIX_FMT_BITSTREAM is wrong

    while(w--){
        int val;
        if(flags & PIX_FMT_BE) val= AV_RB16(p);
        else                   val= AV_RL16(p);
        val = (val>>shift) & mask;
        if(flags & PIX_FMT_PAL)
            val= data[1][4*val + c];
        if(flags & PIX_FMT_BITSTREAM){
            shift-=depth;
            while(shift<0){
                shift+=8;
                p++;
            }
        }else
            p+= step;
        *dst++= val;
    }
}
