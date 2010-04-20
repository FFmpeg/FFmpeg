/*
 * Sierra VMD Format Demuxer
 * Copyright (c) 2004 The ffmpeg Project
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
 * Sierra VMD file demuxer
 * by Vladimir "VAG" Gneushev (vagsoft at mail.ru)
 * for more information on the Sierra VMD file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"

#define VMD_HEADER_SIZE 0x0330
#define BYTES_PER_FRAME_RECORD 16

typedef struct {
  int stream_index;
  int64_t frame_offset;
  unsigned int frame_size;
  int64_t pts;
  int keyframe;
  unsigned char frame_record[BYTES_PER_FRAME_RECORD];
} vmd_frame;

typedef struct VmdDemuxContext {
    int video_stream_index;
    int audio_stream_index;

    unsigned int frame_count;
    unsigned int frames_per_block;
    vmd_frame *frame_table;
    unsigned int current_frame;
    int is_indeo3;

    int sample_rate;
    int64_t audio_sample_counter;
    int skiphdr;

    unsigned char vmd_header[VMD_HEADER_SIZE];
} VmdDemuxContext;

static int vmd_probe(AVProbeData *p)
{
    int w, h;
    if (p->buf_size < 16)
        return 0;
    /* check if the first 2 bytes of the file contain the appropriate size
     * of a VMD header chunk */
    if (AV_RL16(&p->buf[0]) != VMD_HEADER_SIZE - 2)
        return 0;
    w = AV_RL16(&p->buf[12]);
    h = AV_RL16(&p->buf[14]);
    if (!w || w > 2048 || !h || h > 2048)
        return 0;

    /* only return half certainty since this check is a bit sketchy */
    return AVPROBE_SCORE_MAX / 2;
}

static int vmd_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    VmdDemuxContext *vmd = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVStream *st = NULL, *vst;
    unsigned int toc_offset;
    unsigned char *raw_frame_table;
    int raw_frame_table_size;
    int64_t current_offset;
    int i, j;
    unsigned int total_frames;
    int64_t current_audio_pts = 0;
    unsigned char chunk[BYTES_PER_FRAME_RECORD];
    int num, den;
    int sound_buffers;

    /* fetch the main header, including the 2 header length bytes */
    url_fseek(pb, 0, SEEK_SET);
    if (get_buffer(pb, vmd->vmd_header, VMD_HEADER_SIZE) != VMD_HEADER_SIZE)
        return AVERROR(EIO);

    if(vmd->vmd_header[16] == 'i' && vmd->vmd_header[17] == 'v' && vmd->vmd_header[18] == '3')
        vmd->is_indeo3 = 1;
    else
        vmd->is_indeo3 = 0;
    /* start up the decoders */
    vst = av_new_stream(s, 0);
    if (!vst)
        return AVERROR(ENOMEM);
    av_set_pts_info(vst, 33, 1, 10);
    vmd->video_stream_index = vst->index;
    vst->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    vst->codec->codec_id = vmd->is_indeo3 ? CODEC_ID_INDEO3 : CODEC_ID_VMDVIDEO;
    vst->codec->codec_tag = 0;  /* no fourcc */
    vst->codec->width = AV_RL16(&vmd->vmd_header[12]);
    vst->codec->height = AV_RL16(&vmd->vmd_header[14]);
    if(vmd->is_indeo3 && vst->codec->width > 320){
        vst->codec->width >>= 1;
        vst->codec->height >>= 1;
    }
    vst->codec->extradata_size = VMD_HEADER_SIZE;
    vst->codec->extradata = av_mallocz(VMD_HEADER_SIZE + FF_INPUT_BUFFER_PADDING_SIZE);
    memcpy(vst->codec->extradata, vmd->vmd_header, VMD_HEADER_SIZE);

    /* if sample rate is 0, assume no audio */
    vmd->sample_rate = AV_RL16(&vmd->vmd_header[804]);
    if (vmd->sample_rate) {
        st = av_new_stream(s, 0);
        if (!st)
            return AVERROR(ENOMEM);
        vmd->audio_stream_index = st->index;
        st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codec->codec_id = CODEC_ID_VMDAUDIO;
        st->codec->codec_tag = 0;  /* no fourcc */
        st->codec->channels = (vmd->vmd_header[811] & 0x80) ? 2 : 1;
        st->codec->sample_rate = vmd->sample_rate;
        st->codec->block_align = AV_RL16(&vmd->vmd_header[806]);
        if (st->codec->block_align & 0x8000) {
            st->codec->bits_per_coded_sample = 16;
            st->codec->block_align = -(st->codec->block_align - 0x10000);
        } else {
            st->codec->bits_per_coded_sample = 8;
        }
        st->codec->bit_rate = st->codec->sample_rate *
            st->codec->bits_per_coded_sample * st->codec->channels;

        /* calculate pts */
        num = st->codec->block_align;
        den = st->codec->sample_rate * st->codec->channels;
        av_reduce(&den, &num, den, num, (1UL<<31)-1);
        av_set_pts_info(vst, 33, num, den);
        av_set_pts_info(st, 33, num, den);
    }

    toc_offset = AV_RL32(&vmd->vmd_header[812]);
    vmd->frame_count = AV_RL16(&vmd->vmd_header[6]);
    vmd->frames_per_block = AV_RL16(&vmd->vmd_header[18]);
    url_fseek(pb, toc_offset, SEEK_SET);

    raw_frame_table = NULL;
    vmd->frame_table = NULL;
    sound_buffers = AV_RL16(&vmd->vmd_header[808]);
    raw_frame_table_size = vmd->frame_count * 6;
    if(vmd->frame_count * vmd->frames_per_block >= UINT_MAX / sizeof(vmd_frame) - sound_buffers){
        av_log(s, AV_LOG_ERROR, "vmd->frame_count * vmd->frames_per_block too large\n");
        return -1;
    }
    raw_frame_table = av_malloc(raw_frame_table_size);
    vmd->frame_table = av_malloc((vmd->frame_count * vmd->frames_per_block + sound_buffers) * sizeof(vmd_frame));
    if (!raw_frame_table || !vmd->frame_table) {
        av_free(raw_frame_table);
        av_free(vmd->frame_table);
        return AVERROR(ENOMEM);
    }
    if (get_buffer(pb, raw_frame_table, raw_frame_table_size) !=
        raw_frame_table_size) {
        av_free(raw_frame_table);
        av_free(vmd->frame_table);
        return AVERROR(EIO);
    }

    total_frames = 0;
    for (i = 0; i < vmd->frame_count; i++) {

        current_offset = AV_RL32(&raw_frame_table[6 * i + 2]);

        /* handle each entry in index block */
        for (j = 0; j < vmd->frames_per_block; j++) {
            int type;
            uint32_t size;

            get_buffer(pb, chunk, BYTES_PER_FRAME_RECORD);
            type = chunk[0];
            size = AV_RL32(&chunk[2]);
            if(!size && type != 1)
                continue;
            switch(type) {
            case 1: /* Audio Chunk */
                if (!st) break;
                /* first audio chunk contains several audio buffers */
                vmd->frame_table[total_frames].frame_offset = current_offset;
                vmd->frame_table[total_frames].stream_index = vmd->audio_stream_index;
                vmd->frame_table[total_frames].frame_size = size;
                memcpy(vmd->frame_table[total_frames].frame_record, chunk, BYTES_PER_FRAME_RECORD);
                vmd->frame_table[total_frames].pts = current_audio_pts;
                total_frames++;
                if(!current_audio_pts)
                    current_audio_pts += sound_buffers;
                else
                    current_audio_pts++;
                break;
            case 2: /* Video Chunk */
                vmd->frame_table[total_frames].frame_offset = current_offset;
                vmd->frame_table[total_frames].stream_index = vmd->video_stream_index;
                vmd->frame_table[total_frames].frame_size = size;
                memcpy(vmd->frame_table[total_frames].frame_record, chunk, BYTES_PER_FRAME_RECORD);
                vmd->frame_table[total_frames].pts = i;
                total_frames++;
                break;
            }
            current_offset += size;
        }
    }

    av_free(raw_frame_table);

    vmd->current_frame = 0;
    vmd->frame_count = total_frames;

    return 0;
}

static int vmd_read_packet(AVFormatContext *s,
                           AVPacket *pkt)
{
    VmdDemuxContext *vmd = s->priv_data;
    ByteIOContext *pb = s->pb;
    int ret = 0;
    vmd_frame *frame;

    if (vmd->current_frame >= vmd->frame_count)
        return AVERROR(EIO);

    frame = &vmd->frame_table[vmd->current_frame];
    /* position the stream (will probably be there already) */
    url_fseek(pb, frame->frame_offset, SEEK_SET);

    if (av_new_packet(pkt, frame->frame_size + BYTES_PER_FRAME_RECORD))
        return AVERROR(ENOMEM);
    pkt->pos= url_ftell(pb);
    memcpy(pkt->data, frame->frame_record, BYTES_PER_FRAME_RECORD);
    if(vmd->is_indeo3)
        ret = get_buffer(pb, pkt->data, frame->frame_size);
    else
        ret = get_buffer(pb, pkt->data + BYTES_PER_FRAME_RECORD,
            frame->frame_size);

    if (ret != frame->frame_size) {
        av_free_packet(pkt);
        ret = AVERROR(EIO);
    }
    pkt->stream_index = frame->stream_index;
    pkt->pts = frame->pts;
    av_log(s, AV_LOG_DEBUG, " dispatching %s frame with %d bytes and pts %"PRId64"\n",
            (frame->frame_record[0] == 0x02) ? "video" : "audio",
            frame->frame_size + BYTES_PER_FRAME_RECORD,
            pkt->pts);

    vmd->current_frame++;

    return ret;
}

static int vmd_read_close(AVFormatContext *s)
{
    VmdDemuxContext *vmd = s->priv_data;

    av_free(vmd->frame_table);

    return 0;
}

AVInputFormat vmd_demuxer = {
    "vmd",
    NULL_IF_CONFIG_SMALL("Sierra VMD format"),
    sizeof(VmdDemuxContext),
    vmd_probe,
    vmd_read_header,
    vmd_read_packet,
    vmd_read_close,
};
