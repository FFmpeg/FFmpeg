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

#include "libavutil/intreadwrite.h"
#include "libavutil/dict.h"
#include "libavutil/intfloat.h"
#include "libavutil/avassert.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"
#include "libavcodec/codec_desc.h"
#include "libavcodec/mpeg4audio.h"
#include "avio.h"
#include "avc.h"
#include "av1.h"
#include "vpcc.h"
#include "hevc.h"
#include "avformat.h"
#include "flv.h"
#include "internal.h"
#include "nal.h"
#include "mux.h"
#include "libavutil/opt.h"
#include "libavcodec/put_bits.h"


static const AVCodecTag flv_video_codec_ids[] = {
    { AV_CODEC_ID_FLV1,     FLV_CODECID_H263 },
    { AV_CODEC_ID_H263,     FLV_CODECID_REALH263 },
    { AV_CODEC_ID_MPEG4,    FLV_CODECID_MPEG4 },
    { AV_CODEC_ID_FLASHSV,  FLV_CODECID_SCREEN },
    { AV_CODEC_ID_FLASHSV2, FLV_CODECID_SCREEN2 },
    { AV_CODEC_ID_VP6F,     FLV_CODECID_VP6 },
    { AV_CODEC_ID_VP6,      FLV_CODECID_VP6 },
    { AV_CODEC_ID_VP6A,     FLV_CODECID_VP6A },
    { AV_CODEC_ID_H264,     FLV_CODECID_H264 },
    { AV_CODEC_ID_HEVC,     MKBETAG('h', 'v', 'c', '1') },
    { AV_CODEC_ID_AV1,      MKBETAG('a', 'v', '0', '1') },
    { AV_CODEC_ID_VP9,      MKBETAG('v', 'p', '0', '9') },
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
    { AV_CODEC_ID_OPUS,       MKBETAG('O', 'p', 'u', 's') },
    { AV_CODEC_ID_FLAC,       MKBETAG('f', 'L', 'a', 'C') },
    { AV_CODEC_ID_AC3,        MKBETAG('a', 'c', '-', '3') },
    { AV_CODEC_ID_EAC3,       MKBETAG('e', 'c', '-', '3') },
    { AV_CODEC_ID_NONE,       0 }
};

typedef enum {
    FLV_AAC_SEQ_HEADER_DETECT = (1 << 0),
    FLV_NO_SEQUENCE_END = (1 << 1),
    FLV_ADD_KEYFRAME_INDEX = (1 << 2),
    FLV_NO_METADATA = (1 << 3),
    FLV_NO_DURATION_FILESIZE = (1 << 4),
} FLVFlags;

typedef struct FLVFileposition {
    int64_t keyframe_position;
    double keyframe_timestamp;
    struct FLVFileposition *next;
} FLVFileposition;

typedef struct FLVContext {
    AVClass *av_class;
    int     reserved;
    int64_t duration_offset;
    int64_t filesize_offset;
    int64_t duration;
    int64_t delay;      ///< first dts delay (needed for AVC & Speex)

    int64_t datastart_offset;
    int64_t datasize_offset;
    int64_t datasize;
    int64_t videosize_offset;
    int64_t videosize;
    int64_t audiosize_offset;
    int64_t audiosize;

    int64_t metadata_size_pos;
    int64_t metadata_totalsize_pos;
    int64_t metadata_totalsize;
    int64_t keyframe_index_size;

    int64_t lasttimestamp_offset;
    double lasttimestamp;
    int64_t lastkeyframetimestamp_offset;
    double lastkeyframetimestamp;
    int64_t lastkeyframelocation_offset;
    int64_t lastkeyframelocation;

    int64_t keyframes_info_offset;

    int64_t filepositions_count;
    FLVFileposition *filepositions;
    FLVFileposition *head_filepositions;

    AVCodecParameters *audio_par;
    AVCodecParameters *video_par;
    double framerate;
    AVCodecParameters *data_par;

    int flags;
    int64_t *last_ts;
    int *metadata_pkt_written;
    int *track_idx_map;
} FLVContext;

static int get_audio_flags(AVFormatContext *s, AVCodecParameters *par)
{
    int flags = (par->bits_per_coded_sample == 16) ? FLV_SAMPLESSIZE_16BIT
                                                   : FLV_SAMPLESSIZE_8BIT;

    if (par->codec_id == AV_CODEC_ID_AAC) // specs force these parameters
        return FLV_CODECID_AAC | FLV_SAMPLERATE_44100HZ |
               FLV_SAMPLESSIZE_16BIT | FLV_STEREO;
    if (par->codec_id == AV_CODEC_ID_OPUS || par->codec_id == AV_CODEC_ID_FLAC
        || par->codec_id == AV_CODEC_ID_AC3 || par->codec_id == AV_CODEC_ID_EAC3)
        return FLV_CODECID_EX_HEADER; // only needed for codec support check
    else if (par->codec_id == AV_CODEC_ID_SPEEX) {
        if (par->sample_rate != 16000) {
            av_log(s, AV_LOG_ERROR,
                   "FLV only supports wideband (16kHz) Speex audio\n");
            return AVERROR(EINVAL);
        }
        if (par->ch_layout.nb_channels != 1) {
            av_log(s, AV_LOG_ERROR, "FLV only supports mono Speex audio\n");
            return AVERROR(EINVAL);
        }
        return FLV_CODECID_SPEEX | FLV_SAMPLERATE_11025HZ | FLV_SAMPLESSIZE_16BIT;
    } else {
        switch (par->sample_rate) {
        case 48000:
            // 48khz mp3 is stored with 44k1 samplerate identifer
            if (par->codec_id == AV_CODEC_ID_MP3) {
                flags |= FLV_SAMPLERATE_44100HZ;
                break;
            } else {
                goto error;
            }
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
            if (par->codec_id != AV_CODEC_ID_MP3) {
                flags |= FLV_SAMPLERATE_SPECIAL;
                break;
            }
        default:
error:
            av_log(s, AV_LOG_ERROR,
                   "FLV does not support sample rate %d, "
                   "choose from (44100, 22050, 11025)\n", par->sample_rate);
            return AVERROR(EINVAL);
        }
    }

    if (par->ch_layout.nb_channels > 1)
        flags |= FLV_STEREO;

    switch (par->codec_id) {
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
        if (par->sample_rate == 8000)
            flags |= FLV_CODECID_NELLYMOSER_8KHZ_MONO  | FLV_SAMPLESSIZE_16BIT;
        else if (par->sample_rate == 16000)
            flags |= FLV_CODECID_NELLYMOSER_16KHZ_MONO | FLV_SAMPLESSIZE_16BIT;
        else
            flags |= FLV_CODECID_NELLYMOSER            | FLV_SAMPLESSIZE_16BIT;
        break;
    case AV_CODEC_ID_PCM_MULAW:
        flags |= FLV_CODECID_PCM_MULAW | FLV_SAMPLESSIZE_16BIT;
        break;
    case AV_CODEC_ID_PCM_ALAW:
        flags |= FLV_CODECID_PCM_ALAW | FLV_SAMPLESSIZE_16BIT;
        break;
    case 0:
        flags |= par->codec_tag << 4;
        break;
    default:
        av_log(s, AV_LOG_ERROR, "Audio codec '%s' not compatible with FLV\n",
               avcodec_get_name(par->codec_id));
        return AVERROR(EINVAL);
    }

    return flags;
}

static void put_amf_string(AVIOContext *pb, const char *str)
{
    size_t len = strlen(str);
    avio_wb16(pb, len);
    // Avoid avio_write() if put_amf_string(pb, "") is inlined.
    if (av_builtin_constant_p(len == 0) && len == 0)
        return;
    avio_write(pb, str, len);
}

// FLV timestamps are 32 bits signed, RTMP timestamps should be 32-bit unsigned
static void put_timestamp(AVIOContext *pb, int64_t ts) {
    avio_wb24(pb, ts & 0xFFFFFF);
    avio_w8(pb, (ts >> 24) & 0x7F);
}

static void put_eos_tag(AVIOContext *pb, unsigned ts, enum AVCodecID codec_id)
{
    uint32_t tag = ff_codec_get_tag(flv_video_codec_ids, codec_id);
    /* ub[4] FrameType = 1, ub[4] CodecId */
    tag |= 1 << 4;
    avio_w8(pb, FLV_TAG_TYPE_VIDEO);
    avio_wb24(pb, 5);               /* Tag Data Size */
    put_timestamp(pb, ts);
    avio_wb24(pb, 0);               /* StreamId = 0 */
    avio_w8(pb, tag);
    avio_w8(pb, 2);                 /* AVC end of sequence */
    avio_wb24(pb, 0);               /* Always 0 for AVC EOS. */
    avio_wb32(pb, 16);              /* Size of FLV tag */
}

static void put_amf_double(AVIOContext *pb, double d)
{
    avio_w8(pb, AMF_DATA_TYPE_NUMBER);
    avio_wb64(pb, av_double2int(d));
}

static void put_amf_byte(AVIOContext *pb, unsigned char abyte)
{
    avio_w8(pb, abyte);
}

static void put_amf_dword_array(AVIOContext *pb, uint32_t dw)
{
    avio_w8(pb, AMF_DATA_TYPE_ARRAY);
    avio_wb32(pb, dw);
}

static void put_amf_bool(AVIOContext *pb, int b)
{
    avio_w8(pb, AMF_DATA_TYPE_BOOL);
    avio_w8(pb, !!b);
}

static void write_metadata(AVFormatContext *s, unsigned int ts)
{
    AVIOContext *pb = s->pb;
    FLVContext *flv = s->priv_data;
    int write_duration_filesize = !(flv->flags & FLV_NO_DURATION_FILESIZE);
    int metadata_count = 0;
    int64_t metadata_count_pos;
    const AVDictionaryEntry *tag = NULL;

    /* write meta_tag */
    avio_w8(pb, FLV_TAG_TYPE_META);            // tag type META
    flv->metadata_size_pos = avio_tell(pb);
    avio_wb24(pb, 0);           // size of data part (sum of all parts below)
    put_timestamp(pb, ts);      // timestamp
    avio_wb24(pb, 0);           // reserved

    /* now data of data_size size */

    /* first event name as a string */
    avio_w8(pb, AMF_DATA_TYPE_STRING);
    put_amf_string(pb, "onMetaData"); // 12 bytes

    /* mixed array (hash) with size and string/type/data tuples */
    avio_w8(pb, AMF_DATA_TYPE_MIXEDARRAY);
    metadata_count_pos = avio_tell(pb);
    metadata_count = 4 * !!flv->video_par +
                     5 * !!flv->audio_par +
                     1 * !!flv->data_par;
    if (write_duration_filesize) {
        metadata_count += 2; // +2 for duration and file size
    }
    avio_wb32(pb, metadata_count);

    if (write_duration_filesize) {
        put_amf_string(pb, "duration");
        flv->duration_offset = avio_tell(pb);
        // fill in the guessed duration, it'll be corrected later if incorrect
        put_amf_double(pb, s->duration / AV_TIME_BASE);
    }

    if (flv->video_par) {
        put_amf_string(pb, "width");
        put_amf_double(pb, flv->video_par->width);

        put_amf_string(pb, "height");
        put_amf_double(pb, flv->video_par->height);

        put_amf_string(pb, "videodatarate");
        put_amf_double(pb, flv->video_par->bit_rate / 1024.0);

        if (flv->framerate != 0.0) {
            put_amf_string(pb, "framerate");
            put_amf_double(pb, flv->framerate);
            metadata_count++;
        }

        put_amf_string(pb, "videocodecid");
        put_amf_double(pb, flv->video_par->codec_tag);
    }

    if (flv->audio_par) {
        put_amf_string(pb, "audiodatarate");
        put_amf_double(pb, flv->audio_par->bit_rate / 1024.0);

        put_amf_string(pb, "audiosamplerate");
        put_amf_double(pb, flv->audio_par->sample_rate);

        put_amf_string(pb, "audiosamplesize");
        put_amf_double(pb, flv->audio_par->codec_id == AV_CODEC_ID_PCM_U8 ? 8 : 16);

        put_amf_string(pb, "stereo");
        put_amf_bool(pb, flv->audio_par->ch_layout.nb_channels == 2);

        put_amf_string(pb, "audiocodecid");
        put_amf_double(pb, flv->audio_par->codec_tag);
    }

    if (flv->data_par) {
        put_amf_string(pb, "datastream");
        put_amf_double(pb, 0.0);
    }

    ff_standardize_creation_time(s);
    while ((tag = av_dict_iterate(s->metadata, tag))) {
        if(   !strcmp(tag->key, "width")
            ||!strcmp(tag->key, "height")
            ||!strcmp(tag->key, "videodatarate")
            ||!strcmp(tag->key, "framerate")
            ||!strcmp(tag->key, "videocodecid")
            ||!strcmp(tag->key, "audiodatarate")
            ||!strcmp(tag->key, "audiosamplerate")
            ||!strcmp(tag->key, "audiosamplesize")
            ||!strcmp(tag->key, "stereo")
            ||!strcmp(tag->key, "audiocodecid")
            ||!strcmp(tag->key, "duration")
            ||!strcmp(tag->key, "onMetaData")
            ||!strcmp(tag->key, "datasize")
            ||!strcmp(tag->key, "lasttimestamp")
            ||!strcmp(tag->key, "totalframes")
            ||!strcmp(tag->key, "hasAudio")
            ||!strcmp(tag->key, "hasVideo")
            ||!strcmp(tag->key, "hasCuePoints")
            ||!strcmp(tag->key, "hasMetadata")
            ||!strcmp(tag->key, "hasKeyframes")
        ){
            av_log(s, AV_LOG_DEBUG, "Ignoring metadata for %s\n", tag->key);
            continue;
        }
        put_amf_string(pb, tag->key);
        avio_w8(pb, AMF_DATA_TYPE_STRING);
        put_amf_string(pb, tag->value);
        metadata_count++;
    }

    if (write_duration_filesize) {
        put_amf_string(pb, "filesize");
        flv->filesize_offset = avio_tell(pb);
        put_amf_double(pb, 0); // delayed write
    }

    if (flv->flags & FLV_ADD_KEYFRAME_INDEX) {
        flv->keyframe_index_size = 0;

        put_amf_string(pb, "hasVideo");
        put_amf_bool(pb, !!flv->video_par);
        metadata_count++;

        put_amf_string(pb, "hasKeyframes");
        put_amf_bool(pb, 1);
        metadata_count++;

        put_amf_string(pb, "hasAudio");
        put_amf_bool(pb, !!flv->audio_par);
        metadata_count++;

        put_amf_string(pb, "hasMetadata");
        put_amf_bool(pb, 1);
        metadata_count++;

        put_amf_string(pb, "canSeekToEnd");
        put_amf_bool(pb, 1);
        metadata_count++;

        put_amf_string(pb, "datasize");
        flv->datasize_offset = avio_tell(pb);
        flv->datasize = 0;
        put_amf_double(pb, flv->datasize);
        metadata_count++;

        put_amf_string(pb, "videosize");
        flv->videosize_offset = avio_tell(pb);
        flv->videosize = 0;
        put_amf_double(pb, flv->videosize);
        metadata_count++;

        put_amf_string(pb, "audiosize");
        flv->audiosize_offset = avio_tell(pb);
        flv->audiosize = 0;
        put_amf_double(pb, flv->audiosize);
        metadata_count++;

        put_amf_string(pb, "lasttimestamp");
        flv->lasttimestamp_offset = avio_tell(pb);
        flv->lasttimestamp = 0;
        put_amf_double(pb, 0);
        metadata_count++;

        put_amf_string(pb, "lastkeyframetimestamp");
        flv->lastkeyframetimestamp_offset = avio_tell(pb);
        flv->lastkeyframetimestamp = 0;
        put_amf_double(pb, 0);
        metadata_count++;

        put_amf_string(pb, "lastkeyframelocation");
        flv->lastkeyframelocation_offset = avio_tell(pb);
        flv->lastkeyframelocation = 0;
        put_amf_double(pb, 0);
        metadata_count++;

        put_amf_string(pb, "keyframes");
        put_amf_byte(pb, AMF_DATA_TYPE_OBJECT);
        metadata_count++;

        flv->keyframes_info_offset = avio_tell(pb);
    }

    put_amf_string(pb, "");
    avio_w8(pb, AMF_END_OF_OBJECT);

    /* write total size of tag */
    flv->metadata_totalsize = avio_tell(pb) - flv->metadata_size_pos - 10;

    avio_seek(pb, metadata_count_pos, SEEK_SET);
    avio_wb32(pb, metadata_count);

    avio_seek(pb, flv->metadata_size_pos, SEEK_SET);
    avio_wb24(pb, flv->metadata_totalsize);
    avio_skip(pb, flv->metadata_totalsize + 10 - 3);
    flv->metadata_totalsize_pos = avio_tell(pb);
    avio_wb32(pb, flv->metadata_totalsize + 11);
}

static void write_codec_fourcc(AVIOContext *pb, enum AVCodecID codec_id)
{
    switch (codec_id) {
    case AV_CODEC_ID_AAC:
        avio_write(pb, "mp4a", 4);
        return;
    case AV_CODEC_ID_OPUS:
        avio_write(pb, "Opus", 4);
        return;
    case AV_CODEC_ID_FLAC:
        avio_write(pb, "fLaC", 4);
        return;
    case AV_CODEC_ID_MP3:
        avio_write(pb, ".mp3", 4);
        return;
    case AV_CODEC_ID_AC3:
        avio_write(pb, "ac-3", 4);
        return;
    case AV_CODEC_ID_EAC3:
        avio_write(pb, "ec-3", 4);
        return;
    case AV_CODEC_ID_H264:
        avio_write(pb, "avc1", 4);
        return;
    case AV_CODEC_ID_HEVC:
        avio_write(pb, "hvc1", 4);
        return;
    case AV_CODEC_ID_AV1:
        avio_write(pb, "av01", 4);
        return;
    case AV_CODEC_ID_VP9:
        avio_write(pb, "vp09", 4);
        return;
    default:
        av_log(NULL, AV_LOG_ERROR, "Invalid codec FourCC write requested.\n");
        av_assert0(0);
    }
}

static void flv_write_metadata_packet(AVFormatContext *s, AVCodecParameters *par, unsigned int ts, int stream_idx)
{
    AVIOContext *pb = s->pb;
    FLVContext *flv = s->priv_data;
    AVContentLightMetadata *lightMetadata = NULL;
    AVMasteringDisplayMetadata *displayMetadata = NULL;
    int64_t metadata_size_pos = 0;
    int64_t total_size = 0;
    const AVPacketSideData *side_data = NULL;

    if (flv->metadata_pkt_written[stream_idx])
        return;

    if (par->codec_id == AV_CODEC_ID_HEVC || par->codec_id == AV_CODEC_ID_AV1 ||
        par->codec_id == AV_CODEC_ID_VP9) {
        int flags_size = 5;
        side_data = av_packet_side_data_get(par->coded_side_data, par->nb_coded_side_data,
                                            AV_PKT_DATA_CONTENT_LIGHT_LEVEL);
        if (side_data)
            lightMetadata = (AVContentLightMetadata *)side_data->data;

        side_data = av_packet_side_data_get(par->coded_side_data, par->nb_coded_side_data,
                                            AV_PKT_DATA_MASTERING_DISPLAY_METADATA);
        if (side_data)
            displayMetadata = (AVMasteringDisplayMetadata *)side_data->data;

        /*
        * Reference Enhancing FLV
        * https://github.com/veovera/enhanced-rtmp/blob/main/enhanced-rtmp.pdf
        * */
        avio_w8(pb, FLV_TAG_TYPE_VIDEO); //write video tag type
        metadata_size_pos = avio_tell(pb);
        avio_wb24(pb, 0 + flags_size);
        put_timestamp(pb, ts); //ts = pkt->dts, gen
        avio_wb24(pb, flv->reserved);

        avio_w8(pb, FLV_IS_EX_HEADER | PacketTypeMetadata | FLV_FRAME_VIDEO_INFO_CMD); // ExVideoTagHeader mode with PacketTypeMetadata
        write_codec_fourcc(pb, par->codec_id);

        avio_w8(pb, AMF_DATA_TYPE_STRING);
        put_amf_string(pb, "colorInfo");

        avio_w8(pb, AMF_DATA_TYPE_OBJECT);

        put_amf_string(pb, "colorConfig");  // colorConfig

        avio_w8(pb, AMF_DATA_TYPE_OBJECT);

        if (par->color_trc != AVCOL_TRC_UNSPECIFIED &&
            par->color_trc < AVCOL_TRC_NB) {
            put_amf_string(pb, "transferCharacteristics");  // color_trc
            put_amf_double(pb, par->color_trc);
        }

        if (par->color_space != AVCOL_SPC_UNSPECIFIED &&
            par->color_space < AVCOL_SPC_NB) {
            put_amf_string(pb, "matrixCoefficients"); // colorspace
            put_amf_double(pb, par->color_space);
        }

        if (par->color_primaries != AVCOL_PRI_UNSPECIFIED &&
            par->color_primaries < AVCOL_PRI_NB) {
            put_amf_string(pb, "colorPrimaries"); // color_primaries
            put_amf_double(pb, par->color_primaries);
        }

        put_amf_string(pb, "");
        avio_w8(pb, AMF_END_OF_OBJECT);

        if (lightMetadata) {
            put_amf_string(pb, "hdrCll");
            avio_w8(pb, AMF_DATA_TYPE_OBJECT);

            put_amf_string(pb, "maxFall");
            put_amf_double(pb, lightMetadata->MaxFALL);

            put_amf_string(pb, "maxCLL");
            put_amf_double(pb, lightMetadata->MaxCLL);

            put_amf_string(pb, "");
            avio_w8(pb, AMF_END_OF_OBJECT);
        }

        if (displayMetadata && (displayMetadata->has_primaries || displayMetadata->has_luminance)) {
            put_amf_string(pb, "hdrMdcv");
            avio_w8(pb, AMF_DATA_TYPE_OBJECT);
            if (displayMetadata->has_primaries) {
                put_amf_string(pb, "redX");
                put_amf_double(pb, av_q2d(displayMetadata->display_primaries[0][0]));

                put_amf_string(pb, "redY");
                put_amf_double(pb, av_q2d(displayMetadata->display_primaries[0][1]));

                put_amf_string(pb, "greenX");
                put_amf_double(pb, av_q2d(displayMetadata->display_primaries[1][0]));

                put_amf_string(pb, "greenY");
                put_amf_double(pb, av_q2d(displayMetadata->display_primaries[1][1]));

                put_amf_string(pb, "blueX");
                put_amf_double(pb, av_q2d(displayMetadata->display_primaries[2][0]));

                put_amf_string(pb, "blueY");
                put_amf_double(pb, av_q2d(displayMetadata->display_primaries[2][1]));

                put_amf_string(pb, "whitePointX");
                put_amf_double(pb, av_q2d(displayMetadata->white_point[0]));

                put_amf_string(pb, "whitePointY");
                put_amf_double(pb, av_q2d(displayMetadata->white_point[1]));
            }
            if (displayMetadata->has_luminance) {
                put_amf_string(pb, "maxLuminance");
                put_amf_double(pb, av_q2d(displayMetadata->max_luminance));

                put_amf_string(pb, "minLuminance");
                put_amf_double(pb, av_q2d(displayMetadata->min_luminance));
            }
            put_amf_string(pb, "");
            avio_w8(pb, AMF_END_OF_OBJECT);
        }
        put_amf_string(pb, "");
        avio_w8(pb, AMF_END_OF_OBJECT);

        total_size = avio_tell(pb) - metadata_size_pos - 10;
        avio_seek(pb, metadata_size_pos, SEEK_SET);
        avio_wb24(pb, total_size);
        avio_skip(pb, total_size + 10 - 3);
        avio_wb32(pb, total_size + 11); // previous tag size
        flv->metadata_pkt_written[stream_idx] = 1;
    }
}

static int unsupported_codec(AVFormatContext *s,
                             const char* type, int codec_id)
{
    const AVCodecDescriptor *desc = avcodec_descriptor_get(codec_id);
    av_log(s, AV_LOG_ERROR,
           "%s codec %s not compatible with flv\n",
            type,
            desc ? desc->name : "unknown");
    return AVERROR(ENOSYS);
}

static void flv_write_aac_header(AVFormatContext* s, AVCodecParameters* par)
{
    AVIOContext *pb = s->pb;
    FLVContext *flv = s->priv_data;

    if (!par->extradata_size && (flv->flags & FLV_AAC_SEQ_HEADER_DETECT)) {
        PutBitContext pbc;
        int samplerate_index;
        int channels = par->ch_layout.nb_channels
                - (par->ch_layout.nb_channels == 8 ? 1 : 0);
        uint8_t data[2];

        for (samplerate_index = 0; samplerate_index < 16;
                samplerate_index++)
            if (par->sample_rate
                    == ff_mpeg4audio_sample_rates[samplerate_index])
                break;

        init_put_bits(&pbc, data, sizeof(data));
        put_bits(&pbc, 5, par->profile + 1); //profile
        put_bits(&pbc, 4, samplerate_index); //sample rate index
        put_bits(&pbc, 4, channels);
        put_bits(&pbc, 1, 0); //frame length - 1024 samples
        put_bits(&pbc, 1, 0); //does not depend on core coder
        put_bits(&pbc, 1, 0); //is not extension
        flush_put_bits(&pbc);

        avio_w8(pb, data[0]);
        avio_w8(pb, data[1]);

        av_log(s, AV_LOG_WARNING, "AAC sequence header: %02x %02x.\n",
                data[0], data[1]);
    }
    avio_write(pb, par->extradata, par->extradata_size);
}

static void flv_write_multichannel_body(AVFormatContext* s, AVCodecParameters* par)
{
    AVIOContext *pb = s->pb;

    switch (par->ch_layout.order) {
    case AV_CHANNEL_ORDER_NATIVE:
        avio_w8(pb, AudioChannelOrderNative);
        break;
    case AV_CHANNEL_ORDER_CUSTOM:
        avio_w8(pb, AudioChannelOrderCustom);
        break;
    default:
        avio_w8(pb, AudioChannelOrderUnspecified);
        break;
    }

    avio_w8(pb, par->ch_layout.nb_channels);

    if (par->ch_layout.order == AV_CHANNEL_ORDER_NATIVE) {
        // The first 18 entries are identical between FFmpeg and flv
        uint32_t mask = par->ch_layout.u.mask & 0x03FFFF;
        // The remaining 6 flv entries are in the right order, but start at AV_CHAN_LOW_FREQUENCY_2
        mask |= (par->ch_layout.u.mask >> (AV_CHAN_LOW_FREQUENCY_2 - 18)) & 0xFC0000;

        avio_wb32(pb, mask);
    } else if (par->ch_layout.order == AV_CHANNEL_ORDER_CUSTOM) {
        for (int i = 0; i < par->ch_layout.nb_channels; i++) {
            enum AVChannel id = par->ch_layout.u.map[i].id;
            if (id >= AV_CHAN_FRONT_LEFT && id <= AV_CHAN_TOP_BACK_RIGHT) {
                avio_w8(pb, id - AV_CHAN_FRONT_LEFT + 0);
            } else if (id >= AV_CHAN_LOW_FREQUENCY_2 && id <= AV_CHAN_BOTTOM_FRONT_RIGHT) {
                avio_w8(pb, id - AV_CHAN_LOW_FREQUENCY_2 + 18);
            } else if (id == AV_CHAN_UNUSED) {
                avio_w8(pb, 0xFE);
            } else {
                avio_w8(pb, 0xFF); // unknown
            }
        }
    }
}

static int flv_get_multichannel_body_size(AVCodecParameters* par)
{
    int res = 2;

    if (par->ch_layout.order == AV_CHANNEL_ORDER_NATIVE)
        res += 4;
    else if (par->ch_layout.order == AV_CHANNEL_ORDER_CUSTOM)
        res += par->ch_layout.nb_channels;

    return res;
}

static void flv_write_multichannel_header(AVFormatContext* s, AVCodecParameters* par, int64_t ts, int stream_index)
{
    AVIOContext *pb = s->pb;
    FLVContext *flv = s->priv_data;

    int track_idx = flv->track_idx_map[stream_index];
    int data_size = flv_get_multichannel_body_size(par);
    if (track_idx)
        data_size += 2;

    avio_w8(pb, FLV_TAG_TYPE_AUDIO);
    avio_wb24(pb, data_size + 5); // size
    put_timestamp(pb, ts);
    avio_wb24(pb, 0); // streamid

    if (track_idx) {
        avio_w8(pb, FLV_CODECID_EX_HEADER | AudioPacketTypeMultitrack);
        avio_w8(pb, MultitrackTypeOneTrack | AudioPacketTypeMultichannelConfig);
    } else {
        avio_w8(pb, FLV_CODECID_EX_HEADER | AudioPacketTypeMultichannelConfig);
    }

    write_codec_fourcc(pb, par->codec_id);

    if (track_idx)
        avio_w8(pb, track_idx);

    flv_write_multichannel_body(s, par);

    avio_wb32(pb, data_size + 5 + 11); // previous tag size
}

static void flv_write_codec_header(AVFormatContext* s, AVCodecParameters* par, int64_t ts, int stream_index) {
    int64_t data_size;
    AVIOContext *pb = s->pb;
    FLVContext *flv = s->priv_data;
    int track_idx = flv->track_idx_map[stream_index];
    int extended_flv = 0;

    if (par->codec_id == AV_CODEC_ID_AAC || par->codec_id == AV_CODEC_ID_H264
            || par->codec_id == AV_CODEC_ID_MPEG4 || par->codec_id == AV_CODEC_ID_HEVC
            || par->codec_id == AV_CODEC_ID_AV1 || par->codec_id == AV_CODEC_ID_VP9
            || (par->codec_id == AV_CODEC_ID_MP3 && track_idx)
            || par->codec_id == AV_CODEC_ID_OPUS || par->codec_id == AV_CODEC_ID_FLAC
            || par->codec_id == AV_CODEC_ID_AC3 || par->codec_id == AV_CODEC_ID_EAC3) {
        int64_t pos;
        avio_w8(pb,
                par->codec_type == AVMEDIA_TYPE_VIDEO ?
                        FLV_TAG_TYPE_VIDEO : FLV_TAG_TYPE_AUDIO);
        avio_wb24(pb, 0); // size patched later
        put_timestamp(pb, ts);
        avio_wb24(pb, 0); // streamid
        pos = avio_tell(pb);
        if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
            extended_flv = (par->codec_id == AV_CODEC_ID_AAC && track_idx)
                                    || (par->codec_id == AV_CODEC_ID_MP3 && track_idx)
                                    || par->codec_id == AV_CODEC_ID_OPUS
                                    || par->codec_id == AV_CODEC_ID_FLAC
                                    || par->codec_id == AV_CODEC_ID_AC3
                                    || par->codec_id == AV_CODEC_ID_EAC3;

            if (extended_flv) {
                if (track_idx) {
                    avio_w8(pb, FLV_CODECID_EX_HEADER | AudioPacketTypeMultitrack);
                    avio_w8(pb, MultitrackTypeOneTrack | AudioPacketTypeSequenceStart);
                } else {
                    avio_w8(pb, FLV_CODECID_EX_HEADER | AudioPacketTypeSequenceStart);
                }

                write_codec_fourcc(pb, par->codec_id);

                if (track_idx)
                    avio_w8(pb, track_idx);

                if (par->codec_id == AV_CODEC_ID_AAC) {
                    flv_write_aac_header(s, par);
                } else if (par->codec_id == AV_CODEC_ID_OPUS || par->codec_id == AV_CODEC_ID_FLAC) {
                    av_assert0(par->extradata_size);
                    avio_write(pb, par->extradata, par->extradata_size);
                }
            } else if (par->codec_id == AV_CODEC_ID_AAC) {
                avio_w8(pb, get_audio_flags(s, par));
                avio_w8(pb, 0); // AAC sequence header

                flv_write_aac_header(s, par);
            }
        } else {
            // If video stream has track_idx > 0 we need to send H.264 as extended video packet
            extended_flv = (par->codec_id == AV_CODEC_ID_H264 && track_idx) ||
                            par->codec_id == AV_CODEC_ID_HEVC ||
                            par->codec_id == AV_CODEC_ID_AV1 ||
                            par->codec_id == AV_CODEC_ID_VP9;

            if (extended_flv) {
                if (track_idx) {
                    avio_w8(pb, FLV_IS_EX_HEADER | PacketTypeMultitrack | FLV_FRAME_KEY);
                    avio_w8(pb, MultitrackTypeOneTrack | PacketTypeSequenceStart);
                } else {
                    avio_w8(pb, FLV_IS_EX_HEADER | PacketTypeSequenceStart | FLV_FRAME_KEY);
                }

                write_codec_fourcc(pb, par->codec_id);

                if (track_idx)
                    avio_w8(pb, track_idx);
            } else {
                avio_w8(pb, par->codec_tag | FLV_FRAME_KEY); // flags
                avio_w8(pb, 0); // AVC sequence header
                avio_wb24(pb, 0); // composition time
            }

            if (par->codec_id == AV_CODEC_ID_HEVC)
                ff_isom_write_hvcc(pb, par->extradata, par->extradata_size, 0, s);
            else if (par->codec_id == AV_CODEC_ID_AV1)
                ff_isom_write_av1c(pb, par->extradata, par->extradata_size, 1);
            else if (par->codec_id == AV_CODEC_ID_VP9)
                ff_isom_write_vpcc(s, pb, par->extradata, par->extradata_size, par);
            else if (par->codec_id == AV_CODEC_ID_H264)
                ff_isom_write_avcc(pb, par->extradata, par->extradata_size);
            else if (par->codec_id == AV_CODEC_ID_MPEG4)
                avio_write(pb, par->extradata, par->extradata_size);
            else
                av_assert0(0);
        }
        data_size = avio_tell(pb) - pos;
        avio_seek(pb, -data_size - 10, SEEK_CUR);
        avio_wb24(pb, data_size);
        avio_skip(pb, data_size + 10 - 3);
        avio_wb32(pb, data_size + 11); // previous tag size
    }

    if (par->codec_type == AVMEDIA_TYPE_AUDIO && (extended_flv ||
        (av_channel_layout_compare(&par->ch_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO) == 1 &&
         av_channel_layout_compare(&par->ch_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_MONO) == 1)))
        flv_write_multichannel_header(s, par, ts, stream_index);
}

static int flv_append_keyframe_info(AVFormatContext *s, FLVContext *flv, double ts, int64_t pos)
{
    FLVFileposition *position = av_malloc(sizeof(FLVFileposition));

    if (!position) {
        av_log(s, AV_LOG_WARNING, "no mem for add keyframe index!\n");
        return AVERROR(ENOMEM);
    }

    position->keyframe_timestamp = ts;
    position->keyframe_position = pos;

    if (!flv->filepositions_count) {
        flv->filepositions = position;
        flv->head_filepositions = flv->filepositions;
        position->next = NULL;
    } else {
        flv->filepositions->next = position;
        position->next = NULL;
        flv->filepositions = flv->filepositions->next;
    }

    flv->filepositions_count++;

    return 0;
}

static int shift_data(AVFormatContext *s)
{
    int ret;
    int64_t metadata_size = 0;
    FLVContext *flv = s->priv_data;

    metadata_size = flv->filepositions_count * 9 * 2 + 10; /* filepositions and times value */
    metadata_size += 2 + 13; /* filepositions String */
    metadata_size += 2 + 5; /* times String */
    metadata_size += 3; /* Object end */

    flv->keyframe_index_size = metadata_size;

    if (metadata_size < 0)
        return metadata_size;

    ret = ff_format_shift_data(s, flv->keyframes_info_offset, metadata_size);
    if (ret < 0)
        return ret;

    avio_seek(s->pb, flv->metadata_size_pos, SEEK_SET);
    avio_wb24(s->pb, flv->metadata_totalsize + metadata_size);

    avio_seek(s->pb, flv->metadata_totalsize_pos + metadata_size, SEEK_SET);
    avio_wb32(s->pb, flv->metadata_totalsize + 11 + metadata_size);

    return 0;
}

static int flv_init(struct AVFormatContext *s)
{
    int i;
    int video_ctr = 0, audio_ctr = 0;
    FLVContext *flv = s->priv_data;

    flv->last_ts = av_calloc(s->nb_streams, sizeof(*flv->last_ts));
    flv->metadata_pkt_written = av_calloc(s->nb_streams, sizeof(*flv->metadata_pkt_written));
    flv->track_idx_map = av_calloc(s->nb_streams, sizeof(*flv->track_idx_map));
    if (!flv->last_ts || !flv->metadata_pkt_written || !flv->track_idx_map)
        return AVERROR(ENOMEM);

    for (i = 0; i < s->nb_streams; i++) {
        AVCodecParameters *par = s->streams[i]->codecpar;

        switch (par->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            if (video_ctr &&
                par->codec_id != AV_CODEC_ID_VP8 &&
                par->codec_id != AV_CODEC_ID_VP9 &&
                par->codec_id != AV_CODEC_ID_AV1 &&
                par->codec_id != AV_CODEC_ID_H264 &&
                par->codec_id != AV_CODEC_ID_HEVC) {
                av_log(s, AV_LOG_ERROR, "Unsupported multi-track video codec.\n");
                return AVERROR(EINVAL);
            }
            if (s->streams[i]->avg_frame_rate.den &&
                s->streams[i]->avg_frame_rate.num) {
                flv->framerate = av_q2d(s->streams[i]->avg_frame_rate);
            }
            flv->track_idx_map[i] = video_ctr++;
            if (flv->video_par && flv->flags & FLV_ADD_KEYFRAME_INDEX) {
                av_log(s, AV_LOG_ERROR,
                       "at most one video stream is supported in flv with keyframe index\n");
                return AVERROR(EINVAL);
            } else if (flv->video_par) {
                av_log(s, AV_LOG_WARNING,
                       "more than one video stream is not supported by most flv demuxers.\n");
            }
            if (!flv->video_par)
                flv->video_par = par;
            if (!ff_codec_get_tag(flv_video_codec_ids, par->codec_id))
                return unsupported_codec(s, "Video", par->codec_id);

            if (par->codec_id == AV_CODEC_ID_MPEG4 ||
                par->codec_id == AV_CODEC_ID_H263) {
                int error = s->strict_std_compliance > FF_COMPLIANCE_UNOFFICIAL;
                av_log(s, error ? AV_LOG_ERROR : AV_LOG_WARNING,
                       "Codec %s is not supported in the official FLV specification,\n", avcodec_get_name(par->codec_id));

                if (error) {
                    av_log(s, AV_LOG_ERROR,
                           "use vstrict=-1 / -strict -1 to use it anyway.\n");
                    return AVERROR(EINVAL);
                }
            } else if (par->codec_id == AV_CODEC_ID_VP6) {
                av_log(s, AV_LOG_WARNING,
                       "Muxing VP6 in flv will produce flipped video on playback.\n");
            }
            break;
        case AVMEDIA_TYPE_AUDIO:
            if (audio_ctr &&
                par->codec_id != AV_CODEC_ID_AAC &&
                par->codec_id != AV_CODEC_ID_MP3 &&
                par->codec_id != AV_CODEC_ID_OPUS &&
                par->codec_id != AV_CODEC_ID_FLAC &&
                par->codec_id != AV_CODEC_ID_AC3 &&
                par->codec_id != AV_CODEC_ID_EAC3) {
                av_log(s, AV_LOG_ERROR, "Unsupported multi-track audio codec.\n");
                return AVERROR(EINVAL);
            }
            flv->track_idx_map[i] = audio_ctr++;
            if (flv->audio_par)
                av_log(s, AV_LOG_WARNING,
                       "more than one audio stream is not supported by most flv demuxers.\n");
            else
                flv->audio_par = par;
            if (get_audio_flags(s, par) < 0)
                return unsupported_codec(s, "Audio", par->codec_id);
            if (par->codec_id == AV_CODEC_ID_PCM_S16BE)
                av_log(s, AV_LOG_WARNING,
                       "16-bit big-endian audio in flv is valid but most likely unplayable (hardware dependent); use s16le\n");
            break;
        case AVMEDIA_TYPE_DATA:
            if (par->codec_id != AV_CODEC_ID_TEXT && par->codec_id != AV_CODEC_ID_NONE)
                return unsupported_codec(s, "Data", par->codec_id);
            flv->data_par = par;
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            if (par->codec_id != AV_CODEC_ID_TEXT) {
                av_log(s, AV_LOG_ERROR, "Subtitle codec '%s' for stream %d is not compatible with FLV\n",
                       avcodec_get_name(par->codec_id), i);
                return AVERROR_INVALIDDATA;
            }
            flv->data_par = par;
            break;
        default:
            av_log(s, AV_LOG_ERROR, "Codec type '%s' for stream %d is not compatible with FLV\n",
                   av_get_media_type_string(par->codec_type), i);
            return AVERROR(EINVAL);
        }
        avpriv_set_pts_info(s->streams[i], 32, 1, 1000); /* 32 bit pts in ms */
        flv->last_ts[i] = -1;
    }

    flv->delay = AV_NOPTS_VALUE;

    return 0;
}

static int flv_write_header(AVFormatContext *s)
{
    int i;
    AVIOContext *pb = s->pb;
    FLVContext *flv = s->priv_data;

    avio_write(pb, "FLV", 3);
    avio_w8(pb, 1);
    avio_w8(pb, FLV_HEADER_FLAG_HASAUDIO * !!flv->audio_par +
                FLV_HEADER_FLAG_HASVIDEO * !!flv->video_par);
    avio_wb32(pb, 9);
    avio_wb32(pb, 0);

    for (i = 0; i < s->nb_streams; i++)
        if (s->streams[i]->codecpar->codec_tag == 5) {
            avio_w8(pb, 8);     // message type
            avio_wb24(pb, 0);   // include flags
            avio_wb24(pb, 0);   // time stamp
            avio_wb32(pb, 0);   // reserved
            avio_wb32(pb, 11);  // size
            flv->reserved = 5;
        }

    if (flv->flags & FLV_NO_METADATA) {
        pb->seekable = 0;
    } else {
        write_metadata(s, 0);
    }

    for (i = 0; i < s->nb_streams; i++) {
        flv_write_codec_header(s, s->streams[i]->codecpar, 0, i);
    }

    flv->datastart_offset = avio_tell(pb);
    return 0;
}

static int flv_write_trailer(AVFormatContext *s)
{
    int64_t file_size;
    AVIOContext *pb = s->pb;
    FLVContext *flv = s->priv_data;
    int build_keyframes_idx = flv->flags & FLV_ADD_KEYFRAME_INDEX;
    int i, res;
    int64_t cur_pos = avio_tell(s->pb);

    if (build_keyframes_idx) {
        const FLVFileposition *newflv_posinfo;

        avio_seek(pb, flv->videosize_offset, SEEK_SET);
        put_amf_double(pb, flv->videosize);

        avio_seek(pb, flv->audiosize_offset, SEEK_SET);
        put_amf_double(pb, flv->audiosize);

        avio_seek(pb, flv->lasttimestamp_offset, SEEK_SET);
        put_amf_double(pb, flv->lasttimestamp);

        avio_seek(pb, flv->lastkeyframetimestamp_offset, SEEK_SET);
        put_amf_double(pb, flv->lastkeyframetimestamp);

        avio_seek(pb, flv->lastkeyframelocation_offset, SEEK_SET);
        put_amf_double(pb, flv->lastkeyframelocation + flv->keyframe_index_size);
        avio_seek(pb, cur_pos, SEEK_SET);

        res = shift_data(s);
        if (res < 0) {
             goto end;
        }
        avio_seek(pb, flv->keyframes_info_offset, SEEK_SET);
        put_amf_string(pb, "filepositions");
        put_amf_dword_array(pb, flv->filepositions_count);
        for (newflv_posinfo = flv->head_filepositions; newflv_posinfo; newflv_posinfo = newflv_posinfo->next) {
            put_amf_double(pb, newflv_posinfo->keyframe_position + flv->keyframe_index_size);
        }

        put_amf_string(pb, "times");
        put_amf_dword_array(pb, flv->filepositions_count);
        for (newflv_posinfo = flv->head_filepositions; newflv_posinfo; newflv_posinfo = newflv_posinfo->next) {
            put_amf_double(pb, newflv_posinfo->keyframe_timestamp);
        }

        put_amf_string(pb, "");
        avio_w8(pb, AMF_END_OF_OBJECT);

        avio_seek(pb, cur_pos + flv->keyframe_index_size, SEEK_SET);
    }

end:
    if (flv->flags & FLV_NO_SEQUENCE_END) {
        av_log(s, AV_LOG_DEBUG, "FLV no sequence end mode open\n");
    } else {
        /* Add EOS tag */
        for (i = 0; i < s->nb_streams; i++) {
            AVCodecParameters *par = s->streams[i]->codecpar;
            if (par->codec_type == AVMEDIA_TYPE_VIDEO &&
                    (par->codec_id == AV_CODEC_ID_H264 || par->codec_id == AV_CODEC_ID_MPEG4))
                put_eos_tag(pb, flv->last_ts[i], par->codec_id);
        }
    }

    file_size = avio_tell(pb);

    if (build_keyframes_idx) {
        flv->datasize = file_size - flv->datastart_offset;
        avio_seek(pb, flv->datasize_offset, SEEK_SET);
        put_amf_double(pb, flv->datasize);
    }
    if (!(flv->flags & FLV_NO_METADATA)) {
        if (!(flv->flags & FLV_NO_DURATION_FILESIZE)) {
            /* update information */
            if (avio_seek(pb, flv->duration_offset, SEEK_SET) < 0) {
                av_log(s, AV_LOG_WARNING, "Failed to update header with correct duration.\n");
            } else {
                put_amf_double(pb, flv->duration / (double)1000);
            }
            if (avio_seek(pb, flv->filesize_offset, SEEK_SET) < 0) {
                av_log(s, AV_LOG_WARNING, "Failed to update header with correct filesize.\n");
            } else {
                put_amf_double(pb, file_size);
            }
        }
    }

    return 0;
}

static int flv_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb      = s->pb;
    AVCodecParameters *par = s->streams[pkt->stream_index]->codecpar;
    FLVContext *flv      = s->priv_data;
    unsigned ts;
    int size = pkt->size;
    uint8_t *data = NULL;
    uint8_t frametype = pkt->flags & AV_PKT_FLAG_KEY ? FLV_FRAME_KEY : FLV_FRAME_INTER;
    int flags = -1, flags_size, ret = 0;
    int64_t cur_offset = avio_tell(pb);
    int track_idx = flv->track_idx_map[pkt->stream_index];

    int extended_audio = (par->codec_id == AV_CODEC_ID_AAC && track_idx)
                            || (par->codec_id == AV_CODEC_ID_MP3 && track_idx)
                            || par->codec_id == AV_CODEC_ID_OPUS
                            || par->codec_id == AV_CODEC_ID_FLAC
                            || par->codec_id == AV_CODEC_ID_AC3
                            || par->codec_id == AV_CODEC_ID_EAC3;

    if (extended_audio)
        flags_size = 5;
    else if (par->codec_id == AV_CODEC_ID_VP6F || par->codec_id == AV_CODEC_ID_VP6A ||
             par->codec_id == AV_CODEC_ID_VP6  || par->codec_id == AV_CODEC_ID_AAC)
        flags_size = 2;
    else if (par->codec_id == AV_CODEC_ID_H264 || par->codec_id == AV_CODEC_ID_MPEG4 ||
             par->codec_id == AV_CODEC_ID_HEVC || par->codec_id == AV_CODEC_ID_AV1 ||
             par->codec_id == AV_CODEC_ID_VP9)
        flags_size = 5;
    else
        flags_size = 1;

    if ((par->codec_type == AVMEDIA_TYPE_VIDEO || par->codec_type == AVMEDIA_TYPE_AUDIO) && track_idx)
        flags_size += 2; // additional header bytes for multi-track flv

    if ((par->codec_id == AV_CODEC_ID_HEVC ||
        (par->codec_id == AV_CODEC_ID_H264 && track_idx))
            && pkt->pts != pkt->dts)
        flags_size += 3;

    if (par->codec_id == AV_CODEC_ID_AAC || par->codec_id == AV_CODEC_ID_H264
            || par->codec_id == AV_CODEC_ID_MPEG4 || par->codec_id == AV_CODEC_ID_HEVC
            || par->codec_id == AV_CODEC_ID_AV1 || par->codec_id == AV_CODEC_ID_VP9
            || par->codec_id == AV_CODEC_ID_OPUS || par->codec_id == AV_CODEC_ID_FLAC) {
        size_t side_size;
        uint8_t *side = av_packet_get_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA, &side_size);
        if (side && side_size > 0 && (side_size != par->extradata_size || memcmp(side, par->extradata, side_size))) {
            ret = ff_alloc_extradata(par, side_size);
            if (ret < 0)
                return ret;
            memcpy(par->extradata, side, side_size);
            flv_write_codec_header(s, par, pkt->dts, pkt->stream_index);
        }
        flv_write_metadata_packet(s, par, pkt->dts, pkt->stream_index);
    }

    if (flv->delay == AV_NOPTS_VALUE)
        flv->delay = -pkt->dts;

    if (pkt->dts < -flv->delay) {
        av_log(s, AV_LOG_WARNING,
               "Packets are not in the proper order with respect to DTS\n");
        return AVERROR(EINVAL);
    }
    if (par->codec_id == AV_CODEC_ID_H264 || par->codec_id == AV_CODEC_ID_MPEG4 ||
        par->codec_id == AV_CODEC_ID_HEVC ||  par->codec_id == AV_CODEC_ID_AV1 ||
        par->codec_id == AV_CODEC_ID_VP9) {
        if (pkt->pts == AV_NOPTS_VALUE) {
            av_log(s, AV_LOG_ERROR, "Packet is missing PTS\n");
            return AVERROR(EINVAL);
        }
    }

    ts = pkt->dts;

    if (s->event_flags & AVSTREAM_EVENT_FLAG_METADATA_UPDATED) {
        write_metadata(s, ts);
        s->event_flags &= ~AVSTREAM_EVENT_FLAG_METADATA_UPDATED;
    }

    avio_write_marker(pb, av_rescale(ts, AV_TIME_BASE, 1000),
                      pkt->flags & AV_PKT_FLAG_KEY && (flv->video_par ? par->codec_type == AVMEDIA_TYPE_VIDEO : 1) ? AVIO_DATA_MARKER_SYNC_POINT : AVIO_DATA_MARKER_BOUNDARY_POINT);

    switch (par->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        avio_w8(pb, FLV_TAG_TYPE_VIDEO);

        flags = ff_codec_get_tag(flv_video_codec_ids, par->codec_id);

        flags |= frametype;
        break;
    case AVMEDIA_TYPE_AUDIO:
        flags = get_audio_flags(s, par);

        avio_w8(pb, FLV_TAG_TYPE_AUDIO);
        break;
    case AVMEDIA_TYPE_SUBTITLE:
    case AVMEDIA_TYPE_DATA:
        avio_w8(pb, FLV_TAG_TYPE_META);
        break;
    default:
        return AVERROR(EINVAL);
    }

    if (par->codec_id == AV_CODEC_ID_H264 || par->codec_id == AV_CODEC_ID_MPEG4) {
        /* check if extradata looks like mp4 formatted */
        if (par->extradata_size > 0 && *(uint8_t*)par->extradata != 1)
            if ((ret = ff_nal_parse_units_buf(pkt->data, &data, &size)) < 0)
                return ret;
    } else if (par->codec_id == AV_CODEC_ID_HEVC) {
        if (par->extradata_size > 0 && *(uint8_t*)par->extradata != 1)
            if ((ret = ff_hevc_annexb2mp4_buf(pkt->data, &data, &size, 0, NULL)) < 0)
                return ret;
    } else if (par->codec_id == AV_CODEC_ID_AAC && pkt->size > 2 &&
               (AV_RB16(pkt->data) & 0xfff0) == 0xfff0) {
        if (!s->streams[pkt->stream_index]->nb_frames) {
            av_log(s, AV_LOG_ERROR, "Malformed AAC bitstream detected: "
                   "use the audio bitstream filter 'aac_adtstoasc' to fix it "
                   "('-bsf:a aac_adtstoasc' option with ffmpeg)\n");
            return AVERROR_INVALIDDATA;
        }
        av_log(s, AV_LOG_WARNING, "aac bitstream error\n");
    }

    /* check Speex packet duration */
    if (par->codec_id == AV_CODEC_ID_SPEEX && ts - flv->last_ts[pkt->stream_index] > 160)
        av_log(s, AV_LOG_WARNING, "Warning: Speex stream has more than "
                                  "8 frames per packet. Adobe Flash "
                                  "Player cannot handle this!\n");

    if (flv->last_ts[pkt->stream_index] < ts)
        flv->last_ts[pkt->stream_index] = ts;

    if (size + flags_size >= 1<<24) {
        av_log(s, AV_LOG_ERROR, "Too large packet with size %u >= %u\n",
               size + flags_size, 1<<24);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    avio_wb24(pb, size + flags_size);
    put_timestamp(pb, ts);
    avio_wb24(pb, flv->reserved);

    if (par->codec_type == AVMEDIA_TYPE_DATA ||
        par->codec_type == AVMEDIA_TYPE_SUBTITLE ) {
        int data_size;
        int64_t metadata_size_pos = avio_tell(pb);
        if (par->codec_id == AV_CODEC_ID_TEXT) {
            // legacy FFmpeg magic?
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
        } else {
            // just pass the metadata through
            avio_write(pb, data ? data : pkt->data, size);
        }
        /* write total size of tag */
        data_size = avio_tell(pb) - metadata_size_pos;
        avio_seek(pb, metadata_size_pos - 10, SEEK_SET);
        avio_wb24(pb, data_size);
        avio_seek(pb, data_size + 10 - 3, SEEK_CUR);
        avio_wb32(pb, data_size + 11);
    } else {
        int extended_video = (par->codec_id == AV_CODEC_ID_H264 && track_idx) ||
                              par->codec_id == AV_CODEC_ID_HEVC ||
                              par->codec_id == AV_CODEC_ID_AV1 ||
                              par->codec_id == AV_CODEC_ID_VP9;

        if (extended_video) {
            int h2645 = par->codec_id == AV_CODEC_ID_H264 ||
                        par->codec_id == AV_CODEC_ID_HEVC;
            int pkttype = PacketTypeCodedFrames;
            // Optimisation for HEVC/H264: Do not send composition time if DTS == PTS
            if (h2645 && pkt->pts == pkt->dts)
                pkttype = PacketTypeCodedFramesX;

            if (track_idx) {
                avio_w8(pb, FLV_IS_EX_HEADER | PacketTypeMultitrack | frametype);
                avio_w8(pb, MultitrackTypeOneTrack | pkttype);
            } else {
                avio_w8(pb, FLV_IS_EX_HEADER | pkttype | frametype);
            }

            write_codec_fourcc(pb, par->codec_id);

            if (track_idx)
                avio_w8(pb, track_idx);
            if (h2645 && pkttype == PacketTypeCodedFrames)
                avio_wb24(pb, pkt->pts - pkt->dts);
        } else if (extended_audio) {
            if (track_idx) {
                avio_w8(pb, FLV_CODECID_EX_HEADER | AudioPacketTypeMultitrack);
                avio_w8(pb, MultitrackTypeOneTrack | AudioPacketTypeCodedFrames);
            } else {
                avio_w8(pb, FLV_CODECID_EX_HEADER | AudioPacketTypeCodedFrames);
            }
            write_codec_fourcc(pb, par->codec_id);
            if (track_idx)
                avio_w8(pb, track_idx);
        } else if (track_idx) {
            av_log(s, AV_LOG_ERROR, "Attempted to write legacy codec into extended flv track.\n");
            ret = AVERROR(EINVAL);
            goto fail;
        } else {
            av_assert1(flags >= 0);
            avio_w8(pb, flags);

            if (par->codec_id == AV_CODEC_ID_VP6) {
                avio_w8(pb,0);
            } else if (par->codec_id == AV_CODEC_ID_VP6F ||
                        par->codec_id == AV_CODEC_ID_VP6A) {
                if (par->extradata_size)
                    avio_w8(pb, par->extradata[0]);
                else
                    avio_w8(pb, ((FFALIGN(par->width,  16) - par->width) << 4) |
                                (FFALIGN(par->height, 16) - par->height));
            } else if (par->codec_id == AV_CODEC_ID_AAC) {
                avio_w8(pb, 1); // AAC raw
            } else if (par->codec_id == AV_CODEC_ID_H264 ||
                        par->codec_id == AV_CODEC_ID_MPEG4) {
                avio_w8(pb, 1); // AVC NALU
                avio_wb24(pb, pkt->pts - pkt->dts);
            }
        }

        avio_write(pb, data ? data : pkt->data, size);

        avio_wb32(pb, size + flags_size + 11); // previous tag size
        flv->duration = FFMAX(flv->duration,
                              pkt->pts + flv->delay + pkt->duration);
    }

    if (flv->flags & FLV_ADD_KEYFRAME_INDEX) {
        switch (par->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                flv->videosize += (avio_tell(pb) - cur_offset);
                flv->lasttimestamp = pkt->dts / 1000.0;
                if (pkt->flags & AV_PKT_FLAG_KEY) {
                    flv->lastkeyframetimestamp = flv->lasttimestamp;
                    flv->lastkeyframelocation = cur_offset;
                    ret = flv_append_keyframe_info(s, flv, flv->lasttimestamp, cur_offset);
                    if (ret < 0)
                        goto fail;
                }
                break;

            case AVMEDIA_TYPE_AUDIO:
                flv->audiosize += (avio_tell(pb) - cur_offset);
                break;

            default:
                av_log(s, AV_LOG_WARNING, "par->codec_type is type = [%d]\n", par->codec_type);
                break;
        }
    }
fail:
    av_free(data);

    return ret;
}

static int flv_check_bitstream(AVFormatContext *s, AVStream *st,
                               const AVPacket *pkt)
{
    if (st->codecpar->codec_id == AV_CODEC_ID_AAC) {
        if (pkt->size > 2 && (AV_RB16(pkt->data) & 0xfff0) == 0xfff0)
            return ff_stream_add_bitstream_filter(st, "aac_adtstoasc", NULL);
    }
    if (!st->codecpar->extradata_size &&
            (st->codecpar->codec_id == AV_CODEC_ID_H264 ||
             st->codecpar->codec_id == AV_CODEC_ID_HEVC ||
             st->codecpar->codec_id == AV_CODEC_ID_AV1 ||
             st->codecpar->codec_id == AV_CODEC_ID_MPEG4))
        return ff_stream_add_bitstream_filter(st, "extract_extradata", NULL);
    return 1;
}

static void flv_deinit(AVFormatContext *s)
{
    FLVContext *flv = s->priv_data;
    FLVFileposition *filepos = flv->head_filepositions;

    while (filepos) {
        FLVFileposition *next = filepos->next;
        av_free(filepos);
        filepos = next;
    }
    flv->filepositions = flv->head_filepositions = NULL;
    flv->filepositions_count = 0;

    av_freep(&flv->last_ts);
    av_freep(&flv->metadata_pkt_written);
    av_freep(&flv->track_idx_map);
}

static const AVOption options[] = {
    { "flvflags", "FLV muxer flags", offsetof(FLVContext, flags), AV_OPT_TYPE_FLAGS, {.i64 = 0}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "flvflags" },
    { "aac_seq_header_detect", "Put AAC sequence header based on stream data", 0, AV_OPT_TYPE_CONST, {.i64 = FLV_AAC_SEQ_HEADER_DETECT}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "flvflags" },
    { "no_sequence_end", "disable sequence end for FLV", 0, AV_OPT_TYPE_CONST, {.i64 = FLV_NO_SEQUENCE_END}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "flvflags" },
    { "no_metadata", "disable metadata for FLV", 0, AV_OPT_TYPE_CONST, {.i64 = FLV_NO_METADATA}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "flvflags" },
    { "no_duration_filesize", "disable duration and filesize zero value metadata for FLV", 0, AV_OPT_TYPE_CONST, {.i64 = FLV_NO_DURATION_FILESIZE}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "flvflags" },
    { "add_keyframe_index", "Add keyframe index metadata", 0, AV_OPT_TYPE_CONST, {.i64 = FLV_ADD_KEYFRAME_INDEX}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "flvflags" },
    { NULL },
};

static const AVClass flv_muxer_class = {
    .class_name = "flv muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFOutputFormat ff_flv_muxer = {
    .p.name         = "flv",
    .p.long_name    = NULL_IF_CONFIG_SMALL("FLV (Flash Video)"),
    .p.mime_type    = "video/x-flv",
    .p.extensions   = "flv",
    .priv_data_size = sizeof(FLVContext),
    .p.audio_codec  = CONFIG_LIBMP3LAME ? AV_CODEC_ID_MP3 : AV_CODEC_ID_ADPCM_SWF,
    .p.video_codec  = AV_CODEC_ID_FLV1,
    .init           = flv_init,
    .write_header   = flv_write_header,
    .write_packet   = flv_write_packet,
    .write_trailer  = flv_write_trailer,
    .deinit         = flv_deinit,
    .check_bitstream= flv_check_bitstream,
    .p.codec_tag    = (const AVCodecTag* const []) {
                          flv_video_codec_ids, flv_audio_codec_ids, 0
                      },
    .p.flags        = AVFMT_GLOBALHEADER | AVFMT_VARIABLE_FPS |
                      AVFMT_TS_NONSTRICT,
    .p.priv_class   = &flv_muxer_class,
};
