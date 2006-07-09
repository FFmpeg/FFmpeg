/*
 * RAW encoder and decoder
 * Copyright (c) 2001 Fabrice Bellard.
 * Copyright (c) 2005 Alex Beregszaszi
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include "avformat.h"

#ifdef CONFIG_MUXERS
/* simple formats */
static int raw_write_header(struct AVFormatContext *s)
{
    return 0;
}

static int flac_write_header(struct AVFormatContext *s)
{
    static const uint8_t header[8] = {
        0x66, 0x4C, 0x61, 0x43, 0x80, 0x00, 0x00, 0x22
    };
    uint8_t *streaminfo = s->streams[0]->codec->extradata;
    int len = s->streams[0]->codec->extradata_size;
    if(streaminfo != NULL && len > 0) {
        put_buffer(&s->pb, header, 8);
        put_buffer(&s->pb, streaminfo, len);
    }
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
#endif //CONFIG_MUXERS

/* raw input */
static int raw_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    AVStream *st;
    int id;

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR_NOMEM;

        id = s->iformat->value;
        if (id == CODEC_ID_RAWVIDEO) {
            st->codec->codec_type = CODEC_TYPE_VIDEO;
        } else {
            st->codec->codec_type = CODEC_TYPE_AUDIO;
        }
        st->codec->codec_id = id;

        switch(st->codec->codec_type) {
        case CODEC_TYPE_AUDIO:
            st->codec->sample_rate = ap->sample_rate;
            st->codec->channels = ap->channels;
            av_set_pts_info(st, 64, 1, st->codec->sample_rate);
            break;
        case CODEC_TYPE_VIDEO:
            av_set_pts_info(st, 64, ap->time_base.num, ap->time_base.den);
            st->codec->width = ap->width;
            st->codec->height = ap->height;
            st->codec->pix_fmt = ap->pix_fmt;
            if(st->codec->pix_fmt == PIX_FMT_NONE)
                st->codec->pix_fmt= PIX_FMT_YUV420P;
            break;
        default:
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

    ret= av_get_packet(&s->pb, pkt, size);

    pkt->stream_index = 0;
    if (ret <= 0) {
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

    pkt->pos= url_ftell(&s->pb);
    pkt->stream_index = 0;
    ret = get_partial_buffer(&s->pb, pkt->data, size);
    if (ret <= 0) {
        av_free_packet(pkt);
        return AVERROR_IO;
    }
    pkt->size = ret;
    return ret;
}

// http://www.artificis.hu/files/texts/ingenient.txt
static int ingenient_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, size, w, h, unk1, unk2;

    if (get_le32(&s->pb) != MKTAG('M', 'J', 'P', 'G'))
        return AVERROR_IO; // FIXME

    size = get_le32(&s->pb);

    w = get_le16(&s->pb);
    h = get_le16(&s->pb);

    url_fskip(&s->pb, 8); // zero + size (padded?)
    url_fskip(&s->pb, 2);
    unk1 = get_le16(&s->pb);
    unk2 = get_le16(&s->pb);
    url_fskip(&s->pb, 22); // ascii timestamp

    av_log(NULL, AV_LOG_DEBUG, "Ingenient packet: size=%d, width=%d, height=%d, unk1=%d unk2=%d\n",
        size, w, h, unk1, unk2);

    if (av_new_packet(pkt, size) < 0)
        return AVERROR_IO;

    pkt->pos = url_ftell(&s->pb);
    pkt->stream_index = 0;
    ret = get_buffer(&s->pb, pkt->data, size);
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
                  int stream_index, int64_t timestamp, int flags)
{
    AVStream *st;
    int block_align, byte_rate;
    int64_t pos;

    st = s->streams[0];
    switch(st->codec->codec_id) {
    case CODEC_ID_PCM_S16LE:
    case CODEC_ID_PCM_S16BE:
    case CODEC_ID_PCM_U16LE:
    case CODEC_ID_PCM_U16BE:
        block_align = 2 * st->codec->channels;
        byte_rate = block_align * st->codec->sample_rate;
        break;
    case CODEC_ID_PCM_S8:
    case CODEC_ID_PCM_U8:
    case CODEC_ID_PCM_MULAW:
    case CODEC_ID_PCM_ALAW:
        block_align = st->codec->channels;
        byte_rate = block_align * st->codec->sample_rate;
        break;
    default:
        block_align = st->codec->block_align;
        byte_rate = st->codec->bit_rate / 8;
        break;
    }

    if (block_align <= 0 || byte_rate <= 0)
        return -1;

    /* compute the position by aligning it to block_align */
    pos = av_rescale_rnd(timestamp * byte_rate,
                         st->time_base.num,
                         st->time_base.den * (int64_t)block_align,
                         (flags & AVSEEK_FLAG_BACKWARD) ? AV_ROUND_DOWN : AV_ROUND_UP);
    pos *= block_align;

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

    st->codec->codec_type = CODEC_TYPE_AUDIO;
    st->codec->codec_id = CODEC_ID_AC3;
    st->need_parsing = 1;
    /* the parameters will be extracted from the compressed bitstream */
    return 0;
}

static int shorten_read_header(AVFormatContext *s,
                               AVFormatParameters *ap)
{
    AVStream *st;

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR_NOMEM;
    st->codec->codec_type = CODEC_TYPE_AUDIO;
    st->codec->codec_id = CODEC_ID_SHORTEN;
    st->need_parsing = 1;
    /* the parameters will be extracted from the compressed bitstream */
    return 0;
}

/* flac read */
static int flac_read_header(AVFormatContext *s,
                            AVFormatParameters *ap)
{
    AVStream *st;

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR_NOMEM;
    st->codec->codec_type = CODEC_TYPE_AUDIO;
    st->codec->codec_id = CODEC_ID_FLAC;
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

    st->codec->codec_type = CODEC_TYPE_AUDIO;
    st->codec->codec_id = CODEC_ID_DTS;
    st->need_parsing = 1;
    /* the parameters will be extracted from the compressed bitstream */
    return 0;
}

/* aac read */
static int aac_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    AVStream *st;

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR_NOMEM;

    st->codec->codec_type = CODEC_TYPE_AUDIO;
    st->codec->codec_id = CODEC_ID_AAC;
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

    st->codec->codec_type = CODEC_TYPE_VIDEO;
    st->codec->codec_id = s->iformat->value;
    st->need_parsing = 1;

    /* for mjpeg, specify frame rate */
    /* for mpeg4 specify it too (most mpeg4 streams dont have the fixed_vop_rate set ...)*/
    if (ap->time_base.num) {
        av_set_pts_info(st, 64, ap->time_base.num, ap->time_base.den);
    } else if ( st->codec->codec_id == CODEC_ID_MJPEG ||
                st->codec->codec_id == CODEC_ID_MPEG4 ||
                st->codec->codec_id == CODEC_ID_H264) {
        av_set_pts_info(st, 64, 1, 25);
    }

    return 0;
}

#define SEQ_START_CODE          0x000001b3
#define GOP_START_CODE          0x000001b8
#define PICTURE_START_CODE      0x00000100
#define SLICE_START_CODE        0x00000101
#define PACK_START_CODE         0x000001ba
#define VIDEO_ID                0x000001e0
#define AUDIO_ID                0x000001c0

static int mpegvideo_probe(AVProbeData *p)
{
    uint32_t code= -1;
    int pic=0, seq=0, slice=0, pspack=0, pes=0;
    int i;

    for(i=0; i<p->buf_size; i++){
        code = (code<<8) + p->buf[i];
        if ((code & 0xffffff00) == 0x100) {
            switch(code){
            case     SEQ_START_CODE:   seq++; break;
            case PICTURE_START_CODE:   pic++; break;
            case   SLICE_START_CODE: slice++; break;
            case    PACK_START_CODE: pspack++; break;
            case           VIDEO_ID:
            case           AUDIO_ID:   pes++; break;
            }
        }
    }
    if(seq && seq*9<=pic*10 && pic*9<=slice*10 && !pspack && !pes)
        return AVPROBE_SCORE_MAX/2+1; // +1 for .mpg
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

AVInputFormat shorten_demuxer = {
    "shn",
    "raw shorten",
    0,
    NULL,
    shorten_read_header,
    raw_read_partial_packet,
    raw_read_close,
    .extensions = "shn",
};

AVInputFormat flac_demuxer = {
    "flac",
    "raw flac",
    0,
    NULL,
    flac_read_header,
    raw_read_partial_packet,
    raw_read_close,
    .extensions = "flac",
};

#ifdef CONFIG_MUXERS
AVOutputFormat flac_muxer = {
    "flac",
    "raw flac",
    "audio/x-flac",
    "flac",
    0,
    CODEC_ID_FLAC,
    0,
    flac_write_header,
    raw_write_packet,
    raw_write_trailer,
};
#endif //CONFIG_MUXERS

AVInputFormat ac3_demuxer = {
    "ac3",
    "raw ac3",
    0,
    NULL,
    ac3_read_header,
    raw_read_partial_packet,
    raw_read_close,
    .extensions = "ac3",
};

#ifdef CONFIG_MUXERS
AVOutputFormat ac3_muxer = {
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
#endif //CONFIG_MUXERS

AVInputFormat dts_demuxer = {
    "dts",
    "raw dts",
    0,
    NULL,
    dts_read_header,
    raw_read_partial_packet,
    raw_read_close,
    .extensions = "dts",
};

AVInputFormat aac_demuxer = {
    "aac",
    "ADTS AAC",
    0,
    NULL,
    aac_read_header,
    raw_read_partial_packet,
    raw_read_close,
    .extensions = "aac",
};

AVInputFormat h261_demuxer = {
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

#ifdef CONFIG_MUXERS
AVOutputFormat h261_muxer = {
    "h261",
    "raw h261",
    "video/x-h261",
    "h261",
    0,
    0,
    CODEC_ID_H261,
    raw_write_header,
    raw_write_packet,
    raw_write_trailer,
};
#endif //CONFIG_MUXERS

AVInputFormat h263_demuxer = {
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

#ifdef CONFIG_MUXERS
AVOutputFormat h263_muxer = {
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
#endif //CONFIG_MUXERS

AVInputFormat m4v_demuxer = {
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

#ifdef CONFIG_MUXERS
AVOutputFormat m4v_muxer = {
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
#endif //CONFIG_MUXERS

AVInputFormat h264_demuxer = {
    "h264",
    "raw H264 video format",
    0,
    NULL /*mpegvideo_probe*/,
    video_read_header,
    raw_read_partial_packet,
    raw_read_close,
    .extensions = "h26l,h264,264", //FIXME remove after writing mpeg4_probe
    .value = CODEC_ID_H264,
};

#ifdef CONFIG_MUXERS
AVOutputFormat h264_muxer = {
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
#endif //CONFIG_MUXERS

AVInputFormat mpegvideo_demuxer = {
    "mpegvideo",
    "MPEG video",
    0,
    mpegvideo_probe,
    video_read_header,
    raw_read_partial_packet,
    raw_read_close,
    .value = CODEC_ID_MPEG1VIDEO,
};

#ifdef CONFIG_MUXERS
AVOutputFormat mpeg1video_muxer = {
    "mpeg1video",
    "MPEG video",
    "video/x-mpeg",
    "mpg,mpeg,m1v",
    0,
    0,
    CODEC_ID_MPEG1VIDEO,
    raw_write_header,
    raw_write_packet,
    raw_write_trailer,
};
#endif //CONFIG_MUXERS

#ifdef CONFIG_MUXERS
AVOutputFormat mpeg2video_muxer = {
    "mpeg2video",
    "MPEG2 video",
    NULL,
    "m2v",
    0,
    0,
    CODEC_ID_MPEG2VIDEO,
    raw_write_header,
    raw_write_packet,
    raw_write_trailer,
};
#endif //CONFIG_MUXERS

AVInputFormat mjpeg_demuxer = {
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

AVInputFormat ingenient_demuxer = {
    "ingenient",
    "Ingenient MJPEG",
    0,
    NULL,
    video_read_header,
    ingenient_read_packet,
    raw_read_close,
    .extensions = "cgi", // FIXME
    .value = CODEC_ID_MJPEG,
};

#ifdef CONFIG_MUXERS
AVOutputFormat mjpeg_muxer = {
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
#endif //CONFIG_MUXERS

/* pcm formats */

#define PCMINPUTDEF(name, long_name, ext, codec) \
AVInputFormat pcm_ ## name ## _demuxer = {\
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

#define PCMOUTPUTDEF(name, long_name, ext, codec) \
AVOutputFormat pcm_ ## name ## _muxer = {\
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


#if !defined(CONFIG_MUXERS) && defined(CONFIG_DEMUXERS)
#define PCMDEF(name, long_name, ext, codec) \
        PCMINPUTDEF(name, long_name, ext, codec)
#elif defined(CONFIG_MUXERS) && !defined(CONFIG_DEMUXERS)
#define PCMDEF(name, long_name, ext, codec) \
        PCMOUTPUTDEF(name, long_name, ext, codec)
#elif defined(CONFIG_MUXERS) && defined(CONFIG_DEMUXERS)
#define PCMDEF(name, long_name, ext, codec) \
        PCMINPUTDEF(name, long_name, ext, codec)\
        PCMOUTPUTDEF(name, long_name, ext, codec)
#else
#define PCMDEF(name, long_name, ext, codec)
#endif

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

    width = st->codec->width;
    height = st->codec->height;

    packet_size = avpicture_get_size(st->codec->pix_fmt, width, height);
    if (packet_size < 0)
        return -1;

    ret= av_get_packet(&s->pb, pkt, packet_size);

    pkt->stream_index = 0;
    if (ret != packet_size) {
        return AVERROR_IO;
    } else {
        return 0;
    }
}

AVInputFormat rawvideo_demuxer = {
    "rawvideo",
    "raw video format",
    0,
    NULL,
    raw_read_header,
    rawvideo_read_packet,
    raw_read_close,
    .extensions = "yuv,cif,qcif",
    .value = CODEC_ID_RAWVIDEO,
};

#ifdef CONFIG_MUXERS
AVOutputFormat rawvideo_muxer = {
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
#endif //CONFIG_MUXERS

#ifdef CONFIG_MUXERS
static int null_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    return 0;
}

AVOutputFormat null_muxer = {
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
#endif //CONFIG_MUXERS

#ifndef CONFIG_MUXERS
#define av_register_output_format(format)
#endif
#ifndef CONFIG_DEMUXERS
#define av_register_input_format(format)
#endif

int raw_init(void)
{

    av_register_input_format(&shorten_demuxer);
    av_register_input_format(&flac_demuxer);
    av_register_output_format(&flac_muxer);

    av_register_input_format(&ac3_demuxer);
    av_register_output_format(&ac3_muxer);

    av_register_input_format(&aac_demuxer);

    av_register_input_format(&dts_demuxer);

    av_register_input_format(&h261_demuxer);
    av_register_output_format(&h261_muxer);

    av_register_input_format(&h263_demuxer);
    av_register_output_format(&h263_muxer);

    av_register_input_format(&m4v_demuxer);
    av_register_output_format(&m4v_muxer);

    av_register_input_format(&h264_demuxer);
    av_register_output_format(&h264_muxer);

    av_register_input_format(&mpegvideo_demuxer);
    av_register_output_format(&mpeg1video_muxer);

    av_register_output_format(&mpeg2video_muxer);

    av_register_input_format(&mjpeg_demuxer);
    av_register_output_format(&mjpeg_muxer);

    av_register_input_format(&ingenient_demuxer);

    av_register_input_format(&pcm_s16le_demuxer);
    av_register_output_format(&pcm_s16le_muxer);
    av_register_input_format(&pcm_s16be_demuxer);
    av_register_output_format(&pcm_s16be_muxer);
    av_register_input_format(&pcm_u16le_demuxer);
    av_register_output_format(&pcm_u16le_muxer);
    av_register_input_format(&pcm_u16be_demuxer);
    av_register_output_format(&pcm_u16be_muxer);
    av_register_input_format(&pcm_s8_demuxer);
    av_register_output_format(&pcm_s8_muxer);
    av_register_input_format(&pcm_u8_demuxer);
    av_register_output_format(&pcm_u8_muxer);
    av_register_input_format(&pcm_mulaw_demuxer);
    av_register_output_format(&pcm_mulaw_muxer);
    av_register_input_format(&pcm_alaw_demuxer);
    av_register_output_format(&pcm_alaw_muxer);

    av_register_input_format(&rawvideo_demuxer);
    av_register_output_format(&rawvideo_muxer);

    av_register_output_format(&null_muxer);
    return 0;
}
