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

#include "libavutil/pixfmt.h"
#include "pixdesc.h"

static const AVPixFmtDescriptor pix_fmt_desc[PIX_FMT_NB] = {
    [PIX_FMT_YUV422P] = {
        .nb_channels  = 3,
        .log2_chroma_w= 1,
        .log2_chroma_h= 0,
        .comp = {
            {0,0,1,0,7},
            {1,0,1,0,7},
            {2,0,1,0,7},
        },
    },
    [PIX_FMT_YUV420P] = {
        .nb_channels  = 3,
        .log2_chroma_w= 1,
        .log2_chroma_h= 1,
        .comp = {
            {0,0,1,0,7},
            {1,0,1,0,7},
            {2,0,1,0,7},
        },
    },
    [PIX_FMT_YUV410P] = {
        .nb_channels  = 3,
        .log2_chroma_w= 2,
        .log2_chroma_h= 2,
        .comp = {
            {0,0,1,0,7},
            {1,0,1,0,7},
            {2,0,1,0,7},
        },
    },
    [PIX_FMT_NV12] = {
        .nb_channels  = 3,
        .log2_chroma_w= 1,
        .log2_chroma_h= 1,
        .comp = {
            {0,0,1,0,7},
            {1,1,1,0,7},
            {1,1,2,0,7},
        },
    },
    [PIX_FMT_YUYV422] = {
        .nb_channels  = 3,
        .log2_chroma_w= 1,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,1,0,7},
            {0,3,2,0,7},
            {0,3,4,0,7},
        },
    },
    [PIX_FMT_UYVY422] = {
        .nb_channels  = 3,
        .log2_chroma_w= 1,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,2,0,7},
            {0,3,1,0,7},
            {0,3,3,0,7},
        },
    },
    [PIX_FMT_GRAY16LE] = {
        .nb_channels  = 1,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,1,0,15},
        },
    },
    [PIX_FMT_GRAY16BE] = {
        .nb_channels  = 1,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,1,0,15},
        },
        .flags = PIX_FMT_BE,
    },
    [PIX_FMT_RGB24] = {
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,2,1,0,7},
            {0,2,2,0,7},
            {0,2,3,0,7},
        },
    },
    [PIX_FMT_RGBA] = {
        .nb_channels  = 4,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,3,1,0,7},
            {0,3,2,0,7},
            {0,3,3,0,7},
            {0,3,4,0,7},
        },
    },
    [PIX_FMT_RGB48LE] = {
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,5,1,0,15},
            {0,5,3,0,15},
            {0,5,5,0,15},
        },
    },
    [PIX_FMT_RGB48BE] = {
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,5,1,0,15},
            {0,5,3,0,15},
            {0,5,5,0,15},
        },
        .flags = PIX_FMT_BE,
    },
//FIXME change pix fmt defines so that we have a LE & BE instead of a native-endian
#if 0
    [PIX_FMT_RGB565_LE] = {
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,1,0,4},
            {0,1,1,5,5},
            {0,1,2,3,4},
        },
    },
    [PIX_FMT_RGB565_BE] = {
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,1, 0,4},
            {0,1,1, 5,5},
            {0,1,0, 3,4},
        },
        .flags = PIX_FMT_BE,
    },
#endif
    [PIX_FMT_MONOBLACK] = {
        .nb_channels  = 1,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,0,1,7,0},
        },
        .flags = PIX_FMT_BITSTREAM,
    },
    [PIX_FMT_PAL8] = {
        .nb_channels  = 1,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,0,1,0,7},
        },
        .flags = PIX_FMT_PAL,
    },
};
