/*
 * FLV muxer
 * Copyright (c) 2003 The Libav Project
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/dict.h"
#include "libavutil/intfloat.h"
#include "avc.h"
#include "avformat.h"
#include "flv.h"
#include "internal.h"
#include "metadata.h"

#undef NDEBUG
#include <assert.h>

static const AVCodecTag flv_video_codec_ids[] = {
    { AV_CODEC_ID_FLV1,     FLV_CODECID_H263 },
    { AV_CODEC_ID_FLASHSV,  FLV_CODECID_SCREEN },
    { AV_CODEC_ID_FLASHSV2, FLV_CODECID_SCREEN2 },
    { AV_CODEC_ID_VP6F,     FLV_CODECID_VP6 },
    { AV_CODEC_ID_VP6A,     FLV_CODECID_VP6A },
    { AV_CODEC_ID_H264,     FLV_CODECID_H264 },
    { AV_CODEC_ID_NONE,     0 }
};

static const AVCodecTag flv_audio_codec_ids[] = {
    { AV_CODEC_ID_MP3,        FLV_CODECID_MP3        >> FLV_AUDIO_CODECID_OFFSET },
    { AV_CODEC_ID_PCM_U8,     FLV_CODECID_PCM        >> FLV_AUDIO_CODECID_OFFSET },
    { AV_CODEC_ID_PCM_S16BE,  FLV_CODECID_PCM        >> FLV_AUDIO_CODECID_OFFSET },
    { AV_CODEC_ID_PCM_S16LE,  FLV_CODECID_PCM_LE     >> FLV_AUDIO_CODECID_OFFSET },
    { AV_CODEC_ID_ADPCM_SWF,  FLV_CODECID_ADPCM      >> FLV_AUDIO_CODECID_OFFSET },
    { AV_CODEC_ID_AAC,        FLV_CODECID_AAC        >> FLV_AUDIO_CODECID_OFFSET },
    { AV_CODEC_ID_NELLYMOSER, FLV_CODECID_NELLYMOSER >> FLV_AUDIO_CODECID_OFFSET },
    { AV_CODEC_ID_PCM_MULAW,  FLV_CODECID_PCM_MULAW  >> FLV_AUDIO_CODECID_OFFSET },
    { AV_CODEC_ID_PCM_ALAW,   FLV_CODECID_PCM_ALAW   >> FLV_AUDIO_CODECID_OFFSET },
    { AV_CODEC_ID_SPEEX,      FLV_CODECID_SPEEX      >> FLV_AUDIO_CODECID_OFFSET },
    { AV_CODEC_ID_NONE,       0 }
};

typedef struct FLVContext {
    int     reserved;
    int64_t duration_offset;
    int64_t filesize_offset;
    int64_t duration;
    int64_t delay;      ///< first dts delay (needed for AVC & Speex)
} FLVContext;

typedef struct FLVStreamContext {
    int64_t last_ts;    ///< last timestamp for each stream
} FLVStreamContext;

static int get_audio_flags(AVFormatContext *s, AVCodecContext *enc)
{
    int flags = (enc->bits_per_coded_sample == 16) ? FLV_SAMPLESSIZE_16BIT
                                                   : FLV_SAMPLESSIZE_8BIT;

    if (enc->codec_id == AV_CODEC_ID_AAC) // specs force these parameters
        return FLV_CODECID_AAC | FLV_SAMPLERATE_44100HZ |
               FLV_SAMPLESSIZE_16BIT | FLV_STEREO;
    else if (enc->codec_id == AV_CODEC_ID_SPEEX) {
        if (enc->sample_rate != 16000) {
            av_log(s, AV_LOG_ERROR,
                   "flv only supports wideband (16kHz) Speex audio\n");
            return -1;
        }
        if (enc->channels != 1) {
            av_log(s, AV_LOG_ERROR, "flv only supports mono Speex audio\n");
            return -1;
        }
        return FLV_CODECID_SPEEX | FLV_SAMPLERATE_11025HZ | FLV_SAMPLESSIZE_16BIT;
    } else {
        switch (enc->sample_rate) {
        case 44100:
            flags |= FLV_SAMPLERATE_44100HZ;
            break;
        case 22050:
            flags |= FLV_SAMPLERATE_22050HZ;
            break;
        case 11025:
            flags |= FLV_SAMPLERATE_11025HZ;
            break;
        case 16000: // nellymoser only
        case  8000: // nellymoser only
        case  5512: // not MP3
            if (enc->codec_id != AV_CODEC_ID_MP3) {
                flags |= FLV_SAMPLERATE_SPECIAL;
                break;
            }
        default:
            av_log(s, AV_LOG_ERROR,
                   "flv does not support that sample rate, "
                   "choose from (44100, 22050, 11025).\n");
            return -1;
        }
    }

    if (enc->channels > 1)
        flags |= FLV_STEREO;

    switch (enc->codec_id) {
    case AV_CODEC_ID_MP3:
        flags |= FLV_CODECID_MP3    | FLV_SAMPLESSIZE_16BIT;
        break;
    case AV_CODEC_ID_PCM_U8:
        flags |= FLV_CODECID_PCM    | FLV_SAMPLESSIZE_8BIT;
        break;
    case AV_CODEC_ID_PCM_S16BE:
        flags |= FLV_CODECID_PCM    | FLV_SAMPLESSIZE_16BIT;
        break;
    case AV_CODEC_ID_PCM_S16LE:
        flags |= FLV_CODECID_PCM_LE | FLV_SAMPLESSIZE_16BIT;
        break;
    case AV_CODEC_ID_ADPCM_SWF:
        flags |= FLV_CODECID_ADPCM  | FLV_SAMPLESSIZE_16BIT;
        break;
    case AV_CODEC_ID_NELLYMOSER:
        if (enc->sample_rate == 8000)
            flags |= FLV_CODECID_NELLYMOSER_8KHZ_MONO  | FLV_SAMPLESSIZE_16BIT;
        else if (enc->sample_rate == 16000)
            flags |= FLV_CODECID_NELLYMOSER_16KHZ_MONO | FLV_SAMPLESSIZE_16BIT;
        else
            flags |= FLV_CODECID_NELLYMOSER            | FLV_SAMPLESSIZE_16BIT;
        break;
    case AV_CODEC_ID_PCM_MULAW:
        flags = FLV_CODECID_PCM_MULAW | FLV_SAMPLERATE_SPECIAL | FLV_SAMPLESSIZE_16BIT;
        break;
    case AV_CODEC_ID_PCM_ALAW:
        flags = FLV_CODECID_PCM_ALAW  | FLV_SAMPLERATE_SPECIAL | FLV_SAMPLESSIZE_16BIT;
        break;
    case 0:
        flags |= enc->codec_tag << 4;
        break;
    default:
        av_log(s, AV_LOG_ERROR, "codec not compatible with flv\n");
        return -1;
    }

    return flags;
}

static void put_amf_string(AVIOContext *pb, const char *str)
{
    size_t len = strlen(str);
    avio_wb16(pb, len);
    avio_write(pb, str, len);
}

static void put_avc_eos_tag(AVIOContext *pb, unsigned ts)
{
    avio_w8(pb, FLV_TAG_TYPE_VIDEO);
    avio_wb24(pb, 5);               /* Tag Data Size */
    avio_wb24(pb, ts);              /* lower 24 bits of timestamp in ms */
    avio_w8(pb, (ts >> 24) & 0x7F); /* MSB of ts in ms */
    avio_wb24(pb, 0);               /* StreamId = 0 */
    avio_w8(pb, 23);                /* ub[4] FrameType = 1, ub[4] CodecId = 7 */
    avio_w8(pb, 2);                 /* AVC end of sequence */
    avio_wb24(pb, 0);               /* Always 0 for AVC EOS. */
    avio_wb32(pb, 16);              /* Size of FLV tag */
}

static void put_amf_double(AVIOContext *pb, double d)
{
    avio_w8(pb, AMF_DATA_TYPE_NUMBER);
    avio_wb64(pb, av_double2int(d));
}

static void put_amf_bool(AVIOContext *pb, int b)
{
    avio_w8(pb, AMF_DATA_TYPE_BOOL);
    avio_w8(pb, !!b);
}

static int flv_write_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    FLVContext *flv = s->priv_data;
    AVCodecContext *audio_enc = NULL, *video_enc = NULL, *data_enc = NULL;
    int i, metadata_count = 0;
    double framerate = 0.0;
    int64_t metadata_size_pos, data_size, metadata_count_pos;
    AVDictionaryEntry *tag = NULL;

    for (i = 0; i < s->nb_streams; i++) {
        AVCodecContext *enc = s->streams[i]->codec;
        FLVStreamContext *sc;
        switch (enc->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            if (s->streams[i]->avg_frame_rate.den &&
                s->streams[i]->avg_frame_rate.num) {
                framerate = av_q2d(s->streams[i]->avg_frame_rate);
            } else {
                framerate = 1 / av_q2d(s->streams[i]->codec->time_base);
            }
            if (video_enc) {
                av_log(s, AV_LOG_ERROR,
                       "at most one video stream is supported in flv\n");
                return AVERROR(EINVAL);
            }
            video_enc = enc;
            if (enc->codec_tag == 0) {
                av_log(s, AV_LOG_ERROR, "video codec not compatible with flv\n");
                return -1;
            }
            break;
        case AVMEDIA_TYPE_AUDIO:
            if (audio_enc) {
                av_log(s, AV_LOG_ERROR,
                       "at most one audio stream is supported in flv\n");
                return AVERROR(EINVAL);
            }
            audio_enc = enc;
            if (get_audio_flags(s, enc) < 0)
                return AVERROR_INVALIDDATA;
            break;
        case AVMEDIA_TYPE_DATA:
            if (enc->codec_id != AV_CODEC_ID_TEXT) {
                av_log(s, AV_LOG_ERROR, "codec not compatible with flv\n");
                return AVERROR_INVALIDDATA;
            }
            data_enc = enc;
            break;
        default:
            av_log(s, AV_LOG_ERROR, "codec not compatible with flv\n");
            return -1;
        }
        avpriv_set_pts_info(s->streams[i], 32, 1, 1000); /* 32 bit pts in ms */

        sc = av_mallocz(sizeof(FLVStreamContext));
        if (!sc)
            return AVERROR(ENOMEM);
        s->streams[i]->priv_data = sc;
        sc->last_ts = -1;
    }

    flv->delay = AV_NOPTS_VALUE;

    avio_write(pb, "FLV", 3);
    avio_w8(pb, 1);
    avio_w8(pb, FLV_HEADER_FLAG_HASAUDIO * !!audio_enc +
                FLV_HEADER_FLAG_HASVIDEO * !!video_enc);
    avio_wb32(pb, 9);
    avio_wb32(pb, 0);

    for (i = 0; i < s->nb_streams; i++)
        if (s->streams[i]->codec->codec_tag == 5) {
            avio_w8(pb, 8);     // message type
            avio_wb24(pb, 0);   // include flags
            avio_wb24(pb, 0);   // time stamp
            avio_wb32(pb, 0);   // reserved
            avio_wb32(pb, 11);  // size
            flv->reserved = 5;
        }

    /* write meta_tag */
    avio_w8(pb, 18);            // tag type META
    metadata_size_pos = avio_tell(pb);
    avio_wb24(pb, 0);           // size of data part (sum of all parts below)
    avio_wb24(pb, 0);           // timestamp
    avio_wb32(pb, 0);           // reserved

    /* now data of data_size size */

    /* first event name as a string */
    avio_w8(pb, AMF_DATA_TYPE_STRING);
    put_amf_string(pb, "onMetaData"); // 12 bytes

    /* mixed array (hash) with size and string/type/data tuples */
    avio_w8(pb, AMF_DATA_TYPE_MIXEDARRAY);
    metadata_count_pos = avio_tell(pb);
    metadata_count = 5 * !!video_enc +
                     5 * !!audio_enc +
                     1 * !!data_enc  +
                     2; // +2 for duration and file size

    avio_wb32(pb, metadata_count);

    put_amf_string(pb, "duration");
    flv->duration_offset= avio_tell(pb);

    // fill in the guessed duration, it'll be corrected later if incorrect
    put_amf_double(pb, s->duration / AV_TIME_BASE);

    if (video_enc) {
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

    if (audio_enc) {
        put_amf_string(pb, "audiodatarate");
        put_amf_double(pb, audio_enc->bit_rate / 1024.0);

        put_amf_string(pb, "audiosamplerate");
        put_amf_double(pb, audio_enc->sample_rate);

        put_amf_string(pb, "audiosamplesize");
        put_amf_double(pb, audio_enc->codec_id == AV_CODEC_ID_PCM_U8 ? 8 : 16);

        put_amf_string(pb, "stereo");
        put_amf_bool(pb, audio_enc->channels == 2);

        put_amf_string(pb, "audiocodecid");
        put_amf_double(pb, audio_enc->codec_tag);
    }

    if (data_enc) {
        put_amf_string(pb, "datastream");
        put_amf_double(pb, 0.0);
    }

    while ((tag = av_dict_get(s->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        put_amf_string(pb, tag->key);
        avio_w8(pb, AMF_DATA_TYPE_STRING);
        put_amf_string(pb, tag->value);
        metadata_count++;
    }

    put_amf_string(pb, "filesize");
    flv->filesize_offset = avio_tell(pb);
    put_amf_double(pb, 0); // delayed write

    put_amf_string(pb, "");
    avio_w8(pb, AMF_END_OF_OBJECT);

    /* write total size of tag */
    data_size = avio_tell(pb) - metadata_size_pos - 10;

    avio_seek(pb, metadata_count_pos, SEEK_SET);
    avio_wb32(pb, metadata_count);

    avio_seek(pb, metadata_size_pos, SEEK_SET);
    avio_wb24(pb, data_size);
    avio_skip(pb, data_size + 10 - 3);
    avio_wb32(pb, data_size + 11);

    for (i = 0; i < s->nb_streams; i++) {
        AVCodecContext *enc = s->streams[i]->codec;
        if (enc->codec_id == AV_CODEC_ID_AAC || enc->codec_id == AV_CODEC_ID_H264) {
            int64_t pos;
            avio_w8(pb, enc->codec_type == AVMEDIA_TYPE_VIDEO ?
                    FLV_TAG_TYPE_VIDEO : FLV_TAG_TYPE_AUDIO);
            avio_wb24(pb, 0); // size patched later
            avio_wb24(pb, 0); // ts
            avio_w8(pb, 0);   // ts ext
            avio_wb24(pb, 0); // streamid
            pos = avio_tell(pb);
            if (enc->codec_id == AV_CODEC_ID_AAC) {
                avio_w8(pb, get_audio_flags(s, enc));
                avio_w8(pb, 0); // AAC sequence header
                avio_write(pb, enc->extradata, enc->extradata_size);
            } else {
                avio_w8(pb, enc->codec_tag | FLV_FRAME_KEY); // flags
                avio_w8(pb, 0); // AVC sequence header
                avio_wb24(pb, 0); // composition time
                ff_isom_write_avcc(pb, enc->extradata, enc->extradata_size);
            }
            data_size = avio_tell(pb) - pos;
            avio_seek(pb, -data_size - 10, SEEK_CUR);
            avio_wb24(pb, data_size);
            avio_skip(pb, data_size + 10 - 3);
            avio_wb32(pb, data_size + 11); // previous tag size
        }
    }

    return 0;
}

static int flv_write_trailer(AVFormatContext *s)
{
    int64_t file_size;

    AVIOContext *pb = s->pb;
    FLVContext *flv = s->priv_data;
    int i;

    /* Add EOS tag */
    for (i = 0; i < s->nb_streams; i++) {
        AVCodecContext *enc = s->streams[i]->codec;
        FLVStreamContext *sc = s->streams[i]->priv_data;
        if (enc->codec_type == AVMEDIA_TYPE_VIDEO &&
            enc->codec_id == AV_CODEC_ID_H264)
            put_avc_eos_tag(pb, sc->last_ts);
    }

    file_size = avio_tell(pb);

    /* update information */
    if (avio_seek(pb, flv->duration_offset, SEEK_SET) < 0)
        av_log(s, AV_LOG_WARNING, "Failed to update header with correct duration.\n");
    else
        put_amf_double(pb, flv->duration / (double)1000);
    if (avio_seek(pb, flv->filesize_offset, SEEK_SET) < 0)
        av_log(s, AV_LOG_WARNING, "Failed to update header with correct filesize.\n");
    else
        put_amf_double(pb, file_size);

    avio_seek(pb, file_size, SEEK_SET);
    return 0;
}

static int flv_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb      = s->pb;
    AVCodecContext *enc  = s->streams[pkt->stream_index]->codec;
    FLVContext *flv      = s->priv_data;
    FLVStreamContext *sc = s->streams[pkt->stream_index]->priv_data;
    unsigned ts;
    int size = pkt->size;
    uint8_t *data = NULL;
    int flags = 0, flags_size;

    if (enc->codec_id == AV_CODEC_ID_VP6F || enc->codec_id == AV_CODEC_ID_VP6A ||
        enc->codec_id == AV_CODEC_ID_AAC)
        flags_size = 2;
    else if (enc->codec_id == AV_CODEC_ID_H264)
        flags_size = 5;
    else
        flags_size = 1;

    switch (enc->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        avio_w8(pb, FLV_TAG_TYPE_VIDEO);

        flags = enc->codec_tag;
        if (flags == 0) {
            av_log(s, AV_LOG_ERROR,
                   "video codec %X not compatible with flv\n",
                   enc->codec_id);
            return -1;
        }

        flags |= pkt->flags & AV_PKT_FLAG_KEY ? FLV_FRAME_KEY : FLV_FRAME_INTER;
        break;
    case AVMEDIA_TYPE_AUDIO:
        flags = get_audio_flags(s, enc);

        assert(size);

        avio_w8(pb, FLV_TAG_TYPE_AUDIO);
        break;
    case AVMEDIA_TYPE_DATA:
        avio_w8(pb, FLV_TAG_TYPE_META);
        break;
    default:
        return AVERROR(EINVAL);
    }

    if (enc->codec_id == AV_CODEC_ID_H264)
        /* check if extradata looks like MP4 */
        if (enc->extradata_size > 0 && *(uint8_t*)enc->extradata != 1)
            if (ff_avc_parse_nal_units_buf(pkt->data, &data, &size) < 0)
                return -1;

    if (flv->delay == AV_NOPTS_VALUE)
        flv->delay = -pkt->dts;

    if (pkt->dts < -flv->delay) {
        av_log(s, AV_LOG_WARNING,
               "Packets are not in the proper order with respect to DTS\n");
        return AVERROR(EINVAL);
    }

    ts = pkt->dts + flv->delay; // add delay to force positive dts

    /* check Speex packet duration */
    if (enc->codec_id == AV_CODEC_ID_SPEEX && ts - sc->last_ts > 160)
        av_log(s, AV_LOG_WARNING, "Warning: Speex stream has more than "
                                  "8 frames per packet. Adobe Flash "
                                  "Player cannot handle this!\n");

    if (sc->last_ts < ts)
        sc->last_ts = ts;

    avio_wb24(pb, size + flags_size);
    avio_wb24(pb, ts);
    avio_w8(pb, (ts >> 24) & 0x7F); // timestamps are 32 bits _signed_
    avio_wb24(pb, flv->reserved);

    if (enc->codec_type == AVMEDIA_TYPE_DATA) {
        int data_size;
        int64_t metadata_size_pos = avio_tell(pb);
        avio_w8(pb, AMF_DATA_TYPE_STRING);
        put_amf_string(pb, "onTextData");
        avio_w8(pb, AMF_DATA_TYPE_MIXEDARRAY);
        avio_wb32(pb, 2);
        put_amf_string(pb, "type");
        avio_w8(pb, AMF_DATA_TYPE_STRING);
        put_amf_string(pb, "Text");
        put_amf_string(pb, "text");
        avio_w8(pb, AMF_DATA_TYPE_STRING);
        put_amf_string(pb, pkt->data);
        put_amf_string(pb, "");
        avio_w8(pb, AMF_END_OF_OBJECT);
        /* write total size of tag */
        data_size = avio_tell(pb) - metadata_size_pos;
        avio_seek(pb, metadata_size_pos - 10, SEEK_SET);
        avio_wb24(pb, data_size);
        avio_seek(pb, data_size + 10 - 3, SEEK_CUR);
        avio_wb32(pb, data_size + 11);
    } else {
        avio_w8(pb,flags);
        if (enc->codec_id == AV_CODEC_ID_VP6F || enc->codec_id == AV_CODEC_ID_VP6A) {
            if (enc->extradata_size)
                avio_w8(pb, enc->extradata[0]);
            else
                avio_w8(pb, ((FFALIGN(enc->width,  16) - enc->width) << 4) |
                             (FFALIGN(enc->height, 16) - enc->height));
        } else if (enc->codec_id == AV_CODEC_ID_AAC)
            avio_w8(pb, 1); // AAC raw
        else if (enc->codec_id == AV_CODEC_ID_H264) {
            avio_w8(pb, 1); // AVC NALU
            avio_wb24(pb, pkt->pts - pkt->dts);
        }

        avio_write(pb, data ? data : pkt->data, size);

        avio_wb32(pb, size + flags_size + 11); // previous tag size
        flv->duration = FFMAX(flv->duration,
                              pkt->pts + flv->delay + pkt->duration);
    }

    av_free(data);

    return pb->error;
}

AVOutputFormat ff_flv_muxer = {
    .name           = "flv",
    .long_name      = NULL_IF_CONFIG_SMALL("FLV (Flash Video)"),
    .mime_type      = "video/x-flv",
    .extensions     = "flv",
    .priv_data_size = sizeof(FLVContext),
    .audio_codec    = CONFIG_LIBMP3LAME ? AV_CODEC_ID_MP3 : AV_CODEC_ID_ADPCM_SWF,
    .video_codec    = AV_CODEC_ID_FLV1,
    .write_header   = flv_write_header,
    .write_packet   = flv_write_packet,
    .write_trailer  = flv_write_trailer,
    .codec_tag      = (const AVCodecTag* const []) {
                          flv_video_codec_ids, flv_audio_codec_ids, 0
                      },
    .flags          = AVFMT_GLOBALHEADER | AVFMT_VARIABLE_FPS |
                      AVFMT_TS_NONSTRICT,
};
