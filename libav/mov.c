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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "avformat.h"
#include "avi.h"

#ifdef CONFIG_ZLIB
#include <zlib.h>
#endif

/*
 * First version by Francois Revol revol@free.fr
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
 * 
 * Funny I didn't know about http://sourceforge.net/projects/qt-ffmpeg/
 * when coding this :) (it's a writer anyway)
 * 
 * Reference documents:
 * http://www.geocities.com/xhelmboyx/quicktime/formats/qtm-layout.txt
 * Apple:
 *  http://developer.apple.com/techpubs/quicktime/qtdevdocs/QTFF/qtff.html
 *  http://developer.apple.com/techpubs/quicktime/qtdevdocs/PDF/QTFileFormat.pdf
 * QuickTime is a trademark of Apple (AFAIK :))
 */

//#define DEBUG

/* allows chunk splitting - should work now... */
/* in case you can't read a file, try commenting */
#define MOV_SPLIT_CHUNKS

#ifdef DEBUG
/*
 * XXX: static sux, even more in a multithreaded environment...
 * Avoid them. This is here just to help debugging.
 */
static int debug_indent = 0;
void print_atom(const char *str, UINT32 type, UINT64 offset, UINT64 size)
{
    unsigned int tag, i;
    tag = (unsigned int) type;
    i=debug_indent;
    if(tag == 0) tag = MKTAG('N', 'U', 'L', 'L');
    while(i--)
        printf("|");
    printf("parse:");
    printf(" %s: tag=%c%c%c%c offset=0x%x size=0x%x\n",
           str, tag & 0xff,
           (tag >> 8) & 0xff,
           (tag >> 16) & 0xff,
           (tag >> 24) & 0xff,
           (unsigned int)offset,
           (unsigned int)size);
}
#endif

/* some streams in QT (and in MP4 mostly) aren't either video nor audio */
/* so we first list them as this, then clean up the list of streams we give back, */
/* getting rid of these */
#define CODEC_TYPE_MOV_OTHER 2

static const CodecTag mov_video_tags[] = {
/*  { CODEC_ID_, MKTAG('c', 'v', 'i', 'd') }, *//* Cinepak */
/*  { CODEC_ID_JPEG, MKTAG('j', 'p', 'e', 'g') }, *//* JPEG */
/*  { CODEC_ID_H263, MKTAG('r', 'a', 'w', ' ') }, *//* Uncompressed RGB */
/*  { CODEC_ID_H263, MKTAG('Y', 'u', 'v', '2') }, *//* Uncompressed YUV422 */
/* Graphics */
/* Animation */
/* Apple video */
/* Kodak Photo CD */
    { CODEC_ID_MJPEG, MKTAG('j', 'p', 'e', 'g') }, /* PhotoJPEG */
    { CODEC_ID_MPEG1VIDEO, MKTAG('m', 'p', 'e', 'g') }, /* MPEG */
    { CODEC_ID_MJPEG, MKTAG('m', 'j', 'p', 'a') }, /* Motion-JPEG (format A) */
    { CODEC_ID_MJPEG, MKTAG('m', 'j', 'p', 'b') }, /* Motion-JPEG (format B) */
/*    { CODEC_ID_GIF, MKTAG('g', 'i', 'f', ' ') }, *//* embedded gif files as frames (usually one "click to play movie" frame) */
/* Sorenson video */
    { CODEC_ID_SVQ1, MKTAG('S', 'V', 'Q', '1') }, /* Sorenson Video v1 */
    { CODEC_ID_SVQ1, MKTAG('s', 'v', 'q', '1') }, /* Sorenson Video v1 */
    { CODEC_ID_SVQ1, MKTAG('s', 'v', 'q', 'i') }, /* Sorenson Video v1 (from QT specs)*/
    { CODEC_ID_MPEG4, MKTAG('m', 'p', '4', 'v') }, /* OpenDiVX *//* yeah ! */
    { CODEC_ID_MPEG4, MKTAG('D', 'I', 'V', 'X') }, /* OpenDiVX *//* sample files at http://heroinewarrior.com/xmovie.php3 use this tag */
/*    { CODEC_ID_, MKTAG('I', 'V', '5', '0') }, *//* Indeo 5.0 */
    { CODEC_ID_H263, MKTAG('h', '2', '6', '3') }, /* H263 */
    { 0, 0 }, 
};

static const CodecTag mov_audio_tags[] = {
/*    { CODEC_ID_PCM_S16BE, MKTAG('N', 'O', 'N', 'E') }, *//* uncompressed */
    { CODEC_ID_PCM_S16BE, MKTAG('t', 'w', 'o', 's') }, /* 16 bits */
    { CODEC_ID_PCM_S8, MKTAG('t', 'w', 'o', 's') }, /* 8 bits */
    { CODEC_ID_PCM_U8, 0x20776172 }, /* 8 bits unsigned */
    { CODEC_ID_PCM_S16LE, MKTAG('s', 'o', 'w', 't') }, /*  */
    { CODEC_ID_PCM_MULAW, MKTAG('u', 'l', 'a', 'w') }, /*  */
    { CODEC_ID_PCM_ALAW, MKTAG('a', 'l', 'a', 'w') }, /*  */
    { CODEC_ID_ADPCM_IMA_QT, MKTAG('i', 'm', 'a', '4') }, /* IMA-4 ADPCM */

    { CODEC_ID_MP2, MKTAG('.', 'm', 'p', '3') }, /* MPEG layer 3 */ /* sample files at http://www.3ivx.com/showcase.html use this tag */
    { CODEC_ID_MP2, 0x6D730055 }, /* MPEG layer 3 */
    { CODEC_ID_MP2, 0x5500736D }, /* MPEG layer 3 *//* XXX: check endianness */
/*    { CODEC_ID_OGG_VORBIS, MKTAG('O', 'g', 'g', 'S') }, *//* sample files at http://heroinewarrior.com/xmovie.php3 use this tag */
/* MP4 tags */
/*    { CODEC_ID_AAC, MKTAG('m', 'p', '4', 'a') }, *//* MPEG 4 AAC or audio ? */
                                                     /* The standard for mpeg4 audio is still not normalised AFAIK anyway */
    { 0, 0 }, 
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

typedef struct MOVStreamContext {
    int ffindex; /* the ffmpeg stream id */
    int is_ff_stream; /* Is this stream presented to ffmpeg ? i.e. is this an audio or video stream ? */
    long next_chunk;
    long chunk_count;
    INT64 *chunk_offsets;
    long sample_to_chunk_sz;
    MOV_sample_to_chunk_tbl *sample_to_chunk;
    long sample_to_chunk_index;
    long sample_size;
    long sample_count;
    long *sample_sizes;
    long time_scale;
    long current_sample;
    long left_in_chunk; /* how many samples before next chunk */
} MOVStreamContext;

typedef struct MOVContext {
    int mp4; /* set to 1 as soon as we are sure that the file is an .mp4 file (even some header parsing depends on this) */
    AVFormatContext *fc;
    long time_scale;
    int found_moov; /* when both 'moov' and 'mdat' sections has been found */
    int found_mdat; /* we suppose we have enough data to read the file */
    INT64 mdat_size;
    INT64 mdat_offset;
    int total_streams;
    /* some streams listed here aren't presented to the ffmpeg API, since they aren't either video nor audio
     * but we need the info to be able to skip data from those streams in the 'mdat' section
     */
    MOVStreamContext *streams[MAX_STREAMS];
    
    INT64 next_chunk_offset;
    int partial; /* != 0 : there is still to read in the current chunk (=id of the stream + 1) */
} MOVContext;


struct MOVParseTableEntry;

/* XXX: it's the first time I make a recursive parser I think... sorry if it's ugly :P */

/* those functions parse an atom */
/* return code:
 1: found what I wanted, exit
 0: continue to parse next atom
 -1: error occured, exit
 */
typedef int (*mov_parse_function)(const struct MOVParseTableEntry *parse_table,
                                  ByteIOContext *pb,
                                  UINT32 atom_type,
                                  INT64 atom_offset, /* after the size and type field (and eventually the extended size) */
                                  INT64 atom_size, /* total size (excluding the size and type fields) */
                                  void *param);

/* links atom IDs to parse functions */
typedef struct MOVParseTableEntry {
    UINT32 type;
    mov_parse_function func;
} MOVParseTableEntry;

static int parse_leaf(const MOVParseTableEntry *parse_table, ByteIOContext *pb, UINT32 atom_type, INT64 atom_offset, INT64 atom_size, void *param)
{
#ifdef DEBUG
    print_atom("leaf", atom_type, atom_offset, atom_size);
#endif
    if(atom_size>1)
        url_fskip(pb, atom_size);
/*        url_seek(pb, atom_offset+atom_size, SEEK_SET); */
    return 0;
}


static int parse_default(const MOVParseTableEntry *parse_table, ByteIOContext *pb, UINT32 atom_type, INT64 atom_offset, INT64 atom_size, void *param)
{
    UINT32 type, foo=0;
    UINT64 offset, size;
    UINT64 total_size = 0;
    int i;
    int err = 0;
    foo=0;
#ifdef DEBUG
    print_atom("default", atom_type, atom_offset, atom_size);
    debug_indent++;
#endif
    
    offset = atom_offset;

    if(atom_size < 0)
        atom_size = 0x0FFFFFFFFFFFFFFF;
    while((total_size < atom_size) && !url_feof(pb) && !err) {
        size=atom_size;
        type=0L;
        if(atom_size >= 8) {
            size = get_be32(pb);
            type = get_le32(pb);
        }
        total_size += 8;
        offset+=8;
//        printf("type: %08lx  sz: %08lx", type, size);
        if(size == 1) { /* 64 bit extended size */
            size = get_be64(pb);
            offset+=8;
            total_size+=8;
            size-=8;
        }
        if(size == 0)
            size = atom_size - total_size;
        size-=8;
        for(i=0; parse_table[i].type != 0L && parse_table[i].type != type; i++);
        
//        printf(" i=%ld\n", i);
	if (parse_table[i].type == 0) { /* skip leaf atoms data */
//            url_seek(pb, atom_offset+atom_size, SEEK_SET);
#ifdef DEBUG
            print_atom("unknown", type, offset, size);
#endif
            url_fskip(pb, size);
        } else
            err = (parse_table[i].func)(parse_table, pb, type, offset, size, param);

        offset+=size;
        total_size+=size;
    }

#ifdef DEBUG
    debug_indent--;
#endif
    return err;
}

static int parse_mvhd(const MOVParseTableEntry *parse_table, ByteIOContext *pb, UINT32 atom_type, INT64 atom_offset, INT64 atom_size, void *param)
{
    MOVContext *c;
#ifdef DEBUG
    print_atom("mvhd", atom_type, atom_offset, atom_size);
#endif
    c = (MOVContext *)param;

    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */

    get_be32(pb); /* creation time */
    get_be32(pb); /* modification time */
    c->time_scale = get_be32(pb); /* time scale */
#ifdef DEBUG
    printf("time scale = %li\n", c->time_scale);
#endif
    get_be32(pb); /* duration */
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

/* this atom should contain all header atoms */
static int parse_moov(const MOVParseTableEntry *parse_table, ByteIOContext *pb, UINT32 atom_type, INT64 atom_offset, INT64 atom_size, void *param)
{
    int err;
    MOVContext *c;
#ifdef DEBUG
    print_atom("moov", atom_type, atom_offset, atom_size);
#endif
    c = (MOVContext *)param;

    err = parse_default(parse_table, pb, atom_type, atom_offset, atom_size, param);
    /* we parsed the 'moov' atom, we can terminate the parsing as soon as we find the 'mdat' */
    /* so we don't parse the whole file if over a network */
    c->found_moov=1;
    if(c->found_mdat)
        return 1; /* found both, just go */
    return 0; /* now go for mdat */
}

/* this atom contains actual media data */
static int parse_mdat(const MOVParseTableEntry *parse_table, ByteIOContext *pb, UINT32 atom_type, INT64 atom_offset, INT64 atom_size, void *param)
{
    MOVContext *c;
#ifdef DEBUG
    print_atom("mdat", atom_type, atom_offset, atom_size);
#endif
    c = (MOVContext *)param;

    if(atom_size == 0) /* wrong one (MP4) */
        return 0;
    c->found_mdat=1;
    c->mdat_offset = atom_offset;
    c->mdat_size = atom_size;
    if(c->found_moov)
        return 1; /* found both, just go */
    url_fskip(pb, atom_size);
    return 0; /* now go for moov */
}

/* this atom should be null (from specs), but some buggy files put the 'moov' atom inside it... */
/* like the files created with Adobe Premiere 5.0, for samples see */
/* http://graphics.tudelft.nl/~wouter/publications/soundtests/ */
static int parse_wide(const MOVParseTableEntry *parse_table, ByteIOContext *pb, UINT32 atom_type, INT64 atom_offset, INT64 atom_size, void *param)
{
    int err;
    UINT32 type;
#ifdef DEBUG
    print_atom("wide", atom_type, atom_offset, atom_size);
    debug_indent++;
#endif
    if (atom_size < 8)
        return 0; /* continue */
    if (get_be32(pb) != 0) { /* 0 sized mdat atom... use the 'wide' atom size */
        url_fskip(pb, atom_size - 4);
        return 0;
    }
    type = get_le32(pb);
    if (type != MKTAG('m', 'd', 'a', 't')) {
        url_fskip(pb, atom_size - 8);
        return 0;
    }
    err = parse_mdat(parse_table, pb, type, atom_offset + 8, atom_size - 8, param);
#ifdef DEBUG
    debug_indent--;
#endif
    return err;
}

static int parse_trak(const MOVParseTableEntry *parse_table, ByteIOContext *pb, UINT32 atom_type, INT64 atom_offset, INT64 atom_size, void *param)
{
    MOVContext *c;
    AVStream *st;
    MOVStreamContext *sc;
#ifdef DEBUG
    print_atom("trak", atom_type, atom_offset, atom_size);
#endif

    c = (MOVContext *)param;
    st = av_new_stream(c->fc, c->fc->nb_streams);
    if (!st) return -2;
    sc = av_malloc(sizeof(MOVStreamContext));
    sc->sample_to_chunk_index = -1;
    st->priv_data = sc;
    st->codec.codec_type = CODEC_TYPE_MOV_OTHER;
    c->streams[c->fc->nb_streams-1] = sc;
    return parse_default(parse_table, pb, atom_type, atom_offset, atom_size, param);
}

static int parse_tkhd(const MOVParseTableEntry *parse_table, ByteIOContext *pb, UINT32 atom_type, INT64 atom_offset, INT64 atom_size, void *param)
{
    MOVContext *c;
    AVStream *st;
#ifdef DEBUG
    print_atom("tkhd", atom_type, atom_offset, atom_size);
#endif

    c = (MOVContext *)param;
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
    get_be32(pb); /* duration */
    get_be32(pb); /* reserved */
    get_be32(pb); /* reserved */
    
    get_be16(pb); /* layer */
    get_be16(pb); /* alternate group */
    get_be16(pb); /* volume */
    get_be16(pb); /* reserved */

    url_fskip(pb, 36); /* display matrix */

    /* those are fixed-point */
    st->codec.width = get_be32(pb) >> 16; /* track width */
    st->codec.height = get_be32(pb) >> 16; /* track height */
    
    return 0;
}

static int parse_mdhd(const MOVParseTableEntry *parse_table, ByteIOContext *pb, UINT32 atom_type, INT64 atom_offset, INT64 atom_size, void *param)
{
    MOVContext *c;
    AVStream *st;
#ifdef DEBUG
    print_atom("mdhd", atom_type, atom_offset, atom_size);
#endif

    c = (MOVContext *)param;
    st = c->fc->streams[c->fc->nb_streams-1];
    
    get_byte(pb); /* version */

    get_byte(pb); get_byte(pb);
    get_byte(pb); /* flags */

    get_be32(pb); /* creation time */
    get_be32(pb); /* modification time */

    c->streams[c->total_streams]->time_scale = get_be32(pb);

#ifdef DEBUG
    printf("track[%i].time_scale = %li\n", c->fc->nb_streams-1, c->streams[c->total_streams]->time_scale); /* time scale */
#endif
    get_be32(pb); /* duration */

    get_be16(pb); /* language */
    get_be16(pb); /* quality */
    
    return 0;
}

static int parse_hdlr(const MOVParseTableEntry *parse_table, ByteIOContext *pb, UINT32 atom_type, INT64 atom_offset, INT64 atom_size, void *param)
{
    MOVContext *c;
    int len = 0;
    char *buf;
    UINT32 type;
    AVStream *st;
    UINT32 ctype;
#ifdef DEBUG
    print_atom("hdlr", atom_type, atom_offset, atom_size);
#endif
    c = (MOVContext *)param;
    st = c->fc->streams[c->fc->nb_streams-1];

    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */

    /* component type */
    ctype = get_le32(pb);
    type = get_le32(pb); /* component subtype */

#ifdef DEBUG
    printf("ctype= %c%c%c%c (0x%08lx)\n", *((char *)&ctype), ((char *)&ctype)[1], ((char *)&ctype)[2], ((char *)&ctype)[3], (long) ctype);
    printf("stype= %c%c%c%c\n", *((char *)&type), ((char *)&type)[1], ((char *)&type)[2], ((char *)&type)[3]);
#endif
#ifdef DEBUG
/* XXX: yeah this is ugly... */
    if(ctype == MKTAG('m', 'h', 'l', 'r')) { /* MOV */
        if(type == MKTAG('v', 'i', 'd', 'e'))
            puts("hdlr: vide");
        else if(type == MKTAG('s', 'o', 'u', 'n'))
            puts("hdlr: soun");
    } else if(ctype == 0) { /* MP4 */
        if(type == MKTAG('v', 'i', 'd', 'e'))
            puts("hdlr: vide");
        else if(type == MKTAG('s', 'o', 'u', 'n'))
            puts("hdlr: soun");
        else if(type == MKTAG('o', 'd', 's', 'm'))
            puts("hdlr: odsm");
        else if(type == MKTAG('s', 'd', 's', 'm'))
            puts("hdlr: sdsm");
    } else puts("hdlr: meta");
#endif

    if(ctype == MKTAG('m', 'h', 'l', 'r')) { /* MOV */
        /* helps parsing the string hereafter... */
        c->mp4 = 0;
        if(type == MKTAG('v', 'i', 'd', 'e'))
            st->codec.codec_type = CODEC_TYPE_VIDEO;
        else if(type == MKTAG('s', 'o', 'u', 'n'))
            st->codec.codec_type = CODEC_TYPE_AUDIO;
    } else if(ctype == 0) { /* MP4 */
        /* helps parsing the string hereafter... */
        c->mp4 = 1;
        if(type == MKTAG('v', 'i', 'd', 'e'))
            st->codec.codec_type = CODEC_TYPE_VIDEO;
        else if(type == MKTAG('s', 'o', 'u', 'n'))
            st->codec.codec_type = CODEC_TYPE_AUDIO;
    }
    get_be32(pb); /* component  manufacture */
    get_be32(pb); /* component flags */
    get_be32(pb); /* component flags mask */

    if(atom_size <= 24)
        return 0; /* nothing left to read */
    /* XXX: MP4 uses a C string, not a pascal one */
    /* component name */

    if(c->mp4) {
        /* .mp4: C string */
        while(get_byte(pb) && (++len < (atom_size - 24)));
    } else {
        /* .mov: PASCAL string */
        len = get_byte(pb);
        buf = av_malloc(len+1);
        get_buffer(pb, buf, len);
        buf[len] = '\0';
#ifdef DEBUG
        printf("**buf='%s'\n", buf);
#endif
        av_free(buf);
    }
#if 0
    len = get_byte(pb);
    /* XXX: use a better heuristic */
    if(len < 32) {
        /* assume that it is a Pascal like string */
        buf = av_malloc(len+1);
        get_buffer(pb, buf, len);
        buf[len] = '\0';
#ifdef DEBUG
        printf("**buf='%s'\n", buf);
#endif
        av_free(buf);
    } else {
        /* MP4 string */
        for(;;) {
            if (len == 0)
                break;
            len = get_byte(pb);
        }
    }
#endif
    
    return 0;
}

static int parse_stsd(const MOVParseTableEntry *parse_table, ByteIOContext *pb, UINT32 atom_type, INT64 atom_offset, INT64 atom_size, void *param)
{
    MOVContext *c;
    int entries, size, samp_sz, frames_per_sample;
    UINT32 format;
    AVStream *st;
#ifdef DEBUG
    print_atom("stsd", atom_type, atom_offset, atom_size);
#endif
    c = (MOVContext *)param;
    st = c->fc->streams[c->fc->nb_streams-1];

    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */


    entries = get_be32(pb);

    while(entries--) {
        size = get_be32(pb); /* size */
        format = get_le32(pb); /* data format */
        
        get_be32(pb); /* reserved */
        get_be16(pb); /* reserved */
        get_be16(pb); /* index */
/*        if(format == MKTAG('m', 'p', '4', 'v')) */
/*            st->codec.codec_type=CODEC_TYPE_VIDEO; *//* force things (XXX: was this for .mp4 ?) */
        if(st->codec.codec_type==CODEC_TYPE_VIDEO) {
            st->codec.codec_tag = format;
            st->codec.codec_id = codec_get_id(mov_video_tags, format);
            get_be16(pb); /* version */
            get_be16(pb); /* revision level */
            get_be32(pb); /* vendor */
            get_be32(pb); /* temporal quality */
            get_be32(pb); /* spacial quality */
            st->codec.width = get_be16(pb); /* width */
            st->codec.height = get_be16(pb); /* height */
            get_be32(pb); /* horiz resolution */
            get_be32(pb); /* vert resolution */
            get_be32(pb); /* data size, always 0 */
            frames_per_sample = get_be16(pb); /* frame per samples */
#ifdef DEBUG
	    printf("frames/samples = %d\n", frames_per_sample);
#endif
            url_fskip(pb, 32); /* codec name */

            get_be16(pb); /* depth */
            get_be16(pb); /* colortable id */
            get_be16(pb); /*  */
            get_be16(pb); /*  */
            
            st->codec.sample_rate = 25 * FRAME_RATE_BASE;
            
            if(size > 16)
                url_fskip(pb, size-(16+24+18+32));
        } else {
            st->codec.codec_tag = format;

            get_be16(pb); /* version */
            get_be16(pb); /* revision level */
            get_be32(pb); /* vendor */

            st->codec.channels = get_be16(pb);/* channel count */
            samp_sz = get_be16(pb); /* sample size */
#ifdef DEBUG
            if(samp_sz != 16)
                puts("!!! stsd: audio sample size is not 16 bit !");
#endif            
            st->codec.codec_id = codec_get_id(mov_audio_tags, format);
            /* handle specific s8 codec */
            if (st->codec.codec_id == CODEC_ID_PCM_S16BE && samp_sz == 8)
            st->codec.codec_id = CODEC_ID_PCM_S8;

            get_be16(pb); /* compression id = 0*/
            get_be16(pb); /* packet size = 0 */
            
            st->codec.sample_rate = ((get_be32(pb) >> 16));
            st->codec.bit_rate = 0;

            /* this is for .mp4 files */
            if(format == MKTAG('m', 'p', '4', 'v')) { /* XXX */
                st->codec.codec_type=CODEC_TYPE_VIDEO; /* force things */
                st->codec.codec_id = CODEC_ID_MPEG4;
                st->codec.frame_rate = 25 * FRAME_RATE_BASE;
                st->codec.bit_rate = 100000;
            }
//            if(format == MKTAG('m', 'p', '4', 'a')) { /* XXX */
//                st->codec.codec_type=CODEC_TYPE_AUDIO; /* force things */
//                st->codec.codec_id = CODEC_ID_AAC;
////                st->codec.frame_rate = 25 * FRAME_RATE_BASE;
////                st->codec.bit_rate = 100000;
//            }

#if 0

            get_be16(pb); get_be16(pb); /*  */
            get_be16(pb); /*  */
            get_be16(pb); /*  */
            get_be16(pb); /*  */
            get_be16(pb); /*  */
#endif            
            if(size > 16)
                url_fskip(pb, size-(16+20));
        }
#ifdef DEBUG
        printf("4CC= %c%c%c%c\n", *((char *)&format), ((char *)&format)[1], ((char *)&format)[2], ((char *)&format)[3]);
#endif
    }
/*
    if(len) {
    buf = av_malloc(len+1);
        get_buffer(pb, buf, len);
        buf[len] = '\0';
        puts(buf);
        av_free(buf);
    }
*/
    return 0;
}

static int parse_stco(const MOVParseTableEntry *parse_table, ByteIOContext *pb, UINT32 atom_type, INT64 atom_offset, INT64 atom_size, void *param)
{
    MOVContext *c;
    int entries, i;
    AVStream *st;
    MOVStreamContext *sc;
#ifdef DEBUG
    print_atom("stco", atom_type, atom_offset, atom_size);
#endif
    c = (MOVContext *)param;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = (MOVStreamContext *)st->priv_data;
    
    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */

    entries = get_be32(pb);
    sc->chunk_count = entries;
    sc->chunk_offsets = av_malloc(entries * sizeof(INT64));
    if(atom_type == MKTAG('s', 't', 'c', 'o')) {
        for(i=0; i<entries; i++) {
            sc->chunk_offsets[i] = get_be32(pb);
            /*printf("chunk offset=%ld\n", sc->chunk_offsets[i]);*/
        }
    } else if(atom_type == MKTAG('c', 'o', '6', '4')) {
        for(i=0; i<entries; i++) {
            sc->chunk_offsets[i] = get_be64(pb);
            /*printf("chunk offset=%ld\n", sc->chunk_offsets[i]);*/
        }
    } else
        return -1;
    return 0;
}

static int parse_stsc(const MOVParseTableEntry *parse_table, ByteIOContext *pb, UINT32 atom_type, INT64 atom_offset, INT64 atom_size, void *param)
{
    MOVContext *c;
    int entries, i;
    AVStream *st;
    MOVStreamContext *sc;
#ifdef DEBUG
    print_atom("stsc", atom_type, atom_offset, atom_size);
#endif
    c = (MOVContext *)param;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = (MOVStreamContext *)st->priv_data;
    
    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */

    entries = get_be32(pb);
#ifdef DEBUG
printf("track[%i].stsc.entries = %i\n", c->fc->nb_streams-1, entries);
#endif
    sc->sample_to_chunk_sz = entries;
    sc->sample_to_chunk = av_malloc(entries * sizeof(MOV_sample_to_chunk_tbl));
    for(i=0; i<entries; i++) {
        sc->sample_to_chunk[i].first = get_be32(pb);
        sc->sample_to_chunk[i].count = get_be32(pb);
        sc->sample_to_chunk[i].id = get_be32(pb);
#ifdef DEBUG
/*        printf("sample_to_chunk first=%ld count=%ld, id=%ld\n", sc->sample_to_chunk[i].first, sc->sample_to_chunk[i].count, sc->sample_to_chunk[i].id); */
#endif
    }
    return 0;
}

static int parse_stsz(const MOVParseTableEntry *parse_table, ByteIOContext *pb, UINT32 atom_type, INT64 atom_offset, INT64 atom_size, void *param)
{
    MOVContext *c;
    int entries, i;
    AVStream *st;
    MOVStreamContext *sc;
#ifdef DEBUG
    print_atom("stsz", atom_type, atom_offset, atom_size);
#endif
    c = (MOVContext *)param;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = (MOVStreamContext *)st->priv_data;
    
    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */
    
    sc->sample_size = get_be32(pb);
    entries = get_be32(pb);
    sc->sample_count = entries;
#ifdef DEBUG
    printf("sample_size = %ld sample_count = %ld\n", sc->sample_size, sc->sample_count);
#endif
    if(sc->sample_size)
        return 0; /* there isn't any table following */
    sc->sample_sizes = av_malloc(entries * sizeof(long));
    for(i=0; i<entries; i++) {
        sc->sample_sizes[i] = get_be32(pb);
#ifdef DEBUG
/*        printf("sample_sizes[]=%ld\n", sc->sample_sizes[i]); */
#endif
    }
    return 0;
}

static int parse_stts(const MOVParseTableEntry *parse_table, ByteIOContext *pb, UINT32 atom_type, INT64 atom_offset, INT64 atom_size, void *param)
{
    MOVContext *c;
    int entries, i;
    AVStream *st;
    MOVStreamContext *sc;
#ifdef DEBUG
    print_atom("stts", atom_type, atom_offset, atom_size);
#endif
    c = (MOVContext *)param;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = (MOVStreamContext *)st->priv_data;

    get_byte(pb); /* version */
    get_byte(pb); get_byte(pb); get_byte(pb); /* flags */
    entries = get_be32(pb);
#ifdef DEBUG
printf("track[%i].stts.entries = %i\n", c->fc->nb_streams-1, entries);
#endif
    for(i=0; i<entries; i++) {
        int sample_duration;

        get_be32(pb);
        sample_duration = get_be32(pb);

        if (!i && st->codec.codec_type==CODEC_TYPE_VIDEO) {
            st->codec.frame_rate = FRAME_RATE_BASE * c->streams[c->total_streams]->time_scale;
            if (sample_duration)
                st->codec.frame_rate /= sample_duration;
#ifdef DEBUG
            printf("VIDEO FRAME RATE= %i (sd= %i)\n", st->codec.frame_rate, sample_duration);
#endif
        }
    }
    return 0;
}

#ifdef CONFIG_ZLIB
static int null_read_packet(void *opaque, UINT8 *buf, int buf_size)
{
    return -1;
}

static int parse_cmov(const MOVParseTableEntry *parse_table, ByteIOContext *pb, UINT32 atom_type, INT64 atom_offset, INT64 atom_size, void *param)
{
    MOVContext *c;
    ByteIOContext ctx;
    char *cmov_data;
    unsigned char *moov_data; /* uncompressed data */
    long cmov_len, moov_len;
    int ret;
#ifdef DEBUG
    print_atom("cmov", atom_type, atom_offset, atom_size);
#endif
    c = (MOVContext *)param;

    get_be32(pb); /* dcom atom */
    if (get_le32(pb) != MKTAG( 'd', 'c', 'o', 'm' ))
        return -1;
    if (get_le32(pb) != MKTAG( 'z', 'l', 'i', 'b' )) {
        puts("unknown compression for cmov atom !");
        return -1;
    }
    get_be32(pb); /* cmvd atom */
    if (get_le32(pb) != MKTAG( 'c', 'm', 'v', 'd' ))
        return -1;
    moov_len = get_be32(pb); /* uncompressed size */
    cmov_len = atom_size - 6 * 4;
    
    cmov_data = av_malloc(cmov_len);
    if (!cmov_data)
        return -1;
    moov_data = av_malloc(moov_len);
    if (!moov_data) {
        av_free(cmov_data);
        return -1;
    }
    get_buffer(pb, cmov_data, cmov_len);
    if(uncompress (moov_data, &moov_len, (const Bytef *)cmov_data, cmov_len) != Z_OK)
        return -1;
    if(init_put_byte(&ctx, moov_data, moov_len, 0, NULL, null_read_packet, NULL, NULL) != 0)
        return -1;
    ctx.buf_end = ctx.buffer + moov_len;
    ret = parse_default(parse_table, &ctx, MKTAG( 'm', 'o', 'o', 'v' ), 0, moov_len, param);
    av_free(moov_data);
    av_free(cmov_data);
    return ret;
}
#endif

static const MOVParseTableEntry mov_default_parse_table[] = {
/* mp4 atoms */
{ MKTAG( 'm', 'p', '4', 'a' ), parse_default },
{ MKTAG( 'c', 'o', '6', '4' ), parse_stco },
{ MKTAG( 's', 't', 'c', 'o' ), parse_stco },
{ MKTAG( 'c', 'r', 'h', 'd' ), parse_default },
{ MKTAG( 'c', 't', 't', 's' ), parse_leaf },
{ MKTAG( 'c', 'p', 'r', 't' ), parse_default },
{ MKTAG( 'u', 'r', 'l', ' ' ), parse_leaf },
{ MKTAG( 'u', 'r', 'n', ' ' ), parse_leaf },
{ MKTAG( 'd', 'i', 'n', 'f' ), parse_default },
{ MKTAG( 'd', 'r', 'e', 'f' ), parse_leaf },
{ MKTAG( 's', 't', 'd', 'p' ), parse_default },
{ MKTAG( 'e', 's', 'd', 's' ), parse_default },
{ MKTAG( 'e', 'd', 't', 's' ), parse_default },
{ MKTAG( 'e', 'l', 's', 't' ), parse_leaf },
{ MKTAG( 'u', 'u', 'i', 'd' ), parse_default },
{ MKTAG( 'f', 'r', 'e', 'e' ), parse_leaf },
{ MKTAG( 'h', 'd', 'l', 'r' ), parse_hdlr },
{ MKTAG( 'h', 'm', 'h', 'd' ), parse_leaf },
{ MKTAG( 'h', 'i', 'n', 't' ), parse_leaf },
{ MKTAG( 'n', 'm', 'h', 'd' ), parse_leaf },
{ MKTAG( 'm', 'p', '4', 's' ), parse_default },
{ MKTAG( 'm', 'd', 'i', 'a' ), parse_default },
{ MKTAG( 'm', 'd', 'a', 't' ), parse_mdat },
{ MKTAG( 'm', 'd', 'h', 'd' ), parse_mdhd },
{ MKTAG( 'm', 'i', 'n', 'f' ), parse_default },
{ MKTAG( 'm', 'o', 'o', 'v' ), parse_moov },
{ MKTAG( 'm', 'v', 'h', 'd' ), parse_mvhd },
{ MKTAG( 'i', 'o', 'd', 's' ), parse_leaf },
{ MKTAG( 'o', 'd', 'h', 'd' ), parse_default },
{ MKTAG( 'm', 'p', 'o', 'd' ), parse_leaf },
{ MKTAG( 's', 't', 's', 'd' ), parse_stsd },
{ MKTAG( 's', 't', 's', 'z' ), parse_stsz },
{ MKTAG( 's', 't', 'b', 'l' ), parse_default },
{ MKTAG( 's', 't', 's', 'c' ), parse_stsc },
{ MKTAG( 's', 'd', 'h', 'd' ), parse_default },
{ MKTAG( 's', 't', 's', 'h' ), parse_default },
{ MKTAG( 's', 'k', 'i', 'p' ), parse_default },
{ MKTAG( 's', 'm', 'h', 'd' ), parse_leaf },
{ MKTAG( 'd', 'p', 'n', 'd' ), parse_leaf },
{ MKTAG( 's', 't', 's', 's' ), parse_leaf },
{ MKTAG( 's', 't', 't', 's' ), parse_stts },
{ MKTAG( 't', 'r', 'a', 'k' ), parse_trak },
{ MKTAG( 't', 'k', 'h', 'd' ), parse_tkhd },
{ MKTAG( 't', 'r', 'e', 'f' ), parse_default }, /* not really */
{ MKTAG( 'u', 'd', 't', 'a' ), parse_leaf },
{ MKTAG( 'v', 'm', 'h', 'd' ), parse_leaf },
{ MKTAG( 'm', 'p', '4', 'v' ), parse_default },
/* extra mp4 */
{ MKTAG( 'M', 'D', 'E', 'S' ), parse_leaf },
/* QT atoms */
{ MKTAG( 'c', 'h', 'a', 'p' ), parse_leaf },
{ MKTAG( 'c', 'l', 'i', 'p' ), parse_default },
{ MKTAG( 'c', 'r', 'g', 'n' ), parse_leaf },
{ MKTAG( 'k', 'm', 'a', 't' ), parse_leaf },
{ MKTAG( 'm', 'a', 't', 't' ), parse_default },
{ MKTAG( 'r', 'd', 'r', 'f' ), parse_leaf },
{ MKTAG( 'r', 'm', 'd', 'a' ), parse_default },
{ MKTAG( 'r', 'm', 'd', 'r' ), parse_leaf },
//{ MKTAG( 'r', 'm', 'q', 'u' ), parse_leaf },
{ MKTAG( 'r', 'm', 'r', 'a' ), parse_default },
{ MKTAG( 's', 'c', 'p', 't' ), parse_leaf },
{ MKTAG( 's', 'y', 'n', 'c' ), parse_leaf },
{ MKTAG( 's', 's', 'r', 'c' ), parse_leaf },
{ MKTAG( 't', 'c', 'm', 'd' ), parse_leaf },
{ MKTAG( 'w', 'i', 'd', 'e' ), parse_wide }, /* place holder */
#ifdef CONFIG_ZLIB
{ MKTAG( 'c', 'm', 'o', 'v' ), parse_cmov },
#else
{ MKTAG( 'c', 'm', 'o', 'v' ), parse_leaf },
#endif
{ 0L, parse_leaf }
};

static void mov_free_stream_context(MOVStreamContext *sc)
{
    if(sc) {
        av_free(sc->chunk_offsets);
        av_free(sc->sample_to_chunk);
        av_free(sc);
    }
}

/* XXX: is it suffisant ? */
static int mov_probe(AVProbeData *p)
{
    int offset;

    /* check file header */
    if (p->buf_size <= 12)
        return 0;
    if ((p->buf[4] == 'm' && p->buf[5] == 'o' &&
         p->buf[6] == 'o' && p->buf[7] == 'v') ||
        (p->buf[4] == 'w' && p->buf[5] == 'i' &&
         p->buf[6] == 'd' && p->buf[7] == 'e') ||
        (p->buf[4] == 'f' && p->buf[5] == 'r' &&
         p->buf[6] == 'e' && p->buf[7] == 'e') ||
        (p->buf[4] == 'm' && p->buf[5] == 'd' &&
         p->buf[6] == 'a' && p->buf[7] == 't'))
        return AVPROBE_SCORE_MAX;
    if (p->buf[4] == 'f' && p->buf[5] == 't' &&
        p->buf[6] == 'y' && p->buf[7] == 'p') {
        offset = p->buf[0]; offset <<= 8;
        offset |= p->buf[1]; offset <<= 8;
        offset |= p->buf[2]; offset <<= 8;
        offset |= p->buf[3];
        if (offset > p->buf_size)
            return 0;
        if ((p->buf[offset+4] == 'm' && p->buf[offset+5] == 'o' &&
             p->buf[offset+6] == 'o' && p->buf[offset+7] == 'v') ||
            (p->buf[offset+4] == 'w' && p->buf[offset+5] == 'i' &&
             p->buf[offset+6] == 'd' && p->buf[offset+7] == 'e') ||
            (p->buf[offset+4] == 'm' && p->buf[offset+5] == 'd' &&
             p->buf[offset+6] == 'a' && p->buf[offset+7] == 't'))
            return AVPROBE_SCORE_MAX;

    }
    return 0;
}

static int mov_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    MOVContext *mov = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int i, j, nb, err;
    INT64 size;

    mov->fc = s;
#if 0
    /* XXX: I think we should auto detect */
    if(s->iformat->name[1] == 'p')
        mov->mp4 = 1;
#endif
    if(!url_is_streamed(pb)) /* .mov and .mp4 aren't streamable anyway (only progressive download if moov is before mdat) */
        size = url_filesize(url_fileno(pb));
    else
        size = 0x7FFFFFFFFFFFFFFF;

#ifdef DEBUG
    printf("filesz=%Ld\n", size);
#endif

    /* check MOV header */
    err = parse_default(mov_default_parse_table, pb, 0L, 0LL, size, mov);
    if(err<0 || (!mov->found_moov || !mov->found_mdat)) {
        puts("header not found !!!");
        exit(1);
    }
#ifdef DEBUG
    printf("on_parse_exit_offset=%d\n", (int) url_ftell(pb));
#endif
    /* some cleanup : make sure we are on the mdat atom */
    if(!url_is_streamed(pb) && (url_ftell(pb) != mov->mdat_offset))
        url_fseek(pb, mov->mdat_offset, SEEK_SET);

    mov->next_chunk_offset = mov->mdat_offset; /* initialise reading */

#ifdef DEBUG
    printf("mdat_reset_offset=%d\n", (int) url_ftell(pb));
#endif

#ifdef DEBUG
    printf("streams= %d\n", s->nb_streams);
#endif
    mov->total_streams = nb = s->nb_streams;
    
#if 1
    for(i=0; i<s->nb_streams;) {
        if(s->streams[i]->codec.codec_type == CODEC_TYPE_MOV_OTHER) {/* not audio, not video, delete */
            av_free(s->streams[i]);
            for(j=i+1; j<s->nb_streams; j++)
                s->streams[j-1] = s->streams[j];
            s->nb_streams--;
        } else
            i++;
    }
    for(i=0; i<s->nb_streams;i++) {
        MOVStreamContext *sc;
        sc = (MOVStreamContext *)s->streams[i]->priv_data;
        sc->ffindex = i;
        sc->is_ff_stream = 1;
    }
#endif
#ifdef DEBUG
    printf("real streams= %d\n", s->nb_streams);
#endif
    return 0;
}

/* Yes, this is ugly... I didn't write the specs of QT :p */
/* XXX:remove useless commented code sometime */
static int mov_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MOVContext *mov = s->priv_data;
    INT64 offset = 0x0FFFFFFFFFFFFFFF;
    int i;
    int st_id = 0, size;
    size = 0x0FFFFFFF;

#ifdef MOV_SPLIT_CHUNKS
    if (mov->partial) {
        
        int idx;

        st_id = mov->partial - 1;
        idx = mov->streams[st_id]->sample_to_chunk_index;
        if (idx < 0) return 0;
        size = mov->streams[st_id]->sample_sizes[mov->streams[st_id]->current_sample];

        mov->streams[st_id]->current_sample++;
        mov->streams[st_id]->left_in_chunk--;

        if(mov->streams[st_id]->left_in_chunk <= 0)
            mov->partial = 0;
        offset = mov->next_chunk_offset;
        /* extract the sample */

        goto readchunk;
    }
#endif

again:
    for(i=0; i<mov->total_streams; i++) {
        if((mov->streams[i]->next_chunk < mov->streams[i]->chunk_count)
        && (mov->streams[i]->chunk_offsets[mov->streams[i]->next_chunk] < offset)) {
            st_id = i;
            offset = mov->streams[i]->chunk_offsets[mov->streams[i]->next_chunk];
        }
    }
    mov->streams[st_id]->next_chunk++;
    if(offset==0x0FFFFFFFFFFFFFFF)
        return -1;
    
    if(mov->next_chunk_offset < offset) { /* some meta data */
        url_fskip(&s->pb, (offset - mov->next_chunk_offset));
        mov->next_chunk_offset = offset;
    }

//printf("chunk: [%i] %lli -> %lli\n", st_id, mov->next_chunk_offset, offset);
    if(!mov->streams[st_id]->is_ff_stream) {
        url_fskip(&s->pb, (offset - mov->next_chunk_offset));
        mov->next_chunk_offset = offset;
        offset = 0x0FFFFFFFFFFFFFFF;
        goto again;
    }

    /* now get the chunk size... */

    for(i=0; i<mov->total_streams; i++) {
        if((mov->streams[i]->next_chunk < mov->streams[i]->chunk_count)
        && ((mov->streams[i]->chunk_offsets[mov->streams[i]->next_chunk] - offset) < size)) {
            size = mov->streams[i]->chunk_offsets[mov->streams[i]->next_chunk] - offset;
        }
    }
#ifdef MOV_SPLIT_CHUNKS
    /* split chunks into samples */
    if(mov->streams[st_id]->sample_size == 0) {
        int idx;
        idx = mov->streams[st_id]->sample_to_chunk_index;
        if ((idx + 1 < mov->streams[st_id]->sample_to_chunk_sz)
               && (mov->streams[st_id]->next_chunk >= mov->streams[st_id]->sample_to_chunk[idx + 1].first))
           idx++; 
        mov->streams[st_id]->sample_to_chunk_index = idx;
        if(idx >= 0 && mov->streams[st_id]->sample_to_chunk[idx].count != 1) {
            mov->partial = st_id+1;
            /* we'll have to get those samples before next chunk */
            mov->streams[st_id]->left_in_chunk = (mov->streams[st_id]->sample_to_chunk[idx].count) - 1;
            size = mov->streams[st_id]->sample_sizes[mov->streams[st_id]->current_sample];
        }

        mov->streams[st_id]->current_sample++;
    }
#endif

readchunk:

//printf("chunk: [%i] %lli -> %lli (%i)\n", st_id, offset, offset + size, size);
    if(size == 0x0FFFFFFF)
        size = mov->mdat_size + mov->mdat_offset - offset;
    if(size < 0)
        return -1;
    if(size == 0)
        return -1;
    av_new_packet(pkt, size);
    pkt->stream_index = mov->streams[st_id]->ffindex;

    get_buffer(&s->pb, pkt->data, pkt->size);

#ifdef DEBUG
/*
    printf("Packet (%d, %d, %ld) ", pkt->stream_index, st_id, pkt->size);
    for(i=0; i<8; i++)
        printf("%02x ", pkt->data[i]);
    for(i=0; i<8; i++)
        printf("%c ", (pkt->data[i]) & 0x7F);
    puts("");
*/
#endif

    mov->next_chunk_offset = offset + size;

    return 0;
}

static int mov_read_close(AVFormatContext *s)
{
    int i;
    MOVContext *mov = s->priv_data;
    for(i=0; i<mov->total_streams; i++)
        mov_free_stream_context(mov->streams[i]);
    for(i=0; i<s->nb_streams; i++)
        av_free(s->streams[i]);
    return 0;
}

static AVInputFormat mov_iformat = {
    "mov",
    "QuickTime/MPEG4 format",
    sizeof(MOVContext),
    mov_probe,
    mov_read_header,
    mov_read_packet,
    mov_read_close,
};

int mov_init(void)
{
    av_register_input_format(&mov_iformat);
    return 0;
}
