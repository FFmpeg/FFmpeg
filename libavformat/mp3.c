/*
 * MP3 muxer and demuxer
 * Copyright (c) 2003 Fabrice Bellard
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

#include <strings.h>
#include "libavutil/avstring.h"
#include "libavcodec/mpegaudio.h"
#include "libavcodec/mpegaudiodecheader.h"
#include "avformat.h"
#include "id3v2.h"

#define ID3v1_TAG_SIZE 128

#define ID3v1_GENRE_MAX 125

static const char * const id3v1_genre_str[ID3v1_GENRE_MAX + 1] = {
    [0] = "Blues",
    [1] = "Classic Rock",
    [2] = "Country",
    [3] = "Dance",
    [4] = "Disco",
    [5] = "Funk",
    [6] = "Grunge",
    [7] = "Hip-Hop",
    [8] = "Jazz",
    [9] = "Metal",
    [10] = "New Age",
    [11] = "Oldies",
    [12] = "Other",
    [13] = "Pop",
    [14] = "R&B",
    [15] = "Rap",
    [16] = "Reggae",
    [17] = "Rock",
    [18] = "Techno",
    [19] = "Industrial",
    [20] = "Alternative",
    [21] = "Ska",
    [22] = "Death Metal",
    [23] = "Pranks",
    [24] = "Soundtrack",
    [25] = "Euro-Techno",
    [26] = "Ambient",
    [27] = "Trip-Hop",
    [28] = "Vocal",
    [29] = "Jazz+Funk",
    [30] = "Fusion",
    [31] = "Trance",
    [32] = "Classical",
    [33] = "Instrumental",
    [34] = "Acid",
    [35] = "House",
    [36] = "Game",
    [37] = "Sound Clip",
    [38] = "Gospel",
    [39] = "Noise",
    [40] = "AlternRock",
    [41] = "Bass",
    [42] = "Soul",
    [43] = "Punk",
    [44] = "Space",
    [45] = "Meditative",
    [46] = "Instrumental Pop",
    [47] = "Instrumental Rock",
    [48] = "Ethnic",
    [49] = "Gothic",
    [50] = "Darkwave",
    [51] = "Techno-Industrial",
    [52] = "Electronic",
    [53] = "Pop-Folk",
    [54] = "Eurodance",
    [55] = "Dream",
    [56] = "Southern Rock",
    [57] = "Comedy",
    [58] = "Cult",
    [59] = "Gangsta",
    [60] = "Top 40",
    [61] = "Christian Rap",
    [62] = "Pop/Funk",
    [63] = "Jungle",
    [64] = "Native American",
    [65] = "Cabaret",
    [66] = "New Wave",
    [67] = "Psychadelic",
    [68] = "Rave",
    [69] = "Showtunes",
    [70] = "Trailer",
    [71] = "Lo-Fi",
    [72] = "Tribal",
    [73] = "Acid Punk",
    [74] = "Acid Jazz",
    [75] = "Polka",
    [76] = "Retro",
    [77] = "Musical",
    [78] = "Rock & Roll",
    [79] = "Hard Rock",
    [80] = "Folk",
    [81] = "Folk-Rock",
    [82] = "National Folk",
    [83] = "Swing",
    [84] = "Fast Fusion",
    [85] = "Bebob",
    [86] = "Latin",
    [87] = "Revival",
    [88] = "Celtic",
    [89] = "Bluegrass",
    [90] = "Avantgarde",
    [91] = "Gothic Rock",
    [92] = "Progressive Rock",
    [93] = "Psychedelic Rock",
    [94] = "Symphonic Rock",
    [95] = "Slow Rock",
    [96] = "Big Band",
    [97] = "Chorus",
    [98] = "Easy Listening",
    [99] = "Acoustic",
    [100] = "Humour",
    [101] = "Speech",
    [102] = "Chanson",
    [103] = "Opera",
    [104] = "Chamber Music",
    [105] = "Sonata",
    [106] = "Symphony",
    [107] = "Booty Bass",
    [108] = "Primus",
    [109] = "Porn Groove",
    [110] = "Satire",
    [111] = "Slow Jam",
    [112] = "Club",
    [113] = "Tango",
    [114] = "Samba",
    [115] = "Folklore",
    [116] = "Ballad",
    [117] = "Power Ballad",
    [118] = "Rhythmic Soul",
    [119] = "Freestyle",
    [120] = "Duet",
    [121] = "Punk Rock",
    [122] = "Drum Solo",
    [123] = "A capella",
    [124] = "Euro-House",
    [125] = "Dance Hall",
};

static unsigned int id3v2_get_size(ByteIOContext *s, int len)
{
    int v=0;
    while(len--)
        v= (v<<7) + (get_byte(s)&0x7F);
    return v;
}

static void id3v2_read_ttag(AVFormatContext *s, int taglen, const char *key)
{
    char *q, dst[512];
    int len, dstlen = sizeof(dst) - 1;
    unsigned genre;

    dst[0]= 0;
    if(taglen < 1)
        return;

    taglen--; /* account for encoding type byte */

    switch(get_byte(s->pb)) { /* encoding type */

    case 0:  /* ISO-8859-1 (0 - 255 maps directly into unicode) */
        q = dst;
        while(taglen--) {
            uint8_t tmp;
            PUT_UTF8(get_byte(s->pb), tmp, if (q - dst < dstlen - 1) *q++ = tmp;)
        }
        *q = '\0';
        break;

    case 3:  /* UTF-8 */
        len = FFMIN(taglen, dstlen-1);
        get_buffer(s->pb, dst, len);
        dst[len] = 0;
        break;
    }

    if (!strcmp(key, "genre")
        && (sscanf(dst, "(%d)", &genre) == 1 || sscanf(dst, "%d", &genre) == 1)
        && genre <= ID3v1_GENRE_MAX)
        av_strlcpy(dst, id3v1_genre_str[genre], sizeof(dst));

    if (*dst)
        av_metadata_set(&s->metadata, key, dst);
}

/**
 * ID3v2 parser
 *
 * Handles ID3v2.2, 2.3 and 2.4.
 *
 */

static void id3v2_parse(AVFormatContext *s, int len, uint8_t version, uint8_t flags)
{
    int isv34, tlen;
    uint32_t tag;
    int64_t next;
    int taghdrlen;
    const char *reason;

    switch(version) {
    case 2:
        if(flags & 0x40) {
            reason = "compression";
            goto error;
        }
        isv34 = 0;
        taghdrlen = 6;
        break;

    case 3:
    case 4:
        isv34 = 1;
        taghdrlen = 10;
        break;

    default:
        reason = "version";
        goto error;
    }

    if(flags & 0x80) {
        reason = "unsynchronization";
        goto error;
    }

    if(isv34 && flags & 0x40) /* Extended header present, just skip over it */
        url_fskip(s->pb, id3v2_get_size(s->pb, 4));

    while(len >= taghdrlen) {
        if(isv34) {
            tag  = get_be32(s->pb);
            tlen = id3v2_get_size(s->pb, 4);
            get_be16(s->pb); /* flags */
        } else {
            tag  = get_be24(s->pb);
            tlen = id3v2_get_size(s->pb, 3);
        }
        len -= taghdrlen + tlen;

        if(len < 0)
            break;

        next = url_ftell(s->pb) + tlen;

        switch(tag) {
        case MKBETAG('T', 'I', 'T', '2'):
        case MKBETAG(0,   'T', 'T', '2'):
            id3v2_read_ttag(s, tlen, "title");
            break;
        case MKBETAG('T', 'P', 'E', '1'):
        case MKBETAG(0,   'T', 'P', '1'):
            id3v2_read_ttag(s, tlen, "author");
            break;
        case MKBETAG('T', 'A', 'L', 'B'):
        case MKBETAG(0,   'T', 'A', 'L'):
            id3v2_read_ttag(s, tlen, "album");
            break;
        case MKBETAG('T', 'C', 'O', 'N'):
        case MKBETAG(0,   'T', 'C', 'O'):
            id3v2_read_ttag(s, tlen, "genre");
            break;
        case MKBETAG('T', 'C', 'O', 'P'):
        case MKBETAG(0,   'T', 'C', 'R'):
            id3v2_read_ttag(s, tlen, "copyright");
            break;
        case MKBETAG('T', 'R', 'C', 'K'):
        case MKBETAG(0,   'T', 'R', 'K'):
            id3v2_read_ttag(s, tlen, "track");
            break;
        case 0:
            /* padding, skip to end */
            url_fskip(s->pb, len);
            len = 0;
            continue;
        }
        /* Skip to end of tag */
        url_fseek(s->pb, next, SEEK_SET);
    }

    if(version == 4 && flags & 0x10) /* Footer preset, always 10 bytes, skip over it */
        url_fskip(s->pb, 10);
    return;

  error:
    av_log(s, AV_LOG_INFO, "ID3v2.%d tag skipped, cannot handle %s\n", version, reason);
    url_fskip(s->pb, len);
}

static void id3v1_get_string(AVFormatContext *s, const char *key,
                             const uint8_t *buf, int buf_size)
{
    int i, c;
    char *q, str[512];

    q = str;
    for(i = 0; i < buf_size; i++) {
        c = buf[i];
        if (c == '\0')
            break;
        if ((q - str) >= sizeof(str) - 1)
            break;
        *q++ = c;
    }
    *q = '\0';

    if (*str)
        av_metadata_set(&s->metadata, key, str);
}

/* 'buf' must be ID3v1_TAG_SIZE byte long */
static int id3v1_parse_tag(AVFormatContext *s, const uint8_t *buf)
{
    char str[5];
    int genre;

    if (!(buf[0] == 'T' &&
          buf[1] == 'A' &&
          buf[2] == 'G'))
        return -1;
    id3v1_get_string(s, "title",   buf +  3, 30);
    id3v1_get_string(s, "author",  buf + 33, 30);
    id3v1_get_string(s, "album",   buf + 63, 30);
    id3v1_get_string(s, "year",    buf + 93,  4);
    id3v1_get_string(s, "comment", buf + 97, 30);
    if (buf[125] == 0 && buf[126] != 0) {
        snprintf(str, sizeof(str), "%d", buf[126]);
        av_metadata_set(&s->metadata, "track", str);
    }
    genre = buf[127];
    if (genre <= ID3v1_GENRE_MAX)
        av_metadata_set(&s->metadata, "genre", id3v1_genre_str[genre]);
    return 0;
}

/* mp3 read */

static int mp3_read_probe(AVProbeData *p)
{
    int max_frames, first_frames = 0;
    int fsize, frames, sample_rate;
    uint32_t header;
    uint8_t *buf, *buf0, *buf2, *end;
    AVCodecContext avctx;

    buf0 = p->buf;
    if(ff_id3v2_match(buf0)) {
        buf0 += ff_id3v2_tag_len(buf0);
    }

    max_frames = 0;
    buf = buf0;
    end = p->buf + p->buf_size - sizeof(uint32_t);

    for(; buf < end; buf= buf2+1) {
        buf2 = buf;

        for(frames = 0; buf2 < end; frames++) {
            header = AV_RB32(buf2);
            fsize = ff_mpa_decode_header(&avctx, header, &sample_rate, &sample_rate, &sample_rate, &sample_rate);
            if(fsize < 0)
                break;
            buf2 += fsize;
        }
        max_frames = FFMAX(max_frames, frames);
        if(buf == buf0)
            first_frames= frames;
    }
    if   (first_frames>=3) return AVPROBE_SCORE_MAX/2+1;
    else if(max_frames>500)return AVPROBE_SCORE_MAX/2;
    else if(max_frames>=3) return AVPROBE_SCORE_MAX/4;
    else if(buf0!=p->buf)  return AVPROBE_SCORE_MAX/4-1;
    else if(max_frames>=1) return 1;
    else                   return 0;
}

/**
 * Try to find Xing/Info/VBRI tags and compute duration from info therein
 */
static int mp3_parse_vbr_tags(AVFormatContext *s, AVStream *st, int64_t base)
{
    uint32_t v, spf;
    int frames = -1; /* Total number of frames in file */
    const int64_t xing_offtbl[2][2] = {{32, 17}, {17,9}};
    MPADecodeHeader c;
    int vbrtag_size = 0;

    v = get_be32(s->pb);
    if(ff_mpa_check_header(v) < 0)
      return -1;

    if (ff_mpegaudio_decode_header(&c, v) == 0)
        vbrtag_size = c.frame_size;
    if(c.layer != 3)
        return -1;

    /* Check for Xing / Info tag */
    url_fseek(s->pb, xing_offtbl[c.lsf == 1][c.nb_channels == 1], SEEK_CUR);
    v = get_be32(s->pb);
    if(v == MKBETAG('X', 'i', 'n', 'g') || v == MKBETAG('I', 'n', 'f', 'o')) {
        v = get_be32(s->pb);
        if(v & 0x1)
            frames = get_be32(s->pb);
    }

    /* Check for VBRI tag (always 32 bytes after end of mpegaudio header) */
    url_fseek(s->pb, base + 4 + 32, SEEK_SET);
    v = get_be32(s->pb);
    if(v == MKBETAG('V', 'B', 'R', 'I')) {
        /* Check tag version */
        if(get_be16(s->pb) == 1) {
            /* skip delay, quality and total bytes */
            url_fseek(s->pb, 8, SEEK_CUR);
            frames = get_be32(s->pb);
        }
    }

    if(frames < 0)
        return -1;

    /* Skip the vbr tag frame */
    url_fseek(s->pb, base + vbrtag_size, SEEK_SET);

    spf = c.lsf ? 576 : 1152; /* Samples per frame, layer 3 */
    st->duration = av_rescale_q(frames, (AVRational){spf, c.sample_rate},
                                st->time_base);
    return 0;
}

static int mp3_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    AVStream *st;
    uint8_t buf[ID3v1_TAG_SIZE];
    int len, ret, filesize;
    int64_t off;

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

    st->codec->codec_type = CODEC_TYPE_AUDIO;
    st->codec->codec_id = CODEC_ID_MP3;
    st->need_parsing = AVSTREAM_PARSE_FULL;
    st->start_time = 0;

    /* try to get the TAG */
    if (!url_is_streamed(s->pb)) {
        /* XXX: change that */
        filesize = url_fsize(s->pb);
        if (filesize > 128) {
            url_fseek(s->pb, filesize - 128, SEEK_SET);
            ret = get_buffer(s->pb, buf, ID3v1_TAG_SIZE);
            if (ret == ID3v1_TAG_SIZE) {
                id3v1_parse_tag(s, buf);
            }
            url_fseek(s->pb, 0, SEEK_SET);
        }
    }

    /* if ID3v2 header found, skip it */
    ret = get_buffer(s->pb, buf, ID3v2_HEADER_SIZE);
    if (ret != ID3v2_HEADER_SIZE)
        return -1;
    if (ff_id3v2_match(buf)) {
        /* parse ID3v2 header */
        len = ((buf[6] & 0x7f) << 21) |
            ((buf[7] & 0x7f) << 14) |
            ((buf[8] & 0x7f) << 7) |
            (buf[9] & 0x7f);
        id3v2_parse(s, len, buf[3], buf[5]);
    } else {
        url_fseek(s->pb, 0, SEEK_SET);
    }

    off = url_ftell(s->pb);
    if (mp3_parse_vbr_tags(s, st, off) < 0)
        url_fseek(s->pb, off, SEEK_SET);

    /* the parameters will be extracted from the compressed bitstream */
    return 0;
}

#define MP3_PACKET_SIZE 1024

static int mp3_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, size;
    //    AVStream *st = s->streams[0];

    size= MP3_PACKET_SIZE;

    ret= av_get_packet(s->pb, pkt, size);

    pkt->stream_index = 0;
    if (ret <= 0) {
        return AVERROR(EIO);
    }
    /* note: we need to modify the packet size here to handle the last
       packet */
    pkt->size = ret;
    return ret;
}

#if CONFIG_MP2_MUXER || CONFIG_MP3_MUXER
static int id3v1_set_string(AVFormatContext *s, const char *key,
                            uint8_t *buf, int buf_size)
{
    AVMetadataTag *tag;
    if ((tag = av_metadata_get(s->metadata, key, NULL, 0)))
        strncpy(buf, tag->value, buf_size);
    return !!tag;
}

static int id3v1_create_tag(AVFormatContext *s, uint8_t *buf)
{
    AVMetadataTag *tag;
    int i, count = 0;

    memset(buf, 0, ID3v1_TAG_SIZE); /* fail safe */
    buf[0] = 'T';
    buf[1] = 'A';
    buf[2] = 'G';
    count += id3v1_set_string(s, "title",   buf +  3, 30);
    count += id3v1_set_string(s, "author",  buf + 33, 30);
    count += id3v1_set_string(s, "album",   buf + 63, 30);
    count += id3v1_set_string(s, "year",    buf + 93,  4);
    count += id3v1_set_string(s, "comment", buf + 97, 30);
    if ((tag = av_metadata_get(s->metadata, "track", NULL, 0))) {
        buf[125] = 0;
        buf[126] = atoi(tag->value);
        count++;
    }
    if ((tag = av_metadata_get(s->metadata, "genre", NULL, 0))) {
        for(i = 0; i <= ID3v1_GENRE_MAX; i++) {
            if (!strcasecmp(tag->value, id3v1_genre_str[i])) {
                buf[127] = i;
                count++;
                break;
            }
        }
    }
    return count;
}

/* simple formats */

static void id3v2_put_size(AVFormatContext *s, int size)
{
    put_byte(s->pb, size >> 21 & 0x7f);
    put_byte(s->pb, size >> 14 & 0x7f);
    put_byte(s->pb, size >> 7  & 0x7f);
    put_byte(s->pb, size       & 0x7f);
}

static void id3v2_put_ttag(AVFormatContext *s, const char *string, uint32_t tag)
{
    int len = strlen(string);
    put_be32(s->pb, tag);
    id3v2_put_size(s, len + 1);
    put_be16(s->pb, 0);
    put_byte(s->pb, 3); /* UTF-8 */
    put_buffer(s->pb, string, len);
}


/**
 * Write an ID3v2.4 header at beginning of stream
 */

static int mp3_write_header(struct AVFormatContext *s)
{
    AVMetadataTag *title, *author, *album, *genre, *copyright, *track, *year;
    int totlen = 0;

    title     = av_metadata_get(s->metadata, "title",     NULL, 0);
    author    = av_metadata_get(s->metadata, "author",    NULL, 0);
    album     = av_metadata_get(s->metadata, "album",     NULL, 0);
    genre     = av_metadata_get(s->metadata, "genre",     NULL, 0);
    copyright = av_metadata_get(s->metadata, "copyright", NULL, 0);
    track     = av_metadata_get(s->metadata, "track",     NULL, 0);
    year      = av_metadata_get(s->metadata, "year",      NULL, 0);

    if(title)     totlen += 11 + strlen(title->value);
    if(author)    totlen += 11 + strlen(author->value);
    if(album)     totlen += 11 + strlen(album->value);
    if(genre)     totlen += 11 + strlen(genre->value);
    if(copyright) totlen += 11 + strlen(copyright->value);
    if(track)     totlen += 11 + strlen(track->value);
    if(year)      totlen += 11 + strlen(year->value);
    if(!(s->streams[0]->codec->flags & CODEC_FLAG_BITEXACT))
        totlen += strlen(LIBAVFORMAT_IDENT) + 11;

    if(totlen == 0)
        return 0;

    put_be32(s->pb, MKBETAG('I', 'D', '3', 0x04)); /* ID3v2.4 */
    put_byte(s->pb, 0);
    put_byte(s->pb, 0); /* flags */

    id3v2_put_size(s, totlen);

    if(title)     id3v2_put_ttag(s, title->value,     MKBETAG('T', 'I', 'T', '2'));
    if(author)    id3v2_put_ttag(s, author->value,    MKBETAG('T', 'P', 'E', '1'));
    if(album)     id3v2_put_ttag(s, album->value,     MKBETAG('T', 'A', 'L', 'B'));
    if(genre)     id3v2_put_ttag(s, genre->value,     MKBETAG('T', 'C', 'O', 'N'));
    if(copyright) id3v2_put_ttag(s, copyright->value, MKBETAG('T', 'C', 'O', 'P'));
    if(track)     id3v2_put_ttag(s, track->value,     MKBETAG('T', 'R', 'C', 'K'));
    if(year)      id3v2_put_ttag(s, year->value,      MKBETAG('T', 'Y', 'E', 'R'));
    if(!(s->streams[0]->codec->flags & CODEC_FLAG_BITEXACT))
        id3v2_put_ttag(s, LIBAVFORMAT_IDENT,            MKBETAG('T', 'E', 'N', 'C'));
    return 0;
}

static int mp3_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    put_buffer(s->pb, pkt->data, pkt->size);
    put_flush_packet(s->pb);
    return 0;
}

static int mp3_write_trailer(struct AVFormatContext *s)
{
    uint8_t buf[ID3v1_TAG_SIZE];

    /* write the id3v1 tag */
    if (id3v1_create_tag(s, buf) > 0) {
        put_buffer(s->pb, buf, ID3v1_TAG_SIZE);
        put_flush_packet(s->pb);
    }
    return 0;
}
#endif /* CONFIG_MP2_MUXER || CONFIG_MP3_MUXER */

#if CONFIG_MP3_DEMUXER
AVInputFormat mp3_demuxer = {
    "mp3",
    NULL_IF_CONFIG_SMALL("MPEG audio layer 2/3"),
    0,
    mp3_read_probe,
    mp3_read_header,
    mp3_read_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "mp2,mp3,m2a", /* XXX: use probe */
};
#endif
#if CONFIG_MP2_MUXER
AVOutputFormat mp2_muxer = {
    "mp2",
    NULL_IF_CONFIG_SMALL("MPEG audio layer 2"),
    "audio/x-mpeg",
    "mp2,m2a",
    0,
    CODEC_ID_MP2,
    CODEC_ID_NONE,
    NULL,
    mp3_write_packet,
    mp3_write_trailer,
};
#endif
#if CONFIG_MP3_MUXER
AVOutputFormat mp3_muxer = {
    "mp3",
    NULL_IF_CONFIG_SMALL("MPEG audio layer 3"),
    "audio/x-mpeg",
    "mp3",
    0,
    CODEC_ID_MP3,
    CODEC_ID_NONE,
    mp3_write_header,
    mp3_write_packet,
    mp3_write_trailer,
};
#endif
