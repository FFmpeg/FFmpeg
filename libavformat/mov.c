/*
 * MOV demuxer
 * Copyright (c) 2001 Fabrice Bellard.
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

#include <limits.h>

//#define DEBUG

#include "avformat.h"
#include "riff.h"
#include "isom.h"
#include "dv.h"

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
    unsigned int chunk_count;
    int64_t *chunk_offsets;
    unsigned int stts_count;
    Time2Sample *stts_data;
    unsigned int ctts_count;
    Time2Sample *ctts_data;
    unsigned int edit_count; /* number of 'edit' (elst atom) */
    unsigned int sample_to_chunk_sz;
    MOV_sample_to_chunk_tbl *sample_to_chunk;
    int sample_to_ctime_index;
    int sample_to_ctime_sample;
    unsigned int sample_size;
    unsigned int sample_count;
    long *sample_sizes;
    unsigned int keyframe_count;
    long *keyframes;
    int time_scale;
    int time_rate;
    long current_sample;
    MOV_esds_t esds;
    unsigned int bytes_per_frame;
    unsigned int samples_per_frame;
    int dv_audio_container;
} MOVStreamContext;

typedef struct MOVContext {
    AVFormatContext *fc;
    int time_scale;
    int64_t duration; /* duration of the longest track */
    int found_moov; /* when both 'moov' and 'mdat' sections has been found */
    int found_mdat; /* we suppose we have enough data to read the file */
    int64_t mdat_offset;
    int total_streams;
    MOVStreamContext *streams[MAX_STREAMS];

    int ctab_size;
    MOV_ctab_t **ctab;           /* color tables */
    const struct MOVParseTableEntry *parse_table; /* could be eventually used to change the table */
    /* NOTE: for recursion save to/ restore from local variable! */

    AVPaletteControl palette_control;
    MOV_mdat_atom_t *mdat_list;
    int mdat_count;
    DVDemuxContext *dv_demux;
    AVFormatContext *dv_fctx;
    int isom; /* 1 if file is ISO Media (mp4/3gp) */
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
        dprintf(c->fc, "type: %08x  %.4s  sz: %"PRIx64"  %"PRIx64"   %"PRIx64"\n", a.type, (char*)&a.type, a.size, atom.size, total_size);
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
        a.size -= 8;
        if(a.size < 0 || a.size > atom.size - total_size)
            break;

        for (i = 0; c->parse_table[i].type != 0L
             && c->parse_table[i].type != a.type; i++)
            /* empty */;

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
    uint32_t type;
    uint32_t ctype;

    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */

    /* component type */
    ctype = get_le32(pb);
    type = get_le32(pb); /* component subtype */

    dprintf(c->fc, "ctype= %c%c%c%c (0x%08lx)\n", *((char *)&ctype), ((char *)&ctype)[1], ((char *)&ctype)[2], ((char *)&ctype)[3], (long) ctype);
    dprintf(c->fc, "stype= %c%c%c%c\n", *((char *)&type), ((char *)&type)[1], ((char *)&type)[2], ((char *)&type)[3]);
    if(!ctype)
        c->isom = 1;
    if(type == MKTAG('v', 'i', 'd', 'e'))
        st->codec->codec_type = CODEC_TYPE_VIDEO;
    else if(type == MKTAG('s', 'o', 'u', 'n'))
        st->codec->codec_type = CODEC_TYPE_AUDIO;
    else if(type == MKTAG('m', '1', 'a', ' '))
        st->codec->codec_id = CODEC_ID_MP2;
    else if(type == MKTAG('s', 'u', 'b', 'p')) {
        st->codec->codec_type = CODEC_TYPE_SUBTITLE;
        st->codec->codec_id = CODEC_ID_DVD_SUBTITLE;
    }
    get_be32(pb); /* component  manufacture */
    get_be32(pb); /* component flags */
    get_be32(pb); /* component flags mask */

    if(atom.size <= 24)
        return 0; /* nothing left to read */

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

static int mov_mp4_read_descr(MOVContext *c, ByteIOContext *pb, int *tag)
{
    int len;
    *tag = get_byte(pb);
    len = mov_mp4_read_descr_len(pb);
    dprintf(c->fc, "MPEG4 description: tag=0x%02x len=%d\n", *tag, len);
    return len;
}

static int mov_read_esds(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    MOVStreamContext *sc = (MOVStreamContext *)st->priv_data;
    int tag, len;

    /* Well, broken but suffisant for some MP4 streams */
    get_be32(pb); /* version + flags */
    len = mov_mp4_read_descr(c, pb, &tag);
    if (tag == MP4ESDescrTag) {
        get_be16(pb); /* ID */
        get_byte(pb); /* priority */
    } else
        get_be16(pb); /* ID */

    len = mov_mp4_read_descr(c, pb, &tag);
    if (tag == MP4DecConfigDescrTag) {
        sc->esds.object_type_id = get_byte(pb);
        sc->esds.stream_type = get_byte(pb);
        sc->esds.buffer_size_db = get_be24(pb);
        sc->esds.max_bitrate = get_be32(pb);
        sc->esds.avg_bitrate = get_be32(pb);

        st->codec->codec_id= codec_get_id(ff_mp4_obj_type, sc->esds.object_type_id);
        dprintf(c->fc, "esds object type id %d\n", sc->esds.object_type_id);
        len = mov_mp4_read_descr(c, pb, &tag);
        if (tag == MP4DecSpecificDescrTag) {
            dprintf(c->fc, "Specific MPEG4 header len=%d\n", len);
            st->codec->extradata = av_mallocz(len + FF_INPUT_BUFFER_PADDING_SIZE);
            if (st->codec->extradata) {
                get_buffer(pb, st->codec->extradata, len);
                st->codec->extradata_size = len;
                /* from mplayer */
                if ((*st->codec->extradata >> 3) == 29) {
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
    if(c->found_moov)
        return 1; /* found both, just go */
    url_fskip(pb, atom.size);
    return 0; /* now go for moov */
}

static int mov_read_ftyp(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    uint32_t type = get_le32(pb);

    if (type != MKTAG('q','t',' ',' '))
        c->isom = 1;
    av_log(c->fc, AV_LOG_DEBUG, "ISO: File Type Major Brand: %.4s\n",(char *)&type);
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
    st->codec->extradata = av_mallocz(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);

    if (st->codec->extradata) {
        memcpy(st->codec->extradata, "SVQ3", 4); // fake
        get_buffer(pb, st->codec->extradata + 0x5a, atom.size);
        dprintf(c->fc, "Reading SMI %"PRId64"  %s\n", atom.size, st->codec->extradata + 0x5a);
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

/* FIXME modify qdm2/svq3/h264 decoders to take full atom as extradata */
static int mov_read_extradata(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    if((uint64_t)atom.size > (1<<30))
        return -1;
    av_free(st->codec->extradata);
    st->codec->extradata_size = atom.size + 8;
    st->codec->extradata = av_mallocz(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (st->codec->extradata) {
        AV_WL32(st->codec->extradata + 4, atom.type);
        get_buffer(pb, st->codec->extradata + 8, atom.size);
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
        st->codec->extradata = av_mallocz(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);

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

static int mov_read_avcC(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];

    if((uint64_t)atom.size > (1<<30))
        return -1;

    av_free(st->codec->extradata);

    st->codec->extradata_size = atom.size;
    st->codec->extradata = av_mallocz(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);

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
    sc->chunk_offsets = av_malloc(entries * sizeof(int64_t));
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
        id = codec_get_id(codec_movaudio_tags, format);
        if (st->codec->codec_type != CODEC_TYPE_VIDEO && id > 0) {
            st->codec->codec_type = CODEC_TYPE_AUDIO;
        } else if (st->codec->codec_type != CODEC_TYPE_AUDIO && /* do not overwrite codec type */
                   format && format != MKTAG('m', 'p', '4', 's')) { /* skip old asf mpeg4 tag */
            id = codec_get_id(codec_movvideo_tags, format);
            if (id <= 0)
                id = codec_get_id(codec_bmp_tags, format);
            if (id > 0)
                st->codec->codec_type = CODEC_TYPE_VIDEO;
        }

        dprintf(c->fc, "size=%d 4CC= %c%c%c%c codec_type=%d\n",
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
            dprintf(c->fc, "audio channels %d\n", st->codec->channels);
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
                else if (st->codec->bits_per_sample == 24)
                    st->codec->codec_id = CODEC_ID_PCM_S24BE;
                break;
            default:
                break;
            }

            //Read QT version 1 fields. In version 0 theese dont exist
            dprintf(c->fc, "version =%d, isom =%d\n",version,c->isom);
            if(!c->isom) {
                if(version==1) {
                    sc->samples_per_frame = get_be32(pb);
                    get_be32(pb); /* bytes per packet */
                    sc->bytes_per_frame = get_be32(pb);
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
            }

            bits_per_sample = av_get_bits_per_sample(st->codec->codec_id);
            if (bits_per_sample) {
                st->codec->bits_per_sample = bits_per_sample;
                sc->sample_size = (bits_per_sample >> 3) * st->codec->channels;
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

    /* special codec parameters handling */
    switch (st->codec->codec_id) {
#ifdef CONFIG_H261_DECODER
    case CODEC_ID_H261:
#endif
#ifdef CONFIG_H263_DECODER
    case CODEC_ID_H263:
#endif
#ifdef CONFIG_MPEG4_DECODER
    case CODEC_ID_MPEG4:
#endif
        st->codec->width= 0; /* let decoder init width/height */
        st->codec->height= 0;
        break;
#ifdef CONFIG_LIBFAAD
    case CODEC_ID_AAC:
#endif
#ifdef CONFIG_VORBIS_DECODER
    case CODEC_ID_VORBIS:
#endif
    case CODEC_ID_MP3ON4:
        st->codec->sample_rate= 0; /* let decoder init parameters properly */
        break;
#ifdef CONFIG_DV_DEMUXER
    case CODEC_ID_DVAUDIO:
        c->dv_fctx = av_alloc_format_context();
        c->dv_demux = dv_init_demux(c->dv_fctx);
        if (!c->dv_demux) {
            av_log(c->fc, AV_LOG_ERROR, "dv demux context init error\n");
            return -1;
        }
        sc->dv_audio_container = 1;
        st->codec->codec_id = CODEC_ID_PCM_S16LE;
        break;
#endif
    /* no ifdef since parameters are always those */
    case CODEC_ID_AMR_WB:
        st->codec->sample_rate= 16000;
        st->codec->channels= 1; /* really needed */
        break;
    case CODEC_ID_AMR_NB:
        st->codec->sample_rate= 8000;
        st->codec->channels= 1; /* really needed */
        break;
    case CODEC_ID_MP2:
    case CODEC_ID_MP3:
        st->codec->codec_type = CODEC_TYPE_AUDIO; /* force type after stsd for m1a hdlr */
        st->need_parsing = 1;
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
    sc->sample_to_chunk = av_malloc(entries * sizeof(MOV_sample_to_chunk_tbl));
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
    av_log(NULL, AV_LOG_DEBUG, "keyframe_count = %d\n", sc->keyframe_count);
#endif
    sc->keyframes = av_malloc(entries * sizeof(long));
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
    av_log(NULL, AV_LOG_DEBUG, "sample_size = %d sample_count = %d\n", sc->sample_size, sc->sample_count);
#endif
    sc->sample_sizes = av_malloc(entries * sizeof(long));
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

        dprintf(c->fc, "sample_count=%d, sample_duration=%d\n",sample_count,sample_duration);

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

    dprintf(c->fc, "track[%i].ctts.entries = %i\n", c->fc->nb_streams-1, entries);

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
    sc = av_mallocz(sizeof(MOVStreamContext));
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

static int mov_read_cmov(MOVContext *c, ByteIOContext *pb, MOV_atom_t atom)
{
#ifdef CONFIG_ZLIB
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

    cmov_data = av_malloc(cmov_len);
    if (!cmov_data)
        return -1;
    moov_data = av_malloc(moov_len);
    if (!moov_data) {
        av_free(cmov_data);
        return -1;
    }
    get_buffer(pb, cmov_data, cmov_len);
    if(uncompress (moov_data, (uLongf *) &moov_len, (const Bytef *)cmov_data, cmov_len) != Z_OK)
        return -1;
    if(init_put_byte(&ctx, moov_data, moov_len, 0, NULL, NULL, NULL, NULL) != 0)
        return -1;
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
#else
    av_log(c->fc, AV_LOG_ERROR, "this file requires zlib support compiled in\n");
    return -1;
#endif
}

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
    dprintf(c->fc, "track[%i].edit_count = %i\n", c->fc->nb_streams-1, c->streams[c->fc->nb_streams-1]->edit_count);
    return 0;
}

static const MOVParseTableEntry mov_default_parse_table[] = {
/* mp4 atoms */
{ MKTAG( 'c', 'o', '6', '4' ), mov_read_stco },
{ MKTAG( 'c', 't', 't', 's' ), mov_read_ctts }, /* composition time to sample */
{ MKTAG( 'e', 'd', 't', 's' ), mov_read_default },
{ MKTAG( 'e', 'l', 's', 't' ), mov_read_elst },
{ MKTAG( 'e', 'n', 'd', 'a' ), mov_read_enda },
{ MKTAG( 'f', 'i', 'e', 'l' ), mov_read_extradata },
{ MKTAG( 'f', 't', 'y', 'p' ), mov_read_ftyp },
{ MKTAG( 'h', 'd', 'l', 'r' ), mov_read_hdlr },
{ MKTAG( 'j', 'p', '2', 'h' ), mov_read_extradata },
{ MKTAG( 'm', 'd', 'a', 't' ), mov_read_mdat },
{ MKTAG( 'm', 'd', 'h', 'd' ), mov_read_mdhd },
{ MKTAG( 'm', 'd', 'i', 'a' ), mov_read_default },
{ MKTAG( 'm', 'i', 'n', 'f' ), mov_read_default },
{ MKTAG( 'm', 'o', 'o', 'v' ), mov_read_moov },
{ MKTAG( 'm', 'v', 'h', 'd' ), mov_read_mvhd },
{ MKTAG( 'S', 'M', 'I', ' ' ), mov_read_smi }, /* Sorenson extension ??? */
{ MKTAG( 'a', 'l', 'a', 'c' ), mov_read_extradata }, /* alac specific atom */
{ MKTAG( 'a', 'v', 'c', 'C' ), mov_read_avcC },
{ MKTAG( 's', 't', 'b', 'l' ), mov_read_default },
{ MKTAG( 's', 't', 'c', 'o' ), mov_read_stco },
{ MKTAG( 's', 't', 's', 'c' ), mov_read_stsc },
{ MKTAG( 's', 't', 's', 'd' ), mov_read_stsd }, /* sample description */
{ MKTAG( 's', 't', 's', 's' ), mov_read_stss }, /* sync sample */
{ MKTAG( 's', 't', 's', 'z' ), mov_read_stsz }, /* sample size */
{ MKTAG( 's', 't', 't', 's' ), mov_read_stts },
{ MKTAG( 't', 'k', 'h', 'd' ), mov_read_tkhd }, /* track header */
{ MKTAG( 't', 'r', 'a', 'k' ), mov_read_trak },
{ MKTAG( 'w', 'a', 'v', 'e' ), mov_read_wave },
{ MKTAG( 'c', 't', 'a', 'b' ), mov_read_ctab },
{ MKTAG( 'e', 's', 'd', 's' ), mov_read_esds },
{ MKTAG( 'w', 'i', 'd', 'e' ), mov_read_wide }, /* place holder */
{ MKTAG( 'c', 'm', 'o', 'v' ), mov_read_cmov },
{ 0L, NULL }
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
        tag = AV_RL32(p->buf + offset + 4);
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
            offset = AV_RB32(p->buf+offset) + offset;
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
    unsigned int stts_index = 0;
    unsigned int stsc_index = 0;
    unsigned int stss_index = 0;
    unsigned int i, j, k;

    if (sc->sample_sizes || st->codec->codec_type == CODEC_TYPE_VIDEO || sc->dv_audio_container) {
        unsigned int current_sample = 0;
        unsigned int stts_sample = 0;
        unsigned int keyframe, sample_size;
        unsigned int distance = 0;

        st->nb_frames = sc->sample_count;
        for (i = 0; i < sc->chunk_count; i++) {
            current_offset = sc->chunk_offsets[i];
            if (stsc_index + 1 < sc->sample_to_chunk_sz && i + 1 == sc->sample_to_chunk[stsc_index + 1].first)
                stsc_index++;
            for (j = 0; j < sc->sample_to_chunk[stsc_index].count; j++) {
                if (current_sample >= sc->sample_count) {
                    av_log(mov->fc, AV_LOG_ERROR, "wrong sample count\n");
                    goto out;
                }
                keyframe = !sc->keyframe_count || current_sample + 1 == sc->keyframes[stss_index];
                if (keyframe) {
                    distance = 0;
                    if (stss_index + 1 < sc->keyframe_count)
                        stss_index++;
                }
                sample_size = sc->sample_size > 0 ? sc->sample_size : sc->sample_sizes[current_sample];
                dprintf(mov->fc, "AVIndex stream %d, sample %d, offset %"PRIx64", dts %"PRId64", size %d, distance %d, keyframe %d\n",
                        st->index, current_sample, current_offset, current_dts, sample_size, distance, keyframe);
                av_add_index_entry(st, current_offset, current_dts, sample_size, distance, keyframe ? AVINDEX_KEYFRAME : 0);
                current_offset += sample_size;
                assert(sc->stts_data[stts_index].duration % sc->time_rate == 0);
                current_dts += sc->stts_data[stts_index].duration / sc->time_rate;
                distance++;
                stts_sample++;
                current_sample++;
                if (stts_index + 1 < sc->stts_count && stts_sample == sc->stts_data[stts_index].count) {
                    stts_sample = 0;
                    stts_index++;
                }
            }
        }
    } else { /* read whole chunk */
        unsigned int chunk_samples, chunk_size, chunk_duration;

        for (i = 0; i < sc->chunk_count; i++) {
            current_offset = sc->chunk_offsets[i];
            if (stsc_index + 1 < sc->sample_to_chunk_sz && i + 1 == sc->sample_to_chunk[stsc_index + 1].first)
                stsc_index++;
            chunk_samples = sc->sample_to_chunk[stsc_index].count;
            /* get chunk size */
            if (sc->sample_size > 1 || st->codec->codec_id == CODEC_ID_PCM_U8 || st->codec->codec_id == CODEC_ID_PCM_S8)
                chunk_size = chunk_samples * sc->sample_size;
            else if (sc->samples_per_frame > 0 && (chunk_samples * sc->bytes_per_frame % sc->samples_per_frame == 0))
                chunk_size = chunk_samples * sc->bytes_per_frame / sc->samples_per_frame;
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
                        dprintf(mov->fc, "mdat %d, offset %"PRIx64", size %"PRId64", current offset %"PRIx64"\n",
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
            dprintf(mov->fc, "AVIndex stream %d, chunk %d, offset %"PRIx64", dts %"PRId64", size %d, duration %d\n",
                    st->index, i, current_offset, current_dts, chunk_size, chunk_duration);
            assert(chunk_duration % sc->time_rate == 0);
            current_dts += chunk_duration / sc->time_rate;
        }
    }
 out:
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
    dprintf(mov->fc, "on_parse_exit_offset=%d\n", (int) url_ftell(pb));

    /* some cleanup : make sure we are on the mdat atom */
    if(!url_is_streamed(pb) && (url_ftell(pb) != mov->mdat_offset))
        url_fseek(pb, mov->mdat_offset, SEEK_SET);

    mov->total_streams = s->nb_streams;

    for(i=0; i<mov->total_streams; i++) {
        MOVStreamContext *sc = mov->streams[i];
        AVStream *st = s->streams[i];
        /* sanity checks */
        if(!sc->stts_count || !sc->chunk_count || !sc->sample_to_chunk_sz ||
           (!sc->sample_size && !sc->sample_count)){
            av_log(s, AV_LOG_ERROR, "missing mandatory atoms, broken header\n");
            sc->sample_count = 0; //ignore track
            continue;
        }
        if(!sc->time_rate)
            sc->time_rate=1;
        if(!sc->time_scale)
            sc->time_scale= mov->time_scale;
        av_set_pts_info(st, 64, sc->time_rate, sc->time_scale);

        if (st->codec->codec_type == CODEC_TYPE_AUDIO && sc->stts_count == 1)
            st->codec->frame_size = sc->stts_data[0].duration;

        if(st->duration != AV_NOPTS_VALUE){
            assert(st->duration % sc->time_rate == 0);
            st->duration /= sc->time_rate;
        }
        sc->ffindex = i;
        mov_build_index(mov, st);
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

            dprintf(s, "stream %d, sample %ld, dts %"PRId64"\n", i, msc->current_sample, dts);
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
        av_log(mov->fc, AV_LOG_ERROR, "stream %d, offset 0x%"PRIx64": partial file\n", sc->ffindex, sample->pos);
        return -1;
    }
#ifdef CONFIG_DV_DEMUXER
    if (sc->dv_audio_container) {
        dv_get_packet(mov->dv_demux, pkt);
        dprintf(s, "dv audio pkt size %d\n", pkt->size);
    } else {
#endif
        url_fseek(&s->pb, sample->pos, SEEK_SET);
        av_get_packet(&s->pb, pkt, sample->size);
#ifdef CONFIG_DV_DEMUXER
        if (mov->dv_demux) {
            void *pkt_destruct_func = pkt->destruct;
            dv_produce_packet(mov->dv_demux, pkt, pkt->data, pkt->size);
            pkt->destruct = pkt_destruct_func;
        }
    }
#endif
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
    dprintf(s, "stream %d, pts %"PRId64", dts %"PRId64", pos 0x%"PRIx64", duration %d\n", pkt->stream_index, pkt->pts, pkt->dts, pkt->pos, pkt->duration);
    return 0;
}

static int mov_seek_stream(AVStream *st, int64_t timestamp, int flags)
{
    MOVStreamContext *sc = st->priv_data;
    int sample, time_sample;
    int i;

    sample = av_index_search_timestamp(st, timestamp, flags);
    dprintf(st->codec, "stream %d, timestamp %"PRId64", sample %d\n", st->index, timestamp, sample);
    if (sample < 0) /* not sure what to do */
        return -1;
    sc->current_sample = sample;
    dprintf(st->codec, "stream %d, found sample %ld\n", st->index, sc->current_sample);
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
    if(mov->dv_demux){
        for(i=0; i<mov->dv_fctx->nb_streams; i++){
            av_freep(&mov->dv_fctx->streams[i]->codec);
            av_freep(&mov->dv_fctx->streams[i]);
        }
        av_freep(&mov->dv_fctx);
        av_freep(&mov->dv_demux);
    }
    av_freep(&mov->ctab);
    return 0;
}

AVInputFormat mov_demuxer = {
    "mov,mp4,m4a,3gp,3g2,mj2",
    "QuickTime/MPEG4/Motion JPEG 2000 format",
    sizeof(MOVContext),
    mov_probe,
    mov_read_header,
    mov_read_packet,
    mov_read_close,
    mov_read_seek,
};
