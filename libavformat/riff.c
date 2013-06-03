/*
 * RIFF common functions and data
 * Copyright (c) 2000 Fabrice Bellard
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

#include "libavutil/mathematics.h"
#include "libavcodec/avcodec.h"
#include "avformat.h"
#include "avio_internal.h"
#include "riff.h"
#include "libavcodec/bytestream.h"
#include "libavutil/avassert.h"

/* Note: When encoding, the first matching tag is used, so order is
 * important if multiple tags are possible for a given codec.
 * Note also that this list is used for more than just riff, other
 * files use it as well.
 */
const AVCodecTag ff_codec_bmp_tags[] = {
    { AV_CODEC_ID_H264,         MKTAG('H', '2', '6', '4') },
    { AV_CODEC_ID_H264,         MKTAG('h', '2', '6', '4') },
    { AV_CODEC_ID_H264,         MKTAG('X', '2', '6', '4') },
    { AV_CODEC_ID_H264,         MKTAG('x', '2', '6', '4') },
    { AV_CODEC_ID_H264,         MKTAG('a', 'v', 'c', '1') },
    { AV_CODEC_ID_H264,         MKTAG('D', 'A', 'V', 'C') },
    { AV_CODEC_ID_H264,         MKTAG('S', 'M', 'V', '2') },
    { AV_CODEC_ID_H264,         MKTAG('V', 'S', 'S', 'H') },
    { AV_CODEC_ID_H264,         MKTAG('Q', '2', '6', '4') }, /* QNAP surveillance system */
    { AV_CODEC_ID_H264,         MKTAG('V', '2', '6', '4') },
    { AV_CODEC_ID_H263,         MKTAG('H', '2', '6', '3') },
    { AV_CODEC_ID_H263,         MKTAG('X', '2', '6', '3') },
    { AV_CODEC_ID_H263,         MKTAG('T', '2', '6', '3') },
    { AV_CODEC_ID_H263,         MKTAG('L', '2', '6', '3') },
    { AV_CODEC_ID_H263,         MKTAG('V', 'X', '1', 'K') },
    { AV_CODEC_ID_H263,         MKTAG('Z', 'y', 'G', 'o') },
    { AV_CODEC_ID_H263,         MKTAG('M', '2', '6', '3') },
    { AV_CODEC_ID_H263,         MKTAG('l', 's', 'v', 'm') },
    { AV_CODEC_ID_H263P,        MKTAG('H', '2', '6', '3') },
    { AV_CODEC_ID_H263I,        MKTAG('I', '2', '6', '3') }, /* Intel H.263 */
    { AV_CODEC_ID_H261,         MKTAG('H', '2', '6', '1') },
    { AV_CODEC_ID_H263,         MKTAG('U', '2', '6', '3') },
    { AV_CODEC_ID_MPEG4,        MKTAG('F', 'M', 'P', '4') },
    { AV_CODEC_ID_MPEG4,        MKTAG('D', 'I', 'V', 'X') },
    { AV_CODEC_ID_MPEG4,        MKTAG('D', 'X', '5', '0') },
    { AV_CODEC_ID_MPEG4,        MKTAG('X', 'V', 'I', 'D') },
    { AV_CODEC_ID_MPEG4,        MKTAG('M', 'P', '4', 'S') },
    { AV_CODEC_ID_MPEG4,        MKTAG('M', '4', 'S', '2') },
    /* some broken AVIs use this */
    { AV_CODEC_ID_MPEG4,        MKTAG( 4 ,  0 ,  0 ,  0 ) },
    /* some broken AVIs use this */
    { AV_CODEC_ID_MPEG4,        MKTAG('Z', 'M', 'P', '4') },
    { AV_CODEC_ID_MPEG4,        MKTAG('D', 'I', 'V', '1') },
    { AV_CODEC_ID_MPEG4,        MKTAG('B', 'L', 'Z', '0') },
    { AV_CODEC_ID_MPEG4,        MKTAG('m', 'p', '4', 'v') },
    { AV_CODEC_ID_MPEG4,        MKTAG('U', 'M', 'P', '4') },
    { AV_CODEC_ID_MPEG4,        MKTAG('W', 'V', '1', 'F') },
    { AV_CODEC_ID_MPEG4,        MKTAG('S', 'E', 'D', 'G') },
    { AV_CODEC_ID_MPEG4,        MKTAG('R', 'M', 'P', '4') },
    { AV_CODEC_ID_MPEG4,        MKTAG('3', 'I', 'V', '2') },
    /* WaWv MPEG-4 Video Codec */
    { AV_CODEC_ID_MPEG4,        MKTAG('W', 'A', 'W', 'V') },
    { AV_CODEC_ID_MPEG4,        MKTAG('F', 'F', 'D', 'S') },
    { AV_CODEC_ID_MPEG4,        MKTAG('F', 'V', 'F', 'W') },
    { AV_CODEC_ID_MPEG4,        MKTAG('D', 'C', 'O', 'D') },
    { AV_CODEC_ID_MPEG4,        MKTAG('M', 'V', 'X', 'M') },
    { AV_CODEC_ID_MPEG4,        MKTAG('P', 'M', '4', 'V') },
    { AV_CODEC_ID_MPEG4,        MKTAG('S', 'M', 'P', '4') },
    { AV_CODEC_ID_MPEG4,        MKTAG('D', 'X', 'G', 'M') },
    { AV_CODEC_ID_MPEG4,        MKTAG('V', 'I', 'D', 'M') },
    { AV_CODEC_ID_MPEG4,        MKTAG('M', '4', 'T', '3') },
    { AV_CODEC_ID_MPEG4,        MKTAG('G', 'E', 'O', 'X') },
    /* flipped video */
    { AV_CODEC_ID_MPEG4,        MKTAG('H', 'D', 'X', '4') },
    { AV_CODEC_ID_MPEG4,        MKTAG('D', 'M', 'K', '2') },
    { AV_CODEC_ID_MPEG4,        MKTAG('D', 'I', 'G', 'I') },
    { AV_CODEC_ID_MPEG4,        MKTAG('I', 'N', 'M', 'C') },
    /* Ephv MPEG-4 */
    { AV_CODEC_ID_MPEG4,        MKTAG('E', 'P', 'H', 'V') },
    { AV_CODEC_ID_MPEG4,        MKTAG('E', 'M', '4', 'A') },
    /* Divio MPEG-4 */
    { AV_CODEC_ID_MPEG4,        MKTAG('M', '4', 'C', 'C') },
    { AV_CODEC_ID_MPEG4,        MKTAG('S', 'N', '4', '0') },
    { AV_CODEC_ID_MPEG4,        MKTAG('V', 'S', 'P', 'X') },
    { AV_CODEC_ID_MPEG4,        MKTAG('U', 'L', 'D', 'X') },
    { AV_CODEC_ID_MPEG4,        MKTAG('G', 'E', 'O', 'V') },
    /* Samsung SHR-6040 */
    { AV_CODEC_ID_MPEG4,        MKTAG('S', 'I', 'P', 'P') },
    { AV_CODEC_ID_MPEG4,        MKTAG('S', 'M', '4', 'V') },
    { AV_CODEC_ID_MPEG4,        MKTAG('X', 'V', 'I', 'X') },
    { AV_CODEC_ID_MPEG4,        MKTAG('D', 'r', 'e', 'X') },
    { AV_CODEC_ID_MPEG4,        MKTAG('Q', 'M', 'P', '4') }, /* QNAP Systems */
    { AV_CODEC_ID_MPEG4,        MKTAG('P', 'L', 'V', '1') }, /* Pelco DVR MPEG-4 */
    { AV_CODEC_ID_MSMPEG4V3,    MKTAG('M', 'P', '4', '3') },
    { AV_CODEC_ID_MSMPEG4V3,    MKTAG('D', 'I', 'V', '3') },
    { AV_CODEC_ID_MSMPEG4V3,    MKTAG('M', 'P', 'G', '3') },
    { AV_CODEC_ID_MSMPEG4V3,    MKTAG('D', 'I', 'V', '5') },
    { AV_CODEC_ID_MSMPEG4V3,    MKTAG('D', 'I', 'V', '6') },
    { AV_CODEC_ID_MSMPEG4V3,    MKTAG('D', 'I', 'V', '4') },
    { AV_CODEC_ID_MSMPEG4V3,    MKTAG('D', 'V', 'X', '3') },
    { AV_CODEC_ID_MSMPEG4V3,    MKTAG('A', 'P', '4', '1') },
    { AV_CODEC_ID_MSMPEG4V3,    MKTAG('C', 'O', 'L', '1') },
    { AV_CODEC_ID_MSMPEG4V3,    MKTAG('C', 'O', 'L', '0') },
    { AV_CODEC_ID_MSMPEG4V2,    MKTAG('M', 'P', '4', '2') },
    { AV_CODEC_ID_MSMPEG4V2,    MKTAG('D', 'I', 'V', '2') },
    { AV_CODEC_ID_MSMPEG4V1,    MKTAG('M', 'P', 'G', '4') },
    { AV_CODEC_ID_MSMPEG4V1,    MKTAG('M', 'P', '4', '1') },
    { AV_CODEC_ID_WMV1,         MKTAG('W', 'M', 'V', '1') },
    { AV_CODEC_ID_WMV2,         MKTAG('W', 'M', 'V', '2') },
    { AV_CODEC_ID_WMV2,         MKTAG('G', 'X', 'V', 'E') },
    { AV_CODEC_ID_DVVIDEO,      MKTAG('d', 'v', 's', 'd') },
    { AV_CODEC_ID_DVVIDEO,      MKTAG('d', 'v', 'h', 'd') },
    { AV_CODEC_ID_DVVIDEO,      MKTAG('d', 'v', 'h', '1') },
    { AV_CODEC_ID_DVVIDEO,      MKTAG('d', 'v', 's', 'l') },
    { AV_CODEC_ID_DVVIDEO,      MKTAG('d', 'v', '2', '5') },
    { AV_CODEC_ID_DVVIDEO,      MKTAG('d', 'v', '5', '0') },
    /* Canopus DV */
    { AV_CODEC_ID_DVVIDEO,      MKTAG('c', 'd', 'v', 'c') },
    /* Canopus DV */
    { AV_CODEC_ID_DVVIDEO,      MKTAG('C', 'D', 'V', 'H') },
    /* Canopus DV */
    { AV_CODEC_ID_DVVIDEO,      MKTAG('C', 'D', 'V', '5') },
    { AV_CODEC_ID_DVVIDEO,      MKTAG('d', 'v', 'c', ' ') },
    { AV_CODEC_ID_DVVIDEO,      MKTAG('d', 'v', 'c', 's') },
    { AV_CODEC_ID_DVVIDEO,      MKTAG('d', 'v', 'h', '1') },
    { AV_CODEC_ID_DVVIDEO,      MKTAG('d', 'v', 'i', 's') },
    { AV_CODEC_ID_DVVIDEO,      MKTAG('p', 'd', 'v', 'c') },
    { AV_CODEC_ID_DVVIDEO,      MKTAG('S', 'L', '2', '5') },
    { AV_CODEC_ID_DVVIDEO,      MKTAG('S', 'L', 'D', 'V') },
    { AV_CODEC_ID_MPEG1VIDEO,   MKTAG('m', 'p', 'g', '1') },
    { AV_CODEC_ID_MPEG1VIDEO,   MKTAG('m', 'p', 'g', '2') },
    { AV_CODEC_ID_MPEG2VIDEO,   MKTAG('m', 'p', 'g', '2') },
    { AV_CODEC_ID_MPEG2VIDEO,   MKTAG('M', 'P', 'E', 'G') },
    { AV_CODEC_ID_MPEG1VIDEO,   MKTAG('P', 'I', 'M', '1') },
    { AV_CODEC_ID_MPEG2VIDEO,   MKTAG('P', 'I', 'M', '2') },
    { AV_CODEC_ID_MPEG1VIDEO,   MKTAG('V', 'C', 'R', '2') },
    { AV_CODEC_ID_MPEG1VIDEO,   MKTAG( 1 ,  0 ,  0 ,  16) },
    { AV_CODEC_ID_MPEG2VIDEO,   MKTAG( 2 ,  0 ,  0 ,  16) },
    { AV_CODEC_ID_MPEG4,        MKTAG( 4 ,  0 ,  0 ,  16) },
    { AV_CODEC_ID_MPEG2VIDEO,   MKTAG('D', 'V', 'R', ' ') },
    { AV_CODEC_ID_MPEG2VIDEO,   MKTAG('M', 'M', 'E', 'S') },
    /* Lead MPEG-2 in AVI */
    { AV_CODEC_ID_MPEG2VIDEO,   MKTAG('L', 'M', 'P', '2') },
    { AV_CODEC_ID_MPEG2VIDEO,   MKTAG('s', 'l', 'i', 'f') },
    { AV_CODEC_ID_MPEG2VIDEO,   MKTAG('E', 'M', '2', 'V') },
    /* Matrox MPEG-2 intra-only */
    { AV_CODEC_ID_MPEG2VIDEO,   MKTAG('M', '7', '0', '1') },
    { AV_CODEC_ID_MPEG2VIDEO,   MKTAG('m', 'p', 'g', 'v') },
    { AV_CODEC_ID_MPEG1VIDEO,   MKTAG('B', 'W', '1', '0') },
    { AV_CODEC_ID_MPEG1VIDEO,   MKTAG('X', 'M', 'P', 'G') }, /* Xing MPEG intra only */
    { AV_CODEC_ID_MJPEG,        MKTAG('M', 'J', 'P', 'G') },
    { AV_CODEC_ID_MJPEG,        MKTAG('L', 'J', 'P', 'G') },
    { AV_CODEC_ID_MJPEG,        MKTAG('d', 'm', 'b', '1') },
    { AV_CODEC_ID_MJPEG,        MKTAG('m', 'j', 'p', 'a') },
    { AV_CODEC_ID_LJPEG,        MKTAG('L', 'J', 'P', 'G') },
    /* Pegasus lossless JPEG */
    { AV_CODEC_ID_MJPEG,        MKTAG('J', 'P', 'G', 'L') },
    /* JPEG-LS custom FOURCC for AVI - encoder */
    { AV_CODEC_ID_JPEGLS,       MKTAG('M', 'J', 'L', 'S') },
    { AV_CODEC_ID_JPEGLS,       MKTAG('M', 'J', 'P', 'G') },
    /* JPEG-LS custom FOURCC for AVI - decoder */
    { AV_CODEC_ID_MJPEG,        MKTAG('M', 'J', 'L', 'S') },
    { AV_CODEC_ID_MJPEG,        MKTAG('j', 'p', 'e', 'g') },
    { AV_CODEC_ID_MJPEG,        MKTAG('I', 'J', 'P', 'G') },
    { AV_CODEC_ID_AVRN,         MKTAG('A', 'V', 'R', 'n') },
    { AV_CODEC_ID_MJPEG,        MKTAG('A', 'C', 'D', 'V') },
    { AV_CODEC_ID_MJPEG,        MKTAG('Q', 'I', 'V', 'G') },
    /* SL M-JPEG */
    { AV_CODEC_ID_MJPEG,        MKTAG('S', 'L', 'M', 'J') },
    /* Creative Webcam JPEG */
    { AV_CODEC_ID_MJPEG,        MKTAG('C', 'J', 'P', 'G') },
    /* Intel JPEG Library Video Codec */
    { AV_CODEC_ID_MJPEG,        MKTAG('I', 'J', 'L', 'V') },
    /* Midvid JPEG Video Codec */
    { AV_CODEC_ID_MJPEG,        MKTAG('M', 'V', 'J', 'P') },
    { AV_CODEC_ID_MJPEG,        MKTAG('A', 'V', 'I', '1') },
    { AV_CODEC_ID_MJPEG,        MKTAG('A', 'V', 'I', '2') },
    { AV_CODEC_ID_MJPEG,        MKTAG('M', 'T', 'S', 'J') },
    /* Paradigm Matrix M-JPEG Codec */
    { AV_CODEC_ID_MJPEG,        MKTAG('Z', 'J', 'P', 'G') },
    { AV_CODEC_ID_MJPEG,        MKTAG('M', 'M', 'J', 'P') },
    { AV_CODEC_ID_HUFFYUV,      MKTAG('H', 'F', 'Y', 'U') },
    { AV_CODEC_ID_FFVHUFF,      MKTAG('F', 'F', 'V', 'H') },
    { AV_CODEC_ID_CYUV,         MKTAG('C', 'Y', 'U', 'V') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG( 0 ,  0 ,  0 ,  0 ) },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG( 3 ,  0 ,  0 ,  0 ) },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('I', '4', '2', '0') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('Y', 'U', 'Y', '2') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('Y', '4', '2', '2') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('V', '4', '2', '2') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('Y', 'U', 'N', 'V') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('U', 'Y', 'N', 'V') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('U', 'Y', 'N', 'Y') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('u', 'y', 'v', '1') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('2', 'V', 'u', '1') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('2', 'v', 'u', 'y') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('y', 'u', 'v', 's') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('y', 'u', 'v', '2') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('P', '4', '2', '2') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('Y', 'V', '1', '2') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('Y', 'V', '1', '6') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('Y', 'V', '2', '4') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('U', 'Y', 'V', 'Y') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('V', 'Y', 'U', 'Y') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('I', 'Y', 'U', 'V') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('Y', '8', '0', '0') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('Y', '8', ' ', ' ') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('H', 'D', 'Y', 'C') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('Y', 'V', 'U', '9') },
    /* SoftLab-NSK VideoTizer */
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('V', 'D', 'T', 'Z') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('Y', '4', '1', '1') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('N', 'V', '1', '2') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('N', 'V', '2', '1') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('Y', '4', '1', 'B') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('Y', '4', '2', 'B') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('Y', 'U', 'V', '9') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('Y', 'V', 'U', '9') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('a', 'u', 'v', '2') },
    { AV_CODEC_ID_RAWVIDEO,     MKTAG('Y', 'V', 'Y', 'U') },
    { AV_CODEC_ID_FRWU,         MKTAG('F', 'R', 'W', 'U') },
    { AV_CODEC_ID_R10K,         MKTAG('R', '1', '0', 'k') },
    { AV_CODEC_ID_R210,         MKTAG('r', '2', '1', '0') },
    { AV_CODEC_ID_V210,         MKTAG('v', '2', '1', '0') },
    { AV_CODEC_ID_V308,         MKTAG('v', '3', '0', '8') },
    { AV_CODEC_ID_V408,         MKTAG('v', '4', '0', '8') },
    { AV_CODEC_ID_AYUV,         MKTAG('A', 'Y', 'U', 'V') },
    { AV_CODEC_ID_V410,         MKTAG('v', '4', '1', '0') },
    { AV_CODEC_ID_YUV4,         MKTAG('y', 'u', 'v', '4') },
    { AV_CODEC_ID_INDEO3,       MKTAG('I', 'V', '3', '1') },
    { AV_CODEC_ID_INDEO3,       MKTAG('I', 'V', '3', '2') },
    { AV_CODEC_ID_INDEO4,       MKTAG('I', 'V', '4', '1') },
    { AV_CODEC_ID_INDEO5,       MKTAG('I', 'V', '5', '0') },
    { AV_CODEC_ID_VP3,          MKTAG('V', 'P', '3', '1') },
    { AV_CODEC_ID_VP3,          MKTAG('V', 'P', '3', '0') },
    { AV_CODEC_ID_VP5,          MKTAG('V', 'P', '5', '0') },
    { AV_CODEC_ID_VP6,          MKTAG('V', 'P', '6', '0') },
    { AV_CODEC_ID_VP6,          MKTAG('V', 'P', '6', '1') },
    { AV_CODEC_ID_VP6,          MKTAG('V', 'P', '6', '2') },
    { AV_CODEC_ID_VP6F,         MKTAG('V', 'P', '6', 'F') },
    { AV_CODEC_ID_VP6F,         MKTAG('F', 'L', 'V', '4') },
    { AV_CODEC_ID_VP8,          MKTAG('V', 'P', '8', '0') },
    { AV_CODEC_ID_ASV1,         MKTAG('A', 'S', 'V', '1') },
    { AV_CODEC_ID_ASV2,         MKTAG('A', 'S', 'V', '2') },
    { AV_CODEC_ID_VCR1,         MKTAG('V', 'C', 'R', '1') },
    { AV_CODEC_ID_FFV1,         MKTAG('F', 'F', 'V', '1') },
    { AV_CODEC_ID_XAN_WC4,      MKTAG('X', 'x', 'a', 'n') },
    { AV_CODEC_ID_MIMIC,        MKTAG('L', 'M', '2', '0') },
    { AV_CODEC_ID_MSRLE,        MKTAG('m', 'r', 'l', 'e') },
    { AV_CODEC_ID_MSRLE,        MKTAG( 1 ,  0 ,  0 ,  0 ) },
    { AV_CODEC_ID_MSRLE,        MKTAG( 2 ,  0 ,  0 ,  0 ) },
    { AV_CODEC_ID_MSVIDEO1,     MKTAG('M', 'S', 'V', 'C') },
    { AV_CODEC_ID_MSVIDEO1,     MKTAG('m', 's', 'v', 'c') },
    { AV_CODEC_ID_MSVIDEO1,     MKTAG('C', 'R', 'A', 'M') },
    { AV_CODEC_ID_MSVIDEO1,     MKTAG('c', 'r', 'a', 'm') },
    { AV_CODEC_ID_MSVIDEO1,     MKTAG('W', 'H', 'A', 'M') },
    { AV_CODEC_ID_MSVIDEO1,     MKTAG('w', 'h', 'a', 'm') },
    { AV_CODEC_ID_CINEPAK,      MKTAG('c', 'v', 'i', 'd') },
    { AV_CODEC_ID_TRUEMOTION1,  MKTAG('D', 'U', 'C', 'K') },
    { AV_CODEC_ID_TRUEMOTION1,  MKTAG('P', 'V', 'E', 'Z') },
    { AV_CODEC_ID_MSZH,         MKTAG('M', 'S', 'Z', 'H') },
    { AV_CODEC_ID_ZLIB,         MKTAG('Z', 'L', 'I', 'B') },
    { AV_CODEC_ID_SNOW,         MKTAG('S', 'N', 'O', 'W') },
    { AV_CODEC_ID_4XM,          MKTAG('4', 'X', 'M', 'V') },
    { AV_CODEC_ID_FLV1,         MKTAG('F', 'L', 'V', '1') },
    { AV_CODEC_ID_FLV1,         MKTAG('S', '2', '6', '3') },
    { AV_CODEC_ID_FLASHSV,      MKTAG('F', 'S', 'V', '1') },
    { AV_CODEC_ID_SVQ1,         MKTAG('s', 'v', 'q', '1') },
    { AV_CODEC_ID_TSCC,         MKTAG('t', 's', 'c', 'c') },
    { AV_CODEC_ID_ULTI,         MKTAG('U', 'L', 'T', 'I') },
    { AV_CODEC_ID_VIXL,         MKTAG('V', 'I', 'X', 'L') },
    { AV_CODEC_ID_QPEG,         MKTAG('Q', 'P', 'E', 'G') },
    { AV_CODEC_ID_QPEG,         MKTAG('Q', '1', '.', '0') },
    { AV_CODEC_ID_QPEG,         MKTAG('Q', '1', '.', '1') },
    { AV_CODEC_ID_WMV3,         MKTAG('W', 'M', 'V', '3') },
    { AV_CODEC_ID_WMV3IMAGE,    MKTAG('W', 'M', 'V', 'P') },
    { AV_CODEC_ID_VC1,          MKTAG('W', 'V', 'C', '1') },
    { AV_CODEC_ID_VC1,          MKTAG('W', 'M', 'V', 'A') },
    { AV_CODEC_ID_VC1IMAGE,     MKTAG('W', 'V', 'P', '2') },
    { AV_CODEC_ID_LOCO,         MKTAG('L', 'O', 'C', 'O') },
    { AV_CODEC_ID_WNV1,         MKTAG('W', 'N', 'V', '1') },
    { AV_CODEC_ID_WNV1,         MKTAG('Y', 'U', 'V', '8') },
    { AV_CODEC_ID_AASC,         MKTAG('A', 'A', 'S', '4') },
    { AV_CODEC_ID_AASC,         MKTAG('A', 'A', 'S', 'C') },
    { AV_CODEC_ID_INDEO2,       MKTAG('R', 'T', '2', '1') },
    { AV_CODEC_ID_FRAPS,        MKTAG('F', 'P', 'S', '1') },
    { AV_CODEC_ID_THEORA,       MKTAG('t', 'h', 'e', 'o') },
    { AV_CODEC_ID_TRUEMOTION2,  MKTAG('T', 'M', '2', '0') },
    { AV_CODEC_ID_CSCD,         MKTAG('C', 'S', 'C', 'D') },
    { AV_CODEC_ID_ZMBV,         MKTAG('Z', 'M', 'B', 'V') },
    { AV_CODEC_ID_KMVC,         MKTAG('K', 'M', 'V', 'C') },
    { AV_CODEC_ID_CAVS,         MKTAG('C', 'A', 'V', 'S') },
    { AV_CODEC_ID_JPEG2000,     MKTAG('m', 'j', 'p', '2') },
    { AV_CODEC_ID_JPEG2000,     MKTAG('M', 'J', '2', 'C') },
    { AV_CODEC_ID_JPEG2000,     MKTAG('L', 'J', '2', 'C') },
    { AV_CODEC_ID_JPEG2000,     MKTAG('L', 'J', '2', 'K') },
    { AV_CODEC_ID_JPEG2000,     MKTAG('I', 'P', 'J', '2') },
    { AV_CODEC_ID_VMNC,         MKTAG('V', 'M', 'n', 'c') },
    { AV_CODEC_ID_TARGA,        MKTAG('t', 'g', 'a', ' ') },
    { AV_CODEC_ID_PNG,          MKTAG('M', 'P', 'N', 'G') },
    { AV_CODEC_ID_PNG,          MKTAG('P', 'N', 'G', '1') },
    { AV_CODEC_ID_CLJR,         MKTAG('C', 'L', 'J', 'R') },
    { AV_CODEC_ID_DIRAC,        MKTAG('d', 'r', 'a', 'c') },
    { AV_CODEC_ID_RPZA,         MKTAG('a', 'z', 'p', 'r') },
    { AV_CODEC_ID_RPZA,         MKTAG('R', 'P', 'Z', 'A') },
    { AV_CODEC_ID_RPZA,         MKTAG('r', 'p', 'z', 'a') },
    { AV_CODEC_ID_SP5X,         MKTAG('S', 'P', '5', '4') },
    { AV_CODEC_ID_AURA,         MKTAG('A', 'U', 'R', 'A') },
    { AV_CODEC_ID_AURA2,        MKTAG('A', 'U', 'R', '2') },
    { AV_CODEC_ID_DPX,          MKTAG('d', 'p', 'x', ' ') },
    { AV_CODEC_ID_KGV1,         MKTAG('K', 'G', 'V', '1') },
    { AV_CODEC_ID_LAGARITH,     MKTAG('L', 'A', 'G', 'S') },
    { AV_CODEC_ID_AMV,          MKTAG('A', 'M', 'V', 'F') },
    { AV_CODEC_ID_UTVIDEO,      MKTAG('U', 'L', 'R', 'A') },
    { AV_CODEC_ID_UTVIDEO,      MKTAG('U', 'L', 'R', 'G') },
    { AV_CODEC_ID_UTVIDEO,      MKTAG('U', 'L', 'Y', '0') },
    { AV_CODEC_ID_UTVIDEO,      MKTAG('U', 'L', 'Y', '2') },
    { AV_CODEC_ID_VBLE,         MKTAG('V', 'B', 'L', 'E') },
    { AV_CODEC_ID_ESCAPE130,    MKTAG('E', '1', '3', '0') },
    { AV_CODEC_ID_DXTORY,       MKTAG('x', 't', 'o', 'r') },
    { AV_CODEC_ID_ZEROCODEC,    MKTAG('Z', 'E', 'C', 'O') },
    { AV_CODEC_ID_Y41P,         MKTAG('Y', '4', '1', 'P') },
    { AV_CODEC_ID_FLIC,         MKTAG('A', 'F', 'L', 'C') },
    { AV_CODEC_ID_EXR,          MKTAG('e', 'x', 'r', ' ') },
    { AV_CODEC_ID_MSS1,         MKTAG('M', 'S', 'S', '1') },
    { AV_CODEC_ID_MSA1,         MKTAG('M', 'S', 'A', '1') },
    { AV_CODEC_ID_TSCC2,        MKTAG('T', 'S', 'C', '2') },
    { AV_CODEC_ID_MTS2,         MKTAG('M', 'T', 'S', '2') },
    { AV_CODEC_ID_CLLC,         MKTAG('C', 'L', 'L', 'C') },
    { AV_CODEC_ID_MSS2,         MKTAG('M', 'S', 'S', '2') },
    { AV_CODEC_ID_SVQ3,         MKTAG('S', 'V', 'Q', '3') },
    { AV_CODEC_ID_012V,         MKTAG('0', '1', '2', 'v') },
    { AV_CODEC_ID_012V,         MKTAG('a', '1', '2', 'v') },
    { AV_CODEC_ID_G2M,          MKTAG('G', '2', 'M', '2') },
    { AV_CODEC_ID_G2M,          MKTAG('G', '2', 'M', '3') },
    { AV_CODEC_ID_G2M,          MKTAG('G', '2', 'M', '4') },
    { AV_CODEC_ID_NONE,         0 }
};

const AVCodecTag ff_codec_wav_tags[] = {
    { AV_CODEC_ID_PCM_S16LE,       0x0001 },
    /* must come after s16le in this list */
    { AV_CODEC_ID_PCM_U8,          0x0001 },
    { AV_CODEC_ID_PCM_S24LE,       0x0001 },
    { AV_CODEC_ID_PCM_S32LE,       0x0001 },
    { AV_CODEC_ID_ADPCM_MS,        0x0002 },
    { AV_CODEC_ID_PCM_F32LE,       0x0003 },
    /* must come after f32le in this list */
    { AV_CODEC_ID_PCM_F64LE,       0x0003 },
    { AV_CODEC_ID_PCM_ALAW,        0x0006 },
    { AV_CODEC_ID_PCM_MULAW,       0x0007 },
    { AV_CODEC_ID_WMAVOICE,        0x000A },
    { AV_CODEC_ID_ADPCM_IMA_OKI,   0x0010 },
    { AV_CODEC_ID_ADPCM_IMA_WAV,   0x0011 },
    /* must come after adpcm_ima_wav in this list */
    { AV_CODEC_ID_PCM_ZORK,        0x0011 },
    { AV_CODEC_ID_ADPCM_IMA_OKI,   0x0017 },
    { AV_CODEC_ID_ADPCM_YAMAHA,    0x0020 },
    { AV_CODEC_ID_TRUESPEECH,      0x0022 },
    { AV_CODEC_ID_GSM_MS,          0x0031 },
    { AV_CODEC_ID_AMR_NB,          0x0038 },  /* rogue format number */
    { AV_CODEC_ID_G723_1,          0x0042 },
    { AV_CODEC_ID_ADPCM_G726,      0x0045 },
    { AV_CODEC_ID_MP2,             0x0050 },
    { AV_CODEC_ID_MP3,             0x0055 },
    { AV_CODEC_ID_AMR_NB,          0x0057 },
    { AV_CODEC_ID_AMR_WB,          0x0058 },
    /* rogue format number */
    { AV_CODEC_ID_ADPCM_IMA_DK4,   0x0061 },
    /* rogue format number */
    { AV_CODEC_ID_ADPCM_IMA_DK3,   0x0062 },
    { AV_CODEC_ID_ADPCM_IMA_WAV,   0x0069 },
    { AV_CODEC_ID_VOXWARE,         0x0075 },
    { AV_CODEC_ID_AAC,             0x00ff },
    { AV_CODEC_ID_SIPR,            0x0130 },
    { AV_CODEC_ID_WMAV1,           0x0160 },
    { AV_CODEC_ID_WMAV2,           0x0161 },
    { AV_CODEC_ID_WMAPRO,          0x0162 },
    { AV_CODEC_ID_WMALOSSLESS,     0x0163 },
    { AV_CODEC_ID_ADPCM_CT,        0x0200 },
    { AV_CODEC_ID_ATRAC3,          0x0270 },
    { AV_CODEC_ID_ADPCM_G722,      0x028F },
    { AV_CODEC_ID_IMC,             0x0401 },
    { AV_CODEC_ID_IAC,             0x0402 },
    { AV_CODEC_ID_GSM_MS,          0x1500 },
    { AV_CODEC_ID_TRUESPEECH,      0x1501 },
    /* ADTS AAC */
    { AV_CODEC_ID_AAC,             0x1600 },
    { AV_CODEC_ID_AAC_LATM,        0x1602 },
    { AV_CODEC_ID_AC3,             0x2000 },
    { AV_CODEC_ID_DTS,             0x2001 },
    { AV_CODEC_ID_SONIC,           0x2048 },
    { AV_CODEC_ID_SONIC_LS,        0x2048 },
    { AV_CODEC_ID_PCM_MULAW,       0x6c75 },
    { AV_CODEC_ID_AAC,             0x706d },
    { AV_CODEC_ID_AAC,             0x4143 },
    { AV_CODEC_ID_G723_1,          0xA100 },
    { AV_CODEC_ID_AAC,             0xA106 },
    { AV_CODEC_ID_SPEEX,           0xA109 },
    { AV_CODEC_ID_FLAC,            0xF1AC },
    { AV_CODEC_ID_ADPCM_SWF,       ('S' << 8) + 'F' },
    /* HACK/FIXME: Does Vorbis in WAV/AVI have an (in)official ID? */
    { AV_CODEC_ID_VORBIS,          ('V' << 8) + 'o' },
    { AV_CODEC_ID_NONE,      0 },
};

const AVCodecGuid ff_codec_wav_guids[] = {
    { AV_CODEC_ID_AC3,      { 0x2C, 0x80, 0x6D, 0xE0, 0x46, 0xDB, 0xCF, 0x11, 0xB4, 0xD1, 0x00, 0x80, 0x5F, 0x6C, 0xBB, 0xEA } },
    { AV_CODEC_ID_ATRAC3P,  { 0xBF, 0xAA, 0x23, 0xE9, 0x58, 0xCB, 0x71, 0x44, 0xA1, 0x19, 0xFF, 0xFA, 0x01, 0xE4, 0xCE, 0x62 } },
    { AV_CODEC_ID_EAC3,     { 0xAF, 0x87, 0xFB, 0xA7, 0x02, 0x2D, 0xFB, 0x42, 0xA4, 0xD4, 0x05, 0xCD, 0x93, 0x84, 0x3B, 0xDD } },
    { AV_CODEC_ID_MP2,      { 0x2B, 0x80, 0x6D, 0xE0, 0x46, 0xDB, 0xCF, 0x11, 0xB4, 0xD1, 0x00, 0x80, 0x5F, 0x6C, 0xBB, 0xEA } },
    { AV_CODEC_ID_NONE }
};

const AVMetadataConv ff_riff_info_conv[] = {
    { "IART", "artist"     },
    { "ICMT", "comment"    },
    { "ICOP", "copyright"  },
    { "ICRD", "date"       },
    { "IGNR", "genre"      },
    { "ILNG", "language"   },
    { "INAM", "title"      },
    { "IPRD", "album"      },
    { "IPRT", "track"      },
    { "ISFT", "encoder"    },
    { "ISMP", "timecode"   },
    { "ITCH", "encoded_by" },
    { 0 },
};

void ff_get_guid(AVIOContext *s, ff_asf_guid *g)
{
    av_assert0(sizeof(*g) == 16); //compiler will optimize this out
    if (avio_read(s, *g, sizeof(*g)) < (int)sizeof(*g))
        memset(*g, 0, sizeof(*g));
}

enum AVCodecID ff_codec_guid_get_id(const AVCodecGuid *guids, ff_asf_guid guid)
{
    int i;
    for (i = 0; guids[i].id != AV_CODEC_ID_NONE; i++)
        if (!ff_guidcmp(guids[i].guid, guid))
            return guids[i].id;
    return AV_CODEC_ID_NONE;
}

#if CONFIG_MUXERS
int64_t ff_start_tag(AVIOContext *pb, const char *tag)
{
    ffio_wfourcc(pb, tag);
    avio_wl32(pb, 0);
    return avio_tell(pb);
}

void ff_end_tag(AVIOContext *pb, int64_t start)
{
    int64_t pos;

    av_assert0((start&1) == 0);

    pos = avio_tell(pb);
    if (pos & 1)
        avio_w8(pb, 0);
    avio_seek(pb, start - 4, SEEK_SET);
    avio_wl32(pb, (uint32_t)(pos - start));
    avio_seek(pb, FFALIGN(pos, 2), SEEK_SET);
}

/* WAVEFORMATEX header */
/* returns the size or -1 on error */
int ff_put_wav_header(AVIOContext *pb, AVCodecContext *enc)
{
    int bps, blkalign, bytespersec, frame_size;
    int hdrsize = 18;
    int waveformatextensible;
    uint8_t temp[256];
    uint8_t *riff_extradata       = temp;
    uint8_t *riff_extradata_start = temp;

    if (!enc->codec_tag || enc->codec_tag > 0xffff)
        return -1;

    /* We use the known constant frame size for the codec if known, otherwise
     * fall back on using AVCodecContext.frame_size, which is not as reliable
     * for indicating packet duration. */
    frame_size = av_get_audio_frame_duration(enc, 0);
    if (!frame_size)
        frame_size = enc->frame_size;

    waveformatextensible = (enc->channels > 2 && enc->channel_layout) ||
                           enc->sample_rate > 48000 ||
                           av_get_bits_per_sample(enc->codec_id) > 16;

    if (waveformatextensible)
        avio_wl16(pb, 0xfffe);
    else
        avio_wl16(pb, enc->codec_tag);

    avio_wl16(pb, enc->channels);
    avio_wl32(pb, enc->sample_rate);
    if (enc->codec_id == AV_CODEC_ID_ATRAC3 ||
        enc->codec_id == AV_CODEC_ID_G723_1 ||
        enc->codec_id == AV_CODEC_ID_MP2    ||
        enc->codec_id == AV_CODEC_ID_MP3    ||
        enc->codec_id == AV_CODEC_ID_GSM_MS) {
        bps = 0;
    } else {
        if (!(bps = av_get_bits_per_sample(enc->codec_id))) {
            if (enc->bits_per_coded_sample)
                bps = enc->bits_per_coded_sample;
            else
                bps = 16;  // default to 16
        }
    }
    if (bps != enc->bits_per_coded_sample && enc->bits_per_coded_sample) {
        av_log(enc, AV_LOG_WARNING,
               "requested bits_per_coded_sample (%d) "
               "and actually stored (%d) differ\n",
               enc->bits_per_coded_sample, bps);
    }

    if (enc->codec_id == AV_CODEC_ID_MP2 ||
        enc->codec_id == AV_CODEC_ID_MP3) {
        /* This is wrong, but it seems many demuxers do not work if this
         * is set correctly. */
        blkalign = frame_size;
        // blkalign = 144 * enc->bit_rate/enc->sample_rate;
    } else if (enc->codec_id == AV_CODEC_ID_AC3) {
        blkalign = 3840;                /* maximum bytes per frame */
    } else if (enc->codec_id == AV_CODEC_ID_AAC) {
        blkalign = 768 * enc->channels; /* maximum bytes per frame */
    } else if (enc->codec_id == AV_CODEC_ID_G723_1) {
        blkalign = 24;
    } else if (enc->block_align != 0) { /* specified by the codec */
        blkalign = enc->block_align;
    } else
        blkalign = bps * enc->channels / av_gcd(8, bps);
    if (enc->codec_id == AV_CODEC_ID_PCM_U8 ||
        enc->codec_id == AV_CODEC_ID_PCM_S24LE ||
        enc->codec_id == AV_CODEC_ID_PCM_S32LE ||
        enc->codec_id == AV_CODEC_ID_PCM_F32LE ||
        enc->codec_id == AV_CODEC_ID_PCM_F64LE ||
        enc->codec_id == AV_CODEC_ID_PCM_S16LE) {
        bytespersec = enc->sample_rate * blkalign;
    } else if (enc->codec_id == AV_CODEC_ID_G723_1) {
        bytespersec = 800;
    } else {
        bytespersec = enc->bit_rate / 8;
    }
    avio_wl32(pb, bytespersec); /* bytes per second */
    avio_wl16(pb, blkalign);    /* block align */
    avio_wl16(pb, bps);         /* bits per sample */
    if (enc->codec_id == AV_CODEC_ID_MP3) {
        hdrsize += 12;
        bytestream_put_le16(&riff_extradata, 1);    /* wID */
        bytestream_put_le32(&riff_extradata, 2);    /* fdwFlags */
        bytestream_put_le16(&riff_extradata, 1152); /* nBlockSize */
        bytestream_put_le16(&riff_extradata, 1);    /* nFramesPerBlock */
        bytestream_put_le16(&riff_extradata, 1393); /* nCodecDelay */
    } else if (enc->codec_id == AV_CODEC_ID_MP2) {
        hdrsize += 22;
        /* fwHeadLayer */
        bytestream_put_le16(&riff_extradata, 2);
        /* dwHeadBitrate */
        bytestream_put_le32(&riff_extradata, enc->bit_rate);
        /* fwHeadMode */
        bytestream_put_le16(&riff_extradata, enc->channels == 2 ? 1 : 8);
        /* fwHeadModeExt */
        bytestream_put_le16(&riff_extradata, 0);
        /* wHeadEmphasis */
        bytestream_put_le16(&riff_extradata, 1);
        /* fwHeadFlags */
        bytestream_put_le16(&riff_extradata, 16);
        /* dwPTSLow */
        bytestream_put_le32(&riff_extradata, 0);
        /* dwPTSHigh */
        bytestream_put_le32(&riff_extradata, 0);
    } else if (enc->codec_id == AV_CODEC_ID_G723_1) {
        hdrsize += 20;
        bytestream_put_le32(&riff_extradata, 0x9ace0002); /* extradata needed for msacm g723.1 codec */
        bytestream_put_le32(&riff_extradata, 0xaea2f732);
        bytestream_put_le16(&riff_extradata, 0xacde);
    } else if (enc->codec_id == AV_CODEC_ID_GSM_MS ||
               enc->codec_id == AV_CODEC_ID_ADPCM_IMA_WAV) {
        hdrsize += 2;
        /* wSamplesPerBlock */
        bytestream_put_le16(&riff_extradata, frame_size);
    } else if (enc->extradata_size) {
        riff_extradata_start = enc->extradata;
        riff_extradata       = enc->extradata + enc->extradata_size;
        hdrsize             += enc->extradata_size;
    }
    /* write WAVEFORMATEXTENSIBLE extensions */
    if (waveformatextensible) {
        hdrsize += 22;
        /* 22 is WAVEFORMATEXTENSIBLE size */
        avio_wl16(pb, riff_extradata - riff_extradata_start + 22);
        /* ValidBitsPerSample || SamplesPerBlock || Reserved */
        avio_wl16(pb, bps);
        /* dwChannelMask */
        avio_wl32(pb, enc->channel_layout);
        /* GUID + next 3 */
        avio_wl32(pb, enc->codec_tag);
        avio_wl32(pb, 0x00100000);
        avio_wl32(pb, 0xAA000080);
        avio_wl32(pb, 0x719B3800);
    } else {
        avio_wl16(pb, riff_extradata - riff_extradata_start); /* cbSize */
    }
    avio_write(pb, riff_extradata_start, riff_extradata - riff_extradata_start);
    if (hdrsize & 1) {
        hdrsize++;
        avio_w8(pb, 0);
    }

    return hdrsize;
}

/* BITMAPINFOHEADER header */
void ff_put_bmp_header(AVIOContext *pb, AVCodecContext *enc,
                       const AVCodecTag *tags, int for_asf)
{
    /* size */
    avio_wl32(pb, 40 + enc->extradata_size);
    avio_wl32(pb, enc->width);
    //We always store RGB TopDown
    avio_wl32(pb, enc->codec_tag ? enc->height : -enc->height);
    /* planes */
    avio_wl16(pb, 1);
    /* depth */
    avio_wl16(pb, enc->bits_per_coded_sample ? enc->bits_per_coded_sample : 24);
    /* compression type */
    avio_wl32(pb, enc->codec_tag);
    avio_wl32(pb, (enc->width * enc->height * (enc->bits_per_coded_sample ? enc->bits_per_coded_sample : 24)+7) / 8);
    avio_wl32(pb, 0);
    avio_wl32(pb, 0);
    avio_wl32(pb, 0);
    avio_wl32(pb, 0);

    avio_write(pb, enc->extradata, enc->extradata_size);

    if (!for_asf && enc->extradata_size & 1)
        avio_w8(pb, 0);
}

void ff_parse_specific_params(AVCodecContext *stream, int *au_rate,
                              int *au_ssize, int *au_scale)
{
    int gcd;
    int audio_frame_size;

    /* We use the known constant frame size for the codec if known, otherwise
     * fall back on using AVCodecContext.frame_size, which is not as reliable
     * for indicating packet duration. */
    audio_frame_size = av_get_audio_frame_duration(stream, 0);
    if (!audio_frame_size)
        audio_frame_size = stream->frame_size;

    *au_ssize = stream->block_align;
    if (audio_frame_size && stream->sample_rate) {
        *au_scale = audio_frame_size;
        *au_rate  = stream->sample_rate;
    } else if (stream->codec_type == AVMEDIA_TYPE_VIDEO ||
               stream->codec_type == AVMEDIA_TYPE_DATA ||
               stream->codec_type == AVMEDIA_TYPE_SUBTITLE) {
        *au_scale = stream->time_base.num;
        *au_rate  = stream->time_base.den;
    } else {
        *au_scale = stream->block_align ? stream->block_align * 8 : 8;
        *au_rate  = stream->bit_rate ? stream->bit_rate :
                    8 * stream->sample_rate;
    }
    gcd        = av_gcd(*au_scale, *au_rate);
    *au_scale /= gcd;
    *au_rate  /= gcd;
}

void ff_riff_write_info_tag(AVIOContext *pb, const char *tag, const char *str)
{
    int len = strlen(str);
    if (len > 0) {
        len++;
        ffio_wfourcc(pb, tag);
        avio_wl32(pb, len);
        avio_put_str(pb, str);
        if (len & 1)
            avio_w8(pb, 0);
    }
}

static const char riff_tags[][5] = {
    "IARL", "IART", "ICMS", "ICMT", "ICOP", "ICRD", "ICRP", "IDIM", "IDPI",
    "IENG", "IGNR", "IKEY", "ILGT", "ILNG", "IMED", "INAM", "IPLT", "IPRD",
    "IPRT", "ISBJ", "ISFT", "ISHP", "ISMP", "ISRC", "ISRF", "ITCH",
    { 0 }
};

static int riff_has_valid_tags(AVFormatContext *s)
{
    int i;

    for (i = 0; *riff_tags[i]; i++)
        if (av_dict_get(s->metadata, riff_tags[i], NULL, AV_DICT_MATCH_CASE))
            return 1;

    return 0;
}

void ff_riff_write_info(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    int i;
    int64_t list_pos;
    AVDictionaryEntry *t = NULL;

    ff_metadata_conv(&s->metadata, ff_riff_info_conv, NULL);

    /* writing empty LIST is not nice and may cause problems */
    if (!riff_has_valid_tags(s))
        return;

    list_pos = ff_start_tag(pb, "LIST");
    ffio_wfourcc(pb, "INFO");
    for (i = 0; *riff_tags[i]; i++)
        if ((t = av_dict_get(s->metadata, riff_tags[i],
                             NULL, AV_DICT_MATCH_CASE)))
            ff_riff_write_info_tag(s->pb, t->key, t->value);
    ff_end_tag(pb, list_pos);
}
#endif /* CONFIG_MUXERS */

#if CONFIG_DEMUXERS
/* We could be given one of the three possible structures here:
 * WAVEFORMAT, PCMWAVEFORMAT or WAVEFORMATEX. Each structure
 * is an expansion of the previous one with the fields added
 * at the bottom. PCMWAVEFORMAT adds 'WORD wBitsPerSample' and
 * WAVEFORMATEX adds 'WORD  cbSize' and basically makes itself
 * an openended structure.
 */

static void parse_waveformatex(AVIOContext *pb, AVCodecContext *c)
{
    ff_asf_guid subformat;
    int bps = avio_rl16(pb);
    if (bps)
        c->bits_per_coded_sample = bps;

    c->channel_layout        = avio_rl32(pb); /* dwChannelMask */

    ff_get_guid(pb, &subformat);
    if (!memcmp(subformat + 4,
                (const uint8_t[]){ FF_MEDIASUBTYPE_BASE_GUID }, 12)) {
        c->codec_tag = AV_RL32(subformat);
        c->codec_id  = ff_wav_codec_get_id(c->codec_tag,
                                           c->bits_per_coded_sample);
    } else {
        c->codec_id = ff_codec_guid_get_id(ff_codec_wav_guids, subformat);
        if (!c->codec_id)
            av_log(c, AV_LOG_WARNING,
                   "unknown subformat:"FF_PRI_GUID"\n",
                   FF_ARG_GUID(subformat));
    }
}

int ff_get_wav_header(AVIOContext *pb, AVCodecContext *codec, int size)
{
    int id;

    id                 = avio_rl16(pb);
    codec->codec_type  = AVMEDIA_TYPE_AUDIO;
    codec->channels    = avio_rl16(pb);
    codec->sample_rate = avio_rl32(pb);
    codec->bit_rate    = avio_rl32(pb) * 8;
    codec->block_align = avio_rl16(pb);
    if (size == 14) {  /* We're dealing with plain vanilla WAVEFORMAT */
        codec->bits_per_coded_sample = 8;
    } else
        codec->bits_per_coded_sample = avio_rl16(pb);
    if (id == 0xFFFE) {
        codec->codec_tag = 0;
    } else {
        codec->codec_tag = id;
        codec->codec_id  = ff_wav_codec_get_id(id,
                                               codec->bits_per_coded_sample);
    }
    if (size >= 18) {  /* We're obviously dealing with WAVEFORMATEX */
        int cbSize = avio_rl16(pb); /* cbSize */
        size  -= 18;
        cbSize = FFMIN(size, cbSize);
        if (cbSize >= 22 && id == 0xfffe) { /* WAVEFORMATEXTENSIBLE */
            parse_waveformatex(pb, codec);
            cbSize -= 22;
            size   -= 22;
        }
        codec->extradata_size = cbSize;
        if (cbSize > 0) {
            av_free(codec->extradata);
            codec->extradata = av_mallocz(codec->extradata_size +
                                          FF_INPUT_BUFFER_PADDING_SIZE);
            if (!codec->extradata)
                return AVERROR(ENOMEM);
            avio_read(pb, codec->extradata, codec->extradata_size);
            size -= cbSize;
        }

        /* It is possible for the chunk to contain garbage at the end */
        if (size > 0)
            avio_skip(pb, size);
    }
    if (codec->codec_id == AV_CODEC_ID_AAC_LATM) {
        /* Channels and sample_rate values are those prior to applying SBR
         * and/or PS. */
        codec->channels    = 0;
        codec->sample_rate = 0;
    }
    /* override bits_per_coded_sample for G.726 */
    if (codec->codec_id == AV_CODEC_ID_ADPCM_G726 && codec->sample_rate)
        codec->bits_per_coded_sample = codec->bit_rate / codec->sample_rate;

    return 0;
}

enum AVCodecID ff_wav_codec_get_id(unsigned int tag, int bps)
{
    enum AVCodecID id;
    id = ff_codec_get_id(ff_codec_wav_tags, tag);
    if (id <= 0)
        return id;

    if (id == AV_CODEC_ID_PCM_S16LE)
        id = ff_get_pcm_codec_id(bps, 0, 0, ~1);
    else if (id == AV_CODEC_ID_PCM_F32LE)
        id = ff_get_pcm_codec_id(bps, 1, 0,  0);

    if (id == AV_CODEC_ID_ADPCM_IMA_WAV && bps == 8)
        id = AV_CODEC_ID_PCM_ZORK;
    return id;
}

int ff_get_bmp_header(AVIOContext *pb, AVStream *st, unsigned *esize)
{
    int tag1;
    if(esize) *esize  = avio_rl32(pb);
    else                avio_rl32(pb);
    st->codec->width  = avio_rl32(pb);
    st->codec->height = (int32_t)avio_rl32(pb);
    avio_rl16(pb); /* planes */
    st->codec->bits_per_coded_sample = avio_rl16(pb); /* depth */
    tag1                             = avio_rl32(pb);
    avio_rl32(pb); /* ImageSize */
    avio_rl32(pb); /* XPelsPerMeter */
    avio_rl32(pb); /* YPelsPerMeter */
    avio_rl32(pb); /* ClrUsed */
    avio_rl32(pb); /* ClrImportant */
    return tag1;
}

int ff_read_riff_info(AVFormatContext *s, int64_t size)
{
    int64_t start, end, cur;
    AVIOContext *pb = s->pb;

    start = avio_tell(pb);
    end   = start + size;

    while ((cur = avio_tell(pb)) >= 0 &&
           cur <= end - 8 /* = tag + size */) {
        uint32_t chunk_code;
        int64_t chunk_size;
        char key[5] = { 0 };
        char *value;

        chunk_code = avio_rl32(pb);
        chunk_size = avio_rl32(pb);
        if (url_feof(pb)) {
            if (chunk_code || chunk_size) {
                av_log(s, AV_LOG_WARNING, "INFO subchunk truncated\n");
                return AVERROR_INVALIDDATA;
            }
            return AVERROR_EOF;
        }
        if (chunk_size > end ||
            end - chunk_size < cur ||
            chunk_size == UINT_MAX) {
            avio_seek(pb, -9, SEEK_CUR);
            chunk_code = avio_rl32(pb);
            chunk_size = avio_rl32(pb);
            if (chunk_size > end || end - chunk_size < cur || chunk_size == UINT_MAX) {
                av_log(s, AV_LOG_WARNING, "too big INFO subchunk\n");
                return AVERROR_INVALIDDATA;
            }
        }

        chunk_size += (chunk_size & 1);

        if (!chunk_code) {
            if (chunk_size)
                avio_skip(pb, chunk_size);
            else if (pb->eof_reached) {
                av_log(s, AV_LOG_WARNING, "truncated file\n");
                return AVERROR_EOF;
            }
            continue;
        }

        value = av_mallocz(chunk_size + 1);
        if (!value) {
            av_log(s, AV_LOG_ERROR,
                   "out of memory, unable to read INFO tag\n");
            return AVERROR(ENOMEM);
        }

        AV_WL32(key, chunk_code);

        if (avio_read(pb, value, chunk_size) != chunk_size) {
            av_log(s, AV_LOG_WARNING,
                   "premature end of file while reading INFO tag\n");
        }

        av_dict_set(&s->metadata, key, value, AV_DICT_DONT_STRDUP_VAL);
    }

    return 0;
}
#endif /* CONFIG_DEMUXERS */
