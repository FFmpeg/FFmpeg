/*
 * FLV demuxer
 * Copyright (c) 2003 The FFmpeg Project
 *
 * This demuxer will generate a 1 byte extradata for VP6F content.
 * It is composed of:
 *  - upper 4bits: difference between encoded width and visible width
 *  - lower 4bits: difference between encoded height and visible height
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

#include "libavcodec/mpeg4audio.h"
#include "avformat.h"
#include "flv.h"

typedef struct {
    int wrong_dts; ///< wrong dts due to negative cts
} FLVContext;

static int flv_probe(AVProbeData *p)
{
    const uint8_t *d;

    d = p->buf;
    if (d[0] == 'F' && d[1] == 'L' && d[2] == 'V' && d[3] < 5 && d[5]==0) {
        return AVPROBE_SCORE_MAX;
    }
    return 0;
}

static void flv_set_audio_codec(AVFormatContext *s, AVStream *astream, int flv_codecid) {
    AVCodecContext *acodec = astream->codec;
    switch(flv_codecid) {
        //no distinction between S16 and S8 PCM codec flags
        case FLV_CODECID_PCM:
            acodec->codec_id = acodec->bits_per_coded_sample == 8 ? CODEC_ID_PCM_S8 :
#ifdef WORDS_BIGENDIAN
                                CODEC_ID_PCM_S16BE;
#else
                                CODEC_ID_PCM_S16LE;
#endif
            break;
        case FLV_CODECID_PCM_LE:
            acodec->codec_id = acodec->bits_per_coded_sample == 8 ? CODEC_ID_PCM_S8 : CODEC_ID_PCM_S16LE; break;
        case FLV_CODECID_AAC  : acodec->codec_id = CODEC_ID_AAC;                                    break;
        case FLV_CODECID_ADPCM: acodec->codec_id = CODEC_ID_ADPCM_SWF;                              break;
        case FLV_CODECID_SPEEX:
            acodec->codec_id = CODEC_ID_SPEEX;
            acodec->sample_rate = 16000;
            break;
        case FLV_CODECID_MP3  : acodec->codec_id = CODEC_ID_MP3      ; astream->need_parsing = AVSTREAM_PARSE_FULL; break;
        case FLV_CODECID_NELLYMOSER_8KHZ_MONO:
            acodec->sample_rate = 8000; //in case metadata does not otherwise declare samplerate
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
        case FLV_CODECID_VP6   : vcodec->codec_id = CODEC_ID_VP6F   ;
        case FLV_CODECID_VP6A  :
            if(flv_codecid == FLV_CODECID_VP6A)
                vcodec->codec_id = CODEC_ID_VP6A;
            if(vcodec->extradata_size != 1) {
                vcodec->extradata_size = 1;
                vcodec->extradata = av_malloc(1);
            }
            vcodec->extradata[0] = get_byte(s->pb);
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

static int amf_parse_object(AVFormatContext *s, AVStream *astream, AVStream *vstream, const char *key, int64_t max_pos, int depth) {
    AVCodecContext *acodec, *vcodec;
    ByteIOContext *ioc;
    AMFDataType amf_type;
    char str_val[256];
    double num_val;

    num_val = 0;
    ioc = s->pb;

    amf_type = get_byte(ioc);

    switch(amf_type) {
        case AMF_DATA_TYPE_NUMBER:
            num_val = av_int2dbl(get_be64(ioc)); break;
        case AMF_DATA_TYPE_BOOL:
            num_val = get_byte(ioc); break;
        case AMF_DATA_TYPE_STRING:
            if(amf_get_string(ioc, str_val, sizeof(str_val)) < 0)
                return -1;
            break;
        case AMF_DATA_TYPE_OBJECT: {
            unsigned int keylen;

            while(url_ftell(ioc) < max_pos - 2 && (keylen = get_be16(ioc))) {
                url_fskip(ioc, keylen); //skip key string
                if(amf_parse_object(s, NULL, NULL, NULL, max_pos, depth + 1) < 0)
                    return -1; //if we couldn't skip, bomb out.
            }
            if(get_byte(ioc) != AMF_END_OF_OBJECT)
                return -1;
        }
            break;
        case AMF_DATA_TYPE_NULL:
        case AMF_DATA_TYPE_UNDEFINED:
        case AMF_DATA_TYPE_UNSUPPORTED:
            break; //these take up no additional space
        case AMF_DATA_TYPE_MIXEDARRAY:
            url_fskip(ioc, 4); //skip 32-bit max array index
            while(url_ftell(ioc) < max_pos - 2 && amf_get_string(ioc, str_val, sizeof(str_val)) > 0) {
                //this is the only case in which we would want a nested parse to not skip over the object
                if(amf_parse_object(s, astream, vstream, str_val, max_pos, depth + 1) < 0)
                    return -1;
            }
            if(get_byte(ioc) != AMF_END_OF_OBJECT)
                return -1;
            break;
        case AMF_DATA_TYPE_ARRAY: {
            unsigned int arraylen, i;

            arraylen = get_be32(ioc);
            for(i = 0; i < arraylen && url_ftell(ioc) < max_pos - 1; i++) {
                if(amf_parse_object(s, NULL, NULL, NULL, max_pos, depth + 1) < 0)
                    return -1; //if we couldn't skip, bomb out.
            }
        }
            break;
        case AMF_DATA_TYPE_DATE:
            url_fskip(ioc, 8 + 2); //timestamp (double) and UTC offset (int16)
            break;
        default: //unsupported type, we couldn't skip
            return -1;
    }

    if(depth == 1 && key) { //only look for metadata values when we are not nested and key != NULL
        acodec = astream ? astream->codec : NULL;
        vcodec = vstream ? vstream->codec : NULL;

        if(amf_type == AMF_DATA_TYPE_BOOL) {
            if(!strcmp(key, "stereo") && acodec) acodec->channels = num_val > 0 ? 2 : 1;
        } else if(amf_type == AMF_DATA_TYPE_NUMBER) {
            if(!strcmp(key, "duration")) s->duration = num_val * AV_TIME_BASE;
//            else if(!strcmp(key, "width")  && vcodec && num_val > 0) vcodec->width  = num_val;
//            else if(!strcmp(key, "height") && vcodec && num_val > 0) vcodec->height = num_val;
            else if(!strcmp(key, "videodatarate") && vcodec && 0 <= (int)(num_val * 1024.0))
                vcodec->bit_rate = num_val * 1024.0;
            else if(!strcmp(key, "audiocodecid") && acodec && 0 <= (int)num_val)
                flv_set_audio_codec(s, astream, (int)num_val << FLV_AUDIO_CODECID_OFFSET);
            else if(!strcmp(key, "videocodecid") && vcodec && 0 <= (int)num_val)
                flv_set_video_codec(s, vstream, (int)num_val);
            else if(!strcmp(key, "audiosamplesize") && acodec && 0 < (int)num_val) {
                acodec->bits_per_coded_sample = num_val;
                //we may have to rewrite a previously read codecid because FLV only marks PCM endianness.
                if(num_val == 8 && (acodec->codec_id == CODEC_ID_PCM_S16BE || acodec->codec_id == CODEC_ID_PCM_S16LE))
                    acodec->codec_id = CODEC_ID_PCM_S8;
            }
            else if(!strcmp(key, "audiosamplerate") && acodec && num_val >= 0) {
                //some tools, like FLVTool2, write consistently approximate metadata sample rates
                if (!acodec->sample_rate) {
                    switch((int)num_val) {
                        case 44000: acodec->sample_rate = 44100  ; break;
                        case 22000: acodec->sample_rate = 22050  ; break;
                        case 11000: acodec->sample_rate = 11025  ; break;
                        case 5000 : acodec->sample_rate = 5512   ; break;
                        default   : acodec->sample_rate = num_val;
                    }
                }
            }
        }
    }

    return 0;
}

static int flv_read_metabody(AVFormatContext *s, int64_t next_pos) {
    AMFDataType type;
    AVStream *stream, *astream, *vstream;
    ByteIOContext *ioc;
    int i, keylen;
    char buffer[11]; //only needs to hold the string "onMetaData". Anything longer is something we don't want.

    astream = NULL;
    vstream = NULL;
    keylen = 0;
    ioc = s->pb;

    //first object needs to be "onMetaData" string
    type = get_byte(ioc);
    if(type != AMF_DATA_TYPE_STRING || amf_get_string(ioc, buffer, sizeof(buffer)) < 0 || strcmp(buffer, "onMetaData"))
        return -1;

    //find the streams now so that amf_parse_object doesn't need to do the lookup every time it is called.
    for(i = 0; i < s->nb_streams; i++) {
        stream = s->streams[i];
        if     (stream->codec->codec_type == CODEC_TYPE_AUDIO) astream = stream;
        else if(stream->codec->codec_type == CODEC_TYPE_VIDEO) vstream = stream;
    }

    //parse the second object (we want a mixed array)
    if(amf_parse_object(s, astream, vstream, buffer, next_pos, 0) < 0)
        return -1;

    return 0;
}

static AVStream *create_stream(AVFormatContext *s, int is_audio){
    AVStream *st = av_new_stream(s, is_audio);
    if (!st)
        return NULL;
    st->codec->codec_type = is_audio ? CODEC_TYPE_AUDIO : CODEC_TYPE_VIDEO;
    av_set_pts_info(st, 32, 1, 1000); /* 32 bit pts in ms */
    return st;
}

static int flv_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    int offset, flags;

    url_fskip(s->pb, 4);
    flags = get_byte(s->pb);
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

    offset = get_be32(s->pb);
    url_fseek(s->pb, offset, SEEK_SET);

    s->start_time = 0;

    return 0;
}

static int flv_get_extradata(AVFormatContext *s, AVStream *st, int size)
{
    av_free(st->codec->extradata);
    st->codec->extradata = av_mallocz(size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!st->codec->extradata)
        return AVERROR(ENOMEM);
    st->codec->extradata_size = size;
    get_buffer(s->pb, st->codec->extradata, st->codec->extradata_size);
    return 0;
}

static int flv_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    FLVContext *flv = s->priv_data;
    int ret, i, type, size, flags, is_audio;
    int64_t next, pos;
    int64_t dts, pts = AV_NOPTS_VALUE;
    AVStream *st = NULL;

 for(;;){
    pos = url_ftell(s->pb);
    url_fskip(s->pb, 4); /* size of previous packet */
    type = get_byte(s->pb);
    size = get_be24(s->pb);
    dts = get_be24(s->pb);
    dts |= get_byte(s->pb) << 24;
//    av_log(s, AV_LOG_DEBUG, "type:%d, size:%d, dts:%d\n", type, size, dts);
    if (url_feof(s->pb))
        return AVERROR_EOF;
    url_fskip(s->pb, 3); /* stream id, always 0 */
    flags = 0;

    if(size == 0)
        continue;

    next= size + url_ftell(s->pb);

    if (type == FLV_TAG_TYPE_AUDIO) {
        is_audio=1;
        flags = get_byte(s->pb);
        size--;
    } else if (type == FLV_TAG_TYPE_VIDEO) {
        is_audio=0;
        flags = get_byte(s->pb);
        size--;
        if ((flags & 0xf0) == 0x50) /* video info / command frame */
            goto skip;
    } else {
        if (type == FLV_TAG_TYPE_META && size > 13+1+4)
            flv_read_metabody(s, next);
        else /* skip packet */
            av_log(s, AV_LOG_ERROR, "skipping flv packet: type %d, size %d, flags %d\n", type, size, flags);
    skip:
        url_fseek(s->pb, next, SEEK_SET);
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
//    av_log(s, AV_LOG_DEBUG, "%d %X %d \n", is_audio, flags, st->discard);
    if(  (st->discard >= AVDISCARD_NONKEY && !((flags & FLV_VIDEO_FRAMETYPE_MASK) == FLV_FRAME_KEY ||         is_audio))
       ||(st->discard >= AVDISCARD_BIDIR  &&  ((flags & FLV_VIDEO_FRAMETYPE_MASK) == FLV_FRAME_DISP_INTER && !is_audio))
       || st->discard >= AVDISCARD_ALL
       ){
        url_fseek(s->pb, next, SEEK_SET);
        continue;
    }
    if ((flags & FLV_VIDEO_FRAMETYPE_MASK) == FLV_FRAME_KEY)
        av_add_index_entry(st, pos, dts, size, 0, AVINDEX_KEYFRAME);
    break;
 }

    // if not streamed and no duration from metadata then seek to end to find the duration from the timestamps
    if(!url_is_streamed(s->pb) && s->duration==AV_NOPTS_VALUE){
        int size;
        const int64_t pos= url_ftell(s->pb);
        const int64_t fsize= url_fsize(s->pb);
        url_fseek(s->pb, fsize-4, SEEK_SET);
        size= get_be32(s->pb);
        url_fseek(s->pb, fsize-3-size, SEEK_SET);
        if(size == get_be24(s->pb) + 11){
            s->duration= get_be24(s->pb) * (int64_t)AV_TIME_BASE / 1000;
        }
        url_fseek(s->pb, pos, SEEK_SET);
    }

    if(is_audio){
        if(!st->codec->channels || !st->codec->sample_rate || !st->codec->bits_per_coded_sample) {
            st->codec->channels = (flags & FLV_AUDIO_CHANNEL_MASK) == FLV_STEREO ? 2 : 1;
            st->codec->sample_rate = (44100 << ((flags & FLV_AUDIO_SAMPLERATE_MASK) >> FLV_AUDIO_SAMPLERATE_OFFSET) >> 3);
            st->codec->bits_per_coded_sample = (flags & FLV_AUDIO_SAMPLESIZE_MASK) ? 16 : 8;
        }
        if(!st->codec->codec_id){
            flv_set_audio_codec(s, st, flags & FLV_AUDIO_CODECID_MASK);
        }
    }else{
        size -= flv_set_video_codec(s, st, flags & FLV_VIDEO_CODECID_MASK);
    }

    if (st->codec->codec_id == CODEC_ID_AAC ||
        st->codec->codec_id == CODEC_ID_H264) {
        int type = get_byte(s->pb);
        size--;
        if (st->codec->codec_id == CODEC_ID_H264) {
            int32_t cts = (get_be24(s->pb)+0xff800000)^0xff800000; // sign extension
            pts = dts + cts;
            if (cts < 0) { // dts are wrong
                flv->wrong_dts = 1;
                av_log(s, AV_LOG_WARNING, "negative cts, previous timestamps might be wrong\n");
            }
            if (flv->wrong_dts)
                dts = AV_NOPTS_VALUE;
        }
        if (type == 0) {
            if ((ret = flv_get_extradata(s, st, size)) < 0)
                return ret;
            if (st->codec->codec_id == CODEC_ID_AAC) {
                MPEG4AudioConfig cfg;
                ff_mpeg4audio_get_config(&cfg, st->codec->extradata,
                                         st->codec->extradata_size);
                if (cfg.chan_config > 7)
                    return -1;
                st->codec->channels = ff_mpeg4audio_channels[cfg.chan_config];
                st->codec->sample_rate = cfg.sample_rate;
                dprintf(s, "mp4a config channels %d sample rate %d\n",
                        st->codec->channels, st->codec->sample_rate);
            }

            return AVERROR(EAGAIN);
        }
    }

    ret= av_get_packet(s->pb, pkt, size);
    if (ret <= 0) {
        return AVERROR(EIO);
    }
    /* note: we need to modify the packet size here to handle the last
       packet */
    pkt->size = ret;
    pkt->dts = dts;
    pkt->pts = pts == AV_NOPTS_VALUE ? dts : pts;
    pkt->stream_index = st->index;

    if (is_audio || ((flags & FLV_VIDEO_FRAMETYPE_MASK) == FLV_FRAME_KEY))
        pkt->flags |= PKT_FLAG_KEY;

    return ret;
}

AVInputFormat flv_demuxer = {
    "flv",
    NULL_IF_CONFIG_SMALL("FLV format"),
    sizeof(FLVContext),
    flv_probe,
    flv_read_header,
    flv_read_packet,
    .extensions = "flv",
    .value = CODEC_ID_FLV1,
};
