/*
 * RIFF codec tags
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

/* Note: when encoding, the first matching tag is used, so order is
   important if multiple tags possible for a given codec. */
const AVCodecTag ff_codec_bmp_tags[] = {
    { CODEC_ID_H264,         MKTAG('H', '2', '6', '4') },
    { CODEC_ID_H264,         MKTAG('h', '2', '6', '4') },
    { CODEC_ID_H264,         MKTAG('X', '2', '6', '4') },
    { CODEC_ID_H264,         MKTAG('x', '2', '6', '4') },
    { CODEC_ID_H264,         MKTAG('a', 'v', 'c', '1') },
    { CODEC_ID_H264,         MKTAG('D', 'A', 'V', 'C') },
    { CODEC_ID_H264,         MKTAG('V', 'S', 'S', 'H') },
    { CODEC_ID_H264,         MKTAG('Q', '2', '6', '4') }, /* QNAP surveillance system */
    { CODEC_ID_H263,         MKTAG('H', '2', '6', '3') },
    { CODEC_ID_H263,         MKTAG('X', '2', '6', '3') },
    { CODEC_ID_H263,         MKTAG('T', '2', '6', '3') },
    { CODEC_ID_H263,         MKTAG('L', '2', '6', '3') },
    { CODEC_ID_H263,         MKTAG('V', 'X', '1', 'K') },
    { CODEC_ID_H263,         MKTAG('Z', 'y', 'G', 'o') },
    { CODEC_ID_H263,         MKTAG('M', '2', '6', '3') },
    { CODEC_ID_H263P,        MKTAG('H', '2', '6', '3') },
    { CODEC_ID_H263I,        MKTAG('I', '2', '6', '3') }, /* intel h263 */
    { CODEC_ID_H261,         MKTAG('H', '2', '6', '1') },
    { CODEC_ID_H263P,        MKTAG('U', '2', '6', '3') },
    { CODEC_ID_H263P,        MKTAG('v', 'i', 'v', '1') },
    { CODEC_ID_MPEG4,        MKTAG('F', 'M', 'P', '4') },
    { CODEC_ID_MPEG4,        MKTAG('D', 'I', 'V', 'X') },
    { CODEC_ID_MPEG4,        MKTAG('D', 'X', '5', '0') },
    { CODEC_ID_MPEG4,        MKTAG('X', 'V', 'I', 'D') },
    { CODEC_ID_MPEG4,        MKTAG('M', 'P', '4', 'S') },
    { CODEC_ID_MPEG4,        MKTAG('M', '4', 'S', '2') },
    { CODEC_ID_MPEG4,        MKTAG( 4 ,  0 ,  0 ,  0 ) }, /* some broken avi use this */
    { CODEC_ID_MPEG4,        MKTAG('D', 'I', 'V', '1') },
    { CODEC_ID_MPEG4,        MKTAG('B', 'L', 'Z', '0') },
    { CODEC_ID_MPEG4,        MKTAG('m', 'p', '4', 'v') },
    { CODEC_ID_MPEG4,        MKTAG('U', 'M', 'P', '4') },
    { CODEC_ID_MPEG4,        MKTAG('W', 'V', '1', 'F') },
    { CODEC_ID_MPEG4,        MKTAG('S', 'E', 'D', 'G') },
    { CODEC_ID_MPEG4,        MKTAG('R', 'M', 'P', '4') },
    { CODEC_ID_MPEG4,        MKTAG('3', 'I', 'V', '2') },
    { CODEC_ID_MPEG4,        MKTAG('W', 'A', 'W', 'V') }, /* WaWv MPEG-4 Video Codec */
    { CODEC_ID_MPEG4,        MKTAG('F', 'F', 'D', 'S') },
    { CODEC_ID_MPEG4,        MKTAG('F', 'V', 'F', 'W') },
    { CODEC_ID_MPEG4,        MKTAG('D', 'C', 'O', 'D') },
    { CODEC_ID_MPEG4,        MKTAG('M', 'V', 'X', 'M') },
    { CODEC_ID_MPEG4,        MKTAG('P', 'M', '4', 'V') },
    { CODEC_ID_MPEG4,        MKTAG('S', 'M', 'P', '4') },
    { CODEC_ID_MPEG4,        MKTAG('D', 'X', 'G', 'M') },
    { CODEC_ID_MPEG4,        MKTAG('V', 'I', 'D', 'M') },
    { CODEC_ID_MPEG4,        MKTAG('M', '4', 'T', '3') },
    { CODEC_ID_MPEG4,        MKTAG('G', 'E', 'O', 'X') },
    { CODEC_ID_MPEG4,        MKTAG('H', 'D', 'X', '4') }, /* flipped video */
    { CODEC_ID_MPEG4,        MKTAG('D', 'M', 'K', '2') },
    { CODEC_ID_MPEG4,        MKTAG('D', 'I', 'G', 'I') },
    { CODEC_ID_MPEG4,        MKTAG('I', 'N', 'M', 'C') },
    { CODEC_ID_MPEG4,        MKTAG('E', 'P', 'H', 'V') }, /* Ephv MPEG-4 */
    { CODEC_ID_MPEG4,        MKTAG('E', 'M', '4', 'A') },
    { CODEC_ID_MPEG4,        MKTAG('M', '4', 'C', 'C') }, /* Divio MPEG-4 */
    { CODEC_ID_MPEG4,        MKTAG('S', 'N', '4', '0') },
    { CODEC_ID_MPEG4,        MKTAG('V', 'S', 'P', 'X') },
    { CODEC_ID_MPEG4,        MKTAG('U', 'L', 'D', 'X') },
    { CODEC_ID_MPEG4,        MKTAG('G', 'E', 'O', 'V') },
    { CODEC_ID_MPEG4,        MKTAG('S', 'I', 'P', 'P') }, /* Samsung SHR-6040 */
    { CODEC_ID_MPEG4,        MKTAG('X', 'V', 'I', 'X') },
    { CODEC_ID_MPEG4,        MKTAG('D', 'r', 'e', 'X') },
    { CODEC_ID_MPEG4,        MKTAG('Q', 'M', 'P', '4') }, /* QNAP Systems */
    { CODEC_ID_MSMPEG4V3,    MKTAG('M', 'P', '4', '3') },
    { CODEC_ID_MSMPEG4V3,    MKTAG('D', 'I', 'V', '3') },
    { CODEC_ID_MSMPEG4V3,    MKTAG('M', 'P', 'G', '3') },
    { CODEC_ID_MSMPEG4V3,    MKTAG('D', 'I', 'V', '5') },
    { CODEC_ID_MSMPEG4V3,    MKTAG('D', 'I', 'V', '6') },
    { CODEC_ID_MSMPEG4V3,    MKTAG('D', 'I', 'V', '4') },
    { CODEC_ID_MSMPEG4V3,    MKTAG('D', 'V', 'X', '3') },
    { CODEC_ID_MSMPEG4V3,    MKTAG('A', 'P', '4', '1') },
    { CODEC_ID_MSMPEG4V3,    MKTAG('C', 'O', 'L', '1') },
    { CODEC_ID_MSMPEG4V3,    MKTAG('C', 'O', 'L', '0') },
    { CODEC_ID_MSMPEG4V2,    MKTAG('M', 'P', '4', '2') },
    { CODEC_ID_MSMPEG4V2,    MKTAG('D', 'I', 'V', '2') },
    { CODEC_ID_MSMPEG4V1,    MKTAG('M', 'P', 'G', '4') },
    { CODEC_ID_MSMPEG4V1,    MKTAG('M', 'P', '4', '1') },
    { CODEC_ID_WMV1,         MKTAG('W', 'M', 'V', '1') },
    { CODEC_ID_WMV2,         MKTAG('W', 'M', 'V', '2') },
    { CODEC_ID_DVVIDEO,      MKTAG('d', 'v', 's', 'd') },
    { CODEC_ID_DVVIDEO,      MKTAG('d', 'v', 'h', 'd') },
    { CODEC_ID_DVVIDEO,      MKTAG('d', 'v', 'h', '1') },
    { CODEC_ID_DVVIDEO,      MKTAG('d', 'v', 's', 'l') },
    { CODEC_ID_DVVIDEO,      MKTAG('d', 'v', '2', '5') },
    { CODEC_ID_DVVIDEO,      MKTAG('d', 'v', '5', '0') },
    { CODEC_ID_DVVIDEO,      MKTAG('c', 'd', 'v', 'c') }, /* Canopus DV */
    { CODEC_ID_DVVIDEO,      MKTAG('C', 'D', 'V', 'H') }, /* Canopus DV */
    { CODEC_ID_DVVIDEO,      MKTAG('C', 'D', 'V', '5') }, /* Canopus DV */
    { CODEC_ID_DVVIDEO,      MKTAG('d', 'v', 'c', ' ') },
    { CODEC_ID_DVVIDEO,      MKTAG('d', 'v', 'c', 's') },
    { CODEC_ID_DVVIDEO,      MKTAG('d', 'v', 'h', '1') },
    { CODEC_ID_MPEG1VIDEO,   MKTAG('m', 'p', 'g', '1') },
    { CODEC_ID_MPEG1VIDEO,   MKTAG('m', 'p', 'g', '2') },
    { CODEC_ID_MPEG2VIDEO,   MKTAG('m', 'p', 'g', '2') },
    { CODEC_ID_MPEG2VIDEO,   MKTAG('M', 'P', 'E', 'G') },
    { CODEC_ID_MPEG1VIDEO,   MKTAG('P', 'I', 'M', '1') },
    { CODEC_ID_MPEG2VIDEO,   MKTAG('P', 'I', 'M', '2') },
    { CODEC_ID_MPEG1VIDEO,   MKTAG('V', 'C', 'R', '2') },
    { CODEC_ID_MPEG1VIDEO,   MKTAG( 1 ,  0 ,  0 ,  16) },
    { CODEC_ID_MPEG2VIDEO,   MKTAG( 2 ,  0 ,  0 ,  16) },
    { CODEC_ID_MPEG4,        MKTAG( 4 ,  0 ,  0 ,  16) },
    { CODEC_ID_MPEG2VIDEO,   MKTAG('D', 'V', 'R', ' ') },
    { CODEC_ID_MPEG2VIDEO,   MKTAG('M', 'M', 'E', 'S') },
    { CODEC_ID_MPEG2VIDEO,   MKTAG('L', 'M', 'P', '2') }, /* Lead MPEG2 in avi */
    { CODEC_ID_MPEG2VIDEO,   MKTAG('s', 'l', 'i', 'f') },
    { CODEC_ID_MPEG2VIDEO,   MKTAG('E', 'M', '2', 'V') },
    { CODEC_ID_MPEG2VIDEO,   MKTAG('M', '7', '0', '1') }, /* Matrox MPEG2 intra-only */
    { CODEC_ID_MPEG2VIDEO,   MKTAG('m', 'p', 'g', 'v') },
    { CODEC_ID_MPEG1VIDEO,   MKTAG('B', 'W', '1', '0') },
    { CODEC_ID_MPEG1VIDEO,   MKTAG('X', 'M', 'P', 'G') }, /* Xing MPEG intra only */
    { CODEC_ID_MJPEG,        MKTAG('M', 'J', 'P', 'G') },
    { CODEC_ID_MJPEG,        MKTAG('L', 'J', 'P', 'G') },
    { CODEC_ID_MJPEG,        MKTAG('d', 'm', 'b', '1') },
    { CODEC_ID_MJPEG,        MKTAG('m', 'j', 'p', 'a') },
    { CODEC_ID_LJPEG,        MKTAG('L', 'J', 'P', 'G') },
    { CODEC_ID_MJPEG,        MKTAG('J', 'P', 'G', 'L') }, /* Pegasus lossless JPEG */
    { CODEC_ID_JPEGLS,       MKTAG('M', 'J', 'L', 'S') }, /* JPEG-LS custom FOURCC for avi - encoder */
    { CODEC_ID_JPEGLS,       MKTAG('M', 'J', 'P', 'G') },
    { CODEC_ID_MJPEG,        MKTAG('M', 'J', 'L', 'S') }, /* JPEG-LS custom FOURCC for avi - decoder */
    { CODEC_ID_MJPEG,        MKTAG('j', 'p', 'e', 'g') },
    { CODEC_ID_MJPEG,        MKTAG('I', 'J', 'P', 'G') },
    { CODEC_ID_MJPEG,        MKTAG('A', 'V', 'R', 'n') },
    { CODEC_ID_MJPEG,        MKTAG('A', 'C', 'D', 'V') },
    { CODEC_ID_MJPEG,        MKTAG('Q', 'I', 'V', 'G') },
    { CODEC_ID_MJPEG,        MKTAG('S', 'L', 'M', 'J') }, /* SL M-JPEG */
    { CODEC_ID_MJPEG,        MKTAG('C', 'J', 'P', 'G') }, /* Creative Webcam JPEG */
    { CODEC_ID_MJPEG,        MKTAG('I', 'J', 'L', 'V') }, /* Intel JPEG Library Video Codec */
    { CODEC_ID_MJPEG,        MKTAG('M', 'V', 'J', 'P') }, /* Midvid JPEG Video Codec */
    { CODEC_ID_MJPEG,        MKTAG('A', 'V', 'I', '1') },
    { CODEC_ID_MJPEG,        MKTAG('A', 'V', 'I', '2') },
    { CODEC_ID_MJPEG,        MKTAG('M', 'T', 'S', 'J') },
    { CODEC_ID_MJPEG,        MKTAG('Z', 'J', 'P', 'G') }, /* Paradigm Matrix M-JPEG Codec */
    { CODEC_ID_MJPEG,        MKTAG('M', 'M', 'J', 'P') },
    { CODEC_ID_HUFFYUV,      MKTAG('H', 'F', 'Y', 'U') },
    { CODEC_ID_FFVHUFF,      MKTAG('F', 'F', 'V', 'H') },
    { CODEC_ID_CYUV,         MKTAG('C', 'Y', 'U', 'V') },
    { CODEC_ID_RAWVIDEO,     MKTAG( 0 ,  0 ,  0 ,  0 ) },
    { CODEC_ID_RAWVIDEO,     MKTAG( 3 ,  0 ,  0 ,  0 ) },
    { CODEC_ID_RAWVIDEO,     MKTAG('I', '4', '2', '0') },
    { CODEC_ID_RAWVIDEO,     MKTAG('Y', 'U', 'Y', '2') },
    { CODEC_ID_RAWVIDEO,     MKTAG('Y', '4', '2', '2') },
    { CODEC_ID_RAWVIDEO,     MKTAG('V', '4', '2', '2') },
    { CODEC_ID_RAWVIDEO,     MKTAG('Y', 'U', 'N', 'V') },
    { CODEC_ID_RAWVIDEO,     MKTAG('U', 'Y', 'N', 'V') },
    { CODEC_ID_RAWVIDEO,     MKTAG('U', 'Y', 'N', 'Y') },
    { CODEC_ID_RAWVIDEO,     MKTAG('u', 'y', 'v', '1') },
    { CODEC_ID_RAWVIDEO,     MKTAG('2', 'V', 'u', '1') },
    { CODEC_ID_RAWVIDEO,     MKTAG('2', 'v', 'u', 'y') },
    { CODEC_ID_RAWVIDEO,     MKTAG('y', 'u', 'v', 's') },
    { CODEC_ID_RAWVIDEO,     MKTAG('y', 'u', 'v', '2') },
    { CODEC_ID_RAWVIDEO,     MKTAG('P', '4', '2', '2') },
    { CODEC_ID_RAWVIDEO,     MKTAG('Y', 'V', '1', '2') },
    { CODEC_ID_RAWVIDEO,     MKTAG('Y', 'V', '1', '6') },
    { CODEC_ID_RAWVIDEO,     MKTAG('Y', 'V', '2', '4') },
    { CODEC_ID_RAWVIDEO,     MKTAG('U', 'Y', 'V', 'Y') },
    { CODEC_ID_RAWVIDEO,     MKTAG('V', 'Y', 'U', 'Y') },
    { CODEC_ID_RAWVIDEO,     MKTAG('I', 'Y', 'U', 'V') },
    { CODEC_ID_RAWVIDEO,     MKTAG('Y', '8', '0', '0') },
    { CODEC_ID_RAWVIDEO,     MKTAG('Y', '8', ' ', ' ') },
    { CODEC_ID_RAWVIDEO,     MKTAG('H', 'D', 'Y', 'C') },
    { CODEC_ID_RAWVIDEO,     MKTAG('Y', 'V', 'U', '9') },
    { CODEC_ID_RAWVIDEO,     MKTAG('V', 'D', 'T', 'Z') }, /* SoftLab-NSK VideoTizer */
    { CODEC_ID_RAWVIDEO,     MKTAG('Y', '4', '1', '1') },
    { CODEC_ID_RAWVIDEO,     MKTAG('N', 'V', '1', '2') },
    { CODEC_ID_RAWVIDEO,     MKTAG('N', 'V', '2', '1') },
    { CODEC_ID_RAWVIDEO,     MKTAG('Y', '4', '1', 'B') },
    { CODEC_ID_RAWVIDEO,     MKTAG('Y', '4', '2', 'B') },
    { CODEC_ID_RAWVIDEO,     MKTAG('Y', 'U', 'V', '9') },
    { CODEC_ID_RAWVIDEO,     MKTAG('Y', 'V', 'U', '9') },
    { CODEC_ID_RAWVIDEO,     MKTAG('a', 'u', 'v', '2') },
    { CODEC_ID_FRWU,         MKTAG('F', 'R', 'W', 'U') },
    { CODEC_ID_R10K,         MKTAG('R', '1', '0', 'k') },
    { CODEC_ID_R210,         MKTAG('r', '2', '1', '0') },
    { CODEC_ID_V210,         MKTAG('v', '2', '1', '0') },
    { CODEC_ID_V308,         MKTAG('v', '3', '0', '8') },
    { CODEC_ID_V410,         MKTAG('v', '4', '1', '0') },
    { CODEC_ID_YUV4,         MKTAG('y', 'u', 'v', '4') },
    { CODEC_ID_INDEO3,       MKTAG('I', 'V', '3', '1') },
    { CODEC_ID_INDEO3,       MKTAG('I', 'V', '3', '2') },
    { CODEC_ID_INDEO4,       MKTAG('I', 'V', '4', '1') },
    { CODEC_ID_INDEO5,       MKTAG('I', 'V', '5', '0') },
    { CODEC_ID_VP3,          MKTAG('V', 'P', '3', '1') },
    { CODEC_ID_VP3,          MKTAG('V', 'P', '3', '0') },
    { CODEC_ID_VP5,          MKTAG('V', 'P', '5', '0') },
    { CODEC_ID_VP6,          MKTAG('V', 'P', '6', '0') },
    { CODEC_ID_VP6,          MKTAG('V', 'P', '6', '1') },
    { CODEC_ID_VP6,          MKTAG('V', 'P', '6', '2') },
    { CODEC_ID_VP6F,         MKTAG('V', 'P', '6', 'F') },
    { CODEC_ID_VP6F,         MKTAG('F', 'L', 'V', '4') },
    { CODEC_ID_VP8,          MKTAG('V', 'P', '8', '0') },
    { CODEC_ID_ASV1,         MKTAG('A', 'S', 'V', '1') },
    { CODEC_ID_ASV2,         MKTAG('A', 'S', 'V', '2') },
    { CODEC_ID_VCR1,         MKTAG('V', 'C', 'R', '1') },
    { CODEC_ID_FFV1,         MKTAG('F', 'F', 'V', '1') },
    { CODEC_ID_XAN_WC4,      MKTAG('X', 'x', 'a', 'n') },
    { CODEC_ID_MIMIC,        MKTAG('L', 'M', '2', '0') },
    { CODEC_ID_MSRLE,        MKTAG('m', 'r', 'l', 'e') },
    { CODEC_ID_MSRLE,        MKTAG( 1 ,  0 ,  0 ,  0 ) },
    { CODEC_ID_MSRLE,        MKTAG( 2 ,  0 ,  0 ,  0 ) },
    { CODEC_ID_MSVIDEO1,     MKTAG('M', 'S', 'V', 'C') },
    { CODEC_ID_MSVIDEO1,     MKTAG('m', 's', 'v', 'c') },
    { CODEC_ID_MSVIDEO1,     MKTAG('C', 'R', 'A', 'M') },
    { CODEC_ID_MSVIDEO1,     MKTAG('c', 'r', 'a', 'm') },
    { CODEC_ID_MSVIDEO1,     MKTAG('W', 'H', 'A', 'M') },
    { CODEC_ID_MSVIDEO1,     MKTAG('w', 'h', 'a', 'm') },
    { CODEC_ID_CINEPAK,      MKTAG('c', 'v', 'i', 'd') },
    { CODEC_ID_TRUEMOTION1,  MKTAG('D', 'U', 'C', 'K') },
    { CODEC_ID_TRUEMOTION1,  MKTAG('P', 'V', 'E', 'Z') },
    { CODEC_ID_MSZH,         MKTAG('M', 'S', 'Z', 'H') },
    { CODEC_ID_ZLIB,         MKTAG('Z', 'L', 'I', 'B') },
    { CODEC_ID_SNOW,         MKTAG('S', 'N', 'O', 'W') },
    { CODEC_ID_4XM,          MKTAG('4', 'X', 'M', 'V') },
    { CODEC_ID_FLV1,         MKTAG('F', 'L', 'V', '1') },
    { CODEC_ID_FLV1,         MKTAG('S', '2', '6', '3') },
    { CODEC_ID_FLASHSV,      MKTAG('F', 'S', 'V', '1') },
    { CODEC_ID_SVQ1,         MKTAG('s', 'v', 'q', '1') },
    { CODEC_ID_TSCC,         MKTAG('t', 's', 'c', 'c') },
    { CODEC_ID_ULTI,         MKTAG('U', 'L', 'T', 'I') },
    { CODEC_ID_VIXL,         MKTAG('V', 'I', 'X', 'L') },
    { CODEC_ID_QPEG,         MKTAG('Q', 'P', 'E', 'G') },
    { CODEC_ID_QPEG,         MKTAG('Q', '1', '.', '0') },
    { CODEC_ID_QPEG,         MKTAG('Q', '1', '.', '1') },
    { CODEC_ID_WMV3,         MKTAG('W', 'M', 'V', '3') },
    { CODEC_ID_WMV3IMAGE,    MKTAG('W', 'M', 'V', 'P') },
    { CODEC_ID_VC1,          MKTAG('W', 'V', 'C', '1') },
    { CODEC_ID_VC1,          MKTAG('W', 'M', 'V', 'A') },
    { CODEC_ID_VC1IMAGE,     MKTAG('W', 'V', 'P', '2') },
    { CODEC_ID_LOCO,         MKTAG('L', 'O', 'C', 'O') },
    { CODEC_ID_WNV1,         MKTAG('W', 'N', 'V', '1') },
    { CODEC_ID_AASC,         MKTAG('A', 'A', 'S', 'C') },
    { CODEC_ID_INDEO2,       MKTAG('R', 'T', '2', '1') },
    { CODEC_ID_FRAPS,        MKTAG('F', 'P', 'S', '1') },
    { CODEC_ID_THEORA,       MKTAG('t', 'h', 'e', 'o') },
    { CODEC_ID_TRUEMOTION2,  MKTAG('T', 'M', '2', '0') },
    { CODEC_ID_CSCD,         MKTAG('C', 'S', 'C', 'D') },
    { CODEC_ID_ZMBV,         MKTAG('Z', 'M', 'B', 'V') },
    { CODEC_ID_KMVC,         MKTAG('K', 'M', 'V', 'C') },
    { CODEC_ID_CAVS,         MKTAG('C', 'A', 'V', 'S') },
    { CODEC_ID_JPEG2000,     MKTAG('m', 'j', 'p', '2') },
    { CODEC_ID_JPEG2000,     MKTAG('M', 'J', '2', 'C') },
    { CODEC_ID_JPEG2000,     MKTAG('L', 'J', '2', 'C') },
    { CODEC_ID_JPEG2000,     MKTAG('L', 'J', '2', 'K') },
    { CODEC_ID_VMNC,         MKTAG('V', 'M', 'n', 'c') },
    { CODEC_ID_TARGA,        MKTAG('t', 'g', 'a', ' ') },
    { CODEC_ID_PNG,          MKTAG('M', 'P', 'N', 'G') },
    { CODEC_ID_PNG,          MKTAG('P', 'N', 'G', '1') },
    { CODEC_ID_CLJR,         MKTAG('C', 'L', 'J', 'R') },
    { CODEC_ID_DIRAC,        MKTAG('d', 'r', 'a', 'c') },
    { CODEC_ID_RPZA,         MKTAG('a', 'z', 'p', 'r') },
    { CODEC_ID_RPZA,         MKTAG('R', 'P', 'Z', 'A') },
    { CODEC_ID_RPZA,         MKTAG('r', 'p', 'z', 'a') },
    { CODEC_ID_SP5X,         MKTAG('S', 'P', '5', '4') },
    { CODEC_ID_AURA,         MKTAG('A', 'U', 'R', 'A') },
    { CODEC_ID_AURA2,        MKTAG('A', 'U', 'R', '2') },
    { CODEC_ID_DPX,          MKTAG('d', 'p', 'x', ' ') },
    { CODEC_ID_KGV1,         MKTAG('K', 'G', 'V', '1') },
    { CODEC_ID_LAGARITH,     MKTAG('L', 'A', 'G', 'S') },
    { CODEC_ID_G2M,          MKTAG('G', '2', 'M', '2') },
    { CODEC_ID_G2M,          MKTAG('G', '2', 'M', '3') },
    { CODEC_ID_G2M,          MKTAG('G', '2', 'M', '4') },
    { CODEC_ID_AMV,          MKTAG('A', 'M', 'V', 'F') },
    { CODEC_ID_UTVIDEO,      MKTAG('U', 'L', 'R', 'A') },
    { CODEC_ID_UTVIDEO,      MKTAG('U', 'L', 'R', 'G') },
    { CODEC_ID_UTVIDEO,      MKTAG('U', 'L', 'Y', '0') },
    { CODEC_ID_UTVIDEO,      MKTAG('U', 'L', 'Y', '2') },
    { CODEC_ID_VBLE,         MKTAG('V', 'B', 'L', 'E') },
    { CODEC_ID_ESCAPE130,    MKTAG('E', '1', '3', '0') },
    { CODEC_ID_DXTORY,       MKTAG('x', 't', 'o', 'r') },
    { CODEC_ID_Y41P,         MKTAG('Y', '4', '1', 'P') },
    { CODEC_ID_NONE,         0 }
};

const AVCodecTag ff_codec_wav_tags[] = {
    { CODEC_ID_PCM_S16LE,       0x0001 },
    { CODEC_ID_PCM_U8,          0x0001 }, /* must come after s16le in this list */
    { CODEC_ID_PCM_S24LE,       0x0001 },
    { CODEC_ID_PCM_S32LE,       0x0001 },
    { CODEC_ID_ADPCM_MS,        0x0002 },
    { CODEC_ID_PCM_F32LE,       0x0003 },
    { CODEC_ID_PCM_F64LE,       0x0003 }, /* must come after f32le in this list */
    { CODEC_ID_PCM_ALAW,        0x0006 },
    { CODEC_ID_PCM_MULAW,       0x0007 },
    { CODEC_ID_WMAVOICE,        0x000A },
    { CODEC_ID_ADPCM_IMA_WAV,   0x0011 },
    { CODEC_ID_PCM_ZORK,        0x0011 }, /* must come after adpcm_ima_wav in this list */
    { CODEC_ID_ADPCM_YAMAHA,    0x0020 },
    { CODEC_ID_TRUESPEECH,      0x0022 },
    { CODEC_ID_GSM_MS,          0x0031 },
    { CODEC_ID_AMR_NB,          0x0038 },  /* rogue format number */
    { CODEC_ID_ADPCM_G726,      0x0045 },
    { CODEC_ID_MP2,             0x0050 },
    { CODEC_ID_MP3,             0x0055 },
    { CODEC_ID_AMR_NB,          0x0057 },
    { CODEC_ID_AMR_WB,          0x0058 },
    { CODEC_ID_ADPCM_IMA_DK4,   0x0061 },  /* rogue format number */
    { CODEC_ID_ADPCM_IMA_DK3,   0x0062 },  /* rogue format number */
    { CODEC_ID_ADPCM_IMA_WAV,   0x0069 },
    { CODEC_ID_VOXWARE,         0x0075 },
    { CODEC_ID_AAC,             0x00ff },
    { CODEC_ID_SIPR,            0x0130 },
    { CODEC_ID_WMAV1,           0x0160 },
    { CODEC_ID_WMAV2,           0x0161 },
    { CODEC_ID_WMAPRO,          0x0162 },
    { CODEC_ID_WMALOSSLESS,     0x0163 },
    { CODEC_ID_ADPCM_CT,        0x0200 },
    { CODEC_ID_ATRAC3,          0x0270 },
    { CODEC_ID_ADPCM_G722,      0x028F },
    { CODEC_ID_IMC,             0x0401 },
    { CODEC_ID_GSM_MS,          0x1500 },
    { CODEC_ID_TRUESPEECH,      0x1501 },
    { CODEC_ID_AAC,             0x1600 }, /* ADTS AAC */
    { CODEC_ID_AAC_LATM,        0x1602 },
    { CODEC_ID_AC3,             0x2000 },
    { CODEC_ID_DTS,             0x2001 },
    { CODEC_ID_SONIC,           0x2048 },
    { CODEC_ID_SONIC_LS,        0x2048 },
    { CODEC_ID_PCM_MULAW,       0x6c75 },
    { CODEC_ID_AAC,             0x706d },
    { CODEC_ID_AAC,             0x4143 },
    { CODEC_ID_SPEEX,           0xA109 },
    { CODEC_ID_FLAC,            0xF1AC },
    { CODEC_ID_ADPCM_SWF,       ('S'<<8)+'F' },
    { CODEC_ID_VORBIS,          ('V'<<8)+'o' }, //HACK/FIXME, does vorbis in WAV/AVI have an (in)official id?

    /* FIXME: All of the IDs below are not 16 bit and thus illegal. */
    // for NuppelVideo (nuv.c)
    { CODEC_ID_PCM_S16LE, MKTAG('R', 'A', 'W', 'A') },
    { CODEC_ID_MP3,       MKTAG('L', 'A', 'M', 'E') },
    { CODEC_ID_MP3,       MKTAG('M', 'P', '3', ' ') },
    { CODEC_ID_NONE,      0 },
};

const AVCodecGuid ff_codec_wav_guids[] = {
    {CODEC_ID_AC3,        {0x2C,0x80,0x6D,0xE0,0x46,0xDB,0xCF,0x11,0xB4,0xD1,0x00,0x80,0x5F,0x6C,0xBB,0xEA}},
    {CODEC_ID_ATRAC3P,    {0xBF,0xAA,0x23,0xE9,0x58,0xCB,0x71,0x44,0xA1,0x19,0xFF,0xFA,0x01,0xE4,0xCE,0x62}},
    {CODEC_ID_EAC3,       {0xAF,0x87,0xFB,0xA7,0x02,0x2D,0xFB,0x42,0xA4,0xD4,0x05,0xCD,0x93,0x84,0x3B,0xDD}},
    {CODEC_ID_MP2,        {0x2B,0x80,0x6D,0xE0,0x46,0xDB,0xCF,0x11,0xB4,0xD1,0x00,0x80,0x5F,0x6C,0xBB,0xEA}},
    {CODEC_ID_NONE}
};

const AVMetadataConv ff_riff_info_conv[] = {
    { "IART", "artist"    },
    { "ICMT", "comment"   },
    { "ICOP", "copyright" },
    { "ICRD", "date"      },
    { "IGNR", "genre"     },
    { "ILNG", "language"  },
    { "INAM", "title"     },
    { "IPRD", "album"     },
    { "IPRT", "track"     },
    { "ISFT", "encoder"   },
    { "ITCH", "encoded_by"},
    { 0 },
};

const char ff_riff_tags[][5] = {
    "IARL", "IART", "ICMS", "ICMT", "ICOP", "ICRD", "ICRP", "IDIM", "IDPI",
    "IENG", "IGNR", "IKEY", "ILGT", "ILNG", "IMED", "INAM", "IPLT", "IPRD",
    "IPRT", "ISBJ", "ISFT", "ISHP", "ISRC", "ISRF", "ITCH",
    {0}
};

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

    pos = avio_tell(pb);
    avio_seek(pb, start - 4, SEEK_SET);
    avio_wl32(pb, (uint32_t)(pos - start));
    avio_seek(pb, pos, SEEK_SET);
}

/* WAVEFORMATEX header */
/* returns the size or -1 on error */
int ff_put_wav_header(AVIOContext *pb, AVCodecContext *enc)
{
    int bps, blkalign, bytespersec;
    int hdrsize = 18;
    int waveformatextensible;
    uint8_t temp[256];
    uint8_t *riff_extradata= temp;
    uint8_t *riff_extradata_start= temp;

    if(!enc->codec_tag || enc->codec_tag > 0xffff)
        return -1;
    waveformatextensible =   (enc->channels > 2 && enc->channel_layout)
                          || enc->sample_rate > 48000
                          || av_get_bits_per_sample(enc->codec_id) > 16;

    if (waveformatextensible) {
        avio_wl16(pb, 0xfffe);
    } else {
        avio_wl16(pb, enc->codec_tag);
    }
    avio_wl16(pb, enc->channels);
    avio_wl32(pb, enc->sample_rate);
    if (enc->codec_id == CODEC_ID_MP2 || enc->codec_id == CODEC_ID_MP3 || enc->codec_id == CODEC_ID_GSM_MS) {
        bps = 0;
    } else {
        if (!(bps = av_get_bits_per_sample(enc->codec_id))) {
            if (enc->bits_per_coded_sample)
                bps = enc->bits_per_coded_sample;
            else
                bps = 16; // default to 16
        }
    }
    if(bps != enc->bits_per_coded_sample && enc->bits_per_coded_sample){
        av_log(enc, AV_LOG_WARNING, "requested bits_per_coded_sample (%d) and actually stored (%d) differ\n", enc->bits_per_coded_sample, bps);
    }

    if (enc->codec_id == CODEC_ID_MP2 || enc->codec_id == CODEC_ID_MP3) {
        blkalign = enc->frame_size; //this is wrong, but it seems many demuxers do not work if this is set correctly
        //blkalign = 144 * enc->bit_rate/enc->sample_rate;
    } else if (enc->codec_id == CODEC_ID_AC3) {
            blkalign = 3840; //maximum bytes per frame
    } else if (enc->block_align != 0) { /* specified by the codec */
        blkalign = enc->block_align;
    } else
        blkalign = bps * enc->channels / av_gcd(8, bps);
    if (enc->codec_id == CODEC_ID_PCM_U8 ||
        enc->codec_id == CODEC_ID_PCM_S24LE ||
        enc->codec_id == CODEC_ID_PCM_S32LE ||
        enc->codec_id == CODEC_ID_PCM_F32LE ||
        enc->codec_id == CODEC_ID_PCM_F64LE ||
        enc->codec_id == CODEC_ID_PCM_S16LE) {
        bytespersec = enc->sample_rate * blkalign;
    } else {
        bytespersec = enc->bit_rate / 8;
    }
    avio_wl32(pb, bytespersec); /* bytes per second */
    avio_wl16(pb, blkalign); /* block align */
    avio_wl16(pb, bps); /* bits per sample */
    if (enc->codec_id == CODEC_ID_MP3) {
        hdrsize += 12;
        bytestream_put_le16(&riff_extradata, 1);    /* wID */
        bytestream_put_le32(&riff_extradata, 2);    /* fdwFlags */
        bytestream_put_le16(&riff_extradata, 1152); /* nBlockSize */
        bytestream_put_le16(&riff_extradata, 1);    /* nFramesPerBlock */
        bytestream_put_le16(&riff_extradata, 1393); /* nCodecDelay */
    } else if (enc->codec_id == CODEC_ID_MP2) {
        hdrsize += 22;
        bytestream_put_le16(&riff_extradata, 2);                          /* fwHeadLayer */
        bytestream_put_le32(&riff_extradata, enc->bit_rate);              /* dwHeadBitrate */
        bytestream_put_le16(&riff_extradata, enc->channels == 2 ? 1 : 8); /* fwHeadMode */
        bytestream_put_le16(&riff_extradata, 0);                          /* fwHeadModeExt */
        bytestream_put_le16(&riff_extradata, 1);                          /* wHeadEmphasis */
        bytestream_put_le16(&riff_extradata, 16);                         /* fwHeadFlags */
        bytestream_put_le32(&riff_extradata, 0);                          /* dwPTSLow */
        bytestream_put_le32(&riff_extradata, 0);                          /* dwPTSHigh */
    } else if (enc->codec_id == CODEC_ID_GSM_MS || enc->codec_id == CODEC_ID_ADPCM_IMA_WAV) {
        hdrsize += 2;
        bytestream_put_le16(&riff_extradata, enc->frame_size); /* wSamplesPerBlock */
    } else if(enc->extradata_size){
        riff_extradata_start= enc->extradata;
        riff_extradata= enc->extradata + enc->extradata_size;
        hdrsize += enc->extradata_size;
    }
    if(waveformatextensible) {                                    /* write WAVEFORMATEXTENSIBLE extensions */
        hdrsize += 22;
        avio_wl16(pb, riff_extradata - riff_extradata_start + 22); /* 22 is WAVEFORMATEXTENSIBLE size */
        avio_wl16(pb, bps);                                        /* ValidBitsPerSample || SamplesPerBlock || Reserved */
        avio_wl32(pb, enc->channel_layout);                        /* dwChannelMask */
        avio_wl32(pb, enc->codec_tag);                             /* GUID + next 3 */
        avio_wl32(pb, 0x00100000);
        avio_wl32(pb, 0xAA000080);
        avio_wl32(pb, 0x719B3800);
    } else {
        avio_wl16(pb, riff_extradata - riff_extradata_start); /* cbSize */
    }
    avio_write(pb, riff_extradata_start, riff_extradata - riff_extradata_start);
    if(hdrsize&1){
        hdrsize++;
        avio_w8(pb, 0);
    }

    return hdrsize;
}

/* BITMAPINFOHEADER header */
void ff_put_bmp_header(AVIOContext *pb, AVCodecContext *enc, const AVCodecTag *tags, int for_asf)
{
    avio_wl32(pb, 40 + enc->extradata_size); /* size */
    avio_wl32(pb, enc->width);
    //We always store RGB TopDown
    avio_wl32(pb, enc->codec_tag ? enc->height : -enc->height);
    avio_wl16(pb, 1); /* planes */

    avio_wl16(pb, enc->bits_per_coded_sample ? enc->bits_per_coded_sample : 24); /* depth */
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
#endif //CONFIG_MUXERS

#if CONFIG_DEMUXERS
/* We could be given one of the three possible structures here:
 * WAVEFORMAT, PCMWAVEFORMAT or WAVEFORMATEX. Each structure
 * is an expansion of the previous one with the fields added
 * at the bottom. PCMWAVEFORMAT adds 'WORD wBitsPerSample' and
 * WAVEFORMATEX adds 'WORD  cbSize' and basically makes itself
 * an openended structure.
 */
int ff_get_wav_header(AVIOContext *pb, AVCodecContext *codec, int size)
{
    int id;

    id = avio_rl16(pb);
    codec->codec_type = AVMEDIA_TYPE_AUDIO;
    codec->channels = avio_rl16(pb);
    codec->sample_rate = avio_rl32(pb);
    codec->bit_rate = avio_rl32(pb) * 8;
    codec->block_align = avio_rl16(pb);
    if (size == 14) {  /* We're dealing with plain vanilla WAVEFORMAT */
        codec->bits_per_coded_sample = 8;
    }else
        codec->bits_per_coded_sample = avio_rl16(pb);
    if (id == 0xFFFE) {
        codec->codec_tag = 0;
    } else {
        codec->codec_tag = id;
        codec->codec_id = ff_wav_codec_get_id(id, codec->bits_per_coded_sample);
    }
    if (size >= 18) {  /* We're obviously dealing with WAVEFORMATEX */
        int cbSize = avio_rl16(pb); /* cbSize */
        size -= 18;
        cbSize = FFMIN(size, cbSize);
        if (cbSize >= 22 && id == 0xfffe) { /* WAVEFORMATEXTENSIBLE */
            ff_asf_guid subformat;
            codec->bits_per_coded_sample = avio_rl16(pb);
            codec->channel_layout = avio_rl32(pb); /* dwChannelMask */
            ff_get_guid(pb, &subformat);
            if (!memcmp(subformat + 4, (const uint8_t[]){FF_MEDIASUBTYPE_BASE_GUID}, 12)) {
                codec->codec_tag = AV_RL32(subformat);
                codec->codec_id = ff_wav_codec_get_id(codec->codec_tag, codec->bits_per_coded_sample);
            } else {
                codec->codec_id = ff_codec_guid_get_id(ff_codec_wav_guids, subformat);
                if (!codec->codec_id)
                    av_log(codec, AV_LOG_WARNING, "unknown subformat:"FF_PRI_GUID"\n", FF_ARG_GUID(subformat));
            }
            cbSize -= 22;
            size -= 22;
        }
        codec->extradata_size = cbSize;
        if (cbSize > 0) {
            av_free(codec->extradata);
            codec->extradata = av_mallocz(codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
            if (!codec->extradata)
                return AVERROR(ENOMEM);
            avio_read(pb, codec->extradata, codec->extradata_size);
            size -= cbSize;
        }

        /* It is possible for the chunk to contain garbage at the end */
        if (size > 0)
            avio_skip(pb, size);
    }
    if (codec->codec_id == CODEC_ID_AAC_LATM) {
        /* channels and sample_rate values are those prior to applying SBR and/or PS */
        codec->channels    = 0;
        codec->sample_rate = 0;
    }
    /* override bits_per_coded_sample for G.726 */
    if (codec->codec_id == CODEC_ID_ADPCM_G726)
        codec->bits_per_coded_sample = codec->bit_rate / codec->sample_rate;

    return 0;
}


enum CodecID ff_wav_codec_get_id(unsigned int tag, int bps)
{
    enum CodecID id;
    id = ff_codec_get_id(ff_codec_wav_tags, tag);
    if (id <= 0)
        return id;
    /* handle specific u8 codec */
    if (id == CODEC_ID_PCM_S16LE && bps == 8)
        id = CODEC_ID_PCM_U8;
    if (id == CODEC_ID_PCM_S16LE && bps == 24)
        id = CODEC_ID_PCM_S24LE;
    if (id == CODEC_ID_PCM_S16LE && bps == 32)
        id = CODEC_ID_PCM_S32LE;
    if (id == CODEC_ID_PCM_F32LE && bps == 64)
        id = CODEC_ID_PCM_F64LE;
    if (id == CODEC_ID_ADPCM_IMA_WAV && bps == 8)
        id = CODEC_ID_PCM_ZORK;
    return id;
}

int ff_get_bmp_header(AVIOContext *pb, AVStream *st)
{
    int tag1;
    avio_rl32(pb); /* size */
    st->codec->width = avio_rl32(pb);
    st->codec->height = (int32_t)avio_rl32(pb);
    avio_rl16(pb); /* planes */
    st->codec->bits_per_coded_sample= avio_rl16(pb); /* depth */
    tag1 = avio_rl32(pb);
    avio_rl32(pb); /* ImageSize */
    avio_rl32(pb); /* XPelsPerMeter */
    avio_rl32(pb); /* YPelsPerMeter */
    avio_rl32(pb); /* ClrUsed */
    avio_rl32(pb); /* ClrImportant */
    return tag1;
}
#endif // CONFIG_DEMUXERS

void ff_parse_specific_params(AVCodecContext *stream, int *au_rate, int *au_ssize, int *au_scale)
{
    int gcd;

    *au_ssize= stream->block_align;
    if(stream->frame_size && stream->sample_rate){
        *au_scale=stream->frame_size;
        *au_rate= stream->sample_rate;
    }else if(stream->codec_type == AVMEDIA_TYPE_VIDEO ||
             stream->codec_type == AVMEDIA_TYPE_DATA ||
             stream->codec_type == AVMEDIA_TYPE_SUBTITLE){
        *au_scale= stream->time_base.num;
        *au_rate = stream->time_base.den;
    }else{
        *au_scale= stream->block_align ? stream->block_align*8 : 8;
        *au_rate = stream->bit_rate ? stream->bit_rate : 8*stream->sample_rate;
    }
    gcd= av_gcd(*au_scale, *au_rate);
    *au_scale /= gcd;
    *au_rate /= gcd;
}

void ff_get_guid(AVIOContext *s, ff_asf_guid *g)
{
    assert(sizeof(*g) == 16);
    if (avio_read(s, *g, sizeof(*g)) < (int)sizeof(*g))
        memset(*g, 0, sizeof(*g));
}

enum CodecID ff_codec_guid_get_id(const AVCodecGuid *guids, ff_asf_guid guid)
{
    int i;
    for (i = 0; guids[i].id != CODEC_ID_NONE; i++) {
        if (!ff_guidcmp(guids[i].guid, guid))
            return guids[i].id;
    }
    return CODEC_ID_NONE;
}

int ff_read_riff_info(AVFormatContext *s, int64_t size)
{
    int64_t start, end, cur;
    AVIOContext *pb = s->pb;

    start = avio_tell(pb);
    end = start + size;

    while ((cur = avio_tell(pb)) >= 0 && cur <= end - 8 /* = tag + size */) {
        uint32_t chunk_code;
        int64_t chunk_size;
        char key[5] = {0};
        char *value;

        chunk_code = avio_rl32(pb);
        chunk_size = avio_rl32(pb);
        if (chunk_size > end || end - chunk_size < cur || chunk_size == UINT_MAX) {
            av_log(s, AV_LOG_ERROR, "too big INFO subchunk\n");
            return AVERROR_INVALIDDATA;
        }

        chunk_size += (chunk_size & 1);

        value = av_malloc(chunk_size + 1);
        if (!value) {
            av_log(s, AV_LOG_ERROR, "out of memory, unable to read INFO tag\n");
            return AVERROR(ENOMEM);
        }

        AV_WL32(key, chunk_code);

        if (avio_read(pb, value, chunk_size) != chunk_size) {
            av_freep(&value);
            av_log(s, AV_LOG_ERROR, "premature end of file while reading INFO tag\n");
            return AVERROR_INVALIDDATA;
        }

        value[chunk_size] = 0;

        av_dict_set(&s->metadata, key, value, AV_DICT_DONT_STRDUP_VAL);
    }

    return 0;
}
