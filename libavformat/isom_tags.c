/*
 * ISO Media codec tags
 * Copyright (c) 2001 Fabrice Bellard
 * Copyright (c) 2002 Francois Revol <revol@free.fr>
 * Copyright (c) 2006 Baptiste Coudurier <baptiste.coudurier@free.fr>
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

#include "libavcodec/codec_id.h"
#include "avformat.h"
#include "internal.h"
#include "isom.h"

const AVCodecTag ff_codec_movvideo_tags[] = {
/*  { AV_CODEC_ID_, MKTAG('I', 'V', '5', '0') }, *//* Indeo 5.0 */

    { AV_CODEC_ID_RAWVIDEO, MKTAG('r', 'a', 'w', ' ') }, /* uncompressed RGB */
    { AV_CODEC_ID_RAWVIDEO, MKTAG('y', 'u', 'v', '2') }, /* uncompressed YUV422 */
    { AV_CODEC_ID_RAWVIDEO, MKTAG('2', 'v', 'u', 'y') }, /* uncompressed 8-bit 4:2:2 */
    { AV_CODEC_ID_RAWVIDEO, MKTAG('y', 'u', 'v', 's') }, /* same as 2VUY but byte-swapped */

    { AV_CODEC_ID_RAWVIDEO, MKTAG('L', '5', '5', '5') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('L', '5', '6', '5') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('B', '5', '6', '5') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('2', '4', 'B', 'G') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('B', 'G', 'R', 'A') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('R', 'G', 'B', 'A') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('A', 'B', 'G', 'R') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('b', '1', '6', 'g') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('b', '4', '8', 'r') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('b', '6', '4', 'a') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('b', 'x', 'b', 'g') }, /* BOXX */
    { AV_CODEC_ID_RAWVIDEO, MKTAG('b', 'x', 'r', 'g') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('b', 'x', 'y', 'v') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('N', 'O', '1', '6') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('D', 'V', 'O', 'O') }, /* Digital Voodoo SD 8 Bit */
    { AV_CODEC_ID_RAWVIDEO, MKTAG('R', '4', '2', '0') }, /* Radius DV YUV PAL */
    { AV_CODEC_ID_RAWVIDEO, MKTAG('R', '4', '1', '1') }, /* Radius DV YUV NTSC */

    { AV_CODEC_ID_R10K,   MKTAG('R', '1', '0', 'k') }, /* uncompressed 10-bit RGB */
    { AV_CODEC_ID_R10K,   MKTAG('R', '1', '0', 'g') }, /* uncompressed 10-bit RGB */
    { AV_CODEC_ID_R210,   MKTAG('r', '2', '1', '0') }, /* uncompressed 10-bit RGB */
    { AV_CODEC_ID_AVUI,   MKTAG('A', 'V', 'U', 'I') }, /* AVID Uncompressed deinterleaved UYVY422 */
    { AV_CODEC_ID_AVRP,   MKTAG('A', 'V', 'r', 'p') }, /* Avid 1:1 10-bit RGB Packer */
    { AV_CODEC_ID_AVRP,   MKTAG('S', 'U', 'D', 'S') }, /* Avid DS Uncompressed */
    { AV_CODEC_ID_V210,   MKTAG('v', '2', '1', '0') }, /* uncompressed 10-bit 4:2:2 */
    { AV_CODEC_ID_V210,   MKTAG('b', 'x', 'y', '2') }, /* BOXX 10-bit 4:2:2 */
    { AV_CODEC_ID_V308,   MKTAG('v', '3', '0', '8') }, /* uncompressed  8-bit 4:4:4 */
    { AV_CODEC_ID_V408,   MKTAG('v', '4', '0', '8') }, /* uncompressed  8-bit 4:4:4:4 */
    { AV_CODEC_ID_V410,   MKTAG('v', '4', '1', '0') }, /* uncompressed 10-bit 4:4:4 */
    { AV_CODEC_ID_Y41P,   MKTAG('Y', '4', '1', 'P') }, /* uncompressed 12-bit 4:1:1 */
    { AV_CODEC_ID_YUV4,   MKTAG('y', 'u', 'v', '4') }, /* libquicktime packed yuv420p */
    { AV_CODEC_ID_TARGA_Y216, MKTAG('Y', '2', '1', '6') },

    { AV_CODEC_ID_MJPEG,  MKTAG('j', 'p', 'e', 'g') }, /* PhotoJPEG */
    { AV_CODEC_ID_MJPEG,  MKTAG('m', 'j', 'p', 'a') }, /* Motion-JPEG (format A) */
    { AV_CODEC_ID_MJPEG,  MKTAG('A', 'V', 'D', 'J') }, /* MJPEG with alpha-channel (AVID JFIF meridien compressed) */
    { AV_CODEC_ID_MJPEG,  MKTAG('A', 'V', 'R', 'n') }, /* MJPEG with alpha-channel (AVID ABVB/Truevision NuVista) */
    { AV_CODEC_ID_MJPEG,  MKTAG('d', 'm', 'b', '1') }, /* Motion JPEG OpenDML */
    { AV_CODEC_ID_MJPEGB, MKTAG('m', 'j', 'p', 'b') }, /* Motion-JPEG (format B) */

    { AV_CODEC_ID_SVQ1, MKTAG('S', 'V', 'Q', '1') }, /* Sorenson Video v1 */
    { AV_CODEC_ID_SVQ1, MKTAG('s', 'v', 'q', '1') }, /* Sorenson Video v1 */
    { AV_CODEC_ID_SVQ1, MKTAG('s', 'v', 'q', 'i') }, /* Sorenson Video v1 (from QT specs)*/
    { AV_CODEC_ID_SVQ3, MKTAG('S', 'V', 'Q', '3') }, /* Sorenson Video v3 */

    { AV_CODEC_ID_MPEG4, MKTAG('m', 'p', '4', 'v') },
    { AV_CODEC_ID_MPEG4, MKTAG('D', 'I', 'V', 'X') }, /* OpenDiVX *//* sample files at http://heroinewarrior.com/xmovie.php3 use this tag */
    { AV_CODEC_ID_MPEG4, MKTAG('X', 'V', 'I', 'D') },
    { AV_CODEC_ID_MPEG4, MKTAG('3', 'I', 'V', '2') }, /* experimental: 3IVX files before ivx D4 4.5.1 */

    { AV_CODEC_ID_H263, MKTAG('h', '2', '6', '3') }, /* H.263 */
    { AV_CODEC_ID_H263, MKTAG('s', '2', '6', '3') }, /* H.263 ?? works */

    { AV_CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'c', 'p') }, /* DV PAL */
    { AV_CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'c', ' ') }, /* DV NTSC */
    { AV_CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'l', 'p') }, /* DV PAL 16:9 produced by Radius SoftDV */
    { AV_CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'l', ' ') }, /* DV NTSC 16:9 produced by Radius SoftDV */
    { AV_CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'p', 'p') }, /* DVCPRO PAL produced by FCP */
    { AV_CODEC_ID_DVVIDEO, MKTAG('d', 'v', '5', 'p') }, /* DVCPRO50 PAL produced by FCP */
    { AV_CODEC_ID_DVVIDEO, MKTAG('d', 'v', '5', 'n') }, /* DVCPRO50 NTSC produced by FCP */
    { AV_CODEC_ID_DVVIDEO, MKTAG('A', 'V', 'd', 'v') }, /* AVID DV */
    { AV_CODEC_ID_DVVIDEO, MKTAG('A', 'V', 'd', '1') }, /* AVID DV100 */
    { AV_CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'h', 'q') }, /* DVCPRO HD 720p50 */
    { AV_CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'h', 'p') }, /* DVCPRO HD 720p60 */
    { AV_CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'h', '1') },
    { AV_CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'h', '2') },
    { AV_CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'h', '4') },
    { AV_CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'h', '5') }, /* DVCPRO HD 50i produced by FCP */
    { AV_CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'h', '6') }, /* DVCPRO HD 60i produced by FCP */
    { AV_CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'h', '3') }, /* DVCPRO HD 30p produced by FCP */

    { AV_CODEC_ID_VP3,     MKTAG('V', 'P', '3', '1') }, /* On2 VP3 */
    { AV_CODEC_ID_RPZA,    MKTAG('r', 'p', 'z', 'a') }, /* Apple Video (RPZA) */
    { AV_CODEC_ID_CINEPAK, MKTAG('c', 'v', 'i', 'd') }, /* Cinepak */
    { AV_CODEC_ID_8BPS,    MKTAG('8', 'B', 'P', 'S') }, /* Planar RGB (8BPS) */
    { AV_CODEC_ID_SMC,     MKTAG('s', 'm', 'c', ' ') }, /* Apple Graphics (SMC) */
    { AV_CODEC_ID_QTRLE,   MKTAG('r', 'l', 'e', ' ') }, /* Apple Animation (RLE) */
    { AV_CODEC_ID_SGIRLE,  MKTAG('r', 'l', 'e', '1') }, /* SGI RLE 8-bit */
    { AV_CODEC_ID_MSRLE,   MKTAG('W', 'R', 'L', 'E') },
    { AV_CODEC_ID_QDRAW,   MKTAG('q', 'd', 'r', 'w') }, /* QuickDraw */
    { AV_CODEC_ID_CDTOONS, MKTAG('Q', 'k', 'B', 'k') }, /* CDToons */

    { AV_CODEC_ID_RAWVIDEO, MKTAG('W', 'R', 'A', 'W') },

    { AV_CODEC_ID_HEVC, MKTAG('h', 'e', 'v', '1') }, /* HEVC/H.265 which indicates parameter sets may be in ES */
    { AV_CODEC_ID_HEVC, MKTAG('h', 'v', 'c', '1') }, /* HEVC/H.265 which indicates parameter sets shall not be in ES */
    { AV_CODEC_ID_HEVC, MKTAG('d', 'v', 'h', 'e') }, /* HEVC-based Dolby Vision derived from hev1 */
                                                     /* dvh1 is handled within mov.c */

    { AV_CODEC_ID_H264, MKTAG('a', 'v', 'c', '1') }, /* AVC-1/H.264 */
    { AV_CODEC_ID_H264, MKTAG('a', 'v', 'c', '2') },
    { AV_CODEC_ID_H264, MKTAG('a', 'v', 'c', '3') },
    { AV_CODEC_ID_H264, MKTAG('a', 'v', 'c', '4') },
    { AV_CODEC_ID_H264, MKTAG('a', 'i', '5', 'p') }, /* AVC-Intra  50M 720p24/30/60 */
    { AV_CODEC_ID_H264, MKTAG('a', 'i', '5', 'q') }, /* AVC-Intra  50M 720p25/50 */
    { AV_CODEC_ID_H264, MKTAG('a', 'i', '5', '2') }, /* AVC-Intra  50M 1080p25/50 */
    { AV_CODEC_ID_H264, MKTAG('a', 'i', '5', '3') }, /* AVC-Intra  50M 1080p24/30/60 */
    { AV_CODEC_ID_H264, MKTAG('a', 'i', '5', '5') }, /* AVC-Intra  50M 1080i50 */
    { AV_CODEC_ID_H264, MKTAG('a', 'i', '5', '6') }, /* AVC-Intra  50M 1080i60 */
    { AV_CODEC_ID_H264, MKTAG('a', 'i', '1', 'p') }, /* AVC-Intra 100M 720p24/30/60 */
    { AV_CODEC_ID_H264, MKTAG('a', 'i', '1', 'q') }, /* AVC-Intra 100M 720p25/50 */
    { AV_CODEC_ID_H264, MKTAG('a', 'i', '1', '2') }, /* AVC-Intra 100M 1080p25/50 */
    { AV_CODEC_ID_H264, MKTAG('a', 'i', '1', '3') }, /* AVC-Intra 100M 1080p24/30/60 */
    { AV_CODEC_ID_H264, MKTAG('a', 'i', '1', '5') }, /* AVC-Intra 100M 1080i50 */
    { AV_CODEC_ID_H264, MKTAG('a', 'i', '1', '6') }, /* AVC-Intra 100M 1080i60 */
    { AV_CODEC_ID_H264, MKTAG('A', 'V', 'i', 'n') }, /* AVC-Intra with implicit SPS/PPS */
    { AV_CODEC_ID_H264, MKTAG('a', 'i', 'v', 'x') }, /* XAVC 10-bit 4:2:2 */
    { AV_CODEC_ID_H264, MKTAG('r', 'v', '6', '4') }, /* X-Com Radvision */
    { AV_CODEC_ID_H264, MKTAG('x', 'a', 'l', 'g') }, /* XAVC-L HD422 produced by FCP */
    { AV_CODEC_ID_H264, MKTAG('a', 'v', 'l', 'g') }, /* Panasonic P2 AVC-LongG */
    { AV_CODEC_ID_H264, MKTAG('d', 'v', 'a', '1') }, /* AVC-based Dolby Vision derived from avc1 */
    { AV_CODEC_ID_H264, MKTAG('d', 'v', 'a', 'v') }, /* AVC-based Dolby Vision derived from avc3 */

    { AV_CODEC_ID_VP8,  MKTAG('v', 'p', '0', '8') }, /* VP8 */
    { AV_CODEC_ID_VP9,  MKTAG('v', 'p', '0', '9') }, /* VP9 */
    { AV_CODEC_ID_AV1,  MKTAG('a', 'v', '0', '1') }, /* AV1 */

    { AV_CODEC_ID_MPEG1VIDEO, MKTAG('m', '1', 'v', ' ') },
    { AV_CODEC_ID_MPEG1VIDEO, MKTAG('m', '1', 'v', '1') }, /* Apple MPEG-1 Camcorder */
    { AV_CODEC_ID_MPEG1VIDEO, MKTAG('m', 'p', 'e', 'g') }, /* MPEG */
    { AV_CODEC_ID_MPEG1VIDEO, MKTAG('m', 'p', '1', 'v') }, /* CoreMedia CMVideoCodecType */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('m', '2', 'v', '1') }, /* Apple MPEG-2 Camcorder */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('h', 'd', 'v', '1') }, /* MPEG-2 HDV 720p30 */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('h', 'd', 'v', '2') }, /* MPEG-2 HDV 1080i60 */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('h', 'd', 'v', '3') }, /* MPEG-2 HDV 1080i50 */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('h', 'd', 'v', '4') }, /* MPEG-2 HDV 720p24 */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('h', 'd', 'v', '5') }, /* MPEG-2 HDV 720p25 */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('h', 'd', 'v', '6') }, /* MPEG-2 HDV 1080p24 */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('h', 'd', 'v', '7') }, /* MPEG-2 HDV 1080p25 */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('h', 'd', 'v', '8') }, /* MPEG-2 HDV 1080p30 */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('h', 'd', 'v', '9') }, /* MPEG-2 HDV 720p60 JVC */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('h', 'd', 'v', 'a') }, /* MPEG-2 HDV 720p50 */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('m', 'x', '5', 'n') }, /* MPEG-2 IMX NTSC 525/60 50mb/s produced by FCP */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('m', 'x', '5', 'p') }, /* MPEG-2 IMX PAL 625/50 50mb/s produced by FCP */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('m', 'x', '4', 'n') }, /* MPEG-2 IMX NTSC 525/60 40mb/s produced by FCP */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('m', 'x', '4', 'p') }, /* MPEG-2 IMX PAL 625/50 40mb/s produced by FCP */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('m', 'x', '3', 'n') }, /* MPEG-2 IMX NTSC 525/60 30mb/s produced by FCP */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('m', 'x', '3', 'p') }, /* MPEG-2 IMX PAL 625/50 30mb/s produced by FCP */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', '5', '1') }, /* XDCAM HD422 720p30 CBR */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', '5', '4') }, /* XDCAM HD422 720p24 CBR */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', '5', '5') }, /* XDCAM HD422 720p25 CBR */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', '5', '9') }, /* XDCAM HD422 720p60 CBR */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', '5', 'a') }, /* XDCAM HD422 720p50 CBR */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', '5', 'b') }, /* XDCAM HD422 1080i60 CBR */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', '5', 'c') }, /* XDCAM HD422 1080i50 CBR */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', '5', 'd') }, /* XDCAM HD422 1080p24 CBR */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', '5', 'e') }, /* XDCAM HD422 1080p25 CBR */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', '5', 'f') }, /* XDCAM HD422 1080p30 CBR */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', 'v', '1') }, /* XDCAM EX 720p30 VBR */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', 'v', '2') }, /* XDCAM HD 1080i60 */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', 'v', '3') }, /* XDCAM HD 1080i50 VBR */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', 'v', '4') }, /* XDCAM EX 720p24 VBR */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', 'v', '5') }, /* XDCAM EX 720p25 VBR */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', 'v', '6') }, /* XDCAM HD 1080p24 VBR */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', 'v', '7') }, /* XDCAM HD 1080p25 VBR */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', 'v', '8') }, /* XDCAM HD 1080p30 VBR */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', 'v', '9') }, /* XDCAM EX 720p60 VBR */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', 'v', 'a') }, /* XDCAM EX 720p50 VBR */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', 'v', 'b') }, /* XDCAM EX 1080i60 VBR */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', 'v', 'c') }, /* XDCAM EX 1080i50 VBR */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', 'v', 'd') }, /* XDCAM EX 1080p24 VBR */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', 'v', 'e') }, /* XDCAM EX 1080p25 VBR */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', 'v', 'f') }, /* XDCAM EX 1080p30 VBR */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', 'h', 'd') }, /* XDCAM HD 540p */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('x', 'd', 'h', '2') }, /* XDCAM HD422 540p */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('A', 'V', 'm', 'p') }, /* AVID IMX PAL */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('m', 'p', '2', 'v') }, /* FCP5 */

    { AV_CODEC_ID_JPEG2000, MKTAG('m', 'j', 'p', '2') }, /* JPEG 2000 produced by FCP */

    { AV_CODEC_ID_TARGA, MKTAG('t', 'g', 'a', ' ') }, /* Truevision Targa */
    { AV_CODEC_ID_TIFF,  MKTAG('t', 'i', 'f', 'f') }, /* TIFF embedded in MOV */
    { AV_CODEC_ID_GIF,   MKTAG('g', 'i', 'f', ' ') }, /* embedded gif files as frames (usually one "click to play movie" frame) */
    { AV_CODEC_ID_PNG,   MKTAG('p', 'n', 'g', ' ') },
    { AV_CODEC_ID_PNG,   MKTAG('M', 'N', 'G', ' ') },

    { AV_CODEC_ID_VC1, MKTAG('v', 'c', '-', '1') }, /* SMPTE RP 2025 */
    { AV_CODEC_ID_CAVS, MKTAG('a', 'v', 's', '2') },

    { AV_CODEC_ID_DIRAC,     MKTAG('d', 'r', 'a', 'c') },
    { AV_CODEC_ID_DNXHD,     MKTAG('A', 'V', 'd', 'n') }, /* AVID DNxHD */
    { AV_CODEC_ID_DNXHD,     MKTAG('A', 'V', 'd', 'h') }, /* AVID DNxHR */
    { AV_CODEC_ID_H263,      MKTAG('H', '2', '6', '3') },
    { AV_CODEC_ID_MSMPEG4V3, MKTAG('3', 'I', 'V', 'D') }, /* 3ivx DivX Doctor */
    { AV_CODEC_ID_RAWVIDEO,  MKTAG('A', 'V', '1', 'x') }, /* AVID 1:1x */
    { AV_CODEC_ID_RAWVIDEO,  MKTAG('A', 'V', 'u', 'p') },
    { AV_CODEC_ID_SGI,       MKTAG('s', 'g', 'i', ' ') }, /* SGI  */
    { AV_CODEC_ID_DPX,       MKTAG('d', 'p', 'x', ' ') }, /* DPX */
    { AV_CODEC_ID_EXR,       MKTAG('e', 'x', 'r', ' ') }, /* OpenEXR */

    { AV_CODEC_ID_PRORES, MKTAG('a', 'p', 'c', 'h') }, /* Apple ProRes 422 High Quality */
    { AV_CODEC_ID_PRORES, MKTAG('a', 'p', 'c', 'n') }, /* Apple ProRes 422 Standard Definition */
    { AV_CODEC_ID_PRORES, MKTAG('a', 'p', 'c', 's') }, /* Apple ProRes 422 LT */
    { AV_CODEC_ID_PRORES, MKTAG('a', 'p', 'c', 'o') }, /* Apple ProRes 422 Proxy */
    { AV_CODEC_ID_PRORES, MKTAG('a', 'p', '4', 'h') }, /* Apple ProRes 4444 */
    { AV_CODEC_ID_PRORES, MKTAG('a', 'p', '4', 'x') }, /* Apple ProRes 4444 XQ */
    { AV_CODEC_ID_FLIC,   MKTAG('f', 'l', 'i', 'c') },

    { AV_CODEC_ID_AIC, MKTAG('i', 'c', 'o', 'd') },

    { AV_CODEC_ID_HAP, MKTAG('H', 'a', 'p', '1') },
    { AV_CODEC_ID_HAP, MKTAG('H', 'a', 'p', '5') },
    { AV_CODEC_ID_HAP, MKTAG('H', 'a', 'p', 'Y') },
    { AV_CODEC_ID_HAP, MKTAG('H', 'a', 'p', 'A') },
    { AV_CODEC_ID_HAP, MKTAG('H', 'a', 'p', 'M') },

    { AV_CODEC_ID_DXV, MKTAG('D', 'X', 'D', '3') },
    { AV_CODEC_ID_DXV, MKTAG('D', 'X', 'D', 'I') },

    { AV_CODEC_ID_MAGICYUV, MKTAG('M', '0', 'R', '0') },
    { AV_CODEC_ID_MAGICYUV, MKTAG('M', '0', 'R', 'A') },
    { AV_CODEC_ID_MAGICYUV, MKTAG('M', '0', 'R', 'G') },
    { AV_CODEC_ID_MAGICYUV, MKTAG('M', '0', 'Y', '0') },
    { AV_CODEC_ID_MAGICYUV, MKTAG('M', '0', 'Y', '2') },
    { AV_CODEC_ID_MAGICYUV, MKTAG('M', '0', 'Y', '4') },
    { AV_CODEC_ID_MAGICYUV, MKTAG('M', '8', 'R', 'G') },
    { AV_CODEC_ID_MAGICYUV, MKTAG('M', '8', 'R', 'A') },
    { AV_CODEC_ID_MAGICYUV, MKTAG('M', '8', 'G', '0') },
    { AV_CODEC_ID_MAGICYUV, MKTAG('M', '8', 'Y', '0') },
    { AV_CODEC_ID_MAGICYUV, MKTAG('M', '8', 'Y', '2') },
    { AV_CODEC_ID_MAGICYUV, MKTAG('M', '8', 'Y', '4') },
    { AV_CODEC_ID_MAGICYUV, MKTAG('M', '8', 'Y', 'A') },
    { AV_CODEC_ID_MAGICYUV, MKTAG('M', '2', 'R', 'A') },
    { AV_CODEC_ID_MAGICYUV, MKTAG('M', '2', 'R', 'G') },

    { AV_CODEC_ID_SHEERVIDEO, MKTAG('S', 'h', 'r', '0') },
    { AV_CODEC_ID_SHEERVIDEO, MKTAG('S', 'h', 'r', '1') },
    { AV_CODEC_ID_SHEERVIDEO, MKTAG('S', 'h', 'r', '2') },
    { AV_CODEC_ID_SHEERVIDEO, MKTAG('S', 'h', 'r', '3') },
    { AV_CODEC_ID_SHEERVIDEO, MKTAG('S', 'h', 'r', '4') },
    { AV_CODEC_ID_SHEERVIDEO, MKTAG('S', 'h', 'r', '5') },
    { AV_CODEC_ID_SHEERVIDEO, MKTAG('S', 'h', 'r', '6') },
    { AV_CODEC_ID_SHEERVIDEO, MKTAG('S', 'h', 'r', '7') },

    { AV_CODEC_ID_PIXLET, MKTAG('p', 'x', 'l', 't') },

    { AV_CODEC_ID_NOTCHLC, MKTAG('n', 'c', 'l', 'c') },

    { AV_CODEC_ID_RAWVIDEO, MKTAG('B', 'G', 'G', 'R') }, /* ASC Bayer BGGR */

    { AV_CODEC_ID_MEDIA100, MKTAG('6', '0', '1', 'N') },
    { AV_CODEC_ID_MEDIA100, MKTAG('6', '0', '1', 'P') },
    { AV_CODEC_ID_MEDIA100, MKTAG('d', 't', 'n', 't') },
    { AV_CODEC_ID_MEDIA100, MKTAG('d', 't', 'N', 'T') },
    { AV_CODEC_ID_MEDIA100, MKTAG('d', 't', 'p', 'a') },
    { AV_CODEC_ID_MEDIA100, MKTAG('d', 't', 'P', 'A') },

    { AV_CODEC_ID_NONE, 0 },
};

const AVCodecTag ff_codec_movaudio_tags[] = {
    { AV_CODEC_ID_AAC,             MKTAG('m', 'p', '4', 'a') },
    { AV_CODEC_ID_AC3,             MKTAG('a', 'c', '-', '3') }, /* ETSI TS 102 366 Annex F */
    { AV_CODEC_ID_AC3,             MKTAG('s', 'a', 'c', '3') }, /* Nero Recode */
    { AV_CODEC_ID_ADPCM_IMA_QT,    MKTAG('i', 'm', 'a', '4') },
    { AV_CODEC_ID_ALAC,            MKTAG('a', 'l', 'a', 'c') },
    { AV_CODEC_ID_AMR_NB,          MKTAG('s', 'a', 'm', 'r') }, /* AMR-NB 3gp */
    { AV_CODEC_ID_AMR_WB,          MKTAG('s', 'a', 'w', 'b') }, /* AMR-WB 3gp */
    { AV_CODEC_ID_DTS,             MKTAG('d', 't', 's', 'c') }, /* DTS formats prior to DTS-HD */
    { AV_CODEC_ID_DTS,             MKTAG('d', 't', 's', 'h') }, /* DTS-HD audio formats */
    { AV_CODEC_ID_DTS,             MKTAG('d', 't', 's', 'l') }, /* DTS-HD Lossless formats */
    { AV_CODEC_ID_DTS,             MKTAG('d', 't', 's', 'e') }, /* DTS Express */
    { AV_CODEC_ID_DTS,             MKTAG('D', 'T', 'S', ' ') }, /* non-standard */
    { AV_CODEC_ID_EAC3,            MKTAG('e', 'c', '-', '3') }, /* ETSI TS 102 366 Annex F (only valid in ISOBMFF) */
    { AV_CODEC_ID_DVAUDIO,         MKTAG('v', 'd', 'v', 'a') },
    { AV_CODEC_ID_DVAUDIO,         MKTAG('d', 'v', 'c', 'a') },
    { AV_CODEC_ID_GSM,             MKTAG('a', 'g', 's', 'm') },
    { AV_CODEC_ID_ILBC,            MKTAG('i', 'l', 'b', 'c') },
    { AV_CODEC_ID_MACE3,           MKTAG('M', 'A', 'C', '3') },
    { AV_CODEC_ID_MACE6,           MKTAG('M', 'A', 'C', '6') },
    { AV_CODEC_ID_MP1,             MKTAG('.', 'm', 'p', '1') },
    { AV_CODEC_ID_MP2,             MKTAG('.', 'm', 'p', '2') },
    { AV_CODEC_ID_MP3,             MKTAG('.', 'm', 'p', '3') },
    { AV_CODEC_ID_MP3,             MKTAG('m', 'p', '3', ' ') }, /* vlc */
    { AV_CODEC_ID_MP3,             0x6D730055                },
    { AV_CODEC_ID_NELLYMOSER,      MKTAG('n', 'm', 'o', 's') }, /* Flash Media Server */
    { AV_CODEC_ID_NELLYMOSER,      MKTAG('N', 'E', 'L', 'L') }, /* Perian */
    { AV_CODEC_ID_PCM_ALAW,        MKTAG('a', 'l', 'a', 'w') },
    { AV_CODEC_ID_PCM_F32BE,       MKTAG('f', 'l', '3', '2') },
    { AV_CODEC_ID_PCM_F32LE,       MKTAG('f', 'l', '3', '2') },
    { AV_CODEC_ID_PCM_F64BE,       MKTAG('f', 'l', '6', '4') },
    { AV_CODEC_ID_PCM_F64LE,       MKTAG('f', 'l', '6', '4') },
    { AV_CODEC_ID_PCM_MULAW,       MKTAG('u', 'l', 'a', 'w') },
    { AV_CODEC_ID_PCM_S16BE,       MKTAG('t', 'w', 'o', 's') },
    { AV_CODEC_ID_PCM_S16LE,       MKTAG('s', 'o', 'w', 't') },
    { AV_CODEC_ID_PCM_S16BE,       MKTAG('l', 'p', 'c', 'm') },
    { AV_CODEC_ID_PCM_S16LE,       MKTAG('l', 'p', 'c', 'm') },
    { AV_CODEC_ID_PCM_S16BE,       MKTAG('i', 'p', 'c', 'm') },
    { AV_CODEC_ID_PCM_S16LE,       MKTAG('i', 'p', 'c', 'm') },
    { AV_CODEC_ID_PCM_S24BE,       MKTAG('i', 'n', '2', '4') },
    { AV_CODEC_ID_PCM_S24LE,       MKTAG('i', 'n', '2', '4') },
    { AV_CODEC_ID_PCM_S32BE,       MKTAG('i', 'n', '3', '2') },
    { AV_CODEC_ID_PCM_S32LE,       MKTAG('i', 'n', '3', '2') },
    { AV_CODEC_ID_PCM_S8,          MKTAG('s', 'o', 'w', 't') },
    { AV_CODEC_ID_PCM_U8,          MKTAG('r', 'a', 'w', ' ') },
    { AV_CODEC_ID_PCM_U8,          MKTAG('N', 'O', 'N', 'E') },
    { AV_CODEC_ID_QCELP,           MKTAG('Q', 'c', 'l', 'p') },
    { AV_CODEC_ID_QCELP,           MKTAG('Q', 'c', 'l', 'q') },
    { AV_CODEC_ID_QCELP,           MKTAG('s', 'q', 'c', 'p') }, /* ISO Media fourcc */
    { AV_CODEC_ID_QDM2,            MKTAG('Q', 'D', 'M', '2') },
    { AV_CODEC_ID_QDMC,            MKTAG('Q', 'D', 'M', 'C') },
    { AV_CODEC_ID_SPEEX,           MKTAG('s', 'p', 'e', 'x') }, /* Flash Media Server */
    { AV_CODEC_ID_SPEEX,           MKTAG('S', 'P', 'X', 'N') }, /* ZygoAudio (quality 10 mode) */
    { AV_CODEC_ID_EVRC,            MKTAG('s', 'e', 'v', 'c') }, /* 3GPP2 */
    { AV_CODEC_ID_SMV,             MKTAG('s', 's', 'm', 'v') }, /* 3GPP2 */
    { AV_CODEC_ID_FLAC,            MKTAG('f', 'L', 'a', 'C') },
    { AV_CODEC_ID_TRUEHD,          MKTAG('m', 'l', 'p', 'a') }, /* mp4ra.org */
    { AV_CODEC_ID_OPUS,            MKTAG('O', 'p', 'u', 's') }, /* mp4ra.org */
    { AV_CODEC_ID_MPEGH_3D_AUDIO,  MKTAG('m', 'h', 'm', '1') }, /* MPEG-H 3D Audio bitstream */
    { AV_CODEC_ID_NONE, 0 },
};

const struct AVCodecTag *avformat_get_mov_video_tags(void)
{
    return ff_codec_movvideo_tags;
}

const struct AVCodecTag *avformat_get_mov_audio_tags(void)
{
    return ff_codec_movaudio_tags;
}
