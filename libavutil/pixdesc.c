/*
 * pixel format descriptor
 * Copyright (c) 2009 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdio.h>
#include <string.h>

#include "avstring.h"
#include "common.h"
#include "pixfmt.h"
#include "pixdesc.h"
#include "internal.h"
#include "intreadwrite.h"
#include "version.h"

void av_read_image_line(uint16_t *dst,
                        const uint8_t *data[4], const int linesize[4],
                        const AVPixFmtDescriptor *desc,
                        int x, int y, int c, int w,
                        int read_pal_component)
{
    AVComponentDescriptor comp = desc->comp[c];
    int plane = comp.plane;
    int depth = comp.depth;
    int mask  = (1 << depth) - 1;
    int shift = comp.shift;
    int step  = comp.step;
    int flags = desc->flags;

    if (flags & AV_PIX_FMT_FLAG_BITSTREAM) {
        int skip = x * step + comp.offset;
        const uint8_t *p = data[plane] + y * linesize[plane] + (skip >> 3);
        int shift = 8 - depth - (skip & 7);

        while (w--) {
            int val = (*p >> shift) & mask;
            if (read_pal_component)
                val = data[1][4*val + c];
            shift -= step;
            p -= shift >> 3;
            shift &= 7;
            *dst++ = val;
        }
    } else {
        const uint8_t *p = data[plane] + y * linesize[plane] +
                           x * step + comp.offset;
        int is_8bit = shift + depth <= 8;

        if (is_8bit)
            p += !!(flags & AV_PIX_FMT_FLAG_BE);

        while (w--) {
            int val = is_8bit ? *p :
                flags & AV_PIX_FMT_FLAG_BE ? AV_RB16(p) : AV_RL16(p);
            val = (val >> shift) & mask;
            if (read_pal_component)
                val = data[1][4 * val + c];
            p += step;
            *dst++ = val;
        }
    }
}

void av_write_image_line(const uint16_t *src,
                         uint8_t *data[4], const int linesize[4],
                         const AVPixFmtDescriptor *desc,
                         int x, int y, int c, int w)
{
    AVComponentDescriptor comp = desc->comp[c];
    int plane = comp.plane;
    int depth = comp.depth;
    int step  = comp.step;
    int flags = desc->flags;

    if (flags & AV_PIX_FMT_FLAG_BITSTREAM) {
        int skip = x * step + comp.offset;
        uint8_t *p = data[plane] + y * linesize[plane] + (skip >> 3);
        int shift = 8 - depth - (skip & 7);

        while (w--) {
            *p |= *src++ << shift;
            shift -= step;
            p -= shift >> 3;
            shift &= 7;
        }
    } else {
        int shift = comp.shift;
        uint8_t *p = data[plane] + y * linesize[plane] +
                     x * step + comp.offset;

        if (shift + depth <= 8) {
            p += !!(flags & AV_PIX_FMT_FLAG_BE);
            while (w--) {
                *p |= (*src++ << shift);
                p += step;
            }
        } else {
            while (w--) {
                if (flags & AV_PIX_FMT_FLAG_BE) {
                    uint16_t val = AV_RB16(p) | (*src++ << shift);
                    AV_WB16(p, val);
                } else {
                    uint16_t val = AV_RL16(p) | (*src++ << shift);
                    AV_WL16(p, val);
                }
                p += step;
            }
        }
    }
}

#if FF_API_PLUS1_MINUS1
FF_DISABLE_DEPRECATION_WARNINGS
#endif
static const AVPixFmtDescriptor av_pix_fmt_descriptors[AV_PIX_FMT_NB] = {
    [AV_PIX_FMT_YUV420P] = {
        .name = "yuv420p",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .comp = {
            { 0, 1, 0, 0, 8, 0, 7, 1 },        /* Y */
            { 1, 1, 0, 0, 8, 0, 7, 1 },        /* U */
            { 2, 1, 0, 0, 8, 0, 7, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_YUYV422] = {
        .name = "yuyv422",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 8, 1, 7, 1 },        /* Y */
            { 0, 4, 1, 0, 8, 3, 7, 2 },        /* U */
            { 0, 4, 3, 0, 8, 3, 7, 4 },        /* V */
        },
    },
    [AV_PIX_FMT_YVYU422] = {
        .name = "yvyu422",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 8, 1, 7, 1 },        /* Y */
            { 0, 4, 3, 0, 8, 3, 7, 4 },        /* U */
            { 0, 4, 1, 0, 8, 3, 7, 2 },        /* V */
        },
    },
    [AV_PIX_FMT_RGB24] = {
        .name = "rgb24",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 3, 0, 0, 8, 2, 7, 1 },        /* R */
            { 0, 3, 1, 0, 8, 2, 7, 2 },        /* G */
            { 0, 3, 2, 0, 8, 2, 7, 3 },        /* B */
        },
        .flags = AV_PIX_FMT_FLAG_RGB,
    },
    [AV_PIX_FMT_BGR24] = {
        .name = "bgr24",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 3, 2, 0, 8, 2, 7, 3 },        /* R */
            { 0, 3, 1, 0, 8, 2, 7, 2 },        /* G */
            { 0, 3, 0, 0, 8, 2, 7, 1 },        /* B */
        },
        .flags = AV_PIX_FMT_FLAG_RGB,
    },
    [AV_PIX_FMT_YUV422P] = {
        .name = "yuv422p",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 1, 0, 0, 8, 0, 7, 1 },        /* Y */
            { 1, 1, 0, 0, 8, 0, 7, 1 },        /* U */
            { 2, 1, 0, 0, 8, 0, 7, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_YUV444P] = {
        .name = "yuv444p",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 1, 0, 0, 8, 0, 7, 1 },        /* Y */
            { 1, 1, 0, 0, 8, 0, 7, 1 },        /* U */
            { 2, 1, 0, 0, 8, 0, 7, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_YUV410P] = {
        .name = "yuv410p",
        .nb_components = 3,
        .log2_chroma_w = 2,
        .log2_chroma_h = 2,
        .comp = {
            { 0, 1, 0, 0, 8, 0, 7, 1 },        /* Y */
            { 1, 1, 0, 0, 8, 0, 7, 1 },        /* U */
            { 2, 1, 0, 0, 8, 0, 7, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_YUV411P] = {
        .name = "yuv411p",
        .nb_components = 3,
        .log2_chroma_w = 2,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 1, 0, 0, 8, 0, 7, 1 },        /* Y */
            { 1, 1, 0, 0, 8, 0, 7, 1 },        /* U */
            { 2, 1, 0, 0, 8, 0, 7, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_GRAY8] = {
        .name = "gray",
        .nb_components = 1,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 1, 0, 0, 8, 0, 7, 1 },        /* Y */
        },
        .flags = AV_PIX_FMT_FLAG_PSEUDOPAL,
        .alias = "gray8,y8",
    },
    [AV_PIX_FMT_MONOWHITE] = {
        .name = "monow",
        .nb_components = 1,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 1, 0, 0, 1, 0, 0, 1 },        /* Y */
        },
        .flags = AV_PIX_FMT_FLAG_BITSTREAM,
    },
    [AV_PIX_FMT_MONOBLACK] = {
        .name = "monob",
        .nb_components = 1,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 1, 0, 7, 1, 0, 0, 1 },        /* Y */
        },
        .flags = AV_PIX_FMT_FLAG_BITSTREAM,
    },
    [AV_PIX_FMT_PAL8] = {
        .name = "pal8",
        .nb_components = 1,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 1, 0, 0, 8, 0, 7, 1 },
        },
        .flags = AV_PIX_FMT_FLAG_PAL,
    },
    [AV_PIX_FMT_YUVJ420P] = {
        .name = "yuvj420p",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .comp = {
            { 0, 1, 0, 0, 8, 0, 7, 1 },        /* Y */
            { 1, 1, 0, 0, 8, 0, 7, 1 },        /* U */
            { 2, 1, 0, 0, 8, 0, 7, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_YUVJ422P] = {
        .name = "yuvj422p",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 1, 0, 0, 8, 0, 7, 1 },        /* Y */
            { 1, 1, 0, 0, 8, 0, 7, 1 },        /* U */
            { 2, 1, 0, 0, 8, 0, 7, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_YUVJ444P] = {
        .name = "yuvj444p",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 1, 0, 0, 8, 0, 7, 1 },        /* Y */
            { 1, 1, 0, 0, 8, 0, 7, 1 },        /* U */
            { 2, 1, 0, 0, 8, 0, 7, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR,
    },
#if FF_API_XVMC
    [AV_PIX_FMT_XVMC_MPEG2_MC] = {
        .name = "xvmcmc",
        .flags = AV_PIX_FMT_FLAG_HWACCEL,
    },
    [AV_PIX_FMT_XVMC_MPEG2_IDCT] = {
        .name = "xvmcidct",
        .flags = AV_PIX_FMT_FLAG_HWACCEL,
    },
#endif /* FF_API_XVMC */
    [AV_PIX_FMT_UYVY422] = {
        .name = "uyvy422",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 1, 0, 8, 1, 7, 2 },        /* Y */
            { 0, 4, 0, 0, 8, 3, 7, 1 },        /* U */
            { 0, 4, 2, 0, 8, 3, 7, 3 },        /* V */
        },
    },
    [AV_PIX_FMT_UYYVYY411] = {
        .name = "uyyvyy411",
        .nb_components = 3,
        .log2_chroma_w = 2,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 4, 1, 0, 8, 3, 7, 2 },        /* Y */
            { 0, 6, 0, 0, 8, 5, 7, 1 },        /* U */
            { 0, 6, 3, 0, 8, 5, 7, 4 },        /* V */
        },
    },
    [AV_PIX_FMT_BGR8] = {
        .name = "bgr8",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 1, 0, 0, 3, 0, 2, 1 },        /* R */
            { 0, 1, 0, 3, 3, 0, 2, 1 },        /* G */
            { 0, 1, 0, 6, 2, 0, 1, 1 },        /* B */
        },
        .flags = AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_PSEUDOPAL,
    },
    [AV_PIX_FMT_BGR4] = {
        .name = "bgr4",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 4, 3, 0, 1, 3, 0, 4 },        /* R */
            { 0, 4, 1, 0, 2, 3, 1, 2 },        /* G */
            { 0, 4, 0, 0, 1, 3, 0, 1 },        /* B */
        },
        .flags = AV_PIX_FMT_FLAG_BITSTREAM | AV_PIX_FMT_FLAG_RGB,
    },
    [AV_PIX_FMT_BGR4_BYTE] = {
        .name = "bgr4_byte",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 1, 0, 0, 1, 0, 0, 1 },        /* R */
            { 0, 1, 0, 1, 2, 0, 1, 1 },        /* G */
            { 0, 1, 0, 3, 1, 0, 0, 1 },        /* B */
        },
        .flags = AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_PSEUDOPAL,
    },
    [AV_PIX_FMT_RGB8] = {
        .name = "rgb8",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 1, 0, 6, 2, 0, 1, 1 },        /* R */
            { 0, 1, 0, 3, 3, 0, 2, 1 },        /* G */
            { 0, 1, 0, 0, 3, 0, 2, 1 },        /* B */
        },
        .flags = AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_PSEUDOPAL,
    },
    [AV_PIX_FMT_RGB4] = {
        .name = "rgb4",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 4, 0, 0, 1, 3, 0, 1 },        /* R */
            { 0, 4, 1, 0, 2, 3, 1, 2 },        /* G */
            { 0, 4, 3, 0, 1, 3, 0, 4 },        /* B */
        },
        .flags = AV_PIX_FMT_FLAG_BITSTREAM | AV_PIX_FMT_FLAG_RGB,
    },
    [AV_PIX_FMT_RGB4_BYTE] = {
        .name = "rgb4_byte",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 1, 0, 3, 1, 0, 0, 1 },        /* R */
            { 0, 1, 0, 1, 2, 0, 1, 1 },        /* G */
            { 0, 1, 0, 0, 1, 0, 0, 1 },        /* B */
        },
        .flags = AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_PSEUDOPAL,
    },
    [AV_PIX_FMT_NV12] = {
        .name = "nv12",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .comp = {
            { 0, 1, 0, 0, 8, 0, 7, 1 },        /* Y */
            { 1, 2, 0, 0, 8, 1, 7, 1 },        /* U */
            { 1, 2, 1, 0, 8, 1, 7, 2 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_NV21] = {
        .name = "nv21",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .comp = {
            { 0, 1, 0, 0, 8, 0, 7, 1 },        /* Y */
            { 1, 2, 1, 0, 8, 1, 7, 2 },        /* U */
            { 1, 2, 0, 0, 8, 1, 7, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_ARGB] = {
        .name = "argb",
        .nb_components = 4,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 4, 1, 0, 8, 3, 7, 2 },        /* R */
            { 0, 4, 2, 0, 8, 3, 7, 3 },        /* G */
            { 0, 4, 3, 0, 8, 3, 7, 4 },        /* B */
            { 0, 4, 0, 0, 8, 3, 7, 1 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_RGBA] = {
        .name = "rgba",
        .nb_components = 4,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 4, 0, 0, 8, 3, 7, 1 },        /* R */
            { 0, 4, 1, 0, 8, 3, 7, 2 },        /* G */
            { 0, 4, 2, 0, 8, 3, 7, 3 },        /* B */
            { 0, 4, 3, 0, 8, 3, 7, 4 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_ABGR] = {
        .name = "abgr",
        .nb_components = 4,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 4, 3, 0, 8, 3, 7, 4 },        /* R */
            { 0, 4, 2, 0, 8, 3, 7, 3 },        /* G */
            { 0, 4, 1, 0, 8, 3, 7, 2 },        /* B */
            { 0, 4, 0, 0, 8, 3, 7, 1 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_BGRA] = {
        .name = "bgra",
        .nb_components = 4,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 4, 2, 0, 8, 3, 7, 3 },        /* R */
            { 0, 4, 1, 0, 8, 3, 7, 2 },        /* G */
            { 0, 4, 0, 0, 8, 3, 7, 1 },        /* B */
            { 0, 4, 3, 0, 8, 3, 7, 4 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_GRAY16BE] = {
        .name = "gray16be",
        .nb_components = 1,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 16, 1, 15, 1 },       /* Y */
        },
        .flags = AV_PIX_FMT_FLAG_BE,
        .alias = "y16be",
    },
    [AV_PIX_FMT_GRAY16LE] = {
        .name = "gray16le",
        .nb_components = 1,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 16, 1, 15, 1 },       /* Y */
        },
        .alias = "y16le",
    },
    [AV_PIX_FMT_YUV440P] = {
        .name = "yuv440p",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 1,
        .comp = {
            { 0, 1, 0, 0, 8, 0, 7, 1 },        /* Y */
            { 1, 1, 0, 0, 8, 0, 7, 1 },        /* U */
            { 2, 1, 0, 0, 8, 0, 7, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_YUVJ440P] = {
        .name = "yuvj440p",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 1,
        .comp = {
            { 0, 1, 0, 0, 8, 0, 7, 1 },        /* Y */
            { 1, 1, 0, 0, 8, 0, 7, 1 },        /* U */
            { 2, 1, 0, 0, 8, 0, 7, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_YUVA420P] = {
        .name = "yuva420p",
        .nb_components = 4,
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .comp = {
            { 0, 1, 0, 0, 8, 0, 7, 1 },        /* Y */
            { 1, 1, 0, 0, 8, 0, 7, 1 },        /* U */
            { 2, 1, 0, 0, 8, 0, 7, 1 },        /* V */
            { 3, 1, 0, 0, 8, 0, 7, 1 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_ALPHA,
    },
        [AV_PIX_FMT_YUVA422P] = {
        .name = "yuva422p",
        .nb_components = 4,
        .log2_chroma_w = 1,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 1, 0, 0, 8, 0, 7, 1 },        /* Y */
            { 1, 1, 0, 0, 8, 0, 7, 1 },        /* U */
            { 2, 1, 0, 0, 8, 0, 7, 1 },        /* V */
            { 3, 1, 0, 0, 8, 0, 7, 1 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_YUVA444P] = {
        .name = "yuva444p",
        .nb_components = 4,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 1, 0, 0, 8, 0, 7, 1 },        /* Y */
            { 1, 1, 0, 0, 8, 0, 7, 1 },        /* U */
            { 2, 1, 0, 0, 8, 0, 7, 1 },        /* V */
            { 3, 1, 0, 0, 8, 0, 7, 1 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_YUVA420P9BE] = {
        .name = "yuva420p9be",
        .nb_components = 4,
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .comp = {
            { 0, 2, 0, 0, 9, 1, 8, 1 },        /* Y */
            { 1, 2, 0, 0, 9, 1, 8, 1 },        /* U */
            { 2, 2, 0, 0, 9, 1, 8, 1 },        /* V */
            { 3, 2, 0, 0, 9, 1, 8, 1 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_YUVA420P9LE] = {
        .name = "yuva420p9le",
        .nb_components = 4,
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .comp = {
            { 0, 2, 0, 0, 9, 1, 8, 1 },        /* Y */
            { 1, 2, 0, 0, 9, 1, 8, 1 },        /* U */
            { 2, 2, 0, 0, 9, 1, 8, 1 },        /* V */
            { 3, 2, 0, 0, 9, 1, 8, 1 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_YUVA422P9BE] = {
        .name = "yuva422p9be",
        .nb_components = 4,
        .log2_chroma_w = 1,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 9, 1, 8, 1 },        /* Y */
            { 1, 2, 0, 0, 9, 1, 8, 1 },        /* U */
            { 2, 2, 0, 0, 9, 1, 8, 1 },        /* V */
            { 3, 2, 0, 0, 9, 1, 8, 1 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_YUVA422P9LE] = {
        .name = "yuva422p9le",
        .nb_components = 4,
        .log2_chroma_w = 1,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 9, 1, 8, 1 },        /* Y */
            { 1, 2, 0, 0, 9, 1, 8, 1 },        /* U */
            { 2, 2, 0, 0, 9, 1, 8, 1 },        /* V */
            { 3, 2, 0, 0, 9, 1, 8, 1 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_YUVA444P9BE] = {
        .name = "yuva444p9be",
        .nb_components = 4,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 9, 1, 8, 1 },        /* Y */
            { 1, 2, 0, 0, 9, 1, 8, 1 },        /* U */
            { 2, 2, 0, 0, 9, 1, 8, 1 },        /* V */
            { 3, 2, 0, 0, 9, 1, 8, 1 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_YUVA444P9LE] = {
        .name = "yuva444p9le",
        .nb_components = 4,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 9, 1, 8, 1 },        /* Y */
            { 1, 2, 0, 0, 9, 1, 8, 1 },        /* U */
            { 2, 2, 0, 0, 9, 1, 8, 1 },        /* V */
            { 3, 2, 0, 0, 9, 1, 8, 1 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_YUVA420P10BE] = {
        .name = "yuva420p10be",
        .nb_components = 4,
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .comp = {
            { 0, 2, 0, 0, 10, 1, 9, 1 },        /* Y */
            { 1, 2, 0, 0, 10, 1, 9, 1 },        /* U */
            { 2, 2, 0, 0, 10, 1, 9, 1 },        /* V */
            { 3, 2, 0, 0, 10, 1, 9, 1 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_YUVA420P10LE] = {
        .name = "yuva420p10le",
        .nb_components = 4,
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .comp = {
            { 0, 2, 0, 0, 10, 1, 9, 1 },        /* Y */
            { 1, 2, 0, 0, 10, 1, 9, 1 },        /* U */
            { 2, 2, 0, 0, 10, 1, 9, 1 },        /* V */
            { 3, 2, 0, 0, 10, 1, 9, 1 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_YUVA422P10BE] = {
        .name = "yuva422p10be",
        .nb_components = 4,
        .log2_chroma_w = 1,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 10, 1, 9, 1 },        /* Y */
            { 1, 2, 0, 0, 10, 1, 9, 1 },        /* U */
            { 2, 2, 0, 0, 10, 1, 9, 1 },        /* V */
            { 3, 2, 0, 0, 10, 1, 9, 1 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_YUVA422P10LE] = {
        .name = "yuva422p10le",
        .nb_components = 4,
        .log2_chroma_w = 1,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 10, 1, 9, 1 },        /* Y */
            { 1, 2, 0, 0, 10, 1, 9, 1 },        /* U */
            { 2, 2, 0, 0, 10, 1, 9, 1 },        /* V */
            { 3, 2, 0, 0, 10, 1, 9, 1 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_YUVA444P10BE] = {
        .name = "yuva444p10be",
        .nb_components = 4,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 10, 1, 9, 1 },        /* Y */
            { 1, 2, 0, 0, 10, 1, 9, 1 },        /* U */
            { 2, 2, 0, 0, 10, 1, 9, 1 },        /* V */
            { 3, 2, 0, 0, 10, 1, 9, 1 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_YUVA444P10LE] = {
        .name = "yuva444p10le",
        .nb_components = 4,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 10, 1, 9, 1 },        /* Y */
            { 1, 2, 0, 0, 10, 1, 9, 1 },        /* U */
            { 2, 2, 0, 0, 10, 1, 9, 1 },        /* V */
            { 3, 2, 0, 0, 10, 1, 9, 1 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_YUVA420P16BE] = {
        .name = "yuva420p16be",
        .nb_components = 4,
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .comp = {
            { 0, 2, 0, 0, 16, 1, 15, 1 },        /* Y */
            { 1, 2, 0, 0, 16, 1, 15, 1 },        /* U */
            { 2, 2, 0, 0, 16, 1, 15, 1 },        /* V */
            { 3, 2, 0, 0, 16, 1, 15, 1 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_YUVA420P16LE] = {
        .name = "yuva420p16le",
        .nb_components = 4,
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .comp = {
            { 0, 2, 0, 0, 16, 1, 15, 1 },        /* Y */
            { 1, 2, 0, 0, 16, 1, 15, 1 },        /* U */
            { 2, 2, 0, 0, 16, 1, 15, 1 },        /* V */
            { 3, 2, 0, 0, 16, 1, 15, 1 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_YUVA422P16BE] = {
        .name = "yuva422p16be",
        .nb_components = 4,
        .log2_chroma_w = 1,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 16, 1, 15, 1 },        /* Y */
            { 1, 2, 0, 0, 16, 1, 15, 1 },        /* U */
            { 2, 2, 0, 0, 16, 1, 15, 1 },        /* V */
            { 3, 2, 0, 0, 16, 1, 15, 1 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_YUVA422P16LE] = {
        .name = "yuva422p16le",
        .nb_components = 4,
        .log2_chroma_w = 1,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 16, 1, 15, 1 },        /* Y */
            { 1, 2, 0, 0, 16, 1, 15, 1 },        /* U */
            { 2, 2, 0, 0, 16, 1, 15, 1 },        /* V */
            { 3, 2, 0, 0, 16, 1, 15, 1 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_YUVA444P16BE] = {
        .name = "yuva444p16be",
        .nb_components = 4,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 16, 1, 15, 1 },        /* Y */
            { 1, 2, 0, 0, 16, 1, 15, 1 },        /* U */
            { 2, 2, 0, 0, 16, 1, 15, 1 },        /* V */
            { 3, 2, 0, 0, 16, 1, 15, 1 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_YUVA444P16LE] = {
        .name = "yuva444p16le",
        .nb_components = 4,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 16, 1, 15, 1 },        /* Y */
            { 1, 2, 0, 0, 16, 1, 15, 1 },        /* U */
            { 2, 2, 0, 0, 16, 1, 15, 1 },        /* V */
            { 3, 2, 0, 0, 16, 1, 15, 1 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_ALPHA,
    },
#if FF_API_VDPAU
    [AV_PIX_FMT_VDPAU_H264] = {
        .name = "vdpau_h264",
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .flags = AV_PIX_FMT_FLAG_HWACCEL,
    },
    [AV_PIX_FMT_VDPAU_MPEG1] = {
        .name = "vdpau_mpeg1",
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .flags = AV_PIX_FMT_FLAG_HWACCEL,
    },
    [AV_PIX_FMT_VDPAU_MPEG2] = {
        .name = "vdpau_mpeg2",
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .flags = AV_PIX_FMT_FLAG_HWACCEL,
    },
    [AV_PIX_FMT_VDPAU_WMV3] = {
        .name = "vdpau_wmv3",
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .flags = AV_PIX_FMT_FLAG_HWACCEL,
    },
    [AV_PIX_FMT_VDPAU_VC1] = {
        .name = "vdpau_vc1",
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .flags = AV_PIX_FMT_FLAG_HWACCEL,
    },
    [AV_PIX_FMT_VDPAU_MPEG4] = {
        .name = "vdpau_mpeg4",
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .flags = AV_PIX_FMT_FLAG_HWACCEL,
    },
#endif
    [AV_PIX_FMT_RGB48BE] = {
        .name = "rgb48be",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 6, 0, 0, 16, 5, 15, 1 },       /* R */
            { 0, 6, 2, 0, 16, 5, 15, 3 },       /* G */
            { 0, 6, 4, 0, 16, 5, 15, 5 },       /* B */
        },
        .flags = AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_BE,
    },
    [AV_PIX_FMT_RGB48LE] = {
        .name = "rgb48le",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 6, 0, 0, 16, 5, 15, 1 },       /* R */
            { 0, 6, 2, 0, 16, 5, 15, 3 },       /* G */
            { 0, 6, 4, 0, 16, 5, 15, 5 },       /* B */
        },
        .flags = AV_PIX_FMT_FLAG_RGB,
    },
    [AV_PIX_FMT_RGBA64BE] = {
        .name = "rgba64be",
        .nb_components = 4,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 8, 0, 0, 16, 7, 15, 1 },       /* R */
            { 0, 8, 2, 0, 16, 7, 15, 3 },       /* G */
            { 0, 8, 4, 0, 16, 7, 15, 5 },       /* B */
            { 0, 8, 6, 0, 16, 7, 15, 7 },       /* A */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_RGBA64LE] = {
        .name = "rgba64le",
        .nb_components = 4,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 8, 0, 0, 16, 7, 15, 1 },       /* R */
            { 0, 8, 2, 0, 16, 7, 15, 3 },       /* G */
            { 0, 8, 4, 0, 16, 7, 15, 5 },       /* B */
            { 0, 8, 6, 0, 16, 7, 15, 7 },       /* A */
        },
        .flags = AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_RGB565BE] = {
        .name = "rgb565be",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, -1, 3, 5, 1, 4, 0 },        /* R */
            { 0, 2,  0, 5, 6, 1, 5, 1 },        /* G */
            { 0, 2,  0, 0, 5, 1, 4, 1 },        /* B */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_RGB,
    },
    [AV_PIX_FMT_RGB565LE] = {
        .name = "rgb565le",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 1, 3, 5, 1, 4, 2 },        /* R */
            { 0, 2, 0, 5, 6, 1, 5, 1 },        /* G */
            { 0, 2, 0, 0, 5, 1, 4, 1 },        /* B */
        },
        .flags = AV_PIX_FMT_FLAG_RGB,
    },
    [AV_PIX_FMT_RGB555BE] = {
        .name = "rgb555be",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, -1, 2, 5, 1, 4, 0 },        /* R */
            { 0, 2,  0, 5, 5, 1, 4, 1 },        /* G */
            { 0, 2,  0, 0, 5, 1, 4, 1 },        /* B */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_RGB,
    },
    [AV_PIX_FMT_RGB555LE] = {
        .name = "rgb555le",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 1, 2, 5, 1, 4, 2 },        /* R */
            { 0, 2, 0, 5, 5, 1, 4, 1 },        /* G */
            { 0, 2, 0, 0, 5, 1, 4, 1 },        /* B */
        },
        .flags = AV_PIX_FMT_FLAG_RGB,
    },
    [AV_PIX_FMT_RGB444BE] = {
        .name = "rgb444be",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, -1, 0, 4, 1, 3, 0 },        /* R */
            { 0, 2,  0, 4, 4, 1, 3, 1 },        /* G */
            { 0, 2,  0, 0, 4, 1, 3, 1 },        /* B */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_RGB,
    },
    [AV_PIX_FMT_RGB444LE] = {
        .name = "rgb444le",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 1, 0, 4, 1, 3, 2 },        /* R */
            { 0, 2, 0, 4, 4, 1, 3, 1 },        /* G */
            { 0, 2, 0, 0, 4, 1, 3, 1 },        /* B */
        },
        .flags = AV_PIX_FMT_FLAG_RGB,
    },
    [AV_PIX_FMT_BGR48BE] = {
        .name = "bgr48be",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 6, 4, 0, 16, 5, 15, 5 },       /* R */
            { 0, 6, 2, 0, 16, 5, 15, 3 },       /* G */
            { 0, 6, 0, 0, 16, 5, 15, 1 },       /* B */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_RGB,
    },
    [AV_PIX_FMT_BGR48LE] = {
        .name = "bgr48le",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 6, 4, 0, 16, 5, 15, 5 },       /* R */
            { 0, 6, 2, 0, 16, 5, 15, 3 },       /* G */
            { 0, 6, 0, 0, 16, 5, 15, 1 },       /* B */
        },
        .flags = AV_PIX_FMT_FLAG_RGB,
    },
    [AV_PIX_FMT_BGRA64BE] = {
        .name = "bgra64be",
        .nb_components = 4,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 8, 4, 0, 16, 7, 15, 5 },       /* R */
            { 0, 8, 2, 0, 16, 7, 15, 3 },       /* G */
            { 0, 8, 0, 0, 16, 7, 15, 1 },       /* B */
            { 0, 8, 6, 0, 16, 7, 15, 7 },       /* A */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_BGRA64LE] = {
        .name = "bgra64le",
        .nb_components = 4,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 8, 4, 0, 16, 7, 15, 5 },       /* R */
            { 0, 8, 2, 0, 16, 7, 15, 3 },       /* G */
            { 0, 8, 0, 0, 16, 7, 15, 1 },       /* B */
            { 0, 8, 6, 0, 16, 7, 15, 7 },       /* A */
        },
        .flags = AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_BGR565BE] = {
        .name = "bgr565be",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2,  0, 0, 5, 1, 4, 1 },        /* R */
            { 0, 2,  0, 5, 6, 1, 5, 1 },        /* G */
            { 0, 2, -1, 3, 5, 1, 4, 0 },        /* B */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_RGB,
    },
    [AV_PIX_FMT_BGR565LE] = {
        .name = "bgr565le",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 5, 1, 4, 1 },        /* R */
            { 0, 2, 0, 5, 6, 1, 5, 1 },        /* G */
            { 0, 2, 1, 3, 5, 1, 4, 2 },        /* B */
        },
        .flags = AV_PIX_FMT_FLAG_RGB,
    },
    [AV_PIX_FMT_BGR555BE] = {
        .name = "bgr555be",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2,  0, 0, 5, 1, 4, 1 },       /* R */
            { 0, 2,  0, 5, 5, 1, 4, 1 },       /* G */
            { 0, 2, -1, 2, 5, 1, 4, 0 },       /* B */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_RGB,
     },
    [AV_PIX_FMT_BGR555LE] = {
        .name = "bgr555le",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 5, 1, 4, 1 },        /* R */
            { 0, 2, 0, 5, 5, 1, 4, 1 },        /* G */
            { 0, 2, 1, 2, 5, 1, 4, 2 },        /* B */
        },
        .flags = AV_PIX_FMT_FLAG_RGB,
    },
    [AV_PIX_FMT_BGR444BE] = {
        .name = "bgr444be",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2,  0, 0, 4, 1, 3, 1 },       /* R */
            { 0, 2,  0, 4, 4, 1, 3, 1 },       /* G */
            { 0, 2, -1, 0, 4, 1, 3, 0 },       /* B */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_RGB,
     },
    [AV_PIX_FMT_BGR444LE] = {
        .name = "bgr444le",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 4, 1, 3, 1 },        /* R */
            { 0, 2, 0, 4, 4, 1, 3, 1 },        /* G */
            { 0, 2, 1, 0, 4, 1, 3, 2 },        /* B */
        },
        .flags = AV_PIX_FMT_FLAG_RGB,
    },
#if FF_API_VAAPI
    [AV_PIX_FMT_VAAPI_MOCO] = {
        .name = "vaapi_moco",
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .flags = AV_PIX_FMT_FLAG_HWACCEL,
    },
    [AV_PIX_FMT_VAAPI_IDCT] = {
        .name = "vaapi_idct",
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .flags = AV_PIX_FMT_FLAG_HWACCEL,
    },
    [AV_PIX_FMT_VAAPI_VLD] = {
        .name = "vaapi_vld",
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .flags = AV_PIX_FMT_FLAG_HWACCEL,
    },
#else
    [AV_PIX_FMT_VAAPI] = {
        .name = "vaapi",
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .flags = AV_PIX_FMT_FLAG_HWACCEL,
    },
#endif
    [AV_PIX_FMT_VDA_VLD] = {
        .name = "vda_vld",
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .flags = AV_PIX_FMT_FLAG_HWACCEL,
    },
    [AV_PIX_FMT_YUV420P9LE] = {
        .name = "yuv420p9le",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .comp = {
            { 0, 2, 0, 0, 9, 1, 8, 1 },        /* Y */
            { 1, 2, 0, 0, 9, 1, 8, 1 },        /* U */
            { 2, 2, 0, 0, 9, 1, 8, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_YUV420P9BE] = {
        .name = "yuv420p9be",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .comp = {
            { 0, 2, 0, 0, 9, 1, 8, 1 },        /* Y */
            { 1, 2, 0, 0, 9, 1, 8, 1 },        /* U */
            { 2, 2, 0, 0, 9, 1, 8, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_YUV420P10LE] = {
        .name = "yuv420p10le",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .comp = {
            { 0, 2, 0, 0, 10, 1, 9, 1 },        /* Y */
            { 1, 2, 0, 0, 10, 1, 9, 1 },        /* U */
            { 2, 2, 0, 0, 10, 1, 9, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_YUV420P10BE] = {
        .name = "yuv420p10be",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .comp = {
            { 0, 2, 0, 0, 10, 1, 9, 1 },        /* Y */
            { 1, 2, 0, 0, 10, 1, 9, 1 },        /* U */
            { 2, 2, 0, 0, 10, 1, 9, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_YUV420P16LE] = {
        .name = "yuv420p16le",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .comp = {
            { 0, 2, 0, 0, 16, 1, 15, 1 },        /* Y */
            { 1, 2, 0, 0, 16, 1, 15, 1 },        /* U */
            { 2, 2, 0, 0, 16, 1, 15, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_YUV420P16BE] = {
        .name = "yuv420p16be",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .comp = {
            { 0, 2, 0, 0, 16, 1, 15, 1 },        /* Y */
            { 1, 2, 0, 0, 16, 1, 15, 1 },        /* U */
            { 2, 2, 0, 0, 16, 1, 15, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_YUV422P9LE] = {
        .name = "yuv422p9le",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 9, 1, 8, 1 },        /* Y */
            { 1, 2, 0, 0, 9, 1, 8, 1 },        /* U */
            { 2, 2, 0, 0, 9, 1, 8, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_YUV422P9BE] = {
        .name = "yuv422p9be",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 9, 1, 8, 1 },        /* Y */
            { 1, 2, 0, 0, 9, 1, 8, 1 },        /* U */
            { 2, 2, 0, 0, 9, 1, 8, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_YUV422P10LE] = {
        .name = "yuv422p10le",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 10, 1, 9, 1 },        /* Y */
            { 1, 2, 0, 0, 10, 1, 9, 1 },        /* U */
            { 2, 2, 0, 0, 10, 1, 9, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_YUV422P10BE] = {
        .name = "yuv422p10be",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 10, 1, 9, 1 },        /* Y */
            { 1, 2, 0, 0, 10, 1, 9, 1 },        /* U */
            { 2, 2, 0, 0, 10, 1, 9, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_YUV422P16LE] = {
        .name = "yuv422p16le",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 16, 1, 15, 1 },        /* Y */
            { 1, 2, 0, 0, 16, 1, 15, 1 },        /* U */
            { 2, 2, 0, 0, 16, 1, 15, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_YUV422P16BE] = {
        .name = "yuv422p16be",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 16, 1, 15, 1 },        /* Y */
            { 1, 2, 0, 0, 16, 1, 15, 1 },        /* U */
            { 2, 2, 0, 0, 16, 1, 15, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_YUV444P16LE] = {
        .name = "yuv444p16le",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 16, 1, 15, 1 },        /* Y */
            { 1, 2, 0, 0, 16, 1, 15, 1 },        /* U */
            { 2, 2, 0, 0, 16, 1, 15, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_YUV444P16BE] = {
        .name = "yuv444p16be",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 16, 1, 15, 1 },        /* Y */
            { 1, 2, 0, 0, 16, 1, 15, 1 },        /* U */
            { 2, 2, 0, 0, 16, 1, 15, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_YUV444P10LE] = {
        .name = "yuv444p10le",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 10, 1, 9, 1 },        /* Y */
            { 1, 2, 0, 0, 10, 1, 9, 1 },        /* U */
            { 2, 2, 0, 0, 10, 1, 9, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_YUV444P10BE] = {
        .name = "yuv444p10be",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 10, 1, 9, 1 },        /* Y */
            { 1, 2, 0, 0, 10, 1, 9, 1 },        /* U */
            { 2, 2, 0, 0, 10, 1, 9, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_YUV444P9LE] = {
        .name = "yuv444p9le",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 9, 1, 8, 1 },        /* Y */
            { 1, 2, 0, 0, 9, 1, 8, 1 },        /* U */
            { 2, 2, 0, 0, 9, 1, 8, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_YUV444P9BE] = {
        .name = "yuv444p9be",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 9, 1, 8, 1 },        /* Y */
            { 1, 2, 0, 0, 9, 1, 8, 1 },        /* U */
            { 2, 2, 0, 0, 9, 1, 8, 1 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_D3D11VA_VLD] = {
        .name = "d3d11va_vld",
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .flags = AV_PIX_FMT_FLAG_HWACCEL,
    },
    [AV_PIX_FMT_DXVA2_VLD] = {
        .name = "dxva2_vld",
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .flags = AV_PIX_FMT_FLAG_HWACCEL,
    },
    [AV_PIX_FMT_YA8] = {
        .name = "ya8",
        .nb_components = 2,
        .comp = {
            { 0, 2, 0, 0, 8, 1, 7, 1 },        /* Y */
            { 0, 2, 1, 0, 8, 1, 7, 2 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_ALPHA,
        .alias = "gray8a",
    },
    [AV_PIX_FMT_YA16LE] = {
        .name = "ya16le",
        .nb_components = 2,
        .comp = {
            { 0, 4, 0, 0, 16, 3, 15, 1 },        /* Y */
            { 0, 4, 2, 0, 16, 3, 15, 3 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_YA16BE] = {
        .name = "ya16be",
        .nb_components = 2,
        .comp = {
            { 0, 4, 0, 0, 16, 3, 15, 1 },        /* Y */
            { 0, 4, 2, 0, 16, 3, 15, 3 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_GBRP] = {
        .name = "gbrp",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 2, 1, 0, 0, 8, 0, 7, 1 },        /* R */
            { 0, 1, 0, 0, 8, 0, 7, 1 },        /* G */
            { 1, 1, 0, 0, 8, 0, 7, 1 },        /* B */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_RGB,
    },
    [AV_PIX_FMT_GBRP9LE] = {
        .name = "gbrp9le",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 2, 2, 0, 0, 9, 1, 8, 1 },        /* R */
            { 0, 2, 0, 0, 9, 1, 8, 1 },        /* G */
            { 1, 2, 0, 0, 9, 1, 8, 1 },        /* B */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_RGB,
    },
    [AV_PIX_FMT_GBRP9BE] = {
        .name = "gbrp9be",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 2, 2, 0, 0, 9, 1, 8, 1 },        /* R */
            { 0, 2, 0, 0, 9, 1, 8, 1 },        /* G */
            { 1, 2, 0, 0, 9, 1, 8, 1 },        /* B */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_RGB,
    },
    [AV_PIX_FMT_GBRP10LE] = {
        .name = "gbrp10le",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 2, 2, 0, 0, 10, 1, 9, 1 },        /* R */
            { 0, 2, 0, 0, 10, 1, 9, 1 },        /* G */
            { 1, 2, 0, 0, 10, 1, 9, 1 },        /* B */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_RGB,
    },
    [AV_PIX_FMT_GBRP10BE] = {
        .name = "gbrp10be",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 2, 2, 0, 0, 10, 1, 9, 1 },        /* R */
            { 0, 2, 0, 0, 10, 1, 9, 1 },        /* G */
            { 1, 2, 0, 0, 10, 1, 9, 1 },        /* B */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_RGB,
    },
    [AV_PIX_FMT_GBRP16LE] = {
        .name = "gbrp16le",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 2, 2, 0, 0, 16, 1, 15, 1 },       /* R */
            { 0, 2, 0, 0, 16, 1, 15, 1 },       /* G */
            { 1, 2, 0, 0, 16, 1, 15, 1 },       /* B */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_RGB,
    },
    [AV_PIX_FMT_GBRP16BE] = {
        .name = "gbrp16be",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 2, 2, 0, 0, 16, 1, 15, 1 },       /* R */
            { 0, 2, 0, 0, 16, 1, 15, 1 },       /* G */
            { 1, 2, 0, 0, 16, 1, 15, 1 },       /* B */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_RGB,
    },
    [AV_PIX_FMT_GBRAP] = {
        .name = "gbrap",
        .nb_components = 4,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 2, 1, 0, 0, 8, 0, 7, 1 },        /* R */
            { 0, 1, 0, 0, 8, 0, 7, 1 },        /* G */
            { 1, 1, 0, 0, 8, 0, 7, 1 },        /* B */
            { 3, 1, 0, 0, 8, 0, 7, 1 },        /* A */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_RGB |
                 AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_GBRAP16LE] = {
        .name = "gbrap16le",
        .nb_components = 4,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 2, 2, 0, 0, 16, 1, 15, 1 },       /* R */
            { 0, 2, 0, 0, 16, 1, 15, 1 },       /* G */
            { 1, 2, 0, 0, 16, 1, 15, 1 },       /* B */
            { 3, 2, 0, 0, 16, 1, 15, 1 },       /* A */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_RGB |
                 AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_GBRAP16BE] = {
        .name = "gbrap16be",
        .nb_components = 4,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 2, 2, 0, 0, 16, 1, 15, 1 },       /* R */
            { 0, 2, 0, 0, 16, 1, 15, 1 },       /* G */
            { 1, 2, 0, 0, 16, 1, 15, 1 },       /* B */
            { 3, 2, 0, 0, 16, 1, 15, 1 },       /* A */
        },
        .flags = AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_PLANAR |
                 AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_ALPHA,
    },
    [AV_PIX_FMT_VDPAU] = {
        .name = "vdpau",
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .flags = AV_PIX_FMT_FLAG_HWACCEL,
    },
    [AV_PIX_FMT_XYZ12LE] = {
        .name = "xyz12le",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 6, 0, 4, 12, 5, 11, 1 },       /* X */
            { 0, 6, 2, 4, 12, 5, 11, 3 },       /* Y */
            { 0, 6, 4, 4, 12, 5, 11, 5 },       /* Z */
      },
      /*.flags = -- not used*/
    },
    [AV_PIX_FMT_XYZ12BE] = {
        .name = "xyz12be",
        .nb_components = 3,
        .log2_chroma_w = 0,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 6, 0, 4, 12, 5, 11, 1 },       /* X */
            { 0, 6, 2, 4, 12, 5, 11, 3 },       /* Y */
            { 0, 6, 4, 4, 12, 5, 11, 5 },       /* Z */
       },
        .flags = AV_PIX_FMT_FLAG_BE,
    },
    [AV_PIX_FMT_NV16] = {
        .name = "nv16",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 1, 0, 0, 8, 0, 7, 1 },        /* Y */
            { 1, 2, 0, 0, 8, 1, 7, 1 },        /* U */
            { 1, 2, 1, 0, 8, 1, 7, 2 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_NV20LE] = {
        .name = "nv20le",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 10, 1, 9, 1 },        /* Y */
            { 1, 4, 0, 0, 10, 3, 9, 1 },        /* U */
            { 1, 4, 2, 0, 10, 3, 9, 3 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_NV20BE] = {
        .name = "nv20be",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 0,
        .comp = {
            { 0, 2, 0, 0, 10, 1, 9, 1 },        /* Y */
            { 1, 4, 0, 0, 10, 3, 9, 1 },        /* U */
            { 1, 4, 2, 0, 10, 3, 9, 3 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_BE,
    },
    [AV_PIX_FMT_VDA] = {
        .name = "vda",
        .flags = AV_PIX_FMT_FLAG_HWACCEL,
    },
    [AV_PIX_FMT_QSV] = {
        .name = "qsv",
        .flags = AV_PIX_FMT_FLAG_HWACCEL,
    },
    [AV_PIX_FMT_MMAL] = {
        .name = "mmal",
        .flags = AV_PIX_FMT_FLAG_HWACCEL,
    },
    [AV_PIX_FMT_CUDA] = {
        .name = "cuda",
        .flags = AV_PIX_FMT_FLAG_HWACCEL,
    },
    [AV_PIX_FMT_P010LE] = {
        .name = "p010le",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .comp = {
            { 0, 2, 0, 6, 10, 1, 9, 1 },        /* Y */
            { 1, 4, 0, 6, 10, 3, 9, 1 },        /* U */
            { 1, 4, 2, 6, 10, 3, 9, 3 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR,
    },
    [AV_PIX_FMT_P010BE] = {
        .name = "p010be",
        .nb_components = 3,
        .log2_chroma_w = 1,
        .log2_chroma_h = 1,
        .comp = {
            { 0, 2, 0, 6, 10, 1, 9, 1 },        /* Y */
            { 1, 4, 0, 6, 10, 3, 9, 1 },        /* U */
            { 1, 4, 2, 6, 10, 3, 9, 3 },        /* V */
        },
        .flags = AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_BE,
    },
};
#if FF_API_PLUS1_MINUS1
FF_ENABLE_DEPRECATION_WARNINGS
#endif

static const char *color_range_names[] = {
    [AVCOL_RANGE_UNSPECIFIED] = "unknown",
    [AVCOL_RANGE_MPEG] = "tv",
    [AVCOL_RANGE_JPEG] = "pc",
};

static const char *color_primaries_names[] = {
    [AVCOL_PRI_RESERVED0] = "reserved",
    [AVCOL_PRI_BT709] = "bt709",
    [AVCOL_PRI_UNSPECIFIED] = "unknown",
    [AVCOL_PRI_RESERVED] = "reserved",
    [AVCOL_PRI_BT470M] = "bt470m",
    [AVCOL_PRI_BT470BG] = "bt470bg",
    [AVCOL_PRI_SMPTE170M] = "smpte170m",
    [AVCOL_PRI_SMPTE240M] = "smpte240m",
    [AVCOL_PRI_FILM] = "film",
    [AVCOL_PRI_BT2020] = "bt2020",
    [AVCOL_PRI_SMPTE428] = "smpte428",
    [AVCOL_PRI_SMPTE431] = "smpte431",
    [AVCOL_PRI_SMPTE432] = "smpte432",
};

static const char *color_transfer_names[] = {
    [AVCOL_TRC_RESERVED0] = "reserved",
    [AVCOL_TRC_BT709] = "bt709",
    [AVCOL_TRC_UNSPECIFIED] = "unknown",
    [AVCOL_TRC_RESERVED] = "reserved",
    [AVCOL_TRC_GAMMA22] = "bt470m",
    [AVCOL_TRC_GAMMA28] = "bt470bg",
    [AVCOL_TRC_SMPTE170M] = "smpte170m",
    [AVCOL_TRC_SMPTE240M] = "smpte240m",
    [AVCOL_TRC_LINEAR] = "linear",
    [AVCOL_TRC_LOG] = "log100",
    [AVCOL_TRC_LOG_SQRT] = "log316",
    [AVCOL_TRC_IEC61966_2_4] = "iec61966-2-4",
    [AVCOL_TRC_BT1361_ECG] = "bt1361e",
    [AVCOL_TRC_IEC61966_2_1] = "iec61966-2-1",
    [AVCOL_TRC_BT2020_10] = "bt2020-10",
    [AVCOL_TRC_BT2020_12] = "bt2020-12",
    [AVCOL_TRC_SMPTE2084] = "smpte2084",
    [AVCOL_TRC_SMPTE428] = "smpte428",
    [AVCOL_TRC_ARIB_STD_B67] = "arib-std-b67",
};

static const char *color_space_names[] = {
    [AVCOL_SPC_RGB] = "gbr",
    [AVCOL_SPC_BT709] = "bt709",
    [AVCOL_SPC_UNSPECIFIED] = "unknown",
    [AVCOL_SPC_RESERVED] = "reserved",
    [AVCOL_SPC_FCC] = "fcc",
    [AVCOL_SPC_BT470BG] = "bt470bg",
    [AVCOL_SPC_SMPTE170M] = "smpte170m",
    [AVCOL_SPC_SMPTE240M] = "smpte240m",
    [AVCOL_SPC_YCOCG] = "ycgco",
    [AVCOL_SPC_BT2020_NCL] = "bt2020nc",
    [AVCOL_SPC_BT2020_CL] = "bt2020c",
    [AVCOL_SPC_SMPTE2085] = "smpte2085",
};

static const char *chroma_location_names[] = {
    [AVCHROMA_LOC_UNSPECIFIED] = "unspecified",
    [AVCHROMA_LOC_LEFT] = "left",
    [AVCHROMA_LOC_CENTER] = "center",
    [AVCHROMA_LOC_TOPLEFT] = "topleft",
    [AVCHROMA_LOC_TOP] = "top",
    [AVCHROMA_LOC_BOTTOMLEFT] = "bottomleft",
    [AVCHROMA_LOC_BOTTOM] = "bottom",
};

static enum AVPixelFormat get_pix_fmt_internal(const char *name)
{
    enum AVPixelFormat pix_fmt;

    for (pix_fmt = 0; pix_fmt < AV_PIX_FMT_NB; pix_fmt++)
        if (av_pix_fmt_descriptors[pix_fmt].name &&
            (!strcmp(av_pix_fmt_descriptors[pix_fmt].name, name) ||
             av_match_name(name, av_pix_fmt_descriptors[pix_fmt].alias)))
            return pix_fmt;

    return AV_PIX_FMT_NONE;
}

const char *av_get_pix_fmt_name(enum AVPixelFormat pix_fmt)
{
    return (unsigned)pix_fmt < AV_PIX_FMT_NB ?
        av_pix_fmt_descriptors[pix_fmt].name : NULL;
}

#if HAVE_BIGENDIAN
#   define X_NE(be, le) be
#else
#   define X_NE(be, le) le
#endif

enum AVPixelFormat av_get_pix_fmt(const char *name)
{
    enum AVPixelFormat pix_fmt;

    if (!strcmp(name, "rgb32"))
        name = X_NE("argb", "bgra");
    else if (!strcmp(name, "bgr32"))
        name = X_NE("abgr", "rgba");

    pix_fmt = get_pix_fmt_internal(name);
    if (pix_fmt == AV_PIX_FMT_NONE) {
        char name2[32];

        snprintf(name2, sizeof(name2), "%s%s", name, X_NE("be", "le"));
        pix_fmt = get_pix_fmt_internal(name2);
    }

#if FF_API_VAAPI
    if (pix_fmt == AV_PIX_FMT_NONE && !strcmp(name, "vaapi"))
        pix_fmt = AV_PIX_FMT_VAAPI;
#endif
    return pix_fmt;
}

int av_get_bits_per_pixel(const AVPixFmtDescriptor *pixdesc)
{
    int c, bits = 0;
    int log2_pixels = pixdesc->log2_chroma_w + pixdesc->log2_chroma_h;

    for (c = 0; c < pixdesc->nb_components; c++) {
        int s = c == 1 || c == 2 ? 0 : log2_pixels;
        bits += pixdesc->comp[c].depth << s;
    }

    return bits >> log2_pixels;
}

char *av_get_pix_fmt_string(char *buf, int buf_size,
                            enum AVPixelFormat pix_fmt)
{
    /* print header */
    if (pix_fmt < 0) {
       snprintf (buf, buf_size, "name" " nb_components" " nb_bits");
    } else {
        const AVPixFmtDescriptor *pixdesc = &av_pix_fmt_descriptors[pix_fmt];
        snprintf(buf, buf_size, "%-11s %7d %10d", pixdesc->name,
                 pixdesc->nb_components, av_get_bits_per_pixel(pixdesc));
    }

    return buf;
}

const AVPixFmtDescriptor *av_pix_fmt_desc_get(enum AVPixelFormat pix_fmt)
{
    if (pix_fmt < 0 || pix_fmt >= AV_PIX_FMT_NB)
        return NULL;
    return &av_pix_fmt_descriptors[pix_fmt];
}

const AVPixFmtDescriptor *av_pix_fmt_desc_next(const AVPixFmtDescriptor *prev)
{
    if (!prev)
        return &av_pix_fmt_descriptors[0];
    if (prev - av_pix_fmt_descriptors < FF_ARRAY_ELEMS(av_pix_fmt_descriptors) - 1)
        return prev + 1;
    return NULL;
}

enum AVPixelFormat av_pix_fmt_desc_get_id(const AVPixFmtDescriptor *desc)
{
    if (desc < av_pix_fmt_descriptors ||
        desc >= av_pix_fmt_descriptors + FF_ARRAY_ELEMS(av_pix_fmt_descriptors))
        return AV_PIX_FMT_NONE;

    return desc - av_pix_fmt_descriptors;
}

int av_pix_fmt_get_chroma_sub_sample(enum AVPixelFormat pix_fmt,
                                     int *h_shift, int *v_shift)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    if (!desc)
        return AVERROR(ENOSYS);
    *h_shift = desc->log2_chroma_w;
    *v_shift = desc->log2_chroma_h;

    return 0;
}

int av_pix_fmt_count_planes(enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    int i, planes[4] = { 0 }, ret = 0;

    if (!desc)
        return AVERROR(EINVAL);

    for (i = 0; i < desc->nb_components; i++)
        planes[desc->comp[i].plane] = 1;
    for (i = 0; i < FF_ARRAY_ELEMS(planes); i++)
        ret += planes[i];
    return ret;
}


enum AVPixelFormat av_pix_fmt_swap_endianness(enum AVPixelFormat pix_fmt)
{
#define PIX_FMT_SWAP_ENDIANNESS(fmt)                                           \
    case AV_PIX_FMT_ ## fmt ## BE: return AV_PIX_FMT_ ## fmt ## LE;            \
    case AV_PIX_FMT_ ## fmt ## LE: return AV_PIX_FMT_ ## fmt ## BE

    switch (pix_fmt) {
    PIX_FMT_SWAP_ENDIANNESS(GRAY16);
    PIX_FMT_SWAP_ENDIANNESS(YA16);
    PIX_FMT_SWAP_ENDIANNESS(RGB48);
    PIX_FMT_SWAP_ENDIANNESS(RGB565);
    PIX_FMT_SWAP_ENDIANNESS(RGB555);
    PIX_FMT_SWAP_ENDIANNESS(RGB444);
    PIX_FMT_SWAP_ENDIANNESS(BGR48);
    PIX_FMT_SWAP_ENDIANNESS(BGR565);
    PIX_FMT_SWAP_ENDIANNESS(BGR555);
    PIX_FMT_SWAP_ENDIANNESS(BGR444);

    PIX_FMT_SWAP_ENDIANNESS(YUV420P9);
    PIX_FMT_SWAP_ENDIANNESS(YUV422P9);
    PIX_FMT_SWAP_ENDIANNESS(YUV444P9);
    PIX_FMT_SWAP_ENDIANNESS(YUV420P10);
    PIX_FMT_SWAP_ENDIANNESS(YUV422P10);
    PIX_FMT_SWAP_ENDIANNESS(YUV444P10);
    PIX_FMT_SWAP_ENDIANNESS(YUV420P16);
    PIX_FMT_SWAP_ENDIANNESS(YUV422P16);
    PIX_FMT_SWAP_ENDIANNESS(YUV444P16);

    PIX_FMT_SWAP_ENDIANNESS(GBRP9);
    PIX_FMT_SWAP_ENDIANNESS(GBRP10);
    PIX_FMT_SWAP_ENDIANNESS(GBRP16);
    PIX_FMT_SWAP_ENDIANNESS(YUVA420P9);
    PIX_FMT_SWAP_ENDIANNESS(YUVA422P9);
    PIX_FMT_SWAP_ENDIANNESS(YUVA444P9);
    PIX_FMT_SWAP_ENDIANNESS(YUVA420P10);
    PIX_FMT_SWAP_ENDIANNESS(YUVA422P10);
    PIX_FMT_SWAP_ENDIANNESS(YUVA444P10);
    PIX_FMT_SWAP_ENDIANNESS(YUVA420P16);
    PIX_FMT_SWAP_ENDIANNESS(YUVA422P16);
    PIX_FMT_SWAP_ENDIANNESS(YUVA444P16);

    PIX_FMT_SWAP_ENDIANNESS(XYZ12);
    PIX_FMT_SWAP_ENDIANNESS(NV20);
    PIX_FMT_SWAP_ENDIANNESS(RGBA64);
    PIX_FMT_SWAP_ENDIANNESS(BGRA64);
    default:
        return AV_PIX_FMT_NONE;
    }
#undef PIX_FMT_SWAP_ENDIANNESS
}

const char *av_color_range_name(enum AVColorRange range)
{
    return (unsigned) range < AVCOL_RANGE_NB ?
        color_range_names[range] : NULL;
}

const char *av_color_primaries_name(enum AVColorPrimaries primaries)
{
    return (unsigned) primaries < AVCOL_PRI_NB ?
        color_primaries_names[primaries] : NULL;
}

const char *av_color_transfer_name(enum AVColorTransferCharacteristic transfer)
{
    return (unsigned) transfer < AVCOL_TRC_NB ?
        color_transfer_names[transfer] : NULL;
}

const char *av_color_space_name(enum AVColorSpace space)
{
    return (unsigned) space < AVCOL_SPC_NB ?
        color_space_names[space] : NULL;
}

const char *av_chroma_location_name(enum AVChromaLocation location)
{
    return (unsigned) location < AVCHROMA_LOC_NB ?
        chroma_location_names[location] : NULL;
}

