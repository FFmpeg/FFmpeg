/*
 * NSV decoder.
 * Copyright (c) 2004 The FFmpeg Project.
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

#define DEBUG
//#define DEBUG_DUMP_INDEX // XXX dumbdriving-271.nsv breaks with it commented!!
//#define DEBUG_SEEK
#define CHECK_SUBSEQUENT_NSVS
//#define DISABLE_AUDIO

/* max bytes to crawl for trying to resync
 * stupid streaming servers don't start at chunk boundaries...
 */
#define NSV_MAX_RESYNC (500*1024)
#define NSV_MAX_RESYNC_TRIES 300

/*
 * First version by Francois Revol - revol@free.fr
 * References:
 * (1) http://www.multimedia.cx/nsv-format.txt
 * seems someone came to the same conclusions as me, and updated it:
 * (2) http://www.stud.ktu.lt/~vitslav/nsv/nsv-format.txt
 *     http://www.stud.ktu.lt/~vitslav/nsv/
 * Sample files:
 * (S1) http://www.nullsoft.com/nsv/samples/
 * http://www.nullsoft.com/nsv/samples/faster.nsv
 * http://streamripper.sourceforge.net/openbb/read.php?TID=492&page=4
 */

/*
 * notes on the header (Francois Revol):
 * 
 * It is followed by strings, then a table, but nothing tells 
 * where the table begins according to (1). After checking faster.nsv,
 * I believe NVSf[16-19] gives the size of the strings data
 * (that is the offset of the data table after the header).
 * After checking all samples from (S1) all confirms this.
 *
 * Then, about NSVf[12-15], faster.nsf has 179700. When veiwing it in VLC,
 * I noticed there was about 1 NVSs chunk/s, so I ran
 * strings faster.nsv | grep NSVs | wc -l
 * which gave me 180. That leads me to think that NSVf[12-15] might be the 
 * file length in milliseconds.
 * Let's try that:
 * for f in *.nsv; do HTIME="$(od -t x4 "$f" | head -1 | sed 's/.* //')"; echo "'$f' $((0x$HTIME))s = $((0x$HTIME/1000/60)):$((0x$HTIME/1000%60))"; done
 * except for nstrailer (which doesn't have an NSVf header), it repports correct time.
 *
 * nsvtrailer.nsv (S1) does not have any NSVf header, only NSVs chunks, 
 * so the header seems to not be mandatory. (for streaming).
 * 
 * index slice duration check (excepts nsvtrailer.nsv):
 * for f in [^n]*.nsv; do DUR="$(ffmpeg -i "$f" 2>/dev/null | grep 'NSVf duration' | cut -d ' ' -f 4)"; IC="$(ffmpeg -i "$f" 2>/dev/null | grep 'INDEX ENTRIES' | cut -d ' ' -f 2)"; echo "duration $DUR, slite time $(($DUR/$IC))"; done
 */

/*
 * TODO:
 * - handle timestamps !!!
 * - use index
 * - mime-type in probe()
 * - seek
 */

#ifdef DEBUG
#define PRINT(_v) printf _v
#else
#define PRINT(_v) 
#endif

#if 0
struct NSVf_header {
    uint32_t chunk_tag; /* 'NSVf' */
    uint32_t chunk_size;
    uint32_t file_size; /* max 4GB ??? noone learns anything it seems :^) */
    uint32_t file_length; //unknown1;  /* what about MSB of file_size ? */
    uint32_t info_strings_size; /* size of the info strings */ //unknown2;
    uint32_t table_entries;
    uint32_t table_entries_used; /* the left ones should be -1 */
};

struct NSVs_header {
    uint32_t chunk_tag; /* 'NSVs' */
    uint32_t v4cc;      /* or 'NONE' */
    uint32_t a4cc;      /* or 'NONE' */
    uint16_t vwidth;    /* assert(vwidth%16==0) */
    uint16_t vheight;   /* assert(vheight%16==0) */
    uint8_t framerate;  /* value = (framerate&0x80)?frtable[frameratex0x7f]:framerate */
    uint16_t unknown;
};

struct nsv_avchunk_header {
    uint8_t vchunk_size_lsb;
    uint16_t vchunk_size_msb; /* value = (vchunk_size_msb << 4) | (vchunk_size_lsb >> 4) */
    uint16_t achunk_size;
};

struct nsv_pcm_header {
    uint8_t bits_per_sample;
    uint8_t channel_count;
    uint16_t sample_rate;
};
#endif

/* variation from avi.h */
/*typedef struct CodecTag {
    int id;
    unsigned int tag;
} CodecTag;*/

/* tags */

#define T_NSVF MKTAG('N', 'S', 'V', 'f') /* file header */
#define T_NSVS MKTAG('N', 'S', 'V', 's') /* chunk header */
#define T_TOC2 MKTAG('T', 'O', 'C', '2') /* extra index marker */
#define T_NONE MKTAG('N', 'O', 'N', 'E') /* null a/v 4CC */
#define T_SUBT MKTAG('S', 'U', 'B', 'T') /* subtitle aux data */
#define T_ASYN MKTAG('A', 'S', 'Y', 'N') /* async a/v aux marker */
#define T_KEYF MKTAG('K', 'E', 'Y', 'F') /* video keyframe aux marker (addition) */

#define TB_NSVF MKBETAG('N', 'S', 'V', 'f')
#define TB_NSVS MKBETAG('N', 'S', 'V', 's')

/* hardcoded stream indices */
#define NSV_ST_VIDEO 0
#define NSV_ST_AUDIO 1
#define NSV_ST_SUBT 2

enum NSVStatus {
    NSV_UNSYNC,
    NSV_FOUND_NSVF,
    NSV_HAS_READ_NSVF,
    NSV_FOUND_NSVS,
    NSV_HAS_READ_NSVS,
    NSV_FOUND_BEEF,
    NSV_GOT_VIDEO,
    NSV_GOT_AUDIO,
};

typedef struct NSVStream {
    int frame_offset; /* current frame (video) or byte (audio) counter
                         (used to compute the pts) */
    int scale;
    int rate;    
    int sample_size; /* audio only data */
    int start;
    
    int new_frame_offset; /* temporary storage (used during seek) */
    int cum_len; /* temporary storage (used during seek) */
} NSVStream;

typedef struct {
    int  base_offset;
    int  NSVf_end;
    uint32_t *nsvf_index_data;
    int index_entries;
    enum NSVStatus state;
    AVPacket ahead[2]; /* [v, a] if .data is !NULL there is something */
    /* cached */
    int64_t duration;
    uint32_t vtag, atag;
    uint16_t vwidth, vheight;
    //DVDemuxContext* dv_demux;
} NSVContext;

static const CodecTag nsv_codec_video_tags[] = {
    { CODEC_ID_VP3, MKTAG('V', 'P', '3', ' ') },
    { CODEC_ID_VP3, MKTAG('V', 'P', '3', '0') },
    { CODEC_ID_VP3, MKTAG('V', 'P', '3', '1') },
/*
    { CODEC_ID_VP4, MKTAG('V', 'P', '4', ' ') },
    { CODEC_ID_VP4, MKTAG('V', 'P', '4', '0') },
    { CODEC_ID_VP5, MKTAG('V', 'P', '5', ' ') },
    { CODEC_ID_VP5, MKTAG('V', 'P', '5', '0') },
    { CODEC_ID_VP6, MKTAG('V', 'P', '6', ' ') },
    { CODEC_ID_VP6, MKTAG('V', 'P', '6', '0') },
    { CODEC_ID_VP6, MKTAG('V', 'P', '6', '1') },
    { CODEC_ID_VP6, MKTAG('V', 'P', '6', '2') },
*/
    { CODEC_ID_XVID, MKTAG('X', 'V', 'I', 'D') }, /* cf sample xvid decoder from nsv_codec_sdk.zip */
    { CODEC_ID_RAWVIDEO, MKTAG('R', 'G', 'B', '3') },
    { 0, 0 },
};

static const CodecTag nsv_codec_audio_tags[] = {
    { CODEC_ID_MP3, MKTAG('M', 'P', '3', ' ') },
    { CODEC_ID_AAC, MKTAG('A', 'A', 'C', ' ') },
    { CODEC_ID_AAC, MKTAG('A', 'A', 'C', 'P') }, /* _CUTTED__MUXED_2 Heads - Out Of The City.nsv */
    { CODEC_ID_PCM_U16LE, MKTAG('P', 'C', 'M', ' ') },
    { 0, 0 },
};

static const uint64_t nsv_framerate_table[] = {
    ((uint64_t)AV_TIME_BASE * 30),
    ((uint64_t)AV_TIME_BASE * 30000 / 1001), /* 29.97 */
    ((uint64_t)AV_TIME_BASE * 25),
    ((uint64_t)AV_TIME_BASE * 24000 / 1001), /* 23.98 */
    ((uint64_t)AV_TIME_BASE * 30), /* ?? */
    ((uint64_t)AV_TIME_BASE * 15000 / 1001), /* 14.98 */
};

//static int nsv_load_index(AVFormatContext *s);
static int nsv_read_chunk(AVFormatContext *s, int fill_header);

#ifdef DEBUG
static void print_tag(const char *str, unsigned int tag, int size)
{
    printf("%s: tag=%c%c%c%c\n",
           str, tag & 0xff,
           (tag >> 8) & 0xff,
           (tag >> 16) & 0xff,
           (tag >> 24) & 0xff);
}
#endif

/* try to find something we recognize, and set the state accordingly */
static int nsv_resync(AVFormatContext *s)
{
    NSVContext *nsv = s->priv_data;
    ByteIOContext *pb = &s->pb;
    uint32_t v = 0;
    int i;
    
    PRINT(("%s(), offset = %Ld, state = %d\n", __FUNCTION__, url_ftell(pb), nsv->state));
    
    //nsv->state = NSV_UNSYNC;
    
    for (i = 0; i < NSV_MAX_RESYNC; i++) {
        if (url_feof(pb)) {
            PRINT(("NSV EOF\n"));
            nsv->state = NSV_UNSYNC;
            return -1;
        }
        v <<= 8;
        v |= get_byte(pb);
/*
        if (i < 8) {
            PRINT(("NSV resync: [%d] = %02x\n", i, v & 0x0FF));
        }
*/
        
        if ((v & 0x0000ffff) == 0xefbe) { /* BEEF */
            PRINT(("NSV resynced on BEEF after %d bytes\n", i+1));
            nsv->state = NSV_FOUND_BEEF;
            return 0;
        }
        /* we read as big endian, thus the MK*BE* */
        if (v == TB_NSVF) { /* NSVf */
            PRINT(("NSV resynced on NSVf after %d bytes\n", i+1));
            nsv->state = NSV_FOUND_NSVF;
            return 0;
        }
        if (v == MKBETAG('N', 'S', 'V', 's')) { /* NSVs */
            PRINT(("NSV resynced on NSVs after %d bytes\n", i+1));
            nsv->state = NSV_FOUND_NSVS;
            return 0;
        }
        
    }
    PRINT(("NSV sync lost\n"));
    return -1;
}

static int nsv_parse_NSVf_header(AVFormatContext *s, AVFormatParameters *ap)
{
    NSVContext *nsv = s->priv_data;
    ByteIOContext *pb = &s->pb;
    uint32_t tag, tag1, handler;
    int codec_type, stream_index, frame_period, bit_rate, scale, rate;
    unsigned int file_size, size, nb_frames;
    int64_t duration;
    int strings_size;
    int table_entries;
    int table_entries_used;
    int i, n;
    AVStream *st;
    NSVStream *ast;

    PRINT(("%s()\n", __FUNCTION__));

    nsv->state = NSV_UNSYNC; /* in case we fail */
    
    size = get_le32(pb);
    if (size < 28)
        return -1;
    nsv->NSVf_end = size;

    //s->file_size = (uint32_t)get_le32(pb);
    file_size = (uint32_t)get_le32(pb);
    PRINT(("NSV NSVf chunk_size %ld\n", size));
    PRINT(("NSV NSVf file_size %Ld\n", file_size));

    duration = get_le32(pb); /* in ms */
    nsv->duration = duration * AV_TIME_BASE / 1000; /* convert */
    PRINT(("NSV NSVf duration %Ld ms\n", duration));
    // XXX: store it in AVStreams

    strings_size = get_le32(pb);
    table_entries = get_le32(pb);
    table_entries_used = get_le32(pb);
    PRINT(("NSV NSVf info-strings size: %d, table entries: %d, bis %d\n", 
            strings_size, table_entries, table_entries_used));
    if (url_feof(pb))
        return -1;
    
    PRINT(("NSV got header; filepos %Ld\n", url_ftell(pb)));

    if (strings_size > 0) {
        char *strings; /* last byte will be '\0' to play safe with str*() */
        char *p, *endp;
        char *token, *value;
        char quote;

        p = strings = av_mallocz(strings_size + 1);
        endp = strings + strings_size;
        get_buffer(pb, strings, strings_size);
        while (p < endp) {
            while (*p == ' ')
                p++; /* strip out spaces */
            if (p >= endp-2)
                break;
            token = p;
            p = strchr(p, '=');
            if (!p || p >= endp-2)
                break;
            *p++ = '\0';
            quote = *p++;
            value = p;
            p = strchr(p, quote);
            if (!p || p >= endp)
                break;
            *p++ = '\0';
            PRINT(("NSV NSVf INFO: %s='%s'\n", token, value));
            if (!strcmp(token, "ASPECT")) {
                /* don't care */
            } else if (!strcmp(token, "CREATOR") || !strcmp(token, "Author")) {
                strncpy(s->author, value, 512-1);
            } else if (!strcmp(token, "Copyright")) {
                strncpy(s->copyright, value, 512-1);
            } else if (!strcmp(token, "TITLE") || !strcmp(token, "Title")) {
                strncpy(s->title, value, 512-1);
            }
        }
        av_free(strings);
    }
    if (url_feof(pb))
        return -1;
    
    PRINT(("NSV got infos; filepos %Ld\n", url_ftell(pb)));

    if (table_entries_used > 0) {
        nsv->index_entries = table_entries_used;
        if((unsigned)table_entries >= UINT_MAX / sizeof(uint32_t))
            return -1;
        nsv->nsvf_index_data = av_malloc(table_entries * sizeof(uint32_t));
        get_buffer(pb, nsv->nsvf_index_data, table_entries * sizeof(uint32_t));
    }

    PRINT(("NSV got index; filepos %Ld\n", url_ftell(pb)));
    
#ifdef DEBUG_DUMP_INDEX
#define V(v) ((v<0x20 || v > 127)?'.':v)
    /* dump index */
    PRINT(("NSV %d INDEX ENTRIES:\n", table_entries));
    PRINT(("NSV [dataoffset][fileoffset]\n", table_entries));
    for (i = 0; i < table_entries; i++) {
        unsigned char b[8];
        url_fseek(pb, size + nsv->nsvf_index_data[i], SEEK_SET);
        get_buffer(pb, b, 8);
        PRINT(("NSV [0x%08lx][0x%08lx]: %02x %02x %02x %02x %02x %02x %02x %02x"
           "%c%c%c%c%c%c%c%c\n",
           nsv->nsvf_index_data[i], size + nsv->nsvf_index_data[i],
           b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], 
           V(b[0]), V(b[1]), V(b[2]), V(b[3]), V(b[4]), V(b[5]), V(b[6]), V(b[7]) ));
    }
    //url_fseek(pb, size, SEEK_SET); /* go back to end of header */
#undef V
#endif
    
    url_fseek(pb, nsv->base_offset + size, SEEK_SET); /* required for dumbdriving-271.nsv (2 extra bytes) */
    
    if (url_feof(pb))
        return -1;
    nsv->state = NSV_HAS_READ_NSVF;
    return 0;
}

static int nsv_parse_NSVs_header(AVFormatContext *s, AVFormatParameters *ap)
{
    NSVContext *nsv = s->priv_data;
    ByteIOContext *pb = &s->pb;
    uint32_t vtag, atag;
    uint16_t vwidth, vheight;
    uint32_t framerate;
    uint16_t unknown;
    AVStream *st;
    NSVStream *nst;
    PRINT(("%s()\n", __FUNCTION__));

    vtag = get_le32(pb);
    atag = get_le32(pb);
    vwidth = get_le16(pb);
    vheight = get_le16(pb);
    framerate = (uint8_t)get_byte(pb);
    /* XXX how big must the table be ? */
    /* seems there is more to that... */
    PRINT(("NSV NSVs framerate code %2x\n", framerate));
    framerate = (framerate & 0x80)?(nsv_framerate_table[framerate & 0x7F]):(framerate*AV_TIME_BASE);
    unknown = get_le16(pb);
#ifdef DEBUG
    print_tag("NSV NSVs vtag", vtag, 0);
    print_tag("NSV NSVs atag", atag, 0);
    PRINT(("NSV NSVs vsize %dx%d\n", vwidth, vheight));
    PRINT(("NSV NSVs framerate %2x\n", framerate));
#endif
    
    /* XXX change to ap != NULL ? */
    if (s->nb_streams == 0) { /* streams not yet published, let's do that */
        nsv->vtag = vtag;
        nsv->atag = atag;
        nsv->vwidth = vwidth;
        nsv->vheight = vwidth;
        if (vtag != T_NONE) {
            st = av_new_stream(s, NSV_ST_VIDEO);
            if (!st)
                goto fail;

            nst = av_mallocz(sizeof(NSVStream));
            if (!nst)
                goto fail;
            st->priv_data = nst;
            st->codec.codec_type = CODEC_TYPE_VIDEO;
            st->codec.codec_tag = vtag;
            st->codec.codec_id = codec_get_id(nsv_codec_video_tags, vtag);
            st->codec.width = vwidth;
            st->codec.height = vheight;
            st->codec.bits_per_sample = 24; /* depth XXX */

            st->codec.frame_rate = framerate;
            st->codec.frame_rate_base = AV_TIME_BASE;
            av_set_pts_info(st, 64, AV_TIME_BASE, framerate);
            st->start_time = 0;
            st->duration = nsv->duration;
        }
        if (atag != T_NONE) {
#ifndef DISABLE_AUDIO
            st = av_new_stream(s, NSV_ST_AUDIO);
            if (!st)
                goto fail;

            nst = av_mallocz(sizeof(NSVStream));
            if (!nst)
                goto fail;
            st->priv_data = nst;
            st->codec.codec_type = CODEC_TYPE_AUDIO;
            st->codec.codec_tag = atag;
            st->codec.codec_id = codec_get_id(nsv_codec_audio_tags, atag);
            st->start_time = 0;
            st->duration = nsv->duration;
            
            st->need_parsing = 1; /* for PCM we will read a chunk later and put correct info */
            /* XXX:FIXME */
            //st->codec.channels = 2; //XXX:channels;
            //st->codec.sample_rate = 1000;
            //av_set_pts_info(st, 64, 1, st->codec.sample_rate);

#endif
        }
#ifdef CHECK_SUBSEQUENT_NSVS
    } else {
        if (nsv->vtag != vtag || nsv->atag != atag || nsv->vwidth != vwidth || nsv->vheight != vwidth) {
            PRINT(("NSV NSVs header values differ from the first one!!!\n"));
            //return -1;
        }
#endif /* CHECK_SUBSEQUENT_NSVS */
    }

    nsv->state = NSV_HAS_READ_NSVS;
    return 0;
fail:
    /* XXX */
    nsv->state = NSV_UNSYNC;
    return -1;
}

static int nsv_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    NSVContext *nsv = s->priv_data;
    ByteIOContext *pb = &s->pb;
    uint32_t tag, tag1, handler;
    int codec_type, stream_index, frame_period, bit_rate, scale, rate;
    unsigned int size, nb_frames;
    int table_entries;
    int i, n, err;
    AVStream *st;
    NSVStream *ast;

    PRINT(("%s()\n", __FUNCTION__));
    PRINT(("filename '%s'\n", s->filename));

    nsv->state = NSV_UNSYNC;
    nsv->ahead[0].data = nsv->ahead[1].data = NULL;
    
    for (i = 0; i < NSV_MAX_RESYNC_TRIES; i++) {
        if (nsv_resync(s) < 0)
            return -1;
        if (nsv->state == NSV_FOUND_NSVF)
            err = nsv_parse_NSVf_header(s, ap);
            /* we need the first NSVs also... */
        if (nsv->state == NSV_FOUND_NSVS) {
            err = nsv_parse_NSVs_header(s, ap);
            break; /* we just want the first one */
        }
    }
    if (s->nb_streams < 1) /* no luck so far */
        return -1;
    /* now read the first chunk, so we can attempt to decode more info */
    err = nsv_read_chunk(s, 1);
    
    PRINT(("parsed header\n"));
    return 0;
}

static int nsv_read_chunk(AVFormatContext *s, int fill_header)
{
    NSVContext *nsv = s->priv_data;
    ByteIOContext *pb = &s->pb;
    AVStream *st[2] = {NULL, NULL};
    NSVStream *nst;
    AVPacket *pkt;
    uint32_t v = 0;
    int i, err = 0;
    uint8_t auxcount; /* number of aux metadata, also 4 bits of vsize */
    uint32_t vsize;
    uint16_t asize;
    uint16_t auxsize;
    uint32_t auxtag;
    
    PRINT(("%s(%d)\n", __FUNCTION__, fill_header));
    
    if (nsv->ahead[0].data || nsv->ahead[1].data)
        return 0; //-1; /* hey! eat what you've in your plate first! */

null_chunk_retry:
    if (url_feof(pb))
        return -1;
    
    for (i = 0; i < NSV_MAX_RESYNC_TRIES && nsv->state < NSV_FOUND_NSVS && !err; i++)
        err = nsv_resync(s);
    if (err < 0)
        return err;
    if (nsv->state == NSV_FOUND_NSVS)
        err = nsv_parse_NSVs_header(s, NULL);
    if (err < 0)
        return err;
    if (nsv->state != NSV_HAS_READ_NSVS && nsv->state != NSV_FOUND_BEEF)
        return -1;
    
    auxcount = get_byte(pb);
    vsize = get_le16(pb);
    asize = get_le16(pb);
    vsize = (vsize << 4) | (auxcount >> 4);
    auxcount &= 0x0f;
    PRINT(("NSV CHUNK %d aux, %ld bytes video, %d bytes audio\n", auxcount, vsize, asize));
    /* skip aux stuff */
    for (i = 0; i < auxcount; i++) {
        auxsize = get_le16(pb);
        auxtag = get_le32(pb);
        PRINT(("NSV aux data: '%c%c%c%c', %d bytes\n", 
              (auxtag & 0x0ff), 
              ((auxtag >> 8) & 0x0ff), 
              ((auxtag >> 16) & 0x0ff),
              ((auxtag >> 24) & 0x0ff),
              auxsize));
        url_fskip(pb, auxsize);
        vsize -= auxsize + sizeof(uint16_t) + sizeof(uint32_t); /* that's becoming braindead */
    }
    
    if (url_feof(pb))
        return -1;
    if (!vsize && !asize) {
        nsv->state = NSV_UNSYNC;
        goto null_chunk_retry;
    }
    
    /* map back streams to v,a */
    if (s->streams[0])
        st[s->streams[0]->id] = s->streams[0];
    if (s->streams[1])
        st[s->streams[1]->id] = s->streams[1];
    
    if (vsize/* && st[NSV_ST_VIDEO]*/) {
        nst = st[NSV_ST_VIDEO]->priv_data;
        pkt = &nsv->ahead[NSV_ST_VIDEO];
        av_new_packet(pkt, vsize);
        get_buffer(pb, pkt->data, vsize);
        pkt->stream_index = st[NSV_ST_VIDEO]->index;//NSV_ST_VIDEO;
        pkt->dts = nst->frame_offset++;
        pkt->flags |= PKT_FLAG_KEY; /* stupid format has no way to tell XXX: try the index */
/*
        for (i = 0; i < MIN(8, vsize); i++)
            PRINT(("NSV video: [%d] = %02x\n", i, pkt->data[i]));
*/
    }
    if (asize/*st[NSV_ST_AUDIO]*/) {
        nst = st[NSV_ST_AUDIO]->priv_data;
        pkt = &nsv->ahead[NSV_ST_AUDIO];
        /* read raw audio specific header on the first audio chunk... */
        /* on ALL audio chunks ?? seems so! */
        if (asize && st[NSV_ST_AUDIO]->codec.codec_tag == MKTAG('P', 'C', 'M', ' ')/* && fill_header*/) {
            uint8_t bps;
            uint8_t channels;
            uint16_t samplerate;
            bps = get_byte(pb);
            channels = get_byte(pb);
            samplerate = get_le16(pb);
            asize-=4;
            PRINT(("NSV RAWAUDIO: bps %d, nchan %d, srate %ld\n", bps, channels, samplerate));
            if (fill_header) {
                st[NSV_ST_AUDIO]->need_parsing = 0; /* we know everything */
                if (bps != 16) {
                    PRINT(("NSV AUDIO bit/sample != 16 (%d)!!!\n", bps));
                }
                bps /= channels; // ???
                if (bps == 8)
                    st[NSV_ST_AUDIO]->codec.codec_id = CODEC_ID_PCM_U8;
                samplerate /= 4;/* UGH ??? XXX */
                channels = 1;
                st[NSV_ST_AUDIO]->codec.channels = channels;
                st[NSV_ST_AUDIO]->codec.sample_rate = samplerate;
                av_set_pts_info(st[NSV_ST_AUDIO], 64, 1, 
                                st[NSV_ST_AUDIO]->codec.sample_rate);
                PRINT(("NSV RAWAUDIO: bps %d, nchan %d, srate %ld\n", bps, channels, samplerate));
            }
        }
        av_new_packet(pkt, asize);
        if (asize)
            get_buffer(pb, pkt->data, asize);
        pkt->stream_index = st[NSV_ST_AUDIO]->index;//NSV_ST_AUDIO;
        //pkt->dts = nst->frame_offset;
        //if (nst->sample_size)
        //    pkt->dts /= nst->sample_size;
        nst->frame_offset += asize; // XXX: that's valid only for PCM !?
    }
    
    //pkt->flags |= PKT_FLAG_KEY;
    nsv->state = NSV_UNSYNC;
    return 0;
}


static int nsv_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    NSVContext *nsv = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int i, err = 0;

    PRINT(("%s()\n", __FUNCTION__));
    
    /* in case we don't already have something to eat ... */
    if (nsv->ahead[0].data == NULL && nsv->ahead[1].data == NULL)
        err = nsv_read_chunk(s, 0);
    if (err < 0)
        return err;
    
    /* now pick one of the plates */
    for (i = 0; i < 2; i++) {
        if (nsv->ahead[i].data) {
        	PRINT(("%s: using cached packet[%d]\n", __FUNCTION__, i));
            /* avoid the cost of new_packet + memcpy(->data) */
            memcpy(pkt, &nsv->ahead[i], sizeof(AVPacket));
            nsv->ahead[i].data = NULL; /* we ate that one */
            return pkt->size;
        }
    }
    
    /* this restaurant is not approvisionned :^] */
    return -1;
}

static int nsv_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    NSVContext *avi = s->priv_data;
    AVStream *st;
    NSVStream *ast;
    int frame_number, i;
    int64_t pos;

    return -1;
}

static int nsv_read_close(AVFormatContext *s)
{
    int i;
    NSVContext *nsv = s->priv_data;

    if (nsv->index_entries)
        av_free(nsv->nsvf_index_data);

#if 0

    for(i=0;i<s->nb_streams;i++) {
        AVStream *st = s->streams[i];
        NSVStream *ast = st->priv_data;
        if(ast){
            av_free(ast->index_entries);
            av_free(ast);
        }
        av_free(st->codec.extradata);
        av_free(st->codec.palctrl);
    }

#endif
    return 0;
}

static int nsv_probe(AVProbeData *p)
{
    int i;
//    PRINT(("nsv_probe(), buf_size %d\n", p->buf_size));
    /* check file header */
    if (p->buf_size <= 32)
        return 0;
    if (p->buf[0] == 'N' && p->buf[1] == 'S' &&
        p->buf[2] == 'V' && p->buf[3] == 'f')
        return AVPROBE_SCORE_MAX;
    /* streamed files might not have any header */
    if (p->buf[0] == 'N' && p->buf[1] == 'S' &&
        p->buf[2] == 'V' && p->buf[3] == 's')
        return AVPROBE_SCORE_MAX;
    /* XXX: do streamed files always start at chunk boundary ?? */
    /* or do we need to search NSVs in the byte stream ? */
    /* seems the servers don't bother starting clean chunks... */
    /* sometimes even the first header is at 9KB or something :^) */
    for (i = 1; i < p->buf_size - 3; i++) {
        if (p->buf[i+0] == 'N' && p->buf[i+1] == 'S' &&
            p->buf[i+2] == 'V' && p->buf[i+3] == 's')
            return AVPROBE_SCORE_MAX-20;
    }
    /* so we'll have more luck on extension... */
    if (match_ext(p->filename, "nsv"))
        return AVPROBE_SCORE_MAX-20;
    /* FIXME: add mime-type check */
    return 0;
}

static AVInputFormat nsv_iformat = {
    "nsv",
    "NullSoft Video format",
    sizeof(NSVContext),
    nsv_probe,
    nsv_read_header,
    nsv_read_packet,
    nsv_read_close,
    nsv_read_seek,
};

int nsvdec_init(void)
{
    av_register_input_format(&nsv_iformat);
    return 0;
}
