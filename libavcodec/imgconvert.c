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
    uint8_t is_hwaccel : 1;  /**< true if this is an HW accelerated format */
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
    [PIX_FMT_RGB48BE] = {
        .name = "rgb48be",
        .nb_channels = 3,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 16,
        .x_chroma_shift = 0, .y_chroma_shift = 0,
    },
    [PIX_FMT_RGB48LE] = {
        .name = "rgb48le",
        .nb_channels = 3,
        .color_type = FF_COLOR_RGB,
        .pixel_type = FF_PIXEL_PACKED,
        .depth = 16,
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
        .is_hwaccel = 1,
    },
    [PIX_FMT_XVMC_MPEG2_IDCT] = {
        .name = "xvmcidct",
        .is_hwaccel = 1,
    },
    [PIX_FMT_VDPAU_MPEG1] = {
        .name = "vdpau_mpeg1",
        .is_hwaccel = 1,
        .x_chroma_shift = 1, .y_chroma_shift = 1,
    },
    [PIX_FMT_VDPAU_MPEG2] = {
        .name = "vdpau_mpeg2",
        .is_hwaccel = 1,
        .x_chroma_shift = 1, .y_chroma_shift = 1,
    },
    [PIX_FMT_VDPAU_H264] = {
        .name = "vdpau_h264",
        .is_hwaccel = 1,
        .x_chroma_shift = 1, .y_chroma_shift = 1,
    },
    [PIX_FMT_VDPAU_WMV3] = {
        .name = "vdpau_wmv3",
        .is_hwaccel = 1,
        .x_chroma_shift = 1, .y_chroma_shift = 1,
    },
    [PIX_FMT_VDPAU_VC1] = {
        .name = "vdpau_vc1",
        .is_hwaccel = 1,
        .x_chroma_shift = 1, .y_chroma_shift = 1,
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

    /* VA API formats */
    [PIX_FMT_VAAPI_MOCO] = {
        .name = "vaapi_moco",
        .is_hwaccel = 1,
        .x_chroma_shift = 1, .y_chroma_shift = 1,
    },
    [PIX_FMT_VAAPI_IDCT] = {
        .name = "vaapi_idct",
        .is_hwaccel = 1,
        .x_chroma_shift = 1, .y_chroma_shift = 1,
    },
    [PIX_FMT_VAAPI_VLD] = {
        .name = "vaapi_vld",
        .is_hwaccel = 1,
        .x_chroma_shift = 1, .y_chroma_shift = 1,
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

int ff_is_hwaccel_pix_fmt(enum PixelFormat pix_fmt)
{
    return pix_fmt_info[pix_fmt].is_hwaccel;
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
        default:
            return -1;
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
    case PIX_FMT_RGB48BE:
    case PIX_FMT_RGB48LE:
        picture->linesize[0] = width * 6;
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
    case PIX_FMT_RGB48BE:
    case PIX_FMT_RGB48LE:
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
    int i, j, w, ow, h, oh, data_planes;
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

    ow = w;
    oh = h;

    for (i=0; i<data_planes; i++) {
         if (i == 1) {
             w = width >> pf->x_chroma_shift;
             h = height >> pf->y_chroma_shift;
         } else if (i == 3) {
             w = ow;
             h = oh;
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

