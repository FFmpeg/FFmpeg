/*
 * MOV demuxer
 * Copyright (c) 2001 Fabrice Bellard
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

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "riff.h"
#include "isom.h"
#include "dv.h"
#include "libavcodec/mpeg4audio.h"
#include "libavcodec/mpegaudiodata.h"

#if CONFIG_ZLIB
#include <zlib.h>
#endif

/*
 * First version by Francois Revol revol@free.fr
 * Seek function by Gael Chardon gael.dev@4now.net
 *
 * Features and limitations:
 * - reads most of the QT files I have (at least the structure),
 *   Sample QuickTime files with mp3 audio can be found at: http://www.3ivx.com/showcase.html
 * - the code is quite ugly... maybe I won't do it recursive next time :-)
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

typedef struct {
    int first;
    int count;
    int id;
} MOVStsc;

typedef struct {
    uint32_t type;
    char *path;
} MOVDref;

typedef struct {
    uint32_t type;
    int64_t offset;
    int64_t size; /* total size (excluding the size and type fields) */
} MOVAtom;

struct MOVParseTableEntry;

typedef struct {
    unsigned track_id;
    uint64_t base_data_offset;
    uint64_t moof_offset;
    unsigned stsd_id;
    unsigned duration;
    unsigned size;
    unsigned flags;
} MOVFragment;

typedef struct {
    unsigned track_id;
    unsigned stsd_id;
    unsigned duration;
    unsigned size;
    unsigned flags;
} MOVTrackExt;

typedef struct MOVStreamContext {
    ByteIOContext *pb;
    int ffindex; /* the ffmpeg stream id */
    int next_chunk;
    unsigned int chunk_count;
    int64_t *chunk_offsets;
    unsigned int stts_count;
    MOVStts *stts_data;
    unsigned int ctts_count;
    MOVStts *ctts_data;
    unsigned int edit_count; /* number of 'edit' (elst atom) */
    unsigned int sample_to_chunk_sz;
    MOVStsc *sample_to_chunk;
    int sample_to_ctime_index;
    int sample_to_ctime_sample;
    unsigned int sample_size;
    unsigned int sample_count;
    int *sample_sizes;
    unsigned int keyframe_count;
    int *keyframes;
    int time_scale;
    int time_rate;
    int current_sample;
    unsigned int bytes_per_frame;
    unsigned int samples_per_frame;
    int dv_audio_container;
    int pseudo_stream_id; ///< -1 means demux all ids
    int16_t audio_cid; ///< stsd audio compression id
    unsigned drefs_count;
    MOVDref *drefs;
    int dref_id;
    int wrong_dts; ///< dts are wrong due to negative ctts
    int width;  ///< tkhd width
    int height; ///< tkhd height
} MOVStreamContext;

typedef struct MOVContext {
    AVFormatContext *fc;
    int time_scale;
    int64_t duration; /* duration of the longest track */
    int found_moov; /* when both 'moov' and 'mdat' sections has been found */
    int found_mdat; /* we suppose we have enough data to read the file */
    AVPaletteControl palette_control;
    DVDemuxContext *dv_demux;
    AVFormatContext *dv_fctx;
    int isom; /* 1 if file is ISO Media (mp4/3gp) */
    MOVFragment fragment; ///< current fragment in moof atom
    MOVTrackExt *trex_data;
    unsigned trex_count;
    int itunes_metadata; ///< metadata are itunes style
} MOVContext;


/* XXX: it's the first time I make a recursive parser I think... sorry if it's ugly :P */

/* those functions parse an atom */
/* return code:
  0: continue to parse next atom
 <0: error occurred, exit
*/
/* links atom IDs to parse functions */
typedef struct MOVParseTableEntry {
    uint32_t type;
    int (*parse)(MOVContext *ctx, ByteIOContext *pb, MOVAtom atom);
} MOVParseTableEntry;

static const MOVParseTableEntry mov_default_parse_table[];

static int mov_read_default(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    int64_t total_size = 0;
    MOVAtom a;
    int i;
    int err = 0;

    a.offset = atom.offset;

    if (atom.size < 0)
        atom.size = INT64_MAX;
    while(((total_size + 8) < atom.size) && !url_feof(pb) && !err) {
        a.size = atom.size;
        a.type=0;
        if(atom.size >= 8) {
            a.size = get_be32(pb);
            a.type = get_le32(pb);
        }
        total_size += 8;
        a.offset += 8;
        dprintf(c->fc, "type: %08x  %.4s  sz: %"PRIx64"  %"PRIx64"   %"PRIx64"\n",
                a.type, (char*)&a.type, a.size, atom.size, total_size);
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
        if(a.size < 0)
            break;
        a.size = FFMIN(a.size, atom.size - total_size);

        for (i = 0; mov_default_parse_table[i].type != 0
             && mov_default_parse_table[i].type != a.type; i++)
            /* empty */;

        if (mov_default_parse_table[i].type == 0) { /* skip leaf atoms data */
            url_fskip(pb, a.size);
        } else {
            int64_t start_pos = url_ftell(pb);
            int64_t left;
            err = mov_default_parse_table[i].parse(c, pb, a);
            if (url_is_streamed(pb) && c->found_moov && c->found_mdat)
                break;
            left = a.size - url_ftell(pb) + start_pos;
            if (left > 0) /* skip garbage at atom end */
                url_fskip(pb, left);
        }

        a.offset += a.size;
        total_size += a.size;
    }

    if (!err && total_size < atom.size && atom.size < 0x7ffff)
        url_fskip(pb, atom.size - total_size);

    return err;
}

static int mov_read_dref(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    MOVStreamContext *sc = st->priv_data;
    int entries, i, j;

    get_be32(pb); // version + flags
    entries = get_be32(pb);
    if (entries >= UINT_MAX / sizeof(*sc->drefs))
        return -1;
    sc->drefs_count = entries;
    sc->drefs = av_mallocz(entries * sizeof(*sc->drefs));

    for (i = 0; i < sc->drefs_count; i++) {
        MOVDref *dref = &sc->drefs[i];
        uint32_t size = get_be32(pb);
        int64_t next = url_ftell(pb) + size - 4;

        dref->type = get_le32(pb);
        get_be32(pb); // version + flags
        dprintf(c->fc, "type %.4s size %d\n", (char*)&dref->type, size);

        if (dref->type == MKTAG('a','l','i','s') && size > 150) {
            /* macintosh alias record */
            uint16_t volume_len, len;
            char volume[28];
            int16_t type;

            url_fskip(pb, 10);

            volume_len = get_byte(pb);
            volume_len = FFMIN(volume_len, 27);
            get_buffer(pb, volume, 27);
            volume[volume_len] = 0;
            av_log(c->fc, AV_LOG_DEBUG, "volume %s, len %d\n", volume, volume_len);

            url_fskip(pb, 112);

            for (type = 0; type != -1 && url_ftell(pb) < next; ) {
                type = get_be16(pb);
                len = get_be16(pb);
                av_log(c->fc, AV_LOG_DEBUG, "type %d, len %d\n", type, len);
                if (len&1)
                    len += 1;
                if (type == 2) { // absolute path
                    av_free(dref->path);
                    dref->path = av_mallocz(len+1);
                    if (!dref->path)
                        return AVERROR(ENOMEM);
                    get_buffer(pb, dref->path, len);
                    if (len > volume_len && !strncmp(dref->path, volume, volume_len)) {
                        len -= volume_len;
                        memmove(dref->path, dref->path+volume_len, len);
                        dref->path[len] = 0;
                    }
                    for (j = 0; j < len; j++)
                        if (dref->path[j] == ':')
                            dref->path[j] = '/';
                    av_log(c->fc, AV_LOG_DEBUG, "path %s\n", dref->path);
                } else
                    url_fskip(pb, len);
            }
        }
        url_fseek(pb, next, SEEK_SET);
    }
    return 0;
}

static int mov_read_hdlr(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    uint32_t type;
    uint32_t ctype;

    get_byte(pb); /* version */
    get_be24(pb); /* flags */

    /* component type */
    ctype = get_le32(pb);
    type = get_le32(pb); /* component subtype */

    dprintf(c->fc, "ctype= %c%c%c%c (0x%08x)\n", *((char *)&ctype), ((char *)&ctype)[1],
            ((char *)&ctype)[2], ((char *)&ctype)[3], (int) ctype);
    dprintf(c->fc, "stype= %c%c%c%c\n",
            *((char *)&type), ((char *)&type)[1], ((char *)&type)[2], ((char *)&type)[3]);
    if(!ctype)
        c->isom = 1;
    if     (type == MKTAG('v','i','d','e'))
        st->codec->codec_type = CODEC_TYPE_VIDEO;
    else if(type == MKTAG('s','o','u','n'))
        st->codec->codec_type = CODEC_TYPE_AUDIO;
    else if(type == MKTAG('m','1','a',' '))
        st->codec->codec_id = CODEC_ID_MP2;
    else if(type == MKTAG('s','u','b','p')) {
        st->codec->codec_type = CODEC_TYPE_SUBTITLE;
    }
    get_be32(pb); /* component  manufacture */
    get_be32(pb); /* component flags */
    get_be32(pb); /* component flags mask */

    if(atom.size <= 24)
        return 0; /* nothing left to read */

    url_fskip(pb, atom.size - (url_ftell(pb) - atom.offset));
    return 0;
}

static int mp4_read_descr_len(ByteIOContext *pb)
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

static int mp4_read_descr(MOVContext *c, ByteIOContext *pb, int *tag)
{
    int len;
    *tag = get_byte(pb);
    len = mp4_read_descr_len(pb);
    dprintf(c->fc, "MPEG4 description: tag=0x%02x len=%d\n", *tag, len);
    return len;
}

#define MP4ESDescrTag                   0x03
#define MP4DecConfigDescrTag            0x04
#define MP4DecSpecificDescrTag          0x05

static const AVCodecTag mp4_audio_types[] = {
    { CODEC_ID_MP3ON4, 29 }, /* old mp3on4 draft */
    { CODEC_ID_MP3ON4, 32 }, /* layer 1 */
    { CODEC_ID_MP3ON4, 33 }, /* layer 2 */
    { CODEC_ID_MP3ON4, 34 }, /* layer 3 */
    { CODEC_ID_NONE,    0 },
};

static int mov_read_esds(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    int tag, len;

    get_be32(pb); /* version + flags */
    len = mp4_read_descr(c, pb, &tag);
    if (tag == MP4ESDescrTag) {
        get_be16(pb); /* ID */
        get_byte(pb); /* priority */
    } else
        get_be16(pb); /* ID */

    len = mp4_read_descr(c, pb, &tag);
    if (tag == MP4DecConfigDescrTag) {
        int object_type_id = get_byte(pb);
        get_byte(pb); /* stream type */
        get_be24(pb); /* buffer size db */
        get_be32(pb); /* max bitrate */
        get_be32(pb); /* avg bitrate */

        st->codec->codec_id= codec_get_id(ff_mp4_obj_type, object_type_id);
        dprintf(c->fc, "esds object type id %d\n", object_type_id);
        len = mp4_read_descr(c, pb, &tag);
        if (tag == MP4DecSpecificDescrTag) {
            dprintf(c->fc, "Specific MPEG4 header len=%d\n", len);
            if((uint64_t)len > (1<<30))
                return -1;
            st->codec->extradata = av_mallocz(len + FF_INPUT_BUFFER_PADDING_SIZE);
            if (!st->codec->extradata)
                return AVERROR(ENOMEM);
            get_buffer(pb, st->codec->extradata, len);
            st->codec->extradata_size = len;
            if (st->codec->codec_id == CODEC_ID_AAC) {
                MPEG4AudioConfig cfg;
                ff_mpeg4audio_get_config(&cfg, st->codec->extradata,
                                         st->codec->extradata_size);
                if (cfg.chan_config > 7)
                    return -1;
                st->codec->channels = ff_mpeg4audio_channels[cfg.chan_config];
                if (cfg.object_type == 29 && cfg.sampling_index < 3) // old mp3on4
                    st->codec->sample_rate = ff_mpa_freq_tab[cfg.sampling_index];
                else
                    st->codec->sample_rate = cfg.sample_rate; // ext sample rate ?
                dprintf(c->fc, "mp4a config channels %d obj %d ext obj %d "
                        "sample rate %d ext sample rate %d\n", st->codec->channels,
                        cfg.object_type, cfg.ext_object_type,
                        cfg.sample_rate, cfg.ext_sample_rate);
                if (!(st->codec->codec_id = codec_get_id(mp4_audio_types,
                                                         cfg.object_type)))
                    st->codec->codec_id = CODEC_ID_AAC;
            }
        }
    }
    return 0;
}

static int mov_read_pasp(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    const int num = get_be32(pb);
    const int den = get_be32(pb);
    AVStream * const st = c->fc->streams[c->fc->nb_streams-1];
    if (den != 0) {
        if ((st->sample_aspect_ratio.den != 1 || st->sample_aspect_ratio.num) && // default
            (den != st->sample_aspect_ratio.den || num != st->sample_aspect_ratio.num))
            av_log(c->fc, AV_LOG_WARNING,
                   "sample aspect ratio already set to %d:%d, overriding by 'pasp' atom\n",
                   st->sample_aspect_ratio.num, st->sample_aspect_ratio.den);
        st->sample_aspect_ratio.num = num;
        st->sample_aspect_ratio.den = den;
    }
    return 0;
}

/* this atom contains actual media data */
static int mov_read_mdat(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    if(atom.size == 0) /* wrong one (MP4) */
        return 0;
    c->found_mdat=1;
    return 0; /* now go for moov */
}

static int mov_read_ftyp(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
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
static int mov_read_moov(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    if (mov_read_default(c, pb, atom) < 0)
        return -1;
    /* we parsed the 'moov' atom, we can terminate the parsing as soon as we find the 'mdat' */
    /* so we don't parse the whole file if over a network */
    c->found_moov=1;
    return 0; /* now go for mdat */
}

static int mov_read_moof(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    c->fragment.moof_offset = url_ftell(pb) - 8;
    dprintf(c->fc, "moof offset %llx\n", c->fragment.moof_offset);
    return mov_read_default(c, pb, atom);
}

static int mov_read_mdhd(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    MOVStreamContext *sc = st->priv_data;
    int version = get_byte(pb);
    unsigned lang;

    if (version > 1)
        return -1; /* unsupported */

    get_be24(pb); /* flags */
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

static int mov_read_mvhd(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    int version = get_byte(pb); /* version */
    get_be24(pb); /* flags */

    if (version == 1) {
        get_be64(pb);
        get_be64(pb);
    } else {
        get_be32(pb); /* creation time */
        get_be32(pb); /* modification time */
    }
    c->time_scale = get_be32(pb); /* time scale */

    dprintf(c->fc, "time scale = %i\n", c->time_scale);

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

static int mov_read_smi(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];

    if((uint64_t)atom.size > (1<<30))
        return -1;

    // currently SVQ3 decoder expect full STSD header - so let's fake it
    // this should be fixed and just SMI header should be passed
    av_free(st->codec->extradata);
    st->codec->extradata = av_mallocz(atom.size + 0x5a + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!st->codec->extradata)
        return AVERROR(ENOMEM);
    st->codec->extradata_size = 0x5a + atom.size;
    memcpy(st->codec->extradata, "SVQ3", 4); // fake
    get_buffer(pb, st->codec->extradata + 0x5a, atom.size);
    dprintf(c->fc, "Reading SMI %"PRId64"  %s\n", atom.size, st->codec->extradata + 0x5a);
    return 0;
}

static int mov_read_enda(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    int little_endian = get_be16(pb);

    dprintf(c->fc, "enda %d\n", little_endian);
    if (little_endian == 1) {
        switch (st->codec->codec_id) {
        case CODEC_ID_PCM_S24BE:
            st->codec->codec_id = CODEC_ID_PCM_S24LE;
            break;
        case CODEC_ID_PCM_S32BE:
            st->codec->codec_id = CODEC_ID_PCM_S32LE;
            break;
        case CODEC_ID_PCM_F32BE:
            st->codec->codec_id = CODEC_ID_PCM_F32LE;
            break;
        case CODEC_ID_PCM_F64BE:
            st->codec->codec_id = CODEC_ID_PCM_F64LE;
            break;
        default:
            break;
        }
    }
    return 0;
}

/* FIXME modify qdm2/svq3/h264 decoders to take full atom as extradata */
static int mov_read_extradata(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    uint64_t size;
    uint8_t *buf;

    if (c->fc->nb_streams < 1) // will happen with jp2 files
        return 0;
    st= c->fc->streams[c->fc->nb_streams-1];
    size= (uint64_t)st->codec->extradata_size + atom.size + 8 + FF_INPUT_BUFFER_PADDING_SIZE;
    if(size > INT_MAX || (uint64_t)atom.size > INT_MAX)
        return -1;
    buf= av_realloc(st->codec->extradata, size);
    if(!buf)
        return -1;
    st->codec->extradata= buf;
    buf+= st->codec->extradata_size;
    st->codec->extradata_size= size - FF_INPUT_BUFFER_PADDING_SIZE;
    AV_WB32(       buf    , atom.size + 8);
    AV_WL32(       buf + 4, atom.type);
    get_buffer(pb, buf + 8, atom.size);
    return 0;
}

static int mov_read_wave(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];

    if((uint64_t)atom.size > (1<<30))
        return -1;

    if (st->codec->codec_id == CODEC_ID_QDM2) {
        // pass all frma atom to codec, needed at least for QDM2
        av_free(st->codec->extradata);
        st->codec->extradata = av_mallocz(atom.size + FF_INPUT_BUFFER_PADDING_SIZE);
        if (!st->codec->extradata)
            return AVERROR(ENOMEM);
        st->codec->extradata_size = atom.size;
        get_buffer(pb, st->codec->extradata, atom.size);
    } else if (atom.size > 8) { /* to read frma, esds atoms */
        if (mov_read_default(c, pb, atom) < 0)
            return -1;
    } else
        url_fskip(pb, atom.size);
    return 0;
}

/**
 * This function reads atom content and puts data in extradata without tag
 * nor size unlike mov_read_extradata.
 */
static int mov_read_glbl(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];

    if((uint64_t)atom.size > (1<<30))
        return -1;

    av_free(st->codec->extradata);
    st->codec->extradata = av_mallocz(atom.size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!st->codec->extradata)
        return AVERROR(ENOMEM);
    st->codec->extradata_size = atom.size;
    get_buffer(pb, st->codec->extradata, atom.size);
    return 0;
}

static int mov_read_stco(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    MOVStreamContext *sc = st->priv_data;
    unsigned int i, entries;

    get_byte(pb); /* version */
    get_be24(pb); /* flags */

    entries = get_be32(pb);

    if(entries >= UINT_MAX/sizeof(int64_t))
        return -1;

    sc->chunk_count = entries;
    sc->chunk_offsets = av_malloc(entries * sizeof(int64_t));
    if (!sc->chunk_offsets)
        return -1;
    if      (atom.type == MKTAG('s','t','c','o'))
        for(i=0; i<entries; i++)
            sc->chunk_offsets[i] = get_be32(pb);
    else if (atom.type == MKTAG('c','o','6','4'))
        for(i=0; i<entries; i++)
            sc->chunk_offsets[i] = get_be64(pb);
    else
        return -1;

    return 0;
}

/**
 * Compute codec id for 'lpcm' tag.
 * See CoreAudioTypes and AudioStreamBasicDescription at Apple.
 */
static enum CodecID mov_get_lpcm_codec_id(int bps, int flags)
{
    if (flags & 1) { // floating point
        if (flags & 2) { // big endian
            if      (bps == 32) return CODEC_ID_PCM_F32BE;
            else if (bps == 64) return CODEC_ID_PCM_F64BE;
        } else {
            if      (bps == 32) return CODEC_ID_PCM_F32LE;
            else if (bps == 64) return CODEC_ID_PCM_F64LE;
        }
    } else {
        if (flags & 2) {
            if      (bps == 8)
                // signed integer
                if (flags & 4)  return CODEC_ID_PCM_S8;
                else            return CODEC_ID_PCM_U8;
            else if (bps == 16) return CODEC_ID_PCM_S16BE;
            else if (bps == 24) return CODEC_ID_PCM_S24BE;
            else if (bps == 32) return CODEC_ID_PCM_S32BE;
        } else {
            if      (bps == 8)
                if (flags & 4)  return CODEC_ID_PCM_S8;
                else            return CODEC_ID_PCM_U8;
            else if (bps == 16) return CODEC_ID_PCM_S16LE;
            else if (bps == 24) return CODEC_ID_PCM_S24LE;
            else if (bps == 32) return CODEC_ID_PCM_S32LE;
        }
    }
    return CODEC_ID_NONE;
}

static int mov_read_stsd(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    MOVStreamContext *sc = st->priv_data;
    int j, entries, pseudo_stream_id;

    get_byte(pb); /* version */
    get_be24(pb); /* flags */

    entries = get_be32(pb);

    for(pseudo_stream_id=0; pseudo_stream_id<entries; pseudo_stream_id++) {
        //Parsing Sample description table
        enum CodecID id;
        int dref_id;
        MOVAtom a = { 0, 0, 0 };
        int64_t start_pos = url_ftell(pb);
        int size = get_be32(pb); /* size */
        uint32_t format = get_le32(pb); /* data format */

        get_be32(pb); /* reserved */
        get_be16(pb); /* reserved */
        dref_id = get_be16(pb);

        if (st->codec->codec_tag &&
            st->codec->codec_tag != format &&
            (c->fc->video_codec_id ? codec_get_id(codec_movvideo_tags, format) != c->fc->video_codec_id
                                   : st->codec->codec_tag != MKTAG('j','p','e','g'))
           ){
            /* Multiple fourcc, we skip JPEG. This is not correct, we should
             * export it as a separate AVStream but this needs a few changes
             * in the MOV demuxer, patch welcome. */
            av_log(c->fc, AV_LOG_WARNING, "multiple fourcc not supported\n");
            url_fskip(pb, size - (url_ftell(pb) - start_pos));
            continue;
        }
        sc->pseudo_stream_id = st->codec->codec_tag ? -1 : pseudo_stream_id;
        sc->dref_id= dref_id;

        st->codec->codec_tag = format;
        id = codec_get_id(codec_movaudio_tags, format);
        if (id<=0 && (format&0xFFFF) == 'm'+('s'<<8))
            id = codec_get_id(codec_wav_tags, bswap_32(format)&0xFFFF);

        if (st->codec->codec_type != CODEC_TYPE_VIDEO && id > 0) {
            st->codec->codec_type = CODEC_TYPE_AUDIO;
        } else if (st->codec->codec_type != CODEC_TYPE_AUDIO && /* do not overwrite codec type */
                   format && format != MKTAG('m','p','4','s')) { /* skip old asf mpeg4 tag */
            id = codec_get_id(codec_movvideo_tags, format);
            if (id <= 0)
                id = codec_get_id(codec_bmp_tags, format);
            if (id > 0)
                st->codec->codec_type = CODEC_TYPE_VIDEO;
            else if(st->codec->codec_type == CODEC_TYPE_DATA){
                id = codec_get_id(ff_codec_movsubtitle_tags, format);
                if(id > 0)
                    st->codec->codec_type = CODEC_TYPE_SUBTITLE;
            }
        }

        dprintf(c->fc, "size=%d 4CC= %c%c%c%c codec_type=%d\n", size,
                (format >> 0) & 0xff, (format >> 8) & 0xff, (format >> 16) & 0xff,
                (format >> 24) & 0xff, st->codec->codec_type);

        if(st->codec->codec_type==CODEC_TYPE_VIDEO) {
            uint8_t codec_name[32];
            unsigned int color_depth;
            int color_greyscale;

            st->codec->codec_id = id;
            get_be16(pb); /* version */
            get_be16(pb); /* revision level */
            get_be32(pb); /* vendor */
            get_be32(pb); /* temporal quality */
            get_be32(pb); /* spatial quality */

            st->codec->width = get_be16(pb); /* width */
            st->codec->height = get_be16(pb); /* height */

            get_be32(pb); /* horiz resolution */
            get_be32(pb); /* vert resolution */
            get_be32(pb); /* data size, always 0 */
            get_be16(pb); /* frames per samples */

            get_buffer(pb, codec_name, 32); /* codec name, pascal string */
            if (codec_name[0] <= 31) {
                memcpy(st->codec->codec_name, &codec_name[1],codec_name[0]);
                st->codec->codec_name[codec_name[0]] = 0;
            }

            st->codec->bits_per_coded_sample = get_be16(pb); /* depth */
            st->codec->color_table_id = get_be16(pb); /* colortable id */
            dprintf(c->fc, "depth %d, ctab id %d\n",
                   st->codec->bits_per_coded_sample, st->codec->color_table_id);
            /* figure out the palette situation */
            color_depth = st->codec->bits_per_coded_sample & 0x1F;
            color_greyscale = st->codec->bits_per_coded_sample & 0x20;

            /* if the depth is 2, 4, or 8 bpp, file is palettized */
            if ((color_depth == 2) || (color_depth == 4) ||
                (color_depth == 8)) {
                /* for palette traversal */
                unsigned int color_start, color_count, color_end;
                unsigned char r, g, b;

                if (color_greyscale) {
                    int color_index, color_dec;
                    /* compute the greyscale palette */
                    st->codec->bits_per_coded_sample = color_depth;
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
                } else if (st->codec->color_table_id) {
                    const uint8_t *color_table;
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
                    if ((color_start <= 255) &&
                        (color_end <= 255)) {
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
                }
                st->codec->palctrl = &c->palette_control;
                st->codec->palctrl->palette_changed = 1;
            } else
                st->codec->palctrl = NULL;
        } else if(st->codec->codec_type==CODEC_TYPE_AUDIO) {
            int bits_per_sample, flags;
            uint16_t version = get_be16(pb);

            st->codec->codec_id = id;
            get_be16(pb); /* revision level */
            get_be32(pb); /* vendor */

            st->codec->channels = get_be16(pb);             /* channel count */
            dprintf(c->fc, "audio channels %d\n", st->codec->channels);
            st->codec->bits_per_coded_sample = get_be16(pb);      /* sample size */

            sc->audio_cid = get_be16(pb);
            get_be16(pb); /* packet size = 0 */

            st->codec->sample_rate = ((get_be32(pb) >> 16));

            //Read QT version 1 fields. In version 0 these do not exist.
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
                    st->codec->bits_per_coded_sample = get_be32(pb); /* bits per channel if sound is uncompressed */
                    flags = get_be32(pb); /* lcpm format specific flag */
                    sc->bytes_per_frame = get_be32(pb); /* bytes per audio packet if constant */
                    sc->samples_per_frame = get_be32(pb); /* lpcm frames per audio packet if constant */
                    if (format == MKTAG('l','p','c','m'))
                        st->codec->codec_id = mov_get_lpcm_codec_id(st->codec->bits_per_coded_sample, flags);
                }
            }

            switch (st->codec->codec_id) {
            case CODEC_ID_PCM_S8:
            case CODEC_ID_PCM_U8:
                if (st->codec->bits_per_coded_sample == 16)
                    st->codec->codec_id = CODEC_ID_PCM_S16BE;
                break;
            case CODEC_ID_PCM_S16LE:
            case CODEC_ID_PCM_S16BE:
                if (st->codec->bits_per_coded_sample == 8)
                    st->codec->codec_id = CODEC_ID_PCM_S8;
                else if (st->codec->bits_per_coded_sample == 24)
                    st->codec->codec_id =
                        st->codec->codec_id == CODEC_ID_PCM_S16BE ?
                        CODEC_ID_PCM_S24BE : CODEC_ID_PCM_S24LE;
                break;
            /* set values for old format before stsd version 1 appeared */
            case CODEC_ID_MACE3:
                sc->samples_per_frame = 6;
                sc->bytes_per_frame = 2*st->codec->channels;
                break;
            case CODEC_ID_MACE6:
                sc->samples_per_frame = 6;
                sc->bytes_per_frame = 1*st->codec->channels;
                break;
            case CODEC_ID_ADPCM_IMA_QT:
                sc->samples_per_frame = 64;
                sc->bytes_per_frame = 34*st->codec->channels;
                break;
            case CODEC_ID_GSM:
                sc->samples_per_frame = 160;
                sc->bytes_per_frame = 33;
                break;
            default:
                break;
            }

            bits_per_sample = av_get_bits_per_sample(st->codec->codec_id);
            if (bits_per_sample) {
                st->codec->bits_per_coded_sample = bits_per_sample;
                sc->sample_size = (bits_per_sample >> 3) * st->codec->channels;
            }
        } else if(st->codec->codec_type==CODEC_TYPE_SUBTITLE){
            // ttxt stsd contains display flags, justification, background
            // color, fonts, and default styles, so fake an atom to read it
            MOVAtom fake_atom = { .size = size - (url_ftell(pb) - start_pos) };
            mov_read_glbl(c, pb, fake_atom);
            st->codec->codec_id= id;
            st->codec->width = sc->width;
            st->codec->height = sc->height;
        } else {
            /* other codec type, just skip (rtp, mp4s, tmcd ...) */
            url_fskip(pb, size - (url_ftell(pb) - start_pos));
        }
        /* this will read extra atoms at the end (wave, alac, damr, avcC, SMI ...) */
        a.size = size - (url_ftell(pb) - start_pos);
        if (a.size > 8) {
            if (mov_read_default(c, pb, a) < 0)
                return -1;
        } else if (a.size > 0)
            url_fskip(pb, a.size);
    }

    if(st->codec->codec_type==CODEC_TYPE_AUDIO && st->codec->sample_rate==0 && sc->time_scale>1)
        st->codec->sample_rate= sc->time_scale;

    /* special codec parameters handling */
    switch (st->codec->codec_id) {
#if CONFIG_DV_DEMUXER
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
    case CODEC_ID_QCELP:
        st->codec->frame_size= 160;
        st->codec->channels= 1; /* really needed */
        break;
    case CODEC_ID_AMR_NB:
    case CODEC_ID_AMR_WB:
        st->codec->frame_size= sc->samples_per_frame;
        st->codec->channels= 1; /* really needed */
        /* force sample rate for amr, stsd in 3gp does not store sample rate */
        if (st->codec->codec_id == CODEC_ID_AMR_NB)
            st->codec->sample_rate = 8000;
        else if (st->codec->codec_id == CODEC_ID_AMR_WB)
            st->codec->sample_rate = 16000;
        break;
    case CODEC_ID_MP2:
    case CODEC_ID_MP3:
        st->codec->codec_type = CODEC_TYPE_AUDIO; /* force type after stsd for m1a hdlr */
        st->need_parsing = AVSTREAM_PARSE_FULL;
        break;
    case CODEC_ID_GSM:
    case CODEC_ID_ADPCM_MS:
    case CODEC_ID_ADPCM_IMA_WAV:
        st->codec->block_align = sc->bytes_per_frame;
        break;
    case CODEC_ID_ALAC:
        if (st->codec->extradata_size == 36) {
            st->codec->frame_size = AV_RB32(st->codec->extradata+12);
            st->codec->channels   = AV_RB8 (st->codec->extradata+21);
        }
        break;
    default:
        break;
    }

    return 0;
}

static int mov_read_stsc(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    MOVStreamContext *sc = st->priv_data;
    unsigned int i, entries;

    get_byte(pb); /* version */
    get_be24(pb); /* flags */

    entries = get_be32(pb);

    if(entries >= UINT_MAX / sizeof(*sc->sample_to_chunk))
        return -1;

    dprintf(c->fc, "track[%i].stsc.entries = %i\n", c->fc->nb_streams-1, entries);

    sc->sample_to_chunk_sz = entries;
    sc->sample_to_chunk = av_malloc(entries * sizeof(*sc->sample_to_chunk));
    if (!sc->sample_to_chunk)
        return -1;
    for(i=0; i<entries; i++) {
        sc->sample_to_chunk[i].first = get_be32(pb);
        sc->sample_to_chunk[i].count = get_be32(pb);
        sc->sample_to_chunk[i].id = get_be32(pb);
    }
    return 0;
}

static int mov_read_stss(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    MOVStreamContext *sc = st->priv_data;
    unsigned int i, entries;

    get_byte(pb); /* version */
    get_be24(pb); /* flags */

    entries = get_be32(pb);

    if(entries >= UINT_MAX / sizeof(int))
        return -1;

    sc->keyframe_count = entries;

    dprintf(c->fc, "keyframe_count = %d\n", sc->keyframe_count);

    sc->keyframes = av_malloc(entries * sizeof(int));
    if (!sc->keyframes)
        return -1;
    for(i=0; i<entries; i++) {
        sc->keyframes[i] = get_be32(pb);
        //dprintf(c->fc, "keyframes[]=%d\n", sc->keyframes[i]);
    }
    return 0;
}

static int mov_read_stsz(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    MOVStreamContext *sc = st->priv_data;
    unsigned int i, entries, sample_size;

    get_byte(pb); /* version */
    get_be24(pb); /* flags */

    sample_size = get_be32(pb);
    if (!sc->sample_size) /* do not overwrite value computed in stsd */
        sc->sample_size = sample_size;
    entries = get_be32(pb);
    if(entries >= UINT_MAX / sizeof(int))
        return -1;

    sc->sample_count = entries;
    if (sample_size)
        return 0;

    dprintf(c->fc, "sample_size = %d sample_count = %d\n", sc->sample_size, sc->sample_count);

    sc->sample_sizes = av_malloc(entries * sizeof(int));
    if (!sc->sample_sizes)
        return -1;
    for(i=0; i<entries; i++)
        sc->sample_sizes[i] = get_be32(pb);
    return 0;
}

static int mov_read_stts(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    MOVStreamContext *sc = st->priv_data;
    unsigned int i, entries;
    int64_t duration=0;
    int64_t total_sample_count=0;

    get_byte(pb); /* version */
    get_be24(pb); /* flags */
    entries = get_be32(pb);
    if(entries >= UINT_MAX / sizeof(*sc->stts_data))
        return -1;

    sc->stts_count = entries;
    sc->stts_data = av_malloc(entries * sizeof(*sc->stts_data));
    if (!sc->stts_data)
        return -1;
    dprintf(c->fc, "track[%i].stts.entries = %i\n", c->fc->nb_streams-1, entries);

    sc->time_rate=0;

    for(i=0; i<entries; i++) {
        int sample_duration;
        int sample_count;

        sample_count=get_be32(pb);
        sample_duration = get_be32(pb);
        sc->stts_data[i].count= sample_count;
        sc->stts_data[i].duration= sample_duration;

        sc->time_rate= av_gcd(sc->time_rate, sample_duration);

        dprintf(c->fc, "sample_count=%d, sample_duration=%d\n",sample_count,sample_duration);

        duration+=(int64_t)sample_duration*sample_count;
        total_sample_count+=sample_count;
    }

    st->nb_frames= total_sample_count;
    if(duration)
        st->duration= duration;
    return 0;
}

static int mov_read_ctts(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    MOVStreamContext *sc = st->priv_data;
    unsigned int i, entries;

    get_byte(pb); /* version */
    get_be24(pb); /* flags */
    entries = get_be32(pb);
    if(entries >= UINT_MAX / sizeof(*sc->ctts_data))
        return -1;

    sc->ctts_count = entries;
    sc->ctts_data = av_malloc(entries * sizeof(*sc->ctts_data));
    if (!sc->ctts_data)
        return -1;
    dprintf(c->fc, "track[%i].ctts.entries = %i\n", c->fc->nb_streams-1, entries);

    for(i=0; i<entries; i++) {
        int count    =get_be32(pb);
        int duration =get_be32(pb);

        if (duration < 0) {
            sc->wrong_dts = 1;
            st->codec->has_b_frames = 1;
        }
        sc->ctts_data[i].count   = count;
        sc->ctts_data[i].duration= duration;

        sc->time_rate= av_gcd(sc->time_rate, FFABS(duration));
    }
    return 0;
}

static void mov_build_index(MOVContext *mov, AVStream *st)
{
    MOVStreamContext *sc = st->priv_data;
    int64_t current_offset;
    int64_t current_dts = 0;
    unsigned int stts_index = 0;
    unsigned int stsc_index = 0;
    unsigned int stss_index = 0;
    unsigned int i, j;

    /* only use old uncompressed audio chunk demuxing when stts specifies it */
    if (!(st->codec->codec_type == CODEC_TYPE_AUDIO &&
          sc->stts_count == 1 && sc->stts_data[0].duration == 1)) {
        unsigned int current_sample = 0;
        unsigned int stts_sample = 0;
        unsigned int keyframe, sample_size;
        unsigned int distance = 0;
        int key_off = sc->keyframes && sc->keyframes[0] == 1;

        st->nb_frames = sc->sample_count;
        for (i = 0; i < sc->chunk_count; i++) {
            current_offset = sc->chunk_offsets[i];
            if (stsc_index + 1 < sc->sample_to_chunk_sz &&
                i + 1 == sc->sample_to_chunk[stsc_index + 1].first)
                stsc_index++;
            for (j = 0; j < sc->sample_to_chunk[stsc_index].count; j++) {
                if (current_sample >= sc->sample_count) {
                    av_log(mov->fc, AV_LOG_ERROR, "wrong sample count\n");
                    goto out;
                }
                keyframe = !sc->keyframe_count || current_sample+key_off == sc->keyframes[stss_index];
                if (keyframe) {
                    distance = 0;
                    if (stss_index + 1 < sc->keyframe_count)
                        stss_index++;
                }
                sample_size = sc->sample_size > 0 ? sc->sample_size : sc->sample_sizes[current_sample];
                if(sc->pseudo_stream_id == -1 ||
                   sc->sample_to_chunk[stsc_index].id - 1 == sc->pseudo_stream_id) {
                    av_add_index_entry(st, current_offset, current_dts, sample_size, distance,
                                    keyframe ? AVINDEX_KEYFRAME : 0);
                    dprintf(mov->fc, "AVIndex stream %d, sample %d, offset %"PRIx64", dts %"PRId64", "
                            "size %d, distance %d, keyframe %d\n", st->index, current_sample,
                            current_offset, current_dts, sample_size, distance, keyframe);
                }
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
        unsigned int frames = 1;
        for (i = 0; i < sc->chunk_count; i++) {
            current_offset = sc->chunk_offsets[i];
            if (stsc_index + 1 < sc->sample_to_chunk_sz &&
                i + 1 == sc->sample_to_chunk[stsc_index + 1].first)
                stsc_index++;
            chunk_samples = sc->sample_to_chunk[stsc_index].count;
            /* get chunk size, beware of alaw/ulaw/mace */
            if (sc->samples_per_frame > 0 &&
                (chunk_samples * sc->bytes_per_frame % sc->samples_per_frame == 0)) {
                if (sc->samples_per_frame < 160)
                    chunk_size = chunk_samples * sc->bytes_per_frame / sc->samples_per_frame;
                else {
                    chunk_size = sc->bytes_per_frame;
                    frames = chunk_samples / sc->samples_per_frame;
                    chunk_samples = sc->samples_per_frame;
                }
            } else
                chunk_size = chunk_samples * sc->sample_size;
            for (j = 0; j < frames; j++) {
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
                        if (stts_index + 1 < sc->stts_count)
                            stts_index++;
                    }
                }
                current_offset += sc->bytes_per_frame;
                dprintf(mov->fc, "AVIndex stream %d, chunk %d, offset %"PRIx64", dts %"PRId64", "
                        "size %d, duration %d\n", st->index, i, current_offset, current_dts,
                        chunk_size, chunk_duration);
                assert(chunk_duration % sc->time_rate == 0);
                current_dts += chunk_duration / sc->time_rate;
            }
        }
    }
 out:
    /* adjust sample count to avindex entries */
    sc->sample_count = st->nb_index_entries;
}

static int mov_read_trak(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    int ret;

    st = av_new_stream(c->fc, c->fc->nb_streams);
    if (!st) return AVERROR(ENOMEM);
    sc = av_mallocz(sizeof(MOVStreamContext));
    if (!sc) return AVERROR(ENOMEM);

    st->priv_data = sc;
    st->codec->codec_type = CODEC_TYPE_DATA;
    st->start_time = 0; /* XXX: check */
    sc->ffindex = st->index;

    if ((ret = mov_read_default(c, pb, atom)) < 0)
        return ret;

    /* sanity checks */
    if(sc->chunk_count && (!sc->stts_count || !sc->sample_to_chunk_sz ||
                           (!sc->sample_size && !sc->sample_count))){
        av_log(c->fc, AV_LOG_ERROR, "stream %d, missing mandatory atoms, broken header\n",
               st->index);
        sc->sample_count = 0; //ignore track
        return 0;
    }
    if(!sc->time_rate)
        sc->time_rate=1;
    if(!sc->time_scale)
        sc->time_scale= c->time_scale;
    av_set_pts_info(st, 64, sc->time_rate, sc->time_scale);

    if (st->codec->codec_type == CODEC_TYPE_AUDIO &&
        !st->codec->frame_size && sc->stts_count == 1)
        st->codec->frame_size = av_rescale(sc->time_rate, st->codec->sample_rate, sc->time_scale);

    if(st->duration != AV_NOPTS_VALUE){
        assert(st->duration % sc->time_rate == 0);
        st->duration /= sc->time_rate;
    }

    mov_build_index(c, st);

    if (sc->dref_id-1 < sc->drefs_count && sc->drefs[sc->dref_id-1].path) {
        if (url_fopen(&sc->pb, sc->drefs[sc->dref_id-1].path, URL_RDONLY) < 0)
            av_log(c->fc, AV_LOG_ERROR, "stream %d, error opening file %s: %s\n",
                   st->index, sc->drefs[sc->dref_id-1].path, strerror(errno));
    } else
        sc->pb = c->fc->pb;

    switch (st->codec->codec_id) {
#if CONFIG_H261_DECODER
    case CODEC_ID_H261:
#endif
#if CONFIG_H263_DECODER
    case CODEC_ID_H263:
#endif
#if CONFIG_MPEG4_DECODER
    case CODEC_ID_MPEG4:
#endif
        st->codec->width= 0; /* let decoder init width/height */
        st->codec->height= 0;
        break;
    }

    /* Do not need those anymore. */
    av_freep(&sc->chunk_offsets);
    av_freep(&sc->sample_to_chunk);
    av_freep(&sc->sample_sizes);
    av_freep(&sc->keyframes);
    av_freep(&sc->stts_data);

    return 0;
}

static int mov_read_ilst(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    int ret;
    c->itunes_metadata = 1;
    ret = mov_read_default(c, pb, atom);
    c->itunes_metadata = 0;
    return ret;
}

static int mov_read_meta(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    url_fskip(pb, 4); // version + flags
    atom.size -= 4;
    return mov_read_default(c, pb, atom);
}

static int mov_read_trkn(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    get_be32(pb); // type
    get_be32(pb); // unknown
    c->fc->track = get_be32(pb);
    dprintf(c->fc, "%.4s %d\n", (char*)&atom.type, c->fc->track);
    return 0;
}

static int mov_read_udta_string(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    char *str = NULL;
    int size;
    uint16_t str_size;

    if (c->itunes_metadata) {
        int data_size = get_be32(pb);
        int tag = get_le32(pb);
        if (tag == MKTAG('d','a','t','a')) {
            get_be32(pb); // type
            get_be32(pb); // unknown
            str_size = data_size - 16;
            atom.size -= 16;
        } else return 0;
    } else {
        str_size = get_be16(pb); // string length
        get_be16(pb); // language
        atom.size -= 4;
    }
    switch (atom.type) {
    case MKTAG(0xa9,'n','a','m'):
        str = c->fc->title; size = sizeof(c->fc->title); break;
    case MKTAG(0xa9,'A','R','T'):
    case MKTAG(0xa9,'w','r','t'):
        str = c->fc->author; size = sizeof(c->fc->author); break;
    case MKTAG(0xa9,'c','p','y'):
        str = c->fc->copyright; size = sizeof(c->fc->copyright); break;
    case MKTAG(0xa9,'c','m','t'):
    case MKTAG(0xa9,'i','n','f'):
        str = c->fc->comment; size = sizeof(c->fc->comment); break;
    case MKTAG(0xa9,'a','l','b'):
        str = c->fc->album; size = sizeof(c->fc->album); break;
    }
    if (!str)
        return 0;
    if (atom.size < 0)
        return -1;

    get_buffer(pb, str, FFMIN3(size, str_size, atom.size));
    dprintf(c->fc, "%.4s %s %d %lld\n", (char*)&atom.type, str, str_size, atom.size);
    return 0;
}

static int mov_read_tkhd(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    int i;
    int width;
    int height;
    int64_t disp_transform[2];
    int display_matrix[3][2];
    AVStream *st = c->fc->streams[c->fc->nb_streams-1];
    MOVStreamContext *sc = st->priv_data;
    int version = get_byte(pb);

    get_be24(pb); /* flags */
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
    /* highlevel (considering edits) duration in movie timebase */
    (version == 1) ? get_be64(pb) : get_be32(pb);
    get_be32(pb); /* reserved */
    get_be32(pb); /* reserved */

    get_be16(pb); /* layer */
    get_be16(pb); /* alternate group */
    get_be16(pb); /* volume */
    get_be16(pb); /* reserved */

    //read in the display matrix (outlined in ISO 14496-12, Section 6.2.2)
    // they're kept in fixed point format through all calculations
    // ignore u,v,z b/c we don't need the scale factor to calc aspect ratio
    for (i = 0; i < 3; i++) {
        display_matrix[i][0] = get_be32(pb);   // 16.16 fixed point
        display_matrix[i][1] = get_be32(pb);   // 16.16 fixed point
        get_be32(pb);           // 2.30 fixed point (not used)
    }

    width = get_be32(pb);       // 16.16 fixed point track width
    height = get_be32(pb);      // 16.16 fixed point track height
    sc->width = width >> 16;
    sc->height = height >> 16;

    //transform the display width/height according to the matrix
    // skip this if the display matrix is the default identity matrix
    // to keep the same scale, use [width height 1<<16]
    if (width && height &&
        (display_matrix[0][0] != 65536 || display_matrix[0][1]           ||
        display_matrix[1][0]           || display_matrix[1][1] != 65536  ||
        display_matrix[2][0]           || display_matrix[2][1])) {
        for (i = 0; i < 2; i++)
            disp_transform[i] =
                (int64_t)  width  * display_matrix[0][i] +
                (int64_t)  height * display_matrix[1][i] +
                ((int64_t) display_matrix[2][i] << 16);

        //sample aspect ratio is new width/height divided by old width/height
        st->sample_aspect_ratio = av_d2q(
            ((double) disp_transform[0] * height) /
            ((double) disp_transform[1] * width), INT_MAX);
    }
    return 0;
}

static int mov_read_tfhd(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    MOVFragment *frag = &c->fragment;
    MOVTrackExt *trex = NULL;
    int flags, track_id, i;

    get_byte(pb); /* version */
    flags = get_be24(pb);

    track_id = get_be32(pb);
    if (!track_id || track_id > c->fc->nb_streams)
        return -1;
    frag->track_id = track_id;
    for (i = 0; i < c->trex_count; i++)
        if (c->trex_data[i].track_id == frag->track_id) {
            trex = &c->trex_data[i];
            break;
        }
    if (!trex) {
        av_log(c->fc, AV_LOG_ERROR, "could not find corresponding trex\n");
        return -1;
    }

    if (flags & 0x01) frag->base_data_offset = get_be64(pb);
    else              frag->base_data_offset = frag->moof_offset;
    if (flags & 0x02) frag->stsd_id          = get_be32(pb);
    else              frag->stsd_id          = trex->stsd_id;

    frag->duration = flags & 0x08 ? get_be32(pb) : trex->duration;
    frag->size     = flags & 0x10 ? get_be32(pb) : trex->size;
    frag->flags    = flags & 0x20 ? get_be32(pb) : trex->flags;
    dprintf(c->fc, "frag flags 0x%x\n", frag->flags);
    return 0;
}

static int mov_read_trex(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    MOVTrackExt *trex;

    if ((uint64_t)c->trex_count+1 >= UINT_MAX / sizeof(*c->trex_data))
        return -1;
    c->trex_data = av_realloc(c->trex_data, (c->trex_count+1)*sizeof(*c->trex_data));
    if (!c->trex_data)
        return AVERROR(ENOMEM);
    trex = &c->trex_data[c->trex_count++];
    get_byte(pb); /* version */
    get_be24(pb); /* flags */
    trex->track_id = get_be32(pb);
    trex->stsd_id  = get_be32(pb);
    trex->duration = get_be32(pb);
    trex->size     = get_be32(pb);
    trex->flags    = get_be32(pb);
    return 0;
}

static int mov_read_trun(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    MOVFragment *frag = &c->fragment;
    AVStream *st;
    MOVStreamContext *sc;
    uint64_t offset;
    int64_t dts;
    int data_offset = 0;
    unsigned entries, first_sample_flags = frag->flags;
    int flags, distance, i;

    if (!frag->track_id || frag->track_id > c->fc->nb_streams)
        return -1;
    st = c->fc->streams[frag->track_id-1];
    sc = st->priv_data;
    if (sc->pseudo_stream_id+1 != frag->stsd_id)
        return 0;
    get_byte(pb); /* version */
    flags = get_be24(pb);
    entries = get_be32(pb);
    dprintf(c->fc, "flags 0x%x entries %d\n", flags, entries);
    if (flags & 0x001) data_offset        = get_be32(pb);
    if (flags & 0x004) first_sample_flags = get_be32(pb);
    if (flags & 0x800) {
        if ((uint64_t)entries+sc->ctts_count >= UINT_MAX/sizeof(*sc->ctts_data))
            return -1;
        sc->ctts_data = av_realloc(sc->ctts_data,
                                   (entries+sc->ctts_count)*sizeof(*sc->ctts_data));
        if (!sc->ctts_data)
            return AVERROR(ENOMEM);
    }
    dts = st->duration;
    offset = frag->base_data_offset + data_offset;
    distance = 0;
    dprintf(c->fc, "first sample flags 0x%x\n", first_sample_flags);
    for (i = 0; i < entries; i++) {
        unsigned sample_size = frag->size;
        int sample_flags = i ? frag->flags : first_sample_flags;
        unsigned sample_duration = frag->duration;
        int keyframe;

        if (flags & 0x100) sample_duration = get_be32(pb);
        if (flags & 0x200) sample_size     = get_be32(pb);
        if (flags & 0x400) sample_flags    = get_be32(pb);
        if (flags & 0x800) {
            sc->ctts_data[sc->ctts_count].count = 1;
            sc->ctts_data[sc->ctts_count].duration = get_be32(pb);
            sc->ctts_count++;
        }
        if ((keyframe = st->codec->codec_type == CODEC_TYPE_AUDIO ||
             (flags & 0x004 && !i && !sample_flags) || sample_flags & 0x2000000))
            distance = 0;
        av_add_index_entry(st, offset, dts, sample_size, distance,
                           keyframe ? AVINDEX_KEYFRAME : 0);
        dprintf(c->fc, "AVIndex stream %d, sample %d, offset %"PRIx64", dts %"PRId64", "
                "size %d, distance %d, keyframe %d\n", st->index, sc->sample_count+i,
                offset, dts, sample_size, distance, keyframe);
        distance++;
        assert(sample_duration % sc->time_rate == 0);
        dts += sample_duration / sc->time_rate;
        offset += sample_size;
    }
    frag->moof_offset = offset;
    sc->sample_count = st->nb_index_entries;
    st->duration = dts;
    return 0;
}

/* this atom should be null (from specs), but some buggy files put the 'moov' atom inside it... */
/* like the files created with Adobe Premiere 5.0, for samples see */
/* http://graphics.tudelft.nl/~wouter/publications/soundtests/ */
static int mov_read_wide(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
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
    if (atom.type != MKTAG('m','d','a','t')) {
        url_fskip(pb, atom.size);
        return 0;
    }
    err = mov_read_mdat(c, pb, atom);
    return err;
}

static int mov_read_cmov(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
#if CONFIG_ZLIB
    ByteIOContext ctx;
    uint8_t *cmov_data;
    uint8_t *moov_data; /* uncompressed data */
    long cmov_len, moov_len;
    int ret = -1;

    get_be32(pb); /* dcom atom */
    if (get_le32(pb) != MKTAG('d','c','o','m'))
        return -1;
    if (get_le32(pb) != MKTAG('z','l','i','b')) {
        av_log(NULL, AV_LOG_ERROR, "unknown compression for cmov atom !");
        return -1;
    }
    get_be32(pb); /* cmvd atom */
    if (get_le32(pb) != MKTAG('c','m','v','d'))
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
        goto free_and_return;
    if(init_put_byte(&ctx, moov_data, moov_len, 0, NULL, NULL, NULL, NULL) != 0)
        goto free_and_return;
    atom.type = MKTAG('m','o','o','v');
    atom.offset = 0;
    atom.size = moov_len;
#ifdef DEBUG
//    { int fd = open("/tmp/uncompheader.mov", O_WRONLY | O_CREAT); write(fd, moov_data, moov_len); close(fd); }
#endif
    ret = mov_read_default(c, &ctx, atom);
free_and_return:
    av_free(moov_data);
    av_free(cmov_data);
    return ret;
#else
    av_log(c->fc, AV_LOG_ERROR, "this file requires zlib support compiled in\n");
    return -1;
#endif
}

/* edit list atom */
static int mov_read_elst(MOVContext *c, ByteIOContext *pb, MOVAtom atom)
{
    MOVStreamContext *sc = c->fc->streams[c->fc->nb_streams-1]->priv_data;
    int i, edit_count;

    get_byte(pb); /* version */
    get_be24(pb); /* flags */
    edit_count= sc->edit_count = get_be32(pb);     /* entries */

    for(i=0; i<edit_count; i++){
        int time;
        get_be32(pb); /* Track duration */
        time = get_be32(pb); /* Media time */
        get_be32(pb); /* Media rate */
        if (time != 0)
            av_log(c->fc, AV_LOG_WARNING, "edit list not starting at 0, "
                   "a/v desync might occur, patch welcome\n");
    }
    dprintf(c->fc, "track[%i].edit_count = %i\n", c->fc->nb_streams-1, sc->edit_count);
    return 0;
}

static const MOVParseTableEntry mov_default_parse_table[] = {
{ MKTAG('a','v','s','s'), mov_read_extradata },
{ MKTAG('c','o','6','4'), mov_read_stco },
{ MKTAG('c','t','t','s'), mov_read_ctts }, /* composition time to sample */
{ MKTAG('d','i','n','f'), mov_read_default },
{ MKTAG('d','r','e','f'), mov_read_dref },
{ MKTAG('e','d','t','s'), mov_read_default },
{ MKTAG('e','l','s','t'), mov_read_elst },
{ MKTAG('e','n','d','a'), mov_read_enda },
{ MKTAG('f','i','e','l'), mov_read_extradata },
{ MKTAG('f','t','y','p'), mov_read_ftyp },
{ MKTAG('g','l','b','l'), mov_read_glbl },
{ MKTAG('h','d','l','r'), mov_read_hdlr },
{ MKTAG('i','l','s','t'), mov_read_ilst },
{ MKTAG('j','p','2','h'), mov_read_extradata },
{ MKTAG('m','d','a','t'), mov_read_mdat },
{ MKTAG('m','d','h','d'), mov_read_mdhd },
{ MKTAG('m','d','i','a'), mov_read_default },
{ MKTAG('m','e','t','a'), mov_read_meta },
{ MKTAG('m','i','n','f'), mov_read_default },
{ MKTAG('m','o','o','f'), mov_read_moof },
{ MKTAG('m','o','o','v'), mov_read_moov },
{ MKTAG('m','v','e','x'), mov_read_default },
{ MKTAG('m','v','h','d'), mov_read_mvhd },
{ MKTAG('S','M','I',' '), mov_read_smi }, /* Sorenson extension ??? */
{ MKTAG('a','l','a','c'), mov_read_extradata }, /* alac specific atom */
{ MKTAG('a','v','c','C'), mov_read_glbl },
{ MKTAG('p','a','s','p'), mov_read_pasp },
{ MKTAG('s','t','b','l'), mov_read_default },
{ MKTAG('s','t','c','o'), mov_read_stco },
{ MKTAG('s','t','s','c'), mov_read_stsc },
{ MKTAG('s','t','s','d'), mov_read_stsd }, /* sample description */
{ MKTAG('s','t','s','s'), mov_read_stss }, /* sync sample */
{ MKTAG('s','t','s','z'), mov_read_stsz }, /* sample size */
{ MKTAG('s','t','t','s'), mov_read_stts },
{ MKTAG('t','k','h','d'), mov_read_tkhd }, /* track header */
{ MKTAG('t','f','h','d'), mov_read_tfhd }, /* track fragment header */
{ MKTAG('t','r','a','k'), mov_read_trak },
{ MKTAG('t','r','a','f'), mov_read_default },
{ MKTAG('t','r','e','x'), mov_read_trex },
{ MKTAG('t','r','k','n'), mov_read_trkn },
{ MKTAG('t','r','u','n'), mov_read_trun },
{ MKTAG('u','d','t','a'), mov_read_default },
{ MKTAG('w','a','v','e'), mov_read_wave },
{ MKTAG('e','s','d','s'), mov_read_esds },
{ MKTAG('w','i','d','e'), mov_read_wide }, /* place holder */
{ MKTAG('c','m','o','v'), mov_read_cmov },
{ MKTAG(0xa9,'n','a','m'), mov_read_udta_string },
{ MKTAG(0xa9,'w','r','t'), mov_read_udta_string },
{ MKTAG(0xa9,'c','p','y'), mov_read_udta_string },
{ MKTAG(0xa9,'i','n','f'), mov_read_udta_string },
{ MKTAG(0xa9,'i','n','f'), mov_read_udta_string },
{ MKTAG(0xa9,'A','R','T'), mov_read_udta_string },
{ MKTAG(0xa9,'a','l','b'), mov_read_udta_string },
{ MKTAG(0xa9,'c','m','t'), mov_read_udta_string },
{ 0, NULL }
};

static int mov_probe(AVProbeData *p)
{
    unsigned int offset;
    uint32_t tag;
    int score = 0;

    /* check file header */
    offset = 0;
    for(;;) {
        /* ignore invalid offset */
        if ((offset + 8) > (unsigned int)p->buf_size)
            return score;
        tag = AV_RL32(p->buf + offset + 4);
        switch(tag) {
        /* check for obvious tags */
        case MKTAG('j','P',' ',' '): /* jpeg 2000 signature */
        case MKTAG('m','o','o','v'):
        case MKTAG('m','d','a','t'):
        case MKTAG('p','n','o','t'): /* detect movs with preview pics like ew.mov and april.mov */
        case MKTAG('u','d','t','a'): /* Packet Video PVAuthor adds this and a lot of more junk */
        case MKTAG('f','t','y','p'):
            return AVPROBE_SCORE_MAX;
        /* those are more common words, so rate then a bit less */
        case MKTAG('e','d','i','w'): /* xdcam files have reverted first tags */
        case MKTAG('w','i','d','e'):
        case MKTAG('f','r','e','e'):
        case MKTAG('j','u','n','k'):
        case MKTAG('p','i','c','t'):
            return AVPROBE_SCORE_MAX - 5;
        case MKTAG(0x82,0x82,0x7f,0x7d):
        case MKTAG('s','k','i','p'):
        case MKTAG('u','u','i','d'):
        case MKTAG('p','r','f','l'):
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

static int mov_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    MOVContext *mov = s->priv_data;
    ByteIOContext *pb = s->pb;
    int err;
    MOVAtom atom = { 0, 0, 0 };

    mov->fc = s;
    /* .mov and .mp4 aren't streamable anyway (only progressive download if moov is before mdat) */
    if(!url_is_streamed(pb))
        atom.size = url_fsize(pb);
    else
        atom.size = INT64_MAX;

    /* check MOV header */
    if ((err = mov_read_default(mov, pb, atom)) < 0) {
        av_log(s, AV_LOG_ERROR, "error reading header: %d\n", err);
        return err;
    }
    if (!mov->found_moov) {
        av_log(s, AV_LOG_ERROR, "moov atom not found\n");
        return -1;
    }
    dprintf(mov->fc, "on_parse_exit_offset=%lld\n", url_ftell(pb));

    return 0;
}

static int mov_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MOVContext *mov = s->priv_data;
    MOVStreamContext *sc = 0;
    AVIndexEntry *sample = 0;
    int64_t best_dts = INT64_MAX;
    int i;
 retry:
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        MOVStreamContext *msc = st->priv_data;
        if (st->discard != AVDISCARD_ALL && msc->pb && msc->current_sample < msc->sample_count) {
            AVIndexEntry *current_sample = &st->index_entries[msc->current_sample];
            int64_t dts = av_rescale(current_sample->timestamp * (int64_t)msc->time_rate,
                                     AV_TIME_BASE, msc->time_scale);
            dprintf(s, "stream %d, sample %d, dts %"PRId64"\n", i, msc->current_sample, dts);
            if (!sample || (url_is_streamed(s->pb) && current_sample->pos < sample->pos) ||
                (!url_is_streamed(s->pb) &&
                 ((msc->pb != s->pb && dts < best_dts) || (msc->pb == s->pb &&
                 ((FFABS(best_dts - dts) <= AV_TIME_BASE && current_sample->pos < sample->pos) ||
                  (FFABS(best_dts - dts) > AV_TIME_BASE && dts < best_dts)))))) {
                sample = current_sample;
                best_dts = dts;
                sc = msc;
            }
        }
    }
    if (!sample) {
        mov->found_mdat = 0;
        if (!url_is_streamed(s->pb) ||
            mov_read_default(mov, s->pb, (MOVAtom){ 0, 0, INT64_MAX }) < 0 ||
            url_feof(s->pb))
            return -1;
        dprintf(s, "read fragments, offset 0x%llx\n", url_ftell(s->pb));
        goto retry;
    }
    /* must be done just before reading, to avoid infinite loop on sample */
    sc->current_sample++;
    if (url_fseek(sc->pb, sample->pos, SEEK_SET) != sample->pos) {
        av_log(mov->fc, AV_LOG_ERROR, "stream %d, offset 0x%"PRIx64": partial file\n",
               sc->ffindex, sample->pos);
        return -1;
    }
    av_get_packet(sc->pb, pkt, sample->size);
#if CONFIG_DV_DEMUXER
    if (mov->dv_demux && sc->dv_audio_container) {
        dv_produce_packet(mov->dv_demux, pkt, pkt->data, pkt->size);
        av_free(pkt->data);
        pkt->size = 0;
        if (dv_get_packet(mov->dv_demux, pkt) < 0)
            return -1;
    }
#endif
    pkt->stream_index = sc->ffindex;
    pkt->dts = sample->timestamp;
    if (sc->ctts_data) {
        assert(sc->ctts_data[sc->sample_to_ctime_index].duration % sc->time_rate == 0);
        pkt->pts = pkt->dts + sc->ctts_data[sc->sample_to_ctime_index].duration / sc->time_rate;
        /* update ctts context */
        sc->sample_to_ctime_sample++;
        if (sc->sample_to_ctime_index < sc->ctts_count &&
            sc->ctts_data[sc->sample_to_ctime_index].count == sc->sample_to_ctime_sample) {
            sc->sample_to_ctime_index++;
            sc->sample_to_ctime_sample = 0;
        }
        if (sc->wrong_dts)
            pkt->dts = AV_NOPTS_VALUE;
    } else {
        AVStream *st = s->streams[sc->ffindex];
        int64_t next_dts = (sc->current_sample < sc->sample_count) ?
            st->index_entries[sc->current_sample].timestamp : st->duration;
        pkt->duration = next_dts - pkt->dts;
        pkt->pts = pkt->dts;
    }
    pkt->flags |= sample->flags & AVINDEX_KEYFRAME ? PKT_FLAG_KEY : 0;
    pkt->pos = sample->pos;
    dprintf(s, "stream %d, pts %"PRId64", dts %"PRId64", pos 0x%"PRIx64", duration %d\n",
            pkt->stream_index, pkt->pts, pkt->dts, pkt->pos, pkt->duration);
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
    dprintf(st->codec, "stream %d, found sample %d\n", st->index, sc->current_sample);
    /* adjust ctts index */
    if (sc->ctts_data) {
        time_sample = 0;
        for (i = 0; i < sc->ctts_count; i++) {
            int next = time_sample + sc->ctts_data[i].count;
            if (next > sc->current_sample) {
                sc->sample_to_ctime_index = i;
                sc->sample_to_ctime_sample = sc->current_sample - time_sample;
                break;
            }
            time_sample = next;
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
    if (sample_time < 0)
        sample_time = 0;

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
    int i, j;
    MOVContext *mov = s->priv_data;
    for(i=0; i<s->nb_streams; i++) {
        MOVStreamContext *sc = s->streams[i]->priv_data;
        av_freep(&sc->ctts_data);
        for (j=0; j<sc->drefs_count; j++)
            av_freep(&sc->drefs[j].path);
        av_freep(&sc->drefs);
        if (sc->pb && sc->pb != s->pb)
            url_fclose(sc->pb);
    }
    if(mov->dv_demux){
        for(i=0; i<mov->dv_fctx->nb_streams; i++){
            av_freep(&mov->dv_fctx->streams[i]->codec);
            av_freep(&mov->dv_fctx->streams[i]);
        }
        av_freep(&mov->dv_fctx);
        av_freep(&mov->dv_demux);
    }
    av_freep(&mov->trex_data);
    return 0;
}

AVInputFormat mov_demuxer = {
    "mov,mp4,m4a,3gp,3g2,mj2",
    NULL_IF_CONFIG_SMALL("QuickTime/MPEG-4/Motion JPEG 2000 format"),
    sizeof(MOVContext),
    mov_probe,
    mov_read_header,
    mov_read_packet,
    mov_read_close,
    mov_read_seek,
};
