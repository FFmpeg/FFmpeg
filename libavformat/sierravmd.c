/*
 * Sierra VMD Format Demuxer
 * Copyright (c) 2004 The FFmpeg project
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

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"
#include "avio_internal.h"

#define VMD_HEADER_SIZE 0x0330
#define BYTES_PER_FRAME_RECORD 16

typedef struct vmd_frame {
  int stream_index;
  unsigned int frame_size;
  int64_t frame_offset;
  int64_t pts;
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

static int vmd_probe(const AVProbeData *p)
{
    int w, h, sample_rate;
    if (p->buf_size < 806)
        return 0;
    /* check if the first 2 bytes of the file contain the appropriate size
     * of a VMD header chunk */
    if (AV_RL16(&p->buf[0]) != VMD_HEADER_SIZE - 2)
        return 0;
    w = AV_RL16(&p->buf[12]);
    h = AV_RL16(&p->buf[14]);
    sample_rate = AV_RL16(&p->buf[804]);
    if ((!w || w > 2048 || !h || h > 2048) &&
        sample_rate != 22050)
        return 0;

    /* only return half certainty since this check is a bit sketchy */
    return AVPROBE_SCORE_EXTENSION;
}

static int vmd_read_header(AVFormatContext *s)
{
    VmdDemuxContext *vmd = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st = NULL, *vst = NULL;
    unsigned int toc_offset;
    unsigned char *raw_frame_table;
    int raw_frame_table_size;
    int64_t current_offset;
    int i, j, ret;
    int width, height;
    unsigned int total_frames;
    int64_t current_audio_pts = 0;
    unsigned char chunk[BYTES_PER_FRAME_RECORD];
    int num, den;
    int sound_buffers;

    /* fetch the main header, including the 2 header length bytes */
    avio_seek(pb, 0, SEEK_SET);
    if (avio_read(pb, vmd->vmd_header, VMD_HEADER_SIZE) != VMD_HEADER_SIZE)
        return AVERROR(EIO);

    width = AV_RL16(&vmd->vmd_header[12]);
    height = AV_RL16(&vmd->vmd_header[14]);
    if (width && height) {
        if(vmd->vmd_header[24] == 'i' && vmd->vmd_header[25] == 'v' && vmd->vmd_header[26] == '3') {
            vmd->is_indeo3 = 1;
        } else {
            vmd->is_indeo3 = 0;
        }
        /* start up the decoders */
        vst = avformat_new_stream(s, NULL);
        if (!vst)
            return AVERROR(ENOMEM);
        avpriv_set_pts_info(vst, 33, 1, 10);
        vmd->video_stream_index = vst->index;
        vst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        vst->codecpar->codec_id = vmd->is_indeo3 ? AV_CODEC_ID_INDEO3 : AV_CODEC_ID_VMDVIDEO;
        vst->codecpar->codec_tag = 0;  /* no fourcc */
        vst->codecpar->width = width;
        vst->codecpar->height = height;
        if(vmd->is_indeo3 && vst->codecpar->width > 320){
            vst->codecpar->width >>= 1;
            vst->codecpar->height >>= 1;
        }
        if ((ret = ff_alloc_extradata(vst->codecpar, VMD_HEADER_SIZE)) < 0)
            return ret;
        memcpy(vst->codecpar->extradata, vmd->vmd_header, VMD_HEADER_SIZE);
    }

    /* if sample rate is 0, assume no audio */
    vmd->sample_rate = AV_RL16(&vmd->vmd_header[804]);
    if (vmd->sample_rate) {
        int channels;
        st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR(ENOMEM);
        vmd->audio_stream_index = st->index;
        st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codecpar->codec_id   = AV_CODEC_ID_VMDAUDIO;
        st->codecpar->codec_tag  = 0;  /* no fourcc */
        st->codecpar->sample_rate = vmd->sample_rate;
        st->codecpar->block_align = AV_RL16(&vmd->vmd_header[806]);
        if (st->codecpar->block_align & 0x8000) {
            st->codecpar->bits_per_coded_sample = 16;
            st->codecpar->block_align = -(st->codecpar->block_align - 0x10000);
        } else {
            st->codecpar->bits_per_coded_sample = 8;
        }
        if (vmd->vmd_header[811] & 0x80) {
            channels = 2;
        } else if (vmd->vmd_header[811] & 0x2) {
            /* Shivers 2 stereo audio */
            /* Frame length is for 1 channel */
            channels        = 2;
            st->codecpar->block_align = st->codecpar->block_align << 1;
        } else {
            channels = 1;
        }
        av_channel_layout_default(&st->codecpar->ch_layout, channels);
        st->codecpar->bit_rate = st->codecpar->sample_rate *
            st->codecpar->bits_per_coded_sample * channels;

        /* calculate pts */
        num = st->codecpar->block_align;
        den = st->codecpar->sample_rate * channels;
        av_reduce(&num, &den, num, den, (1UL<<31)-1);
        if (vst)
            avpriv_set_pts_info(vst, 33, num, den);
        avpriv_set_pts_info(st, 33, num, den);
    }
    if (!s->nb_streams)
        return AVERROR_INVALIDDATA;

    toc_offset = AV_RL32(&vmd->vmd_header[812]);
    vmd->frame_count = AV_RL16(&vmd->vmd_header[6]);
    vmd->frames_per_block = AV_RL16(&vmd->vmd_header[18]);
    avio_seek(pb, toc_offset, SEEK_SET);

    raw_frame_table = NULL;
    vmd->frame_table = NULL;
    sound_buffers = AV_RL16(&vmd->vmd_header[808]);
    raw_frame_table_size = vmd->frame_count * 6;
    raw_frame_table = av_malloc(raw_frame_table_size);
    vmd->frame_table = av_malloc_array(vmd->frame_count * vmd->frames_per_block + sound_buffers, sizeof(vmd_frame));
    if (!raw_frame_table || !vmd->frame_table) {
        ret = AVERROR(ENOMEM);
        goto error;
    }
    if (avio_read(pb, raw_frame_table, raw_frame_table_size) !=
        raw_frame_table_size) {
        ret = AVERROR(EIO);
        goto error;
    }

    total_frames = 0;
    for (i = 0; i < vmd->frame_count; i++) {

        current_offset = AV_RL32(&raw_frame_table[6 * i + 2]);

        /* handle each entry in index block */
        for (j = 0; j < vmd->frames_per_block; j++) {
            int type;
            uint32_t size;

            if ((ret = avio_read(pb, chunk, BYTES_PER_FRAME_RECORD)) != BYTES_PER_FRAME_RECORD) {
                av_log(s, AV_LOG_ERROR, "Failed to read frame record\n");
                if (ret >= 0)
                    ret = AVERROR_INVALIDDATA;
                goto error;
            }
            type = chunk[0];
            size = AV_RL32(&chunk[2]);
            if (size > INT_MAX / 2) {
                av_log(s, AV_LOG_ERROR, "Invalid frame size\n");
                ret = AVERROR_INVALIDDATA;
                goto error;
            }
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
                    current_audio_pts += sound_buffers - 1;
                else
                    current_audio_pts++;
                break;
            case 2: /* Video Chunk */
                if (!vst)
                    break;
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


    vmd->current_frame = 0;
    vmd->frame_count = total_frames;

    ret = 0;
error:
    av_freep(&raw_frame_table);
    return ret;
}

static int vmd_read_packet(AVFormatContext *s,
                           AVPacket *pkt)
{
    VmdDemuxContext *vmd = s->priv_data;
    AVIOContext *pb = s->pb;
    int ret = 0;
    vmd_frame *frame;

    if (vmd->current_frame >= vmd->frame_count)
        return AVERROR_EOF;

    frame = &vmd->frame_table[vmd->current_frame];
    /* position the stream (will probably be there already) */
    avio_seek(pb, frame->frame_offset, SEEK_SET);

    if(ffio_limit(pb, frame->frame_size) != frame->frame_size)
        return AVERROR(EIO);
    ret = av_new_packet(pkt, frame->frame_size + BYTES_PER_FRAME_RECORD);
    if (ret < 0)
        return ret;
    pkt->pos= avio_tell(pb);
    memcpy(pkt->data, frame->frame_record, BYTES_PER_FRAME_RECORD);
    if(vmd->is_indeo3 && frame->frame_record[0] == 0x02)
        ret = avio_read(pb, pkt->data, frame->frame_size);
    else
        ret = avio_read(pb, pkt->data + BYTES_PER_FRAME_RECORD,
            frame->frame_size);

    if (ret != frame->frame_size) {
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

    av_freep(&vmd->frame_table);

    return 0;
}

const AVInputFormat ff_vmd_demuxer = {
    .name           = "vmd",
    .long_name      = NULL_IF_CONFIG_SMALL("Sierra VMD"),
    .priv_data_size = sizeof(VmdDemuxContext),
    .flags_internal = FF_FMT_INIT_CLEANUP,
    .read_probe     = vmd_probe,
    .read_header    = vmd_read_header,
    .read_packet    = vmd_read_packet,
    .read_close     = vmd_read_close,
};
