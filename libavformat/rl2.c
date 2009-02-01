/*
 * RL2 Format Demuxer
 * Copyright (c) 2008 Sascha Sommer (saschasommer@freenet.de)
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

/**
 * RL2 file demuxer
 * @file libavformat/rl2.c
 * @author Sascha Sommer (saschasommer@freenet.de)
 * For more information regarding the RL2 file format, visit:
 *   http://wiki.multimedia.cx/index.php?title=RL2
 *
 * extradata:
 * 2 byte le initial drawing offset within 320x200 viewport
 * 4 byte le number of used colors
 * 256 * 3 bytes rgb palette
 * optional background_frame
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"

#define EXTRADATA1_SIZE (6 + 256 * 3) ///< video base, clr, palette

#define FORM_TAG MKBETAG('F', 'O', 'R', 'M')
#define RLV2_TAG MKBETAG('R', 'L', 'V', '2')
#define RLV3_TAG MKBETAG('R', 'L', 'V', '3')

typedef struct Rl2DemuxContext {
    unsigned int index_pos[2];   ///< indexes in the sample tables
} Rl2DemuxContext;


/**
 * check if the file is in rl2 format
 * @param p probe buffer
 * @return 0 when the probe buffer does not contain rl2 data, > 0 otherwise
 */
static int rl2_probe(AVProbeData *p)
{

    if(AV_RB32(&p->buf[0]) != FORM_TAG)
        return 0;

    if(AV_RB32(&p->buf[8]) != RLV2_TAG &&
        AV_RB32(&p->buf[8]) != RLV3_TAG)
        return 0;

    return AVPROBE_SCORE_MAX;
}

/**
 * read rl2 header data and setup the avstreams
 * @param s demuxer context
 * @param ap format parameters
 * @return 0 on success, AVERROR otherwise
 */
static av_cold int rl2_read_header(AVFormatContext *s,
                            AVFormatParameters *ap)
{
    ByteIOContext *pb = s->pb;
    AVStream *st;
    unsigned int frame_count;
    unsigned int audio_frame_counter = 0;
    unsigned int video_frame_counter = 0;
    unsigned int back_size;
    int data_size;
    unsigned short encoding_method;
    unsigned short sound_rate;
    unsigned short rate;
    unsigned short channels;
    unsigned short def_sound_size;
    unsigned int signature;
    unsigned int pts_den = 11025; /* video only case */
    unsigned int pts_num = 1103;
    unsigned int* chunk_offset = NULL;
    int* chunk_size = NULL;
    int* audio_size = NULL;
    int i;
    int ret = 0;

    url_fskip(pb,4);          /* skip FORM tag */
    back_size = get_le32(pb); /** get size of the background frame */
    signature = get_be32(pb);
    data_size = get_be32(pb);
    frame_count = get_le32(pb);

    /* disallow back_sizes and frame_counts that may lead to overflows later */
    if(back_size > INT_MAX/2  || frame_count > INT_MAX / sizeof(uint32_t))
        return AVERROR_INVALIDDATA;

    encoding_method = get_le16(pb);
    sound_rate = get_le16(pb);
    rate = get_le16(pb);
    channels = get_le16(pb);
    def_sound_size = get_le16(pb);

    /** setup video stream */
    st = av_new_stream(s, 0);
    if(!st)
         return AVERROR(ENOMEM);

    st->codec->codec_type = CODEC_TYPE_VIDEO;
    st->codec->codec_id = CODEC_ID_RL2;
    st->codec->codec_tag = 0;  /* no fourcc */
    st->codec->width = 320;
    st->codec->height = 200;

    /** allocate and fill extradata */
    st->codec->extradata_size = EXTRADATA1_SIZE;

    if(signature == RLV3_TAG && back_size > 0)
        st->codec->extradata_size += back_size;

    st->codec->extradata = av_mallocz(st->codec->extradata_size +
                                          FF_INPUT_BUFFER_PADDING_SIZE);
    if(!st->codec->extradata)
        return AVERROR(ENOMEM);

    if(get_buffer(pb,st->codec->extradata,st->codec->extradata_size) !=
                      st->codec->extradata_size)
        return AVERROR(EIO);

    /** setup audio stream if present */
    if(sound_rate){
        pts_num = def_sound_size;
        pts_den = rate;

        st = av_new_stream(s, 0);
        if (!st)
            return AVERROR(ENOMEM);
        st->codec->codec_type = CODEC_TYPE_AUDIO;
        st->codec->codec_id = CODEC_ID_PCM_U8;
        st->codec->codec_tag = 1;
        st->codec->channels = channels;
        st->codec->bits_per_coded_sample = 8;
        st->codec->sample_rate = rate;
        st->codec->bit_rate = st->codec->channels * st->codec->sample_rate *
            st->codec->bits_per_coded_sample;
        st->codec->block_align = st->codec->channels *
            st->codec->bits_per_coded_sample / 8;
        av_set_pts_info(st,32,1,rate);
    }

    av_set_pts_info(s->streams[0], 32, pts_num, pts_den);

    chunk_size =   av_malloc(frame_count * sizeof(uint32_t));
    audio_size =   av_malloc(frame_count * sizeof(uint32_t));
    chunk_offset = av_malloc(frame_count * sizeof(uint32_t));

    if(!chunk_size || !audio_size || !chunk_offset){
        av_free(chunk_size);
        av_free(audio_size);
        av_free(chunk_offset);
        return AVERROR(ENOMEM);
    }

    /** read offset and size tables */
    for(i=0; i < frame_count;i++)
        chunk_size[i] = get_le32(pb);
    for(i=0; i < frame_count;i++)
        chunk_offset[i] = get_le32(pb);
    for(i=0; i < frame_count;i++)
        audio_size[i] = get_le32(pb) & 0xFFFF;

    /** build the sample index */
    for(i=0;i<frame_count;i++){
        if(chunk_size[i] < 0 || audio_size[i] > chunk_size[i]){
            ret = AVERROR_INVALIDDATA;
            break;
        }

        if(sound_rate && audio_size[i]){
            av_add_index_entry(s->streams[1], chunk_offset[i],
                audio_frame_counter,audio_size[i], 0, AVINDEX_KEYFRAME);
            audio_frame_counter += audio_size[i] / channels;
        }
        av_add_index_entry(s->streams[0], chunk_offset[i] + audio_size[i],
            video_frame_counter,chunk_size[i]-audio_size[i],0,AVINDEX_KEYFRAME);
        ++video_frame_counter;
    }


    av_free(chunk_size);
    av_free(audio_size);
    av_free(chunk_offset);

    return ret;
}

/**
 * read a single audio or video packet
 * @param s demuxer context
 * @param pkt the packet to be filled
 * @return 0 on success, AVERROR otherwise
 */
static int rl2_read_packet(AVFormatContext *s,
                            AVPacket *pkt)
{
    Rl2DemuxContext *rl2 = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVIndexEntry *sample = NULL;
    int i;
    int ret = 0;
    int stream_id = -1;
    int64_t pos = INT64_MAX;

    /** check if there is a valid video or audio entry that can be used */
    for(i=0; i<s->nb_streams; i++){
        if(rl2->index_pos[i] < s->streams[i]->nb_index_entries
              && s->streams[i]->index_entries[ rl2->index_pos[i] ].pos < pos){
            sample = &s->streams[i]->index_entries[ rl2->index_pos[i] ];
            pos= sample->pos;
            stream_id= i;
        }
    }

    if(stream_id == -1)
        return AVERROR(EIO);

    ++rl2->index_pos[stream_id];

    /** position the stream (will probably be there anyway) */
    url_fseek(pb, sample->pos, SEEK_SET);

    /** fill the packet */
    ret = av_get_packet(pb, pkt, sample->size);
    if(ret != sample->size){
        av_free_packet(pkt);
        return AVERROR(EIO);
    }

    pkt->stream_index = stream_id;
    pkt->pts = sample->timestamp;

    return ret;
}

/**
 * seek to a new timestamp
 * @param s demuxer context
 * @param stream_index index of the stream that should be seeked
 * @param timestamp wanted timestamp
 * @param flags direction and seeking mode
 * @return 0 on success, -1 otherwise
 */
static int rl2_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    AVStream *st = s->streams[stream_index];
    Rl2DemuxContext *rl2 = s->priv_data;
    int i;
    int index = av_index_search_timestamp(st, timestamp, flags);
    if(index < 0)
        return -1;

    rl2->index_pos[stream_index] = index;
    timestamp = st->index_entries[index].timestamp;

    for(i=0; i < s->nb_streams; i++){
        AVStream *st2 = s->streams[i];
        index = av_index_search_timestamp(st2,
                    av_rescale_q(timestamp, st->time_base, st2->time_base),
                    flags | AVSEEK_FLAG_BACKWARD);

        if(index < 0)
            index = 0;

        rl2->index_pos[i] = index;
    }

    return 0;
}

AVInputFormat rl2_demuxer = {
    "rl2",
    NULL_IF_CONFIG_SMALL("rl2 format"),
    sizeof(Rl2DemuxContext),
    rl2_probe,
    rl2_read_header,
    rl2_read_packet,
    NULL,
    rl2_read_seek,
};

