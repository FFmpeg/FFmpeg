/*
 * Interplay C93 demuxer
 * Copyright (c) 2007 Anssi Hannula <anssi.hannula@gmail.com>
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

#include "avformat.h"
#include "voc.h"
#include "libavutil/intreadwrite.h"

typedef struct {
    uint16_t index;
    uint8_t length;
    uint8_t frames;
} C93BlockRecord;

typedef struct {
    VocDecContext voc;

    C93BlockRecord block_records[512];
    int current_block;

    uint32_t frame_offsets[32];
    int current_frame;
    int next_pkt_is_audio;

    AVStream *audio;
} C93DemuxContext;

static int probe(AVProbeData *p)
{
    int i;
    int index = 1;
    if (p->buf_size < 16)
        return 0;
    for (i = 0; i < 16; i += 4) {
        if (AV_RL16(p->buf + i) != index || !p->buf[i + 2] || !p->buf[i + 3])
            return 0;
        index += p->buf[i + 2];
    }
    return AVPROBE_SCORE_MAX;
}

static int read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    AVStream *video;
    ByteIOContext *pb = s->pb;
    C93DemuxContext *c93 = s->priv_data;
    int i;
    int framecount = 0;

    for (i = 0; i < 512; i++) {
        c93->block_records[i].index = get_le16(pb);
        c93->block_records[i].length = get_byte(pb);
        c93->block_records[i].frames = get_byte(pb);
        if (c93->block_records[i].frames > 32) {
            av_log(s, AV_LOG_ERROR, "too many frames in block\n");
            return AVERROR_INVALIDDATA;
        }
        framecount += c93->block_records[i].frames;
    }

    /* Audio streams are added if audio packets are found */
    s->ctx_flags |= AVFMTCTX_NOHEADER;

    video = av_new_stream(s, 0);
    if (!video)
        return AVERROR(ENOMEM);

    video->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    video->codec->codec_id = CODEC_ID_C93;
    video->codec->width = 320;
    video->codec->height = 192;
    /* 4:3 320x200 with 8 empty lines */
    video->sample_aspect_ratio = (AVRational) { 5, 6 };
    video->time_base = (AVRational) { 2, 25 };
    video->nb_frames = framecount;
    video->duration = framecount;
    video->start_time = 0;

    c93->current_block = 0;
    c93->current_frame = 0;
    c93->next_pkt_is_audio = 0;
    return 0;
}

#define C93_HAS_PALETTE 0x01
#define C93_FIRST_FRAME 0x02

static int read_packet(AVFormatContext *s, AVPacket *pkt)
{
    ByteIOContext *pb = s->pb;
    C93DemuxContext *c93 = s->priv_data;
    C93BlockRecord *br = &c93->block_records[c93->current_block];
    int datasize;
    int ret, i;

    if (c93->next_pkt_is_audio) {
        c93->current_frame++;
        c93->next_pkt_is_audio = 0;
        datasize = get_le16(pb);
        if (datasize > 42) {
            if (!c93->audio) {
                c93->audio = av_new_stream(s, 1);
                if (!c93->audio)
                    return AVERROR(ENOMEM);
                c93->audio->codec->codec_type = AVMEDIA_TYPE_AUDIO;
            }
            url_fskip(pb, 26); /* VOC header */
            ret = voc_get_packet(s, pkt, c93->audio, datasize - 26);
            if (ret > 0) {
                pkt->stream_index = 1;
                pkt->flags |= AV_PKT_FLAG_KEY;
                return ret;
            }
        }
    }
    if (c93->current_frame >= br->frames) {
        if (c93->current_block >= 511 || !br[1].length)
            return AVERROR(EIO);
        br++;
        c93->current_block++;
        c93->current_frame = 0;
    }

    if (c93->current_frame == 0) {
        url_fseek(pb, br->index * 2048, SEEK_SET);
        for (i = 0; i < 32; i++) {
            c93->frame_offsets[i] = get_le32(pb);
        }
    }

    url_fseek(pb,br->index * 2048 +
            c93->frame_offsets[c93->current_frame], SEEK_SET);
    datasize = get_le16(pb); /* video frame size */

    ret = av_new_packet(pkt, datasize + 768 + 1);
    if (ret < 0)
        return ret;
    pkt->data[0] = 0;
    pkt->size = datasize + 1;

    ret = get_buffer(pb, pkt->data + 1, datasize);
    if (ret < datasize) {
        ret = AVERROR(EIO);
        goto fail;
    }

    datasize = get_le16(pb); /* palette size */
    if (datasize) {
        if (datasize != 768) {
            av_log(s, AV_LOG_ERROR, "invalid palette size %u\n", datasize);
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
        pkt->data[0] |= C93_HAS_PALETTE;
        ret = get_buffer(pb, pkt->data + pkt->size, datasize);
        if (ret < datasize) {
            ret = AVERROR(EIO);
            goto fail;
        }
        pkt->size += 768;
    }
    pkt->stream_index = 0;
    c93->next_pkt_is_audio = 1;

    /* only the first frame is guaranteed to not reference previous frames */
    if (c93->current_block == 0 && c93->current_frame == 0) {
        pkt->flags |= AV_PKT_FLAG_KEY;
        pkt->data[0] |= C93_FIRST_FRAME;
    }
    return 0;

    fail:
    av_free_packet(pkt);
    return ret;
}

AVInputFormat c93_demuxer = {
    "c93",
    NULL_IF_CONFIG_SMALL("Interplay C93"),
    sizeof(C93DemuxContext),
    probe,
    read_header,
    read_packet,
};
