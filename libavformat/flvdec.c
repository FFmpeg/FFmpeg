/*
 * FLV demuxer
 * Copyright (c) 2003 The Libav Project
 *
 * This demuxer will generate a 1 byte extradata for VP6F content.
 * It is composed of:
 *  - upper 4bits: difference between encoded width and visible width
 *  - lower 4bits: difference between encoded height and visible height
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

#include "libavutil/avstring.h"
#include "libavutil/dict.h"
#include "libavutil/intfloat.h"
#include "libavutil/mathematics.h"
#include "libavcodec/bytestream.h"
#include "libavcodec/mpeg4audio.h"
#include "avformat.h"
#include "internal.h"
#include "avio_internal.h"
#include "flv.h"

#define KEYFRAMES_TAG            "keyframes"
#define KEYFRAMES_TIMESTAMP_TAG  "times"
#define KEYFRAMES_BYTEOFFSET_TAG "filepositions"

typedef struct {
    int wrong_dts; ///< wrong dts due to negative cts
    uint8_t *new_extradata[2];
    int      new_extradata_size[2];
    int      last_sample_rate;
    int      last_channels;
} FLVContext;

static int flv_probe(AVProbeData *p)
{
    const uint8_t *d;

    d = p->buf;
    if (d[0] == 'F' && d[1] == 'L' && d[2] == 'V' && d[3] < 5 && d[5]==0 && AV_RB32(d+5)>8) {
        return AVPROBE_SCORE_MAX;
    }
    return 0;
}

static void flv_set_audio_codec(AVFormatContext *s, AVStream *astream, AVCodecContext *acodec, int flv_codecid) {
    switch(flv_codecid) {
        //no distinction between S16 and S8 PCM codec flags
        case FLV_CODECID_PCM:
            acodec->codec_id = acodec->bits_per_coded_sample == 8 ? CODEC_ID_PCM_U8 :
#if HAVE_BIGENDIAN
                                CODEC_ID_PCM_S16BE;
#else
                                CODEC_ID_PCM_S16LE;
#endif
            break;
        case FLV_CODECID_PCM_LE:
            acodec->codec_id = acodec->bits_per_coded_sample == 8 ? CODEC_ID_PCM_U8 : CODEC_ID_PCM_S16LE; break;
        case FLV_CODECID_AAC  : acodec->codec_id = CODEC_ID_AAC;                                    break;
        case FLV_CODECID_ADPCM: acodec->codec_id = CODEC_ID_ADPCM_SWF;                              break;
        case FLV_CODECID_SPEEX:
            acodec->codec_id = CODEC_ID_SPEEX;
            acodec->sample_rate = 16000;
            break;
        case FLV_CODECID_MP3  : acodec->codec_id = CODEC_ID_MP3      ; astream->need_parsing = AVSTREAM_PARSE_FULL; break;
        case FLV_CODECID_NELLYMOSER_8KHZ_MONO:
            acodec->sample_rate = 8000; //in case metadata does not otherwise declare samplerate
            acodec->codec_id = CODEC_ID_NELLYMOSER;
            break;
        case FLV_CODECID_NELLYMOSER_16KHZ_MONO:
            acodec->sample_rate = 16000;
            acodec->codec_id = CODEC_ID_NELLYMOSER;
            break;
        case FLV_CODECID_NELLYMOSER:
            acodec->codec_id = CODEC_ID_NELLYMOSER;
            break;
        default:
            av_log(s, AV_LOG_INFO, "Unsupported audio codec (%x)\n", flv_codecid >> FLV_AUDIO_CODECID_OFFSET);
            acodec->codec_tag = flv_codecid >> FLV_AUDIO_CODECID_OFFSET;
    }
}

static int flv_set_video_codec(AVFormatContext *s, AVStream *vstream, int flv_codecid) {
    AVCodecContext *vcodec = vstream->codec;
    switch(flv_codecid) {
        case FLV_CODECID_H263  : vcodec->codec_id = CODEC_ID_FLV1   ; break;
        case FLV_CODECID_SCREEN: vcodec->codec_id = CODEC_ID_FLASHSV; break;
        case FLV_CODECID_SCREEN2: vcodec->codec_id = CODEC_ID_FLASHSV2; break;
        case FLV_CODECID_VP6   : vcodec->codec_id = CODEC_ID_VP6F   ;
        case FLV_CODECID_VP6A  :
            if(flv_codecid == FLV_CODECID_VP6A)
                vcodec->codec_id = CODEC_ID_VP6A;
            if(vcodec->extradata_size != 1) {
                vcodec->extradata_size = 1;
                vcodec->extradata = av_malloc(1);
            }
            vcodec->extradata[0] = avio_r8(s->pb);
            return 1; // 1 byte body size adjustment for flv_read_packet()
        case FLV_CODECID_H264:
            vcodec->codec_id = CODEC_ID_H264;
            return 3; // not 4, reading packet type will consume one byte
        default:
            av_log(s, AV_LOG_INFO, "Unsupported video codec (%x)\n", flv_codecid);
            vcodec->codec_tag = flv_codecid;
    }

    return 0;
}

static int amf_get_string(AVIOContext *ioc, char *buffer, int buffsize) {
    int length = avio_rb16(ioc);
    if(length >= buffsize) {
        avio_skip(ioc, length);
        return -1;
    }

    avio_read(ioc, buffer, length);

    buffer[length] = '\0';

    return length;
}

static int parse_keyframes_index(AVFormatContext *s, AVIOContext *ioc, AVStream *vstream, int64_t max_pos) {
    unsigned int arraylen = 0, timeslen = 0, fileposlen = 0, i;
    double num_val;
    char str_val[256];
    int64_t *times = NULL;
    int64_t *filepositions = NULL;
    int ret = AVERROR(ENOSYS);
    int64_t initial_pos = avio_tell(ioc);
    AVDictionaryEntry *creator = av_dict_get(s->metadata, "metadatacreator",
                                             NULL, 0);

    if (creator && !strcmp(creator->value, "MEGA")) {
        /* Files with this metadatacreator tag seem to have filepositions
         * pointing at the 4 trailer bytes of the previous packet,
         * which isn't the norm (nor what we expect here, nor what
         * jwplayer + lighttpd expect, nor what flvtool2 produces).
         * Just ignore the index in this case, instead of risking trying
         * to adjust it to something that might or might not work. */
        return 0;
    }

    while (avio_tell(ioc) < max_pos - 2 && amf_get_string(ioc, str_val, sizeof(str_val)) > 0) {
        int64_t* current_array;

        // Expect array object in context
        if (avio_r8(ioc) != AMF_DATA_TYPE_ARRAY)
            break;

        arraylen = avio_rb32(ioc);
        if (arraylen >> 28)
            break;

        /*
         * Expect only 'times' or 'filepositions' sub-arrays in other case refuse to use such metadata
         * for indexing
         */
        if (!strcmp(KEYFRAMES_TIMESTAMP_TAG, str_val) && !times) {
            if (!(times = av_mallocz(sizeof(*times) * arraylen))) {
                ret = AVERROR(ENOMEM);
                goto finish;
            }
            timeslen = arraylen;
            current_array = times;
        } else if (!strcmp(KEYFRAMES_BYTEOFFSET_TAG, str_val) && !filepositions) {
            if (!(filepositions = av_mallocz(sizeof(*filepositions) * arraylen))) {
                ret = AVERROR(ENOMEM);
                goto finish;
            }
            fileposlen = arraylen;
            current_array = filepositions;
        } else // unexpected metatag inside keyframes, will not use such metadata for indexing
            break;

        for (i = 0; i < arraylen && avio_tell(ioc) < max_pos - 1; i++) {
            if (avio_r8(ioc) != AMF_DATA_TYPE_NUMBER)
                goto finish;
            num_val = av_int2double(avio_rb64(ioc));
            current_array[i] = num_val;
        }
        if (times && filepositions) {
            // All done, exiting at a position allowing amf_parse_object
            // to finish parsing the object
            ret = 0;
            break;
        }
    }

    if (!ret && timeslen == fileposlen)
         for (i = 0; i < fileposlen; i++)
             av_add_index_entry(vstream, filepositions[i], times[i]*1000, 0, 0, AVINDEX_KEYFRAME);
    else
        av_log(s, AV_LOG_WARNING, "Invalid keyframes object, skipping.\n");

finish:
    av_freep(&times);
    av_freep(&filepositions);
    // If we got unexpected data, but successfully reset back to
    // the start pos, the caller can continue parsing
    if (ret < 0 && avio_seek(ioc, initial_pos, SEEK_SET) > 0)
        return 0;
    return ret;
}

static int amf_parse_object(AVFormatContext *s, AVStream *astream, AVStream *vstream, const char *key, int64_t max_pos, int depth) {
    AVCodecContext *acodec, *vcodec;
    AVIOContext *ioc;
    AMFDataType amf_type;
    char str_val[256];
    double num_val;

    num_val = 0;
    ioc = s->pb;

    amf_type = avio_r8(ioc);

    switch(amf_type) {
        case AMF_DATA_TYPE_NUMBER:
            num_val = av_int2double(avio_rb64(ioc)); break;
        case AMF_DATA_TYPE_BOOL:
            num_val = avio_r8(ioc); break;
        case AMF_DATA_TYPE_STRING:
            if(amf_get_string(ioc, str_val, sizeof(str_val)) < 0)
                return -1;
            break;
        case AMF_DATA_TYPE_OBJECT: {
            unsigned int keylen;

            if ((vstream || astream) && key && !strcmp(KEYFRAMES_TAG, key) && depth == 1)
                if (parse_keyframes_index(s, ioc, vstream ? vstream : astream,
                                          max_pos) < 0)
                    return -1;

            while(avio_tell(ioc) < max_pos - 2 && (keylen = avio_rb16(ioc))) {
                avio_skip(ioc, keylen); //skip key string
                if(amf_parse_object(s, NULL, NULL, NULL, max_pos, depth + 1) < 0)
                    return -1; //if we couldn't skip, bomb out.
            }
            if(avio_r8(ioc) != AMF_END_OF_OBJECT)
                return -1;
        }
            break;
        case AMF_DATA_TYPE_NULL:
        case AMF_DATA_TYPE_UNDEFINED:
        case AMF_DATA_TYPE_UNSUPPORTED:
            break; //these take up no additional space
        case AMF_DATA_TYPE_MIXEDARRAY:
            avio_skip(ioc, 4); //skip 32-bit max array index
            while(avio_tell(ioc) < max_pos - 2 && amf_get_string(ioc, str_val, sizeof(str_val)) > 0) {
                //this is the only case in which we would want a nested parse to not skip over the object
                if(amf_parse_object(s, astream, vstream, str_val, max_pos, depth + 1) < 0)
                    return -1;
            }
            if(avio_r8(ioc) != AMF_END_OF_OBJECT)
                return -1;
            break;
        case AMF_DATA_TYPE_ARRAY: {
            unsigned int arraylen, i;

            arraylen = avio_rb32(ioc);
            for(i = 0; i < arraylen && avio_tell(ioc) < max_pos - 1; i++) {
                if(amf_parse_object(s, NULL, NULL, NULL, max_pos, depth + 1) < 0)
                    return -1; //if we couldn't skip, bomb out.
            }
        }
            break;
        case AMF_DATA_TYPE_DATE:
            avio_skip(ioc, 8 + 2); //timestamp (double) and UTC offset (int16)
            break;
        default: //unsupported type, we couldn't skip
            return -1;
    }

    if(depth == 1 && key) { //only look for metadata values when we are not nested and key != NULL
        acodec = astream ? astream->codec : NULL;
        vcodec = vstream ? vstream->codec : NULL;

        if (amf_type == AMF_DATA_TYPE_NUMBER) {
            if (!strcmp(key, "duration"))
                s->duration = num_val * AV_TIME_BASE;
            else if (!strcmp(key, "videodatarate") && vcodec && 0 <= (int)(num_val * 1024.0))
                vcodec->bit_rate = num_val * 1024.0;
            else if (!strcmp(key, "audiodatarate") && acodec && 0 <= (int)(num_val * 1024.0))
                acodec->bit_rate = num_val * 1024.0;
        }

        if (!strcmp(key, "duration")        ||
            !strcmp(key, "filesize")        ||
            !strcmp(key, "width")           ||
            !strcmp(key, "height")          ||
            !strcmp(key, "videodatarate")   ||
            !strcmp(key, "framerate")       ||
            !strcmp(key, "videocodecid")    ||
            !strcmp(key, "audiodatarate")   ||
            !strcmp(key, "audiosamplerate") ||
            !strcmp(key, "audiosamplesize") ||
            !strcmp(key, "stereo")          ||
            !strcmp(key, "audiocodecid"))
            return 0;

        if(amf_type == AMF_DATA_TYPE_BOOL) {
            av_strlcpy(str_val, num_val > 0 ? "true" : "false", sizeof(str_val));
            av_dict_set(&s->metadata, key, str_val, 0);
        } else if(amf_type == AMF_DATA_TYPE_NUMBER) {
            snprintf(str_val, sizeof(str_val), "%.f", num_val);
            av_dict_set(&s->metadata, key, str_val, 0);
        } else if (amf_type == AMF_DATA_TYPE_STRING)
            av_dict_set(&s->metadata, key, str_val, 0);
    }

    return 0;
}

static int flv_read_metabody(AVFormatContext *s, int64_t next_pos) {
    AMFDataType type;
    AVStream *stream, *astream, *vstream;
    AVIOContext *ioc;
    int i;
    char buffer[11]; //only needs to hold the string "onMetaData". Anything longer is something we don't want.

    astream = NULL;
    vstream = NULL;
    ioc = s->pb;

    //first object needs to be "onMetaData" string
    type = avio_r8(ioc);
    if(type != AMF_DATA_TYPE_STRING || amf_get_string(ioc, buffer, sizeof(buffer)) < 0 || strcmp(buffer, "onMetaData"))
        return -1;

    //find the streams now so that amf_parse_object doesn't need to do the lookup every time it is called.
    for(i = 0; i < s->nb_streams; i++) {
        stream = s->streams[i];
        if     (stream->codec->codec_type == AVMEDIA_TYPE_AUDIO) astream = stream;
        else if(stream->codec->codec_type == AVMEDIA_TYPE_VIDEO) vstream = stream;
    }

    //parse the second object (we want a mixed array)
    if(amf_parse_object(s, astream, vstream, buffer, next_pos, 0) < 0)
        return -1;

    return 0;
}

static AVStream *create_stream(AVFormatContext *s, int is_audio){
    AVStream *st = avformat_new_stream(s, NULL);
    if (!st)
        return NULL;
    st->id = is_audio;
    st->codec->codec_type = is_audio ? AVMEDIA_TYPE_AUDIO : AVMEDIA_TYPE_VIDEO;
    avpriv_set_pts_info(st, 32, 1, 1000); /* 32 bit pts in ms */
    return st;
}

static int flv_read_header(AVFormatContext *s)
{
    int offset, flags;

    avio_skip(s->pb, 4);
    flags = avio_r8(s->pb);
    /* old flvtool cleared this field */
    /* FIXME: better fix needed */
    if (!flags) {
        flags = FLV_HEADER_FLAG_HASVIDEO | FLV_HEADER_FLAG_HASAUDIO;
        av_log(s, AV_LOG_WARNING, "Broken FLV file, which says no streams present, this might fail\n");
    }

    if((flags & (FLV_HEADER_FLAG_HASVIDEO|FLV_HEADER_FLAG_HASAUDIO))
             != (FLV_HEADER_FLAG_HASVIDEO|FLV_HEADER_FLAG_HASAUDIO))
        s->ctx_flags |= AVFMTCTX_NOHEADER;

    if(flags & FLV_HEADER_FLAG_HASVIDEO){
        if(!create_stream(s, 0))
            return AVERROR(ENOMEM);
    }
    if(flags & FLV_HEADER_FLAG_HASAUDIO){
        if(!create_stream(s, 1))
            return AVERROR(ENOMEM);
    }

    offset = avio_rb32(s->pb);
    avio_seek(s->pb, offset, SEEK_SET);
    avio_skip(s->pb, 4);

    s->start_time = 0;

    return 0;
}

static int flv_read_close(AVFormatContext *s)
{
    FLVContext *flv = s->priv_data;
    av_freep(&flv->new_extradata[0]);
    av_freep(&flv->new_extradata[1]);
    return 0;
}

static int flv_get_extradata(AVFormatContext *s, AVStream *st, int size)
{
    av_free(st->codec->extradata);
    st->codec->extradata = av_mallocz(size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!st->codec->extradata)
        return AVERROR(ENOMEM);
    st->codec->extradata_size = size;
    avio_read(s->pb, st->codec->extradata, st->codec->extradata_size);
    return 0;
}

static int flv_queue_extradata(FLVContext *flv, AVIOContext *pb, int stream,
                               int size)
{
    av_free(flv->new_extradata[stream]);
    flv->new_extradata[stream] = av_mallocz(size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!flv->new_extradata[stream])
        return AVERROR(ENOMEM);
    flv->new_extradata_size[stream] = size;
    avio_read(pb, flv->new_extradata[stream], size);
    return 0;
}

static int flv_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    FLVContext *flv = s->priv_data;
    int ret, i, type, size, flags, is_audio;
    int64_t next, pos;
    int64_t dts, pts = AV_NOPTS_VALUE;
    int sample_rate = 0, channels = 0;
    AVStream *st = NULL;

 for(;;avio_skip(s->pb, 4)){ /* pkt size is repeated at end. skip it */
    pos = avio_tell(s->pb);
    type = avio_r8(s->pb);
    size = avio_rb24(s->pb);
    dts = avio_rb24(s->pb);
    dts |= avio_r8(s->pb) << 24;
    av_dlog(s, "type:%d, size:%d, dts:%"PRId64"\n", type, size, dts);
    if (s->pb->eof_reached)
        return AVERROR_EOF;
    avio_skip(s->pb, 3); /* stream id, always 0 */
    flags = 0;

    if(size == 0)
        continue;

    next= size + avio_tell(s->pb);

    if (type == FLV_TAG_TYPE_AUDIO) {
        is_audio=1;
        flags = avio_r8(s->pb);
        size--;
    } else if (type == FLV_TAG_TYPE_VIDEO) {
        is_audio=0;
        flags = avio_r8(s->pb);
        size--;
        if ((flags & 0xf0) == 0x50) /* video info / command frame */
            goto skip;
    } else {
        if (type == FLV_TAG_TYPE_META && size > 13+1+4)
            flv_read_metabody(s, next);
        else /* skip packet */
            av_log(s, AV_LOG_DEBUG, "skipping flv packet: type %d, size %d, flags %d\n", type, size, flags);
    skip:
        avio_seek(s->pb, next, SEEK_SET);
        continue;
    }

    /* skip empty data packets */
    if (!size)
        continue;

    /* now find stream */
    for(i=0;i<s->nb_streams;i++) {
        st = s->streams[i];
        if (st->id == is_audio)
            break;
    }
    if(i == s->nb_streams){
        av_log(s, AV_LOG_ERROR, "invalid stream\n");
        st= create_stream(s, is_audio);
        s->ctx_flags &= ~AVFMTCTX_NOHEADER;
    }
    av_dlog(s, "%d %X %d \n", is_audio, flags, st->discard);
    if(  (st->discard >= AVDISCARD_NONKEY && !((flags & FLV_VIDEO_FRAMETYPE_MASK) == FLV_FRAME_KEY ||         is_audio))
       ||(st->discard >= AVDISCARD_BIDIR  &&  ((flags & FLV_VIDEO_FRAMETYPE_MASK) == FLV_FRAME_DISP_INTER && !is_audio))
       || st->discard >= AVDISCARD_ALL
       ){
        avio_seek(s->pb, next, SEEK_SET);
        continue;
    }
    if ((flags & FLV_VIDEO_FRAMETYPE_MASK) == FLV_FRAME_KEY)
        av_add_index_entry(st, pos, dts, size, 0, AVINDEX_KEYFRAME);
    break;
 }

    // if not streamed and no duration from metadata then seek to end to find the duration from the timestamps
    if(s->pb->seekable && (!s->duration || s->duration==AV_NOPTS_VALUE)){
        int size;
        const int64_t pos= avio_tell(s->pb);
        const int64_t fsize= avio_size(s->pb);
        avio_seek(s->pb, fsize-4, SEEK_SET);
        size= avio_rb32(s->pb);
        avio_seek(s->pb, fsize-3-size, SEEK_SET);
        if(size == avio_rb24(s->pb) + 11){
            uint32_t ts = avio_rb24(s->pb);
            ts |= avio_r8(s->pb) << 24;
            s->duration = ts * (int64_t)AV_TIME_BASE / 1000;
        }
        avio_seek(s->pb, pos, SEEK_SET);
    }

    if(is_audio){
        int bits_per_coded_sample;
        channels    = (flags & FLV_AUDIO_CHANNEL_MASK) == FLV_STEREO ? 2 : 1;
        sample_rate = (44100 << ((flags & FLV_AUDIO_SAMPLERATE_MASK) >> FLV_AUDIO_SAMPLERATE_OFFSET) >> 3);
        bits_per_coded_sample = (flags & FLV_AUDIO_SAMPLESIZE_MASK) ? 16 : 8;
        if(!st->codec->channels || !st->codec->sample_rate || !st->codec->bits_per_coded_sample) {
            st->codec->channels              = channels;
            st->codec->sample_rate           = sample_rate;
            st->codec->bits_per_coded_sample = bits_per_coded_sample;
        }
        if(!st->codec->codec_id){
            flv_set_audio_codec(s, st, st->codec, flags & FLV_AUDIO_CODECID_MASK);
            flv->last_sample_rate = st->codec->sample_rate;
            flv->last_channels    = st->codec->channels;
        } else {
            AVCodecContext ctx;
            ctx.sample_rate = sample_rate;
            flv_set_audio_codec(s, st, &ctx, flags & FLV_AUDIO_CODECID_MASK);
            sample_rate = ctx.sample_rate;
        }
    }else{
        size -= flv_set_video_codec(s, st, flags & FLV_VIDEO_CODECID_MASK);
    }

    if (st->codec->codec_id == CODEC_ID_AAC ||
        st->codec->codec_id == CODEC_ID_H264) {
        int type = avio_r8(s->pb);
        size--;
        if (st->codec->codec_id == CODEC_ID_H264) {
            int32_t cts = (avio_rb24(s->pb)+0xff800000)^0xff800000; // sign extension
            pts = dts + cts;
            if (cts < 0) { // dts are wrong
                flv->wrong_dts = 1;
                av_log(s, AV_LOG_WARNING, "negative cts, previous timestamps might be wrong\n");
            }
            if (flv->wrong_dts)
                dts = AV_NOPTS_VALUE;
        }
        if (type == 0) {
            if (st->codec->extradata) {
                if ((ret = flv_queue_extradata(flv, s->pb, is_audio, size)) < 0)
                    return ret;
                ret = AVERROR(EAGAIN);
                goto leave;
            }
            if ((ret = flv_get_extradata(s, st, size)) < 0)
                return ret;
            if (st->codec->codec_id == CODEC_ID_AAC) {
                MPEG4AudioConfig cfg;
                avpriv_mpeg4audio_get_config(&cfg, st->codec->extradata,
                                             st->codec->extradata_size * 8, 1);
                st->codec->channels = cfg.channels;
                if (cfg.ext_sample_rate)
                    st->codec->sample_rate = cfg.ext_sample_rate;
                else
                    st->codec->sample_rate = cfg.sample_rate;
                av_dlog(s, "mp4a config channels %d sample rate %d\n",
                        st->codec->channels, st->codec->sample_rate);
            }

            ret = AVERROR(EAGAIN);
            goto leave;
        }
    }

    /* skip empty data packets */
    if (!size) {
        ret = AVERROR(EAGAIN);
        goto leave;
    }

    ret= av_get_packet(s->pb, pkt, size);
    if (ret < 0) {
        return AVERROR(EIO);
    }
    /* note: we need to modify the packet size here to handle the last
       packet */
    pkt->size = ret;
    pkt->dts = dts;
    pkt->pts = pts == AV_NOPTS_VALUE ? dts : pts;
    pkt->stream_index = st->index;
    if (flv->new_extradata[is_audio]) {
        uint8_t *side = av_packet_new_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA,
                                                flv->new_extradata_size[is_audio]);
        if (side) {
            memcpy(side, flv->new_extradata[is_audio],
                   flv->new_extradata_size[is_audio]);
            av_freep(&flv->new_extradata[is_audio]);
            flv->new_extradata_size[is_audio] = 0;
        }
    }
    if (is_audio && (sample_rate != flv->last_sample_rate ||
                     channels != flv->last_channels)) {
        flv->last_sample_rate = sample_rate;
        flv->last_channels    = channels;
        ff_add_param_change(pkt, channels, 0, sample_rate, 0, 0);
    }

    if (is_audio || ((flags & FLV_VIDEO_FRAMETYPE_MASK) == FLV_FRAME_KEY))
        pkt->flags |= AV_PKT_FLAG_KEY;

leave:
    avio_skip(s->pb, 4);
    return ret;
}

static int flv_read_seek(AVFormatContext *s, int stream_index,
    int64_t ts, int flags)
{
    return avio_seek_time(s->pb, stream_index, ts, flags);
}

#if 0 /* don't know enough to implement this */
static int flv_read_seek2(AVFormatContext *s, int stream_index,
    int64_t min_ts, int64_t ts, int64_t max_ts, int flags)
{
    int ret = AVERROR(ENOSYS);

    if (ts - min_ts > (uint64_t)(max_ts - ts)) flags |= AVSEEK_FLAG_BACKWARD;

    if (!s->pb->seekable) {
        if (stream_index < 0) {
            stream_index = av_find_default_stream_index(s);
            if (stream_index < 0)
                return -1;

            /* timestamp for default must be expressed in AV_TIME_BASE units */
            ts = av_rescale_rnd(ts, 1000, AV_TIME_BASE,
                flags & AVSEEK_FLAG_BACKWARD ? AV_ROUND_DOWN : AV_ROUND_UP);
        }
        ret = avio_seek_time(s->pb, stream_index, ts, flags);
    }

    if (ret == AVERROR(ENOSYS))
        ret = av_seek_frame(s, stream_index, ts, flags);
    return ret;
}
#endif

AVInputFormat ff_flv_demuxer = {
    .name           = "flv",
    .long_name      = NULL_IF_CONFIG_SMALL("FLV format"),
    .priv_data_size = sizeof(FLVContext),
    .read_probe     = flv_probe,
    .read_header    = flv_read_header,
    .read_packet    = flv_read_packet,
    .read_seek = flv_read_seek,
#if 0
    .read_seek2 = flv_read_seek2,
#endif
    .read_close = flv_read_close,
    .extensions = "flv",
    .value = CODEC_ID_FLV1,
};
