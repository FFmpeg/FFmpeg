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

#ifndef MPLAYER_MP_IMAGE_H
#define MPLAYER_MP_IMAGE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef printf //FIXME
#undef fprintf //FIXME
#include "mp_msg.h"
#include "libavutil/avutil.h"
#include "libavutil/avassert.h"
#undef realloc
#undef malloc
#undef free
#undef rand
#undef srand
#undef printf
#undef strncpy
#define ASMALIGN(ZEROBITS) ".p2align " #ZEROBITS "\n\t"
#define CODEC_FLAG2_MEMC_ONLY     0x00001000 ///< Only do ME/MC (I frames -> ref, P frame -> ME+MC).

//--------- codec's requirements (filled by the codec/vf) ---------

//--- buffer content restrictions:
// set if buffer content shouldn't be modified:
#define MP_IMGFLAG_PRESERVE 0x01
// set if buffer content will be READ.
// This can be e.g. for next frame's MC: (I/P mpeg frames) -
// then in combination with MP_IMGFLAG_PRESERVE - or it
// can be because a video filter or codec will read a significant
// amount of data while processing that frame (e.g. blending something
// onto the frame, MV based intra prediction).
// A frame marked like this should not be placed in to uncachable
// video RAM for example.
#define MP_IMGFLAG_READABLE 0x02

//--- buffer width/stride/plane restrictions: (used for direct rendering)
// stride _have_to_ be aligned to MB boundary:  [for DR restrictions]
#define MP_IMGFLAG_ACCEPT_ALIGNED_STRIDE 0x4
// stride should be aligned to MB boundary:     [for buffer allocation]
#define MP_IMGFLAG_PREFER_ALIGNED_STRIDE 0x8
// codec accept any stride (>=width):
#define MP_IMGFLAG_ACCEPT_STRIDE 0x10
// codec accept any width (width*bpp=stride -> stride%bpp==0) (>=width):
#define MP_IMGFLAG_ACCEPT_WIDTH 0x20
//--- for planar formats only:
// uses only stride[0], and stride[1]=stride[2]=stride[0]>>mpi->chroma_x_shift
#define MP_IMGFLAG_COMMON_STRIDE 0x40
// uses only planes[0], and calculates planes[1,2] from width,height,imgfmt
#define MP_IMGFLAG_COMMON_PLANE 0x80

#define MP_IMGFLAGMASK_RESTRICTIONS 0xFF

//--------- color info (filled by ff_mp_image_setfmt() ) -----------
// set if number of planes > 1
#define MP_IMGFLAG_PLANAR 0x100
// set if it's YUV colorspace
#define MP_IMGFLAG_YUV 0x200
// set if it's swapped (BGR or YVU) plane/byteorder
#define MP_IMGFLAG_SWAPPED 0x400
// set if you want memory for palette allocated and managed by ff_vf_get_image etc.
#define MP_IMGFLAG_RGB_PALETTE 0x800

#define MP_IMGFLAGMASK_COLORS 0xF00

// codec uses drawing/rendering callbacks (draw_slice()-like thing, DR method 2)
// [the codec will set this flag if it supports callbacks, and the vo _may_
//  clear it in get_image() if draw_slice() not implemented]
#define MP_IMGFLAG_DRAW_CALLBACK 0x1000
// set if it's in video buffer/memory: [set by vo/vf's get_image() !!!]
#define MP_IMGFLAG_DIRECT 0x2000
// set if buffer is allocated (used in destination images):
#define MP_IMGFLAG_ALLOCATED 0x4000

// buffer type was printed (do NOT set this flag - it's for INTERNAL USE!!!)
#define MP_IMGFLAG_TYPE_DISPLAYED 0x8000

// codec doesn't support any form of direct rendering - it has own buffer
// allocation. so we just export its buffer pointers:
#define MP_IMGTYPE_EXPORT 0
// codec requires a static WO buffer, but it does only partial updates later:
#define MP_IMGTYPE_STATIC 1
// codec just needs some WO memory, where it writes/copies the whole frame to:
#define MP_IMGTYPE_TEMP 2
// I+P type, requires 2+ independent static R/W buffers
#define MP_IMGTYPE_IP 3
// I+P+B type, requires 2+ independent static R/W and 1+ temp WO buffers
#define MP_IMGTYPE_IPB 4
// Upper 16 bits give desired buffer number, -1 means get next available
#define MP_IMGTYPE_NUMBERED 5
// Doesn't need any buffer, incomplete image (probably a first field only)
// we need this type to be able to differentiate between half frames and
// all other cases
#define MP_IMGTYPE_INCOMPLETE 6

#define MP_MAX_PLANES 4

#define MP_IMGFIELD_ORDERED 0x01
#define MP_IMGFIELD_TOP_FIRST 0x02
#define MP_IMGFIELD_REPEAT_FIRST 0x04
#define MP_IMGFIELD_TOP 0x08
#define MP_IMGFIELD_BOTTOM 0x10
#define MP_IMGFIELD_INTERLACED 0x20

typedef struct mp_image {
    unsigned int flags;
    unsigned char type;
    int number;
    unsigned char bpp;  // bits/pixel. NOT depth! for RGB it will be n*8
    unsigned int imgfmt;
    int width,height;  // stored dimensions
    int x,y,w,h;  // visible dimensions
    unsigned char* planes[MP_MAX_PLANES];
    int stride[MP_MAX_PLANES];
    char * qscale;
    int qstride;
    int pict_type; // 0->unknown, 1->I, 2->P, 3->B
    int fields;
    int qscale_type; // 0->mpeg1/4/h263, 1->mpeg2
    int num_planes;
    /* these are only used by planar formats Y,U(Cb),V(Cr) */
    int chroma_width;
    int chroma_height;
    int chroma_x_shift; // horizontal
    int chroma_y_shift; // vertical
    int usage_count;
    /* for private use by filter or vo driver (to store buffer id or dmpi) */
    void* priv;
} mp_image_t;

void ff_mp_image_setfmt(mp_image_t* mpi,unsigned int out_fmt);
mp_image_t* ff_new_mp_image(int w,int h);
void ff_free_mp_image(mp_image_t* mpi);

mp_image_t* ff_alloc_mpi(int w, int h, unsigned long int fmt);
void ff_mp_image_alloc_planes(mp_image_t *mpi);
void ff_copy_mpi(mp_image_t *dmpi, mp_image_t *mpi);

#endif /* MPLAYER_MP_IMAGE_H */
