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

const AVPixFmtDescriptor av_pix_fmt_descriptors[PIX_FMT_NB] = {
    [PIX_FMT_YUV420P] = {
        .name = "yuv420p",
        .nb_channels  = 3,
        .log2_chroma_w= 1,
        .log2_chroma_h= 1,
        .comp = {
            {0,0,1,0,7},        /* Y */
            {1,0,1,0,7},        /* U */
            {2,0,1,0,7},        /* V */
        },
    },
    [PIX_FMT_YUYV422] = {
        .name = "yuyv422",
        .nb_channels  = 3,
        .log2_chroma_w= 1,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,1,0,7},        /* Y */
            {0,3,2,0,7},        /* U */
            {0,3,4,0,7},        /* V */
        },
    },
    [PIX_FMT_RGB24] = {
        .name = "rgb24",
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,2,1,0,7},        /* R */
            {0,2,2,0,7},        /* G */
            {0,2,3,0,7},        /* B */
        },
    },
    [PIX_FMT_YUV422P] = {
        .name = "yuv422p",
        .nb_channels  = 3,
        .log2_chroma_w= 1,
        .log2_chroma_h= 0,
        .comp = {
            {0,0,1,0,7},        /* Y */
            {1,0,1,0,7},        /* U */
            {2,0,1,0,7},        /* V */
        },
    },
    [PIX_FMT_YUV410P] = {
        .name = "yuv410p",
        .nb_channels  = 3,
        .log2_chroma_w= 2,
        .log2_chroma_h= 2,
        .comp = {
            {0,0,1,0,7},        /* Y */
            {1,0,1,0,7},        /* U */
            {2,0,1,0,7},        /* V */
        },
    },
    [PIX_FMT_MONOBLACK] = {
        .name = "monoblack",
        .nb_channels  = 1,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,0,1,7,0},        /* Y */
        },
        .flags = PIX_FMT_BITSTREAM,
    },
    [PIX_FMT_PAL8] = {
        .name = "pal8",
        .nb_channels  = 1,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,0,1,0,7},
        },
        .flags = PIX_FMT_PAL,
    },
    [PIX_FMT_UYVY422] = {
        .name = "uyvy422",
        .nb_channels  = 3,
        .log2_chroma_w= 1,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,2,0,7},        /* Y */
            {0,3,1,0,7},        /* U */
            {0,3,3,0,7},        /* V */
        },
    },
    [PIX_FMT_NV12] = {
        .name = "nv12",
        .nb_channels  = 3,
        .log2_chroma_w= 1,
        .log2_chroma_h= 1,
        .comp = {
            {0,0,1,0,7},        /* Y */
            {1,1,1,0,7},        /* U */
            {1,1,2,0,7},        /* V */
        },
    },
    [PIX_FMT_RGBA] = {
        .name = "rgba",
        .nb_channels  = 4,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,3,1,0,7},        /* R */
            {0,3,2,0,7},        /* G */
            {0,3,3,0,7},        /* B */
            {0,3,4,0,7},        /* A */
        },
    },
    [PIX_FMT_GRAY16BE] = {
        .name = "gray16be",
        .nb_channels  = 1,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,1,0,15},       /* Y */
        },
        .flags = PIX_FMT_BE,
    },
    [PIX_FMT_GRAY16LE] = {
        .name = "gray16le",
        .nb_channels  = 1,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,1,0,15},       /* Y */
        },
    },
    [PIX_FMT_RGB48BE] = {
        .name = "rgb48be",
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,5,1,0,15},       /* R */
            {0,5,3,0,15},       /* G */
            {0,5,5,0,15},       /* B */
        },
        .flags = PIX_FMT_BE,
    },
    [PIX_FMT_RGB48LE] = {
        .name = "rgb48le",
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,5,1,0,15},       /* R */
            {0,5,3,0,15},       /* G */
            {0,5,5,0,15},       /* B */
        },
    },
    [PIX_FMT_RGB565BE] = {
        .name = "rgb565be",
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,0,3,4},        /* R */
            {0,1,1,5,5},        /* G */
            {0,1,1,0,4},        /* B */
        },
        .flags = PIX_FMT_BE,
    },
    [PIX_FMT_RGB565LE] = {
        .name = "rgb565le",
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,2,3,4},        /* R */
            {0,1,1,5,5},        /* G */
            {0,1,1,0,4},        /* B */
        },
    },
};
