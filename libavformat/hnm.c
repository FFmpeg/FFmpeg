/*
 * Cryo Interactive Entertainment HNM4 demuxer
 *
 * Copyright (c) 2012 David Kment
 *
 * This file is part of FFmpeg .
 *
 * FFmpeg  is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg  is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg ; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <inttypes.h>

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "demux.h"
#include "internal.h"

#define HNM4_TAG MKTAG('H', 'N', 'M', '4')

#define HNM4_SAMPLE_RATE 22050
#define HNM4_FRAME_FPS 24

#define HNM4_CHUNK_ID_PL 19536
#define HNM4_CHUNK_ID_IZ 23113
#define HNM4_CHUNK_ID_IU 21833
#define HNM4_CHUNK_ID_SD 17491

typedef struct Hnm4DemuxContext {
    uint32_t frames;
    uint32_t currentframe;
    uint32_t superchunk_remaining;
} Hnm4DemuxContext;

static int hnm_probe(const AVProbeData *p)
{
    if (p->buf_size < 4)
        return 0;

    // check for HNM4 header.
    // currently only HNM v4/v4A is supported
    if (AV_RL32(&p->buf[0]) == HNM4_TAG)
        return AVPROBE_SCORE_MAX;

    return 0;
}

static int hnm_read_header(AVFormatContext *s)
{
    Hnm4DemuxContext *hnm = s->priv_data;
    AVIOContext *pb = s->pb;
    unsigned width, height;
    AVStream *vst;
    int ret;

    avio_skip(pb, 8);
    width          = avio_rl16(pb);
    height         = avio_rl16(pb);
    avio_rl32(pb); // filesize
    hnm->frames    = avio_rl32(pb);
    avio_skip(pb, 44);

    if (width  < 256 || width  > 640 ||
        height < 150 || height > 480) {
        av_log(s, AV_LOG_ERROR,
               "invalid resolution: %ux%u\n", width, height);
        return AVERROR_INVALIDDATA;
    }

    if (!(vst = avformat_new_stream(s, NULL)))
        return AVERROR(ENOMEM);

    vst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    vst->codecpar->codec_id   = AV_CODEC_ID_HNM4_VIDEO;
    vst->codecpar->codec_tag  = 0;
    vst->codecpar->width      = width;
    vst->codecpar->height     = height;
    if ((ret = ff_alloc_extradata(vst->codecpar, 1)) < 0)
        return ret;

    // TODO: find a better way to detect HNM4A
    vst->codecpar->extradata[0] = width == 640 ? 0x4a : 0x40;

    vst->start_time = 0;

    avpriv_set_pts_info(vst, 33, 1, HNM4_FRAME_FPS);

    return 0;
}

static int hnm_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    Hnm4DemuxContext *hnm = s->priv_data;
    AVIOContext *pb = s->pb;
    int ret = 0;

    uint32_t superchunk_size, chunk_size;
    uint16_t chunk_id;

    if (hnm->currentframe == hnm->frames || pb->eof_reached)
        return AVERROR_EOF;

    if (hnm->superchunk_remaining == 0) {
        /* parse next superchunk */
        superchunk_size = avio_rl24(pb);
        avio_skip(pb, 1);

        hnm->superchunk_remaining = superchunk_size - 4;
    }

    chunk_size = avio_rl24(pb);
    avio_skip(pb, 1);
    chunk_id = avio_rl16(pb);
    avio_skip(pb, 2);

    if (chunk_size > hnm->superchunk_remaining || !chunk_size) {
        av_log(s, AV_LOG_ERROR,
               "invalid chunk size: %"PRIu32", offset: %"PRId64"\n",
               chunk_size, avio_tell(pb));
        avio_skip(pb, hnm->superchunk_remaining - 8);
        hnm->superchunk_remaining = 0;
    }

    switch (chunk_id) {
    case HNM4_CHUNK_ID_PL:
    case HNM4_CHUNK_ID_IZ:
    case HNM4_CHUNK_ID_IU:
        avio_seek(pb, -8, SEEK_CUR);
        ret += av_get_packet(pb, pkt, chunk_size);
        hnm->superchunk_remaining -= chunk_size;
        if (chunk_id == HNM4_CHUNK_ID_IZ || chunk_id == HNM4_CHUNK_ID_IU)
            hnm->currentframe++;
        break;

    case HNM4_CHUNK_ID_SD:
        avio_skip(pb, chunk_size - 8);
        hnm->superchunk_remaining -= chunk_size;
        break;

    default:
        av_log(s, AV_LOG_WARNING, "unknown chunk found: %"PRIu16", offset: %"PRId64"\n",
               chunk_id, avio_tell(pb));
        avio_skip(pb, chunk_size - 8);
        hnm->superchunk_remaining -= chunk_size;
        break;
    }

    return ret;
}

const FFInputFormat ff_hnm_demuxer = {
    .p.name         = "hnm",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Cryo HNM v4"),
    .p.flags        = AVFMT_NO_BYTE_SEEK | AVFMT_NOGENSEARCH | AVFMT_NOBINSEARCH,
    .priv_data_size = sizeof(Hnm4DemuxContext),
    .read_probe     = hnm_probe,
    .read_header    = hnm_read_header,
    .read_packet    = hnm_read_packet,
};
