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

/* http://gpac.sourceforge.net/tutorial/mediatypes.htm */
const AVCodecTag ff_mov_obj_type[] = {
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
    { CODEC_ID_MP3       , 105 },
    { CODEC_ID_MPEG1VIDEO, 106 },
    { CODEC_ID_MP2       , 107 },
    { CODEC_ID_MJPEG     , 108 },
    { CODEC_ID_PCM_S16LE , 224 },
    { CODEC_ID_VORBIS    , 221 },
    { CODEC_ID_QCELP     , 225 },
    { CODEC_ID_AC3       , 226 },
    { CODEC_ID_PCM_ALAW  , 227 },
    { CODEC_ID_PCM_MULAW , 228 },
    { CODEC_ID_PCM_S16BE , 230 },
    { CODEC_ID_H263      , 242 },
    { CODEC_ID_H261      , 243 },
    { 0, 0 },
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
