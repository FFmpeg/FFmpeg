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
#include "dvcore.h"

typedef struct DVDemuxContext {
    int       is_audio;
    uint8_t   buf[144000];    
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
    vst->codec.bit_rate = 25000000;

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
    int ret;
    DVDemuxContext *c = s->priv_data;
    
    if (!c->is_audio) {
        ret = get_buffer(&s->pb, c->buf, 4);
        if (ret <= 0) 
            return -EIO;
	c->size = dv_frame_profile(&c->buf[0])->frame_size;
	
	ret = get_buffer(&s->pb, c->buf + 4, c->size - 4);
	if (ret <= 0)
	    return -EIO;
    }
    
    av_init_packet(pkt);
    pkt->destruct = __destruct_pkt;
    pkt->data     = c->buf;
    pkt->size     = c->size;
    pkt->stream_index = c->is_audio;
    pkt->flags   |= PKT_FLAG_KEY;
    
    c->is_audio = !c->is_audio;
    return c->size;
}

static int dv_read_close(AVFormatContext *s)
{
    return 0;
}

int dv_write_header(struct AVFormatContext *s)
{
    DVMuxContext *c = s->priv_data;
    
    if (s->nb_streams != 2 || dv_core_init(c, s->streams) != 0) {
        fprintf(stderr, "Can't initialize DV format!\n"
		    "Make sure that you supply exactly two streams:\n"
		    "     video: 25fps or 29.97fps, audio: 2ch/48Khz/PCM\n");
	return -1;
    }
    return 0;
}

int dv_write_packet(struct AVFormatContext *s, 
                     int stream_index,
                     unsigned char *buf, int size, int force_pts)
{
    DVMuxContext *c = s->priv_data;
   
    if (stream_index == c->vst)
        dv_assemble_frame(c, buf, NULL, 0);
    else
        dv_assemble_frame(c, NULL, buf, size);
   
    if (c->has_audio && c->has_video) {
        put_buffer(&s->pb, &c->frame_buf[0], c->sys->frame_size);
        put_flush_packet(&s->pb);
    }
    
    return 0;
}

/* 
 * We might end up with some extra A/V data without matching counterpart.
 * E.g. video data without enough audio to write the complete frame.
 * Currently we simply drop the last frame. I don't know whether this 
 * is the best strategy of all
 */
int dv_write_trailer(struct AVFormatContext *s)
{
    dv_core_delete((DVMuxContext *)s->priv_data);
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

AVOutputFormat dv_oformat = {
    "dv",
    "DV video format",
    NULL,
    "dv",
    sizeof(DVMuxContext),
    CODEC_ID_PCM_S16LE,
    CODEC_ID_DVVIDEO,
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
