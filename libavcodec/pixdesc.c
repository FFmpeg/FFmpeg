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
    [PIX_FMT_BGR24] = {
        .name = "bgr24",
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,2,1,0,7},        /* B */
            {0,2,2,0,7},        /* G */
            {0,2,3,0,7},        /* R */
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
    [PIX_FMT_YUV444P] = {
        .name = "yuv444p",
        .nb_channels  = 3,
        .log2_chroma_w= 0,
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
    [PIX_FMT_YUV411P] = {
        .name = "yuv411p",
        .nb_channels  = 3,
        .log2_chroma_w= 2,
        .log2_chroma_h= 0,
        .comp = {
            {0,0,1,0,7},        /* Y */
            {1,0,1,0,7},        /* U */
            {2,0,1,0,7},        /* V */
        },
    },
    [PIX_FMT_GRAY8] = {
        .name = "gray8",
        .nb_channels  = 1,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,0,1,0,7},        /* Y */
        },
    },
    [PIX_FMT_MONOWHITE] = {
        .name = "monowhite",
        .nb_channels  = 1,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,0,1,0,0},        /* Y */
        },
        .flags = PIX_FMT_BITSTREAM,
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
    [PIX_FMT_YUVJ420P] = {
        .name = "yuvj420p",
        .nb_channels  = 3,
        .log2_chroma_w= 1,
        .log2_chroma_h= 1,
        .comp = {
            {0,0,1,0,7},        /* Y */
            {1,0,1,0,7},        /* U */
            {2,0,1,0,7},        /* V */
        },
    },
    [PIX_FMT_YUVJ422P] = {
        .name = "yuvj422p",
        .nb_channels  = 3,
        .log2_chroma_w= 1,
        .log2_chroma_h= 0,
        .comp = {
            {0,0,1,0,7},        /* Y */
            {1,0,1,0,7},        /* U */
            {2,0,1,0,7},        /* V */
        },
    },
    [PIX_FMT_YUVJ444P] = {
        .name = "yuvj444p",
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,0,1,0,7},        /* Y */
            {1,0,1,0,7},        /* U */
            {2,0,1,0,7},        /* V */
        },
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
    [PIX_FMT_UYYVYY411] = {
        .name = "uyyvyy411",
        .nb_channels  = 3,
        .log2_chroma_w= 2,
        .log2_chroma_h= 0,
        .comp = {
            {0,3,2,0,7},        /* Y */
            {0,5,1,0,7},        /* U */
            {0,5,4,0,7},        /* V */
        },
    },
    [PIX_FMT_BGR8] = {
        .name = "bgr8",
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,0,1,6,1},        /* B */
            {0,0,1,3,2},        /* G */
            {0,0,1,0,2},        /* R */
        },
    },
    [PIX_FMT_BGR4] = {
        .name = "bgr4",
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,3,1,0,0},        /* B */
            {0,3,2,0,1},        /* G */
            {0,3,4,0,0},        /* R */
        },
        .flags = PIX_FMT_BITSTREAM,
    },
    [PIX_FMT_BGR4_BYTE] = {
        .name = "bgr4_byte",
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,0,1,3,0},        /* B */
            {0,0,1,1,1},        /* G */
            {0,0,1,0,0},        /* R */
        },
    },
    [PIX_FMT_RGB8] = {
        .name = "rgb8",
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,0,1,6,1},        /* R */
            {0,0,1,3,2},        /* G */
            {0,0,1,0,2},        /* B */
        },
    },
    [PIX_FMT_RGB4] = {
        .name = "rgb4",
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,3,1,0,0},       /* R */
            {0,3,2,0,1},       /* G */
            {0,3,4,0,0},       /* B */
        },
        .flags = PIX_FMT_BITSTREAM,
    },
    [PIX_FMT_RGB4_BYTE] = {
        .name = "rgb4_byte",
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,0,1,3,0},        /* R */
            {0,0,1,1,1},        /* G */
            {0,0,1,0,0},        /* B */
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
    [PIX_FMT_NV21] = {
        .name = "nv21",
        .nb_channels  = 3,
        .log2_chroma_w= 1,
        .log2_chroma_h= 1,
        .comp = {
            {0,0,1,0,7},        /* Y */
            {1,1,1,0,7},        /* V */
            {1,1,2,0,7},        /* U */
        },
    },
    [PIX_FMT_ARGB] = {
        .name = "argb",
        .nb_channels  = 4,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,3,1,0,7},        /* A */
            {0,3,2,0,7},        /* R */
            {0,3,3,0,7},        /* G */
            {0,3,4,0,7},        /* B */
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
    [PIX_FMT_ABGR] = {
        .name = "abgr",
        .nb_channels  = 4,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,3,1,0,7},        /* A */
            {0,3,2,0,7},        /* B */
            {0,3,3,0,7},        /* G */
            {0,3,4,0,7},        /* R */
        },
    },
    [PIX_FMT_BGRA] = {
        .name = "bgra",
        .nb_channels  = 4,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,3,1,0,7},        /* B */
            {0,3,2,0,7},        /* G */
            {0,3,3,0,7},        /* R */
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
    [PIX_FMT_YUV440P] = {
        .name = "yuv440p",
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 1,
        .comp = {
            {0,0,1,0,7},        /* Y */
            {1,0,1,0,7},        /* U */
            {2,0,1,0,7},        /* V */
        },
    },
    [PIX_FMT_YUVJ440P] = {
        .name = "yuvj440p",
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 1,
        .comp = {
            {0,0,1,0,7},        /* Y */
            {1,0,1,0,7},        /* U */
            {2,0,1,0,7},        /* V */
        },
    },
    [PIX_FMT_YUVA420P] = {
        .name = "yuva420p",
        .nb_channels  = 4,
        .log2_chroma_w= 1,
        .log2_chroma_h= 1,
        .comp = {
            {0,0,1,0,7},        /* Y */
            {1,0,1,0,7},        /* U */
            {2,0,1,0,7},        /* V */
            {3,0,1,0,7},        /* A */
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
    [PIX_FMT_RGB555BE] = {
        .name = "rgb555be",
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,0,2,4},        /* R */
            {0,1,1,5,4},        /* G */
            {0,1,1,0,4},        /* B */
        },
        .flags = PIX_FMT_BE,
    },
    [PIX_FMT_RGB555LE] = {
        .name = "rgb555le",
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,2,2,4},        /* R */
            {0,1,1,5,4},        /* G */
            {0,1,1,0,4},        /* B */
        },
    },
    [PIX_FMT_BGR565BE] = {
        .name = "bgr565be",
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,0,3,4},        /* B */
            {0,1,1,5,5},        /* G */
            {0,1,1,0,4},        /* R */
        },
        .flags = PIX_FMT_BE,
    },
    [PIX_FMT_BGR565LE] = {
        .name = "bgr565le",
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,2,3,4},        /* B */
            {0,1,1,5,5},        /* G */
            {0,1,1,0,4},        /* R */
        },
    },
    [PIX_FMT_BGR555BE] = {
        .name = "bgr555be",
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,0,2,4},       /* B */
            {0,1,1,5,4},       /* G */
            {0,1,1,0,4},       /* R */
        },
        .flags = PIX_FMT_BE,
     },
    [PIX_FMT_BGR555LE] = {
        .name = "bgr555le",
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,2,2,4},        /* B */
            {0,1,1,5,4},        /* G */
            {0,1,1,0,4},        /* R */
        },
    },
    [PIX_FMT_YUV420PLE] = {
        .name = "yuv420ple",
        .nb_channels  = 3,
        .log2_chroma_w= 1,
        .log2_chroma_h= 1,
        .comp = {
            {0,1,1,0,15},        /* Y */
            {1,1,1,0,15},        /* U */
            {2,1,1,0,15},        /* V */
        },
    },
    [PIX_FMT_YUV420PBE] = {
        .name = "yuv420pbe",
        .nb_channels  = 3,
        .log2_chroma_w= 1,
        .log2_chroma_h= 1,
        .comp = {
            {0,1,1,0,15},        /* Y */
            {1,1,1,0,15},        /* U */
            {2,1,1,0,15},        /* V */
        },
        .flags = PIX_FMT_BE,
    },
    [PIX_FMT_YUV422PLE] = {
        .name = "yuv422ple",
        .nb_channels  = 3,
        .log2_chroma_w= 1,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,1,0,15},        /* Y */
            {1,1,1,0,15},        /* U */
            {2,1,1,0,15},        /* V */
        },
    },
    [PIX_FMT_YUV422PBE] = {
        .name = "yuv422pbe",
        .nb_channels  = 3,
        .log2_chroma_w= 1,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,1,0,15},        /* Y */
            {1,1,1,0,15},        /* U */
            {2,1,1,0,15},        /* V */
        },
        .flags = PIX_FMT_BE,
    },
    [PIX_FMT_YUV444PLE] = {
        .name = "yuv444ple",
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,1,0,15},        /* Y */
            {1,1,1,0,15},        /* U */
            {2,1,1,0,15},        /* V */
        },
    },
    [PIX_FMT_YUV444PBE] = {
        .name = "yuv444pbe",
        .nb_channels  = 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,1,0,15},        /* Y */
            {1,1,1,0,15},        /* U */
            {2,1,1,0,15},        /* V */
        },
        .flags = PIX_FMT_BE,
    },
};

int av_get_bits_per_pixel(const AVPixFmtDescriptor *pixdesc)
{
    int c, bits = 0;
    int log2_pixels = pixdesc->log2_chroma_w + pixdesc->log2_chroma_h;

    for (c = 0; c < pixdesc->nb_channels; c++) {
        int s = c==1 || c==2 ? 0 : log2_pixels;
        bits += (pixdesc->comp[c].depth_minus1+1) << s;
    }

    return bits >> log2_pixels;
}
