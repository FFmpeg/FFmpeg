/*
 * Tiertex Limited SEQ File Demuxer
 * Copyright (c) 2006 Gregory Montoir (cyx@users.sourceforge.net)
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
 * @file
 * Tiertex Limited SEQ file demuxer
 */

#include "avformat.h"

#define SEQ_FRAME_SIZE         6144
#define SEQ_FRAME_W            256
#define SEQ_FRAME_H            128
#define SEQ_NUM_FRAME_BUFFERS  30
#define SEQ_AUDIO_BUFFER_SIZE  882
#define SEQ_SAMPLE_RATE        22050
#define SEQ_FRAME_RATE         25


typedef struct TiertexSeqFrameBuffer {
    int fill_size;
    int data_size;
    unsigned char *data;
} TiertexSeqFrameBuffer;

typedef struct SeqDemuxContext {
    int audio_stream_index;
    int video_stream_index;
    int current_frame_pts;
    int current_frame_offs;
    TiertexSeqFrameBuffer frame_buffers[SEQ_NUM_FRAME_BUFFERS];
    int frame_buffers_count;
    unsigned int current_audio_data_size;
    unsigned int current_audio_data_offs;
    unsigned int current_pal_data_size;
    unsigned int current_pal_data_offs;
    unsigned int current_video_data_size;
    unsigned char *current_video_data_ptr;
    int audio_buffer_full;
} SeqDemuxContext;


static int seq_probe(AVProbeData *p)
{
    int i;

    if (p->buf_size < 258)
        return 0;

    /* there's no real header in a .seq file, the only thing they have in common */
    /* is the first 256 bytes of the file which are always filled with 0 */
    for (i = 0; i < 256; i++)
        if (p->buf[i])
            return 0;

    if(p->buf[256]==0 && p->buf[257]==0)
        return 0;

    /* only one fourth of the score since the previous check is too naive */
    return AVPROBE_SCORE_MAX / 4;
}

static int seq_init_frame_buffers(SeqDemuxContext *seq, ByteIOContext *pb)
{
    int i, sz;
    TiertexSeqFrameBuffer *seq_buffer;

    url_fseek(pb, 256, SEEK_SET);

    for (i = 0; i < SEQ_NUM_FRAME_BUFFERS; i++) {
        sz = get_le16(pb);
        if (sz == 0)
            break;
        else {
            seq_buffer = &seq->frame_buffers[i];
            seq_buffer->fill_size = 0;
            seq_buffer->data_size = sz;
            seq_buffer->data = av_malloc(sz);
            if (!seq_buffer->data)
                return AVERROR(ENOMEM);
        }
    }
    seq->frame_buffers_count = i;
    return 0;
}

static int seq_fill_buffer(SeqDemuxContext *seq, ByteIOContext *pb, int buffer_num, unsigned int data_offs, int data_size)
{
    TiertexSeqFrameBuffer *seq_buffer;

    if (buffer_num >= SEQ_NUM_FRAME_BUFFERS)
        return AVERROR_INVALIDDATA;

    seq_buffer = &seq->frame_buffers[buffer_num];
    if (seq_buffer->fill_size + data_size > seq_buffer->data_size || data_size <= 0)
        return AVERROR_INVALIDDATA;

    url_fseek(pb, seq->current_frame_offs + data_offs, SEEK_SET);
    if (get_buffer(pb, seq_buffer->data + seq_buffer->fill_size, data_size) != data_size)
        return AVERROR(EIO);

    seq_buffer->fill_size += data_size;
    return 0;
}

static int seq_parse_frame_data(SeqDemuxContext *seq, ByteIOContext *pb)
{
    unsigned int offset_table[4], buffer_num[4];
    TiertexSeqFrameBuffer *seq_buffer;
    int i, e, err;

    seq->current_frame_offs += SEQ_FRAME_SIZE;
    url_fseek(pb, seq->current_frame_offs, SEEK_SET);

    /* sound data */
    seq->current_audio_data_offs = get_le16(pb);
    if (seq->current_audio_data_offs) {
        seq->current_audio_data_size = SEQ_AUDIO_BUFFER_SIZE * 2;
    } else {
        seq->current_audio_data_size = 0;
    }

    /* palette data */
    seq->current_pal_data_offs = get_le16(pb);
    if (seq->current_pal_data_offs) {
        seq->current_pal_data_size = 768;
    } else {
        seq->current_pal_data_size = 0;
    }

    /* video data */
    for (i = 0; i < 4; i++)
        buffer_num[i] = get_byte(pb);

    for (i = 0; i < 4; i++)
        offset_table[i] = get_le16(pb);

    for (i = 0; i < 3; i++) {
        if (offset_table[i]) {
            for (e = i + 1; e < 3 && offset_table[e] == 0; e++);
            err = seq_fill_buffer(seq, pb, buffer_num[1 + i],
              offset_table[i],
              offset_table[e] - offset_table[i]);
            if (err)
                return err;
        }
    }

    if (buffer_num[0] != 255) {
        if (buffer_num[0] >= SEQ_NUM_FRAME_BUFFERS)
            return AVERROR_INVALIDDATA;

        seq_buffer = &seq->frame_buffers[buffer_num[0]];
        seq->current_video_data_size = seq_buffer->fill_size;
        seq->current_video_data_ptr  = seq_buffer->data;
        seq_buffer->fill_size = 0;
    } else {
        seq->current_video_data_size = 0;
        seq->current_video_data_ptr  = 0;
    }

    return 0;
}

static int seq_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    int i, rc;
    SeqDemuxContext *seq = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVStream *st;

    /* init internal buffers */
    rc = seq_init_frame_buffers(seq, pb);
    if (rc)
        return rc;

    seq->current_frame_offs = 0;

    /* preload (no audio data, just buffer operations related data) */
    for (i = 1; i <= 100; i++) {
        rc = seq_parse_frame_data(seq, pb);
        if (rc)
            return rc;
    }

    seq->current_frame_pts = 0;

    seq->audio_buffer_full = 0;

    /* initialize the video decoder stream */
    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

    av_set_pts_info(st, 32, 1, SEQ_FRAME_RATE);
    seq->video_stream_index = st->index;
    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id = CODEC_ID_TIERTEXSEQVIDEO;
    st->codec->codec_tag = 0;  /* no fourcc */
    st->codec->width = SEQ_FRAME_W;
    st->codec->height = SEQ_FRAME_H;

    /* initialize the audio decoder stream */
    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

    av_set_pts_info(st, 32, 1, SEQ_SAMPLE_RATE);
    seq->audio_stream_index = st->index;
    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id = CODEC_ID_PCM_S16BE;
    st->codec->codec_tag = 0;  /* no tag */
    st->codec->channels = 1;
    st->codec->sample_rate = SEQ_SAMPLE_RATE;
    st->codec->bits_per_coded_sample = 16;
    st->codec->bit_rate = st->codec->sample_rate * st->codec->bits_per_coded_sample * st->codec->channels;
    st->codec->block_align = st->codec->channels * st->codec->bits_per_coded_sample;

    return 0;
}

static int seq_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int rc;
    SeqDemuxContext *seq = s->priv_data;
    ByteIOContext *pb = s->pb;

    if (!seq->audio_buffer_full) {
        rc = seq_parse_frame_data(seq, pb);
        if (rc)
            return rc;

        /* video packet */
        if (seq->current_pal_data_size + seq->current_video_data_size != 0) {
            if (av_new_packet(pkt, 1 + seq->current_pal_data_size + seq->current_video_data_size))
                return AVERROR(ENOMEM);

            pkt->data[0] = 0;
            if (seq->current_pal_data_size) {
                pkt->data[0] |= 1;
                url_fseek(pb, seq->current_frame_offs + seq->current_pal_data_offs, SEEK_SET);
                if (get_buffer(pb, &pkt->data[1], seq->current_pal_data_size) != seq->current_pal_data_size)
                    return AVERROR(EIO);
            }
            if (seq->current_video_data_size) {
                pkt->data[0] |= 2;
                memcpy(&pkt->data[1 + seq->current_pal_data_size],
                  seq->current_video_data_ptr,
                  seq->current_video_data_size);
            }
            pkt->stream_index = seq->video_stream_index;
            pkt->pts = seq->current_frame_pts;

            /* sound buffer will be processed on next read_packet() call */
            seq->audio_buffer_full = 1;
            return 0;
       }
    }

    /* audio packet */
    if (seq->current_audio_data_offs == 0) /* end of data reached */
        return AVERROR(EIO);

    url_fseek(pb, seq->current_frame_offs + seq->current_audio_data_offs, SEEK_SET);
    rc = av_get_packet(pb, pkt, seq->current_audio_data_size);
    if (rc < 0)
        return rc;

    pkt->stream_index = seq->audio_stream_index;
    seq->current_frame_pts++;

    seq->audio_buffer_full = 0;
    return 0;
}

static int seq_read_close(AVFormatContext *s)
{
    int i;
    SeqDemuxContext *seq = s->priv_data;

    for (i = 0; i < SEQ_NUM_FRAME_BUFFERS; i++)
        av_free(seq->frame_buffers[i].data);

    return 0;
}

AVInputFormat tiertexseq_demuxer = {
    "tiertexseq",
    NULL_IF_CONFIG_SMALL("Tiertex Limited SEQ format"),
    sizeof(SeqDemuxContext),
    seq_probe,
    seq_read_header,
    seq_read_packet,
    seq_read_close,
};
