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
 * - correct timestamp handling
 * - index writing
 * - info and index packet reading support
 * - startcode searching for broken streams
 * - subpacket support
 * - handling of codec specific headers
*/

//#define DEBUG 1

#include "avformat.h"
#include "mpegaudio.h"
#include "avi.h"

//from /dev/random

#define     MAIN_STARTCODE (0xF9526A6200000000ULL + ('N'<<24) + ('U'<<16) + ('T'<<8) + 'M')
#define   STREAM_STARTCODE (0xD667773F00000000ULL + ('N'<<24) + ('U'<<16) + ('T'<<8) + 'S')
#define KEYFRAME_STARTCODE (0xCB86308700000000ULL + ('N'<<24) + ('U'<<16) + ('T'<<8) + 'K')
#define    INDEX_STARTCODE (0xEBFCDE0E00000000ULL + ('N'<<24) + ('U'<<16) + ('T'<<8) + 'X')
#define     INFO_STARTCODE (0xA37B643500000000ULL + ('N'<<24) + ('U'<<16) + ('T'<<8) + 'I')

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
    /* skip remaining bytes */
    for (; i < len; i++)
        get_byte(bc);

    return 0;
}

static int get_bi(ByteIOContext *bc)
{
   int i, len, val = 0;
    
    len = get_v(bc);
    for (i = 0; i < len && i <= 4; i++)
        val |= get_byte(bc) << (i * 8);
    /* skip remaining bytes */
    for (; i < len; i++)
        get_byte(bc);

    return val;
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

/**
 * 
 */
static int get_length(uint64_t val){
    int i;

    for (i=7; ; i+=7)
	if ((val>>i) == 0)
	    return i;

    return 7; //not reached
}

static int put_v(ByteIOContext *bc, uint64_t val)
{
    int i;

//    if (bytes_left(s)*8 < 9)
//	return -1;

    if (bytes_left(bc) < 1)
	return -1;

    val &= 0x7FFFFFFFFFFFFFFFULL; // FIXME can only encode upto 63 bits currently
    i= get_length(val);

    for (i-=7; i>0; i-=7){
	put_byte(bc, 0x80 | (val>>i));
    }

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

static int put_bi(ByteIOContext *bc, int val)
{
    put_v(bc, 4);
    put_le32(bc, val);
    return 0;
}

static int put_packetheader(NUTContext *nut, ByteIOContext *bc, int max_size)
{
    put_flush_packet(bc);
    nut->curr_frame_start = url_ftell(bc);
    nut->curr_frame_size = max_size;
    
    /* packet header */
    put_v(bc, nut->curr_frame_size); /* forward ptr */
    put_v(bc, nut->last_frame_size); /* backward ptr */
    dprintf("Packet: fwd: %d, bwd: %d\n",
	nut->curr_frame_size, nut->last_frame_size);

    nut->last_frame_size = nut->curr_frame_size;
    
    return 0;
}

static int update_packetheader(NUTContext *nut, ByteIOContext *bc, int additional_size){
    offset_t start= nut->curr_frame_start;
    offset_t cur= url_ftell(bc);
    int size= cur - start + additional_size;
    
    assert( size <= nut->curr_frame_size );
    
    url_fseek(bc, start, SEEK_SET);
    put_v(bc, size);
    if(get_length(size) < get_length(nut->curr_frame_size))
        put_byte(bc, 0x80);
    nut->curr_frame_size= size;
    dprintf("Packet update: size: %d\n", size);

    url_fseek(bc, cur, SEEK_SET);    
    
    return 0;
}

static int nut_write_header(AVFormatContext *s)
{
    NUTContext *nut = s->priv_data;
    ByteIOContext *bc = &s->pb;
    AVCodecContext *codec;
    int i;

    /* main header */
    put_be64(bc, MAIN_STARTCODE);
    put_packetheader(nut, bc, 120);
    put_v(bc, 0); /* version */
    put_v(bc, s->nb_streams);
    put_be32(bc, 0); /* FIXME: checksum */
    
    update_packetheader(nut, bc, 0);
    
    /* stream headers */
    for (i = 0; i < s->nb_streams; i++)
    {
	int nom, denom;

	codec = &s->streams[i]->codec;
	
	put_be64(bc, STREAM_STARTCODE);
	put_packetheader(nut, bc, 120);
	put_v(bc, i /*s->streams[i]->index*/);
	put_v(bc, (codec->codec_type == CODEC_TYPE_AUDIO) ? 32 : 0);
	if (codec->codec_tag)
	    put_bi(bc, codec->codec_tag);
	else if (codec->codec_type == CODEC_TYPE_VIDEO)
	{
	    int tmp = codec_get_bmp_tag(codec->codec_id);
	    put_bi(bc, tmp);
	    nom = codec->frame_rate;
	    denom = codec->frame_rate_base;
	}
	else if (codec->codec_type == CODEC_TYPE_AUDIO)
	{
	    int tmp = codec_get_wav_tag(codec->codec_id);
	    put_bi(bc, tmp);
	    nom = codec->sample_rate/8;
	    denom = 8;
	}
	put_v(bc, codec->bit_rate);
	put_v(bc, 0); /* no language code */
	put_v(bc, nom);
	put_v(bc, denom);
	put_v(bc, 0); /* msb timestamp_shift */
	put_v(bc, 0); /* shuffle type */
	put_byte(bc, 0); /* flags: 0x1 - fixed_fps, 0x2 - index_present */
	
	put_v(bc, 0); /* no codec specific headers */
	
	switch(codec->codec_type)
	{
	    case CODEC_TYPE_AUDIO:
		put_v(bc, (codec->sample_rate * denom) / nom);
		put_v(bc, codec->channels);
		put_be32(bc, 0); /* FIXME: checksum */
		break;
	    case CODEC_TYPE_VIDEO:
		put_v(bc, codec->width);
		put_v(bc, codec->height);
		put_v(bc, 0); /* aspected w */
		put_v(bc, 0); /* aspected h */
		put_v(bc, 0); /* csp type -- unknown */
		put_be32(bc, 0); /* FIXME: checksum */
		break;
            default:
                break;
	}
        update_packetheader(nut, bc, 0);
    }

#if 0
    /* info header */
    put_be64(bc, INFO_STARTCODE);
    put_packetheader(nut, bc, 16+strlen(s->author)+strlen(s->title)+
        strlen(s->comment)+strlen(s->copyright)); 
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
    put_b(bc, LIBAVFORMAT_IDENT "\0", strlen(LIBAVFORMAT_IDENT));
    
    put_v(bc, 0); /* eof info */

    put_be32(bc, 0); /* FIXME: checksum */
    update_packetheader(nut, bc, 0);
#endif
        
    put_flush_packet(bc);
    
    return 0;
}

static int nut_write_packet(AVFormatContext *s, int stream_index, 
			    const uint8_t *buf, int size, int64_t pts)
{
    NUTContext *nut = s->priv_data;
    ByteIOContext *bc = &s->pb;
    int key_frame = 0;
    int flags;
    AVCodecContext *enc;

    if (stream_index > s->nb_streams)
	return 1;

    enc = &s->streams[stream_index]->codec;
    key_frame = enc->coded_frame->key_frame;

    if (key_frame)
	put_be64(bc, KEYFRAME_STARTCODE);
    
    flags=0;
    flags<<=2; flags|=1; //priority
    flags<<=1; flags|=0; //checksum
    flags<<=1; flags|=0; //msb_timestamp_flag
    flags<<=2; flags|=1; //subpacket_type
    flags<<=1; flags|=0; //reserved

    put_byte(bc, flags);

    put_packetheader(nut, bc, size+20);
    put_v(bc, stream_index);
    put_s(bc, pts); /* lsb_timestamp */
    update_packetheader(nut, bc, size);
    
    put_buffer(bc, buf, size);
    
    put_flush_packet(bc);

    return 0;
}

static int nut_write_trailer(AVFormatContext *s)
{
    ByteIOContext *bc = &s->pb;
#if 0
    int i;

    /* WRITE INDEX */

    for (i = 0; s->nb_streams; i++)
    {
	put_be64(bc, INDEX_STARTCODE);
	put_packetheader(nut, bc, 64);
	put_v(bc, s->streams[i]->id);
	put_v(bc, ...);
	put_be32(bc, 0); /* FIXME: checksum */
        update_packetheader(nut, bc, 0);
    }
#endif

    put_flush_packet(bc);

    return 0;
}

static int nut_probe(AVProbeData *p)
{
    int i;
    uint64_t code;

    code = 0xff;
    for (i = 0; i < p->buf_size; i++) {
        int c = p->buf[i];
        code = (code << 8) | c;
        if (code == MAIN_STARTCODE)
            return AVPROBE_SCORE_MAX;
    }
    return 0;
}

static int nut_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    NUTContext *nut = s->priv_data;
    ByteIOContext *bc = &s->pb;
    uint64_t tmp;
    int cur_stream, nb_streams;
    
    /* main header */
    tmp = get_be64(bc);
    if (tmp != MAIN_STARTCODE)
	fprintf(stderr, "damaged? startcode!=1 (%Ld)\n", tmp);
    get_packetheader(nut, bc);
    
    tmp = get_v(bc);
    if (tmp != 0)
	fprintf(stderr, "bad version (%Ld)\n", tmp);
    
    nb_streams = get_v(bc);
    get_be32(bc); /* checkusm */
    
    s->bit_rate = 0;
    
    /* stream header */
    for (cur_stream = 0; cur_stream < nb_streams; cur_stream++)
    {
	int class, nom, denom;
	AVStream *st;
	
	tmp = get_be64(bc);
	if (tmp != STREAM_STARTCODE)
	    fprintf(stderr, "damaged? startcode!=1 (%Ld)\n", tmp);
	get_packetheader(nut, bc);
	st = av_new_stream(s, get_v(bc));
	if (!st)
	    return AVERROR_NOMEM;
	class = get_v(bc);
	tmp = get_bi(bc);
	switch(class)
	{
	    case 0:
		st->codec.codec_type = CODEC_TYPE_VIDEO;
		st->codec.codec_id = codec_get_bmp_id(tmp);
		if (st->codec.codec_id == CODEC_ID_NONE)
		    fprintf(stderr, "Unknown codec?!\n");
		break;
	    case 32:
		st->codec.codec_type = CODEC_TYPE_AUDIO;
		st->codec.codec_id = codec_get_wav_id(tmp);
		if (st->codec.codec_id == CODEC_ID_NONE)
		    fprintf(stderr, "Unknown codec?!\n");
		break;
	    default:
		fprintf(stderr, "Unknown stream class (%d)\n", class);
		return -1;
	}
	s->bit_rate += get_v(bc);
	get_b(bc, NULL, 0); /* language code */
	nom = get_v(bc);
	denom = get_v(bc);
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
	    get_be32(bc); /* checksum */

	    st->codec.frame_rate = nom;
	    st->codec.frame_rate_base = denom;
	}
	if (class == 32) /* AUDIO */
	{
	    st->codec.sample_rate = (get_v(bc) * nom) / denom;
	    st->codec.channels = get_v(bc);
	    get_be32(bc); /* checksum */
	}
    }    
    
    return 0;
}

static int nut_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    NUTContext *nut = s->priv_data;
    ByteIOContext *bc = &s->pb;
    int id, timestamp, size;
    int key_frame = 0;
    uint64_t tmp;


    if (url_feof(bc))
	return -1;
    
    tmp = get_byte(bc);
    if (tmp & 0x80) /* zero bit set? */
    {
	tmp<<=8 ; tmp |= get_byte(bc);
	tmp<<=16; tmp |= get_be16(bc);
	tmp<<=32; tmp |= get_be32(bc);
	if (tmp == KEYFRAME_STARTCODE)
	{
	    key_frame = 1;
	    tmp = get_byte(bc); /* flags */
	}
	else
	    fprintf(stderr, "error in zero bit / startcode %LX\n", tmp);
    }
    get_packetheader(nut, bc);
#if 0
    if (((tmp & 0x60)>>5) > 3) /* priority <= 3 */
	fprintf(stderr, "sanity check failed!\n");
#endif
    id = get_v(bc);
    timestamp = get_s(bc);
    
    size = (nut->curr_frame_size - (url_ftell(bc)-nut->curr_frame_start));
    dprintf("flags: 0x%Lx, timestamp: %d, packet size: %d\n", tmp, timestamp, size);
    
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
    CODEC_ID_MP3,
#else
    CODEC_ID_MP2, /* AC3 needs liba52 decoder */
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
