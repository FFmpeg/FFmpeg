/* 
 * AU encoder and decoder
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

/*
 * First version by Francois Revol revol@free.fr
 *
 * Reference documents:
 * http://www.opengroup.org/public/pubs/external/auformat.html
 * http://www.goice.co.jp/member/mo/formats/au.html
 */

#include "avformat.h"
#include "avi.h"

/* if we don't know the size in advance */
#define AU_UNKOWN_SIZE ((uint32_t)(~0))

/* The ffmpeg codecs we support, and the IDs they have in the file */
static const CodecTag codec_au_tags[] = {
    { CODEC_ID_PCM_MULAW, 1 },
    { CODEC_ID_PCM_S16BE, 3 },
    { CODEC_ID_PCM_ALAW, 27 },
    { 0, 0 },
};

#ifdef CONFIG_ENCODERS
/* AUDIO_FILE header */
static int put_au_header(ByteIOContext *pb, AVCodecContext *enc)
{
    if(!enc->codec_tag)
       enc->codec_tag = codec_get_tag(codec_au_tags, enc->codec_id);
    if(!enc->codec_tag)
        return -1;
    put_tag(pb, ".snd");       /* magic number */
    put_be32(pb, 24);           /* header size */
    put_be32(pb, AU_UNKOWN_SIZE); /* data size */
    put_be32(pb, (uint32_t)enc->codec_tag);     /* codec ID */
    put_be32(pb, enc->sample_rate);
    put_be32(pb, (uint32_t)enc->channels);
    return 0;
}

static int au_write_header(AVFormatContext *s)
{
    ByteIOContext *pb = &s->pb;

    s->priv_data = NULL;

    /* format header */
    if (put_au_header(pb, &s->streams[0]->codec) < 0) {
        return -1;
    }

    put_flush_packet(pb);

    return 0;
}

static int au_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    ByteIOContext *pb = &s->pb;
    put_buffer(pb, pkt->data, pkt->size);
    return 0;
}

static int au_write_trailer(AVFormatContext *s)
{
    ByteIOContext *pb = &s->pb;
    offset_t file_size;

    if (!url_is_streamed(&s->pb)) {

        /* update file size */
        file_size = url_ftell(pb);
        url_fseek(pb, 8, SEEK_SET);
        put_be32(pb, (uint32_t)(file_size - 24));
        url_fseek(pb, file_size, SEEK_SET);

        put_flush_packet(pb);
    }

    return 0;
}
#endif //CONFIG_ENCODERS

static int au_probe(AVProbeData *p)
{
    /* check file header */
    if (p->buf_size <= 24)
        return 0;
    if (p->buf[0] == '.' && p->buf[1] == 's' &&
        p->buf[2] == 'n' && p->buf[3] == 'd')
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

/* au input */
static int au_read_header(AVFormatContext *s,
                          AVFormatParameters *ap)
{
    int size;
    unsigned int tag;
    ByteIOContext *pb = &s->pb;
    unsigned int id, codec, channels, rate;
    AVStream *st;

    /* check ".snd" header */
    tag = get_le32(pb);
    if (tag != MKTAG('.', 's', 'n', 'd'))
        return -1;
    size = get_be32(pb); /* header size */
    get_be32(pb); /* data size */
    
    id = get_be32(pb);
    rate = get_be32(pb);
    channels = get_be32(pb);
    
    codec = codec_get_id(codec_au_tags, id);

    if (size >= 24) {
        /* skip unused data */
        url_fseek(pb, size - 24, SEEK_CUR);
    }

    /* now we are ready: build format streams */
    st = av_new_stream(s, 0);
    if (!st)
        return -1;
    st->codec.codec_type = CODEC_TYPE_AUDIO;
    st->codec.codec_tag = id;
    st->codec.codec_id = codec;
    st->codec.channels = channels;
    st->codec.sample_rate = rate;
    return 0;
}

#define MAX_SIZE 4096

static int au_read_packet(AVFormatContext *s,
                          AVPacket *pkt)
{
    int ret;

    if (url_feof(&s->pb))
        return AVERROR_IO;
    if (av_new_packet(pkt, MAX_SIZE))
        return AVERROR_IO;
    pkt->stream_index = 0;

    ret = get_buffer(&s->pb, pkt->data, pkt->size);
    if (ret < 0)
        av_free_packet(pkt);
    /* note: we need to modify the packet size here to handle the last
       packet */
    pkt->size = ret;
    return 0;
}

static int au_read_close(AVFormatContext *s)
{
    return 0;
}

static AVInputFormat au_iformat = {
    "au",
    "SUN AU Format",
    0,
    au_probe,
    au_read_header,
    au_read_packet,
    au_read_close,
    pcm_read_seek,
};

#ifdef CONFIG_ENCODERS
static AVOutputFormat au_oformat = {
    "au",
    "SUN AU Format",
    "audio/basic",
    "au",
    0,
    CODEC_ID_PCM_S16BE,
    CODEC_ID_NONE,
    au_write_header,
    au_write_packet,
    au_write_trailer,
};
#endif //CONFIG_ENCODERS

int au_init(void)
{
    av_register_input_format(&au_iformat);
#ifdef CONFIG_ENCODERS
    av_register_output_format(&au_oformat);
#endif //CONFIG_ENCODERS
    return 0;
}
