/*
 * Sierra VMD Format Demuxer
 * Copyright (c) 2004 The ffmpeg Project
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * @file sierravmd.c
 * Sierra VMD file demuxer
 * by Vladimir "VAG" Gneushev (vagsoft at mail.ru)
 * for more information on the Sierra VMD file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 */

#include "avformat.h"

#define VMD_HEADER_SIZE 0x0330
#define BYTES_PER_FRAME_RECORD 16

typedef struct {
  int stream_index;
  offset_t frame_offset;
  unsigned int frame_size;
  int64_t pts;
  int keyframe;
  unsigned char frame_record[BYTES_PER_FRAME_RECORD];
} vmd_frame_t;

typedef struct VmdDemuxContext {
    int video_stream_index;
    int audio_stream_index;

    unsigned int audio_type;
    unsigned int audio_samplerate;
    unsigned int audio_bits;
    unsigned int audio_channels;

    unsigned int frame_count;
    vmd_frame_t *frame_table;
    unsigned int current_frame;

    int sample_rate;
    int64_t audio_sample_counter;
    int audio_frame_divisor;
    int audio_block_align;

    unsigned char vmd_header[VMD_HEADER_SIZE];
} VmdDemuxContext;

static int vmd_probe(AVProbeData *p)
{
    if (p->buf_size < 2)
        return 0;

    /* check if the first 2 bytes of the file contain the appropriate size
     * of a VMD header chunk */
    if (LE_16(&p->buf[0]) != VMD_HEADER_SIZE - 2)
        return 0;

    /* only return half certainty since this check is a bit sketchy */
    return AVPROBE_SCORE_MAX / 2;
}

/* This is a support function to determine the duration, in sample
 * frames, of a particular audio chunk, taking into account silent
 * encodings. */
static int vmd_calculate_audio_duration(unsigned char *audio_chunk,
    int audio_chunk_size, int block_align)
{
    unsigned char *p = audio_chunk + 16;
    unsigned char *p_end = audio_chunk + audio_chunk_size;
    int total_samples = 0;
    unsigned int sound_flags;

    if (audio_chunk_size < 16)
        return 0;

    sound_flags = LE_32(p);
    p += 4;
    while (p < p_end) {
        total_samples += block_align;
        if ((sound_flags & 0x01) == 0)
            p += block_align;
        sound_flags >>= 1;
    }

    return total_samples;
}

static int vmd_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    VmdDemuxContext *vmd = (VmdDemuxContext *)s->priv_data;
    ByteIOContext *pb = &s->pb;
    AVStream *st;
    unsigned int toc_offset;
    unsigned char *raw_frame_table;
    int raw_frame_table_size;
    unsigned char *current_frame_record;
    offset_t current_offset;
    int i;
    unsigned int total_frames;
    int64_t video_pts_inc;
    int64_t current_video_pts = 0;

    /* fetch the main header, including the 2 header length bytes */
    url_fseek(pb, 0, SEEK_SET);
    if (get_buffer(pb, vmd->vmd_header, VMD_HEADER_SIZE) != VMD_HEADER_SIZE)
        return AVERROR_IO;

    vmd->audio_sample_counter = 0;
    vmd->audio_frame_divisor = 1;
    vmd->audio_block_align = 1;

    /* start up the decoders */
    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR_NOMEM;
    av_set_pts_info(st, 33, 1, 90000);
    vmd->video_stream_index = st->index;
    st->codec.codec_type = CODEC_TYPE_VIDEO;
    st->codec.codec_id = CODEC_ID_VMDVIDEO;
    st->codec.codec_tag = 0;  /* no fourcc */
    st->codec.width = LE_16(&vmd->vmd_header[12]);
    st->codec.height = LE_16(&vmd->vmd_header[14]);
    st->codec.extradata_size = VMD_HEADER_SIZE;
    st->codec.extradata = av_malloc(VMD_HEADER_SIZE);
    memcpy(st->codec.extradata, vmd->vmd_header, VMD_HEADER_SIZE);

    /* if sample rate is 0, assume no audio */
    vmd->sample_rate = LE_16(&vmd->vmd_header[804]);
    if (vmd->sample_rate) {
        st = av_new_stream(s, 0);
        if (!st)
            return AVERROR_NOMEM;
        av_set_pts_info(st, 33, 1, 90000);
        vmd->audio_stream_index = st->index;
        st->codec.codec_type = CODEC_TYPE_AUDIO;
        st->codec.codec_id = CODEC_ID_VMDAUDIO;
        st->codec.codec_tag = 0;  /* no codec tag */
        st->codec.channels = (vmd->vmd_header[811] & 0x80) ? 2 : 1;
        st->codec.sample_rate = vmd->sample_rate;
        st->codec.block_align = vmd->audio_block_align = 
            LE_16(&vmd->vmd_header[806]);
        if (st->codec.block_align & 0x8000) {
            st->codec.bits_per_sample = 16;
            st->codec.block_align = -(st->codec.block_align - 0x10000);
        } else
            st->codec.bits_per_sample = 16;
//            st->codec.bits_per_sample = 8;
        st->codec.bit_rate = st->codec.sample_rate * 
            st->codec.bits_per_sample * st->codec.channels;

        /* for calculating pts */
        vmd->audio_frame_divisor = st->codec.bits_per_sample / 8 / 
            st->codec.channels;

        video_pts_inc = 90000;
        video_pts_inc *= st->codec.block_align;
        video_pts_inc /= st->codec.sample_rate;
    } else {
        /* if no audio, assume 10 frames/second */
        video_pts_inc = 90000 / 10;
    }

    /* skip over the offset table and load the table of contents; don't 
     * care about the offset table since demuxer will calculate those 
     * independently */
    toc_offset = LE_32(&vmd->vmd_header[812]);
    vmd->frame_count = LE_16(&vmd->vmd_header[6]);
    url_fseek(pb, toc_offset + vmd->frame_count * 6, SEEK_SET);

    /* each on-disk VMD frame has an audio part and a video part; demuxer
     * accounts them separately */
    vmd->frame_count *= 2;
    raw_frame_table = NULL;
    vmd->frame_table = NULL;
    raw_frame_table_size = vmd->frame_count * BYTES_PER_FRAME_RECORD;
    raw_frame_table = av_malloc(raw_frame_table_size);
    vmd->frame_table = av_malloc(vmd->frame_count * sizeof(vmd_frame_t));
    if (!raw_frame_table || !vmd->frame_table) {
        av_free(raw_frame_table);
        av_free(vmd->frame_table);
        return AVERROR_NOMEM;
    }
    if (get_buffer(pb, raw_frame_table, raw_frame_table_size) != 
        raw_frame_table_size) {
        av_free(raw_frame_table);
        av_free(vmd->frame_table);
        return AVERROR_IO;
    }

    current_offset = LE_32(&vmd->vmd_header[20]);
    current_frame_record = raw_frame_table;
    total_frames = vmd->frame_count;
    i = 0;
    while (total_frames--) {

        /* if the frame size is 0, do not count the frame and bring the
         * total frame count down */
        vmd->frame_table[i].frame_size = LE_32(&current_frame_record[2]);

        /* this logic is present so that 0-length audio chunks are not
         * accounted */
        if (!vmd->frame_table[i].frame_size) {
            vmd->frame_count--;  /* one less frame to count */
            current_frame_record += BYTES_PER_FRAME_RECORD;
            continue;
        }

        if (current_frame_record[0] == 0x02)
            vmd->frame_table[i].stream_index = vmd->video_stream_index;
        else
            vmd->frame_table[i].stream_index = vmd->audio_stream_index;
        vmd->frame_table[i].frame_offset = current_offset;
        current_offset += vmd->frame_table[i].frame_size;
        memcpy(vmd->frame_table[i].frame_record, current_frame_record,
            BYTES_PER_FRAME_RECORD);

        /* figure out the pts for this frame */
        if (current_frame_record[0] == 0x02) {
            vmd->frame_table[i].pts = current_video_pts;
            current_video_pts += video_pts_inc;
        } else if (current_frame_record[0] == 0x01) {
            /* figure out the pts during the dispatch phase */
            vmd->frame_table[i].pts = 0;
        }

        current_frame_record += BYTES_PER_FRAME_RECORD;
        i++;
    }

    av_free(raw_frame_table);

    vmd->current_frame = 0;

    return 0;
}

static int vmd_read_packet(AVFormatContext *s,
                           AVPacket *pkt)
{
    VmdDemuxContext *vmd = (VmdDemuxContext *)s->priv_data;
    ByteIOContext *pb = &s->pb;
    int ret = 0;
    vmd_frame_t *frame;

    if (vmd->current_frame >= vmd->frame_count)
        return AVERROR_IO;

    frame = &vmd->frame_table[vmd->current_frame];
    /* position the stream (will probably be there already) */
    url_fseek(pb, frame->frame_offset, SEEK_SET);

    if (av_new_packet(pkt, frame->frame_size + BYTES_PER_FRAME_RECORD))
        return AVERROR_NOMEM;
    memcpy(pkt->data, frame->frame_record, BYTES_PER_FRAME_RECORD);
    ret = get_buffer(pb, pkt->data + BYTES_PER_FRAME_RECORD, 
        frame->frame_size);

    if (ret != frame->frame_size) {
        av_free_packet(pkt);
        ret = AVERROR_IO;
    }
    pkt->stream_index = frame->stream_index;
    if (frame->frame_record[0] == 0x02)
        pkt->pts = frame->pts;
    else {
        pkt->pts = vmd->audio_sample_counter;
        pkt->pts *= 90000;
        pkt->pts /= vmd->sample_rate;
//        pkt->pts /= vmd->audio_frame_divisor;
        vmd->audio_sample_counter += vmd_calculate_audio_duration(
            pkt->data, pkt->size, vmd->audio_block_align);

    }
printf (" dispatching %s frame with %d bytes and pts %lld (%0.1f sec)\n",
  (frame->frame_record[0] == 0x02) ? "video" : "audio",
  frame->frame_size + BYTES_PER_FRAME_RECORD,
  pkt->pts, (float)(pkt->pts / 90000.0));

    vmd->current_frame++;

    return ret;
}

static int vmd_read_close(AVFormatContext *s)
{
    VmdDemuxContext *vmd = (VmdDemuxContext *)s->priv_data;

    av_free(vmd->frame_table);

    return 0;
}

static AVInputFormat vmd_iformat = {
    "vmd",
    "Sierra VMD format",
    sizeof(VmdDemuxContext),
    vmd_probe,
    vmd_read_header,
    vmd_read_packet,
    vmd_read_close,
};

int vmd_init(void)
{
    av_register_input_format(&vmd_iformat);
    return 0;
}
