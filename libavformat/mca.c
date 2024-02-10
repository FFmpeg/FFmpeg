/*
 * MCA demuxer
 * Copyright (c) 2020 Zixing Liu
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
#include "avformat.h"
#include "avio_internal.h"
#include "demux.h"
#include "internal.h"

typedef struct MCADemuxContext {
    uint32_t block_count;
    uint16_t block_size;
    uint32_t current_block;
    uint32_t data_start;
    uint32_t samples_per_block;
} MCADemuxContext;

static int probe(const AVProbeData *p)
{
    if (AV_RL32(p->buf) == MKTAG('M', 'A', 'D', 'P') &&
        AV_RL16(p->buf + 4) <= 0x5)
        return AVPROBE_SCORE_MAX / 3 * 2;
    return 0;
}

static int read_header(AVFormatContext *s)
{
    AVStream *st;
    MCADemuxContext *m = s->priv_data;
    AVCodecParameters *par;
    int64_t file_size = avio_size(s->pb);
    uint16_t version = 0;
    uint32_t header_size, data_size, data_offset, loop_start, loop_end,
        nb_samples, nb_metadata, coef_offset = 0;
    int ch, ret;
    int64_t ret_size;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    par = st->codecpar;
    par->codec_type = AVMEDIA_TYPE_AUDIO;

    // parse file headers
    avio_skip(s->pb, 0x4);      // skip the file magic
    version          = avio_rl16(s->pb);
    avio_skip(s->pb, 0x2);      // padding
    par->ch_layout.nb_channels = avio_r8(s->pb);
    avio_skip(s->pb, 0x1);      // padding
    m->block_size    = avio_rl16(s->pb);
    nb_samples       = avio_rl32(s->pb);
    par->sample_rate = avio_rl32(s->pb);
    loop_start       = avio_rl32(s->pb);
    loop_end         = avio_rl32(s->pb);
    header_size      = avio_rl32(s->pb);
    data_size        = avio_rl32(s->pb);
    avio_skip(s->pb, 0x4);
    nb_metadata      = avio_rl16(s->pb);
    avio_skip(s->pb, 0x2);      // unknown u16 field

    // samples per frame = 14; frame size = 8 (2^3)
    m->samples_per_block = (m->block_size * 14) >> 3;

    if (m->samples_per_block < 1)
        return AVERROR_INVALIDDATA;

    m->block_count = nb_samples / m->samples_per_block;
    st->duration = nb_samples;

    // sanity checks
    if (!par->ch_layout.nb_channels || par->sample_rate <= 0
        || loop_start > loop_end || m->block_count < 1)
        return AVERROR_INVALIDDATA;
    if ((ret = av_dict_set_int(&s->metadata, "loop_start",
                        av_rescale(loop_start, AV_TIME_BASE,
                                   par->sample_rate), 0)) < 0)
        return ret;
    if ((ret = av_dict_set_int(&s->metadata, "loop_end",
                        av_rescale(loop_end, AV_TIME_BASE,
                                   par->sample_rate), 0)) < 0)
        return ret;
    if ((32 + 4 + m->block_size) > (INT_MAX / par->ch_layout.nb_channels) ||
        (32 + 4 + m->block_size) * par->ch_layout.nb_channels > INT_MAX - 8)
        return AVERROR_INVALIDDATA;
    avpriv_set_pts_info(st, 64, 1, par->sample_rate);

    if (version <= 4) {
        // version <= 4 needs to use the file size to calculate the offsets
        if (file_size < 0) {
            return AVERROR(EIO);
        }
        if (file_size - data_size > UINT32_MAX)
            return AVERROR_INVALIDDATA;
        m->data_start = file_size - data_size;
        if (version <= 3) {
            nb_metadata = 0;
            // header_size is not available or incorrect in older versions
            header_size = m->data_start;
        }
    } else if (version == 5) {
        // read data_start location from the header
        if (0x30 * par->ch_layout.nb_channels + 0x4 > header_size)
            return AVERROR_INVALIDDATA;
        data_offset = header_size - 0x30 * par->ch_layout.nb_channels - 0x4;
        if ((ret_size = avio_seek(s->pb, data_offset, SEEK_SET)) < 0)
            return ret_size;
        m->data_start = avio_rl32(s->pb);
        // check if the metadata is reasonable
        if (file_size > 0 && (int64_t)m->data_start + data_size > file_size) {
            // the header is broken beyond repair
            if ((int64_t)header_size + data_size > file_size) {
                av_log(s, AV_LOG_ERROR,
                       "MCA metadata corrupted, unable to determine the data offset.\n");
                return AVERROR_INVALIDDATA;
            }
            // recover the data_start information from the data size
            av_log(s, AV_LOG_WARNING,
                   "Incorrect header size found in metadata, "
                   "header size approximated from the data size\n");
            if (file_size - data_offset > UINT32_MAX)
                return AVERROR_INVALIDDATA;
            m->data_start = file_size - data_size;
        }
    } else {
        avpriv_request_sample(s, "version %d", version);
        return AVERROR_PATCHWELCOME;
    }

    // coefficient alignment = 0x30; metadata size = 0x14
    if (0x30 * par->ch_layout.nb_channels + nb_metadata * 0x14 > header_size)
        return AVERROR_INVALIDDATA;
    coef_offset =
        header_size - 0x30 * par->ch_layout.nb_channels + nb_metadata * 0x14;

    st->start_time = 0;
    par->codec_id = AV_CODEC_ID_ADPCM_THP_LE;

    ret = ff_alloc_extradata(st->codecpar, 32 * par->ch_layout.nb_channels);
    if (ret < 0)
        return ret;

    if ((ret_size = avio_seek(s->pb, coef_offset, SEEK_SET)) < 0)
        return ret_size;
    for (ch = 0; ch < par->ch_layout.nb_channels; ch++) {
        if ((ret = ffio_read_size(s->pb, par->extradata + ch * 32, 32)) < 0)
            return ret;
        // 0x30 (alignment) - 0x20 (actual size, 32) = 0x10 (padding)
        avio_skip(s->pb, 0x10);
    }

    // seek to the beginning of the adpcm data
    // there are some files where the adpcm audio data is not immediately after the header
    if ((ret_size = avio_seek(s->pb, m->data_start, SEEK_SET)) < 0)
        return ret_size;

    return 0;
}

static int read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVCodecParameters *par = s->streams[0]->codecpar;
    MCADemuxContext *m     = s->priv_data;
    uint16_t size          = m->block_size;
    uint32_t samples       = m->samples_per_block;
    int ret = 0;

    if (avio_feof(s->pb))
        return AVERROR_EOF;
    m->current_block++;
    if (m->current_block > m->block_count)
        return AVERROR_EOF;

    if ((ret = av_get_packet(s->pb, pkt, size * par->ch_layout.nb_channels)) < 0)
        return ret;
    pkt->duration = samples;
    pkt->stream_index = 0;

    return 0;
}

static int read_seek(AVFormatContext *s, int stream_index,
                     int64_t timestamp, int flags)
{
    AVStream *st = s->streams[stream_index];
    MCADemuxContext *m = s->priv_data;
    int64_t ret = 0;

    if (timestamp < 0)
        timestamp = 0;
    timestamp /= m->samples_per_block;
    if (timestamp >= m->block_count)
        timestamp = m->block_count - 1;
    ret = avio_seek(s->pb, m->data_start + timestamp * m->block_size *
                    st->codecpar->ch_layout.nb_channels, SEEK_SET);
    if (ret < 0)
        return ret;

    m->current_block = timestamp;
    avpriv_update_cur_dts(s, st, timestamp * m->samples_per_block);
    return 0;
}

const FFInputFormat ff_mca_demuxer = {
    .p.name         = "mca",
    .p.long_name    = NULL_IF_CONFIG_SMALL("MCA Audio Format"),
    .p.extensions   = "mca",
    .priv_data_size = sizeof(MCADemuxContext),
    .read_probe     = probe,
    .read_header    = read_header,
    .read_packet    = read_packet,
    .read_seek      = read_seek,
};
