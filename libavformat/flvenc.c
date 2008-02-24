/*
 * FLV muxer
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
 */
#include "avformat.h"
#include "flv.h"
#include "riff.h"

#undef NDEBUG
#include <assert.h>

static const AVCodecTag flv_video_codec_ids[] = {
    {CODEC_ID_FLV1,    FLV_CODECID_H263  },
    {CODEC_ID_FLASHSV, FLV_CODECID_SCREEN},
    {CODEC_ID_VP6F,    FLV_CODECID_VP6   },
    {CODEC_ID_VP6,     FLV_CODECID_VP6   },
    {CODEC_ID_NONE,    0}
};

static const AVCodecTag flv_audio_codec_ids[] = {
    {CODEC_ID_MP3,       FLV_CODECID_MP3    >> FLV_AUDIO_CODECID_OFFSET},
    {CODEC_ID_PCM_S8,    FLV_CODECID_PCM    >> FLV_AUDIO_CODECID_OFFSET},
    {CODEC_ID_PCM_S16BE, FLV_CODECID_PCM    >> FLV_AUDIO_CODECID_OFFSET},
    {CODEC_ID_PCM_S16LE, FLV_CODECID_PCM_LE >> FLV_AUDIO_CODECID_OFFSET},
    {CODEC_ID_ADPCM_SWF, FLV_CODECID_ADPCM  >> FLV_AUDIO_CODECID_OFFSET},
    {CODEC_ID_NONE,      0}
};

typedef struct FLVContext {
    int hasAudio;
    int hasVideo;
    int reserved;
    offset_t duration_offset;
    offset_t filesize_offset;
    int64_t duration;
} FLVContext;

static int get_audio_flags(AVCodecContext *enc){
    int flags = (enc->bits_per_sample == 16) ? FLV_SAMPLESSIZE_16BIT : FLV_SAMPLESSIZE_8BIT;

    switch (enc->sample_rate) {
        case    44100:
            flags |= FLV_SAMPLERATE_44100HZ;
            break;
        case    22050:
            flags |= FLV_SAMPLERATE_22050HZ;
            break;
        case    11025:
            flags |= FLV_SAMPLERATE_11025HZ;
            break;
        case     8000: //nellymoser only
        case     5512: //not mp3
            if(enc->codec_id != CODEC_ID_MP3){
                flags |= FLV_SAMPLERATE_SPECIAL;
                break;
            }
        default:
            av_log(enc, AV_LOG_ERROR, "flv does not support that sample rate, choose from (44100, 22050, 11025).\n");
            return -1;
    }

    if (enc->channels > 1) {
        flags |= FLV_STEREO;
    }

    switch(enc->codec_id){
    case CODEC_ID_MP3:
        flags |= FLV_CODECID_MP3    | FLV_SAMPLESSIZE_16BIT;
        break;
    case CODEC_ID_PCM_S8:
        flags |= FLV_CODECID_PCM    | FLV_SAMPLESSIZE_8BIT;
        break;
    case CODEC_ID_PCM_S16BE:
        flags |= FLV_CODECID_PCM    | FLV_SAMPLESSIZE_16BIT;
        break;
    case CODEC_ID_PCM_S16LE:
        flags |= FLV_CODECID_PCM_LE | FLV_SAMPLESSIZE_16BIT;
        break;
    case CODEC_ID_ADPCM_SWF:
        flags |= FLV_CODECID_ADPCM | FLV_SAMPLESSIZE_16BIT;
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

static void put_amf_string(ByteIOContext *pb, const char *str)
{
    size_t len = strlen(str);
    put_be16(pb, len);
    put_buffer(pb, str, len);
}

static void put_amf_double(ByteIOContext *pb, double d)
{
    put_byte(pb, AMF_DATA_TYPE_NUMBER);
    put_be64(pb, av_dbl2int(d));
}

static void put_amf_bool(ByteIOContext *pb, int b) {
    put_byte(pb, AMF_DATA_TYPE_BOOL);
    put_byte(pb, !!b);
}

static int flv_write_header(AVFormatContext *s)
{
    ByteIOContext *pb = s->pb;
    FLVContext *flv = s->priv_data;
    int i, width, height, samplerate, samplesize, channels, audiocodecid, videocodecid;
    double framerate = 0.0;
    int metadata_size_pos, data_size;

    flv->hasAudio = 0;
    flv->hasVideo = 0;

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

            videocodecid = enc->codec_tag;
            if(videocodecid == 0) {
                av_log(enc, AV_LOG_ERROR, "video codec not compatible with flv\n");
                return -1;
            }
        } else {
            flv->hasAudio=1;
            samplerate = enc->sample_rate;
            channels = enc->channels;

            audiocodecid = enc->codec_tag;
            samplesize = (enc->codec_id == CODEC_ID_PCM_S8) ? 8 : 16;

            if(get_audio_flags(enc)<0)
                return -1;
        }
        av_set_pts_info(s->streams[i], 24, 1, 1000); /* 24 bit pts in ms */
    }
    put_tag(pb,"FLV");
    put_byte(pb,1);
    put_byte(pb,   FLV_HEADER_FLAG_HASAUDIO * flv->hasAudio
                 + FLV_HEADER_FLAG_HASVIDEO * flv->hasVideo);
    put_be32(pb,9);
    put_be32(pb,0);

    for(i=0; i<s->nb_streams; i++){
        if(s->streams[i]->codec->codec_tag == 5){
            put_byte(pb,8); // message type
            put_be24(pb,0); // include flags
            put_be24(pb,0); // time stamp
            put_be32(pb,0); // reserved
            put_be32(pb,11); // size
            flv->reserved=5;
        }
    }

    /* write meta_tag */
    put_byte(pb, 18);         // tag type META
    metadata_size_pos= url_ftell(pb);
    put_be24(pb, 0);          // size of data part (sum of all parts below)
    put_be24(pb, 0);          // time stamp
    put_be32(pb, 0);          // reserved

    /* now data of data_size size */

    /* first event name as a string */
    put_byte(pb, AMF_DATA_TYPE_STRING);
    put_amf_string(pb, "onMetaData"); // 12 bytes

    /* mixed array (hash) with size and string/type/data tuples */
    put_byte(pb, AMF_DATA_TYPE_MIXEDARRAY);
    put_be32(pb, 5*flv->hasVideo + 4*flv->hasAudio + 2); // +2 for duration and file size

    put_amf_string(pb, "duration");
    flv->duration_offset= url_ftell(pb);
    put_amf_double(pb, 0); // delayed write

    if(flv->hasVideo){
        put_amf_string(pb, "width");
        put_amf_double(pb, width);

        put_amf_string(pb, "height");
        put_amf_double(pb, height);

        put_amf_string(pb, "videodatarate");
        put_amf_double(pb, s->bit_rate / 1024.0);

        put_amf_string(pb, "framerate");
        put_amf_double(pb, framerate);

        put_amf_string(pb, "videocodecid");
        put_amf_double(pb, videocodecid);
    }

    if(flv->hasAudio){
        put_amf_string(pb, "audiosamplerate");
        put_amf_double(pb, samplerate);

        put_amf_string(pb, "audiosamplesize");
        put_amf_double(pb, samplesize);

        put_amf_string(pb, "stereo");
        put_amf_bool(pb, (channels == 2));

        put_amf_string(pb, "audiocodecid");
        put_amf_double(pb, audiocodecid);
    }

    put_amf_string(pb, "filesize");
    flv->filesize_offset= url_ftell(pb);
    put_amf_double(pb, 0); // delayed write

    put_amf_string(pb, "");
    put_byte(pb, AMF_END_OF_OBJECT);

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

    ByteIOContext *pb = s->pb;
    FLVContext *flv = s->priv_data;

    file_size = url_ftell(pb);

    /* update informations */
    url_fseek(pb, flv->duration_offset, SEEK_SET);
    put_amf_double(pb, flv->duration / (double)1000);
    url_fseek(pb, flv->filesize_offset, SEEK_SET);
    put_amf_double(pb, file_size);

    url_fseek(pb, file_size, SEEK_SET);
    return 0;
}

static int flv_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    ByteIOContext *pb = s->pb;
    AVCodecContext *enc = s->streams[pkt->stream_index]->codec;
    FLVContext *flv = s->priv_data;
    int size= pkt->size;
    int flags, flags_size;

//    av_log(s, AV_LOG_DEBUG, "type:%d pts: %"PRId64" size:%d\n", enc->codec_type, timestamp, size);

    if(enc->codec_id == CODEC_ID_VP6 || enc->codec_id == CODEC_ID_VP6F)
        flags_size= 2;
    else
        flags_size= 1;

    if (enc->codec_type == CODEC_TYPE_VIDEO) {
        put_byte(pb, FLV_TAG_TYPE_VIDEO);

        flags = enc->codec_tag;
        if(flags == 0) {
            av_log(enc, AV_LOG_ERROR, "video codec %X not compatible with flv\n",enc->codec_id);
            return -1;
        }

        flags |= pkt->flags & PKT_FLAG_KEY ? FLV_FRAME_KEY : FLV_FRAME_INTER;
    } else {
        assert(enc->codec_type == CODEC_TYPE_AUDIO);
        flags = get_audio_flags(enc);

        assert(size);

        put_byte(pb, FLV_TAG_TYPE_AUDIO);
    }

    put_be24(pb,size + flags_size);
    put_be24(pb,pkt->pts);
    put_byte(pb,pkt->pts >> 24);
    put_be24(pb,flv->reserved);
    put_byte(pb,flags);
    if (enc->codec_id == CODEC_ID_VP6)
        put_byte(pb,0);
    if (enc->codec_id == CODEC_ID_VP6F)
        put_byte(pb, enc->extradata_size ? enc->extradata[0] : 0);
    put_buffer(pb, pkt->data, size);
    put_be32(pb,size+flags_size+11); // previous tag size
    flv->duration = pkt->pts + pkt->duration;

    put_flush_packet(pb);
    return 0;
}

AVOutputFormat flv_muxer = {
    "flv",
    "flv format",
    "video/x-flv",
    "flv",
    sizeof(FLVContext),
#ifdef CONFIG_LIBMP3LAME
    CODEC_ID_MP3,
#else // CONFIG_LIBMP3LAME
    CODEC_ID_ADPCM_SWF,
#endif // CONFIG_LIBMP3LAME
    CODEC_ID_FLV1,
    flv_write_header,
    flv_write_packet,
    flv_write_trailer,
    .codec_tag= (const AVCodecTag*[]){flv_video_codec_ids, flv_audio_codec_ids, 0},
};
