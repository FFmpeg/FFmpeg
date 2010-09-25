/*
 * FLV muxer
 * Copyright (c) 2003 The FFmpeg Project
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
#include "internal.h"
#include "avc.h"
#include "metadata.h"

#undef NDEBUG
#include <assert.h>

static const AVCodecTag flv_video_codec_ids[] = {
    {CODEC_ID_FLV1,    FLV_CODECID_H263  },
    {CODEC_ID_FLASHSV, FLV_CODECID_SCREEN},
    {CODEC_ID_FLASHSV2, FLV_CODECID_SCREEN2},
    {CODEC_ID_VP6F,    FLV_CODECID_VP6   },
    {CODEC_ID_VP6,     FLV_CODECID_VP6   },
    {CODEC_ID_H264,    FLV_CODECID_H264  },
    {CODEC_ID_NONE,    0}
};

static const AVCodecTag flv_audio_codec_ids[] = {
    {CODEC_ID_MP3,       FLV_CODECID_MP3    >> FLV_AUDIO_CODECID_OFFSET},
    {CODEC_ID_PCM_U8,    FLV_CODECID_PCM    >> FLV_AUDIO_CODECID_OFFSET},
    {CODEC_ID_PCM_S16BE, FLV_CODECID_PCM    >> FLV_AUDIO_CODECID_OFFSET},
    {CODEC_ID_PCM_S16LE, FLV_CODECID_PCM_LE >> FLV_AUDIO_CODECID_OFFSET},
    {CODEC_ID_ADPCM_SWF, FLV_CODECID_ADPCM  >> FLV_AUDIO_CODECID_OFFSET},
    {CODEC_ID_AAC,       FLV_CODECID_AAC    >> FLV_AUDIO_CODECID_OFFSET},
    {CODEC_ID_NELLYMOSER, FLV_CODECID_NELLYMOSER >> FLV_AUDIO_CODECID_OFFSET},
    {CODEC_ID_SPEEX,     FLV_CODECID_SPEEX  >> FLV_AUDIO_CODECID_OFFSET},
    {CODEC_ID_NONE,      0}
};

typedef struct FLVContext {
    int reserved;
    int64_t duration_offset;
    int64_t filesize_offset;
    int64_t duration;
    int delay; ///< first dts delay for AVC
    int64_t last_video_ts;
} FLVContext;

static int get_audio_flags(AVCodecContext *enc){
    int flags = (enc->bits_per_coded_sample == 16) ? FLV_SAMPLESSIZE_16BIT : FLV_SAMPLESSIZE_8BIT;

    if (enc->codec_id == CODEC_ID_AAC) // specs force these parameters
        return FLV_CODECID_AAC | FLV_SAMPLERATE_44100HZ | FLV_SAMPLESSIZE_16BIT | FLV_STEREO;
    else if (enc->codec_id == CODEC_ID_SPEEX) {
        if (enc->sample_rate != 16000) {
            av_log(enc, AV_LOG_ERROR, "flv only supports wideband (16kHz) Speex audio\n");
            return -1;
        }
        if (enc->channels != 1) {
            av_log(enc, AV_LOG_ERROR, "flv only supports mono Speex audio\n");
            return -1;
        }
        if (enc->frame_size / 320 > 8) {
            av_log(enc, AV_LOG_WARNING, "Warning: Speex stream has more than "
                                        "8 frames per packet. Adobe Flash "
                                        "Player cannot handle this!\n");
        }
        return FLV_CODECID_SPEEX | FLV_SAMPLERATE_11025HZ | FLV_SAMPLESSIZE_16BIT;
    } else {
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
    }

    if (enc->channels > 1) {
        flags |= FLV_STEREO;
    }

    switch(enc->codec_id){
    case CODEC_ID_MP3:
        flags |= FLV_CODECID_MP3    | FLV_SAMPLESSIZE_16BIT;
        break;
    case CODEC_ID_PCM_U8:
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
    case CODEC_ID_NELLYMOSER:
        if (enc->sample_rate == 8000) {
            flags |= FLV_CODECID_NELLYMOSER_8KHZ_MONO | FLV_SAMPLESSIZE_16BIT;
        } else {
            flags |= FLV_CODECID_NELLYMOSER | FLV_SAMPLESSIZE_16BIT;
        }
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

static void put_avc_eos_tag(ByteIOContext *pb, unsigned ts) {
    put_byte(pb, FLV_TAG_TYPE_VIDEO);
    put_be24(pb, 5);  /* Tag Data Size */
    put_be24(pb, ts);  /* lower 24 bits of timestamp in ms*/
    put_byte(pb, (ts >> 24) & 0x7F);  /* MSB of ts in ms*/
    put_be24(pb, 0);  /* StreamId = 0 */
    put_byte(pb, 23);  /* ub[4] FrameType = 1, ub[4] CodecId = 7 */
    put_byte(pb, 2);  /* AVC end of sequence */
    put_be24(pb, 0);  /* Always 0 for AVC EOS. */
    put_be32(pb, 16);  /* Size of FLV tag */
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
    AVCodecContext *audio_enc = NULL, *video_enc = NULL;
    int i;
    double framerate = 0.0;
    int metadata_size_pos, data_size;
    AVMetadataTag *tag = NULL;

    for(i=0; i<s->nb_streams; i++){
        AVCodecContext *enc = s->streams[i]->codec;
        if (enc->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (s->streams[i]->r_frame_rate.den && s->streams[i]->r_frame_rate.num) {
                framerate = av_q2d(s->streams[i]->r_frame_rate);
            } else {
                framerate = 1/av_q2d(s->streams[i]->codec->time_base);
            }
            video_enc = enc;
            if(enc->codec_tag == 0) {
                av_log(enc, AV_LOG_ERROR, "video codec not compatible with flv\n");
                return -1;
            }
        } else {
            audio_enc = enc;
            if(get_audio_flags(enc)<0)
                return -1;
        }
        av_set_pts_info(s->streams[i], 32, 1, 1000); /* 32 bit pts in ms */
    }
    put_tag(pb,"FLV");
    put_byte(pb,1);
    put_byte(pb,   FLV_HEADER_FLAG_HASAUDIO * !!audio_enc
                 + FLV_HEADER_FLAG_HASVIDEO * !!video_enc);
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

    flv->last_video_ts = -1;

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
    put_be32(pb, 5*!!video_enc + 5*!!audio_enc + 2); // +2 for duration and file size

    put_amf_string(pb, "duration");
    flv->duration_offset= url_ftell(pb);
    put_amf_double(pb, s->duration / AV_TIME_BASE); // fill in the guessed duration, it'll be corrected later if incorrect

    if(video_enc){
        put_amf_string(pb, "width");
        put_amf_double(pb, video_enc->width);

        put_amf_string(pb, "height");
        put_amf_double(pb, video_enc->height);

        put_amf_string(pb, "videodatarate");
        put_amf_double(pb, video_enc->bit_rate / 1024.0);

        put_amf_string(pb, "framerate");
        put_amf_double(pb, framerate);

        put_amf_string(pb, "videocodecid");
        put_amf_double(pb, video_enc->codec_tag);
    }

    if(audio_enc){
        put_amf_string(pb, "audiodatarate");
        put_amf_double(pb, audio_enc->bit_rate / 1024.0);

        put_amf_string(pb, "audiosamplerate");
        put_amf_double(pb, audio_enc->sample_rate);

        put_amf_string(pb, "audiosamplesize");
        put_amf_double(pb, audio_enc->codec_id == CODEC_ID_PCM_U8 ? 8 : 16);

        put_amf_string(pb, "stereo");
        put_amf_bool(pb, audio_enc->channels == 2);

        put_amf_string(pb, "audiocodecid");
        put_amf_double(pb, audio_enc->codec_tag);
    }

    while ((tag = av_metadata_get(s->metadata, "", tag, AV_METADATA_IGNORE_SUFFIX))) {
        put_amf_string(pb, tag->key);
        put_byte(pb, AMF_DATA_TYPE_STRING);
        put_amf_string(pb, tag->value);
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

    for (i = 0; i < s->nb_streams; i++) {
        AVCodecContext *enc = s->streams[i]->codec;
        if (enc->codec_id == CODEC_ID_AAC || enc->codec_id == CODEC_ID_H264) {
            int64_t pos;
            put_byte(pb, enc->codec_type == AVMEDIA_TYPE_VIDEO ?
                     FLV_TAG_TYPE_VIDEO : FLV_TAG_TYPE_AUDIO);
            put_be24(pb, 0); // size patched later
            put_be24(pb, 0); // ts
            put_byte(pb, 0); // ts ext
            put_be24(pb, 0); // streamid
            pos = url_ftell(pb);
            if (enc->codec_id == CODEC_ID_AAC) {
                put_byte(pb, get_audio_flags(enc));
                put_byte(pb, 0); // AAC sequence header
                put_buffer(pb, enc->extradata, enc->extradata_size);
            } else {
                put_byte(pb, enc->codec_tag | FLV_FRAME_KEY); // flags
                put_byte(pb, 0); // AVC sequence header
                put_be24(pb, 0); // composition time
                ff_isom_write_avcc(pb, enc->extradata, enc->extradata_size);
            }
            data_size = url_ftell(pb) - pos;
            url_fseek(pb, -data_size - 10, SEEK_CUR);
            put_be24(pb, data_size);
            url_fseek(pb, data_size + 10 - 3, SEEK_CUR);
            put_be32(pb, data_size + 11); // previous tag size
        }
    }

    return 0;
}

static int flv_write_trailer(AVFormatContext *s)
{
    int64_t file_size;

    ByteIOContext *pb = s->pb;
    FLVContext *flv = s->priv_data;
    int i;

    /* Add EOS tag */
    for (i = 0; i < s->nb_streams; i++) {
        AVCodecContext *enc = s->streams[i]->codec;
        if (enc->codec_type == AVMEDIA_TYPE_VIDEO &&
                enc->codec_id == CODEC_ID_H264) {
            put_avc_eos_tag(pb, flv->last_video_ts);
        }
    }

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
    unsigned ts;
    int size= pkt->size;
    uint8_t *data= NULL;
    int flags, flags_size;

//    av_log(s, AV_LOG_DEBUG, "type:%d pts: %"PRId64" size:%d\n", enc->codec_type, timestamp, size);

    if(enc->codec_id == CODEC_ID_VP6 || enc->codec_id == CODEC_ID_VP6F ||
       enc->codec_id == CODEC_ID_AAC)
        flags_size= 2;
    else if(enc->codec_id == CODEC_ID_H264)
        flags_size= 5;
    else
        flags_size= 1;

    if (enc->codec_type == AVMEDIA_TYPE_VIDEO) {
        put_byte(pb, FLV_TAG_TYPE_VIDEO);

        flags = enc->codec_tag;
        if(flags == 0) {
            av_log(enc, AV_LOG_ERROR, "video codec %X not compatible with flv\n",enc->codec_id);
            return -1;
        }

        flags |= pkt->flags & AV_PKT_FLAG_KEY ? FLV_FRAME_KEY : FLV_FRAME_INTER;
    } else {
        assert(enc->codec_type == AVMEDIA_TYPE_AUDIO);
        flags = get_audio_flags(enc);

        assert(size);

        put_byte(pb, FLV_TAG_TYPE_AUDIO);
    }

    if (enc->codec_id == CODEC_ID_H264) {
        /* check if extradata looks like mp4 formated */
        if (enc->extradata_size > 0 && *(uint8_t*)enc->extradata != 1) {
            if (ff_avc_parse_nal_units_buf(pkt->data, &data, &size) < 0)
                return -1;
        }
        if (!flv->delay && pkt->dts < 0)
            flv->delay = -pkt->dts;
    }

    ts = pkt->dts + flv->delay; // add delay to force positive dts
    if (enc->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (flv->last_video_ts < ts)
            flv->last_video_ts = ts;
    }
    put_be24(pb,size + flags_size);
    put_be24(pb,ts);
    put_byte(pb,(ts >> 24) & 0x7F); // timestamps are 32bits _signed_
    put_be24(pb,flv->reserved);
    put_byte(pb,flags);
    if (enc->codec_id == CODEC_ID_VP6)
        put_byte(pb,0);
    if (enc->codec_id == CODEC_ID_VP6F)
        put_byte(pb, enc->extradata_size ? enc->extradata[0] : 0);
    else if (enc->codec_id == CODEC_ID_AAC)
        put_byte(pb,1); // AAC raw
    else if (enc->codec_id == CODEC_ID_H264) {
        put_byte(pb,1); // AVC NALU
        put_be24(pb,pkt->pts - pkt->dts);
    }

    put_buffer(pb, data ? data : pkt->data, size);

    put_be32(pb,size+flags_size+11); // previous tag size
    flv->duration = FFMAX(flv->duration, pkt->pts + flv->delay + pkt->duration);

    put_flush_packet(pb);

    av_free(data);

    return 0;
}

AVOutputFormat flv_muxer = {
    "flv",
    NULL_IF_CONFIG_SMALL("FLV format"),
    "video/x-flv",
    "flv",
    sizeof(FLVContext),
#if CONFIG_LIBMP3LAME
    CODEC_ID_MP3,
#else // CONFIG_LIBMP3LAME
    CODEC_ID_ADPCM_SWF,
#endif // CONFIG_LIBMP3LAME
    CODEC_ID_FLV1,
    flv_write_header,
    flv_write_packet,
    flv_write_trailer,
    .codec_tag= (const AVCodecTag* const []){flv_video_codec_ids, flv_audio_codec_ids, 0},
    .flags= AVFMT_GLOBALHEADER | AVFMT_VARIABLE_FPS,
};
