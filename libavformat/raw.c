/* 
 * RAW encoder and decoder
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
#include "avformat.h"

#ifdef CONFIG_ENCODERS
/* simple formats */
static int raw_write_header(struct AVFormatContext *s)
{
    return 0;
}

static int raw_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    put_buffer(&s->pb, pkt->data, pkt->size);
    put_flush_packet(&s->pb);
    return 0;
}

static int raw_write_trailer(struct AVFormatContext *s)
{
    return 0;
}
#endif //CONFIG_ENCODERS

/* raw input */
static int raw_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    AVStream *st;
    int id;

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR_NOMEM;
    if (ap) {
        id = s->iformat->value;
        if (id == CODEC_ID_RAWVIDEO) {
            st->codec.codec_type = CODEC_TYPE_VIDEO;
        } else {
            st->codec.codec_type = CODEC_TYPE_AUDIO;
        }
        st->codec.codec_id = id;

        switch(st->codec.codec_type) {
        case CODEC_TYPE_AUDIO:
            st->codec.sample_rate = ap->sample_rate;
            st->codec.channels = ap->channels;
            break;
        case CODEC_TYPE_VIDEO:
            st->codec.frame_rate      = ap->frame_rate;
            st->codec.frame_rate_base = ap->frame_rate_base;
            st->codec.width = ap->width;
            st->codec.height = ap->height;
	    st->codec.pix_fmt = ap->pix_fmt;
            break;
        default:
            return -1;
        }
    } else {
        return -1;
    }
    return 0;
}

#define RAW_PACKET_SIZE 1024

static int raw_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, size;
    //    AVStream *st = s->streams[0];
    
    size= RAW_PACKET_SIZE;

    if (av_new_packet(pkt, size) < 0)
        return AVERROR_IO;

    pkt->stream_index = 0;
    ret = get_buffer(&s->pb, pkt->data, size);
    if (ret <= 0) {
        av_free_packet(pkt);
        return AVERROR_IO;
    }
    /* note: we need to modify the packet size here to handle the last
       packet */
    pkt->size = ret;
    return ret;
}

static int raw_read_partial_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, size;

    size = RAW_PACKET_SIZE;

    if (av_new_packet(pkt, size) < 0)
        return AVERROR_IO;

    pkt->stream_index = 0;
    ret = get_partial_buffer(&s->pb, pkt->data, size);
    if (ret <= 0) {
        av_free_packet(pkt);
        return AVERROR_IO;
    }
    pkt->size = ret;
    return ret;
}

static int raw_read_close(AVFormatContext *s)
{
    return 0;
}

int pcm_read_seek(AVFormatContext *s, 
                  int stream_index, int64_t timestamp)
{
    AVStream *st;
    int block_align, byte_rate;
    int64_t pos;

    st = s->streams[0];
    switch(st->codec.codec_id) {
    case CODEC_ID_PCM_S16LE:
    case CODEC_ID_PCM_S16BE:
    case CODEC_ID_PCM_U16LE:
    case CODEC_ID_PCM_U16BE:
        block_align = 2 * st->codec.channels;
        byte_rate = block_align * st->codec.sample_rate;
        break;
    case CODEC_ID_PCM_S8:
    case CODEC_ID_PCM_U8:
    case CODEC_ID_PCM_MULAW:
    case CODEC_ID_PCM_ALAW:
        block_align = st->codec.channels;
        byte_rate = block_align * st->codec.sample_rate;
        break;
    default:
        block_align = st->codec.block_align;
        byte_rate = st->codec.bit_rate / 8;
        break;
    }
    
    if (block_align <= 0 || byte_rate <= 0)
        return -1;

    /* compute the position by aligning it to block_align */
    pos = av_rescale(timestamp * byte_rate, st->time_base.num, st->time_base.den);
    pos = (pos / block_align) * block_align;

    /* recompute exact position */
    st->cur_dts = av_rescale(pos, st->time_base.den, byte_rate * (int64_t)st->time_base.num);
    url_fseek(&s->pb, pos + s->data_offset, SEEK_SET);
    return 0;
}

/* ac3 read */
static int ac3_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    AVStream *st;

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR_NOMEM;

    st->codec.codec_type = CODEC_TYPE_AUDIO;
    st->codec.codec_id = CODEC_ID_AC3;
    st->need_parsing = 1;
    /* the parameters will be extracted from the compressed bitstream */
    return 0;
}

/* dts read */
static int dts_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    AVStream *st;

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR_NOMEM;

    st->codec.codec_type = CODEC_TYPE_AUDIO;
    st->codec.codec_id = CODEC_ID_DTS;
    st->need_parsing = 1;
    /* the parameters will be extracted from the compressed bitstream */
    return 0;
}

/* mpeg1/h263 input */
static int video_read_header(AVFormatContext *s,
                             AVFormatParameters *ap)
{
    AVStream *st;

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR_NOMEM;

    st->codec.codec_type = CODEC_TYPE_VIDEO;
    st->codec.codec_id = s->iformat->value;
    st->need_parsing = 1;

    /* for mjpeg, specify frame rate */
    /* for mpeg4 specify it too (most mpeg4 streams dont have the fixed_vop_rate set ...)*/
    if (st->codec.codec_id == CODEC_ID_MJPEG || 
        st->codec.codec_id == CODEC_ID_MPEG4) {
        if (ap && ap->frame_rate) {
            st->codec.frame_rate      = ap->frame_rate;
            st->codec.frame_rate_base = ap->frame_rate_base;
        } else {
            st->codec.frame_rate      = 25;
            st->codec.frame_rate_base = 1;
        }
    }
    return 0;
}

#define SEQ_START_CODE		0x000001b3
#define GOP_START_CODE		0x000001b8
#define PICTURE_START_CODE	0x00000100

/* XXX: improve that by looking at several start codes */
static int mpegvideo_probe(AVProbeData *p)
{
    int code;
    const uint8_t *d;

    /* we search the first start code. If it is a sequence, gop or
       picture start code then we decide it is an mpeg video
       stream. We do not send highest value to give a chance to mpegts */
    /* NOTE: the search range was restricted to avoid too many false
       detections */

    if (p->buf_size < 6)
        return 0;
    d = p->buf;
    code = (d[0] << 24) | (d[1] << 16) | (d[2] << 8) | (d[3]);
    if ((code & 0xffffff00) == 0x100) {
        if (code == SEQ_START_CODE ||
            code == GOP_START_CODE ||
            code == PICTURE_START_CODE)
            return 50 - 1;
        else
            return 0;
    }
    return 0;
}

static int h263_probe(AVProbeData *p)
{
    int code;
    const uint8_t *d;

    if (p->buf_size < 6)
        return 0;
    d = p->buf;
    code = (d[0] << 14) | (d[1] << 6) | (d[2] >> 2);
    if (code == 0x20) {
        return 50;
    }
    return 0;
}

static int h261_probe(AVProbeData *p)
{
    int code;
    const uint8_t *d;

    if (p->buf_size < 6)
        return 0;
    d = p->buf;
    code = (d[0] << 12) | (d[1] << 4) | (d[2] >> 4);
    if (code == 0x10) {
        return 50;
    }
    return 0;
}

AVInputFormat ac3_iformat = {
    "ac3",
    "raw ac3",
    0,
    NULL,
    ac3_read_header,
    raw_read_partial_packet,
    raw_read_close,
    .extensions = "ac3",
};

#ifdef CONFIG_ENCODERS
AVOutputFormat ac3_oformat = {
    "ac3",
    "raw ac3",
    "audio/x-ac3", 
    "ac3",
    0,
    CODEC_ID_AC3,
    0,
    raw_write_header,
    raw_write_packet,
    raw_write_trailer,
};
#endif //CONFIG_ENCODERS

AVInputFormat dts_iformat = {
    "dts",
    "raw dts",
    0,
    NULL,
    dts_read_header,
    raw_read_partial_packet,
    raw_read_close,
    .extensions = "dts",
};

AVInputFormat h261_iformat = {
    "h261",
    "raw h261",
    0,
    h261_probe,
    video_read_header,
    raw_read_partial_packet,
    raw_read_close,
    .extensions = "h261",
    .value = CODEC_ID_H261,
};

AVInputFormat h263_iformat = {
    "h263",
    "raw h263",
    0,
    h263_probe,
    video_read_header,
    raw_read_partial_packet,
    raw_read_close,
//    .extensions = "h263", //FIXME remove after writing mpeg4_probe
    .value = CODEC_ID_H263,
};

#ifdef CONFIG_ENCODERS
AVOutputFormat h263_oformat = {
    "h263",
    "raw h263",
    "video/x-h263",
    "h263",
    0,
    0,
    CODEC_ID_H263,
    raw_write_header,
    raw_write_packet,
    raw_write_trailer,
};
#endif //CONFIG_ENCODERS

AVInputFormat m4v_iformat = {
    "m4v",
    "raw MPEG4 video format",
    0,
    NULL /*mpegvideo_probe*/,
    video_read_header,
    raw_read_partial_packet,
    raw_read_close,
    .extensions = "m4v", //FIXME remove after writing mpeg4_probe
    .value = CODEC_ID_MPEG4,
};

#ifdef CONFIG_ENCODERS
AVOutputFormat m4v_oformat = {
    "m4v",
    "raw MPEG4 video format",
    NULL,
    "m4v",
    0,
    CODEC_ID_NONE,
    CODEC_ID_MPEG4,
    raw_write_header,
    raw_write_packet,
    raw_write_trailer,
};
#endif //CONFIG_ENCODERS

AVInputFormat h264_iformat = {
    "h264",
    "raw H264 video format",
    0,
    NULL /*mpegvideo_probe*/,
    video_read_header,
    raw_read_partial_packet,
    raw_read_close,
    .extensions = "h26l,h264", //FIXME remove after writing mpeg4_probe
    .value = CODEC_ID_H264,
};

#ifdef CONFIG_ENCODERS
AVOutputFormat h264_oformat = {
    "h264",
    "raw H264 video format",
    NULL,
    "h264",
    0,
    CODEC_ID_NONE,
    CODEC_ID_H264,
    raw_write_header,
    raw_write_packet,
    raw_write_trailer,
};
#endif //CONFIG_ENCODERS

AVInputFormat mpegvideo_iformat = {
    "mpegvideo",
    "MPEG video",
    0,
    mpegvideo_probe,
    video_read_header,
    raw_read_partial_packet,
    raw_read_close,
    .value = CODEC_ID_MPEG1VIDEO,
};

#ifdef CONFIG_ENCODERS
AVOutputFormat mpeg1video_oformat = {
    "mpeg1video",
    "MPEG video",
    "video/x-mpeg",
    "mpg,mpeg",
    0,
    0,
    CODEC_ID_MPEG1VIDEO,
    raw_write_header,
    raw_write_packet,
    raw_write_trailer,
};
#endif //CONFIG_ENCODERS

AVInputFormat mjpeg_iformat = {
    "mjpeg",
    "MJPEG video",
    0,
    NULL,
    video_read_header,
    raw_read_partial_packet,
    raw_read_close,
    .extensions = "mjpg,mjpeg",
    .value = CODEC_ID_MJPEG,
};

#ifdef CONFIG_ENCODERS
AVOutputFormat mjpeg_oformat = {
    "mjpeg",
    "MJPEG video",
    "video/x-mjpeg",
    "mjpg,mjpeg",
    0,
    0,
    CODEC_ID_MJPEG,
    raw_write_header,
    raw_write_packet,
    raw_write_trailer,
};
#endif //CONFIG_ENCODERS

/* pcm formats */

#define PCMINPUTDEF(name, long_name, ext, codec) \
AVInputFormat pcm_ ## name ## _iformat = {\
    #name,\
    long_name,\
    0,\
    NULL,\
    raw_read_header,\
    raw_read_packet,\
    raw_read_close,\
    pcm_read_seek,\
    .extensions = ext,\
    .value = codec,\
};

#if !defined(CONFIG_ENCODERS) && defined(CONFIG_DECODERS)

#define PCMDEF(name, long_name, ext, codec) \
    PCMINPUTDEF(name, long_name, ext, codec)

#else

#define PCMDEF(name, long_name, ext, codec) \
    PCMINPUTDEF(name, long_name, ext, codec)\
\
AVOutputFormat pcm_ ## name ## _oformat = {\
    #name,\
    long_name,\
    NULL,\
    ext,\
    0,\
    codec,\
    0,\
    raw_write_header,\
    raw_write_packet,\
    raw_write_trailer,\
};
#endif //CONFIG_ENCODERS

#ifdef WORDS_BIGENDIAN
#define BE_DEF(s) s
#define LE_DEF(s) NULL
#else
#define BE_DEF(s) NULL
#define LE_DEF(s) s
#endif


PCMDEF(s16le, "pcm signed 16 bit little endian format", 
       LE_DEF("sw"), CODEC_ID_PCM_S16LE)

PCMDEF(s16be, "pcm signed 16 bit big endian format", 
       BE_DEF("sw"), CODEC_ID_PCM_S16BE)

PCMDEF(u16le, "pcm unsigned 16 bit little endian format", 
       LE_DEF("uw"), CODEC_ID_PCM_U16LE)

PCMDEF(u16be, "pcm unsigned 16 bit big endian format", 
       BE_DEF("uw"), CODEC_ID_PCM_U16BE)

PCMDEF(s8, "pcm signed 8 bit format", 
       "sb", CODEC_ID_PCM_S8)

PCMDEF(u8, "pcm unsigned 8 bit format", 
       "ub", CODEC_ID_PCM_U8)

PCMDEF(mulaw, "pcm mu law format", 
       "ul", CODEC_ID_PCM_MULAW)

PCMDEF(alaw, "pcm A law format", 
       "al", CODEC_ID_PCM_ALAW)

static int rawvideo_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int packet_size, ret, width, height;
    AVStream *st = s->streams[0];

    width = st->codec.width;
    height = st->codec.height;

    packet_size = avpicture_get_size(st->codec.pix_fmt, width, height);
    if (packet_size < 0)
        av_abort();

    if (av_new_packet(pkt, packet_size) < 0)
        return AVERROR_IO;

    pkt->stream_index = 0;
#if 0
    /* bypass buffered I/O */
    ret = url_read(url_fileno(&s->pb), pkt->data, pkt->size);
#else
    ret = get_buffer(&s->pb, pkt->data, pkt->size);
#endif
    if (ret != pkt->size) {
        av_free_packet(pkt);
        return AVERROR_IO;
    } else {
        return 0;
    }
}

AVInputFormat rawvideo_iformat = {
    "rawvideo",
    "raw video format",
    0,
    NULL,
    raw_read_header,
    rawvideo_read_packet,
    raw_read_close,
    .extensions = "yuv",
    .value = CODEC_ID_RAWVIDEO,
};

#ifdef CONFIG_ENCODERS
AVOutputFormat rawvideo_oformat = {
    "rawvideo",
    "raw video format",
    NULL,
    "yuv",
    0,
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    raw_write_header,
    raw_write_packet,
    raw_write_trailer,
};
#endif //CONFIG_ENCODERS

#ifdef CONFIG_ENCODERS
static int null_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    return 0;
}

AVOutputFormat null_oformat = {
    "null",
    "null video format",
    NULL,
    NULL,
    0,
#ifdef WORDS_BIGENDIAN
    CODEC_ID_PCM_S16BE,
#else
    CODEC_ID_PCM_S16LE,
#endif
    CODEC_ID_RAWVIDEO,
    raw_write_header,
    null_write_packet,
    raw_write_trailer,
    .flags = AVFMT_NOFILE | AVFMT_RAWPICTURE,
};
#endif //CONFIG_ENCODERS

#ifndef CONFIG_ENCODERS
#define av_register_output_format(format)
#endif
#ifndef CONFIG_DECODERS
#define av_register_input_format(format)
#endif

int raw_init(void)
{
    av_register_input_format(&ac3_iformat);
    av_register_output_format(&ac3_oformat);

    av_register_input_format(&dts_iformat);

    av_register_input_format(&h261_iformat);

    av_register_input_format(&h263_iformat);
    av_register_output_format(&h263_oformat);
    
    av_register_input_format(&m4v_iformat);
    av_register_output_format(&m4v_oformat);
    
    av_register_input_format(&h264_iformat);
    av_register_output_format(&h264_oformat);

    av_register_input_format(&mpegvideo_iformat);
    av_register_output_format(&mpeg1video_oformat);

    av_register_input_format(&mjpeg_iformat);
    av_register_output_format(&mjpeg_oformat);

    av_register_input_format(&pcm_s16le_iformat);
    av_register_output_format(&pcm_s16le_oformat);
    av_register_input_format(&pcm_s16be_iformat);
    av_register_output_format(&pcm_s16be_oformat);
    av_register_input_format(&pcm_u16le_iformat);
    av_register_output_format(&pcm_u16le_oformat);
    av_register_input_format(&pcm_u16be_iformat);
    av_register_output_format(&pcm_u16be_oformat);
    av_register_input_format(&pcm_s8_iformat);
    av_register_output_format(&pcm_s8_oformat);
    av_register_input_format(&pcm_u8_iformat);
    av_register_output_format(&pcm_u8_oformat);
    av_register_input_format(&pcm_mulaw_iformat);
    av_register_output_format(&pcm_mulaw_oformat);
    av_register_input_format(&pcm_alaw_iformat);
    av_register_output_format(&pcm_alaw_oformat);

    av_register_input_format(&rawvideo_iformat);
    av_register_output_format(&rawvideo_oformat);

    av_register_output_format(&null_oformat);
    return 0;
}
