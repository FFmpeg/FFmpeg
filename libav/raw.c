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

/* simple formats */
int raw_write_header(struct AVFormatContext *s)
{
    return 0;
}

int raw_write_packet(struct AVFormatContext *s, 
                     int stream_index,
                     unsigned char *buf, int size, int force_pts)
{
    put_buffer(&s->pb, buf, size);
    put_flush_packet(&s->pb);
    return 0;
}

int raw_write_trailer(struct AVFormatContext *s)
{
    return 0;
}

/* raw input */
static int raw_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
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
            st->codec.frame_rate = ap->frame_rate;
            st->codec.width = ap->width;
            st->codec.height = ap->height;
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

int raw_read_packet(AVFormatContext *s,
                    AVPacket *pkt)
{
    int ret;

    if (av_new_packet(pkt, RAW_PACKET_SIZE) < 0)
        return -EIO;

    pkt->stream_index = 0;
    ret = get_buffer(&s->pb, pkt->data, RAW_PACKET_SIZE);
    if (ret <= 0) {
        av_free_packet(pkt);
        return -EIO;
    }
    /* note: we need to modify the packet size here to handle the last
       packet */
    pkt->size = ret;
    return ret;
}

int raw_read_close(AVFormatContext *s)
{
    return 0;
}

/* mp3 read */
static int mp3_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    AVStream *st;

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR_NOMEM;

    st->codec.codec_type = CODEC_TYPE_AUDIO;
    st->codec.codec_id = CODEC_ID_MP2;
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
    /* for mjpeg, specify frame rate */
    if (st->codec.codec_id == CODEC_ID_MJPEG) {
        if (ap) {
            st->codec.frame_rate = ap->frame_rate;
        } else {
            st->codec.frame_rate = 25 * FRAME_RATE_BASE;
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
    int code, c, i;
    code = 0xff;

    /* we search the first start code. If it is a sequence, gop or
       picture start code then we decide it is an mpeg video
       stream. We do not send highest value to give a chance to mpegts */
    for(i=0;i<p->buf_size;i++) {
        c = p->buf[i];
        code = (code << 8) | c;
        if ((code & 0xffffff00) == 0x100) {
            if (code == SEQ_START_CODE ||
                code == GOP_START_CODE ||
                code == PICTURE_START_CODE)
                return AVPROBE_SCORE_MAX - 1;
            else
                return 0;
        }
    }
    return 0;
}

AVInputFormat mp3_iformat = {
    "mp3",
    "MPEG audio",
    0,
    NULL,
    mp3_read_header,
    raw_read_packet,
    raw_read_close,
    extensions: "mp2,mp3", /* XXX: use probe */
};

AVOutputFormat mp2_oformat = {
    "mp2",
    "MPEG audio layer 2",
    "audio/x-mpeg",
    "mp2,mp3",
    0,
    CODEC_ID_MP2,
    0,
    raw_write_header,
    raw_write_packet,
    raw_write_trailer,
};


AVInputFormat ac3_iformat = {
    "ac3",
    "raw ac3",
    0,
    NULL,
    raw_read_header,
    raw_read_packet,
    raw_read_close,
    extensions: "ac3",
    value: CODEC_ID_AC3,
};

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

AVInputFormat mpegvideo_iformat = {
    "mpegvideo",
    "MPEG video",
    0,
    mpegvideo_probe,
    video_read_header,
    raw_read_packet,
    raw_read_close,
    value: CODEC_ID_MPEG1VIDEO,
};

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

AVInputFormat mjpeg_iformat = {
    "mjpeg",
    "MJPEG video",
    0,
    NULL,
    video_read_header,
    raw_read_packet,
    raw_read_close,
    extensions: "mjpg,mjpeg",
    value: CODEC_ID_MJPEG,
};

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

/* pcm formats */

#define PCMDEF(name, long_name, ext, codec) \
AVInputFormat pcm_ ## name ## _iformat = {\
    #name,\
    long_name,\
    0,\
    NULL,\
    raw_read_header,\
    raw_read_packet,\
    raw_read_close,\
    extensions: ext,\
    value: codec,\
};\
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

int rawvideo_read_packet(AVFormatContext *s,
                         AVPacket *pkt)
{
    int packet_size, ret, width, height;
    AVStream *st = s->streams[0];

    width = st->codec.width;
    height = st->codec.height;

    switch(st->codec.pix_fmt) {
    case PIX_FMT_YUV420P:
        packet_size = (width * height * 3) / 2;
        break;
    case PIX_FMT_YUV422:
        packet_size = (width * height * 2);
        break;
    case PIX_FMT_BGR24:
    case PIX_FMT_RGB24:
        packet_size = (width * height * 3);
        break;
    default:
        abort();
        break;
    }

    if (av_new_packet(pkt, packet_size) < 0)
        return -EIO;

    pkt->stream_index = 0;
#if 0
    /* bypass buffered I/O */
    ret = url_read(url_fileno(&s->pb), pkt->data, pkt->size);
#else
    ret = get_buffer(&s->pb, pkt->data, pkt->size);
#endif
    if (ret != pkt->size) {
        av_free_packet(pkt);
        return -EIO;
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
    extensions: "yuv",
    value: CODEC_ID_RAWVIDEO,
};

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

int raw_init(void)
{
    av_register_input_format(&mp3_iformat);
    av_register_output_format(&mp2_oformat);
    
    av_register_input_format(&ac3_iformat);
    av_register_output_format(&ac3_oformat);

    av_register_output_format(&h263_oformat);

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
    return 0;
}
