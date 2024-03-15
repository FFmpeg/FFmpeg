/*
 * Square Enix SCD demuxer
 * Copyright (C) 2021 Zane van Iperen (zane@zanevaniperen.com)
 *
 * Based off documentation:
 *   http://ffxivexplorer.fragmenterworks.com/research/scd%20files.txt
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
#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/internal.h"
#include "libavutil/macros.h"
#include "libavutil/avassert.h"
#include "libavformat/internal.h"
#include "avformat.h"
#include "demux.h"

#define SCD_MAGIC              ((uint64_t)MKBETAG('S', 'E', 'D', 'B') << 32 | \
                                          MKBETAG('S', 'S', 'C', 'F'))
#define SCD_MIN_HEADER_SIZE    20
#define SCD_OFFSET_HEADER_SIZE 28
#define SCD_TRACK_HEADER_SIZE  32

#define SCD_TRACK_ID_PCM        0
#define SCD_TRACK_ID_OGG        6
#define SCD_TRACK_ID_MP3        7
#define SCD_TRACK_ID_MS_ADPCM  12

typedef struct SCDOffsetTable {
    uint16_t  count;
    uint32_t  offset;
    uint32_t *entries;
} SCDOffsetTable;

typedef struct SCDHeader {
    uint64_t magic;         /* SEDBSSCF                                     */
    uint32_t version;       /* Verison number. We only know about 3.        */
    uint16_t unk1;          /* Unknown, 260 in Drakengard 3, 1024 in FFXIV. */
    uint16_t header_size;   /* Total size of this header.                   */
    uint32_t file_size;     /* Is often 0, just ignore it.                  */

    SCDOffsetTable table0;  /* Table 0, no idea. 56 uint32's/entry.         */
    SCDOffsetTable table1;  /* Table 1, contains the track info.            */
    SCDOffsetTable table2;  /* Table 2, no idea. 40 uint32's/entry.         */
    uint16_t unk2;          /* Unknown, not a count.                        */
    uint32_t unk3;          /* Unknown, not an offset.                      */
    uint32_t unk4;          /* Unknown, offset to offset.                   */
} SCDHeader;

typedef struct SCDTrackHeader {
    uint32_t length;
    uint32_t num_channels;
    uint32_t sample_rate;
    uint32_t data_type;
    uint32_t loop_start;
    uint32_t loop_end;
    uint32_t data_offset; /* Offset to data + this header. */
    uint32_t aux_count;

    uint32_t absolute_offset;
    uint32_t bytes_read;
} SCDTrackHeader;

typedef struct SCDDemuxContext {
    SCDHeader        hdr;
    SCDTrackHeader  *tracks;
    int              current_track;
} SCDDemuxContext;

static int scd_probe(const AVProbeData *p)
{
    if (AV_RB64(p->buf) != SCD_MAGIC)
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int scd_read_table(AVFormatContext *s, SCDOffsetTable *table)
{
    int64_t ret;

    if ((ret = avio_seek(s->pb, table->offset, SEEK_SET)) < 0)
        return ret;

    if ((table->entries = av_calloc(table->count, sizeof(uint32_t))) == NULL)
        return ret;

    if ((ret = avio_read(s->pb, (unsigned char*)table->entries, table->count * sizeof(uint32_t))) < 0)
        return ret;

    for (size_t i = 0; i < table->count; i++)
        table->entries[i] = AV_RB32(table->entries + i);

    av_log(s, AV_LOG_TRACE, "Table, size = %u, offset = %u\n", table->count, table->offset);
    for (size_t i = 0; i < table->count; i++)
        av_log(s, AV_LOG_TRACE, "  [%02zu]: %u\n", i, table->entries[i]);

    return 0;
}

static int scd_read_offsets(AVFormatContext *s)
{
    int64_t ret;
    SCDDemuxContext  *ctx = s->priv_data;
    uint8_t buf[SCD_OFFSET_HEADER_SIZE];

    if ((ret = avio_read(s->pb, buf, SCD_OFFSET_HEADER_SIZE)) < 0)
        return ret;

    ctx->hdr.table0.count  = AV_RB16(buf +  0);
    ctx->hdr.table1.count  = AV_RB16(buf +  2);
    ctx->hdr.table2.count  = AV_RB16(buf +  4);
    ctx->hdr.unk2          = AV_RB16(buf +  6);
    ctx->hdr.table0.offset = AV_RB32(buf +  8);
    ctx->hdr.table1.offset = AV_RB32(buf + 12);
    ctx->hdr.table2.offset = AV_RB32(buf + 16);
    ctx->hdr.unk3          = AV_RB32(buf + 20);
    ctx->hdr.unk4          = AV_RB32(buf + 24);

    if ((ret = scd_read_table(s, &ctx->hdr.table0)) < 0)
        return ret;

    if ((ret = scd_read_table(s, &ctx->hdr.table1)) < 0)
        return ret;

    if ((ret = scd_read_table(s, &ctx->hdr.table2)) < 0)
        return ret;

    return 0;
}

static int scd_read_track(AVFormatContext *s, SCDTrackHeader *track, int index)
{
    int64_t ret;
    uint32_t hoffset;
    AVStream *st;
    AVCodecParameters *par;
    SCDDemuxContext *ctx = s->priv_data;
    uint8_t buf[SCD_TRACK_HEADER_SIZE];

    /* Mark as experimental until I find more files from more than just one game. */
    if (s->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL) {
        av_log(s, AV_LOG_ERROR, "SCD demuxing is experimental, "
               "add '-strict %d' if you want to use it.\n",
               FF_COMPLIANCE_EXPERIMENTAL);
        return AVERROR_EXPERIMENTAL;
    }

    hoffset = ctx->hdr.table1.entries[index];

    if ((ret = avio_seek(s->pb, hoffset, SEEK_SET)) < 0)
        return ret;

    if ((ret = avio_read(s->pb, buf, SCD_TRACK_HEADER_SIZE)) < 0)
        return ret;

    track->length       = AV_RB32(buf +  0);
    track->num_channels = AV_RB32(buf +  4);
    track->sample_rate  = AV_RB32(buf +  8);
    track->data_type    = AV_RB32(buf + 12);
    track->loop_start   = AV_RB32(buf + 16);
    track->loop_end     = AV_RB32(buf + 20);
    track->data_offset  = AV_RB32(buf + 24);
    track->aux_count    = AV_RB32(buf + 28);

    /* Sanity checks */
    if (track->num_channels > 8 || track->sample_rate >= 192000 ||
        track->loop_start > track->loop_end)
        return AVERROR_INVALIDDATA;

    track->absolute_offset = hoffset + SCD_TRACK_HEADER_SIZE + track->data_offset;
    track->bytes_read      = 0;

    /* Not sure what to do with these, it seems to be fine to ignore them. */
    if (track->aux_count != 0)
        av_log(s, AV_LOG_DEBUG, "[%d] Track has %u auxiliary chunk(s).\n", index, track->aux_count);

    if ((st = avformat_new_stream(s, NULL)) == NULL)
        return AVERROR(ENOMEM);

    par               = st->codecpar;
    par->codec_type   = AVMEDIA_TYPE_AUDIO;
    par->ch_layout.nb_channels = (int)track->num_channels;
    par->sample_rate  = (int)track->sample_rate;
    st->index         = index;
    st->start_time    = 0;

    /* TODO: Check this with other types. Drakengard 3 MP3s have 47999 instead of 48000. */
    if (track->data_type == SCD_TRACK_ID_MP3)
        par->sample_rate += 1;

    avpriv_set_pts_info(st, 64, 1, par->sample_rate);

    if (av_dict_set_int(&st->metadata, "start", track->absolute_offset, 0) < 0)
        return AVERROR(ENOMEM);

    if (av_dict_set_int(&st->metadata, "loop_start", track->loop_start, 0) < 0)
        return AVERROR(ENOMEM);

    if (av_dict_set_int(&st->metadata, "loop_end", track->loop_end, 0) < 0)
        return AVERROR(ENOMEM);

    switch(track->data_type) {
        case SCD_TRACK_ID_PCM:
            par->codec_id              = AV_CODEC_ID_PCM_S16BE;
            par->bits_per_coded_sample = 16;
            par->block_align           = par->bits_per_coded_sample * par->ch_layout.nb_channels / 8;
            break;
        case SCD_TRACK_ID_MP3:
            par->codec_id              = AV_CODEC_ID_MP3;
            ffstream(st)->need_parsing = AVSTREAM_PARSE_FULL_RAW;
            break;
        case SCD_TRACK_ID_OGG:
        case SCD_TRACK_ID_MS_ADPCM:
        default:
            par->codec_id              = AV_CODEC_ID_NONE;
            avpriv_request_sample(s, "data type %u", track->data_type);
    }

    return 0;
}

static int scd_read_header(AVFormatContext *s)
{
    int64_t ret;
    SCDDemuxContext *ctx = s->priv_data;
    uint8_t buf[SCD_MIN_HEADER_SIZE];

    if ((ret = avio_read(s->pb, buf, SCD_MIN_HEADER_SIZE)) < 0)
        return ret;

    ctx->hdr.magic       = AV_RB64(buf +  0);
    ctx->hdr.version     = AV_RB32(buf +  8);
    ctx->hdr.unk1        = AV_RB16(buf + 12);
    ctx->hdr.header_size = AV_RB16(buf + 14);
    ctx->hdr.file_size   = AV_RB32(buf + 16);

    if (ctx->hdr.magic != SCD_MAGIC)
        return AVERROR_INVALIDDATA;

    if (ctx->hdr.version != 3) {
        avpriv_request_sample(s, "SCD version %u", ctx->hdr.version);
        return AVERROR_PATCHWELCOME;
    }

    if (ctx->hdr.header_size < SCD_MIN_HEADER_SIZE)
        return AVERROR_INVALIDDATA;

    if ((ret = avio_skip(s->pb, ctx->hdr.header_size - SCD_MIN_HEADER_SIZE)) < 0)
        return ret;

    if ((ret = scd_read_offsets(s)) < 0)
        return ret;

    ctx->tracks = av_calloc(ctx->hdr.table1.count, sizeof(SCDTrackHeader));
    if (ctx->tracks == NULL)
        return AVERROR(ENOMEM);

    for (int i = 0; i < ctx->hdr.table1.count; i++) {
        if ((ret = scd_read_track(s, ctx->tracks + i, i)) < 0)
            return ret;
    }

    if (ctx->hdr.table1.count == 0)
        return 0;

    if ((ret = avio_seek(s->pb, ctx->tracks[0].absolute_offset, SEEK_SET)) < 0)
        return ret;

    return 0;
}

static int scd_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    SCDDemuxContext   *ctx = s->priv_data;
    AVCodecParameters *par;

    /* Streams aren't interleaved, round-robin them. */
    for (int i = 0; i < ctx->hdr.table1.count; i++) {
        int64_t ret;
        int size;
        SCDTrackHeader *trk;

        ctx->current_track %= ctx->hdr.table1.count;

        trk = ctx->tracks + ctx->current_track;
        par = s->streams[ctx->current_track]->codecpar;

        if (trk->bytes_read >= trk->length)
            continue;

        if ((ret = avio_seek(s->pb, trk->absolute_offset + trk->bytes_read, SEEK_SET)) < 0)
            return ret;

        switch(trk->data_type) {
            case SCD_TRACK_ID_PCM:
                size = par->block_align;
                break;
            case SCD_TRACK_ID_MP3:
            default:
                size = FFMIN(trk->length - trk->bytes_read, 4096);
                break;
        }

        ret = av_get_packet(s->pb, pkt, size);
        if (ret == AVERROR_EOF) {
            trk->length = trk->bytes_read;
            continue;
        } else if (ret < 0) {
            return ret;
        }

        if (trk->data_type == SCD_TRACK_ID_PCM) {
            pkt->pts      = trk->bytes_read / (par->ch_layout.nb_channels * sizeof(uint16_t));
            pkt->duration = size / (par->ch_layout.nb_channels * sizeof(int16_t));
        }

        trk->bytes_read   += ret;
        pkt->flags        &= ~AV_PKT_FLAG_CORRUPT;
        pkt->stream_index  = ctx->current_track;

        ctx->current_track++;
        return 0;
    }

    return AVERROR_EOF;
}

static int scd_seek(AVFormatContext *s, int stream_index,
                    int64_t pts, int flags)
{
    SCDDemuxContext *ctx = s->priv_data;

    if (pts != 0)
        return AVERROR(EINVAL);

    for(int i = 0; i < ctx->hdr.table1.count; ++i)
        ctx->tracks[i].bytes_read = 0;

    return 0;
}

static int scd_read_close(AVFormatContext *s)
{
    SCDDemuxContext *ctx = s->priv_data;

    av_freep(&ctx->hdr.table0.entries);
    av_freep(&ctx->hdr.table1.entries);
    av_freep(&ctx->hdr.table2.entries);
    av_freep(&ctx->tracks);
    return 0;
}

const FFInputFormat ff_scd_demuxer = {
    .p.name         = "scd",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Square Enix SCD"),
    .priv_data_size = sizeof(SCDDemuxContext),
    .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
    .read_probe     = scd_probe,
    .read_header    = scd_read_header,
    .read_packet    = scd_read_packet,
    .read_seek      = scd_seek,
    .read_close     = scd_read_close,
};
