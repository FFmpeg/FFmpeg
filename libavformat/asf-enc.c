/*
 * Adaptive stream format encoder
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
#include "asf.h"

#undef NDEBUG
#include <assert.h>

#ifdef CONFIG_ENCODERS
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
        c = (uint8_t)*tag++;
        put_le16(s, c);
        if (c == '\0')
            break;
    }
}

static void put_str16_nolen(ByteIOContext *s, const char *tag)
{
    int c;

    for(;;) {
        c = (uint8_t)*tag++;
        put_le16(s, c);
        if (c == '\0')
            break;
    }
}

static int64_t put_header(ByteIOContext *pb, const GUID *g)
{
    int64_t pos;

    pos = url_ftell(pb);
    put_guid(pb, g);
    put_le64(pb, 24);
    return pos;
}

/* update header size */
static void end_header(ByteIOContext *pb, int64_t pos)
{
    int64_t pos1;

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
static int64_t unix_to_file_time(int ti)
{
    int64_t t;

    t = ti * int64_t_C(10000000);
    t += int64_t_C(116444736000000000);
    return t;
}

/* write the header (used two times if non streamed) */
static int asf_write_header1(AVFormatContext *s, int64_t file_size, int64_t data_chunk_size)
{
    ASFContext *asf = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int header_size, n, extra_size, extra_size2, wav_extra_size, file_time;
    int has_title;
    AVCodecContext *enc;
    int64_t header_offset, cur_pos, hpos;
    int bit_rate;

    has_title = (s->title[0] || s->author[0] || s->copyright[0] || s->comment[0]);

    bit_rate = 0;
    for(n=0;n<s->nb_streams;n++) {
        enc = &s->streams[n]->codec;

        bit_rate += enc->bit_rate;
    }

    if (asf->is_streamed) {
        put_chunk(s, 0x4824, 0, 0xc00); /* start of stream (length will be patched later) */
    }

    put_guid(pb, &asf_header);
    put_le64(pb, -1); /* header length, will be patched after */
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
    put_le32(pb, asf->is_streamed ? 1 : 0); /* ??? */
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
        int64_t es_pos;
        //        ASFStream *stream = &asf->streams[n];

        enc = &s->streams[n]->codec;
        asf->streams[n].num = n + 1;
        asf->streams[n].seq = 0;

        switch(enc->codec_type) {
        case CODEC_TYPE_AUDIO:
            wav_extra_size = 0;
            extra_size = 18 + wav_extra_size;
            extra_size2 = 0;
            break;
        default:
        case CODEC_TYPE_VIDEO:
            wav_extra_size = 0;
            extra_size = 0x33;
            extra_size2 = 0;
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
            put_bmp_header(pb, enc, codec_bmp_tags, 1);
        }
        end_header(pb, hpos);
    }

    /* media comments */

    hpos = put_header(pb, &codec_comment_header);
    put_guid(pb, &codec_comment1_header);
    put_le32(pb, s->nb_streams);
    for(n=0;n<s->nb_streams;n++) {
        AVCodec *p;

        enc = &s->streams[n]->codec;
        p = avcodec_find_encoder(enc->codec_id);

        put_le16(pb, asf->streams[n].num);
        put_str16(pb, p ? p->name : enc->codec_name);
        put_le16(pb, 0); /* no parameters */
        
        
        /* id */
        if (enc->codec_type == CODEC_TYPE_AUDIO) {
            put_le16(pb, 2);
            if(!enc->codec_tag)
                enc->codec_tag = codec_get_tag(codec_wav_tags, enc->codec_id);
            if(!enc->codec_tag)
                return -1;
            put_le16(pb, enc->codec_tag);
        } else {
            put_le16(pb, 4);
            if(!enc->codec_tag)
                enc->codec_tag = codec_get_tag(codec_bmp_tags, enc->codec_id);
            if(!enc->codec_tag)
                return -1;
            put_le32(pb, enc->codec_tag);
        }
    }
    end_header(pb, hpos);

    /* patch the header size fields */

    cur_pos = url_ftell(pb);
    header_size = cur_pos - header_offset;
    if (asf->is_streamed) {
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

    av_set_pts_info(s, 32, 1, 1000); /* 32 bit pts in ms */

    asf->packet_size = PACKET_SIZE;
    asf->nb_packets = 0;

    if (asf_write_header1(s, 0, 50) < 0) {
        //av_free(asf);
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

static int asf_write_stream_header(AVFormatContext *s)
{
    ASFContext *asf = s->priv_data;

    asf->is_streamed = 1;

    return asf_write_header(s);
}

/* write a fixed size packet */
static int put_packet(AVFormatContext *s,
                       unsigned int timestamp, unsigned int duration,
                       int nb_frames, int padsize)
{
    ASFContext *asf = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int flags;

    if (asf->is_streamed) {
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
    if (s->streams[val - 1]->codec.coded_frame->key_frame /* && frag_offset == 0 */)
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
                      const uint8_t *buf, int payload_size)
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
            put_frame_header(s, stream, timestamp+1, payload_size, frag_pos, frag_len);
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
                            const uint8_t *buf, int size, int64_t timestamp)
{
    ASFContext *asf = s->priv_data;
    ASFStream *stream;
    int64_t duration;
    AVCodecContext *codec;

    codec = &s->streams[stream_index]->codec;
    stream = &asf->streams[stream_index];

    if (codec->codec_type == CODEC_TYPE_AUDIO) {
        duration = (codec->frame_number * codec->frame_size * int64_t_C(10000000)) /
            codec->sample_rate;
    } else {
        duration = av_rescale(codec->frame_number * codec->frame_rate_base, 10000000, codec->frame_rate);
    }
    if (duration > asf->duration)
        asf->duration = duration;

    put_frame(s, stream, timestamp, buf, size);
    return 0;
}

static int asf_write_trailer(AVFormatContext *s)
{
    ASFContext *asf = s->priv_data;
    int64_t file_size;

    /* flush the current packet */
    if (asf->pb.buf_ptr > asf->pb.buffer)
        flush_packet(s);

    if (asf->is_streamed) {
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

AVOutputFormat asf_oformat = {
    "asf",
    "asf format",
    "video/x-ms-asf",
    "asf,wmv",
    sizeof(ASFContext),
#ifdef CONFIG_MP3LAME
    CODEC_ID_MP3,
#else
    CODEC_ID_MP2,
#endif
    CODEC_ID_MSMPEG4V3,
    asf_write_header,
    asf_write_packet,
    asf_write_trailer,
};

AVOutputFormat asf_stream_oformat = {
    "asf_stream",
    "asf format",
    "video/x-ms-asf",
    "asf,wmv",
    sizeof(ASFContext),
#ifdef CONFIG_MP3LAME
    CODEC_ID_MP3,
#else
    CODEC_ID_MP2,
#endif
    CODEC_ID_MSMPEG4V3,
    asf_write_stream_header,
    asf_write_packet,
    asf_write_trailer,
};
#endif //CONFIG_ENCODERS
