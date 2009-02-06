/*
 * Misc image conversion routines
 * Copyright (c) 2001, 2002, 2003 Fabrice Bellard
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

/**
 * @file libavcodec/imgconvert.c
 * misc image conversion routines
 */

/* TODO:
 * - write 'ffimg' program to test all the image related stuff
 * - move all api to slice based system
 * - integrate deinterlacing, postprocessing and scaling in the conversion process
 */

#include "avcodec.h"
#include "dsputil.h"
#include "colorspace.h"

#if HAVE_MMX
#include "x86/mmx.h"
#include "x86/dsputil_mmx.h"
#endif

#define xglue(x, y) x ## y
#define glue(x, y) xglue(x, y)

#define FF_COLOR_RGB      0 /**< RGB color space */
#define FF_COLOR_GRAY     1 /**< gray color space */
#define FF_COLOR_YUV      2 /**< YUV color space. 16 <= Y <= 235, 16 <= U, V <= 240 */
#define FF_COLOR_YUV_JPEG 3 /**< YUV color space. 0 <= Y <= 255, 0 <= U, V <= 255 */

#define FF_PIXEL_PLANAR   0 /**< each channel has one component in AVPicture */
#define FF_PIXEL_PACKED   1 /**< only one components containing all the channels */
#define FF_PIXEL_PALETTE  2  /**< one components containing indexes for a palette */

typedef struct PixFmtInfo {
    const char *name;
    uint8_t nb_channels;     /**< number of channels (including alpha) */
    uint8_t color_type;      /**< color type (see FF_COLOR_xxx constants) */
    uint8_t pixel_type;      /**< pixel storage type (see FF_PIXEL_xxx constants) */
    uint8_t is_alpha : 1;    /**< true if alpha can be specified */
    uint8_t x_chroma_shift;  /**< X chroma subsampling factor is 2 ^ shift */
    uint8_t y_chroma_shift;  /**< Y chroma subsampling factor is 2 ^ shift */
    uint8_t depth;           /**< bit depth of the color components */
} PixFmtInfo;

/* this table gives more information about formats */
static const PixFmtInfo pix_fmt_info[PIX_FMT_NB] = {
    /* YUV formats */
    [PIX_FMT_YUV420P] = {
        .name = "yuv420p",
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
        .x_chroma_shift = 1, .y_chroma_shift = 1,
    },
    [PIX_FMT_YUV422P] = {
        .name = "yuv422p",
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
        .x_chroma_shift = 1, .y_chroma_shift = 0,
    },
    [PIX_FMT_YUV444P] = {
        .name = "yuv444p",
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
        .x_chroma_shift = 0, .y_chroma_shift = 0,
    },
    [PIX_FMT_YUYV422] = {
        .name = "yuyv422",
        .nb_channels = 1,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
        .x_chroma_shift = 1, .y_chroma_shift = 0,
    },
    [PIX_FMT_UYVY422] = {
        .name = "uyvy422",
        .nb_channels = 1,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
        .x_chroma_shift = 1, .y_chroma_shift = 0,
    },
    [PIX_FMT_YUV410P] = {
        .name = "yuv410p",
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
        .x_chroma_shift = 2, .y_chroma_shift = 2,
    },
    [PIX_FMT_YUV411P] = {
        .name = "yuv411p",
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
        .x_chroma_shift = 2, .y_chroma_shift = 0,
    },
    [PIX_FMT_YUV440P] = {
        .name = "yuv440p",
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
        .x_chroma_shift = 0, .y_chroma_shift = 1,
    },

    /* YUV formats with alpha plane */
    [PIX_FMT_YUVA420P] = {
        .name = "yuva420p",
        .nb_channels = 4,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
        .x_chroma_shift = 1, .y_chroma_shift = 1,
    },

    /* JPEG YUV */
    [PIX_FMT_YUVJ420P] = {
        .name = "yuvj420p",
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV_JPEG,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
        .x_chroma_shift = 1, .y_chroma_shift = 1,
    },
    [PIX_FMT_YUVJ422P] = {
        .name = "yuvj422p",
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV_JPEG,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
        .x_chroma_shift = 1, .y_chroma_shift = 0,
    },
    [PIX_FMT_YUVJ444P] = {
        .name = "yuvj444p",
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV_JPEG,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
        .x_chroma_shift = 0, .y_chroma_shift = 0,
    },
    [PIX_FMT_YUVJ440P] = {
        .name = "yuvj440p",
        .nb_channels = 3,
        .color_type = FF_COLOR_YUV_JPEG,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
        .x_chroma_shift = 0, .y_chroma_shift = 1,
    },

    /* RGB formats */
    [PIX_FMT_RGB24] = {
        .name = "rgb24",
        .nb_channels = 3,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
        .x_chroma_shift = 0, .y_chroma_shift = 0,
    },
    [PIX_FMT_BGR24] = {
        .name = "bgr24",
        .nb_channels = 3,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
        .x_chroma_shift = 0, .y_chroma_shift = 0,
    },
    [PIX_FMT_RGB32] = {
        .name = "rgb32",
        .nb_channels = 4, .is_alpha = 1,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
        .x_chroma_shift = 0, .y_chroma_shift = 0,
    },
    [PIX_FMT_RGB565] = {
        .name = "rgb565",
        .nb_channels = 3,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 5,
        .x_chroma_shift = 0, .y_chroma_shift = 0,
    },
    [PIX_FMT_RGB555] = {
        .name = "rgb555",
        .nb_channels = 3,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 5,
        .x_chroma_shift = 0, .y_chroma_shift = 0,
    },

    /* gray / mono formats */
    [PIX_FMT_GRAY16BE] = {
        .name = "gray16be",
        .nb_channels = 1,
        .color_type = FF_COLOR_GRAY,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 16,
    },
    [PIX_FMT_GRAY16LE] = {
        .name = "gray16le",
        .nb_channels = 1,
        .color_type = FF_COLOR_GRAY,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 16,
    },
    [PIX_FMT_GRAY8] = {
        .name = "gray",
        .nb_channels = 1,
        .color_type = FF_COLOR_GRAY,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
    },
    [PIX_FMT_MONOWHITE] = {
        .name = "monow",
        .nb_channels = 1,
        .color_type = FF_COLOR_GRAY,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 1,
    },
    [PIX_FMT_MONOBLACK] = {
        .name = "monob",
        .nb_channels = 1,
        .color_type = FF_COLOR_GRAY,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 1,
    },

    /* paletted formats */
    [PIX_FMT_PAL8] = {
        .name = "pal8",
        .nb_channels = 4, .is_alpha = 1,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PALETTE,
        .depth = 8,
    },
    [PIX_FMT_XVMC_MPEG2_MC] = {
        .name = "xvmcmc",
    },
    [PIX_FMT_XVMC_MPEG2_IDCT] = {
        .name = "xvmcidct",
    },
    [PIX_FMT_VDPAU_MPEG1] = {
        .name = "vdpau_mpeg1",
    },
    [PIX_FMT_VDPAU_MPEG2] = {
        .name = "vdpau_mpeg2",
    },
    [PIX_FMT_VDPAU_H264] = {
        .name = "vdpau_h264",
    },
    [PIX_FMT_VDPAU_WMV3] = {
        .name = "vdpau_wmv3",
    },
    [PIX_FMT_VDPAU_VC1] = {
        .name = "vdpau_vc1",
    },
    [PIX_FMT_UYYVYY411] = {
        .name = "uyyvyy411",
        .nb_channels = 1,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
        .x_chroma_shift = 2, .y_chroma_shift = 0,
    },
    [PIX_FMT_BGR32] = {
        .name = "bgr32",
        .nb_channels = 4, .is_alpha = 1,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
        .x_chroma_shift = 0, .y_chroma_shift = 0,
    },
    [PIX_FMT_BGR565] = {
        .name = "bgr565",
        .nb_channels = 3,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 5,
        .x_chroma_shift = 0, .y_chroma_shift = 0,
    },
    [PIX_FMT_BGR555] = {
        .name = "bgr555",
        .nb_channels = 3,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 5,
        .x_chroma_shift = 0, .y_chroma_shift = 0,
    },
    [PIX_FMT_RGB8] = {
        .name = "rgb8",
        .nb_channels = 1,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
        .x_chroma_shift = 0, .y_chroma_shift = 0,
    },
    [PIX_FMT_RGB4] = {
        .name = "rgb4",
        .nb_channels = 1,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 4,
        .x_chroma_shift = 0, .y_chroma_shift = 0,
    },
    [PIX_FMT_RGB4_BYTE] = {
        .name = "rgb4_byte",
        .nb_channels = 1,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
        .x_chroma_shift = 0, .y_chroma_shift = 0,
    },
    [PIX_FMT_BGR8] = {
        .name = "bgr8",
        .nb_channels = 1,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
        .x_chroma_shift = 0, .y_chroma_shift = 0,
    },
    [PIX_FMT_BGR4] = {
        .name = "bgr4",
        .nb_channels = 1,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 4,
        .x_chroma_shift = 0, .y_chroma_shift = 0,
    },
    [PIX_FMT_BGR4_BYTE] = {
        .name = "bgr4_byte",
        .nb_channels = 1,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
        .x_chroma_shift = 0, .y_chroma_shift = 0,
    },
    [PIX_FMT_NV12] = {
        .name = "nv12",
        .nb_channels = 2,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
        .x_chroma_shift = 1, .y_chroma_shift = 1,
    },
    [PIX_FMT_NV21] = {
        .name = "nv12",
        .nb_channels = 2,
        .color_type = FF_COLOR_YUV,
        .pixel_type = FF_PIXEL_PLANAR,
        .depth = 8,
        .x_chroma_shift = 1, .y_chroma_shift = 1,
    },

    [PIX_FMT_BGR32_1] = {
        .name = "bgr32_1",
        .nb_channels = 4, .is_alpha = 1,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
        .x_chroma_shift = 0, .y_chroma_shift = 0,
    },
    [PIX_FMT_RGB32_1] = {
        .name = "rgb32_1",
        .nb_channels = 4, .is_alpha = 1,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 8,
        .x_chroma_shift = 0, .y_chroma_shift = 0,
    },
};

void avcodec_get_chroma_sub_sample(int pix_fmt, int *h_shift, int *v_shift)
{
    *h_shift = pix_fmt_info[pix_fmt].x_chroma_shift;
    *v_shift = pix_fmt_info[pix_fmt].y_chroma_shift;
}

const char *avcodec_get_pix_fmt_name(int pix_fmt)
{
    if (pix_fmt < 0 || pix_fmt >= PIX_FMT_NB)
        return NULL;
    else
        return pix_fmt_info[pix_fmt].name;
}

enum PixelFormat avcodec_get_pix_fmt(const char* name)
{
    int i;

    for (i=0; i < PIX_FMT_NB; i++)
         if (!strcmp(pix_fmt_info[i].name, name))
             return i;
    return PIX_FMT_NONE;
}

void avcodec_pix_fmt_string (char *buf, int buf_size, int pix_fmt)
{
    /* print header */
    if (pix_fmt < 0)
        snprintf (buf, buf_size,
                  "name      " " nb_channels" " depth" " is_alpha"
            );
    else{
        PixFmtInfo info= pix_fmt_info[pix_fmt];

        char is_alpha_char= info.is_alpha ? 'y' : 'n';

        snprintf (buf, buf_size,
                  "%-10s" "      %1d     " "   %2d " "     %c   ",
                  info.name,
                  info.nb_channels,
                  info.depth,
                  is_alpha_char
            );
    }
}

int ff_set_systematic_pal(uint32_t pal[256], enum PixelFormat pix_fmt){
    int i;

    for(i=0; i<256; i++){
        int r,g,b;

        switch(pix_fmt) {
        case PIX_FMT_RGB8:
            r= (i>>5    )*36;
            g= ((i>>2)&7)*36;
            b= (i&3     )*85;
            break;
        case PIX_FMT_BGR8:
            b= (i>>6    )*85;
            g= ((i>>3)&7)*36;
            r= (i&7     )*36;
            break;
        case PIX_FMT_RGB4_BYTE:
            r= (i>>3    )*255;
            g= ((i>>1)&3)*85;
            b= (i&1     )*255;
            break;
        case PIX_FMT_BGR4_BYTE:
            b= (i>>3    )*255;
            g= ((i>>1)&3)*85;
            r= (i&1     )*255;
            break;
        case PIX_FMT_GRAY8:
            r=b=g= i;
            break;
        }
        pal[i] =  b + (g<<8) + (r<<16);
    }

    return 0;
}

int ff_fill_linesize(AVPicture *picture, int pix_fmt, int width)
{
    int w2;
    const PixFmtInfo *pinfo;

    memset(picture->linesize, 0, sizeof(picture->linesize));

    pinfo = &pix_fmt_info[pix_fmt];
    switch(pix_fmt) {
    case PIX_FMT_YUV420P:
    case PIX_FMT_YUV422P:
    case PIX_FMT_YUV444P:
    case PIX_FMT_YUV410P:
    case PIX_FMT_YUV411P:
    case PIX_FMT_YUV440P:
    case PIX_FMT_YUVJ420P:
    case PIX_FMT_YUVJ422P:
    case PIX_FMT_YUVJ444P:
    case PIX_FMT_YUVJ440P:
        w2 = (width + (1 << pinfo->x_chroma_shift) - 1) >> pinfo->x_chroma_shift;
        picture->linesize[0] = width;
        picture->linesize[1] = w2;
        picture->linesize[2] = w2;
        break;
    case PIX_FMT_YUVA420P:
        w2 = (width + (1 << pinfo->x_chroma_shift) - 1) >> pinfo->x_chroma_shift;
        picture->linesize[0] = width;
        picture->linesize[1] = w2;
        picture->linesize[2] = w2;
        picture->linesize[3] = width;
        break;
    case PIX_FMT_NV12:
    case PIX_FMT_NV21:
        w2 = (width + (1 << pinfo->x_chroma_shift) - 1) >> pinfo->x_chroma_shift;
        picture->linesize[0] = width;
        picture->linesize[1] = w2;
        break;
    case PIX_FMT_RGB24:
    case PIX_FMT_BGR24:
        picture->linesize[0] = width * 3;
        break;
    case PIX_FMT_RGB32:
    case PIX_FMT_BGR32:
    case PIX_FMT_RGB32_1:
    case PIX_FMT_BGR32_1:
        picture->linesize[0] = width * 4;
        break;
    case PIX_FMT_GRAY16BE:
    case PIX_FMT_GRAY16LE:
    case PIX_FMT_BGR555:
    case PIX_FMT_BGR565:
    case PIX_FMT_RGB555:
    case PIX_FMT_RGB565:
    case PIX_FMT_YUYV422:
        picture->linesize[0] = width * 2;
        break;
    case PIX_FMT_UYVY422:
        picture->linesize[0] = width * 2;
        break;
    case PIX_FMT_UYYVYY411:
        picture->linesize[0] = width + width/2;
        break;
    case PIX_FMT_RGB4:
    case PIX_FMT_BGR4:
        picture->linesize[0] = width / 2;
        break;
    case PIX_FMT_MONOWHITE:
    case PIX_FMT_MONOBLACK:
        picture->linesize[0] = (width + 7) >> 3;
        break;
    case PIX_FMT_PAL8:
    case PIX_FMT_RGB8:
    case PIX_FMT_BGR8:
    case PIX_FMT_RGB4_BYTE:
    case PIX_FMT_BGR4_BYTE:
    case PIX_FMT_GRAY8:
        picture->linesize[0] = width;
        picture->linesize[1] = 4;
        break;
    default:
        return -1;
    }
    return 0;
}

int ff_fill_pointer(AVPicture *picture, uint8_t *ptr, int pix_fmt,
                    int height)
{
    int size, h2, size2;
    const PixFmtInfo *pinfo;

    pinfo = &pix_fmt_info[pix_fmt];
    size = picture->linesize[0] * height;
    switch(pix_fmt) {
    case PIX_FMT_YUV420P:
    case PIX_FMT_YUV422P:
    case PIX_FMT_YUV444P:
    case PIX_FMT_YUV410P:
    case PIX_FMT_YUV411P:
    case PIX_FMT_YUV440P:
    case PIX_FMT_YUVJ420P:
    case PIX_FMT_YUVJ422P:
    case PIX_FMT_YUVJ444P:
    case PIX_FMT_YUVJ440P:
        h2 = (height + (1 << pinfo->y_chroma_shift) - 1) >> pinfo->y_chroma_shift;
        size2 = picture->linesize[1] * h2;
        picture->data[0] = ptr;
        picture->data[1] = picture->data[0] + size;
        picture->data[2] = picture->data[1] + size2;
        picture->data[3] = NULL;
        return size + 2 * size2;
    case PIX_FMT_YUVA420P:
        h2 = (height + (1 << pinfo->y_chroma_shift) - 1) >> pinfo->y_chroma_shift;
        size2 = picture->linesize[1] * h2;
        picture->data[0] = ptr;
        picture->data[1] = picture->data[0] + size;
        picture->data[2] = picture->data[1] + size2;
        picture->data[3] = picture->data[1] + size2 + size2;
        return 2 * size + 2 * size2;
    case PIX_FMT_NV12:
    case PIX_FMT_NV21:
        h2 = (height + (1 << pinfo->y_chroma_shift) - 1) >> pinfo->y_chroma_shift;
        size2 = picture->linesize[1] * h2 * 2;
        picture->data[0] = ptr;
        picture->data[1] = picture->data[0] + size;
        picture->data[2] = NULL;
        picture->data[3] = NULL;
        return size + 2 * size2;
    case PIX_FMT_RGB24:
    case PIX_FMT_BGR24:
    case PIX_FMT_RGB32:
    case PIX_FMT_BGR32:
    case PIX_FMT_RGB32_1:
    case PIX_FMT_BGR32_1:
    case PIX_FMT_GRAY16BE:
    case PIX_FMT_GRAY16LE:
    case PIX_FMT_BGR555:
    case PIX_FMT_BGR565:
    case PIX_FMT_RGB555:
    case PIX_FMT_RGB565:
    case PIX_FMT_YUYV422:
    case PIX_FMT_UYVY422:
    case PIX_FMT_UYYVYY411:
    case PIX_FMT_RGB4:
    case PIX_FMT_BGR4:
    case PIX_FMT_MONOWHITE:
    case PIX_FMT_MONOBLACK:
        picture->data[0] = ptr;
        picture->data[1] = NULL;
        picture->data[2] = NULL;
        picture->data[3] = NULL;
        return size;
    case PIX_FMT_PAL8:
    case PIX_FMT_RGB8:
    case PIX_FMT_BGR8:
    case PIX_FMT_RGB4_BYTE:
    case PIX_FMT_BGR4_BYTE:
    case PIX_FMT_GRAY8:
        size2 = (size + 3) & ~3;
        picture->data[0] = ptr;
        picture->data[1] = ptr + size2; /* palette is stored here as 256 32 bit words */
        picture->data[2] = NULL;
        picture->data[3] = NULL;
        return size2 + 256 * 4;
    default:
        picture->data[0] = NULL;
        picture->data[1] = NULL;
        picture->data[2] = NULL;
        picture->data[3] = NULL;
        return -1;
    }
}

int avpicture_fill(AVPicture *picture, uint8_t *ptr,
                   int pix_fmt, int width, int height)
{

    if(avcodec_check_dimensions(NULL, width, height))
        return -1;

    if (ff_fill_linesize(picture, pix_fmt, width))
        return -1;

    return ff_fill_pointer(picture, ptr, pix_fmt, height);
}

int avpicture_layout(const AVPicture* src, int pix_fmt, int width, int height,
                     unsigned char *dest, int dest_size)
{
    const PixFmtInfo* pf = &pix_fmt_info[pix_fmt];
    int i, j, w, h, data_planes;
    const unsigned char* s;
    int size = avpicture_get_size(pix_fmt, width, height);

    if (size > dest_size || size < 0)
        return -1;

    if (pf->pixel_type == FF_PIXEL_PACKED || pf->pixel_type == FF_PIXEL_PALETTE) {
        if (pix_fmt == PIX_FMT_YUYV422 ||
            pix_fmt == PIX_FMT_UYVY422 ||
            pix_fmt == PIX_FMT_BGR565 ||
            pix_fmt == PIX_FMT_BGR555 ||
            pix_fmt == PIX_FMT_RGB565 ||
            pix_fmt == PIX_FMT_RGB555)
            w = width * 2;
        else if (pix_fmt == PIX_FMT_UYYVYY411)
          w = width + width/2;
        else if (pix_fmt == PIX_FMT_PAL8)
          w = width;
        else
          w = width * (pf->depth * pf->nb_channels / 8);

        data_planes = 1;
        h = height;
    } else {
        data_planes = pf->nb_channels;
        w = (width*pf->depth + 7)/8;
        h = height;
    }

    for (i=0; i<data_planes; i++) {
         if (i == 1) {
             w = width >> pf->x_chroma_shift;
             h = height >> pf->y_chroma_shift;
         }
         s = src->data[i];
         for(j=0; j<h; j++) {
             memcpy(dest, s, w);
             dest += w;
             s += src->linesize[i];
         }
    }

    if (pf->pixel_type == FF_PIXEL_PALETTE)
        memcpy((unsigned char *)(((size_t)dest + 3) & ~3), src->data[1], 256 * 4);

    return size;
}

int avpicture_get_size(int pix_fmt, int width, int height)
{
    AVPicture dummy_pict;
    return avpicture_fill(&dummy_pict, NULL, pix_fmt, width, height);
}

int avcodec_get_pix_fmt_loss(int dst_pix_fmt, int src_pix_fmt,
                             int has_alpha)
{
    const PixFmtInfo *pf, *ps;
    int loss;

    ps = &pix_fmt_info[src_pix_fmt];
    pf = &pix_fmt_info[dst_pix_fmt];

    /* compute loss */
    loss = 0;
    pf = &pix_fmt_info[dst_pix_fmt];
    if (pf->depth < ps->depth ||
        (dst_pix_fmt == PIX_FMT_RGB555 && src_pix_fmt == PIX_FMT_RGB565))
        loss |= FF_LOSS_DEPTH;
    if (pf->x_chroma_shift > ps->x_chroma_shift ||
        pf->y_chroma_shift > ps->y_chroma_shift)
        loss |= FF_LOSS_RESOLUTION;
    switch(pf->color_type) {
    case FF_COLOR_RGB:
        if (ps->color_type != FF_COLOR_RGB &&
            ps->color_type != FF_COLOR_GRAY)
            loss |= FF_LOSS_COLORSPACE;
        break;
    case FF_COLOR_GRAY:
        if (ps->color_type != FF_COLOR_GRAY)
            loss |= FF_LOSS_COLORSPACE;
        break;
    case FF_COLOR_YUV:
        if (ps->color_type != FF_COLOR_YUV)
            loss |= FF_LOSS_COLORSPACE;
        break;
    case FF_COLOR_YUV_JPEG:
        if (ps->color_type != FF_COLOR_YUV_JPEG &&
            ps->color_type != FF_COLOR_YUV &&
            ps->color_type != FF_COLOR_GRAY)
            loss |= FF_LOSS_COLORSPACE;
        break;
    default:
        /* fail safe test */
        if (ps->color_type != pf->color_type)
            loss |= FF_LOSS_COLORSPACE;
        break;
    }
    if (pf->color_type == FF_COLOR_GRAY &&
        ps->color_type != FF_COLOR_GRAY)
        loss |= FF_LOSS_CHROMA;
    if (!pf->is_alpha && (ps->is_alpha && has_alpha))
        loss |= FF_LOSS_ALPHA;
    if (pf->pixel_type == FF_PIXEL_PALETTE &&
        (ps->pixel_type != FF_PIXEL_PALETTE && ps->color_type != FF_COLOR_GRAY))
        loss |= FF_LOSS_COLORQUANT;
    return loss;
}

static int avg_bits_per_pixel(int pix_fmt)
{
    int bits;
    const PixFmtInfo *pf;

    pf = &pix_fmt_info[pix_fmt];
    switch(pf->pixel_type) {
    case FF_PIXEL_PACKED:
        switch(pix_fmt) {
        case PIX_FMT_YUYV422:
        case PIX_FMT_UYVY422:
        case PIX_FMT_RGB565:
        case PIX_FMT_RGB555:
        case PIX_FMT_BGR565:
        case PIX_FMT_BGR555:
            bits = 16;
            break;
        case PIX_FMT_UYYVYY411:
            bits = 12;
            break;
        default:
            bits = pf->depth * pf->nb_channels;
            break;
        }
        break;
    case FF_PIXEL_PLANAR:
        if (pf->x_chroma_shift == 0 && pf->y_chroma_shift == 0) {
            bits = pf->depth * pf->nb_channels;
        } else {
            bits = pf->depth + ((2 * pf->depth) >>
                                (pf->x_chroma_shift + pf->y_chroma_shift));
        }
        break;
    case FF_PIXEL_PALETTE:
        bits = 8;
        break;
    default:
        bits = -1;
        break;
    }
    return bits;
}

static int avcodec_find_best_pix_fmt1(int64_t pix_fmt_mask,
                                      int src_pix_fmt,
                                      int has_alpha,
                                      int loss_mask)
{
    int dist, i, loss, min_dist, dst_pix_fmt;

    /* find exact color match with smallest size */
    dst_pix_fmt = -1;
    min_dist = 0x7fffffff;
    for(i = 0;i < PIX_FMT_NB; i++) {
        if (pix_fmt_mask & (1ULL << i)) {
            loss = avcodec_get_pix_fmt_loss(i, src_pix_fmt, has_alpha) & loss_mask;
            if (loss == 0) {
                dist = avg_bits_per_pixel(i);
                if (dist < min_dist) {
                    min_dist = dist;
                    dst_pix_fmt = i;
                }
            }
        }
    }
    return dst_pix_fmt;
}

int avcodec_find_best_pix_fmt(int64_t pix_fmt_mask, int src_pix_fmt,
                              int has_alpha, int *loss_ptr)
{
    int dst_pix_fmt, loss_mask, i;
    static const int loss_mask_order[] = {
        ~0, /* no loss first */
        ~FF_LOSS_ALPHA,
        ~FF_LOSS_RESOLUTION,
        ~(FF_LOSS_COLORSPACE | FF_LOSS_RESOLUTION),
        ~FF_LOSS_COLORQUANT,
        ~FF_LOSS_DEPTH,
        0,
    };

    /* try with successive loss */
    i = 0;
    for(;;) {
        loss_mask = loss_mask_order[i++];
        dst_pix_fmt = avcodec_find_best_pix_fmt1(pix_fmt_mask, src_pix_fmt,
                                                 has_alpha, loss_mask);
        if (dst_pix_fmt >= 0)
            goto found;
        if (loss_mask == 0)
            break;
    }
    return -1;
 found:
    if (loss_ptr)
        *loss_ptr = avcodec_get_pix_fmt_loss(dst_pix_fmt, src_pix_fmt, has_alpha);
    return dst_pix_fmt;
}

void ff_img_copy_plane(uint8_t *dst, int dst_wrap,
                           const uint8_t *src, int src_wrap,
                           int width, int height)
{
    if((!dst) || (!src))
        return;
    for(;height > 0; height--) {
        memcpy(dst, src, width);
        dst += dst_wrap;
        src += src_wrap;
    }
}

int ff_get_plane_bytewidth(enum PixelFormat pix_fmt, int width, int plane)
{
    int bits;
    const PixFmtInfo *pf = &pix_fmt_info[pix_fmt];

    pf = &pix_fmt_info[pix_fmt];
    switch(pf->pixel_type) {
    case FF_PIXEL_PACKED:
        switch(pix_fmt) {
        case PIX_FMT_YUYV422:
        case PIX_FMT_UYVY422:
        case PIX_FMT_RGB565:
        case PIX_FMT_RGB555:
        case PIX_FMT_BGR565:
        case PIX_FMT_BGR555:
            bits = 16;
            break;
        case PIX_FMT_UYYVYY411:
            bits = 12;
            break;
        default:
            bits = pf->depth * pf->nb_channels;
            break;
        }
        return (width * bits + 7) >> 3;
        break;
    case FF_PIXEL_PLANAR:
            if (plane == 1 || plane == 2)
                width= -((-width)>>pf->x_chroma_shift);

            return (width * pf->depth + 7) >> 3;
        break;
    case FF_PIXEL_PALETTE:
        if (plane == 0)
            return width;
        break;
    }

    return -1;
}

void av_picture_copy(AVPicture *dst, const AVPicture *src,
              int pix_fmt, int width, int height)
{
    int i;
    const PixFmtInfo *pf = &pix_fmt_info[pix_fmt];

    pf = &pix_fmt_info[pix_fmt];
    switch(pf->pixel_type) {
    case FF_PIXEL_PACKED:
    case FF_PIXEL_PLANAR:
        for(i = 0; i < pf->nb_channels; i++) {
            int h;
            int bwidth = ff_get_plane_bytewidth(pix_fmt, width, i);
            h = height;
            if (i == 1 || i == 2) {
                h= -((-height)>>pf->y_chroma_shift);
            }
            ff_img_copy_plane(dst->data[i], dst->linesize[i],
                           src->data[i], src->linesize[i],
                           bwidth, h);
        }
        break;
    case FF_PIXEL_PALETTE:
        ff_img_copy_plane(dst->data[0], dst->linesize[0],
                       src->data[0], src->linesize[0],
                       width, height);
        /* copy the palette */
        ff_img_copy_plane(dst->data[1], dst->linesize[1],
                       src->data[1], src->linesize[1],
                       4, 256);
        break;
    }
}

/* XXX: totally non optimized */

static void yuyv422_to_yuv420p(AVPicture *dst, const AVPicture *src,
                              int width, int height)
{
    const uint8_t *p, *p1;
    uint8_t *lum, *cr, *cb, *lum1, *cr1, *cb1;
    int w;

    p1 = src->data[0];
    lum1 = dst->data[0];
    cb1 = dst->data[1];
    cr1 = dst->data[2];

    for(;height >= 1; height -= 2) {
        p = p1;
        lum = lum1;
        cb = cb1;
        cr = cr1;
        for(w = width; w >= 2; w -= 2) {
            lum[0] = p[0];
            cb[0] = p[1];
            lum[1] = p[2];
            cr[0] = p[3];
            p += 4;
            lum += 2;
            cb++;
            cr++;
        }
        if (w) {
            lum[0] = p[0];
            cb[0] = p[1];
            cr[0] = p[3];
            cb++;
            cr++;
        }
        p1 += src->linesize[0];
        lum1 += dst->linesize[0];
        if (height>1) {
            p = p1;
            lum = lum1;
            for(w = width; w >= 2; w -= 2) {
                lum[0] = p[0];
                lum[1] = p[2];
                p += 4;
                lum += 2;
            }
            if (w) {
                lum[0] = p[0];
            }
            p1 += src->linesize[0];
            lum1 += dst->linesize[0];
        }
        cb1 += dst->linesize[1];
        cr1 += dst->linesize[2];
    }
}

static void uyvy422_to_yuv420p(AVPicture *dst, const AVPicture *src,
                              int width, int height)
{
    const uint8_t *p, *p1;
    uint8_t *lum, *cr, *cb, *lum1, *cr1, *cb1;
    int w;

    p1 = src->data[0];

    lum1 = dst->data[0];
    cb1 = dst->data[1];
    cr1 = dst->data[2];

    for(;height >= 1; height -= 2) {
        p = p1;
        lum = lum1;
        cb = cb1;
        cr = cr1;
        for(w = width; w >= 2; w -= 2) {
            lum[0] = p[1];
            cb[0] = p[0];
            lum[1] = p[3];
            cr[0] = p[2];
            p += 4;
            lum += 2;
            cb++;
            cr++;
        }
        if (w) {
            lum[0] = p[1];
            cb[0] = p[0];
            cr[0] = p[2];
            cb++;
            cr++;
        }
        p1 += src->linesize[0];
        lum1 += dst->linesize[0];
        if (height>1) {
            p = p1;
            lum = lum1;
            for(w = width; w >= 2; w -= 2) {
                lum[0] = p[1];
                lum[1] = p[3];
                p += 4;
                lum += 2;
            }
            if (w) {
                lum[0] = p[1];
            }
            p1 += src->linesize[0];
            lum1 += dst->linesize[0];
        }
        cb1 += dst->linesize[1];
        cr1 += dst->linesize[2];
    }
}


static void uyvy422_to_yuv422p(AVPicture *dst, const AVPicture *src,
                              int width, int height)
{
    const uint8_t *p, *p1;
    uint8_t *lum, *cr, *cb, *lum1, *cr1, *cb1;
    int w;

    p1 = src->data[0];
    lum1 = dst->data[0];
    cb1 = dst->data[1];
    cr1 = dst->data[2];
    for(;height > 0; height--) {
        p = p1;
        lum = lum1;
        cb = cb1;
        cr = cr1;
        for(w = width; w >= 2; w -= 2) {
            lum[0] = p[1];
            cb[0] = p[0];
            lum[1] = p[3];
            cr[0] = p[2];
            p += 4;
            lum += 2;
            cb++;
            cr++;
        }
        p1 += src->linesize[0];
        lum1 += dst->linesize[0];
        cb1 += dst->linesize[1];
        cr1 += dst->linesize[2];
    }
}


static void yuyv422_to_yuv422p(AVPicture *dst, const AVPicture *src,
                              int width, int height)
{
    const uint8_t *p, *p1;
    uint8_t *lum, *cr, *cb, *lum1, *cr1, *cb1;
    int w;

    p1 = src->data[0];
    lum1 = dst->data[0];
    cb1 = dst->data[1];
    cr1 = dst->data[2];
    for(;height > 0; height--) {
        p = p1;
        lum = lum1;
        cb = cb1;
        cr = cr1;
        for(w = width; w >= 2; w -= 2) {
            lum[0] = p[0];
            cb[0] = p[1];
            lum[1] = p[2];
            cr[0] = p[3];
            p += 4;
            lum += 2;
            cb++;
            cr++;
        }
        p1 += src->linesize[0];
        lum1 += dst->linesize[0];
        cb1 += dst->linesize[1];
        cr1 += dst->linesize[2];
    }
}

static void yuv422p_to_yuyv422(AVPicture *dst, const AVPicture *src,
                              int width, int height)
{
    uint8_t *p, *p1;
    const uint8_t *lum, *cr, *cb, *lum1, *cr1, *cb1;
    int w;

    p1 = dst->data[0];
    lum1 = src->data[0];
    cb1 = src->data[1];
    cr1 = src->data[2];
    for(;height > 0; height--) {
        p = p1;
        lum = lum1;
        cb = cb1;
        cr = cr1;
        for(w = width; w >= 2; w -= 2) {
            p[0] = lum[0];
            p[1] = cb[0];
            p[2] = lum[1];
            p[3] = cr[0];
            p += 4;
            lum += 2;
            cb++;
            cr++;
        }
        p1 += dst->linesize[0];
        lum1 += src->linesize[0];
        cb1 += src->linesize[1];
        cr1 += src->linesize[2];
    }
}

static void yuv422p_to_uyvy422(AVPicture *dst, const AVPicture *src,
                              int width, int height)
{
    uint8_t *p, *p1;
    const uint8_t *lum, *cr, *cb, *lum1, *cr1, *cb1;
    int w;

    p1 = dst->data[0];
    lum1 = src->data[0];
    cb1 = src->data[1];
    cr1 = src->data[2];
    for(;height > 0; height--) {
        p = p1;
        lum = lum1;
        cb = cb1;
        cr = cr1;
        for(w = width; w >= 2; w -= 2) {
            p[1] = lum[0];
            p[0] = cb[0];
            p[3] = lum[1];
            p[2] = cr[0];
            p += 4;
            lum += 2;
            cb++;
            cr++;
        }
        p1 += dst->linesize[0];
        lum1 += src->linesize[0];
        cb1 += src->linesize[1];
        cr1 += src->linesize[2];
    }
}

static void uyyvyy411_to_yuv411p(AVPicture *dst, const AVPicture *src,
                              int width, int height)
{
    const uint8_t *p, *p1;
    uint8_t *lum, *cr, *cb, *lum1, *cr1, *cb1;
    int w;

    p1 = src->data[0];
    lum1 = dst->data[0];
    cb1 = dst->data[1];
    cr1 = dst->data[2];
    for(;height > 0; height--) {
        p = p1;
        lum = lum1;
        cb = cb1;
        cr = cr1;
        for(w = width; w >= 4; w -= 4) {
            cb[0] = p[0];
            lum[0] = p[1];
            lum[1] = p[2];
            cr[0] = p[3];
            lum[2] = p[4];
            lum[3] = p[5];
            p += 6;
            lum += 4;
            cb++;
            cr++;
        }
        p1 += src->linesize[0];
        lum1 += dst->linesize[0];
        cb1 += dst->linesize[1];
        cr1 += dst->linesize[2];
    }
}


static void yuv420p_to_yuyv422(AVPicture *dst, const AVPicture *src,
                              int width, int height)
{
    int w, h;
    uint8_t *line1, *line2, *linesrc = dst->data[0];
    uint8_t *lum1, *lum2, *lumsrc = src->data[0];
    uint8_t *cb1, *cb2 = src->data[1];
    uint8_t *cr1, *cr2 = src->data[2];

    for(h = height / 2; h--;) {
        line1 = linesrc;
        line2 = linesrc + dst->linesize[0];

        lum1 = lumsrc;
        lum2 = lumsrc + src->linesize[0];

        cb1 = cb2;
        cr1 = cr2;

        for(w = width / 2; w--;) {
                *line1++ = *lum1++; *line2++ = *lum2++;
                *line1++ =          *line2++ = *cb1++;
                *line1++ = *lum1++; *line2++ = *lum2++;
                *line1++ =          *line2++ = *cr1++;
        }

        linesrc += dst->linesize[0] * 2;
        lumsrc += src->linesize[0] * 2;
        cb2 += src->linesize[1];
        cr2 += src->linesize[2];
    }
}

static void yuv420p_to_uyvy422(AVPicture *dst, const AVPicture *src,
                              int width, int height)
{
    int w, h;
    uint8_t *line1, *line2, *linesrc = dst->data[0];
    uint8_t *lum1, *lum2, *lumsrc = src->data[0];
    uint8_t *cb1, *cb2 = src->data[1];
    uint8_t *cr1, *cr2 = src->data[2];

    for(h = height / 2; h--;) {
        line1 = linesrc;
        line2 = linesrc + dst->linesize[0];

        lum1 = lumsrc;
        lum2 = lumsrc + src->linesize[0];

        cb1 = cb2;
        cr1 = cr2;

        for(w = width / 2; w--;) {
                *line1++ =          *line2++ = *cb1++;
                *line1++ = *lum1++; *line2++ = *lum2++;
                *line1++ =          *line2++ = *cr1++;
                *line1++ = *lum1++; *line2++ = *lum2++;
        }

        linesrc += dst->linesize[0] * 2;
        lumsrc += src->linesize[0] * 2;
        cb2 += src->linesize[1];
        cr2 += src->linesize[2];
    }
}

/* 2x2 -> 1x1 */
void ff_shrink22(uint8_t *dst, int dst_wrap,
                     const uint8_t *src, int src_wrap,
                     int width, int height)
{
    int w;
    const uint8_t *s1, *s2;
    uint8_t *d;

    for(;height > 0; height--) {
        s1 = src;
        s2 = s1 + src_wrap;
        d = dst;
        for(w = width;w >= 4; w-=4) {
            d[0] = (s1[0] + s1[1] + s2[0] + s2[1] + 2) >> 2;
            d[1] = (s1[2] + s1[3] + s2[2] + s2[3] + 2) >> 2;
            d[2] = (s1[4] + s1[5] + s2[4] + s2[5] + 2) >> 2;
            d[3] = (s1[6] + s1[7] + s2[6] + s2[7] + 2) >> 2;
            s1 += 8;
            s2 += 8;
            d += 4;
        }
        for(;w > 0; w--) {
            d[0] = (s1[0] + s1[1] + s2[0] + s2[1] + 2) >> 2;
            s1 += 2;
            s2 += 2;
            d++;
        }
        src += 2 * src_wrap;
        dst += dst_wrap;
    }
}

/* 4x4 -> 1x1 */
void ff_shrink44(uint8_t *dst, int dst_wrap,
                     const uint8_t *src, int src_wrap,
                     int width, int height)
{
    int w;
    const uint8_t *s1, *s2, *s3, *s4;
    uint8_t *d;

    for(;height > 0; height--) {
        s1 = src;
        s2 = s1 + src_wrap;
        s3 = s2 + src_wrap;
        s4 = s3 + src_wrap;
        d = dst;
        for(w = width;w > 0; w--) {
            d[0] = (s1[0] + s1[1] + s1[2] + s1[3] +
                    s2[0] + s2[1] + s2[2] + s2[3] +
                    s3[0] + s3[1] + s3[2] + s3[3] +
                    s4[0] + s4[1] + s4[2] + s4[3] + 8) >> 4;
            s1 += 4;
            s2 += 4;
            s3 += 4;
            s4 += 4;
            d++;
        }
        src += 4 * src_wrap;
        dst += dst_wrap;
    }
}

/* 8x8 -> 1x1 */
void ff_shrink88(uint8_t *dst, int dst_wrap,
                     const uint8_t *src, int src_wrap,
                     int width, int height)
{
    int w, i;

    for(;height > 0; height--) {
        for(w = width;w > 0; w--) {
            int tmp=0;
            for(i=0; i<8; i++){
                tmp += src[0] + src[1] + src[2] + src[3] + src[4] + src[5] + src[6] + src[7];
                src += src_wrap;
            }
            *(dst++) = (tmp + 32)>>6;
            src += 8 - 8*src_wrap;
        }
        src += 8*src_wrap - 8*width;
        dst += dst_wrap - width;
    }
}

/* XXX: add jpeg quantize code */

#define TRANSP_INDEX (6*6*6)

/* this is maybe slow, but allows for extensions */
static inline unsigned char gif_clut_index(uint8_t r, uint8_t g, uint8_t b)
{
    return (((r) / 47) % 6) * 6 * 6 + (((g) / 47) % 6) * 6 + (((b) / 47) % 6);
}

static void build_rgb_palette(uint8_t *palette, int has_alpha)
{
    uint32_t *pal;
    static const uint8_t pal_value[6] = { 0x00, 0x33, 0x66, 0x99, 0xcc, 0xff };
    int i, r, g, b;

    pal = (uint32_t *)palette;
    i = 0;
    for(r = 0; r < 6; r++) {
        for(g = 0; g < 6; g++) {
            for(b = 0; b < 6; b++) {
                pal[i++] = (0xff << 24) | (pal_value[r] << 16) |
                    (pal_value[g] << 8) | pal_value[b];
            }
        }
    }
    if (has_alpha)
        pal[i++] = 0;
    while (i < 256)
        pal[i++] = 0xff000000;
}

/* copy bit n to bits 0 ... n - 1 */
static inline unsigned int bitcopy_n(unsigned int a, int n)
{
    int mask;
    mask = (1 << n) - 1;
    return (a & (0xff & ~mask)) | ((-((a >> n) & 1)) & mask);
}

/* rgb555 handling */

#define RGB_NAME rgb555

#define RGB_IN(r, g, b, s)\
{\
    unsigned int v = ((const uint16_t *)(s))[0];\
    r = bitcopy_n(v >> (10 - 3), 3);\
    g = bitcopy_n(v >> (5 - 3), 3);\
    b = bitcopy_n(v << 3, 3);\
}


#define RGB_OUT(d, r, g, b)\
{\
    ((uint16_t *)(d))[0] = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);\
}

#define BPP 2

#include "imgconvert_template.c"

/* rgb565 handling */

#define RGB_NAME rgb565

#define RGB_IN(r, g, b, s)\
{\
    unsigned int v = ((const uint16_t *)(s))[0];\
    r = bitcopy_n(v >> (11 - 3), 3);\
    g = bitcopy_n(v >> (5 - 2), 2);\
    b = bitcopy_n(v << 3, 3);\
}

#define RGB_OUT(d, r, g, b)\
{\
    ((uint16_t *)(d))[0] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);\
}

#define BPP 2

#include "imgconvert_template.c"

/* bgr24 handling */

#define RGB_NAME bgr24

#define RGB_IN(r, g, b, s)\
{\
    b = (s)[0];\
    g = (s)[1];\
    r = (s)[2];\
}

#define RGB_OUT(d, r, g, b)\
{\
    (d)[0] = b;\
    (d)[1] = g;\
    (d)[2] = r;\
}

#define BPP 3

#include "imgconvert_template.c"

#undef RGB_IN
#undef RGB_OUT
#undef BPP

/* rgb24 handling */

#define RGB_NAME rgb24
#define FMT_RGB24

#define RGB_IN(r, g, b, s)\
{\
    r = (s)[0];\
    g = (s)[1];\
    b = (s)[2];\
}

#define RGB_OUT(d, r, g, b)\
{\
    (d)[0] = r;\
    (d)[1] = g;\
    (d)[2] = b;\
}

#define BPP 3

#include "imgconvert_template.c"

/* rgb32 handling */

#define RGB_NAME rgb32
#define FMT_RGB32

#define RGB_IN(r, g, b, s)\
{\
    unsigned int v = ((const uint32_t *)(s))[0];\
    r = (v >> 16) & 0xff;\
    g = (v >> 8) & 0xff;\
    b = v & 0xff;\
}

#define RGBA_IN(r, g, b, a, s)\
{\
    unsigned int v = ((const uint32_t *)(s))[0];\
    a = (v >> 24) & 0xff;\
    r = (v >> 16) & 0xff;\
    g = (v >> 8) & 0xff;\
    b = v & 0xff;\
}

#define RGBA_OUT(d, r, g, b, a)\
{\
    ((uint32_t *)(d))[0] = (a << 24) | (r << 16) | (g << 8) | b;\
}

#define BPP 4

#include "imgconvert_template.c"

static void mono_to_gray(AVPicture *dst, const AVPicture *src,
                         int width, int height, int xor_mask)
{
    const unsigned char *p;
    unsigned char *q;
    int v, dst_wrap, src_wrap;
    int y, w;

    p = src->data[0];
    src_wrap = src->linesize[0] - ((width + 7) >> 3);

    q = dst->data[0];
    dst_wrap = dst->linesize[0] - width;
    for(y=0;y<height;y++) {
        w = width;
        while (w >= 8) {
            v = *p++ ^ xor_mask;
            q[0] = -(v >> 7);
            q[1] = -((v >> 6) & 1);
            q[2] = -((v >> 5) & 1);
            q[3] = -((v >> 4) & 1);
            q[4] = -((v >> 3) & 1);
            q[5] = -((v >> 2) & 1);
            q[6] = -((v >> 1) & 1);
            q[7] = -((v >> 0) & 1);
            w -= 8;
            q += 8;
        }
        if (w > 0) {
            v = *p++ ^ xor_mask;
            do {
                q[0] = -((v >> 7) & 1);
                q++;
                v <<= 1;
            } while (--w);
        }
        p += src_wrap;
        q += dst_wrap;
    }
}

static void monowhite_to_gray(AVPicture *dst, const AVPicture *src,
                               int width, int height)
{
    mono_to_gray(dst, src, width, height, 0xff);
}

static void monoblack_to_gray(AVPicture *dst, const AVPicture *src,
                               int width, int height)
{
    mono_to_gray(dst, src, width, height, 0x00);
}

static void gray_to_mono(AVPicture *dst, const AVPicture *src,
                         int width, int height, int xor_mask)
{
    int n;
    const uint8_t *s;
    uint8_t *d;
    int j, b, v, n1, src_wrap, dst_wrap, y;

    s = src->data[0];
    src_wrap = src->linesize[0] - width;

    d = dst->data[0];
    dst_wrap = dst->linesize[0] - ((width + 7) >> 3);

    for(y=0;y<height;y++) {
        n = width;
        while (n >= 8) {
            v = 0;
            for(j=0;j<8;j++) {
                b = s[0];
                s++;
                v = (v << 1) | (b >> 7);
            }
            d[0] = v ^ xor_mask;
            d++;
            n -= 8;
        }
        if (n > 0) {
            n1 = n;
            v = 0;
            while (n > 0) {
                b = s[0];
                s++;
                v = (v << 1) | (b >> 7);
                n--;
            }
            d[0] = (v << (8 - (n1 & 7))) ^ xor_mask;
            d++;
        }
        s += src_wrap;
        d += dst_wrap;
    }
}

static void gray_to_monowhite(AVPicture *dst, const AVPicture *src,
                              int width, int height)
{
    gray_to_mono(dst, src, width, height, 0xff);
}

static void gray_to_monoblack(AVPicture *dst, const AVPicture *src,
                              int width, int height)
{
    gray_to_mono(dst, src, width, height, 0x00);
}

static void gray_to_gray16(AVPicture *dst, const AVPicture *src,
                              int width, int height)
{
    int x, y, src_wrap, dst_wrap;
    uint8_t *s, *d;
    s = src->data[0];
    src_wrap = src->linesize[0] - width;
    d = dst->data[0];
    dst_wrap = dst->linesize[0] - width * 2;
    for(y=0; y<height; y++){
        for(x=0; x<width; x++){
            *d++ = *s;
            *d++ = *s++;
        }
        s += src_wrap;
        d += dst_wrap;
    }
}

static void gray16_to_gray(AVPicture *dst, const AVPicture *src,
                              int width, int height)
{
    int x, y, src_wrap, dst_wrap;
    uint8_t *s, *d;
    s = src->data[0];
    src_wrap = src->linesize[0] - width * 2;
    d = dst->data[0];
    dst_wrap = dst->linesize[0] - width;
    for(y=0; y<height; y++){
        for(x=0; x<width; x++){
            *d++ = *s;
            s += 2;
        }
        s += src_wrap;
        d += dst_wrap;
    }
}

static void gray16be_to_gray(AVPicture *dst, const AVPicture *src,
                              int width, int height)
{
    gray16_to_gray(dst, src, width, height);
}

static void gray16le_to_gray(AVPicture *dst, const AVPicture *src,
                              int width, int height)
{
    AVPicture tmpsrc = *src;
    tmpsrc.data[0]++;
    gray16_to_gray(dst, &tmpsrc, width, height);
}

static void gray16_to_gray16(AVPicture *dst, const AVPicture *src,
                              int width, int height)
{
    int x, y, src_wrap, dst_wrap;
    uint16_t *s, *d;
    s = (uint16_t*)src->data[0];
    src_wrap = (src->linesize[0] - width * 2)/2;
    d = (uint16_t*)dst->data[0];
    dst_wrap = (dst->linesize[0] - width * 2)/2;
    for(y=0; y<height; y++){
        for(x=0; x<width; x++){
            *d++ = bswap_16(*s++);
        }
        s += src_wrap;
        d += dst_wrap;
    }
}


typedef struct ConvertEntry {
    void (*convert)(AVPicture *dst,
                    const AVPicture *src, int width, int height);
} ConvertEntry;

/* Add each new conversion function in this table. In order to be able
   to convert from any format to any format, the following constraints
   must be satisfied:

   - all FF_COLOR_RGB formats must convert to and from PIX_FMT_RGB24

   - all FF_COLOR_GRAY formats must convert to and from PIX_FMT_GRAY8

   - all FF_COLOR_RGB formats with alpha must convert to and from PIX_FMT_RGB32

   - PIX_FMT_YUV444P and PIX_FMT_YUVJ444P must convert to and from
     PIX_FMT_RGB24.

   - PIX_FMT_422 must convert to and from PIX_FMT_422P.

   The other conversion functions are just optimizations for common cases.
*/
static const ConvertEntry convert_table[PIX_FMT_NB][PIX_FMT_NB] = {
    [PIX_FMT_YUV420P] = {
        [PIX_FMT_YUYV422] = {
            .convert = yuv420p_to_yuyv422,
        },
        [PIX_FMT_RGB555] = {
            .convert = yuv420p_to_rgb555
        },
        [PIX_FMT_RGB565] = {
            .convert = yuv420p_to_rgb565
        },
        [PIX_FMT_BGR24] = {
            .convert = yuv420p_to_bgr24
        },
        [PIX_FMT_RGB24] = {
            .convert = yuv420p_to_rgb24
        },
        [PIX_FMT_RGB32] = {
            .convert = yuv420p_to_rgb32
        },
        [PIX_FMT_UYVY422] = {
            .convert = yuv420p_to_uyvy422,
        },
    },
    [PIX_FMT_YUV422P] = {
        [PIX_FMT_YUYV422] = {
            .convert = yuv422p_to_yuyv422,
        },
        [PIX_FMT_UYVY422] = {
            .convert = yuv422p_to_uyvy422,
        },
    },
    [PIX_FMT_YUV444P] = {
        [PIX_FMT_RGB24] = {
            .convert = yuv444p_to_rgb24
        },
    },
    [PIX_FMT_YUVJ420P] = {
        [PIX_FMT_RGB555] = {
            .convert = yuvj420p_to_rgb555
        },
        [PIX_FMT_RGB565] = {
            .convert = yuvj420p_to_rgb565
        },
        [PIX_FMT_BGR24] = {
            .convert = yuvj420p_to_bgr24
        },
        [PIX_FMT_RGB24] = {
            .convert = yuvj420p_to_rgb24
        },
        [PIX_FMT_RGB32] = {
            .convert = yuvj420p_to_rgb32
        },
    },
    [PIX_FMT_YUVJ444P] = {
        [PIX_FMT_RGB24] = {
            .convert = yuvj444p_to_rgb24
        },
    },
    [PIX_FMT_YUYV422] = {
        [PIX_FMT_YUV420P] = {
            .convert = yuyv422_to_yuv420p,
        },
        [PIX_FMT_YUV422P] = {
            .convert = yuyv422_to_yuv422p,
        },
    },
    [PIX_FMT_UYVY422] = {
        [PIX_FMT_YUV420P] = {
            .convert = uyvy422_to_yuv420p,
        },
        [PIX_FMT_YUV422P] = {
            .convert = uyvy422_to_yuv422p,
        },
    },
    [PIX_FMT_RGB24] = {
        [PIX_FMT_YUV420P] = {
            .convert = rgb24_to_yuv420p
        },
        [PIX_FMT_RGB565] = {
            .convert = rgb24_to_rgb565
        },
        [PIX_FMT_RGB555] = {
            .convert = rgb24_to_rgb555
        },
        [PIX_FMT_RGB32] = {
            .convert = rgb24_to_rgb32
        },
        [PIX_FMT_BGR24] = {
            .convert = rgb24_to_bgr24
        },
        [PIX_FMT_GRAY8] = {
            .convert = rgb24_to_gray
        },
        [PIX_FMT_PAL8] = {
            .convert = rgb24_to_pal8
        },
        [PIX_FMT_YUV444P] = {
            .convert = rgb24_to_yuv444p
        },
        [PIX_FMT_YUVJ420P] = {
            .convert = rgb24_to_yuvj420p
        },
        [PIX_FMT_YUVJ444P] = {
            .convert = rgb24_to_yuvj444p
        },
    },
    [PIX_FMT_RGB32] = {
        [PIX_FMT_RGB24] = {
            .convert = rgb32_to_rgb24
        },
        [PIX_FMT_BGR24] = {
            .convert = rgb32_to_bgr24
        },
        [PIX_FMT_RGB565] = {
            .convert = rgb32_to_rgb565
        },
        [PIX_FMT_RGB555] = {
            .convert = rgb32_to_rgb555
        },
        [PIX_FMT_PAL8] = {
            .convert = rgb32_to_pal8
        },
        [PIX_FMT_YUV420P] = {
            .convert = rgb32_to_yuv420p
        },
        [PIX_FMT_GRAY8] = {
            .convert = rgb32_to_gray
        },
    },
    [PIX_FMT_BGR24] = {
        [PIX_FMT_RGB32] = {
            .convert = bgr24_to_rgb32
        },
        [PIX_FMT_RGB24] = {
            .convert = bgr24_to_rgb24
        },
        [PIX_FMT_YUV420P] = {
            .convert = bgr24_to_yuv420p
        },
        [PIX_FMT_GRAY8] = {
            .convert = bgr24_to_gray
        },
    },
    [PIX_FMT_RGB555] = {
        [PIX_FMT_RGB24] = {
            .convert = rgb555_to_rgb24
        },
        [PIX_FMT_RGB32] = {
            .convert = rgb555_to_rgb32
        },
        [PIX_FMT_YUV420P] = {
            .convert = rgb555_to_yuv420p
        },
        [PIX_FMT_GRAY8] = {
            .convert = rgb555_to_gray
        },
    },
    [PIX_FMT_RGB565] = {
        [PIX_FMT_RGB32] = {
            .convert = rgb565_to_rgb32
        },
        [PIX_FMT_RGB24] = {
            .convert = rgb565_to_rgb24
        },
        [PIX_FMT_YUV420P] = {
            .convert = rgb565_to_yuv420p
        },
        [PIX_FMT_GRAY8] = {
            .convert = rgb565_to_gray
        },
    },
    [PIX_FMT_GRAY16BE] = {
        [PIX_FMT_GRAY8] = {
            .convert = gray16be_to_gray
        },
        [PIX_FMT_GRAY16LE] = {
            .convert = gray16_to_gray16
        },
    },
    [PIX_FMT_GRAY16LE] = {
        [PIX_FMT_GRAY8] = {
            .convert = gray16le_to_gray
        },
        [PIX_FMT_GRAY16BE] = {
            .convert = gray16_to_gray16
        },
    },
    [PIX_FMT_GRAY8] = {
        [PIX_FMT_RGB555] = {
            .convert = gray_to_rgb555
        },
        [PIX_FMT_RGB565] = {
            .convert = gray_to_rgb565
        },
        [PIX_FMT_RGB24] = {
            .convert = gray_to_rgb24
        },
        [PIX_FMT_BGR24] = {
            .convert = gray_to_bgr24
        },
        [PIX_FMT_RGB32] = {
            .convert = gray_to_rgb32
        },
        [PIX_FMT_MONOWHITE] = {
            .convert = gray_to_monowhite
        },
        [PIX_FMT_MONOBLACK] = {
            .convert = gray_to_monoblack
        },
        [PIX_FMT_GRAY16LE] = {
            .convert = gray_to_gray16
        },
        [PIX_FMT_GRAY16BE] = {
            .convert = gray_to_gray16
        },
    },
    [PIX_FMT_MONOWHITE] = {
        [PIX_FMT_GRAY8] = {
            .convert = monowhite_to_gray
        },
    },
    [PIX_FMT_MONOBLACK] = {
        [PIX_FMT_GRAY8] = {
            .convert = monoblack_to_gray
        },
    },
    [PIX_FMT_PAL8] = {
        [PIX_FMT_RGB555] = {
            .convert = pal8_to_rgb555
        },
        [PIX_FMT_RGB565] = {
            .convert = pal8_to_rgb565
        },
        [PIX_FMT_BGR24] = {
            .convert = pal8_to_bgr24
        },
        [PIX_FMT_RGB24] = {
            .convert = pal8_to_rgb24
        },
        [PIX_FMT_RGB32] = {
            .convert = pal8_to_rgb32
        },
    },
    [PIX_FMT_UYYVYY411] = {
        [PIX_FMT_YUV411P] = {
            .convert = uyyvyy411_to_yuv411p,
        },
    },

};

int avpicture_alloc(AVPicture *picture,
                           int pix_fmt, int width, int height)
{
    int size;
    void *ptr;

    size = avpicture_get_size(pix_fmt, width, height);
    if(size<0)
        goto fail;
    ptr = av_malloc(size);
    if (!ptr)
        goto fail;
    avpicture_fill(picture, ptr, pix_fmt, width, height);
    if(picture->data[1] && !picture->data[2])
        ff_set_systematic_pal((uint32_t*)picture->data[1], pix_fmt);

    return 0;
 fail:
    memset(picture, 0, sizeof(AVPicture));
    return -1;
}

void avpicture_free(AVPicture *picture)
{
    av_free(picture->data[0]);
}

/* return true if yuv planar */
static inline int is_yuv_planar(const PixFmtInfo *ps)
{
    return (ps->color_type == FF_COLOR_YUV ||
            ps->color_type == FF_COLOR_YUV_JPEG) &&
        ps->pixel_type == FF_PIXEL_PLANAR;
}

int av_picture_crop(AVPicture *dst, const AVPicture *src,
              int pix_fmt, int top_band, int left_band)
{
    int y_shift;
    int x_shift;

    if (pix_fmt < 0 || pix_fmt >= PIX_FMT_NB || !is_yuv_planar(&pix_fmt_info[pix_fmt]))
        return -1;

    y_shift = pix_fmt_info[pix_fmt].y_chroma_shift;
    x_shift = pix_fmt_info[pix_fmt].x_chroma_shift;

    dst->data[0] = src->data[0] + (top_band * src->linesize[0]) + left_band;
    dst->data[1] = src->data[1] + ((top_band >> y_shift) * src->linesize[1]) + (left_band >> x_shift);
    dst->data[2] = src->data[2] + ((top_band >> y_shift) * src->linesize[2]) + (left_band >> x_shift);

    dst->linesize[0] = src->linesize[0];
    dst->linesize[1] = src->linesize[1];
    dst->linesize[2] = src->linesize[2];
    return 0;
}

int av_picture_pad(AVPicture *dst, const AVPicture *src, int height, int width,
            int pix_fmt, int padtop, int padbottom, int padleft, int padright,
            int *color)
{
    uint8_t *optr;
    int y_shift;
    int x_shift;
    int yheight;
    int i, y;

    if (pix_fmt < 0 || pix_fmt >= PIX_FMT_NB ||
        !is_yuv_planar(&pix_fmt_info[pix_fmt])) return -1;

    for (i = 0; i < 3; i++) {
        x_shift = i ? pix_fmt_info[pix_fmt].x_chroma_shift : 0;
        y_shift = i ? pix_fmt_info[pix_fmt].y_chroma_shift : 0;

        if (padtop || padleft) {
            memset(dst->data[i], color[i],
                dst->linesize[i] * (padtop >> y_shift) + (padleft >> x_shift));
        }

        if (padleft || padright) {
            optr = dst->data[i] + dst->linesize[i] * (padtop >> y_shift) +
                (dst->linesize[i] - (padright >> x_shift));
            yheight = (height - 1 - (padtop + padbottom)) >> y_shift;
            for (y = 0; y < yheight; y++) {
                memset(optr, color[i], (padleft + padright) >> x_shift);
                optr += dst->linesize[i];
            }
        }

        if (src) { /* first line */
            uint8_t *iptr = src->data[i];
            optr = dst->data[i] + dst->linesize[i] * (padtop >> y_shift) +
                    (padleft >> x_shift);
            memcpy(optr, iptr, (width - padleft - padright) >> x_shift);
            iptr += src->linesize[i];
            optr = dst->data[i] + dst->linesize[i] * (padtop >> y_shift) +
                (dst->linesize[i] - (padright >> x_shift));
            yheight = (height - 1 - (padtop + padbottom)) >> y_shift;
            for (y = 0; y < yheight; y++) {
                memset(optr, color[i], (padleft + padright) >> x_shift);
                memcpy(optr + ((padleft + padright) >> x_shift), iptr,
                       (width - padleft - padright) >> x_shift);
                iptr += src->linesize[i];
                optr += dst->linesize[i];
            }
        }

        if (padbottom || padright) {
            optr = dst->data[i] + dst->linesize[i] *
                ((height - padbottom) >> y_shift) - (padright >> x_shift);
            memset(optr, color[i],dst->linesize[i] *
                (padbottom >> y_shift) + (padright >> x_shift));
        }
    }
    return 0;
}

#if !CONFIG_SWSCALE
static uint8_t y_ccir_to_jpeg[256];
static uint8_t y_jpeg_to_ccir[256];
static uint8_t c_ccir_to_jpeg[256];
static uint8_t c_jpeg_to_ccir[256];

/* init various conversion tables */
static void img_convert_init(void)
{
    int i;
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;

    for(i = 0;i < 256; i++) {
        y_ccir_to_jpeg[i] = Y_CCIR_TO_JPEG(i);
        y_jpeg_to_ccir[i] = Y_JPEG_TO_CCIR(i);
        c_ccir_to_jpeg[i] = C_CCIR_TO_JPEG(i);
        c_jpeg_to_ccir[i] = C_JPEG_TO_CCIR(i);
    }
}

/* apply to each pixel the given table */
static void img_apply_table(uint8_t *dst, int dst_wrap,
                            const uint8_t *src, int src_wrap,
                            int width, int height, const uint8_t *table1)
{
    int n;
    const uint8_t *s;
    uint8_t *d;
    const uint8_t *table;

    table = table1;
    for(;height > 0; height--) {
        s = src;
        d = dst;
        n = width;
        while (n >= 4) {
            d[0] = table[s[0]];
            d[1] = table[s[1]];
            d[2] = table[s[2]];
            d[3] = table[s[3]];
            d += 4;
            s += 4;
            n -= 4;
        }
        while (n > 0) {
            d[0] = table[s[0]];
            d++;
            s++;
            n--;
        }
        dst += dst_wrap;
        src += src_wrap;
    }
}

/* XXX: use generic filter ? */
/* XXX: in most cases, the sampling position is incorrect */

/* 4x1 -> 1x1 */
static void shrink41(uint8_t *dst, int dst_wrap,
                     const uint8_t *src, int src_wrap,
                     int width, int height)
{
    int w;
    const uint8_t *s;
    uint8_t *d;

    for(;height > 0; height--) {
        s = src;
        d = dst;
        for(w = width;w > 0; w--) {
            d[0] = (s[0] + s[1] + s[2] + s[3] + 2) >> 2;
            s += 4;
            d++;
        }
        src += src_wrap;
        dst += dst_wrap;
    }
}

/* 2x1 -> 1x1 */
static void shrink21(uint8_t *dst, int dst_wrap,
                     const uint8_t *src, int src_wrap,
                     int width, int height)
{
    int w;
    const uint8_t *s;
    uint8_t *d;

    for(;height > 0; height--) {
        s = src;
        d = dst;
        for(w = width;w > 0; w--) {
            d[0] = (s[0] + s[1]) >> 1;
            s += 2;
            d++;
        }
        src += src_wrap;
        dst += dst_wrap;
    }
}

/* 1x2 -> 1x1 */
static void shrink12(uint8_t *dst, int dst_wrap,
                     const uint8_t *src, int src_wrap,
                     int width, int height)
{
    int w;
    uint8_t *d;
    const uint8_t *s1, *s2;

    for(;height > 0; height--) {
        s1 = src;
        s2 = s1 + src_wrap;
        d = dst;
        for(w = width;w >= 4; w-=4) {
            d[0] = (s1[0] + s2[0]) >> 1;
            d[1] = (s1[1] + s2[1]) >> 1;
            d[2] = (s1[2] + s2[2]) >> 1;
            d[3] = (s1[3] + s2[3]) >> 1;
            s1 += 4;
            s2 += 4;
            d += 4;
        }
        for(;w > 0; w--) {
            d[0] = (s1[0] + s2[0]) >> 1;
            s1++;
            s2++;
            d++;
        }
        src += 2 * src_wrap;
        dst += dst_wrap;
    }
}

static void grow21_line(uint8_t *dst, const uint8_t *src,
                        int width)
{
    int w;
    const uint8_t *s1;
    uint8_t *d;

    s1 = src;
    d = dst;
    for(w = width;w >= 4; w-=4) {
        d[1] = d[0] = s1[0];
        d[3] = d[2] = s1[1];
        s1 += 2;
        d += 4;
    }
    for(;w >= 2; w -= 2) {
        d[1] = d[0] = s1[0];
        s1 ++;
        d += 2;
    }
    /* only needed if width is not a multiple of two */
    /* XXX: veryfy that */
    if (w) {
        d[0] = s1[0];
    }
}

static void grow41_line(uint8_t *dst, const uint8_t *src,
                        int width)
{
    int w, v;
    const uint8_t *s1;
    uint8_t *d;

    s1 = src;
    d = dst;
    for(w = width;w >= 4; w-=4) {
        v = s1[0];
        d[0] = v;
        d[1] = v;
        d[2] = v;
        d[3] = v;
        s1 ++;
        d += 4;
    }
}

/* 1x1 -> 2x1 */
static void grow21(uint8_t *dst, int dst_wrap,
                   const uint8_t *src, int src_wrap,
                   int width, int height)
{
    for(;height > 0; height--) {
        grow21_line(dst, src, width);
        src += src_wrap;
        dst += dst_wrap;
    }
}

/* 1x1 -> 1x2 */
static void grow12(uint8_t *dst, int dst_wrap,
                   const uint8_t *src, int src_wrap,
                   int width, int height)
{
    for(;height > 0; height-=2) {
        memcpy(dst, src, width);
        dst += dst_wrap;
        memcpy(dst, src, width);
        dst += dst_wrap;
        src += src_wrap;
    }
}

/* 1x1 -> 2x2 */
static void grow22(uint8_t *dst, int dst_wrap,
                   const uint8_t *src, int src_wrap,
                   int width, int height)
{
    for(;height > 0; height--) {
        grow21_line(dst, src, width);
        if (height%2)
            src += src_wrap;
        dst += dst_wrap;
    }
}

/* 1x1 -> 4x1 */
static void grow41(uint8_t *dst, int dst_wrap,
                   const uint8_t *src, int src_wrap,
                   int width, int height)
{
    for(;height > 0; height--) {
        grow41_line(dst, src, width);
        src += src_wrap;
        dst += dst_wrap;
    }
}

/* 1x1 -> 4x4 */
static void grow44(uint8_t *dst, int dst_wrap,
                   const uint8_t *src, int src_wrap,
                   int width, int height)
{
    for(;height > 0; height--) {
        grow41_line(dst, src, width);
        if ((height & 3) == 1)
            src += src_wrap;
        dst += dst_wrap;
    }
}

/* 1x2 -> 2x1 */
static void conv411(uint8_t *dst, int dst_wrap,
                    const uint8_t *src, int src_wrap,
                    int width, int height)
{
    int w, c;
    const uint8_t *s1, *s2;
    uint8_t *d;

    width>>=1;

    for(;height > 0; height--) {
        s1 = src;
        s2 = src + src_wrap;
        d = dst;
        for(w = width;w > 0; w--) {
            c = (s1[0] + s2[0]) >> 1;
            d[0] = c;
            d[1] = c;
            s1++;
            s2++;
            d += 2;
        }
        src += src_wrap * 2;
        dst += dst_wrap;
    }
}

/* XXX: always use linesize. Return -1 if not supported */
int img_convert(AVPicture *dst, int dst_pix_fmt,
                const AVPicture *src, int src_pix_fmt,
                int src_width, int src_height)
{
    static int initialized;
    int i, ret, dst_width, dst_height, int_pix_fmt;
    const PixFmtInfo *src_pix, *dst_pix;
    const ConvertEntry *ce;
    AVPicture tmp1, *tmp = &tmp1;

    if (src_pix_fmt < 0 || src_pix_fmt >= PIX_FMT_NB ||
        dst_pix_fmt < 0 || dst_pix_fmt >= PIX_FMT_NB)
        return -1;
    if (src_width <= 0 || src_height <= 0)
        return 0;

    if (!initialized) {
        initialized = 1;
        img_convert_init();
    }

    dst_width = src_width;
    dst_height = src_height;

    dst_pix = &pix_fmt_info[dst_pix_fmt];
    src_pix = &pix_fmt_info[src_pix_fmt];
    if (src_pix_fmt == dst_pix_fmt) {
        /* no conversion needed: just copy */
        av_picture_copy(dst, src, dst_pix_fmt, dst_width, dst_height);
        return 0;
    }

    ce = &convert_table[src_pix_fmt][dst_pix_fmt];
    if (ce->convert) {
        /* specific conversion routine */
        ce->convert(dst, src, dst_width, dst_height);
        return 0;
    }

    /* gray to YUV */
    if (is_yuv_planar(dst_pix) &&
        src_pix_fmt == PIX_FMT_GRAY8) {
        int w, h, y;
        uint8_t *d;

        if (dst_pix->color_type == FF_COLOR_YUV_JPEG) {
            ff_img_copy_plane(dst->data[0], dst->linesize[0],
                     src->data[0], src->linesize[0],
                     dst_width, dst_height);
        } else {
            img_apply_table(dst->data[0], dst->linesize[0],
                            src->data[0], src->linesize[0],
                            dst_width, dst_height,
                            y_jpeg_to_ccir);
        }
        /* fill U and V with 128 */
        w = dst_width;
        h = dst_height;
        w >>= dst_pix->x_chroma_shift;
        h >>= dst_pix->y_chroma_shift;
        for(i = 1; i <= 2; i++) {
            d = dst->data[i];
            for(y = 0; y< h; y++) {
                memset(d, 128, w);
                d += dst->linesize[i];
            }
        }
        return 0;
    }

    /* YUV to gray */
    if (is_yuv_planar(src_pix) &&
        dst_pix_fmt == PIX_FMT_GRAY8) {
        if (src_pix->color_type == FF_COLOR_YUV_JPEG) {
            ff_img_copy_plane(dst->data[0], dst->linesize[0],
                     src->data[0], src->linesize[0],
                     dst_width, dst_height);
        } else {
            img_apply_table(dst->data[0], dst->linesize[0],
                            src->data[0], src->linesize[0],
                            dst_width, dst_height,
                            y_ccir_to_jpeg);
        }
        return 0;
    }

    /* YUV to YUV planar */
    if (is_yuv_planar(dst_pix) && is_yuv_planar(src_pix)) {
        int x_shift, y_shift, w, h, xy_shift;
        void (*resize_func)(uint8_t *dst, int dst_wrap,
                            const uint8_t *src, int src_wrap,
                            int width, int height);

        /* compute chroma size of the smallest dimensions */
        w = dst_width;
        h = dst_height;
        if (dst_pix->x_chroma_shift >= src_pix->x_chroma_shift)
            w >>= dst_pix->x_chroma_shift;
        else
            w >>= src_pix->x_chroma_shift;
        if (dst_pix->y_chroma_shift >= src_pix->y_chroma_shift)
            h >>= dst_pix->y_chroma_shift;
        else
            h >>= src_pix->y_chroma_shift;

        x_shift = (dst_pix->x_chroma_shift - src_pix->x_chroma_shift);
        y_shift = (dst_pix->y_chroma_shift - src_pix->y_chroma_shift);
        xy_shift = ((x_shift & 0xf) << 4) | (y_shift & 0xf);
        /* there must be filters for conversion at least from and to
           YUV444 format */
        switch(xy_shift) {
        case 0x00:
            resize_func = ff_img_copy_plane;
            break;
        case 0x10:
            resize_func = shrink21;
            break;
        case 0x20:
            resize_func = shrink41;
            break;
        case 0x01:
            resize_func = shrink12;
            break;
        case 0x11:
            resize_func = ff_shrink22;
            break;
        case 0x22:
            resize_func = ff_shrink44;
            break;
        case 0xf0:
            resize_func = grow21;
            break;
        case 0x0f:
            resize_func = grow12;
            break;
        case 0xe0:
            resize_func = grow41;
            break;
        case 0xff:
            resize_func = grow22;
            break;
        case 0xee:
            resize_func = grow44;
            break;
        case 0xf1:
            resize_func = conv411;
            break;
        default:
            /* currently not handled */
            goto no_chroma_filter;
        }

        ff_img_copy_plane(dst->data[0], dst->linesize[0],
                       src->data[0], src->linesize[0],
                       dst_width, dst_height);

        for(i = 1;i <= 2; i++)
            resize_func(dst->data[i], dst->linesize[i],
                        src->data[i], src->linesize[i],
                        dst_width>>dst_pix->x_chroma_shift, dst_height>>dst_pix->y_chroma_shift);
        /* if yuv color space conversion is needed, we do it here on
           the destination image */
        if (dst_pix->color_type != src_pix->color_type) {
            const uint8_t *y_table, *c_table;
            if (dst_pix->color_type == FF_COLOR_YUV) {
                y_table = y_jpeg_to_ccir;
                c_table = c_jpeg_to_ccir;
            } else {
                y_table = y_ccir_to_jpeg;
                c_table = c_ccir_to_jpeg;
            }
            img_apply_table(dst->data[0], dst->linesize[0],
                            dst->data[0], dst->linesize[0],
                            dst_width, dst_height,
                            y_table);

            for(i = 1;i <= 2; i++)
                img_apply_table(dst->data[i], dst->linesize[i],
                                dst->data[i], dst->linesize[i],
                                dst_width>>dst_pix->x_chroma_shift,
                                dst_height>>dst_pix->y_chroma_shift,
                                c_table);
        }
        return 0;
    }
 no_chroma_filter:

    /* try to use an intermediate format */
    if (src_pix_fmt == PIX_FMT_YUYV422 ||
        dst_pix_fmt == PIX_FMT_YUYV422) {
        /* specific case: convert to YUV422P first */
        int_pix_fmt = PIX_FMT_YUV422P;
    } else if (src_pix_fmt == PIX_FMT_UYVY422 ||
        dst_pix_fmt == PIX_FMT_UYVY422) {
        /* specific case: convert to YUV422P first */
        int_pix_fmt = PIX_FMT_YUV422P;
    } else if (src_pix_fmt == PIX_FMT_UYYVYY411 ||
        dst_pix_fmt == PIX_FMT_UYYVYY411) {
        /* specific case: convert to YUV411P first */
        int_pix_fmt = PIX_FMT_YUV411P;
    } else if ((src_pix->color_type == FF_COLOR_GRAY &&
                src_pix_fmt != PIX_FMT_GRAY8) ||
               (dst_pix->color_type == FF_COLOR_GRAY &&
                dst_pix_fmt != PIX_FMT_GRAY8)) {
        /* gray8 is the normalized format */
        int_pix_fmt = PIX_FMT_GRAY8;
    } else if ((is_yuv_planar(src_pix) &&
                src_pix_fmt != PIX_FMT_YUV444P &&
                src_pix_fmt != PIX_FMT_YUVJ444P)) {
        /* yuv444 is the normalized format */
        if (src_pix->color_type == FF_COLOR_YUV_JPEG)
            int_pix_fmt = PIX_FMT_YUVJ444P;
        else
            int_pix_fmt = PIX_FMT_YUV444P;
    } else if ((is_yuv_planar(dst_pix) &&
                dst_pix_fmt != PIX_FMT_YUV444P &&
                dst_pix_fmt != PIX_FMT_YUVJ444P)) {
        /* yuv444 is the normalized format */
        if (dst_pix->color_type == FF_COLOR_YUV_JPEG)
            int_pix_fmt = PIX_FMT_YUVJ444P;
        else
            int_pix_fmt = PIX_FMT_YUV444P;
    } else {
        /* the two formats are rgb or gray8 or yuv[j]444p */
        if (src_pix->is_alpha && dst_pix->is_alpha)
            int_pix_fmt = PIX_FMT_RGB32;
        else
            int_pix_fmt = PIX_FMT_RGB24;
    }
    if (src_pix_fmt == int_pix_fmt)
        return -1;
    if (avpicture_alloc(tmp, int_pix_fmt, dst_width, dst_height) < 0)
        return -1;
    ret = -1;
    if (img_convert(tmp, int_pix_fmt,
                    src, src_pix_fmt, src_width, src_height) < 0)
        goto fail1;
    if (img_convert(dst, dst_pix_fmt,
                    tmp, int_pix_fmt, dst_width, dst_height) < 0)
        goto fail1;
    ret = 0;
 fail1:
    avpicture_free(tmp);
    return ret;
}
#endif

/* NOTE: we scan all the pixels to have an exact information */
static int get_alpha_info_pal8(const AVPicture *src, int width, int height)
{
    const unsigned char *p;
    int src_wrap, ret, x, y;
    unsigned int a;
    uint32_t *palette = (uint32_t *)src->data[1];

    p = src->data[0];
    src_wrap = src->linesize[0] - width;
    ret = 0;
    for(y=0;y<height;y++) {
        for(x=0;x<width;x++) {
            a = palette[p[0]] >> 24;
            if (a == 0x00) {
                ret |= FF_ALPHA_TRANSP;
            } else if (a != 0xff) {
                ret |= FF_ALPHA_SEMI_TRANSP;
            }
            p++;
        }
        p += src_wrap;
    }
    return ret;
}

int img_get_alpha_info(const AVPicture *src,
                       int pix_fmt, int width, int height)
{
    const PixFmtInfo *pf = &pix_fmt_info[pix_fmt];
    int ret;

    pf = &pix_fmt_info[pix_fmt];
    /* no alpha can be represented in format */
    if (!pf->is_alpha)
        return 0;
    switch(pix_fmt) {
    case PIX_FMT_RGB32:
        ret = get_alpha_info_rgb32(src, width, height);
        break;
    case PIX_FMT_PAL8:
        ret = get_alpha_info_pal8(src, width, height);
        break;
    default:
        /* we do not know, so everything is indicated */
        ret = FF_ALPHA_TRANSP | FF_ALPHA_SEMI_TRANSP;
        break;
    }
    return ret;
}

#if HAVE_MMX
#define DEINT_INPLACE_LINE_LUM \
                    movd_m2r(lum_m4[0],mm0);\
                    movd_m2r(lum_m3[0],mm1);\
                    movd_m2r(lum_m2[0],mm2);\
                    movd_m2r(lum_m1[0],mm3);\
                    movd_m2r(lum[0],mm4);\
                    punpcklbw_r2r(mm7,mm0);\
                    movd_r2m(mm2,lum_m4[0]);\
                    punpcklbw_r2r(mm7,mm1);\
                    punpcklbw_r2r(mm7,mm2);\
                    punpcklbw_r2r(mm7,mm3);\
                    punpcklbw_r2r(mm7,mm4);\
                    paddw_r2r(mm3,mm1);\
                    psllw_i2r(1,mm2);\
                    paddw_r2r(mm4,mm0);\
                    psllw_i2r(2,mm1);\
                    paddw_r2r(mm6,mm2);\
                    paddw_r2r(mm2,mm1);\
                    psubusw_r2r(mm0,mm1);\
                    psrlw_i2r(3,mm1);\
                    packuswb_r2r(mm7,mm1);\
                    movd_r2m(mm1,lum_m2[0]);

#define DEINT_LINE_LUM \
                    movd_m2r(lum_m4[0],mm0);\
                    movd_m2r(lum_m3[0],mm1);\
                    movd_m2r(lum_m2[0],mm2);\
                    movd_m2r(lum_m1[0],mm3);\
                    movd_m2r(lum[0],mm4);\
                    punpcklbw_r2r(mm7,mm0);\
                    punpcklbw_r2r(mm7,mm1);\
                    punpcklbw_r2r(mm7,mm2);\
                    punpcklbw_r2r(mm7,mm3);\
                    punpcklbw_r2r(mm7,mm4);\
                    paddw_r2r(mm3,mm1);\
                    psllw_i2r(1,mm2);\
                    paddw_r2r(mm4,mm0);\
                    psllw_i2r(2,mm1);\
                    paddw_r2r(mm6,mm2);\
                    paddw_r2r(mm2,mm1);\
                    psubusw_r2r(mm0,mm1);\
                    psrlw_i2r(3,mm1);\
                    packuswb_r2r(mm7,mm1);\
                    movd_r2m(mm1,dst[0]);
#endif

/* filter parameters: [-1 4 2 4 -1] // 8 */
static void deinterlace_line(uint8_t *dst,
                             const uint8_t *lum_m4, const uint8_t *lum_m3,
                             const uint8_t *lum_m2, const uint8_t *lum_m1,
                             const uint8_t *lum,
                             int size)
{
#if !HAVE_MMX
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;
    int sum;

    for(;size > 0;size--) {
        sum = -lum_m4[0];
        sum += lum_m3[0] << 2;
        sum += lum_m2[0] << 1;
        sum += lum_m1[0] << 2;
        sum += -lum[0];
        dst[0] = cm[(sum + 4) >> 3];
        lum_m4++;
        lum_m3++;
        lum_m2++;
        lum_m1++;
        lum++;
        dst++;
    }
#else

    {
        pxor_r2r(mm7,mm7);
        movq_m2r(ff_pw_4,mm6);
    }
    for (;size > 3; size-=4) {
        DEINT_LINE_LUM
        lum_m4+=4;
        lum_m3+=4;
        lum_m2+=4;
        lum_m1+=4;
        lum+=4;
        dst+=4;
    }
#endif
}
static void deinterlace_line_inplace(uint8_t *lum_m4, uint8_t *lum_m3, uint8_t *lum_m2, uint8_t *lum_m1, uint8_t *lum,
                             int size)
{
#if !HAVE_MMX
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;
    int sum;

    for(;size > 0;size--) {
        sum = -lum_m4[0];
        sum += lum_m3[0] << 2;
        sum += lum_m2[0] << 1;
        lum_m4[0]=lum_m2[0];
        sum += lum_m1[0] << 2;
        sum += -lum[0];
        lum_m2[0] = cm[(sum + 4) >> 3];
        lum_m4++;
        lum_m3++;
        lum_m2++;
        lum_m1++;
        lum++;
    }
#else

    {
        pxor_r2r(mm7,mm7);
        movq_m2r(ff_pw_4,mm6);
    }
    for (;size > 3; size-=4) {
        DEINT_INPLACE_LINE_LUM
        lum_m4+=4;
        lum_m3+=4;
        lum_m2+=4;
        lum_m1+=4;
        lum+=4;
    }
#endif
}

/* deinterlacing : 2 temporal taps, 3 spatial taps linear filter. The
   top field is copied as is, but the bottom field is deinterlaced
   against the top field. */
static void deinterlace_bottom_field(uint8_t *dst, int dst_wrap,
                                    const uint8_t *src1, int src_wrap,
                                    int width, int height)
{
    const uint8_t *src_m2, *src_m1, *src_0, *src_p1, *src_p2;
    int y;

    src_m2 = src1;
    src_m1 = src1;
    src_0=&src_m1[src_wrap];
    src_p1=&src_0[src_wrap];
    src_p2=&src_p1[src_wrap];
    for(y=0;y<(height-2);y+=2) {
        memcpy(dst,src_m1,width);
        dst += dst_wrap;
        deinterlace_line(dst,src_m2,src_m1,src_0,src_p1,src_p2,width);
        src_m2 = src_0;
        src_m1 = src_p1;
        src_0 = src_p2;
        src_p1 += 2*src_wrap;
        src_p2 += 2*src_wrap;
        dst += dst_wrap;
    }
    memcpy(dst,src_m1,width);
    dst += dst_wrap;
    /* do last line */
    deinterlace_line(dst,src_m2,src_m1,src_0,src_0,src_0,width);
}

static void deinterlace_bottom_field_inplace(uint8_t *src1, int src_wrap,
                                             int width, int height)
{
    uint8_t *src_m1, *src_0, *src_p1, *src_p2;
    int y;
    uint8_t *buf;
    buf = (uint8_t*)av_malloc(width);

    src_m1 = src1;
    memcpy(buf,src_m1,width);
    src_0=&src_m1[src_wrap];
    src_p1=&src_0[src_wrap];
    src_p2=&src_p1[src_wrap];
    for(y=0;y<(height-2);y+=2) {
        deinterlace_line_inplace(buf,src_m1,src_0,src_p1,src_p2,width);
        src_m1 = src_p1;
        src_0 = src_p2;
        src_p1 += 2*src_wrap;
        src_p2 += 2*src_wrap;
    }
    /* do last line */
    deinterlace_line_inplace(buf,src_m1,src_0,src_0,src_0,width);
    av_free(buf);
}

int avpicture_deinterlace(AVPicture *dst, const AVPicture *src,
                          int pix_fmt, int width, int height)
{
    int i;

    if (pix_fmt != PIX_FMT_YUV420P &&
        pix_fmt != PIX_FMT_YUV422P &&
        pix_fmt != PIX_FMT_YUV444P &&
        pix_fmt != PIX_FMT_YUV411P &&
        pix_fmt != PIX_FMT_GRAY8)
        return -1;
    if ((width & 3) != 0 || (height & 3) != 0)
        return -1;

    for(i=0;i<3;i++) {
        if (i == 1) {
            switch(pix_fmt) {
            case PIX_FMT_YUV420P:
                width >>= 1;
                height >>= 1;
                break;
            case PIX_FMT_YUV422P:
                width >>= 1;
                break;
            case PIX_FMT_YUV411P:
                width >>= 2;
                break;
            default:
                break;
            }
            if (pix_fmt == PIX_FMT_GRAY8) {
                break;
            }
        }
        if (src == dst) {
            deinterlace_bottom_field_inplace(dst->data[i], dst->linesize[i],
                                 width, height);
        } else {
            deinterlace_bottom_field(dst->data[i],dst->linesize[i],
                                        src->data[i], src->linesize[i],
                                        width, height);
        }
    }
    emms_c();
    return 0;
}

