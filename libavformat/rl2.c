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
 * @file
 * @author Sascha Sommer (saschasommer@freenet.de)
 * @see http://wiki.multimedia.cx/index.php?title=RL2
 *
 * extradata:
 * 2 byte le initial drawing offset within 320x200 viewport
 * 4 byte le number of used colors
 * 256 * 3 bytes rgb palette
 * optional background_frame
 */

#include <stdint.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "avformat.h"
#include "internal.h"

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
static int rl2_probe(const AVProbeData *p)
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
 * @return 0 on success, AVERROR otherwise
 */
static av_cold int rl2_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVStream *st;
    unsigned int frame_count;
    unsigned int audio_frame_counter = 0;
    unsigned int video_frame_counter = 0;
    unsigned int back_size;
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

    avio_skip(pb,4);          /* skip FORM tag */
    back_size = avio_rl32(pb); /**< get size of the background frame */
    signature = avio_rb32(pb);
    avio_skip(pb, 4);         /* data size */
    frame_count = avio_rl32(pb);

    /* disallow back_sizes and frame_counts that may lead to overflows later */
    if(back_size > INT_MAX/2  || frame_count > INT_MAX / sizeof(uint32_t))
        return AVERROR_INVALIDDATA;

    avio_skip(pb, 2);         /* encoding method */
    sound_rate = avio_rl16(pb);
    rate = avio_rl16(pb);
    channels = avio_rl16(pb);
    def_sound_size = avio_rl16(pb);

    /** setup video stream */
    st = avformat_new_stream(s, NULL);
    if(!st)
         return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = AV_CODEC_ID_RL2;
    st->codecpar->codec_tag = 0;  /* no fourcc */
    st->codecpar->width = 320;
    st->codecpar->height = 200;

    /** allocate and fill extradata */
    st->codecpar->extradata_size = EXTRADATA1_SIZE;

    if(signature == RLV3_TAG && back_size > 0)
        st->codecpar->extradata_size += back_size;

    ret = ff_get_extradata(s, st->codecpar, pb, st->codecpar->extradata_size);
    if (ret < 0)
        return ret;

    /** setup audio stream if present */
    if(sound_rate){
        if (!channels || channels > 42) {
            av_log(s, AV_LOG_ERROR, "Invalid number of channels: %d\n", channels);
            return AVERROR_INVALIDDATA;
        }

        pts_num = def_sound_size;
        pts_den = rate;

        st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR(ENOMEM);
        st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codecpar->codec_id = AV_CODEC_ID_PCM_U8;
        st->codecpar->codec_tag = 1;
        st->codecpar->ch_layout.nb_channels = channels;
        st->codecpar->bits_per_coded_sample = 8;
        st->codecpar->sample_rate = rate;
        st->codecpar->bit_rate = channels * st->codecpar->sample_rate *
            st->codecpar->bits_per_coded_sample;
        st->codecpar->block_align = channels *
            st->codecpar->bits_per_coded_sample / 8;
        avpriv_set_pts_info(st,32,1,rate);
    }

    avpriv_set_pts_info(s->streams[0], 32, pts_num, pts_den);

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
    for(i=0; i < frame_count;i++) {
        if (avio_feof(pb)) {
            ret = AVERROR_INVALIDDATA;
            goto end;
        }
        chunk_size[i] = avio_rl32(pb);
    }
    for(i=0; i < frame_count;i++) {
        if (avio_feof(pb)) {
            ret = AVERROR_INVALIDDATA;
            goto end;
        }
        chunk_offset[i] = avio_rl32(pb);
    }
    for(i=0; i < frame_count;i++) {
        if (avio_feof(pb)) {
            ret = AVERROR_INVALIDDATA;
            goto end;
        }
        audio_size[i] = avio_rl32(pb) & 0xFFFF;
    }

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

end:
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
    AVIOContext *pb = s->pb;
    AVIndexEntry *sample = NULL;
    int i;
    int ret = 0;
    int stream_id = -1;
    int64_t pos = INT64_MAX;

    /** check if there is a valid video or audio entry that can be used */
    for(i=0; i<s->nb_streams; i++){
        const FFStream *const sti = ffstream(s->streams[i]);
        if (rl2->index_pos[i] < sti->nb_index_entries
              && sti->index_entries[ rl2->index_pos[i] ].pos < pos) {
            sample = &sti->index_entries[ rl2->index_pos[i] ];
            pos= sample->pos;
            stream_id= i;
        }
    }

    if(stream_id == -1)
        return AVERROR_EOF;

    ++rl2->index_pos[stream_id];

    /** position the stream (will probably be there anyway) */
    avio_seek(pb, sample->pos, SEEK_SET);

    /** fill the packet */
    ret = av_get_packet(pb, pkt, sample->size);
    if(ret != sample->size){
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
    timestamp = ffstream(st)->index_entries[index].timestamp;

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

const AVInputFormat ff_rl2_demuxer = {
    .name           = "rl2",
    .long_name      = NULL_IF_CONFIG_SMALL("RL2"),
    .priv_data_size = sizeof(Rl2DemuxContext),
    .read_probe     = rl2_probe,
    .read_header    = rl2_read_header,
    .read_packet    = rl2_read_packet,
    .read_seek      = rl2_read_seek,
};
