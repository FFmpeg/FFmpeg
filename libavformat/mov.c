/*
 * MOV decoder.
 * Copyright (c) 2001 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <limits.h>

//#define DEBUG

#include "avformat.h"
#include "avi.h"
#include "mov.h"

#ifdef CONFIG_ZLIB
#include <zlib.h>
#endif

/*
 * First version by Francois Revol revol@free.fr
 * Seek function by Gael Chardon gael.dev@4now.net
 *
 * Features and limitations:
 * - reads most of the QT files I have (at least the structure),
 *   the exceptions are .mov with zlib compressed headers ('cmov' section). It shouldn't be hard to implement.
 *   FIXED, Francois Revol, 07/17/2002
 * - ffmpeg has nearly none of the usual QuickTime codecs,
 *   although I succesfully dumped raw and mp3 audio tracks off .mov files.
 *   Sample QuickTime files with mp3 audio can be found at: http://www.3ivx.com/showcase.html
 * - .mp4 parsing is still hazardous, although the format really is QuickTime with some minor changes
 *   (to make .mov parser crash maybe ?), despite what they say in the MPEG FAQ at
 *   http://mpeg.telecomitalialab.com/faq.htm
 * - the code is quite ugly... maybe I won't do it recursive next time :-)
 * - seek is not supported with files that contain edit list
 *
 * Funny I didn't know about http://sourceforge.net/projects/qt-ffmpeg/
 * when coding this :) (it's a writer anyway)
 *
 * Reference documents:
 * http://www.geocities.com/xhelmboyx/quicktime/formats/qtm-layout.txt
 * Apple:
 *  http://developer.apple.com/documentation/QuickTime/QTFF/
 *  http://developer.apple.com/documentation/QuickTime/QTFF/qtff.pdf
 * QuickTime is a trademark of Apple (AFAIK :))
 */

#include "qtpalette.h"


#undef NDEBUG
#include <assert.h>

/* http://gpac.sourceforge.net/tutorial/mediatypes.htm */
const CodecTag ff_mov_obj_type[] = {
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
    { CODEC_ID_AC3       , 226 },
    { CODEC_ID_PCM_ALAW  , 227 },
    { CODEC_ID_PCM_MULAW , 228 },
    { CODEC_ID_PCM_S16BE , 230 },
    { CODEC_ID_H263      , 242 },
    { CODEC_ID_H261      , 243 },
    { 0, 0 },
};

static const CodecTag mov_video_tags[] = {
/*  { CODEC_ID_, MKTAG('c', 'v', 'i', 'd') }, *//* Cinepak */
/*  { CODEC_ID_H263, MKTAG('r', 'a', 'w', ' ') }, *//* Uncompressed RGB */
/*  { CODEC_ID_H263, MKTAG('Y', 'u', 'v', '2') }, *//* Uncompressed YUV422 */
/*    { CODEC_ID_RAWVIDEO, MKTAG('A', 'V', 'U', 'I') }, *//* YUV with alpha-channel (AVID Uncompressed) */
/* Graphics */
/* Animation */
/* Apple video */
/* Kodak Photo CD */
    { CODEC_ID_MJPEG, MKTAG('j', 'p', 'e', 'g') }, /* PhotoJPEG */
    { CODEC_ID_MPEG1VIDEO, MKTAG('m', 'p', 'e', 'g') }, /* MPEG */
    { CODEC_ID_MJPEG, MKTAG('m', 'j', 'p', 'a') }, /* Motion-JPEG (format A) */
    { CODEC_ID_MJPEGB, MKTAG('m', 'j', 'p', 'b') }, /* Motion-JPEG (format B) */
    { CODEC_ID_MJPEG, MKTAG('A', 'V', 'D', 'J') }, /* MJPEG with alpha-channel (AVID JFIF meridien compressed) */
/*    { CODEC_ID_MJPEG, MKTAG('A', 'V', 'R', 'n') }, *//* MJPEG with alpha-channel (AVID ABVB/Truevision NuVista) */
/*    { CODEC_ID_GIF, MKTAG('g', 'i', 'f', ' ') }, *//* embedded gif files as frames (usually one "click to play movie" frame) */
/* Sorenson video */
    { CODEC_ID_SVQ1, MKTAG('S', 'V', 'Q', '1') }, /* Sorenson Video v1 */
    { CODEC_ID_SVQ1, MKTAG('s', 'v', 'q', '1') }, /* Sorenson Video v1 */
    { CODEC_ID_SVQ1, MKTAG('s', 'v', 'q', 'i') }, /* Sorenson Video v1 (from QT specs)*/
    { CODEC_ID_SVQ3, MKTAG('S', 'V', 'Q', '3') }, /* Sorenson Video v3 */
    { CODEC_ID_MPEG4, MKTAG('m', 'p', '4', 'v') },
    { CODEC_ID_MPEG4, MKTAG('D', 'I', 'V', 'X') }, /* OpenDiVX *//* sample files at http://heroinewarrior.com/xmovie.php3 use this tag */
    { CODEC_ID_MPEG4, MKTAG('X', 'V', 'I', 'D') },
    { CODEC_ID_MPEG4, MKTAG('3', 'I', 'V', '2') }, /* experimental: 3IVX files before ivx D4 4.5.1 */
/*    { CODEC_ID_, MKTAG('I', 'V', '5', '0') }, *//* Indeo 5.0 */
    { CODEC_ID_H263, MKTAG('h', '2', '6', '3') }, /* H263 */
    { CODEC_ID_H263, MKTAG('s', '2', '6', '3') }, /* H263 ?? works */
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'c', ' ') }, /* DV NTSC */
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'c', 'p') }, /* DV PAL */
/*    { CODEC_ID_DVVIDEO, MKTAG('A', 'V', 'd', 'v') }, *//* AVID dv */
    { CODEC_ID_VP3, MKTAG('V', 'P', '3', '1') }, /* On2 VP3 */
    { CODEC_ID_RPZA, MKTAG('r', 'p', 'z', 'a') }, /* Apple Video (RPZA) */
    { CODEC_ID_CINEPAK, MKTAG('c', 'v', 'i', 'd') }, /* Cinepak */
    { CODEC_ID_8BPS, MKTAG('8', 'B', 'P', 'S') }, /* Planar RGB (8BPS) */
    { CODEC_ID_SMC, MKTAG('s', 'm', 'c', ' ') }, /* Apple Graphics (SMC) */
    { CODEC_ID_QTRLE, MKTAG('r', 'l', 'e', ' ') }, /* Apple Animation (RLE) */
    { CODEC_ID_QDRAW, MKTAG('q', 'd', 'r', 'w') }, /* QuickDraw */
    { CODEC_ID_H264, MKTAG('a', 'v', 'c', '1') }, /* AVC-1/H.264 */
    { CODEC_ID_MPEG2VIDEO, MKTAG('h', 'd', 'v', '2') }, /* MPEG2 produced by Sony HD camera */
    { CODEC_ID_MPEG2VIDEO, MKTAG('h', 'd', 'v', '3') }, /* HDV produced by FCP */
    { CODEC_ID_MPEG2VIDEO, MKTAG('m', 'x', '5', 'n') }, /* MPEG2 IMX NTSC 525/60 50mb/s produced by FCP */
    { CODEC_ID_MPEG2VIDEO, MKTAG('m', 'x', '5', 'p') }, /* MPEG2 IMX PAL 625/50 50mb/s produced by FCP */
    { CODEC_ID_MPEG2VIDEO, MKTAG('m', 'x', '3', 'n') }, /* MPEG2 IMX NTSC 525/60 30mb/s produced by FCP */
    { CODEC_ID_MPEG2VIDEO, MKTAG('m', 'x', '3', 'p') }, /* MPEG2 IMX PAL 625/50 30mb/s produced by FCP */
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'p', 'p') }, /* DVCPRO PAL produced by FCP */
    //{ CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'h', '5') }, /* DVCPRO HD 50i produced by FCP */
    //{ CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'h', '6') }, /* DVCPRO HD 60i produced by FCP */
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', '5', 'p') }, /* DVCPRO50 PAL produced by FCP */
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', '5', 'n') }, /* DVCPRO50 NTSC produced by FCP */
    { CODEC_ID_DVVIDEO, MKTAG('A', 'V', 'd', 'v') }, /* AVID DV */
    //{ CODEC_ID_JPEG2000, MKTAG('m', 'j', 'p', '2') }, /* JPEG 2000 produced by FCP */
    { CODEC_ID_RAWVIDEO, MKTAG('2', 'v', 'u', 'y') }, /* UNCOMPRESSED 8BIT 4:2:2 */
    { CODEC_ID_NONE, 0 },
};

static const CodecTag mov_audio_tags[] = {
    { CODEC_ID_PCM_S32BE, MKTAG('i', 'n', '3', '2') },
    { CODEC_ID_PCM_S24BE, MKTAG('i', 'n', '2', '4') },
/*    { CODEC_ID_PCM_S16BE, MKTAG('N', 'O', 'N', 'E') }, *//* uncompressed */
    { CODEC_ID_PCM_S16BE, MKTAG('t', 'w', 'o', 's') }, /* 16 bits */
    /* { CODEC_ID_PCM_S8, MKTAG('t', 'w', 'o', 's') },*/ /* 8 bits */
    { CODEC_ID_PCM_U8, MKTAG('r', 'a', 'w', ' ') }, /* 8 bits unsigned */
    { CODEC_ID_PCM_S16LE, MKTAG('s', 'o', 'w', 't') }, /*  */
    { CODEC_ID_PCM_MULAW, MKTAG('u', 'l', 'a', 'w') }, /*  */
    { CODEC_ID_PCM_ALAW, MKTAG('a', 'l', 'a', 'w') }, /*  */
    { CODEC_ID_ADPCM_IMA_QT, MKTAG('i', 'm', 'a', '4') }, /* IMA-4 ADPCM */
    { CODEC_ID_ADPCM_MS, MKTAG('m', 's', 0x00, 0x02) }, /* MS ADPCM */
    { CODEC_ID_MACE3, MKTAG('M', 'A', 'C', '3') }, /* Macintosh Audio Compression and Expansion 3:1 */
    { CODEC_ID_MACE6, MKTAG('M', 'A', 'C', '6') }, /* Macintosh Audio Compression and Expansion 6:1 */

    { CODEC_ID_MP2, MKTAG('.', 'm', 'p', '3') }, /* MPEG layer 3 */ /* sample files at http://www.3ivx.com/showcase.html use this tag */
    { CODEC_ID_MP2, 0x6D730055 }, /* MPEG layer 3 */
    { CODEC_ID_MP2, 0x5500736D }, /* MPEG layer 3 *//* XXX: check endianness */
/*    { CODEC_ID_OGG_VORBIS, MKTAG('O', 'g', 'g', 'S') }, *//* sample files at http://heroinewarrior.com/xmovie.php3 use this tag */
/* MP4 tags */
    { CODEC_ID_AAC, MKTAG('m', 'p', '4', 'a') }, /* MPEG-4 AAC */
    /* The standard for mpeg4 audio is still not normalised AFAIK anyway */
    { CODEC_ID_AMR_NB, MKTAG('s', 'a', 'm', 'r') }, /* AMR-NB 3gp */
    { CODEC_ID_AMR_WB, MKTAG('s', 'a', 'w', 'b') }, /* AMR-WB 3gp */
    { CODEC_ID_AC3, MKTAG('m', 's', 0x20, 0x00) }, /* Dolby AC-3 */
    { CODEC_ID_ALAC,MKTAG('a', 'l', 'a', 'c') }, /* Apple Lossless */
    { CODEC_ID_QDM2,MKTAG('Q', 'D', 'M', '2') }, /* QDM2 */
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

/* the QuickTime file format is quite convoluted...
 * it has lots of index tables, each indexing something in another one...
 * Here we just use what is needed to read the chunks
 */

typedef struct MOV_sample_to_chunk_tbl {
    long first;
    long count;
    long id;
} MOV_sample_to_chunk_tbl;

typedef struct {
    uint32_t type;
    int64_t offset;
    int64_t size; /* total size (excluding the size and type fields) */
} MOV_atom_t;

typedef struct {
    int seed;
    int flags;
    int size;
    void* clrs;
} MOV_ctab_t;

typedef struct MOV_mdat_atom_s {
    offset_t offset;
    int64_t size;
} MOV_mdat_atom_t;

typedef struct {
    uint8_t  version;
    uint32_t flags; // 24bit

    /* 0x03 ESDescrTag */
    uint16_t es_id;
#define MP4ODescrTag                    0x01
#define MP4IODescrTag                   0x02
#define MP4ESDescrTag                   0x03
#define MP4DecConfigDescrTag            0x04
#define MP4DecSpecificDescrTag          0x05
#define MP4SLConfigDescrTag             0x06
#define MP4ContentIdDescrTag            0x07
#define MP4SupplContentIdDescrTag       0x08
#define MP4IPIPtrDescrTag               0x09
#define MP4IPMPPtrDescrTag              0x0A
#define MP4IPMPDescrTag                 0x0B
#define MP4RegistrationDescrTag         0x0D
#define MP4ESIDIncDescrTag              0x0E
#define MP4ESIDRefDescrTag              0x0F
#define MP4FileIODescrTag               0x10
#define MP4FileODescrTag                0x11
#define MP4ExtProfileLevelDescrTag      0x13
#define MP4ExtDescrTagsStart            0x80
#define MP4ExtDescrTagsEnd              0xFE
    uint8_t  stream_priority;

    /* 0x04 DecConfigDescrTag */
    uint8_t  object_type_id;
    uint8_t  stream_type;
    /* XXX: really streamType is
     * only 6bit, followed by:
     * 1bit  upStream
     * 1bit  reserved
     */
    uint32_t buffer_size_db; // 24
    uint32_t max_bitrate;
    uint32_t avg_bitrate;

    /* 0x05 DecSpecificDescrTag */
    uint8_t  decoder_cfg_len;
    uint8_t *decoder_cfg;

    /* 0x06 SLConfigDescrTag */
    uint8_t  sl_config_len;
    uint8_t *sl_config;
} MOV_esds_t;

struct MOVParseTableEntry;

typedef struct MOVStreamContext {
    int ffindex; /* the ffmpeg stream id */
    long next_chunk;
    long chunk_count;
    int64_t *chunk_offsets;
    int stts_count;
    Time2Sample *stts_data;
    int ctts_count;
    Time2Sample *ctts_data;
    int edit_count;             /* number of 'edit' (elst atom) */
    long sample_to_chunk_sz;
    MOV_sample_to_chunk_tbl *sample_to_chunk;
    int sample_to_ctime_index;
    int sample_to_ctime_sample;
    long sample_size;
    long sample_count;
    long *sample_sizes;
    long keyframe_count;
    long *keyframes;
    int time_scale;
    int time_rate;
    long current_sample;
    MOV_esds_t esds;
    AVRational sample_size_v1;
} MOVStreamContext;

typedef struct MOVContext {
    int mp4; /* set to 1 as soon as we are sure that the file is an .mp4 file (even some header parsing depends on this) */
    AVFormatContext *fc;
    int time_scale;
    int64_t duration; /* duration of the longest track */
    int found_moov; /* when both 'moov' and 'mdat' sections has been found */
    int found_mdat; /* we suppose we have enough data to read the file */
    int64_t mdat_size;
    int64_t mdat_offset;
    int total_streams;
    /* some streams listed here aren't presented to the ffmpeg API, since they aren't either video nor audio
     * but we need the info to be able to skip data from those streams in the 'mdat' section
     */
    MOVStreamContext *streams[MAX_STREAMS];

    int ctab_size;
    MOV_ctab_t **ctab;           /* color tables */
    const struct MOVParseTableEntry *parse_table; /* could be eventually used to change the table */
    /* NOTE: for recursion save to/ restore from local variable! */

    AVPaletteControl palette_control;
    MOV_mdat_atom_t *mdat_list;
    int mdat_count;
} MOVContext;


/* XXX: it's the first time I make a recursive parser I think... sorry if it's ugly :P */

/* those functions parse an atom */
/* return code:
 1: found what I wanted, exit
 0: continue to parse next atom
 -1: error occured, exit
 */
typedef int (*mov_parse_function)(MOVContext *ctx, ByteIOContext *pb, MOV_atom_t atom);

/* links atom IDs to parse functions */
typedef struct MOVParseTableEntry {
    uint32_t type;
    mov_parse_function func;
} MOVParseTableEntry;

static int ff_mov_lang_to_iso639(int code, char *to)
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

static int mov_read_leaf(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    if (atom.size>1)
        url_fskip(pb, atom.size);
/*        url_seek(pb, atom_offset+atom.size, SEEK_SET); */
    return 0;
}

static int mov_read_default(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    int64_t total_size = 0;
    MOV_atom_t a;
    int i;
    int err = 0;

    a.offset = atom.offset;

    if (atom.size < 0)
        atom.size = 0x7fffffffffffffffLL;
    while(((total_size + 8) < atom.size) && !url_feof(pb) && !err) {
        a.size = atom.size;
        a.type=0L;
        if(atom.size >= 8) {
            a.size = get_be32(pb);
            a.type = get_le32(pb);
        }
        total_size += 8;
        a.offset += 8;
        dprintf("type: %08x  %.4s  sz: %"PRIx64"  %"PRIx64"   %"PRIx64"\n", a.type, (char*)&a.type, a.size, atom.size, total_size);
        if (a.size == 1) { /* 64 bit extended size */
            a.size = get_be64(pb) - 8;
            a.offset += 8;
            total_size += 8;
        }
        if (a.size == 0) {
            a.size = atom.size - total_size;
            if (a.size <= 8)
                break;
        }
        for (i = 0; c->parse_table[i].type != 0L
             && c->parse_table[i].type != a.type; i++)
            /* empty */;

        a.size -= 8;

        if(a.size < 0)
            break;

        if (c->parse_table[i].type == 0) { /* skip leaf atoms data */
            url_fskip(pb, a.size);
        } else {
            offset_t start_pos = url_ftell(pb);
            int64_t left;
            err = (c->parse_table[i].func)(c, pb, a);
            left = a.size - url_ftell(pb) + start_pos;
            if (left > 0) /* skip garbage at atom end */
                url_fskip(pb, left);
        }

        a.offset += a.size;
        total_size += a.size;
    }

    if (!err && total_size < atom.size && atom.size < 0x7ffff) {
        url_fskip(pb, atom.size - total_size);
    }

    return err;
}

static int mov_read_ctab(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
#if 1
    url_fskip(pb, atom.size); // for now
#else
    VERY VERY BROKEN, NEVER execute this, needs rewrite
    unsigned int len;
    MOV_ctab_t *t;
    c->ctab = av_realloc(c->ctab, ++c->ctab_size);
    t = c->ctab[c->ctab_size];
    t->seed = get_be32(pb);
    t->flags = get_be16(pb);
    t->size = get_be16(pb) + 1;
    len = 2 * t->size * 4;
    if (len > 0) {
        t->clrs = av_malloc(len); // 16bit A R G B
        if (t->clrs)
            get_buffer(pb, t->clrs, len);
    }
#endif

    return 0;
}

static int mov_read_hdlr(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    int len = 0;
    uint32_t type;
    uint32_t ctype;

    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */

    /* component type */
    ctype = get_le32(pb);
    type = get_le32(pb); /* component subtype */

    dprintf("ctype= %c%c%c%c (0x%08lx)\n", *((char *)&ctype), ((char *)&ctype)[1], ((char *)&ctype)[2], ((char *)&ctype)[3], (long) ctype);
    dprintf("stype= %c%c%c%c\n", *((char *)&type), ((char *)&type)[1], ((char *)&type)[2], ((char *)&type)[3]);
    if(ctype == MKTAG('m', 'h', 'l', 'r')) /* MOV */
        c->mp4 = 0;
    else if(ctype == 0)
        c->mp4 = 1;
    if(type == MKTAG('v', 'i', 'd', 'e'))
        st->codec->codec_type = CODEC_TYPE_VIDEO;
    else if(type == MKTAG('s', 'o', 'u', 'n'))
        st->codec->codec_type = CODEC_TYPE_AUDIO;
    get_be32(pb); /* component  manufacture */
    get_be32(pb); /* component flags */
    get_be32(pb); /* component flags mask */

    if(atom.size <= 24)
        return 0; /* nothing left to read */
    /* XXX: MP4 uses a C string, not a pascal one */
    /* component name */

    if(c->mp4) {
        /* .mp4: C string */
        while(get_byte(pb) && (++len < (atom.size - 24)));
    } else {
        /* .mov: PASCAL string */
        len = get_byte(pb);
        url_fskip(pb, len);
    }

    url_fskip(pb, atom.size - (url_ftell(pb) - atom.offset));
    return 0;
}

static int mov_mp4_read_descr_len(ByteIOContext *pb)
{
    int len = 0;
    int count = 4;
    while (count--) {
        int c = get_byte(pb);
        len = (len << 7) | (c & 0x7f);
        if (!(c & 0x80))
            break;
    }
    return len;
}

static int mov_mp4_read_descr(ByteIOContext *pb, int *tag)
{
    int len;
    *tag = get_byte(pb);
    len = mov_mp4_read_descr_len(pb);
    dprintf("MPEG4 description: tag=0x%02x len=%d\n", *tag, len);
    return len;
}

static int mov_read_esds(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    MOVStreamContext *sc = (MOVStreamContext *)st->priv_data;
    int tag, len;

    /* Well, broken but suffisant for some MP4 streams */
    get_be32(pb); /* version + flags */
    len = mov_mp4_read_descr(pb, &tag);
    if (tag == MP4ESDescrTag) {
        get_be16(pb); /* ID */
        get_byte(pb); /* priority */
    } else
        get_be16(pb); /* ID */

    len = mov_mp4_read_descr(pb, &tag);
    if (tag == MP4DecConfigDescrTag) {
        sc->esds.object_type_id = get_byte(pb);
        sc->esds.stream_type = get_byte(pb);
        sc->esds.buffer_size_db = get_be24(pb);
        sc->esds.max_bitrate = get_be32(pb);
        sc->esds.avg_bitrate = get_be32(pb);

        st->codec->codec_id= codec_get_id(ff_mov_obj_type, sc->esds.object_type_id);
        len = mov_mp4_read_descr(pb, &tag);
        if (tag == MP4DecSpecificDescrTag) {
            dprintf("Specific MPEG4 header len=%d\n", len);
            st->codec->extradata = (uint8_t*) av_mallocz(len + FF_INPUT_BUFFER_PADDING_SIZE);
            if (st->codec->extradata) {
                get_buffer(pb, st->codec->extradata, len);
                st->codec->extradata_size = len;
                /* from mplayer */
                if ((*(uint8_t *)st->codec->extradata >> 3) == 29) {
                    st->codec->codec_id = CODEC_ID_MP3ON4;
                }
            }
        }
    }
    return 0;
}

/* this atom contains actual media data */
static int mov_read_mdat(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    if(atom.size == 0) /* wrong one (MP4) */
        return 0;
    c->mdat_list = av_realloc(c->mdat_list, (c->mdat_count + 1) * sizeof(*c->mdat_list));
    c->mdat_list[c->mdat_count].offset = atom.offset;
    c->mdat_list[c->mdat_count].size = atom.size;
    c->mdat_count++;
    c->found_mdat=1;
    c->mdat_offset = atom.offset;
    c->mdat_size = atom.size;
    if(c->found_moov)
        return 1; /* found both, just go */
    url_fskip(pb, atom.size);
    return 0; /* now go for moov */
}

static int mov_read_ftyp(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    uint32_t type = get_le32(pb);

    /* from mplayer */
    switch (type) {
    case MKTAG('i', 's', 'o', 'm'):
    case MKTAG('m', 'p', '4', '1'):
    case MKTAG('m', 'p', '4', '2'):
    case MKTAG('3', 'g', 'p', '1'):
    case MKTAG('3', 'g', 'p', '2'):
    case MKTAG('3', 'g', '2', 'a'):
    case MKTAG('3', 'g', 'p', '3'):
    case MKTAG('3', 'g', 'p', '4'):
    case MKTAG('3', 'g', 'p', '5'):
    case MKTAG('m', 'm', 'p', '4'): /* Mobile MP4 */
    case MKTAG('M', '4', 'A', ' '): /* Apple iTunes AAC-LC Audio */
    case MKTAG('M', '4', 'P', ' '): /* Apple iTunes AAC-LC Protected Audio */
    case MKTAG('m', 'j', 'p', '2'): /* Motion Jpeg 2000 */
        c->mp4 = 1;
    case MKTAG('q', 't', ' ', ' '):
    default:
        av_log(c->fc, AV_LOG_DEBUG, "ISO: File Type Major Brand: %.4s\n",(char *)&type);
    }
    get_be32(pb); /* minor version */
    url_fskip(pb, atom.size - 8);
    return 0;
}

/* this atom should contain all header atoms */
static int mov_read_moov(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    int err;

    err = mov_read_default(c, pb, atom);
    /* we parsed the 'moov' atom, we can terminate the parsing as soon as we find the 'mdat' */
    /* so we don't parse the whole file if over a network */
    c->found_moov=1;
    if(c->found_mdat)
        return 1; /* found both, just go */
    return 0; /* now go for mdat */
}


static int mov_read_mdhd(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    MOVStreamContext *sc = (MOVStreamContext *)st->priv_data;
    int version = get_byte(pb);
    int lang;

    if (version > 1)
        return 1; /* unsupported */

    get_byte(pb); get_byte(pb);
    get_byte(pb); /* flags */

    if (version == 1) {
        get_be64(pb);
        get_be64(pb);
    } else {
        get_be32(pb); /* creation time */
        get_be32(pb); /* modification time */
    }

    sc->time_scale = get_be32(pb);
    st->duration = (version == 1) ? get_be64(pb) : get_be32(pb); /* duration */

    lang = get_be16(pb); /* language */
    ff_mov_lang_to_iso639(lang, st->language);
    get_be16(pb); /* quality */

    return 0;
}

static int mov_read_mvhd(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    int version = get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */

    if (version == 1) {
        get_be64(pb);
        get_be64(pb);
    } else {
        get_be32(pb); /* creation time */
        get_be32(pb); /* modification time */
    }
    c->time_scale = get_be32(pb); /* time scale */
#ifdef DEBUG
    av_log(NULL, AV_LOG_DEBUG, "time scale = %i\n", c->time_scale);
#endif
    c->duration = (version == 1) ? get_be64(pb) : get_be32(pb); /* duration */
    get_be32(pb); /* preferred scale */

    get_be16(pb); /* preferred volume */

    url_fskip(pb, 10); /* reserved */

    url_fskip(pb, 36); /* display matrix */

    get_be32(pb); /* preview time */
    get_be32(pb); /* preview duration */
    get_be32(pb); /* poster time */
    get_be32(pb); /* selection time */
    get_be32(pb); /* selection duration */
    get_be32(pb); /* current time */
    get_be32(pb); /* next track ID */

    return 0;
}

static int mov_read_smi(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];

    if((uint64_t)atom.size > (1<<30))
        return -1;

    // currently SVQ3 decoder expect full STSD header - so let's fake it
    // this should be fixed and just SMI header should be passed
    av_free(st->codec->extradata);
    st->codec->extradata_size = 0x5a + atom.size;
    st->codec->extradata = (uint8_t*) av_mallocz(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);

    if (st->codec->extradata) {
        strcpy(st->codec->extradata, "SVQ3"); // fake
        get_buffer(pb, st->codec->extradata + 0x5a, atom.size);
        dprintf("Reading SMI %"PRId64"  %s\n", atom.size, (char*)st->codec->extradata + 0x5a);
    } else
        url_fskip(pb, atom.size);

    return 0;
}

static int mov_read_enda(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    int little_endian = get_be16(pb);

    if (little_endian) {
        switch (st->codec->codec_id) {
        case CODEC_ID_PCM_S24BE:
            st->codec->codec_id = CODEC_ID_PCM_S24LE;
            break;
        case CODEC_ID_PCM_S32BE:
            st->codec->codec_id = CODEC_ID_PCM_S32LE;
            break;
        default:
            break;
        }
    }
    return 0;
}

static int mov_read_alac(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];

    // currently ALAC decoder expect full atom header - so let's fake it
    // this should be fixed and just ALAC header should be passed

    av_free(st->codec->extradata);
    st->codec->extradata_size = 36;
    st->codec->extradata = (uint8_t*) av_mallocz(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);

    if (st->codec->extradata) {
        strcpy(st->codec->extradata + 4, "alac"); // fake
        get_buffer(pb, st->codec->extradata + 8, 36 - 8);
        dprintf("Reading alac %d  %s\n", st->codec->extradata_size, (char*)st->codec->extradata);
    } else
        url_fskip(pb, atom.size);
    return 0;
}

static int mov_read_wave(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];

    if((uint64_t)atom.size > (1<<30))
        return -1;

    if (st->codec->codec_id == CODEC_ID_QDM2) {
        // pass all frma atom to codec, needed at least for QDM2
        av_free(st->codec->extradata);
        st->codec->extradata_size = atom.size;
        st->codec->extradata = (uint8_t*) av_mallocz(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);

        if (st->codec->extradata) {
            get_buffer(pb, st->codec->extradata, atom.size);
        } else
            url_fskip(pb, atom.size);
    } else if (atom.size > 8) { /* to read frma, esds atoms */
        mov_read_default(c, pb, atom);
    } else
        url_fskip(pb, atom.size);
    return 0;
}

static int mov_read_jp2h(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];

    if((uint64_t)atom.size > (1<<30))
        return -1;

    av_free(st->codec->extradata);

    st->codec->extradata_size = atom.size + 8;
    st->codec->extradata = (uint8_t*) av_mallocz(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);

    /* pass all jp2h atom to codec */
    if (st->codec->extradata) {
        strcpy(st->codec->extradata + 4, "jp2h");
        get_buffer(pb, st->codec->extradata + 8, atom.size);
    } else
        url_fskip(pb, atom.size);
    return 0;
}

static int mov_read_avcC(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];

    if((uint64_t)atom.size > (1<<30))
        return -1;

    av_free(st->codec->extradata);

    st->codec->extradata_size = atom.size;
    st->codec->extradata = (uint8_t*) av_mallocz(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);

    if (st->codec->extradata) {
        get_buffer(pb, st->codec->extradata, atom.size);
    } else
        url_fskip(pb, atom.size);

    return 0;
}

static int mov_read_stco(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    MOVStreamContext *sc = (MOVStreamContext *)st->priv_data;
    unsigned int i, entries;

    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */

    entries = get_be32(pb);

    if(entries >= UINT_MAX/sizeof(int64_t))
        return -1;

    sc->chunk_count = entries;
    sc->chunk_offsets = (int64_t*) av_malloc(entries * sizeof(int64_t));
    if (!sc->chunk_offsets)
        return -1;
    if (atom.type == MKTAG('s', 't', 'c', 'o')) {
        for(i=0; i<entries; i++) {
            sc->chunk_offsets[i] = get_be32(pb);
        }
    } else if (atom.type == MKTAG('c', 'o', '6', '4')) {
        for(i=0; i<entries; i++) {
            sc->chunk_offsets[i] = get_be64(pb);
        }
    } else
        return -1;

    return 0;
}

static int mov_read_stsd(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    MOVStreamContext *sc = (MOVStreamContext *)st->priv_data;
    int entries, frames_per_sample;
    uint32_t format;
    uint8_t codec_name[32];

    /* for palette traversal */
    int color_depth;
    int color_start;
    int color_count;
    int color_end;
    int color_index;
    int color_dec;
    int color_greyscale;
    unsigned char *color_table;
    int j;
    unsigned char r, g, b;

    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */

    entries = get_be32(pb);

    while(entries--) { //Parsing Sample description table
        enum CodecID id;
        MOV_atom_t a = { 0, 0, 0 };
        offset_t start_pos = url_ftell(pb);
        int size = get_be32(pb); /* size */
        format = get_le32(pb); /* data format */

        get_be32(pb); /* reserved */
        get_be16(pb); /* reserved */
        get_be16(pb); /* index */

        if (st->codec->codec_tag) {
            /* multiple fourcc, just skip for now */
            url_fskip(pb, size - (url_ftell(pb) - start_pos));
            continue;
        }

        st->codec->codec_tag = format;
        id = codec_get_id(mov_audio_tags, format);
        if (id > 0) {
            st->codec->codec_type = CODEC_TYPE_AUDIO;
        } else if (format && format != MKTAG('m', 'p', '4', 's')) { /* skip old asf mpeg4 tag */
            id = codec_get_id(mov_video_tags, format);
            if (id <= 0)
                id = codec_get_id(codec_bmp_tags, format);
            if (id > 0)
                st->codec->codec_type = CODEC_TYPE_VIDEO;
        }

        dprintf("size=%d 4CC= %c%c%c%c codec_type=%d\n",
                size,
                (format >> 0) & 0xff, (format >> 8) & 0xff, (format >> 16) & 0xff, (format >> 24) & 0xff,
                st->codec->codec_type);

        if(st->codec->codec_type==CODEC_TYPE_VIDEO) {
            st->codec->codec_id = id;
            get_be16(pb); /* version */
            get_be16(pb); /* revision level */
            get_be32(pb); /* vendor */
            get_be32(pb); /* temporal quality */
            get_be32(pb); /* spacial quality */

            st->codec->width = get_be16(pb); /* width */
            st->codec->height = get_be16(pb); /* height */

            get_be32(pb); /* horiz resolution */
            get_be32(pb); /* vert resolution */
            get_be32(pb); /* data size, always 0 */
            frames_per_sample = get_be16(pb); /* frames per samples */
#ifdef DEBUG
            av_log(NULL, AV_LOG_DEBUG, "frames/samples = %d\n", frames_per_sample);
#endif
            get_buffer(pb, codec_name, 32); /* codec name, pascal string (FIXME: true for mp4?) */
            if (codec_name[0] <= 31) {
                memcpy(st->codec->codec_name, &codec_name[1],codec_name[0]);
                st->codec->codec_name[codec_name[0]] = 0;
            }

            st->codec->bits_per_sample = get_be16(pb); /* depth */
            st->codec->color_table_id = get_be16(pb); /* colortable id */

            /* figure out the palette situation */
            color_depth = st->codec->bits_per_sample & 0x1F;
            color_greyscale = st->codec->bits_per_sample & 0x20;

            /* if the depth is 2, 4, or 8 bpp, file is palettized */
            if ((color_depth == 2) || (color_depth == 4) ||
                (color_depth == 8)) {

                if (color_greyscale) {

                    /* compute the greyscale palette */
                    color_count = 1 << color_depth;
                    color_index = 255;
                    color_dec = 256 / (color_count - 1);
                    for (j = 0; j < color_count; j++) {
                        r = g = b = color_index;
                        c->palette_control.palette[j] =
                            (r << 16) | (g << 8) | (b);
                        color_index -= color_dec;
                        if (color_index < 0)
                            color_index = 0;
                    }

                } else if (st->codec->color_table_id & 0x08) {

                    /* if flag bit 3 is set, use the default palette */
                    color_count = 1 << color_depth;
                    if (color_depth == 2)
                        color_table = ff_qt_default_palette_4;
                    else if (color_depth == 4)
                        color_table = ff_qt_default_palette_16;
                    else
                        color_table = ff_qt_default_palette_256;

                    for (j = 0; j < color_count; j++) {
                        r = color_table[j * 4 + 0];
                        g = color_table[j * 4 + 1];
                        b = color_table[j * 4 + 2];
                        c->palette_control.palette[j] =
                            (r << 16) | (g << 8) | (b);
                    }

                } else {

                    /* load the palette from the file */
                    color_start = get_be32(pb);
                    color_count = get_be16(pb);
                    color_end = get_be16(pb);
                    for (j = color_start; j <= color_end; j++) {
                        /* each R, G, or B component is 16 bits;
                         * only use the top 8 bits; skip alpha bytes
                         * up front */
                        get_byte(pb);
                        get_byte(pb);
                        r = get_byte(pb);
                        get_byte(pb);
                        g = get_byte(pb);
                        get_byte(pb);
                        b = get_byte(pb);
                        get_byte(pb);
                        c->palette_control.palette[j] =
                            (r << 16) | (g << 8) | (b);
                    }
                }

                st->codec->palctrl = &c->palette_control;
                st->codec->palctrl->palette_changed = 1;
            } else
                st->codec->palctrl = NULL;
        } else if(st->codec->codec_type==CODEC_TYPE_AUDIO) {
            int bits_per_sample;
            uint16_t version = get_be16(pb);

            st->codec->codec_id = id;
            get_be16(pb); /* revision level */
            get_be32(pb); /* vendor */

            st->codec->channels = get_be16(pb);             /* channel count */
            st->codec->bits_per_sample = get_be16(pb);      /* sample size */
            /* do we need to force to 16 for AMR ? */

            /* handle specific s8 codec */
            get_be16(pb); /* compression id = 0*/
            get_be16(pb); /* packet size = 0 */

            st->codec->sample_rate = ((get_be32(pb) >> 16));

            switch (st->codec->codec_id) {
            case CODEC_ID_PCM_S8:
            case CODEC_ID_PCM_U8:
                if (st->codec->bits_per_sample == 16)
                    st->codec->codec_id = CODEC_ID_PCM_S16BE;
                break;
            case CODEC_ID_PCM_S16LE:
            case CODEC_ID_PCM_S16BE:
                if (st->codec->bits_per_sample == 8)
                    st->codec->codec_id = CODEC_ID_PCM_S8;
                break;
            case CODEC_ID_AMR_WB:
                st->codec->sample_rate = 16000; /* should really we ? */
                st->codec->channels=1; /* really needed */
                break;
            case CODEC_ID_AMR_NB:
                st->codec->sample_rate = 8000; /* should really we ? */
                st->codec->channels=1; /* really needed */
                break;
            default:
                break;
            }

            bits_per_sample = av_get_bits_per_sample(st->codec->codec_id);
            if (bits_per_sample) {
                st->codec->bits_per_sample = bits_per_sample;
                sc->sample_size = (bits_per_sample >> 3) * st->codec->channels;
            }

            //Read QT version 1 fields. In version 0 theese dont exist
            dprintf("version =%d mp4=%d\n",version,c->mp4);
            if(version==1) {
                sc->sample_size_v1.den = get_be32(pb); /* samples per packet */
                get_be32(pb); /* bytes per packet */
                sc->sample_size_v1.num = get_be32(pb); /* bytes per frame */
                get_be32(pb); /* bytes per sample */
            } else if(version==2) {
                get_be32(pb); /* sizeof struct only */
                st->codec->sample_rate = av_int2dbl(get_be64(pb)); /* float 64 */
                st->codec->channels = get_be32(pb);
                get_be32(pb); /* always 0x7F000000 */
                get_be32(pb); /* bits per channel if sound is uncompressed */
                get_be32(pb); /* lcpm format specific flag */
                get_be32(pb); /* bytes per audio packet if constant */
                get_be32(pb); /* lpcm frames per audio packet if constant */
            }
        } else {
            /* other codec type, just skip (rtp, mp4s, tmcd ...) */
            url_fskip(pb, size - (url_ftell(pb) - start_pos));
        }
        /* this will read extra atoms at the end (wave, alac, damr, avcC, SMI ...) */
        a.size = size - (url_ftell(pb) - start_pos);
        if (a.size > 8)
            mov_read_default(c, pb, a);
        else if (a.size > 0)
            url_fskip(pb, a.size);
    }

    if(st->codec->codec_type==CODEC_TYPE_AUDIO && st->codec->sample_rate==0 && sc->time_scale>1) {
        st->codec->sample_rate= sc->time_scale;
    }

    switch (st->codec->codec_id) {
#ifdef CONFIG_FAAD
    case CODEC_ID_AAC:
#endif
#ifdef CONFIG_VORBIS_DECODER
    case CODEC_ID_VORBIS:
#endif
    case CODEC_ID_MP3ON4:
        st->codec->sample_rate= 0; /* let decoder init parameters properly */
        break;
    default:
        break;
    }

    return 0;
}

static int mov_read_stsc(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    MOVStreamContext *sc = (MOVStreamContext *)st->priv_data;
    unsigned int i, entries;

    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */

    entries = get_be32(pb);

    if(entries >= UINT_MAX / sizeof(MOV_sample_to_chunk_tbl))
        return -1;

#ifdef DEBUG
av_log(NULL, AV_LOG_DEBUG, "track[%i].stsc.entries = %i\n", c->fc->nb_streams-1, entries);
#endif
    sc->sample_to_chunk_sz = entries;
    sc->sample_to_chunk = (MOV_sample_to_chunk_tbl*) av_malloc(entries * sizeof(MOV_sample_to_chunk_tbl));
    if (!sc->sample_to_chunk)
        return -1;
    for(i=0; i<entries; i++) {
        sc->sample_to_chunk[i].first = get_be32(pb);
        sc->sample_to_chunk[i].count = get_be32(pb);
        sc->sample_to_chunk[i].id = get_be32(pb);
    }
    return 0;
}

static int mov_read_stss(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    MOVStreamContext *sc = (MOVStreamContext *)st->priv_data;
    unsigned int i, entries;

    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */

    entries = get_be32(pb);

    if(entries >= UINT_MAX / sizeof(long))
        return -1;

    sc->keyframe_count = entries;
#ifdef DEBUG
    av_log(NULL, AV_LOG_DEBUG, "keyframe_count = %ld\n", sc->keyframe_count);
#endif
    sc->keyframes = (long*) av_malloc(entries * sizeof(long));
    if (!sc->keyframes)
        return -1;
    for(i=0; i<entries; i++) {
        sc->keyframes[i] = get_be32(pb);
#ifdef DEBUG
/*        av_log(NULL, AV_LOG_DEBUG, "keyframes[]=%ld\n", sc->keyframes[i]); */
#endif
    }
    return 0;
}

static int mov_read_stsz(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    MOVStreamContext *sc = (MOVStreamContext *)st->priv_data;
    unsigned int i, entries, sample_size;

    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */

    sample_size = get_be32(pb);
    if (!sc->sample_size) /* do not overwrite value computed in stsd */
        sc->sample_size = sample_size;
    entries = get_be32(pb);
    if(entries >= UINT_MAX / sizeof(long))
        return -1;

    sc->sample_count = entries;
    if (sample_size)
        return 0;

#ifdef DEBUG
    av_log(NULL, AV_LOG_DEBUG, "sample_size = %ld sample_count = %ld\n", sc->sample_size, sc->sample_count);
#endif
    sc->sample_sizes = (long*) av_malloc(entries * sizeof(long));
    if (!sc->sample_sizes)
        return -1;
    for(i=0; i<entries; i++) {
        sc->sample_sizes[i] = get_be32(pb);
#ifdef DEBUG
        av_log(NULL, AV_LOG_DEBUG, "sample_sizes[]=%ld\n", sc->sample_sizes[i]);
#endif
    }
    return 0;
}

static int mov_read_stts(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    MOVStreamContext *sc = (MOVStreamContext *)st->priv_data;
    unsigned int i, entries;
    int64_t duration=0;
    int64_t total_sample_count=0;

    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */
    entries = get_be32(pb);
    if(entries >= UINT_MAX / sizeof(Time2Sample))
        return -1;

    sc->stts_count = entries;
    sc->stts_data = av_malloc(entries * sizeof(Time2Sample));

#ifdef DEBUG
av_log(NULL, AV_LOG_DEBUG, "track[%i].stts.entries = %i\n", c->fc->nb_streams-1, entries);
#endif

    sc->time_rate=0;

    for(i=0; i<entries; i++) {
        int sample_duration;
        int sample_count;

        sample_count=get_be32(pb);
        sample_duration = get_be32(pb);
        sc->stts_data[i].count= sample_count;
        sc->stts_data[i].duration= sample_duration;

        sc->time_rate= ff_gcd(sc->time_rate, sample_duration);

        dprintf("sample_count=%d, sample_duration=%d\n",sample_count,sample_duration);

        duration+=(int64_t)sample_duration*sample_count;
        total_sample_count+=sample_count;
    }

    st->nb_frames= total_sample_count;
    if(duration)
        st->duration= duration;
    return 0;
}

static int mov_read_ctts(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    MOVStreamContext *sc = (MOVStreamContext *)st->priv_data;
    unsigned int i, entries;

    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */
    entries = get_be32(pb);
    if(entries >= UINT_MAX / sizeof(Time2Sample))
        return -1;

    sc->ctts_count = entries;
    sc->ctts_data = av_malloc(entries * sizeof(Time2Sample));

    dprintf("track[%i].ctts.entries = %i\n", c->fc->nb_streams-1, entries);

    for(i=0; i<entries; i++) {
        int count    =get_be32(pb);
        int duration =get_be32(pb);

        if (duration < 0) {
            av_log(c->fc, AV_LOG_ERROR, "negative ctts, ignoring\n");
            sc->ctts_count = 0;
            url_fskip(pb, 8 * (entries - i - 1));
            break;
        }
        sc->ctts_data[i].count   = count;
        sc->ctts_data[i].duration= duration;

        sc->time_rate= ff_gcd(sc->time_rate, duration);
    }
    return 0;
}

static int mov_read_trak(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st;
    MOVStreamContext *sc;

    st = av_new_stream(c->fc, c->fc->nb_streams);
    if (!st) return -2;
    sc = (MOVStreamContext*) av_mallocz(sizeof(MOVStreamContext));
    if (!sc) {
        av_free(st);
        return -1;
    }

    st->priv_data = sc;
    st->codec->codec_type = CODEC_TYPE_DATA;
    st->start_time = 0; /* XXX: check */
    c->streams[c->fc->nb_streams-1] = sc;

    return mov_read_default(c, pb, atom);
}

static int mov_read_tkhd(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    int version = get_byte(pb);

    get_byte(pb); get_byte(pb);
    get_byte(pb); /* flags */
    /*
    MOV_TRACK_ENABLED 0x0001
    MOV_TRACK_IN_MOVIE 0x0002
    MOV_TRACK_IN_PREVIEW 0x0004
    MOV_TRACK_IN_POSTER 0x0008
    */

    if (version == 1) {
        get_be64(pb);
        get_be64(pb);
    } else {
        get_be32(pb); /* creation time */
        get_be32(pb); /* modification time */
    }
    st->id = (int)get_be32(pb); /* track id (NOT 0 !)*/
    get_be32(pb); /* reserved */
    st->start_time = 0; /* check */
    (version == 1) ? get_be64(pb) : get_be32(pb); /* highlevel (considering edits) duration in movie timebase */
    get_be32(pb); /* reserved */
    get_be32(pb); /* reserved */

    get_be16(pb); /* layer */
    get_be16(pb); /* alternate group */
    get_be16(pb); /* volume */
    get_be16(pb); /* reserved */

    url_fskip(pb, 36); /* display matrix */

    /* those are fixed-point */
    get_be32(pb); /* track width */
    get_be32(pb); /* track height */

    return 0;
}

/* this atom should be null (from specs), but some buggy files put the 'moov' atom inside it... */
/* like the files created with Adobe Premiere 5.0, for samples see */
/* http://graphics.tudelft.nl/~wouter/publications/soundtests/ */
static int mov_read_wide(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    int err;

    if (atom.size < 8)
        return 0; /* continue */
    if (get_be32(pb) != 0) { /* 0 sized mdat atom... use the 'wide' atom size */
        url_fskip(pb, atom.size - 4);
        return 0;
    }
    atom.type = get_le32(pb);
    atom.offset += 8;
    atom.size -= 8;
    if (atom.type != MKTAG('m', 'd', 'a', 't')) {
        url_fskip(pb, atom.size);
        return 0;
    }
    err = mov_read_mdat(c, pb, atom);
    return err;
}


#ifdef CONFIG_ZLIB
static int null_read_packet(void *opaque, uint8_t *buf, int buf_size)
{
    return -1;
}

static int mov_read_cmov(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    ByteIOContext ctx;
    uint8_t *cmov_data;
    uint8_t *moov_data; /* uncompressed data */
    long cmov_len, moov_len;
    int ret;

    get_be32(pb); /* dcom atom */
    if (get_le32(pb) != MKTAG( 'd', 'c', 'o', 'm' ))
        return -1;
    if (get_le32(pb) != MKTAG( 'z', 'l', 'i', 'b' )) {
        av_log(NULL, AV_LOG_ERROR, "unknown compression for cmov atom !");
        return -1;
    }
    get_be32(pb); /* cmvd atom */
    if (get_le32(pb) != MKTAG( 'c', 'm', 'v', 'd' ))
        return -1;
    moov_len = get_be32(pb); /* uncompressed size */
    cmov_len = atom.size - 6 * 4;

    cmov_data = (uint8_t *) av_malloc(cmov_len);
    if (!cmov_data)
        return -1;
    moov_data = (uint8_t *) av_malloc(moov_len);
    if (!moov_data) {
        av_free(cmov_data);
        return -1;
    }
    get_buffer(pb, cmov_data, cmov_len);
    if(uncompress (moov_data, (uLongf *) &moov_len, (const Bytef *)cmov_data, cmov_len) != Z_OK)
        return -1;
    if(init_put_byte(&ctx, moov_data, moov_len, 0, NULL, null_read_packet, NULL, NULL) != 0)
        return -1;
    ctx.buf_end = ctx.buffer + moov_len;
    atom.type = MKTAG( 'm', 'o', 'o', 'v' );
    atom.offset = 0;
    atom.size = moov_len;
#ifdef DEBUG
//    { int fd = open("/tmp/uncompheader.mov", O_WRONLY | O_CREAT); write(fd, moov_data, moov_len); close(fd); }
#endif
    ret = mov_read_default(c, &ctx, atom);
    av_free(moov_data);
    av_free(cmov_data);

    return ret;
}
#endif

/* edit list atom */
static int mov_read_elst(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    int i, edit_count;

    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */
    edit_count= c->streams[c->fc->nb_streams-1]->edit_count = get_be32(pb);     /* entries */

    for(i=0; i<edit_count; i++){
        get_be32(pb); /* Track duration */
        get_be32(pb); /* Media time */
        get_be32(pb); /* Media rate */
    }
    dprintf("track[%i].edit_count = %i\n", c->fc->nb_streams-1, c->streams[c->fc->nb_streams-1]->edit_count);
    return 0;
}

static const MOVParseTableEntry mov_default_parse_table[] = {
/* mp4 atoms */
{ MKTAG( 'c', 'o', '6', '4' ), mov_read_stco },
{ MKTAG( 'c', 'p', 'r', 't' ), mov_read_default },
{ MKTAG( 'c', 'r', 'h', 'd' ), mov_read_default },
{ MKTAG( 'c', 't', 't', 's' ), mov_read_ctts }, /* composition time to sample */
{ MKTAG( 'd', 'i', 'n', 'f' ), mov_read_default }, /* data information */
{ MKTAG( 'd', 'p', 'n', 'd' ), mov_read_leaf },
{ MKTAG( 'd', 'r', 'e', 'f' ), mov_read_leaf },
{ MKTAG( 'e', 'd', 't', 's' ), mov_read_default },
{ MKTAG( 'e', 'l', 's', 't' ), mov_read_elst },
{ MKTAG( 'e', 'n', 'd', 'a' ), mov_read_enda },
{ MKTAG( 'f', 'r', 'e', 'e' ), mov_read_leaf },
{ MKTAG( 'f', 't', 'y', 'p' ), mov_read_ftyp },
{ MKTAG( 'h', 'd', 'l', 'r' ), mov_read_hdlr },
{ MKTAG( 'h', 'i', 'n', 't' ), mov_read_leaf },
{ MKTAG( 'h', 'm', 'h', 'd' ), mov_read_leaf },
{ MKTAG( 'i', 'o', 'd', 's' ), mov_read_leaf },
{ MKTAG( 'j', 'p', '2', 'h' ), mov_read_jp2h },
{ MKTAG( 'm', 'd', 'a', 't' ), mov_read_mdat },
{ MKTAG( 'm', 'd', 'h', 'd' ), mov_read_mdhd },
{ MKTAG( 'm', 'd', 'i', 'a' ), mov_read_default },
{ MKTAG( 'm', 'i', 'n', 'f' ), mov_read_default },
{ MKTAG( 'm', 'o', 'o', 'v' ), mov_read_moov },
{ MKTAG( 'm', 'p', '4', 'a' ), mov_read_default },
{ MKTAG( 'm', 'p', '4', 's' ), mov_read_default },
{ MKTAG( 'm', 'p', '4', 'v' ), mov_read_default },
{ MKTAG( 'm', 'p', 'o', 'd' ), mov_read_leaf },
{ MKTAG( 'm', 'v', 'h', 'd' ), mov_read_mvhd },
{ MKTAG( 'n', 'm', 'h', 'd' ), mov_read_leaf },
{ MKTAG( 'o', 'd', 'h', 'd' ), mov_read_default },
{ MKTAG( 's', 'd', 'h', 'd' ), mov_read_default },
{ MKTAG( 's', 'k', 'i', 'p' ), mov_read_leaf },
{ MKTAG( 's', 'm', 'h', 'd' ), mov_read_leaf }, /* sound media info header */
{ MKTAG( 'S', 'M', 'I', ' ' ), mov_read_smi }, /* Sorenson extension ??? */
{ MKTAG( 'a', 'l', 'a', 'c' ), mov_read_alac }, /* alac specific atom */
{ MKTAG( 'a', 'v', 'c', 'C' ), mov_read_avcC },
{ MKTAG( 's', 't', 'b', 'l' ), mov_read_default },
{ MKTAG( 's', 't', 'c', 'o' ), mov_read_stco },
{ MKTAG( 's', 't', 'd', 'p' ), mov_read_default },
{ MKTAG( 's', 't', 's', 'c' ), mov_read_stsc },
{ MKTAG( 's', 't', 's', 'd' ), mov_read_stsd }, /* sample description */
{ MKTAG( 's', 't', 's', 'h' ), mov_read_default },
{ MKTAG( 's', 't', 's', 's' ), mov_read_stss }, /* sync sample */
{ MKTAG( 's', 't', 's', 'z' ), mov_read_stsz }, /* sample size */
{ MKTAG( 's', 't', 't', 's' ), mov_read_stts },
{ MKTAG( 't', 'k', 'h', 'd' ), mov_read_tkhd }, /* track header */
{ MKTAG( 't', 'r', 'a', 'k' ), mov_read_trak },
{ MKTAG( 't', 'r', 'e', 'f' ), mov_read_default }, /* not really */
{ MKTAG( 'u', 'd', 't', 'a' ), mov_read_leaf },
{ MKTAG( 'u', 'r', 'l', ' ' ), mov_read_leaf },
{ MKTAG( 'u', 'r', 'n', ' ' ), mov_read_leaf },
{ MKTAG( 'u', 'u', 'i', 'd' ), mov_read_leaf },
{ MKTAG( 'v', 'm', 'h', 'd' ), mov_read_leaf }, /* video media info header */
{ MKTAG( 'w', 'a', 'v', 'e' ), mov_read_wave },
/* extra mp4 */
{ MKTAG( 'M', 'D', 'E', 'S' ), mov_read_leaf },
/* QT atoms */
{ MKTAG( 'c', 'h', 'a', 'p' ), mov_read_leaf },
{ MKTAG( 'c', 'l', 'i', 'p' ), mov_read_default },
{ MKTAG( 'c', 'r', 'g', 'n' ), mov_read_leaf },
{ MKTAG( 'c', 't', 'a', 'b' ), mov_read_ctab },
{ MKTAG( 'e', 's', 'd', 's' ), mov_read_esds },
{ MKTAG( 'k', 'm', 'a', 't' ), mov_read_leaf },
{ MKTAG( 'm', 'a', 't', 't' ), mov_read_default },
{ MKTAG( 'r', 'd', 'r', 'f' ), mov_read_leaf },
{ MKTAG( 'r', 'm', 'd', 'a' ), mov_read_default },
{ MKTAG( 'r', 'm', 'd', 'r' ), mov_read_leaf },
{ MKTAG( 'r', 'm', 'r', 'a' ), mov_read_default },
{ MKTAG( 's', 'c', 'p', 't' ), mov_read_leaf },
{ MKTAG( 's', 's', 'r', 'c' ), mov_read_leaf },
{ MKTAG( 's', 'y', 'n', 'c' ), mov_read_leaf },
{ MKTAG( 't', 'c', 'm', 'd' ), mov_read_leaf },
{ MKTAG( 'w', 'i', 'd', 'e' ), mov_read_wide }, /* place holder */
//{ MKTAG( 'r', 'm', 'q', 'u' ), mov_read_leaf },
#ifdef CONFIG_ZLIB
{ MKTAG( 'c', 'm', 'o', 'v' ), mov_read_cmov },
#else
{ MKTAG( 'c', 'm', 'o', 'v' ), mov_read_leaf },
#endif
{ 0L, mov_read_leaf }
};

static void mov_free_stream_context(MOVStreamContext *sc)
{
    if(sc) {
        av_freep(&sc->ctts_data);
        av_freep(&sc);
    }
}

/* XXX: is it sufficient ? */
static int mov_probe(AVProbeData *p)
{
    unsigned int offset;
    uint32_t tag;
    int score = 0;

    /* check file header */
    if (p->buf_size <= 12)
        return 0;
    offset = 0;
    for(;;) {
        /* ignore invalid offset */
        if ((offset + 8) > (unsigned int)p->buf_size)
            return score;
        tag = LE_32(p->buf + offset + 4);
        switch(tag) {
        /* check for obvious tags */
        case MKTAG( 'j', 'P', ' ', ' ' ): /* jpeg 2000 signature */
        case MKTAG( 'm', 'o', 'o', 'v' ):
        case MKTAG( 'm', 'd', 'a', 't' ):
        case MKTAG( 'p', 'n', 'o', 't' ): /* detect movs with preview pics like ew.mov and april.mov */
        case MKTAG( 'u', 'd', 't', 'a' ): /* Packet Video PVAuthor adds this and a lot of more junk */
            return AVPROBE_SCORE_MAX;
        /* those are more common words, so rate then a bit less */
        case MKTAG( 'w', 'i', 'd', 'e' ):
        case MKTAG( 'f', 'r', 'e', 'e' ):
        case MKTAG( 'j', 'u', 'n', 'k' ):
        case MKTAG( 'p', 'i', 'c', 't' ):
            return AVPROBE_SCORE_MAX - 5;
        case MKTAG( 'f', 't', 'y', 'p' ):
        case MKTAG( 's', 'k', 'i', 'p' ):
        case MKTAG( 'u', 'u', 'i', 'd' ):
            offset = BE_32(p->buf+offset) + offset;
            /* if we only find those cause probedata is too small at least rate them */
            score = AVPROBE_SCORE_MAX - 50;
            break;
        default:
            /* unrecognized tag */
            return score;
        }
    }
    return score;
}

static void mov_build_index(MOVContext *mov, AVStream *st)
{
    MOVStreamContext *sc = st->priv_data;
    offset_t current_offset;
    int64_t current_dts = 0;
    int stts_index = 0;
    int stsc_index = 0;
    int stss_index = 0;
    int i, j, k;

    if (sc->sample_sizes || st->codec->codec_type == CODEC_TYPE_VIDEO) {
        int keyframe, sample_size;
        int current_sample = 0;
        int stts_sample = 0;
        int distance = 0;

        st->nb_frames = sc->sample_count;
        for (i = 0; i < sc->chunk_count; i++) {
            current_offset = sc->chunk_offsets[i];
            if (stsc_index + 1 < sc->sample_to_chunk_sz && i + 1 == sc->sample_to_chunk[stsc_index + 1].first)
                stsc_index++;
            for (j = 0; j < sc->sample_to_chunk[stsc_index].count; j++) {
                keyframe = !sc->keyframe_count || current_sample + 1 == sc->keyframes[stss_index];
                if (keyframe) {
                    distance = 0;
                    if (stss_index + 1 < sc->keyframe_count)
                        stss_index++;
                }
                sample_size = sc->sample_size > 0 ? sc->sample_size : sc->sample_sizes[current_sample];
                dprintf("AVIndex stream %d, sample %d, offset %llx, dts %lld, size %d, distance %d, keyframe %d\n",
                        st->index, current_sample, current_offset, current_dts, sample_size, distance, keyframe);
                av_add_index_entry(st, current_offset, current_dts, sample_size, distance, keyframe ? AVINDEX_KEYFRAME : 0);
                current_offset += sample_size;
                assert(sc->stts_data[stts_index].duration % sc->time_rate == 0);
                current_dts += sc->stts_data[stts_index].duration / sc->time_rate;
                distance++;
                stts_sample++;
                if (current_sample + 1 < sc->sample_count)
                    current_sample++;
                if (stts_index + 1 < sc->stts_count && stts_sample == sc->stts_data[stts_index].count) {
                    stts_sample = 0;
                    stts_index++;
                }
            }
        }
    } else { /* read whole chunk */
        int chunk_samples, chunk_size, chunk_duration;

        for (i = 0; i < sc->chunk_count; i++) {
            current_offset = sc->chunk_offsets[i];
            if (stsc_index + 1 < sc->sample_to_chunk_sz && i + 1 == sc->sample_to_chunk[stsc_index + 1].first)
                stsc_index++;
            chunk_samples = sc->sample_to_chunk[stsc_index].count;
            /* get chunk size */
            if (sc->sample_size > 1)
                chunk_size = chunk_samples * sc->sample_size;
            else if (sc->sample_size_v1.den > 0 && (chunk_samples * sc->sample_size_v1.num % sc->sample_size_v1.den == 0))
                chunk_size = chunk_samples * sc->sample_size_v1.num / sc->sample_size_v1.den;
            else { /* workaround to find nearest next chunk offset */
                chunk_size = INT_MAX;
                for (j = 0; j < mov->total_streams; j++) {
                    MOVStreamContext *msc = mov->streams[j];

                    for (k = msc->next_chunk; k < msc->chunk_count; k++) {
                        if (msc->chunk_offsets[k] > current_offset && msc->chunk_offsets[k] - current_offset < chunk_size) {
                            chunk_size = msc->chunk_offsets[k] - current_offset;
                            msc->next_chunk = k;
                            break;
                        }
                    }
                }
                /* check for last chunk */
                if (chunk_size == INT_MAX)
                    for (j = 0; j < mov->mdat_count; j++) {
                        dprintf("mdat %d, offset %llx, size %lld, current offset %llx\n",
                                j, mov->mdat_list[j].offset, mov->mdat_list[j].size, current_offset);
                        if (mov->mdat_list[j].offset <= current_offset && mov->mdat_list[j].offset + mov->mdat_list[j].size > current_offset)
                            chunk_size = mov->mdat_list[j].offset + mov->mdat_list[j].size - current_offset;
                    }
                assert(chunk_size != INT_MAX);
                for (j = 0; j < mov->total_streams; j++) {
                    mov->streams[j]->next_chunk = 0;
                }
            }
            av_add_index_entry(st, current_offset, current_dts, chunk_size, 0, AVINDEX_KEYFRAME);
            /* get chunk duration */
            chunk_duration = 0;
            while (chunk_samples > 0) {
                if (chunk_samples < sc->stts_data[stts_index].count) {
                    chunk_duration += sc->stts_data[stts_index].duration * chunk_samples;
                    sc->stts_data[stts_index].count -= chunk_samples;
                    break;
                } else {
                    chunk_duration += sc->stts_data[stts_index].duration * chunk_samples;
                    chunk_samples -= sc->stts_data[stts_index].count;
                    if (stts_index + 1 < sc->stts_count) {
                        stts_index++;
                    }
                }
            }
            dprintf("AVIndex stream %d, chunk %d, offset %llx, dts %lld, size %d, duration %d\n",
                    st->index, i, current_offset, current_dts, chunk_size, chunk_duration);
            assert(chunk_duration % sc->time_rate == 0);
            current_dts += chunk_duration / sc->time_rate;
        }
    }
    /* adjust sample count to avindex entries */
    sc->sample_count = st->nb_index_entries;
}

static int mov_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    MOVContext *mov = (MOVContext *) s->priv_data;
    ByteIOContext *pb = &s->pb;
    int i, err;
    MOV_atom_t atom = { 0, 0, 0 };

    mov->fc = s;
    mov->parse_table = mov_default_parse_table;

    if(!url_is_streamed(pb)) /* .mov and .mp4 aren't streamable anyway (only progressive download if moov is before mdat) */
        atom.size = url_fsize(pb);
    else
        atom.size = 0x7FFFFFFFFFFFFFFFLL;

    /* check MOV header */
    err = mov_read_default(mov, pb, atom);
    if (err<0 || (!mov->found_moov && !mov->found_mdat)) {
        av_log(s, AV_LOG_ERROR, "mov: header not found !!! (err:%d, moov:%d, mdat:%d) pos:%"PRId64"\n",
                err, mov->found_moov, mov->found_mdat, url_ftell(pb));
        return -1;
    }
    dprintf("on_parse_exit_offset=%d\n", (int) url_ftell(pb));

    /* some cleanup : make sure we are on the mdat atom */
    if(!url_is_streamed(pb) && (url_ftell(pb) != mov->mdat_offset))
        url_fseek(pb, mov->mdat_offset, SEEK_SET);

    mov->total_streams = s->nb_streams;

    for(i=0; i<mov->total_streams; i++) {
        MOVStreamContext *sc = mov->streams[i];

        if(!sc->time_rate)
            sc->time_rate=1;
        if(!sc->time_scale)
            sc->time_scale= mov->time_scale;
        av_set_pts_info(s->streams[i], 64, sc->time_rate, sc->time_scale);

        if(s->streams[i]->duration != AV_NOPTS_VALUE){
            assert(s->streams[i]->duration % sc->time_rate == 0);
            s->streams[i]->duration /= sc->time_rate;
        }
        sc->ffindex = i;
        mov_build_index(mov, s->streams[i]);
    }

    for(i=0; i<mov->total_streams; i++) {
        /* dont need those anymore */
        av_freep(&mov->streams[i]->chunk_offsets);
        av_freep(&mov->streams[i]->sample_to_chunk);
        av_freep(&mov->streams[i]->sample_sizes);
        av_freep(&mov->streams[i]->keyframes);
        av_freep(&mov->streams[i]->stts_data);
    }
    av_freep(&mov->mdat_list);
    return 0;
}

static int mov_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MOVContext *mov = s->priv_data;
    MOVStreamContext *sc = 0;
    AVIndexEntry *sample = 0;
    int64_t best_dts = INT64_MAX;
    int i;

    for (i = 0; i < mov->total_streams; i++) {
        MOVStreamContext *msc = mov->streams[i];

        if (s->streams[i]->discard != AVDISCARD_ALL && msc->current_sample < msc->sample_count) {
            AVIndexEntry *current_sample = &s->streams[i]->index_entries[msc->current_sample];
            int64_t dts = av_rescale(current_sample->timestamp * (int64_t)msc->time_rate, AV_TIME_BASE, msc->time_scale);

            dprintf("stream %d, sample %ld, dts %lld\n", i, msc->current_sample, dts);
            if (dts < best_dts) {
                sample = current_sample;
                best_dts = dts;
                sc = msc;
            }
        }
    }
    if (!sample)
        return -1;
    /* must be done just before reading, to avoid infinite loop on sample */
    sc->current_sample++;
    if (sample->pos >= url_fsize(&s->pb)) {
        av_log(mov->fc, AV_LOG_ERROR, "stream %d, offset 0x%llx: partial file\n", sc->ffindex, sample->pos);
        return -1;
    }
    url_fseek(&s->pb, sample->pos, SEEK_SET);
    av_get_packet(&s->pb, pkt, sample->size);
    pkt->stream_index = sc->ffindex;
    pkt->dts = sample->timestamp;
    if (sc->ctts_data) {
        assert(sc->ctts_data[sc->sample_to_ctime_index].duration % sc->time_rate == 0);
        pkt->pts = pkt->dts + sc->ctts_data[sc->sample_to_ctime_index].duration / sc->time_rate;
        /* update ctts context */
        sc->sample_to_ctime_sample++;
        if (sc->sample_to_ctime_index < sc->ctts_count && sc->ctts_data[sc->sample_to_ctime_index].count == sc->sample_to_ctime_sample) {
            sc->sample_to_ctime_index++;
            sc->sample_to_ctime_sample = 0;
        }
    } else {
        pkt->pts = pkt->dts;
    }
    pkt->flags |= sample->flags & AVINDEX_KEYFRAME ? PKT_FLAG_KEY : 0;
    pkt->pos = sample->pos;
    dprintf("stream %d, pts %lld, dts %lld, pos 0x%llx, duration %d\n", pkt->stream_index, pkt->pts, pkt->dts, pkt->pos, pkt->duration);
    return 0;
}

static int mov_seek_stream(AVStream *st, int64_t timestamp, int flags)
{
    MOVStreamContext *sc = st->priv_data;
    int sample, time_sample;
    int i;

    sample = av_index_search_timestamp(st, timestamp, flags);
    dprintf("stream %d, timestamp %lld, sample %d\n", st->index, timestamp, sample);
    if (sample < 0) /* not sure what to do */
        return -1;
    sc->current_sample = sample;
    dprintf("stream %d, found sample %ld\n", st->index, sc->current_sample);
    /* adjust ctts index */
    if (sc->ctts_data) {
        time_sample = 0;
        for (i = 0; i < sc->ctts_count; i++) {
            time_sample += sc->ctts_data[i].count;
            if (time_sample >= sc->current_sample) {
                sc->sample_to_ctime_index = i;
                sc->sample_to_ctime_sample = time_sample - sc->current_sample;
                break;
            }
        }
    }
    return sample;
}

static int mov_read_seek(AVFormatContext *s, int stream_index, int64_t sample_time, int flags)
{
    AVStream *st;
    int64_t seek_timestamp, timestamp;
    int sample;
    int i;

    if (stream_index >= s->nb_streams)
        return -1;

    st = s->streams[stream_index];
    sample = mov_seek_stream(st, sample_time, flags);
    if (sample < 0)
        return -1;

    /* adjust seek timestamp to found sample timestamp */
    seek_timestamp = st->index_entries[sample].timestamp;

    for (i = 0; i < s->nb_streams; i++) {
        st = s->streams[i];
        if (stream_index == i || st->discard == AVDISCARD_ALL)
            continue;

        timestamp = av_rescale_q(seek_timestamp, s->streams[stream_index]->time_base, st->time_base);
        mov_seek_stream(st, timestamp, flags);
    }
    return 0;
}

static int mov_read_close(AVFormatContext *s)
{
    int i;
    MOVContext *mov = (MOVContext *) s->priv_data;
    for(i=0; i<mov->total_streams; i++)
        mov_free_stream_context(mov->streams[i]);
    /* free color tabs */
    for(i=0; i<mov->ctab_size; i++)
        av_freep(&mov->ctab[i]);
    av_freep(&mov->ctab);
    return 0;
}

static AVInputFormat mov_demuxer = {
    "mov,mp4,m4a,3gp,3g2,mj2",
    "QuickTime/MPEG4/Motion JPEG 2000 format",
    sizeof(MOVContext),
    mov_probe,
    mov_read_header,
    mov_read_packet,
    mov_read_close,
    mov_read_seek,
};

int mov_init(void)
{
    av_register_input_format(&mov_demuxer);
    return 0;
}
