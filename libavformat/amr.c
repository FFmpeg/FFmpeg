/* 
 * amr file format
 * Copyright (c) 2001 ffmpeg project
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
Write and read amr data according to RFC3267, http://www.ietf.org/rfc/rfc3267.txt?number=3267

Only mono files are supported.

*/
#include "avformat.h"

static const unsigned char AMR_header [] = "#!AMR\n";
static const unsigned char AMRWB_header [] = "#!AMR-WB\n";

static int amr_write_header(AVFormatContext *s)
{
    ByteIOContext *pb = &s->pb;
    AVCodecContext *enc = &s->streams[0]->codec;

    s->priv_data = NULL;

    if (enc->codec_id == CODEC_ID_AMR_NB)
    {
        put_tag(pb, AMR_header);       /* magic number */
    }
    else if(enc->codec_id == CODEC_ID_AMR_WB)
    {
        put_tag(pb, AMRWB_header);       /* magic number */
    }
    else
    {
        //This is an error!
    }
    put_flush_packet(pb);
    return 0;
}

static int amr_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    put_buffer(&s->pb, pkt->data, pkt->size);
    put_flush_packet(&s->pb);
    return 0;
}

static int amr_write_trailer(AVFormatContext *s)
{
    return 0;
}

static int amr_probe(AVProbeData *p)
{
    //Only check for "#!AMR" which could be amr-wb, amr-nb. 
    //This will also trigger multichannel files: "#!AMR_MC1.0\n" and 
    //"#!AMR-WB_MC1.0\n" (not supported)

    if (p->buf_size < 5)
        return 0;
    if(memcmp(p->buf,AMR_header,5)==0)
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

/* amr input */
static int amr_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    ByteIOContext *pb = &s->pb;
    AVStream *st;
    uint8_t header[9];

    get_buffer(pb, header, 6);

    if(memcmp(header,AMR_header,6)!=0)
    {
        get_buffer(pb, header+6, 3);
        if(memcmp(header,AMRWB_header,9)!=0)
        {
            return -1;
        }
        st = av_new_stream(s, 0);
        if (!st)
        {
            return AVERROR_NOMEM;
        }
    
        st->codec.codec_type = CODEC_TYPE_AUDIO;
        st->codec.codec_tag = CODEC_ID_AMR_WB;
        st->codec.codec_id = CODEC_ID_AMR_WB;
        st->codec.channels = 1;
        st->codec.sample_rate = 16000;
    }
    else
    {
        st = av_new_stream(s, 0);
        if (!st)
        {
            return AVERROR_NOMEM;
        }
    
        st->codec.codec_type = CODEC_TYPE_AUDIO;
        st->codec.codec_tag = CODEC_ID_AMR_NB;
        st->codec.codec_id = CODEC_ID_AMR_NB;
        st->codec.channels = 1;
        st->codec.sample_rate = 8000;
    }

    return 0;
}

#define MAX_SIZE 32

static int amr_read_packet(AVFormatContext *s,
                          AVPacket *pkt)
{
    AVCodecContext *enc = &s->streams[0]->codec;

    if (enc->codec_id == CODEC_ID_AMR_NB)
    {
        const static uint8_t packed_size[16] = {12, 13, 15, 17, 19, 20, 26, 31, 5, 0, 0, 0, 0, 0, 0, 0};
        uint8_t toc, q, ft;
        int read;
        int size;
    
        if (url_feof(&s->pb))
        {
            return AVERROR_IO;
        }
    
        toc=get_byte(&s->pb);
        q  = (toc >> 2) & 0x01;
        ft = (toc >> 3) & 0x0F;
    
        size=packed_size[ft];
    
        if (av_new_packet(pkt, size+1))
        {
            return AVERROR_IO;
        }
        pkt->stream_index = 0;
        
        pkt->data[0]=toc;
    
        read = get_buffer(&s->pb, pkt->data+1, size);
    
        if (read != size)
        {
            av_free_packet(pkt);
            return AVERROR_IO;
        }
    
        return 0;
    }
    else if(enc->codec_id == CODEC_ID_AMR_WB)
    {
        static uint8_t packed_size[16] = {18, 24, 33, 37, 41, 47, 51, 59, 61, 6, 6, 0, 0, 0, 1, 1};
        uint8_t toc, mode;
        int read;
        int size;
    
        if (url_feof(&s->pb))
        {
            return AVERROR_IO;
        }
    
        toc=get_byte(&s->pb);
        mode = (uint8_t)((toc >> 3) & 0x0F);
        size = packed_size[mode];
    
        if ( (size==0) || av_new_packet(pkt, size))
        {
            return AVERROR_IO;
        }
    
        pkt->stream_index = 0;
        pkt->data[0]=toc;
    
        read = get_buffer(&s->pb, pkt->data+1, size-1);
    
        if (read != (size-1))
        {
            av_free_packet(pkt);
            return AVERROR_IO;
        }
    
        return 0;
    }
    else
    {
        return AVERROR_IO;
    }
}

static int amr_read_close(AVFormatContext *s)
{
    return 0;
}

static AVInputFormat amr_iformat = {
    "amr",
    "3gpp amr file format",
    0, /*priv_data_size*/
    amr_probe,
    amr_read_header,
    amr_read_packet,
    amr_read_close,
};

static AVOutputFormat amr_oformat = {
    "amr",
    "3gpp amr file format",
    "audio/amr",
    "amr",
    0,
    CODEC_ID_AMR_NB,
    CODEC_ID_NONE,
    amr_write_header,
    amr_write_packet,
    amr_write_trailer,
};

int amr_init(void)
{
    av_register_input_format(&amr_iformat);
    av_register_output_format(&amr_oformat);
    return 0;
}
