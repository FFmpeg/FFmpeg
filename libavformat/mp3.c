/*
 * MP3 muxer and demuxer
 * Copyright (c) 2003 Fabrice Bellard.
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
#include "mpegaudio.h"
#include "avstring.h"
#include "mpegaudiodecheader.h"

#define ID3v2_HEADER_SIZE 10
#define ID3v1_TAG_SIZE 128

#define ID3v1_GENRE_MAX 125

static const char *id3v1_genre_str[ID3v1_GENRE_MAX + 1] = {
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

/* buf must be ID3v2_HEADER_SIZE byte long */
static int id3v2_match(const uint8_t *buf)
{
    return (buf[0] == 'I' &&
            buf[1] == 'D' &&
            buf[2] == '3' &&
            buf[3] != 0xff &&
            buf[4] != 0xff &&
            (buf[6] & 0x80) == 0 &&
            (buf[7] & 0x80) == 0 &&
            (buf[8] & 0x80) == 0 &&
            (buf[9] & 0x80) == 0);
}

static unsigned int id3v2_get_size(ByteIOContext *s, int len)
{
    int v=0;
    while(len--)
        v= (v<<7) + (get_byte(s)&0x7F);
    return v;
}

static void id3v2_read_ttag(AVFormatContext *s, int taglen, char *dst, int dstlen)
{
    char *q;
    int len;

    if(taglen < 1)
        return;

    taglen--; /* account for encoding type byte */
    dstlen--; /* Leave space for zero terminator */

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
        len = FFMIN(taglen, dstlen);
        get_buffer(s->pb, dst, len);
        dst[len] = 0;
        break;
    }
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
    offset_t next;
    char tmp[16];
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
            id3v2_read_ttag(s, tlen, s->title, sizeof(s->title));
            break;
        case MKBETAG('T', 'P', 'E', '1'):
        case MKBETAG(0,   'T', 'P', '1'):
            id3v2_read_ttag(s, tlen, s->author, sizeof(s->author));
            break;
        case MKBETAG('T', 'A', 'L', 'B'):
        case MKBETAG(0,   'T', 'A', 'L'):
            id3v2_read_ttag(s, tlen, s->album, sizeof(s->album));
            break;
        case MKBETAG('T', 'C', 'O', 'N'):
        case MKBETAG(0,   'T', 'C', 'O'):
            id3v2_read_ttag(s, tlen, s->genre, sizeof(s->genre));
            break;
        case MKBETAG('T', 'C', 'O', 'P'):
        case MKBETAG(0,   'T', 'C', 'R'):
            id3v2_read_ttag(s, tlen, s->copyright, sizeof(s->copyright));
            break;
        case MKBETAG('T', 'R', 'C', 'K'):
        case MKBETAG(0,   'T', 'R', 'K'):
            id3v2_read_ttag(s, tlen, tmp, sizeof(tmp));
            s->track = atoi(tmp);
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

static void id3v1_get_string(char *str, int str_size,
                             const uint8_t *buf, int buf_size)
{
    int i, c;
    char *q;

    q = str;
    for(i = 0; i < buf_size; i++) {
        c = buf[i];
        if (c == '\0')
            break;
        if ((q - str) >= str_size - 1)
            break;
        *q++ = c;
    }
    *q = '\0';
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
    id3v1_get_string(s->title, sizeof(s->title), buf + 3, 30);
    id3v1_get_string(s->author, sizeof(s->author), buf + 33, 30);
    id3v1_get_string(s->album, sizeof(s->album), buf + 63, 30);
    id3v1_get_string(str, sizeof(str), buf + 93, 4);
    s->year = atoi(str);
    id3v1_get_string(s->comment, sizeof(s->comment), buf + 97, 30);
    if (buf[125] == 0 && buf[126] != 0)
        s->track = buf[126];
    genre = buf[127];
    if (genre <= ID3v1_GENRE_MAX)
        av_strlcpy(s->genre, id3v1_genre_str[genre], sizeof(s->genre));
    return 0;
}

static void id3v1_create_tag(AVFormatContext *s, uint8_t *buf)
{
    int v, i;

    memset(buf, 0, ID3v1_TAG_SIZE); /* fail safe */
    buf[0] = 'T';
    buf[1] = 'A';
    buf[2] = 'G';
    strncpy(buf + 3, s->title, 30);
    strncpy(buf + 33, s->author, 30);
    strncpy(buf + 63, s->album, 30);
    v = s->year;
    if (v > 0) {
        for(i = 0;i < 4; i++) {
            buf[96 - i] = '0' + (v % 10);
            v = v / 10;
        }
    }
    strncpy(buf + 97, s->comment, 30);
    if (s->track != 0) {
        buf[125] = 0;
        buf[126] = s->track;
    }
    for(i = 0; i <= ID3v1_GENRE_MAX; i++) {
        if (!strcasecmp(s->genre, id3v1_genre_str[i])) {
            buf[127] = i;
            break;
        }
    }
}

/* mp3 read */

static int mp3_read_probe(AVProbeData *p)
{
    int max_frames, first_frames = 0;
    int fsize, frames, sample_rate;
    uint32_t header;
    uint8_t *buf, *buf2, *end;
    AVCodecContext avctx;

    if(id3v2_match(p->buf))
        return AVPROBE_SCORE_MAX/2+1; // this must be less than mpeg-ps because some retards put id3v2 tags before mpeg-ps files

    max_frames = 0;
    buf = p->buf;
    end = buf + p->buf_size - sizeof(uint32_t);

    for(; buf < end; buf= buf2+1) {
        buf2 = buf;

        for(frames = 0; buf2 < end; frames++) {
            header = AV_RB32(buf2);
            fsize = ff_mpa_decode_header(&avctx, header, &sample_rate);
            if(fsize < 0)
                break;
            buf2 += fsize;
        }
        max_frames = FFMAX(max_frames, frames);
        if(buf == p->buf)
            first_frames= frames;
    }
    if   (first_frames>=3) return AVPROBE_SCORE_MAX/2+1;
    else if(max_frames>500)return AVPROBE_SCORE_MAX/2;
    else if(max_frames>=3) return AVPROBE_SCORE_MAX/4;
    else if(max_frames>=1) return 1;
    else                   return 0;
}

/**
 * Try to find Xing/Info/VBRI tags and compute duration from info therein
 */
static void mp3_parse_vbr_tags(AVFormatContext *s, AVStream *st, offset_t base)
{
    uint32_t v, spf;
    int frames = -1; /* Total number of frames in file */
    const offset_t xing_offtbl[2][2] = {{32, 17}, {17,9}};
    MPADecodeContext c;

    v = get_be32(s->pb);
    if(ff_mpa_check_header(v) < 0)
      return;

    ff_mpegaudio_decode_header(&c, v);
    if(c.layer != 3)
        return;

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
        return;

    spf = c.lsf ? 576 : 1152; /* Samples per frame, layer 3 */
    st->duration = av_rescale_q(frames, (AVRational){spf, c.sample_rate},
                                st->time_base);
}

static int mp3_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    AVStream *st;
    uint8_t buf[ID3v1_TAG_SIZE];
    int len, ret, filesize;
    offset_t off;

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
    if (id3v2_match(buf)) {
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
    mp3_parse_vbr_tags(s, st, off);
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

static int mp3_read_close(AVFormatContext *s)
{
    return 0;
}

#ifdef CONFIG_MUXERS
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
    int totlen = 0;
    char tracktxt[10];
    char yeartxt[10];

    if(s->track)
        snprintf(tracktxt, sizeof(tracktxt), "%d", s->track);
    if(s->year)
        snprintf( yeartxt, sizeof(yeartxt) , "%d", s->year );

    if(s->title[0])     totlen += 11 + strlen(s->title);
    if(s->author[0])    totlen += 11 + strlen(s->author);
    if(s->album[0])     totlen += 11 + strlen(s->album);
    if(s->genre[0])     totlen += 11 + strlen(s->genre);
    if(s->copyright[0]) totlen += 11 + strlen(s->copyright);
    if(s->track)        totlen += 11 + strlen(tracktxt);
    if(s->year)         totlen += 11 + strlen(yeartxt);
    if(!(s->streams[0]->codec->flags & CODEC_FLAG_BITEXACT))
        totlen += strlen(LIBAVFORMAT_IDENT) + 11;

    if(totlen == 0)
        return 0;

    put_be32(s->pb, MKBETAG('I', 'D', '3', 0x04)); /* ID3v2.4 */
    put_byte(s->pb, 0);
    put_byte(s->pb, 0); /* flags */

    id3v2_put_size(s, totlen);

    if(s->title[0])     id3v2_put_ttag(s, s->title,     MKBETAG('T', 'I', 'T', '2'));
    if(s->author[0])    id3v2_put_ttag(s, s->author,    MKBETAG('T', 'P', 'E', '1'));
    if(s->album[0])     id3v2_put_ttag(s, s->album,     MKBETAG('T', 'A', 'L', 'B'));
    if(s->genre[0])     id3v2_put_ttag(s, s->genre,     MKBETAG('T', 'C', 'O', 'N'));
    if(s->copyright[0]) id3v2_put_ttag(s, s->copyright, MKBETAG('T', 'C', 'O', 'P'));
    if(s->track)        id3v2_put_ttag(s, tracktxt,     MKBETAG('T', 'R', 'C', 'K'));
    if(s->year)         id3v2_put_ttag(s, yeartxt,      MKBETAG('T', 'Y', 'E', 'R'));
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
    if (s->title[0] != '\0') {
        id3v1_create_tag(s, buf);
        put_buffer(s->pb, buf, ID3v1_TAG_SIZE);
        put_flush_packet(s->pb);
    }
    return 0;
}
#endif //CONFIG_MUXERS

#ifdef CONFIG_MP3_DEMUXER
AVInputFormat mp3_demuxer = {
    "mp3",
    "MPEG audio",
    0,
    mp3_read_probe,
    mp3_read_header,
    mp3_read_packet,
    mp3_read_close,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "mp2,mp3,m2a", /* XXX: use probe */
};
#endif
#ifdef CONFIG_MP2_MUXER
AVOutputFormat mp2_muxer = {
    "mp2",
    "MPEG audio layer 2",
    "audio/x-mpeg",
#ifdef CONFIG_LIBMP3LAME
    "mp2,m2a",
#else
    "mp2,mp3,m2a",
#endif
    0,
    CODEC_ID_MP2,
    0,
    NULL,
    mp3_write_packet,
    mp3_write_trailer,
};
#endif
#ifdef CONFIG_MP3_MUXER
AVOutputFormat mp3_muxer = {
    "mp3",
    "MPEG audio layer 3",
    "audio/x-mpeg",
    "mp3",
    0,
    CODEC_ID_MP3,
    0,
    mp3_write_header,
    mp3_write_packet,
    mp3_write_trailer,
};
#endif
