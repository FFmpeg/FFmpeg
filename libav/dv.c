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
    int is_audio;
} DVDemuxContext;

/* raw input */
static int dv_read_header(AVFormatContext *s,
                          AVFormatParameters *ap)
{
    AVStream *vst, *ast;

    vst = av_new_stream(s, 0);
    if (!vst)
        return AVERROR_NOMEM;
    vst->codec.codec_type = CODEC_TYPE_VIDEO;
    vst->codec.codec_id = CODEC_ID_DVVIDEO;

#if 0
    ast = av_new_stream(s, 1);
    if (!ast)
        return AVERROR_NOMEM;

    ast->codec.codec_type = CODEC_TYPE_AUDIO;
    ast->codec.codec_id = CODEC_ID_DVAUDIO;
#endif
    return 0;
}

/* XXX: build fake audio stream when DV audio decoder will be finished */
int dv_read_packet(AVFormatContext *s,
                   AVPacket *pkt)
{
    int ret, size, dsf;
    uint8_t buf[4];
    
    ret = get_buffer(&s->pb, buf, 4);
    if (ret <= 0) 
        return -EIO;
    dsf = buf[3] & 0x80;
    if (!dsf)
        size = NTSC_FRAME_SIZE;
    else
        size = PAL_FRAME_SIZE;
    
    if (av_new_packet(pkt, size) < 0)
        return -EIO;

    pkt->stream_index = 0;
    memcpy(pkt->data, buf, 4);
    ret = get_buffer(&s->pb, pkt->data + 4, size - 4);
    if (ret <= 0) {
        av_free_packet(pkt);
        return -EIO;
    }
    return ret;
}

int dv_read_close(AVFormatContext *s)
{
    return 0;
}

AVInputFormat dv_iformat = {
    "dv",
    "DV video format",
    sizeof(DVDemuxContext),
    NULL,
    dv_read_header,
    dv_read_packet,
    dv_read_close,
    .extensions = "dv",
};

#if 0
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
#endif

int dv_init(void)
{
    av_register_input_format(&dv_iformat);
    //    av_register_output_format(&dv_oformat);
    return 0;
}
