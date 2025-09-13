/*
 * HXVS/HXVT IP camera format
 *
 * Copyright (c) 2025 Zhao Zhili <quinkblack@foxmail.com>
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

#include "libavutil/intreadwrite.h"

#include "avio_internal.h"
#include "demux.h"
#include "internal.h"

/*
 * Ref
 * https://code.videolan.org/videolan/vlc/-/blob/master/modules/demux/hx.c
 * https://github.com/francescovannini/ipcam26Xconvert/tree/main
 */

/* H.264
 *
 * uint32_t tag;
 * uint32_t width;
 * uint32_t height;
 * uint8_t padding[4];
 */
#define HXVS    MKTAG('H', 'X', 'V', 'S')

/* H.265
 *
 * Same as HXVS.
 */
#define HXVT    MKTAG('H', 'X', 'V', 'T')

/* video frame
 *
 * uint32_t tag;
 * uint32_t bytes
 * uint32_t timestamp;
 * uint32_t flags;
 * ------------------
 * uint8_t data[bytes]
 *
 * Note: each HXVF contains a single NALU or slice, not a frame.
 */
#define HXVF    MKTAG('H', 'X', 'V', 'F')

/* audio frame
 *
 * uint32_t tag;
 * uint32_t bytes
 * uint32_t timestamp;
 * uint32_t flags;
 * ------------------
 * uint8_t data[bytes]
 *
 * Note: The first four bytes of data is fake start code and NALU type,
 * which should be skipped.
 */
#define HXAF    MKTAG('H', 'X', 'A', 'F')

/* RAP frame index
 *
 * uint32_t tag;
 * uint32_t bytes
 * uint32_t duration;
 * uint32_t flags;
 */
#define HXFI    MKTAG('H', 'X', 'F', 'I')

#define HXFI_TABLE_SIZE  200000
#define HXFI_TABLE_COUNT (200000 / 8)

typedef struct HxvsContext {
    int video_index;
    int audio_index;
} HxvsContext;

static int hxvs_probe(const AVProbeData *p)
{
    uint32_t flag = 0;
    uint32_t bytes;

    for (size_t i = 0; i < p->buf_size; ) {
        uint32_t tag = AV_RL32(&p->buf[i]);

        // first four bytes must begin with HXVS/HXVT
        if (i == 0) {
            if (tag != HXVS && tag != HXVT)
                return 0;
            flag |= 1;
            i += 16;
            continue;
        }

        // Got RAP index at the end
        if (tag == HXFI) {
            if (flag == 7)
                return AVPROBE_SCORE_MAX;
            break;
        }

        i += 4;
        if (tag == HXVF || tag == HXAF) {
            bytes = AV_RL32(&p->buf[i]);
            i += 12 + bytes;
            flag |= (tag == HXVF) ? 2 : 4;
            continue;
        }

        return 0;
    }

    // Get audio and video
    if (flag == 7)
        return AVPROBE_SCORE_EXTENSION + 10;
    // Get video only
    if (flag == 3)
        return AVPROBE_SCORE_EXTENSION + 2;

    return 0;
}

static int hxvs_create_video_stream(AVFormatContext *s, enum AVCodecID codec_id)
{
    HxvsContext *ctx = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *vt = avformat_new_stream(s, NULL);
    if (!vt)
        return AVERROR(ENOMEM);

    vt->id = 0;
    vt->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    vt->codecpar->codec_id = codec_id;
    vt->codecpar->width = avio_rl32(pb);
    vt->codecpar->height = avio_rl32(pb);
    avpriv_set_pts_info(vt, 32, 1, 1000);
    ffstream(vt)->need_parsing = AVSTREAM_PARSE_FULL;
    ctx->video_index = vt->index;

    // skip padding
    avio_skip(pb, 4);

    return 0;
}

static int hxvs_create_audio_stream(AVFormatContext *s)
{
    HxvsContext *ctx = s->priv_data;
    AVStream *at = avformat_new_stream(s, NULL);
    if (!at)
        return AVERROR(ENOMEM);

    at->id = 1;
    at->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    at->codecpar->codec_id = AV_CODEC_ID_PCM_ALAW;
    at->codecpar->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
    at->codecpar->sample_rate = 8000;
    avpriv_set_pts_info(at, 32, 1, 1000);
    ctx->audio_index = at->index;

    return 0;
}

static int hxvs_build_index(AVFormatContext *s)
{
    HxvsContext *ctx = s->priv_data;
    AVIOContext *pb = s->pb;

    int64_t size = avio_size(pb);
    if (size < 0)
        return size;
    // Don't return error when HXFI is missing
    int64_t pos = avio_seek(pb, size -(HXFI_TABLE_SIZE + 16), SEEK_SET);
    if (pos < 0)
        return 0;

    uint32_t tag = avio_rl32(pb);
    if (tag != HXFI)
        return 0;
    avio_skip(pb, 4);
    AVStream *st = s->streams[ctx->video_index];
    st->duration = avio_rl32(pb);
    avio_skip(pb, 4);

    FFStream *const sti = ffstream(st);
    uint32_t prev_time;
    for (int i = 0; i < HXFI_TABLE_COUNT; i++) {
        uint32_t offset = avio_rl32(pb);
        // pts = first_frame_pts + time
        uint32_t time = avio_rl32(pb);
        av_log(s, AV_LOG_TRACE, "%s/%d: offset %u, time %u\n",
               av_fourcc2str(HXAF), i, offset, time);
        if (!offset)
            break;

        if (!i) {
            // Get first frame timestamp
            int64_t save_pos = avio_tell(pb);
            pos = avio_seek(pb, offset, SEEK_SET);
            if (pos < 0)
                return pos;
            tag = avio_rl32(pb);
            if (tag != HXVF) {
                av_log(s, AV_LOG_ERROR, "invalid tag %s at pos %u\n",
                       av_fourcc2str(tag), offset);
                return AVERROR_INVALIDDATA;
            }
            avio_skip(pb, 4);
            // save first frame timestamp to stream start_time
            st->start_time = avio_rl32(pb);
            pos = avio_seek(pb, save_pos, SEEK_SET);
            if (pos < 0)
                return pos;
        } else if (time == prev_time) {
            // hxvs put SPS, PPS and slice into separate entries with same timestamp.
            // Only record the first entry.
            continue;
        }
        prev_time = time;
        int ret = ff_add_index_entry(&sti->index_entries,
                                     &sti->nb_index_entries,
                                     &sti->index_entries_allocated_size,
                                     offset, st->start_time + time,
                                     0, 0, AVINDEX_KEYFRAME);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int hxvs_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    uint32_t tag = avio_rl32(pb);
    enum AVCodecID codec_id;

    if (tag == HXVS) {
        codec_id = AV_CODEC_ID_H264;
    } else if (tag == HXVT) {
        codec_id = AV_CODEC_ID_HEVC;
    } else {
        av_log(s, AV_LOG_ERROR, "Unknown tag %s\n", av_fourcc2str(tag));
        return AVERROR_INVALIDDATA;
    }

    int ret = hxvs_create_video_stream(s, codec_id);
    if (ret < 0)
        return ret;

    ret = hxvs_create_audio_stream(s);
    if (ret < 0)
        return ret;

    if (pb->seekable & AVIO_SEEKABLE_NORMAL) {
        int64_t pos = avio_tell(pb);
        if (pos < 0)
            return pos;

        ret = hxvs_build_index(s);
        if (ret < 0)
            return ret;

        pos = avio_seek(pb, pos, SEEK_SET);
        if (pos < 0)
            return ret;
    }

    return 0;
}

static int hxvs_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    HxvsContext *ctx = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t pos = avio_tell(pb);
    uint32_t tag = avio_rl32(pb);
    uint32_t bytes;
    int ret;

    if (avio_feof(pb) || (tag == HXFI))
        return AVERROR_EOF;

    if (tag != HXVF && tag != HXAF)
        return AVERROR_INVALIDDATA;

    bytes = avio_rl32(pb);
    if (bytes < 4)
        return AVERROR_INVALIDDATA;

    uint32_t timestamp = avio_rl32(pb);
    int key_flag = 0;
    int index;
    if (tag == HXVF) {
        if (avio_rl32(pb) == 1)
            key_flag = AV_PKT_FLAG_KEY;
        index = ctx->video_index;
    } else {
        avio_skip(pb, 8);
        index = ctx->audio_index;
        bytes -= 4;
    }

    ret = av_get_packet(pb, pkt, bytes);
    if (ret < 0)
        return ret;
    pkt->pts = timestamp;
    pkt->pos = pos;
    pkt->stream_index = index;
    pkt->flags |= key_flag;

    return 0;
}

const FFInputFormat ff_hxvs_demuxer = {
    .p.name         = "hxvs",
    .p.long_name    = NULL_IF_CONFIG_SMALL("HXVF/HXVS IP camera format"),
    .p.extensions   = "264,265",
    .p.flags        = AVFMT_GENERIC_INDEX,
    .read_probe     = hxvs_probe,
    .read_header    = hxvs_read_header,
    .read_packet    = hxvs_read_packet,
    .priv_data_size = sizeof(HxvsContext),
};
