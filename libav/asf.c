/*
 * ASF compatible encoder and decoder.
 * Copyright (c) 2000, 2001 Fabrice Bellard.
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
#include "tick.h"
#include "mpegaudio.h"

#define PACKET_SIZE 3200
#define PACKET_HEADER_SIZE 12
#define FRAME_HEADER_SIZE 17

typedef struct {
    int num;
    int seq;
    Ticker pts_ticker;
    /* use for reading */
    AVPacket pkt;
    int frag_offset;
    INT64 duration;
} ASFStream;

typedef struct {
    int seqno;
    int packet_size;

    ASFStream streams[2];
    /* non streamed additonnal info */
    int data_offset;
    INT64 nb_packets;
    INT64 duration; /* in 100ns units */
    /* packet filling */
    int packet_size_left;
    int packet_timestamp_start;
    int packet_timestamp_end;
    int packet_nb_frames;
    UINT8 packet_buf[PACKET_SIZE];
    ByteIOContext pb;
    /* only for reading */
    int packet_padsize;
} ASFContext;

typedef struct {
    UINT32 v1;
    UINT16 v2;
    UINT16 v3;
    UINT8 v4[8];
} GUID;

static const GUID asf_header = {
    0x75B22630, 0x668E, 0x11CF, { 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C },
};

static const GUID file_header = {
    0x8CABDCA1, 0xA947, 0x11CF, { 0x8E, 0xE4, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 },
};

static const GUID stream_header = {
    0xB7DC0791, 0xA9B7, 0x11CF, { 0x8E, 0xE6, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 },
};

static const GUID audio_stream = {
    0xF8699E40, 0x5B4D, 0x11CF, { 0xA8, 0xFD, 0x00, 0x80, 0x5F, 0x5C, 0x44, 0x2B },
};

static const GUID audio_conceal_none = {
    // 0x49f1a440, 0x4ece, 0x11d0, { 0xa3, 0xac, 0x00, 0xa0, 0xc9, 0x03, 0x48, 0xf6 },
    // New value lifted from avifile
    0x20fb5700, 0x5b55, 0x11cf, { 0xa8, 0xfd, 0x00, 0x80, 0x5f, 0x5c, 0x44, 0x2b },
};

static const GUID video_stream = {
    0xBC19EFC0, 0x5B4D, 0x11CF, { 0xA8, 0xFD, 0x00, 0x80, 0x5F, 0x5C, 0x44, 0x2B },
};

static const GUID video_conceal_none = {
    0x20FB5700, 0x5B55, 0x11CF, { 0xA8, 0xFD, 0x00, 0x80, 0x5F, 0x5C, 0x44, 0x2B },
};


static const GUID comment_header = {
    0x75b22633, 0x668e, 0x11cf, { 0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c },
};

static const GUID codec_comment_header = {
    0x86D15240, 0x311D, 0x11D0, { 0xA3, 0xA4, 0x00, 0xA0, 0xC9, 0x03, 0x48, 0xF6 },
};
static const GUID codec_comment1_header = {
    0x86d15241, 0x311d, 0x11d0, { 0xa3, 0xa4, 0x00, 0xa0, 0xc9, 0x03, 0x48, 0xf6 },
};

static const GUID data_header = {
    0x75b22636, 0x668e, 0x11cf, { 0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c },
};

static const GUID index_guid = {
    0x33000890, 0xe5b1, 0x11cf, { 0x89, 0xf4, 0x00, 0xa0, 0xc9, 0x03, 0x49, 0xcb },
};

static const GUID head1_guid = {
    0x5fbf03b5, 0xa92e, 0x11cf, { 0x8e, 0xe3, 0x00, 0xc0, 0x0c, 0x20, 0x53, 0x65 },
};

static const GUID head2_guid = {
    0xabd3d211, 0xa9ba, 0x11cf, { 0x8e, 0xe6, 0x00, 0xc0, 0x0c, 0x20, 0x53, 0x65 },
};
    
/* I am not a number !!! This GUID is the one found on the PC used to
   generate the stream */
static const GUID my_guid = {
    0, 0, 0, { 0, 0, 0, 0, 0, 0, 0, 0 },
};

CodecTag codec_asf_bmp_tags[] = {
    { CODEC_ID_H263, MKTAG('H', '2', '6', '3') },
    { CODEC_ID_H263P, MKTAG('H', '2', '6', '3') },
    { CODEC_ID_H263I, MKTAG('I', '2', '6', '3') }, /* intel h263 */
    { CODEC_ID_MJPEG, MKTAG('M', 'J', 'P', 'G') },
    { CODEC_ID_MPEG4, MKTAG('D', 'I', 'V', 'X') },
    { CODEC_ID_MPEG4, MKTAG('d', 'i', 'v', 'x') },
    { CODEC_ID_MPEG4, MKTAG(0x04, 0, 0, 0) }, /* some broken avi use this */
    { CODEC_ID_MSMPEG4V3, MKTAG('M', 'P', '4', '3') }, /* default signature when using MSMPEG4 */ 
    { CODEC_ID_MSMPEG4V3, MKTAG('D', 'I', 'V', '3') },
    { CODEC_ID_MSMPEG4V2, MKTAG('M', 'P', '4', '2') }, 
    { CODEC_ID_MSMPEG4V1, MKTAG('M', 'P', '4', '1') }, 
    { 0, 0 },
};



static void put_guid(ByteIOContext *s, const GUID *g)
{
    int i;

    put_le32(s, g->v1);
    put_le16(s, g->v2);
    put_le16(s, g->v3);
    for(i=0;i<8;i++)
        put_byte(s, g->v4[i]);
}

static void put_str16(ByteIOContext *s, const char *tag)
{
    int c;

    put_le16(s,strlen(tag) + 1);
    for(;;) {
        c = (UINT8)*tag++;
        put_le16(s, c);
        if (c == '\0') 
            break;
    }
}

static void put_str16_nolen(ByteIOContext *s, const char *tag)
{
    int c;

    for(;;) {
        c = (UINT8)*tag++;
        put_le16(s, c);
        if (c == '\0') 
            break;
    }
}

static INT64 put_header(ByteIOContext *pb, const GUID *g)
{
    INT64 pos;

    pos = url_ftell(pb);
    put_guid(pb, g);
    put_le64(pb, 24);
    return pos;
}

/* update header size */
static void end_header(ByteIOContext *pb, INT64 pos)
{
    INT64 pos1;

    pos1 = url_ftell(pb);
    url_fseek(pb, pos + 16, SEEK_SET);
    put_le64(pb, pos1 - pos);
    url_fseek(pb, pos1, SEEK_SET);
}

/* write an asf chunk (only used in streaming case) */
static void put_chunk(AVFormatContext *s, int type, int payload_length, int flags)
{
    ASFContext *asf = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int length;

    length = payload_length + 8;
    put_le16(pb, type); 
    put_le16(pb, length); 
    put_le32(pb, asf->seqno);
    put_le16(pb, flags); /* unknown bytes */
    put_le16(pb, length);
    asf->seqno++;
}

/* convert from unix to windows time */
static INT64 unix_to_file_time(int ti)
{
    INT64 t;
    
    t = ti * INT64_C(10000000);
    t += INT64_C(116444736000000000);
    return t;
}

/* write the header (used two times if non streamed) */
static int asf_write_header1(AVFormatContext *s, INT64 file_size, INT64 data_chunk_size)
{
    ASFContext *asf = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int header_size, n, extra_size, extra_size2, wav_extra_size, file_time;
    int has_title;
    AVCodecContext *enc;
    INT64 header_offset, cur_pos, hpos;
    int bit_rate;

    has_title = (s->title[0] != '\0');

    bit_rate = 0;
    for(n=0;n<s->nb_streams;n++) {
        enc = &s->streams[n]->codec;

        bit_rate += enc->bit_rate;
    }

    if (url_is_streamed(&s->pb)) {
        put_chunk(s, 0x4824, 0, 0xc00); /* start of stream (length will be patched later) */
    }

    put_guid(pb, &asf_header);
    put_le64(pb, 0); /* header length, will be patched after */
    put_le32(pb, 3 + has_title + s->nb_streams); /* number of chunks in header */
    put_byte(pb, 1); /* ??? */
    put_byte(pb, 2); /* ??? */
    
    /* file header */
    header_offset = url_ftell(pb);
    hpos = put_header(pb, &file_header);
    put_guid(pb, &my_guid);
    put_le64(pb, file_size);
    file_time = 0;
    put_le64(pb, unix_to_file_time(file_time));
    put_le64(pb, asf->nb_packets); /* number of packets */
    put_le64(pb, asf->duration); /* end time stamp (in 100ns units) */
    put_le64(pb, asf->duration); /* duration (in 100ns units) */
    put_le32(pb, 0); /* start time stamp */ 
    put_le32(pb, 0); /* ??? */ 
    put_le32(pb, url_is_streamed(&s->pb) ? 1 : 0); /* ??? */ 
    put_le32(pb, asf->packet_size); /* packet size */
    put_le32(pb, asf->packet_size); /* packet size */
    put_le32(pb, bit_rate); /* Nominal data rate in bps */
    end_header(pb, hpos);

    /* unknown headers */
    hpos = put_header(pb, &head1_guid);
    put_guid(pb, &head2_guid);
    put_le32(pb, 6);
    put_le16(pb, 0);
    end_header(pb, hpos);

    /* title and other infos */
    if (has_title) {
        hpos = put_header(pb, &comment_header);
        put_le16(pb, 2 * (strlen(s->title) + 1));
        put_le16(pb, 2 * (strlen(s->author) + 1));
        put_le16(pb, 2 * (strlen(s->copyright) + 1));
        put_le16(pb, 2 * (strlen(s->comment) + 1));
        put_le16(pb, 0);
        put_str16_nolen(pb, s->title);
        put_str16_nolen(pb, s->author);
        put_str16_nolen(pb, s->copyright);
        put_str16_nolen(pb, s->comment);
        end_header(pb, hpos);
    }

    /* stream headers */
    for(n=0;n<s->nb_streams;n++) {
        INT64 es_pos;
        ASFStream *stream = &asf->streams[n];

        enc = &s->streams[n]->codec;
        asf->streams[n].num = n + 1;
        asf->streams[n].seq = 0;
        
        switch(enc->codec_type) {
        case CODEC_TYPE_AUDIO:
            wav_extra_size = 0;
            extra_size = 18 + wav_extra_size;
            extra_size2 = 0;
            /* Init the ticker */
            ticker_init(&stream->pts_ticker,
                        enc->sample_rate,
                        1000 * enc->frame_size);
            break;
        default:
        case CODEC_TYPE_VIDEO:
            wav_extra_size = 0;
            extra_size = 0x33;
            extra_size2 = 0;
            /* Init the ticker */
            ticker_init(&stream->pts_ticker,
                        enc->frame_rate,
                        1000 * FRAME_RATE_BASE);
            break;
        }

        hpos = put_header(pb, &stream_header);
        if (enc->codec_type == CODEC_TYPE_AUDIO) {
            put_guid(pb, &audio_stream);
            put_guid(pb, &audio_conceal_none);
        } else {
            put_guid(pb, &video_stream);
            put_guid(pb, &video_conceal_none);
        }
        put_le64(pb, 0); /* ??? */
        es_pos = url_ftell(pb);
        put_le32(pb, extra_size); /* wav header len */
        put_le32(pb, extra_size2); /* additional data len */
        put_le16(pb, n + 1); /* stream number */
        put_le32(pb, 0); /* ??? */
        
        if (enc->codec_type == CODEC_TYPE_AUDIO) {
            /* WAVEFORMATEX header */
            int wavsize = put_wav_header(pb, enc);

            if (wavsize < 0)
                return -1;
            if (wavsize != extra_size) {
                cur_pos = url_ftell(pb);
                url_fseek(pb, es_pos, SEEK_SET);
                put_le32(pb, wavsize); /* wav header len */
                url_fseek(pb, cur_pos, SEEK_SET);
            }
        } else {
            put_le32(pb, enc->width);
            put_le32(pb, enc->height);
            put_byte(pb, 2); /* ??? */
            put_le16(pb, 40); /* size */

            /* BITMAPINFOHEADER header */
            put_bmp_header(pb, enc, codec_asf_bmp_tags);
        }
        end_header(pb, hpos);
    }

    /* media comments */

    hpos = put_header(pb, &codec_comment_header);
    put_guid(pb, &codec_comment1_header);
    put_le32(pb, s->nb_streams);
    for(n=0;n<s->nb_streams;n++) {
        enc = &s->streams[n]->codec;

        put_le16(pb, asf->streams[n].num);
        put_str16(pb, enc->codec_name);
        put_le16(pb, 0); /* no parameters */
        /* id */
        if (enc->codec_type == CODEC_TYPE_AUDIO) {
            put_le16(pb, 2);
            put_le16(pb, codec_get_tag(codec_wav_tags, enc->codec_id));
        } else {
            put_le16(pb, 4);
            put_le32(pb, codec_get_tag(codec_asf_bmp_tags, enc->codec_id));
        }
    }
    end_header(pb, hpos);

    /* patch the header size fields */

    cur_pos = url_ftell(pb);
    header_size = cur_pos - header_offset;
    if (url_is_streamed(&s->pb)) {
        header_size += 8 + 30 + 50;

        url_fseek(pb, header_offset - 10 - 30, SEEK_SET);
        put_le16(pb, header_size);
        url_fseek(pb, header_offset - 2 - 30, SEEK_SET);
        put_le16(pb, header_size);

        header_size -= 8 + 30 + 50;
    }
    header_size += 24 + 6;
    url_fseek(pb, header_offset - 14, SEEK_SET);
    put_le64(pb, header_size);
    url_fseek(pb, cur_pos, SEEK_SET);

    /* movie chunk, followed by packets of packet_size */
    asf->data_offset = cur_pos;
    put_guid(pb, &data_header);
    put_le64(pb, data_chunk_size);
    put_guid(pb, &my_guid);
    put_le64(pb, asf->nb_packets); /* nb packets */
    put_byte(pb, 1); /* ??? */
    put_byte(pb, 1); /* ??? */
    return 0;
}

static int asf_write_header(AVFormatContext *s)
{
    ASFContext *asf = s->priv_data;

    asf->packet_size = PACKET_SIZE;
    asf->nb_packets = 0;

    if (asf_write_header1(s, 0, 50) < 0) {
        av_free(asf);
        return -1;
    }

    put_flush_packet(&s->pb);

    asf->packet_nb_frames = 0;
    asf->packet_timestamp_start = -1;
    asf->packet_timestamp_end = -1;
    asf->packet_size_left = asf->packet_size - PACKET_HEADER_SIZE;
    init_put_byte(&asf->pb, asf->packet_buf, asf->packet_size, 1,
                  NULL, NULL, NULL, NULL);

    return 0;
}

/* write a fixed size packet */
static int put_packet(AVFormatContext *s, 
                       unsigned int timestamp, unsigned int duration, 
                       int nb_frames, int padsize)
{
    ASFContext *asf = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int flags;

    if (url_is_streamed(&s->pb)) {
        put_chunk(s, 0x4424, asf->packet_size, 0);
    }

    put_byte(pb, 0x82);
    put_le16(pb, 0);
    
    flags = 0x01; /* nb segments present */
    if (padsize > 0) {
        if (padsize < 256)
            flags |= 0x08;
        else
            flags |= 0x10;
    }
    put_byte(pb, flags); /* flags */
    put_byte(pb, 0x5d);
    if (flags & 0x10)
        put_le16(pb, padsize - 2);
    if (flags & 0x08)
        put_byte(pb, padsize - 1);
    put_le32(pb, timestamp);
    put_le16(pb, duration);
    put_byte(pb, nb_frames | 0x80);

    return PACKET_HEADER_SIZE + ((flags & 0x18) >> 3);
}

static void flush_packet(AVFormatContext *s)
{
    ASFContext *asf = s->priv_data;
    int hdr_size, ptr;
    
    hdr_size = put_packet(s, asf->packet_timestamp_start, 
               asf->packet_timestamp_end - asf->packet_timestamp_start, 
               asf->packet_nb_frames, asf->packet_size_left);
    
    /* Clear out the padding bytes */
    ptr = asf->packet_size - hdr_size - asf->packet_size_left;
    memset(asf->packet_buf + ptr, 0, asf->packet_size_left);
    
    put_buffer(&s->pb, asf->packet_buf, asf->packet_size - hdr_size);
    
    put_flush_packet(&s->pb);
    asf->nb_packets++;
    asf->packet_nb_frames = 0;
    asf->packet_timestamp_start = -1;
    asf->packet_timestamp_end = -1;
    asf->packet_size_left = asf->packet_size - PACKET_HEADER_SIZE;
    init_put_byte(&asf->pb, asf->packet_buf, asf->packet_size, 1,
                  NULL, NULL, NULL, NULL);
}

static void put_frame_header(AVFormatContext *s, ASFStream *stream, int timestamp,
                             int payload_size, int frag_offset, int frag_len)
{
    ASFContext *asf = s->priv_data;
    ByteIOContext *pb = &asf->pb;
    int val;

    val = stream->num;
    if (s->streams[val - 1]->codec.key_frame /* && frag_offset == 0 */)
        val |= 0x80;
    put_byte(pb, val);
    put_byte(pb, stream->seq);
    put_le32(pb, frag_offset); /* fragment offset */
    put_byte(pb, 0x08); /* flags */
    put_le32(pb, payload_size);
    put_le32(pb, timestamp);
    put_le16(pb, frag_len);
}


/* Output a frame. We suppose that payload_size <= PACKET_SIZE.

   It is there that you understand that the ASF format is really
   crap. They have misread the MPEG Systems spec ! 
 */
static void put_frame(AVFormatContext *s, ASFStream *stream, int timestamp,
                      UINT8 *buf, int payload_size)
{
    ASFContext *asf = s->priv_data;
    int frag_pos, frag_len, frag_len1;
    
    frag_pos = 0;
    while (frag_pos < payload_size) {
        frag_len = payload_size - frag_pos;
        frag_len1 = asf->packet_size_left - FRAME_HEADER_SIZE;
        if (frag_len1 > 0) {
            if (frag_len > frag_len1)
                frag_len = frag_len1;
            put_frame_header(s, stream, timestamp, payload_size, frag_pos, frag_len);
            put_buffer(&asf->pb, buf, frag_len);
            asf->packet_size_left -= (frag_len + FRAME_HEADER_SIZE);
            asf->packet_timestamp_end = timestamp;
            if (asf->packet_timestamp_start == -1)
                asf->packet_timestamp_start = timestamp;
            asf->packet_nb_frames++;
        } else {
            frag_len = 0;
        }
        frag_pos += frag_len;
        buf += frag_len;
        /* output the frame if filled */
        if (asf->packet_size_left <= FRAME_HEADER_SIZE)
            flush_packet(s);
    }
    stream->seq++;
}


static int asf_write_packet(AVFormatContext *s, int stream_index,
                            UINT8 *buf, int size, int force_pts)
{
    ASFContext *asf = s->priv_data;
    ASFStream *stream;
    int timestamp;
    INT64 duration;
    AVCodecContext *codec;

    stream = &asf->streams[stream_index];
    codec = &s->streams[stream_index]->codec;
    stream = &asf->streams[stream_index];
    
    if (codec->codec_type == CODEC_TYPE_AUDIO) {
        timestamp = (int)ticker_tick(&stream->pts_ticker, codec->frame_number);
        duration = (codec->frame_number * codec->frame_size * INT64_C(10000000)) / 
            codec->sample_rate;
    } else {
        timestamp = (int)ticker_tick(&stream->pts_ticker, codec->frame_number);
        duration = codec->frame_number * 
            ((INT64_C(10000000) * FRAME_RATE_BASE) / codec->frame_rate);
    }
    if (duration > asf->duration)
        asf->duration = duration;
    
    put_frame(s, stream, timestamp, buf, size);
    return 0;
}
    
static int asf_write_trailer(AVFormatContext *s)
{
    ASFContext *asf = s->priv_data;
    INT64 file_size;

    /* flush the current packet */
    if (asf->pb.buf_ptr > asf->pb.buffer)
        flush_packet(s);

    if (url_is_streamed(&s->pb)) {
        put_chunk(s, 0x4524, 0, 0); /* end of stream */
    } else {
        /* rewrite an updated header */
        file_size = url_ftell(&s->pb);
        url_fseek(&s->pb, 0, SEEK_SET);
        asf_write_header1(s, file_size, file_size - asf->data_offset);
    }

    put_flush_packet(&s->pb);
    return 0;
}

/**********************************/
/* decoding */

//#define DEBUG

#ifdef DEBUG
static void print_guid(const GUID *g)
{
    int i;
    printf("0x%08x, 0x%04x, 0x%04x, {", g->v1, g->v2, g->v3);
    for(i=0;i<8;i++) 
        printf(" 0x%02x,", g->v4[i]);
    printf("}\n");
}
#endif

static void get_guid(ByteIOContext *s, GUID *g)
{
    int i;

    g->v1 = get_le32(s);
    g->v2 = get_le16(s);
    g->v3 = get_le16(s);
    for(i=0;i<8;i++)
        g->v4[i] = get_byte(s);
}

#if 0
static void get_str16(ByteIOContext *pb, char *buf, int buf_size)
{
    int len, c;
    char *q;

    len = get_le16(pb);
    q = buf;
    while (len > 0) {
        c = get_le16(pb);
        if ((q - buf) < buf_size - 1)
            *q++ = c;
        len--;
    }
    *q = '\0';
}
#endif

static void get_str16_nolen(ByteIOContext *pb, int len, char *buf, int buf_size)
{
    int c;
    char *q;

    q = buf;
    while (len > 0) {
        c = get_le16(pb);
        if ((q - buf) < buf_size - 1)
            *q++ = c;
        len-=2;
    }
    *q = '\0';
}

static int asf_probe(AVProbeData *pd)
{
    GUID g;
    const unsigned char *p;
    int i;

    /* check file header */
    if (pd->buf_size <= 32)
        return 0;
    p = pd->buf;
    g.v1 = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
    p += 4;
    g.v2 = p[0] | (p[1] << 8);
    p += 2;
    g.v3 = p[0] | (p[1] << 8);
    p += 2;
    for(i=0;i<8;i++)
        g.v4[i] = *p++;

    if (!memcmp(&g, &asf_header, sizeof(GUID)))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int asf_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    ASFContext *asf = s->priv_data;
    GUID g;
    ByteIOContext *pb = &s->pb;
    AVStream *st;
    ASFStream *asf_st;
    int size, i, bps;
    INT64 gsize;

    get_guid(pb, &g);
    if (memcmp(&g, &asf_header, sizeof(GUID)))
        goto fail;
    get_le64(pb);
    get_le32(pb);
    get_byte(pb);
    get_byte(pb);

    for(;;) {
        get_guid(pb, &g);
        gsize = get_le64(pb);
#ifdef DEBUG
        printf("%08Lx: ", url_ftell(pb) - 24);
        print_guid(&g);
        printf("  size=0x%Lx\n", gsize);
#endif
        if (gsize < 24)
            goto fail;
        if (!memcmp(&g, &file_header, sizeof(GUID))) {
            get_guid(pb, &g);
            get_le64(pb); /* file size */
            get_le64(pb); /* file time */
            get_le64(pb); /* nb_packets */
            get_le64(pb); /* length 0 in us */
            get_le64(pb); /* length 1 in us */
            get_le32(pb);
            get_le32(pb);
            get_le32(pb);
            asf->packet_size = get_le32(pb);
            get_le32(pb);
            get_le32(pb);
        } else if (!memcmp(&g, &stream_header, sizeof(GUID))) {
            int type, id, total_size;
            unsigned int tag1;
            INT64 pos1, pos2;
            
            pos1 = url_ftell(pb);

            st = av_mallocz(sizeof(AVStream));
            if (!st)
                goto fail;
            s->streams[s->nb_streams++] = st;
            asf_st = av_mallocz(sizeof(ASFStream));
            if (!asf_st)
                goto fail;
            st->priv_data = asf_st;

            get_guid(pb, &g);
            if (!memcmp(&g, &audio_stream, sizeof(GUID))) {
                type = CODEC_TYPE_AUDIO;
            } else if (!memcmp(&g, &video_stream, sizeof(GUID))) {
                type = CODEC_TYPE_VIDEO;
            } else {
                goto fail;
            }
            get_guid(pb, &g);
            total_size = get_le64(pb);
            get_le32(pb);
            get_le32(pb);
            st->id = get_le16(pb); /* stream id */
            get_le32(pb);
            st->codec.codec_type = type;
            if (type == CODEC_TYPE_AUDIO) {
                id = get_le16(pb); 
                st->codec.codec_tag = id;
                st->codec.channels = get_le16(pb);
                st->codec.sample_rate = get_le32(pb);
                st->codec.bit_rate = get_le32(pb) * 8;
                get_le16(pb); /* block align */
                bps = get_le16(pb); /* bits per sample */
                st->codec.codec_id = wav_codec_get_id(id, bps);
                size = get_le16(pb);
                url_fskip(pb, size);
                /* We have to init the frame size at some point .... */
                switch (st->codec.codec_id) {
                case CODEC_ID_MP3LAME:
                    st->codec.frame_size = MPA_FRAME_SIZE;
                    break;
                case CODEC_ID_PCM_S16LE:
                case CODEC_ID_PCM_S16BE:
                case CODEC_ID_PCM_U16LE:
                case CODEC_ID_PCM_U16BE:
                case CODEC_ID_PCM_S8:
                case CODEC_ID_PCM_U8:
                case CODEC_ID_PCM_ALAW:
                case CODEC_ID_PCM_MULAW:
                    st->codec.frame_size = 1;
                    break;
                default:
                    /* This is probably wrong, but it prevents a crash later */
                    st->codec.frame_size = 1;
                    break;
                }
            } else {
                get_le32(pb);
                get_le32(pb);
                get_byte(pb);
                size = get_le16(pb); /* size */
                get_le32(pb); /* size */
                st->codec.width = get_le32(pb);
                st->codec.height = get_le32(pb);
                st->codec.frame_rate = 25 * FRAME_RATE_BASE; /* XXX: find it */
                get_le16(pb); /* panes */
                get_le16(pb); /* depth */
                tag1 = get_le32(pb);
                st->codec.codec_tag = tag1;
                st->codec.codec_id = codec_get_id(codec_asf_bmp_tags, tag1);
                url_fskip(pb, size - 5 * 4);
            }
            pos2 = url_ftell(pb);
            url_fskip(pb, gsize - (pos2 - pos1 + 24));
        } else if (!memcmp(&g, &data_header, sizeof(GUID))) {
            break;
        } else if (!memcmp(&g, &comment_header, sizeof(GUID))) {
            int len1, len2, len3, len4, len5;

            len1 = get_le16(pb);
            len2 = get_le16(pb);
            len3 = get_le16(pb);
            len4 = get_le16(pb);
            len5 = get_le16(pb);
            get_str16_nolen(pb, len1, s->title, sizeof(s->title));
            get_str16_nolen(pb, len2, s->author, sizeof(s->author));
            get_str16_nolen(pb, len3, s->copyright, sizeof(s->copyright));
            get_str16_nolen(pb, len4, s->comment, sizeof(s->comment));
            url_fskip(pb, len5);
#if 0
        } else if (!memcmp(&g, &head1_guid, sizeof(GUID))) {
            int v1, v2;
            get_guid(pb, &g);
            v1 = get_le32(pb);
            v2 = get_le16(pb);
        } else if (!memcmp(&g, &codec_comment_header, sizeof(GUID))) {
            int len, v1, n, num;
            char str[256], *q;
            char tag[16];

            get_guid(pb, &g);
            print_guid(&g);
            
            n = get_le32(pb);
            for(i=0;i<n;i++) {
                num = get_le16(pb); /* stream number */
                get_str16(pb, str, sizeof(str));
                get_str16(pb, str, sizeof(str));
                len = get_le16(pb);
                q = tag;
                while (len > 0) {
                    v1 = get_byte(pb);
                    if ((q - tag) < sizeof(tag) - 1)
                        *q++ = v1;
                    len--;
                }
                *q = '\0';
            }
#endif
        } else if (url_feof(pb)) {
            goto fail;
        } else {
            url_fseek(pb, gsize - 24, SEEK_CUR);
        }
    }
    get_guid(pb, &g);
    get_le64(pb);
    get_byte(pb);
    get_byte(pb);

    asf->packet_size_left = 0;

    return 0;

 fail:
    for(i=0;i<s->nb_streams;i++) {
        AVStream *st = s->streams[i];
        if (st)
            av_free(st->priv_data);
        av_free(st);
    }
    av_free(asf);
    return -1;
}

static int asf_get_packet(AVFormatContext *s)
{
    ASFContext *asf = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int c, flags, timestamp, hdr_size;

    hdr_size = 12;
    c = get_byte(pb);
    if (c != 0x82)
        return -EIO;
    get_le16(pb);
    flags = get_byte(pb);
    get_byte(pb);
    asf->packet_padsize = 0;
    if (flags & 0x10) {
        asf->packet_padsize = get_le16(pb);
        hdr_size += 2;
    } else if (flags & 0x08) {
        asf->packet_padsize = get_byte(pb);
        hdr_size++;
    }
    timestamp = get_le32(pb);
    get_le16(pb); /* duration */
    get_byte(pb); /* nb_frames */
#ifdef DEBUG
    printf("packet: size=%d padsize=%d\n", asf->packet_size, asf->packet_padsize);
#endif
    asf->packet_size_left = asf->packet_size - hdr_size;
    return 0;
}

static int asf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    ASFContext *asf = s->priv_data;
    AVStream *st;
    ASFStream *asf_st;
    ByteIOContext *pb = &s->pb;
    int ret, num, seq, frag_offset, payload_size, frag_len;
    int key_frame;
    int timestamp, i;

    for(;;) {
        if (asf->packet_size_left < FRAME_HEADER_SIZE ||
            asf->packet_size_left <= asf->packet_padsize) {
            /* fail safe */
            if (asf->packet_size_left)
                url_fskip(pb, asf->packet_size_left);
            ret = asf_get_packet(s);
            if (ret < 0)
                return -EIO;
        }
        /* read frame header */
        num = get_byte(pb);
        if (num & 0x80)
            key_frame = 1;
        else    
            key_frame = 0;

        num &= 0x7f;    
        seq = get_byte(pb);
        frag_offset = get_le32(pb);
        get_byte(pb); /* flags */
        payload_size = get_le32(pb);
        timestamp = get_le32(pb);
        frag_len = get_le16(pb);
#ifdef DEBUG
        printf("num=%d seq=%d totsize=%d frag_off=%d frag_size=%d\n",
               num, seq, payload_size, frag_offset, frag_len);
#endif
        st = NULL;
        for(i=0;i<s->nb_streams;i++) {
            st = s->streams[i];
            if (st->id == num)
                break;
        }
        asf->packet_size_left -= FRAME_HEADER_SIZE + frag_len;
        if (i == s->nb_streams) {
            /* unhandled packet (should not happen) */
            url_fskip(pb, frag_len);
        } else {
            asf_st = st->priv_data;
            if (asf_st->frag_offset == 0) {
                /* new packet */
                av_new_packet(&asf_st->pkt, payload_size);
                asf_st->seq = seq;
                if (key_frame)
                    asf_st->pkt.flags |= PKT_FLAG_KEY;

                asf_st->pkt.pts = timestamp;    
            } else {
                if (seq == asf_st->seq && 
                    frag_offset == asf_st->frag_offset) {
                    /* continuing packet */
                } else {
                    /* cannot continue current packet: free it */
                    av_free_packet(&asf_st->pkt);
                    asf_st->frag_offset = 0;
                    if (frag_offset != 0) {
                        /* cannot create new packet */
                        url_fskip(pb, frag_len);
                        goto next_frame;
                    } else {
                        /* create new packet */
                        av_new_packet(&asf_st->pkt, payload_size);
                        asf_st->seq = seq;
                    }
                }
            }
            /* read data */
            get_buffer(pb, asf_st->pkt.data + frag_offset, frag_len);
            asf_st->frag_offset += frag_len;
            /* test if whole packet read */
            if (asf_st->frag_offset == asf_st->pkt.size) {
                /* return packet */
                asf_st->pkt.stream_index = i;
                asf_st->frag_offset = 0;
                memcpy(pkt, &asf_st->pkt, sizeof(AVPacket)); 
                break;
            }
        }
    next_frame:;
    }

    return 0;
}

static int asf_read_close(AVFormatContext *s)
{
    ASFContext *asf = s->priv_data;
    int i;

    for(i=0;i<s->nb_streams;i++) {
        AVStream *st = s->streams[i];
        av_free(st->priv_data);
    }
    av_free(asf);
    return 0;
}

AVInputFormat asf_iformat = {
    "asf",
    "asf format",
    sizeof(ASFContext),
    asf_probe,
    asf_read_header,
    asf_read_packet,
    asf_read_close,
};

AVOutputFormat asf_oformat = {
    "asf",
    "asf format",
    "application/octet-stream",
    "asf,wmv",
    sizeof(ASFContext),
#ifdef CONFIG_MP3LAME
    CODEC_ID_MP3LAME,
#else
    CODEC_ID_MP2,
#endif
    CODEC_ID_MSMPEG4,
    asf_write_header,
    asf_write_packet,
    asf_write_trailer,
};

int asf_init(void)
{
    av_register_input_format(&asf_iformat);
    av_register_output_format(&asf_oformat);
    return 0;
}
