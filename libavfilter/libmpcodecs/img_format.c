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

#include "config.h"
#include "img_format.h"
#include "stdio.h"
#include "libavutil/bswap.h"

const char *ff_vo_format_name(int format)
{
    static char unknown_format[20];
    switch(format)
    {
    case IMGFMT_RGB1:    return "RGB 1-bit";
    case IMGFMT_RGB4:    return "RGB 4-bit";
    case IMGFMT_RG4B:    return "RGB 4-bit per byte";
    case IMGFMT_RGB8:    return "RGB 8-bit";
    case IMGFMT_RGB12:   return "RGB 12-bit";
    case IMGFMT_RGB15:   return "RGB 15-bit";
    case IMGFMT_RGB16:   return "RGB 16-bit";
    case IMGFMT_RGB24:   return "RGB 24-bit";
//  case IMGFMT_RGB32:   return "RGB 32-bit";
    case IMGFMT_RGB48LE: return "RGB 48-bit LE";
    case IMGFMT_RGB48BE: return "RGB 48-bit BE";
    case IMGFMT_RGB64LE: return "RGB 64-bit LE";
    case IMGFMT_RGB64BE: return "RGB 64-bit BE";
    case IMGFMT_BGR1:    return "BGR 1-bit";
    case IMGFMT_BGR4:    return "BGR 4-bit";
    case IMGFMT_BG4B:    return "BGR 4-bit per byte";
    case IMGFMT_BGR8:    return "BGR 8-bit";
    case IMGFMT_BGR12:   return "BGR 12-bit";
    case IMGFMT_BGR15:   return "BGR 15-bit";
    case IMGFMT_BGR16:   return "BGR 16-bit";
    case IMGFMT_BGR24:   return "BGR 24-bit";
//  case IMGFMT_BGR32:   return "BGR 32-bit";
    case IMGFMT_ABGR:    return "ABGR";
    case IMGFMT_BGRA:    return "BGRA";
    case IMGFMT_ARGB:    return "ARGB";
    case IMGFMT_RGBA:    return "RGBA";
    case IMGFMT_GBR24P:  return "Planar GBR 24-bit";
    case IMGFMT_GBR12P:  return "Planar GBR 36-bit";
    case IMGFMT_GBR14P:  return "Planar GBR 42-bit";
    case IMGFMT_YVU9:    return "Planar YVU9";
    case IMGFMT_IF09:    return "Planar IF09";
    case IMGFMT_YV12:    return "Planar YV12";
    case IMGFMT_I420:    return "Planar I420";
    case IMGFMT_IYUV:    return "Planar IYUV";
    case IMGFMT_CLPL:    return "Planar CLPL";
    case IMGFMT_Y800:    return "Planar Y800";
    case IMGFMT_Y8:      return "Planar Y8";
    case IMGFMT_Y8A:     return "Planar Y8 with alpha";
    case IMGFMT_Y16_LE:  return "Planar Y16 little-endian";
    case IMGFMT_Y16_BE:  return "Planar Y16 big-endian";
    case IMGFMT_420P16_LE: return "Planar 420P 16-bit little-endian";
    case IMGFMT_420P16_BE: return "Planar 420P 16-bit big-endian";
    case IMGFMT_420P14_LE: return "Planar 420P 14-bit little-endian";
    case IMGFMT_420P14_BE: return "Planar 420P 14-bit big-endian";
    case IMGFMT_420P12_LE: return "Planar 420P 12-bit little-endian";
    case IMGFMT_420P12_BE: return "Planar 420P 12-bit big-endian";
    case IMGFMT_420P10_LE: return "Planar 420P 10-bit little-endian";
    case IMGFMT_420P10_BE: return "Planar 420P 10-bit big-endian";
    case IMGFMT_420P9_LE:  return "Planar 420P 9-bit little-endian";
    case IMGFMT_420P9_BE:  return "Planar 420P 9-bit big-endian";
    case IMGFMT_422P16_LE: return "Planar 422P 16-bit little-endian";
    case IMGFMT_422P16_BE: return "Planar 422P 16-bit big-endian";
    case IMGFMT_422P14_LE: return "Planar 422P 14-bit little-endian";
    case IMGFMT_422P14_BE: return "Planar 422P 14-bit big-endian";
    case IMGFMT_422P12_LE: return "Planar 422P 12-bit little-endian";
    case IMGFMT_422P12_BE: return "Planar 422P 12-bit big-endian";
    case IMGFMT_422P10_LE: return "Planar 422P 10-bit little-endian";
    case IMGFMT_422P10_BE: return "Planar 422P 10-bit big-endian";
    case IMGFMT_422P9_LE:  return "Planar 422P 9-bit little-endian";
    case IMGFMT_422P9_BE:  return "Planar 422P 9-bit big-endian";
    case IMGFMT_444P16_LE: return "Planar 444P 16-bit little-endian";
    case IMGFMT_444P16_BE: return "Planar 444P 16-bit big-endian";
    case IMGFMT_444P14_LE: return "Planar 444P 14-bit little-endian";
    case IMGFMT_444P14_BE: return "Planar 444P 14-bit big-endian";
    case IMGFMT_444P12_LE: return "Planar 444P 12-bit little-endian";
    case IMGFMT_444P12_BE: return "Planar 444P 12-bit big-endian";
    case IMGFMT_444P10_LE: return "Planar 444P 10-bit little-endian";
    case IMGFMT_444P10_BE: return "Planar 444P 10-bit big-endian";
    case IMGFMT_444P9_LE:  return "Planar 444P 9-bit little-endian";
    case IMGFMT_444P9_BE:  return "Planar 444P 9-bit big-endian";
    case IMGFMT_420A: return "Planar 420P with alpha";
    case IMGFMT_444P: return "Planar 444P";
    case IMGFMT_444A: return "Planar 444P with alpha";
    case IMGFMT_422P: return "Planar 422P";
    case IMGFMT_422A: return "Planar 422P with alpha";
    case IMGFMT_411P: return "Planar 411P";
    case IMGFMT_NV12: return "Planar NV12";
    case IMGFMT_NV21: return "Planar NV21";
    case IMGFMT_HM12: return "Planar NV12 Macroblock";
    case IMGFMT_IUYV: return "Packed IUYV";
    case IMGFMT_IY41: return "Packed IY41";
    case IMGFMT_IYU1: return "Packed IYU1";
    case IMGFMT_IYU2: return "Packed IYU2";
    case IMGFMT_UYVY: return "Packed UYVY";
    case IMGFMT_UYNV: return "Packed UYNV";
    case IMGFMT_cyuv: return "Packed CYUV";
    case IMGFMT_Y422: return "Packed Y422";
    case IMGFMT_YUY2: return "Packed YUY2";
    case IMGFMT_YUNV: return "Packed YUNV";
    case IMGFMT_YVYU: return "Packed YVYU";
    case IMGFMT_Y41P: return "Packed Y41P";
    case IMGFMT_Y211: return "Packed Y211";
    case IMGFMT_Y41T: return "Packed Y41T";
    case IMGFMT_Y42T: return "Packed Y42T";
    case IMGFMT_V422: return "Packed V422";
    case IMGFMT_V655: return "Packed V655";
    case IMGFMT_CLJR: return "Packed CLJR";
    case IMGFMT_YUVP: return "Packed YUVP";
    case IMGFMT_UYVP: return "Packed UYVP";
    case IMGFMT_MPEGPES:         return "Mpeg PES";
    case IMGFMT_ZRMJPEGNI:       return "Zoran MJPEG non-interlaced";
    case IMGFMT_ZRMJPEGIT:       return "Zoran MJPEG top field first";
    case IMGFMT_ZRMJPEGIB:       return "Zoran MJPEG bottom field first";
    case IMGFMT_XVMC_MOCO_MPEG2: return "MPEG1/2 Motion Compensation";
    case IMGFMT_XVMC_IDCT_MPEG2: return "MPEG1/2 Motion Compensation and IDCT";
    case IMGFMT_VDPAU_MPEG1:     return "MPEG1 VDPAU acceleration";
    case IMGFMT_VDPAU_MPEG2:     return "MPEG2 VDPAU acceleration";
    case IMGFMT_VDPAU_H264:      return "H.264 VDPAU acceleration";
    case IMGFMT_VDPAU_MPEG4:     return "MPEG-4 Part 2 VDPAU acceleration";
    case IMGFMT_VDPAU_WMV3:      return "WMV3 VDPAU acceleration";
    case IMGFMT_VDPAU_VC1:       return "VC1 VDPAU acceleration";
    }
    snprintf(unknown_format,20,"Unknown 0x%04x",format);
    return unknown_format;
}

int ff_mp_get_chroma_shift(int format, int *x_shift, int *y_shift, int *component_bits)
{
    int xs = 0, ys = 0;
    int bpp;
    int err = 0;
    int bits = 8;
    if ((format & 0xff0000f0) == 0x34000050)
        format = av_bswap32(format);
    if ((format & 0xf00000ff) == 0x50000034) {
        switch (format >> 24) {
        case 0x50:
            break;
        case 0x51:
            bits = 16;
            break;
        case 0x52:
            bits = 10;
            break;
        case 0x53:
            bits = 9;
            break;
        default:
            err = 1;
            break;
        }
        switch (format & 0x00ffffff) {
        case 0x00343434: // 444
            xs = 0;
            ys = 0;
            break;
        case 0x00323234: // 422
            xs = 1;
            ys = 0;
            break;
        case 0x00303234: // 420
            xs = 1;
            ys = 1;
            break;
        case 0x00313134: // 411
            xs = 2;
            ys = 0;
            break;
        case 0x00303434: // 440
            xs = 0;
            ys = 1;
            break;
        default:
            err = 1;
            break;
        }
    } else switch (format) {
    case IMGFMT_444A:
        xs = 0;
        ys = 0;
        break;
    case IMGFMT_422A:
        xs = 1;
        ys = 0;
        break;
    case IMGFMT_420A:
    case IMGFMT_I420:
    case IMGFMT_IYUV:
    case IMGFMT_YV12:
        xs = 1;
        ys = 1;
        break;
    case IMGFMT_IF09:
    case IMGFMT_YVU9:
        xs = 2;
        ys = 2;
        break;
    case IMGFMT_Y8:
    case IMGFMT_Y800:
        xs = 31;
        ys = 31;
        break;
    default:
        err = 1;
        break;
    }
    if (x_shift) *x_shift = xs;
    if (y_shift) *y_shift = ys;
    if (component_bits) *component_bits = bits;
    bpp = 8 + ((16 >> xs) >> ys);
    if (format == IMGFMT_420A || format == IMGFMT_422A || format == IMGFMT_444A)
        bpp += 8;
    bpp *= (bits + 7) >> 3;
    return err ? 0 : bpp;
}
