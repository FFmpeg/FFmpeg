/* 
 * Raw DV format
 * Copyright (c) 2002 Fabrice Bellard.
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

#define NTSC_FRAME_SIZE 120000
#define PAL_FRAME_SIZE  144000

typedef struct DVDemuxContext {
    int       is_audio;
    uint8_t   buf[PAL_FRAME_SIZE];
    int       size;
} DVDemuxContext;

/* raw input */
static int dv_read_header(AVFormatContext *s,
                          AVFormatParameters *ap)
{
    AVStream *vst, *ast;
    DVDemuxContext *c = s->priv_data;

    vst = av_new_stream(s, 0);
    if (!vst)
        return AVERROR_NOMEM;
    vst->codec.codec_type = CODEC_TYPE_VIDEO;
    vst->codec.codec_id = CODEC_ID_DVVIDEO;


    ast = av_new_stream(s, 1);
    if (!ast)
        return AVERROR_NOMEM;

    ast->codec.codec_type = CODEC_TYPE_AUDIO;
    ast->codec.codec_id = CODEC_ID_DVAUDIO;
    c->is_audio = 0;

    return 0;
}

static void __destruct_pkt(struct AVPacket *pkt)
{
    pkt->data = NULL; pkt->size = 0;
    return;
}

static int dv_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, dsf;
    DVDemuxContext *c = s->priv_data;
    
    if (!c->is_audio) {
        ret = get_buffer(&s->pb, c->buf, 4);
        if (ret <= 0) 
            return -EIO;
        dsf = c->buf[3] & 0x80;
        if (!dsf)
            c->size = NTSC_FRAME_SIZE;
        else
            c->size = PAL_FRAME_SIZE;
	
	ret = get_buffer(&s->pb, c->buf + 4, c->size - 4);
	if (ret <= 0)
	    return -EIO;
    }
    
    av_init_packet(pkt);
    pkt->destruct = __destruct_pkt;
    pkt->data     = c->buf;
    pkt->size     = c->size;
    pkt->stream_index = c->is_audio;
    
    c->is_audio = !c->is_audio;
    return c->size;
}

static int dv_read_close(AVFormatContext *s)
{
    return 0;
}

static AVInputFormat dv_iformat = {
    "dv",
    "DV video format",
    sizeof(DVDemuxContext),
    NULL,
    dv_read_header,
    dv_read_packet,
    dv_read_close,
    .extensions = "dv",
};


int dv_write_header(struct AVFormatContext *s)
{
    return 0;
}

int dv_write_packet(struct AVFormatContext *s, 
                     int stream_index,
                     unsigned char *buf, int size, int force_pts)
{
    put_buffer(&s->pb, buf, size);
    put_flush_packet(&s->pb);
    return 0;
}

int dv_write_trailer(struct AVFormatContext *s)
{
    return 0;
}

AVOutputFormat dv_oformat = {
    "dv",
    "DV video format",
    NULL,
    "dv",
    0,
    CODEC_ID_DVVIDEO,
    CODEC_ID_DVAUDIO,
    dv_write_header,
    dv_write_packet,
    dv_write_trailer,
};

int dv_init(void)
{
    av_register_input_format(&dv_iformat);
    av_register_output_format(&dv_oformat);
    return 0;
}
