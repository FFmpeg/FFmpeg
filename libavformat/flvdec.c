/*
 * FLV demuxer
 * Copyright (c) 2003 The FFmpeg Project.
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 *
 * This demuxer will generate a 1 byte extradata for VP6F content.
 * It is composed of:
 *  - upper 4bits: difference between encoded width and visible width
 *  - lower 4bits: difference between encoded height and visible height
 */
#include "avformat.h"
#include "flv.h"

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

static int amf_get_string(ByteIOContext *ioc, char *buffer, int buffsize) {
    int length = get_be16(ioc);
    if(length >= buffsize) {
        url_fskip(ioc, length);
        return -1;
    }

    get_buffer(ioc, buffer, length);

    buffer[length] = '\0';

    return length;
}

static int flv_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    int offset, flags, size;
    AVStream *st;

    url_fskip(&s->pb, 4);
    flags = get_byte(&s->pb);

    if(flags & FLV_HEADER_FLAG_HASVIDEO){
        st = av_new_stream(s, 0);
        if (!st)
            return AVERROR_NOMEM;
        st->codec->codec_type = CODEC_TYPE_VIDEO;
        av_set_pts_info(st, 24, 1, 1000); /* 24 bit pts in ms */
    }
    if(flags & FLV_HEADER_FLAG_HASAUDIO){
        st = av_new_stream(s, 1);
        if (!st)
            return AVERROR_NOMEM;
        st->codec->codec_type = CODEC_TYPE_AUDIO;
        av_set_pts_info(st, 24, 1, 1000); /* 24 bit pts in ms */
    }

    offset = get_be32(&s->pb);

    if(!url_is_streamed(&s->pb)){
        const int fsize= url_fsize(&s->pb);
        url_fseek(&s->pb, fsize-4, SEEK_SET);
        size= get_be32(&s->pb);
        url_fseek(&s->pb, fsize-3-size, SEEK_SET);
        if(size == get_be24(&s->pb) + 11){
            s->duration= get_be24(&s->pb) * (int64_t)AV_TIME_BASE / 1000;
        }
    }

    url_fseek(&s->pb, offset, SEEK_SET);

    s->start_time = 0;

    return 0;
}

static int flv_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, i, type, size, pts, flags, is_audio, next, pos;
    AVStream *st = NULL;

 for(;;){
    pos = url_ftell(&s->pb);
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

    next= size + url_ftell(&s->pb);

    if (type == FLV_TAG_TYPE_AUDIO) {
        is_audio=1;
        flags = get_byte(&s->pb);
    } else if (type == FLV_TAG_TYPE_VIDEO) {
        is_audio=0;
        flags = get_byte(&s->pb);
    } else if (type == FLV_TAG_TYPE_META && size > 13+1+4) {
        url_fskip(&s->pb, 13); //onMetaData blah
        if(get_byte(&s->pb) == 8){
            url_fskip(&s->pb, 4);
        }
        while(url_ftell(&s->pb) + 5 < next){
            char tmp[128];
            int type, len;
            double d= 0;

            if(amf_get_string(&s->pb, tmp, sizeof(tmp))<0)
                break;

            type= get_byte(&s->pb);
            if(type == AMF_DATA_TYPE_NUMBER){
                d= av_int2dbl(get_be64(&s->pb));
            }else if(type == AMF_DATA_TYPE_STRING){
                amf_get_string(&s->pb, NULL, 0);
            }else if(type == AMF_DATA_TYPE_MIXEDARRAY){
                //array
                break;
            }else if(type == AMF_DATA_TYPE_DATE){
                d= av_int2dbl(get_be64(&s->pb));
                get_be16(&s->pb);
            }

            if(!strcmp(tmp, "duration")){
                s->duration = d*AV_TIME_BASE;
            }else if(!strcmp(tmp, "videodatarate")){
            }else if(!strcmp(tmp, "audiodatarate")){
            }
        }
        url_fseek(&s->pb, next, SEEK_SET);
        continue;
    } else {
        /* skip packet */
        av_log(s, AV_LOG_ERROR, "skipping flv packet: type %d, size %d, flags %d\n", type, size, flags);
        url_fseek(&s->pb, next, SEEK_SET);
        continue;
    }

    /* now find stream */
    for(i=0;i<s->nb_streams;i++) {
        st = s->streams[i];
        if (st->id == is_audio)
            break;
    }
    if(i == s->nb_streams){
        av_log(NULL, AV_LOG_ERROR, "invalid stream\n");
        url_fseek(&s->pb, next, SEEK_SET);
        continue;
    }
//    av_log(NULL, AV_LOG_DEBUG, "%d %X %d \n", is_audio, flags, st->discard);
    if(  (st->discard >= AVDISCARD_NONKEY && !((flags & FLV_VIDEO_FRAMETYPE_MASK) == FLV_FRAME_KEY ||         is_audio))
       ||(st->discard >= AVDISCARD_BIDIR  &&  ((flags & FLV_VIDEO_FRAMETYPE_MASK) == FLV_FRAME_DISP_INTER && !is_audio))
       || st->discard >= AVDISCARD_ALL
       ){
        url_fseek(&s->pb, next, SEEK_SET);
        continue;
    }
    if ((flags & FLV_VIDEO_FRAMETYPE_MASK) == FLV_FRAME_KEY)
        av_add_index_entry(st, pos, pts, size, 0, AVINDEX_KEYFRAME);
    break;
 }

    if(is_audio){
        if(st->codec->sample_rate == 0){
            st->codec->channels = (flags & FLV_AUDIO_CHANNEL_MASK) == FLV_STEREO ? 2 : 1;
            if((flags & FLV_AUDIO_CODECID_MASK) == FLV_CODECID_NELLYMOSER_8HZ_MONO)
                st->codec->sample_rate= 8000;
            else
                st->codec->sample_rate = (44100 << ((flags & FLV_AUDIO_SAMPLERATE_MASK) >> FLV_AUDIO_SAMPLERATE_OFFSET) >> 3);
            switch(flags & FLV_AUDIO_CODECID_MASK) {
            case FLV_CODECID_PCM_BE: if (flags & FLV_AUDIO_SAMPLESIZE_MASK) st->codec->codec_id = CODEC_ID_PCM_S16BE;
                    else st->codec->codec_id = CODEC_ID_PCM_S8; break;
            case FLV_CODECID_ADPCM : st->codec->codec_id = CODEC_ID_ADPCM_SWF;                       break;
            case FLV_CODECID_MP3   : st->codec->codec_id = CODEC_ID_MP3      ; st->need_parsing = 1; break;
            // this is not listed at FLV but at SWF, strange...
            case FLV_CODECID_PCM_LE: if (flags & FLV_AUDIO_SAMPLESIZE_MASK) st->codec->codec_id = CODEC_ID_PCM_S16LE;
                    else st->codec->codec_id = CODEC_ID_PCM_S8; break;
            default:
                    av_log(s, AV_LOG_INFO, "Unsupported audio codec (%x)\n", flags >> 4);
                st->codec->codec_tag= (flags >> 4);
            }
            st->codec->bits_per_sample = (flags & FLV_AUDIO_SAMPLESIZE_MASK) ? 16 : 8;
        }
    }else{
            switch(flags & FLV_VIDEO_CODECID_MASK){
            case FLV_CODECID_H263  : st->codec->codec_id = CODEC_ID_FLV1   ; break;
            case FLV_CODECID_SCREEN: st->codec->codec_id = CODEC_ID_FLASHSV; break;
            case FLV_CODECID_VP6   :
                st->codec->codec_id = CODEC_ID_VP6F;
                if (st->codec->extradata_size != 1) {
                    st->codec->extradata_size = 1;
                    st->codec->extradata = av_malloc(1);
                }
                /* width and height adjustment */
                st->codec->extradata[0] = get_byte(&s->pb);
                size--;
                break;
            default:
                av_log(s, AV_LOG_INFO, "Unsupported video codec (%x)\n", flags & FLV_VIDEO_CODECID_MASK);
                st->codec->codec_tag = flags & FLV_VIDEO_CODECID_MASK;
            }
    }

    ret= av_get_packet(&s->pb, pkt, size - 1);
    if (ret <= 0) {
        return AVERROR_IO;
    }
    /* note: we need to modify the packet size here to handle the last
       packet */
    pkt->size = ret;
    pkt->pts = pts;
    pkt->stream_index = st->index;

    if (is_audio || ((flags & FLV_VIDEO_FRAMETYPE_MASK) == FLV_FRAME_KEY))
        pkt->flags |= PKT_FLAG_KEY;

    return ret;
}

static int flv_read_close(AVFormatContext *s)
{
    return 0;
}

static int flv_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    AVStream *st = s->streams[stream_index];
    int index = av_index_search_timestamp(st, timestamp, flags);
    if (index < 0)
        return -1;
    url_fseek(&s->pb, st->index_entries[index].pos, SEEK_SET);

    return 0;
}

AVInputFormat flv_demuxer = {
    "flv",
    "flv format",
    0,
    flv_probe,
    flv_read_header,
    flv_read_packet,
    flv_read_close,
    flv_read_seek,
    .extensions = "flv",
    .value = CODEC_ID_FLV1,
};
