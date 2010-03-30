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

#include "libavcodec/avcodec.h"
#include "avformat.h"
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
    { CODEC_ID_H264,         MKTAG('V', 'S', 'S', 'H') },
    { CODEC_ID_H263,         MKTAG('H', '2', '6', '3') },
    { CODEC_ID_H263,         MKTAG('X', '2', '6', '3') },
    { CODEC_ID_H263,         MKTAG('T', '2', '6', '3') },
    { CODEC_ID_H263,         MKTAG('L', '2', '6', '3') },
    { CODEC_ID_H263,         MKTAG('V', 'X', '1', 'K') },
    { CODEC_ID_H263,         MKTAG('Z', 'y', 'G', 'o') },
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
    { CODEC_ID_MSMPEG4V3,    MKTAG('D', 'I', 'V', '3') }, /* default signature when using MSMPEG4 */
    { CODEC_ID_MSMPEG4V3,    MKTAG('M', 'P', '4', '3') },
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
    { CODEC_ID_MJPEG,        MKTAG('M', 'J', 'P', 'G') },
    { CODEC_ID_MJPEG,        MKTAG('L', 'J', 'P', 'G') },
    { CODEC_ID_MJPEG,        MKTAG('d', 'm', 'b', '1') },
    { CODEC_ID_MJPEG,        MKTAG('m', 'j', 'p', 'a') },
    { CODEC_ID_LJPEG,        MKTAG('L', 'J', 'P', 'G') },
    { CODEC_ID_MJPEG,        MKTAG('J', 'P', 'G', 'L') }, /* Pegasus lossless JPEG */
    { CODEC_ID_JPEGLS,       MKTAG('M', 'J', 'L', 'S') }, /* JPEG-LS custom FOURCC for avi - encoder */
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
    { CODEC_ID_RAWVIDEO,     MKTAG('P', '4', '2', '2') },
    { CODEC_ID_RAWVIDEO,     MKTAG('Y', 'V', '1', '2') },
    { CODEC_ID_RAWVIDEO,     MKTAG('U', 'Y', 'V', 'Y') },
    { CODEC_ID_RAWVIDEO,     MKTAG('V', 'Y', 'U', 'Y') },
    { CODEC_ID_RAWVIDEO,     MKTAG('I', 'Y', 'U', 'V') },
    { CODEC_ID_RAWVIDEO,     MKTAG('Y', '8', '0', '0') },
    { CODEC_ID_RAWVIDEO,     MKTAG('H', 'D', 'Y', 'C') },
    { CODEC_ID_RAWVIDEO,     MKTAG('Y', 'V', 'U', '9') },
    { CODEC_ID_RAWVIDEO,     MKTAG('V', 'D', 'T', 'Z') }, /* SoftLab-NSK VideoTizer */
    { CODEC_ID_FRWU,         MKTAG('F', 'R', 'W', 'U') },
    { CODEC_ID_R210,         MKTAG('r', '2', '1', '0') },
    { CODEC_ID_V210,         MKTAG('v', '2', '1', '0') },
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
    { CODEC_ID_FLASHSV,      MKTAG('F', 'S', 'V', '1') },
    { CODEC_ID_VP6F,         MKTAG('V', 'P', '6', 'F') },
    { CODEC_ID_SVQ1,         MKTAG('s', 'v', 'q', '1') },
    { CODEC_ID_TSCC,         MKTAG('t', 's', 'c', 'c') },
    { CODEC_ID_ULTI,         MKTAG('U', 'L', 'T', 'I') },
    { CODEC_ID_VIXL,         MKTAG('V', 'I', 'X', 'L') },
    { CODEC_ID_QPEG,         MKTAG('Q', 'P', 'E', 'G') },
    { CODEC_ID_QPEG,         MKTAG('Q', '1', '.', '0') },
    { CODEC_ID_QPEG,         MKTAG('Q', '1', '.', '1') },
    { CODEC_ID_WMV3,         MKTAG('W', 'M', 'V', '3') },
    { CODEC_ID_VC1,          MKTAG('W', 'V', 'C', '1') },
    { CODEC_ID_VC1,          MKTAG('W', 'M', 'V', 'A') },
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
    { CODEC_ID_JPEG2000,     MKTAG('M', 'J', '2', 'C') },
    { CODEC_ID_VMNC,         MKTAG('V', 'M', 'n', 'c') },
    { CODEC_ID_TARGA,        MKTAG('t', 'g', 'a', ' ') },
    { CODEC_ID_PNG,          MKTAG('M', 'P', 'N', 'G') },
    { CODEC_ID_PNG,          MKTAG('P', 'N', 'G', '1') },
    { CODEC_ID_CLJR,         MKTAG('c', 'l', 'j', 'r') },
    { CODEC_ID_DIRAC,        MKTAG('d', 'r', 'a', 'c') },
    { CODEC_ID_RPZA,         MKTAG('a', 'z', 'p', 'r') },
    { CODEC_ID_RPZA,         MKTAG('R', 'P', 'Z', 'A') },
    { CODEC_ID_RPZA,         MKTAG('r', 'p', 'z', 'a') },
    { CODEC_ID_SP5X,         MKTAG('S', 'P', '5', '4') },
    { CODEC_ID_AURA,         MKTAG('A', 'U', 'R', 'A') },
    { CODEC_ID_AURA2,        MKTAG('A', 'U', 'R', '2') },
    { CODEC_ID_DPX,          MKTAG('d', 'p', 'x', ' ') },
    { CODEC_ID_KGV1,         MKTAG('K', 'G', 'V', '1') },
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
    { CODEC_ID_IMC,             0x0401 },
    { CODEC_ID_GSM_MS,          0x1500 },
    { CODEC_ID_TRUESPEECH,      0x1501 },
    { CODEC_ID_AC3,             0x2000 },
    { CODEC_ID_DTS,             0x2001 },
    { CODEC_ID_SONIC,           0x2048 },
    { CODEC_ID_SONIC_LS,        0x2048 },
    { CODEC_ID_PCM_MULAW,       0x6c75 },
    { CODEC_ID_AAC,             0x706d },
    { CODEC_ID_AAC,             0x4143 },
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

#if CONFIG_MUXERS
int64_t ff_start_tag(ByteIOContext *pb, const char *tag)
{
    put_tag(pb, tag);
    put_le32(pb, 0);
    return url_ftell(pb);
}

void ff_end_tag(ByteIOContext *pb, int64_t start)
{
    int64_t pos;

    pos = url_ftell(pb);
    url_fseek(pb, start - 4, SEEK_SET);
    put_le32(pb, (uint32_t)(pos - start));
    url_fseek(pb, pos, SEEK_SET);
}

/* WAVEFORMATEX header */
/* returns the size or -1 on error */
int ff_put_wav_header(ByteIOContext *pb, AVCodecContext *enc)
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
        put_le16(pb, 0xfffe);
    } else {
        put_le16(pb, enc->codec_tag);
    }
    put_le16(pb, enc->channels);
    put_le32(pb, enc->sample_rate);
    if (enc->codec_id == CODEC_ID_MP2 || enc->codec_id == CODEC_ID_MP3 || enc->codec_id == CODEC_ID_GSM_MS) {
        bps = 0;
    } else if (enc->codec_id == CODEC_ID_ADPCM_G726) {
        bps = 4;
    } else {
        if (!(bps = av_get_bits_per_sample(enc->codec_id)))
            bps = 16; // default to 16
    }
    if(bps != enc->bits_per_coded_sample && enc->bits_per_coded_sample){
        av_log(enc, AV_LOG_WARNING, "requested bits_per_coded_sample (%d) and actually stored (%d) differ\n", enc->bits_per_coded_sample, bps);
    }

    if (enc->codec_id == CODEC_ID_MP2 || enc->codec_id == CODEC_ID_MP3 || enc->codec_id == CODEC_ID_AC3) {
        blkalign = enc->frame_size; //this is wrong, but it seems many demuxers do not work if this is set correctly
        //blkalign = 144 * enc->bit_rate/enc->sample_rate;
    } else if (enc->codec_id == CODEC_ID_ADPCM_G726) { //
        blkalign = 1;
    } else if (enc->block_align != 0) { /* specified by the codec */
        blkalign = enc->block_align;
    } else
        blkalign = enc->channels*bps >> 3;
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
    put_le32(pb, bytespersec); /* bytes per second */
    put_le16(pb, blkalign); /* block align */
    put_le16(pb, bps); /* bits per sample */
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
    } else if (!waveformatextensible){
        hdrsize -= 2;
    }
    if(waveformatextensible) {                                    /* write WAVEFORMATEXTENSIBLE extensions */
        hdrsize += 22;
        put_le16(pb, riff_extradata - riff_extradata_start + 22); /* 22 is WAVEFORMATEXTENSIBLE size */
        put_le16(pb, enc->bits_per_coded_sample);                 /* ValidBitsPerSample || SamplesPerBlock || Reserved */
        put_le32(pb, enc->channel_layout);                        /* dwChannelMask */
        put_le32(pb, enc->codec_tag);                             /* GUID + next 3 */
        put_le32(pb, 0x00100000);
        put_le32(pb, 0xAA000080);
        put_le32(pb, 0x719B3800);
    } else if(riff_extradata - riff_extradata_start) {
        put_le16(pb, riff_extradata - riff_extradata_start);
    }
    put_buffer(pb, riff_extradata_start, riff_extradata - riff_extradata_start);
    if(hdrsize&1){
        hdrsize++;
        put_byte(pb, 0);
    }

    return hdrsize;
}

/* BITMAPINFOHEADER header */
void ff_put_bmp_header(ByteIOContext *pb, AVCodecContext *enc, const AVCodecTag *tags, int for_asf)
{
    put_le32(pb, 40 + enc->extradata_size); /* size */
    put_le32(pb, enc->width);
    //We always store RGB TopDown
    put_le32(pb, enc->codec_tag ? enc->height : -enc->height);
    put_le16(pb, 1); /* planes */

    put_le16(pb, enc->bits_per_coded_sample ? enc->bits_per_coded_sample : 24); /* depth */
    /* compression type */
    put_le32(pb, enc->codec_tag);
    put_le32(pb, enc->width * enc->height * 3);
    put_le32(pb, 0);
    put_le32(pb, 0);
    put_le32(pb, 0);
    put_le32(pb, 0);

    put_buffer(pb, enc->extradata, enc->extradata_size);

    if (!for_asf && enc->extradata_size & 1)
        put_byte(pb, 0);
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
void ff_get_wav_header(ByteIOContext *pb, AVCodecContext *codec, int size)
{
    int id;

    id = get_le16(pb);
    codec->codec_type = AVMEDIA_TYPE_AUDIO;
    codec->codec_tag = id;
    codec->channels = get_le16(pb);
    codec->sample_rate = get_le32(pb);
    codec->bit_rate = get_le32(pb) * 8;
    codec->block_align = get_le16(pb);
    if (size == 14) {  /* We're dealing with plain vanilla WAVEFORMAT */
        codec->bits_per_coded_sample = 8;
    }else
        codec->bits_per_coded_sample = get_le16(pb);
    if (size >= 18) {  /* We're obviously dealing with WAVEFORMATEX */
        int cbSize = get_le16(pb); /* cbSize */
        size -= 18;
        cbSize = FFMIN(size, cbSize);
        if (cbSize >= 22 && id == 0xfffe) { /* WAVEFORMATEXTENSIBLE */
            codec->bits_per_coded_sample = get_le16(pb);
            codec->channel_layout = get_le32(pb); /* dwChannelMask */
            id = get_le32(pb); /* 4 first bytes of GUID */
            url_fskip(pb, 12); /* skip end of GUID */
            cbSize -= 22;
            size -= 22;
        }
        codec->extradata_size = cbSize;
        if (cbSize > 0) {
            codec->extradata = av_mallocz(codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
            get_buffer(pb, codec->extradata, codec->extradata_size);
            size -= cbSize;
        }

        /* It is possible for the chunk to contain garbage at the end */
        if (size > 0)
            url_fskip(pb, size);
    }
    codec->codec_id = ff_wav_codec_get_id(id, codec->bits_per_coded_sample);
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
#endif // CONFIG_DEMUXERS

void ff_parse_specific_params(AVCodecContext *stream, int *au_rate, int *au_ssize, int *au_scale)
{
    int gcd;

    *au_ssize= stream->block_align;
    if(stream->frame_size && stream->sample_rate){
        *au_scale=stream->frame_size;
        *au_rate= stream->sample_rate;
    }else if(stream->codec_type == AVMEDIA_TYPE_VIDEO ||
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
