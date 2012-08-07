/*
 * ISO Media common code
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

//#define DEBUG

#include "avformat.h"
#include "internal.h"
#include "isom.h"
#include "riff.h"
#include "libavcodec/mpeg4audio.h"
#include "libavcodec/mpegaudiodata.h"

/* http://www.mp4ra.org */
/* ordered by muxing preference */
const AVCodecTag ff_mp4_obj_type[] = {
    { AV_CODEC_ID_MOV_TEXT    , 0x08 },
    { AV_CODEC_ID_MPEG4       , 0x20 },
    { AV_CODEC_ID_H264        , 0x21 },
    { AV_CODEC_ID_AAC         , 0x40 },
    { AV_CODEC_ID_MP4ALS      , 0x40 }, /* 14496-3 ALS */
    { AV_CODEC_ID_MPEG2VIDEO  , 0x61 }, /* MPEG2 Main */
    { AV_CODEC_ID_MPEG2VIDEO  , 0x60 }, /* MPEG2 Simple */
    { AV_CODEC_ID_MPEG2VIDEO  , 0x62 }, /* MPEG2 SNR */
    { AV_CODEC_ID_MPEG2VIDEO  , 0x63 }, /* MPEG2 Spatial */
    { AV_CODEC_ID_MPEG2VIDEO  , 0x64 }, /* MPEG2 High */
    { AV_CODEC_ID_MPEG2VIDEO  , 0x65 }, /* MPEG2 422 */
    { AV_CODEC_ID_AAC         , 0x66 }, /* MPEG2 AAC Main */
    { AV_CODEC_ID_AAC         , 0x67 }, /* MPEG2 AAC Low */
    { AV_CODEC_ID_AAC         , 0x68 }, /* MPEG2 AAC SSR */
    { AV_CODEC_ID_MP3         , 0x69 }, /* 13818-3 */
    { AV_CODEC_ID_MP2         , 0x69 }, /* 11172-3 */
    { AV_CODEC_ID_MPEG1VIDEO  , 0x6A }, /* 11172-2 */
    { AV_CODEC_ID_MP3         , 0x6B }, /* 11172-3 */
    { AV_CODEC_ID_MJPEG       , 0x6C }, /* 10918-1 */
    { AV_CODEC_ID_PNG         , 0x6D },
    { AV_CODEC_ID_JPEG2000    , 0x6E }, /* 15444-1 */
    { AV_CODEC_ID_VC1         , 0xA3 },
    { AV_CODEC_ID_DIRAC       , 0xA4 },
    { AV_CODEC_ID_AC3         , 0xA5 },
    { AV_CODEC_ID_DTS         , 0xA9 }, /* mp4ra.org */
    { AV_CODEC_ID_VORBIS      , 0xDD }, /* non standard, gpac uses it */
    { AV_CODEC_ID_DVD_SUBTITLE, 0xE0 }, /* non standard, see unsupported-embedded-subs-2.mp4 */
    { AV_CODEC_ID_QCELP       , 0xE1 },
    { AV_CODEC_ID_MPEG4SYSTEMS, 0x01 },
    { AV_CODEC_ID_MPEG4SYSTEMS, 0x02 },
    { AV_CODEC_ID_NONE        ,    0 },
};

const AVCodecTag ff_codec_movvideo_tags[] = {
/*  { AV_CODEC_ID_, MKTAG('I', 'V', '5', '0') }, *//* Indeo 5.0 */

    { AV_CODEC_ID_RAWVIDEO, MKTAG('r', 'a', 'w', ' ') }, /* Uncompressed RGB */
    { AV_CODEC_ID_RAWVIDEO, MKTAG('y', 'u', 'v', '2') }, /* Uncompressed YUV422 */
    { AV_CODEC_ID_RAWVIDEO, MKTAG('2', 'v', 'u', 'y') }, /* UNCOMPRESSED 8BIT 4:2:2 */
    { AV_CODEC_ID_RAWVIDEO, MKTAG('y', 'u', 'v', 's') }, /* same as 2vuy but byte swapped */

    { AV_CODEC_ID_RAWVIDEO, MKTAG('L', '5', '5', '5') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('L', '5', '6', '5') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('B', '5', '6', '5') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('2', '4', 'B', 'G') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('B', 'G', 'R', 'A') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('R', 'G', 'B', 'A') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('A', 'B', 'G', 'R') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('b', '1', '6', 'g') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('b', '4', '8', 'r') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('b', 'x', 'b', 'g') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('b', 'x', 'r', 'g') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('b', 'x', 'y', 'v') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('N', 'O', '1', '6') },
    { AV_CODEC_ID_RAWVIDEO, MKTAG('D', 'V', 'O', 'O') }, /* Digital Voodoo SD 8 Bit */
    { AV_CODEC_ID_RAWVIDEO, MKTAG('R', '4', '2', '0') }, /* Radius DV YUV PAL */
    { AV_CODEC_ID_RAWVIDEO, MKTAG('R', '4', '1', '1') }, /* Radius DV YUV NTSC */

    { AV_CODEC_ID_R10K,   MKTAG('R', '1', '0', 'k') }, /* UNCOMPRESSED 10BIT RGB */
    { AV_CODEC_ID_R10K,   MKTAG('R', '1', '0', 'g') }, /* UNCOMPRESSED 10BIT RGB */
    { AV_CODEC_ID_R210,   MKTAG('r', '2', '1', '0') }, /* UNCOMPRESSED 10BIT RGB */
    { AV_CODEC_ID_AVUI,   MKTAG('A', 'V', 'U', 'I') }, /* AVID Uncompressed deinterleaved UYVY422 */
    { AV_CODEC_ID_AVRP,   MKTAG('A', 'V', 'r', 'p') }, /* Avid 1:1 10-bit RGB Packer */
    { AV_CODEC_ID_AVRP,   MKTAG('S', 'U', 'D', 'S') }, /* Avid DS Uncompressed */
    { AV_CODEC_ID_V210,   MKTAG('v', '2', '1', '0') }, /* UNCOMPRESSED 10BIT 4:2:2 */
    { AV_CODEC_ID_V210,   MKTAG('b', 'x', 'y', '2') }, /* BOXX 10BIT 4:2:2 */
    { AV_CODEC_ID_V308,   MKTAG('v', '3', '0', '8') }, /* UNCOMPRESSED  8BIT 4:4:4 */
    { AV_CODEC_ID_V408,   MKTAG('v', '4', '0', '8') }, /* UNCOMPRESSED  8BIT 4:4:4:4 */
    { AV_CODEC_ID_V410,   MKTAG('v', '4', '1', '0') }, /* UNCOMPRESSED 10BIT 4:4:4 */
    { AV_CODEC_ID_Y41P,   MKTAG('Y', '4', '1', 'P') }, /* UNCOMPRESSED 12BIT 4:1:1 */
    { AV_CODEC_ID_YUV4,   MKTAG('y', 'u', 'v', '4') }, /* libquicktime packed yuv420p */

    { AV_CODEC_ID_MJPEG,  MKTAG('j', 'p', 'e', 'g') }, /* PhotoJPEG */
    { AV_CODEC_ID_MJPEG,  MKTAG('m', 'j', 'p', 'a') }, /* Motion-JPEG (format A) */
    { AV_CODEC_ID_MJPEG,  MKTAG('A', 'V', 'D', 'J') }, /* MJPEG with alpha-channel (AVID JFIF meridien compressed) */
/*  { AV_CODEC_ID_MJPEG,  MKTAG('A', 'V', 'R', 'n') }, *//* MJPEG with alpha-channel (AVID ABVB/Truevision NuVista) */
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

    { AV_CODEC_ID_H263, MKTAG('h', '2', '6', '3') }, /* H263 */
    { AV_CODEC_ID_H263, MKTAG('s', '2', '6', '3') }, /* H263 ?? works */

    { AV_CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'c', 'p') }, /* DV PAL */
    { AV_CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'c', ' ') }, /* DV NTSC */
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
    { AV_CODEC_ID_MSRLE,   MKTAG('W', 'R', 'L', 'E') },
    { AV_CODEC_ID_QDRAW,   MKTAG('q', 'd', 'r', 'w') }, /* QuickDraw */

    { AV_CODEC_ID_RAWVIDEO, MKTAG('W', 'R', 'A', 'W') },

    { AV_CODEC_ID_H264, MKTAG('a', 'v', 'c', '1') }, /* AVC-1/H.264 */
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

    { AV_CODEC_ID_MPEG1VIDEO, MKTAG('m', '1', 'v', '1') }, /* Apple MPEG-1 Camcorder */
    { AV_CODEC_ID_MPEG1VIDEO, MKTAG('m', 'p', 'e', 'g') }, /* MPEG */
    { AV_CODEC_ID_MPEG1VIDEO, MKTAG('m', '1', 'v', ' ') },
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('m', '2', 'v', '1') }, /* Apple MPEG-2 Camcorder */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('h', 'd', 'v', '1') }, /* MPEG2 HDV 720p30 */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('h', 'd', 'v', '2') }, /* MPEG2 HDV 1080i60 */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('h', 'd', 'v', '3') }, /* MPEG2 HDV 1080i50 */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('h', 'd', 'v', '4') }, /* MPEG2 HDV 720p24 */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('h', 'd', 'v', '5') }, /* MPEG2 HDV 720p25 */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('h', 'd', 'v', '6') }, /* MPEG2 HDV 1080p24 */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('h', 'd', 'v', '7') }, /* MPEG2 HDV 1080p25 */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('h', 'd', 'v', '8') }, /* MPEG2 HDV 1080p30 */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('h', 'd', 'v', '9') }, /* MPEG2 HDV 720p60 JVC */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('h', 'd', 'v', 'a') }, /* MPEG2 HDV 720p50 */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('m', 'x', '5', 'n') }, /* MPEG2 IMX NTSC 525/60 50mb/s produced by FCP */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('m', 'x', '5', 'p') }, /* MPEG2 IMX PAL 625/50 50mb/s produced by FCP */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('m', 'x', '4', 'n') }, /* MPEG2 IMX NTSC 525/60 40mb/s produced by FCP */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('m', 'x', '4', 'p') }, /* MPEG2 IMX PAL 625/50 40mb/s produced by FCP */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('m', 'x', '3', 'n') }, /* MPEG2 IMX NTSC 525/60 30mb/s produced by FCP */
    { AV_CODEC_ID_MPEG2VIDEO, MKTAG('m', 'x', '3', 'p') }, /* MPEG2 IMX PAL 625/50 30mb/s produced by FCP */
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
//  { AV_CODEC_ID_FLV1,      MKTAG('H', '2', '6', '3') }, /* Flash Media Server */
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
    { AV_CODEC_ID_FLIC,   MKTAG('f', 'l', 'i', 'c') },

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
    { AV_CODEC_ID_MP3,             0x6D730055                },
    { AV_CODEC_ID_NELLYMOSER,      MKTAG('n', 'm', 'o', 's') }, /* Flash Media Server */
    { AV_CODEC_ID_PCM_ALAW,        MKTAG('a', 'l', 'a', 'w') },
    { AV_CODEC_ID_PCM_F32BE,       MKTAG('f', 'l', '3', '2') },
    { AV_CODEC_ID_PCM_F32LE,       MKTAG('f', 'l', '3', '2') },
    { AV_CODEC_ID_PCM_F64BE,       MKTAG('f', 'l', '6', '4') },
    { AV_CODEC_ID_PCM_F64LE,       MKTAG('f', 'l', '6', '4') },
    { AV_CODEC_ID_PCM_MULAW,       MKTAG('u', 'l', 'a', 'w') },
    { AV_CODEC_ID_PCM_S16BE,       MKTAG('t', 'w', 'o', 's') },
    { AV_CODEC_ID_PCM_S16LE,       MKTAG('s', 'o', 'w', 't') },
    { AV_CODEC_ID_PCM_S16LE,       MKTAG('l', 'p', 'c', 'm') },
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
    { AV_CODEC_ID_WMAV2,           MKTAG('W', 'M', 'A', '2') },
    { AV_CODEC_ID_NONE, 0 },
};

const AVCodecTag ff_codec_movsubtitle_tags[] = {
    { AV_CODEC_ID_MOV_TEXT, MKTAG('t', 'e', 'x', 't') },
    { AV_CODEC_ID_MOV_TEXT, MKTAG('t', 'x', '3', 'g') },
    { AV_CODEC_ID_EIA_608,  MKTAG('c', '6', '0', '8') },
    { AV_CODEC_ID_NONE, 0 },
};

/* map numeric codes from mdhd atom to ISO 639 */
/* cf. QTFileFormat.pdf p253, qtff.pdf p205 */
/* http://developer.apple.com/documentation/mac/Text/Text-368.html */
/* deprecated by putting the code as 3*5bit ascii */
static const char mov_mdhd_language_map[][4] = {
    /* 0-9 */
    "eng", "fra", "ger", "ita", "dut", "sve", "spa", "dan", "por", "nor",
    "heb", "jpn", "ara", "fin", "gre", "ice", "mlt", "tur", "hr "/*scr*/, "chi"/*ace?*/,
    "urd", "hin", "tha", "kor", "lit", "pol", "hun", "est", "lav",    "",
    "fo ",    "", "rus", "chi",    "", "iri", "alb", "ron", "ces", "slk",
    "slv", "yid", "sr ", "mac", "bul", "ukr", "bel", "uzb", "kaz", "aze",
    /*?*/
    "aze", "arm", "geo", "mol", "kir", "tgk", "tuk", "mon",    "", "pus",
    "kur", "kas", "snd", "tib", "nep", "san", "mar", "ben", "asm", "guj",
    "pa ", "ori", "mal", "kan", "tam", "tel",    "", "bur", "khm", "lao",
    /*                   roman? arabic? */
    "vie", "ind", "tgl", "may", "may", "amh", "tir", "orm", "som", "swa",
    /*==rundi?*/
       "", "run",    "", "mlg", "epo",    "",    "",    "",    "",    "",
    /* 100 */
       "",    "",    "",    "",    "",    "",    "",    "",    "",    "",
       "",    "",    "",    "",    "",    "",    "",    "",    "",    "",
       "",    "",    "",    "",    "",    "",    "",    "", "wel", "baq",
    "cat", "lat", "que", "grn", "aym", "tat", "uig", "dzo", "jav"
};

int ff_mov_iso639_to_lang(const char lang[4], int mp4)
{
    int i, code = 0;

    /* old way, only for QT? */
    for (i = 0; lang[0] && !mp4 && i < FF_ARRAY_ELEMS(mov_mdhd_language_map); i++) {
        if (!strcmp(lang, mov_mdhd_language_map[i]))
            return i;
    }
    /* XXX:can we do that in mov too? */
    if (!mp4)
        return -1;
    /* handle undefined as such */
    if (lang[0] == '\0')
        lang = "und";
    /* 5bit ascii */
    for (i = 0; i < 3; i++) {
        uint8_t c = lang[i];
        c -= 0x60;
        if (c > 0x1f)
            return -1;
        code <<= 5;
        code |= c;
    }
    return code;
}

int ff_mov_lang_to_iso639(unsigned code, char to[4])
{
    int i;
    memset(to, 0, 4);
    /* is it the mangled iso code? */
    /* see http://www.geocities.com/xhelmboyx/quicktime/formats/mp4-layout.txt */
    if (code >= 0x400 && code != 0x7fff) {
        for (i = 2; i >= 0; i--) {
            to[i] = 0x60 + (code & 0x1f);
            code >>= 5;
        }
        return 1;
    }
    /* old fashion apple lang code */
    if (code >= FF_ARRAY_ELEMS(mov_mdhd_language_map))
        return 0;
    if (!mov_mdhd_language_map[code][0])
        return 0;
    memcpy(to, mov_mdhd_language_map[code], 4);
    return 1;
}

int ff_mp4_read_descr_len(AVIOContext *pb)
{
    int len = 0;
    int count = 4;
    while (count--) {
        int c = avio_r8(pb);
        len = (len << 7) | (c & 0x7f);
        if (!(c & 0x80))
            break;
    }
    return len;
}

int ff_mp4_read_descr(AVFormatContext *fc, AVIOContext *pb, int *tag)
{
    int len;
    *tag = avio_r8(pb);
    len = ff_mp4_read_descr_len(pb);
    av_dlog(fc, "MPEG4 description: tag=0x%02x len=%d\n", *tag, len);
    return len;
}

void ff_mp4_parse_es_descr(AVIOContext *pb, int *es_id)
{
     int flags;
     if (es_id) *es_id = avio_rb16(pb);
     else                avio_rb16(pb);
     flags = avio_r8(pb);
     if (flags & 0x80) //streamDependenceFlag
         avio_rb16(pb);
     if (flags & 0x40) { //URL_Flag
         int len = avio_r8(pb);
         avio_skip(pb, len);
     }
     if (flags & 0x20) //OCRstreamFlag
         avio_rb16(pb);
}

static const AVCodecTag mp4_audio_types[] = {
    { AV_CODEC_ID_MP3ON4, AOT_PS   }, /* old mp3on4 draft */
    { AV_CODEC_ID_MP3ON4, AOT_L1   }, /* layer 1 */
    { AV_CODEC_ID_MP3ON4, AOT_L2   }, /* layer 2 */
    { AV_CODEC_ID_MP3ON4, AOT_L3   }, /* layer 3 */
    { AV_CODEC_ID_MP4ALS, AOT_ALS  }, /* MPEG-4 ALS */
    { AV_CODEC_ID_NONE,   AOT_NULL },
};

int ff_mp4_read_dec_config_descr(AVFormatContext *fc, AVStream *st, AVIOContext *pb)
{
    int len, tag;
    int object_type_id = avio_r8(pb);
    avio_r8(pb); /* stream type */
    avio_rb24(pb); /* buffer size db */
    avio_rb32(pb); /* max bitrate */
    avio_rb32(pb); /* avg bitrate */

    st->codec->codec_id= ff_codec_get_id(ff_mp4_obj_type, object_type_id);
    av_dlog(fc, "esds object type id 0x%02x\n", object_type_id);
    len = ff_mp4_read_descr(fc, pb, &tag);
    if (tag == MP4DecSpecificDescrTag) {
        av_dlog(fc, "Specific MPEG4 header len=%d\n", len);
        if (!len || (uint64_t)len > (1<<30))
            return -1;
        av_free(st->codec->extradata);
        st->codec->extradata = av_mallocz(len + FF_INPUT_BUFFER_PADDING_SIZE);
        if (!st->codec->extradata)
            return AVERROR(ENOMEM);
        avio_read(pb, st->codec->extradata, len);
        st->codec->extradata_size = len;
        if (st->codec->codec_id == AV_CODEC_ID_AAC) {
            MPEG4AudioConfig cfg;
            avpriv_mpeg4audio_get_config(&cfg, st->codec->extradata,
                                         st->codec->extradata_size * 8, 1);
            st->codec->channels = cfg.channels;
            if (cfg.object_type == 29 && cfg.sampling_index < 3) // old mp3on4
                st->codec->sample_rate = avpriv_mpa_freq_tab[cfg.sampling_index];
            else if (cfg.ext_sample_rate)
                st->codec->sample_rate = cfg.ext_sample_rate;
            else
                st->codec->sample_rate = cfg.sample_rate;
            av_dlog(fc, "mp4a config channels %d obj %d ext obj %d "
                    "sample rate %d ext sample rate %d\n", st->codec->channels,
                    cfg.object_type, cfg.ext_object_type,
                    cfg.sample_rate, cfg.ext_sample_rate);
            if (!(st->codec->codec_id = ff_codec_get_id(mp4_audio_types,
                                                        cfg.object_type)))
                st->codec->codec_id = AV_CODEC_ID_AAC;
        }
    }
    return 0;
}

typedef struct MovChannelLayout {
    int64_t  channel_layout;
    uint32_t layout_tag;
} MovChannelLayout;

static const MovChannelLayout mov_channel_layout[] = {
    { AV_CH_LAYOUT_MONO,                         (100<<16) | 1}, // kCAFChannelLayoutTag_Mono
    { AV_CH_LAYOUT_STEREO,                       (101<<16) | 2}, // kCAFChannelLayoutTag_Stereo
    { AV_CH_LAYOUT_STEREO,                       (102<<16) | 2}, // kCAFChannelLayoutTag_StereoHeadphones
    { AV_CH_LAYOUT_2_1,                          (131<<16) | 3}, // kCAFChannelLayoutTag_ITU_2_1
    { AV_CH_LAYOUT_QUAD,                         (132<<16) | 4}, // kCAFChannelLayoutTag_ITU_2_2
    { AV_CH_LAYOUT_2_2,                          (132<<16) | 4}, // kCAFChannelLayoutTag_ITU_2_2
    { AV_CH_LAYOUT_QUAD,                         (108<<16) | 4}, // kCAFChannelLayoutTag_Quadraphonic
    { AV_CH_LAYOUT_SURROUND,                     (113<<16) | 3}, // kCAFChannelLayoutTag_MPEG_3_0_A
    { AV_CH_LAYOUT_4POINT0,                      (115<<16) | 4}, // kCAFChannelLayoutTag_MPEG_4_0_A
    { AV_CH_LAYOUT_5POINT0_BACK,                 (117<<16) | 5}, // kCAFChannelLayoutTag_MPEG_5_0_A
    { AV_CH_LAYOUT_5POINT0,                      (117<<16) | 5}, // kCAFChannelLayoutTag_MPEG_5_0_A
    { AV_CH_LAYOUT_5POINT1_BACK,                 (121<<16) | 6}, // kCAFChannelLayoutTag_MPEG_5_1_A
    { AV_CH_LAYOUT_5POINT1,                      (121<<16) | 6}, // kCAFChannelLayoutTag_MPEG_5_1_A
    { AV_CH_LAYOUT_7POINT1,                      (128<<16) | 8}, // kCAFChannelLayoutTag_MPEG_7_1_C
    { AV_CH_LAYOUT_7POINT1_WIDE,                 (126<<16) | 8}, // kCAFChannelLayoutTag_MPEG_7_1_A
    { AV_CH_LAYOUT_5POINT1_BACK|AV_CH_LAYOUT_STEREO_DOWNMIX, (130<<16) | 8}, // kCAFChannelLayoutTag_SMPTE_DTV
    { AV_CH_LAYOUT_STEREO|AV_CH_LOW_FREQUENCY,   (133<<16) | 3}, // kCAFChannelLayoutTag_DVD_4
    { AV_CH_LAYOUT_2_1|AV_CH_LOW_FREQUENCY,      (134<<16) | 4}, // kCAFChannelLayoutTag_DVD_5
    { AV_CH_LAYOUT_QUAD|AV_CH_LOW_FREQUENCY,     (135<<16) | 4}, // kCAFChannelLayoutTag_DVD_6
    { AV_CH_LAYOUT_2_2|AV_CH_LOW_FREQUENCY,      (135<<16) | 4}, // kCAFChannelLayoutTag_DVD_6
    { AV_CH_LAYOUT_SURROUND|AV_CH_LOW_FREQUENCY, (136<<16) | 4}, // kCAFChannelLayoutTag_DVD_10
    { AV_CH_LAYOUT_4POINT0|AV_CH_LOW_FREQUENCY,  (137<<16) | 5}, // kCAFChannelLayoutTag_DVD_11
    { 0, 0},
};
#if 0
int ff_mov_read_chan(AVFormatContext *s, AVStream *st, int64_t size)
{
    AVCodecContext *codec= st->codec;
    uint32_t layout_tag;
    AVIOContext *pb = s->pb;
    const MovChannelLayout *layouts = mov_channel_layout;

    if (size < 12)
        return AVERROR_INVALIDDATA;

    layout_tag = avio_rb32(pb);
    size -= 4;
    if (layout_tag == 0) { // kCAFChannelLayoutTag_UseChannelDescriptions
        // Channel descriptions not implemented
        av_log_ask_for_sample(s, "Unimplemented container channel layout.\n");
        avio_skip(pb, size);
        return 0;
    }
    if (layout_tag == 0x10000) { // kCAFChannelLayoutTag_UseChannelBitmap
        codec->channel_layout = avio_rb32(pb);
        size -= 4;
        avio_skip(pb, size);
        return 0;
    }
    while (layouts->channel_layout) {
        if (layout_tag == layouts->layout_tag) {
            codec->channel_layout = layouts->channel_layout;
            break;
        }
        layouts++;
    }
    if (!codec->channel_layout)
        av_log(s, AV_LOG_WARNING, "Unknown container channel layout.\n");
    avio_skip(pb, size);

    return 0;
}
#endif

void ff_mov_write_chan(AVIOContext *pb, int64_t channel_layout)
{
    const MovChannelLayout *layouts;
    uint32_t layout_tag = 0;

    for (layouts = mov_channel_layout; layouts->channel_layout; layouts++)
        if (channel_layout == layouts->channel_layout) {
            layout_tag = layouts->layout_tag;
            break;
        }

    if (layout_tag) {
        avio_wb32(pb, layout_tag); // mChannelLayoutTag
        avio_wb32(pb, 0);          // mChannelBitmap
    } else {
        avio_wb32(pb, 0x10000);    // kCAFChannelLayoutTag_UseChannelBitmap
        avio_wb32(pb, channel_layout);
    }
    avio_wb32(pb, 0);              // mNumberChannelDescriptions
}

