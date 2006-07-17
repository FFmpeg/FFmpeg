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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include "avformat.h"

#undef NDEBUG
#include <assert.h>

typedef struct FLVContext {
    int hasAudio;
    int hasVideo;
    int reserved;
} FLVContext;

static int get_audio_flags(AVCodecContext *enc){
    int flags = (enc->bits_per_sample == 16) ? 0x2 : 0x0;

    switch (enc->sample_rate) {
        case    44100:
            flags |= 0x0C;
            break;
        case    22050:
            flags |= 0x08;
            break;
        case    11025:
            flags |= 0x04;
            break;
        case     8000: //nellymoser only
        case     5512: //not mp3
            flags |= 0x00;
            break;
        default:
            av_log(enc, AV_LOG_ERROR, "flv doesnt support that sample rate, choose from (44100, 22050, 11025)\n");
            return -1;
    }

    if (enc->channels > 1) {
        flags |= 0x01;
    }

    switch(enc->codec_id){
    case CODEC_ID_MP3:
        flags |= 0x20 | 0x2;
        break;
    case CODEC_ID_PCM_S8:
        break;
    case CODEC_ID_PCM_S16BE:
        flags |= 0x60 | 0x2;
        break;
    case CODEC_ID_PCM_S16LE:
        flags |= 0x2;
        break;
    case CODEC_ID_ADPCM_SWF:
        flags |= 0x10;
        break;
    case 0:
        flags |= enc->codec_tag<<4;
        break;
    default:
        av_log(enc, AV_LOG_ERROR, "codec not compatible with flv\n");
        return -1;
    }

    return flags;
}

#define AMF_DOUBLE  0
#define AMF_BOOLEAN 1
#define AMF_STRING  2
#define AMF_OBJECT  3
#define AMF_MIXED_ARRAY 8
#define AMF_ARRAY  10
#define AMF_DATE   11

static void put_amf_string(ByteIOContext *pb, const char *str)
{
    size_t len = strlen(str);
    put_be16(pb, len);
    put_buffer(pb, str, len);
}

static void put_amf_double(ByteIOContext *pb, double d)
{
    put_byte(pb, AMF_DOUBLE);
    put_be64(pb, av_dbl2int(d));
}

static int flv_write_header(AVFormatContext *s)
{
    ByteIOContext *pb = &s->pb;
    FLVContext *flv = s->priv_data;
    int i, width, height, samplerate;
    double framerate = 0.0;
    int metadata_size_pos, data_size;

    flv->hasAudio = 0;
    flv->hasVideo = 0;

    put_tag(pb,"FLV");
    put_byte(pb,1);
    put_byte(pb,0); // delayed write
    put_be32(pb,9);
    put_be32(pb,0);

    for(i=0; i<s->nb_streams; i++){
        AVCodecContext *enc = s->streams[i]->codec;
        if (enc->codec_type == CODEC_TYPE_VIDEO) {
            width = enc->width;
            height = enc->height;
            if (s->streams[i]->r_frame_rate.den && s->streams[i]->r_frame_rate.num) {
                framerate = av_q2d(s->streams[i]->r_frame_rate);
            } else {
                framerate = 1/av_q2d(s->streams[i]->codec->time_base);
            }
            flv->hasVideo=1;
        } else {
            flv->hasAudio=1;
            samplerate = enc->sample_rate;
        }
        av_set_pts_info(s->streams[i], 24, 1, 1000); /* 24 bit pts in ms */
        if(enc->codec_tag == 5){
            put_byte(pb,8); // message type
            put_be24(pb,0); // include flags
            put_be24(pb,0); // time stamp
            put_be32(pb,0); // reserved
            put_be32(pb,11); // size
            flv->reserved=5;
        }
        if(enc->codec_type == CODEC_TYPE_AUDIO && get_audio_flags(enc)<0)
            return -1;
    }

    /* write meta_tag */
    put_byte(pb, 18);         // tag type META
    metadata_size_pos= url_ftell(pb);
    put_be24(pb, 0);          // size of data part (sum of all parts below)
    put_be24(pb, 0);          // time stamp
    put_be32(pb, 0);          // reserved

    /* now data of data_size size */

    /* first event name as a string */
    put_byte(pb, AMF_STRING); // 1 byte
    put_amf_string(pb, "onMetaData"); // 12 bytes

    /* mixed array (hash) with size and string/type/data tuples */
    put_byte(pb, AMF_MIXED_ARRAY);
    put_be32(pb, 4*flv->hasVideo + flv->hasAudio + (!!s->file_size) + (s->duration != AV_NOPTS_VALUE && s->duration));

    if (s->duration != AV_NOPTS_VALUE && s->duration) {
        put_amf_string(pb, "duration");
        put_amf_double(pb, s->duration / (double)AV_TIME_BASE);
    }

    if(flv->hasVideo){
        put_amf_string(pb, "width");
        put_amf_double(pb, width);

        put_amf_string(pb, "height");
        put_amf_double(pb, height);

        put_amf_string(pb, "videodatarate");
        put_amf_double(pb, s->bit_rate / 1024.0);

        put_amf_string(pb, "framerate");
        put_amf_double(pb, framerate);
    }

    if(flv->hasAudio){
        put_amf_string(pb, "audiosamplerate");
        put_amf_double(pb, samplerate);
    }

    if(s->file_size){
        put_amf_string(pb, "filesize");
        put_amf_double(pb, s->file_size);
    }

    put_amf_string(pb, "");
    put_byte(pb, 9); // end marker 1 byte

    /* write total size of tag */
    data_size= url_ftell(pb) - metadata_size_pos - 10;
    url_fseek(pb, metadata_size_pos, SEEK_SET);
    put_be24(pb, data_size);
    url_fseek(pb, data_size + 10 - 3, SEEK_CUR);
    put_be32(pb, data_size + 11);

    return 0;
}

static int flv_write_trailer(AVFormatContext *s)
{
    int64_t file_size;
    int flags = 0;

    ByteIOContext *pb = &s->pb;
    FLVContext *flv = s->priv_data;

    file_size = url_ftell(pb);
    flags |= flv->hasAudio ? 4 : 0;
    flags |= flv->hasVideo ? 1 : 0;
    url_fseek(pb, 4, SEEK_SET);
    put_byte(pb,flags);
    url_fseek(pb, file_size, SEEK_SET);
    return 0;
}

static int flv_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    ByteIOContext *pb = &s->pb;
    AVCodecContext *enc = s->streams[pkt->stream_index]->codec;
    FLVContext *flv = s->priv_data;
    int size= pkt->size;
    int flags;

//    av_log(s, AV_LOG_DEBUG, "type:%d pts: %lld size:%d\n", enc->codec_type, timestamp, size);

    if (enc->codec_type == CODEC_TYPE_VIDEO) {
        put_byte(pb, 9);
        flags = 2; // choose h263
        flags |= pkt->flags & PKT_FLAG_KEY ? 0x10 : 0x20; // add keyframe indicator
    } else {
        assert(enc->codec_type == CODEC_TYPE_AUDIO);
        flags = get_audio_flags(enc);

        assert(size);

        put_byte(pb, 8);
    }

    put_be24(pb,size+1); // include flags
    put_be24(pb,pkt->pts);
    put_be32(pb,flv->reserved);
    put_byte(pb,flags);
    put_buffer(pb, pkt->data, size);
    put_be32(pb,size+1+11); // previous tag size

    put_flush_packet(pb);
    return 0;
}

AVOutputFormat flv_muxer = {
    "flv",
    "flv format",
    "video/x-flv",
    "flv",
    sizeof(FLVContext),
#ifdef CONFIG_MP3LAME
    CODEC_ID_MP3,
#else // CONFIG_MP3LAME
    CODEC_ID_NONE,
#endif // CONFIG_MP3LAME
    CODEC_ID_FLV1,
    flv_write_header,
    flv_write_packet,
    flv_write_trailer,
};
