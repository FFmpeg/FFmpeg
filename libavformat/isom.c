/*
 * ISO Media common code
 * Copyright (c) 2001 Fabrice Bellard.
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

#include "avformat.h"
#include "riff.h"
#include "isom.h"

/* http://www.mp4ra.org */
const AVCodecTag ff_mp4_obj_type[] = {
    { CODEC_ID_MPEG4     ,  32 },
    { CODEC_ID_H264      ,  33 },
    { CODEC_ID_AAC       ,  64 },
    { CODEC_ID_MPEG2VIDEO,  96 }, /* MPEG2 Simple */
    { CODEC_ID_MPEG2VIDEO,  97 }, /* MPEG2 Main */
    { CODEC_ID_MPEG2VIDEO,  98 }, /* MPEG2 SNR */
    { CODEC_ID_MPEG2VIDEO,  99 }, /* MPEG2 Spatial */
    { CODEC_ID_MPEG2VIDEO, 100 }, /* MPEG2 High */
    { CODEC_ID_MPEG2VIDEO, 101 }, /* MPEG2 422 */
    { CODEC_ID_AAC       , 102 }, /* MPEG2 AAC Main */
    { CODEC_ID_AAC       , 103 }, /* MPEG2 AAC Low */
    { CODEC_ID_AAC       , 104 }, /* MPEG2 AAC SSR */
    { CODEC_ID_MP3       , 105 }, /* 13818-3 */
    { CODEC_ID_MPEG1VIDEO, 106 }, /* 11172-2 */
    { CODEC_ID_MP3       , 107 }, /* 11172-3 */
    { CODEC_ID_MJPEG     , 108 }, /* 10918-1 */
    { CODEC_ID_PNG       , 109 },
    { CODEC_ID_JPEG2000  , 110 }, /* 15444-1 */
    { CODEC_ID_VC1       , 163 },
    { CODEC_ID_VORBIS    , 221 },
    { CODEC_ID_PCM_S16LE , 224 },
    { CODEC_ID_QCELP     , 225 },
    { CODEC_ID_AC3       , 226 },
    { CODEC_ID_PCM_ALAW  , 227 },
    { CODEC_ID_PCM_MULAW , 228 },
    { CODEC_ID_PCM_S16BE , 230 },
    { CODEC_ID_H263      , 242 },
    { CODEC_ID_H261      , 243 },
    { 0, 0 },
};

const AVCodecTag codec_movvideo_tags[] = {
/*  { CODEC_ID_, MKTAG('I', 'V', '5', '0') }, *//* Indeo 5.0 */

    { CODEC_ID_RAWVIDEO, MKTAG('r', 'a', 'w', ' ') }, /* Uncompressed RGB */
/*  { CODEC_ID_RAWVIDEO, MKTAG('Y', 'u', 'v', '2') }, *//* Uncompressed YUV422 */
    { CODEC_ID_RAWVIDEO, MKTAG('A', 'V', 'U', 'I') }, /* YUV with alpha-channel (AVID Uncompressed) */
    { CODEC_ID_RAWVIDEO, MKTAG('2', 'v', 'u', 'y') }, /* UNCOMPRESSED 8BIT 4:2:2 */

    { CODEC_ID_MJPEG,  MKTAG('j', 'p', 'e', 'g') }, /* PhotoJPEG */
    { CODEC_ID_MJPEG,  MKTAG('m', 'j', 'p', 'a') }, /* Motion-JPEG (format A) */
    { CODEC_ID_MJPEG,  MKTAG('A', 'V', 'D', 'J') }, /* MJPEG with alpha-channel (AVID JFIF meridien compressed) */
/*  { CODEC_ID_MJPEG,  MKTAG('A', 'V', 'R', 'n') }, *//* MJPEG with alpha-channel (AVID ABVB/Truevision NuVista) */
    { CODEC_ID_MJPEGB, MKTAG('m', 'j', 'p', 'b') }, /* Motion-JPEG (format B) */

    { CODEC_ID_SVQ1, MKTAG('S', 'V', 'Q', '1') }, /* Sorenson Video v1 */
    { CODEC_ID_SVQ1, MKTAG('s', 'v', 'q', '1') }, /* Sorenson Video v1 */
    { CODEC_ID_SVQ1, MKTAG('s', 'v', 'q', 'i') }, /* Sorenson Video v1 (from QT specs)*/
    { CODEC_ID_SVQ3, MKTAG('S', 'V', 'Q', '3') }, /* Sorenson Video v3 */

    { CODEC_ID_MPEG4, MKTAG('m', 'p', '4', 'v') },
    { CODEC_ID_MPEG4, MKTAG('D', 'I', 'V', 'X') }, /* OpenDiVX *//* sample files at http://heroinewarrior.com/xmovie.php3 use this tag */
    { CODEC_ID_MPEG4, MKTAG('X', 'V', 'I', 'D') },
    { CODEC_ID_MPEG4, MKTAG('3', 'I', 'V', '2') }, /* experimental: 3IVX files before ivx D4 4.5.1 */

    { CODEC_ID_H263, MKTAG('h', '2', '6', '3') }, /* H263 */
    { CODEC_ID_H263, MKTAG('s', '2', '6', '3') }, /* H263 ?? works */

    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'c', 'p') }, /* DV PAL */
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'c', ' ') }, /* DV NTSC */
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'p', 'p') }, /* DVCPRO PAL produced by FCP */
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', '5', 'p') }, /* DVCPRO50 PAL produced by FCP */
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', '5', 'n') }, /* DVCPRO50 NTSC produced by FCP */
    { CODEC_ID_DVVIDEO, MKTAG('A', 'V', 'd', 'v') }, /* AVID DV */
  //{ CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'h', '5') }, /* DVCPRO HD 50i produced by FCP */
  //{ CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'h', '6') }, /* DVCPRO HD 60i produced by FCP */

    { CODEC_ID_VP3,     MKTAG('V', 'P', '3', '1') }, /* On2 VP3 */
    { CODEC_ID_RPZA,    MKTAG('r', 'p', 'z', 'a') }, /* Apple Video (RPZA) */
    { CODEC_ID_CINEPAK, MKTAG('c', 'v', 'i', 'd') }, /* Cinepak */
    { CODEC_ID_8BPS,    MKTAG('8', 'B', 'P', 'S') }, /* Planar RGB (8BPS) */
    { CODEC_ID_SMC,     MKTAG('s', 'm', 'c', ' ') }, /* Apple Graphics (SMC) */
    { CODEC_ID_QTRLE,   MKTAG('r', 'l', 'e', ' ') }, /* Apple Animation (RLE) */
    { CODEC_ID_MSRLE,   MKTAG('W', 'R', 'L', 'E') },
    { CODEC_ID_QDRAW,   MKTAG('q', 'd', 'r', 'w') }, /* QuickDraw */

    { CODEC_ID_H264, MKTAG('a', 'v', 'c', '1') }, /* AVC-1/H.264 */

    { CODEC_ID_MPEG1VIDEO, MKTAG('m', 'p', 'e', 'g') }, /* MPEG */
    { CODEC_ID_MPEG2VIDEO, MKTAG('h', 'd', 'v', '1') }, /* HDV 720p30 */
    { CODEC_ID_MPEG2VIDEO, MKTAG('h', 'd', 'v', '2') }, /* MPEG2 produced by Sony HD camera */
    { CODEC_ID_MPEG2VIDEO, MKTAG('h', 'd', 'v', '3') }, /* HDV produced by FCP */
    { CODEC_ID_MPEG2VIDEO, MKTAG('m', 'x', '5', 'n') }, /* MPEG2 IMX NTSC 525/60 50mb/s produced by FCP */
    { CODEC_ID_MPEG2VIDEO, MKTAG('m', 'x', '5', 'p') }, /* MPEG2 IMX PAL 625/50 50mb/s produced by FCP */
    { CODEC_ID_MPEG2VIDEO, MKTAG('m', 'x', '3', 'n') }, /* MPEG2 IMX NTSC 525/60 30mb/s produced by FCP */
    { CODEC_ID_MPEG2VIDEO, MKTAG('m', 'x', '3', 'p') }, /* MPEG2 IMX PAL 625/50 30mb/s produced by FCP */
    { CODEC_ID_MPEG2VIDEO, MKTAG('A', 'V', 'm', 'p') }, /* AVID IMX PAL */

  //{ CODEC_ID_JPEG2000, MKTAG('m', 'j', 'p', '2') }, /* JPEG 2000 produced by FCP */

    { CODEC_ID_TARGA, MKTAG('t', 'g', 'a', ' ') }, /* Truevision Targa */
    { CODEC_ID_TIFF,  MKTAG('t', 'i', 'f', 'f') }, /* TIFF embedded in MOV */
    { CODEC_ID_GIF,   MKTAG('g', 'i', 'f', ' ') }, /* embedded gif files as frames (usually one "click to play movie" frame) */
    { CODEC_ID_PNG,   MKTAG('p', 'n', 'g', ' ') },

    { CODEC_ID_VC1, MKTAG('v', 'c', '-', '1') }, /* SMPTE RP 2025 */

    { CODEC_ID_DNXHD, MKTAG('A', 'V', 'd', 'n') }, /* AVID DNxHD */

    { CODEC_ID_NONE, 0 },
};

const AVCodecTag codec_movaudio_tags[] = {
    { CODEC_ID_PCM_S32BE, MKTAG('i', 'n', '3', '2') },
    { CODEC_ID_PCM_S32LE, MKTAG('i', 'n', '3', '2') },
    { CODEC_ID_PCM_S24BE, MKTAG('i', 'n', '2', '4') },
    { CODEC_ID_PCM_S24LE, MKTAG('i', 'n', '2', '4') },
    { CODEC_ID_PCM_S16BE, MKTAG('t', 'w', 'o', 's') }, /* 16 bits */
    { CODEC_ID_PCM_S16BE, MKTAG('N', 'O', 'N', 'E') }, /* uncompressed */
    { CODEC_ID_PCM_S16LE, MKTAG('s', 'o', 'w', 't') }, /*  */
    { CODEC_ID_PCM_S16LE, MKTAG('l', 'p', 'c', 'm') },
    { CODEC_ID_PCM_S8,    MKTAG('s', 'o', 'w', 't') },
    { CODEC_ID_PCM_U8,    MKTAG('r', 'a', 'w', ' ') }, /* 8 bits unsigned */
    { CODEC_ID_PCM_MULAW, MKTAG('u', 'l', 'a', 'w') }, /*  */
    { CODEC_ID_PCM_ALAW,  MKTAG('a', 'l', 'a', 'w') }, /*  */

    { CODEC_ID_ADPCM_IMA_QT, MKTAG('i', 'm', 'a', '4') }, /* IMA-4 ADPCM */
    { CODEC_ID_ADPCM_MS,     MKTAG('m', 's', 0x00, 0x02) }, /* MS ADPCM */

    { CODEC_ID_MACE3, MKTAG('M', 'A', 'C', '3') }, /* Macintosh Audio Compression and Expansion 3:1 */
    { CODEC_ID_MACE6, MKTAG('M', 'A', 'C', '6') }, /* Macintosh Audio Compression and Expansion 6:1 */

    { CODEC_ID_MP3, MKTAG('.', 'm', 'p', '3') }, /* MPEG layer 3 */ /* sample files at http://www.3ivx.com/showcase.html use this tag */
    { CODEC_ID_MP3, 0x6D730055 }, /* MPEG layer 3 */
    { CODEC_ID_MP3, MKTAG('m', 's', 0x00, 0x55) }, /* MPEG layer 3 *//* XXX: check endianness */

/*  { CODEC_ID_OGG_VORBIS, MKTAG('O', 'g', 'g', 'S') }, *//* sample files at http://heroinewarrior.com/xmovie.php3 use this tag */

    { CODEC_ID_AAC, MKTAG('m', 'p', '4', 'a') }, /* MPEG-4 AAC */

    { CODEC_ID_AMR_NB, MKTAG('s', 'a', 'm', 'r') }, /* AMR-NB 3gp */
    { CODEC_ID_AMR_WB, MKTAG('s', 'a', 'w', 'b') }, /* AMR-WB 3gp */

    { CODEC_ID_AC3,  MKTAG('m', 's', 0x20, 0x00) }, /* Dolby AC-3 */

    { CODEC_ID_ALAC, MKTAG('a', 'l', 'a', 'c') }, /* Apple Lossless */
    { CODEC_ID_QDM2, MKTAG('Q', 'D', 'M', '2') }, /* QDM2 */

    { CODEC_ID_DVAUDIO, MKTAG('v', 'd', 'v', 'a') },
    { CODEC_ID_DVAUDIO, MKTAG('d', 'v', 'c', 'a') },

    { CODEC_ID_NONE, 0 },
};

/* map numeric codes from mdhd atom to ISO 639 */
/* cf. QTFileFormat.pdf p253, qtff.pdf p205 */
/* http://developer.apple.com/documentation/mac/Text/Text-368.html */
/* deprecated by putting the code as 3*5bit ascii */
static const char *mov_mdhd_language_map[] = {
    /* 0-9 */
    "eng", "fra", "ger", "ita", "dut", "sve", "spa", "dan", "por", "nor",
    "heb", "jpn", "ara", "fin", "gre", "ice", "mlt", "tur", "hr "/*scr*/, "chi"/*ace?*/,
    "urd", "hin", "tha", "kor", "lit", "pol", "hun", "est", "lav",  NULL,
    "fo ",  NULL, "rus", "chi",  NULL, "iri", "alb", "ron", "ces", "slk",
    "slv", "yid", "sr ", "mac", "bul", "ukr", "bel", "uzb", "kaz", "aze",
    /*?*/
    "aze", "arm", "geo", "mol", "kir", "tgk", "tuk", "mon",  NULL, "pus",
    "kur", "kas", "snd", "tib", "nep", "san", "mar", "ben", "asm", "guj",
    "pa ", "ori", "mal", "kan", "tam", "tel",  NULL, "bur", "khm", "lao",
    /*                   roman? arabic? */
    "vie", "ind", "tgl", "may", "may", "amh", "tir", "orm", "som", "swa",
    /*==rundi?*/
    NULL, "run",  NULL, "mlg", "epo",  NULL,  NULL,  NULL,  NULL,  NULL,
    /* 100 */
    NULL,  NULL,  NULL,  NULL,  NULL,  NULL,  NULL,  NULL,  NULL,  NULL,
    NULL,  NULL,  NULL,  NULL,  NULL,  NULL,  NULL,  NULL,  NULL,  NULL,
    NULL,  NULL,  NULL,  NULL,  NULL,  NULL,  NULL,  NULL, "wel", "baq",
    "cat", "lat", "que", "grn", "aym", "tat", "uig", "dzo", "jav"
};

int ff_mov_iso639_to_lang(const char *lang, int mp4)
{
    int i, code = 0;

    /* old way, only for QT? */
    for (i = 0; !mp4 && (i < (sizeof(mov_mdhd_language_map)/sizeof(char *))); i++) {
        if (mov_mdhd_language_map[i] && !strcmp(lang, mov_mdhd_language_map[i]))
            return i;
    }
    /* XXX:can we do that in mov too? */
    if (!mp4)
        return 0;
    /* handle undefined as such */
    if (lang[0] == '\0')
        lang = "und";
    /* 5bit ascii */
    for (i = 0; i < 3; i++) {
        unsigned char c = (unsigned char)lang[i];
        if (c < 0x60)
            return 0;
        if (c > 0x60 + 0x1f)
            return 0;
        code <<= 5;
        code |= (c - 0x60);
    }
    return code;
}

int ff_mov_lang_to_iso639(int code, char *to)
{
    int i;
    /* is it the mangled iso code? */
    /* see http://www.geocities.com/xhelmboyx/quicktime/formats/mp4-layout.txt */
    if (code > 138) {
        for (i = 2; i >= 0; i--) {
            to[i] = 0x60 + (code & 0x1f);
            code >>= 5;
        }
        return 1;
    }
    /* old fashion apple lang code */
    if (code >= (sizeof(mov_mdhd_language_map)/sizeof(char *)))
        return 0;
    if (!mov_mdhd_language_map[code])
        return 0;
    strncpy(to, mov_mdhd_language_map[code], 4);
    return 1;
}
