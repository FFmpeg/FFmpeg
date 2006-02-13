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

/* Allows seeking */
#define MOV_SEEK

/* Special handling for movies created with Minolta Dimaxe Xi*/
/* this fix should not interfere with other .mov files, but just in case*/
#define MOV_MINOLTA_FIX

/* some streams in QT (and in MP4 mostly) aren't either video nor audio */
/* so we first list them as this, then clean up the list of streams we give back, */
/* getting rid of these */
#define CODEC_TYPE_MOV_OTHER    (enum CodecType) 2

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
    { CODEC_ID_VORBIS    , 225 },
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
    { CODEC_ID_MJPEG, MKTAG('m', 'j', 'p', 'b') }, /* Motion-JPEG (format B) */
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
    { CODEC_ID_NONE, 0 },
};

static const CodecTag mov_audio_tags[] = {
/*    { CODEC_ID_PCM_S16BE, MKTAG('N', 'O', 'N', 'E') }, *//* uncompressed */
    { CODEC_ID_PCM_S16BE, MKTAG('t', 'w', 'o', 's') }, /* 16 bits */
    /* { CODEC_ID_PCM_S8, MKTAG('t', 'w', 'o', 's') },*/ /* 8 bits */
    { CODEC_ID_PCM_U8, MKTAG('r', 'a', 'w', ' ') }, /* 8 bits unsigned */
    { CODEC_ID_PCM_S16LE, MKTAG('s', 'o', 'w', 't') }, /*  */
    { CODEC_ID_PCM_MULAW, MKTAG('u', 'l', 'a', 'w') }, /*  */
    { CODEC_ID_PCM_ALAW, MKTAG('a', 'l', 'a', 'w') }, /*  */
    { CODEC_ID_ADPCM_IMA_QT, MKTAG('i', 'm', 'a', '4') }, /* IMA-4 ADPCM */
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

typedef struct Time2Sample{
    int count;
    int duration;
}Time2Sample;

typedef struct MOVStreamContext {
    int ffindex; /* the ffmpeg stream id */
    int is_ff_stream; /* Is this stream presented to ffmpeg ? i.e. is this an audio or video stream ? */
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
    long sample_to_chunk_index;
    int sample_to_time_index;
    long sample_to_time_sample;
    uint64_t sample_to_time_time;
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
    long left_in_chunk; /* how many samples before next chunk */
    MOV_esds_t esds;
} MOVStreamContext;

typedef struct MOVContext {
    int mp4; /* set to 1 as soon as we are sure that the file is an .mp4 file (even some header parsing depends on this) */
    AVFormatContext *fc;
    int time_scale;
    int duration; /* duration of the longest track */
    int found_moov; /* when both 'moov' and 'mdat' sections has been found */
    int found_mdat; /* we suppose we have enough data to read the file */
    int64_t mdat_size;
    int64_t mdat_offset;
    int ni;                                         ///< non interleaved mode
    int total_streams;
    /* some streams listed here aren't presented to the ffmpeg API, since they aren't either video nor audio
     * but we need the info to be able to skip data from those streams in the 'mdat' section
     */
    MOVStreamContext *streams[MAX_STREAMS];

    int64_t next_chunk_offset;
    MOVStreamContext *partial; /* != 0 : there is still to read in the current chunk */
    int ctab_size;
    MOV_ctab_t **ctab;           /* color tables */
    const struct MOVParseTableEntry *parse_table; /* could be eventually used to change the table */
    /* NOTE: for recursion save to/ restore from local variable! */

    AVPaletteControl palette_control;
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

extern int ff_mov_iso639_to_lang(const char *lang, int mp4); /* for movenc.c */
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
        dprintf("type: %08x  %.4s  sz: %Lx  %Lx   %Lx\n", a.type, (char*)&a.type, a.size, atom.size, total_size);
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
            err = (c->parse_table[i].func)(c, pb, a);
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
    if(ctype == MKTAG('m', 'h', 'l', 'r')) { /* MOV */
        /* helps parsing the string hereafter... */
        c->mp4 = 0;
        if(type == MKTAG('v', 'i', 'd', 'e'))
            st->codec->codec_type = CODEC_TYPE_VIDEO;
        else if(type == MKTAG('s', 'o', 'u', 'n'))
            st->codec->codec_type = CODEC_TYPE_AUDIO;
    } else if(ctype == 0) { /* MP4 */
        /* helps parsing the string hereafter... */
        c->mp4 = 1;
        if(type == MKTAG('v', 'i', 'd', 'e'))
            st->codec->codec_type = CODEC_TYPE_VIDEO;
        else if(type == MKTAG('s', 'o', 'u', 'n'))
            st->codec->codec_type = CODEC_TYPE_AUDIO;
    }
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
    int64_t start_pos = url_ftell(pb);
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
            }
        }
    }
    /* in any case, skip garbage */
    url_fskip(pb, atom.size - ((url_ftell(pb) - start_pos)));
    return 0;
}

/* this atom contains actual media data */
static int mov_read_mdat(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    if(atom.size == 0) /* wrong one (MP4) */
        return 0;
    c->found_mdat=1;
    c->mdat_offset = atom.offset;
    c->mdat_size = atom.size;
    if(c->found_moov)
        return 1; /* found both, just go */
    url_fskip(pb, atom.size);
    return 0; /* now go for moov */
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
    int version;
    int lang;

    version = get_byte(pb); /* version */
    if (version > 1)
        return 1; /* unsupported */

    get_byte(pb); get_byte(pb);
    get_byte(pb); /* flags */

    (version==1)?get_be64(pb):get_be32(pb); /* creation time */
    (version==1)?get_be64(pb):get_be32(pb); /* modification time */

    c->streams[c->fc->nb_streams-1]->time_scale = get_be32(pb);
    c->fc->streams[c->fc->nb_streams-1]->duration = (version==1)?get_be64(pb):get_be32(pb); /* duration */

    lang = get_be16(pb); /* language */
    ff_mov_lang_to_iso639(lang, c->fc->streams[c->fc->nb_streams-1]->language);
    get_be16(pb); /* quality */

    return 0;
}

static int mov_read_mvhd(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */

    get_be32(pb); /* creation time */
    get_be32(pb); /* modification time */
    c->time_scale = get_be32(pb); /* time scale */
#ifdef DEBUG
    av_log(NULL, AV_LOG_DEBUG, "time scale = %i\n", c->time_scale);
#endif
    c->duration = get_be32(pb); /* duration */
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
        dprintf("Reading SMI %Ld  %s\n", atom.size, (char*)st->codec->extradata + 0x5a);
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
    } else if (atom.size > 0)
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

    for(i=0; i<c->fc->nb_streams; i++){
        MOVStreamContext *sc2 = (MOVStreamContext *)c->fc->streams[i]->priv_data;
        if(sc2 && sc2->chunk_offsets){
            int64_t first= sc2->chunk_offsets[0];
            int64_t last= sc2->chunk_offsets[sc2->chunk_count-1];
            if(first >= sc->chunk_offsets[entries-1] || last <= sc->chunk_offsets[0])
                c->ni=1;
        }
    }
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
        offset_t start_pos = url_ftell(pb);
        int size = get_be32(pb); /* size */
        format = get_le32(pb); /* data format */

        get_be32(pb); /* reserved */
        get_be16(pb); /* reserved */
        get_be16(pb); /* index */

        /* for MPEG4: set codec type by looking for it */
        id = codec_get_id(mov_video_tags, format);
        if(id <= 0)
            id = codec_get_id(codec_bmp_tags, format);
        if (id >= 0) {
            AVCodec *codec;
            codec = avcodec_find_decoder(id);
            if (codec)
                st->codec->codec_type = codec->type;
        }
        dprintf("size=%d 4CC= %c%c%c%c codec_type=%d\n",
                size,
                (format >> 0) & 0xff, (format >> 8) & 0xff, (format >> 16) & 0xff, (format >> 24) & 0xff,
                st->codec->codec_type);
        st->codec->codec_tag = format;
        if(st->codec->codec_type==CODEC_TYPE_VIDEO) {
            MOV_atom_t a = { 0, 0, 0 };
            st->codec->codec_id = id;
            get_be16(pb); /* version */
            get_be16(pb); /* revision level */
            get_be32(pb); /* vendor */
            get_be32(pb); /* temporal quality */
            get_be32(pb); /* spacial quality */
            if(st->codec->codec_id == CODEC_ID_MPEG4){ //FIXME this is silly
                get_be16(pb);
                get_be16(pb);
            }else{
                st->codec->width = get_be16(pb); /* width */
                st->codec->height = get_be16(pb); /* height */
            }
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

/*          These are set in mov_read_stts and might already be set!
            st->codec->time_base.den      = 25;
            st->codec->time_base.num = 1;
*/

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

            a.size = size - (url_ftell(pb) - start_pos);
            if (a.size > 8)
                mov_read_default(c, pb, a);
            else if (a.size > 0)
                url_fskip(pb, a.size);
        } else {
            st->codec->codec_id = codec_get_id(mov_audio_tags, format);
            if(st->codec->codec_id==CODEC_ID_AMR_NB || st->codec->codec_id==CODEC_ID_AMR_WB) //from TS26.244
            {
               dprintf("AMR audio identified %d!!\n", st->codec->codec_id);
               get_be32(pb);get_be32(pb); //Reserved_8
               get_be16(pb);//Reserved_2
               get_be16(pb);//Reserved_2
               get_be32(pb);//Reserved_4
               get_be16(pb);//TimeScale
               get_be16(pb);//Reserved_2

                //AMRSpecificBox.(10 bytes)

               get_be32(pb); //size
               get_be32(pb); //type=='damr'
               get_be32(pb); //vendor
               get_byte(pb); //decoder version
               get_be16(pb); //mode_set
               get_byte(pb); //mode_change_period
               get_byte(pb); //frames_per_sample

               st->duration = AV_NOPTS_VALUE;//Not possible to get from this info, must count number of AMR frames
               if(st->codec->codec_id==CODEC_ID_AMR_NB)
               {
                   st->codec->sample_rate=8000;
                   st->codec->channels=1;
               }
               else //AMR-WB
               {
                   st->codec->sample_rate=16000;
                   st->codec->channels=1;
               }
               st->codec->bits_per_sample=16;
               st->codec->bit_rate=0; /*It is not possible to tell this before we have
                                       an audio frame and even then every frame can be different*/
            }
            else if( st->codec->codec_tag == MKTAG( 'm', 'p', '4', 's' ))
            {
                //This is some stuff for the hint track, lets ignore it!
                //Do some mp4 auto detect.
                c->mp4=1;
                size-=(16);
                url_fskip(pb, size); /* The mp4s atom also contians a esds atom that we can skip*/
            }
            else if( st->codec->codec_tag == MKTAG( 'm', 'p', '4', 'a' ))
            {
                MOV_atom_t a;
                int mp4_version;

                /* Handle mp4 audio tag */
                mp4_version=get_be16(pb);/*version*/
                get_be16(pb); /*revesion*/
                get_be32(pb);
                st->codec->channels = get_be16(pb); /* channels */
                st->codec->bits_per_sample = get_be16(pb); /* bits per sample */
                get_be32(pb);
                st->codec->sample_rate = get_be16(pb); /* sample rate, not always correct */
                if(st->codec->sample_rate == 1) //nonsese rate? -> ignore
                    st->codec->sample_rate= 0;

                get_be16(pb);
                c->mp4=1;

                if(mp4_version==1)
                {
                    url_fskip(pb,16);
                    a.size=size-(16+20+16);
                }
                else
                    a.size=size-(16+20);

                a.offset=url_ftell(pb);

                mov_read_default(c, pb, a);

                /* Get correct sample rate from extradata */
                if(st->codec->extradata_size) {
                   const int samplerate_table[] = {
                     96000, 88200, 64000, 48000, 44100, 32000,
                     24000, 22050, 16000, 12000, 11025, 8000,
                     7350, 0, 0, 0
                   };
                   unsigned char *px = st->codec->extradata;
                   // 5 bits objectTypeIndex, 4 bits sampleRateIndex, 4 bits channels
                   int samplerate_index = ((px[0] & 7) << 1) + ((px[1] >> 7) & 1);
                   st->codec->sample_rate = samplerate_table[samplerate_index];
                   st->codec->channels = (px[1] >> 3) & 15;
                }
            }
            else if( st->codec->codec_tag == MKTAG( 'a', 'l', 'a', 'c' ))
            {
                /* Handle alac audio tag + special extradata */
                get_be32(pb); /* version */
                get_be32(pb);
                st->codec->channels = get_be16(pb); /* channels */
                st->codec->bits_per_sample = get_be16(pb); /* bits per sample */
                get_be32(pb);
                st->codec->sample_rate = get_be16(pb);
                get_be16(pb);

                /* fetch the 36-byte extradata needed for alac decoding */
                st->codec->extradata_size = 36;
                st->codec->extradata = (uint8_t*)
                    av_mallocz(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
                get_buffer(pb, st->codec->extradata, st->codec->extradata_size);
            }
            else if(size>=(16+20))
            {//16 bytes read, reading atleast 20 more
                uint16_t version;
                version = get_be16(pb); /* version */
                get_be16(pb); /* revision level */
                get_be32(pb); /* vendor */

                st->codec->channels = get_be16(pb);             /* channel count */
                st->codec->bits_per_sample = get_be16(pb);      /* sample size */

                /* handle specific s8 codec */
                get_be16(pb); /* compression id = 0*/
                get_be16(pb); /* packet size = 0 */

                st->codec->sample_rate = ((get_be32(pb) >> 16));

                switch (st->codec->codec_id) {
                case CODEC_ID_PCM_S16BE:
                    if (st->codec->bits_per_sample == 8)
                        st->codec->codec_id = CODEC_ID_PCM_S8;
                    /* fall */
                case CODEC_ID_PCM_U8:
                    st->codec->bit_rate = st->codec->sample_rate * 8;
                    break;
                default:
                    ;
                }

                //Read QT version 1 fields. In version 0 theese dont exist
                dprintf("version =%d mp4=%d\n",version,c->mp4);
                if((version==1) && size>=(16+20+16))
                {
                    get_be32(pb); /* samples per packet */
                    get_be32(pb); /* bytes per packet */
                    get_be32(pb); /* bytes per frame */
                    get_be32(pb); /* bytes per sample */
                    if(size>(16+20+16))
                    {
                        //Optional, additional atom-based fields
                        MOV_atom_t a = { format, url_ftell(pb), size - (16 + 20 + 16 + 8) };
                        mov_read_default(c, pb, a);
                    }
                }
                else
                {
                    //We should be down to 0 bytes here, but lets make sure.
                    size-=(16+20);
                    if(size>0) {
                        dprintf("skipping 0x%X bytes\n",size-(16+20));
                        url_fskip(pb, size);
                    }
                }
            }
            else
            {
                size-=16;
                //Unknown size, but lets do our best and skip the rest.
                dprintf("Strange size, skipping 0x%X bytes\n",size);
                url_fskip(pb, size);
            }
        }
    }

    if(st->codec->codec_type==CODEC_TYPE_AUDIO && st->codec->sample_rate==0 && sc->time_scale>1) {
        st->codec->sample_rate= sc->time_scale;
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
    unsigned int i, entries;

    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */

    sc->sample_size = get_be32(pb);
    entries = get_be32(pb);
    if(entries >= UINT_MAX / sizeof(long))
        return -1;

    sc->sample_count = entries;
#ifdef DEBUG
    av_log(NULL, AV_LOG_DEBUG, "sample_size = %ld sample_count = %ld\n", sc->sample_size, sc->sample_count);
#endif
    if(sc->sample_size)
        return 0; /* there isn't any table following */
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

        duration+=sample_duration*sample_count;
        total_sample_count+=sample_count;
    }

    st->nb_frames= total_sample_count;
    if(duration)
        st->duration= duration;
    return 0;
}

static int mov_read_ctts(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    unsigned int i, entries;

    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */
    entries = get_be32(pb);
    if(entries >= UINT_MAX / sizeof(Time2Sample))
        return -1;

    c->streams[c->fc->nb_streams-1]->ctts_count = entries;
    c->streams[c->fc->nb_streams-1]->ctts_data = av_malloc(entries * sizeof(Time2Sample));

    dprintf("track[%i].ctts.entries = %i\n", c->fc->nb_streams-1, entries);

    for(i=0; i<entries; i++) {
        c->streams[c->fc->nb_streams - 1]->ctts_data[i].count= get_be32(pb);
        c->streams[c->fc->nb_streams - 1]->ctts_data[i].duration= get_be32(pb);
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

    sc->sample_to_chunk_index = -1;
    st->priv_data = sc;
    st->codec->codec_type = CODEC_TYPE_MOV_OTHER;
    st->start_time = 0; /* XXX: check */
    c->streams[c->fc->nb_streams-1] = sc;

    return mov_read_default(c, pb, atom);
}

static int mov_read_tkhd(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st;

    st = c->fc->streams[c->fc->nb_streams-1];

    get_byte(pb); /* version */

    get_byte(pb); get_byte(pb);
    get_byte(pb); /* flags */
    /*
    MOV_TRACK_ENABLED 0x0001
    MOV_TRACK_IN_MOVIE 0x0002
    MOV_TRACK_IN_PREVIEW 0x0004
    MOV_TRACK_IN_POSTER 0x0008
    */

    get_be32(pb); /* creation time */
    get_be32(pb); /* modification time */
    st->id = (int)get_be32(pb); /* track id (NOT 0 !)*/
    get_be32(pb); /* reserved */
    st->start_time = 0; /* check */
    get_be32(pb); /* highlevel (considering edits) duration in movie timebase */
    get_be32(pb); /* reserved */
    get_be32(pb); /* reserved */

    get_be16(pb); /* layer */
    get_be16(pb); /* alternate group */
    get_be16(pb); /* volume */
    get_be16(pb); /* reserved */

    url_fskip(pb, 36); /* display matrix */

    /* those are fixed-point */
    /*st->codec->width =*/ get_be32(pb) >> 16; /* track width */
    /*st->codec->height =*/ get_be32(pb) >> 16; /* track height */

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
{ MKTAG( 'f', 'r', 'e', 'e' ), mov_read_leaf },
{ MKTAG( 'h', 'd', 'l', 'r' ), mov_read_hdlr },
{ MKTAG( 'h', 'i', 'n', 't' ), mov_read_leaf },
{ MKTAG( 'h', 'm', 'h', 'd' ), mov_read_leaf },
{ MKTAG( 'i', 'o', 'd', 's' ), mov_read_leaf },
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
        av_freep(&sc->chunk_offsets);
        av_freep(&sc->sample_to_chunk);
        av_freep(&sc->sample_sizes);
        av_freep(&sc->keyframes);
        av_freep(&sc->stts_data);
        av_freep(&sc->ctts_data);
        av_freep(&sc);
    }
}

static inline uint32_t mov_to_tag(uint8_t *buf)
{
    return MKTAG(buf[0], buf[1], buf[2], buf[3]);
}

static inline uint32_t to_be32(uint8_t *buf)
{
    return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
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
        tag = mov_to_tag(p->buf + offset + 4);
        switch(tag) {
        /* check for obvious tags */
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
            offset = to_be32(p->buf+offset) + offset;
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

static int mov_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    MOVContext *mov = (MOVContext *) s->priv_data;
    ByteIOContext *pb = &s->pb;
    int i, j, nb, err;
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

    mov->next_chunk_offset = mov->mdat_offset; /* initialise reading */
    mov->total_streams = nb = s->nb_streams;

#if 1
    for(i=0; i<s->nb_streams;) {
        if(s->streams[i]->codec->codec_type == CODEC_TYPE_MOV_OTHER) {/* not audio, not video, delete */
            av_free(s->streams[i]);
            for(j=i+1; j<s->nb_streams; j++)
                s->streams[j-1] = s->streams[j];
            s->nb_streams--;
        } else
            i++;
    }
    for(i=0; i<s->nb_streams;i++) {
        MOVStreamContext *sc = (MOVStreamContext *)s->streams[i]->priv_data;

        if(!sc->time_rate)
            sc->time_rate=1;
        av_set_pts_info(s->streams[i], 64, sc->time_rate, sc->time_scale);

        if(s->streams[i]->duration != AV_NOPTS_VALUE){
            assert(s->streams[i]->duration % sc->time_rate == 0);
            s->streams[i]->duration /= sc->time_rate;
        }

        sc->ffindex = i;
        sc->is_ff_stream = 1;
    }
#endif
    return 0;
}

/* Yes, this is ugly... I didn't write the specs of QT :p */
/* XXX:remove useless commented code sometime */
static int mov_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MOVContext *mov = (MOVContext *) s->priv_data;
    MOVStreamContext *sc;
    AVStream *st;
    int64_t offset = INT64_MAX;
    int64_t best_dts = INT64_MAX;
    int i, a, b, m;
    int size;
    int idx;
    size = 0x0FFFFFFF;

    if (mov->partial) {
        sc = mov->partial;
        idx = sc->sample_to_chunk_index;

        if (idx < 0) return 0;
        dprintf("sc[ffid %d]->sample_size = %ld\n", sc->ffindex, sc->sample_size);
        //size = sc->sample_sizes[sc->current_sample];
        // that ain't working...
        //size = (sc->sample_size)?sc->sample_size:sc->sample_sizes[sc->current_sample];
        size = (sc->sample_size > 1)?sc->sample_size:sc->sample_sizes[sc->current_sample];

        sc->current_sample++;
        sc->left_in_chunk--;

        if (sc->left_in_chunk <= 0)
            mov->partial = 0;
        offset = mov->next_chunk_offset;
        /* extract the sample */

        goto readchunk;
    }

again:
    sc = 0;
    if(offset == INT64_MAX)
        best_dts= INT64_MAX;
    for(i=0; i<mov->total_streams; i++) {
        MOVStreamContext *msc = mov->streams[i];

        if ((msc->next_chunk < msc->chunk_count) && msc->next_chunk >= 0){
            if (msc->sample_to_time_index < msc->stts_count && mov->ni) {
                int64_t dts;
                int index= msc->sample_to_time_index;
                int sample= msc->sample_to_time_sample;
                int time= msc->sample_to_time_time;
                int duration = msc->stts_data[index].duration;
                int count = msc->stts_data[index].count;
                if (sample + count < msc->current_sample) {
                    sample += count;
                    time   += count*duration;
                    index ++;
                    duration = msc->stts_data[index].duration;
                }
                dts = time + (msc->current_sample-1 - sample) * (int64_t)duration;
                dts = av_rescale(dts, AV_TIME_BASE, msc->time_scale);
                dprintf("stream: %d dts: %Ld best_dts: %Ld offset: %Ld \n", i, dts, best_dts, offset);
                if(dts < best_dts){
                    best_dts= dts;
                    sc = msc;
                    offset = msc->chunk_offsets[msc->next_chunk];
                }
            }else{
                if ((msc->chunk_offsets[msc->next_chunk] < offset)) {
                    sc = msc;
                    offset = msc->chunk_offsets[msc->next_chunk];
                }
            }
        }
    }
    if (!sc || offset==INT64_MAX)
        return -1;

    sc->next_chunk++;

    if(mov->next_chunk_offset < offset) { /* some meta data */
        url_fskip(&s->pb, (offset - mov->next_chunk_offset));
        mov->next_chunk_offset = offset;
    }

    if(!sc->is_ff_stream || (s->streams[sc->ffindex]->discard >= AVDISCARD_ALL)) {
        url_fskip(&s->pb, (offset - mov->next_chunk_offset));
        mov->next_chunk_offset = offset;
        offset = INT64_MAX;
        goto again;
    }

    /* now get the chunk size... */

    for(i=0; i<mov->total_streams; i++) {
        MOVStreamContext *msc = mov->streams[i];
        if ((msc->next_chunk < msc->chunk_count)
            && msc->chunk_offsets[msc->next_chunk] - offset < size
            && msc->chunk_offsets[msc->next_chunk] > offset)
            size = msc->chunk_offsets[msc->next_chunk] - offset;
    }

#ifdef MOV_MINOLTA_FIX
    //Make sure that size is according to sample_size (Needed by .mov files
    //created on a Minolta Dimage Xi where audio chunks contains waste data in the end)
    //Maybe we should really not only check sc->sample_size, but also sc->sample_sizes
    //but I have no such movies
    if (sc->sample_size > 0) {
        int foundsize=0;
        for(i=0; i<(sc->sample_to_chunk_sz); i++) {
            if( (sc->sample_to_chunk[i].first)<=(sc->next_chunk) )
            {
                // I can't figure out why for PCM audio sample_size is always 1
                // (it should actually be channels*bits_per_second/8) but it is.
                AVCodecContext* cod = s->streams[sc->ffindex]->codec;
                if (sc->sample_size == 1 && (cod->codec_id == CODEC_ID_PCM_S16BE || cod->codec_id == CODEC_ID_PCM_S16LE))
                    foundsize=(sc->sample_to_chunk[i].count*cod->channels*cod->bits_per_sample)/8;
                else
                    foundsize=sc->sample_to_chunk[i].count*sc->sample_size;
            }
            dprintf("sample_to_chunk first=%ld count=%ld, id=%ld\n", sc->sample_to_chunk[i].first, sc->sample_to_chunk[i].count, sc->sample_to_chunk[i].id);
        }
        if( (foundsize>0) && (foundsize<size) )
        {
            size=foundsize;
        }
    }
#endif //MOV_MINOLTA_FIX

    idx = sc->sample_to_chunk_index;
    if (idx + 1 < sc->sample_to_chunk_sz && sc->next_chunk >= sc->sample_to_chunk[idx + 1].first)
        idx++;
    sc->sample_to_chunk_index = idx;
    /* split chunks into samples */
    if (sc->sample_size == 0 || sc->sample_size > 100) {
        if (idx >= 0 && sc->sample_to_chunk[idx].count != 1) {
            mov->partial = sc;
            /* we'll have to get those samples before next chunk */
            sc->left_in_chunk = sc->sample_to_chunk[idx].count - 1;
            size = (sc->sample_size > 1)?sc->sample_size:sc->sample_sizes[sc->current_sample];
        }

        sc->current_sample++;
    }else if(idx + 1 < sc->sample_to_chunk_sz){
        sc->current_sample += sc->sample_size * sc->sample_to_chunk[idx].count;
    }

readchunk:
    dprintf("chunk: %lli -> %lli (%i)\n", offset, offset + size, size);
    if(size == 0x0FFFFFFF)
        size = mov->mdat_size + mov->mdat_offset - offset;
    if(size < 0)
        return -1;
    if(size == 0)
        return -1;
    url_fseek(&s->pb, offset, SEEK_SET);

    av_get_packet(&s->pb, pkt, size);
    pkt->stream_index = sc->ffindex;

    // If the keyframes table exists, mark any samples that are in the table as key frames.
    // If no table exists, treat very sample as a key frame.
    if (sc->keyframes) {
        a = 0;
        b = sc->keyframe_count - 1;

        while (a < b) {
            m = (a + b + 1) >> 1;
            if (sc->keyframes[m] > sc->current_sample) {
                b = m - 1;
            } else {
                a = m;
            }
        }

        if (sc->keyframes[a] == sc->current_sample)
            pkt->flags |= PKT_FLAG_KEY;
    }
    else
        pkt->flags |= PKT_FLAG_KEY;

    mov->next_chunk_offset = offset + size;

    /* find the corresponding dts */
    if (sc && sc->sample_to_time_index < sc->stts_count && pkt) {
      unsigned int count;
      uint64_t dts, pts;
      unsigned int duration = sc->stts_data[sc->sample_to_time_index].duration;
      count = sc->stts_data[sc->sample_to_time_index].count;
      if ((sc->sample_to_time_sample + count) < sc->current_sample) {
        sc->sample_to_time_sample += count;
        sc->sample_to_time_time   += count*duration;
        sc->sample_to_time_index ++;
        duration = sc->stts_data[sc->sample_to_time_index].duration;
      }
      dts = sc->sample_to_time_time + (sc->current_sample-1 - sc->sample_to_time_sample) * (int64_t)duration;
        /* find the corresponding pts */
        if (sc->sample_to_ctime_index < sc->ctts_count) {
            int duration = sc->ctts_data[sc->sample_to_ctime_index].duration;
            int count = sc->ctts_data[sc->sample_to_ctime_index].count;

            if ((sc->sample_to_ctime_sample + count) < sc->current_sample) {
                sc->sample_to_ctime_sample += count;
                sc->sample_to_ctime_index ++;
                duration = sc->ctts_data[sc->sample_to_ctime_index].duration;
            }
            pts = dts + duration;
        }else
            pts = dts;

        st= s->streams[ sc->ffindex ];
        assert(pts % st->time_base.num == 0);
        assert(dts % st->time_base.num == 0);

        pkt->pts = pts / st->time_base.num;
        pkt->dts = dts / st->time_base.num;
        dprintf("stream #%d smp #%ld dts = %lld pts = %lld (smp:%ld time:%lld idx:%d ent:%d count:%d dur:%d)\n"
                , pkt->stream_index, sc->current_sample-1, pkt->dts, pkt->pts
                , sc->sample_to_time_sample
                , sc->sample_to_time_time
                , sc->sample_to_time_index
                , sc->stts_count
                , count
                , duration);
    }

    return 0;
}

#if defined(MOV_SEEK)
/**
 * Seek method based on the one described in the Appendix C of QTFileFormat.pdf
 */
static int mov_read_seek(AVFormatContext *s, int stream_index, int64_t sample_time, int flags)
{
    MOVContext* mov = (MOVContext *) s->priv_data;
    MOVStreamContext* sc;
    int32_t i, a, b, m;
    int64_t start_time;
    int32_t seek_sample, sample;
    int32_t duration;
    int32_t count;
    int32_t chunk;
    int32_t left_in_chunk;
    int64_t chunk_file_offset;
    int64_t sample_file_offset;
    int32_t first_chunk_sample;
    int32_t sample_to_chunk_idx;
    int sample_to_time_index;
    long sample_to_time_sample = 0;
    uint64_t sample_to_time_time = 0;
    int mov_idx;

    // Find the corresponding mov stream
    for (mov_idx = 0; mov_idx < mov->total_streams; mov_idx++)
        if (mov->streams[mov_idx]->ffindex == stream_index)
            break;
    if (mov_idx == mov->total_streams) {
        av_log(s, AV_LOG_ERROR, "mov: requested stream was not found in mov streams (idx=%i)\n", stream_index);
        return -1;
    }
    sc = mov->streams[mov_idx];

    sample_time *= s->streams[stream_index]->time_base.num;

    // Step 1. Find the edit that contains the requested time (elst)
    if (sc->edit_count && 0) {
        // FIXME should handle edit list
        av_log(s, AV_LOG_ERROR, "mov: does not handle seeking in files that contain edit list (c:%d)\n", sc->edit_count);
        return -1;
    }

    // Step 2. Find the corresponding sample using the Time-to-sample atom (stts) */
    dprintf("Searching for time %li in stream #%i (time_scale=%i)\n", (long)sample_time, mov_idx, sc->time_scale);
    start_time = 0; // FIXME use elst atom
    sample = 1; // sample are 0 based in table

    for (i = 0; i < sc->stts_count; i++) {
        count = sc->stts_data[i].count;
        duration = sc->stts_data[i].duration;
        if ((start_time + count*duration) > sample_time) {
            sample_to_time_time = start_time;
            sample_to_time_index = i;
            sample_to_time_sample = sample;
            sample += (sample_time - start_time) / duration;
            break;
        }
        sample += count;
        start_time += count * duration;
    }
    sample_to_time_time = start_time;
    sample_to_time_index = i;
    /* NOTE: despite what qt doc say, the dt value (Display Time in qt vocabulary) computed with the stts atom
       is a decoding time stamp (dts) not a presentation time stamp. And as usual dts != pts for stream with b frames */

    dprintf("Found time %li at sample #%u\n", (long)sample_time, sample);
    if (sample > sc->sample_count) {
        av_log(s, AV_LOG_ERROR, "mov: sample pos is too high, unable to seek (req. sample=%i, sample count=%ld)\n", sample, sc->sample_count);
        return -1;
    }

    // Step 3. Find the prior sync. sample using the Sync sample atom (stss)
    if (sc->keyframes) {
        a = 0;
        b = sc->keyframe_count - 1;
        while (a < b) {
            m = (a + b + 1) >> 1;
            if (sc->keyframes[m] > sample) {
                b = m - 1;
            } else {
                a = m;
            }
        }
        // for low latency prob: always use the previous keyframe, just uncomment the next line
        // if (a) a--;
        seek_sample = sc->keyframes[a];
    }
    else
        seek_sample = sample; // else all samples are key frames
    dprintf("Found nearest keyframe at sample #%i \n", seek_sample);

    // Step 4. Find the chunk of the sample using the Sample-to-chunk-atom (stsc)
    for (first_chunk_sample = 1, i = 0; i < (sc->sample_to_chunk_sz - 1); i++) {
        b = (sc->sample_to_chunk[i + 1].first - sc->sample_to_chunk[i].first) * sc->sample_to_chunk[i].count;
        if (seek_sample >= first_chunk_sample && seek_sample < (first_chunk_sample + b))
            break;
        first_chunk_sample += b;
    }
    chunk = sc->sample_to_chunk[i].first + (seek_sample - first_chunk_sample) / sc->sample_to_chunk[i].count;
    left_in_chunk = sc->sample_to_chunk[i].count - (seek_sample - first_chunk_sample) % sc->sample_to_chunk[i].count;
    first_chunk_sample += ((seek_sample - first_chunk_sample) / sc->sample_to_chunk[i].count) * sc->sample_to_chunk[i].count;
    sample_to_chunk_idx = i;
    dprintf("Sample was found in chunk #%i at sample offset %i (idx %i)\n", chunk, seek_sample - first_chunk_sample, sample_to_chunk_idx);

    // Step 5. Find the offset of the chunk using the chunk offset atom
    if (!sc->chunk_offsets) {
        av_log(s, AV_LOG_ERROR, "mov: no chunk offset atom, unable to seek\n");
        return -1;
    }
    if (chunk > sc->chunk_count) {
        av_log(s, AV_LOG_ERROR, "mov: chunk offset atom too short, unable to seek (req. chunk=%i, chunk count=%li)\n", chunk, sc->chunk_count);
        return -1;
    }
    chunk_file_offset = sc->chunk_offsets[chunk - 1];
    dprintf("Chunk file offset is #%llu \n", chunk_file_offset);

    // Step 6. Find the byte offset within the chunk using the sample size atom
    sample_file_offset = chunk_file_offset;
    if (sc->sample_size)
        sample_file_offset += (seek_sample - first_chunk_sample) * sc->sample_size;
    else {
        for (i = 0; i < (seek_sample - first_chunk_sample); i++) {
        sample_file_offset += sc->sample_sizes[first_chunk_sample + i - 1];
        }
    }
    dprintf("Sample file offset is #%llu \n", sample_file_offset);

    // Step 6. Update the parser
    mov->partial = sc;
    mov->next_chunk_offset = sample_file_offset;
    // Update current stream state
    sc->current_sample = seek_sample - 1;  // zero based
    sc->left_in_chunk = left_in_chunk;
    sc->next_chunk = chunk; // +1 -1 (zero based)
    sc->sample_to_chunk_index = sample_to_chunk_idx;

    // Update other streams
    for (i = 0; i<mov->total_streams; i++) {
        MOVStreamContext *msc;
        if (i == mov_idx) continue;
        // Find the nearest 'next' chunk
        msc = mov->streams[i];
        a = 0;
        b = msc->chunk_count - 1;
        while (a < b) {
            m = (a + b + 1) >> 1;
            if (msc->chunk_offsets[m] > chunk_file_offset) {
                b = m - 1;
            } else {
                a = m;
            }
        }
        msc->next_chunk = a;
        if (msc->chunk_offsets[a] < chunk_file_offset && a < (msc->chunk_count-1))
            msc->next_chunk ++;
        dprintf("Nearest next chunk for stream #%i is #%li @%lli\n", i, msc->next_chunk+1, msc->chunk_offsets[msc->next_chunk]);

        // Compute sample count and index in the sample_to_chunk table (what a pity)
        msc->sample_to_chunk_index = 0;
        msc->current_sample = 0;
        for(;  msc->sample_to_chunk_index < (msc->sample_to_chunk_sz - 1)
            && msc->sample_to_chunk[msc->sample_to_chunk_index + 1].first <= (1 + msc->next_chunk); msc->sample_to_chunk_index++) {
            msc->current_sample += (msc->sample_to_chunk[msc->sample_to_chunk_index + 1].first - msc->sample_to_chunk[msc->sample_to_chunk_index].first) \
            * msc->sample_to_chunk[msc->sample_to_chunk_index].count;
        }
        msc->current_sample += (msc->next_chunk - (msc->sample_to_chunk[msc->sample_to_chunk_index].first - 1)) * sc->sample_to_chunk[msc->sample_to_chunk_index].count;
        msc->left_in_chunk = msc->sample_to_chunk[msc->sample_to_chunk_index].count - 1;
        // Find corresponding position in stts (used later to compute dts)
        sample = 0;
        start_time = 0;
        for (msc->sample_to_time_index = 0; msc->sample_to_time_index < msc->stts_count; msc->sample_to_time_index++) {
            count = msc->stts_data[msc->sample_to_time_index].count;
            duration = msc->stts_data[msc->sample_to_time_index].duration;
            if ((sample + count - 1) > msc->current_sample) {
                msc->sample_to_time_time = start_time;
                msc->sample_to_time_sample = sample;
                break;
            }
            sample += count;
            start_time += count * duration;
        }
        sample = 0;
        for (msc->sample_to_ctime_index = 0; msc->sample_to_ctime_index < msc->ctts_count; msc->sample_to_ctime_index++) {
            count = msc->ctts_data[msc->sample_to_ctime_index].count;
            duration = msc->ctts_data[msc->sample_to_ctime_index].duration;
            if ((sample + count - 1) > msc->current_sample) {
                msc->sample_to_ctime_sample = sample;
                break;
            }
            sample += count;
        }
        dprintf("Next Sample for stream #%i is #%li @%li\n", i, msc->current_sample + 1, msc->sample_to_chunk_index + 1);
    }
    return 0;
}
#endif

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

static AVInputFormat mov_iformat = {
    "mov,mp4,m4a,3gp,3g2",
    "QuickTime/MPEG4 format",
    sizeof(MOVContext),
    mov_probe,
    mov_read_header,
    mov_read_packet,
    mov_read_close,
#if defined(MOV_SEEK)
    mov_read_seek,
#endif
};

int mov_init(void)
{
    av_register_input_format(&mov_iformat);
    return 0;
}
