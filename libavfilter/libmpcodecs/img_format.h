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

#ifndef MPLAYER_IMG_FORMAT_H
#define MPLAYER_IMG_FORMAT_H

#include "config.h"

/* RGB/BGR Formats */

#define IMGFMT_RGB_MASK 0xFFFFFF00
#define IMGFMT_RGB (('R'<<24)|('G'<<16)|('B'<<8))
#define IMGFMT_RGB1  (IMGFMT_RGB|1)
#define IMGFMT_RGB4  (IMGFMT_RGB|4)
#define IMGFMT_RGB4_CHAR  (IMGFMT_RGB|4|128) // RGB4 with 1 pixel per byte
#define IMGFMT_RGB8  (IMGFMT_RGB|8)
#define IMGFMT_RGB12 (IMGFMT_RGB|12)
#define IMGFMT_RGB15 (IMGFMT_RGB|15)
#define IMGFMT_RGB16 (IMGFMT_RGB|16)
#define IMGFMT_RGB24 (IMGFMT_RGB|24)
#define IMGFMT_RGB32 (IMGFMT_RGB|32)
#define IMGFMT_RGB48LE (IMGFMT_RGB|48)
#define IMGFMT_RGB48BE (IMGFMT_RGB|48|128)
#define IMGFMT_RGB64LE (IMGFMT_RGB|64)
#define IMGFMT_RGB64BE (IMGFMT_RGB|64|128)

#define IMGFMT_BGR_MASK 0xFFFFFF00
#define IMGFMT_BGR (('B'<<24)|('G'<<16)|('R'<<8))
#define IMGFMT_BGR1  (IMGFMT_BGR|1)
#define IMGFMT_BGR4  (IMGFMT_BGR|4)
#define IMGFMT_BGR4_CHAR (IMGFMT_BGR|4|128) // BGR4 with 1 pixel per byte
#define IMGFMT_BGR8  (IMGFMT_BGR|8)
#define IMGFMT_BGR12 (IMGFMT_BGR|12)
#define IMGFMT_BGR15 (IMGFMT_BGR|15)
#define IMGFMT_BGR16 (IMGFMT_BGR|16)
#define IMGFMT_BGR24 (IMGFMT_BGR|24)
#define IMGFMT_BGR32 (IMGFMT_BGR|32)

#define IMGFMT_GBR24P (('G'<<24)|('B'<<16)|('R'<<8)|24)
#define IMGFMT_GBR12PLE (('G'<<24)|('B'<<16)|('R'<<8)|36)
#define IMGFMT_GBR12PBE (('G'<<24)|('B'<<16)|('R'<<8)|36|128)
#define IMGFMT_GBR14PLE (('G'<<24)|('B'<<16)|('R'<<8)|42)
#define IMGFMT_GBR14PBE (('G'<<24)|('B'<<16)|('R'<<8)|42|128)

#if HAVE_BIGENDIAN
#define IMGFMT_ABGR    IMGFMT_RGB32
#define IMGFMT_BGRA    (IMGFMT_RGB32|128)
#define IMGFMT_ARGB    IMGFMT_BGR32
#define IMGFMT_RGBA    (IMGFMT_BGR32|128)
#define IMGFMT_RGB64NE IMGFMT_RGB64BE
#define IMGFMT_RGB48NE IMGFMT_RGB48BE
#define IMGFMT_RGB12BE IMGFMT_RGB12
#define IMGFMT_RGB12LE (IMGFMT_RGB12|128)
#define IMGFMT_RGB15BE IMGFMT_RGB15
#define IMGFMT_RGB15LE (IMGFMT_RGB15|128)
#define IMGFMT_RGB16BE IMGFMT_RGB16
#define IMGFMT_RGB16LE (IMGFMT_RGB16|128)
#define IMGFMT_BGR12BE IMGFMT_BGR12
#define IMGFMT_BGR12LE (IMGFMT_BGR12|128)
#define IMGFMT_BGR15BE IMGFMT_BGR15
#define IMGFMT_BGR15LE (IMGFMT_BGR15|128)
#define IMGFMT_BGR16BE IMGFMT_BGR16
#define IMGFMT_BGR16LE (IMGFMT_BGR16|128)
#define IMGFMT_GBR12P IMGFMT_GBR12PBE
#define IMGFMT_GBR14P IMGFMT_GBR14PBE
#else
#define IMGFMT_ABGR (IMGFMT_BGR32|128)
#define IMGFMT_BGRA IMGFMT_BGR32
#define IMGFMT_ARGB (IMGFMT_RGB32|128)
#define IMGFMT_RGBA IMGFMT_RGB32
#define IMGFMT_RGB64NE IMGFMT_RGB64LE
#define IMGFMT_RGB48NE IMGFMT_RGB48LE
#define IMGFMT_RGB12BE (IMGFMT_RGB12|128)
#define IMGFMT_RGB12LE IMGFMT_RGB12
#define IMGFMT_RGB15BE (IMGFMT_RGB15|128)
#define IMGFMT_RGB15LE IMGFMT_RGB15
#define IMGFMT_RGB16BE (IMGFMT_RGB16|128)
#define IMGFMT_RGB16LE IMGFMT_RGB16
#define IMGFMT_BGR12BE (IMGFMT_BGR12|128)
#define IMGFMT_BGR12LE IMGFMT_BGR12
#define IMGFMT_BGR15BE (IMGFMT_BGR15|128)
#define IMGFMT_BGR15LE IMGFMT_BGR15
#define IMGFMT_BGR16BE (IMGFMT_BGR16|128)
#define IMGFMT_BGR16LE IMGFMT_BGR16
#define IMGFMT_GBR12P IMGFMT_GBR12PLE
#define IMGFMT_GBR14P IMGFMT_GBR14PLE
#endif

/* old names for compatibility */
#define IMGFMT_RG4B  IMGFMT_RGB4_CHAR
#define IMGFMT_BG4B  IMGFMT_BGR4_CHAR

#define IMGFMT_IS_RGB(fmt) (((fmt)&IMGFMT_RGB_MASK)==IMGFMT_RGB)
#define IMGFMT_IS_BGR(fmt) (((fmt)&IMGFMT_BGR_MASK)==IMGFMT_BGR)

#define IMGFMT_RGB_DEPTH(fmt) ((fmt)&0x7F)
#define IMGFMT_BGR_DEPTH(fmt) ((fmt)&0x7F)


/* Planar YUV Formats */

#define IMGFMT_YVU9 0x39555659
#define IMGFMT_IF09 0x39304649
#define IMGFMT_YV12 0x32315659
#define IMGFMT_I420 0x30323449
#define IMGFMT_IYUV 0x56555949
#define IMGFMT_CLPL 0x4C504C43
#define IMGFMT_Y800 0x30303859
#define IMGFMT_Y8   0x20203859
#define IMGFMT_NV12 0x3231564E
#define IMGFMT_NV21 0x3132564E
#define IMGFMT_Y16_LE 0x20363159

/* unofficial Planar Formats, FIXME if official 4CC exists */
#define IMGFMT_444P 0x50343434
#define IMGFMT_422P 0x50323234
#define IMGFMT_411P 0x50313134
#define IMGFMT_440P 0x50303434
#define IMGFMT_HM12 0x32314D48
#define IMGFMT_Y16_BE 0x59313620

// Gray with alpha
#define IMGFMT_Y8A 0x59320008
// 4:2:0 planar with alpha
#define IMGFMT_420A 0x41303234
// 4:2:2 planar with alpha
#define IMGFMT_422A 0x41323234
// 4:4:4 planar with alpha
#define IMGFMT_444A 0x41343434

#define IMGFMT_444P16_LE 0x51343434
#define IMGFMT_444P16_BE 0x34343451
#define IMGFMT_444P14_LE 0x54343434
#define IMGFMT_444P14_BE 0x34343454
#define IMGFMT_444P12_LE 0x55343434
#define IMGFMT_444P12_BE 0x34343455
#define IMGFMT_444P10_LE 0x52343434
#define IMGFMT_444P10_BE 0x34343452
#define IMGFMT_444P9_LE  0x53343434
#define IMGFMT_444P9_BE  0x34343453
#define IMGFMT_422P16_LE 0x51323234
#define IMGFMT_422P16_BE 0x34323251
#define IMGFMT_422P14_LE 0x54323234
#define IMGFMT_422P14_BE 0x34323254
#define IMGFMT_422P12_LE 0x55323234
#define IMGFMT_422P12_BE 0x34323255
#define IMGFMT_422P10_LE 0x52323234
#define IMGFMT_422P10_BE 0x34323252
#define IMGFMT_422P9_LE  0x53323234
#define IMGFMT_422P9_BE  0x34323253
#define IMGFMT_420P16_LE 0x51303234
#define IMGFMT_420P16_BE 0x34323051
#define IMGFMT_420P14_LE 0x54303234
#define IMGFMT_420P14_BE 0x34323054
#define IMGFMT_420P12_LE 0x55303234
#define IMGFMT_420P12_BE 0x34323055
#define IMGFMT_420P10_LE 0x52303234
#define IMGFMT_420P10_BE 0x34323052
#define IMGFMT_420P9_LE  0x53303234
#define IMGFMT_420P9_BE  0x34323053
#if HAVE_BIGENDIAN
#define IMGFMT_444P16 IMGFMT_444P16_BE
#define IMGFMT_444P14 IMGFMT_444P14_BE
#define IMGFMT_444P12 IMGFMT_444P12_BE
#define IMGFMT_444P10 IMGFMT_444P10_BE
#define IMGFMT_444P9  IMGFMT_444P9_BE
#define IMGFMT_422P16 IMGFMT_422P16_BE
#define IMGFMT_422P14 IMGFMT_422P14_BE
#define IMGFMT_422P12 IMGFMT_422P12_BE
#define IMGFMT_422P10 IMGFMT_422P10_BE
#define IMGFMT_422P9  IMGFMT_422P9_BE
#define IMGFMT_420P16 IMGFMT_420P16_BE
#define IMGFMT_420P14 IMGFMT_420P14_BE
#define IMGFMT_420P12 IMGFMT_420P12_BE
#define IMGFMT_420P10 IMGFMT_420P10_BE
#define IMGFMT_420P9  IMGFMT_420P9_BE
#define IMGFMT_Y16    IMGFMT_Y16_BE
#define IMGFMT_IS_YUVP16_NE(fmt) IMGFMT_IS_YUVP16_BE(fmt)
#else
#define IMGFMT_444P16 IMGFMT_444P16_LE
#define IMGFMT_444P14 IMGFMT_444P14_LE
#define IMGFMT_444P12 IMGFMT_444P12_LE
#define IMGFMT_444P10 IMGFMT_444P10_LE
#define IMGFMT_444P9  IMGFMT_444P9_LE
#define IMGFMT_422P16 IMGFMT_422P16_LE
#define IMGFMT_422P14 IMGFMT_422P14_LE
#define IMGFMT_422P12 IMGFMT_422P12_LE
#define IMGFMT_422P10 IMGFMT_422P10_LE
#define IMGFMT_422P9  IMGFMT_422P9_LE
#define IMGFMT_420P16 IMGFMT_420P16_LE
#define IMGFMT_420P14 IMGFMT_420P14_LE
#define IMGFMT_420P12 IMGFMT_420P12_LE
#define IMGFMT_420P10 IMGFMT_420P10_LE
#define IMGFMT_420P9  IMGFMT_420P9_LE
#define IMGFMT_Y16    IMGFMT_Y16_LE
#define IMGFMT_IS_YUVP16_NE(fmt) IMGFMT_IS_YUVP16_LE(fmt)
#endif

#define IMGFMT_IS_YUVP16_LE(fmt) (((fmt - 0x51000034) & 0xfc0000ff) == 0)
#define IMGFMT_IS_YUVP16_BE(fmt) (((fmt - 0x34000051) & 0xff0000fc) == 0)
#define IMGFMT_IS_YUVP16(fmt)    (IMGFMT_IS_YUVP16_LE(fmt) || IMGFMT_IS_YUVP16_BE(fmt))

/**
 * \brief Find the corresponding full 16 bit format, i.e. IMGFMT_420P10_LE -> IMGFMT_420P16_LE
 * \return normalized format ID or 0 if none exists.
 */
static inline int normalize_yuvp16(int fmt) {
    if (IMGFMT_IS_YUVP16_LE(fmt))
        return (fmt & 0x00ffffff) | 0x51000000;
    if (IMGFMT_IS_YUVP16_BE(fmt))
        return (fmt & 0xffffff00) | 0x00000051;
    return 0;
}

/* Packed YUV Formats */

#define IMGFMT_IUYV 0x56595549 // Interlaced UYVY
#define IMGFMT_IY41 0x31435949 // Interlaced Y41P
#define IMGFMT_IYU1 0x31555949
#define IMGFMT_IYU2 0x32555949
#define IMGFMT_UYVY 0x59565955
#define IMGFMT_UYNV 0x564E5955 // Exactly same as UYVY
#define IMGFMT_cyuv 0x76757963 // upside-down UYVY
#define IMGFMT_Y422 0x32323459 // Exactly same as UYVY
#define IMGFMT_YUY2 0x32595559
#define IMGFMT_YUNV 0x564E5559 // Exactly same as YUY2
#define IMGFMT_YVYU 0x55595659
#define IMGFMT_Y41P 0x50313459
#define IMGFMT_Y211 0x31313259
#define IMGFMT_Y41T 0x54313459 // Y41P, Y lsb = transparency
#define IMGFMT_Y42T 0x54323459 // UYVY, Y lsb = transparency
#define IMGFMT_V422 0x32323456 // upside-down UYVY?
#define IMGFMT_V655 0x35353656
#define IMGFMT_CLJR 0x524A4C43
#define IMGFMT_YUVP 0x50565559 // 10-bit YUYV
#define IMGFMT_UYVP 0x50565955 // 10-bit UYVY

/* Compressed Formats */
#define IMGFMT_MPEGPES (('M'<<24)|('P'<<16)|('E'<<8)|('S'))
#define IMGFMT_MJPEG (('M')|('J'<<8)|('P'<<16)|('G'<<24))
/* Formats that are understood by zoran chips, we include
 * non-interlaced, interlaced top-first, interlaced bottom-first */
#define IMGFMT_ZRMJPEGNI  (('Z'<<24)|('R'<<16)|('N'<<8)|('I'))
#define IMGFMT_ZRMJPEGIT (('Z'<<24)|('R'<<16)|('I'<<8)|('T'))
#define IMGFMT_ZRMJPEGIB (('Z'<<24)|('R'<<16)|('I'<<8)|('B'))

// I think that this code could not be used by any other codec/format
#define IMGFMT_XVMC 0x1DC70000
#define IMGFMT_XVMC_MASK 0xFFFF0000
#define IMGFMT_IS_XVMC(fmt) (((fmt)&IMGFMT_XVMC_MASK)==IMGFMT_XVMC)
//these are chroma420
#define IMGFMT_XVMC_MOCO_MPEG2 (IMGFMT_XVMC|0x02)
#define IMGFMT_XVMC_IDCT_MPEG2 (IMGFMT_XVMC|0x82)

// VDPAU specific format.
#define IMGFMT_VDPAU               0x1DC80000
#define IMGFMT_VDPAU_MASK          0xFFFF0000
#define IMGFMT_IS_VDPAU(fmt)       (((fmt)&IMGFMT_VDPAU_MASK)==IMGFMT_VDPAU)
#define IMGFMT_VDPAU_MPEG1         (IMGFMT_VDPAU|0x01)
#define IMGFMT_VDPAU_MPEG2         (IMGFMT_VDPAU|0x02)
#define IMGFMT_VDPAU_H264          (IMGFMT_VDPAU|0x03)
#define IMGFMT_VDPAU_WMV3          (IMGFMT_VDPAU|0x04)
#define IMGFMT_VDPAU_VC1           (IMGFMT_VDPAU|0x05)
#define IMGFMT_VDPAU_MPEG4         (IMGFMT_VDPAU|0x06)

#define IMGFMT_IS_HWACCEL(fmt) (IMGFMT_IS_VDPAU(fmt) || IMGFMT_IS_XVMC(fmt))

typedef struct {
    void* data;
    int size;
    int id;        // stream id. usually 0x1E0
    int timestamp; // pts, 90000 Hz counter based
} vo_mpegpes_t;

const char *ff_vo_format_name(int format);

/**
 * Calculates the scale shifts for the chroma planes for planar YUV
 *
 * \param component_bits bits per component
 * \return bits-per-pixel for format if successful (i.e. format is 3 or 4-planes planar YUV), 0 otherwise
 */
int ff_mp_get_chroma_shift(int format, int *x_shift, int *y_shift, int *component_bits);

#endif /* MPLAYER_IMG_FORMAT_H */
