/*
 * FLV encoder.
 * Copyright (c) 2003 The FFmpeg Project.
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

unsigned int get_be24(ByteIOContext *s)
{
    unsigned int val;
    val = get_byte(s) << 16;
    val |= get_byte(s) << 8;
    val |= get_byte(s);
    return val;
}

static int flv_probe(AVProbeData *p)
{
    const uint8_t *d;

    if (p->buf_size < 6)
        return 0;
    d = p->buf;
    if (d[0] == 'F' && d[1] == 'L' && d[2] == 'V') {
        return 50;
    }
    return 0;
}

static int flv_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    int offset, flags;
    AVStream *st;
    
    s->ctx_flags |= AVFMTCTX_NOHEADER; //ok we have a header but theres no fps, codec type, sample_rate, ...

    url_fskip(&s->pb, 4);
    flags = get_byte(&s->pb);

    offset = get_be32(&s->pb);
    url_fseek(&s->pb, offset, SEEK_SET);

    return 0;
}

static int flv_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, i, type, size, pts, flags, is_audio;
    AVStream *st;
    
 for(;;){
    url_fskip(&s->pb, 4); /* size of previous packet */
    type = get_byte(&s->pb);
    size = get_be24(&s->pb);
    pts = get_be24(&s->pb);
//    av_log(s, AV_LOG_DEBUG, "type:%d, size:%d, pts:%d\n", type, size, pts);
    if (url_feof(&s->pb))
        return AVERROR_IO;
    url_fskip(&s->pb, 4); /* reserved */
    flags = 0;
    
    if(size == 0)
        continue;
    
    if (type == 8) {
        is_audio=1;
        flags = get_byte(&s->pb);
        size--;
    } else if (type == 9) {
        is_audio=0;
        flags = get_byte(&s->pb);
        size--;
    } else {
        /* skip packet */
        av_log(s, AV_LOG_ERROR, "skipping flv packet: type %d, size %d, flags %d\n", type, size, flags);
        url_fskip(&s->pb, size);
        continue;
    }

    /* now find stream */
    for(i=0;i<s->nb_streams;i++) {
        st = s->streams[i];
        if (st->id == is_audio)
            break;
    }
    if(i == s->nb_streams){
        st = av_new_stream(s, is_audio);
        if (!st)
            return AVERROR_NOMEM;

        av_set_pts_info(st, 24, 1, 1000); /* 24 bit pts in ms */
        st->codec.frame_rate_base= 0;
    }
    break;
 }

    if(is_audio){
        if(st->codec.sample_rate == 0){
            st->codec.codec_type = CODEC_TYPE_AUDIO;
            st->codec.channels = (flags&1)+1;
            if((flags >> 4) == 5)
                st->codec.sample_rate= 8000;
            else
                st->codec.sample_rate = (44100<<((flags>>2)&3))>>3;
            switch(flags >> 4){/* 0: uncompressed 1: ADPCM 2: mp3 5: Nellymoser 8kHz mono 6: Nellymoser*/
            case 2: st->codec.codec_id = CODEC_ID_MP3; break;
            default:
                st->codec.codec_tag= (flags >> 4);
            }
        }
    }else{
        if(st->codec.frame_rate_base == 0){
            st->codec.codec_type = CODEC_TYPE_VIDEO;
            //guess the frame rate
            if(pts){
                st->codec.frame_rate_base=1;
                st->codec.frame_rate= (1000 + pts/2)/pts;
            }
            switch(flags & 0xF){
            case 2: st->codec.codec_id = CODEC_ID_FLV1; break;
            default:
                st->codec.codec_tag= flags & 0xF;
            }
        }
    }

    if (av_new_packet(pkt, size) < 0)
        return AVERROR_IO;

    ret = get_buffer(&s->pb, pkt->data, size);
    if (ret <= 0) {
        av_free_packet(pkt);
        return AVERROR_IO;
    }
    /* note: we need to modify the packet size here to handle the last
       packet */
    pkt->size = ret;
    pkt->pts = pts;
    pkt->stream_index = st->index;
    return ret;
}

static int flv_read_close(AVFormatContext *s)
{
    return 0;
}

AVInputFormat flv_iformat = {
    "flv",
    "flv format",
    0,
    flv_probe,
    flv_read_header,
    flv_read_packet,
    flv_read_close,
    .extensions = "flv",
    .value = CODEC_ID_FLV1,
};

int flvdec_init(void)
{
    av_register_input_format(&flv_iformat);
    return 0;
}
