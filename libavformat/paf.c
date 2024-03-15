/*
 * Packed Animation File demuxer
 * Copyright (c) 2012 Paul B Mahol
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

#include "libavutil/channel_layout.h"
#include "libavcodec/paf.h"
#include "avformat.h"
#include "demux.h"
#include "internal.h"

#define MAGIC "Packed Animation File V1.0\n(c) 1992-96 Amazing Studio\x0a\x1a"

typedef struct PAFDemuxContext {
    uint32_t buffer_size;
    uint32_t frame_blks;
    uint32_t nb_frames;
    uint32_t start_offset;
    uint32_t preload_count;
    uint32_t max_video_blks;
    uint32_t max_audio_blks;

    uint32_t current_frame;
    uint32_t current_frame_count;
    uint32_t current_frame_block;

    uint32_t *blocks_count_table;
    uint32_t *frames_offset_table;
    uint32_t *blocks_offset_table;

    uint8_t  *video_frame;
    int video_size;

    uint8_t  *audio_frame;
    uint8_t  *temp_audio_frame;
    int audio_size;

    int got_audio;
} PAFDemuxContext;

static int read_probe(const AVProbeData *p)
{
    if ((p->buf_size >= strlen(MAGIC)) &&
        !memcmp(p->buf, MAGIC, strlen(MAGIC)))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int read_close(AVFormatContext *s)
{
    PAFDemuxContext *p = s->priv_data;

    av_freep(&p->blocks_count_table);
    av_freep(&p->frames_offset_table);
    av_freep(&p->blocks_offset_table);
    av_freep(&p->video_frame);
    av_freep(&p->audio_frame);
    av_freep(&p->temp_audio_frame);

    return 0;
}

static int read_table(AVFormatContext *s, uint32_t *table, uint32_t count)
{
    int i;

    for (i = 0; i < count; i++) {
        if (avio_feof(s->pb))
            return AVERROR_INVALIDDATA;
        table[i] = avio_rl32(s->pb);
    }

    avio_skip(s->pb, 4 * (FFALIGN(count, 512) - count));
    return 0;
}

static int read_header(AVFormatContext *s)
{
    PAFDemuxContext *p  = s->priv_data;
    AVIOContext     *pb = s->pb;
    AVStream        *ast, *vst;
    int frame_ms, ret = 0;

    avio_skip(pb, 132);

    vst = avformat_new_stream(s, 0);
    if (!vst)
        return AVERROR(ENOMEM);

    vst->start_time = 0;
    vst->nb_frames  =
    vst->duration   =
    p->nb_frames    = avio_rl32(pb);
    frame_ms        = avio_rl32(pb);
    if (frame_ms < 1)
        return AVERROR_INVALIDDATA;

    vst->codecpar->width  = avio_rl32(pb);
    vst->codecpar->height = avio_rl32(pb);
    avio_skip(pb, 4);

    vst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    vst->codecpar->codec_tag  = 0;
    vst->codecpar->codec_id   = AV_CODEC_ID_PAF_VIDEO;
    avpriv_set_pts_info(vst, 64, frame_ms, 1000);

    ast = avformat_new_stream(s, 0);
    if (!ast)
        return AVERROR(ENOMEM);

    ast->start_time            = 0;
    ast->codecpar->codec_type     = AVMEDIA_TYPE_AUDIO;
    ast->codecpar->codec_tag      = 0;
    ast->codecpar->codec_id       = AV_CODEC_ID_PAF_AUDIO;
    ast->codecpar->ch_layout      = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
    ast->codecpar->sample_rate    = 22050;
    avpriv_set_pts_info(ast, 64, 1, 22050);

    p->buffer_size    = avio_rl32(pb);
    p->preload_count  = avio_rl32(pb);
    p->frame_blks     = avio_rl32(pb);
    p->start_offset   = avio_rl32(pb);
    p->max_video_blks = avio_rl32(pb);
    p->max_audio_blks = avio_rl32(pb);

    if (avio_feof(pb))
        return AVERROR_INVALIDDATA;

    if (p->buffer_size    < 175  ||
        p->max_audio_blks < 2    ||
        p->max_video_blks < 1    ||
        p->frame_blks     < 1    ||
        p->nb_frames      < 1    ||
        p->preload_count  < 1    ||
        p->buffer_size    > 2048 ||
        p->max_video_blks > 2048 ||
        p->max_audio_blks > 2048 ||
        p->nb_frames      > INT_MAX / sizeof(uint32_t) ||
        p->frame_blks     > INT_MAX / sizeof(uint32_t))
        return AVERROR_INVALIDDATA;

    p->blocks_count_table  = av_malloc_array(p->nb_frames,
                                        sizeof(*p->blocks_count_table));
    p->frames_offset_table = av_malloc_array(p->nb_frames,
                                        sizeof(*p->frames_offset_table));
    p->blocks_offset_table = av_malloc_array(p->frame_blks,
                                        sizeof(*p->blocks_offset_table));

    p->video_size  = p->max_video_blks * p->buffer_size;
    p->video_frame = av_mallocz(p->video_size);

    p->audio_size       = p->max_audio_blks * p->buffer_size;
    p->audio_frame      = av_mallocz(p->audio_size);
    p->temp_audio_frame = av_mallocz(p->audio_size);

    if (!p->blocks_count_table  ||
        !p->frames_offset_table ||
        !p->blocks_offset_table ||
        !p->video_frame         ||
        !p->audio_frame         ||
        !p->temp_audio_frame)
        return AVERROR(ENOMEM);

    avio_seek(pb, p->buffer_size, SEEK_SET);

    ret = read_table(s, p->blocks_count_table,  p->nb_frames);
    if (ret < 0)
        return ret;
    ret = read_table(s, p->frames_offset_table, p->nb_frames);
    if (ret < 0)
        return ret;
    ret = read_table(s, p->blocks_offset_table, p->frame_blks);
    if (ret < 0)
        return ret;

    p->got_audio = 0;
    p->current_frame = 0;
    p->current_frame_block = 0;

    avio_seek(pb, p->start_offset, SEEK_SET);

    return 0;
}

static int read_packet(AVFormatContext *s, AVPacket *pkt)
{
    PAFDemuxContext *p  = s->priv_data;
    AVIOContext     *pb = s->pb;
    uint32_t        count, offset;
    int             size, i, ret;

    if (p->current_frame >= p->nb_frames)
        return AVERROR_EOF;

    if (avio_feof(pb))
        return AVERROR_EOF;

    if (p->got_audio) {
        if ((ret = av_new_packet(pkt, p->audio_size)) < 0)
            return ret;

        memcpy(pkt->data, p->temp_audio_frame, p->audio_size);
        pkt->duration     = PAF_SOUND_SAMPLES * (p->audio_size / PAF_SOUND_FRAME_SIZE);
        pkt->flags       |= AV_PKT_FLAG_KEY;
        pkt->stream_index = 1;
        p->got_audio      = 0;
        return pkt->size;
    }

    count = (p->current_frame == 0) ? p->preload_count
                                    : p->blocks_count_table[p->current_frame - 1];
    for (i = 0; i < count; i++) {
        if (p->current_frame_block >= p->frame_blks)
            return AVERROR_INVALIDDATA;

        offset = p->blocks_offset_table[p->current_frame_block] & ~(1U << 31);
        if (p->blocks_offset_table[p->current_frame_block] & (1U << 31)) {
            if (offset > p->audio_size - p->buffer_size)
                return AVERROR_INVALIDDATA;

            avio_read(pb, p->audio_frame + offset, p->buffer_size);
            if (offset == (p->max_audio_blks - 2) * p->buffer_size) {
                memcpy(p->temp_audio_frame, p->audio_frame, p->audio_size);
                p->got_audio = 1;
            }
        } else {
            if (offset > p->video_size - p->buffer_size)
                return AVERROR_INVALIDDATA;

            avio_read(pb, p->video_frame + offset, p->buffer_size);
        }
        p->current_frame_block++;
    }

    if (p->frames_offset_table[p->current_frame] >= p->video_size)
        return AVERROR_INVALIDDATA;

    size = p->video_size - p->frames_offset_table[p->current_frame];

    if ((ret = av_new_packet(pkt, size)) < 0)
        return ret;

    pkt->stream_index = 0;
    pkt->duration     = 1;
    memcpy(pkt->data, p->video_frame + p->frames_offset_table[p->current_frame], size);
    if (pkt->data[0] & 0x20)
        pkt->flags |= AV_PKT_FLAG_KEY;
    p->current_frame++;

    return pkt->size;
}

const FFInputFormat ff_paf_demuxer = {
    .p.name         = "paf",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Amazing Studio Packed Animation File"),
    .priv_data_size = sizeof(PAFDemuxContext),
    .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
    .read_probe     = read_probe,
    .read_header    = read_header,
    .read_packet    = read_packet,
    .read_close     = read_close,
};
