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

#include <stdio.h>
#include <string.h>
#include "pixfmt.h"
#include "pixdesc.h"

#include "intreadwrite.h"

void av_read_image_line(uint16_t *dst, const uint8_t *data[4], const int linesize[4],
                        const AVPixFmtDescriptor *desc, int x, int y, int c, int w, int read_pal_component)
{
    AVComponentDescriptor comp= desc->comp[c];
    int plane= comp.plane;
    int depth= comp.depth_minus1+1;
    int mask = (1<<depth)-1;
    int shift= comp.shift;
    int step = comp.step_minus1+1;
    int flags= desc->flags;

    if (flags & PIX_FMT_BITSTREAM){
        int skip = x*step + comp.offset_plus1-1;
        const uint8_t *p = data[plane] + y*linesize[plane] + (skip>>3);
        int shift = 8 - depth - (skip&7);

        while(w--){
            int val = (*p >> shift) & mask;
            if(read_pal_component)
                val= data[1][4*val + c];
            shift -= step;
            p -= shift>>3;
            shift &= 7;
            *dst++= val;
        }
    } else {
        const uint8_t *p = data[plane]+ y*linesize[plane] + x*step + comp.offset_plus1-1;
        int is_8bit = shift + depth <= 8;

        if (is_8bit)
            p += !!(flags & PIX_FMT_BE);

        while(w--){
            int val = is_8bit ? *p :
                flags & PIX_FMT_BE ? AV_RB16(p) : AV_RL16(p);
            val = (val>>shift) & mask;
            if(read_pal_component)
                val= data[1][4*val + c];
            p+= step;
            *dst++= val;
        }
    }
}

void av_write_image_line(const uint16_t *src, uint8_t *data[4], const int linesize[4],
                         const AVPixFmtDescriptor *desc, int x, int y, int c, int w)
{
    AVComponentDescriptor comp = desc->comp[c];
    int plane = comp.plane;
    int depth = comp.depth_minus1+1;
    int step  = comp.step_minus1+1;
    int flags = desc->flags;

    if (flags & PIX_FMT_BITSTREAM) {
        int skip = x*step + comp.offset_plus1-1;
        uint8_t *p = data[plane] + y*linesize[plane] + (skip>>3);
        int shift = 8 - depth - (skip&7);

        while (w--) {
            *p |= *src++ << shift;
            shift -= step;
            p -= shift>>3;
            shift &= 7;
        }
    } else {
        int shift = comp.shift;
        uint8_t *p = data[plane]+ y*linesize[plane] + x*step + comp.offset_plus1-1;

        if (shift + depth <= 8) {
            p += !!(flags & PIX_FMT_BE);
            while (w--) {
                *p |= (*src++<<shift);
                p += step;
            }
        } else {
            while (w--) {
                if (flags & PIX_FMT_BE) {
                    uint16_t val = AV_RB16(p) | (*src++<<shift);
                    AV_WB16(p, val);
                } else {
                    uint16_t val = AV_RL16(p) | (*src++<<shift);
                    AV_WL16(p, val);
                }
                p+= step;
            }
        }
    }
}

const AVPixFmtDescriptor av_pix_fmt_descriptors[PIX_FMT_NB] = {
    [PIX_FMT_YUV420P] = {
        .name = "yuv420p",
        .nb_components= 3,
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
        .nb_components= 3,
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
        .nb_components= 3,
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
        .nb_components= 3,
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
        .nb_components= 3,
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
        .nb_components= 3,
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
        .nb_components= 3,
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
        .nb_components= 3,
        .log2_chroma_w= 2,
        .log2_chroma_h= 0,
        .comp = {
            {0,0,1,0,7},        /* Y */
            {1,0,1,0,7},        /* U */
            {2,0,1,0,7},        /* V */
        },
    },
    [PIX_FMT_GRAY8] = {
        .name = "gray",
        .nb_components= 1,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,0,1,0,7},        /* Y */
        },
        .flags = PIX_FMT_PAL,
    },
    [PIX_FMT_MONOWHITE] = {
        .name = "monow",
        .nb_components= 1,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,0,1,0,0},        /* Y */
        },
        .flags = PIX_FMT_BITSTREAM,
    },
    [PIX_FMT_MONOBLACK] = {
        .name = "monob",
        .nb_components= 1,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,0,1,7,0},        /* Y */
        },
        .flags = PIX_FMT_BITSTREAM,
    },
    [PIX_FMT_PAL8] = {
        .name = "pal8",
        .nb_components= 1,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,0,1,0,7},
        },
        .flags = PIX_FMT_PAL,
    },
    [PIX_FMT_YUVJ420P] = {
        .name = "yuvj420p",
        .nb_components= 3,
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
        .nb_components= 3,
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
        .nb_components= 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,0,1,0,7},        /* Y */
            {1,0,1,0,7},        /* U */
            {2,0,1,0,7},        /* V */
        },
    },
    [PIX_FMT_XVMC_MPEG2_MC] = {
        .name = "xvmcmc",
        .flags = PIX_FMT_HWACCEL,
    },
    [PIX_FMT_XVMC_MPEG2_IDCT] = {
        .name = "xvmcidct",
        .flags = PIX_FMT_HWACCEL,
    },
    [PIX_FMT_UYVY422] = {
        .name = "uyvy422",
        .nb_components= 3,
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
        .nb_components= 3,
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
        .nb_components= 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,0,1,6,1},        /* B */
            {0,0,1,3,2},        /* G */
            {0,0,1,0,2},        /* R */
        },
        .flags = PIX_FMT_PAL,
    },
    [PIX_FMT_BGR4] = {
        .name = "bgr4",
        .nb_components= 3,
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
        .nb_components= 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,0,1,3,0},        /* B */
            {0,0,1,1,1},        /* G */
            {0,0,1,0,0},        /* R */
        },
        .flags = PIX_FMT_PAL,
    },
    [PIX_FMT_RGB8] = {
        .name = "rgb8",
        .nb_components= 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,0,1,6,1},        /* R */
            {0,0,1,3,2},        /* G */
            {0,0,1,0,2},        /* B */
        },
        .flags = PIX_FMT_PAL,
    },
    [PIX_FMT_RGB4] = {
        .name = "rgb4",
        .nb_components= 3,
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
        .nb_components= 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,0,1,3,0},        /* R */
            {0,0,1,1,1},        /* G */
            {0,0,1,0,0},        /* B */
        },
        .flags = PIX_FMT_PAL,
    },
    [PIX_FMT_NV12] = {
        .name = "nv12",
        .nb_components= 3,
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
        .nb_components= 3,
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
        .nb_components= 4,
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
        .nb_components= 4,
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
        .nb_components= 4,
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
        .nb_components= 4,
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
        .nb_components= 1,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,1,0,15},       /* Y */
        },
        .flags = PIX_FMT_BE,
    },
    [PIX_FMT_GRAY16LE] = {
        .name = "gray16le",
        .nb_components= 1,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,1,0,15},       /* Y */
        },
    },
    [PIX_FMT_YUV440P] = {
        .name = "yuv440p",
        .nb_components= 3,
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
        .nb_components= 3,
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
        .nb_components= 4,
        .log2_chroma_w= 1,
        .log2_chroma_h= 1,
        .comp = {
            {0,0,1,0,7},        /* Y */
            {1,0,1,0,7},        /* U */
            {2,0,1,0,7},        /* V */
            {3,0,1,0,7},        /* A */
        },
    },
    [PIX_FMT_VDPAU_H264] = {
        .name = "vdpau_h264",
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .flags = PIX_FMT_HWACCEL,
    },
    [PIX_FMT_VDPAU_MPEG1] = {
        .name = "vdpau_mpeg1",
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .flags = PIX_FMT_HWACCEL,
    },
    [PIX_FMT_VDPAU_MPEG2] = {
        .name = "vdpau_mpeg2",
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .flags = PIX_FMT_HWACCEL,
    },
    [PIX_FMT_VDPAU_WMV3] = {
        .name = "vdpau_wmv3",
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .flags = PIX_FMT_HWACCEL,
    },
    [PIX_FMT_VDPAU_VC1] = {
        .name = "vdpau_vc1",
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .flags = PIX_FMT_HWACCEL,
    },
    [PIX_FMT_VDPAU_MPEG4] = {
        .name = "vdpau_mpeg4",
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .flags = PIX_FMT_HWACCEL,
    },
    [PIX_FMT_RGB48BE] = {
        .name = "rgb48be",
        .nb_components= 3,
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
        .nb_components= 3,
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
        .nb_components= 3,
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
        .nb_components= 3,
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
        .nb_components= 3,
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
        .nb_components= 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,2,2,4},        /* R */
            {0,1,1,5,4},        /* G */
            {0,1,1,0,4},        /* B */
        },
    },
    [PIX_FMT_RGB444BE] = {
        .name = "rgb444be",
        .nb_components= 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,0,0,3},        /* R */
            {0,1,1,4,3},        /* G */
            {0,1,1,0,3},        /* B */
        },
        .flags = PIX_FMT_BE,
    },
    [PIX_FMT_RGB444LE] = {
        .name = "rgb444le",
        .nb_components= 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,2,0,3},        /* R */
            {0,1,1,4,3},        /* G */
            {0,1,1,0,3},        /* B */
        },
    },
    [PIX_FMT_BGR565BE] = {
        .name = "bgr565be",
        .nb_components= 3,
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
        .nb_components= 3,
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
        .nb_components= 3,
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
        .nb_components= 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,2,2,4},        /* B */
            {0,1,1,5,4},        /* G */
            {0,1,1,0,4},        /* R */
        },
    },
    [PIX_FMT_BGR444BE] = {
        .name = "bgr444be",
        .nb_components= 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,0,0,3},       /* B */
            {0,1,1,4,3},       /* G */
            {0,1,1,0,3},       /* R */
        },
        .flags = PIX_FMT_BE,
     },
    [PIX_FMT_BGR444LE] = {
        .name = "bgr444le",
        .nb_components= 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,2,0,3},        /* B */
            {0,1,1,4,3},        /* G */
            {0,1,1,0,3},        /* R */
        },
    },
    [PIX_FMT_VAAPI_MOCO] = {
        .name = "vaapi_moco",
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .flags = PIX_FMT_HWACCEL,
    },
    [PIX_FMT_VAAPI_IDCT] = {
        .name = "vaapi_idct",
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .flags = PIX_FMT_HWACCEL,
    },
    [PIX_FMT_VAAPI_VLD] = {
        .name = "vaapi_vld",
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .flags = PIX_FMT_HWACCEL,
    },
    [PIX_FMT_YUV420P16LE] = {
        .name = "yuv420p16le",
        .nb_components= 3,
        .log2_chroma_w= 1,
        .log2_chroma_h= 1,
        .comp = {
            {0,1,1,0,15},        /* Y */
            {1,1,1,0,15},        /* U */
            {2,1,1,0,15},        /* V */
        },
    },
    [PIX_FMT_YUV420P16BE] = {
        .name = "yuv420p16be",
        .nb_components= 3,
        .log2_chroma_w= 1,
        .log2_chroma_h= 1,
        .comp = {
            {0,1,1,0,15},        /* Y */
            {1,1,1,0,15},        /* U */
            {2,1,1,0,15},        /* V */
        },
        .flags = PIX_FMT_BE,
    },
    [PIX_FMT_YUV422P16LE] = {
        .name = "yuv422p16le",
        .nb_components= 3,
        .log2_chroma_w= 1,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,1,0,15},        /* Y */
            {1,1,1,0,15},        /* U */
            {2,1,1,0,15},        /* V */
        },
    },
    [PIX_FMT_YUV422P16BE] = {
        .name = "yuv422p16be",
        .nb_components= 3,
        .log2_chroma_w= 1,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,1,0,15},        /* Y */
            {1,1,1,0,15},        /* U */
            {2,1,1,0,15},        /* V */
        },
        .flags = PIX_FMT_BE,
    },
    [PIX_FMT_YUV444P16LE] = {
        .name = "yuv444p16le",
        .nb_components= 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,1,0,15},        /* Y */
            {1,1,1,0,15},        /* U */
            {2,1,1,0,15},        /* V */
        },
    },
    [PIX_FMT_YUV444P16BE] = {
        .name = "yuv444p16be",
        .nb_components= 3,
        .log2_chroma_w= 0,
        .log2_chroma_h= 0,
        .comp = {
            {0,1,1,0,15},        /* Y */
            {1,1,1,0,15},        /* U */
            {2,1,1,0,15},        /* V */
        },
        .flags = PIX_FMT_BE,
    },
    [PIX_FMT_DXVA2_VLD] = {
        .name = "dxva2_vld",
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .flags = PIX_FMT_HWACCEL,
    },
    [PIX_FMT_Y400A] = {
        .name = "y400a",
        .nb_components= 2,
        .comp = {
            {0,1,1,0,7},        /* Y */
            {0,1,2,0,7},        /* A */
        },
    },
};

static enum PixelFormat get_pix_fmt_internal(const char *name)
{
    enum PixelFormat pix_fmt;

    for (pix_fmt = 0; pix_fmt < PIX_FMT_NB; pix_fmt++)
        if (av_pix_fmt_descriptors[pix_fmt].name &&
            !strcmp(av_pix_fmt_descriptors[pix_fmt].name, name))
            return pix_fmt;

    return PIX_FMT_NONE;
}

#if HAVE_BIGENDIAN
#   define X_NE(be, le) be
#else
#   define X_NE(be, le) le
#endif

enum PixelFormat av_get_pix_fmt(const char *name)
{
    enum PixelFormat pix_fmt;

    if (!strcmp(name, "rgb32"))
        name = X_NE("argb", "bgra");
    else if (!strcmp(name, "bgr32"))
        name = X_NE("abgr", "rgba");

    pix_fmt = get_pix_fmt_internal(name);
    if (pix_fmt == PIX_FMT_NONE) {
        char name2[32];

        snprintf(name2, sizeof(name2), "%s%s", name, X_NE("be", "le"));
        pix_fmt = get_pix_fmt_internal(name2);
    }
    return pix_fmt;
}

int av_get_bits_per_pixel(const AVPixFmtDescriptor *pixdesc)
{
    int c, bits = 0;
    int log2_pixels = pixdesc->log2_chroma_w + pixdesc->log2_chroma_h;

    for (c = 0; c < pixdesc->nb_components; c++) {
        int s = c==1 || c==2 ? 0 : log2_pixels;
        bits += (pixdesc->comp[c].depth_minus1+1) << s;
    }

    return bits >> log2_pixels;
}

char *av_get_pix_fmt_string (char *buf, int buf_size, enum PixelFormat pix_fmt)
{
    /* print header */
    if (pix_fmt < 0) {
        snprintf (buf, buf_size, "name      " " nb_components" " nb_bits");
    } else {
        const AVPixFmtDescriptor *pixdesc = &av_pix_fmt_descriptors[pix_fmt];
        snprintf(buf, buf_size, "%-11s %7d %10d",
                 pixdesc->name, pixdesc->nb_components, av_get_bits_per_pixel(pixdesc));
    }

    return buf;
}
