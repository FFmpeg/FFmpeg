/*
 * YUV4MPEG format
 * Copyright (c) 2001, 2002, 2003 Fabrice Bellard.
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

#define Y4M_MAGIC "YUV4MPEG2"
#define Y4M_FRAME_MAGIC "FRAME"
#define Y4M_LINE_MAX 256

#ifdef CONFIG_ENCODERS

static int yuv4_generate_header(AVFormatContext *s, char* buf)
{
    AVStream *st;
    int width, height;
    int raten, rated, aspectn, aspectd, n;
    char inter;

    st = s->streams[0];
    width = st->codec.width;
    height = st->codec.height;

    av_reduce(&raten, &rated, st->codec.frame_rate, st->codec.frame_rate_base, (1UL<<31)-1);
    
    aspectn = st->codec.sample_aspect_ratio.num;
    aspectd = st->codec.sample_aspect_ratio.den;
    
    inter = 'p'; /* progressive is the default */
    if (st->codec.coded_frame && st->codec.coded_frame->interlaced_frame) {
        inter = st->codec.coded_frame->top_field_first ? 't' : 'b';
    }

    /* construct stream header, if this is the first frame */
    n = snprintf(buf, Y4M_LINE_MAX, "%s W%d H%d F%d:%d I%c A%d:%d%s\n",
                 Y4M_MAGIC,
                 width,
                 height,
                 raten, rated,
                 inter,
                 aspectn, aspectd,
		 (st->codec.pix_fmt == PIX_FMT_YUV411P) ? " C411 XYSCSS=411" : " C420mpeg2 XYSCSS=420MPEG2");
		 
    return n;
}

static int yuv4_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVStream *st = s->streams[pkt->stream_index];
    ByteIOContext *pb = &s->pb;
    AVPicture *picture;
    int* first_pkt = s->priv_data;
    int width, height;
    int i, m;
    char buf2[Y4M_LINE_MAX+1];
    char buf1[20];
    uint8_t *ptr, *ptr1, *ptr2;

    picture = (AVPicture *)pkt->data;

    /* for the first packet we have to output the header as well */
    if (*first_pkt) {
        *first_pkt = 0;
	if (yuv4_generate_header(s, buf2) < 0) {
	    av_log(s, AV_LOG_ERROR, "Error. YUV4MPEG stream header write failed.\n");
	    return AVERROR_IO;
	} else {
	    put_buffer(pb, buf2, strlen(buf2)); 
	}
    }

    /* construct frame header */
    
    m = snprintf(buf1, sizeof(buf1), "%s\n", Y4M_FRAME_MAGIC);
    put_buffer(pb, buf1, strlen(buf1));

    width = st->codec.width;
    height = st->codec.height;
    
    ptr = picture->data[0];
    for(i=0;i<height;i++) {
        put_buffer(pb, ptr, width);
        ptr += picture->linesize[0];
    }

    height >>= 1;
    width >>= 1;
    ptr1 = picture->data[1];
    ptr2 = picture->data[2];
    for(i=0;i<height;i++) {		/* Cb */
        put_buffer(pb, ptr1, width);
        ptr1 += picture->linesize[1];
    }
    for(i=0;i<height;i++) {	/* Cr */
        put_buffer(pb, ptr2, width);
            ptr2 += picture->linesize[2];
    }
    put_flush_packet(pb);
    return 0;
}

static int yuv4_write_header(AVFormatContext *s)
{
    int* first_pkt = s->priv_data;
    
    if (s->nb_streams != 1)
        return AVERROR_IO;
    
    if (s->streams[0]->codec.pix_fmt == PIX_FMT_YUV411P) {
        av_log(s, AV_LOG_ERROR, "Warning: generating non-standard 4:1:1 YUV stream, some mjpegtools might not work.\n");
    } 
    else if (s->streams[0]->codec.pix_fmt != PIX_FMT_YUV420P) {
        av_log(s, AV_LOG_ERROR, "ERROR: yuv4mpeg only handles 4:2:0, 4:1:1 YUV data. Use -pix_fmt to select one.\n");
	return AVERROR_IO;
    }
    
    *first_pkt = 1;
    return 0;
}

static int yuv4_write_trailer(AVFormatContext *s)
{
    return 0;
}

AVOutputFormat yuv4mpegpipe_oformat = {
    "yuv4mpegpipe",
    "YUV4MPEG pipe format",
    "",
    "yuv4mpeg",
    sizeof(int),
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    yuv4_write_header,
    yuv4_write_packet,
    yuv4_write_trailer,
    .flags = AVFMT_RAWPICTURE,
};
#endif //CONFIG_ENCODERS

/* Header size increased to allow room for optional flags */
#define MAX_YUV4_HEADER 80
#define MAX_FRAME_HEADER 10

static int yuv4_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    char header[MAX_YUV4_HEADER+1];
    int i;
    ByteIOContext *pb = &s->pb;
    int width, height, raten, rated, aspectn, aspectd;
    char lacing;
    AVStream *st;
    
    for (i=0; i<MAX_YUV4_HEADER; i++) {
        header[i] = get_byte(pb);
	if (header[i] == '\n') {
	    header[i+1] = 0;
	    break;
	}
    }
    if (i == MAX_YUV4_HEADER) return -1;
    if (strncmp(header, Y4M_MAGIC, strlen(Y4M_MAGIC))) return -1;
    sscanf(header+strlen(Y4M_MAGIC), " W%d H%d F%d:%d I%c A%d:%d",
           &width, &height, &raten, &rated, &lacing, &aspectn, &aspectd);
    
    st = av_new_stream(s, 0);
    st = s->streams[0];
    st->codec.width = width;
    st->codec.height = height;
    av_reduce(&raten, &rated, raten, rated, (1UL<<31)-1);
    st->codec.frame_rate = raten;
    st->codec.frame_rate_base = rated;
    st->codec.pix_fmt = PIX_FMT_YUV420P;
    st->codec.codec_type = CODEC_TYPE_VIDEO;
    st->codec.codec_id = CODEC_ID_RAWVIDEO;
    st->codec.sample_aspect_ratio= (AVRational){aspectn, aspectd};

    return 0;
}

static int yuv4_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int i;
    char header[MAX_FRAME_HEADER+1];
    int packet_size, ret, width, height;
    AVStream *st = s->streams[0];

    for (i=0; i<MAX_FRAME_HEADER; i++) {
        header[i] = get_byte(&s->pb);
	if (header[i] == '\n') {
	    header[i+1] = 0;
	    break;
	}
    }
    if (i == MAX_FRAME_HEADER) return -1;
    if (strncmp(header, Y4M_FRAME_MAGIC, strlen(Y4M_FRAME_MAGIC))) return -1;
    
    width = st->codec.width;
    height = st->codec.height;

    packet_size = avpicture_get_size(st->codec.pix_fmt, width, height);
    if (packet_size < 0)
        av_abort();

    if (av_new_packet(pkt, packet_size) < 0)
        return AVERROR_IO;

    pkt->stream_index = 0;
    ret = get_buffer(&s->pb, pkt->data, pkt->size);
    if (ret != pkt->size) {
        av_free_packet(pkt);
        return AVERROR_IO;
    } else {
        return 0;
    }
}

static int yuv4_read_close(AVFormatContext *s)
{
    return 0;
}

AVInputFormat yuv4mpegpipe_iformat = {
    "yuv4mpegpipe",
    "YUV4MPEG pipe format",
    0,
    NULL,
    yuv4_read_header,
    yuv4_read_packet,
    yuv4_read_close,
    .extensions = "yuv4mpeg"
};

int yuv4mpeg_init(void)
{
    av_register_input_format(&yuv4mpegpipe_iformat);
#ifdef CONFIG_ENCODERS
    av_register_output_format(&yuv4mpegpipe_oformat);
#endif //CONFIG_ENCODERS
    return 0;
}

