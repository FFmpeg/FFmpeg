/*
 * NUT (de)muxer based on initial draft
 * Copyright (c) 2003 Alex Beregszaszi
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
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * NUT DRAFT can be found in MPlayer CVS at DOCS/tech/mpcf.txt
 *
 * Compatible with draft version 20030906
 *
 */

/*
 * TODO:
 * - checksumming
 * - correct rate denom/nom and sample_mul
 * - correct timestamp handling
 * - correct startcodes
 * - index writing
 * - info and index packet reading support
 * - startcode searching for broken streams
 * - subpacket support
 * - handling of codec specific headers
*/

//#define DEBUG 1

#include "avformat.h"
#include "mpegaudio.h"

typedef struct {
    int curr_frame_start;
    int last_frame_size;
    int curr_frame_size;
} NUTContext;

static int bytes_left(ByteIOContext *bc)
{
    return bc->buf_end - bc->buf_ptr;
}

static uint64_t get_v(ByteIOContext *bc)
{
    uint64_t val = 0;

//    for (; bytes_left(s)*8 > 0; )
    for(; bytes_left(bc) > 0; )
    {
	int tmp = get_byte(bc);

	if (tmp&0x80)
	    val= (val<<7) + tmp - 0x80;
	else
	    return (val<<7) + tmp;
    }
    return -1;
}

static int64_t get_s(ByteIOContext *bc)
{
    int64_t v = get_v(bc) + 1;

    if (v&1)
        return -(v>>1);
    else
        return (v>>1);
}

static int get_b(ByteIOContext *bc, char *data, int maxlen)
{
    int i, len;
    
    len = get_v(bc);
    for (i = 0; i < len && i < maxlen; i++)
	data[i] = get_byte(bc);
    if (i < len)
    {
	len = i;
	for (i = 0; i < len; i++)
	    get_byte(bc);
    }

    return 0;
}

static int get_packetheader(NUTContext *nut, ByteIOContext *bc)
{
    nut->curr_frame_start = url_ftell(bc);
    nut->curr_frame_size = get_v(bc);
    nut->last_frame_size = get_v(bc);
    dprintf("Packet: fwd: %d bwd: %d\n",
	nut->curr_frame_size, nut->last_frame_size);
    
    return 0;
}

static int get_padding(NUTContext *nut, ByteIOContext *bc)
{
    int i, tmp, len = nut->curr_frame_size - (url_ftell(bc) - nut->curr_frame_start);

    for (i = 0; i < len; i++)
    {
	tmp = get_byte(bc);
	if (tmp != 0)
	    fprintf(stderr, "bad padding\n");
    }

    return 0;
}

static int put_v(ByteIOContext *bc, uint64_t val)
{
    int i;

//    if (bytes_left(s)*8 < 9)
//	return -1;

    if (bytes_left(bc) < 1)
	return -1;

    val &= 0x7FFFFFFFFFFFFFFFULL; // FIXME can only encode upto 63 bits currently
    for (i=7; ; i+=7)
	if ((val>>i) == 0)
	    break;

    for (i-=7; i>0; i-=8)
	put_byte(bc, 0x80 | (val>>i));

    put_byte(bc, val&0x7f);

    return 0;
}

static int put_s(ByteIOContext *bc, uint64_t val)
{
    if (val<=0)
	return put_v(bc, -2*val);
    else
	return put_v(bc, 2*val-1);
}

static int put_b(ByteIOContext *bc, char *data, int len)
{
    int i;
    
    put_v(bc, len);
    for (i = 0; i < len; i++)
	put_byte(bc, data[i]);

    return 0;
}

static int put_packetheader(NUTContext *nut, ByteIOContext *bc, int est_size)
{
    put_flush_packet(bc);
    nut->curr_frame_start = url_ftell(bc);
    nut->curr_frame_size = est_size;
    
    /* packet header */
    put_v(bc, nut->curr_frame_size); /* forward ptr */
    put_v(bc, nut->last_frame_size); /* backward ptr */
    dprintf("Packet: fwd: %d, bwd: %d\n",
	nut->curr_frame_size, nut->last_frame_size);

    nut->last_frame_size = nut->curr_frame_size;
    
    return 0;
}

static int put_padding(NUTContext *nut, ByteIOContext *bc)
{
    int i, len = nut->curr_frame_size - (url_ftell(bc) - nut->curr_frame_start);
    
    put_flush_packet(bc);
    for (i = 0; i < len; i++)
	put_byte(bc, 0);

    dprintf("padded %d bytes\n", i);

    return 0;
}

static int nut_write_header(AVFormatContext *s)
{
    NUTContext *nut = s->priv_data;
    ByteIOContext *bc = &s->pb;
    AVCodecContext *codec;
    int i;
    int stream_length = 0;

    for (i = 0; i < s->nb_streams; i++)
    {
	if (stream_length < (s->streams[i]->duration * (AV_TIME_BASE / 1000)))
	    stream_length = s->streams[i]->duration * (AV_TIME_BASE / 1000);
    }

    put_packetheader(nut, bc, 16); /* FIXME: estimation */
    
    /* main header */
    put_le64(bc, 1); /* FIXME: unique startcode */
    put_v(bc, 0); /* version */
    put_v(bc, s->nb_streams);
    put_v(bc, 0); /* file size */
    put_v(bc, stream_length); /* len in msec */
    put_padding(nut, bc);
    put_le32(bc, 0); /* FIXME: checksum */
    
    /* stream headers */
    for (i = 0; i < s->nb_streams; i++)
    {
	codec = &s->streams[i]->codec;
	
	put_packetheader(nut, bc, 64); /* FIXME: estimation */
	put_le64(bc, 1); /* FIXME: unique startcode */
	put_v(bc, s->streams[i]->index);
	put_v(bc, (codec->codec_type == CODEC_TYPE_AUDIO) ? 32 : 0);
	if (codec->codec_tag)
	    put_b(bc, codec->codec_tag, 4);
	else if (codec->codec_type == CODEC_TYPE_VIDEO)
	{
	    int tmp = codec_get_bmp_tag(codec->codec_id);
	    put_b(bc, &tmp, 4);
//	    put_v(bc, 4); /* len */
//	    put_le32(bc, codec_get_bmp_tag(codec->codec_id));
	}
	else if (codec->codec_type == CODEC_TYPE_AUDIO)
	{
	    int tmp = codec_get_wav_tag(codec->codec_id);
	    put_b(bc, &tmp, 4);
//	    put_v(bc, 4); /* len */
//	    put_le32(bc, codec_get_wav_tag(codec->codec_id));
	}
	put_v(bc, codec->bit_rate);
	put_v(bc, 0); /* no language code */
	put_v(bc, codec->frame_rate_base);
	put_v(bc, codec->frame_rate);
	put_v(bc, 0); /* timestamp_shift */
	put_v(bc, 0); /* shuffle type */
	put_byte(bc, 0); /* flags: 0x1 - fixed_fps, 0x2 - index_present */
	
	put_v(bc, 0); /* no codec specific headers */
	
	switch(codec->codec_type)
	{
	    case CODEC_TYPE_AUDIO:
		put_v(bc, codec->sample_rate / (double)(codec->frame_rate_base / codec->frame_rate));
		put_v(bc, codec->channels);
		put_padding(nut, bc);
		put_le32(bc, 0); /* FIXME: checksum */
		break;
	    case CODEC_TYPE_VIDEO:
		put_v(bc, codec->width);
		put_v(bc, codec->height);
		put_v(bc, 0); /* aspected w */
		put_v(bc, 0); /* aspected h */
		put_v(bc, 0); /* csp type -- unknown */
		put_padding(nut, bc);
		put_le32(bc, 0); /* FIXME: checksum */
		break;
	}
    }

#if 0
    /* info header */
    put_packetheader(nut, bc, 16+strlen(s->author)+strlen(s->title)+
        strlen(s->comment)+strlen(s->copyright)); /* FIXME: estimation */
    put_le64(bc, 1); /* FIXME: unique startcode */
    if (s->author[0])
    {
        put_v(bc, 5); /* type */
        put_b(bc, s->author, strlen(s->author));
    }
    if (s->title[0])
    {
        put_v(bc, 6); /* type */
        put_b(bc, s->title, strlen(s->title));
    }
    if (s->comment[0])
    {
        put_v(bc, 7); /* type */
        put_b(bc, s->comment, strlen(s->comment));
    }
    if (s->copyright[0])
    {
        put_v(bc, 8); /* type */
        put_b(bc, s->copyright, strlen(s->copyright));
    }
    /* encoder */
    put_v(bc, 9); /* type */
    put_b(bc, LIBAVFORMAT_IDENT, strlen(LIBAVFORMAT_IDENT));

    put_padding(nut, bc);
    put_le32(bc, 0); /* FIXME: checksum */
#endif
        
    put_flush_packet(bc);
    
    return 0;
}

static int nut_write_packet(AVFormatContext *s, int stream_index, 
			    uint8_t *buf, int size, int force_pts)
{
    NUTContext *nut = s->priv_data;
    ByteIOContext *bc = &s->pb;
    int key_frame = 0;
    AVCodecContext *enc;

    if (stream_index > s->nb_streams)
	return 1;

    enc = &s->streams[stream_index]->codec;
    if (enc->codec_type == CODEC_TYPE_VIDEO)
	key_frame = enc->coded_frame->key_frame;

    put_packetheader(nut, bc, size+(key_frame?16:8)+4); /* FIXME: estimation */

    if (key_frame)
	put_le64(bc, 1); /* FIXME: unique startcode */
    put_byte(bc, (key_frame ? 1<<5 : 0) + (1 << 1)); /* flags */
    put_v(bc, stream_index);
    put_s(bc, 0); /* lsb_timestamp */
    
    put_buffer(bc, buf, size);
    
    put_padding(nut, bc);

    put_flush_packet(bc);

    return 0;
}

static int nut_write_trailer(AVFormatContext *s)
{
    NUTContext *nut = s->priv_data;
    ByteIOContext *bc = &s->pb;
#if 0
    int i;

    /* WRITE INDEX */

    for (i = 0; s->nb_streams; i++)
    {
	put_packetheader(nut, bc, 64); /* FIXME: estimation */
	put_le64(bc, 1); /* FIXME: unique startcode */
	put_v(bc, s->streams[i]->id);
	put_v(bc, ...);
	put_padding(nut, bc);
	put_le32(bc, 0); /* FIXME: checksum */
    }
#endif

    put_flush_packet(bc);

    return 0;
}

static int nut_probe(AVProbeData *p)
{
    return AVPROBE_SCORE_MAX;
}

static int nut_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    NUTContext *nut = s->priv_data;
    ByteIOContext *bc = &s->pb;
    int tmp;
    int cur_stream, nb_streams;
    
    /* main header */
    get_packetheader(nut, bc);
    tmp = get_le64(bc);
    if (tmp != 1)
	fprintf(stderr, "damaged? startcode!=1 (%d)\n", tmp);
    
    tmp = get_v(bc);
    if (tmp != 0)
	fprintf(stderr, "bad version (%d)\n", tmp);
    
    nb_streams = get_v(bc);
    
    s->file_size = get_v(bc);
    s->duration = get_v(bc) / (AV_TIME_BASE / 1000);

    get_padding(nut, bc);
    get_le32(bc); /* checkusm */
    
    s->bit_rate = 0;
    
    /* stream header */
    for (cur_stream = 0; cur_stream < nb_streams; cur_stream++)
    {
	int class;
	AVStream *st;
	
	get_packetheader(nut, bc);
	tmp = get_le64(bc);
	if (tmp != 1)
	    fprintf(stderr, "damaged? startcode!=1 (%d)\n", tmp);
	st = av_new_stream(s, get_v(bc));
	if (!st)
	    return AVERROR_NOMEM;
	class = get_v(bc);
	switch(class)
	{
	    case 0:
		st->codec.codec_type = CODEC_TYPE_VIDEO;
//		get_v(bc);
//		tmp = get_le32(bc);
		get_b(bc, &tmp, 4);
		st->codec.codec_id = codec_get_bmp_id(tmp);
		if (st->codec.codec_id == CODEC_ID_NONE)
		    fprintf(stderr, "Unknown codec?!\n");
		break;
	    case 32:
		st->codec.codec_type = CODEC_TYPE_AUDIO;
//		tmp = get_v(bc);
//		tmp = get_le32(bc);
		get_b(bc, &tmp, 4);
		st->codec.codec_id = codec_get_wav_id(tmp);
		if (st->codec.codec_id == CODEC_ID_NONE)
		    fprintf(stderr, "Unknown codec?!\n");
		break;
	    default:
		fprintf(stderr, "Unknown stream class (%d)\n", class);
		return -1;
	}
	s->bit_rate += get_v(bc);
	tmp = get_v(bc); /* language code */
	while(tmp--)
	    get_byte(bc);
	st->codec.frame_rate_base = get_v(bc);
	st->codec.frame_rate = get_v(bc);
	get_v(bc); /* FIXME: msb timestamp base */
	get_v(bc); /* shuffle type */
	get_byte(bc); /* flags */
	
	get_v(bc); /* FIXME: codec specific data headers */
	
	if (class == 0) /* VIDEO */
	{
	    st->codec.width = get_v(bc);
	    st->codec.height = get_v(bc);
	    get_v(bc); /* aspected w */
	    get_v(bc); /* aspected h */
	    get_v(bc); /* csp type */
	    get_padding(nut, bc);
	    get_le32(bc); /* checksum */
	}
	if (class == 32) /* AUDIO */
	{
	    st->codec.sample_rate = get_v(bc) * (double)(st->codec.frame_rate_base / st->codec.frame_rate);
	    st->codec.channels = get_v(bc);
	    get_padding(nut, bc);
	    get_le32(bc); /* checksum */
	}
    }    
    
    return 0;
}

static int nut_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    NUTContext *nut = s->priv_data;
    ByteIOContext *bc = &s->pb;
    int tmp, id, timestamp, size;
    int key_frame = 0;

    get_packetheader(nut, bc);

    if (url_feof(bc))
	return -1;
    
    tmp = get_byte(bc);
    if ((tmp & 0x7f) == 1) /* zero bit set? */
    {
	tmp = get_le32(bc)+get_le16(bc)+get_byte(bc);
	if (!tmp)
	{
	    key_frame = 1;
	    tmp = get_byte(bc); /* flags */
	}
	else
	    fprintf(stderr, "error in zero bit / startcode\n");
    }
    if ((tmp & 0x9f) > 3) /* priority <= 3 */
	fprintf(stderr, "sanity check failed!\n");
    id = get_v(bc);
    timestamp = get_s(bc);
    
    size = (nut->curr_frame_size - (url_ftell(bc)-nut->curr_frame_start));
    dprintf("flags: 0x%x, timestamp: %d, packet size: %d\n", tmp, timestamp, size);
    
    if (size < 0)
	return -1;

    av_new_packet(pkt, size);
    get_buffer(bc, pkt->data, size);
    pkt->stream_index = id;
    if (key_frame)
	pkt->flags |= PKT_FLAG_KEY;
    pkt->pts = timestamp;

    return 0;
}

static AVInputFormat nut_iformat = {
    "nut",
    "nut format",
    sizeof(NUTContext),
    nut_probe,
    nut_read_header,
    nut_read_packet,
//    nut_read_close,
//    nut_read_seek,
    .extensions = "nut",
};

static AVOutputFormat nut_oformat = {
    "nut",
    "nut format",
    "video/x-nut",
    "nut",
    sizeof(NUTContext),
#ifdef CONFIG_VORBIS
    CODEC_ID_VORBIS,
#elif defined(CONFIG_MP3LAME)
    CODEC_ID_MP3LAME,
#else
    CODEC_ID_MP2,
#endif
    CODEC_ID_MPEG4,
    nut_write_header,
    nut_write_packet,
    nut_write_trailer,
};

int nut_init(void)
{
    av_register_input_format(&nut_iformat);
    av_register_output_format(&nut_oformat);
    return 0;
}
